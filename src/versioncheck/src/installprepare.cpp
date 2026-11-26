#include "installprepare.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <thread>

#include "updatefiles.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Both need <windows.h> first.
#include <restartmanager.h>
#include <tlhelp32.h>
#endif

namespace fs = std::filesystem;

namespace cilogg::install_prepare {
namespace {

using update_files::RenameResult;

// Moved-aside copies of one file that may coexist: each update while an old instance still runs
// adds one, and the next update deletes those no process uses any more.
constexpr int MaxMovedAsideCopies = 99;
// Security software can hold a freshly written file for a moment: files that could not be moved
// are tried again after a pause, all together.
constexpr int MoveRetryRounds = 3;
constexpr auto MoveRetryDelay = std::chrono::milliseconds( 500 );
// Appended to the manifest before the old copies are deleted: an update interrupted after it is
// completed by the next one, an update interrupted before it is undone.
constexpr char FinishingMarker[] = "#finishing";

// The folders cilogg.nsi installs below the installation folder; its uninstaller removes each as a
// whole. Files in any other folder belong to someone else and are never touched.
constexpr const char* OwnedFolders[] = { "generic",      "iconengines",    "imageformats",
                                         "networkinformation", "platforms", "plugin-sdk",
                                         "plugins",      "python_runtime", "sqldrivers",
                                         "styles",       "tls",            "translations" };
// The documents cilogg.nsi writes next to cilogg.exe (lower case).
constexpr const char* OwnedDocuments[] = { "copying",       "documentation.html", "documentation.md",
                                           "mcp.md",        "notice",             "plugin-sdk.md",
                                           "readme.md" };

constexpr char OutsideError[]
    = "a folder on the way is a link or a junction, or the path is outside the installation folder";

std::string lowerAscii( std::string text )
{
    std::transform( text.begin(), text.end(), text.begin(), []( char character ) {
        return static_cast<char>( std::tolower( static_cast<unsigned char>( character ) ) );
    } );
    return text;
}

std::string toUtf8( const fs::path& path )
{
#ifdef __cpp_char8_t
    const auto text = path.u8string();
    return std::string( text.begin(), text.end() );
#else
    return path.u8string();
#endif
}

fs::path fromUtf8( const std::string& text )
{
#ifdef __cpp_char8_t
    return fs::path( std::u8string( text.begin(), text.end() ) );
#else
    return fs::u8path( text );
#endif
}

// Tabs and line breaks separate the fields and lines of the report and the manifest.
std::string withoutSeparators( std::string text )
{
    std::replace_if(
        text.begin(), text.end(),
        []( char character ) {
            return character == '\t' || character == '\r' || character == '\n';
        },
        ' ' );
    return text;
}

std::string describeForLog( const ProcessInfo& process )
{
    std::string text = ( process.name.empty() ? std::string( "unknown process" ) : process.name )
                       + " (PID " + std::to_string( process.pid );
    if ( !process.image.empty() )
        text += ", " + toUtf8( process.image );
    text += ")";
    if ( !process.parentName.empty() )
        text += ", started by " + process.parentName + " (PID "
                + std::to_string( process.parentPid ) + ")";
    return text;
}

#ifdef _WIN32
std::string systemErrorText( DWORD code )
{
    return std::system_category().message( static_cast<int>( code ) ) + " ("
           + std::to_string( code ) + ")";
}

std::string fromWide( const std::wstring& text )
{
    return toUtf8( fs::path( text ) );
}

// The full, long-form path without a trailing separator, for comparisons.
std::wstring normalized( const fs::path& path )
{
    std::wstring full( MAX_PATH, L'\0' );
    auto length
        = GetFullPathNameW( path.c_str(), static_cast<DWORD>( full.size() ), full.data(), nullptr );
    if ( length >= full.size() ) {
        full.resize( length + 1 );
        length = GetFullPathNameW( path.c_str(), static_cast<DWORD>( full.size() ), full.data(),
                                   nullptr );
    }
    if ( length == 0 || length >= full.size() )
        full = path.wstring();
    else
        full.resize( length );

    // Short (8.3) names expand only for existing paths: expand the folder of a file that was
    // moved away.
    const auto longPath = []( const std::wstring& candidate ) {
        std::wstring longForm( candidate.size() + MAX_PATH, L'\0' );
        const auto longLength = GetLongPathNameW( candidate.c_str(), longForm.data(),
                                                  static_cast<DWORD>( longForm.size() ) );
        if ( longLength == 0 || longLength >= longForm.size() )
            return std::wstring{};
        longForm.resize( longLength );
        return longForm;
    };
    if ( auto expanded = longPath( full ); !expanded.empty() ) {
        full = std::move( expanded );
    }
    else {
        const auto separator = full.find_last_of( L"\\/" );
        if ( separator != std::wstring::npos && separator > 2 ) {
            if ( auto folder = longPath( full.substr( 0, separator ) ); !folder.empty() )
                full = folder + full.substr( separator );
        }
    }
    while ( full.size() > 3 && ( full.back() == L'\\' || full.back() == L'/' ) )
        full.pop_back();
    return full;
}

bool liesInside( const std::wstring& path, const std::wstring& directory )
{
    if ( directory.empty() || path.size() <= directory.size() + 1 )
        return false;
    if ( directory.back() != L'\\' && path[ directory.size() ] != L'\\' )
        return false;
    return CompareStringOrdinal( path.c_str(), static_cast<int>( directory.size() ),
                                 directory.c_str(), static_cast<int>( directory.size() ), TRUE )
           == CSTR_EQUAL;
}

bool sameText( const std::wstring& left, const std::wstring& right )
{
    return CompareStringOrdinal( left.c_str(), static_cast<int>( left.size() ), right.c_str(),
                                 static_cast<int>( right.size() ), TRUE )
           == CSTR_EQUAL;
}

struct ProcessEntry {
    DWORD parentPid = 0;
    std::wstring exeName;
};

std::map<DWORD, ProcessEntry> snapshotProcesses()
{
    std::map<DWORD, ProcessEntry> processes;
    const auto snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
    if ( snapshot == INVALID_HANDLE_VALUE )
        return processes;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof( entry );
    for ( auto more = Process32FirstW( snapshot, &entry ); more;
          more = Process32NextW( snapshot, &entry ) ) {
        processes[ entry.th32ProcessID ] = { entry.th32ParentProcessID, entry.szExeFile };
    }
    CloseHandle( snapshot );
    return processes;
}

std::wstring imagePathOf( DWORD pid )
{
    const auto process = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid );
    if ( !process )
        return {};
    std::wstring buffer( 32768, L'\0' );
    auto size = static_cast<DWORD>( buffer.size() );
    const bool queried = QueryFullProcessImageNameW( process, 0, buffer.data(), &size ) != FALSE;
    CloseHandle( process );
    if ( !queried )
        return {};
    buffer.resize( size );
    return buffer;
}

// 0 when the process cannot be queried.
ULONGLONG creationTimeOf( DWORD pid )
{
    const auto process = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid );
    if ( !process )
        return 0;
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    const bool queried = GetProcessTimes( process, &created, &exited, &kernel, &user ) != FALSE;
    CloseHandle( process );
    if ( !queried )
        return 0;
    return ( static_cast<ULONGLONG>( created.dwHighDateTime ) << 32 ) | created.dwLowDateTime;
}

