// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal (R06b): what the platform crash paths share. The crash path runs in a broken process:
// it works from fixed buffers that CrashDumps::setFolder() fills and avoids the heap and locks
// where it can. That is best effort: on Windows starting the helper thread and resolving modules
// take the loader lock, and backtrace() takes the dynamic linker's lock on Linux.

#ifndef KLOGG_CRASHDUMPS_PLATFORM_H
#define KLOGG_CRASHDUMPS_PLATFORM_H

#include "throwsite.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <cstddef>
#include <ctime>

namespace cilogg::diagnostics::detail {

constexpr int PathCapacity = 1024;
constexpr int CrashFrames = 64;

extern std::atomic_bool fullMemoryFlag;
extern std::atomic_bool crashing; // one crash record per process
extern std::atomic_bool folderReady;
extern char header[ 512 ]; // the version and commit lines of a crash record

// ------------------------------------------------------------------ small text helpers

// Appends text to a fixed buffer, always NUL-terminated, never past its capacity.
template <typename Char>
inline void append( Char* out, size_t capacity, const Char* text )
{
    size_t used = 0;
    while ( used < capacity && out[ used ] != 0 ) {
        ++used;
    }
    while ( used + 1 < capacity && *text != 0 ) {
        out[ used++ ] = *text++;
    }
    if ( used < capacity ) {
        out[ used ] = 0;
    }
}

template <typename Char>
inline void appendNumber( Char* out, size_t capacity, unsigned long long value, int base = 10,
                          int width = 0 )
{
    Char digits[ 32 ] = {};
    int at = 30;
    int written = 0;
    do {
        const auto digit = static_cast<int>( value % static_cast<unsigned>( base ) );
        digits[ at-- ] = static_cast<Char>( digit < 10 ? '0' + digit : 'a' + digit - 10 );
        value /= static_cast<unsigned>( base );
        ++written;
    } while ( ( value != 0 || written < width ) && at >= 0 );
    append( out, capacity, digits + at + 1 );
}

// UTC time as yyyyMMdd-HHmmss, computed from time() alone: gmtime_r takes a lock (tzset), which a
// crash handler must not. Days to a civil date: H. Hinnant's days_from_civil inverse.
template <typename Char>
inline void appendStamp( Char* out, size_t capacity )
{
    const auto seconds = static_cast<long long>( std::time( nullptr ) );
    const auto days = seconds / 86400;
    const auto daySecond = seconds % 86400;
    const auto shifted = days + 719468;
    const auto era = shifted / 146097;
    const auto dayOfEra = shifted - era * 146097;
    const auto yearOfEra
        = ( dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096 ) / 365;
    const auto dayOfYear = dayOfEra - ( 365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100 );
    const auto monthIndex = ( 5 * dayOfYear + 2 ) / 153;
    const auto day = dayOfYear - ( 153 * monthIndex + 2 ) / 5 + 1;
    const auto month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
    const auto year = yearOfEra + era * 400 + ( month <= 2 ? 1 : 0 );
    appendNumber( out, capacity, static_cast<unsigned long long>( year ), 10, 4 );
    appendNumber( out, capacity, static_cast<unsigned long long>( month ), 10, 2 );
    appendNumber( out, capacity, static_cast<unsigned long long>( day ), 10, 2 );
    const Char dash[] = { '-', 0 };
    append( out, capacity, dash );
    appendNumber( out, capacity, static_cast<unsigned long long>( daySecond / 3600 ), 10, 2 );
    appendNumber( out, capacity, static_cast<unsigned long long>( daySecond % 3600 / 60 ), 10, 2 );
    appendNumber( out, capacity, static_cast<unsigned long long>( daySecond % 60 ), 10, 2 );
}

// Per platform (crashdumps_windows.cpp, crashdumps_posix.cpp).
void installPlatformHandlers();
void writeCrashNow( const char* reason );
// Test hook: lets writeCrashForTest write another record in the same process.
void resetCrashStateForTest();
QString lastRecordPath();
void setNativeFolder( const QString& folder );
QStringList formatThrowFrames( const ThrowSite& site );
// A minidump of this process now (Windows); false and *error elsewhere.
bool writeProcessMinidump( const QString& path, bool fullMemory, QString* error );

} // namespace cilogg::diagnostics::detail

#endif
