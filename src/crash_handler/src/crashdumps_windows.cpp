// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// R06b on Windows: unhandled exceptions, abort() and pure virtual calls write a crash record and a
// minidump; a vectored exception handler records where each C++ exception is thrown.
//
// The crashing thread may have almost no stack left (a stack overflow): it only captures its own
// frames into a static job and hands everything else to a helper thread with a fresh stack. The
// record is written before the dump, so a dump that hangs or is cut short still leaves a record.

#include "crashdumps_platform.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h first
#include <dbghelp.h>

namespace cilogg::diagnostics::detail {
namespace {

wchar_t nativeFolder[ PathCapacity ] = {};
wchar_t lastRecord[ PathCapacity ] = {};

thread_local ThrowSite windowsThrow;
constexpr DWORD MsvcExceptionCode = 0xE06D7363; // a C++ throw
constexpr DWORD CrashWaitMs = 5 * 60 * 1000;    // a full-memory dump can take minutes
constexpr DWORD ProcessDumpWaitMs = 30 * 1000;

constexpr auto SmallDump
    = static_cast<MINIDUMP_TYPE>( MiniDumpNormal | MiniDumpWithThreadInfo
                                  | MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData );
constexpr auto FullDump = static_cast<MINIDUMP_TYPE>(
    MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo | MiniDumpWithHandleData
    | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules );

// The one crash of this process (guarded by `crashing`), filled by the crashing thread.
struct CrashJob {
    const char* reason = nullptr; // fixed text, or null: named from `info`
    EXCEPTION_POINTERS* info = nullptr;
    DWORD threadId = 0;
    void* frames[ CrashFrames ] = {};
    int frameCount = 0;
    ThrowSite thrown; // the crashing thread's latest C++ throw
};
CrashJob crashJob;
std::atomic_bool crashDone{ false };
// The throw site std::terminate kept before it rethrew to name the exception.
ThrowSite frozenThrow;
std::atomic_bool throwFrozen{ false };

// The helper thread's buffers (static: nothing big on any stack).
wchar_t basePath[ PathCapacity ] = {};
wchar_t dumpPath[ PathCapacity ] = {};
wchar_t recordPath[ PathCapacity ] = {};
char reasonText[ 512 ] = {};
char recordText[ 4096 ] = {};

bool writeMinidumpFile( const wchar_t* path, EXCEPTION_POINTERS* info, DWORD threadId, bool full )
{
    const auto file = CreateFileW( path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( file == INVALID_HANDLE_VALUE ) {
        return false;
    }
    MINIDUMP_EXCEPTION_INFORMATION exception{ threadId, info, FALSE };
    const auto written = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), file, full ? FullDump : SmallDump,
        info != nullptr ? &exception : nullptr, nullptr, nullptr );
    CloseHandle( file );
    if ( !written ) {
        DeleteFileW( path );
    }
    return written != FALSE;
}

// "module.dll+0x1234" for a code address, as UTF-8.
void appendModuleOffset( char* out, size_t capacity, const void* address )
{
    HMODULE module = nullptr;
    wchar_t modulePath[ MAX_PATH ] = {};
    if ( GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                 | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             static_cast<LPCWSTR>( address ), &module )
         && GetModuleFileNameW( module, modulePath, MAX_PATH ) > 0 ) {
        const wchar_t* name = modulePath;
        for ( const wchar_t* at = modulePath; *at != 0; ++at ) {
            if ( *at == L'\\' || *at == L'/' ) {
                name = at + 1;
            }
        }
        char utf8[ MAX_PATH * 3 ] = {};
        WideCharToMultiByte( CP_UTF8, 0, name, -1, utf8, static_cast<int>( sizeof( utf8 ) - 1 ),
                             nullptr, nullptr );
        append( out, capacity, utf8 );
        append( out, capacity, "+0x" );
        appendNumber( out, capacity,
                      static_cast<unsigned long long>( reinterpret_cast<const char*>( address )
                                                       - reinterpret_cast<const char*>( module ) ),
                      16 );
        return;
    }
    append( out, capacity, "0x" );
    appendNumber( out, capacity, reinterpret_cast<unsigned long long>( address ), 16 );
}

void writeAll( HANDLE file, const char* text )
{
    DWORD written = 0;
    WriteFile( file, text, static_cast<DWORD>( std::strlen( text ) ), &written, nullptr );
}

void writeFrames( HANDLE file, void* const* frames, int count )
{
    for ( int index = 0; index < count; ++index ) {
        char line[ MAX_PATH * 3 + 64 ] = "  ";
        appendModuleOffset( line, sizeof( line ), frames[ index ] );
        append( line, sizeof( line ), "\n" );
        writeAll( file, line );
    }
}

