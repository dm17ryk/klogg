#include "scriptsupervisor.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

#include "log.h"

namespace {

constexpr int OutputTailLimit = 200;
constexpr auto ReceiveEventType = "receive";
constexpr auto ResponseEventType = "response";
constexpr auto TxEventType = "tx";
constexpr auto ActionSendEventType = "action_send";
constexpr auto TabOpenEventType = "tab_open";
constexpr auto TabCloseEventType = "tab_close";
constexpr auto CommStartEventType = "comm_start";
constexpr auto CommStopEventType = "comm_stop";

QString randomToken()
{
    return QString::number( QRandomGenerator::global()->generate64(), 16 )
           + QString::number( QRandomGenerator::global()->generate64(), 16 );
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

bool isActiveState( ScriptRunState state )
{
    return state == ScriptRunState::Starting || state == ScriptRunState::Running
           || state == ScriptRunState::Stopping;
}

bool matchesName( const QString& expected, const QString& actual )
{
    return expected.isEmpty() || expected.compare( actual, Qt::CaseInsensitive ) == 0;
}

} // namespace

struct ScriptSupervisor::ScriptRun {
    ScriptRunScope scope = ScriptRunScope::Tab;
    QString ownerTabId;
    int ownerWindowIndex = -1;
    int ownerTabIndex = -1;
    QString ownerWindowId;
    QString ownerFilePath;
    QString ownerDisplayName;
    QString ownerPortName;

    QString scriptFilePath;
    QString argsJsonFilePath;
    bool enabled = true;
    bool autostart = true;
    bool broken = false;

    QProcess process;
    QTcpServer server;
    QTcpSocket* socket = nullptr;
    QByteArray socketBuffer;
    QString authToken;
    QStringList outputTail;
    QString pendingStdout;
    QString pendingStderr;
    QString lastError;
    QString lastCallbackError;
    QDateTime startedAt;
    QDateTime finishedAt;
    ScriptRunState state = ScriptRunState::Idle;
    int exitCode = 0;
    bool stopRequested = false;
    int droppedEvents = 0;
    QString dispatchState = QStringLiteral( "idle" );
    QVector<ScriptSubscription> subscriptions;
};

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
}

ScriptSupervisor::~ScriptSupervisor() = default;

void ScriptSupervisor::setCommanderExecutor(
    std::function<CommanderResult( const CommanderRequest& )> executor )
{
    commanderExecutor_ = std::move( executor );
}

CommanderResult ScriptSupervisor::runScript( const CommanderRequest& request )
{
    const QFileInfo scriptInfo( request.scriptFilePath );
    if ( !scriptInfo.exists() || !scriptInfo.isFile() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Python script %1 was not found." ).arg( request.scriptFilePath ) );
    }

    if ( !request.argsJsonFilePath.isEmpty() ) {
        const QFileInfo argsInfo( request.argsJsonFilePath );
        if ( !argsInfo.exists() || !argsInfo.isFile() ) {
            return commanderFailure( CommanderResultCode::NotFound,
                                     tr( "JSON args file %1 was not found." )
                                         .arg( request.argsJsonFilePath ) );
        }
    }

    QString errorMessage;
    const auto target = resolveTargetInfo( request, false, &errorMessage );
    if ( target.isEmpty() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 errorMessage.isEmpty()
                                     ? tr( "Requested live communication tab was not found." )
                                     : errorMessage );
    }

    removeRun( target.value( QStringLiteral( "tabId" ) ).toString() );

    auto run = std::make_unique<ScriptRun>();
    run->ownerTabId = target.value( QStringLiteral( "tabId" ) ).toString();
    run->ownerWindowIndex = target.value( QStringLiteral( "windowIndex" ) ).toInt();
    run->ownerTabIndex = target.value( QStringLiteral( "tabIndex" ) ).toInt();
    run->ownerWindowId = target.value( QStringLiteral( "windowId" ) ).toString();
    run->ownerFilePath = target.value( QStringLiteral( "filePath" ) ).toString();
    run->ownerDisplayName = target.value( QStringLiteral( "displayName" ) ).toString();
    run->ownerPortName = target.value( QStringLiteral( "portName" ) ).toString();
    run->scriptFilePath = scriptInfo.absoluteFilePath();
    run->argsJsonFilePath = request.argsJsonFilePath.isEmpty()
                                ? QString{}
                                : QFileInfo( request.argsJsonFilePath ).absoluteFilePath();

    const auto tabId = run->ownerTabId;
    runs_.push_back( std::move( run ) );
    auto* createdRun = findRun( tabId );
    startRunProcess( createdRun );
    return commanderSuccess();
}

CommanderResult ScriptSupervisor::runGlobalScript( const CommanderRequest& request )
{
    const QFileInfo scriptInfo( request.scriptFilePath );
    if ( !scriptInfo.exists() || !scriptInfo.isFile() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Python script %1 was not found." ).arg( request.scriptFilePath ) );
    }

    if ( !request.argsJsonFilePath.isEmpty() ) {
        const QFileInfo argsInfo( request.argsJsonFilePath );
        if ( !argsInfo.exists() || !argsInfo.isFile() ) {
            return commanderFailure( CommanderResultCode::NotFound,
                                     tr( "JSON args file %1 was not found." )
                                         .arg( request.argsJsonFilePath ) );
        }
    }

    if ( findGlobalRun() != nullptr ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "A global Python script is already configured or running." ) );
    }

    auto run = std::make_unique<ScriptRun>();
    run->scope = ScriptRunScope::Global;
    run->ownerDisplayName = tr( "Global Coordinator" );
    run->scriptFilePath = scriptInfo.absoluteFilePath();
    run->argsJsonFilePath = request.argsJsonFilePath.isEmpty()
                                ? QString{}
                                : QFileInfo( request.argsJsonFilePath ).absoluteFilePath();

    runs_.push_back( std::move( run ) );
    auto* createdRun = findGlobalRun();
    startRunProcess( createdRun );
    return commanderSuccess();
}

