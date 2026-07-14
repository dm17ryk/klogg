#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>

#include <shellapi.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
struct Arguments {
    long long waitPid = 0;
    std::string mode;
    fs::path current;
    fs::path staged;
    fs::path backup;
    fs::path relaunch;
    fs::path acknowledgement;
    fs::path log;
    std::string token;
};

std::ofstream logFile;

void log( const std::string& message )
{
    const auto now = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() );
    logFile << now << " " << message << std::endl;
}

fs::path withAsciiSuffix( fs::path path, const std::string& suffix )
{
#ifdef _WIN32
    path += std::wstring( suffix.begin(), suffix.end() );
#else
    path += suffix;
#endif
    return path;
}

#ifdef _WIN32
bool parse( int argc, wchar_t** argv, Arguments& result )
{
    std::map<std::wstring, std::string*> strings{ { L"--mode", &result.mode },
                                                  { L"--token", &result.token } };
    std::map<std::wstring, fs::path*> paths{
        { L"--current", &result.current },     { L"--staged", &result.staged },
        { L"--backup", &result.backup },       { L"--relaunch", &result.relaunch },
        { L"--ack", &result.acknowledgement }, { L"--log", &result.log }
    };
    for ( int i = 1; i < argc; ++i ) {
        const std::wstring key = argv[ i ];
        if ( i + 1 >= argc )
            return false;
        if ( key == L"--wait-pid" ) {
            try {
                result.waitPid = std::stoll( std::wstring( argv[ ++i ] ) );
            } catch ( ... ) {
                return false;
            }
        }
        else if ( const auto stringIt = strings.find( key ); stringIt != strings.end() ) {
            const std::wstring value = argv[ ++i ];
            stringIt->second->clear();
            for ( const auto character : value ) {
                if ( character < 0 || character > 0x7f )
                    return false;
                stringIt->second->push_back( static_cast<char>( character ) );
            }
        }
        else if ( const auto pathIt = paths.find( key ); pathIt != paths.end() ) {
            *pathIt->second = fs::path( argv[ ++i ] );
        }
        else
            return false;
    }
    return result.waitPid > 0 && !result.mode.empty() && !result.current.empty()
           && !result.staged.empty() && !result.relaunch.empty() && !result.log.empty();
}
#else
bool parse( int argc, char** argv, Arguments& result )
{
    std::map<std::string, std::string*> strings{ { "--mode", &result.mode },
                                                 { "--token", &result.token } };
    std::map<std::string, fs::path*> paths{
        { "--current", &result.current },     { "--staged", &result.staged },
        { "--backup", &result.backup },       { "--relaunch", &result.relaunch },
        { "--ack", &result.acknowledgement }, { "--log", &result.log }
    };
    for ( int i = 1; i < argc; ++i ) {
        const std::string key = argv[ i ];
        if ( i + 1 >= argc )
            return false;
        if ( key == "--wait-pid" ) {
            try {
                result.waitPid = std::stoll( argv[ ++i ] );
            } catch ( ... ) {
                return false;
            }
        }
        else if ( const auto stringIt = strings.find( key ); stringIt != strings.end() ) {
            *stringIt->second = argv[ ++i ];
        }
        else if ( const auto pathIt = paths.find( key ); pathIt != paths.end() ) {
            *pathIt->second = fs::u8path( argv[ ++i ] );
        }
        else
            return false;
    }
    return result.waitPid > 0 && !result.mode.empty() && !result.current.empty()
           && !result.staged.empty() && !result.relaunch.empty() && !result.log.empty();
}
#endif

void waitForProcess( long long pid )
{
    log( "Waiting for CILogg process " + std::to_string( pid ) );
#ifdef _WIN32
    const auto process = OpenProcess( SYNCHRONIZE, FALSE, static_cast<DWORD>( pid ) );
    if ( process ) {
        WaitForSingleObject( process, 120000 );
        CloseHandle( process );
    }
#else
    for ( int i = 0; i < 1200 && kill( static_cast<pid_t>( pid ), 0 ) == 0; ++i )
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
#endif
    log( "Wait-PID branch completed" );
}

#ifdef _WIN32
std::wstring quote( const fs::path& value )
{
    return L"\"" + value.wstring() + L"\"";
}

