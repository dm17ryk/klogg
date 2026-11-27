#include <catch2/catch.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <QDir>
#include <QProcess>
#include <QTemporaryDir>

#include "installprepare.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace prepare = cilogg::install_prepare;

namespace {

fs::path nativePath( const QString& path )
{
#ifdef Q_OS_WIN
    return fs::path( QDir::toNativeSeparators( path ).toStdWString() );
#else
    return fs::path( path.toStdString() );
#endif
}

void writeText( const fs::path& path, const std::string& text )
{
    fs::create_directories( path.parent_path() );
    std::ofstream file( path, std::ios::binary | std::ios::trunc );
    file << text;
    REQUIRE( file.good() );
}

std::string readText( const fs::path& path )
{
    std::ifstream file( path, std::ios::binary );
    return std::string( ( std::istreambuf_iterator<char>( file ) ),
                        std::istreambuf_iterator<char>() );
}

prepare::Log recorder( std::vector<std::string>& lines )
{
    return [ &lines ]( const std::string& line ) { lines.push_back( line ); };
}

std::vector<fs::path> movedAsideFiles( const fs::path& directory )
{
    std::vector<fs::path> files;
    for ( const auto& entry : fs::recursive_directory_iterator( directory ) ) {
        if ( entry.is_regular_file() && prepare::isMovedAside( entry.path() ) )
            files.push_back( entry.path() );
    }
    return files;
}

#ifdef Q_OS_WIN
// A process started from `image` and kept suspended: its executable is mapped like that of any
// running CILogg, but it never runs.
class SuspendedProcess {
public:
    explicit SuspendedProcess( const fs::path& image )
    {
        STARTUPINFOW startup{};
        startup.cb = sizeof( startup );
        std::wstring command = L"\"" + image.wstring() + L"\"";
        started_ = CreateProcessW( image.c_str(), command.data(), nullptr, nullptr, FALSE,
                                   CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                                   &process_ )
                   != FALSE;
    }
    ~SuspendedProcess()
    {
        if ( !started_ )
            return;
        TerminateProcess( process_.hProcess, 0 );
        WaitForSingleObject( process_.hProcess, 5000 );
        CloseHandle( process_.hThread );
        CloseHandle( process_.hProcess );
    }
    SuspendedProcess( const SuspendedProcess& ) = delete;
    SuspendedProcess& operator=( const SuspendedProcess& ) = delete;

    bool started() const
    {
        return started_;
    }
    unsigned long pid() const
    {
        return process_.dwProcessId;
    }

private:
    PROCESS_INFORMATION process_{};
    bool started_ = false;
};

fs::path systemExecutable( const wchar_t* name )
{
    wchar_t directory[ MAX_PATH ] = {};
    REQUIRE( GetSystemDirectoryW( directory, MAX_PATH ) > 0 );
    return fs::path( directory ) / name;
}

// Opened like an editor that does not let others rename or delete the file.
class ExclusiveHandle {
public:
    explicit ExclusiveHandle( const fs::path& file )
        : handle_( CreateFileW( file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr ) )
    {
    }
    ~ExclusiveHandle()
    {
        if ( handle_ != INVALID_HANDLE_VALUE )
            CloseHandle( handle_ );
    }
    ExclusiveHandle( const ExclusiveHandle& ) = delete;
    ExclusiveHandle& operator=( const ExclusiveHandle& ) = delete;

