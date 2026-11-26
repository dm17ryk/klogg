#include <catch2/catch.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <QDir>
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
    REQUIRE( prepare::isMovedAsideNameOf( original, folder / "Qt6Core.dll.12.cilogg-old" ) );
    REQUIRE_FALSE( prepare::isMovedAsideNameOf( original, folder / "Qt6Gui.dll.cilogg-old" ) );
    REQUIRE_FALSE( prepare::isMovedAsideNameOf( original, folder / "Qt6Core.dll.x.cilogg-old" ) );
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

TEST_CASE( "Executables are moved aside and put back when they are not replaced",
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

    std::vector<std::string> lines;
    std::vector<prepare::MovedFile> seen;
    const auto result = prepare::moveAside(
        root, recorder( lines ),
        [ &seen ]( const prepare::MovedFile& moved ) { seen.push_back( moved ); } );
    REQUIRE( result.blocked.empty() );
    REQUIRE( result.moved.size() == 4 );
    REQUIRE( seen.size() == 4 );
    // Executables first, so nothing starts from a half-replaced folder.
    REQUIRE( result.moved.front().original.extension() == ".exe" );
    REQUIRE_FALSE( fs::exists( root / "cilogg.exe" ) );
    REQUIRE( readText( root / "cilogg.exe.cilogg-old" ) == "old executable" );
    // A file nobody uses stays where it is: the installer overwrites it in place.
    REQUIRE( readText( root / "README.md" ) == "old readme" );

    // The installation writes its files; dropped.dll is not part of it.
    writeText( root / "cilogg.exe", "new executable" );
    writeText( root / "Qt6Core.dll", "new library" );
    writeText( root / "python_runtime" / "_ssl.pyd", "new extension" );
    prepare::finish( result.moved, recorder( lines ) );

    REQUIRE( readText( root / "cilogg.exe" ) == "new executable" );
    REQUIRE( readText( root / "dropped.dll" ) == "only in the old version" );
    REQUIRE( movedAsideFiles( root ).empty() );
}

TEST_CASE( "A failed installation restores every moved file", "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "Qt6Core.dll", "old library" );

    std::vector<std::string> lines;
    const auto result = prepare::moveAside( root, recorder( lines ) );
    REQUIRE( result.moved.size() == 2 );
    // The copy stopped after one file.
    writeText( root / "Qt6Core.dll", "partly written" );

    REQUIRE( prepare::rollback( result.moved, recorder( lines ) ) );
    REQUIRE( readText( root / "cilogg.exe" ) == "old executable" );
    REQUIRE( readText( root / "Qt6Core.dll" ) == "old library" );
    REQUIRE( movedAsideFiles( root ).empty() );
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

    // A folder that does not exist yet (a first installation) needs nothing.
    REQUIRE( prepare::runPrepare( root / "new", {}, recorder( lines ) )
             == prepare::PreparedExitCode );
}

TEST_CASE( "An interrupted update is completed by the next one", "[installprepare]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto root = nativePath( temporary.path() );
    writeText( root / "cilogg.exe", "old executable" );
    writeText( root / "dropped.dll", "old library" );
    std::vector<std::string> lines;
    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    // The installer stopped after writing cilogg.exe, before finishing.
    writeText( root / "cilogg.exe", "new executable" );

    REQUIRE( prepare::runPrepare( root, {}, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( prepare::runFinish( root, recorder( lines ) ) == prepare::PreparedExitCode );
    REQUIRE( readText( root / "cilogg.exe" ) == "new executable" );
    REQUIRE( readText( root / "dropped.dll" ) == "old library" );
    REQUIRE( movedAsideFiles( root ).empty() );
}

#ifdef Q_OS_WIN
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
    REQUIRE( text.find( "blocker=" + std::to_string( GetCurrentProcessId() ) + "\t" )
             != std::string::npos );
}
#endif
