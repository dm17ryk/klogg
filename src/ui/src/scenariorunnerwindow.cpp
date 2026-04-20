#include "scenariorunnerwindow.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

#include "scenariorunner.h"

namespace {

QString suiteIdForPath( const QString& path )
{
    return QFileInfo( path ).completeBaseName();
}

constexpr auto ScenarioRunnerSettingsGroup = "scenarioRunner";
constexpr auto LastSuiteKey = "lastSuiteFile";
constexpr auto RecentScenariosKey = "recentScenarioFiles";

} // namespace

ScenarioRunnerWindow::ScenarioRunnerWindow( ScenarioRunner* runner, QWidget* parent )
    : QWidget( parent )
    , runner_( runner )
    , scenarioFileEdit_( new QLineEdit( this ) )
    , scenarioArgsEdit_( new QLineEdit( this ) )
    , suiteFileEdit_( new QLineEdit( this ) )
    , suiteScenariosList_( new QListWidget( this ) )
    , selectedScenarioLabel_( new QLabel( this ) )
    , statusLabel_( new QLabel( this ) )
    , summaryLabel_( new QLabel( this ) )
    , reportLabel_( new QLabel( this ) )
    , outputEdit_( new QPlainTextEdit( this ) )
    , runScenarioButton_( new QPushButton( tr( "Run Scenario" ), this ) )
    , runSuiteButton_( new QPushButton( tr( "Run Suite" ), this ) )
    , stopButton_( new QPushButton( tr( "Stop" ), this ) )
    , openJsonButton_( new QPushButton( tr( "Open JSON Report" ), this ) )
    , openJunitButton_( new QPushButton( tr( "Open JUnit Report" ), this ) )
{
    setWindowTitle( tr( "CILogg - scenario runner" ) );
    resize( 980, 720 );

    auto* rootLayout = new QVBoxLayout( this );

    auto* scenarioRow = new QHBoxLayout();
    scenarioRow->addWidget( new QLabel( tr( "Scenario" ), this ) );
    scenarioRow->addWidget( scenarioFileEdit_, 1 );
    auto* browseScenarioButton = new QPushButton( tr( "Browse..." ), this );
    scenarioRow->addWidget( browseScenarioButton );
    rootLayout->addLayout( scenarioRow );

    auto* scenarioArgsRow = new QHBoxLayout();
    scenarioArgsRow->addWidget( new QLabel( tr( "Scenario Args" ), this ) );
    scenarioArgsRow->addWidget( scenarioArgsEdit_, 1 );
    auto* browseScenarioArgsButton = new QPushButton( tr( "Browse..." ), this );
    scenarioArgsRow->addWidget( browseScenarioArgsButton );
    rootLayout->addLayout( scenarioArgsRow );

    auto* suiteRow = new QHBoxLayout();
    suiteRow->addWidget( new QLabel( tr( "Suite" ), this ) );
    suiteRow->addWidget( suiteFileEdit_, 1 );
    auto* browseSuiteButton = new QPushButton( tr( "Browse..." ), this );
    auto* newSuiteButton = new QPushButton( tr( "New..." ), this );
    suiteRow->addWidget( browseSuiteButton );
    suiteRow->addWidget( newSuiteButton );
    rootLayout->addLayout( suiteRow );

    auto* runRow = new QHBoxLayout();
    runRow->addWidget( runScenarioButton_ );
    runRow->addWidget( runSuiteButton_ );
    runRow->addWidget( stopButton_ );
    runRow->addSpacing( 12 );
    runRow->addWidget( openJsonButton_ );
    runRow->addWidget( openJunitButton_ );
    runRow->addStretch( 1 );
    rootLayout->addLayout( runRow );

    rootLayout->addWidget( new QLabel( tr( "Suite Scenarios" ), this ) );
    suiteScenariosList_->setSelectionMode( QAbstractItemView::SingleSelection );
    rootLayout->addWidget( suiteScenariosList_, 1 );

    auto* suiteButtonsRow = new QHBoxLayout();
    auto* addScenarioButton = new QPushButton( tr( "Add" ), this );
    auto* removeScenarioButton = new QPushButton( tr( "Remove" ), this );
    auto* moveUpButton = new QPushButton( tr( "Up" ), this );
    auto* moveDownButton = new QPushButton( tr( "Down" ), this );
    auto* browseSelectedArgsButton = new QPushButton( tr( "Set Args" ), this );
    auto* clearSelectedArgsButton = new QPushButton( tr( "Clear Args" ), this );
    auto* saveSuiteButton = new QPushButton( tr( "Save Suite" ), this );
    auto* deleteSuiteButton = new QPushButton( tr( "Delete Suite" ), this );
    suiteButtonsRow->addWidget( addScenarioButton );
    suiteButtonsRow->addWidget( removeScenarioButton );
    suiteButtonsRow->addWidget( moveUpButton );
    suiteButtonsRow->addWidget( moveDownButton );
    suiteButtonsRow->addWidget( browseSelectedArgsButton );
    suiteButtonsRow->addWidget( clearSelectedArgsButton );
    suiteButtonsRow->addWidget( saveSuiteButton );
    suiteButtonsRow->addWidget( deleteSuiteButton );
    suiteButtonsRow->addStretch( 1 );
    rootLayout->addLayout( suiteButtonsRow );

    selectedScenarioLabel_->setWordWrap( true );
    rootLayout->addWidget( selectedScenarioLabel_ );
    rootLayout->addWidget( statusLabel_ );
    rootLayout->addWidget( summaryLabel_ );
    reportLabel_->setWordWrap( true );
    rootLayout->addWidget( reportLabel_ );

    outputEdit_->setReadOnly( true );
    outputEdit_->setLineWrapMode( QPlainTextEdit::NoWrap );
    rootLayout->addWidget( outputEdit_, 2 );

    connect( browseScenarioButton, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::browseScenarioFile );
    connect( browseScenarioArgsButton, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::browseScenarioArgsFile );
    connect( browseSuiteButton, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::browseSuiteFile );
    connect( newSuiteButton, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::createSuiteFile );
    connect( runScenarioButton_, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::runScenario );
    connect( runSuiteButton_, &QPushButton::clicked, this, &ScenarioRunnerWindow::runSuite );
    connect( stopButton_, &QPushButton::clicked, this, &ScenarioRunnerWindow::stopScenarioRun );
    connect( openJsonButton_, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::openJsonReport );
    connect( openJunitButton_, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::openJunitReport );
    connect( addScenarioButton, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::addSuiteScenario );
    connect( removeScenarioButton, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::removeSuiteScenario );
    connect( moveUpButton, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::moveSuiteScenarioUp );
    connect( moveDownButton, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::moveSuiteScenarioDown );
    connect( browseSelectedArgsButton, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::browseSelectedScenarioArgs );
    connect( clearSelectedArgsButton, &QPushButton::clicked, this,
             &ScenarioRunnerWindow::clearSelectedScenarioArgs );
    connect( saveSuiteButton, &QPushButton::clicked, this, &ScenarioRunnerWindow::saveSuite );
    connect( deleteSuiteButton, &QPushButton::clicked, this, &ScenarioRunnerWindow::deleteSuite );
    connect( suiteScenariosList_, &QListWidget::currentRowChanged, this,
             &ScenarioRunnerWindow::suiteSelectionChanged );
    connect( suiteScenariosList_, &QListWidget::itemChanged, this,
             &ScenarioRunnerWindow::suiteItemChanged );

    if ( runner_ != nullptr ) {
        connect( runner_, &ScenarioRunner::statusChanged, this,
                 &ScenarioRunnerWindow::refreshFromRunner );
        connect( runner_, &ScenarioRunner::outputChanged, this,
                 &ScenarioRunnerWindow::refreshFromRunner );
    }

    loadUiState();
    refreshFromRunner();
}