ProcessInfo queryProcess( DWORD pid, const std::map<DWORD, ProcessEntry>& processes )
{
    ProcessInfo info;
    info.pid = pid;
    info.image = fs::path( imagePathOf( pid ) );
    const auto entry = processes.find( pid );
    if ( entry != processes.end() ) {
        info.name = fromWide( entry->second.exeName );
        info.parentPid = entry->second.parentPid;
        const auto parent = processes.find( entry->second.parentPid );
        if ( parent != processes.end() && parent->first != 0 ) {
            // A "parent" created after its child is an unrelated process that reused the ID.
            const auto parentCreated = creationTimeOf( parent->first );
            const auto created = creationTimeOf( pid );
            if ( parentCreated == 0 || created == 0 || parentCreated <= created )
                info.parentName = fromWide( parent->second.exeName );
        }
    }
    else if ( !info.image.empty() ) {
        info.name = toUtf8( info.image.filename() );
    }
    return info;
}

// The files and folders directly in `directory`. Reparse points (links, junctions) are left out:
// they are neither followed nor touched, so the elevated installer stays in the folder.
struct Listing {
    std::vector<fs::path> files;
    std::vector<fs::path> folders;
};

Listing listDirectory( const fs::path& directory )
{
    Listing listing;
    WIN32_FIND_DATAW data{};
    const auto pattern = directory / L"*";
    const auto find = FindFirstFileExW( pattern.c_str(), FindExInfoBasic, &data,
                                        FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH );
    if ( find == INVALID_HANDLE_VALUE )
        return listing;
    do {
        const std::wstring name = data.cFileName;
        if ( name == L"." || name == L".."
             || ( data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) )
            continue;
        if ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
            listing.folders.push_back( directory / name );
        else
            listing.files.push_back( directory / name );
    } while ( FindNextFileW( find, &data ) );
    FindClose( find );
    return listing;
}

