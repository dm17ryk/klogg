#pragma once

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <optional>

#include "commander.h"

enum class ScenarioRunState {
    Idle,
    Starting,
    Running,
    Stopping,
    Finished,
    Failed,
    Cancelled,
};

class ScenarioRunner : public QObject {
    Q_OBJECT

  public:
    explicit ScenarioRunner( QObject* parent = nullptr );
    ~ScenarioRunner() override;

    void setCommanderExecutor( std::function<CommanderResult( const CommanderRequest& )> executor );

    CommanderResult runScenario( const CommanderRequest& request );
    CommanderResult runSuite( const CommanderRequest& request );
    CommanderResult stopRun();
    CommanderResult status() const;
    CommanderResult report() const;
    QVariantMap statusPayload() const;
    QVariantMap reportPayload() const;
    void publishEvent( const QVariantMap& event );

  Q_SIGNALS:
    void statusChanged();
    void outputChanged();

  private:
    struct Subscription {
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

    QString resolvePythonExecutable() const;
    QString runtimeRoot() const;
    QString scenarioBootstrapPath() const;

    bool beginListening( QString* errorMessage );
    void resetSocketState();
    bool startProcess( const QString& bootstrapPath );
    void handleNewConnection();
    void handleProcessStarted();
    void handleProcessFinished( int exitCode, QProcess::ExitStatus exitStatus );
    void handleProcessError( QProcess::ProcessError error );
    void readProcessStdout();
    void readProcessStderr();
    void readSocketData();
    void handleRpcMessage( const QVariantMap& message );
    void sendRpcMessage( const QVariantMap& response );
    QVariantMap rpcResultEnvelope( int requestId, const CommanderResult& result ) const;
    QVariantMap rpcErrorEnvelope( int requestId, const QString& errorText ) const;
    void appendOutputChunk( const QString& chunk, bool stderrStream );
    void appendOutputLine( const QString& line );
    void setState( ScenarioRunState state );
    void resetRunState();
    bool eventMatchesSubscription( const QVariantMap& event, const Subscription& subscription ) const;
    QVariantMap buildSyntheticReport() const;

    std::function<CommanderResult( const CommanderRequest& )> commanderExecutor_;

    QProcess process_;
    QTcpServer* server_ = nullptr;
    QTcpSocket* socket_ = nullptr;
    QByteArray socketBuffer_;
    QString authToken_;
    QStringList outputTail_;
    QString pendingStdout_;
    QString pendingStderr_;

    ScenarioRunState state_ = ScenarioRunState::Idle;
    bool stopRequested_ = false;
    bool hasRun_ = false;
    int exitCode_ = 0;
    int droppedEvents_ = 0;
    QString lastError_;
    QString lastCallbackError_;
    QString dispatchState_ = QStringLiteral( "idle" );
    QDateTime startedAt_;
    QDateTime finishedAt_;

    QString scenarioFilePath_;
    QString suiteFilePath_;
    QString argsJsonFilePath_;
    QString suiteName_;
    QString suiteId_;
    QString currentScenarioName_;
    QString currentScenarioFile_;
    QString currentStepName_;
    int totalScenarios_ = 0;
    int completedScenarios_ = 0;
    int passedCount_ = 0;
    int failedCount_ = 0;
    int skippedCount_ = 0;
    QString reportJsonPath_;
    QString reportJunitPath_;
    QVariantMap lastReportPayload_;
    QVector<Subscription> subscriptions_;
};

QString scenarioRunStateToString( ScenarioRunState state );
