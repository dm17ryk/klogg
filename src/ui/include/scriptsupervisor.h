#pragma once

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "commander.h"

enum class ScriptRunState {
    Idle,
    Starting,
    Running,
    Stopping,
    Finished,
    Failed,
    Cancelled,
};

enum class ScriptRunScope {
    Tab,
    Global,
};

class ScriptSupervisor : public QObject {
    Q_OBJECT

  public:
    explicit ScriptSupervisor( QObject* parent = nullptr );
    ~ScriptSupervisor() override;

    void setCommanderExecutor( std::function<CommanderResult( const CommanderRequest& )> executor );

    CommanderResult runScript( const CommanderRequest& request );
    CommanderResult runGlobalScript( const CommanderRequest& request );
    CommanderResult stopScript( const CommanderRequest& request );
    CommanderResult stopGlobalScript();
    CommanderResult scriptStatus( const CommanderRequest& request ) const;
    CommanderResult globalScriptStatus() const;
    CommanderResult scriptSubscriptions( const CommanderRequest& request ) const;
    CommanderResult globalScriptSubscriptions() const;
    CommanderResult clearScriptSubscriptions( const CommanderRequest& request );
    CommanderResult clearGlobalScriptSubscriptions();
    void publishEvent( const QVariantMap& event );

    bool hasActiveScripts() const;
    bool hasActiveScriptForTab( const QString& tabId ) const;
    QVariantMap scriptStatusForTab( const QString& tabId ) const;
    QVariantMap scriptBindingForTab( const QString& tabId ) const;
    QVariantMap globalScriptStatusPayload() const;
    QVariantMap globalScriptBinding() const;
    void restoreScriptBinding( const QVariantMap& binding );
    void restoreGlobalScriptBinding( const QVariantMap& binding );
    void forgetTab( const QString& tabId );

  Q_SIGNALS:
    void statusChanged();
    void outputChanged();

  private:
    struct ScriptSubscription {
        QString tabId;
        int windowIndex = -1;
        int tabIndex = -1;
        QString windowId;
        QString filePath;
        QString displayName;
        QString portName;
        QString eventType;
        QString sourceType;
        std::optional<int> responseId;
        QString responseName;
        std::optional<int> actionId;
        QString actionName;
    };

    struct ScriptRun;

    QString resolvePythonExecutable() const;
    QString runtimeRoot() const;
    QString workerBootstrapPath() const;

    ScriptRun* findRun( const QString& tabId ) const;
    ScriptRun* findGlobalRun() const;
    ScriptRun* findRunForRequest( const CommanderRequest& request ) const;
    void removeRun( const QString& tabId );
    void removeGlobalRun();
    bool beginListening( ScriptRun* run, QString* errorMessage );
    void resetSocketState( ScriptRun* run );
    void appendOutputChunk( ScriptRun* run, const QString& chunk, bool stderrStream );
    void appendOutputLine( ScriptRun* run, const QString& line );
    void setState( ScriptRun* run, ScriptRunState state );
    QVariantMap statusPayload( const ScriptRun* run ) const;
    QVariantList subscriptionsPayload( const ScriptRun* run ) const;
    QVariantList allStatusPayloads() const;
    QVariantList allSubscriptionsPayloads() const;
    QVariantMap lifecycleTargetInfo( const QVariantMap& selector, QString* errorMessage ) const;
    QVariantMap resolveTargetInfo( const CommanderRequest& request,
                                   bool requireSelector,
                                   QString* errorMessage ) const;
    QVariantMap resolveTargetInfo( const QVariantMap& selector, QString* errorMessage ) const;
    void startRunProcess( ScriptRun* run );
    void handleNewConnection( const QString& tabId );
    void handleProcessStarted( const QString& tabId );
    void handleProcessFinished( const QString& tabId, int exitCode, QProcess::ExitStatus exitStatus );
    void handleProcessError( const QString& tabId, QProcess::ProcessError error );
    void readProcessStdout( const QString& tabId );
    void readProcessStderr( const QString& tabId );
    void readSocketData( const QString& tabId );
    void handleRpcMessage( ScriptRun* run, const QVariantMap& message );
    void sendRpcMessage( ScriptRun* run, const QVariantMap& response );
    QVariantMap rpcResultEnvelope( int requestId, const CommanderResult& result ) const;
    QVariantMap rpcErrorEnvelope( int requestId, const QString& errorText ) const;
    void clearSubscriptionsForTab( const QString& tabId );
    bool eventMatchesSubscription( const QVariantMap& event,
                                   const ScriptSubscription& subscription ) const;

    std::function<CommanderResult( const CommanderRequest& )> commanderExecutor_;
    std::vector<std::unique_ptr<ScriptRun>> runs_;
};

QString scriptRunStateToString( ScriptRunState state );