    bool isOpen() const
    {
        return handle_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_;
};
#endif

} // namespace

TEST_CASE( "Moved-aside names belong to their original only", "[installprepare]" )
{
    const fs::path folder = fs::temp_directory_path() / "cilogg-install";
    const auto original = folder / "Qt6Core.dll";
    REQUIRE( prepare::isMovedAside( folder / "Qt6Core.dll.cilogg-old" ) );
    REQUIRE( prepare::isMovedAside( folder / "QT6CORE.DLL.CILOGG-OLD" ) );
    REQUIRE_FALSE( prepare::isMovedAside( folder / "Qt6Core.dll" ) );
    REQUIRE_FALSE( prepare::isMovedAside( folder / ".cilogg-old" ) );

    REQUIRE( prepare::isMovedAsideNameOf( original, folder / "Qt6Core.dll.cilogg-old" ) );
    REQUIRE( prepare::isMovedAsideNameOf( original, folder / "Qt6Core.dll.cilogg-old-12" ) );
    REQUIRE( prepare::isMovedAside( folder / "data.2.cilogg-old-3" ) );
    REQUIRE_FALSE( prepare::isMovedAside( folder / "Qt6Core.dll.cilogg-old-" ) );
    REQUIRE_FALSE( prepare::isMovedAsideNameOf( original, folder / "Qt6Gui.dll.cilogg-old" ) );
    REQUIRE_FALSE( prepare::isMovedAsideNameOf( original, folder / "Qt6Core.dll.cilogg-old-x" ) );
    REQUIRE_FALSE( prepare::isMovedAsideNameOf( original, folder / "Qt6Core.dll.2.cilogg-old" ) );
    REQUIRE_FALSE(
        prepare::isMovedAsideNameOf( original, folder / "other" / "Qt6Core.dll.cilogg-old" ) );
    REQUIRE_FALSE(
        prepare::isMovedAsideNameOf( folder / "a.cilogg-old", folder / "a.cilogg-old" ) );

    REQUIRE( prepare::isExecutableImage( folder / "cilogg.exe" ) );
    REQUIRE( prepare::isExecutableImage( folder / "python_runtime" / "_ssl.PYD" ) );
    REQUIRE( prepare::isExecutableImage( folder / "platforms" / "qwindows.dll" ) );
    REQUIRE_FALSE( prepare::isExecutableImage( folder / "README.md" ) );
}

TEST_CASE( "The manifest and the blocker lines round-trip", "[installprepare]" )
{
    const fs::path folder = fs::temp_directory_path() / "cilogg install";
    const prepare::MovedFile moved{ folder / "cilogg.exe", folder / "cilogg.exe.cilogg-old" };
    const auto text = prepare::formatManifestLine( moved ) + "\n" + "not a manifest line\n"
                      + "relative\tpaths\n";
    const auto parsed = prepare::parseManifest( text );
    REQUIRE( parsed.size() == 1 );
    REQUIRE( parsed.front().original == moved.original );
    REQUIRE( parsed.front().movedTo == moved.movedTo );

    prepare::ProcessInfo process;
    process.pid = 4321;
    process.name = "cilogg.exe";
    process.image = folder / "cilogg.exe";
    process.parentPid = 99;
    process.parentName = "claude\texe";
    const auto line = prepare::formatBlocker( process );
    REQUIRE( line.rfind( "4321\tcilogg.exe\t", 0 ) == 0 );
    // Tabs inside a field never break the line into more fields.
    REQUIRE( std::count( line.begin(), line.end(), '\t' ) == 4 );
    REQUIRE( line.find( "\t99\tclaude exe" ) != std::string::npos );
    REQUIRE( prepare::describeProcess( process ).find( "started by claude" ) != std::string::npos );
}

TEST_CASE( "CILogg's files are moved aside and put back when they are not replaced",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() ) / "Install folder";
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "Qt6Core.dll", "old library" );
    writeText( root / "python_runtime" / "_ssl.pyd", "old extension" );
    writeText( root / "README.md", "old readme" );
    writeText( root / "dropped.dll", "only in the old version" );
    // Another program installed into the same folder: none of its files is CILogg's.
    writeText( root / "tool.exe", "another program" );
    writeText( root / "extras" / "helper.dll", "another library" );

    std::vector<std::string> lines;
    std::vector<prepare::MovedFile> seen;
    const auto result = prepare::moveAside(
        root, recorder( lines ),
        [ &seen ]( const prepare::MovedFile& moved ) { seen.push_back( moved ); } );
    REQUIRE( result.blocked.empty() );
    REQUIRE( result.moved.size() == 5 );
    REQUIRE( seen.size() == 5 );
    // Executables first, so nothing starts from a half-replaced folder.
    REQUIRE( result.moved.front().original.extension() == ".exe" );
    REQUIRE_FALSE( fs::exists( root / "cilogg.exe" ) );
    REQUIRE( readText( root / "cilogg.exe.cilogg-old" ) == "old executable" );
    // Every file of the installation, so a failed installation can put each one back.
    REQUIRE( readText( root / "README.md.cilogg-old" ) == "old readme" );
    REQUIRE( readText( root / "tool.exe" ) == "another program" );
    REQUIRE( readText( root / "extras" / "helper.dll" ) == "another library" );

    // The installation writes its files; dropped.dll is not part of it.
    writeText( root / "cilogg.exe", "new executable" );
    writeText( root / "Qt6Core.dll", "new library" );
    writeText( root / "python_runtime" / "_ssl.pyd", "new extension" );
    REQUIRE( prepare::finish( root, result.moved, recorder( lines ) ).empty() );

    REQUIRE( readText( root / "cilogg.exe" ) == "new executable" );
    REQUIRE( readText( root / "dropped.dll" ) == "only in the old version" );
    REQUIRE( readText( root / "README.md" ) == "old readme" );
    REQUIRE( movedAsideFiles( root ).empty() );
}

