#include "scenariorunner.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStandardPaths>

namespace {

constexpr int OutputTailLimit = 200;

QString randomToken()
{
    return QString::number( QRandomGenerator::global()->generate64(), 16 )
           + QString::number( QRandomGenerator::global()->generate64(), 16 );
}

QString trimTrailingNewline( const QString& line )
{
    QString trimmed = line;
    while ( trimmed.endsWith( '\n' ) || trimmed.endsWith( '\r' ) ) {
        trimmed.chop( 1 );
    }
    return trimmed;
}

QString scenarioReportPath( const QString& environmentName, const QString& defaultFileName )
{
    const auto overrideValue = qEnvironmentVariable( environmentName.toLatin1().constData() ).trimmed();
    if ( !overrideValue.isEmpty() ) {
        return QFileInfo( overrideValue ).absoluteFilePath();
    }

    return QDir( QCoreApplication::applicationDirPath() ).filePath( defaultFileName );
}

bool isActiveState( ScenarioRunState state )
{
    return state == ScenarioRunState::Starting || state == ScenarioRunState::Running
           || state == ScenarioRunState::Stopping;
}

QStringList pythonExecutableCandidates( const QString& runtimeRoot )
{
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
    candidates << qEnvironmentVariable( "CILOGG_SCRIPT_PYTHON" );
    candidates << QStandardPaths::findExecutable( QStringLiteral( "python3" ) );
    candidates << QStandardPaths::findExecutable( QStringLiteral( "python" ) );
    candidates.removeAll( QString{} );
    candidates.removeDuplicates();
    return candidates;
}

bool matchesName( const QString& expected, const QString& actual )
{
    return expected.isEmpty() || expected.compare( actual, Qt::CaseInsensitive ) == 0;
}

} // namespace

QString scenarioRunStateToString( ScenarioRunState state )
{
    switch ( state ) {
    case ScenarioRunState::Idle:
        return QStringLiteral( "idle" );
    case ScenarioRunState::Starting:
        return QStringLiteral( "starting" );
    case ScenarioRunState::Running:
        return QStringLiteral( "running" );
    case ScenarioRunState::Stopping:
        return QStringLiteral( "stopping" );
    case ScenarioRunState::Finished:
        return QStringLiteral( "finished" );
    case ScenarioRunState::Failed:
        return QStringLiteral( "failed" );
    case ScenarioRunState::Cancelled:
        return QStringLiteral( "cancelled" );
    }

    return QStringLiteral( "unknown" );
}

ScenarioRunner::ScenarioRunner( QObject* parent )
    : QObject( parent )
    , server_( new QTcpServer( this ) )
{
}

ScenarioRunner::~ScenarioRunner() = default;

void ScenarioRunner::setCommanderExecutor(
    std::function<CommanderResult( const CommanderRequest& )> executor )
{
    commanderExecutor_ = std::move( executor );
}

CommanderResult ScenarioRunner::runScenario( const CommanderRequest& request )
{
    if ( isActiveState( state_ ) ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "A scenario run is already active." ) );
    }

    const QFileInfo scenarioInfo( request.scenarioFilePath );
    if ( !scenarioInfo.exists() || !scenarioInfo.isFile() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Scenario file %1 was not found." ).arg( request.scenarioFilePath ) );
    }

    if ( !request.argsJsonFilePath.isEmpty() ) {
        const QFileInfo argsInfo( request.argsJsonFilePath );
        if ( !argsInfo.exists() || !argsInfo.isFile() ) {
            return commanderFailure( CommanderResultCode::NotFound,
                                     tr( "JSON args file %1 was not found." )
                                         .arg( request.argsJsonFilePath ) );
        }
    }

    resetRunState();
    hasRun_ = true;
    scenarioFilePath_ = scenarioInfo.absoluteFilePath();
    argsJsonFilePath_ = request.argsJsonFilePath.isEmpty()
                            ? QString{}
                            : QFileInfo( request.argsJsonFilePath ).absoluteFilePath();
    reportJsonPath_
        = scenarioReportPath( QStringLiteral( "CILOGG_SCENARIO_REPORT_JSON" ),
                              QStringLiteral( "scenario-report.json" ) );
    reportJunitPath_
        = scenarioReportPath( QStringLiteral( "CILOGG_SCENARIO_REPORT_JUNIT" ),
                              QStringLiteral( "scenario-report.junit.xml" ) );

    if ( !startProcess( scenarioBootstrapPath() ) ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed,
                                 lastError_.isEmpty()
                                     ? tr( "Failed to start scenario run." )
                                     : lastError_ );
    }

    return commanderSuccess();
}

