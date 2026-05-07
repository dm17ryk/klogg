#include "comportutils.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "configuration.h"

namespace {
QString resolveComCaptureBaseDir( const QString& preferredPath )
{
    if ( !preferredPath.trimmed().isEmpty() ) {
        const QFileInfo preferredInfo{ preferredPath };
        if ( preferredInfo.exists() && preferredInfo.isDir() ) {
            return preferredInfo.absoluteFilePath();
        }
        if ( !preferredInfo.absolutePath().isEmpty() ) {
            return preferredInfo.absolutePath();
        }
    }

    const QFileInfo defaultPath{ Configuration::get().defaultComLogPath() };
    if ( defaultPath.exists() && defaultPath.isDir() ) {
        return defaultPath.absoluteFilePath();
    }
    if ( !defaultPath.absolutePath().isEmpty() ) {
        return defaultPath.absolutePath();
    }

    return defaultComLogDirectory();
}

QString buildComCaptureFileName( const SerialCaptureSettings& settings,
                                 const QDateTime& timestamp,
                                 int suffix = 0 )
{
    const auto portName
        = settings.portName.isEmpty() ? QStringLiteral( "port" ) : settings.portName.toLower();
    const auto baudRate = QString::number( settings.baudRate );
    const auto timestampText = timestamp.toString( QStringLiteral( "yyyy-MM-dd_HH-mm-ss" ) );
    auto stem = QString( "%1_%2_%3" ).arg( portName, baudRate, timestampText );
    if ( suffix > 1 ) {
        stem.append( QStringLiteral( "_%1" ).arg( suffix ) );
    }
    return stem.append( QStringLiteral( ".log" ) );
}
} // namespace

QString defaultComLogDirectory()
{
    auto docs = QStandardPaths::writableLocation( QStandardPaths::DocumentsLocation );
    if ( docs.isEmpty() ) {
        docs = QDir::homePath();
    }

    const QString initialDir = QDir( docs ).filePath( QStringLiteral( "kloggs" ) );
    QDir initDir( initialDir );
    if ( !initDir.exists() ) {
        initDir.mkpath( "." );
    }

    return initialDir;
}

SerialCaptureSettings defaultSerialCaptureSettings()
{
    const auto& config = Configuration::get();

    SerialCaptureSettings settings;
    settings.baudRate = config.defaultComBaudRate();
    settings.dataBits = config.defaultComDataBits();
    settings.parity = config.defaultComParity();
    settings.stopBits = config.defaultComStopBits();
    settings.flowControl = config.defaultComFlowControl();
    settings.filePath = config.defaultComLogPath();
    settings.addTimestamps = config.defaultComTimestampEnabled();
    settings.timestampFormat = config.defaultComTimestampFormat();
    settings.logTransmits = config.defaultComLogTransmits();
    return settings;
}

QString suggestedComCapturePath( const SerialCaptureSettings& settings )
{
    auto baseDir = resolveComCaptureBaseDir( settings.filePath );
    QDir dir( baseDir );
    if ( !dir.exists() && !dir.mkpath( "." ) ) {
        dir.setPath( defaultComLogDirectory() );
        dir.mkpath( "." );
    }

    const auto fileName = buildComCaptureFileName( settings, QDateTime::currentDateTime() );
    return dir.filePath( fileName );
}

QString suggestedNextComCapturePath( const SerialCaptureSettings& settings,
                                     const QDateTime& timestamp )
{
    auto baseDir = resolveComCaptureBaseDir( settings.filePath );
    QDir dir( baseDir );
    if ( !dir.exists() && !dir.mkpath( "." ) ) {
        dir.setPath( defaultComLogDirectory() );
        dir.mkpath( "." );
    }

    const QFileInfo currentInfo{ settings.filePath };
    const auto currentPath = currentInfo.absoluteFilePath();
    for ( int suffix = 0;; suffix = suffix == 0 ? 2 : suffix + 1 ) {
        const auto candidate = dir.absoluteFilePath(
            buildComCaptureFileName( settings, timestamp, suffix ) );
        if ( QFileInfo( candidate ).absoluteFilePath() != currentPath && !QFileInfo::exists( candidate ) ) {
            return candidate;
        }
    }
}

