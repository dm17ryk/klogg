#include "scriptrunnerwindow.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "scriptsupervisor.h"

namespace {
CommanderRequest allScriptsRequest( CommanderAction action )
{
    CommanderRequest request;
    request.action = action;
    request.allEntities = true;
    return request;
}
} // namespace

ScriptRunnerWindow::ScriptRunnerWindow( ScriptSupervisor* supervisor, QWidget* parent )
    : QWidget( parent )
    , supervisor_( supervisor )
    , scriptPathEdit_( new QLineEdit( this ) )
    , argsJsonPathEdit_( new QLineEdit( this ) )
    , runsList_( new QListWidget( this ) )
    , statusLabel_( new QLabel( this ) )
    , summaryLabel_( new QLabel( this ) )
    , subscriptionsLabel_( new QLabel( this ) )
    , outputEdit_( new QPlainTextEdit( this ) )
    , runButton_( new QPushButton( tr( "Run On Active Tab" ), this ) )
    , rerunButton_( new QPushButton( tr( "Rerun Selected" ), this ) )
    , stopButton_( new QPushButton( tr( "Stop Selected" ), this ) )
{
    setWindowTitle( tr( "klogg - script runner" ) );
    resize( 960, 620 );

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

    rootLayout->addWidget( new QLabel( tr( "Script Runs" ), this ) );
    rootLayout->addWidget( runsList_, 1 );
    rootLayout->addWidget( statusLabel_ );
    rootLayout->addWidget( summaryLabel_ );
    subscriptionsLabel_->setWordWrap( true );
    rootLayout->addWidget( subscriptionsLabel_ );

    outputEdit_->setReadOnly( true );
    outputEdit_->setLineWrapMode( QPlainTextEdit::NoWrap );
    rootLayout->addWidget( outputEdit_, 2 );

    connect( browseScriptButton, &QPushButton::clicked, this,
             &ScriptRunnerWindow::browseScriptFile );
    connect( browseArgsButton, &QPushButton::clicked, this,
             &ScriptRunnerWindow::browseArgsFile );
    connect( runButton_, &QPushButton::clicked, this, &ScriptRunnerWindow::runScript );
    connect( rerunButton_, &QPushButton::clicked, this, &ScriptRunnerWindow::rerunScript );
    connect( stopButton_, &QPushButton::clicked, this, &ScriptRunnerWindow::stopScript );
    connect( openFolderButton, &QPushButton::clicked, this, &ScriptRunnerWindow::openScriptFolder );
    connect( runsList_, &QListWidget::currentRowChanged, this,
             &ScriptRunnerWindow::selectedRunChanged );

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

    CommanderRequest request;
    request.action = CommanderAction::RunScript;
    request.scriptFilePath = scriptPathEdit_->text().trimmed();
    request.argsJsonFilePath = argsJsonPathEdit_->text().trimmed();
    supervisor_->runScript( request );
    refreshFromSupervisor();
}

void ScriptRunnerWindow::rerunScript()
{
    if ( supervisor_ == nullptr || selectedTabId_.isEmpty() ) {
        return;
    }

    CommanderRequest request;
    request.action = CommanderAction::RunScript;
    request.scriptFilePath = scriptPathEdit_->text().trimmed();
    request.argsJsonFilePath = argsJsonPathEdit_->text().trimmed();
    request.tabId = selectedTabId_;
    supervisor_->runScript( request );
    refreshFromSupervisor();
}

void ScriptRunnerWindow::stopScript()
{
    if ( supervisor_ == nullptr || selectedTabId_.isEmpty() ) {
        return;
    }

    CommanderRequest request;
    request.action = CommanderAction::StopScript;
    request.tabId = selectedTabId_;
    supervisor_->stopScript( request );
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
    QVariantList runs;
    if ( supervisor_ != nullptr ) {
        const auto payload = supervisor_->scriptStatus( allScriptsRequest( CommanderAction::GetScriptStatus ) ).payload;
        runs = payload.value( QStringLiteral( "runs" ) ).toList();
    }

    const auto previousSelection = selectedTabId_;
    runsList_->blockSignals( true );
    runsList_->clear();
    int selectedRow = -1;
    for ( int index = 0; index < runs.size(); ++index ) {
        const auto run = runs.at( index ).toMap();
        const auto label
            = tr( "%1 | %2 | %3" )
                  .arg( run.value( QStringLiteral( "displayName" ) ).toString(),
                        run.value( QStringLiteral( "portName" ) ).toString(),
                        run.value( QStringLiteral( "state" ) ).toString() );
        auto* item = new QListWidgetItem( label, runsList_ );
        item->setData( Qt::UserRole, run );
        if ( run.value( QStringLiteral( "tabId" ) ).toString() == previousSelection ) {
            selectedRow = index;
        }
    }
    runsList_->blockSignals( false );

    if ( selectedRow >= 0 ) {
        runsList_->setCurrentRow( selectedRow );
    }
    else if ( runsList_->count() > 0 ) {
        runsList_->setCurrentRow( 0 );
    }
    else {
        selectedTabId_.clear();
        selectedRunChanged();
    }
}

