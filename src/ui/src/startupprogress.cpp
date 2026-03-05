#include "startupprogress.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <utility>

namespace {
constexpr auto kAdvanceNotifyInterval = std::chrono::milliseconds( 40 );

struct StartupProgressData {
    StartupProgressState state;
    StartupProgress::Callback callback;
    std::chrono::steady_clock::time_point lastAdvanceNotification{};
    bool hasAdvanceNotification = false;
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
        data.lastAdvanceNotification = std::chrono::steady_clock::now();
        data.hasAdvanceNotification = true;
    }
    notifyCallback( currentCallback, state );
}

void StartupProgress::clearCallback()
{
    std::lock_guard<std::mutex> lock( startupProgressMutex() );
    auto& data = startupProgressData();
    data.callback = {};
    data.hasAdvanceNotification = false;
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
        data.lastAdvanceNotification = std::chrono::steady_clock::now();
        data.hasAdvanceNotification = true;
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
        data.lastAdvanceNotification = std::chrono::steady_clock::now();
        data.hasAdvanceNotification = true;
    }
    notifyCallback( callback, state );
}

void StartupProgress::advance( const QString& status, const QString& detail, int step )
{
    StartupProgressState state;
    Callback callback;
    bool shouldNotify = false;

    {
        std::lock_guard<std::mutex> lock( startupProgressMutex() );
        auto& data = startupProgressData();
        const auto previousStatus = data.state.status;
        const auto previousDetail = data.state.detail;
        const int increment = std::max( 1, step );
        const int nextValue = data.state.value + increment;
        const bool closeToTop = ( nextValue * 100 ) >= ( data.state.maximum * 85 );
        if ( nextValue >= data.state.maximum || closeToTop ) {
            const int currentRange = std::max( 1, data.state.maximum - data.state.minimum );
            const int headroom = std::max( increment * 6, std::max( 20, currentRange / 2 ) );
            data.state.maximum = nextValue + headroom;
        }

        data.state.value = nextValue;
        if ( !status.isEmpty() ) {
            data.state.status = status;
        }
        data.state.detail = detail;
        normalizeState( data.state );

        callback = data.callback;
        state = data.state;

        const auto now = std::chrono::steady_clock::now();
        const bool isComplete = data.state.value >= data.state.maximum;
        const bool intervalElapsed
            = !data.hasAdvanceNotification
              || ( now - data.lastAdvanceNotification ) >= kAdvanceNotifyInterval;
        const bool stageTextChanged
            = ( data.state.status != previousStatus ) || ( data.state.detail != previousDetail );

        shouldNotify = isComplete || intervalElapsed || stageTextChanged;
        if ( shouldNotify ) {
            data.lastAdvanceNotification = now;
            data.hasAdvanceNotification = true;
        }
    }

    if ( shouldNotify ) {
        notifyCallback( callback, state );
    }
}

void StartupProgress::complete( const QString& status, const QString& detail )
{
    StartupProgressState state;
    Callback callback;
    {
        std::lock_guard<std::mutex> lock( startupProgressMutex() );
        auto& data = startupProgressData();
        data.state.value = data.state.maximum;
        if ( !status.isEmpty() ) {
            data.state.status = status;
        }
        data.state.detail = detail;
        normalizeState( data.state );
        state = data.state;
        callback = data.callback;
        data.lastAdvanceNotification = std::chrono::steady_clock::now();
        data.hasAdvanceNotification = true;
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
        data.lastAdvanceNotification = std::chrono::steady_clock::now();
        data.hasAdvanceNotification = true;
    }
    notifyCallback( callback, state );
}

bool StartupProgress::isActive()
{
    std::lock_guard<std::mutex> lock( startupProgressMutex() );
    return static_cast<bool>( startupProgressData().callback );
}