void ScenarioRunnerWindow::browseScenarioFile()
{
    const auto fileName = QFileDialog::getOpenFileName( this, tr( "Select scenario file" ),
                                                        scenarioFileEdit_->text(),
                                                        tr( "Python files (*.py);;All files (*)" ) );
    if ( !fileName.isEmpty() ) {
        scenarioFileEdit_->setText( fileName );
        pushRecentScenario( fileName );
        saveUiState();
    }
}

void ScenarioRunnerWindow::browseScenarioArgsFile()
{
    const auto fileName = QFileDialog::getOpenFileName( this, tr( "Select scenario JSON arguments" ),
                                                        scenarioArgsEdit_->text(),
                                                        tr( "JSON files (*.json);;All files (*)" ) );
    if ( !fileName.isEmpty() ) {
        scenarioArgsEdit_->setText( fileName );
        saveUiState();
    }
}

void ScenarioRunnerWindow::browseSuiteFile()
{
    const auto fileName = QFileDialog::getOpenFileName( this, tr( "Select scenario suite" ),
                                                        suiteFileEdit_->text(),
                                                        tr( "JSON files (*.json);;All files (*)" ) );
    if ( !fileName.isEmpty() ) {
        loadSuiteFile( fileName );
    }
}

void ScenarioRunnerWindow::createSuiteFile()
{
    const auto fileName = QFileDialog::getSaveFileName( this, tr( "Create scenario suite" ),
                                                        suiteFileEdit_->text(),
                                                        tr( "JSON files (*.json);;All files (*)" ) );
    if ( !fileName.isEmpty() ) {
        suiteFileEdit_->setText( fileName );
        suiteScenariosList_->clear();
        saveSuiteFile( fileName );
        saveUiState();
    }
}

