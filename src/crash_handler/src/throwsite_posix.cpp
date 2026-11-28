// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// R06b throw-site stacks on Linux and macOS: CILogg's own __cxa_throw records the stack of the
// throwing thread and then calls the C++ runtime's __cxa_throw. Only C headers are included here,
// so no C++ header can declare __cxa_throw with another signature in this file.
//
// Linux: every throw goes through this function (the executable's definition interposes the one
// in libstdc++, also for Qt and the other shared libraries). macOS (two-level namespace): only the
// throws compiled into CILogg itself; a throw inside a system library is not recorded.

#if !defined( _WIN32 )

#include <dlfcn.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "throwsite.h"

namespace cilogg::diagnostics::detail {
namespace {
int captureEnabled = 0; // read with __atomic builtins
thread_local ThrowSite lastThrow;
} // namespace

void enableThrowSiteCapture()
{
    // backtrace() loads its unwinder on first use: do that now, not inside the first throw.
    void* frames[ 1 ];
    backtrace( frames, 1 );
    __atomic_store_n( &captureEnabled, 1, __ATOMIC_RELEASE );
}

const ThrowSite& throwSite()
{
    return lastThrow;
}
} // namespace cilogg::diagnostics::detail

extern "C" {
using CxaThrow = void ( * )( void*, void*, void ( * )( void* ) );

// `used`: nothing in CILogg calls it by name, and link-time optimization must keep it exported.
__attribute__( ( visibility( "default" ), used, noreturn ) ) void
__cxa_throw( void* thrown, void* type, void ( *destructor )( void* ) )
{
    using cilogg::diagnostics::detail::lastThrow;
    if ( __atomic_load_n( &cilogg::diagnostics::detail::captureEnabled, __ATOMIC_ACQUIRE ) ) {
        lastThrow.count
            = backtrace( lastThrow.frames, cilogg::diagnostics::detail::ThrowSite::Capacity );
    }
    static CxaThrow runtimeThrow = [] {
        // dlsym returns an object pointer; copy it, as a cast to a function pointer is not
        // portable.
        void* symbol = dlsym( RTLD_NEXT, "__cxa_throw" );
        CxaThrow function = nullptr;
        memcpy( &function, &symbol, sizeof( function ) );
        return function;
    }();
    if ( runtimeThrow == nullptr ) {
        fputs( "CILogg: the C++ runtime's __cxa_throw was not found\n", stderr );
        abort();
    }
    runtimeThrow( thrown, type, destructor );
    __builtin_unreachable();
}
}

#endif
