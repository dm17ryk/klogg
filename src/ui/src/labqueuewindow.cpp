#include "labqueuewindow.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

#include "labclient.h"

namespace {
constexpr auto LabQueueSettingsGroup = "labQueue";
constexpr auto ControllerUrlKey = "controllerUrl";
constexpr auto TokenFileKey = "tokenFile";
}

LabQueueWindow::LabQueueWindow( QWidget* parent )
    : QWidget( parent )
    , controllerUrlEdit_( new QLineEdit( this ) )
    , tokenFileEdit_( new QLineEdit( this ) )
    , agentsList_( new QListWidget( this ) )
    , jobsList_( new QListWidget( this ) )
    , summaryLabel_( new QLabel( this ) )
    , detailsEdit_( new QPlainTextEdit( this ) )
    , refreshButton_( new QPushButton( tr( "Refresh" ), this ) )
    , openArtifactsButton_( new QPushButton( tr( "Open Artifacts" ), this ) )
{
    setWindowTitle( tr( "CILogg - lab queue" ) );
    resize( 1100, 720 );

    auto* rootLayout = new QVBoxLayout( this );

    auto* controllerRow = new QHBoxLayout();
    controllerRow->addWidget( new QLabel( tr( "Controller URL" ), this ) );
    controllerRow->addWidget( controllerUrlEdit_, 1 );
    controllerRow->addWidget( new QLabel( tr( "Token File" ), this ) );
    controllerRow->addWidget( tokenFileEdit_, 1 );
    auto* browseTokenButton = new QPushButton( tr( "Browse..." ), this );
    controllerRow->addWidget( browseTokenButton );
    controllerRow->addWidget( refreshButton_ );
    controllerRow->addWidget( openArtifactsButton_ );
    rootLayout->addLayout( controllerRow );

    auto* splitter = new QSplitter( this );
    auto* leftPane = new QWidget( splitter );
    auto* leftLayout = new QVBoxLayout( leftPane );
    leftLayout->addWidget( new QLabel( tr( "Agents" ), leftPane ) );
    leftLayout->addWidget( agentsList_, 1 );
    leftLayout->addWidget( new QLabel( tr( "Jobs" ), leftPane ) );
    leftLayout->addWidget( jobsList_, 2 );

    auto* rightPane = new QWidget( splitter );
    auto* rightLayout = new QVBoxLayout( rightPane );
    summaryLabel_->setWordWrap( true );
    rightLayout->addWidget( summaryLabel_ );
    detailsEdit_->setReadOnly( true );
    rightLayout->addWidget( detailsEdit_, 1 );

    splitter->addWidget( leftPane );
    splitter->addWidget( rightPane );
    splitter->setStretchFactor( 0, 1 );
    splitter->setStretchFactor( 1, 2 );
    rootLayout->addWidget( splitter, 1 );

    connect( refreshButton_, &QPushButton::clicked, this, &LabQueueWindow::refresh );
    connect( browseTokenButton, &QPushButton::clicked, this, &LabQueueWindow::browseTokenFile );
    connect( jobsList_, &QListWidget::currentRowChanged, this, &LabQueueWindow::selectedJobChanged );
    connect( openArtifactsButton_, &QPushButton::clicked, this, &LabQueueWindow::openArtifactsFolder );

    loadState();
    refresh();
}