CommanderResult ScenarioRunner::runSuite( const CommanderRequest& request )
{
    if ( isActiveState( state_ ) ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "A scenario run is already active." ) );
    }

    const QFileInfo suiteInfo( request.suiteFilePath );
    if ( !suiteInfo.exists() || !suiteInfo.isFile() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Scenario suite file %1 was not found." ).arg( request.suiteFilePath ) );
    }

    resetRunState();
    hasRun_ = true;
    suiteFilePath_ = suiteInfo.absoluteFilePath();
    reportJsonPath_
        = scenarioReportPath( QStringLiteral( "CILOGG_SCENARIO_REPORT_JSON" ),
                              QStringLiteral( "scenario-report.json" ) );
    reportJunitPath_
        = scenarioReportPath( QStringLiteral( "CILOGG_SCENARIO_REPORT_JUNIT" ),
                              QStringLiteral( "scenario-report.junit.xml" ) );

    if ( !startProcess( scenarioBootstrapPath() ) ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed,
                                 lastError_.isEmpty()
                                     ? tr( "Failed to start scenario suite." )
                                     : lastError_ );
    }

    return commanderSuccess();
}

CommanderResult ScenarioRunner::stopRun()
{
    if ( !hasRun_ || !isActiveState( state_ ) ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No scenario run is currently active." ) );
    }

    stopRequested_ = true;
    setState( ScenarioRunState::Stopping );
    process_.kill();
    return commanderSuccess();
}

CommanderResult ScenarioRunner::status() const
{
    if ( !hasRun_ ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No scenario run was found." ) );
    }

    return commanderSuccess( {}, statusPayload() );
}

CommanderResult ScenarioRunner::report() const
{
    if ( !hasRun_ ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No scenario report was found." ) );
    }

    if ( lastReportPayload_.isEmpty() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Scenario report is not available yet." ) );
    }

    return commanderSuccess( {}, reportPayload() );
}

