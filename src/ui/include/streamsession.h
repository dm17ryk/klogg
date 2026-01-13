#pragma once

#include <QObject>
#include <QByteArray>
#include <QThread>

#include "actionsmanager.h"
#include "previewdecodeutils.h"
#include "serialcaptureworker.h"

class StreamSession : public QObject {
    Q_OBJECT

  public:
    explicit StreamSession( SerialCaptureSettings settings );
    ~StreamSession() override;

    void start();
    void stop( bool waitForCompletion = true );
    bool isConnectionOpen() const;
    QString sourceDisplayName() const;
    QString filePath() const;
    void sendBytes( const QByteArray& data );

  public Q_SLOTS:
    void closeConnection();

  Q_SIGNALS:
    void errorOccurred( const QString& message );
    void connectionClosed();

  private:
    void setupWorker();
    void setConnectionClosed();
    void handleIncomingLine( const QByteArray& lineBytes );
    void appendToFile( const QByteArray& data );

  private Q_SLOTS:
    void handleDataReceived( const QByteArray& data );

  private:
    SerialCaptureSettings settings_;
    QThread thread_;
    SerialCaptureWorker* worker_ = nullptr;
    bool started_ = false;
    bool stopping_ = false;
    bool connectionOpen_ = false;
    QByteArray lineBuffer_;
};
