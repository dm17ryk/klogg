#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVector>
#include <QVariantMap>

#include <functional>
#include <optional>

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

class ScriptSupervisor : public QObject {
    Q_OBJECT

  public:
    explicit ScriptSupervisor( QObject* parent = nullptr );

    void setCommanderExecutor( std::function<CommanderResult( const CommanderRequest& )> executor );

    CommanderResult runScript( const QString& scriptFilePath, const QString& argsJsonFilePath = {} );
    CommanderResult stopScript();
    CommanderResult scriptStatus() const;
    CommanderResult scriptSubscriptions() const;
    CommanderResult clearScriptSubscriptions();
    void publishEvent( const QVariantMap& event );

    bool hasActiveScript() const;
    QString activeScriptFile() const;

  Q_SIGNALS:
    void statusChanged();
    void outputChanged();

  private Q_SLOTS:
    void handleNewConnection();
    void handleProcessStarted();
    void handleProcessFinished( int exitCode, QProcess::ExitStatus exitStatus );
    void handleProcessError( QProcess::ProcessError error );
    void readProcessStdout();
    void readProcessStderr();
    void readSocketData();

  private:
    bool beginListening( QString* errorMessage );
    QString resolvePythonExecutable() const;
    QString runtimeRoot() const;
    QString workerBootstrapPath() const;
    void resetRunState();
    void appendOutputChunk( const QString& chunk, bool stderrStream );
    void appendOutputLine( const QString& line );
    void setState( ScriptRunState state );
    QVariantMap statusPayload() const;
    QVariantList subscriptionsPayload() const;
    void handleRpcMessage( const QVariantMap& message );
    void sendRpcMessage( QTcpSocket* socket, const QVariantMap& response );
    QVariantMap rpcResultEnvelope( int requestId, const CommanderResult& result ) const;
    QVariantMap rpcErrorEnvelope( int requestId, const QString& errorText ) const;
    QVariantMap resolveSubscriptionTarget( const QVariantMap& selector, QString* errorMessage ) const;
    void clearSubscriptionsForTab( const QString& tabId );

    struct ScriptSubscription {
        QString tabId;
        int windowIndex = -1;
        int tabIndex = -1;
        QString windowId;
        QString filePath;
        QString displayName;
        QString portName;
        QString eventType;
        std::optional<int> responseId;
        QString responseName;
    };

    std::function<CommanderResult( const CommanderRequest& )> commanderExecutor_;
    QProcess process_;
    QTcpServer server_;
    QTcpSocket* socket_ = nullptr;
    QByteArray socketBuffer_;
    QString authToken_;
    QString scriptFilePath_;
    QString argsJsonFilePath_;
    QStringList outputTail_;
    QString pendingStdout_;
    QString pendingStderr_;
    QString lastError_;
    QString lastCallbackError_;
    QDateTime startedAt_;
    QDateTime finishedAt_;
    ScriptRunState state_ = ScriptRunState::Idle;
    int exitCode_ = 0;
    bool stopRequested_ = false;
    int droppedEvents_ = 0;
    QString dispatchState_ = QStringLiteral( "idle" );
    QVector<ScriptSubscription> subscriptions_;
};

QString scriptRunStateToString( ScriptRunState state );