int runElevated( const fs::path& executable, const std::wstring& parameters )
{
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof( info );
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_SHOWNORMAL;
    if ( !ShellExecuteExW( &info ) )
        return static_cast<int>( GetLastError() );
    WaitForSingleObject( info.hProcess, INFINITE );
    DWORD code = 1;
    GetExitCodeProcess( info.hProcess, &code );
    CloseHandle( info.hProcess );
    return static_cast<int>( code );
}

bool launch( const Arguments& args )
{
    std::wstring command = quote( args.relaunch ) + L" --cilogg-update-token "
                           + std::wstring( args.token.begin(), args.token.end() )
                           + L" --cilogg-update-ack " + quote( args.acknowledgement );
    STARTUPINFOW startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof( startup );
    std::vector<wchar_t> mutableCommand( command.begin(), command.end() );
    mutableCommand.push_back( L'\0' );
    const bool ok
        = CreateProcessW( nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
                          args.relaunch.parent_path().c_str(), &startup, &process );
    if ( ok ) {
        CloseHandle( process.hThread );
        CloseHandle( process.hProcess );
    }
    return ok;
}
#else
int runProcess( const std::vector<std::string>& command )
{
    const auto pid = fork();
    if ( pid == 0 ) {
        std::vector<char*> arguments;
        for ( const auto& part : command )
            arguments.push_back( const_cast<char*>( part.c_str() ) );
        arguments.push_back( nullptr );
        execv( arguments.front(), arguments.data() );
        _exit( 127 );
    }
    if ( pid < 0 )
        return errno;
    int status = 1;
    waitpid( pid, &status, 0 );
    return WIFEXITED( status ) ? WEXITSTATUS( status ) : 1;
}

bool launch( const Arguments& args )
{
    const auto pid = fork();
    if ( pid == 0 ) {
        const auto relaunch = args.relaunch.string();
        const auto ack = args.acknowledgement.string();
        execl( relaunch.c_str(), relaunch.c_str(), "--cilogg-update-token", args.token.c_str(),
               "--cilogg-update-ack", ack.c_str(), static_cast<char*>( nullptr ) );
        _exit( 127 );
    }
    return pid > 0;
}

#ifdef __APPLE__
std::string shellDoubleQuote( const fs::path& path )
{
    std::string value = path.string();
    std::string escaped;
    for ( const auto character : value ) {
        if ( character == '\\' || character == '"' || character == '$' || character == '`' )
            escaped.push_back( '\\' );
        escaped.push_back( character );
    }
    return "\"" + escaped + "\"";
}

std::string appleScriptQuote( const std::string& command )
{
    std::string escaped;
    for ( const auto character : command ) {
        if ( character == '\\' || character == '"' )
            escaped.push_back( '\\' );
        escaped.push_back( character );
    }
    return "do shell script \"" + escaped + "\" with administrator privileges";
}

bool elevatedSwap( const Arguments& args, bool restore )
{
    std::string command;
    if ( restore ) {
        const auto failed = withAsciiSuffix( args.current, ".failed" );
        command = "/bin/rm -rf " + shellDoubleQuote( failed ) + " && /bin/mv "
                  + shellDoubleQuote( args.current ) + " " + shellDoubleQuote( failed )
                  + " && /bin/mv " + shellDoubleQuote( args.backup ) + " "
                  + shellDoubleQuote( args.current );
    }
    else {
        command = "/bin/rm -rf " + shellDoubleQuote( args.backup ) + " && /bin/mv "
                  + shellDoubleQuote( args.current ) + " " + shellDoubleQuote( args.backup )
                  + " && /bin/mv " + shellDoubleQuote( args.staged ) + " "
                  + shellDoubleQuote( args.current );
    }
    log( restore ? "Requesting administrator authorization for rollback"
                 : "Requesting administrator authorization for app replacement" );
    return runProcess( { "/usr/bin/osascript", "-e", appleScriptQuote( command ) } ) == 0;
}
#endif
#endif

