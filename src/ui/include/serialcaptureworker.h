#pragma once

#include <QFile>
#include <QObject>
#include <QSerialPort>

struct SerialCaptureSettings {
    QString portName;
    qint32 baudRate = 115200;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    QString filePath;
};

class SerialCaptureWorker : public QObject {
    Q_OBJECT

  public:
    explicit SerialCaptureWorker( SerialCaptureSettings settings, QObject* parent = nullptr );

  public Q_SLOTS:
    void start();
    void stop();

  Q_SIGNALS:
    void errorOccurred( const QString& message );
    void finished();

  private Q_SLOTS:
    void onReadyRead();
    void onError( QSerialPort::SerialPortError error );

  private:
    SerialCaptureSettings settings_;
    QSerialPort* port_ = nullptr;
    QFile file_;
    int flushCounter_ = 0;
    bool stopping_ = false;
};
