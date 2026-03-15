#include "scriptsupervisor.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTimer>

#include "log.h"

namespace {

constexpr int OutputTailLimit = 200;
constexpr auto ReceiveEventType = "receive";
constexpr auto ResponseEventType = "response";

QString randomToken()
{
    return QString::number( QRandomGenerator::global()->generate64(), 16 )
           + QString::number( QRandomGenerator::global()->generate64(), 16 );
}

QStringList pythonExecutableCandidates( const QString& runtimeRoot )
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;
    if ( !runtimeRoot.isEmpty() ) {
#ifdef Q_OS_WIN
        candidates << QDir( runtimeRoot ).filePath( "python.exe" );
        candidates << QDir( runtimeRoot ).filePath( "python/python.exe" );
#else
        candidates << QDir( runtimeRoot ).filePath( "bin/python3" );
        candidates << QDir( runtimeRoot ).filePath( "bin/python" );
        candidates << QDir( runtimeRoot ).filePath( "python/bin/python3" );
#endif
    }
    Q_UNUSED( appDir );
    candidates << qEnvironmentVariable( "KLOGG_SCRIPT_PYTHON" );
    candidates << QStandardPaths::findExecutable( QStringLiteral( "python3" ) );
    candidates << QStandardPaths::findExecutable( QStringLiteral( "python" ) );
    candidates.removeAll( QString{} );
    candidates.removeDuplicates();
    return candidates;
}

QString trimTrailingNewline( const QString& line )
{
    QString trimmed = line;
    while ( trimmed.endsWith( '\n' ) || trimmed.endsWith( '\r' ) ) {
        trimmed.chop( 1 );
    }
    return trimmed;
}

QString selectorTabId( const QVariantMap& params )
{
    return params.value( QStringLiteral( "tabId" ) ).toString().trimmed();
}

} // namespace

QString scriptRunStateToString( ScriptRunState state )
{
    switch ( state ) {
    case ScriptRunState::Idle:
        return QStringLiteral( "idle" );
    case ScriptRunState::Starting:
        return QStringLiteral( "starting" );
    case ScriptRunState::Running:
        return QStringLiteral( "running" );
    case ScriptRunState::Stopping:
        return QStringLiteral( "stopping" );
    case ScriptRunState::Finished:
        return QStringLiteral( "finished" );
    case ScriptRunState::Failed:
        return QStringLiteral( "failed" );
    case ScriptRunState::Cancelled:
        return QStringLiteral( "cancelled" );
    }

    return QStringLiteral( "unknown" );
}

ScriptSupervisor::ScriptSupervisor( QObject* parent )
    : QObject( parent )
{
    process_.setProcessChannelMode( QProcess::SeparateChannels );

    connect( &server_, &QTcpServer::newConnection, this, &ScriptSupervisor::handleNewConnection );
    connect( &process_, &QProcess::started, this, &ScriptSupervisor::handleProcessStarted );
    connect( &process_,
             qOverload<int, QProcess::ExitStatus>( &QProcess::finished ),
             this,
             &ScriptSupervisor::handleProcessFinished );
    connect( &process_, &QProcess::errorOccurred, this, &ScriptSupervisor::handleProcessError );
    connect( &process_, &QProcess::readyReadStandardOutput, this, &ScriptSupervisor::readProcessStdout );
    connect( &process_, &QProcess::readyReadStandardError, this, &ScriptSupervisor::readProcessStderr );
}

void ScriptSupervisor::setCommanderExecutor(
    std::function<CommanderResult( const CommanderRequest& )> executor )
{
    commanderExecutor_ = std::move( executor );
}

