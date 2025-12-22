#include "opencomportdialog.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QVBoxLayout>

OpenComPortDialog::OpenComPortDialog( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Open COM Port" ) );

    auto* mainLayout = new QVBoxLayout( this );
    auto* grid = new QGridLayout();
    int row = 0;

    portCombo_ = new QComboBox( this );
    baudCombo_ = new QComboBox( this );
    dataBitsCombo_ = new QComboBox( this );
    parityCombo_ = new QComboBox( this );
    stopBitsCombo_ = new QComboBox( this );
    flowCombo_ = new QComboBox( this );
    fileEdit_ = new QLineEdit( this );
    browseButton_ = new QPushButton( tr( "Browse..." ), this );

    grid->addWidget( new QLabel( tr( "Port" ), this ), row, 0 );
    grid->addWidget( portCombo_, row, 1 );
    row++;
    grid->addWidget( new QLabel( tr( "Baud" ), this ), row, 0 );
    grid->addWidget( baudCombo_, row, 1 );
    row++;
    grid->addWidget( new QLabel( tr( "Data bits" ), this ), row, 0 );
    grid->addWidget( dataBitsCombo_, row, 1 );
    row++;
    grid->addWidget( new QLabel( tr( "Parity" ), this ), row, 0 );
    grid->addWidget( parityCombo_, row, 1 );
    row++;
    grid->addWidget( new QLabel( tr( "Stop bits" ), this ), row, 0 );
    grid->addWidget( stopBitsCombo_, row, 1 );
    row++;
    grid->addWidget( new QLabel( tr( "Flow control" ), this ), row, 0 );
    grid->addWidget( flowCombo_, row, 1 );
    row++;

    auto* fileRow = new QWidget( this );
    auto* fileLayout = new QHBoxLayout( fileRow );
    fileLayout->setContentsMargins( 0, 0, 0, 0 );
    fileLayout->addWidget( fileEdit_ );
    fileLayout->addWidget( browseButton_ );
    grid->addWidget( new QLabel( tr( "File" ), this ), row, 0 );
    grid->addWidget( fileRow, row, 1 );

    mainLayout->addLayout( grid );

    buttonBox_ = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    openButton_ = buttonBox_->button( QDialogButtonBox::Ok );
    openButton_->setText( tr( "Open" ) );
    mainLayout->addWidget( buttonBox_ );

    connect( buttonBox_, &QDialogButtonBox::accepted, this, &QDialog::accept );
    connect( buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject );

    populatePorts();
    populateBaudRates();
    populateDataBits();
    populateParity();
    populateStopBits();
    populateFlowControl();

    connect( portCombo_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &OpenComPortDialog::updateSuggestedFileName );
    connect( baudCombo_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &OpenComPortDialog::updateSuggestedFileName );
    connect( fileEdit_, &QLineEdit::textEdited, this, &OpenComPortDialog::markFilePathEdited );
    connect( fileEdit_, &QLineEdit::textChanged, this, &OpenComPortDialog::validateInputs );
    connect( browseButton_, &QPushButton::clicked, this, &OpenComPortDialog::browseForFile );

    updateSuggestedFileName();
    validateInputs();
}

SerialCaptureSettings OpenComPortDialog::settings() const
{
    SerialCaptureSettings settings;
    settings.portName = portCombo_->currentData().toString();
    settings.baudRate = baudCombo_->currentData().toInt();
    settings.dataBits = static_cast<QSerialPort::DataBits>( dataBitsCombo_->currentData().toInt() );
    settings.parity = static_cast<QSerialPort::Parity>( parityCombo_->currentData().toInt() );
    settings.stopBits = static_cast<QSerialPort::StopBits>( stopBitsCombo_->currentData().toInt() );
    settings.flowControl
        = static_cast<QSerialPort::FlowControl>( flowCombo_->currentData().toInt() );
    settings.filePath = fileEdit_->text().trimmed();
    return settings;
}

void OpenComPortDialog::updateSuggestedFileName()
{
    const auto suggested = suggestedFileName();
    const auto currentPath = fileEdit_->text().trimmed();
    const bool canUpdate
        = !userEditedPath_ || currentPath.isEmpty() || currentPath == lastSuggestedPath_;

    if ( canUpdate ) {
        fileEdit_->setText( suggested );
        lastSuggestedPath_ = suggested;
        userEditedPath_ = false;
    }

    validateInputs();
}

