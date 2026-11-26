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
// Appended once every file prepare moved aside is recorded. Only then does the manifest list
// every file of the previous version, so a rollback may remove all other files as added ones.
constexpr char PreparedMarker[] = "#prepared";

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

bool allDigits( const std::string& text, std::size_t from )
{
    return from < text.size()
           && std::all_of( text.begin() + static_cast<std::ptrdiff_t>( from ), text.end(),
                           []( char character ) {
                               return std::isdigit( static_cast<unsigned char>( character ) ) != 0;
                           } );
}

// Length of the moved-aside part of a lower-case file name, ".cilogg-old" or
// ".cilogg-old-<n>", or 0 when the name has none. What is left is the original name.
std::size_t movedAsideSuffixLength( const std::string& name )
{
    const std::string suffix = MovedAsideSuffix;
    auto body = name.size();
    const auto dash = name.find_last_of( '-' );
    if ( dash != std::string::npos && allDigits( name, dash + 1 ) )
        body = dash;
    if ( body <= suffix.size() || name.compare( body - suffix.size(), suffix.size(), suffix ) != 0 )
        return 0;
    return name.size() - ( body - suffix.size() );
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
    // False when the folder could not be read to its end.
    bool complete = true;
};

Listing listDirectory( const fs::path& directory )
{
    Listing listing;
    WIN32_FIND_DATAW data{};
    const auto pattern = directory / L"*";
    const auto find = FindFirstFileExW( pattern.c_str(), FindExInfoBasic, &data,
                                        FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH );
    if ( find == INVALID_HANDLE_VALUE ) {
        const auto error = GetLastError();
        listing.complete = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
        return listing;
    }
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
    listing.complete = GetLastError() == ERROR_NO_MORE_FILES;
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

// True when `file` is gone. A read-only file is deleted too.
bool removeFile( const fs::path& file, std::string* error )
{
    if ( DeleteFileW( file.c_str() ) )
        return true;
    auto code = GetLastError();
    if ( code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND )
        return true;
    if ( code == ERROR_ACCESS_DENIED ) {
        const auto attributes = GetFileAttributesW( file.c_str() );
        if ( attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_READONLY )
             && SetFileAttributesW( file.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY ) ) {
            if ( DeleteFileW( file.c_str() ) )
                return true;
            code = GetLastError();
            SetFileAttributesW( file.c_str(), attributes );
        }
    }
    if ( error )
        *error = systemErrorText( code );
    return false;
}