// A folder that cannot be inspected counts as a link: nothing is done through it.
bool isLinkOrJunction( const fs::path& path )
{
    const auto attributes = GetFileAttributesW( path.c_str() );
    return attributes == INVALID_FILE_ATTRIBUTES
           || ( attributes & FILE_ATTRIBUTE_REPARSE_POINT ) != 0;
}

// True when `file` is gone.
bool removeFile( const fs::path& file, std::string* error )
{
    if ( DeleteFileW( file.c_str() ) )
        return true;
    const auto code = GetLastError();
    if ( code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND )
        return true;
    if ( error )
        *error = systemErrorText( code );
    return false;
}

#else
struct Listing {
    std::vector<fs::path> files;
    std::vector<fs::path> folders;
};

Listing listDirectory( const fs::path& directory )
{
    Listing listing;
    std::error_code error;
    for ( fs::directory_iterator it( directory, error ), end; !error && it != end;
          it.increment( error ) ) {
        std::error_code status;
        if ( it->is_symlink( status ) )
            continue;
        if ( it->is_directory( status ) )
            listing.folders.push_back( it->path() );
        else if ( it->is_regular_file( status ) )
            listing.files.push_back( it->path() );
    }
    return listing;
}

bool isLinkOrJunction( const fs::path& path )
{
    std::error_code error;
    const auto status = fs::symlink_status( path, error );
    return error || fs::is_symlink( status );
}

bool removeFile( const fs::path& file, std::string* error )
{
    std::error_code code;
    fs::remove( file, code );
    if ( code && error )
        *error = code.message();
    return !code;
}

#endif

void collectFiles( const fs::path& directory, std::vector<fs::path>& files )
{
    const auto listing = listDirectory( directory );
    files.insert( files.end(), listing.files.begin(), listing.files.end() );
    for ( const auto& folder : listing.folders )
        collectFiles( folder, files );
}

std::string lowerName( const fs::path& path )
{
    return lowerAscii( toUtf8( path.filename() ) );
}

std::string lowerExtension( const fs::path& path )
{
    return lowerAscii( toUtf8( path.extension() ) );
}

template <std::size_t Size>
bool listed( const char* const ( &names )[ Size ], const std::string& name )
{
    return std::any_of( std::begin( names ), std::end( names ),
                        [ &name ]( const char* entry ) { return name == entry; } );
}

bool isCiloggExecutable( const fs::path& file )
{
    return lowerName( file ).rfind( "cilogg", 0 ) == 0 && lowerExtension( file ) == ".exe";
}

// The top-level files cilogg.nsi installs: its executables, the uninstaller, the DLLs (its
// uninstaller deletes every DLL there too) and its documents.
bool isOwnedTopLevelFile( const fs::path& file )
{
    const auto extension = lowerExtension( file );
    if ( extension == ".dll" )
        return true;
    if ( extension == ".exe" )
        return isCiloggExecutable( file ) || lowerName( file ) == "uninstall.exe";
    return listed( OwnedDocuments, lowerName( file ) );
}

// The files of the CILogg installation in `directory`: the top-level files cilogg.nsi installs,
// the files earlier updates moved aside there, and everything in the folders it installs.
std::vector<fs::path> installationFiles( const fs::path& directory )
{
    const auto top = listDirectory( directory );
    std::vector<fs::path> files;
    for ( const auto& file : top.files ) {
        if ( isOwnedTopLevelFile( file ) || isMovedAside( file ) )
            files.push_back( file );
    }
    for ( const auto& folder : top.folders ) {
        if ( listed( OwnedFolders, lowerName( folder ) ) )
            collectFiles( folder, files );
    }
    return files;
}

// Whether `directory` holds a CILogg installation, or what an interrupted update of one left. A
// first installation into a folder holding anything else moves nothing there.
bool holdsInstallation( const fs::path& directory )
{
    const auto manifest = lowerAscii( ManifestFileName );
    const auto files = listDirectory( directory ).files;
    return std::any_of( files.begin(), files.end(), [ &manifest ]( const fs::path& file ) {
        return isCiloggExecutable( file ) || isMovedAside( file ) || lowerName( file ) == manifest;
    } );
}

bool liesInsideDirectory( const fs::path& path, const fs::path& directory )
{
#ifdef _WIN32
    return liesInside( normalized( path ), normalized( directory ) );
#else
    const auto file = path.lexically_normal();
    const auto root = directory.lexically_normal();
    const auto relative = file.lexically_relative( root );
    return !relative.empty() && *relative.begin() != ".." && relative != ".";
#endif
}