void ScenarioRunnerWindow::runScenario()
{
    if ( runner_ == nullptr ) {
        return;
    }

    CommanderRequest request;
    request.action = CommanderAction::RunScenario;
    request.scenarioFilePath = scenarioFileEdit_->text().trimmed();
    request.argsJsonFilePath = scenarioArgsEdit_->text().trimmed();
    runner_->runScenario( request );
    saveUiState();
    refreshFromRunner();
}

void ScenarioRunnerWindow::runSuite()
{
    if ( runner_ == nullptr ) {
        return;
    }

    const auto suitePath = suiteFileEdit_->text().trimmed();
    if ( !suitePath.isEmpty() ) {
        saveSuiteFile( suitePath );
    }

    CommanderRequest request;
    request.action = CommanderAction::RunSuite;
    request.suiteFilePath = suitePath;
    runner_->runSuite( request );
    saveUiState();
    refreshFromRunner();
}

void ScenarioRunnerWindow::stopScenarioRun()
{
    if ( runner_ == nullptr ) {
        return;
    }

    runner_->stopRun();
    refreshFromRunner();
}

void ScenarioRunnerWindow::openJsonReport()
{
    if ( runner_ == nullptr ) {
        return;
    }

    const auto path = runner_->statusPayload().value( QStringLiteral( "reportJsonFile" ) ).toString();
    if ( QFileInfo::exists( path ) ) {
        QDesktopServices::openUrl( QUrl::fromLocalFile( path ) );
    }
}

void ScenarioRunnerWindow::openJunitReport()
{
    if ( runner_ == nullptr ) {
        return;
    }

    const auto path = runner_->statusPayload().value( QStringLiteral( "reportJunitFile" ) ).toString();
    if ( QFileInfo::exists( path ) ) {
        QDesktopServices::openUrl( QUrl::fromLocalFile( path ) );
    }
}

void ScenarioRunnerWindow::addSuiteScenario()
{
    const auto fileName = QFileDialog::getOpenFileName( this, tr( "Add scenario to suite" ),
                                                        scenarioFileEdit_->text(),
                                                        tr( "Python files (*.py);;All files (*)" ) );
    if ( fileName.isEmpty() ) {
        return;
    }

    auto* item = new QListWidgetItem( suiteScenariosList_ );
    item->setFlags( item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable );
    item->setCheckState( Qt::Checked );
    QVariantMap entry;
    entry.insert( QStringLiteral( "scenarioFile" ), fileName );
    entry.insert( QStringLiteral( "enabled" ), true );
    item->setData( Qt::UserRole, entry );
    updateItemText( item );
    suiteScenariosList_->setCurrentItem( item );
    pushRecentScenario( fileName );
    saveUiState();
}

void ScenarioRunnerWindow::removeSuiteScenario()
{
    delete suiteScenariosList_->takeItem( suiteScenariosList_->currentRow() );
    suiteSelectionChanged();
}

