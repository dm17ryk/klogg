#include "serialcaptureworker.h"

#include <QIODevice>
#include <QStringView>

#include "log.h"

namespace {
QString previewHex( const QByteArray& data, int maxBytes = 128 )
{
    const auto slice = data.left( maxBytes );
    auto hex = QString::fromLatin1( slice.toHex( ' ' ) );
    if ( data.size() > maxBytes ) {
        hex.append( QStringLiteral( " ... (%1 bytes total)" ).arg( data.size() ) );
    }
    return hex;
}
} // namespace

SerialCaptureWorker::SerialCaptureWorker( SerialCaptureSettings settings, QObject* parent )
    : QObject( parent )
    , settings_( std::move( settings ) )
    , file_( settings_.filePath )
{
}

void SerialCaptureWorker::start()
{
    if ( stopping_ ) {
        Q_EMIT finished();
        return;
    }

    if ( settings_.portName.isEmpty() ) {
        stopping_ = true;
        Q_EMIT errorOccurred( tr( "No COM port selected." ) );
        Q_EMIT finished();
        return;
    }

    if ( settings_.filePath.isEmpty() ) {
        stopping_ = true;
        Q_EMIT errorOccurred( tr( "No capture file path selected." ) );
        Q_EMIT finished();
        return;
    }

    if ( !file_.open( QIODevice::WriteOnly | QIODevice::Append ) ) {
        stopping_ = true;
        Q_EMIT errorOccurred( tr( "Failed to open capture file: %1" ).arg( file_.errorString() ) );
        Q_EMIT finished();
        return;
    }

    port_ = new QSerialPort( this );
    port_->setPortName( settings_.portName );
    port_->setBaudRate( settings_.baudRate );
    port_->setDataBits( settings_.dataBits );
    port_->setParity( settings_.parity );
    port_->setStopBits( settings_.stopBits );
    port_->setFlowControl( settings_.flowControl );

    connect( port_, &QSerialPort::readyRead, this, &SerialCaptureWorker::onReadyRead );
    connect( port_, &QSerialPort::errorOccurred, this, &SerialCaptureWorker::onError );

    if ( !port_->open( QIODevice::ReadWrite ) ) {
        stopping_ = true;
        Q_EMIT errorOccurred( tr( "Failed to open %1: %2" ).arg( settings_.portName, port_->errorString() ) );
        file_.close();
        Q_EMIT finished();
        return;
    }
}

void SerialCaptureWorker::stop()
{
    if ( stopping_ ) {
        return;
    }

    stopping_ = true;

    if ( port_ && port_->isOpen() ) {
        port_->close();
    }

    if ( file_.isOpen() ) {
        file_.flush();
        file_.close();
    }

    Q_EMIT finished();
}

void SerialCaptureWorker::sendData( QByteArray data )
{
    if ( stopping_ || data.isEmpty() || !port_ || !port_->isOpen() ) {
        return;
    }

    LOG_INFO << "COM write to " << settings_.portName.toStdString() << ", bytes: "
             << data.size() << ", hex: " << previewHex( data ).toStdString();

    const auto written = port_->write( data );
    if ( written < 0 ) {
        Q_EMIT errorOccurred(
            tr( "Failed to write to %1: %2" ).arg( settings_.portName, port_->errorString() ) );
        return;
    }
    if ( written != data.size() ) {
        Q_EMIT errorOccurred(
            tr( "Partial write to %1: %2/%3 bytes." ).arg( settings_.portName ).arg( written ).arg( data.size() ) );
    }

    if ( !port_->waitForBytesWritten( 1000 ) ) {
        Q_EMIT errorOccurred(
            tr( "Timed out writing to %1: %2" ).arg( settings_.portName, port_->errorString() ) );
    }
}

void SerialCaptureWorker::appendToFile( QByteArray data )
{
    if ( stopping_ || data.isEmpty() || !file_.isOpen() ) {
        return;
    }

    const auto written = file_.write( data );
    if ( written != data.size() ) {
        Q_EMIT errorOccurred( tr( "Failed to write capture file: %1" ).arg( file_.errorString() ) );
        stop();
        return;
    }
    file_.flush();
}

void SerialCaptureWorker::onReadyRead()
{
    if ( stopping_ || !port_ ) {
        return;
    }

    const auto data = port_->readAll();
    if ( data.isEmpty() ) {
        return;
    }

    const auto written = file_.write( data );
    if ( written != data.size() ) {
        Q_EMIT errorOccurred( tr( "Failed to write capture file: %1" ).arg( file_.errorString() ) );
        stop();
        return;
    }
    if ( ++flushCounter_ >= 8 ) {
        file_.flush();
        flushCounter_ = 0;
    }

    Q_EMIT dataReceived( data );
}

void SerialCaptureWorker::onError( QSerialPort::SerialPortError error )
{
    if ( stopping_ || error == QSerialPort::NoError ) {
        return;
    }

    Q_EMIT errorOccurred( port_ ? port_->errorString() : tr( "Serial port error." ) );
    stop();
}