TEST_CASE( "A failed installation restores every file of the previous version",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "Qt6Core.dll", "old library" );
    writeText( root / "python_runtime" / "Lib" / "os.py", "old module" );

    std::vector<std::string> lines;
    const auto result = prepare::moveAside( root, recorder( lines ) );
    REQUIRE( result.moved.size() == 3 );
    // The copy wrote a new module, a file the previous version did not have and part of a
    // library, then stopped.
    writeText( root / "python_runtime" / "Lib" / "os.py", "new module" );
    writeText( root / "plugins" / "added.json", "only in the new version" );
    writeText( root / "Qt6Core.dll", "partly written" );

    REQUIRE( prepare::rollback( root, result.moved, recorder( lines ), true ).empty() );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    REQUIRE( readText( root / "Qt6Core.dll" ) == "old library" );
    REQUIRE( readText( root / "python_runtime" / "Lib" / "os.py" ) == "old module" );
    REQUIRE_FALSE( fs::exists( root / "plugins" / "added.json" ) );
    REQUIRE( movedAsideFiles( root ).empty() );
    // A second pass (the installer retries an incomplete restore) leaves restored files alone.
    REQUIRE( prepare::rollback( root, result.moved, recorder( lines ) ).empty() );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
}

#ifdef Q_OS_WIN
TEST_CASE( "A rollback keeps its record while an added file cannot be removed",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    std::vector<std::string> lines;
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    const auto manifest = root / prepare::ManifestFileName;
    writeText( root / "cilogg.exe", "new executable" );
    const auto added = root / "plugins" / "added.json";
    writeText( added, "only in the new version" );

    {
        // Another process opened the added file: the previous version would find it.
        ExclusiveHandle held( added );
        REQUIRE( held.isOpen() );
        REQUIRE( prepare::runRollback( root, recorder( lines ) )
                 == prepare::RollbackIncompleteExitCode );
        REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
        REQUIRE( fs::exists( manifest ) );
    }
    REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE_FALSE( fs::exists( added ) );
    REQUIRE_FALSE( fs::exists( manifest ) );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
}

TEST_CASE( "A rollback whose record cannot be deleted reports it", "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    std::vector<std::string> lines;
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    const auto manifest = root / prepare::ManifestFileName;
    writeText( root / "cilogg.exe", "new executable" );

    {
        // A scanner holds the record: it can be neither deleted nor emptied, and its marks
        // would make a later run remove files added after this rollback.
        ExclusiveHandle held( manifest );
        REQUIRE( held.isOpen() );
        REQUIRE( prepare::runRollback( root, recorder( lines ) )
                 == prepare::RollbackIncompleteExitCode );
        REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    }
    REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE_FALSE( fs::exists( manifest ) );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
}
#endif

TEST_CASE( "A first installation into another program's folder moves nothing",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() ) / "Tools";
    writeText( root / "tool.exe", "another program" );
    writeText( root / "library.dll", "another library" );
    writeText( root / "plugins" / "plugin.dll", "another plugin" );
    std::vector<std::string> lines;

    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "tool.exe" ) == "another program" );
    REQUIRE( readText( root / "library.dll" ) == "another library" );
    REQUIRE( readText( root / "plugins" / "plugin.dll" ) == "another plugin" );
    REQUIRE( movedAsideFiles( root ).empty() );
    // The installation is recorded: a successful one only deletes the record.
    REQUIRE( fs::exists( root / prepare::ManifestFileName ) );
    writeText( root / "cilogg.exe", "new executable" );
    REQUIRE( prepare::runFinish( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "new executable" );
    REQUIRE( readText( root / "plugins" / "plugin.dll" ) == "another plugin" );
    REQUIRE_FALSE( fs::exists( root / prepare::ManifestFileName ) );
}

TEST_CASE( "A rollback puts back a file moved aside without a record", "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "Qt6Core.dll", "old library" );
    std::vector<std::string> lines;
    const auto result = prepare::moveAside( root, recorder( lines ) );
    REQUIRE( result.moved.size() == 2 );
    // The helper was stopped after the second rename, before recording it.
    writeText( root / prepare::ManifestFileName,
               prepare::formatManifestLine( result.moved.front() ) + "\n" );

#ifdef Q_OS_WIN
    {
        // A scanner holds the unrecorded copy: the rollback is incomplete and keeps its record.
        ExclusiveHandle scanner( result.moved.back().movedTo );
        REQUIRE( scanner.isOpen() );
        REQUIRE( prepare::runRollback( root, recorder( lines ) )
                 == prepare::RollbackIncompleteExitCode );
        REQUIRE( fs::exists( root / prepare::ManifestFileName ) );
    }
#endif
    REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    REQUIRE( readText( root / "Qt6Core.dll" ) == "old library" );
    REQUIRE( movedAsideFiles( root ).empty() );
    REQUIRE_FALSE( fs::exists( root / prepare::ManifestFileName ) );
}

