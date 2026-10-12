#pragma once
// Standalone C++17 SDK. Link Qt Core only; never link a CILogg host library.
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace cilogg_plugin {
constexpr int MaxMessage = 1024 * 1024;
constexpr int MaxPending = 8 * MaxMessage;
class Server;
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
            std::lock_guard<std::mutex> guard( mutex_ );
            for ( auto& pair : jobs_ ) {
                pair.second->cancelled = true;
            }
            condition_.notify_all();
        }
        for ( auto& worker : workers_ ) {
            worker.get();
        }
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
    void dispatch( const QJsonObject& frame )
    {
        const auto method = frame.value( "method" ).toString();
        const auto params = frame.value( "params" ).toObject();
        if ( method == "initialize" ) {
            if ( initialized_ || params.value( "protocol_version" ).toInt() != 1
                 || params.value( "plugin_id" ) != manifest_.value( "id" ) ) {
                throw std::runtime_error( "Incompatible initialization" );
            }
            initialized_ = true;
            configuration_ = params.value( "configuration" ).toObject();
            send(
                { { "jsonrpc", "2.0" },
                  { "id", frame.value( "id" ) },
                  { "result", QJsonObject{ { "plugin_id", manifest_.value( "id" ) },
                                           { "protocol_version", 1 },
                                           { "capabilities", manifest_.value( "capabilities" ) },
                                           { "actions", manifest_.value( "actions" ) },
                                           { "configuration",
                                             manifest_.value( "configuration" ).toArray() } } } } );
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
