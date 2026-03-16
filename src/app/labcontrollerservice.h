#pragma once

#include <QHash>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include "labrequest.h"
#include "labtypes.h"

class QSqlDatabase;
class QTcpServer;
class QTcpSocket;

class LabControllerService : public QObject {
    Q_OBJECT

  public:
    explicit LabControllerService( QObject* parent = nullptr );
    ~LabControllerService() override;

    bool start( const LabCliRequest& request, QString* errorMessage );
    void stop();

    QVariantMap submitBundle( const LabJobBundle& bundle, const QString& requestedAgentLabel,
                              QString* errorMessage );
    QVariantMap queueSnapshot() const;
    QVariantMap agentsSnapshot() const;
    QVariantMap jobStatus( const QString& jobId, QString* errorMessage ) const;
    QVariantMap cancelJob( const QString& jobId, QString* errorMessage );
    QVariantMap artifactsPayload( const QString& jobId, QString* errorMessage ) const;
    QString stateDirPath() const;

  private Q_SLOTS:
    void handleHttpConnection();
    void handleAgentConnection();
    void handleAgentDisconnected();
    void readAgentSocket();
    void pruneStaleAgents();

  private:
    struct AgentConnectionState;

    bool initializeDatabase( const QString& stateDirPath, QString* errorMessage );
    bool loadToken( const QString& tokenFilePath, QString* errorMessage );
    bool insertOrUpdateAgent( const LabAgentConfig& config, QString* errorMessage );
    bool updateAgentHeartbeat( const QString& agentId, QString* errorMessage );
    bool updateAgentStatus( const QString& agentId, const QString& status, const QString& lastError,
                            QString* errorMessage );
    QVariantMap jobPayloadById( const QString& jobId, QString* errorMessage ) const;
    void scheduleJobs();
    bool assignJobToAgent( const QString& jobId, const QVariantMap& jobRecord,
                           const QString& agentId, const LabAgentConfig& config,
                           QString* errorMessage );
    QVariantList eligibleBindings( const LabJobBundle& bundle, const LabAgentConfig& config ) const;
    QVariantMap queueSnapshotUnlocked() const;
    QVariantMap agentsSnapshotUnlocked() const;
    QVariantMap jobStatusUnlocked( const QString& jobId, QString* errorMessage ) const;
    QVariantMap artifactsPayloadUnlocked( const QString& jobId, QString* errorMessage ) const;
    QString jobDirectoryPath( const QString& jobId ) const;
    QString artifactDirectoryPath( const QString& jobId ) const;
    QString nextJobId() const;
    QVariantMap submitBundleUnlocked( const LabJobBundle& bundle, const QString& requestedAgentLabel,
                                      QString* errorMessage );
    bool writeBundleToDisk( const QString& jobId, const LabJobBundle& bundle, QString* errorMessage ) const;
    QVariantMap loadBundlePayload( const QString& jobId ) const;
    void sendJsonResponse( QTcpSocket* socket, int statusCode, const QVariantMap& payload ) const;
    void processHttpRequest( QTcpSocket* socket, const QByteArray& rawRequest );
    bool authorizeHttpRequest( const QByteArray& headers, QString* errorMessage ) const;
    void processAgentMessage( QTcpSocket* socket, const QVariantMap& message );
    void sendAgentMessage( QTcpSocket* socket, const QVariantMap& message ) const;
    AgentConnectionState* connectionStateForSocket( QTcpSocket* socket );
    const AgentConnectionState* connectionStateForSocket( QTcpSocket* socket ) const;

    QString databaseConnectionName_;
    QTcpServer* httpServer_ = nullptr;
    QTcpServer* agentServer_ = nullptr;
    QByteArray sharedToken_;
    QString stateDirPath_;
    QHash<QTcpSocket*, AgentConnectionState*> agentConnections_;
    int heartbeatTimeoutMs_ = 15000;
};