// True when `path` lies below `directory` and no folder between them is a link or a junction, so
// renaming or deleting `path` stays inside the installation folder. Checked right before acting:
// a folder could have been replaced since the files were listed.
bool staysInside( const fs::path& path, const fs::path& directory )
{
    if ( !liesInsideDirectory( path, directory ) )
        return false;
    for ( auto folder = path.parent_path(); liesInsideDirectory( folder, directory );
          folder = folder.parent_path() ) {
        if ( isLinkOrJunction( folder ) )
            return false;
        if ( folder == folder.parent_path() )
            break;
    }
    return true;
}

RenameResult renameInside( const fs::path& from, const fs::path& to, const fs::path& directory,
                           std::string* error )
{
    if ( !staysInside( from, directory ) || !staysInside( to, directory ) ) {
        if ( error )
            *error = OutsideError;
        return RenameResult::Failed;
    }
    return update_files::renameNoReplace( from, to, error );
}

// Deletes `file` unless a process still uses it; the next update deletes it then. (No deletion at
// restart is scheduled: moved-aside names are reused, and a later update could leave its only
// copy of a file under the same name.) True when it is gone now.
bool deleteOrSchedule( const fs::path& file, const fs::path& directory, const Log& log )
{
    if ( !staysInside( file, directory ) ) {
        log( "Not deleted (" + std::string( OutsideError ) + "): " + toUtf8( file ) );
        return false;
    }
    std::string error;
    if ( removeFile( file, &error ) )
        return true;
    log( "Still in use, deleted by the next update: " + toUtf8( file ) + " (" + error + ")" );
    return false;
}

// Renames `original` to the first free "<name>[.<n>].cilogg-old" next to it.
RenameResult moveToFreeName( const fs::path& original, const fs::path& directory, fs::path* movedTo,
                             std::string* error )
{
    for ( int copy = 1; copy <= MaxMovedAsideCopies; ++copy ) {
        auto name = original.filename();
        if ( copy > 1 )
            name += "." + std::to_string( copy );
        name += MovedAsideSuffix;
        const auto target = original.parent_path() / name;
        const auto result = renameInside( original, target, directory, error );
        if ( result == RenameResult::DestinationExists )
            continue;
        if ( result == RenameResult::Renamed )
            *movedTo = target;
        return result;
    }
    if ( error )
        *error = "every moved-aside name is still in use";
    return RenameResult::DestinationExists;
}

std::vector<MovedFile> readManifest( const fs::path& directory, const fs::path& manifestPath,
                                     const Log& log )
{
    std::ifstream file( manifestPath, std::ios::binary );
    const std::string text( ( std::istreambuf_iterator<char>( file ) ),
                            std::istreambuf_iterator<char>() );
    std::vector<MovedFile> trusted;
    for ( const auto& moved : parseManifest( text ) ) {
        if ( !isMovedAsideNameOf( moved.original, moved.movedTo )
             || !liesInsideDirectory( moved.original, directory ) ) {
            log( "Ignoring a manifest entry outside the installation folder: "
                 + toUtf8( moved.original ) );
            continue;
        }
        trusted.push_back( moved );
    }
    return trusted;
}

bool isFinishing( const fs::path& manifestPath )
{
    std::ifstream file( manifestPath, std::ios::binary );
    std::string line;
    while ( std::getline( file, line ) ) {
        if ( !line.empty() && line.back() == '\r' )
            line.pop_back();
        if ( line == FinishingMarker )
            return true;
    }
    return false;
}

void writeManifest( const fs::path& path, const std::vector<MovedFile>& entries )
{
    std::ofstream file( path, std::ios::binary | std::ios::trunc );
    for ( const auto& entry : entries )
        file << formatManifestLine( entry ) << "\n";
}

void writeReport( const fs::path& report, const std::vector<fs::path>& blocked,
                  const std::vector<ProcessInfo>& blockers, const Log& log )
{
    if ( report.empty() )
        return;
    std::ofstream file( report, std::ios::binary | std::ios::trunc );
    for ( const auto& path : blocked )
        file << "blocked-file=" << withoutSeparators( toUtf8( path ) ) << "\n";
    for ( const auto& process : blockers )
        file << "blocker=" << formatBlocker( process ) << "\n";
    if ( !file )
        log( "Cannot write the installer report " + toUtf8( report ) );
}
} // namespace

std::string formatBlocker( const ProcessInfo& process )
{
    return std::to_string( process.pid ) + "\t" + withoutSeparators( process.name ) + "\t"
           + withoutSeparators( toUtf8( process.image ) ) + "\t"
           + std::to_string( process.parentPid ) + "\t" + withoutSeparators( process.parentName );
}

