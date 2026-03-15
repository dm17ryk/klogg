#include "scriptsupervisor.h"

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
    exitCode_ = 0;
    stopRequested_ = false;
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

    server_.close();
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
    stopRequested_ = false;
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

    QVariantList outputTail;
    for ( const auto& line : outputTail_ ) {
        outputTail.push_back( line );
    }
    payload.insert( QStringLiteral( "outputTail" ), outputTail );
    return payload;
}

void ScriptSupervisor::handleRpcMessage( const QVariantMap& message )
{
    const auto requestId = message.value( QStringLiteral( "id" ) ).toInt();
    const auto token = message.value( QStringLiteral( "token" ) ).toString();
    if ( token != authToken_ ) {
        sendRpcResponse( socket_, rpcErrorEnvelope( requestId, QStringLiteral( "unauthorized" ) ) );
        return;
    }

    const auto method = message.value( QStringLiteral( "method" ) ).toString();
    if ( method == QStringLiteral( "is_stop_requested" ) ) {
        QVariantMap response;
        response.insert( QStringLiteral( "id" ), requestId );
        response.insert( QStringLiteral( "ok" ), true );
        response.insert( QStringLiteral( "result" ),
                         QVariantMap{ { QStringLiteral( "stopRequested" ), stopRequested_ } } );
        sendRpcResponse( socket_, response );
        return;
    }

    if ( method != QStringLiteral( "command" ) ) {
        sendRpcResponse( socket_,
                         rpcErrorEnvelope( requestId, QStringLiteral( "unsupported method" ) ) );
        return;
    }

    if ( !commanderExecutor_ ) {
        sendRpcResponse( socket_,
                         rpcErrorEnvelope( requestId, QStringLiteral( "no commander executor" ) ) );
        return;
    }

    QString errorMessage;
    const auto request = commanderRequestFromVariantMap(
        message.value( QStringLiteral( "params" ) ).toMap(), &errorMessage );
    if ( !request ) {
        sendRpcResponse( socket_, rpcErrorEnvelope( requestId, errorMessage ) );
        return;
    }

    const auto result = commanderExecutor_( *request );
    sendRpcResponse( socket_, rpcResultEnvelope( requestId, result ) );
}

void ScriptSupervisor::sendRpcResponse( QTcpSocket* socket, const QVariantMap& response )
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