const char* exceptionName( DWORD code )
{
    switch ( code ) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "access violation";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "integer division by zero";
    case EXCEPTION_IN_PAGE_ERROR:
        return "in-page error";
    case MsvcExceptionCode:
        return "unhandled C++ exception";
    default:
        return "unhandled exception";
    }
}

// The MSVC C++ exception record (x64/ARM64: image-relative offsets). Its parameters are the magic
// number, the thrown object, the ThrowInfo and the image base.
#pragma pack( push, 4 )
struct CatchableType {
    DWORD properties;
    DWORD typeDescriptor; // offset of a TypeDescriptor
    int memberDisplacement;
    int vbtableDisplacement;
    int vdisplacement;
    DWORD size;
    DWORD copyFunction;
};
struct CatchableTypeArray {
    int count;
    DWORD types[ 1 ]; // offsets of CatchableType
};
struct ThrowInfo {
    DWORD attributes;
    DWORD unwind;
    DWORD forwardCompat;
    DWORD catchableTypes; // offset of a CatchableTypeArray
};
#pragma pack( pop )
struct TypeDescriptor {
    const void* vftable;
    void* spare;
    char name[ 1 ];
};

// The std::exception inside an uncaught C++ exception, or null. No C++ objects here: it reads the
// record under SEH, in case it is not what it claims to be.
const std::exception* thrownStdException( const EXCEPTION_RECORD* record )
{
    __try {
        if ( record->ExceptionCode != MsvcExceptionCode || record->NumberParameters < 4 ) {
            return nullptr;
        }
        const auto base = static_cast<ULONG_PTR>( record->ExceptionInformation[ 3 ] );
        const auto* object = reinterpret_cast<const char*>( record->ExceptionInformation[ 1 ] );
        const auto* throwInfo
            = reinterpret_cast<const ThrowInfo*>( record->ExceptionInformation[ 2 ] );
        if ( base == 0 || object == nullptr || throwInfo == nullptr ) {
            return nullptr;
        }
        const auto* types
            = reinterpret_cast<const CatchableTypeArray*>( base + throwInfo->catchableTypes );
        for ( int index = 0; index < types->count && index < 64; ++index ) {
            const auto* type
                = reinterpret_cast<const CatchableType*>( base + types->types[ index ] );
            const auto* descriptor
                = reinterpret_cast<const TypeDescriptor*>( base + type->typeDescriptor );
            if ( std::strcmp( descriptor->name, ".?AVexception@std@@" ) == 0 ) {
                return reinterpret_cast<const std::exception*>( object + type->memberDisplacement );
            }
        }
    } __except ( EXCEPTION_EXECUTE_HANDLER ) {
    }
    return nullptr;
}

// Appends what() of an uncaught std::exception.
void appendExceptionText( char* out, size_t capacity, const std::exception* exception )
{
    const char* text = nullptr;
    __try {
        text = exception->what();
        if ( text != nullptr ) {
            append( out, capacity, text );
        }
    } __except ( EXCEPTION_EXECUTE_HANDLER ) {
    }
}

// The reason line: the fixed text, or the exception (with the text of an uncaught std::exception,
// which reaches the unhandled-exception filter rather than std::terminate).
void formatReason( char* out, size_t capacity, const CrashJob& job )
{
    if ( job.reason != nullptr ) {
        append( out, capacity, job.reason );
        return;
    }
    const auto* record = job.info != nullptr ? job.info->ExceptionRecord : nullptr;
    const auto code = record != nullptr ? record->ExceptionCode : 0;
    if ( const auto* exception = record != nullptr ? thrownStdException( record ) : nullptr ) {
        append( out, capacity, "uncaught exception: " );
        appendExceptionText( out, capacity, exception );
    }
    else {
        append( out, capacity, exceptionName( code ) );
    }
    append( out, capacity, " (0x" );
    appendNumber( out, capacity, code, 16, 8 );
    append( out, capacity, ")" );
}

