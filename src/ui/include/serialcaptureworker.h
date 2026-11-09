#pragma once

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QSerialPort>
#include <optional>
#ifdef KLOGG_SHARED_SERIAL_WRITER
#include "backendthreadretirement.h"
#include "serialwritedriver.h"
#include <functional>
#endif

struct SerialCaptureSettings {
    QString portName;
    qint32 baudRate = 115200;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    QString filePath;
    bool addTimestamps = false;
    QString timestampFormat = QStringLiteral( "dd/MM/yyyy HH:mm:ss.zzz" );
    bool logTransmits = false;
    bool useForActions = false;
    bool autoResponsesEnabled = true;
};

QString serializeSerialCaptureSettings( const SerialCaptureSettings& settings );
std::optional<SerialCaptureSettings> deserializeSerialCaptureSettings( const QString& serialized );

class SerialCaptureWorker : public QObject {
    Q_OBJECT

public:
    explicit SerialCaptureWorker( SerialCaptureSettings settings, QObject* parent = nullptr );
    bool switchCaptureFile( const QString& filePath, QString* errorMessage = nullptr );

public Q_SLOTS:
    void start();
    void stop();
    void sendData( QByteArray data, bool sensitive = false );
    void appendToFile( QByteArray data );
    void setLoggingEnabled( bool enabled );

Q_SIGNALS:
#ifdef KLOGG_SHARED_SERIAL_WRITER
    void backendReady();
    void backendSnapshot( cilogg::communication::BackendWriteSnapshotPtr snapshot );
#endif
    void errorOccurred( const QString& message );
    void finished();
    void dataReceived( const QByteArray& data );
    void dataTransmitted( const QByteArray& data );

private Q_SLOTS:
    void onReadyRead();
    void onError( QSerialPort::SerialPortError error );

private:
#ifdef KLOGG_SHARED_SERIAL_WRITER
    friend struct SerialCaptureWorkerTestAccess;
    friend class StreamSession;
    std::function<QSerialPort*( QObject* )> portFactory_;
    void startChecked();
    void stopChecked();
    void failChecked( const QString& message );
    bool isCurrentEpoch( quint64 generation, bool requireReady = true ) const;
    void submitWriteOffer( cilogg::communication::SerialWriteEnvelopePtr, bool sensitive );
    void acknowledgeWriteProgress( const cilogg::communication::WriteEpoch&, quint64 sequence );
    void finalizeWritePresentation( const cilogg::communication::WriteEpoch&, const QString& id );
    void captureWrittenPrefix( const QByteArray&, const QString& id );
    void finishTxSegment();
    std::optional<cilogg::communication::SerialWriteContext> writeContext_;
    std::shared_ptr<BackendThreadRetirement> threadRetirement_;
    std::unique_ptr<cilogg::communication::SerialWriteDriver> writeDriver_;
    QString txRequestId_;
    bool txSensitive_ = false;
    bool txSegmentOpen_ = false;
    bool txEndsInNewline_ = true;
    bool txRedactionLogged_ = false;
    bool readyPublished_ = false;
    bool closingStarted_ = false;
#endif
    SerialCaptureSettings settings_;
    QSerialPort* port_ = nullptr;
    QFile file_;
    int flushCounter_ = 0;
    bool stopping_ = false;
    bool atLineStart_ = true;
    bool loggingEnabled_ = true;
};