std::string describeProcess( const ProcessInfo& process )
{
    return describeForLog( process );
}

std::string formatManifestLine( const MovedFile& moved )
{
    return withoutSeparators( toUtf8( moved.original ) ) + "\t"
           + withoutSeparators( toUtf8( moved.movedTo ) );
}

std::vector<MovedFile> parseManifest( const std::string& text )
{
    std::vector<MovedFile> entries;
    std::istringstream lines( text );
    std::string line;
    while ( std::getline( lines, line ) ) {
        if ( !line.empty() && line.back() == '\r' )
            line.pop_back();
        const auto separator = line.find( '\t' );
        if ( separator == std::string::npos
             || line.find( '\t', separator + 1 ) != std::string::npos )
            continue;
        MovedFile moved{ fromUtf8( line.substr( 0, separator ) ),
                         fromUtf8( line.substr( separator + 1 ) ) };
        if ( moved.original.is_absolute() && moved.movedTo.is_absolute() )
            entries.push_back( std::move( moved ) );
    }
    return entries;
}

bool isMovedAside( const fs::path& path )
{
    const auto name = lowerAscii( toUtf8( path.filename() ) );
    const std::string suffix = MovedAsideSuffix;
    return name.size() > suffix.size()
           && name.compare( name.size() - suffix.size(), suffix.size(), suffix ) == 0;
}

bool isExecutableImage( const fs::path& path )
{
    const auto extension = lowerAscii( toUtf8( path.extension() ) );
    return extension == ".exe" || extension == ".dll" || extension == ".pyd";
}

bool isMovedAsideNameOf( const fs::path& original, const fs::path& candidate )
{
    if ( original.filename().empty() || !isMovedAside( candidate ) )
        return false;
#ifdef _WIN32
    if ( !sameText( original.parent_path().lexically_normal().wstring(),
                    candidate.parent_path().lexically_normal().wstring() ) )
        return false;
#else
    if ( original.parent_path().lexically_normal() != candidate.parent_path().lexically_normal() )
        return false;
#endif
    const auto name = lowerAscii( toUtf8( original.filename() ) );
    const auto candidateName = lowerAscii( toUtf8( candidate.filename() ) );
    const auto suffixLength = std::string( MovedAsideSuffix ).size();
    if ( candidateName.size() < name.size() + suffixLength
         || candidateName.compare( 0, name.size(), name ) != 0 )
        return false;
    // Empty for the first copy, ".<n>" for the others.
    const auto counter
        = candidateName.substr( name.size(), candidateName.size() - name.size() - suffixLength );
    return counter.empty()
           || ( counter.size() > 1 && counter.front() == '.'
                && std::all_of( counter.begin() + 1, counter.end(), []( char character ) {
                       return std::isdigit( static_cast<unsigned char>( character ) ) != 0;
                   } ) );
}

std::vector<ProcessInfo> processesRunningFrom( const fs::path& directory )
{
    std::vector<ProcessInfo> running;
#ifdef _WIN32
    const auto root = normalized( directory );
    const auto processes = snapshotProcesses();
    for ( const auto& entry : processes ) {
        if ( entry.first == 0 )
            continue;
        const auto image = imagePathOf( entry.first );
        if ( image.empty() || !liesInside( normalized( fs::path( image ) ), root ) )
            continue;
        running.push_back( queryProcess( entry.first, processes ) );
    }
#else
    (void)directory;
#endif
    return running;
}

std::vector<ProcessInfo> processesUsing( const std::vector<fs::path>& files, std::string* error )
{
    std::vector<ProcessInfo> users;
#ifdef _WIN32
    if ( files.empty() )
        return users;
    DWORD session = 0;
    WCHAR key[ CCH_RM_SESSION_KEY + 1 ] = {};
    auto status = RmStartSession( &session, 0, key );
    if ( status != ERROR_SUCCESS ) {
        if ( error )
            *error = "Restart Manager session failed: " + systemErrorText( status );
        return users;
    }
    std::vector<std::wstring> names;
    for ( const auto& file : files )
        names.push_back( file.wstring() );
    std::vector<LPCWSTR> pointers;
    for ( const auto& name : names )
        pointers.push_back( name.c_str() );
    status = RmRegisterResources( session, static_cast<UINT>( pointers.size() ), pointers.data(), 0,
                                  nullptr, 0, nullptr );
    std::vector<RM_PROCESS_INFO> infos;
    for ( int attempt = 0; status == ERROR_SUCCESS && attempt < 5; ++attempt ) {
        UINT needed = 0;
        auto count = static_cast<UINT>( infos.size() );
        DWORD reasons = 0;
        status = RmGetList( session, &needed, &count, infos.empty() ? nullptr : infos.data(),
                            &reasons );
        if ( status == ERROR_MORE_DATA ) {
            infos.resize( needed );
            status = ERROR_SUCCESS;
            continue;
        }
        if ( status == ERROR_SUCCESS )
            infos.resize( count );
        break;
    }
    RmEndSession( session );
    if ( status != ERROR_SUCCESS ) {
        if ( error )
            *error = "Restart Manager query failed: " + systemErrorText( status );
        return users;
    }
    const auto processes = snapshotProcesses();
    std::set<DWORD> seen;
    for ( const auto& info : infos ) {
        const auto pid = info.Process.dwProcessId;
        if ( !seen.insert( pid ).second )
            continue;
        auto process = queryProcess( pid, processes );
        if ( process.name.empty() )
            process.name = fromWide( info.strAppName );
        users.push_back( std::move( process ) );
    }
#else
    (void)files;
    if ( error )
        *error = "not supported on this system";
#endif
    return users;
}