void OpenComPortDialog::browseForFile()
{
    const auto selected = QFileDialog::getSaveFileName(
        this, tr( "Capture log file" ), fileEdit_->text(),
        tr( "Log files (*.log);;All files (*)" ) );
    if ( selected.isEmpty() ) {
        return;
    }

    fileEdit_->setText( selected );
    userEditedPath_ = true;
    validateInputs();
}

void OpenComPortDialog::validateInputs()
{
    const auto path = fileEdit_->text().trimmed();
    bool validPath = false;
    if ( !path.isEmpty() ) {
        const QFileInfo info( path );
        const QDir dir( info.absolutePath() );
        validPath = dir.exists();
    }

    const bool hasPort = !portCombo_->currentData().toString().isEmpty();
    openButton_->setEnabled( validPath && hasPort );
}

void OpenComPortDialog::markFilePathEdited()
{
    userEditedPath_ = true;
}

void OpenComPortDialog::populatePorts()
{
    const auto ports = QSerialPortInfo::availablePorts();
    for ( const auto& port : ports ) {
        const auto label = port.description().isEmpty()
                               ? port.portName()
                               : QString( "%1 (%2)" ).arg( port.portName(), port.description() );
        portCombo_->addItem( label, port.portName() );
    }

    if ( portCombo_->count() == 0 ) {
        portCombo_->addItem( tr( "No ports detected" ), QString() );
        portCombo_->setEnabled( false );
    }
}

void OpenComPortDialog::populateBaudRates()
{
    const QList<int> rates = { 300,   600,   1200,   1800,  2400,  4800,  7200,   9600,
                               14400, 19200, 38400,  57600, 115200, 230400, 460800, 921600 };
    for ( const auto rate : rates ) {
        baudCombo_->addItem( QString::number( rate ), rate );
    }

    const auto defaultRateIndex = baudCombo_->findData( 115200 );
    if ( defaultRateIndex >= 0 ) {
        baudCombo_->setCurrentIndex( defaultRateIndex );
    }
}

void OpenComPortDialog::populateDataBits()
{
    dataBitsCombo_->addItem( tr( "5" ), QSerialPort::Data5 );
    dataBitsCombo_->addItem( tr( "6" ), QSerialPort::Data6 );
    dataBitsCombo_->addItem( tr( "7" ), QSerialPort::Data7 );
    dataBitsCombo_->addItem( tr( "8" ), QSerialPort::Data8 );
    dataBitsCombo_->setCurrentIndex( dataBitsCombo_->findData( QSerialPort::Data8 ) );
}

void OpenComPortDialog::populateParity()
{
    parityCombo_->addItem( tr( "None" ), QSerialPort::NoParity );
    parityCombo_->addItem( tr( "Even" ), QSerialPort::EvenParity );
    parityCombo_->addItem( tr( "Odd" ), QSerialPort::OddParity );
    parityCombo_->addItem( tr( "Mark" ), QSerialPort::MarkParity );
    parityCombo_->addItem( tr( "Space" ), QSerialPort::SpaceParity );
    parityCombo_->setCurrentIndex( parityCombo_->findData( QSerialPort::NoParity ) );
}

void OpenComPortDialog::populateStopBits()
{
    stopBitsCombo_->addItem( tr( "1" ), QSerialPort::OneStop );
    stopBitsCombo_->addItem( tr( "1.5" ), QSerialPort::OneAndHalfStop );
    stopBitsCombo_->addItem( tr( "2" ), QSerialPort::TwoStop );
    stopBitsCombo_->setCurrentIndex( stopBitsCombo_->findData( QSerialPort::OneStop ) );
}

void OpenComPortDialog::populateFlowControl()
{
    flowCombo_->addItem( tr( "None" ), QSerialPort::NoFlowControl );
    flowCombo_->addItem( tr( "RTS/CTS" ), QSerialPort::HardwareControl );
    flowCombo_->addItem( tr( "XON/XOFF" ), QSerialPort::SoftwareControl );
    flowCombo_->setCurrentIndex( flowCombo_->findData( QSerialPort::NoFlowControl ) );
}

QString OpenComPortDialog::suggestedFileName() const
{
    const auto portName = portCombo_->currentData().toString().toLower();
    const auto baudRate = baudCombo_->currentData().toString();
    const auto timestamp = QDateTime::currentDateTime().toString( "yyyy-MM-dd_HH-mm-ss" );
    return QString( "c:\\logs\\%1_%2_%3.log" ).arg( portName, baudRate, timestamp );
}
