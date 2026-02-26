#include "startupprogress.h"

#include <algorithm>
#include <mutex>
#include <utility>

namespace {
struct StartupProgressData {
    StartupProgressState state;
    StartupProgress::Callback callback;
};

StartupProgressData& startupProgressData()
{
    static StartupProgressData data;
    return data;
}

std::mutex& startupProgressMutex()
{
    static std::mutex mutex;
    return mutex;
}

void normalizeState( StartupProgressState& state )
{
    if ( state.maximum <= state.minimum ) {
        state.maximum = state.minimum + 1;
    }
    state.value = std::clamp( state.value, state.minimum, state.maximum );
}

void notifyCallback( const StartupProgress::Callback& callback, const StartupProgressState& state )
{
    if ( callback ) {
        callback( state );
    }
}
} // namespace

void StartupProgress::setCallback( Callback callback )
{
    StartupProgressState state;
    Callback currentCallback;
    {
        std::lock_guard<std::mutex> lock( startupProgressMutex() );
        auto& data = startupProgressData();
        data.callback = std::move( callback );
        normalizeState( data.state );
        state = data.state;
        currentCallback = data.callback;
    }
    notifyCallback( currentCallback, state );
}

void StartupProgress::clearCallback()
{
    std::lock_guard<std::mutex> lock( startupProgressMutex() );
    startupProgressData().callback = {};
}

void StartupProgress::setRange( int minimum, int maximum )
{
    StartupProgressState state;
    Callback callback;
    {
        std::lock_guard<std::mutex> lock( startupProgressMutex() );
        auto& data = startupProgressData();
        data.state.minimum = minimum;
        data.state.maximum = maximum;
        normalizeState( data.state );
        state = data.state;
        callback = data.callback;
    }
    notifyCallback( callback, state );
}

void StartupProgress::setValue( int value, const QString& status, const QString& detail )
{
    StartupProgressState state;
    Callback callback;
    {
        std::lock_guard<std::mutex> lock( startupProgressMutex() );
        auto& data = startupProgressData();
        data.state.value = value;
        if ( !status.isEmpty() ) {
            data.state.status = status;
        }
        data.state.detail = detail;
        normalizeState( data.state );
        state = data.state;
        callback = data.callback;
    }
    notifyCallback( callback, state );
}

void StartupProgress::advance( const QString& status, const QString& detail, int step )
{
    StartupProgressState state;
    Callback callback;
    {
        std::lock_guard<std::mutex> lock( startupProgressMutex() );
        auto& data = startupProgressData();
        data.state.value += std::max( 1, step );
        if ( data.state.value > data.state.maximum ) {
            data.state.maximum = data.state.value;
        }
        if ( !status.isEmpty() ) {
            data.state.status = status;
        }
        data.state.detail = detail;
        normalizeState( data.state );
        state = data.state;
        callback = data.callback;
    }
    notifyCallback( callback, state );
}

void StartupProgress::message( const QString& status, const QString& detail )
{
    StartupProgressState state;
    Callback callback;
    {
        std::lock_guard<std::mutex> lock( startupProgressMutex() );
        auto& data = startupProgressData();
        if ( !status.isEmpty() ) {
            data.state.status = status;
        }
        data.state.detail = detail;
        normalizeState( data.state );
        state = data.state;
        callback = data.callback;
    }
    notifyCallback( callback, state );
}

bool StartupProgress::isActive()
{
    std::lock_guard<std::mutex> lock( startupProgressMutex() );
    return static_cast<bool>( startupProgressData().callback );
}