CommanderResult ScriptSupervisor::stopScript( const CommanderRequest& request )
{
    if ( request.allEntities ) {
        bool stoppedAny = false;
        for ( const auto& run : runs_ ) {
            if ( run && isActiveState( run->state ) ) {
                run->stopRequested = true;
                setState( run.get(), ScriptRunState::Stopping );
                run->process.kill();
                stoppedAny = true;
            }
        }
        return stoppedAny
                   ? commanderSuccess()
                   : commanderFailure( CommanderResultCode::NotFound,
                                       tr( "No Python scripts are currently running." ) );
    }

    auto* run = findRunForRequest( request );
    if ( run == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No Python script was found for the requested tab." ) );
    }

    if ( !isActiveState( run->state ) ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No Python script is currently running for the requested tab." ) );
    }

    run->stopRequested = true;
    setState( run, ScriptRunState::Stopping );
    run->process.kill();
    return commanderSuccess();
}

CommanderResult ScriptSupervisor::stopGlobalScript()
{
    auto* run = findGlobalRun();
    if ( run == nullptr || !isActiveState( run->state ) ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No global Python script is currently running." ) );
    }

    run->stopRequested = true;
    setState( run, ScriptRunState::Stopping );
    run->process.kill();
    return commanderSuccess();
}

CommanderResult ScriptSupervisor::scriptStatus( const CommanderRequest& request ) const
{
    if ( request.allEntities ) {
        return commanderSuccess( {}, QVariantMap{ { QStringLiteral( "runs" ), allStatusPayloads() } } );
    }

    const auto* run = findRunForRequest( request );
    if ( run == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No Python script was found for the requested tab." ) );
    }

    return commanderSuccess( {}, statusPayload( run ) );
}

CommanderResult ScriptSupervisor::globalScriptStatus() const
{
    const auto* run = findGlobalRun();
    if ( run == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No global Python script was found." ) );
    }

    return commanderSuccess( {}, statusPayload( run ) );
}

CommanderResult ScriptSupervisor::scriptSubscriptions( const CommanderRequest& request ) const
{
    if ( request.allEntities ) {
        return commanderSuccess( {},
                                 QVariantMap{ { QStringLiteral( "runs" ),
                                                allSubscriptionsPayloads() } } );
    }

    const auto* run = findRunForRequest( request );
    if ( run == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No Python script was found for the requested tab." ) );
    }

    QVariantMap payload;
    payload.insert( QStringLiteral( "tabId" ), run->ownerTabId );
    payload.insert( QStringLiteral( "windowIndex" ), run->ownerWindowIndex );
    payload.insert( QStringLiteral( "tabIndex" ), run->ownerTabIndex );
    payload.insert( QStringLiteral( "subscriptions" ), subscriptionsPayload( run ) );
    return commanderSuccess( {}, payload );
}

CommanderResult ScriptSupervisor::globalScriptSubscriptions() const
{
    const auto* run = findGlobalRun();
    if ( run == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No global Python script was found." ) );
    }

    QVariantMap payload;
    payload.insert( QStringLiteral( "scope" ), QStringLiteral( "global" ) );
    payload.insert( QStringLiteral( "subscriptions" ), subscriptionsPayload( run ) );
    return commanderSuccess( {}, payload );
}

CommanderResult ScriptSupervisor::clearScriptSubscriptions( const CommanderRequest& request )
{
    if ( request.allEntities ) {
        for ( const auto& run : runs_ ) {
            if ( run ) {
                run->subscriptions.clear();
            }
        }
        Q_EMIT statusChanged();
        return commanderSuccess();
    }

    auto* run = findRunForRequest( request );
    if ( run == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No Python script was found for the requested tab." ) );
    }

    run->subscriptions.clear();
    Q_EMIT statusChanged();
    return commanderSuccess();
}

CommanderResult ScriptSupervisor::clearGlobalScriptSubscriptions()
{
    auto* run = findGlobalRun();
    if ( run == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "No global Python script was found." ) );
    }

    run->subscriptions.clear();
    Q_EMIT statusChanged();
    return commanderSuccess();
}

void ScriptSupervisor::publishEvent( const QVariantMap& event )
{
    const auto eventType = event.value( QStringLiteral( "eventType" ) ).toString();
    if ( eventType.isEmpty() ) {
        return;
    }

    for ( const auto& run : runs_ ) {
        if ( !run || run->socket == nullptr ) {
            continue;
        }

        const auto shouldSend = std::any_of(
            run->subscriptions.cbegin(), run->subscriptions.cend(),
            [ this, &event ]( const ScriptSubscription& subscription ) {
                return eventMatchesSubscription( event, subscription );
            } );
        if ( !shouldSend ) {
            continue;
        }

        QVariantMap message;
        message.insert( QStringLiteral( "type" ), QStringLiteral( "event" ) );
        message.insert( QStringLiteral( "event" ), event );
        sendRpcMessage( run.get(), message );
    }
}

bool ScriptSupervisor::hasActiveScripts() const
{
    return std::any_of( runs_.cbegin(), runs_.cend(),
                        []( const auto& run ) { return run && isActiveState( run->state ); } );
}

bool ScriptSupervisor::hasActiveScriptForTab( const QString& tabId ) const
{
    const auto* run = findRun( tabId );
    return run != nullptr && isActiveState( run->state );
}

QVariantMap ScriptSupervisor::scriptStatusForTab( const QString& tabId ) const
{
    if ( const auto* run = findRun( tabId ) ) {
        return statusPayload( run );
    }
    return {};
}

QVariantMap ScriptSupervisor::scriptBindingForTab( const QString& tabId ) const
{
    if ( const auto* run = findRun( tabId ) ) {
        QVariantMap binding;
        binding.insert( QStringLiteral( "tabId" ), run->ownerTabId );
        binding.insert( QStringLiteral( "windowIndex" ), run->ownerWindowIndex );
        binding.insert( QStringLiteral( "tabIndex" ), run->ownerTabIndex );
        binding.insert( QStringLiteral( "windowId" ), run->ownerWindowId );
        binding.insert( QStringLiteral( "filePath" ), run->ownerFilePath );
        binding.insert( QStringLiteral( "displayName" ), run->ownerDisplayName );
        binding.insert( QStringLiteral( "portName" ), run->ownerPortName );
        binding.insert( QStringLiteral( "scriptFile" ), run->scriptFilePath );
        binding.insert( QStringLiteral( "argsJsonFile" ), run->argsJsonFilePath );
        binding.insert( QStringLiteral( "enabled" ), run->enabled );
        binding.insert( QStringLiteral( "autostart" ), run->autostart );
        binding.insert( QStringLiteral( "broken" ), run->broken );
        binding.insert( QStringLiteral( "lastError" ), run->lastError );
        return binding;
    }
    return {};
}