CommanderResult ScriptSupervisor::runScript( const QString& scriptFilePath,
                                             const QString& argsJsonFilePath )
{
    if ( hasActiveScript() ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed,
                                 tr( "A Python script is already running." ) );
    }

    const QFileInfo scriptInfo( scriptFilePath );
    if ( !scriptInfo.exists() || !scriptInfo.isFile() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Python script %1 was not found." ).arg( scriptFilePath ) );
    }

    if ( !argsJsonFilePath.isEmpty() ) {
        const QFileInfo argsInfo( argsJsonFilePath );
        if ( !argsInfo.exists() || !argsInfo.isFile() ) {
            return commanderFailure( CommanderResultCode::NotFound,
                                     tr( "JSON args file %1 was not found." )
                                         .arg( argsJsonFilePath ) );
        }
    }

    QString errorMessage;
    if ( !beginListening( &errorMessage ) ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed, errorMessage );
    }

    const auto pythonExecutable = resolvePythonExecutable();
    if ( pythonExecutable.isEmpty() ) {
        resetRunState();
        return commanderFailure(
            CommanderResultCode::ExecutionFailed,
            tr( "No Python runtime was found for script execution." ) );
    }

    const auto bootstrapPath = workerBootstrapPath();
    if ( !QFileInfo::exists( bootstrapPath ) ) {
        resetRunState();
        return commanderFailure(
            CommanderResultCode::ExecutionFailed,
            tr( "Python worker bootstrap was not found at %1." ).arg( bootstrapPath ) );
    }

    scriptFilePath_ = scriptInfo.absoluteFilePath();
    argsJsonFilePath_ = argsJsonFilePath.isEmpty() ? QString{} : QFileInfo( argsJsonFilePath ).absoluteFilePath();
    outputTail_.clear();
    pendingStdout_.clear();
    pendingStderr_.clear();
    socketBuffer_.clear();
    lastError_.clear();
    lastCallbackError_.clear();
    exitCode_ = 0;
    stopRequested_ = false;
    droppedEvents_ = 0;
    dispatchState_ = QStringLiteral( "idle" );
    subscriptions_.clear();
    startedAt_ = QDateTime::currentDateTimeUtc();
    finishedAt_ = {};
    authToken_ = randomToken();
    setState( ScriptRunState::Starting );

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const auto runtime = runtimeRoot();
    environment.insert( QStringLiteral( "KLOGG_SCRIPT_PORT" ),
                        QString::number( server_.serverPort() ) );
    environment.insert( QStringLiteral( "KLOGG_SCRIPT_TOKEN" ), authToken_ );
    environment.insert( QStringLiteral( "KLOGG_SCRIPT_FILE" ), scriptFilePath_ );
    if ( !argsJsonFilePath_.isEmpty() ) {
        environment.insert( QStringLiteral( "KLOGG_SCRIPT_ARGS_JSON_FILE" ), argsJsonFilePath_ );
    }
    if ( !runtime.isEmpty() ) {
        environment.insert( QStringLiteral( "KLOGG_PYTHON_RUNTIME_ROOT" ), runtime );
    }

    QString pythonPath = QFileInfo( bootstrapPath ).absolutePath();
    const auto existingPythonPath = environment.value( QStringLiteral( "PYTHONPATH" ) );
    if ( !existingPythonPath.isEmpty() ) {
        pythonPath += QDir::listSeparator() + existingPythonPath;
    }
    environment.insert( QStringLiteral( "PYTHONPATH" ), pythonPath );

    process_.setProcessEnvironment( environment );
    process_.setProgram( pythonExecutable );
    process_.setArguments( { bootstrapPath } );
    process_.setWorkingDirectory( scriptInfo.absolutePath() );
    process_.start();

    if ( !process_.waitForStarted( 5000 ) ) {
        const auto message = process_.errorString().isEmpty()
                                 ? tr( "Failed to start the Python script worker." )
                                 : process_.errorString();
        resetRunState();
        return commanderFailure( CommanderResultCode::ExecutionFailed, message );
    }

    return commanderSuccess();
}

CommanderResult ScriptSupervisor::stopScript()
{
    if ( !hasActiveScript() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No Python script is currently running." ) );
    }

    stopRequested_ = true;
    setState( ScriptRunState::Stopping );
    QTimer::singleShot( 250, this, [ this ]() {
        if ( process_.state() != QProcess::NotRunning ) {
            process_.kill();
        }
    } );
    return commanderSuccess();
}

CommanderResult ScriptSupervisor::scriptStatus() const
{
    return commanderSuccess( {}, statusPayload() );
}

CommanderResult ScriptSupervisor::scriptSubscriptions() const
{
    QVariantMap payload;
    payload.insert( QStringLiteral( "subscriptions" ), subscriptionsPayload() );
    return commanderSuccess( {}, payload );
}

CommanderResult ScriptSupervisor::clearScriptSubscriptions()
{
    subscriptions_.clear();
    Q_EMIT statusChanged();
    return commanderSuccess();
}