TEST_CASE( "A failed first installation removes only what it wrote", "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto shared = nativePath( temporary.path() ) / "Tools";
    writeText( shared / "tool.exe", "another program" );
    writeText( shared / "library.dll", "another library" );
    writeText( shared / "plugins" / "plugin.dll", "another plugin" );
    const auto missing = nativePath( temporary.path() ) / "New folder";
    std::vector<std::string> lines;

    for ( const auto& root : { shared, missing } ) {
        REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
        // The copy wrote part of the installation, then failed.
        writeText( root / "cilogg.exe", "new executable" );
        writeText( root / "Qt6Core.dll", "new library" );
        writeText( root / "plugins" / "added.json", "new plugin" );
        REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
        REQUIRE_FALSE( fs::exists( root / "cilogg.exe" ) );
        REQUIRE_FALSE( fs::exists( root / "Qt6Core.dll" ) );
        REQUIRE_FALSE( fs::exists( root / "plugins" / "added.json" ) );
        REQUIRE_FALSE( fs::exists( root / prepare::ManifestFileName ) );
    }
    REQUIRE( readText( shared / "tool.exe" ) == "another program" );
    REQUIRE( readText( shared / "library.dll" ) == "another library" );
    REQUIRE( readText( shared / "plugins" / "plugin.dll" ) == "another plugin" );
}

TEST_CASE( "A moved-aside file whose original is missing is put back, never deleted",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "executable" );
    // An update could not put Qt6Core.dll back; cilogg.exe's old copies are obsolete.
    writeText( root / "Qt6Core.dll.cilogg-old", "the only copy" );
    writeText( root / "cilogg.exe.cilogg-old", "obsolete" );
    writeText( root / "cilogg.exe.cilogg-old-2", "obsolete too" );
    writeText( root / "plugins" / "table.2.cilogg-old", "a name with a number" );
    std::vector<std::string> lines;

    // Two copies of a file whose original is missing: the newer one is put back.
    writeText( root / "libssl.dll.cilogg-old-2", "older" );
    writeText( root / "libssl.dll.cilogg-old", "newer" );
    fs::last_write_time( root / "libssl.dll.cilogg-old-2",
                         fs::last_write_time( root / "libssl.dll.cilogg-old" )
                             - std::chrono::hours( 1 ) );

    REQUIRE( prepare::removeMovedAside( root, recorder( lines ) ) == 0 );
    REQUIRE( readText( root / "Qt6Core.dll" ) == "the only copy" );
    REQUIRE( readText( root / "libssl.dll" ) == "newer" );
    REQUIRE_FALSE( fs::exists( root / "libssl.dll.cilogg-old-2" ) );
    REQUIRE_FALSE( fs::exists( root / "cilogg.exe.cilogg-old" ) );
    REQUIRE_FALSE( fs::exists( root / "cilogg.exe.cilogg-old-2" ) );
    REQUIRE( readText( root / "plugins" / "table.2" ) == "a name with a number" );
}

TEST_CASE( "Prepare, finish and rollback keep their manifest inside the folder",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() ) / "app";
    const auto outside = nativePath( temporary.path() ) / "outside.dll";
    writeText( root / "cilogg.exe", "old executable" );
    writeText( outside, "not ours" );
    std::vector<std::string> lines;

    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    const auto manifest = root / prepare::ManifestFileName;
    REQUIRE( fs::exists( manifest ) );
    // A line pointing outside the installation folder is never acted on.
    {
        std::ofstream append( manifest, std::ios::binary | std::ios::app );
        append << prepare::formatManifestLine(
            { outside, outside.parent_path() / "outside.dll.cilogg-old" } )
               << "\n";
    }
    REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    REQUIRE( readText( outside ) == "not ours" );
    REQUIRE_FALSE( fs::exists( manifest ) );
    REQUIRE( std::any_of( lines.begin(), lines.end(), []( const std::string& line ) {
        return line.find( "outside the installation folder" ) != std::string::npos;
    } ) );

    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    writeText( root / "cilogg.exe", "new executable" );
    REQUIRE( prepare::runFinish( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "new executable" );
    REQUIRE_FALSE( fs::exists( manifest ) );
    REQUIRE( movedAsideFiles( root ).empty() );

    // A folder that does not exist yet (a first installation) is created and recorded.
    REQUIRE( prepare::runPrepare( root / "new", {}, recorder( lines ) )
             == prepare::PreparedExitCode );
}

