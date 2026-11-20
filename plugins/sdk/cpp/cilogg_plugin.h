#pragma once
// Standalone C++17 SDK. Link Qt Core only; never link a CILogg host library.
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace cilogg_plugin {
constexpr int MaxMessage = 1024 * 1024;
constexpr int MaxPending = 8 * MaxMessage;
constexpr const char* SdkName = "cilogg_plugin.h";
constexpr const char* SdkVersion = "2.0.0";
class Server;

// A host query (docs/plugin-sdk-advanced.md section 5): a request from the host with a deadline.
// The handler's answer is sent only while expired() is false.
struct Query {
    QString id;
    QString method;
    QJsonObject params;
    std::chrono::steady_clock::time_point deadline;
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>( false );
    // The host stopped waiting (query/cancel) or the deadline passed.
    bool expired() const
    {
        return cancelled->load() || std::chrono::steady_clock::now() >= deadline;
    }
};
struct Job {
    QString id;
    QJsonObject context;
    std::atomic<bool> cancelled{ false };
    void check() const
    {
        if ( cancelled.load() ) {
            throw std::runtime_error( "Job cancelled" );
        }
    }
};

class Server {
public:
    using Action = std::function<QJsonObject( Job&, const QJsonObject& )>;
    explicit Server( const QString& manifestPath = "plugin.json" )
    {
        QFile file( manifestPath );
        if ( !file.open( QIODevice::ReadOnly ) || file.size() > MaxMessage ) {
            throw std::runtime_error( "Cannot read plugin manifest" );
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson( file.readAll(), &error );
        if ( !document.isObject() || error.error != QJsonParseError::NoError ) {
            throw std::runtime_error( "Invalid plugin manifest" );
        }
        manifest_ = document.object();
    }
    void action( const QString& name, Action callback )
    {
        actions_[ name ] = std::move( callback );
    }
    void notify( const QString& method, const QJsonObject& params )
    {
        send( { { "jsonrpc", "2.0" }, { "method", method }, { "params", params } } );
    }
    void progress( Job& job, double value )
    {
        job.check();
        notify( "progress", { { "job_id", job.id }, { "value", value } } );
    }
    void panel( Job& job, const QJsonObject& description )
    {
        job.check();
        notify( "panel/update", { { "job_id", job.id }, { "panel", description } } );
    }
    QJsonObject configuration() const
    {
        std::lock_guard<std::mutex> guard( mutex_ );
        return configuration_;
    }
    // Plugin views and message boxes (docs/plugin-views.md). All are notifications.
    using Notification = std::function<void( const QString& method, const QJsonObject& params )>;
    // Receives view/opened, view/closed, view/event and ui/message_result on the protocol
    // reader thread; keep the handler short or hand work to another thread.
    void onNotification( Notification callback )
    {
        notification_ = std::move( callback );
    }
    QJsonArray hostFeatures() const
    {
        std::lock_guard<std::mutex> guard( mutex_ );
        return hostFeatures_;
    }
    // Whether the host has `feature` in at least `version` (initialize `host_feature_versions`;
    // a host that only lists `host_features` has version 1 of each).
    bool supports( const QString& feature, int version = 1 ) const
    {
        std::lock_guard<std::mutex> guard( mutex_ );
        // `host_features` announces a feature; a version alone announces nothing.
        if ( !hostFeatures_.contains( feature ) ) {
            return false;
        }
        if ( hostFeatureVersions_.contains( feature ) ) {
            return hostFeatureVersions_.value( feature ).toInt() >= version;
        }
        return version <= 1;
    }
    QJsonObject hostLimits() const
    {
        std::lock_guard<std::mutex> guard( mutex_ );
        return hostLimits_;
    }
    QString hostVersion() const
    {
        std::lock_guard<std::mutex> guard( mutex_ );
        return hostVersion_;
    }
    // Host queries (docs/plugin-sdk-advanced.md section 5): `tooltip/resolve`, and later
    // `menu/populate`, `tree/children` and `field/complete`. Register before serve(); the methods
    // are announced in the initialize result's `features` (list them in plugin.json `handles`
    // too). Handlers run on per-method workers, never on the reader thread: tooltip/resolve and
    // field/complete on one worker where a newer waiting query replaces an older one,
    // tree/children on two, every other method on one. A handler returns the result (null for
    // "nothing"); an exception answers a JSON-RPC error.
    using QueryHandler = std::function<QJsonValue( const Query& query )>;
    void onQuery( const QString& method, QueryHandler handler )
    {
        std::lock_guard<std::mutex> guard( queryMutex_ );
        lanes_[ method ].handler = std::move( handler );
        lanes_[ method ].workers = method == "tree/children" ? 2 : 1;
        lanes_[ method ].latestWins = method == "tooltip/resolve" || method == "field/complete";
    }
    // tooltip/invalidate: drops the host's cached advanced tooltips of a view, an element, or some
    // rows. Does nothing on a host without the `tooltips` feature.
    void invalidateTooltips( const QString& viewId, const QString& elementId = {},
                             const QJsonArray& rowIds = {}, const QString& windowId = {} )
    {
        if ( !supports( "tooltips" ) ) {
            return;
        }
        QJsonObject params{ { "view_id", viewId } };
        if ( !elementId.isEmpty() ) {
            params.insert( "element_id", elementId );
        }
        if ( !rowIds.isEmpty() ) {
            params.insert( "row_ids", rowIds );
        }
        notify( "tooltip/invalidate", withWindow( params, windowId ) );
    }
    // Menus (docs/plugin-sdk-advanced.md section 7). `ui/command` arrives when the user picks an
    // `event` menu entry or presses its shortcut; the handler gets its params (origin, menu_id,
    // item_id, location, window_id, checked, context) on the protocol reader thread, so keep it
    // short. Registering one announces `ui/command` in `features` (list it in `handles` too).
    // `menu/populate` is a query: onQuery( "menu/populate", ... ) answers {items, cache_ms}.
    using CommandHandler = std::function<void( const QJsonObject& params )>;
    void onCommand( CommandHandler handler )
    {
        std::lock_guard<std::mutex> guard( queryMutex_ );
        command_ = std::move( handler );
    }
    // menu/update: changes declared entries ([{id, set: {title, icon, tooltip, enabled, visible,
    // checked}}]) in every window. Does nothing on a host without the `menus` feature.
    void updateMenu( const QString& menuId, const QJsonArray& items )
    {
        if ( !supports( "menus" ) ) {
            return;
        }
        notify( "menu/update", { { "menu_id", menuId }, { "items", items } } );
    }
    // ui/icons: registers images ({"<id>": {"png": "<base64>"} or {"svg": "<text>"}}) for
    // `image:<id>` icon references. Does nothing on a host without the `icons` feature.
    void registerIcons( const QJsonObject& icons, const QJsonArray& remove = {} )
    {
        if ( !supports( "icons" ) ) {
            return;
        }
        QJsonObject params{ { "icons", icons } };
        if ( !remove.isEmpty() ) {
            params.insert( "remove", remove );
        }
        notify( "ui/icons", params );
    }
    void updateView( const QString& viewId, const QJsonObject& root, const QString& windowId = {} )
    {
        notify( "view/update",
                withWindow( { { "view_id", viewId }, { "root", root } }, windowId ) );
    }
    void patchView( const QString& viewId, const QJsonArray& patch, const QString& windowId = {} )
    {
        notify( "view/update",
                withWindow( { { "view_id", viewId }, { "patch", patch } }, windowId ) );
    }
    void showView( const QString& viewId, QJsonObject options = {} )
    {
        options.insert( "view_id", viewId );
        notify( "view/show", options );
    }
    void hideView( const QString& viewId, const QString& windowId = {} )
    {
        notify( "view/hide", withWindow( { { "view_id", viewId } }, windowId ) );
    }
    void message( const QJsonObject& params )
    {
        notify( "ui/message", params );
    }
    void closeMessage( const QString& messageId )
    {
        notify( "ui/close_message", { { "message_id", messageId } } );
    }

    QString openStream( Job& job )
    {
        job.check();
        const auto id = QUuid::createUuid().toString( QUuid::WithoutBraces );
        {
            std::lock_guard<std::mutex> guard( mutex_ );
            if ( streams_.size() >= 128 ) {
                throw std::runtime_error( "Too many streams" );
            }
            streams_.emplace( id, StreamState{} );
        }
        try {
            notify( "stream/open", { { "job_id", job.id }, { "stream_id", id } } );
            std::unique_lock<std::mutex> lock( mutex_ );
            wait( lock, job,
                  [ & ] { return streams_.at( id ).opened || streams_.at( id ).closed; } );
            if ( streams_.at( id ).closed ) {
                throw std::runtime_error( "Stream stopped" );
            }
        } catch ( ... ) {
            closeStream( id );
            throw;
        }
        return id;
    }
    void writeStream( Job& job, const QString& id, const QJsonArray& records )
    {
        std::unique_lock<std::mutex> lock( mutex_ );
        auto& stream = streams_.at( id );
        job.check();
        if ( stream.closed ) {
            throw std::runtime_error( "Stream stopped" );
        }
        const auto sequence = stream.sequence;
        const QJsonObject params{ { "stream_id", id },
                                  { "sequence", static_cast<double>( sequence ) },
                                  { "records", records } };
        const auto size = QJsonDocument( params ).toJson( QJsonDocument::Compact ).size() + 128;
        if ( size > MaxMessage ) {
            throw std::runtime_error( "Stream batch exceeds message limit" );
        }
        wait( lock, job, [ & ] { return pendingBytes_ + size <= MaxPending; } );
        pendingBytes_ += size;
        stream.acknowledged = false;
        lock.unlock();
        try {
            notify( "stream/data", params );
            lock.lock();
            wait( lock, job, [ & ] { return stream.acknowledged || stream.closed; } );
            if ( stream.closed ) {
                throw std::runtime_error( "Stream stopped" );
            }
            ++stream.sequence;
        } catch ( ... ) {
            if ( !lock.owns_lock() ) {
                lock.lock();
            }
            pendingBytes_ -= size;
            condition_.notify_all();
            throw;
        }
        pendingBytes_ -= size;
        condition_.notify_all();
    }
    void closeStream( const QString& id )
    {
        notify( "stream/close", { { "stream_id", id } } );
        std::lock_guard<std::mutex> guard( mutex_ );
        streams_.erase( id );
    }
    void serve()
    {
        {
            // The host sends a query only when plugin.json lists it in `handles` too.
            std::lock_guard<std::mutex> guard( queryMutex_ );
            const auto handles = manifest_.value( "handles" ).toArray();
            for ( const auto& lane : lanes_ ) {
                if ( !handles.contains( lane.first ) ) {
                    std::cerr << "cilogg_plugin: " << lane.first.toStdString()
                              << " has a handler but is missing from handles in plugin.json"
                              << std::endl;
                }
            }
        }
        std::vector<char> buffer( MaxMessage + 2 );
        try {
            while ( !stopping_.load()
                    && std::cin.getline( buffer.data(),
                                         static_cast<std::streamsize>( buffer.size() ) ) ) {
                const auto count = std::cin.gcount() - 1;
                if ( count < 0 || count > MaxMessage ) {
                    throw std::runtime_error( "Invalid host frame length" );
                }
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(
                    QByteArray( buffer.data(), static_cast<int>( count ) ), &error );
                if ( !document.isObject() || error.error != QJsonParseError::NoError
                     || document.object().value( "jsonrpc" ) != "2.0" ) {
                    throw std::runtime_error( "Invalid host JSON-RPC" );
                }
                dispatch( document.object() );
                for ( auto it = workers_.begin(); it != workers_.end(); ) {
                    if ( it->wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready ) {
                        it->get();
                        it = workers_.erase( it );
                    }
                    else {
                        ++it;
                    }
                }
            }
        } catch ( const std::exception& error ) {
            std::cerr << error.what() << std::endl;
        }
        stopping_ = true;
        {
            // Waiting queries are dropped and running ones see expired(), so handlers that poll
            // it return at once instead of at their deadlines.
            std::lock_guard<std::mutex> guard( queryMutex_ );
            for ( auto& pending : pendingQueries_ ) {
                *pending.second->cancelled = true;
            }
            for ( auto& lane : lanes_ ) {
                lane.second.queue.clear();
            }
            queryCondition_.notify_all();
        }
        {
            std::lock_guard<std::mutex> guard( mutex_ );
            for ( auto& pair : jobs_ ) {
                pair.second->cancelled = true;
            }
            condition_.notify_all();
        }
        for ( auto& worker : workers_ ) {
            worker.get();
        }
        for ( auto& thread : queryThreads_ ) {
            thread.join();
        }
        queryThreads_.clear();
    }
    bool stopping() const
    {
        return stopping_.load();
    }

private:
    struct StreamState {
        bool opened = false;
        bool closed = false;
        bool acknowledged = false;
        quint64 sequence = 0;
    };
    static QJsonObject withWindow( QJsonObject params, const QString& windowId )
    {
        if ( !windowId.isEmpty() ) {
            params.insert( "window_id", windowId );
        }
        return params;
    }
    template <typename Predicate>
    void wait( std::unique_lock<std::mutex>& lock, Job& job, Predicate predicate )
    {
        while ( !predicate() ) {
            job.check();
            if ( stopping_ ) {
                throw std::runtime_error( "Plugin stopping" );
            }
            condition_.wait_for( lock, std::chrono::milliseconds( 100 ) );
        }
        job.check();
    }
    void send( const QJsonObject& frame )
    {
        const auto bytes = QJsonDocument( frame ).toJson( QJsonDocument::Compact );
        if ( bytes.size() > MaxMessage ) {
            throw std::runtime_error( "Message exceeds 1 MiB" );
        }
        std::lock_guard<std::mutex> guard( outputMutex_ );
        std::cout.write( bytes.constData(), bytes.size() );
        std::cout << '\n' << std::flush;
        if ( !std::cout ) {
            throw std::runtime_error( "Host pipe closed" );
        }
    }
    struct Lane {
        QueryHandler handler;
        int workers = 1;
        bool latestWins = false;
        int started = 0;
        std::deque<std::shared_ptr<Query>> queue;
    };
    bool hasQueryHandler( const QString& method )
    {
        std::lock_guard<std::mutex> guard( queryMutex_ );
        return lanes_.count( method ) > 0;
    }
    CommandHandler commandHandler()
    {
        std::lock_guard<std::mutex> guard( queryMutex_ );
        return command_;
    }
    QJsonArray queryFeatures()
    {
        std::lock_guard<std::mutex> guard( queryMutex_ );
        QJsonArray features;
        for ( const auto& lane : lanes_ ) {
            features.append( lane.first );
        }
        if ( !features.isEmpty() ) {
            features.append( QStringLiteral( "query/cancel" ) );
        }
        if ( command_ ) {
            features.append( QStringLiteral( "ui/command" ) );
        }
        return features;
    }
    void enqueueQuery( const QJsonObject& frame )
    {
        auto query = std::make_shared<Query>();
        query->id = frame.value( "id" ).toString();
        query->method = frame.value( "method" ).toString();
        query->params = frame.value( "params" ).toObject();
        const auto timeout = std::clamp( query->params.value( "timeout_ms" ).toInt( 5000 ), 1, 60000 );
        query->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( timeout );
        std::lock_guard<std::mutex> guard( queryMutex_ );
        auto& lane = lanes_.at( query->method );
        if ( lane.latestWins ) {
            for ( const auto& waiting : lane.queue ) {
                *waiting->cancelled = true; // replaced by a newer query: no answer
                pendingQueries_.erase( waiting->id );
            }
            lane.queue.clear();
        }
        lane.queue.push_back( query );
        pendingQueries_[ query->id ] = query;
        while ( lane.started < lane.workers ) {
            ++lane.started;
            queryThreads_.emplace_back( [ this, method = query->method ] { runLane( method ); } );
        }
        queryCondition_.notify_all();
    }
    void cancelQuery( const QString& id )
    {
        std::lock_guard<std::mutex> guard( queryMutex_ );
        const auto found = pendingQueries_.find( id );
        if ( found == pendingQueries_.end() ) {
            return;
        }
        *found->second->cancelled = true;
        auto& queue = lanes_.at( found->second->method ).queue;
        queue.erase( std::remove( queue.begin(), queue.end(), found->second ), queue.end() );
        pendingQueries_.erase( found );
    }
    void runLane( const QString& method )
    {
        for ( ;; ) {
            std::shared_ptr<Query> query;
            QueryHandler handler;
            {
                std::unique_lock<std::mutex> lock( queryMutex_ );
                auto& lane = lanes_.at( method );
                queryCondition_.wait( lock, [ & ] { return stopping_.load() || !lane.queue.empty(); } );
                if ( lane.queue.empty() ) {
                    return; // stopping
                }
                query = lane.queue.front();
                lane.queue.pop_front();
                handler = lane.handler;
            }
            // A query whose deadline passed before its handler started is dropped.
            if ( !query->expired() ) {
                QJsonObject frame{ { "jsonrpc", "2.0" }, { "id", query->id } };
                try {
                    const auto result = handler( *query );
                    // An undefined value would drop the key: an answer needs `result` or `error`.
                    frame.insert( "result",
                                  result.isUndefined() ? QJsonValue( QJsonValue::Null ) : result );
                } catch ( const std::exception& error ) {
                    frame.insert( "error", QJsonObject{ { "code", -32000 },
                                                        { "message", QString::fromUtf8( error.what() ) } } );
                } catch ( ... ) {
                    // Never std::terminate on a worker thread.
                    frame.insert( "error", QJsonObject{ { "code", -32000 },
                                                        { "message", "Query handler failed" } } );
                }
                if ( !query->expired() ) {
                    try {
                        send( frame );
                    } catch ( ... ) {
                        std::cerr << "Could not deliver a query answer" << std::endl;
                    }
                }
            }
            std::lock_guard<std::mutex> guard( queryMutex_ );
            pendingQueries_.erase( query->id );
        }
    }
    void dispatch( const QJsonObject& frame )
    {
        const auto method = frame.value( "method" ).toString();
        const auto params = frame.value( "params" ).toObject();
        const bool request = frame.contains( "id" );
        if ( method == "initialize" ) {
            if ( initialized_ || params.value( "protocol_version" ).toInt() != 1
                 || params.value( "plugin_id" ) != manifest_.value( "id" ) ) {
                throw std::runtime_error( "Incompatible initialization" );
            }
            initialized_ = true;
            {
                std::lock_guard<std::mutex> guard( mutex_ );
                configuration_ = params.value( "configuration" ).toObject();
                hostFeatures_ = params.value( "host_features" ).toArray();
                hostFeatureVersions_ = params.value( "host_feature_versions" ).toObject();
                hostLimits_ = params.value( "host_limits" ).toObject();
                hostVersion_ = params.value( "host_version" ).toString();
            }
            QJsonObject result{ { "plugin_id", manifest_.value( "id" ) },
                                { "protocol_version", 1 },
                                { "capabilities", manifest_.value( "capabilities" ) },
                                { "actions", manifest_.value( "actions" ) },
                                { "configuration", manifest_.value( "configuration" ).toArray() },
                                { "sdk", QJsonObject{ { "name", SdkName },
                                                      { "version", SdkVersion } } } };
            const auto features = queryFeatures();
            if ( !features.isEmpty() ) {
                result.insert( "features", features );
            }
            send( { { "jsonrpc", "2.0" }, { "id", frame.value( "id" ) }, { "result", result } } );
        }
        else if ( method == "shutdown" ) {
            stopping_ = true;
        }
        else if ( !initialized_ ) {
            throw std::runtime_error( "Plugin not initialized" );
        }
        else if ( method == "invoke" ) {
            auto job = std::make_shared<Job>();
            job->id = params.value( "job_id" ).toString();
            job->context = params.value( "context" ).toObject();
            {
                std::lock_guard<std::mutex> guard( mutex_ );
                if ( jobs_.size() >= 128 ) {
                    throw std::runtime_error( "Too many jobs" );
                }
                jobs_[ job->id ] = job;
            }
            workers_.push_back( std::async( std::launch::async, [ this, job, params, frame ] {
                try {
                    job->check();
                    const auto result = actions_.at( params.value( "action" ).toString() )(
                        *job, params.value( "arguments" ).toObject() );
                    job->check();
                    send( { { "jsonrpc", "2.0" },
                            { "id", frame.value( "id" ) },
                            { "result", result } } );
                } catch ( const std::exception& error ) {
                    try {
                        send( { { "jsonrpc", "2.0" },
                                { "id", frame.value( "id" ) },
                                { "error", QJsonObject{ { "code", -32000 },
                                                        { "message", QString::fromUtf8(
                                                                         error.what() ) } } } } );
                    } catch ( ... ) {
                        std::cerr << "Could not deliver plugin error" << std::endl;
                    }
                }
                std::lock_guard<std::mutex> guard( mutex_ );
                jobs_.erase( job->id );
            } ) );
        }
        else if ( request && hasQueryHandler( method ) ) {
            enqueueQuery( frame );
        }
        else if ( !request && method == "query/cancel" ) {
            cancelQuery( params.value( "id" ).toString() );
        }
        else if ( !request && method == "ui/command" && commandHandler() ) {
            try {
                commandHandler()( params );
            } catch ( const std::exception& error ) {
                std::cerr << "Plugin command handler failed: " << error.what() << std::endl;
            } catch ( ... ) {
                std::cerr << "Plugin command handler failed" << std::endl;
            }
        }
        else if ( !request && ( method.startsWith( "view/" ) || method.startsWith( "ui/" ) ) ) {
            if ( notification_ ) {
                notification_( method, params );
            }
        }
        else if ( request ) {
            // A request this SDK does not know (a newer host): method not found, never fatal.
            send( { { "jsonrpc", "2.0" },
                    { "id", frame.value( "id" ) },
                    { "error",
                      QJsonObject{ { "code", -32601 }, { "message", "Method not found" } } } } );
        }
        else {
            std::lock_guard<std::mutex> guard( mutex_ );
            if ( method == "configure" ) {
                configuration_ = params.value( "configuration" ).toObject();
            }
            else if ( method == "cancel" ) {
                const auto it = jobs_.find( params.value( "job_id" ).toString() );
                if ( it != jobs_.end() ) {
                    it->second->cancelled = true;
                }
            }
            else {
                const auto it = streams_.find( params.value( "stream_id" ).toString() );
                if ( it != streams_.end() ) {
                    if ( method == "stream/opened" ) {
                        it->second.opened = true;
                    }
                    else if ( method == "stream/stop" ) {
                        it->second.closed = true;
                    }
                    else if ( method == "stream/ack"
                              && params.value( "sequence" ).toDouble()
                                     == static_cast<double>( it->second.sequence ) ) {
                        it->second.acknowledged = true;
                    }
                }
            }
            condition_.notify_all();
        }
    }
    QJsonObject manifest_;
    QJsonObject configuration_;
    QJsonArray hostFeatures_;
    QJsonObject hostFeatureVersions_;
    QJsonObject hostLimits_;
    QString hostVersion_;
    std::map<QString, Lane> lanes_;
    std::map<QString, std::shared_ptr<Query>> pendingQueries_; // waiting or running, by id
    std::vector<std::thread> queryThreads_;
    std::mutex queryMutex_;
    std::condition_variable queryCondition_;
    Notification notification_;
    CommandHandler command_; // guarded by queryMutex_
    std::map<QString, Action> actions_;
    std::map<QString, std::shared_ptr<Job>> jobs_;
    std::map<QString, StreamState> streams_;
    std::vector<std::future<void>> workers_;
    mutable std::mutex mutex_;
    std::mutex outputMutex_;
    std::condition_variable condition_;
    std::atomic<bool> stopping_{ false };
    bool initialized_ = false;
    qsizetype pendingBytes_ = 0;
};
} // namespace cilogg_plugin