void ScriptRunnerWindow::selectedRunChanged()
{
    const auto* item = runsList_->currentItem();
    const auto run = item == nullptr ? QVariantMap{} : item->data( Qt::UserRole ).toMap();
    selectedTabId_ = run.value( QStringLiteral( "tabId" ) ).toString();

    if ( run.isEmpty() ) {
        statusLabel_->setText( tr( "Status: -" ) );
        summaryLabel_->setText( tr( "No active or remembered per-tab scripts." ) );
        subscriptionsLabel_->setText( tr( "Subscriptions: -" ) );
        outputEdit_->clear();
        rerunButton_->setEnabled( false );
        stopButton_->setEnabled( false );
        return;
    }

    scriptPathEdit_->setText( run.value( QStringLiteral( "scriptFile" ) ).toString() );
    argsJsonPathEdit_->setText( run.value( QStringLiteral( "argsJsonFile" ) ).toString() );
    statusLabel_->setText(
        tr( "Status: %1 (%2)" )
            .arg( run.value( QStringLiteral( "state" ) ).toString(),
                  run.value( QStringLiteral( "displayName" ) ).toString() ) );

    summaryLabel_->setText(
        tr( "Port: %1 | Window: %2 Tab: %3 | Exit: %4 | Error: %5 | Callback: %6 | Dropped: %7" )
            .arg( run.value( QStringLiteral( "portName" ) ).toString(),
                  QString::number( run.value( QStringLiteral( "windowIndex" ) ).toInt() ),
                  QString::number( run.value( QStringLiteral( "tabIndex" ) ).toInt() ),
                  QString::number( run.value( QStringLiteral( "exitCode" ) ).toInt() ),
                  run.value( QStringLiteral( "lastError" ) ).toString().isEmpty()
                      ? tr( "-" )
                      : run.value( QStringLiteral( "lastError" ) ).toString(),
                  run.value( QStringLiteral( "lastCallbackError" ) ).toString().isEmpty()
                      ? tr( "-" )
                      : run.value( QStringLiteral( "lastCallbackError" ) ).toString(),
                  QString::number( run.value( QStringLiteral( "droppedEvents" ) ).toInt() ) ) );

    QStringList subscriptions;
    for ( const auto& entry : run.value( QStringLiteral( "subscriptions" ) ).toList() ) {
        const auto subscription = entry.toMap();
        auto line = subscription.value( QStringLiteral( "eventType" ) ).toString();
        if ( subscription.contains( QStringLiteral( "responseId" ) ) ) {
            line += tr( " response:%1" ).arg( subscription.value( QStringLiteral( "responseId" ) ).toInt() );
        }
        if ( subscription.contains( QStringLiteral( "actionId" ) ) ) {
            line += tr( " action:%1" ).arg( subscription.value( QStringLiteral( "actionId" ) ).toInt() );
        }
        subscriptions.push_back( line );
    }
    subscriptionsLabel_->setText(
        tr( "Subscriptions: %1" )
            .arg( subscriptions.isEmpty() ? tr( "-" ) : subscriptions.join( tr( "; " ) ) ) );

    QStringList lines;
    for ( const auto& line : run.value( QStringLiteral( "outputTail" ) ).toList() ) {
        lines.push_back( line.toString() );
    }
    outputEdit_->setPlainText( lines.join( '\n' ) );

    const auto state = run.value( QStringLiteral( "state" ) ).toString();
    rerunButton_->setEnabled( !scriptPathEdit_->text().trimmed().isEmpty() );
    stopButton_->setEnabled( state == QStringLiteral( "starting" )
                             || state == QStringLiteral( "running" )
                             || state == QStringLiteral( "stopping" ) );
}