void ScenarioRunnerWindow::moveSuiteScenarioUp()
{
    const auto currentRow = suiteScenariosList_->currentRow();
    if ( currentRow <= 0 ) {
        return;
    }

    auto* item = suiteScenariosList_->takeItem( currentRow );
    suiteScenariosList_->insertItem( currentRow - 1, item );
    suiteScenariosList_->setCurrentRow( currentRow - 1 );
}

void ScenarioRunnerWindow::moveSuiteScenarioDown()
{
    const auto currentRow = suiteScenariosList_->currentRow();
    if ( currentRow < 0 || currentRow >= suiteScenariosList_->count() - 1 ) {
        return;
    }

    auto* item = suiteScenariosList_->takeItem( currentRow );
    suiteScenariosList_->insertItem( currentRow + 1, item );
    suiteScenariosList_->setCurrentRow( currentRow + 1 );
}

void ScenarioRunnerWindow::browseSelectedScenarioArgs()
{
    auto* item = suiteScenariosList_->currentItem();
    if ( item == nullptr ) {
        return;
    }

    const auto fileName = QFileDialog::getOpenFileName( this, tr( "Select scenario JSON arguments" ),
                                                        QString{},
                                                        tr( "JSON files (*.json);;All files (*)" ) );
    if ( fileName.isEmpty() ) {
        return;
    }

    auto entry = item->data( Qt::UserRole ).toMap();
    entry.insert( QStringLiteral( "argsJsonFile" ), fileName );
    item->setData( Qt::UserRole, entry );
    updateItemText( item );
}

void ScenarioRunnerWindow::clearSelectedScenarioArgs()
{
    auto* item = suiteScenariosList_->currentItem();
    if ( item == nullptr ) {
        return;
    }

    auto entry = item->data( Qt::UserRole ).toMap();
    entry.remove( QStringLiteral( "argsJsonFile" ) );
    item->setData( Qt::UserRole, entry );
    updateItemText( item );
}

void ScenarioRunnerWindow::saveSuite()
{
    const auto suitePath = suiteFileEdit_->text().trimmed();
    if ( suitePath.isEmpty() ) {
        createSuiteFile();
        return;
    }

    saveSuiteFile( suitePath );
    saveUiState();
}

void ScenarioRunnerWindow::deleteSuite()
{
    const auto suitePath = suiteFileEdit_->text().trimmed();
    if ( suitePath.isEmpty() ) {
        return;
    }

    QFile::remove( suitePath );
    suiteFileEdit_->clear();
    suiteScenariosList_->clear();
    selectedScenarioLabel_->setText( tr( "Selected scenario: -" ) );
    saveUiState();
}

void ScenarioRunnerWindow::suiteSelectionChanged()
{
    const auto* item = suiteScenariosList_->currentItem();
    if ( item == nullptr ) {
        selectedScenarioLabel_->setText( tr( "Selected scenario: -" ) );
        return;
    }

    const auto entry = item->data( Qt::UserRole ).toMap();
    const auto scenarioFile = entry.value( QStringLiteral( "scenarioFile" ) ).toString();
    const auto argsJsonFile = entry.value( QStringLiteral( "argsJsonFile" ) ).toString();
    selectedScenarioLabel_->setText(
        tr( "Selected scenario: %1 | Args: %2" )
            .arg( scenarioFile.isEmpty() ? tr( "-" ) : scenarioFile,
                  argsJsonFile.isEmpty() ? tr( "-" ) : argsJsonFile ) );
}

void ScenarioRunnerWindow::suiteItemChanged( QListWidgetItem* item )
{
    if ( item == nullptr ) {
        return;
    }

    auto entry = item->data( Qt::UserRole ).toMap();
    entry.insert( QStringLiteral( "enabled" ), item->checkState() == Qt::Checked );
    item->setData( Qt::UserRole, entry );
    updateItemText( item );
}