void ScriptSupervisor::publishEvent( const QVariantMap& event )
{
    if ( socket_ == nullptr || subscriptions_.isEmpty() ) {
        return;
    }

    const auto eventType = event.value( QStringLiteral( "eventType" ) ).toString();
    const auto tabId = event.value( QStringLiteral( "tabId" ) ).toString();
    const auto hasSubscriber = std::any_of(
        subscriptions_.cbegin(), subscriptions_.cend(), [ & ]( const auto& subscription ) {
            return subscription.tabId == tabId && subscription.eventType == eventType;
        } );

    if ( !hasSubscriber ) {
        return;
    }

    QVariantMap message;
    message.insert( QStringLiteral( "type" ), QStringLiteral( "event" ) );
    message.insert( QStringLiteral( "event" ), event );
    sendRpcMessage( socket_, message );
}

bool ScriptSupervisor::hasActiveScript() const
{
    return state_ == ScriptRunState::Starting || state_ == ScriptRunState::Running
           || state_ == ScriptRunState::Stopping;
}

QString ScriptSupervisor::activeScriptFile() const
{
    return scriptFilePath_;
}

void ScriptSupervisor::handleNewConnection()
{
    if ( socket_ != nullptr ) {
        auto* extraSocket = server_.nextPendingConnection();
        if ( extraSocket != nullptr ) {
            extraSocket->close();
            extraSocket->deleteLater();
        }
        return;
    }

    socket_ = server_.nextPendingConnection();
    if ( socket_ == nullptr ) {
        return;
    }

    connect( socket_, &QTcpSocket::readyRead, this, &ScriptSupervisor::readSocketData );
    connect( socket_, &QTcpSocket::disconnected, this, [ this ]() {
        if ( socket_ != nullptr ) {
            socket_->deleteLater();
            socket_ = nullptr;
        }
    } );
}

void ScriptSupervisor::handleProcessStarted()
{
    setState( ScriptRunState::Running );
}

void ScriptSupervisor::handleProcessFinished( int exitCode, QProcess::ExitStatus exitStatus )
{
    exitCode_ = exitCode;
    finishedAt_ = QDateTime::currentDateTimeUtc();

    if ( !pendingStdout_.isEmpty() ) {
        appendOutputChunk( {}, false );
    }
    if ( !pendingStderr_.isEmpty() ) {
        appendOutputChunk( {}, true );
    }

    if ( state_ == ScriptRunState::Stopping || stopRequested_ ) {
        setState( ScriptRunState::Cancelled );
    }
    else if ( exitStatus == QProcess::NormalExit && exitCode == 0 ) {
        setState( ScriptRunState::Finished );
    }
    else {
        if ( lastError_.isEmpty() ) {
            lastError_ = tr( "Python script exited with code %1." ).arg( exitCode );
        }
        setState( ScriptRunState::Failed );
    }

    subscriptions_.clear();
    dispatchState_ = QStringLiteral( "idle" );
    server_.close();
    Q_EMIT statusChanged();
}

void ScriptSupervisor::handleProcessError( QProcess::ProcessError error )
{
    Q_UNUSED( error );
    lastError_ = process_.errorString();
    if ( state_ != ScriptRunState::Stopping ) {
        setState( ScriptRunState::Failed );
    }
}

void ScriptSupervisor::readProcessStdout()
{
    appendOutputChunk( QString::fromUtf8( process_.readAllStandardOutput() ), false );
}

void ScriptSupervisor::readProcessStderr()
{
    appendOutputChunk( QString::fromUtf8( process_.readAllStandardError() ), true );
}

void ScriptSupervisor::readSocketData()
{
    if ( socket_ == nullptr ) {
        return;
    }

    socketBuffer_.append( socket_->readAll() );
    while ( true ) {
        const auto newlineIndex = socketBuffer_.indexOf( '\n' );
        if ( newlineIndex < 0 ) {
            break;
        }

        const auto line = socketBuffer_.left( newlineIndex );
        socketBuffer_.remove( 0, newlineIndex + 1 );
        if ( line.trimmed().isEmpty() ) {
            continue;
        }

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson( line, &parseError );
        if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
            appendOutputLine( tr( "[script-rpc] invalid message: %1" )
                                  .arg( QString::fromUtf8( line ) ) );
            continue;
        }

        handleRpcMessage( document.object().toVariantMap() );
    }
}

