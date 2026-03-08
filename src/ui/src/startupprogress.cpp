#include "startupprogress.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <utility>

namespace {
constexpr auto kAdvanceNotifyInterval = std::chrono::milliseconds( 40 );

struct StartupProgressData {
    struct CallbackHolder {
        std::atomic_bool active{ true };
        StartupProgress::Callback callback;
    };

    StartupProgressState state;
    std::shared_ptr<CallbackHolder> callbackHolder;
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

std::recursive_mutex& callbackInvocationMutex()
{
    static std::recursive_mutex mutex;
    return mutex;
}

void normalizeState( StartupProgressState& state )
{
    if ( state.maximum <= state.minimum ) {
        state.maximum = state.minimum + 1;
    }
    state.value = std::clamp( state.value, state.minimum, state.maximum );
}

void notifyCallback( const std::shared_ptr<StartupProgressData::CallbackHolder>& callbackHolder,
                     const StartupProgressState& state )
{
    if ( !callbackHolder ) {
        return;
    }

    std::lock_guard<std::recursive_mutex> callbackGuard( callbackInvocationMutex() );
    if ( !callbackHolder->active.load( std::memory_order_acquire ) ) {
        return;
    }

    if ( callbackHolder->callback ) {
        callbackHolder->callback( state );
    }
}
} // namespace

void StartupProgress::setCallback( Callback callback )
{
    StartupProgressState state;
    std::shared_ptr<StartupProgressData::CallbackHolder> currentCallbackHolder;
    {
        std::lock_guard<std::mutex> lock( startupProgressMutex() );
        auto& data = startupProgressData();
        if ( data.callbackHolder ) {
            data.callbackHolder->active.store( false, std::memory_order_release );
        }

        if ( callback ) {
            auto callbackHolder = std::make_shared<StartupProgressData::CallbackHolder>();
            callbackHolder->callback = std::move( callback );
            data.callbackHolder = std::move( callbackHolder );
        }
        else {
            data.callbackHolder.reset();
        }
        normalizeState( data.state );
        state = data.state;
        currentCallbackHolder = data.callbackHolder;
        data.lastAdvanceNotification = std::chrono::steady_clock::now();
        data.hasAdvanceNotification = true;
    }
    notifyCallback( currentCallbackHolder, state );
}

void StartupProgress::clearCallback()
{
    std::lock_guard<std::recursive_mutex> callbackGuard( callbackInvocationMutex() );
    std::lock_guard<std::mutex> lock( startupProgressMutex() );
    auto& data = startupProgressData();
    if ( data.callbackHolder ) {
        data.callbackHolder->active.store( false, std::memory_order_release );
    }
    data.callbackHolder.reset();
    data.hasAdvanceNotification = false;
}

void StartupProgress::setRange( int minimum, int maximum )
{
    StartupProgressState state;
    std::shared_ptr<StartupProgressData::CallbackHolder> callbackHolder;
    {
        std::lock_guard<std::mutex> lock( startupProgressMutex() );
        auto& data = startupProgressData();
        data.state.minimum = minimum;
        data.state.maximum = maximum;
        normalizeState( data.state );
        state = data.state;
        callbackHolder = data.callbackHolder;
        data.lastAdvanceNotification = std::chrono::steady_clock::now();
        data.hasAdvanceNotification = true;
    }
    notifyCallback( callbackHolder, state );
}

void StartupProgress::setValue( int value, const QString& status, const QString& detail )
{
    StartupProgressState state;
    std::shared_ptr<StartupProgressData::CallbackHolder> callbackHolder;
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
        callbackHolder = data.callbackHolder;
        data.lastAdvanceNotification = std::chrono::steady_clock::now();
        data.hasAdvanceNotification = true;
    }
    notifyCallback( callbackHolder, state );
}

void StartupProgress::advance( const QString& status, const QString& detail, int step )
{
    StartupProgressState state;
    std::shared_ptr<StartupProgressData::CallbackHolder> callbackHolder;
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

        callbackHolder = data.callbackHolder;
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
        notifyCallback( callbackHolder, state );
    }
}

void StartupProgress::complete( const QString& status, const QString& detail )
{
    StartupProgressState state;
    std::shared_ptr<StartupProgressData::CallbackHolder> callbackHolder;
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
        callbackHolder = data.callbackHolder;
        data.lastAdvanceNotification = std::chrono::steady_clock::now();
        data.hasAdvanceNotification = true;
    }
    notifyCallback( callbackHolder, state );
}

void StartupProgress::message( const QString& status, const QString& detail )
{
    StartupProgressState state;
    std::shared_ptr<StartupProgressData::CallbackHolder> callbackHolder;
    {
        std::lock_guard<std::mutex> lock( startupProgressMutex() );
        auto& data = startupProgressData();
        if ( !status.isEmpty() ) {
            data.state.status = status;
        }
        data.state.detail = detail;
        normalizeState( data.state );
        state = data.state;
        callbackHolder = data.callbackHolder;
        data.lastAdvanceNotification = std::chrono::steady_clock::now();
        data.hasAdvanceNotification = true;
    }
    notifyCallback( callbackHolder, state );
}

bool StartupProgress::isActive()
{
    std::lock_guard<std::mutex> lock( startupProgressMutex() );
    return static_cast<bool>( startupProgressData().callbackHolder );
}