bool replaceSelfContained( const Arguments& args )
{
#ifdef __APPLE__
    if ( args.mode == "macos-bundle" && access( args.current.parent_path().c_str(), W_OK ) != 0 ) {
        return elevatedSwap( args, false );
    }
#endif
    std::error_code error;
    auto effectiveStaged = args.staged;
    const auto sameVolumeStage
        = args.current.parent_path()
          / withAsciiSuffix( args.current.filename(), ".cilogg-stage-" + args.token );
    fs::remove_all( sameVolumeStage, error );
    error.clear();
    fs::rename( args.staged, sameVolumeStage, error );
    if ( error ) {
        error.clear();
        if ( fs::is_directory( args.staged ) ) {
            fs::copy( args.staged, sameVolumeStage,
                      fs::copy_options::recursive | fs::copy_options::copy_symlinks, error );
        }
        else {
            fs::copy_file( args.staged, sameVolumeStage, fs::copy_options::overwrite_existing,
                           error );
        }
    }
    if ( error ) {
        log( "Same-volume staging failed: " + error.message() );
        return false;
    }
    effectiveStaged = sameVolumeStage;
    log( "Same-volume staging completed" );
    fs::remove_all( args.backup, error );
    error.clear();
    fs::rename( args.current, args.backup, error );
    if ( error ) {
        log( "Backup rename failed: " + error.message() );
        return false;
    }
    fs::rename( effectiveStaged, args.current, error );
    if ( error ) {
        log( "Staged rename failed; restoring backup: " + error.message() );
        fs::rename( args.backup, args.current, error );
        return false;
    }
#ifndef _WIN32
    if ( args.mode == "appimage" ) {
        fs::permissions( args.current,
                         fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                         fs::perm_options::add, error );
        log( error ? "AppImage executable permission update failed"
                   : "AppImage permissions preserved" );
    }
#endif
    return true;
}

bool acknowledgementArrived( const Arguments& args )
{
    for ( int i = 0; i < 300; ++i ) {
        std::ifstream ack( args.acknowledgement );
        std::string token;
        std::getline( ack, token );
        if ( token == args.token )
            return true;
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }
    return false;
}

bool rollback( const Arguments& args )
{
    log( "Startup acknowledgement failed; beginning rollback" );
#ifdef __APPLE__
    if ( args.mode == "macos-bundle" && access( args.current.parent_path().c_str(), W_OK ) != 0 ) {
        if ( !elevatedSwap( args, true ) )
            return false;
        log( "Privileged rollback restored previous version" );
        return launch( args );
    }
#endif
    std::error_code error;
    const auto failed = withAsciiSuffix( args.current, ".failed" );
    fs::remove_all( failed, error );
    error.clear();
    fs::rename( args.current, failed, error );
    if ( error )
        log( "Failed-version rename failed: " + error.message() );
    error.clear();
    fs::rename( args.backup, args.current, error );
    if ( error ) {
        log( "Rollback restore failed: " + error.message() );
        return false;
    }
    log( "Rollback restored previous version" );
    return launch( args );
}

int installSystemPackage( const Arguments& args )
{
#ifdef _WIN32
    const auto installDir = args.current.parent_path();
    const auto parameters = L"/S /D=" + quote( installDir );
    log( "Launching NSIS setup with visible UAC" );
    return runElevated( args.staged, parameters );
#else
    if ( args.mode == "deb" ) {
        log( "Launching fixed-path pkexec apt-get install" );
        return runProcess(
            { "/usr/bin/pkexec", "/usr/bin/apt-get", "install", "-y", args.staged.string() } );
    }
    log( "Launching fixed-path pkexec dnf install" );
    return runProcess(
        { "/usr/bin/pkexec", "/usr/bin/dnf", "install", "-y", args.staged.string() } );
#endif
}
} // namespace

#ifdef _WIN32
int wmain( int argc, wchar_t** argv )
#else
int main( int argc, char** argv )
#endif
{
    Arguments args;
    if ( !parse( argc, argv, args ) ) {
        std::cerr << "Invalid cilogg_updater arguments\n";
        return 2;
    }
    logFile.open( args.log, std::ios::app );
    log( "Updater helper started; mode=" + args.mode );
    waitForProcess( args.waitPid );
    std::error_code error;
    fs::remove( args.acknowledgement, error );

    const bool selfContained
        = args.mode == "windows-portable" || args.mode == "macos-bundle" || args.mode == "appimage";
    if ( !selfContained ) {
        const auto code = installSystemPackage( args );
        log( "System installer completed with exit code " + std::to_string( code ) );
        if ( code != 0 )
            return code;
        return launch( args ) ? 0 : 3;
    }
    if ( !replaceSelfContained( args ) )
        return 4;
    if ( !launch( args ) ) {
        log( "New-version launch failed immediately" );
        return rollback( args ) ? 0 : 5;
    }
    if ( acknowledgementArrived( args ) ) {
        log( "New version acknowledged startup; backup retained for this transaction" );
        return 0;
    }
    log( "New version did not acknowledge startup before timeout" );
    return rollback( args ) ? 0 : 6;
}
