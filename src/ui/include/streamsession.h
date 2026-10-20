#pragma once

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QThread>
#include <QVariantList>

#include <deque>
#include <optional>

#include "serialcaptureworker.h"

class StreamSession : public QObject {
    Q_OBJECT

  public:
    explicit StreamSession( SerialCaptureSettings settings );
    ~StreamSession() override;

    void start();
    void stop( bool waitForCompletion = true );
    bool pauseConnection( QString* errorMessage = nullptr );
    bool resumeConnection( QString* errorMessage = nullptr );
    bool isConnectionOpen() const;
    bool isPaused() const;
    bool canResume() const;
    QString sourceDisplayName() const;
    QString filePath() const;
    const SerialCaptureSettings& captureSettings() const;
    bool actionsEnabled() const;
    void setActionsEnabled( bool enabled );
    quint64 actionsGeneration() const;
    bool autoResponsesEnabled() const;
    void setAutoResponsesEnabled( bool enabled );
    quint64 autoResponsesGeneration() const;
    bool startNewCaptureFile( const QString& filePath, QString* errorMessage = nullptr );
    void sendBytes( const QByteArray& data, bool sensitive = false );
    void notifyActionSend( int actionId, const QString& actionName, int stepIndex,
                           const QByteArray& data );
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
    void actionsEnabledChanged( bool enabled );
    void autoResponsesEnabledChanged( bool enabled );
    void dataObserved( const QByteArray& dataBytes );
    void dataTransmitted( const QByteArray& dataBytes );
    void lineObserved( const QByteArray& lineBytes );
    void actionSent( int actionId,
                     const QString& actionName,
                     int stepIndex,
                     const QByteArray& dataBytes );
    void responseMatched( int responseId,
                          const QString& responseName,
                          int counter,
                          const QByteArray& lineBytes,
                          const QString& lineText );
    void captureFileChanged( const QString& oldFilePath, const QString& newFilePath );

  private:
    void startInternal( bool resetResponseCounters );
    void setupWorker();
    void setConnectionClosed();
    bool canOpenConfiguredCapture( QString* errorMessage ) const;
    void handleIncomingLine( const QByteArray& lineBytes, std::optional<quint64> generation );

  private Q_SLOTS:
    void handleDataReceived( const QByteArray& data );

  private:
    friend struct StreamSessionTestAccess;

    SerialCaptureSettings settings_;
    quint64 actionsGeneration_ = 0;
    quint64 autoResponsesGeneration_ = 0;
    QThread thread_;
    SerialCaptureWorker* worker_ = nullptr;
    quint64 workerGeneration_ = 0;
    QMetaObject::Connection threadFinishedConnection_;
    bool started_ = false;
    bool stopping_ = false;
    bool connectionOpen_ = false;
    bool paused_ = false;
    bool loggingEnabled_ = true;
    QByteArray lineBuffer_;
    struct BufferedActionsSegment {
        qsizetype byteCount;
        std::optional<quint64> generation;
    };
    // Coalesce adjacent chunks with identical eligibility. A line spanning any
    // ineligible or different generation segment is observation-only.
    std::deque<BufferedActionsSegment> lineBufferActionsSegments_;
    QMap<int, int> responseCounters_;
};
