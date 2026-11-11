#pragma once

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QThread>
#include <QVariantList>

#include <deque>
#include <functional>
#include <optional>

#include "serialcaptureworker.h"
#ifdef KLOGG_SHARED_SERIAL_WRITER
#include "backendthreadretirement.h"
#include "streamsourceregistry.h"
#include <QTimer>
#include <functional>
#endif

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
    bool hasActiveWorker() const
    {
        return started_;
    }
    bool isPaused() const;
    bool canResume() const;
    // True when no worker owns the serial port (it was never opened or has fully closed).
    bool isPortReleased() const;
    // Processes events until the port is released; false after `timeoutMs`.
    bool waitForPortRelease( int timeoutMs );
    static bool waitForCondition( const std::function<bool()>& condition, int timeoutMs );
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
#ifdef KLOGG_SHARED_SERIAL_WRITER
    DataResult<cilogg::communication::WriteObservation>
    submitActionWrite( const QByteArray&, bool, const cilogg::communication::ActionWriteStamp& );
#endif
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
    void actionSent( int actionId, const QString& actionName, int stepIndex,
                     const QByteArray& dataBytes );
    void responseMatched( int responseId, const QString& responseName, int counter,
                          const QByteArray& lineBytes, const QString& lineText );
    void captureFileChanged( const QString& oldFilePath, const QString& newFilePath );

private:
    void updateActionState( bool actionsEnabled, bool autoResponsesEnabled, const char* operation );
    void startInternal( bool resetResponseCounters );
    void setupWorker();
    void setConnectionClosed();
    bool canOpenConfiguredCapture( QString* errorMessage ) const;
    void handleIncomingLine( const QByteArray& lineBytes, std::optional<quint64> generation );

private Q_SLOTS:
    void handleDataReceived( const QByteArray& data );

private:
    friend struct StreamSessionTestAccess;
#ifdef KLOGG_SHARED_SERIAL_WRITER
    void startChecked( bool resetResponseCounters );
    void stopChecked( bool waitForCompletion );
    void setupCheckedWorker();
    void completeThreadRetirement( quint64 generation );
    void finalizeResolvedWrite( const QString&, const cilogg::communication::WriteObservation& );
    bool publishWriteFailures();
    void closeCheckedConnection();
    void acceptBackendReady( quint64 generation );
    void acceptBackendData( quint64 generation, const QByteArray&, bool transmitted );
    void acceptWriteSnapshot( cilogg::communication::BackendWriteSnapshotPtr );
    qint64 writerNow() const;
    void driveWrites( bool retry = false );
    void publishWritePolicy();
    void finishWriterBackend();
    bool initializeWriter( quint64 generation );
    std::function<SerialCaptureWorker*( SerialCaptureSettings )> workerFactory_;
    std::function<bool( QString* )> capturePreflight_;
    std::function<qint64()> writerClock_;
    StreamSourceRegistry* registry_ = &StreamSourceRegistry::get();
    const StreamLimits writeLimits_ = *StreamLimits::fromDefinition( {} ).value;
    const std::chrono::steady_clock::time_point writerOrigin_ = std::chrono::steady_clock::now();
    const QString writerEndpointId_ = QUuid::createUuid().toString( QUuid::WithoutBraces );
    std::shared_ptr<cilogg::communication::StreamWritePolicyPublication> writePolicy_
        = std::make_shared<cilogg::communication::StreamWritePolicyPublication>();
    std::unique_ptr<cilogg::communication::SharedWriter> writer_;
    std::optional<SerialEndpointLease> serialLease_;
    std::shared_ptr<BackendThreadRetirement> threadRetirement_;
    struct WriteMetadata {
        cilogg::communication::ActionWriteStamp stamp;
        qint64 deadlineNs = 0;
        bool sensitive = false;
        bool redactionPublished = false;
        bool offered = false;
        bool finalizationQueued = false;
        bool failurePublished = false;
    };
    QHash<QString, WriteMetadata> writeMetadata_;
    cilogg::communication::SerialWriteEnvelopePtr currentOffer_;
    QTimer writeTimer_;
    quint64 writeRequestSequence_ = 0;
    quint64 lastWriteDelivery_ = 0;
    quint64 acceptedTicket_ = 0;
    quint64 appliedWritten_ = 0;
    qint64 retryAtNs_ = 0;
    int retryDelayMs_ = 10;
    bool backendRetired_ = true;
    bool closePublished_ = true;
    bool destroying_ = false;
    bool writePolicyExhausted_ = false;
#endif

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