TEST_CASE( "An interrupted update is undone by the next one", "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "dropped.dll", "old library" );
    std::vector<std::string> lines;
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    // The installer was killed after writing part of cilogg.exe.
    writeText( root / "cilogg.exe", "partly written" );

    // The next installation restores the previous version first, then moves it aside again.
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe.cilogg-old" ) == "old executable" );
    writeText( root / "cilogg.exe", "new executable" );
    REQUIRE( prepare::runFinish( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "new executable" );
    REQUIRE( readText( root / "dropped.dll" ) == "old library" );
    REQUIRE( movedAsideFiles( root ).empty() );
}

TEST_CASE( "An update interrupted while deleting old copies is completed, not undone",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "dropped.dll", "old library" );
    std::vector<std::string> lines;
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    writeText( root / "cilogg.exe", "new executable" );
    // The installer was killed in its cleanup: it had marked the manifest and deleted the old
    // cilogg.exe, but not yet put dropped.dll back.
    {
        std::ofstream marker( root / prepare::ManifestFileName, std::ios::binary | std::ios::app );
        marker << "#finishing\n";
    }
    REQUIRE( fs::remove( root / "cilogg.exe.cilogg-old" ) );

    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( prepare::runFinish( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "new executable" );
    REQUIRE( readText( root / "dropped.dll" ) == "old library" );
    REQUIRE( movedAsideFiles( root ).empty() );
}

TEST_CASE( "A rollback after the cleanup started completes it instead", "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "Qt6Core.dll", "old library" );
    std::vector<std::string> lines;
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    writeText( root / "cilogg.exe", "new executable" );
    writeText( root / "Qt6Core.dll", "new library" );
    // The cleanup was stopped after marking the manifest and deleting the old cilogg.exe.
    {
        std::ofstream marker( root / prepare::ManifestFileName, std::ios::binary | std::ios::app );
        marker << "\n#finishing\n";
    }
    REQUIRE( fs::remove( root / "cilogg.exe.cilogg-old" ) );

    REQUIRE( prepare::runRollback( root, recorder( lines ) )
             == prepare::FinishedInsteadExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "new executable" );
    REQUIRE( readText( root / "Qt6Core.dll" ) == "new library" );
    REQUIRE( movedAsideFiles( root ).empty() );
    REQUIRE_FALSE( fs::exists( root / prepare::ManifestFileName ) );
}

TEST_CASE( "A record from a run that named the folder differently is recovered safely",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() ) / "app";
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "plugins" / "kept.json", "old plugin" );
    std::vector<std::string> lines;
    // The earlier run spelled the folder with "." and ".." parts (or an 8.3 name).
    const auto otherSpelling = root / "plugins" / ".." / ".";
    REQUIRE( prepare::runPrepare( otherSpelling, {}, recorder( lines ) )
             == prepare::PreparedExitCode );
    writeText( root / "cilogg.exe", "new executable" );
    writeText( root / "plugins" / "added.json", "only in the new version" );

    // A later run recovers it under the plain spelling: the previous files are back, and only
    // the added file is removed.
    REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    REQUIRE( readText( root / "plugins" / "kept.json" ) == "old plugin" );
    REQUIRE_FALSE( fs::exists( root / "plugins" / "added.json" ) );
    REQUIRE( movedAsideFiles( root ).empty() );
}

TEST_CASE( "A manifest line torn inside a character does not stop later updates",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    // U+00E9 is two bytes in UTF-8, C3 A9.
    const auto plugin = root / "plugins" / nativePath( QString::fromUtf8( "\xC3\xA9.json" ) );
    writeText( plugin, "old plugin" );
    std::vector<std::string> lines;
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );

    // A power loss stopped prepare while it recorded the plugin: its last line ends inside "é",
    // without its line break.
    const auto manifestPath = root / prepare::ManifestFileName;
    const auto text = readText( manifestPath );
    const auto cut = text.rfind( "\xC3\xA9" );
    REQUIRE( cut != std::string::npos );
    writeText( manifestPath, text.substr( 0, cut + 1 ) );
    std::vector<prepare::MovedFile> entries;
    REQUIRE_NOTHROW( entries = prepare::parseManifest( readText( manifestPath ) ) );
    REQUIRE( entries.size() == 1 );
    REQUIRE( entries.front().original == root / "cilogg.exe" );

    // The next update restores the recorded file and puts back the unrecorded copy.
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    REQUIRE( readText( plugin ) == "old plugin" );
    REQUIRE( movedAsideFiles( root ).empty() );
}