QVariantMap ScenarioRunner::statusPayload() const
{
    QVariantMap payload;
    if ( !hasRun_ ) {
        return payload;
    }

    payload.insert( QStringLiteral( "state" ), scenarioRunStateToString( state_ ) );
    payload.insert( QStringLiteral( "scenarioFile" ), scenarioFilePath_ );
    payload.insert( QStringLiteral( "suiteFile" ), suiteFilePath_ );
    payload.insert( QStringLiteral( "argsJsonFile" ), argsJsonFilePath_ );
    payload.insert( QStringLiteral( "suiteName" ), suiteName_ );
    payload.insert( QStringLiteral( "suiteId" ), suiteId_ );
    payload.insert( QStringLiteral( "currentScenarioName" ), currentScenarioName_ );
    payload.insert( QStringLiteral( "currentScenarioFile" ), currentScenarioFile_ );
    payload.insert( QStringLiteral( "currentStepName" ), currentStepName_ );
    payload.insert( QStringLiteral( "totalScenarios" ), totalScenarios_ );
    payload.insert( QStringLiteral( "completedScenarios" ), completedScenarios_ );
    payload.insert( QStringLiteral( "passedCount" ), passedCount_ );
    payload.insert( QStringLiteral( "failedCount" ), failedCount_ );
    payload.insert( QStringLiteral( "skippedCount" ), skippedCount_ );
    payload.insert( QStringLiteral( "reportJsonFile" ), reportJsonPath_ );
    payload.insert( QStringLiteral( "reportJunitFile" ), reportJunitPath_ );
    payload.insert( QStringLiteral( "lastError" ), lastError_ );
    payload.insert( QStringLiteral( "lastCallbackError" ), lastCallbackError_ );
    payload.insert( QStringLiteral( "dispatchState" ), dispatchState_ );
    payload.insert( QStringLiteral( "droppedEvents" ), droppedEvents_ );
    payload.insert( QStringLiteral( "exitCode" ), exitCode_ );

    QVariantList subscriptions;
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
        if ( !subscription.sourceType.isEmpty() ) {
            item.insert( QStringLiteral( "sourceType" ), subscription.sourceType );
        }
        if ( subscription.responseId ) {
            item.insert( QStringLiteral( "responseId" ), *subscription.responseId );
        }
        if ( !subscription.responseName.isEmpty() ) {
            item.insert( QStringLiteral( "responseName" ), subscription.responseName );
        }
        if ( subscription.actionId ) {
            item.insert( QStringLiteral( "actionId" ), *subscription.actionId );
        }
        if ( !subscription.actionName.isEmpty() ) {
            item.insert( QStringLiteral( "actionName" ), subscription.actionName );
        }
        subscriptions.push_back( item );
    }
    payload.insert( QStringLiteral( "subscriptions" ), subscriptions );

    if ( startedAt_.isValid() ) {
        payload.insert( QStringLiteral( "startedAt" ), startedAt_.toString( Qt::ISODateWithMs ) );
    }
    if ( finishedAt_.isValid() ) {
        payload.insert( QStringLiteral( "finishedAt" ), finishedAt_.toString( Qt::ISODateWithMs ) );
    }

    QVariantList outputTail;
    for ( const auto& line : outputTail_ ) {
        outputTail.push_back( line );
    }
    payload.insert( QStringLiteral( "outputTail" ), outputTail );
    return payload;
}

QVariantMap ScenarioRunner::reportPayload() const
{
    auto payload = lastReportPayload_;
    if ( !reportJsonPath_.isEmpty() ) {
        payload.insert( QStringLiteral( "reportJsonFile" ), reportJsonPath_ );
    }
    if ( !reportJunitPath_.isEmpty() ) {
        payload.insert( QStringLiteral( "reportJunitFile" ), reportJunitPath_ );
    }
    return payload;
}

void ScenarioRunner::publishEvent( const QVariantMap& event )
{
    const auto eventType = event.value( QStringLiteral( "eventType" ) ).toString();
    if ( eventType.isEmpty() || socket_ == nullptr ) {
        return;
    }

    const auto shouldSend = std::any_of(
        subscriptions_.cbegin(), subscriptions_.cend(),
        [ this, &event ]( const Subscription& subscription ) {
            return eventMatchesSubscription( event, subscription );
        } );
    if ( !shouldSend ) {
        return;
    }

    QVariantMap message;
    message.insert( QStringLiteral( "type" ), QStringLiteral( "event" ) );
    message.insert( QStringLiteral( "event" ), event );
    sendRpcMessage( message );
}

QString ScenarioRunner::resolvePythonExecutable() const
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

QString ScenarioRunner::runtimeRoot() const
{
    const auto root = QDir( QCoreApplication::applicationDirPath() )
                          .filePath( QStringLiteral( "python_runtime" ) );
    return QFileInfo::exists( root ) ? root : QString{};
}

QString ScenarioRunner::scenarioBootstrapPath() const
{
    const auto runtime = runtimeRoot();
    if ( runtime.isEmpty() ) {
        return {};
    }
    return QDir( runtime ).filePath( QStringLiteral( "scenario_bootstrap.py" ) );
}