QVariantMap ScriptSupervisor::globalScriptStatusPayload() const
{
    if ( const auto* run = findGlobalRun() ) {
        return statusPayload( run );
    }
    return {};
}

QVariantMap ScriptSupervisor::globalScriptBinding() const
{
    if ( const auto* run = findGlobalRun() ) {
        QVariantMap binding;
        binding.insert( QStringLiteral( "scriptFile" ), run->scriptFilePath );
        binding.insert( QStringLiteral( "argsJsonFile" ), run->argsJsonFilePath );
        binding.insert( QStringLiteral( "enabled" ), run->enabled );
        binding.insert( QStringLiteral( "autostart" ), run->autostart );
        binding.insert( QStringLiteral( "broken" ), run->broken );
        binding.insert( QStringLiteral( "lastError" ), run->lastError );
        return binding;
    }
    return {};
}

void ScriptSupervisor::restoreScriptBinding( const QVariantMap& binding )
{
    const auto tabId = binding.value( QStringLiteral( "tabId" ) ).toString();
    if ( tabId.isEmpty() ) {
        return;
    }

    removeRun( tabId );

    auto run = std::make_unique<ScriptRun>();
    run->ownerTabId = tabId;
    run->ownerWindowIndex = binding.value( QStringLiteral( "windowIndex" ), -1 ).toInt();
    run->ownerTabIndex = binding.value( QStringLiteral( "tabIndex" ), -1 ).toInt();
    run->ownerWindowId = binding.value( QStringLiteral( "windowId" ) ).toString();
    run->ownerFilePath = binding.value( QStringLiteral( "filePath" ) ).toString();
    run->ownerDisplayName = binding.value( QStringLiteral( "displayName" ) ).toString();
    run->ownerPortName = binding.value( QStringLiteral( "portName" ) ).toString();
    run->scriptFilePath = binding.value( QStringLiteral( "scriptFile" ) ).toString();
    run->argsJsonFilePath = binding.value( QStringLiteral( "argsJsonFile" ) ).toString();
    run->enabled = binding.value( QStringLiteral( "enabled" ), true ).toBool();
    run->autostart = binding.value( QStringLiteral( "autostart" ), true ).toBool();
    run->broken = binding.value( QStringLiteral( "broken" ), false ).toBool();
    run->lastError = binding.value( QStringLiteral( "lastError" ) ).toString();
    runs_.push_back( std::move( run ) );

    auto* restoredRun = findRun( tabId );
    if ( restoredRun == nullptr || restoredRun->scriptFilePath.isEmpty() ) {
        return;
    }

    if ( !restoredRun->enabled || !restoredRun->autostart ) {
        Q_EMIT statusChanged();
        return;
    }

    if ( !QFileInfo::exists( restoredRun->scriptFilePath ) ) {
        restoredRun->broken = true;
        restoredRun->lastError
            = tr( "Python script %1 was not found during session restore." ).arg( restoredRun->scriptFilePath );
        setState( restoredRun, ScriptRunState::Failed );
        return;
    }

    startRunProcess( restoredRun );
}

void ScriptSupervisor::restoreGlobalScriptBinding( const QVariantMap& binding )
{
    removeGlobalRun();

    auto run = std::make_unique<ScriptRun>();
    run->scope = ScriptRunScope::Global;
    run->ownerDisplayName = tr( "Global Coordinator" );
    run->scriptFilePath = binding.value( QStringLiteral( "scriptFile" ) ).toString();
    run->argsJsonFilePath = binding.value( QStringLiteral( "argsJsonFile" ) ).toString();
    run->enabled = binding.value( QStringLiteral( "enabled" ), true ).toBool();
    run->autostart = binding.value( QStringLiteral( "autostart" ), true ).toBool();
    run->broken = binding.value( QStringLiteral( "broken" ), false ).toBool();
    run->lastError = binding.value( QStringLiteral( "lastError" ) ).toString();
    runs_.push_back( std::move( run ) );

    auto* restoredRun = findGlobalRun();
    if ( restoredRun == nullptr || restoredRun->scriptFilePath.isEmpty() ) {
        return;
    }

    if ( !restoredRun->enabled || !restoredRun->autostart ) {
        Q_EMIT statusChanged();
        return;
    }

    if ( !QFileInfo::exists( restoredRun->scriptFilePath ) ) {
        restoredRun->broken = true;
        restoredRun->lastError
            = tr( "Python script %1 was not found during session restore." ).arg( restoredRun->scriptFilePath );
        setState( restoredRun, ScriptRunState::Failed );
        return;
    }

    startRunProcess( restoredRun );
}

