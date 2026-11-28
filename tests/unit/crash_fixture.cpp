// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// R06b test fixture: installs CILogg's crash handlers with <folder> and crashes as <mode> asks
// (segv, thread: segv on a worker thread, throw, noexcept: a throw out of a noexcept function,
// abort). crashdumps_test.cpp checks the crash
// record (and minidump) it leaves.

#include "crashdumps.h"

#include <QCoreApplication>

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace {
// Volatile, so the compiler cannot turn the write into a trap of its own.
int* volatile nullPointer = nullptr;

[[noreturn]] void throwUncaught()
{
    throw std::runtime_error( "fixture boom" );
}

// An exception that leaves a noexcept function calls std::terminate directly, on every platform.
void throwThroughNoexcept() noexcept
{
    throwUncaught();
}
} // namespace

int main( int argc, char** argv )
{
    // Also the minidump helper of the tests (CILogg itself is that helper in the app).
    if ( const auto code = cilogg::diagnostics::CrashDumps::runDumpHelper( argc, argv );
         code >= 0 ) {
        return code;
    }
    QCoreApplication app( argc, argv );
    if ( argc < 3 ) {
        return 2;
    }
#ifdef _WIN32
    // No Windows Error Reporting or abort() message box: the test runs unattended.
    SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX );
    _set_abort_behavior( 0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT );
#else
    // No core dump (a core_pattern pipe to apport or systemd-coredump could also stall the test).
    rlimit noCore{};
    setrlimit( RLIMIT_CORE, &noCore );
#endif
    cilogg::diagnostics::installThrowSiteCapture();
    if ( !cilogg::diagnostics::CrashDumps::install( QString::fromLocal8Bit( argv[ 1 ] ) ) ) {
        return 3;
    }
    const char* mode = argv[ 2 ];
    if ( std::strcmp( mode, "segv" ) == 0 ) {
        *nullPointer = 1;
    }
    else if ( std::strcmp( mode, "thread" ) == 0 ) {
        std::thread( [] { *nullPointer = 1; } ).join(); // a crash on a worker thread
    }
    else if ( std::strcmp( mode, "throw" ) == 0 ) {
        throwUncaught();
    }
    else if ( std::strcmp( mode, "noexcept" ) == 0 ) {
        throwThroughNoexcept();
    }
    else if ( std::strcmp( mode, "abort" ) == 0 ) {
        std::abort();
    }
    return 4;
}