bool ScriptSupervisor::beginListening( QString* errorMessage )
{
    server_.close();
    if ( socket_ != nullptr ) {
        socket_->deleteLater();
        socket_ = nullptr;
    }

    if ( !server_.listen( QHostAddress::LocalHost ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = tr( "Failed to create local script RPC server: %1" )
                                .arg( server_.errorString() );
        }
        return false;
    }

    return true;
}

QString ScriptSupervisor::resolvePythonExecutable() const
{
    const auto candidates = pythonExecutableCandidates( runtimeRoot() );
    for ( const auto& candidate : candidates ) {
        if ( candidate.isEmpty() ) {
            continue;
        }
        if ( QFileInfo::exists( candidate ) ) {
            return candidate;
        }
        const auto resolved = QStandardPaths::findExecutable( candidate );
        if ( !resolved.isEmpty() ) {
            return resolved;
        }
    }
    return {};
}

QString ScriptSupervisor::runtimeRoot() const
{
    const auto root = QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "python_runtime" ) );
    return QFileInfo::exists( root ) ? root : QString{};
}

QString ScriptSupervisor::workerBootstrapPath() const
{
    const auto runtime = runtimeRoot();
    if ( runtime.isEmpty() ) {
        return {};
    }
    return QDir( runtime ).filePath( QStringLiteral( "worker_bootstrap.py" ) );
}

void ScriptSupervisor::resetRunState()
{
    if ( socket_ != nullptr ) {
        socket_->deleteLater();
        socket_ = nullptr;
    }
    server_.close();
    socketBuffer_.clear();
    scriptFilePath_.clear();
    argsJsonFilePath_.clear();
    authToken_.clear();
    pendingStdout_.clear();
    pendingStderr_.clear();
    outputTail_.clear();
    lastCallbackError_.clear();
    subscriptions_.clear();
    stopRequested_ = false;
    droppedEvents_ = 0;
    dispatchState_ = QStringLiteral( "idle" );
    exitCode_ = 0;
    startedAt_ = {};
    finishedAt_ = {};
    setState( ScriptRunState::Idle );
}

void ScriptSupervisor::appendOutputChunk( const QString& chunk, bool stderrStream )
{
    QString* pending = stderrStream ? &pendingStderr_ : &pendingStdout_;
    pending->append( chunk );

    int newlineIndex = -1;
    while ( ( newlineIndex = pending->indexOf( '\n' ) ) >= 0 ) {
        const auto line = pending->left( newlineIndex + 1 );
        pending->remove( 0, newlineIndex + 1 );
        appendOutputLine( trimTrailingNewline( stderrStream ? QStringLiteral( "stderr: %1" ).arg( line )
                                                            : line ) );
    }

    if ( chunk.isEmpty() && !pending->isEmpty() ) {
        appendOutputLine( trimTrailingNewline( stderrStream ? QStringLiteral( "stderr: %1" ).arg( *pending )
                                                            : *pending ) );
        pending->clear();
    }
}

void ScriptSupervisor::appendOutputLine( const QString& line )
{
    if ( line.isEmpty() ) {
        return;
    }

    outputTail_.push_back( line );
    while ( outputTail_.size() > OutputTailLimit ) {
        outputTail_.removeFirst();
    }
    Q_EMIT outputChanged();
}

void ScriptSupervisor::setState( ScriptRunState state )
{
    state_ = state;
    Q_EMIT statusChanged();
}

QVariantMap ScriptSupervisor::statusPayload() const
{
    QVariantMap payload;
    payload.insert( QStringLiteral( "state" ), scriptRunStateToString( state_ ) );
    payload.insert( QStringLiteral( "scriptFile" ), scriptFilePath_ );
    payload.insert( QStringLiteral( "argsJsonFile" ), argsJsonFilePath_ );
    if ( startedAt_.isValid() ) {
        payload.insert( QStringLiteral( "startedAt" ), startedAt_.toString( Qt::ISODateWithMs ) );
    }
    if ( finishedAt_.isValid() ) {
        payload.insert( QStringLiteral( "finishedAt" ), finishedAt_.toString( Qt::ISODateWithMs ) );
    }
    payload.insert( QStringLiteral( "exitCode" ), exitCode_ );
    payload.insert( QStringLiteral( "lastError" ), lastError_ );
    payload.insert( QStringLiteral( "lastCallbackError" ), lastCallbackError_ );
    payload.insert( QStringLiteral( "droppedEvents" ), droppedEvents_ );
    payload.insert( QStringLiteral( "dispatchState" ), dispatchState_ );
    payload.insert( QStringLiteral( "subscriptions" ), subscriptionsPayload() );

    QVariantList outputTail;
    for ( const auto& line : outputTail_ ) {
        outputTail.push_back( line );
    }
    payload.insert( QStringLiteral( "outputTail" ), outputTail );
    return payload;
}