void ScriptSupervisor::forgetTab( const QString& tabId )
{
    removeRun( tabId );
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
    const auto root = QDir( QCoreApplication::applicationDirPath() )
                          .filePath( QStringLiteral( "python_runtime" ) );
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

ScriptSupervisor::ScriptRun* ScriptSupervisor::findRun( const QString& tabId ) const
{
    const auto it = std::find_if( runs_.begin(), runs_.end(),
                                  [ &tabId ]( const auto& run ) {
                                      return run && run->ownerTabId == tabId;
                                  } );
    return it != runs_.end() ? it->get() : nullptr;
}

ScriptSupervisor::ScriptRun* ScriptSupervisor::findGlobalRun() const
{
    const auto it = std::find_if( runs_.begin(), runs_.end(),
                                  []( const auto& run ) {
                                      return run && run->scope == ScriptRunScope::Global;
                                  } );
    return it != runs_.end() ? it->get() : nullptr;
}

ScriptSupervisor::ScriptRun* ScriptSupervisor::findRunForRequest( const CommanderRequest& request ) const
{
    if ( !request.tabId.isEmpty() ) {
        return findRun( request.tabId );
    }

    QString errorMessage;
    const auto target = resolveTargetInfo( request, false, &errorMessage );
    if ( target.isEmpty() ) {
        return nullptr;
    }
    return findRun( target.value( QStringLiteral( "tabId" ) ).toString() );
}

void ScriptSupervisor::removeRun( const QString& tabId )
{
    const auto it = std::find_if( runs_.begin(), runs_.end(),
                                  [ &tabId ]( const auto& run ) {
                                      return run && run->ownerTabId == tabId;
                                  } );
    if ( it == runs_.end() ) {
        return;
    }

    auto& run = *it;
    if ( run->socket != nullptr ) {
        run->socket->disconnectFromHost();
        run->socket->deleteLater();
        run->socket = nullptr;
    }
    run->server.close();
    if ( run->process.state() != QProcess::NotRunning ) {
        run->process.kill();
        run->process.waitForFinished( 2000 );
    }
    runs_.erase( it );
    Q_EMIT statusChanged();
}

void ScriptSupervisor::removeGlobalRun()
{
    const auto it = std::find_if( runs_.begin(), runs_.end(),
                                  []( const auto& run ) {
                                      return run && run->scope == ScriptRunScope::Global;
                                  } );
    if ( it == runs_.end() ) {
        return;
    }

    auto& run = *it;
    if ( run->socket != nullptr ) {
        run->socket->disconnectFromHost();
        run->socket->deleteLater();
        run->socket = nullptr;
    }
    run->server.close();
    if ( run->process.state() != QProcess::NotRunning ) {
        run->process.kill();
        run->process.waitForFinished( 2000 );
    }
    runs_.erase( it );
    Q_EMIT statusChanged();
}

bool ScriptSupervisor::beginListening( ScriptRun* run, QString* errorMessage )
{
    run->server.close();
    if ( run->socket != nullptr ) {
        run->socket->deleteLater();
        run->socket = nullptr;
    }

    if ( !run->server.listen( QHostAddress::LocalHost ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = tr( "Failed to create local script RPC server: %1" )
                                .arg( run->server.errorString() );
        }
        return false;
    }

    return true;
}

void ScriptSupervisor::resetSocketState( ScriptRun* run )
{
    if ( run->socket != nullptr ) {
        run->socket->deleteLater();
        run->socket = nullptr;
    }
    run->server.close();
    run->socketBuffer.clear();
}

void ScriptSupervisor::appendOutputChunk( ScriptRun* run, const QString& chunk, bool stderrStream )
{
    QString* pending = stderrStream ? &run->pendingStderr : &run->pendingStdout;
    pending->append( chunk );

    qsizetype newlineIndex = -1;
    while ( ( newlineIndex = pending->indexOf( '\n' ) ) >= 0 ) {
        const auto line = pending->left( newlineIndex + 1 );
        pending->remove( 0, newlineIndex + 1 );
        appendOutputLine( run, trimTrailingNewline( stderrStream ? QStringLiteral( "stderr: %1" ).arg( line )
                                                                 : line ) );
    }

    if ( chunk.isEmpty() && !pending->isEmpty() ) {
        appendOutputLine( run, trimTrailingNewline( stderrStream ? QStringLiteral( "stderr: %1" ).arg( *pending )
                                                                 : *pending ) );
        pending->clear();
    }
}

void ScriptSupervisor::appendOutputLine( ScriptRun* run, const QString& line )
{
    if ( line.isEmpty() ) {
        return;
    }

    run->outputTail.push_back( line );
    while ( run->outputTail.size() > OutputTailLimit ) {
        run->outputTail.removeFirst();
    }
    Q_EMIT outputChanged();
}

void ScriptSupervisor::setState( ScriptRun* run, ScriptRunState state )
{
    run->state = state;
    Q_EMIT statusChanged();
}

QVariantMap ScriptSupervisor::statusPayload( const ScriptRun* run ) const
{
    QVariantMap payload;
    payload.insert( QStringLiteral( "scope" ),
                    run->scope == ScriptRunScope::Global ? QStringLiteral( "global" )
                                                         : QStringLiteral( "tab" ) );
    payload.insert( QStringLiteral( "tabId" ), run->ownerTabId );
    payload.insert( QStringLiteral( "windowIndex" ), run->ownerWindowIndex );
    payload.insert( QStringLiteral( "tabIndex" ), run->ownerTabIndex );
    payload.insert( QStringLiteral( "windowId" ), run->ownerWindowId );
    payload.insert( QStringLiteral( "filePath" ), run->ownerFilePath );
    payload.insert( QStringLiteral( "displayName" ), run->ownerDisplayName );
    payload.insert( QStringLiteral( "portName" ), run->ownerPortName );
    payload.insert( QStringLiteral( "state" ), scriptRunStateToString( run->state ) );
    payload.insert( QStringLiteral( "scriptFile" ), run->scriptFilePath );
    payload.insert( QStringLiteral( "argsJsonFile" ), run->argsJsonFilePath );
    payload.insert( QStringLiteral( "enabled" ), run->enabled );
    payload.insert( QStringLiteral( "autostart" ), run->autostart );
    payload.insert( QStringLiteral( "broken" ), run->broken );
    if ( run->startedAt.isValid() ) {
        payload.insert( QStringLiteral( "startedAt" ), run->startedAt.toString( Qt::ISODateWithMs ) );
    }
    if ( run->finishedAt.isValid() ) {
        payload.insert( QStringLiteral( "finishedAt" ), run->finishedAt.toString( Qt::ISODateWithMs ) );
    }
    payload.insert( QStringLiteral( "exitCode" ), run->exitCode );
    payload.insert( QStringLiteral( "lastError" ), run->lastError );
    payload.insert( QStringLiteral( "lastCallbackError" ), run->lastCallbackError );
    payload.insert( QStringLiteral( "droppedEvents" ), run->droppedEvents );
    payload.insert( QStringLiteral( "dispatchState" ), run->dispatchState );
    payload.insert( QStringLiteral( "subscriptions" ), subscriptionsPayload( run ) );

    QVariantList outputTail;
    for ( const auto& line : run->outputTail ) {
        outputTail.push_back( line );
    }
    payload.insert( QStringLiteral( "outputTail" ), outputTail );
    return payload;
}

QVariantList ScriptSupervisor::subscriptionsPayload( const ScriptRun* run ) const
{
    QVariantList payload;
    for ( const auto& subscription : run->subscriptions ) {
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
        payload.push_back( item );
    }
    return payload;
}

QVariantList ScriptSupervisor::allStatusPayloads() const
{
    QVariantList payloads;
    for ( const auto& run : runs_ ) {
        if ( run ) {
            payloads.push_back( statusPayload( run.get() ) );
        }
    }
    return payloads;
}

QVariantList ScriptSupervisor::allSubscriptionsPayloads() const
{
    QVariantList payloads;
    for ( const auto& run : runs_ ) {
        if ( !run ) {
            continue;
        }
        QVariantMap payload;
        payload.insert( QStringLiteral( "scope" ),
                        run->scope == ScriptRunScope::Global ? QStringLiteral( "global" )
                                                             : QStringLiteral( "tab" ) );
        payload.insert( QStringLiteral( "tabId" ), run->ownerTabId );
        payload.insert( QStringLiteral( "windowIndex" ), run->ownerWindowIndex );
        payload.insert( QStringLiteral( "tabIndex" ), run->ownerTabIndex );
        payload.insert( QStringLiteral( "subscriptions" ), subscriptionsPayload( run.get() ) );
        payloads.push_back( payload );
    }
    return payloads;
}

QVariantMap ScriptSupervisor::resolveTargetInfo( const CommanderRequest& request,
                                                 bool requireSelector,
                                                 QString* errorMessage ) const
{
    QVariantMap selector;
    if ( !request.tabId.isEmpty() ) {
        selector.insert( QStringLiteral( "tabId" ), request.tabId );
    }
    if ( request.windowIndex ) {
        selector.insert( QStringLiteral( "windowIndex" ), *request.windowIndex );
    }
    if ( request.tabIndex ) {
        selector.insert( QStringLiteral( "tabIndex" ), *request.tabIndex );
    }
    if ( requireSelector && selector.isEmpty() ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = tr( "A tab selector is required." );
        }
        return {};
    }
    return resolveTargetInfo( selector, errorMessage );
}

