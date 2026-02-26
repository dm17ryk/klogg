#include "comportutils.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

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

