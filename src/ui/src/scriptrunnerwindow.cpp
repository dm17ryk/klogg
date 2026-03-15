#include "scriptrunnerwindow.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "scriptsupervisor.h"

ScriptRunnerWindow::ScriptRunnerWindow( ScriptSupervisor* supervisor, QWidget* parent )
    : QWidget( parent )
    , supervisor_( supervisor )
    , scriptPathEdit_( new QLineEdit( this ) )
    , argsJsonPathEdit_( new QLineEdit( this ) )
    , statusLabel_( new QLabel( this ) )
    , summaryLabel_( new QLabel( this ) )
    , outputEdit_( new QPlainTextEdit( this ) )
    , runButton_( new QPushButton( tr( "Run" ), this ) )
    , rerunButton_( new QPushButton( tr( "Rerun" ), this ) )
    , stopButton_( new QPushButton( tr( "Stop" ), this ) )
{
    setWindowTitle( tr( "klogg - script runner" ) );
    resize( 880, 520 );

    auto* rootLayout = new QVBoxLayout( this );

    auto* scriptRow = new QHBoxLayout();
    scriptRow->addWidget( new QLabel( tr( "Script" ), this ) );
    scriptRow->addWidget( scriptPathEdit_, 1 );
    auto* browseScriptButton = new QPushButton( tr( "Browse..." ), this );
    scriptRow->addWidget( browseScriptButton );
    rootLayout->addLayout( scriptRow );

    auto* argsRow = new QHBoxLayout();
    argsRow->addWidget( new QLabel( tr( "Args JSON" ), this ) );
    argsRow->addWidget( argsJsonPathEdit_, 1 );
    auto* browseArgsButton = new QPushButton( tr( "Browse..." ), this );
    argsRow->addWidget( browseArgsButton );
    rootLayout->addLayout( argsRow );

    auto* buttonRow = new QHBoxLayout();
    auto* openFolderButton = new QPushButton( tr( "Open Folder" ), this );
    buttonRow->addWidget( runButton_ );
    buttonRow->addWidget( rerunButton_ );
    buttonRow->addWidget( stopButton_ );
    buttonRow->addWidget( openFolderButton );
    buttonRow->addStretch( 1 );
    rootLayout->addLayout( buttonRow );

    rootLayout->addWidget( statusLabel_ );
    rootLayout->addWidget( summaryLabel_ );

    outputEdit_->setReadOnly( true );
    outputEdit_->setLineWrapMode( QPlainTextEdit::NoWrap );
    rootLayout->addWidget( outputEdit_, 1 );

    connect( browseScriptButton, &QPushButton::clicked, this,
             &ScriptRunnerWindow::browseScriptFile );
    connect( browseArgsButton, &QPushButton::clicked, this,
             &ScriptRunnerWindow::browseArgsFile );
    connect( runButton_, &QPushButton::clicked, this, &ScriptRunnerWindow::runScript );
    connect( rerunButton_, &QPushButton::clicked, this, &ScriptRunnerWindow::rerunScript );
    connect( stopButton_, &QPushButton::clicked, this, &ScriptRunnerWindow::stopScript );
    connect( openFolderButton, &QPushButton::clicked, this, &ScriptRunnerWindow::openScriptFolder );

    if ( supervisor_ != nullptr ) {
        connect( supervisor_, &ScriptSupervisor::statusChanged, this,
                 &ScriptRunnerWindow::refreshFromSupervisor );
        connect( supervisor_, &ScriptSupervisor::outputChanged, this,
                 &ScriptRunnerWindow::refreshFromSupervisor );
    }

    refreshFromSupervisor();
}

void ScriptRunnerWindow::setScriptPath( const QString& path )
{
    scriptPathEdit_->setText( path );
}

void ScriptRunnerWindow::browseScriptFile()
{
    const auto fileName = QFileDialog::getOpenFileName( this, tr( "Select Python script" ),
                                                        scriptPathEdit_->text(),
                                                        tr( "Python files (*.py);;All files (*)" ) );
    if ( !fileName.isEmpty() ) {
        scriptPathEdit_->setText( fileName );
    }
}

void ScriptRunnerWindow::browseArgsFile()
{
    const auto fileName = QFileDialog::getOpenFileName( this, tr( "Select JSON arguments file" ),
                                                        argsJsonPathEdit_->text(),
                                                        tr( "JSON files (*.json);;All files (*)" ) );
    if ( !fileName.isEmpty() ) {
        argsJsonPathEdit_->setText( fileName );
    }
}

void ScriptRunnerWindow::runScript()
{
    if ( supervisor_ == nullptr ) {
        return;
    }
    supervisor_->runScript( scriptPathEdit_->text().trimmed(), argsJsonPathEdit_->text().trimmed() );
    refreshFromSupervisor();
}

void ScriptRunnerWindow::rerunScript()
{
    runScript();
}

void ScriptRunnerWindow::stopScript()
{
    if ( supervisor_ == nullptr ) {
        return;
    }
    supervisor_->stopScript();
    refreshFromSupervisor();
}

void ScriptRunnerWindow::openScriptFolder()
{
    const QFileInfo info( scriptPathEdit_->text().trimmed() );
    if ( !info.exists() ) {
        return;
    }
    QDesktopServices::openUrl( QUrl::fromLocalFile( info.absolutePath() ) );
}

void ScriptRunnerWindow::refreshFromSupervisor()
{
    QVariantMap status;
    if ( supervisor_ != nullptr ) {
        status = supervisor_->scriptStatus().payload;
    }

    statusLabel_->setText( tr( "Status: %1" ).arg( status.value( QStringLiteral( "state" ) ).toString() ) );

    const auto startedAt = status.value( QStringLiteral( "startedAt" ) ).toString();
    const auto finishedAt = status.value( QStringLiteral( "finishedAt" ) ).toString();
    const auto exitCode = status.value( QStringLiteral( "exitCode" ) ).toInt();
    const auto lastError = status.value( QStringLiteral( "lastError" ) ).toString();
    summaryLabel_->setText(
        tr( "Started: %1 | Finished: %2 | Exit: %3 | Error: %4" )
            .arg( startedAt.isEmpty() ? tr( "-" ) : startedAt,
                  finishedAt.isEmpty() ? tr( "-" ) : finishedAt,
                  QString::number( exitCode ),
                  lastError.isEmpty() ? tr( "-" ) : lastError ) );

    QStringList lines;
    const auto output = status.value( QStringLiteral( "outputTail" ) ).toList();
    for ( const auto& line : output ) {
        lines.push_back( line.toString() );
    }
    outputEdit_->setPlainText( lines.join( '\n' ) );

    const bool active = supervisor_ != nullptr && supervisor_->hasActiveScript();
    runButton_->setEnabled( !active );
    rerunButton_->setEnabled( !scriptPathEdit_->text().trimmed().isEmpty() && !active );
    stopButton_->setEnabled( active );
}