QVariantMap ScriptSupervisor::resolveTargetInfo( const QVariantMap& selector,
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

    auto matchTab = [ & ]( const QVariantMap& window, const QVariantMap& tab ) -> bool {
        if ( !requestedTabId.isEmpty() ) {
            return tab.value( QStringLiteral( "tabId" ) ).toString() == requestedTabId;
        }
        if ( requestedWindowIndex.isValid() || requestedTabIndex.isValid() ) {
            return requestedWindowIndex.isValid() && requestedTabIndex.isValid()
                   && window.value( QStringLiteral( "windowIndex" ) ).toInt() == requestedWindowIndex.toInt()
                   && tab.value( QStringLiteral( "tabIndex" ) ).toInt() == requestedTabIndex.toInt();
        }
        return window.value( QStringLiteral( "isActiveWindow" ) ).toBool()
               && window.value( QStringLiteral( "currentTabId" ) ).toString()
                      == tab.value( QStringLiteral( "tabId" ) ).toString();
    };

    for ( const auto& windowValue : windows ) {
        const auto window = windowValue.toMap();
        const auto tabs = window.value( QStringLiteral( "tabs" ) ).toList();
        for ( const auto& tabValue : tabs ) {
            const auto tab = tabValue.toMap();
            if ( !matchTab( window, tab ) ) {
                continue;
            }

            if ( tab.value( QStringLiteral( "sourceType" ) ).toString() != QStringLiteral( "com" ) ) {
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

QVariantMap ScriptSupervisor::lifecycleTargetInfo( const QVariantMap& selector,
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

    auto matchTab = [ & ]( const QVariantMap& window, const QVariantMap& tab ) -> bool {
        if ( !requestedTabId.isEmpty() ) {
            return tab.value( QStringLiteral( "tabId" ) ).toString() == requestedTabId;
        }
        if ( requestedWindowIndex.isValid() || requestedTabIndex.isValid() ) {
            return requestedWindowIndex.isValid() && requestedTabIndex.isValid()
                   && window.value( QStringLiteral( "windowIndex" ) ).toInt() == requestedWindowIndex.toInt()
                   && tab.value( QStringLiteral( "tabIndex" ) ).toInt() == requestedTabIndex.toInt();
        }
        return window.value( QStringLiteral( "isActiveWindow" ) ).toBool()
               && window.value( QStringLiteral( "currentTabId" ) ).toString()
                      == tab.value( QStringLiteral( "tabId" ) ).toString();
    };

    for ( const auto& windowValue : windows ) {
        const auto window = windowValue.toMap();
        const auto tabs = window.value( QStringLiteral( "tabs" ) ).toList();
        for ( const auto& tabValue : tabs ) {
            const auto tab = tabValue.toMap();
            if ( !matchTab( window, tab ) ) {
                continue;
            }

            QVariantMap target;
            target.insert( QStringLiteral( "tabId" ), tab.value( QStringLiteral( "tabId" ) ) );
            target.insert( QStringLiteral( "tabIndex" ), tab.value( QStringLiteral( "tabIndex" ) ) );
            target.insert( QStringLiteral( "windowId" ), window.value( QStringLiteral( "windowId" ) ) );
            target.insert( QStringLiteral( "windowIndex" ), window.value( QStringLiteral( "windowIndex" ) ) );
            target.insert( QStringLiteral( "filePath" ), tab.value( QStringLiteral( "filePath" ) ) );
            target.insert( QStringLiteral( "displayName" ), tab.value( QStringLiteral( "displayName" ) ) );
            target.insert( QStringLiteral( "sourceType" ), tab.value( QStringLiteral( "sourceType" ) ) );
            target.insert( QStringLiteral( "portName" ),
                           tab.value( QStringLiteral( "com" ) ).toMap().value( QStringLiteral( "portName" ) ) );
            return target;
        }
    }

    if ( errorMessage != nullptr ) {
        *errorMessage = tr( "Requested tab was not found." );
    }
    return {};
}

void ScriptSupervisor::startRunProcess( ScriptRun* run )
{
    QString errorMessage;
    if ( !beginListening( run, &errorMessage ) ) {
        run->lastError = errorMessage;
        run->broken = true;
        setState( run, ScriptRunState::Failed );
        return;
    }

    const auto pythonExecutable = resolvePythonExecutable();
    if ( pythonExecutable.isEmpty() ) {
        resetSocketState( run );
        run->lastError = tr( "No Python runtime was found for script execution." );
        run->broken = true;
        setState( run, ScriptRunState::Failed );
        return;
    }

    const auto bootstrapPath = workerBootstrapPath();
    if ( !QFileInfo::exists( bootstrapPath ) ) {
        resetSocketState( run );
        run->lastError = tr( "Python worker bootstrap was not found at %1." ).arg( bootstrapPath );
        run->broken = true;
        setState( run, ScriptRunState::Failed );
        return;
    }

    run->outputTail.clear();
    run->pendingStdout.clear();
    run->pendingStderr.clear();
    run->socketBuffer.clear();
    run->lastError.clear();
    run->lastCallbackError.clear();
    run->exitCode = 0;
    run->stopRequested = false;
    run->droppedEvents = 0;
    run->dispatchState = QStringLiteral( "idle" );
    run->subscriptions.clear();
    run->broken = false;
    run->startedAt = QDateTime::currentDateTimeUtc();
    run->finishedAt = {};
    run->authToken = randomToken();
    run->process.setProcessChannelMode( QProcess::SeparateChannels );

    disconnect( &run->server, nullptr, this, nullptr );
    disconnect( &run->process, nullptr, this, nullptr );

    connect( &run->server, &QTcpServer::newConnection, this,
             [ this, tabId = run->ownerTabId ]() { handleNewConnection( tabId ); } );
    connect( &run->process, &QProcess::started, this,
             [ this, tabId = run->ownerTabId ]() { handleProcessStarted( tabId ); } );
    connect( &run->process,
             qOverload<int, QProcess::ExitStatus>( &QProcess::finished ),
             this,
             [ this, tabId = run->ownerTabId ]( int exitCode, QProcess::ExitStatus exitStatus ) {
                 handleProcessFinished( tabId, exitCode, exitStatus );
             } );
    connect( &run->process, &QProcess::errorOccurred, this,
             [ this, tabId = run->ownerTabId ]( QProcess::ProcessError error ) {
                 handleProcessError( tabId, error );
             } );
    connect( &run->process, &QProcess::readyReadStandardOutput, this,
             [ this, tabId = run->ownerTabId ]() { readProcessStdout( tabId ); } );
    connect( &run->process, &QProcess::readyReadStandardError, this,
             [ this, tabId = run->ownerTabId ]() { readProcessStderr( tabId ); } );

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const auto runtime = runtimeRoot();
    environment.insert( QStringLiteral( "KLOGG_SCRIPT_PORT" ),
                        QString::number( run->server.serverPort() ) );
    environment.insert( QStringLiteral( "KLOGG_SCRIPT_TOKEN" ), run->authToken );
    environment.insert( QStringLiteral( "KLOGG_SCRIPT_FILE" ), run->scriptFilePath );
    if ( !run->argsJsonFilePath.isEmpty() ) {
        environment.insert( QStringLiteral( "KLOGG_SCRIPT_ARGS_JSON_FILE" ), run->argsJsonFilePath );
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

    run->process.setProcessEnvironment( environment );
    run->process.setProgram( pythonExecutable );
    run->process.setArguments( { bootstrapPath } );
    run->process.setWorkingDirectory( QFileInfo( run->scriptFilePath ).absolutePath() );
    setState( run, ScriptRunState::Starting );
    run->process.start();

    if ( !run->process.waitForStarted( 5000 ) ) {
        resetSocketState( run );
        run->lastError = run->process.errorString().isEmpty()
                             ? tr( "Failed to start the Python script worker." )
                             : run->process.errorString();
        run->broken = true;
        setState( run, ScriptRunState::Failed );
    }
}

void ScriptSupervisor::handleNewConnection( const QString& tabId )
{
    auto* run = findRun( tabId );
    if ( run == nullptr ) {
        return;
    }

    if ( run->socket != nullptr ) {
        if ( auto* extraSocket = run->server.nextPendingConnection() ) {
            extraSocket->close();
            extraSocket->deleteLater();
        }
        return;
    }

    run->socket = run->server.nextPendingConnection();
    if ( run->socket == nullptr ) {
        return;
    }

    connect( run->socket, &QTcpSocket::readyRead, this,
             [ this, tabId ]() { readSocketData( tabId ); } );
    connect( run->socket, &QTcpSocket::disconnected, this, [ this, tabId ]() {
        if ( auto* currentRun = findRun( tabId ); currentRun && currentRun->socket != nullptr ) {
            currentRun->socket->deleteLater();
            currentRun->socket = nullptr;
        }
    } );
}

void ScriptSupervisor::handleProcessStarted( const QString& tabId )
{
    if ( auto* run = findRun( tabId ) ) {
        setState( run, ScriptRunState::Running );
    }
}

void ScriptSupervisor::handleProcessFinished( const QString& tabId, int exitCode,
                                              QProcess::ExitStatus exitStatus )
{
    auto* run = findRun( tabId );
    if ( run == nullptr ) {
        return;
    }

    run->exitCode = exitCode;
    run->finishedAt = QDateTime::currentDateTimeUtc();

    if ( !run->pendingStdout.isEmpty() ) {
        appendOutputChunk( run, {}, false );
    }
    if ( !run->pendingStderr.isEmpty() ) {
        appendOutputChunk( run, {}, true );
    }

    if ( run->state == ScriptRunState::Stopping || run->stopRequested ) {
        setState( run, ScriptRunState::Cancelled );
    }
    else if ( exitStatus == QProcess::NormalExit && exitCode == 0 ) {
        setState( run, ScriptRunState::Finished );
    }
    else {
        if ( run->lastError.isEmpty() ) {
            run->lastError = tr( "Python script exited with code %1." ).arg( exitCode );
        }
        run->broken = true;
        setState( run, ScriptRunState::Failed );
    }

    run->subscriptions.clear();
    run->dispatchState = QStringLiteral( "idle" );
    resetSocketState( run );
    Q_EMIT statusChanged();
}

void ScriptSupervisor::handleProcessError( const QString& tabId, QProcess::ProcessError error )
{
    Q_UNUSED( error );
    if ( auto* run = findRun( tabId ) ) {
        run->lastError = run->process.errorString();
        run->broken = true;
        if ( run->state != ScriptRunState::Stopping ) {
            setState( run, ScriptRunState::Failed );
        }
    }
}

void ScriptSupervisor::readProcessStdout( const QString& tabId )
{
    if ( auto* run = findRun( tabId ) ) {
        appendOutputChunk( run, QString::fromUtf8( run->process.readAllStandardOutput() ), false );
    }
}

void ScriptSupervisor::readProcessStderr( const QString& tabId )
{
    if ( auto* run = findRun( tabId ) ) {
        appendOutputChunk( run, QString::fromUtf8( run->process.readAllStandardError() ), true );
    }
}

void ScriptSupervisor::readSocketData( const QString& tabId )
{
    auto* run = findRun( tabId );
    if ( run == nullptr || run->socket == nullptr ) {
        return;
    }

    run->socketBuffer.append( run->socket->readAll() );
    while ( true ) {
        const auto newlineIndex = run->socketBuffer.indexOf( '\n' );
        if ( newlineIndex < 0 ) {
            break;
        }

        const auto line = run->socketBuffer.left( newlineIndex );
        run->socketBuffer.remove( 0, newlineIndex + 1 );
        if ( line.trimmed().isEmpty() ) {
            continue;
        }

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson( line, &parseError );
        if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
            appendOutputLine( run, tr( "[script-rpc] invalid message: %1" ).arg( QString::fromUtf8( line ) ) );
            continue;
        }

        handleRpcMessage( run, document.object().toVariantMap() );
    }
}

void ScriptSupervisor::handleRpcMessage( ScriptRun* run, const QVariantMap& message )
{
    const auto requestId = message.value( QStringLiteral( "id" ) ).toInt();
    const auto token = message.value( QStringLiteral( "token" ) ).toString();
    const auto isNotification = message.value( QStringLiteral( "notification" ) ).toBool();
    if ( token != run->authToken ) {
        if ( !isNotification ) {
            sendRpcMessage( run, rpcErrorEnvelope( requestId, QStringLiteral( "unauthorized" ) ) );
        }
        return;
    }

    const auto method = message.value( QStringLiteral( "method" ) ).toString();
    if ( method == QStringLiteral( "is_stop_requested" ) ) {
        QVariantMap response;
        response.insert( QStringLiteral( "id" ), requestId );
        response.insert( QStringLiteral( "ok" ), true );
        response.insert( QStringLiteral( "result" ),
                         QVariantMap{ { QStringLiteral( "stopRequested" ), run->stopRequested } } );
        sendRpcMessage( run, response );
        return;
    }

    const auto params = message.value( QStringLiteral( "params" ) ).toMap();

    if ( method == QStringLiteral( "subscribe_event" ) ) {
        const auto eventType = params.value( QStringLiteral( "eventType" ) ).toString().trimmed();
        if ( eventType != QLatin1String( ReceiveEventType )
             && eventType != QLatin1String( ResponseEventType )
             && eventType != QLatin1String( TxEventType )
             && eventType != QLatin1String( ActionSendEventType )
             && eventType != QLatin1String( TabOpenEventType )
             && eventType != QLatin1String( TabCloseEventType )
             && eventType != QLatin1String( CommStartEventType )
             && eventType != QLatin1String( CommStopEventType ) ) {
            if ( !isNotification ) {
                sendRpcMessage( run, rpcErrorEnvelope( requestId, QStringLiteral( "unsupported event type" ) ) );
            }
            return;
        }

        const bool lifecycleEvent = eventType == QLatin1String( TabOpenEventType )
                                    || eventType == QLatin1String( TabCloseEventType );
        QString errorMessage;
        QVariantMap target;
        const bool hasTabSelector = params.contains( QStringLiteral( "tabId" ) )
                                    || params.contains( QStringLiteral( "windowIndex" ) )
                                    || params.contains( QStringLiteral( "tabIndex" ) );
        if ( hasTabSelector ) {
            target = lifecycleEvent ? lifecycleTargetInfo( params, &errorMessage )
                                    : resolveTargetInfo( params, &errorMessage );
            if ( target.isEmpty() ) {
                if ( !isNotification ) {
                    sendRpcMessage( run, rpcErrorEnvelope( requestId, errorMessage ) );
                }
                return;
            }
        }

        if ( run->scope == ScriptRunScope::Tab ) {
            if ( target.isEmpty() ) {
                target = lifecycleEvent ? lifecycleTargetInfo( QVariantMap{
                                                                   { QStringLiteral( "tabId" ),
                                                                     run->ownerTabId } },
                                                               &errorMessage )
                                        : resolveTargetInfo( QVariantMap{
                                                                 { QStringLiteral( "tabId" ),
                                                                   run->ownerTabId } },
                                                             &errorMessage );
                if ( target.isEmpty() ) {
                    if ( !isNotification ) {
                        sendRpcMessage( run, rpcErrorEnvelope( requestId, errorMessage ) );
                    }
                    return;
                }
            }

            if ( target.value( QStringLiteral( "tabId" ) ).toString() != run->ownerTabId ) {
                if ( !isNotification ) {
                    sendRpcMessage( run, rpcErrorEnvelope(
                                             requestId,
                                             QStringLiteral(
                                                 "subscriptions are limited to the owner tab" ) ) );
                }
                return;
            }
        }

        ScriptSubscription subscription;
        subscription.tabId = target.value( QStringLiteral( "tabId" ) ).toString();
        subscription.windowIndex = target.value( QStringLiteral( "windowIndex" ), -1 ).toInt();
        subscription.tabIndex = target.value( QStringLiteral( "tabIndex" ), -1 ).toInt();
        subscription.windowId = target.value( QStringLiteral( "windowId" ) ).toString();
        subscription.filePath = target.value( QStringLiteral( "filePath" ) ).toString();
        subscription.displayName = target.value( QStringLiteral( "displayName" ) ).toString();
        subscription.portName = params.value( QStringLiteral( "portName" ) ).toString().trimmed();
        if ( subscription.portName.isEmpty() ) {
            subscription.portName = target.value( QStringLiteral( "portName" ) ).toString();
        }
        subscription.eventType = eventType;
        subscription.sourceType = target.value( QStringLiteral( "sourceType" ) ).toString();
        if ( params.contains( QStringLiteral( "responseId" ) ) ) {
            subscription.responseId = params.value( QStringLiteral( "responseId" ) ).toInt();
        }
        subscription.responseName = params.value( QStringLiteral( "responseName" ) ).toString().trimmed();
        if ( params.contains( QStringLiteral( "actionId" ) ) ) {
            subscription.actionId = params.value( QStringLiteral( "actionId" ) ).toInt();
        }
        subscription.actionName = params.value( QStringLiteral( "actionName" ) ).toString().trimmed();
        run->subscriptions.push_back( subscription );
        Q_EMIT statusChanged();

        if ( !isNotification ) {
            QVariantMap payload = target;
            payload.insert( QStringLiteral( "eventType" ), eventType );
            if ( !subscription.portName.isEmpty() ) {
                payload.insert( QStringLiteral( "portName" ), subscription.portName );
            }
            if ( subscription.responseId ) {
                payload.insert( QStringLiteral( "responseId" ), *subscription.responseId );
            }
            if ( !subscription.responseName.isEmpty() ) {
                payload.insert( QStringLiteral( "responseName" ), subscription.responseName );
            }
            if ( subscription.actionId ) {
                payload.insert( QStringLiteral( "actionId" ), *subscription.actionId );
            }
            if ( !subscription.actionName.isEmpty() ) {
                payload.insert( QStringLiteral( "actionName" ), subscription.actionName );
            }

            QVariantMap response;
            response.insert( QStringLiteral( "id" ), requestId );
            response.insert( QStringLiteral( "ok" ), true );
            response.insert( QStringLiteral( "result" ), payload );
            sendRpcMessage( run, response );
        }
        return;
    }

    if ( method == QStringLiteral( "clear_event_handlers" ) ) {
        run->subscriptions.clear();
        Q_EMIT statusChanged();
        if ( !isNotification ) {
            QVariantMap response;
            response.insert( QStringLiteral( "id" ), requestId );
            response.insert( QStringLiteral( "ok" ), true );
            response.insert( QStringLiteral( "result" ), QVariantMap{} );
            sendRpcMessage( run, response );
        }
        return;
    }

    if ( method == QStringLiteral( "set_dispatch_state" ) ) {
        run->dispatchState = params.value( QStringLiteral( "state" ) ).toString().trimmed();
        if ( run->dispatchState.isEmpty() ) {
            run->dispatchState = QStringLiteral( "idle" );
        }
        Q_EMIT statusChanged();
        return;
    }

    if ( method == QStringLiteral( "report_callback_error" ) ) {
        run->lastCallbackError = params.value( QStringLiteral( "error" ) ).toString();
        if ( !run->lastCallbackError.isEmpty() ) {
            appendOutputLine( run, tr( "[script-callback] %1" ).arg( run->lastCallbackError ) );
        }
        Q_EMIT statusChanged();
        return;
    }

    if ( method == QStringLiteral( "report_event_stats" ) ) {
        run->droppedEvents = params.value( QStringLiteral( "droppedEvents" ) ).toInt();
        Q_EMIT statusChanged();
        return;
    }

    if ( method != QStringLiteral( "command" ) ) {
        if ( !isNotification ) {
            sendRpcMessage( run, rpcErrorEnvelope( requestId, QStringLiteral( "unsupported method" ) ) );
        }
        return;
    }

    if ( !commanderExecutor_ ) {
        sendRpcMessage( run, rpcErrorEnvelope( requestId, QStringLiteral( "no commander executor" ) ) );
        return;
    }

    QString errorMessage;
    const auto request = commanderRequestFromVariantMap( params, &errorMessage );
    if ( !request ) {
        sendRpcMessage( run, rpcErrorEnvelope( requestId, errorMessage ) );
        return;
    }

    const auto result = commanderExecutor_( *request );
    sendRpcMessage( run, rpcResultEnvelope( requestId, result ) );
}

void ScriptSupervisor::sendRpcMessage( ScriptRun* run, const QVariantMap& response )
{
    if ( run == nullptr || run->socket == nullptr ) {
        return;
    }

    const auto bytes = QJsonDocument::fromVariant( response ).toJson( QJsonDocument::Compact );
    run->socket->write( bytes );
    run->socket->write( "\n" );
    run->socket->flush();
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

void ScriptSupervisor::clearSubscriptionsForTab( const QString& tabId )
{
    if ( auto* run = findRun( tabId ) ) {
        run->subscriptions.clear();
        Q_EMIT statusChanged();
    }
}

bool ScriptSupervisor::eventMatchesSubscription( const QVariantMap& event,
                                                 const ScriptSubscription& subscription ) const
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

    if ( subscription.eventType == QLatin1String( ResponseEventType ) ) {
        if ( subscription.responseId
             && *subscription.responseId != event.value( QStringLiteral( "responseId" ) ).toInt() ) {
            return false;
        }
        return matchesName( subscription.responseName,
                            event.value( QStringLiteral( "responseName" ) ).toString() );
    }

    if ( subscription.eventType == QLatin1String( ActionSendEventType ) ) {
        if ( subscription.actionId
             && *subscription.actionId != event.value( QStringLiteral( "actionId" ) ).toInt() ) {
            return false;
        }
        return matchesName( subscription.actionName,
                            event.value( QStringLiteral( "actionName" ) ).toString() );
    }

    return true;
}
