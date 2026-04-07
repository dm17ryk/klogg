#pragma once

#include <QObject>

#include "labrequest.h"
#include "labtypes.h"

class KloggApp;
class QTcpSocket;

class LabAgentRunner : public QObject {
    Q_OBJECT

  public:
    explicit LabAgentRunner( KloggApp& app, QObject* parent = nullptr );

    bool start( const LabCliRequest& request, QString* errorMessage );

  private Q_SLOTS:
    void connected();
    void disconnected();
    void readyRead();
    void sendHeartbeat();

  private:
    void processMessage( const QVariantMap& message );
    void sendMessage( const QVariantMap& message );
    void runAssignedJob( const QVariantMap& message );

    KloggApp& app_;
    QTcpSocket* socket_ = nullptr;
    QByteArray sharedToken_;
    QByteArray buffer_;
    LabAgentConfig config_;
};
