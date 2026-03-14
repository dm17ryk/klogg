#pragma once

#include <QObject>
#include <QByteArray>
#include <QThread>

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
    const SerialCaptureSettings& captureSettings() const;
    void sendBytes( const QByteArray& data );
    void appendToFile( const QByteArray& data );

  public Q_SLOTS:
    void closeConnection();

  Q_SIGNALS:
    void errorOccurred( const QString& message );
    void connectionClosed();
    void lineObserved( const QByteArray& lineBytes );

  private:
    void setupWorker();
    void setConnectionClosed();
    void handleIncomingLine( const QByteArray& lineBytes );

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
