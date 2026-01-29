#include "opencomportdialog.h"

#include <QCheckBox>
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
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "configuration.h"
#include "streamsourceregistry.h"
#include "comportutils.h"

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
    timestampCheck_ = new QCheckBox( tr( "Add timestamp to log" ), this );
    timestampFormatEdit_ = new QLineEdit( QStringLiteral( "dd/MM/yyyy HH:mm:ss.zzz" ), this );
    logTxCheck_ = new QCheckBox( tr( "Log transmitted data" ), this );
    useForActionsCheck_ = new QCheckBox( tr( "Use this COM for actions" ), this );

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
    grid->addWidget( useForActionsCheck_, row, 0, 1, 2 );
    row++;

    auto* fileRow = new QWidget( this );
    auto* fileLayout = new QHBoxLayout( fileRow );
    fileLayout->setContentsMargins( 0, 0, 0, 0 );
    fileLayout->addWidget( fileEdit_ );
    fileLayout->addWidget( browseButton_ );
    grid->addWidget( new QLabel( tr( "File" ), this ), row, 0 );
    grid->addWidget( fileRow, row, 1 );
    row++;

    grid->addWidget( timestampCheck_, row, 0, 1, 2 );
    row++;
    grid->addWidget( new QLabel( tr( "Timestamp format" ), this ), row, 0 );
    grid->addWidget( timestampFormatEdit_, row, 1 );
    row++;
    grid->addWidget( logTxCheck_, row, 0, 1, 2 );

    mainLayout->addLayout( grid );

    buttonBox_ = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    openButton_ = buttonBox_->button( QDialogButtonBox::Ok );
    openButton_->setText( tr( "Open" ) );
    mainLayout->addWidget( buttonBox_ );

    connect( buttonBox_, &QDialogButtonBox::accepted, this, &QDialog::accept );
    connect( buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject );

    populatePorts();
    populateSerialControls( baudCombo_, dataBitsCombo_, parityCombo_, stopBitsCombo_, flowCombo_ );

    const auto& config = Configuration::get();
    auto selectByValue = []( QComboBox* combo, int value ) {
        const auto idx = combo->findData( value );
        if ( idx >= 0 ) {
            combo->setCurrentIndex( idx );
        }
    };
    selectByValue( baudCombo_, config.defaultComBaudRate() );
    selectByValue( dataBitsCombo_, config.defaultComDataBits() );
    selectByValue( parityCombo_, config.defaultComParity() );
    selectByValue( stopBitsCombo_, config.defaultComStopBits() );
    selectByValue( flowCombo_, config.defaultComFlowControl() );

    if ( !config.defaultComLogPath().isEmpty() ) {
        fileEdit_->setText( config.defaultComLogPath() );
        const QFileInfo info( config.defaultComLogPath() );
        userEditedPath_ = !info.isDir();
    }
    timestampCheck_->setChecked( config.defaultComTimestampEnabled() );
    timestampFormatEdit_->setText( config.defaultComTimestampFormat() );
    timestampFormatEdit_->setEnabled( timestampCheck_->isChecked() );
    logTxCheck_->setChecked( config.defaultComLogTransmits() );

    connect( portCombo_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &OpenComPortDialog::updateSuggestedFileName );
    connect( baudCombo_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &OpenComPortDialog::updateSuggestedFileName );
    connect( fileEdit_, &QLineEdit::textEdited, this, &OpenComPortDialog::markFilePathEdited );
    connect( fileEdit_, &QLineEdit::textChanged, this, &OpenComPortDialog::validateInputs );
    connect( browseButton_, &QPushButton::clicked, this, &OpenComPortDialog::browseForFile );
    connect( timestampCheck_, &QCheckBox::toggled, this, [ this ]( bool checked ) {
        timestampFormatEdit_->setEnabled( checked );
        validateInputs();
    } );
    connect( timestampFormatEdit_, &QLineEdit::textChanged, this, &OpenComPortDialog::validateInputs );

    updateSuggestedFileName();
    // Ensure filename is present even if only a directory was loaded from config.
    if ( QFileInfo( fileEdit_->text() ).isDir() ) {
        const auto suggested = suggestedFileName();
        fileEdit_->setText( suggested );
        lastSuggestedPath_ = suggested;
        userEditedPath_ = false;
    }
    // Keep timestamp format enabled in line with the checkbox default state.
    timestampFormatEdit_->setEnabled( timestampCheck_->isChecked() );
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
    settings.addTimestamps = timestampCheck_->isChecked();
    settings.timestampFormat = timestampFormatEdit_->text().trimmed();
    settings.logTransmits = logTxCheck_->isChecked();
    settings.useForActions = useForActionsCheck_->isChecked();
    return settings;
}

