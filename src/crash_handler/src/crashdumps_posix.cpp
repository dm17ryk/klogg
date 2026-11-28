// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// R06b on Linux and macOS: fatal signals write a crash record with the stack (no minidump; a core
// dump when the system keeps one). Throw sites come from throwsite_posix.cpp.

#if !defined( _WIN32 )

#include "crashdumps_platform.h"

#include <QFile>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>

#include <time.h>

#include <cxxabi.h>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

namespace cilogg::diagnostics::detail {
namespace {

char nativeFolder[ PathCapacity ] = {};
char lastRecord[ PathCapacity ] = {};
alignas( 16 ) char alternateStack[ 64 * 1024 ];
std::atomic_bool crashDone{ false };
constexpr int CrashWaitSteps = 300; // 100 ms each

void writeAll( int file, const char* text )
{
    auto length = std::strlen( text );
    while ( length > 0 ) {
        const auto written = ::write( file, text, length );
        if ( written <= 0 ) {
            return;
        }
        text += written;
        length -= static_cast<size_t>( written );
    }
}

const char* signalName( int number )
{
    switch ( number ) {
    case SIGSEGV:
        return "segmentation fault (SIGSEGV)";
    case SIGBUS:
        return "bus error (SIGBUS)";
    case SIGFPE:
        return "arithmetic error (SIGFPE)";
    case SIGILL:
        return "illegal instruction (SIGILL)";
    case SIGABRT:
        return "abort (SIGABRT)";
    default:
        return "fatal signal";
    }
}

// A thread that crashes while another one writes the record waits for it (bounded), so the
// process does not end half-way through the record. nanosleep is async-signal-safe.
void waitForOtherCrash()
{
    const timespec step{ 0, 100 * 1000 * 1000 };
    for ( int waited = 0; !crashDone.load() && waited < CrashWaitSteps; ++waited ) {
        nanosleep( &step, nullptr );
    }
}

void writeRecord( const char* reason, const void* address, bool inSignalHandler );

void writeCrash( const char* reason, const void* address, bool inSignalHandler )
{
    if ( crashing.exchange( true ) ) {
        waitForOtherCrash();
        return;
    }
    if ( folderReady.load() ) {
        writeRecord( reason, address, inSignalHandler );
    }
    crashDone = true;
}

void writeRecord( const char* reason, const void* address, bool inSignalHandler )
{
    char recordPath[ PathCapacity ] = {};
    append( recordPath, PathCapacity, nativeFolder );
    append( recordPath, PathCapacity, "/crash-" );
    appendStamp( recordPath, PathCapacity );
    append( recordPath, PathCapacity, "-" );
    appendNumber( recordPath, PathCapacity, static_cast<unsigned long long>( getpid() ) );
    append( recordPath, PathCapacity, ".txt" );
    const auto file = ::open( recordPath, O_WRONLY | O_CREAT | O_TRUNC, 0600 );
    if ( file < 0 ) {
        return;
    }
    char text[ 2048 ] = "CILogg crash\nreason: ";
    append( text, sizeof( text ), reason );
    append( text, sizeof( text ), "\ntime: " );
    appendStamp( text, sizeof( text ) );
    append( text, sizeof( text ), " UTC\n" );
    append( text, sizeof( text ), header );
    append( text, sizeof( text ), "pid: " );
    appendNumber( text, sizeof( text ), static_cast<unsigned long long>( getpid() ) );
    if ( address != nullptr ) {
        append( text, sizeof( text ), "\naddress: 0x" );
        appendNumber( text, sizeof( text ), reinterpret_cast<unsigned long long>( address ), 16 );
    }
    rlimit core{};
    append( text, sizeof( text ), "\ncore dump limit: " );
    if ( getrlimit( RLIMIT_CORE, &core ) == 0 ) {
        if ( core.rlim_cur == RLIM_INFINITY ) {
            append( text, sizeof( text ), "unlimited" );
        }
        else {
            appendNumber( text, sizeof( text ), static_cast<unsigned long long>( core.rlim_cur ) );
        }
    }
    append( text, sizeof( text ), "\ndump: none\nstack:\n" );
    writeAll( file, text );
    void* frames[ CrashFrames ] = {};
    const auto count = backtrace( frames, CrashFrames );
    backtrace_symbols_fd( frames, count, file );
#if defined( __APPLE__ )
    // macOS allocates a thread's thread_local block on first use: not inside a signal handler.
    const bool readThrowSite = !inSignalHandler;
#else
    static_cast<void>( inSignalHandler );
    const bool readThrowSite = true;
#endif
    if ( readThrowSite ) {
        const auto& thrown = throwSite();
        if ( thrown.count > 1 ) {
            writeAll( file, "last C++ throw on this thread:\n" );
            backtrace_symbols_fd( thrown.frames + 1, thrown.count - 1, file );
        }
    }
    ::close( file );
    lastRecord[ 0 ] = 0;
    append( lastRecord, PathCapacity, recordPath );
}

void onFatalSignal( int number, siginfo_t* info, void* )
{
    writeCrash( signalName( number ), info != nullptr ? info->si_addr : nullptr, true );
    // SA_RESETHAND restored the default action: end the process as the system would have.
    ::raise( number );
}

// "module(function+0x12) [0x...]" with the C++ name demangled.
QString demangledFrame( const char* raw )
{
    auto frame = QString::fromLocal8Bit( raw );
    const auto open = frame.indexOf( QLatin1Char( '(' ) );
    const auto plus = frame.indexOf( QLatin1Char( '+' ), open + 1 );
    if ( open < 0 || plus <= open + 1 ) {
        return frame;
    }
    const auto mangled = frame.mid( open + 1, plus - open - 1 ).toLocal8Bit();
    int status = 0;
    char* name = abi::__cxa_demangle( mangled.constData(), nullptr, nullptr, &status );
    if ( status == 0 && name != nullptr ) {
        frame.replace( open + 1, plus - open - 1, QString::fromLocal8Bit( name ) );
    }
    std::free( name );
    return frame;
}

} // namespace

void installPlatformHandlers()
{
    stack_t stack{};
    stack.ss_sp = alternateStack;
    stack.ss_size = sizeof( alternateStack );
    // A stack overflow of this thread (the main thread) still reaches the handler; worker threads
    // have no alternate stack, so their stack overflow ends the process without a record.
    sigaltstack( &stack, nullptr );
    // backtrace() loads its unwinder on first use: do that now, not inside a crash.
    void* frames[ 1 ];
    backtrace( frames, 1 );
    struct sigaction action{};
    action.sa_sigaction = onFatalSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset( &action.sa_mask );
    for ( const auto number : { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT } ) {
        sigaction( number, &action, nullptr );
    }
}

void writeCrashNow( const char* reason )
{
    writeCrash( reason, nullptr, false );
}

void resetCrashStateForTest()
{
    crashing = false;
    crashDone = false;
}

QString lastRecordPath()
{
    return QFile::decodeName( lastRecord );
}

void setNativeFolder( const QString& folder )
{
    const auto native = QFile::encodeName( folder );
    nativeFolder[ 0 ] = 0;
    append( nativeFolder, PathCapacity, native.constData() );
}

QStringList formatThrowFrames( const ThrowSite& site )
{
    QStringList frames;
    if ( site.count <= 1 ) {
        return frames;
    }
    // Frame 0 is __cxa_throw itself.
    char** symbols = backtrace_symbols( site.frames + 1, site.count - 1 );
    if ( symbols == nullptr ) {
        return frames;
    }
    for ( int index = 0; index < site.count - 1; ++index ) {
        frames.push_back( demangledFrame( symbols[ index ] ) );
    }
    std::free( symbols );
    return frames;
}

bool writeProcessMinidump( const QString&, bool, QString* error )
{
    if ( error != nullptr ) {
        *error = QStringLiteral( "CILogg writes minidumps on Windows only." );
    }
    return false;
}

} // namespace cilogg::diagnostics::detail

#endif