void ScenarioRunnerWindow::refreshFromRunner()
{
    const auto payload = runner_ == nullptr ? QVariantMap{} : runner_->statusPayload();
    if ( payload.isEmpty() ) {
        statusLabel_->setText( tr( "Status: -" ) );
        summaryLabel_->setText( tr( "No scenario run configured." ) );
        reportLabel_->setText( tr( "Reports: -" ) );
        outputEdit_->clear();
        stopButton_->setEnabled( false );
        openJsonButton_->setEnabled( false );
        openJunitButton_->setEnabled( false );
        return;
    }

    statusLabel_->setText(
        tr( "Status: %1 | Suite: %2 | Scenario: %3 | Step: %4" )
            .arg( payload.value( QStringLiteral( "state" ) ).toString(),
                  payload.value( QStringLiteral( "suiteName" ) ).toString().isEmpty()
                      ? tr( "-" )
                      : payload.value( QStringLiteral( "suiteName" ) ).toString(),
                  payload.value( QStringLiteral( "currentScenarioName" ) ).toString().isEmpty()
                      ? tr( "-" )
                      : payload.value( QStringLiteral( "currentScenarioName" ) ).toString(),
                  payload.value( QStringLiteral( "currentStepName" ) ).toString().isEmpty()
                      ? tr( "-" )
                      : payload.value( QStringLiteral( "currentStepName" ) ).toString() ) );

    summaryLabel_->setText(
        tr( "Scenarios: %1/%2 | Passed: %3 | Failed: %4 | Skipped: %5 | Error: %6" )
            .arg( QString::number( payload.value( QStringLiteral( "completedScenarios" ) ).toInt() ),
                  QString::number( payload.value( QStringLiteral( "totalScenarios" ) ).toInt() ),
                  QString::number( payload.value( QStringLiteral( "passedCount" ) ).toInt() ),
                  QString::number( payload.value( QStringLiteral( "failedCount" ) ).toInt() ),
                  QString::number( payload.value( QStringLiteral( "skippedCount" ) ).toInt() ),
                  payload.value( QStringLiteral( "lastError" ) ).toString().isEmpty()
                      ? tr( "-" )
                      : payload.value( QStringLiteral( "lastError" ) ).toString() ) );

    reportLabel_->setText(
        tr( "JSON: %1 | JUnit: %2" )
            .arg( payload.value( QStringLiteral( "reportJsonFile" ) ).toString().isEmpty()
                      ? tr( "-" )
                      : payload.value( QStringLiteral( "reportJsonFile" ) ).toString(),
                  payload.value( QStringLiteral( "reportJunitFile" ) ).toString().isEmpty()
                      ? tr( "-" )
                      : payload.value( QStringLiteral( "reportJunitFile" ) ).toString() ) );

    QStringList lines;
    for ( const auto& line : payload.value( QStringLiteral( "outputTail" ) ).toList() ) {
        lines.push_back( line.toString() );
    }
    outputEdit_->setPlainText( lines.join( '\n' ) );

    const auto state = payload.value( QStringLiteral( "state" ) ).toString();
    stopButton_->setEnabled( state == QStringLiteral( "starting" )
                             || state == QStringLiteral( "running" )
                             || state == QStringLiteral( "stopping" ) );
    openJsonButton_->setEnabled(
        QFileInfo::exists( payload.value( QStringLiteral( "reportJsonFile" ) ).toString() ) );
    openJunitButton_->setEnabled(
        QFileInfo::exists( payload.value( QStringLiteral( "reportJunitFile" ) ).toString() ) );
}

void ScenarioRunnerWindow::loadUiState()
{
    QSettings settings;
    settings.beginGroup( QLatin1StringView( ScenarioRunnerSettingsGroup ) );
    scenarioFileEdit_->setText( settings.value( QLatin1StringView( RecentScenariosKey ) ).toStringList().value( 0 ) );
    const auto suitePath = settings.value( QLatin1StringView( LastSuiteKey ) ).toString();
    settings.endGroup();
    if ( !suitePath.isEmpty() && QFileInfo::exists( suitePath ) ) {
        loadSuiteFile( suitePath );
    }
}

void ScenarioRunnerWindow::saveUiState() const
{
    QSettings settings;
    settings.beginGroup( QLatin1StringView( ScenarioRunnerSettingsGroup ) );
    settings.setValue( QLatin1StringView( LastSuiteKey ), suiteFileEdit_->text().trimmed() );
    settings.endGroup();
}