TEST_CASE( "Only complete, readable manifest lines are read", "[installprepare]" )
{
    const fs::path folder = fs::temp_directory_path() / "cilogg install";
    const auto first = prepare::formatManifestLine(
        { folder / "cilogg.exe", folder / "cilogg.exe.cilogg-old" } );
    const auto second
        = prepare::formatManifestLine( { folder / "x.dll", folder / "x.dll.cilogg-old-2" } );
    REQUIRE( prepare::parseManifest( first + "\n" + second + "\n" ).size() == 2 );
    // A power loss cut the last line after "x.dll.cilogg-old": it still reads as a path, but it
    // names an older copy, not the one this update moved aside.
    const auto torn = first + "\n" + second.substr( 0, second.size() - 2 );
    REQUIRE( prepare::parseManifest( torn ).size() == 1 );
    REQUIRE( prepare::parseManifest( second.substr( 0, second.size() - 2 ) ).empty() );
#ifdef Q_OS_WIN
    // A line that is not UTF-8 (a damaged disk) is skipped. Only Windows refuses to decode it;
    // elsewhere a name is a byte string, and readManifest() checks what it names.
    const std::string damaged = "C:\\cilogg\\\xC3\tC:\\cilogg\\\xC3.cilogg-old\n";
    REQUIRE( prepare::parseManifest( damaged + first + "\n" ).size() == 1 );
#endif
}

TEST_CASE( "A file whose name the manifest cannot record is left alone", "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
#ifdef Q_OS_WIN
    // NTFS allows an unpaired surrogate in a name, which UTF-8 cannot hold.
    std::wstring name = L"odd";
    name += static_cast<wchar_t>( 0xD800 );
    name += L".dll";
#else
    // A line break would split the manifest line.
    const std::string name = "odd\nname.dll";
#endif
    // In an installed folder, as a folder there, and next to cilogg.exe (where every DLL counts as
    // CILogg's).
    const auto odd = root / "plugins" / name;
    const auto oddTop = root / name;
    const auto inFolder = root / "python_runtime" / name / "inner.json";
    struct RemoveOnExit {
        // QTemporaryDir may not remove such names: done even when a check fails.
        std::vector<fs::path> paths;
        ~RemoveOnExit()
        {
            for ( const auto& path : paths ) {
                std::error_code ignored;
                fs::remove_all( path, ignored );
            }
        }
    } cleanup{ { odd, oddTop, inFolder.parent_path() } };
    writeText( odd, "another program's file" );
    writeText( oddTop, "another program's library" );
    writeText( inFolder, "another program's data" );
    std::vector<std::string> lines;

    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( odd ) == "another program's file" );
    REQUIRE( readText( oddTop ) == "another program's library" );
    REQUIRE( readText( inFolder ) == "another program's data" );
    writeText( root / "cilogg.exe", "new executable" );
    // A failed installation removes the files it added, but none of these.
    REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    REQUIRE( readText( odd ) == "another program's file" );
    REQUIRE( readText( oddTop ) == "another program's library" );
    REQUIRE( readText( inFolder ) == "another program's data" );
}

#ifdef Q_OS_WIN
TEST_CASE( "A record torn after a shorter copy name never restores that older copy",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    // A copy from two updates ago that a running program still keeps open.
    const auto older = root / "x.dll.cilogg-old";
    writeText( older, "library from two updates ago" );
    fs::last_write_time( older, fs::last_write_time( older ) - std::chrono::hours( 1 ) );
    writeText( root / "x.dll", "previous library" );
    ExclusiveHandle program( older );
    REQUIRE( program.isOpen() );
    std::vector<std::string> lines;
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "x.dll.cilogg-old-2" ) == "previous library" );

    // A power loss cut the record of x.dll right after "x.dll.cilogg-old".
    const auto manifestPath = root / prepare::ManifestFileName;
    const auto text = readText( manifestPath );
    const std::string olderName = "x.dll.cilogg-old";
    const auto cut = text.find( olderName + "-2" );
    REQUIRE( cut != std::string::npos );
    writeText( manifestPath, text.substr( 0, cut + olderName.size() ) );

    // The next update puts back the newest copy, the previous version's.
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "x.dll" ) == "previous library" );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    REQUIRE( readText( older ) == "library from two updates ago" );
}

TEST_CASE( "Another program's DLL in use in the same folder does not block the installation",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() ) / "Tools";
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "other.dll", "another program's library" );
    std::vector<std::string> lines;
    // Held without sharing delete or write: it can be neither moved nor overwritten.
    ExclusiveHandle other( root / "other.dll" );
    REQUIRE( other.isOpen() );

    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE_FALSE( fs::exists( root / "cilogg.exe" ) );
    REQUIRE( fs::exists( root / "other.dll" ) );
    REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    // Unchanged: its copy is deleted.
    REQUIRE( movedAsideFiles( root ).empty() );
}