bool ScenarioRunner::beginListening( QString* errorMessage )
{
    server_->close();
    if ( socket_ != nullptr ) {
        socket_->deleteLater();
        socket_ = nullptr;
    }

    if ( !server_->listen( QHostAddress::LocalHost ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = tr( "Failed to create local scenario RPC server: %1" )
                                .arg( server_->errorString() );
        }
        return false;
    }

    return true;
}

void ScenarioRunner::resetSocketState()
{
    if ( socket_ != nullptr ) {
        socket_->deleteLater();
        socket_ = nullptr;
    }
    server_->close();
    socketBuffer_.clear();
}

bool ScenarioRunner::startProcess( const QString& bootstrapPath )
{
    QString errorMessage;
    if ( !beginListening( &errorMessage ) ) {
        lastError_ = errorMessage;
        setState( ScenarioRunState::Failed );
        return false;
    }

    const auto pythonExecutable = resolvePythonExecutable();
    if ( pythonExecutable.isEmpty() ) {
        resetSocketState();
        lastError_ = tr( "No Python runtime was found for scenario execution." );
        setState( ScenarioRunState::Failed );
        return false;
    }

    if ( !QFileInfo::exists( bootstrapPath ) ) {
        resetSocketState();
        lastError_ = tr( "Scenario bootstrap was not found at %1." ).arg( bootstrapPath );
        setState( ScenarioRunState::Failed );
        return false;
    }

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
    lastReportPayload_.clear();
    startedAt_ = QDateTime::currentDateTimeUtc();
    finishedAt_ = {};
    authToken_ = randomToken();
    process_.setProcessChannelMode( QProcess::SeparateChannels );

    disconnect( server_, nullptr, this, nullptr );
    disconnect( &process_, nullptr, this, nullptr );

    connect( server_, &QTcpServer::newConnection, this, &ScenarioRunner::handleNewConnection );
    connect( &process_, &QProcess::started, this, &ScenarioRunner::handleProcessStarted );
    connect( &process_, qOverload<int, QProcess::ExitStatus>( &QProcess::finished ), this,
             &ScenarioRunner::handleProcessFinished );
    connect( &process_, &QProcess::errorOccurred, this, &ScenarioRunner::handleProcessError );
    connect( &process_, &QProcess::readyReadStandardOutput, this,
             &ScenarioRunner::readProcessStdout );
    connect( &process_, &QProcess::readyReadStandardError, this,
             &ScenarioRunner::readProcessStderr );

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const auto runtime = runtimeRoot();
    environment.insert( QStringLiteral( "CILOGG_SCRIPT_PORT" ),
                        QString::number( server_->serverPort() ) );
    environment.insert( QStringLiteral( "CILOGG_SCRIPT_TOKEN" ), authToken_ );
    if ( !scenarioFilePath_.isEmpty() ) {
        environment.insert( QStringLiteral( "CILOGG_SCENARIO_FILE" ), scenarioFilePath_ );
    }
    if ( !suiteFilePath_.isEmpty() ) {
        environment.insert( QStringLiteral( "CILOGG_SUITE_FILE" ), suiteFilePath_ );
    }
    if ( !argsJsonFilePath_.isEmpty() ) {
        environment.insert( QStringLiteral( "CILOGG_SCENARIO_ARGS_JSON_FILE" ), argsJsonFilePath_ );
        environment.insert( QStringLiteral( "CILOGG_SCRIPT_ARGS_JSON_FILE" ), argsJsonFilePath_ );
    }
    environment.insert( QStringLiteral( "CILOGG_SCENARIO_REPORT_JSON" ), reportJsonPath_ );
    environment.insert( QStringLiteral( "CILOGG_SCENARIO_REPORT_JUNIT" ), reportJunitPath_ );
    if ( !runtime.isEmpty() ) {
        environment.insert( QStringLiteral( "CILOGG_PYTHON_RUNTIME_ROOT" ), runtime );
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
    process_.setWorkingDirectory(
        QFileInfo( !scenarioFilePath_.isEmpty() ? scenarioFilePath_ : suiteFilePath_ ).absolutePath() );
    setState( ScenarioRunState::Starting );
    process_.start();

    if ( !process_.waitForStarted( 5000 ) ) {
        resetSocketState();
        lastError_ = process_.errorString().isEmpty()
                         ? tr( "Failed to start the Python scenario worker." )
                         : process_.errorString();
        setState( ScenarioRunState::Failed );
        return false;
    }

    return true;
}

void ScenarioRunner::handleNewConnection()
{
    if ( socket_ != nullptr ) {
        if ( auto* extraSocket = server_->nextPendingConnection() ) {
            extraSocket->close();
            extraSocket->deleteLater();
        }
        return;
    }

    socket_ = server_->nextPendingConnection();
    if ( socket_ == nullptr ) {
        return;
    }

    connect( socket_, &QTcpSocket::readyRead, this, &ScenarioRunner::readSocketData );
    connect( socket_, &QTcpSocket::disconnected, this, [ this ]() {
        if ( socket_ != nullptr ) {
            socket_->deleteLater();
            socket_ = nullptr;
        }
    } );
}

void ScenarioRunner::handleProcessStarted()
{
    setState( ScenarioRunState::Running );
}

void ScenarioRunner::handleProcessFinished( int exitCode, QProcess::ExitStatus exitStatus )
{
    exitCode_ = exitCode;
    finishedAt_ = QDateTime::currentDateTimeUtc();

    if ( !pendingStdout_.isEmpty() ) {
        appendOutputChunk( {}, false );
    }
    if ( !pendingStderr_.isEmpty() ) {
        appendOutputChunk( {}, true );
    }

    if ( state_ == ScenarioRunState::Stopping || stopRequested_ ) {
        setState( ScenarioRunState::Cancelled );
        if ( lastReportPayload_.isEmpty() ) {
            lastReportPayload_ = buildSyntheticReport();
        }
    }
    else if ( exitStatus == QProcess::NormalExit && exitCode == 0 ) {
        setState( ScenarioRunState::Finished );
    }
    else {
        if ( lastError_.isEmpty() ) {
            lastError_ = tr( "Scenario process exited with code %1." ).arg( exitCode );
        }
        if ( lastReportPayload_.isEmpty() ) {
            lastReportPayload_ = buildSyntheticReport();
        }
        setState( ScenarioRunState::Failed );
    }

    currentStepName_.clear();
    dispatchState_ = QStringLiteral( "idle" );
    resetSocketState();
    Q_EMIT statusChanged();
}

void ScenarioRunner::handleProcessError( QProcess::ProcessError error )
{
    Q_UNUSED( error );
    lastError_ = process_.errorString();
    if ( state_ != ScenarioRunState::Stopping ) {
        setState( ScenarioRunState::Failed );
    }
}

void ScenarioRunner::readProcessStdout()
{
    appendOutputChunk( QString::fromUtf8( process_.readAllStandardOutput() ), false );
}

void ScenarioRunner::readProcessStderr()
{
    appendOutputChunk( QString::fromUtf8( process_.readAllStandardError() ), true );
}

void ScenarioRunner::readSocketData()
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
            appendOutputLine( tr( "[scenario-rpc] invalid message: %1" ).arg( QString::fromUtf8( line ) ) );
            continue;
        }

        handleRpcMessage( document.object().toVariantMap() );
    }
}