bool isInUse( const fs::path& file )
{
#ifdef _WIN32
    // The way the installer opens a file it replaces: for writing, sharing only reading.
    const auto handle = CreateFileW( file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( handle == INVALID_HANDLE_VALUE ) {
        const auto error = GetLastError();
        return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
    }
    CloseHandle( handle );
    return false;
#else
    (void)file;
    return false;
#endif
}

std::size_t removeMovedAside( const fs::path& directory, const Log& log )
{
    std::size_t removed = 0;
    std::size_t restored = 0;
    std::size_t remaining = 0;
    const auto suffixLength = std::strlen( MovedAsideSuffix );
    for ( const auto& file : installationFiles( directory ) ) {
        if ( !isMovedAside( file ) )
            continue;
        const auto fileName = toUtf8( file.filename() );
        const auto stem = fileName.substr( 0, fileName.size() - suffixLength );
        const auto dot = stem.find_last_of( '.' );
        const bool counted = dot != std::string::npos && dot + 1 < stem.size()
                             && std::all_of( stem.begin() + static_cast<std::ptrdiff_t>( dot + 1 ),
                                             stem.end(), []( char character ) {
                                                 return std::isdigit(
                                                            static_cast<unsigned char>( character ) )
                                                        != 0;
                                             } );
        const auto original = file.parent_path() / fromUtf8( stem );
        std::error_code error;
        const bool replaced
            = fs::exists( original, error )
              || ( counted
                   && fs::exists( file.parent_path() / fromUtf8( stem.substr( 0, dot ) ), error ) );
        if ( !replaced ) {
            // The only copy of a file an update could not put back: never delete it.
            std::string renameError;
            if ( !counted
                 && renameInside( file, original, directory, &renameError )
                        == RenameResult::Renamed ) {
                ++restored;
                log( "Put back " + toUtf8( original ) );
            }
            else {
                ++remaining;
                log( "Kept " + toUtf8( file ) + ": its original is missing"
                     + ( renameError.empty() ? std::string{} : " (" + renameError + ")" ) );
            }
            continue;
        }
        std::string deleteError;
        if ( staysInside( file, directory ) && removeFile( file, &deleteError ) )
            ++removed;
        else
            ++remaining;
    }
    if ( removed > 0 || restored > 0 || remaining > 0 )
        log( "Files moved aside by earlier updates: " + std::to_string( removed ) + " deleted, "
             + std::to_string( restored ) + " put back, " + std::to_string( remaining )
             + " kept" );
    return remaining;
}

PrepareResult moveAside( const fs::path& directory, const Log& log,
                         const std::function<void( const MovedFile& )>& onMoved )
{
    PrepareResult result;
    if ( !holdsInstallation( directory ) ) {
        log( "No CILogg installation in " + toUtf8( directory ) + "; nothing is moved aside" );
        return result;
    }
    std::vector<fs::path> executables;
    std::vector<fs::path> libraries;
    std::vector<fs::path> others;
    for ( const auto& file : installationFiles( directory ) ) {
        if ( isMovedAside( file ) )
            continue;
        if ( lowerExtension( file ) == ".exe" )
            executables.push_back( file );
        else if ( isExecutableImage( file ) )
            libraries.push_back( file );
        else if ( isInUse( file ) )
            others.push_back( file );
    }
    std::vector<fs::path> ordered = std::move( executables );
    ordered.insert( ordered.end(), libraries.begin(), libraries.end() );
    ordered.insert( ordered.end(), others.begin(), others.end() );

    std::vector<std::pair<fs::path, std::string>> failed;
    const auto moveAll = [ & ]( const std::vector<fs::path>& files ) {
        failed.clear();
        for ( const auto& file : files ) {
            fs::path movedTo;
            std::string error;
            const auto status = moveToFreeName( file, directory, &movedTo, &error );
            if ( status == RenameResult::Renamed ) {
                result.moved.push_back( { file, movedTo } );
                if ( onMoved )
                    onMoved( result.moved.back() );
            }
            else if ( status != RenameResult::SourceMissing ) {
                failed.emplace_back( file, error );
            }
        }
    };
    moveAll( ordered );
    for ( int round = 1; round <= MoveRetryRounds && !failed.empty(); ++round ) {
        std::vector<fs::path> again;
        for ( const auto& entry : failed ) {
            if ( entry.second != OutsideError )
                again.push_back( entry.first );
        }
        if ( again.empty() )
            break;
        std::this_thread::sleep_for( MoveRetryDelay );
        auto outside = failed;
        moveAll( again );
        for ( const auto& entry : outside ) {
            if ( entry.second == OutsideError )
                failed.push_back( entry );
        }
    }
    for ( const auto& [ file, error ] : failed ) {
        // A top-level DLL may belong to another program in the same folder: leave it where it is.
        // If it is one of CILogg's, writing the new one fails and the installation is undone.
        if ( file.parent_path() == directory && lowerExtension( file ) == ".dll"
             && error != OutsideError ) {
            log( "Left in place (" + error + "): " + toUtf8( file ) );
            continue;
        }
        log( "Cannot move aside " + toUtf8( file ) + ": " + error );
        result.blocked.push_back( file );
    }
    return result;
}

std::vector<MovedFile> finish( const fs::path& directory, const std::vector<MovedFile>& moved,
                               const Log& log )
{
    std::vector<MovedFile> notPutBack;
    std::size_t deleted = 0;
    std::size_t kept = 0;
    std::size_t restored = 0;
    for ( const auto& file : moved ) {
        std::error_code error;
        if ( !fs::exists( file.movedTo, error ) )
            continue; // handled by an earlier pass
        if ( fs::exists( file.original, error ) ) {
            if ( deleteOrSchedule( file.movedTo, directory, log ) )
                ++deleted;
            else
                ++kept;
            continue;
        }
        // The installation did not ship this file (an optional component, or one a newer
        // version no longer has): keep the previous one, as an in-place copy would have.
        std::string renameError;
        if ( renameInside( file.movedTo, file.original, directory, &renameError )
             == RenameResult::Renamed ) {
            ++restored;
            log( "Not replaced, kept the previous file: " + toUtf8( file.original ) );
        }
        else {
            log( "Cannot put back " + toUtf8( file.original ) + ": " + renameError );
            notPutBack.push_back( file );
        }
    }
    log( "Replaced files: " + std::to_string( deleted ) + " old copies deleted, "
         + std::to_string( kept ) + " still in use, " + std::to_string( restored )
         + " not replaced and kept, " + std::to_string( notPutBack.size() )
         + " not put back" );
    return notPutBack;
}

bool rollback( const fs::path& directory, const std::vector<MovedFile>& moved, const Log& log )
{
    bool complete = true;
    for ( auto file = moved.rbegin(); file != moved.rend(); ++file ) {
        std::error_code error;
        if ( !fs::exists( file->movedTo, error ) ) {
            // Restored by an earlier pass, unless the original is gone too.
            if ( !fs::exists( file->original, error ) ) {
                log( "Lost: " + toUtf8( file->original ) );
                complete = false;
            }
            continue;
        }
        if ( fs::exists( file->original, error ) ) {
            // A new file was (partly) installed in its place: move it aside as well.
            fs::path newCopy;
            std::string moveError;
            if ( moveToFreeName( file->original, directory, &newCopy, &moveError )
                 != RenameResult::Renamed ) {
                log( "Cannot remove the new " + toUtf8( file->original ) + ": " + moveError );
                complete = false;
                continue;
            }
            deleteOrSchedule( newCopy, directory, log );
        }
        std::string renameError;
        if ( renameInside( file->movedTo, file->original, directory, &renameError )
             != RenameResult::Renamed ) {
            log( "Cannot restore " + toUtf8( file->original ) + ": " + renameError );
            complete = false;
        }
    }
    log( complete ? "Restored " + std::to_string( moved.size() ) + " files"
                  : std::string( "Some files could not be restored" ) );
    return complete;
}

int runPrepare( const fs::path& directory, const fs::path& report, const Log& log )
{
    std::error_code error;
    if ( !report.empty() )
        fs::remove( report, error );
    if ( !fs::is_directory( directory, error ) ) {
        log( "Installation folder " + toUtf8( directory ) + " does not exist yet" );
        return PreparedExitCode;
    }
    const auto manifestPath = directory / ManifestFileName;
    if ( fs::exists( manifestPath, error ) ) {
        // An earlier installation stopped (for example, it was killed). Stopped while copying,
        // it is undone: the previous version is put back. Stopped while deleting the old copies,
        // the new version is complete and the cleanup is finished.
        const auto earlier = readManifest( directory, manifestPath, log );
        if ( isFinishing( manifestPath ) ) {
            log( "Finishing an interrupted update of " + std::to_string( earlier.size() )
                 + " files" );
            const auto notPutBack = finish( directory, earlier, log );
            if ( !notPutBack.empty() ) {
                writeManifest( manifestPath, notPutBack );
                log( "Kept " + toUtf8( manifestPath ) + "; run setup again" );
                return RollbackIncompleteExitCode;
            }
        }
        else {
            log( "Restoring " + std::to_string( earlier.size() )
                 + " files an interrupted update moved aside" );
            if ( !rollback( directory, earlier, log ) ) {
                log( "Kept " + toUtf8( manifestPath ) + "; run setup again" );
                return RollbackIncompleteExitCode;
            }
        }
        fs::remove( manifestPath, error );
    }
    if ( !holdsInstallation( directory ) ) {
        log( "No CILogg installation in " + toUtf8( directory ) + "; nothing is moved aside" );
        return PreparedExitCode;
    }
    removeMovedAside( directory, log );
    for ( const auto& process : processesRunningFrom( directory ) )
        log( "Running from the installation folder: " + describeForLog( process ) );

    std::ofstream manifest( manifestPath, std::ios::binary | std::ios::trunc );
    if ( !manifest ) {
        log( "Cannot write " + toUtf8( manifestPath ) );
        return ManifestFailedExitCode;
    }
    bool recorded = true;
    const auto result = moveAside( directory, log, [ & ]( const MovedFile& moved ) {
        manifest << formatManifestLine( moved ) << "\n";
        manifest.flush();
        recorded = recorded && static_cast<bool>( manifest );
    } );
    if ( result.blocked.empty() && recorded ) {
        log( "Moved aside " + std::to_string( result.moved.size() ) + " files" );
        return PreparedExitCode;
    }
    std::vector<ProcessInfo> blockers;
    if ( !result.blocked.empty() ) {
        std::string queryError;
        blockers = processesUsing( result.blocked, &queryError );
        if ( !queryError.empty() )
            log( queryError );
        for ( const auto& process : blockers )
            log( "Uses a blocked file: " + describeForLog( process ) );
    }
    else {
        log( "Cannot record the moved files in " + toUtf8( manifestPath ) );
    }
    const bool restored = rollback( directory, result.moved, log );
    manifest.close();
    writeReport( report, result.blocked, blockers, log );
    if ( !restored ) {
        log( "Kept " + toUtf8( manifestPath ) + "; run setup again" );
        return RollbackIncompleteExitCode;
    }
    fs::remove( manifestPath, error );
    return result.blocked.empty() ? ManifestFailedExitCode : BlockedExitCode;
}

int runFinish( const fs::path& directory, const Log& log )
{
    const auto manifestPath = directory / ManifestFileName;
    std::error_code error;
    if ( !fs::exists( manifestPath, error ) ) {
        log( "No files were moved aside" );
        return PreparedExitCode;
    }
    {
        std::ofstream marker( manifestPath, std::ios::binary | std::ios::app );
        marker << FinishingMarker << "\n";
    }
    const auto notPutBack = finish( directory, readManifest( directory, manifestPath, log ), log );
    if ( notPutBack.empty() ) {
        fs::remove( manifestPath, error );
        return PreparedExitCode;
    }
    // Only what still has to be put back stays recorded: the next update restores it first.
    writeManifest( manifestPath, notPutBack );
    log( "Kept " + toUtf8( manifestPath ) + "; the next update puts these files back" );
    return FinishIncompleteExitCode;
}

int runRollback( const fs::path& directory, const Log& log )
{
    const auto manifestPath = directory / ManifestFileName;
    std::error_code error;
    if ( !fs::exists( manifestPath, error ) ) {
        log( "No files were moved aside" );
        return PreparedExitCode;
    }
    if ( !rollback( directory, readManifest( directory, manifestPath, log ), log ) ) {
        log( "Kept " + toUtf8( manifestPath ) + "; run setup again" );
        return RollbackIncompleteExitCode;
    }
    fs::remove( manifestPath, error );
    return PreparedExitCode;
}

} // namespace cilogg::install_prepare