void OpenComPortDialog::updateSuggestedFileName()
{
    const auto currentPath = fileEdit_->text().trimmed();

    auto makeSuggested = [&]() {
        const auto portNameValue = portCombo_->currentData().toString();
        const auto portName = portNameValue.isEmpty() ? QString( "port" ) : portNameValue.toLower();
        const auto baudRate = baudCombo_->currentData().toString();
        const auto timestamp = QDateTime::currentDateTime().toString( "yyyy-MM-dd_HH-mm-ss" );
        const auto fileName = QString( "%1_%2_%3.log" ).arg( portName, baudRate, timestamp );

        const auto& config = Configuration::get();
        QString baseDir = config.defaultComLogPath();

        // If the current path points to a directory, prefer it.
        const QFileInfo info( currentPath );
        if ( info.isDir() && info.exists() ) {
            baseDir = info.absoluteFilePath();
        }

        if ( baseDir.isEmpty() ) {
            baseDir = defaultComLogDirectory();
        }

        QDir dir( baseDir );
        if ( !dir.exists() ) {
            if ( !dir.mkpath( "." ) ) {
                dir.setPath( defaultComLogDirectory() );
                dir.mkpath( "." );
            }
        }

        return dir.filePath( fileName );
    };

    const auto suggested = makeSuggested();
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

    const auto currentIndex = portCombo_->currentIndex();
    const bool hasPort = !portCombo_->currentData().toString().isEmpty();
    const bool portEnabled = isPortItemEnabled( currentIndex );
    const bool validFormat = !timestampCheck_->isChecked()
                             || !timestampFormatEdit_->text().trimmed().isEmpty();
    openButton_->setEnabled( validPath && hasPort && portEnabled && validFormat );
}

void OpenComPortDialog::markFilePathEdited()
{
    userEditedPath_ = true;
}

void OpenComPortDialog::populatePorts()
{
    portCombo_->clear();
    portCombo_->setEnabled( true );

    const auto ports = QSerialPortInfo::availablePorts();
    if ( ports.isEmpty() ) {
        portCombo_->addItem( tr( "none" ), QString() );
        portCombo_->setEnabled( false );
        return;
    }

    auto* model = qobject_cast<QStandardItemModel*>( portCombo_->model() );
    int firstEnabled = -1;

    for ( const auto& port : ports ) {
        const auto portName = port.portName();
        const bool inUse = StreamSourceRegistry::get().isSerialPortActive( portName );
        auto label = port.description().isEmpty()
                         ? portName
                         : QString( "%1 (%2)" ).arg( portName, port.description() );
        if ( inUse ) {
            label = QString( "%1 (%2)" ).arg( label, tr( "in use" ) );
        }
        portCombo_->addItem( label, portName );

        const auto index = portCombo_->count() - 1;
        if ( model ) {
            if ( auto* item = model->item( index ) ) {
                item->setEnabled( !inUse );
            }
        }

        if ( !inUse && firstEnabled < 0 ) {
            firstEnabled = index;
        }
    }

    if ( firstEnabled >= 0 ) {
        portCombo_->setCurrentIndex( firstEnabled );
    }
}

QString OpenComPortDialog::suggestedFileName() const
{
    const auto portNameValue = portCombo_->currentData().toString();
    const auto portName = portNameValue.isEmpty() ? QString( "port" ) : portNameValue.toLower();
    const auto baudRate = baudCombo_->currentData().toString();
    const auto timestamp = QDateTime::currentDateTime().toString( "yyyy-MM-dd_HH-mm-ss" );

    const auto& config = Configuration::get();

    QString logsDirPath = config.defaultComLogPath();
    if ( logsDirPath.isEmpty() ) {
        logsDirPath = defaultComLogDirectory();
    }
    QDir logsDir( logsDirPath );
    if ( !logsDir.exists() ) {
        if ( !logsDir.mkpath( "." ) ) {
            logsDir.setPath( defaultComLogDirectory() );
            logsDir.mkpath( "." );
        }
    }

    const auto fileName = QString( "%1_%2_%3.log" ).arg( portName, baudRate, timestamp );
    return logsDir.filePath( fileName );
}

bool OpenComPortDialog::isPortItemEnabled( int index ) const
{
    if ( !portCombo_->isEnabled() ) {
        return false;
    }

    if ( index < 0 ) {
        return false;
    }

    auto* model = qobject_cast<QStandardItemModel*>( portCombo_->model() );
    if ( !model ) {
        return true;
    }

    auto* item = model->item( index );
    return item ? item->isEnabled() : true;
}