QVariantList ScriptSupervisor::subscriptionsPayload() const
{
    QVariantList payload;
    for ( const auto& subscription : subscriptions_ ) {
        QVariantMap item;
        item.insert( QStringLiteral( "tabId" ), subscription.tabId );
        item.insert( QStringLiteral( "windowIndex" ), subscription.windowIndex );
        item.insert( QStringLiteral( "tabIndex" ), subscription.tabIndex );
        item.insert( QStringLiteral( "windowId" ), subscription.windowId );
        item.insert( QStringLiteral( "filePath" ), subscription.filePath );
        item.insert( QStringLiteral( "displayName" ), subscription.displayName );
        item.insert( QStringLiteral( "portName" ), subscription.portName );
        item.insert( QStringLiteral( "eventType" ), subscription.eventType );
        if ( subscription.responseId.has_value() ) {
            item.insert( QStringLiteral( "responseId" ), *subscription.responseId );
        }
        if ( !subscription.responseName.isEmpty() ) {
            item.insert( QStringLiteral( "responseName" ), subscription.responseName );
        }
        payload.push_back( item );
    }
    return payload;
}

void ScriptSupervisor::handleRpcMessage( const QVariantMap& message )
{
    const auto requestId = message.value( QStringLiteral( "id" ) ).toInt();
    const auto token = message.value( QStringLiteral( "token" ) ).toString();
    const auto isNotification = message.value( QStringLiteral( "notification" ) ).toBool();
    if ( token != authToken_ ) {
        if ( !isNotification ) {
            sendRpcMessage( socket_, rpcErrorEnvelope( requestId, QStringLiteral( "unauthorized" ) ) );
        }
        return;
    }

    const auto method = message.value( QStringLiteral( "method" ) ).toString();
    if ( method == QStringLiteral( "is_stop_requested" ) ) {
        QVariantMap response;
        response.insert( QStringLiteral( "id" ), requestId );
        response.insert( QStringLiteral( "ok" ), true );
        response.insert( QStringLiteral( "result" ),
                         QVariantMap{ { QStringLiteral( "stopRequested" ), stopRequested_ } } );
        sendRpcMessage( socket_, response );
        return;
    }

    const auto params = message.value( QStringLiteral( "params" ) ).toMap();

    if ( method == QStringLiteral( "subscribe_event" ) ) {
        QString errorMessage;
        const auto target = resolveSubscriptionTarget( params, &errorMessage );
        if ( target.isEmpty() ) {
            if ( !isNotification ) {
                sendRpcMessage( socket_, rpcErrorEnvelope( requestId, errorMessage ) );
            }
            return;
        }

        const auto eventType = params.value( QStringLiteral( "eventType" ) ).toString().trimmed();
        if ( eventType != QLatin1String( ReceiveEventType )
             && eventType != QLatin1String( ResponseEventType ) ) {
            if ( !isNotification ) {
                sendRpcMessage( socket_, rpcErrorEnvelope( requestId, QStringLiteral( "unsupported event type" ) ) );
            }
            return;
        }

        ScriptSubscription subscription;
        subscription.tabId = target.value( QStringLiteral( "tabId" ) ).toString();
        subscription.windowIndex = target.value( QStringLiteral( "windowIndex" ) ).toInt();
        subscription.tabIndex = target.value( QStringLiteral( "tabIndex" ) ).toInt();
        subscription.windowId = target.value( QStringLiteral( "windowId" ) ).toString();
        subscription.filePath = target.value( QStringLiteral( "filePath" ) ).toString();
        subscription.displayName = target.value( QStringLiteral( "displayName" ) ).toString();
        subscription.portName = target.value( QStringLiteral( "portName" ) ).toString();
        subscription.eventType = eventType;
        if ( params.contains( QStringLiteral( "responseId" ) ) ) {
            subscription.responseId = params.value( QStringLiteral( "responseId" ) ).toInt();
        }
        subscription.responseName = params.value( QStringLiteral( "responseName" ) ).toString().trimmed();
        subscriptions_.push_back( subscription );
        Q_EMIT statusChanged();

        if ( !isNotification ) {
            QVariantMap payload = target;
            payload.insert( QStringLiteral( "eventType" ), eventType );
            if ( subscription.responseId.has_value() ) {
                payload.insert( QStringLiteral( "responseId" ), *subscription.responseId );
            }
            if ( !subscription.responseName.isEmpty() ) {
                payload.insert( QStringLiteral( "responseName" ), subscription.responseName );
            }

            QVariantMap response;
            response.insert( QStringLiteral( "id" ), requestId );
            response.insert( QStringLiteral( "ok" ), true );
            response.insert( QStringLiteral( "result" ), payload );
            sendRpcMessage( socket_, response );
        }
        return;
    }

    if ( method == QStringLiteral( "clear_event_handlers" ) ) {
        const auto tabId = selectorTabId( params );
        if ( tabId.isEmpty() ) {
            QString errorMessage;
            const auto target = resolveSubscriptionTarget( params, &errorMessage );
            if ( target.isEmpty() ) {
                if ( !isNotification ) {
                    sendRpcMessage( socket_, rpcErrorEnvelope( requestId, errorMessage ) );
                }
                return;
            }
            clearSubscriptionsForTab( target.value( QStringLiteral( "tabId" ) ).toString() );
        }
        else {
            clearSubscriptionsForTab( tabId );
        }

        if ( !isNotification ) {
            QVariantMap response;
            response.insert( QStringLiteral( "id" ), requestId );
            response.insert( QStringLiteral( "ok" ), true );
            response.insert( QStringLiteral( "result" ), QVariantMap{} );
            sendRpcMessage( socket_, response );
        }
        return;
    }

    if ( method == QStringLiteral( "set_dispatch_state" ) ) {
        dispatchState_ = params.value( QStringLiteral( "state" ) ).toString().trimmed();
        if ( dispatchState_.isEmpty() ) {
            dispatchState_ = QStringLiteral( "idle" );
        }
        Q_EMIT statusChanged();
        return;
    }

    if ( method == QStringLiteral( "report_callback_error" ) ) {
        lastCallbackError_ = params.value( QStringLiteral( "error" ) ).toString();
        if ( !lastCallbackError_.isEmpty() ) {
            appendOutputLine( tr( "[script-callback] %1" ).arg( lastCallbackError_ ) );
        }
        Q_EMIT statusChanged();
        return;
    }

    if ( method == QStringLiteral( "report_event_stats" ) ) {
        droppedEvents_ = params.value( QStringLiteral( "droppedEvents" ) ).toInt();
        Q_EMIT statusChanged();
        return;
    }

    if ( method != QStringLiteral( "command" ) ) {
        if ( !isNotification ) {
            sendRpcMessage( socket_,
                            rpcErrorEnvelope( requestId, QStringLiteral( "unsupported method" ) ) );
        }
        return;
    }

    if ( !commanderExecutor_ ) {
        sendRpcMessage( socket_,
                        rpcErrorEnvelope( requestId, QStringLiteral( "no commander executor" ) ) );
        return;
    }

    QString errorMessage;
    const auto request = commanderRequestFromVariantMap(
        params, &errorMessage );
    if ( !request ) {
        sendRpcMessage( socket_, rpcErrorEnvelope( requestId, errorMessage ) );
        return;
    }

    const auto result = commanderExecutor_( *request );
    sendRpcMessage( socket_, rpcResultEnvelope( requestId, result ) );
}