TEST_CASE( "A DLL held during prepare is restored if the installation overwrote it",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "Qt6Core.dll", "old library" );
    std::vector<std::string> lines;
    {
        // Security software holds the library through every move attempt.
        ExclusiveHandle scanner( root / "Qt6Core.dll" );
        REQUIRE( scanner.isOpen() );
        REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    }
    // Released, the library is overwritten; then another file fails to copy.
    writeText( root / "Qt6Core.dll", "new library" );
    REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "Qt6Core.dll" ) == "old library" );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    REQUIRE( movedAsideFiles( root ).empty() );
    REQUIRE_FALSE( fs::exists( root / prepare::ManifestFileName ) );
}

TEST_CASE( "An incomplete rollback keeps its whole record for another pass", "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "Qt6Core.dll", "old library" );
    std::vector<std::string> lines;
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    // The copy wrote some files, then failed.
    writeText( root / "Qt6Core.dll", "new library" );
    writeText( root / "plugins" / "added.json", "only in the new version" );
    {
        // Security software holds the old library's copy during the first rollback.
        ExclusiveHandle scanner( root / "Qt6Core.dll.cilogg-old" );
        REQUIRE( scanner.isOpen() );
        REQUIRE( prepare::runRollback( root, recorder( lines ) )
                 == prepare::RollbackIncompleteExitCode );
    }
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    REQUIRE_FALSE( fs::exists( root / "plugins" / "added.json" ) );
    const auto record = readText( root / prepare::ManifestFileName );
    REQUIRE( record.find( "#prepared" ) != std::string::npos );

    REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "Qt6Core.dll" ) == "old library" );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    REQUIRE_FALSE( fs::exists( root / prepare::ManifestFileName ) );
    REQUIRE( movedAsideFiles( root ).empty() );
}

TEST_CASE( "A read-only old copy is deleted too", "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "executable" );
    writeText( root / "plugins" / "user.json", "current" );
    const auto copy = root / "plugins" / "user.json.cilogg-old";
    writeText( copy, "old" );
    REQUIRE( SetFileAttributesW( copy.c_str(), FILE_ATTRIBUTE_READONLY ) );
    std::vector<std::string> lines;

    REQUIRE( prepare::removeMovedAside( root, recorder( lines ) ) == 0 );
    REQUIRE_FALSE( fs::exists( copy ) );
}

TEST_CASE( "Without its finishing mark, nothing is cleaned up", "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    std::vector<std::string> lines;
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    writeText( root / "cilogg.exe", "new executable" );
    const auto manifest = root / prepare::ManifestFileName;
    // Security software holds the manifest: it cannot be marked.
    {
        ExclusiveHandle scanner( manifest );
        REQUIRE( scanner.isOpen() );
        REQUIRE( prepare::runFinish( root, recorder( lines ) )
                 == prepare::ManifestFailedExitCode );
    }
    REQUIRE( readText( root / "cilogg.exe.cilogg-old" ) == "old executable" );
    REQUIRE( prepare::runFinish( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "new executable" );
    REQUIRE( movedAsideFiles( root ).empty() );
}

TEST_CASE( "A running executable is moved aside and replaced, not closed", "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() ) / "cilogg";
    fs::create_directories( root );
    const auto image = root / "cilogg.exe";
    fs::copy_file( systemExecutable( L"hostname.exe" ), image );
    std::optional<SuspendedProcess> running( std::in_place, image );
    REQUIRE( running->started() );
    REQUIRE( prepare::isInUse( image ) );

    const auto instances = prepare::processesRunningFrom( root );
    REQUIRE( instances.size() == 1 );
    REQUIRE( instances.front().pid == running->pid() );
    REQUIRE( instances.front().name == "cilogg.exe" );
    REQUIRE( instances.front().parentPid == GetCurrentProcessId() );
    REQUIRE_FALSE( instances.front().parentName.empty() );

    std::vector<std::string> lines;
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE_FALSE( fs::exists( image ) );
    writeText( image, "new executable" );
    REQUIRE( prepare::runFinish( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( image ) == "new executable" );
    // The old copy stays until no process uses it; the running process was not touched.
    REQUIRE( movedAsideFiles( root ).size() == 1 );
    REQUIRE( prepare::processesRunningFrom( root ).size() == 1 );

    // Once it has exited, the next update deletes the old copy.
    running.reset();
    REQUIRE( prepare::removeMovedAside( root, recorder( lines ) ) == 0 );
    REQUIRE( movedAsideFiles( root ).empty() );
}

TEST_CASE( "A file another program keeps open blocks the installation and is named",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() ) / "cilogg";
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "plugins" / "data.json", "held" );
    const auto report = nativePath( temporary.path() ) / "report.txt";
    std::vector<std::string> lines;
    {
        ExclusiveHandle editor( root / "plugins" / "data.json" );
        REQUIRE( editor.isOpen() );
        REQUIRE( prepare::isInUse( root / "plugins" / "data.json" ) );
        REQUIRE( prepare::runPrepare( root, report, recorder( lines ) )
                 == prepare::BlockedExitCode );
    }
    // Nothing changed: the executable moved first is back, and no manifest is left.
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    REQUIRE( movedAsideFiles( root ).empty() );
    REQUIRE_FALSE( fs::exists( root / prepare::ManifestFileName ) );

    const auto text = readText( report );
    REQUIRE( text.find( "blocked-file=" ) != std::string::npos );
    REQUIRE( text.find( "data.json" ) != std::string::npos );
    // The Restart Manager names this process, unless it is unavailable here (then it says so).
    const bool named = text.find( "blocker=" + std::to_string( GetCurrentProcessId() ) + "\t" )
                       != std::string::npos;
    const bool unavailable = std::any_of( lines.begin(), lines.end(), []( const std::string& line ) {
        return line.find( "Restart Manager" ) != std::string::npos;
    } );
    REQUIRE( ( named || unavailable ) );
}