SerialCaptureSettings resolveCommanderComSettings( const CommanderComSettings& overrides )
{
    auto settings = defaultSerialCaptureSettings();
    settings.portName = overrides.portName.trimmed();

    const bool filePathProvided = overrides.filePath.has_value();
    if ( overrides.filePath ) {
        settings.filePath = overrides.filePath->trimmed();
    }
    if ( overrides.baudRate ) {
        settings.baudRate = *overrides.baudRate;
    }
    if ( overrides.dataBits ) {
        settings.dataBits = *overrides.dataBits;
    }
    if ( overrides.parity ) {
        settings.parity = *overrides.parity;
    }
    if ( overrides.stopBits ) {
        settings.stopBits = *overrides.stopBits;
    }
    if ( overrides.flowControl ) {
        settings.flowControl = *overrides.flowControl;
    }
    if ( overrides.addTimestamps ) {
        settings.addTimestamps = *overrides.addTimestamps;
    }
    if ( overrides.timestampFormat ) {
        settings.timestampFormat = overrides.timestampFormat->trimmed();
    }
    if ( overrides.logTransmits ) {
        settings.logTransmits = *overrides.logTransmits;
    }
    if ( overrides.useForActions ) {
        settings.useForActions = *overrides.useForActions;
    }

    const QFileInfo configuredInfo{ settings.filePath };
    if ( !filePathProvided ) {
        settings.filePath = suggestedComCapturePath( settings );
    }
    else if ( settings.filePath.trimmed().isEmpty()
              || ( configuredInfo.exists() && configuredInfo.isDir() ) ) {
        settings.filePath = suggestedComCapturePath( settings );
    }
    else {
        settings.filePath = configuredInfo.absoluteFilePath();
    }

    return settings;
}

bool ensureComCaptureFileWritable( const QString& path, QString* errorMessage )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Append ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    file.close();
    return true;
}

void populateSerialControls( QComboBox* baudCombo,
                             QComboBox* dataBitsCombo,
                             QComboBox* parityCombo,
                             QComboBox* stopBitsCombo,
                             QComboBox* flowCombo )
{
    if ( baudCombo ) {
        const QList<int> rates = { 300,   600,   1200,  1800,  2400,  4800,  7200,   9600,
                                   14400, 19200, 38400, 57600, 115200, 230400, 460800, 921600 };
        baudCombo->clear();
        for ( const auto rate : rates ) {
            baudCombo->addItem( QString::number( rate ),
                                static_cast<int>( static_cast<QSerialPort::BaudRate>( rate ) ) );
        }
        const auto defaultRateIndex = baudCombo->findData( static_cast<int>( QSerialPort::Baud115200 ) );
        if ( defaultRateIndex >= 0 ) {
            baudCombo->setCurrentIndex( defaultRateIndex );
        }
    }

    if ( dataBitsCombo ) {
        dataBitsCombo->clear();
        dataBitsCombo->addItem( QObject::tr( "5" ), static_cast<int>( QSerialPort::Data5 ) );
        dataBitsCombo->addItem( QObject::tr( "6" ), static_cast<int>( QSerialPort::Data6 ) );
        dataBitsCombo->addItem( QObject::tr( "7" ), static_cast<int>( QSerialPort::Data7 ) );
        dataBitsCombo->addItem( QObject::tr( "8" ), static_cast<int>( QSerialPort::Data8 ) );
        dataBitsCombo->setCurrentIndex( dataBitsCombo->findData( static_cast<int>( QSerialPort::Data8 ) ) );
    }

    if ( parityCombo ) {
        parityCombo->clear();
        parityCombo->addItem( QObject::tr( "None" ), static_cast<int>( QSerialPort::NoParity ) );
        parityCombo->addItem( QObject::tr( "Even" ), static_cast<int>( QSerialPort::EvenParity ) );
        parityCombo->addItem( QObject::tr( "Odd" ), static_cast<int>( QSerialPort::OddParity ) );
        parityCombo->addItem( QObject::tr( "Mark" ), static_cast<int>( QSerialPort::MarkParity ) );
        parityCombo->addItem( QObject::tr( "Space" ), static_cast<int>( QSerialPort::SpaceParity ) );
        parityCombo->setCurrentIndex( parityCombo->findData( static_cast<int>( QSerialPort::NoParity ) ) );
    }

    if ( stopBitsCombo ) {
        stopBitsCombo->clear();
        stopBitsCombo->addItem( QObject::tr( "1" ), static_cast<int>( QSerialPort::OneStop ) );
        stopBitsCombo->addItem( QObject::tr( "1.5" ), static_cast<int>( QSerialPort::OneAndHalfStop ) );
        stopBitsCombo->addItem( QObject::tr( "2" ), static_cast<int>( QSerialPort::TwoStop ) );
        stopBitsCombo->setCurrentIndex( stopBitsCombo->findData( static_cast<int>( QSerialPort::OneStop ) ) );
    }

    if ( flowCombo ) {
        flowCombo->clear();
        flowCombo->addItem( QObject::tr( "None" ), static_cast<int>( QSerialPort::NoFlowControl ) );
        flowCombo->addItem( QObject::tr( "RTS/CTS" ), static_cast<int>( QSerialPort::HardwareControl ) );
        flowCombo->addItem( QObject::tr( "XON/XOFF" ), static_cast<int>( QSerialPort::SoftwareControl ) );
        flowCombo->setCurrentIndex( flowCombo->findData( static_cast<int>( QSerialPort::NoFlowControl ) ) );
    }
}