void ScenarioRunner::handleRpcMessage( const QVariantMap& message )
{
    const auto requestId = message.value( QStringLiteral( "id" ) ).toInt();
    const auto token = message.value( QStringLiteral( "token" ) ).toString();
    const auto isNotification = message.value( QStringLiteral( "notification" ) ).toBool();
    if ( token != authToken_ ) {
        if ( !isNotification ) {
            sendRpcMessage( rpcErrorEnvelope( requestId, QStringLiteral( "unauthorized" ) ) );
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
        sendRpcMessage( response );
        return;
    }

    const auto params = message.value( QStringLiteral( "params" ) ).toMap();

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
        Q_EMIT statusChanged();
        return;
    }

    if ( method == QStringLiteral( "report_event_stats" ) ) {
        droppedEvents_ = params.value( QStringLiteral( "droppedEvents" ) ).toInt();
        Q_EMIT statusChanged();
        return;
    }

    if ( method == QStringLiteral( "report_scenario_status" ) ) {
        suiteName_ = params.value( QStringLiteral( "suiteName" ), suiteName_ ).toString();
        suiteId_ = params.value( QStringLiteral( "suiteId" ), suiteId_ ).toString();
        currentScenarioName_
            = params.value( QStringLiteral( "currentScenarioName" ), currentScenarioName_ ).toString();
        currentScenarioFile_
            = params.value( QStringLiteral( "currentScenarioFile" ), currentScenarioFile_ ).toString();
        currentStepName_
            = params.value( QStringLiteral( "currentStepName" ), currentStepName_ ).toString();
        totalScenarios_ = params.value( QStringLiteral( "totalScenarios" ), totalScenarios_ ).toInt();
        completedScenarios_
            = params.value( QStringLiteral( "completedScenarios" ), completedScenarios_ ).toInt();
        passedCount_ = params.value( QStringLiteral( "passedCount" ), passedCount_ ).toInt();
        failedCount_ = params.value( QStringLiteral( "failedCount" ), failedCount_ ).toInt();
        skippedCount_ = params.value( QStringLiteral( "skippedCount" ), skippedCount_ ).toInt();
        if ( params.contains( QStringLiteral( "reportJsonFile" ) ) ) {
            reportJsonPath_ = params.value( QStringLiteral( "reportJsonFile" ) ).toString();
        }
        if ( params.contains( QStringLiteral( "reportJunitFile" ) ) ) {
            reportJunitPath_ = params.value( QStringLiteral( "reportJunitFile" ) ).toString();
        }
        Q_EMIT statusChanged();
        return;
    }

    if ( method == QStringLiteral( "report_scenario_report" ) ) {
        lastReportPayload_ = params;
        if ( params.contains( QStringLiteral( "reportJsonFile" ) ) ) {
            reportJsonPath_ = params.value( QStringLiteral( "reportJsonFile" ) ).toString();
        }
        if ( params.contains( QStringLiteral( "reportJunitFile" ) ) ) {
            reportJunitPath_ = params.value( QStringLiteral( "reportJunitFile" ) ).toString();
        }
        Q_EMIT statusChanged();
        return;
    }

    if ( method == QStringLiteral( "subscribe_event" ) ) {
        Subscription subscription;
        subscription.tabId = params.value( QStringLiteral( "tabId" ) ).toString();
        subscription.windowIndex = params.value( QStringLiteral( "windowIndex" ), -1 ).toInt();
        subscription.tabIndex = params.value( QStringLiteral( "tabIndex" ), -1 ).toInt();
        subscription.windowId = params.value( QStringLiteral( "windowId" ) ).toString();
        subscription.filePath = params.value( QStringLiteral( "filePath" ) ).toString();
        subscription.displayName = params.value( QStringLiteral( "displayName" ) ).toString();
        subscription.portName = params.value( QStringLiteral( "portName" ) ).toString().trimmed();
        subscription.eventType = params.value( QStringLiteral( "eventType" ) ).toString().trimmed();
        subscription.sourceType = params.value( QStringLiteral( "sourceType" ) ).toString();
        if ( params.contains( QStringLiteral( "responseId" ) ) ) {
            subscription.responseId = params.value( QStringLiteral( "responseId" ) ).toInt();
        }
        subscription.responseName = params.value( QStringLiteral( "responseName" ) ).toString().trimmed();
        if ( params.contains( QStringLiteral( "actionId" ) ) ) {
            subscription.actionId = params.value( QStringLiteral( "actionId" ) ).toInt();
        }
        subscription.actionName = params.value( QStringLiteral( "actionName" ) ).toString().trimmed();
        subscriptions_.push_back( subscription );
        Q_EMIT statusChanged();

        if ( !isNotification ) {
            QVariantMap response;
            response.insert( QStringLiteral( "id" ), requestId );
            response.insert( QStringLiteral( "ok" ), true );
            response.insert( QStringLiteral( "result" ), params );
            sendRpcMessage( response );
        }
        return;
    }

    if ( method == QStringLiteral( "clear_event_handlers" ) ) {
        subscriptions_.clear();
        Q_EMIT statusChanged();
        if ( !isNotification ) {
            QVariantMap response;
            response.insert( QStringLiteral( "id" ), requestId );
            response.insert( QStringLiteral( "ok" ), true );
            response.insert( QStringLiteral( "result" ), QVariantMap{} );
            sendRpcMessage( response );
        }
        return;
    }

    if ( method != QStringLiteral( "command" ) ) {
        if ( !isNotification ) {
            sendRpcMessage( rpcErrorEnvelope( requestId, QStringLiteral( "unsupported method" ) ) );
        }
        return;
    }

    if ( !commanderExecutor_ ) {
        sendRpcMessage( rpcErrorEnvelope( requestId, QStringLiteral( "no commander executor" ) ) );
        return;
    }

    QString errorMessage;
    const auto request = commanderRequestFromVariantMap( params, &errorMessage );
    if ( !request ) {
        sendRpcMessage( rpcErrorEnvelope( requestId, errorMessage ) );
        return;
    }

    const auto result = commanderExecutor_( *request );
    sendRpcMessage( rpcResultEnvelope( requestId, result ) );
}