TEST_CASE( "A file that cannot be put back stays recorded for the next update",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "dropped.dll", "old library" );
    std::vector<std::string> lines;
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    writeText( root / "cilogg.exe", "new executable" );
    {
        // Security software holds the moved-aside copy while the installation finishes.
        ExclusiveHandle scanner( root / "dropped.dll.cilogg-old" );
        REQUIRE( scanner.isOpen() );
        REQUIRE( prepare::runFinish( root, recorder( lines ) )
                 == prepare::FinishIncompleteExitCode );
    }
    REQUIRE( fs::exists( root / prepare::ManifestFileName ) );
    REQUIRE( readText( root / "dropped.dll.cilogg-old" ) == "old library" );

    // The next update puts it back first and keeps the installed files.
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( prepare::runFinish( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "dropped.dll" ) == "old library" );
    REQUIRE( readText( root / "cilogg.exe" ) == "new executable" );
    REQUIRE( movedAsideFiles( root ).empty() );
}

TEST_CASE( "A junction in the installation folder never leads the installer outside it",
           "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() ) / "cilogg";
    const auto outside = nativePath( temporary.path() ) / "protected";
    writeText( root / "cilogg.exe", "executable" );
    writeText( outside / "victim.dll", "protected library" );
    writeText( outside / "victim.dll.cilogg-old", "protected copy" );
    const auto junction = root / "plugins";
    REQUIRE( QProcess::execute( QStringLiteral( "cmd.exe" ),
                                { QStringLiteral( "/c" ), QStringLiteral( "mklink" ),
                                  QStringLiteral( "/J" ),
                                  QString::fromStdWString( junction.wstring() ),
                                  QString::fromStdWString( outside.wstring() ) } )
             == 0 );
    // A manifest written by someone who can write the installation folder.
    writeText( root / prepare::ManifestFileName,
               prepare::formatManifestLine(
                   { junction / "victim.dll", junction / "victim.dll.cilogg-old" } )
                   + "\n" );
    std::vector<std::string> lines;

    // The entry is refused and dropped: it can never be acted on, so it must not block later
    // updates either.
    REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE_FALSE( fs::exists( root / prepare::ManifestFileName ) );
    REQUIRE( readText( outside / "victim.dll" ) == "protected library" );
    REQUIRE( readText( outside / "victim.dll.cilogg-old" ) == "protected copy" );
    for ( const auto* finishing : { "", "#finishing\n" } ) {
        writeText( root / prepare::ManifestFileName,
                   prepare::formatManifestLine(
                       { junction / "victim.dll", junction / "victim.dll.cilogg-old" } )
                       + "\n" + finishing );
        // The record is dropped; then nothing is installed: the installer would write its
        // plugins through the junction, to files outside the installation and its record.
        REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) )
                 == prepare::ManifestFailedExitCode );
        REQUIRE_FALSE( fs::exists( root / prepare::ManifestFileName ) );
        REQUIRE( readText( outside / "victim.dll" ) == "protected library" );
        REQUIRE( readText( outside / "victim.dll.cilogg-old" ) == "protected copy" );
        REQUIRE( prepare::runRollback( root, recorder( lines ) ) == prepare::PreparedExitCode );
        REQUIRE( readText( root / "cilogg.exe" ) == "executable" );
    }
    // Moving aside never enters the junction either.
    const auto result = prepare::moveAside( root, recorder( lines ) );
    REQUIRE( result.blocked.empty() );
    REQUIRE( result.moved.size() == 1 );
    REQUIRE( readText( outside / "victim.dll" ) == "protected library" );
    REQUIRE( prepare::rollback( root, result.moved, recorder( lines ) ).empty() );
    std::error_code removed;
    fs::remove( junction, removed );
}
#endif
