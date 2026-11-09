// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backendthreadretirement.h"
#include <QLoggingCategory>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

Q_LOGGING_CATEGORY( backendRetirementLog, "cilogg.communication.retirement" )

BackendThreadRetirement::~BackendThreadRetirement()
{
#ifdef Q_OS_WIN
    if ( const auto handle = handle_.load( std::memory_order_acquire ) ) {
        CloseHandle( handle );
        qCDebug( backendRetirementLog, "Released thread completion witness" );
    }
#endif
}
bool BackendThreadRetirement::captureCurrentThread()
{
#ifdef Q_OS_WIN
    HANDLE handle = nullptr;
    if ( !DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle,
                           SYNCHRONIZE, FALSE, 0 ) ) {
        qCWarning( backendRetirementLog, "Thread witness failed error=%lu", GetLastError() );
        return false;
    }
    void* expected = nullptr;
    if ( !handle_.compare_exchange_strong( expected, handle, std::memory_order_release,
                                           std::memory_order_relaxed ) ) {
        CloseHandle( handle );
        qCWarning( backendRetirementLog, "Repeated thread witness capture rejected" );
        return false;
    }
#endif
    qCDebug( backendRetirementLog, "Captured existing worker thread completion witness" );
    return true;
}
bool BackendThreadRetirement::complete( bool wait ) const
{
#ifdef Q_OS_WIN
    const auto handle = handle_.load( std::memory_order_acquire );
    // Capture failure occurs before any device open; Qt completion is sufficient then.
    if ( !handle )
        return true;
    const auto result = WaitForSingleObject( handle, wait ? INFINITE : 0 );
    if ( result != WAIT_OBJECT_0 && result != WAIT_TIMEOUT )
        qCWarning( backendRetirementLog, "Thread witness wait failed result=%lu error=%lu", result,
                   GetLastError() );
    return result == WAIT_OBJECT_0;
#else
    Q_UNUSED( wait );
    return true; // The session also joins its QThread on these platforms.
#endif
}