#else
struct Listing {
    std::vector<fs::path> files;
    std::vector<fs::path> folders;
    bool complete = true;
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
    listing.complete = !error || error == std::errc::no_such_file_or_directory;
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

void collectFiles( const fs::path& directory, std::vector<fs::path>& files, bool* complete )
{
    const auto listing = listDirectory( directory );
    *complete = *complete && listing.complete;
    files.insert( files.end(), listing.files.begin(), listing.files.end() );
    for ( const auto& folder : listing.folders )
        collectFiles( folder, files, complete );
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
// `complete` (optional) is false when a folder could not be read to its end.
std::vector<fs::path> installationFiles( const fs::path& directory, bool* complete = nullptr )
{
    const auto top = listDirectory( directory );
    bool read = top.complete;
    std::vector<fs::path> files;
    for ( const auto& file : top.files ) {
        if ( isOwnedTopLevelFile( file ) || isMovedAside( file ) )
            files.push_back( file );
    }
    for ( const auto& folder : top.folders ) {
        if ( listed( OwnedFolders, lowerName( folder ) ) )
            collectFiles( folder, files, &read );
    }
    if ( complete )
        *complete = read;
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

// True when a file holds the name `path`, also one that is being deleted.
bool nameTaken( const fs::path& path )
{
#ifdef _WIN32
    if ( GetFileAttributesW( path.c_str() ) != INVALID_FILE_ATTRIBUTES )
        return true;
    // A file deleted while another process still has it open keeps its name until then.
    return GetLastError() == ERROR_ACCESS_DENIED;
#else
    std::error_code error;
    return fs::exists( fs::symlink_status( path, error ) );
#endif
}

// Renames `original` to the first free "<name>.cilogg-old" or "<name>.cilogg-old-<n>" next to it.
RenameResult moveToFreeName( const fs::path& original, const fs::path& directory, fs::path* movedTo,
                             std::string* error )
{
    for ( int copy = 1; copy <= MaxMovedAsideCopies; ++copy ) {
        auto name = original.filename();
        name += MovedAsideSuffix;
        if ( copy > 1 )
            name += "-" + std::to_string( copy );
        const auto target = original.parent_path() / name;
        const auto result = renameInside( original, target, directory, error );
        if ( result == RenameResult::DestinationExists
             || ( result == RenameResult::Failed && error && *error != OutsideError
                  && nameTaken( target ) ) )
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

// "#left\t<path>": a file of the installation that prepare left where it was (a top-level DLL
// that may be another program's). Rollback keeps it; every other file it did not restore was
// written by the failed installation and is removed.
constexpr char LeftInPlacePrefix[] = "#left\t";

std::vector<fs::path> readLeftInPlace( const fs::path& manifestPath )
{
    std::vector<fs::path> left;
    std::ifstream file( manifestPath, std::ios::binary );
    std::string line;
    const std::string prefix = LeftInPlacePrefix;
    while ( std::getline( file, line ) ) {
        if ( !line.empty() && line.back() == '\r' )
            line.pop_back();
        if ( line.compare( 0, prefix.size(), prefix ) == 0 )
            left.push_back( fromUtf8( line.substr( prefix.size() ) ) );
    }
    return left;
}

// A manifest that exists but cannot be read (security software holding it) must be kept, not
// treated as empty.
bool canRead( const fs::path& manifestPath, const Log& log )
{
    std::ifstream file( manifestPath, std::ios::binary );
    if ( file.is_open() )
        return true;
    log( "Cannot read " + toUtf8( manifestPath ) + "; it is kept for the next attempt" );
    return false;
}

bool hasMarker( const fs::path& manifestPath, const char* marker )
{
    std::ifstream file( manifestPath, std::ios::binary );
    std::string line;
    while ( std::getline( file, line ) ) {
        if ( !line.empty() && line.back() == '\r' )
            line.pop_back();
        if ( line == marker )
            return true;
    }
    return false;
}

// Keeps only `entries`; `marker` (FinishingMarker or none) says how the next run treats them.
void writeManifest( const fs::path& path, const std::vector<MovedFile>& entries,
                    const char* marker = nullptr )
{
    std::ofstream file( path, std::ios::binary | std::ios::trunc );
    for ( const auto& entry : entries )
        file << formatManifestLine( entry ) << "\n";
    if ( marker )
        file << marker << "\n";
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
    return movedAsideSuffixLength( lowerAscii( toUtf8( path.filename() ) ) ) > 0;
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
    const auto suffixLength = movedAsideSuffixLength( candidateName );
    return suffixLength > 0 && candidateName.size() - suffixLength == name.size()
           && candidateName.compare( 0, name.size(), name ) == 0;
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
    bool listComplete = false;
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
        if ( status == ERROR_SUCCESS ) {
            infos.resize( count );
            listComplete = true;
        }
        break;
    }
    RmEndSession( session );
    if ( status == ERROR_SUCCESS && !listComplete )
        status = ERROR_MORE_DATA; // the list kept growing
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
    struct Copy {
        fs::path file;
        fs::path original;
        fs::file_time_type written;
    };
    std::vector<Copy> copies;
    for ( const auto& file : installationFiles( directory ) ) {
        const auto fileName = toUtf8( file.filename() );
        const auto suffixLength = movedAsideSuffixLength( lowerAscii( fileName ) );
        if ( suffixLength == 0 )
            continue;
        std::error_code error;
        copies.push_back(
            { file,
              file.parent_path()
                  / fromUtf8( fileName.substr( 0, fileName.size() - suffixLength ) ),
              fs::last_write_time( file, error ) } );
    }
    // The newest copy of a file first: when its original is missing, that one is put back.
    std::sort( copies.begin(), copies.end(), []( const Copy& left, const Copy& right ) {
        return left.original != right.original ? left.original < right.original
                                               : left.written > right.written;
    } );

    std::size_t removed = 0;
    std::size_t restored = 0;
    std::size_t remaining = 0;
    for ( const auto& [ file, original, written ] : copies ) {
        (void)written;
        std::error_code error;
        if ( !fs::exists( original, error ) ) {
            // The only copy of a file an update could not put back: never delete it.
            std::string renameError;
            if ( renameInside( file, original, directory, &renameError ) == RenameResult::Renamed ) {
                ++restored;
                log( "Put back " + toUtf8( original ) );
            }
            else {
                ++remaining;
                log( "Kept " + toUtf8( file ) + ": its original is missing (" + renameError + ")" );
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
    for ( const auto& file : installationFiles( directory, &result.listedAll ) ) {
        if ( isMovedAside( file ) )
            continue;
        if ( lowerExtension( file ) == ".exe" )
            executables.push_back( file );
        else if ( isExecutableImage( file ) )
            libraries.push_back( file );
        else
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
        // Only one the installer could not overwrite either: a DLL it could write would be
        // replaced without a copy to restore.
        if ( file.parent_path() == directory && lowerExtension( file ) == ".dll"
             && error != OutsideError && isInUse( file ) ) {
            log( "Left in place (" + error + "): " + toUtf8( file ) );
            result.leftInPlace.push_back( file );
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
    std::size_t deleted = 0;
    std::size_t kept = 0;
    std::size_t restored = 0;
    std::vector<MovedFile> notReplaced;
    for ( const auto& file : moved ) {
        std::error_code error;
        if ( !fs::exists( file.movedTo, error ) )
            continue; // handled by an earlier pass
        if ( fs::exists( file.original, error ) ) {
            if ( deleteOrSchedule( file.movedTo, directory, log ) )
                ++deleted;
            else
                ++kept;
        }
        else {
            notReplaced.push_back( file );
        }
    }
    // The installation did not ship these files (an optional component, or one a newer version
    // no longer has): keep the previous ones, as an in-place copy would have. Security software
    // can hold one for a moment: they are tried again after a pause.
    std::vector<std::pair<MovedFile, std::string>> notPutBack;
    for ( int round = 0; round <= MoveRetryRounds && !notReplaced.empty(); ++round ) {
        if ( round > 0 )
            std::this_thread::sleep_for( MoveRetryDelay );
        notPutBack.clear();
        for ( const auto& file : notReplaced ) {
            std::string renameError;
            if ( renameInside( file.movedTo, file.original, directory, &renameError )
                 == RenameResult::Renamed ) {
                ++restored;
                log( "Not replaced, kept the previous file: " + toUtf8( file.original ) );
            }
            else {
                notPutBack.emplace_back( file, renameError );
            }
        }
        notReplaced.clear();
        for ( const auto& entry : notPutBack ) {
            if ( entry.second != OutsideError )
                notReplaced.push_back( entry.first );
        }
    }
    std::vector<MovedFile> left;
    for ( const auto& [ file, error ] : notPutBack ) {
        log( "Cannot put back " + toUtf8( file.original ) + ": " + error );
        // Retried by the next update; an entry that can never be acted on is dropped.
        if ( error != OutsideError )
            left.push_back( file );
    }
    log( "Replaced files: " + std::to_string( deleted ) + " old copies deleted, "
         + std::to_string( kept ) + " still in use, " + std::to_string( restored )
         + " not replaced and kept, " + std::to_string( left.size() ) + " not put back" );
    return left;
}

namespace {
// Equal for every spelling of one path: case, 8.3 short names, relative parts. A manifest may
// come from an earlier run that named the folder differently.
fs::path::string_type pathKey( const fs::path& path )
{
#ifdef _WIN32
    auto key = normalized( path );
    if ( !key.empty() )
        CharUpperBuffW( key.data(), static_cast<DWORD>( key.size() ) );
    return key;
#else
    return path.lexically_normal().native();
#endif
}

// After the previous files are back: every other file of the installation was written by the
// failed installation (prepare moved all of them aside), unless prepare left it in place.
std::size_t removeNewFiles( const fs::path& directory, const std::vector<MovedFile>& moved,
                            const std::vector<fs::path>& leftInPlace, const Log& log )
{
    std::set<fs::path::string_type> known;
    for ( const auto& entry : moved )
        known.insert( pathKey( entry.original ) );
    for ( const auto& path : leftInPlace )
        known.insert( pathKey( path ) );
    std::vector<fs::path> added;
    std::size_t matched = 0;
    for ( const auto& file : installationFiles( directory ) ) {
        if ( isMovedAside( file ) )
            continue;
        if ( known.count( pathKey( file ) ) > 0 )
            ++matched;
        else
            added.push_back( file );
    }
    // The record must describe this folder: when none of its files is found here, it cannot tell
    // added files from previous ones, so nothing is removed.
    if ( matched == 0 && !moved.empty() ) {
        log( "The record matches no file of " + toUtf8( directory ) + "; no file is removed" );
        return 0;
    }
    std::size_t removed = 0;
    for ( const auto& file : added ) {
        fs::path newCopy;
        std::string error;
        if ( moveToFreeName( file, directory, &newCopy, &error ) != RenameResult::Renamed ) {
            log( "Cannot remove the new " + toUtf8( file ) + ": " + error );
            continue;
        }
        deleteOrSchedule( newCopy, directory, log );
        ++removed;
    }
    if ( removed > 0 )
        log( "Removed " + std::to_string( removed ) + " files the installation had added" );
    return removed;
}
} // namespace

std::vector<MovedFile> rollback( const fs::path& directory, const std::vector<MovedFile>& moved,
                                 const Log& log, bool removeAdded,
                                 const std::vector<fs::path>& leftInPlace )
{
    std::vector<MovedFile> left;
    std::size_t lost = 0;
    for ( auto file = moved.rbegin(); file != moved.rend(); ++file ) {
        std::error_code error;
        if ( !fs::exists( file->movedTo, error ) ) {
            // Restored by an earlier pass, unless the original is gone too: then nothing can
            // bring it back, and the next installation writes it again.
            if ( !fs::exists( file->original, error ) ) {
                log( "Lost: " + toUtf8( file->original ) );
                ++lost;
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
                if ( moveError != OutsideError )
                    left.push_back( *file );
                continue;
            }
            deleteOrSchedule( newCopy, directory, log );
        }
        std::string renameError;
        if ( renameInside( file->movedTo, file->original, directory, &renameError )
             != RenameResult::Renamed ) {
            log( "Cannot restore " + toUtf8( file->original ) + ": " + renameError );
            // An entry outside the installation folder can never be acted on: dropped.
            if ( renameError != OutsideError )
                left.push_back( *file );
        }
    }
    log( left.empty() && lost == 0
             ? "Restored " + std::to_string( moved.size() ) + " files"
             : "Restored the previous files except " + std::to_string( left.size() )
                   + " to try again and " + std::to_string( lost ) + " lost" );
    if ( removeAdded )
        removeNewFiles( directory, moved, leftInPlace, log );
    return left;
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
        if ( !canRead( manifestPath, log ) )
            return RollbackIncompleteExitCode;
        const auto earlier = readManifest( directory, manifestPath, log );
        if ( hasMarker( manifestPath, FinishingMarker ) ) {
            log( "Finishing an interrupted update of " + std::to_string( earlier.size() )
                 + " files" );
            const auto notPutBack = finish( directory, earlier, log );
            if ( !notPutBack.empty() ) {
                writeManifest( manifestPath, notPutBack, FinishingMarker );
                log( "Kept " + toUtf8( manifestPath ) + "; run setup again" );
                return RollbackIncompleteExitCode;
            }
        }
        else {
            log( "Restoring " + std::to_string( earlier.size() )
                 + " files an interrupted update moved aside" );
            const auto left
                = rollback( directory, earlier, log, hasMarker( manifestPath, PreparedMarker ),
                            readLeftInPlace( manifestPath ) );
            if ( !left.empty() ) {
                // Kept whole, with its marks: another pass skips what is restored already.
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
    for ( const auto& file : result.leftInPlace )
        manifest << LeftInPlacePrefix << withoutSeparators( toUtf8( file ) ) << "\n";
    // Only a complete listing makes the manifest a complete record of the previous version.
    if ( result.blocked.empty() && result.listedAll )
        manifest << PreparedMarker << "\n";
    if ( !result.listedAll )
        log( "A folder of the installation could not be read to its end; a failed installation "
             "keeps the files it added" );
    manifest.flush();
    recorded = recorded && static_cast<bool>( manifest );
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
    // Nothing was copied yet: the blocked files are the previous version's too.
    auto untouched = result.leftInPlace;
    untouched.insert( untouched.end(), result.blocked.begin(), result.blocked.end() );
    const auto left = rollback( directory, result.moved, log, false, untouched );
    manifest.close();
    writeReport( report, result.blocked, blockers, log );
    if ( !left.empty() ) {
        // Kept whole, with its marks: another pass skips what is restored already.
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
    if ( !canRead( manifestPath, log ) )
        return ManifestFailedExitCode;
    {
        // On a line of its own, also after a line an interrupted pass left unfinished. Without
        // it, a later recovery would undo this installation: nothing is cleaned up then.
        std::ofstream marker( manifestPath, std::ios::binary | std::ios::app );
        marker << "\n" << FinishingMarker << "\n";
        marker.flush();
        if ( !marker ) {
            log( "Cannot mark " + toUtf8( manifestPath )
                 + "; the old copies stay until setup runs again" );
            return ManifestFailedExitCode;
        }
    }
    const auto notPutBack = finish( directory, readManifest( directory, manifestPath, log ), log );
    if ( notPutBack.empty() ) {
        fs::remove( manifestPath, error );
        return PreparedExitCode;
    }
    // Only what still has to be put back stays recorded: the next update puts it back first.
    writeManifest( manifestPath, notPutBack, FinishingMarker );
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
    if ( !canRead( manifestPath, log ) )
        return RollbackIncompleteExitCode;
    if ( hasMarker( manifestPath, FinishingMarker ) ) {
        // The cleanup had started (its helper was stopped): some old copies may be gone, so the
        // new version can no longer be undone. Complete the cleanup instead.
        log( "The update's cleanup had started; completing it instead of undoing the update" );
        const auto notPutBack
            = finish( directory, readManifest( directory, manifestPath, log ), log );
        if ( notPutBack.empty() ) {
            fs::remove( manifestPath, error );
            return FinishedInsteadExitCode;
        }
        writeManifest( manifestPath, notPutBack, FinishingMarker );
        log( "Kept " + toUtf8( manifestPath ) + "; the next update puts these files back" );
        return FinishedInsteadIncompleteExitCode;
    }
    const auto left = rollback( directory, readManifest( directory, manifestPath, log ), log,
                                hasMarker( manifestPath, PreparedMarker ),
                                readLeftInPlace( manifestPath ) );
    if ( !left.empty() ) {
        // Kept whole, with its marks: another pass skips what is restored already.
        log( "Kept " + toUtf8( manifestPath ) + "; run setup again" );
        return RollbackIncompleteExitCode;
    }
    fs::remove( manifestPath, error );
    return PreparedExitCode;
}

} // namespace cilogg::install_prepare