// The record (first) and the dump of crashJob, on the helper thread.
DWORD WINAPI crashThread( void* )
{
    const auto& job = crashJob;
    const bool full = fullMemoryFlag.load();
    reasonText[ 0 ] = 0;
    formatReason( reasonText, sizeof( reasonText ), job );

    basePath[ 0 ] = 0;
    append( basePath, PathCapacity, nativeFolder );
    append( basePath, PathCapacity, L"\\crash-" );
    appendStamp( basePath, PathCapacity );
    append( basePath, PathCapacity, L"-" );
    appendNumber( basePath, PathCapacity, GetCurrentProcessId() );
    dumpPath[ 0 ] = 0;
    append( dumpPath, PathCapacity, basePath );
    append( dumpPath, PathCapacity, L".dmp" );
    recordPath[ 0 ] = 0;
    append( recordPath, PathCapacity, basePath );
    append( recordPath, PathCapacity, L".txt" );

    const auto file = CreateFileW( recordPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( file != INVALID_HANDLE_VALUE ) {
        recordText[ 0 ] = 0;
        auto* text = recordText;
        const auto size = sizeof( recordText );
        append( text, size, "CILogg crash\nreason: " );
        append( text, size, reasonText );
        append( text, size, "\ntime: " );
        appendStamp( text, size );
        append( text, size, " UTC\n" );
        append( text, size, header );
        append( text, size, "pid: " );
        appendNumber( text, size, GetCurrentProcessId() );
        append( text, size, "\nthread: " );
        appendNumber( text, size, job.threadId );
        if ( job.info != nullptr && job.info->ExceptionRecord != nullptr ) {
            append( text, size, "\nexception: 0x" );
            appendNumber( text, size, job.info->ExceptionRecord->ExceptionCode, 16, 8 );
            append( text, size, "\naddress: " );
            appendModuleOffset( text, size, job.info->ExceptionRecord->ExceptionAddress );
        }
        append( text, size, full ? "\ndump: full memory" : "\ndump: small (threads and modules)" );
        append( text, size, "\nstack:\n" );
        writeAll( file, text );
        writeFrames( file, job.frames, job.frameCount );
        if ( job.thrown.count > 0 ) {
            writeAll( file, "last C++ throw on this thread:\n" );
            writeFrames( file, job.thrown.frames, job.thrown.count );
        }
        FlushFileBuffers( file );
    }
    // The dump last: it can take long, and it may not finish.
    const auto written = writeMinidumpFile( dumpPath, job.info, job.threadId, full );
    if ( file != INVALID_HANDLE_VALUE ) {
        writeAll( file, written ? "dump result: written\n" : "dump result: failed\n" );
        CloseHandle( file );
        lastRecord[ 0 ] = 0;
        append( lastRecord, PathCapacity, recordPath );
    }
    return 0;
}

// A thread that crashes while another one writes the record waits for it (bounded), so the
// process does not end half-way through the record.
void waitForOtherCrash()
{
    for ( DWORD waited = 0; !crashDone.load() && waited < CrashWaitMs; waited += 100 ) {
        Sleep( 100 );
    }
}

void writeCrash( const char* reason, EXCEPTION_POINTERS* info )
{
    if ( crashing.exchange( true ) ) {
        waitForOtherCrash();
        return;
    }
    if ( folderReady.load() ) {
        crashJob.reason = reason;
        crashJob.info = info;
        crashJob.threadId = GetCurrentThreadId();
        crashJob.frameCount
            = static_cast<int>( CaptureStackBackTrace( 0, CrashFrames, crashJob.frames, nullptr ) );
        crashJob.thrown = throwFrozen.load() ? frozenThrow : windowsThrow;
        if ( const auto thread
             = CreateThread( nullptr, 256 * 1024, crashThread, nullptr, 0, nullptr ) ) {
            WaitForSingleObject( thread, CrashWaitMs );
            CloseHandle( thread );
        }
        else {
            crashThread( nullptr ); // no thread: try on this one
        }
    }
    crashDone = true;
}

LONG WINAPI onUnhandledException( EXCEPTION_POINTERS* info )
{
    writeCrash( nullptr, info );
    return EXCEPTION_CONTINUE_SEARCH; // Windows ends the process as before
}

void onAbortSignal( int )
{
    writeCrash( "abort", nullptr );
}

void onPureCall()
{
    writeCrash( "pure virtual function call", nullptr );
    std::abort();
}

LONG CALLBACK onFirstChance( EXCEPTION_POINTERS* info )
{
    if ( info != nullptr && info->ExceptionRecord != nullptr
         && info->ExceptionRecord->ExceptionCode == MsvcExceptionCode ) {
        windowsThrow.count = static_cast<int>(
            CaptureStackBackTrace( 1, ThrowSite::Capacity, windowsThrow.frames, nullptr ) );
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Symbols for a throw-site frame when a PDB is at hand (a development build); dbghelp is not
// thread-safe, hence the lock.
QString symbolFor( void* address )
{
    static QMutex mutex;
    static bool initialized = false;
    QMutexLocker locker( &mutex );
    if ( !initialized ) {
        SymSetOptions( SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS );
        initialized = SymInitialize( GetCurrentProcess(), nullptr, TRUE ) != FALSE;
        if ( !initialized ) {
            return {};
        }
    }
    alignas( SYMBOL_INFO ) char buffer[ sizeof( SYMBOL_INFO ) + 512 ] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>( buffer );
    symbol->SizeOfStruct = sizeof( SYMBOL_INFO );
    symbol->MaxNameLen = 511;
    DWORD64 displacement = 0;
    if ( !SymFromAddr( GetCurrentProcess(), reinterpret_cast<DWORD64>( address ), &displacement,
                       symbol ) ) {
        return {};
    }
    return QString::fromLocal8Bit( symbol->Name );
}

// A minidump of the live process, written by a helper thread: MiniDumpWriteDump suspends the other
// threads, and one that holds a heap or loader lock could block it. The job outlives a timeout.
struct ProcessDumpJob {
    std::wstring path;
    bool full = false;
    DWORD callerThread = 0;
    bool written = false;
    DWORD error = 0;
};

DWORD WINAPI processDumpThread( void* parameter )
{
    auto* job = static_cast<ProcessDumpJob*>( parameter );
    job->written = writeMinidumpFile( job->path.c_str(), nullptr, job->callerThread, job->full );
    job->error = job->written ? 0 : GetLastError();
    return 0;
}

} // namespace

void installPlatformHandlers()
{
    SetUnhandledExceptionFilter( onUnhandledException );
    std::signal( SIGABRT, onAbortSignal );
    _set_purecall_handler( onPureCall );
}

void writeCrashNow( const char* reason )
{
    writeCrash( reason, nullptr );
}

void freezeThrowSite()
{
    frozenThrow = windowsThrow;
    throwFrozen = true;
}

void resetCrashStateForTest()
{
    crashing = false;
    crashDone = false;
    throwFrozen = false;
}

QString lastRecordPath()
{
    return QString::fromWCharArray( lastRecord );
}

void setNativeFolder( const QString& folder )
{
    const auto native = QDir::toNativeSeparators( folder ).toStdWString();
    nativeFolder[ 0 ] = 0;
    append( nativeFolder, PathCapacity, native.c_str() );
}

QStringList formatThrowFrames( const ThrowSite& site )
{
    QStringList frames;
    for ( int index = 0; index < site.count; ++index ) {
        char text[ MAX_PATH * 3 + 32 ] = {};
        appendModuleOffset( text, sizeof( text ), site.frames[ index ] );
        auto frame = QString::fromUtf8( text );
        // The frames that raise the exception (ntdll, KernelBase, the C++ runtime) come first:
        // start after the runtime's _CxxThrowException.
        if ( frame.startsWith( QLatin1String( "VCRUNTIME" ), Qt::CaseInsensitive ) ) {
            frames.clear();
            continue;
        }
        const auto symbol = symbolFor( site.frames[ index ] );
        frames.push_back( symbol.isEmpty() ? frame : frame + QStringLiteral( " " ) + symbol );
    }
    return frames;
}

bool writeProcessMinidump( const QString& path, bool fullMemory, QString* error )
{
    auto* job = new ProcessDumpJob;
    job->path = QDir::toNativeSeparators( path ).toStdWString();
    job->full = fullMemory;
    job->callerThread = GetCurrentThreadId();
    const auto thread = CreateThread( nullptr, 0, processDumpThread, job, 0, nullptr );
    if ( thread == nullptr ) {
        const auto failure = GetLastError();
        delete job;
        if ( error != nullptr ) {
            *error = QStringLiteral( "Cannot start the dump thread (error %1)." ).arg( failure );
        }
        return false;
    }
    if ( WaitForSingleObject( thread, ProcessDumpWaitMs ) != WAIT_OBJECT_0 ) {
        CloseHandle( thread ); // the job stays allocated: the thread may still use it
        if ( error != nullptr ) {
            *error = QStringLiteral( "The minidump was not written within %1 s." )
                         .arg( ProcessDumpWaitMs / 1000 );
        }
        return false;
    }
    CloseHandle( thread );
    const auto written = job->written;
    if ( !written && error != nullptr ) {
        *error = QStringLiteral( "MiniDumpWriteDump failed (error %1)." ).arg( job->error );
    }
    delete job;
    return written;
}

void enableThrowSiteCapture()
{
    static std::atomic_bool added{ false };
    if ( !added.exchange( true ) ) {
        AddVectoredExceptionHandler( 0, onFirstChance ); // last: after any debugger hook
    }
}

const ThrowSite& throwSite()
{
    return windowsThrow;
}

} // namespace cilogg::diagnostics::detail
