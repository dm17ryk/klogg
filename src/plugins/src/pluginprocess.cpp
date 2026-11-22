#include "pluginprocess.h"
#include "log.h"
#include "pluginfeatures.h"
#include "pythonruntime.h"
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <memory>
#include <utility>

namespace cilogg::plugins {
PluginProcess::PluginProcess( Manifest manifest, QObject* parent )
    : QObject( parent )
    , manifest_( std::move( manifest ) )
{
    deadline_.setSingleShot( true );
    connect( &deadline_, &QTimer::timeout, this, [ this ] {
        if ( state_ == "stopping" ) {
            LOG_WARNING << "Plugin shutdown deadline id=" << manifest_.id.toStdString();
            process_.kill();
        }
        else {
            fail( initializationTimeoutError() );
        }
    } );
    process_.setProcessChannelMode( QProcess::SeparateChannels );
    connect( &process_, &QProcess::started, this, [ this ] {
        if ( state_ != "starting" ) {
            LOG_DEBUG << "Plugin start completed after stop id=" << manifest_.id.toStdString();
            return;
        }
        setState( "initializing" );
        send( { { "jsonrpc", "2.0" },
                { "id", "initialize" },
                { "method", "initialize" },
                { "params", initializeParams_ } } );
    } );
    connect( &process_, &QProcess::readyReadStandardOutput, this, &PluginProcess::drain );
    connect( &process_, &QProcess::readyReadStandardError, this, [ this ] {
        // Drain in bounded chunks; never forward plugin text to host stdout.
        process_.setReadChannel( QProcess::StandardError );
        while ( process_.bytesAvailable() > 0 ) {
            const auto bytes = process_.read( 32768 );
            stderrBytes_ += bytes.size();
            const auto text = QString::fromUtf8( bytes );
            LOG_DEBUG << "Plugin stderr id=" << manifest_.id.toStdString() << " "
                      << text.toStdString();
            Q_EMIT diagnostic( text );
        }
        process_.setReadChannel( QProcess::StandardOutput );
    } );
    connect( &process_, &QProcess::errorOccurred, this, [ this ]( QProcess::ProcessError error ) {
        if ( error == QProcess::FailedToStart ) {
            fail( QStringLiteral( "Cannot start %1: %2" )
                      .arg( commandLine(), process_.errorString() ) );
            Q_EMIT stopped();
        }
    } );
    connect( &process_, qOverload<int, QProcess::ExitStatus>( &QProcess::finished ), this,
             [ this ]( int code, QProcess::ExitStatus status ) {
                 deadline_.stop();
                 if ( state_ != "failed" && state_ != "stopping" ) {
                     error_ = QStringLiteral( "Plugin exited unexpectedly (code %1, status %2)." )
                                  .arg( code )
                                  .arg( static_cast<int>( status ) );
                     note( error_ );
                     setState( "failed" );
                 }
                 else if ( state_ == "stopping" ) {
                     setState( "stopped" );
                 }
                 waiting_.clear();
                 input_.clear();
                 Q_EMIT stopped();
             } );
}

PluginProcess::~PluginProcess()
{
    disconnect( &process_, nullptr, this, nullptr );
    if ( process_.state() != QProcess::NotRunning ) {
        process_.kill();
        process_.waitForFinished( 1000 );
    }
}

void PluginProcess::setState( const QString& state )
{
    LOG_DEBUG << "Plugin lifecycle id=" << manifest_.id.toStdString() << " " << state_.toStdString()
              << " -> " << state.toStdString() << " reason=" << error_.toStdString();
    state_ = state;
    Q_EMIT changed();
}

void PluginProcess::start( const QJsonObject& configuration, const QString& configDirectory,
                           const QString& pythonExecutable,
                           const QMap<QString, QString>& environment )
{
    if ( state_ != "stopped" ) {
        return;
    }
    error_.clear();
    input_.clear();
    descriptor_ = {};
    features_.clear();
    process_.setWorkingDirectory( manifest_.directory );
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert( "PYTHONUNBUFFERED", "1" );
    for ( auto it = environment.cbegin(); it != environment.cend(); ++it ) {
        env.insert( it.key(), it.value() );
    }
    process_.setProcessEnvironment( env );
    setState( "starting" );
    initializeParams_
        = { { "protocol_version", ProtocolVersion },  { "plugin_id", manifest_.id },
            { "configuration", configuration },       { "config_directory", configDirectory },
            { "max_message_bytes", MaxMessageBytes }, { "max_pending_bytes", MaxPendingBytes } };
    // Host features, their versions, the host's version and limits (docs/plugin-sdk-advanced.md
    // section 4.1). Additive keys only: both SDKs read initialize parameters by name, so older
    // plugin copies ignore them. E01 may later fold this into negotiated capabilities.
    initializeParams_.insert( "host_features", hostFeatureNames() );
    initializeParams_.insert( "host_feature_versions", hostFeatureVersions() );
    initializeParams_.insert( "host_version", hostVersion_ );
    initializeParams_.insert( "host_limits", hostLimits() );
    program_ = manifest_.executable == "python"
                   ? pythonExecutable
                   : QDir( manifest_.directory ).absoluteFilePath( manifest_.executable );
    stderrBytes_ = 0;
    // The plugin's diagnostics start with what was run, so a plugin that fails before writing
    // anything still says which interpreter it was given.
    note( QStringLiteral( "Starting %1 in %2" )
              .arg( commandLine(), QDir::toNativeSeparators( manifest_.directory ) ) );
    deadline_.start( initializationTimeoutMs_ );
    process_.start( program_, manifest_.arguments );
}

QString PluginProcess::commandLine() const
{
    QStringList parts{ QDir::toNativeSeparators( program_ ) };
    parts.append( manifest_.arguments );
    for ( auto& part : parts ) {
        if ( part.isEmpty() || part.contains( QLatin1Char( ' ' ) ) ) {
            part = QLatin1Char( '"' ) + part + QLatin1Char( '"' );
        }
    }
    return parts.join( QLatin1Char( ' ' ) );
}

void PluginProcess::note( const QString& text )
{
    Q_EMIT diagnostic( QStringLiteral( "[CILogg %1] %2\n" )
                           .arg( QDateTime::currentDateTime().toString( "HH:mm:ss" ), text ) );
}

QString PluginProcess::initializationTimeoutError() const
{
    auto error = QStringLiteral( "Plugin initialization timed out: no answer to initialize within "
                                 "%1 s from %2." )
                     .arg( initializationTimeoutMs_ / 1000.0 )
                     .arg( commandLine() );
    if ( manifest_.executable == "python" && pythonruntime::isLauncherScript( program_ ) ) {
        // pathInterpreterCandidates() takes such a script only when no python.exe is on PATH.
        error += QStringLiteral( " That is a launcher script, which can take long to start "
                                 "Python; install Python with python.exe on PATH or set "
                                 "CILOGG_SCRIPT_PYTHON to a python.exe." );
    }
    error += stderrBytes_ == 0 ? QStringLiteral( " The plugin wrote nothing to stderr." )
                               : QStringLiteral( " See its stderr in the plugin's diagnostics." );
    return error;
}

void PluginProcess::request( const QString& id, const QString& method, const QJsonObject& params )
{
    const QJsonObject frame{
        { "jsonrpc", "2.0" }, { "id", id }, { "method", method }, { "params", params }
    };
    if ( state_ == "starting" || state_ == "initializing" ) {
        if ( waiting_.size() >= 128 ) {
            fail( "Too many pending requests." );
            return;
        }
        waiting_.append( frame );
    }
    else if ( state_ == "ready" ) {
        send( frame );
    }
}

void PluginProcess::notify( const QString& method, const QJsonObject& params )
{
    if ( state_ == "ready" || state_ == "stopping" ) {
        send( QJsonObject{ { "jsonrpc", "2.0" }, { "method", method }, { "params", params } } );
    }
}

void PluginProcess::cancelRequest( const QString& id )
{
    const auto previous = waiting_.size();
    waiting_.erase(
        std::remove_if( waiting_.begin(), waiting_.end(),
                        [ &id ]( const QJsonObject& frame ) { return frame.value( "id" ) == id; } ),
        waiting_.end() );
    LOG_DEBUG << "Plugin cancellation id=" << manifest_.id.toStdString()
              << " job=" << id.toStdString() << " queued=" << ( previous != waiting_.size() );
    if ( previous == waiting_.size() ) {
        notify( "cancel", { { "job_id", id } } );
    }
}

bool PluginProcess::waitForStopped( int milliseconds )
{
    return process_.state() == QProcess::NotRunning || process_.waitForFinished( milliseconds );
}

void PluginProcess::send( const QJsonObject& message )
{
    auto bytes = QJsonDocument( message ).toJson( QJsonDocument::Compact );
    if ( bytes.size() > MaxMessageBytes
         || process_.bytesToWrite() + bytes.size() > MaxPendingBytes ) {
        fail( "Plugin transport output limit exceeded." );
        return;
    }
    bytes.append( '\n' );
    if ( process_.write( bytes ) != bytes.size() ) {
        fail( process_.errorString() );
        return;
    }
    Q_EMIT frameTransferred( true, message );
}

void PluginProcess::drain()
{
    if ( state_ == "failed" || state_ == "stopping" ) {
        process_.readAllStandardOutput();
        return;
    }
    process_.setReadChannel( QProcess::StandardOutput );
    // Yield between batches so a busy source cannot monopolize the GUI event loop.
    for ( int frames = 0; frames < 64; ++frames ) {
        auto newline = input_.indexOf( '\n' );
        if ( newline < 0 && process_.bytesAvailable() > 0 ) {
            input_.append(
                process_.read( qMin<qint64>( 65536, MaxMessageBytes + 1 - input_.size() ) ) );
            newline = input_.indexOf( '\n' );
        }
        if ( newline < 0 ) {
            if ( input_.size() > MaxMessageBytes ) {
                fail( "Plugin message exceeds 1 MiB." );
                return;
            }
            break;
        }
        if ( newline > MaxMessageBytes ) {
            fail( "Plugin message exceeds 1 MiB." );
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson( input_.left( newline ), &error );
        input_.remove( 0, newline + 1 );
        const auto frame = document.object();
        if ( error.error != QJsonParseError::NoError || !document.isObject()
             || frame.value( "jsonrpc" ) != "2.0"
             || ( !frame.contains( "method" ) && !frame.contains( "id" ) ) ) {
            fail( "Malformed plugin JSON-RPC message." );
            return;
        }
        const bool response = !frame.contains( "method" );
        const auto rpcError = frame.value( "error" ).toObject();
        if ( ( response
               && ( !frame.value( "id" ).isString()
                    || frame.contains( "result" ) == frame.contains( "error" )
                    || ( frame.contains( "error" )
                         && ( !frame.value( "error" ).isObject()
                              || !rpcError.value( "code" ).isDouble()
                              || !rpcError.value( "message" ).isString() ) ) ) )
             || ( !response
                  && ( !frame.value( "method" ).isString()
                       || !frame.value( "params" ).isObject() ) ) ) {
            fail( "Malformed plugin JSON-RPC envelope." );
            return;
        }
        Q_EMIT frameTransferred( false, frame );
        if ( state_ == "initializing" ) {
            if ( frame.value( "id" ) != "initialize" || !frame.value( "result" ).isObject() ) {
                fail( "Expected initialization response." );
                return;
            }
            descriptor_ = frame.value( "result" ).toObject();
            if ( descriptor_.value( "plugin_id" ) != manifest_.id
                 || descriptor_.value( "protocol_version" ) != QJsonValue( ProtocolVersion )
                 || ( descriptor_.contains( "actions" )
                      && !descriptor_.value( "actions" ).isArray() )
                 || ( descriptor_.contains( "configuration" )
                      && !descriptor_.value( "configuration" ).isArray() )
                 || descriptor_.value( "capabilities" ).toArray()
                        != manifest_.json[ "capabilities" ].toArray()
                 || !validateActions( descriptor_.value( "actions" ).toArray(),
                                      descriptor_.value( "capabilities" ).toArray() )
                         .isEmpty()
                 || !validateFields( descriptor_.value( "configuration" ).toArray() ).isEmpty() ) {
                fail( "Incompatible plugin initialization response." );
                return;
            }
            // Optional E17 keys: invalid values are ignored, never fatal (design 4.2).
            features_ = pluginFeatures( descriptor_.value( "features" ) );
            deadline_.stop();
            setState( "ready" );
            const auto waiting = std::exchange( waiting_, {} );
            for ( const auto& queued : waiting ) {
                send( queued );
            }
        }
        else {
            Q_EMIT message( frame );
        }
        if ( state_ == "failed" ) {
            return;
        }
    }
    if ( process_.bytesAvailable() > 0 || input_.contains( '\n' ) ) {
        QTimer::singleShot( 0, this, &PluginProcess::drain );
    }
}

void PluginProcess::fail( const QString& error )
{
    error_ = error;
    note( error );
    deadline_.stop();
    waiting_.clear();
    setState( "failed" );
    if ( process_.state() != QProcess::NotRunning ) {
        process_.kill();
    }
}

void PluginProcess::stop()
{
    if ( state_ == "stopping" ) {
        return;
    }
    if ( process_.state() == QProcess::NotRunning ) {
        setState( "stopped" );
        return;
    }
    waiting_.clear();
    deadline_.stop();
    setState( "stopping" );
    notify( "shutdown", {} );
    deadline_.start( 3000 );
}
} // namespace cilogg::plugins