void ScenarioRunner::sendRpcMessage( const QVariantMap& response )
{
    if ( socket_ == nullptr ) {
        return;
    }

    const auto bytes = QJsonDocument::fromVariant( response ).toJson( QJsonDocument::Compact );
    socket_->write( bytes );
    socket_->write( "\n" );
    socket_->flush();
}

QVariantMap ScenarioRunner::rpcResultEnvelope( int requestId, const CommanderResult& result ) const
{
    QVariantMap response;
    response.insert( QStringLiteral( "id" ), requestId );
    response.insert( QStringLiteral( "ok" ), true );
    response.insert( QStringLiteral( "result" ), commanderResultToVariantMap( result ) );
    return response;
}

QVariantMap ScenarioRunner::rpcErrorEnvelope( int requestId, const QString& errorText ) const
{
    QVariantMap response;
    response.insert( QStringLiteral( "id" ), requestId );
    response.insert( QStringLiteral( "ok" ), false );
    response.insert( QStringLiteral( "error" ), errorText );
    return response;
}

void ScenarioRunner::appendOutputChunk( const QString& chunk, bool stderrStream )
{
    QString* pending = stderrStream ? &pendingStderr_ : &pendingStdout_;
    pending->append( chunk );

    qsizetype newlineIndex = -1;
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

void ScenarioRunner::appendOutputLine( const QString& line )
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

void ScenarioRunner::setState( ScenarioRunState state )
{
    state_ = state;
    Q_EMIT statusChanged();
}

void ScenarioRunner::resetRunState()
{
    if ( socket_ != nullptr ) {
        socket_->disconnectFromHost();
    }
    if ( process_.state() != QProcess::NotRunning ) {
        process_.kill();
        process_.waitForFinished( 2000 );
    }
    resetSocketState();
    outputTail_.clear();
    pendingStdout_.clear();
    pendingStderr_.clear();
    authToken_.clear();
    lastError_.clear();
    lastCallbackError_.clear();
    dispatchState_ = QStringLiteral( "idle" );
    droppedEvents_ = 0;
    startedAt_ = {};
    finishedAt_ = {};
    stopRequested_ = false;
    exitCode_ = 0;
    scenarioFilePath_.clear();
    suiteFilePath_.clear();
    argsJsonFilePath_.clear();
    suiteName_.clear();
    suiteId_.clear();
    currentScenarioName_.clear();
    currentScenarioFile_.clear();
    currentStepName_.clear();
    totalScenarios_ = 0;
    completedScenarios_ = 0;
    passedCount_ = 0;
    failedCount_ = 0;
    skippedCount_ = 0;
    reportJsonPath_.clear();
    reportJunitPath_.clear();
    lastReportPayload_.clear();
    subscriptions_.clear();
    state_ = ScenarioRunState::Idle;
}

bool ScenarioRunner::eventMatchesSubscription( const QVariantMap& event,
                                               const Subscription& subscription ) const
{
    if ( subscription.eventType != event.value( QStringLiteral( "eventType" ) ).toString() ) {
        return false;
    }

    if ( !subscription.tabId.isEmpty()
         && subscription.tabId != event.value( QStringLiteral( "tabId" ) ).toString() ) {
        return false;
    }

    if ( !subscription.portName.isEmpty()
         && subscription.portName.compare( event.value( QStringLiteral( "portName" ) ).toString(),
                                           Qt::CaseInsensitive )
                != 0 ) {
        return false;
    }

    if ( !subscription.sourceType.isEmpty()
         && subscription.sourceType
                != event.value( QStringLiteral( "sourceType" ) ).toString() ) {
        return false;
    }

    if ( subscription.eventType == QLatin1String( "response" ) ) {
        if ( subscription.responseId
             && *subscription.responseId != event.value( QStringLiteral( "responseId" ) ).toInt() ) {
            return false;
        }
        return matchesName( subscription.responseName,
                            event.value( QStringLiteral( "responseName" ) ).toString() );
    }

    if ( subscription.eventType == QLatin1String( "action_send" ) ) {
        if ( subscription.actionId
             && *subscription.actionId != event.value( QStringLiteral( "actionId" ) ).toInt() ) {
            return false;
        }
        return matchesName( subscription.actionName,
                            event.value( QStringLiteral( "actionName" ) ).toString() );
    }

    return true;
}

QVariantMap ScenarioRunner::buildSyntheticReport() const
{
    QVariantMap counts;
    counts.insert( QStringLiteral( "total" ), totalScenarios_ );
    counts.insert( QStringLiteral( "completed" ), completedScenarios_ );
    counts.insert( QStringLiteral( "passed" ), passedCount_ );
    counts.insert( QStringLiteral( "failed" ), failedCount_ );
    counts.insert( QStringLiteral( "skipped" ), skippedCount_ );

    QVariantMap report;
    report.insert( QStringLiteral( "state" ),
                   state_ == ScenarioRunState::Cancelled ? QStringLiteral( "cancelled" )
                                                         : QStringLiteral( "failed" ) );
    report.insert( QStringLiteral( "suiteName" ), suiteName_ );
    report.insert( QStringLiteral( "suiteId" ), suiteId_ );
    report.insert( QStringLiteral( "suiteFile" ), suiteFilePath_ );
    report.insert( QStringLiteral( "scenarioFile" ), scenarioFilePath_ );
    report.insert( QStringLiteral( "currentScenarioName" ), currentScenarioName_ );
    report.insert( QStringLiteral( "currentScenarioFile" ), currentScenarioFile_ );
    report.insert( QStringLiteral( "currentStepName" ), currentStepName_ );
    report.insert( QStringLiteral( "counts" ), counts );
    report.insert( QStringLiteral( "reportJsonFile" ), reportJsonPath_ );
    report.insert( QStringLiteral( "reportJunitFile" ), reportJunitPath_ );
    report.insert( QStringLiteral( "error" ), lastError_ );
    return report;
}
