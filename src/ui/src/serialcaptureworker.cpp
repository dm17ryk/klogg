#include "serialcaptureworker.h"

#include <QIODevice>
#include <QStringView>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include "configuration.h"
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

QByteArray timestampPrefix( const SerialCaptureSettings& settings, QStringView direction )
{
    const auto ts = QDateTime::currentDateTime().toString( settings.timestampFormat );
    QString prefix = ts;
    prefix.append( ' ' );
    prefix.append( direction );
    prefix.append( QStringLiteral( " - " ) );
    return prefix.toUtf8();
}

} // namespace

QString serializeSerialCaptureSettings( const SerialCaptureSettings& settings )
{
    QJsonObject object;
    object.insert( QStringLiteral( "portName" ), settings.portName );
    object.insert( QStringLiteral( "baudRate" ), settings.baudRate );
    object.insert( QStringLiteral( "dataBits" ), static_cast<int>( settings.dataBits ) );
    object.insert( QStringLiteral( "parity" ), static_cast<int>( settings.parity ) );
    object.insert( QStringLiteral( "stopBits" ), static_cast<int>( settings.stopBits ) );
    object.insert( QStringLiteral( "flowControl" ), static_cast<int>( settings.flowControl ) );
    object.insert( QStringLiteral( "filePath" ), settings.filePath );
    object.insert( QStringLiteral( "addTimestamps" ), settings.addTimestamps );
    object.insert( QStringLiteral( "timestampFormat" ), settings.timestampFormat );
    object.insert( QStringLiteral( "logTransmits" ), settings.logTransmits );
    object.insert( QStringLiteral( "useForActions" ), settings.useForActions );

    return QString::fromUtf8( QJsonDocument( object ).toJson( QJsonDocument::Compact ) );
}

std::optional<SerialCaptureSettings> deserializeSerialCaptureSettings( const QString& serialized )
{
    if ( serialized.trimmed().isEmpty() ) {
        return std::nullopt;
    }

    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson( serialized.toUtf8(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
        return std::nullopt;
    }

    const auto object = document.object();
    SerialCaptureSettings settings;
    const auto& config = Configuration::get();

    settings.baudRate = static_cast<qint32>( config.defaultComBaudRate() );
    settings.dataBits = config.defaultComDataBits();
    settings.parity = config.defaultComParity();
    settings.stopBits = config.defaultComStopBits();
    settings.flowControl = config.defaultComFlowControl();
    settings.filePath = config.defaultComLogPath();
    settings.addTimestamps = config.defaultComTimestampEnabled();
    settings.timestampFormat = config.defaultComTimestampFormat();
    settings.logTransmits = config.defaultComLogTransmits();
    settings.useForActions = false;

    settings.portName = object.value( QStringLiteral( "portName" ) ).toString();
    if ( settings.portName.trimmed().isEmpty() ) {
        return std::nullopt;
    }

    settings.baudRate = object.value( QStringLiteral( "baudRate" ) ).toInt( settings.baudRate );
    settings.dataBits = static_cast<QSerialPort::DataBits>(
        object.value( QStringLiteral( "dataBits" ) ).toInt( static_cast<int>( settings.dataBits ) ) );
    settings.parity = static_cast<QSerialPort::Parity>(
        object.value( QStringLiteral( "parity" ) ).toInt( static_cast<int>( settings.parity ) ) );
    settings.stopBits = static_cast<QSerialPort::StopBits>(
        object.value( QStringLiteral( "stopBits" ) ).toInt( static_cast<int>( settings.stopBits ) ) );
    settings.flowControl = static_cast<QSerialPort::FlowControl>(
        object.value( QStringLiteral( "flowControl" ) ).toInt( static_cast<int>( settings.flowControl ) ) );
    settings.filePath = object.value( QStringLiteral( "filePath" ) ).toString( settings.filePath );
    settings.addTimestamps = object.value( QStringLiteral( "addTimestamps" ) ).toBool( settings.addTimestamps );
    settings.timestampFormat
        = object.value( QStringLiteral( "timestampFormat" ) ).toString( settings.timestampFormat );
    settings.logTransmits = object.value( QStringLiteral( "logTransmits" ) ).toBool( settings.logTransmits );
    settings.useForActions = object.value( QStringLiteral( "useForActions" ) ).toBool( settings.useForActions );

    return settings;
}

SerialCaptureWorker::SerialCaptureWorker( SerialCaptureSettings settings, QObject* parent )
    : QObject( parent )
    , settings_( std::move( settings ) )
    , file_( settings_.filePath )
{
}

bool SerialCaptureWorker::switchCaptureFile( const QString& filePath, QString* errorMessage )
{
    if ( filePath.trimmed().isEmpty() ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = tr( "No capture file path selected." );
        }
        return false;
    }

    QFile nextFile( filePath );
    if ( !nextFile.open( QIODevice::WriteOnly | QIODevice::Append ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = nextFile.errorString();
        }
        return false;
    }
    nextFile.close();

    if ( file_.isOpen() ) {
        file_.flush();
        file_.close();
    }

    file_.setFileName( filePath );
    if ( !file_.open( QIODevice::WriteOnly | QIODevice::Append ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = file_.errorString();
        }
        return false;
    }

    settings_.filePath = filePath;
    atLineStart_ = true;
    flushCounter_ = 0;
    return true;
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
    atLineStart_ = true;
    flushCounter_ = 0;

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
    else {
        Q_EMIT dataTransmitted( data.left( qMax<qint64>( 0, written ) ) );
    }

    if ( loggingEnabled_ && settings_.logTransmits && file_.isOpen() ) {
        QByteArray toWrite;
        toWrite.reserve( data.size() + 64 );
        if ( settings_.addTimestamps ) {
            toWrite.append(
                timestampPrefix( settings_, QStringView( QStringLiteral( "[TX]" ) ) ) );
        }
        else {
            toWrite.append( "[TX] - " );
        }
        toWrite.append( data );
        if ( !data.endsWith( '\n' ) ) {
            toWrite.append( '\n' );
        }
        const auto logged = file_.write( toWrite );
        if ( logged != toWrite.size() ) {
            Q_EMIT errorOccurred( tr( "Failed to log TX to file." ) );
        }
        else {
            file_.flush();
        }
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

void SerialCaptureWorker::setLoggingEnabled( bool enabled )
{
    loggingEnabled_ = enabled;
    if ( file_.isOpen() ) {
        file_.flush();
    }
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

    QByteArray toWrite;
    toWrite.reserve( data.size() + 64 );

    if ( settings_.addTimestamps ) {
        for ( const auto byte : data ) {
            if ( atLineStart_ ) {
                toWrite.append(
                    timestampPrefix( settings_, QStringView( QStringLiteral( "[RX]" ) ) ) );
                atLineStart_ = false;
            }
            toWrite.append( byte );
            if ( byte == '\n' ) {
                atLineStart_ = true;
            }
        }
    }
    else {
        toWrite.append( data );
    }

    const bool hasNewline = data.contains( '\n' );
    if ( loggingEnabled_ ) {
        const auto written = file_.write( toWrite );
        if ( written != toWrite.size() ) {
            Q_EMIT errorOccurred( tr( "Failed to write capture file: %1" ).arg( file_.errorString() ) );
            stop();
            return;
        }
        if ( hasNewline || ++flushCounter_ >= 8 ) {
            file_.flush();
            flushCounter_ = 0;
        }
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