void LabQueueWindow::refresh()
{
    saveState();

    LabClient client;
    QString errorMessage;
    if ( !client.loadToken( tokenFileEdit_->text().trimmed(), &errorMessage ) ) {
        summaryLabel_->setText( errorMessage );
        return;
    }

    const auto snapshot = client.snapshot( controllerUrlEdit_->text().trimmed(), &errorMessage );
    if ( snapshot.isEmpty() ) {
        summaryLabel_->setText( errorMessage );
        return;
    }

    agentsList_->clear();
    for ( const auto& agentValue : snapshot.value( QStringLiteral( "agents" ) ).toList() ) {
        const auto agent = agentValue.toMap();
        auto* item = new QListWidgetItem(
            tr( "%1 [%2]" )
                .arg( agent.value( QStringLiteral( "agentId" ) ).toString(),
                      agent.value( QStringLiteral( "status" ) ).toString() ),
            agentsList_ );
        item->setToolTip( QJsonDocument::fromVariant( agent ).toJson( QJsonDocument::Indented ) );
    }

    const auto previousJobId
        = jobsList_->currentItem() == nullptr ? QString{} : jobsList_->currentItem()->data( Qt::UserRole ).toString();
    jobsList_->clear();
    int selectedRow = -1;
    int row = 0;
    for ( const auto& jobValue : snapshot.value( QStringLiteral( "jobs" ) ).toList() ) {
        const auto job = jobValue.toMap();
        auto* item = new QListWidgetItem(
            tr( "%1 | %2 | %3" )
                .arg( job.value( QStringLiteral( "jobId" ) ).toString(),
                      job.value( QStringLiteral( "state" ) ).toString(),
                      job.value( QStringLiteral( "suiteName" ) ).toString().isEmpty()
                          ? job.value( QStringLiteral( "scenarioFile" ) ).toString()
                          : job.value( QStringLiteral( "suiteName" ) ).toString() ),
            jobsList_ );
        item->setData( Qt::UserRole, job.value( QStringLiteral( "jobId" ) ).toString() );
        item->setToolTip( QJsonDocument::fromVariant( job ).toJson( QJsonDocument::Indented ) );
        if ( item->data( Qt::UserRole ).toString() == previousJobId ) {
            selectedRow = row;
        }
        ++row;
    }

    if ( selectedRow >= 0 ) {
        jobsList_->setCurrentRow( selectedRow );
    }
    else if ( jobsList_->count() > 0 ) {
        jobsList_->setCurrentRow( 0 );
    }

    summaryLabel_->setText(
        tr( "Agents: %1 | Jobs: %2 | State Dir: %3" )
            .arg( QString::number( agentsList_->count() ),
                  QString::number( jobsList_->count() ),
                  snapshot.value( QStringLiteral( "stateDir" ) ).toString() ) );
    selectedJobChanged();
}

void LabQueueWindow::selectedJobChanged()
{
    const auto* item = jobsList_->currentItem();
    if ( item == nullptr ) {
        detailsEdit_->clear();
        openArtifactsButton_->setEnabled( false );
        return;
    }

    LabClient client;
    QString errorMessage;
    if ( !client.loadToken( tokenFileEdit_->text().trimmed(), &errorMessage ) ) {
        detailsEdit_->setPlainText( errorMessage );
        return;
    }

    const auto payload = client.status( controllerUrlEdit_->text().trimmed(),
                                        item->data( Qt::UserRole ).toString(), &errorMessage );
    if ( payload.isEmpty() ) {
        detailsEdit_->setPlainText( errorMessage );
        openArtifactsButton_->setEnabled( false );
        return;
    }

    detailsEdit_->setPlainText(
        QString::fromUtf8( QJsonDocument::fromVariant( payload ).toJson( QJsonDocument::Indented ) ) );
    openArtifactsButton_->setEnabled( !payload.value( QStringLiteral( "artifactDir" ) ).toString().isEmpty() );
}

void LabQueueWindow::browseTokenFile()
{
    const auto fileName = QFileDialog::getOpenFileName( this, tr( "Select token file" ),
                                                        tokenFileEdit_->text() );
    if ( !fileName.isEmpty() ) {
        tokenFileEdit_->setText( fileName );
        saveState();
    }
}

void LabQueueWindow::openArtifactsFolder()
{
    const auto* item = jobsList_->currentItem();
    if ( item == nullptr ) {
        return;
    }

    LabClient client;
    QString errorMessage;
    if ( !client.loadToken( tokenFileEdit_->text().trimmed(), &errorMessage ) ) {
        return;
    }

    const auto payload = client.status( controllerUrlEdit_->text().trimmed(),
                                        item->data( Qt::UserRole ).toString(), &errorMessage );
    const auto artifactDir = payload.value( QStringLiteral( "artifactDir" ) ).toString();
    if ( !artifactDir.isEmpty() ) {
        QDesktopServices::openUrl( QUrl::fromLocalFile( artifactDir ) );
    }
}

void LabQueueWindow::loadState()
{
    QSettings settings;
    settings.beginGroup( QString::fromLatin1( LabQueueSettingsGroup ) );
    controllerUrlEdit_->setText( settings.value( QString::fromLatin1( ControllerUrlKey ),
                                                 QStringLiteral( "http://127.0.0.1:5091" ) )
                                     .toString() );
    tokenFileEdit_->setText( settings.value( QString::fromLatin1( TokenFileKey ) ).toString() );
    settings.endGroup();
}

void LabQueueWindow::saveState() const
{
    QSettings settings;
    settings.beginGroup( QString::fromLatin1( LabQueueSettingsGroup ) );
    settings.setValue( QString::fromLatin1( ControllerUrlKey ), controllerUrlEdit_->text().trimmed() );
    settings.setValue( QString::fromLatin1( TokenFileKey ), tokenFileEdit_->text().trimmed() );
    settings.endGroup();
}