void ScriptSupervisor::sendRpcMessage( QTcpSocket* socket, const QVariantMap& response )
{
    if ( socket == nullptr ) {
        return;
    }

    const auto bytes = QJsonDocument::fromVariant( response ).toJson( QJsonDocument::Compact );
    socket->write( bytes );
    socket->write( "\n" );
    socket->flush();
}

QVariantMap ScriptSupervisor::rpcResultEnvelope( int requestId, const CommanderResult& result ) const
{
    QVariantMap response;
    response.insert( QStringLiteral( "id" ), requestId );
    response.insert( QStringLiteral( "ok" ), true );
    response.insert( QStringLiteral( "result" ), commanderResultToVariantMap( result ) );
    return response;
}

QVariantMap ScriptSupervisor::rpcErrorEnvelope( int requestId, const QString& errorText ) const
{
    QVariantMap response;
    response.insert( QStringLiteral( "id" ), requestId );
    response.insert( QStringLiteral( "ok" ), false );
    response.insert( QStringLiteral( "error" ), errorText );
    return response;
}

QVariantMap ScriptSupervisor::resolveSubscriptionTarget( const QVariantMap& selector,
                                                         QString* errorMessage ) const
{
    if ( !commanderExecutor_ ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "no commander executor" );
        }
        return {};
    }

    CommanderRequest request;
    request.action = CommanderAction::GetInfo;
    const auto infoResult = commanderExecutor_( request );
    if ( !infoResult.ok() ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = infoResult.message;
        }
        return {};
    }

    const auto windows = infoResult.payload.value( QStringLiteral( "windows" ) ).toList();
    const auto requestedTabId = selector.value( QStringLiteral( "tabId" ) ).toString().trimmed();
    const auto requestedWindowIndex = selector.value( QStringLiteral( "windowIndex" ) );
    const auto requestedTabIndex = selector.value( QStringLiteral( "tabIndex" ) );

    for ( const auto& windowValue : windows ) {
        const auto window = windowValue.toMap();
        const auto windowIndex = window.value( QStringLiteral( "windowIndex" ) ).toInt();
        if ( requestedWindowIndex.isValid() && windowIndex != requestedWindowIndex.toInt() ) {
            continue;
        }

        const auto tabs = window.value( QStringLiteral( "tabs" ) ).toList();
        for ( const auto& tabValue : tabs ) {
            const auto tab = tabValue.toMap();
            const auto tabIndex = tab.value( QStringLiteral( "tabIndex" ) ).toInt();
            if ( !requestedTabId.isEmpty() ) {
                if ( tab.value( QStringLiteral( "tabId" ) ).toString() != requestedTabId ) {
                    continue;
                }
            }
            else if ( requestedWindowIndex.isValid() || requestedTabIndex.isValid() ) {
                if ( !requestedWindowIndex.isValid() || !requestedTabIndex.isValid()
                     || tabIndex != requestedTabIndex.toInt() ) {
                    continue;
                }
            }
            else {
                continue;
            }

            const auto sourceType = tab.value( QStringLiteral( "sourceType" ) ).toString();
            if ( sourceType != QStringLiteral( "com" ) ) {
                if ( errorMessage != nullptr ) {
                    *errorMessage = tr( "Requested tab is not a live communication tab." );
                }
                return {};
            }

            QVariantMap target;
            target.insert( QStringLiteral( "tabId" ), tab.value( QStringLiteral( "tabId" ) ) );
            target.insert( QStringLiteral( "tabIndex" ), tab.value( QStringLiteral( "tabIndex" ) ) );
            target.insert( QStringLiteral( "windowId" ), window.value( QStringLiteral( "windowId" ) ) );
            target.insert( QStringLiteral( "windowIndex" ), window.value( QStringLiteral( "windowIndex" ) ) );
            target.insert( QStringLiteral( "filePath" ), tab.value( QStringLiteral( "filePath" ) ) );
            target.insert( QStringLiteral( "displayName" ), tab.value( QStringLiteral( "displayName" ) ) );
            target.insert( QStringLiteral( "portName" ),
                           tab.value( QStringLiteral( "com" ) ).toMap().value( QStringLiteral( "portName" ) ) );
            return target;
        }
    }

    if ( errorMessage != nullptr ) {
        *errorMessage = tr( "Requested live communication tab was not found." );
    }
    return {};
}

void ScriptSupervisor::clearSubscriptionsForTab( const QString& tabId )
{
    subscriptions_.erase( std::remove_if( subscriptions_.begin(), subscriptions_.end(),
                                          [ &tabId ]( const auto& subscription ) {
                                              return subscription.tabId == tabId;
                                          } ),
                          subscriptions_.end() );
    Q_EMIT statusChanged();
}
