#pragma once

#include <QObject>
#include <QByteArray>
#include <QMap>
#include <QThread>
#include <QVariantList>

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
    bool isLoggingEnabled() const;
    void setLoggingEnabled( bool enabled );
    int responseCounter( int responseId ) const;
    QVariantList responseCounters() const;
    void resetResponseCounter( int responseId );
    void resetAllResponseCounters();

  public Q_SLOTS:
    void closeConnection();

  Q_SIGNALS:
    void errorOccurred( const QString& message );
    void connectionClosed();
    void connectionOpened();
    void dataObserved( const QByteArray& dataBytes );
    void lineObserved( const QByteArray& lineBytes );
    void responseMatched( int responseId,
                          const QString& responseName,
                          int counter,
                          const QByteArray& lineBytes,
                          const QString& lineText );

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
    bool loggingEnabled_ = true;
    QByteArray lineBuffer_;
    QMap<int, int> responseCounters_;
};