void ScenarioRunnerWindow::pushRecentScenario( const QString& path )
{
    QSettings settings;
    settings.beginGroup( QLatin1StringView( ScenarioRunnerSettingsGroup ) );
    auto recent = settings.value( QLatin1StringView( RecentScenariosKey ) ).toStringList();
    recent.removeAll( path );
    recent.prepend( path );
    while ( recent.size() > 10 ) {
        recent.removeLast();
    }
    settings.setValue( QLatin1StringView( RecentScenariosKey ), recent );
    settings.endGroup();
}

QVariantMap ScenarioRunnerWindow::suiteEntryFromItem( const QListWidgetItem* item ) const
{
    if ( item == nullptr ) {
        return {};
    }

    auto entry = item->data( Qt::UserRole ).toMap();
    if ( !entry.contains( QStringLiteral( "scenarioFile" ) ) ) {
        entry.insert( QStringLiteral( "scenarioFile" ), item->text() );
    }
    entry.insert( QStringLiteral( "enabled" ), item->checkState() == Qt::Checked );
    return entry;
}

void ScenarioRunnerWindow::updateItemText( QListWidgetItem* item )
{
    if ( item == nullptr ) {
        return;
    }

    const auto entry = suiteEntryFromItem( item );
    const auto scenarioFile = entry.value( QStringLiteral( "scenarioFile" ) ).toString();
    const auto argsJsonFile = entry.value( QStringLiteral( "argsJsonFile" ) ).toString();

    QString text = QFileInfo( scenarioFile ).fileName();
    if ( text.isEmpty() ) {
        text = scenarioFile;
    }
    if ( !argsJsonFile.isEmpty() ) {
        text += tr( " [args]" );
    }

    item->setText( text );
    item->setToolTip(
        tr( "Scenario: %1\nArgs: %2" )
            .arg( scenarioFile.isEmpty() ? tr( "-" ) : scenarioFile,
                  argsJsonFile.isEmpty() ? tr( "-" ) : argsJsonFile ) );
}

void ScenarioRunnerWindow::loadSuiteFile( const QString& path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return;
    }

    const auto document = QJsonDocument::fromJson( file.readAll() );
    if ( !document.isObject() ) {
        return;
    }

    suiteFileEdit_->setText( QFileInfo( path ).absoluteFilePath() );
    suiteScenariosList_->clear();

    const auto root = document.object();
    const auto scenarios = root.value( QStringLiteral( "scenarios" ) ).toArray();
    for ( const auto& scenarioValue : scenarios ) {
        const auto scenarioObject = scenarioValue.toObject();
        auto entry = scenarioObject.toVariantMap();
        if ( !entry.contains( QStringLiteral( "enabled" ) ) ) {
            entry.insert( QStringLiteral( "enabled" ), true );
        }

        auto* item = new QListWidgetItem( suiteScenariosList_ );
        item->setFlags( item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable );
        item->setCheckState(
            entry.value( QStringLiteral( "enabled" ), true ).toBool() ? Qt::Checked
                                                                      : Qt::Unchecked );
        item->setData( Qt::UserRole, entry );
        updateItemText( item );
    }

    if ( suiteScenariosList_->count() > 0 ) {
        suiteScenariosList_->setCurrentRow( 0 );
    }
    suiteSelectionChanged();
    saveUiState();
}

bool ScenarioRunnerWindow::saveSuiteFile( const QString& path )
{
    if ( path.isEmpty() ) {
        return false;
    }

    QJsonArray scenarios;
    for ( int index = 0; index < suiteScenariosList_->count(); ++index ) {
        const auto* item = suiteScenariosList_->item( index );
        const auto entry = suiteEntryFromItem( item );
        scenarios.push_back( QJsonObject::fromVariantMap( entry ) );
    }

    QJsonObject root;
    root.insert( QStringLiteral( "suiteId" ), suiteIdForPath( path ) );
    root.insert( QStringLiteral( "name" ), QFileInfo( path ).completeBaseName() );
    root.insert( QStringLiteral( "scenarios" ), scenarios );

    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        return false;
    }

    file.write( QJsonDocument( root ).toJson( QJsonDocument::Indented ) );
    file.close();
    return true;
}
