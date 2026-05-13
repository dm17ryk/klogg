/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of cilogg.
 *
 * cilogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * cilogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cilogg.  If not, see <http://www.gnu.org/licenses/>.
 */

// This file implements MainWindow. It is responsible for creating and
// managing the menus, the toolbar, and the CrawlerWidget. It also
// load/save the settings on opening/closing of the app

#include "configuration.h"
#include "containers.h"
#include "log.h"
#include <QNetworkReply>
#include <cassert>
#include <exception>

#include <iterator>
#include <qaction.h>
#include <qapplication.h>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // Q_OS_WIN

#include <QClipboard>
#include <QComboBox>
#include <QAbstractButton>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QSerialPortInfo>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QProgressDialog>
#include <QPixmap>
#include <QResource>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QSortFilterProxyModel>
#include <QTabBar>
#include <QTabWidget>
#include <QRegularExpression>
#include <QStringListModel>
#include <QTemporaryFile>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QUrl>
#include <QUrlQuery>
#include <QWindow>

#include "mainwindow.h"

#include "clipboard.h"
#include "crawlerwidget.h"
#include "decompressor.h"
#include "dispatch_to.h"
#include "downloader.h"
#include "actionsmanager.h"
#include "actionruntime.h"
#include "comportutils.h"
#include "encodings.h"
#include "favoritefiles.h"
#include "highlightersdialog.h"
#include "highlightersmenu.h"
#include "importactionsdialog.h"
#include "importpreviewsdialog.h"
#include "issuereporter.h"
#include "klogg_version.h"
#include "logger.h"
#include "mainwindowtext.h"
#include "previewmanager.h"
#include "opencomportdialog.h"
#include "openfilehelper.h"
#include "optionsdialog.h"
#include "predefinedfilters.h"
#include "predefinedfiltersdialog.h"
#include "progress.h"
#include "readablesize.h"
#include "recentfiles.h"
#include "sessioninfo.h"
#include "shortcuts.h"
#include "serialcaptureworker.h"
#include "startupprogress.h"
#include "streamsession.h"
#include "styles.h"
#include "tabbedcrawlerwidget.h"

namespace {

QIcon makePythonScriptRunnerIcon() {
    constexpr auto iconSize = 20;

    QPixmap pixmap( iconSize, iconSize );
    pixmap.fill( Qt::transparent );

    QPainter painter( &pixmap );
    painter.setRenderHint( QPainter::Antialiasing, true );

    auto font = painter.font();
    font.setBold( true );
    font.setPixelSize( 11 );
    painter.setFont( font );
    painter.setPen( qApp->palette().color( QPalette::WindowText ) );
    painter.drawText( pixmap.rect(), Qt::AlignCenter, QStringLiteral( "py" ) );

    return QIcon( pixmap );
}

bool applicationHasMethod( const char* signature )
{
    return qApp != nullptr && qApp->metaObject()->indexOfMethod( signature ) >= 0;
}

void signalCrawlerToFollowFile( CrawlerWidget* crawler_widget )
{
    if ( crawler_widget == nullptr ) {
        return;
    }

    QPointer<CrawlerWidget> crawler = crawler_widget;
    dispatchToObject(
        [ crawler ]() {
            if ( crawler != nullptr ) {
                crawler->followSet( true );
            }
        },
        crawler_widget );
}

void showComPortMessage( QWidget* parent, QMessageBox::Icon icon, const QString& title,
                         const QString& text, bool nonBlocking )
{
    if ( !nonBlocking ) {
        if ( icon == QMessageBox::Information ) {
            QMessageBox::information( parent, title, text );
        }
        else {
            QMessageBox::warning( parent, title, text );
        }
        return;
    }

    auto* box = new QMessageBox( icon, title, text, QMessageBox::Ok, parent );
    box->setAttribute( Qt::WA_DeleteOnClose );
    box->setWindowModality( Qt::NonModal );
    box->open();
}

void waitForCrawlerStartupPreparation( CrawlerWidget* crawler_widget, const QString& fileName )
{
    if ( crawler_widget == nullptr ) {
        return;
    }

    const auto shortName = QFileInfo( fileName ).fileName();
    StartupProgress::advance( QObject::tr( "Preparing tab" ), shortName );

    if ( !crawler_widget->isStartupPreparationPending() ) {
        StartupProgress::advance( QObject::tr( "Tab ready" ), shortName );
        return;
    }

    constexpr int StartupPrepareTimeoutMs = 120000;
    constexpr int StartupPollIntervalMs = 50;

    bool timedOut = false;
    qint64 lastReportedSecond = -1;
    QEventLoop waitLoop;
    QElapsedTimer elapsed;
    QTimer pollTimer;
    elapsed.start();
    pollTimer.setInterval( StartupPollIntervalMs );
    pollTimer.setSingleShot( false );

    const auto tryFinish = [ &waitLoop, crawler_widget ]() {
        if ( !crawler_widget->isStartupPreparationPending() ) {
            waitLoop.quit();
        }
    };

    const auto loadingFinishedConnection = QObject::connect(
        crawler_widget, &CrawlerWidget::loadingFinished, &waitLoop, [ & ]( LoadingStatus status ) {
            const auto detail = QObject::tr( "%1 (%2)" )
                                    .arg( shortName,
                                          status == LoadingStatus::Successful
                                              ? QObject::tr( "loading completed" )
                                              : QObject::tr( "loading failed" ) );
            StartupProgress::advance( QObject::tr( "Preparing tab" ), detail );
            tryFinish();
        } );

    const auto loadingProgressConnection = QObject::connect(
        crawler_widget, &CrawlerWidget::loadingProgressed, &waitLoop, [ & ]( int progress ) {
            StartupProgress::advance( QObject::tr( "Preparing tab" ),
                                      QObject::tr( "%1 (%2%)" ).arg( shortName ).arg( progress ) );
        } );

    const auto pollConnection = QObject::connect( &pollTimer, &QTimer::timeout, &waitLoop, [ & ]() {
        tryFinish();
        if ( !crawler_widget->isStartupPreparationPending() ) {
            return;
        }

        const auto elapsedMs = elapsed.elapsed();
        if ( elapsedMs >= StartupPrepareTimeoutMs ) {
            timedOut = true;
            waitLoop.quit();
            return;
        }

        const qint64 elapsedSeconds = elapsedMs / 1000;
        if ( elapsedSeconds != lastReportedSecond ) {
            lastReportedSecond = elapsedSeconds;
            StartupProgress::message( QObject::tr( "Preparing tab" ),
                                      QObject::tr( "%1 (waiting %2s)" )
                                          .arg( shortName )
                                          .arg( elapsedSeconds ) );
        }
    } );

    pollTimer.start();
    QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents
                                     | QEventLoop::ExcludeSocketNotifiers );
    tryFinish();
    if ( crawler_widget->isStartupPreparationPending() ) {
        waitLoop.exec( QEventLoop::ExcludeUserInputEvents );
    }

    pollTimer.stop();
    QObject::disconnect( loadingFinishedConnection );
    QObject::disconnect( loadingProgressConnection );
    QObject::disconnect( pollConnection );

    if ( timedOut ) {
        LOG_WARNING << "Startup tab preparation timeout for " << fileName.toStdString();
        StartupProgress::advance( QObject::tr( "Preparing tab" ),
                                  QObject::tr( "%1 (timeout)" ).arg( shortName ) );
    }
    else {
        StartupProgress::advance( QObject::tr( "Tab ready" ), shortName );
    }
}

static constexpr auto ClipboardMaxTry = 5;
static const auto ActionsPortSuffix = QStringLiteral( " (actions)" );

QString automationDisplayText( const QObject* object )
{
    if ( object == nullptr ) {
        return {};
    }

    if ( const auto* action = qobject_cast<const QAction*>( object ) ) {
        auto text = action->text();
        text.remove( '&' );
        return text;
    }

    if ( const auto* menu = qobject_cast<const QMenu*>( object ) ) {
        auto title = menu->title();
        title.remove( '&' );
        return title;
    }

    const auto textPropertyIndex = object->metaObject()->indexOfProperty( "text" );
    if ( textPropertyIndex >= 0 ) {
        auto text = object->property( "text" ).toString();
        text.remove( '&' );
        if ( !text.isEmpty() ) {
            return text;
        }
    }

    if ( const auto* widget = qobject_cast<const QWidget*>( object ) ) {
        auto windowTitle = widget->windowTitle();
        windowTitle.remove( '&' );
        return windowTitle;
    }

    return {};
}

QString automationRole( const QObject* object )
{
    if ( qobject_cast<const QAction*>( object ) != nullptr ) {
        return QStringLiteral( "action" );
    }
    if ( qobject_cast<const QMenu*>( object ) != nullptr ) {
        return QStringLiteral( "menu" );
    }
    if ( qobject_cast<const QToolBar*>( object ) != nullptr ) {
        return QStringLiteral( "toolbar" );
    }
    if ( qobject_cast<const QTabBar*>( object ) != nullptr ) {
        return QStringLiteral( "tabbar" );
    }
    if ( qobject_cast<const QTabWidget*>( object ) != nullptr ) {
        return QStringLiteral( "tabwidget" );
    }
    if ( qobject_cast<const QComboBox*>( object ) != nullptr ) {
        return QStringLiteral( "combobox" );
    }
    if ( qobject_cast<const QLineEdit*>( object ) != nullptr ) {
        return QStringLiteral( "lineedit" );
    }
    if ( qobject_cast<const QLabel*>( object ) != nullptr ) {
        return QStringLiteral( "label" );
    }
    if ( qobject_cast<const QSplitter*>( object ) != nullptr ) {
        return QStringLiteral( "splitter" );
    }
    if ( qobject_cast<const AbstractLogView*>( object ) != nullptr ) {
        return QStringLiteral( "logview" );
    }
    if ( qobject_cast<const QWidget*>( object ) != nullptr ) {
        return QStringLiteral( "widget" );
    }

    return QString::fromLatin1( object->metaObject()->className() ).toLower();
}

QVariantMap automationBounds( const QWidget* widget, const QWidget* rootWidget )
{
    QVariantMap bounds;
    if ( widget == nullptr || rootWidget == nullptr ) {
        return bounds;
    }

    const auto topLeft = ( widget == rootWidget ) ? QPoint{} : widget->mapTo( rootWidget, QPoint{} );
    bounds.insert( QStringLiteral( "x" ), topLeft.x() );
    bounds.insert( QStringLiteral( "y" ), topLeft.y() );
    bounds.insert( QStringLiteral( "width" ), widget->width() );
    bounds.insert( QStringLiteral( "height" ), widget->height() );
    return bounds;
}

QVariantMap automationObjectTree( const QObject* object, const QWidget* rootWidget )
{
    QVariantMap node;
    if ( object == nullptr ) {
        return node;
    }

    node.insert( QStringLiteral( "className" ), object->metaObject()->className() );
    node.insert( QStringLiteral( "objectName" ), object->objectName() );
    node.insert( QStringLiteral( "role" ), automationRole( object ) );

    const auto text = automationDisplayText( object );
    if ( !text.isEmpty() ) {
        node.insert( QStringLiteral( "text" ), text );
    }

    if ( const auto* action = qobject_cast<const QAction*>( object ) ) {
        node.insert( QStringLiteral( "enabled" ), action->isEnabled() );
        if ( action->isCheckable() ) {
            node.insert( QStringLiteral( "checked" ), action->isChecked() );
        }
    }

    if ( const auto* widget = qobject_cast<const QWidget*>( object ) ) {
        node.insert( QStringLiteral( "enabled" ), widget->isEnabled() );
        node.insert( QStringLiteral( "visible" ), widget->isVisible() );
        node.insert( QStringLiteral( "bounds" ), automationBounds( widget, rootWidget ) );
        if ( !widget->accessibleName().isEmpty() ) {
            node.insert( QStringLiteral( "accessibleName" ), widget->accessibleName() );
        }
        if ( const auto* button = qobject_cast<const QAbstractButton*>( widget );
             button != nullptr && button->isCheckable() ) {
            node.insert( QStringLiteral( "checked" ), button->isChecked() );
        }
    }

    QVariantList children;
    const auto objectChildren = object->children();
    children.reserve( objectChildren.size() );
    for ( const auto* child : objectChildren ) {
        children.push_back( automationObjectTree( child, rootWidget ) );
    }
    node.insert( QStringLiteral( "children" ), children );

    return node;
}

QVariantMap automationActionPayload( const QAction* action )
{
    QVariantMap payload;
    if ( action == nullptr ) {
        return payload;
    }

    auto text = action->text();
    text.remove( '&' );

    payload.insert( QStringLiteral( "className" ), action->metaObject()->className() );
    payload.insert( QStringLiteral( "objectName" ), action->objectName() );
    payload.insert( QStringLiteral( "role" ), QStringLiteral( "action" ) );
    payload.insert( QStringLiteral( "text" ), text );
    payload.insert( QStringLiteral( "enabled" ), action->isEnabled() );
    if ( action->isCheckable() ) {
        payload.insert( QStringLiteral( "checked" ), action->isChecked() );
    }

    return payload;
}

} // namespace

QTranslator MainWindow::mTranslator;
QTranslator MainWindow::mQtTranslator;

MainWindow::MainWindow( WindowSession session )
    : session_( std::move( session ) )
    , mainIcon_()
    , iconLoader_( this )
    , signalMux_()
    , quickFindMux_( session_.getQuickFindPattern() )
    , mainTabWidget_()
    , tempDir_( QDir::temp().filePath( "cilogg_temp_" ) )
{
    setObjectName( QStringLiteral( "mainWindow" ) );
    createActions();
    createMenus();
    createToolBars();

    setAcceptDrops( true );

    // Default geometry
    const QRect geometry = QApplication::primaryScreen()->availableGeometry();
    setGeometry( geometry.x() + 20, geometry.y() + 40, geometry.width() - 140,
                 geometry.height() - 140 );

    mainIcon_.addFile( ":/images/hicolor/16x16/cilogg.png" );
    // mainIcon_.addFile( ":/images/hicolor/24x24/cilogg.png" );
    mainIcon_.addFile( ":/images/hicolor/32x32/cilogg.png" );
    mainIcon_.addFile( ":/images/hicolor/48x48/cilogg.png" );
    mainIcon_.addFile( ":/images/hicolor/64x64/cilogg.png" );
    mainIcon_.addFile( ":/images/hicolor/128x128/cilogg.png" );
    mainIcon_.addFile( ":/images/hicolor/256x256/cilogg.png" );
    mainIcon_.addFile( ":/images/hicolor/512x512/cilogg.png" );

    setWindowIcon( mainIcon_ );
    StartupProgress::advance( tr( "Initializing main window" ),
                              tr( "Loading settings and UI state" ) );
    readSettings();

    createTrayIcon();

    // Connect the signals to the mux (they will be forwarded to the
    // "current" crawlerwidget

    // Send actions to the crawlerwidget
    signalMux_.connect( this, SIGNAL( followSet( bool ) ), SIGNAL( followSet( bool ) ) );
    signalMux_.connect( this, SIGNAL( textWrapSet( bool ) ), SIGNAL( textWrapSet( bool ) ) );
    signalMux_.connect( this, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
    signalMux_.connect( this, SIGNAL( enteringQuickFind() ), SLOT( enteringQuickFind() ) );
    signalMux_.connect( &quickFindWidget_, SIGNAL( close() ), SLOT( exitingQuickFind() ) );

    // Actions from the CrawlerWidget
    signalMux_.connect( SIGNAL( followModeChanged( bool ) ), this,
                        SLOT( changeFollowMode( bool ) ) );
    signalMux_.connect(
        SIGNAL( newSelection( LineNumber, LinesCount, LineColumn, LineLength ) ), this,
        SLOT( lineNumberHandler( LineNumber, LinesCount, LineColumn, LineLength ) ) );
    signalMux_.connect( SIGNAL( saveCurrentSearchAsPredefinedFilter( QString ) ), this,
                        SLOT( newPredefinedFilterHandler( QString ) ) );

    signalMux_.connect( SIGNAL( sendToScratchpad( QString ) ), this,
                        SLOT( sendToScratchpad( QString ) ) );

    signalMux_.connect( SIGNAL( replaceDataInScratchpad( QString ) ), this,
                        SLOT( replaceDataInScratchpad( QString ) ) );

    signalMux_.connect( SIGNAL( sendToPreview( QString, QString ) ), this,
                        SLOT( sendToPreview( QString, QString ) ) );

    // Register for progress status bar
    signalMux_.connect( SIGNAL( loadingProgressed( int ) ), this,
                        SLOT( updateLoadingProgress( int ) ) );
    signalMux_.connect( SIGNAL( loadingFinished( LoadingStatus ) ), this,
                        SLOT( handleLoadingFinished( LoadingStatus ) ) );

    signalMux_.connect( SIGNAL( filteredViewChanged() ), this,
                        SLOT( handleFilteredViewChanged() ) );

    // Configure the main tabbed widget
    mainTabWidget_.setDocumentMode( true );
    mainTabWidget_.setMovable( true );
    // mainTabWidget_.setTabShape( QTabWidget::Triangular );
    mainTabWidget_.setTabsClosable( true );

    scratchPad_.setWindowIcon( mainIcon_ );
    scratchPad_.setWindowTitle( tr( "CILogg - scratchpad" ) );

    previewWindow_.setWindowIcon( mainIcon_ );
    previewWindow_.setWindowTitle( tr( "CILogg - previewer" ) );
    if ( const auto scratchSize = scratchPad_.sizeHint(); scratchSize.isValid() ) {
        previewWindow_.resize( scratchSize.width() * 2, scratchSize.height() * 3 / 2 );
    }

    actionsResponsesWindow_.setWindowIcon( mainIcon_ );
    actionsResponsesWindow_.setWindowTitle( tr( "CILogg - actions/responses" ) );
    connect( &actionsResponsesWindow_, &ActionsResponsesWindow::sendActionRequested, this,
             &MainWindow::sendActionById );

    StartupProgress::advance( tr( "Loading previews" ), tr( "Loading preview definitions" ) );
    PreviewManager::instance().loadFromRepository();
    StartupProgress::advance( tr( "Loading actions" ), tr( "Loading action/response definitions" ) );
    ActionsManager::instance().loadFromRepository();

    connect( &mainTabWidget_, &TabbedCrawlerWidget::tabCloseRequested, this,
             [ this ]( int index ) { this->closeTab( index, ActionInitiator::User ); } );
    connect( &mainTabWidget_, &TabbedCrawlerWidget::startNewStreamFileRequested, this,
             &MainWindow::startNewStreamFileForTab );
    connect( &mainTabWidget_, &TabbedCrawlerWidget::currentChanged, this,
             &MainWindow::currentTabChanged );

    // Establish the QuickFindWidget and mux ( to send requests from the
    // QFWidget to the right window )
    connect( &quickFindWidget_, SIGNAL( patternConfirmed( const QString&, bool, bool ) ),
             &quickFindMux_, SLOT( confirmPattern( const QString&, bool, bool ) ) );
    connect( &quickFindWidget_, SIGNAL( patternUpdated( const QString&, bool, bool ) ),
             &quickFindMux_, SLOT( setNewPattern( const QString&, bool, bool ) ) );
    connect( &quickFindWidget_, SIGNAL( cancelSearch() ), &quickFindMux_, SLOT( cancelSearch() ) );
    connect( &quickFindWidget_, SIGNAL( searchForward() ), &quickFindMux_,
             SLOT( searchForward() ) );
    connect( &quickFindWidget_, SIGNAL( searchBackward() ), &quickFindMux_,
             SLOT( searchBackward() ) );
    connect( &quickFindWidget_, SIGNAL( searchNext() ), &quickFindMux_, SLOT( searchNext() ) );

    // QuickFind changes coming from the views
    connect( &quickFindMux_, SIGNAL( patternChanged( const QString& ) ), this,
             SLOT( changeQFPattern( const QString& ) ) );
    connect( &quickFindMux_, SIGNAL( notify( const QFNotification& ) ), &quickFindWidget_,
             SLOT( notify( const QFNotification& ) ) );
    connect( &quickFindMux_, SIGNAL( clearNotification() ), &quickFindWidget_,
             SLOT( clearNotification() ) );

    // Construct the QuickFind bar
    quickFindWidget_.hide();

    QWidget* central_widget = new QWidget();
    auto* main_layout = new QVBoxLayout();
    main_layout->setContentsMargins( 0, 0, 0, 0 );
    main_layout->addWidget( &mainTabWidget_ );
    main_layout->addWidget( &quickFindWidget_ );
    central_widget->setLayout( main_layout );

    setCentralWidget( central_widget );

    updateTitleBar( "" );
    loadIcons();
    reTranslateUI();
    refreshScriptStatusIndicators();
}

void MainWindow::reloadGeometry()
{
    QByteArray geometry;

    session_.restoreGeometry( &geometry );
    restoreGeometry( geometry );
}

void MainWindow::reloadSession()
{
    StartupProgress::advance( tr( "Restoring session" ), tr( "Restoring opened tabs" ) );
    const auto& config = Configuration::get();
    const auto followFileOnLoad = config.followFileOnLoad() && config.anyFileWatchEnabled();

    int current_file_index = -1;
    const auto openedFiles
        = session_.restore( [] { return new CrawlerWidget(); }, &current_file_index );

    for ( const auto& open_file : openedFiles ) {
        QString file_name = open_file.fileName;
        StartupProgress::advance( tr( "Restoring tab" ), QFileInfo( file_name ).fileName() );
        auto* crawler_widget = static_cast<CrawlerWidget*>( open_file.view );

        if ( crawler_widget ) {
            mainTabWidget_.addCrawler( crawler_widget, file_name );
            publishScriptLifecycleEvent( file_name, QStringLiteral( "tab_open" ) );

            if ( !open_file.streamContext.trimmed().isEmpty() ) {
                auto streamSettings = deserializeSerialCaptureSettings( open_file.streamContext );
                if ( streamSettings ) {
                    streamSettings->filePath = file_name;
                    StartupProgress::advance( tr( "Restoring COM stream" ),
                                              streamSettings->portName );
                    if ( !startComCaptureSession( *streamSettings,
                                                  ComCaptureStartOptions::restore() ) ) {
                        LOG_WARNING << "Failed to restore COM stream for "
                                    << file_name.toStdString();
                    }
                }
                else {
                    LOG_WARNING << "Skipping invalid stream context while restoring "
                                << file_name.toStdString();
                }
            }

            if ( followFileOnLoad ) {
                signalCrawlerToFollowFile( crawler_widget );
            }

            if ( !open_file.scriptContext.trimmed().isEmpty() ) {
                restoreScriptContextForTab( mainTabWidget_.indexOf( crawler_widget ),
                                            open_file.scriptContext );
            }

            waitForCrawlerStartupPreparation( crawler_widget, file_name );
        }
    }

    restoreGlobalScriptContext();

    if ( current_file_index >= 0 ) {
        mainTabWidget_.setCurrentIndex( current_file_index );

        if ( followFileOnLoad ) {
            followAction->setChecked( true );
        }
    }

    updateOpenedFilesMenu();
    StartupProgress::advance( tr( "Session restored" ), tr( "Finishing startup" ) );
}

bool MainWindow::isStartupReadyForDisplay() const
{
    if ( !loadingFileName.isEmpty() ) {
        return false;
    }

    for ( auto i = 0; i < mainTabWidget_.count(); ++i ) {
        auto* crawler = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( i ) );
        if ( crawler != nullptr && crawler->isStartupPreparationPending() ) {
            return false;
        }
    }

    return true;
}

void MainWindow::loadInitialFile( QString fileName, bool followFile )
{
    LOG_DEBUG << "loadInitialFile";

    // Is there a file passed as argument?
    if ( !fileName.isEmpty() ) {
        loadFile( fileName, followFile );
    }
}

CommanderResult MainWindow::executeCommanderRequest( const CommanderRequest& request )
{
    switch ( request.action ) {
    case CommanderAction::OpenFile:
        return loadFile( request.filePath, request.followFile )
                   ? commanderSuccess()
                   : commanderFailure( CommanderResultCode::ExecutionFailed,
                                       tr( "Failed to open file %1." ).arg( request.filePath ) );
    case CommanderAction::OpenUrl: {
        const auto url = QUrl::fromUserInput( request.url );
        if ( !url.isValid() || url.isEmpty() ) {
            return commanderFailure( CommanderResultCode::InvalidRequest,
                                     tr( "Invalid URL %1." ).arg( request.url ) );
        }
        return openRemoteFile( url, false, request.url );
    }
    case CommanderAction::OpenCom: {
        auto settings = resolveCommanderComSettings( request.comSettings );
        QString errorMessage;
        if ( !startComCaptureSession( settings, { false, false, true }, &errorMessage ) ) {
            return commanderFailure(
                CommanderResultCode::ExecutionFailed,
                errorMessage.isEmpty() ? tr( "Failed to open COM port %1." ).arg( settings.portName )
                                       : errorMessage );
        }

        if ( !loadFile( settings.filePath, true ) ) {
            if ( auto* session = mainTabWidget_.streamSessionForPath( settings.filePath ) ) {
                session->closeConnection();
            }
            return commanderFailure( CommanderResultCode::ExecutionFailed,
                                     tr( "Failed to open capture file %1." ).arg( settings.filePath ) );
        }

        return commanderSuccess();
    }
    case CommanderAction::CloseFile:
        return closeFileByPath( request.filePath );
    case CommanderAction::CloseUrl:
        return closeUrlBySource( request.url );
    case CommanderAction::CloseCom:
        return closeComPortByName( request.portName );
    case CommanderAction::CloseAll:
        return closeAllTabsCommander();
    case CommanderAction::GetInfo:
        return commanderSuccess( {}, commanderWindowInfo() );
    case CommanderAction::GetFilters:
        return commanderFilters( request );
    case CommanderAction::SendAction:
        return commanderSendAction( request );
    case CommanderAction::WaitResponse:
        return commanderWaitResponse( request );
    case CommanderAction::StartComm:
        return commanderStartComm( request );
    case CommanderAction::StopComm:
        return commanderStopComm( request );
    case CommanderAction::GetCommStatus:
        return commanderGetCommStatus( request );
    case CommanderAction::StartLogging:
        return commanderSetLogging( request, true );
    case CommanderAction::StopLogging:
        return commanderSetLogging( request, false );
    case CommanderAction::AddComment:
        return commanderAddComment( request );
    case CommanderAction::GetResponseCounter:
        return commanderGetResponseCounter( request );
    case CommanderAction::ResetResponseCounter:
        return commanderResetResponseCounter( request );
    case CommanderAction::ClearComm:
        return commanderClearComm( request );
    case CommanderAction::FocusTab:
        if ( !request.tabId.isEmpty() ) {
            return focusTabById( request.tabId );
        }
        if ( request.tabIndex ) {
            return focusTabByIndex( *request.tabIndex );
        }
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "focus_tab requires a tab selector." ) );
    case CommanderAction::SetFilter:
        return commanderSetFilter( request );
    case CommanderAction::CloseTab:
        if ( !request.tabId.isEmpty() ) {
            return closeTabById( request.tabId );
        }
        if ( request.tabIndex ) {
            return closeTabByIndex( *request.tabIndex );
        }
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "close_tab requires a tab selector." ) );
    case CommanderAction::Search:
        return commanderSearch( request );
    case CommanderAction::SetFollowMode:
        return commanderSetFollowMode( request );
    case CommanderAction::InvokeAction:
        return commanderInvokeAction( request );
    case CommanderAction::DumpState:
        return commanderSuccess( {}, automationSnapshot() );
    case CommanderAction::CloseKlogg:
    case CommanderAction::GetActions:
    case CommanderAction::GetResponses:
    case CommanderAction::CreateAction:
    case CommanderAction::UpdateAction:
    case CommanderAction::DeleteAction:
    case CommanderAction::CreateResponse:
    case CommanderAction::UpdateResponse:
    case CommanderAction::DeleteResponse:
    case CommanderAction::None:
    default:
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "Unsupported commander action." ) );
    }
}

QVariantMap MainWindow::commanderWindowInfo() const
{
    QVariantList tabs;

    for ( int index = 0; index < mainTabWidget_.count(); ++index ) {
        auto* widget = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( index ) );
        if ( widget == nullptr ) {
            continue;
        }

        const auto filePath = session_.getFilename( widget );
        if ( filePath.isEmpty() ) {
            continue;
        }

        QVariantMap tabInfo;
        const auto tabId = mainTabWidget_.tabIdAt( index );
        tabInfo.insert( QStringLiteral( "tabId" ), tabId );
        tabInfo.insert( QStringLiteral( "tabIndex" ), index );
        tabInfo.insert( QStringLiteral( "filePath" ), filePath );
        tabInfo.insert( QStringLiteral( "displayName" ), mainTabWidget_.tabDisplayNameAt( index ) );

        if ( auto* streamSession = mainTabWidget_.streamSessionForPath( filePath ) ) {
            tabInfo.insert( QStringLiteral( "sourceType" ), QStringLiteral( "com" ) );
            QVariantMap comInfo;
            const auto settings = streamSession->captureSettings();
            comInfo.insert( QStringLiteral( "portName" ), settings.portName );
            comInfo.insert( QStringLiteral( "baudRate" ), settings.baudRate );
            comInfo.insert( QStringLiteral( "connected" ), streamSession->isConnectionOpen() );
            comInfo.insert( QStringLiteral( "loggingEnabled" ), streamSession->isLoggingEnabled() );
            comInfo.insert( QStringLiteral( "isActionsPort" ),
                            isActionsStreamSession( streamSession ) );
            comInfo.insert( QStringLiteral( "responseCounters" ),
                            commanderResponseCounters( streamSession ) );
            tabInfo.insert( QStringLiteral( "com" ), comInfo );
        }
        else {
            const auto remoteSource = remoteFileSources_.find( filePath );
            if ( remoteSource != remoteFileSources_.cend() ) {
                tabInfo.insert( QStringLiteral( "sourceType" ), QStringLiteral( "url" ) );
                tabInfo.insert( QStringLiteral( "sourceUrl" ), remoteSource->second );
            }
            else {
                tabInfo.insert( QStringLiteral( "sourceType" ), QStringLiteral( "file" ) );
            }
        }

        QVariantMap scriptStatus;
        if ( applicationHasMethod( "scriptStatusForTab(QString)" )
             && QMetaObject::invokeMethod( qApp, "scriptStatusForTab", Qt::DirectConnection,
                                        Q_RETURN_ARG( QVariantMap, scriptStatus ),
                                        Q_ARG( QString, tabId ) )
             && !scriptStatus.isEmpty() ) {
            tabInfo.insert( QStringLiteral( "script" ), scriptStatus );
        }

        tabs.push_back( tabInfo );
    }

    QVariantMap windowInfo;
    const auto currentTabIndex = mainTabWidget_.currentIndex();
    windowInfo.insert( QStringLiteral( "windowId" ), session_.windowId() );
    windowInfo.insert( QStringLiteral( "windowIndex" ),
                       static_cast<int>( session_.windowIndex() ) );
    windowInfo.insert( QStringLiteral( "currentTabIndex" ), currentTabIndex );
    windowInfo.insert( QStringLiteral( "currentTabId" ),
                       currentTabIndex >= 0 ? mainTabWidget_.tabIdAt( currentTabIndex )
                                            : QString{} );
    windowInfo.insert( QStringLiteral( "tabs" ), tabs );
    return windowInfo;
}

QVariantMap MainWindow::automationSnapshot() const
{
    auto payload = automationTree();
    payload.insert( QStringLiteral( "schemaVersion" ), 1 );
    payload.insert( QStringLiteral( "windowTitle" ), windowTitle() );
    payload.insert( QStringLiteral( "windowInfo" ), commanderWindowInfo() );
    payload.insert( QStringLiteral( "actions" ), automationActions() );
    payload.insert( QStringLiteral( "ciloggState" ), automationState() );
    return payload;
}

QVariantMap MainWindow::automationTree() const
{
    return automationObjectTree( this, this );
}

QVariantList MainWindow::automationActions() const
{
    QVariantList actions;
    const auto knownActions = findChildren<QAction*>( QString{}, Qt::FindChildrenRecursively );
    for ( const auto* action : knownActions ) {
        if ( action == nullptr || action->objectName().isEmpty() ) {
            continue;
        }
        actions.push_back( automationActionPayload( action ) );
    }
    return actions;
}

QVariantMap MainWindow::automationState() const
{
    const auto windowInfo = commanderWindowInfo();
    const auto tabs = windowInfo.value( QStringLiteral( "tabs" ) ).toList();
    const auto activeTabIndex = mainTabWidget_.currentIndex();
    const auto* crawler = currentCrawlerWidget();

    QVariantMap scratchPad;
    scratchPad.insert( QStringLiteral( "visible" ), scratchPad_.isVisible() );
    scratchPad.insert( QStringLiteral( "hasContent" ), scratchPad_.hasContent() );

    QVariantMap state;
    state.insert( QStringLiteral( "startupReady" ), isStartupReadyForDisplay() );
    state.insert( QStringLiteral( "windowId" ), windowInfo.value( QStringLiteral( "windowId" ) ) );
    state.insert( QStringLiteral( "windowIndex" ), windowInfo.value( QStringLiteral( "windowIndex" ) ) );
    state.insert( QStringLiteral( "activeTabIndex" ), activeTabIndex );
    state.insert( QStringLiteral( "activeTabTitle" ),
                  activeTabIndex >= 0 ? mainTabWidget_.tabDisplayNameAt( activeTabIndex ) : QString{} );
    state.insert( QStringLiteral( "activeFile" ),
                  crawler != nullptr ? session_.getFilename( crawler ) : QString{} );
    state.insert( QStringLiteral( "sourceType" ), QStringLiteral( "file" ) );
    state.insert( QStringLiteral( "cursorLine" ),
                  crawler != nullptr ? QVariant::fromValue(
                                           static_cast<qulonglong>( crawler->currentLineNumber().get() + 1 ) )
                                     : QVariant{} );
    state.insert( QStringLiteral( "cursorColumn" ),
                  crawler != nullptr ? QVariant::fromValue(
                                           static_cast<qulonglong>( crawler->currentColumnNumber().get() + 1 ) )
                                     : QVariant{} );
    state.insert( QStringLiteral( "visibleLineStart" ), QVariant{} );
    state.insert( QStringLiteral( "visibleLineEnd" ), QVariant{} );
    state.insert( QStringLiteral( "mainVisibleLineStart" ), QVariant{} );
    state.insert( QStringLiteral( "mainVisibleLineEnd" ), QVariant{} );
    state.insert( QStringLiteral( "filteredVisibleLineStart" ), QVariant{} );
    state.insert( QStringLiteral( "filteredVisibleLineEnd" ), QVariant{} );
    state.insert( QStringLiteral( "textWrapEnabled" ),
                  crawler != nullptr ? crawler->isTextWrapEnabled() : false );
    state.insert( QStringLiteral( "focusedViewObjectName" ),
                  crawler != nullptr ? crawler->focusedViewObjectName() : QString{} );
    state.insert( QStringLiteral( "searchText" ), crawler != nullptr ? crawler->searchText() : QString{} );
    state.insert( QStringLiteral( "matchCount" ), crawler != nullptr ? crawler->matchCount() : 0 );
    state.insert( QStringLiteral( "searchInProgress" ),
                  crawler != nullptr ? crawler->isSearchInProgress() : false );
    state.insert( QStringLiteral( "followMode" ),
                  crawler != nullptr ? crawler->isFollowEnabled() : false );
    state.insert( QStringLiteral( "loadingInProgress" ),
                  !loadingFileName.isEmpty()
                      || ( crawler != nullptr && crawler->isLoadingInProgress() ) );
    state.insert( QStringLiteral( "encoding" ),
                  crawler != nullptr ? crawler->encodingText() : QString{} );
    state.insert( QStringLiteral( "parserMode" ), QString{} );
    state.insert( QStringLiteral( "scratchPad" ), scratchPad );
    state.insert( QStringLiteral( "previewerVisible" ), previewWindow_.isVisible() );
    state.insert( QStringLiteral( "actionsResponsesVisible" ), actionsResponsesWindow_.isVisible() );
    state.insert( QStringLiteral( "statusBarText" ), infoLine != nullptr ? infoLine->text() : QString{} );
    state.insert( QStringLiteral( "lastErrorText" ),
                  crawler != nullptr ? crawler->lastErrorText() : QString{} );

    if ( crawler != nullptr ) {
        const auto visibleRange = crawler->visibleLineRange();
        const auto mainVisibleRange = crawler->mainVisibleLineRange();
        const auto filteredVisibleRange = crawler->filteredVisibleLineRange();
        state.insert( QStringLiteral( "visibleLineStart" ), visibleRange.value( QStringLiteral( "start" ) ) );
        state.insert( QStringLiteral( "visibleLineEnd" ), visibleRange.value( QStringLiteral( "end" ) ) );
        state.insert( QStringLiteral( "mainVisibleLineStart" ),
                      mainVisibleRange.value( QStringLiteral( "start" ) ) );
        state.insert( QStringLiteral( "mainVisibleLineEnd" ),
                      mainVisibleRange.value( QStringLiteral( "end" ) ) );
        state.insert( QStringLiteral( "filteredVisibleLineStart" ),
                      filteredVisibleRange.value( QStringLiteral( "start" ) ) );
        state.insert( QStringLiteral( "filteredVisibleLineEnd" ),
                      filteredVisibleRange.value( QStringLiteral( "end" ) ) );
    }

    if ( activeTabIndex >= 0 ) {
        const auto match = std::find_if( tabs.cbegin(), tabs.cend(), [ activeTabIndex ]( const auto& tabValue ) {
            return tabValue.toMap().value( QStringLiteral( "tabIndex" ) ).toInt() == activeTabIndex;
        } );
        if ( match != tabs.cend() ) {
            const auto activeTab = match->toMap();
            state.insert( QStringLiteral( "sourceType" ),
                          activeTab.value( QStringLiteral( "sourceType" ) ).toString() );
            if ( state.value( QStringLiteral( "activeFile" ) ).toString().isEmpty() ) {
                state.insert( QStringLiteral( "activeFile" ),
                              activeTab.value( QStringLiteral( "filePath" ) ).toString() );
            }
        }
    }

    return state;
}

QVariantMap MainWindow::automationUiTree() const
{
    return automationSnapshot();
}

QVariantMap MainWindow::scriptEventContextForFile( const QString& filePath ) const
{
    if ( filePath.isEmpty() ) {
        return {};
    }

    for ( int index = 0; index < mainTabWidget_.count(); ++index ) {
        auto* widget = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( index ) );
        if ( widget == nullptr || session_.getFilename( widget ) != filePath ) {
            continue;
        }

        QVariantMap context;
        context.insert( QStringLiteral( "tabId" ), mainTabWidget_.tabIdAt( index ) );
        context.insert( QStringLiteral( "tabIndex" ), index );
        context.insert( QStringLiteral( "windowId" ), session_.windowId() );
        context.insert( QStringLiteral( "windowIndex" ), static_cast<int>( session_.windowIndex() ) );
        context.insert( QStringLiteral( "filePath" ), filePath );
        context.insert( QStringLiteral( "displayName" ), mainTabWidget_.tabDisplayNameAt( index ) );
        if ( auto* streamSession = mainTabWidget_.streamSessionForPath( filePath ) ) {
            context.insert( QStringLiteral( "portName" ),
                            streamSession->captureSettings().portName );
        }
        return context;
    }

    return {};
}

void MainWindow::publishScriptEvent( const QVariantMap& event ) const
{
    if ( event.isEmpty() ) {
        return;
    }

    if ( !applicationHasMethod( "publishScriptEvent(QVariantMap)" ) ) {
        return;
    }

    const auto invoked = QMetaObject::invokeMethod( qApp, "publishScriptEvent",
                                                    Qt::DirectConnection,
                                                    Q_ARG( QVariantMap, event ) );
    if ( !invoked ) {
        LOG_WARNING << "Failed to publish script event";
    }
}

void MainWindow::publishScriptLifecycleEvent( const QString& filePath,
                                              const QString& eventType ) const
{
    auto event = scriptEventContextForFile( filePath );
    if ( event.isEmpty() ) {
        return;
    }

    event.insert( QStringLiteral( "eventType" ), eventType );
    event.insert( QStringLiteral( "timestamp" ),
                  QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ) );
    publishScriptEvent( event );
}

void MainWindow::publishScriptReceiveEvent( const QString& filePath,
                                            const QByteArray& payloadBytes ) const
{
    auto event = scriptEventContextForFile( filePath );
    if ( event.isEmpty() ) {
        return;
    }

    event.insert( QStringLiteral( "eventType" ), QStringLiteral( "receive" ) );
    event.insert( QStringLiteral( "timestamp" ),
                  QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ) );
    event.insert( QStringLiteral( "text" ), QString::fromLatin1( payloadBytes ) );
    event.insert( QStringLiteral( "hexString" ), QString::fromLatin1( payloadBytes.toHex() ) );
    event.insert( QStringLiteral( "rawBase64" ), QString::fromLatin1( payloadBytes.toBase64() ) );
    publishScriptEvent( event );
}

void MainWindow::publishScriptResponseEvent( const QString& filePath,
                                             int responseId,
                                             const QString& responseName,
                                             int counter,
                                             const QByteArray& lineBytes,
                                             const QString& matchedText ) const
{
    auto event = scriptEventContextForFile( filePath );
    if ( event.isEmpty() ) {
        return;
    }

    event.insert( QStringLiteral( "eventType" ), QStringLiteral( "response" ) );
    event.insert( QStringLiteral( "timestamp" ),
                  QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ) );
    event.insert( QStringLiteral( "responseId" ), responseId );
    event.insert( QStringLiteral( "responseName" ), responseName );
    event.insert( QStringLiteral( "counter" ), counter );
    event.insert( QStringLiteral( "matchedText" ), matchedText );
    event.insert( QStringLiteral( "hexString" ), QString::fromLatin1( lineBytes.toHex() ) );
    event.insert( QStringLiteral( "rawBase64" ), QString::fromLatin1( lineBytes.toBase64() ) );
    publishScriptEvent( event );
}

void MainWindow::publishScriptTxEvent( const QString& filePath, const QByteArray& payloadBytes ) const
{
    auto event = scriptEventContextForFile( filePath );
    if ( event.isEmpty() ) {
        return;
    }

    event.insert( QStringLiteral( "eventType" ), QStringLiteral( "tx" ) );
    event.insert( QStringLiteral( "timestamp" ),
                  QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ) );
    event.insert( QStringLiteral( "text" ), QString::fromLatin1( payloadBytes ) );
    event.insert( QStringLiteral( "hexString" ), QString::fromLatin1( payloadBytes.toHex() ) );
    event.insert( QStringLiteral( "rawBase64" ), QString::fromLatin1( payloadBytes.toBase64() ) );
    publishScriptEvent( event );
}

void MainWindow::publishScriptActionSendEvent( const QString& filePath,
                                               int actionId,
                                               const QString& actionName,
                                               int stepIndex,
                                               const QByteArray& payloadBytes ) const
{
    auto event = scriptEventContextForFile( filePath );
    if ( event.isEmpty() ) {
        return;
    }

    event.insert( QStringLiteral( "eventType" ), QStringLiteral( "action_send" ) );
    event.insert( QStringLiteral( "timestamp" ),
                  QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ) );
    event.insert( QStringLiteral( "actionId" ), actionId );
    event.insert( QStringLiteral( "actionName" ), actionName );
    if ( stepIndex >= 0 ) {
        event.insert( QStringLiteral( "stepIndex" ), stepIndex );
    }
    event.insert( QStringLiteral( "text" ), QString::fromLatin1( payloadBytes ) );
    event.insert( QStringLiteral( "hexString" ), QString::fromLatin1( payloadBytes.toHex() ) );
    event.insert( QStringLiteral( "rawBase64" ), QString::fromLatin1( payloadBytes.toBase64() ) );
    publishScriptEvent( event );
}

void MainWindow::refreshScriptStatusIndicators()
{
    for ( int index = 0; index < mainTabWidget_.count(); ++index ) {
        const auto tabId = mainTabWidget_.tabIdAt( index );
        bool active = false;
        if ( applicationHasMethod( "hasActiveScriptForTab(QString)" ) ) {
            QMetaObject::invokeMethod( qApp, "hasActiveScriptForTab", Qt::DirectConnection,
                                       Q_RETURN_ARG( bool, active ),
                                       Q_ARG( QString, tabId ) );
        }
        mainTabWidget_.setTabScriptActive( tabId, active );
    }
}

QString MainWindow::scriptContextForTab( int index ) const
{
    if ( index < 0 || index >= mainTabWidget_.count() ) {
        return {};
    }

    QVariantMap scriptBinding;
    const auto tabId = mainTabWidget_.tabIdAt( index );
    if ( !applicationHasMethod( "scriptBindingForTab(QString)" )
         || !QMetaObject::invokeMethod( qApp, "scriptBindingForTab", Qt::DirectConnection,
                                      Q_RETURN_ARG( QVariantMap, scriptBinding ),
                                      Q_ARG( QString, tabId ) )
         || scriptBinding.isEmpty() ) {
        return {};
    }

    return QString::fromUtf8(
        QJsonDocument::fromVariant( scriptBinding ).toJson( QJsonDocument::Compact ) );
}

QString MainWindow::globalScriptContext() const
{
    QVariantMap scriptBinding;
    if ( !applicationHasMethod( "globalScriptBinding()" )
         || !QMetaObject::invokeMethod( qApp, "globalScriptBinding", Qt::DirectConnection,
                                        Q_RETURN_ARG( QVariantMap, scriptBinding ) )
         || scriptBinding.isEmpty() ) {
        return {};
    }

    return QString::fromUtf8(
        QJsonDocument::fromVariant( scriptBinding ).toJson( QJsonDocument::Compact ) );
}

void MainWindow::restoreScriptContextForTab( int index, const QString& scriptContext )
{
    if ( index < 0 || index >= mainTabWidget_.count() || scriptContext.trimmed().isEmpty() ) {
        return;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( scriptContext.toUtf8(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
        LOG_WARNING << "Invalid script context for restored tab";
        return;
    }

    auto scriptBinding = document.object().toVariantMap();
    scriptBinding.insert( QStringLiteral( "tabId" ), mainTabWidget_.tabIdAt( index ) );
    scriptBinding.insert( QStringLiteral( "tabIndex" ), index );
    scriptBinding.insert( QStringLiteral( "windowId" ), session_.windowId() );
    scriptBinding.insert( QStringLiteral( "windowIndex" ),
                          static_cast<int>( session_.windowIndex() ) );
    scriptBinding.insert( QStringLiteral( "displayName" ),
                          mainTabWidget_.tabDisplayNameAt( index ) );
    const auto filePath = mainTabWidget_.tabPathAt( index );
    scriptBinding.insert( QStringLiteral( "filePath" ), filePath );
    if ( auto* streamSession = mainTabWidget_.streamSessionForPath( filePath ) ) {
        scriptBinding.insert( QStringLiteral( "portName" ),
                              streamSession->captureSettings().portName );
    }

    if ( applicationHasMethod( "restoreScriptBindingVariant(QVariant)" ) ) {
        QMetaObject::invokeMethod( qApp, "restoreScriptBindingVariant", Qt::DirectConnection,
                                   Q_ARG( QVariant, QVariant::fromValue( scriptBinding ) ) );
    }
}

void MainWindow::restoreGlobalScriptContext()
{
    if ( applicationHasMethod( "restoreGlobalScriptBindingFromSession()" ) ) {
        QMetaObject::invokeMethod( qApp, "restoreGlobalScriptBindingFromSession",
                                   Qt::DirectConnection );
    }
}

void MainWindow::reTranslateUI()
{
    using namespace klogg::mainwindow;
    // menu
    auto transMenu = []( const char* text ) -> auto {
        return QApplication::translate( "klogg::mainwindow::menu", text );
    };
    fileMenu->setTitle( transMenu( menu::fileTitle ) );
    editMenu->setTitle( transMenu( menu::editTitle ) );
    viewMenu->setTitle( transMenu( menu::viewTitle ) );
    openedFilesMenu->setTitle( transMenu( menu::openedFilesTitle ) );
    toolsMenu->setTitle( transMenu( menu::toolsTitle ) );
    highlightersMenu->setTitle( transMenu( menu::highlightersTitle ) );
    favoritesMenu->setTitle( transMenu( menu::favoritesTitle ) );
    helpMenu->setTitle( transMenu( menu::helpTitle ) );

    // toolbar
    toolBar->setToolTip(
        QApplication::translate( "klogg::mainwindow::toolbar", toolbar::toolbarTitle ) );

    // action
    auto transAction = []( const char* text ) -> auto {
        return QApplication::translate( "klogg::mainwindow::action", text );
    };
    newWindowAction->setText( transAction( action::newWindowText ) );
    newWindowAction->setStatusTip( transAction( action::newWindowStatusTip ) );

    openAction->setText( transAction( action::openText ) );
    openAction->setStatusTip( transAction( action::openStatusTip ) );

    openComPortAction->setText( transAction( action::openComPortText ) );
    openComPortAction->setStatusTip( transAction( action::openComPortStatusTip ) );

    recentFilesCleanup->setText( transAction( action::recentFilesCleanupText ) );

    closeAction->setText( transAction( action::closeText ) );
    closeAction->setStatusTip( transAction( action::closeStatusTip ) );

    closeAllAction->setText( transAction( action::closeAllText ) );
    closeAllAction->setStatusTip( transAction( action::closeAllStatusTip ) );

    exitAction->setText( transAction( action::exitText ) );
    exitAction->setStatusTip( transAction( action::exitStatusTip ) );

    copyAction->setText( transAction( action::copyText ) );
    copyAction->setStatusTip( transAction( action::copyStatusTip ) );

    selectAllAction->setText( transAction( action::selectAllText ) );
    selectAllAction->setStatusTip( transAction( action::selectAllStatusTip ) );

    goToLineAction->setText( transAction( action::goToLineText ) );
    goToLineAction->setStatusTip( transAction( action::goToLineStatusTip ) );

    findAction->setText( transAction( action::findText ) );
    findAction->setStatusTip( transAction( action::findStatusTip ) );

    clearLogAction->setText( transAction( action::clearLogText ) );
    clearLogAction->setStatusTip( transAction( action::clearLogStatusTip ) );

    openContainingFolderAction->setText( transAction( action::openContainingFolderText ) );
    openContainingFolderAction->setStatusTip(
        transAction( action::openContainingFolderStatusTip ) );

    openInEditorAction->setText( transAction( action::openInEditorText ) );
    openInEditorAction->setStatusTip( transAction( action::openInEditorStatusTip ) );

    copyPathToClipboardAction->setText( transAction( action::copyPathToClipboardText ) );
    copyPathToClipboardAction->setStatusTip( transAction( action::copyPathToClipboardStatusTip ) );

    openClipboardAction->setText( transAction( action::openClipboardText ) );
    openClipboardAction->setStatusTip( transAction( action::openClipboardStatusTip ) );

    openUrlAction->setText( transAction( action::openUrlText ) );
    openUrlAction->setStatusTip( transAction( action::openUrlStatusTip ) );

    overviewVisibleAction->setText( transAction( action::overviewVisibleText ) );

    lineNumbersVisibleInMainAction->setText( transAction( action::lineNumbersVisibleInMainText ) );
    lineNumbersVisibleInFilteredAction->setText(
        transAction( action::lineNumbersVisibleInFilteredText ) );

    followAction->setText( transAction( action::followText ) );
    textWrapAction->setText( transAction( action::wrapText ) );
    showTabsBarAction->setText( transAction( action::showTabsBarText ) );
    reloadAction->setText( transAction( action::reloadText ) );
    stopAction->setText( transAction( action::stopText ) );

    optionsAction->setText( transAction( action::optionsText ) );
    optionsAction->setStatusTip( transAction( action::optionsStatusTip ) );

    editHighlightersAction->setText( transAction( action::editHighlightersText ) );
    editHighlightersAction->setStatusTip( transAction( action::editHighlightersStatusTip ) );

    showDocumentationAction->setText( transAction( action::showDocumentationText ) );
    showDocumentationAction->setStatusTip( transAction( action::showDocumentationStatusTip ) );

    aboutAction->setText( transAction( action::aboutText ) );
    aboutAction->setStatusTip( transAction( action::aboutStatusTip ) );

    aboutQtAction->setText( transAction( action::aboutQtText ) );
    aboutQtAction->setStatusTip( transAction( action::aboutQtStatusTip ) );

    reportIssueAction->setText( transAction( action::reportIssueText ) );
    reportIssueAction->setStatusTip( transAction( action::reportIssueStatusTip ) );

    joinDiscordAction->setText( transAction( action::joinDiscordText ) );
    joinDiscordAction->setStatusTip( transAction( action::joinDiscordStatusTip ) );

    joinTelegramAction->setText( transAction( action::joinTelegramText ) );
    joinTelegramAction->setStatusTip( transAction( action::joinTelegramStatusTip ) );

    generateDumpAction->setText( transAction( action::generateDumpText ) );
    generateDumpAction->setStatusTip( transAction( action::generateDumpStatusTip ) );

    showScratchPadAction->setText( transAction( action::showScratchPadText ) );
    showScratchPadAction->setStatusTip( transAction( action::showScratchPadStatusTip ) );
    showPreviewerAction->setText( transAction( action::showPreviewerText ) );
    showPreviewerAction->setStatusTip( transAction( action::showPreviewerStatusTip ) );
    showActionsResponsesAction->setText( transAction( action::showActionsResponsesText ) );
    showActionsResponsesAction->setStatusTip(
        transAction( action::showActionsResponsesStatusTip ) );
    showScriptRunnerAction->setText( transAction( action::showScriptRunnerText ) );
    showScriptRunnerAction->setStatusTip( transAction( action::showScriptRunnerStatusTip ) );
    showScenarioRunnerAction->setText( transAction( action::showScenarioRunnerText ) );
    showScenarioRunnerAction->setStatusTip( transAction( action::showScenarioRunnerStatusTip ) );
    showLabQueueAction->setText( transAction( action::showLabQueueText ) );
    showLabQueueAction->setStatusTip( transAction( action::showLabQueueStatusTip ) );

    auto curFavoritesIconText = addToFavoritesAction->data().toBool()
                                    ? transAction( action::addToFavoritesText )
                                    : transAction( action::removeFromFavoritesText );
    addToFavoritesAction->setText( curFavoritesIconText );
    addToFavoritesMenuAction->setText( transAction( action::addToFavoritesText ) );

    removeFromFavoritesAction->setText( transAction( action::removeFromFavoritesText ) );

    selectOpenFileAction->setText( transAction( action::selectOpenFileText ) );

    predefinedFiltersDialogAction->setText( transAction( action::predefinedFiltersDialogText ) );
    predefinedFiltersDialogAction->setStatusTip(
        transAction( action::predefinedFiltersDialogStatusTip ) );

    importPreviewsAction->setText( transAction( action::importPreviewsDialogText ) );
    importPreviewsAction->setStatusTip( transAction( action::importPreviewsDialogStatusTip ) );
    importActionsAction->setText( transAction( action::importActionsDialogText ) );
    importActionsAction->setStatusTip( transAction( action::importActionsDialogStatusTip ) );

    // trayIcon
    trayIcon_->setToolTip( QApplication::translate( "klogg::mainwindow::trayicon",
                                                    klogg::mainwindow::trayicon::trayiconTip ) );
}

int MainWindow::installLanguage( QString lang )
{
    if ( lang.isEmpty() ) {
        return -1;
    }

    QApplication::removeTranslator( &mTranslator );
    QApplication::removeTranslator( &mQtTranslator );

    QString qtPath( ":/i18n/qt_" + lang + ".qm" );
    QResource qtTranslations( qtPath );
    if ( !mQtTranslator.load( qtTranslations.data(), (int)qtTranslations.size() ) ) {
        LOG_ERROR << "load fail";
        return -1;
    }
    if ( !QApplication::installTranslator( &mQtTranslator ) ) {
        LOG_ERROR << "install fail";
        return -1;
    }

    QString appPath( ":/i18n/" + lang + ".qm" );
    QResource appTranslations( appPath );
    if ( !mTranslator.load( appTranslations.data(), (int)appTranslations.size() ) ) {
        LOG_ERROR << "load fail";
        return -1;
    }
    if ( !QApplication::installTranslator( &mTranslator ) ) {
        LOG_ERROR << "install fail";
        return -1;
    }

    return 0;
}

// Menu actions
void MainWindow::createActions()
{
    const auto& config = Configuration::get();
    const auto shortcuts = config.shortcuts();

    using namespace klogg::mainwindow;

    newWindowAction = new QAction( tr( action::newWindowText ), this );
    newWindowAction->setStatusTip( tr( action::newWindowStatusTip ) );
    connect( newWindowAction, &QAction::triggered, [ = ] { Q_EMIT newWindow(); } );
    newWindowAction->setVisible( config.allowMultipleWindows() );

    openAction = new QAction( tr( action::openText ), this );
    openAction->setStatusTip( tr( action::openStatusTip ) );
    connect( openAction, &QAction::triggered, [ this ]( auto ) { this->open(); } );

    openComPortAction = new QAction( tr( action::openComPortText ), this );
    openComPortAction->setStatusTip( tr( action::openComPortStatusTip ) );
    connect( openComPortAction, &QAction::triggered, this, [ this ]( auto ) { this->openComPort(); } );

    recentFilesCleanup = new QAction( tr( action::recentFilesCleanupText ), this );
    connect( recentFilesCleanup, &QAction::triggered, this,
             [ this ]( auto ) { this->clearRecentFileActions(); } );

    closeAction = new QAction( tr( action::closeText ), this );
    closeAction->setStatusTip( tr( action::closeStatusTip ) );
    connect( closeAction, &QAction::triggered, this,
             [ this ]( auto ) { this->closeTab( ActionInitiator::User ); } );

    closeAllAction = new QAction( tr( action::closeAllText ), this );
    closeAllAction->setStatusTip( tr( action::closeAllStatusTip ) );
    connect( closeAllAction, &QAction::triggered, this,
             [ this ]( auto ) { this->closeAll( ActionInitiator::User ); } );

    recentFilesGroup = new QActionGroup( this );
    connect( recentFilesGroup, &QActionGroup::triggered, this, &MainWindow::openFileFromRecent );
    for ( auto i = 0u; i < recentFileActions.size(); ++i ) {
        recentFileActions[ i ] = new QAction( this );
        connect( recentFileActions[ i ], &QAction::hovered, [ this, a = recentFileActions[ i ] ]() {
            QToolTip::showText( QCursor::pos(), a->toolTip(), this );
        } );
        recentFileActions[ i ]->setVisible( false );
        recentFileActions[ i ]->setActionGroup( recentFilesGroup );
    }

    exitAction = new QAction( tr( action::exitText ), this );
    exitAction->setStatusTip( tr( action::exitStatusTip ) );
    connect( exitAction, &QAction::triggered, this, &MainWindow::exitRequested );

    copyAction = new QAction( tr( action::copyText ), this );
    copyAction->setStatusTip( tr( action::copyStatusTip ) );
    connect( copyAction, &QAction::triggered, this, [ this ]( auto ) { this->copy(); } );

    selectAllAction = new QAction( tr( action::selectAllText ), this );
    selectAllAction->setStatusTip( tr( action::selectAllStatusTip ) );
    connect( selectAllAction, &QAction::triggered, this, [ this ]( auto ) { this->selectAll(); } );

    goToLineAction = new QAction( tr( action::goToLineText ), this );
    goToLineAction->setStatusTip( tr( action::goToLineStatusTip ) );
    signalMux_.connect( goToLineAction, SIGNAL( triggered() ), SLOT( goToLine() ) );

    findAction = new QAction( tr( action::findText ), this );
    findAction->setStatusTip( tr( action::findStatusTip ) );
    connect( findAction, &QAction::triggered, this, [ this ]( auto ) { this->find(); } );

    clearLogAction = new QAction( tr( action::clearLogText ), this );
    clearLogAction->setStatusTip( tr( action::clearLogStatusTip ) );
    connect( clearLogAction, &QAction::triggered, this, [ this ]( auto ) { this->clearLog(); } );

    openContainingFolderAction = new QAction( tr( action::openContainingFolderText ), this );
    openContainingFolderAction->setStatusTip( tr( action::openContainingFolderStatusTip ) );
    connect( openContainingFolderAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openContainingFolder(); } );

    openInEditorAction = new QAction( tr( action::openInEditorText ), this );
    openInEditorAction->setStatusTip( tr( action::openInEditorStatusTip ) );
    connect( openInEditorAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openInEditor(); } );

    copyPathToClipboardAction = new QAction( tr( action::copyPathToClipboardText ), this );
    copyPathToClipboardAction->setStatusTip( tr( action::copyPathToClipboardStatusTip ) );
    connect( copyPathToClipboardAction, &QAction::triggered, this,
             [ this ]( auto ) { this->copyFullPath(); } );

    openClipboardAction = new QAction( tr( action::openClipboardText ), this );
    openClipboardAction->setObjectName( QStringLiteral( "openClipboardAction" ) );
    openClipboardAction->setStatusTip( tr( action::openClipboardStatusTip ) );
    connect( openClipboardAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openClipboard(); } );

    openUrlAction = new QAction( tr( action::openUrlText ), this );
    openUrlAction->setObjectName( QStringLiteral( "openUrlAction" ) );
    openUrlAction->setStatusTip( tr( action::openUrlStatusTip ) );
    connect( openUrlAction, &QAction::triggered, this, [ this ]( auto ) { this->openUrl(); } );

    overviewVisibleAction = new QAction( tr( action::overviewVisibleText ), this );
    overviewVisibleAction->setCheckable( true );
    overviewVisibleAction->setChecked( config.isOverviewVisible() );
    connect( overviewVisibleAction, &QAction::toggled, this,
             &MainWindow::toggleOverviewVisibility );

    lineNumbersVisibleInMainAction
        = new QAction( tr( action::lineNumbersVisibleInMainText ), this );
    lineNumbersVisibleInMainAction->setCheckable( true );
    lineNumbersVisibleInMainAction->setChecked( config.mainLineNumbersVisible() );
    connect( lineNumbersVisibleInMainAction, &QAction::toggled, this,
             &MainWindow::toggleMainLineNumbersVisibility );

    lineNumbersVisibleInFilteredAction
        = new QAction( tr( action::lineNumbersVisibleInFilteredText ), this );
    lineNumbersVisibleInFilteredAction->setCheckable( true );
    lineNumbersVisibleInFilteredAction->setChecked( config.filteredLineNumbersVisible() );
    connect( lineNumbersVisibleInFilteredAction, &QAction::toggled, this,
             &MainWindow::toggleFilteredLineNumbersVisibility );

    followAction = new QAction( tr( action::followText ), this );
    followAction->setObjectName( QStringLiteral( "followAction" ) );
    followAction->setCheckable( true );
    followAction->setEnabled( config.anyFileWatchEnabled() );
    connect( followAction, &QAction::toggled, this, &MainWindow::followSet );

    textWrapAction = new QAction( tr( action::wrapText ), this );
    textWrapAction->setObjectName( QStringLiteral( "textWrapAction" ) );
    textWrapAction->setCheckable( true );
    textWrapAction->setEnabled( true );
    connect( textWrapAction, &QAction::toggled, this, &MainWindow::textWrapSet );

    showTabsBarAction = new QAction( tr( action::showTabsBarText ), this );
    showTabsBarAction->setCheckable( true );
    showTabsBarAction->setEnabled( true );
    showTabsBarAction->setChecked( config.showTabsBarByDefault() );
    connect( showTabsBarAction, &QAction::toggled, this,
             [ this ]( bool checked ) { mainTabWidget_.setAlwaysShowTabBar( checked ); } );
    mainTabWidget_.setAlwaysShowTabBar( showTabsBarAction->isChecked() );

    reloadAction = new QAction( tr( action::reloadText ), this );
    signalMux_.connect( reloadAction, SIGNAL( triggered() ), SLOT( reload() ) );

    stopAction = new QAction( tr( action::stopText ), this );
    stopAction->setEnabled( true );
    signalMux_.connect( stopAction, SIGNAL( triggered() ), SLOT( stopLoading() ) );

    optionsAction = new QAction( tr( action::optionsText ), this );
    optionsAction->setObjectName( QStringLiteral( "optionsAction" ) );
    optionsAction->setMenuRole( QAction::PreferencesRole );
    optionsAction->setStatusTip( tr( action::optionsStatusTip ) );
    connect( optionsAction, &QAction::triggered, this, [ this ]( auto ) { this->options(); } );

    editHighlightersAction = new QAction( tr( action::editHighlightersText ), this );
    editHighlightersAction->setMenuRole( QAction::NoRole );
    editHighlightersAction->setStatusTip( tr( action::editHighlightersStatusTip ) );
    connect( editHighlightersAction, &QAction::triggered, this,
             [ this ]( auto ) { this->editHighlighters(); } );

    showDocumentationAction = new QAction( tr( action::showDocumentationText ), this );
    showDocumentationAction->setObjectName( QStringLiteral( "showDocumentationAction" ) );
    showDocumentationAction->setStatusTip( tr( action::showDocumentationStatusTip ) );
    connect( showDocumentationAction, &QAction::triggered, this,
             [ this ]( auto ) { this->documentation(); } );

    aboutAction = new QAction( tr( action::aboutText ), this );
    aboutAction->setStatusTip( tr( action::aboutStatusTip ) );
    connect( aboutAction, &QAction::triggered, this, [ this ]( auto ) { this->about(); } );

    aboutQtAction = new QAction( tr( action::aboutQtText ), this );
    aboutQtAction->setStatusTip( tr( action::aboutQtStatusTip ) );
    connect( aboutQtAction, &QAction::triggered, this, [ this ]( auto ) { this->aboutQt(); } );

    reportIssueAction = new QAction( tr( action::reportIssueText ), this );
    reportIssueAction->setStatusTip( tr( action::reportIssueStatusTip ) );
    connect( reportIssueAction, &QAction::triggered, this,
             []( auto ) { IssueReporter::reportIssue( IssueTemplate::Bug ); } );

    joinDiscordAction = new QAction( tr( action::joinDiscordText ), this );
    joinDiscordAction->setStatusTip( tr( action::joinDiscordStatusTip ) );
    connect( joinDiscordAction, &QAction::triggered, this, []( auto ) {
        QUrl url( "https://discord.gg/DruNyQftzB" );
        QDesktopServices::openUrl( url );
    } );

    joinTelegramAction = new QAction( tr( action::joinTelegramText ), this );
    joinTelegramAction->setStatusTip( tr( action::joinTelegramStatusTip ) );
    connect( joinTelegramAction, &QAction::triggered, this, []( auto ) {
        QUrl url( "https://t.me/joinchat/JeIBxstIfp4xZTk6" );
        QDesktopServices::openUrl( url );
    } );

    generateDumpAction = new QAction( tr( action::generateDumpText ), this );
    generateDumpAction->setObjectName( QStringLiteral( "generateDumpAction" ) );
    generateDumpAction->setStatusTip( tr( action::generateDumpStatusTip ) );
    connect( generateDumpAction, &QAction::triggered, this,
             [ this ]( auto ) { this->generateDump(); } );

    showScratchPadAction = new QAction( tr( action::showScratchPadText ), this );
    showScratchPadAction->setObjectName( QStringLiteral( "showScratchPadAction" ) );
    showScratchPadAction->setStatusTip( tr( action::showScratchPadStatusTip ) );
    connect( showScratchPadAction, &QAction::triggered, this,
             [ this ]( auto ) { this->showScratchPad(); } );

    showPreviewerAction = new QAction( tr( action::showPreviewerText ), this );
    showPreviewerAction->setObjectName( QStringLiteral( "showPreviewerAction" ) );
    showPreviewerAction->setStatusTip( tr( action::showPreviewerStatusTip ) );
    connect( showPreviewerAction, &QAction::triggered, this,
             [ this ]( auto ) { this->showPreviewer(); } );

    showActionsResponsesAction = new QAction( tr( action::showActionsResponsesText ), this );
    showActionsResponsesAction->setObjectName( QStringLiteral( "showActionsResponsesAction" ) );
    showActionsResponsesAction->setStatusTip( tr( action::showActionsResponsesStatusTip ) );
    connect( showActionsResponsesAction, &QAction::triggered, this,
             [ this ]( auto ) { this->showActionsResponses(); } );

    showScriptRunnerAction = new QAction( tr( action::showScriptRunnerText ), this );
    showScriptRunnerAction->setStatusTip( tr( action::showScriptRunnerStatusTip ) );
    connect( showScriptRunnerAction, &QAction::triggered, this,
              [ this ]( auto ) { this->showScriptRunner(); } );

    showScenarioRunnerAction = new QAction( tr( action::showScenarioRunnerText ), this );
    showScenarioRunnerAction->setStatusTip( tr( action::showScenarioRunnerStatusTip ) );
    connect( showScenarioRunnerAction, &QAction::triggered, this,
              [ this ]( auto ) { this->showScenarioRunner(); } );

    showLabQueueAction = new QAction( tr( action::showLabQueueText ), this );
    showLabQueueAction->setStatusTip( tr( action::showLabQueueStatusTip ) );
    connect( showLabQueueAction, &QAction::triggered, this,
             [ this ]( auto ) { this->showLabQueue(); } );

    encodingGroup = new QActionGroup( this );
    connect( encodingGroup, &QActionGroup::triggered, this, &MainWindow::encodingChanged );

    favoritesGroup = new QActionGroup( this );
    connect( favoritesGroup, &QActionGroup::triggered, this, &MainWindow::openFileFromFavorites );

    openedFilesGroup = new QActionGroup( this );
    connect( openedFilesGroup, &QActionGroup::triggered, this, &MainWindow::switchToOpenedFile );

    addToFavoritesAction = new QAction( tr( action::addToFavoritesText ), this );
    addToFavoritesAction->setData( true );
    connect( addToFavoritesAction, &QAction::triggered, this,
             [ this ]( auto ) { this->addToFavorites(); } );

    addToFavoritesMenuAction = new QAction( tr( action::addToFavoritesText ), this );
    connect( addToFavoritesMenuAction, &QAction::triggered, this,
             [ this ]( auto ) { this->addToFavorites(); } );

    removeFromFavoritesAction = new QAction( tr( action::removeFromFavoritesText ), this );
    connect( removeFromFavoritesAction, &QAction::triggered, this,
             [ this ]( auto ) { this->removeFromFavorites(); } );

    selectOpenFileAction = new QAction( tr( action::selectOpenFileText ), this );
    connect( selectOpenFileAction, &QAction::triggered, this,
             [ this ]( auto ) { this->selectOpenedFile(); } );

    predefinedFiltersDialogAction = new QAction( tr( action::predefinedFiltersDialogText ), this );
    predefinedFiltersDialogAction->setStatusTip( tr( action::predefinedFiltersDialogStatusTip ) );
    connect( predefinedFiltersDialogAction, &QAction::triggered, this,
             [ this ]( auto ) { this->editPredefinedFilters(); } );

    importPreviewsAction = new QAction( tr( action::importPreviewsDialogText ), this );
    importPreviewsAction->setStatusTip( tr( action::importPreviewsDialogStatusTip ) );
    connect( importPreviewsAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openImportPreviewsDialog(); } );

    importActionsAction = new QAction( tr( action::importActionsDialogText ), this );
    importActionsAction->setStatusTip( tr( action::importActionsDialogStatusTip ) );
    connect( importActionsAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openImportActionsDialog(); } );

    updateShortcuts();
}

void MainWindow::updateShortcuts()
{
    const auto& config = Configuration::get();
    const auto shortcuts = config.shortcuts();

    for ( auto& shortcut : shortcuts_ ) {
        shortcut.second->deleteLater();
    }

    shortcuts_.clear();
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowOpenQfForward,
                                      [ this ] { displayQuickFindBar( QuickFindMux::Forward ); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowOpenQfBackward,
                                      [ this ] { displayQuickFindBar( QuickFindMux::Backward ); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowFocusSearchInput, [ this ] {
                                          if ( auto crawler = currentCrawlerWidget() ) {
                                              crawler->focusSearchEdit();
                                          }
                                      } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowFullScreen,
                                      [ this ] { this->showFullScreen(); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowMax,
                                      [ this ] { this->showMaximized(); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowMin,
                                      [ this ] { this->showMinimized(); } );

    auto setShortcuts = [ &shortcuts ]( auto* action, const auto& actionName ) {
        action->setShortcuts( ShortcutAction::shortcutKeys( actionName, shortcuts ) );
    };

    setShortcuts( newWindowAction, ShortcutAction::MainWindowNewWindow );
    setShortcuts( openAction, ShortcutAction::MainWindowOpenFile );
    setShortcuts( closeAction, ShortcutAction::MainWindowCloseFile );
    setShortcuts( closeAllAction, ShortcutAction::MainWindowCloseAll );
    setShortcuts( exitAction, ShortcutAction::MainWindowQuit );
    setShortcuts( copyAction, ShortcutAction::MainWindowCopy );
    setShortcuts( selectAllAction, ShortcutAction::MainWindowSelectAll );
    setShortcuts( findAction, ShortcutAction::MainWindowOpenQf );
    setShortcuts( clearLogAction, ShortcutAction::MainWindowClearFile );
    setShortcuts( openContainingFolderAction, ShortcutAction::MainWindowOpenContainingFolder );
    setShortcuts( openInEditorAction, ShortcutAction::MainWindowOpenInEditor );
    setShortcuts( copyPathToClipboardAction, ShortcutAction::MainWindowCopyPathToClipboard );
    setShortcuts( openClipboardAction, ShortcutAction::MainWindowOpenFromClipboard );
    setShortcuts( openUrlAction, ShortcutAction::MainWindowOpenFromUrl );
    setShortcuts( followAction, ShortcutAction::MainWindowFollowFile );
    setShortcuts( textWrapAction, ShortcutAction::MainWindowTextWrap );
    setShortcuts( reloadAction, ShortcutAction::MainWindowReload );
    setShortcuts( stopAction, ShortcutAction::MainWindowStop );
    setShortcuts( showScratchPadAction, ShortcutAction::MainWindowScratchpad );
    setShortcuts( selectOpenFileAction, ShortcutAction::MainWindowSelectOpenFile );
    setShortcuts( goToLineAction, ShortcutAction::LogViewJumpToLine );
    setShortcuts( optionsAction, ShortcutAction::MainWindowPreference );
}

void MainWindow::loadIcons()
{
    openAction->setIcon( iconLoader_.load( "icons8-open-file" ) );
    stopAction->setIcon( iconLoader_.load( "icons8-delete" ) );
    reloadAction->setIcon( iconLoader_.load( "icons8-restore-page" ) );
    followAction->setIcon( iconLoader_.load( "icons8-fast-forward" ) );
    showScratchPadAction->setIcon( iconLoader_.load( "icons8-create" ) );
    showPreviewerAction->setIcon( iconLoader_.load( "icons8-search" ) );
    showActionsResponsesAction->setIcon( iconLoader_.load( "icons8-venn-diagram" ) );
    showScriptRunnerAction->setIcon( makePythonScriptRunnerIcon() );
    addToFavoritesAction->setIcon( iconLoader_.load( "icons8-star" ) );
    addToFavoritesMenuAction->setIcon( iconLoader_.load( "icons8-star" ) );
}

void MainWindow::createMenus()
{
    using namespace klogg::mainwindow;

    fileMenu = menuBar()->addMenu( tr( menu::fileTitle ) );
    fileMenu->setObjectName( QStringLiteral( "fileMenu" ) );
    fileMenu->setToolTipsVisible( true );
    fileMenu->addAction( newWindowAction );
    fileMenu->addAction( openAction );
    fileMenu->addAction( openComPortAction );
    fileMenu->addAction( openClipboardAction );
    fileMenu->addAction( openUrlAction );
    recentFilesMenu = fileMenu->addMenu( tr( "Open Recent" ) );
    for ( auto i = 0u; i < recentFileActions.size(); ++i ) {
        recentFilesMenu->addAction( recentFileActions[ i ] );
    }
    recentFilesMenu->addSeparator();
    recentFilesMenu->addAction( recentFilesCleanup );
    recentFilesMenu->setEnabled( false );
    fileMenu->addSeparator();

    fileMenu->addAction( closeAction );
    fileMenu->addAction( closeAllAction );
    fileMenu->addSeparator();

    fileMenu->addAction( optionsAction );
    fileMenu->addSeparator();

    fileMenu->addSeparator();
    fileMenu->addAction( exitAction );

    editMenu = menuBar()->addMenu( tr( menu::editTitle ) );
    editMenu->setObjectName( QStringLiteral( "editMenu" ) );
    editMenu->addAction( copyAction );
    editMenu->addAction( selectAllAction );
    editMenu->addSeparator();
    editMenu->addAction( findAction );
    editMenu->addSeparator();
    editMenu->addAction( goToLineAction );
    editMenu->addSeparator();
    editMenu->addAction( copyPathToClipboardAction );
    editMenu->addAction( openContainingFolderAction );
    editMenu->addSeparator();
    editMenu->addAction( openInEditorAction );
    editMenu->addAction( clearLogAction );
    editMenu->setEnabled( false );

    viewMenu = menuBar()->addMenu( tr( menu::viewTitle ) );
    viewMenu->setObjectName( QStringLiteral( "viewMenu" ) );
    openedFilesMenu = viewMenu->addMenu( tr( menu::openedFilesTitle ) );
    openedFilesMenu->setObjectName( QStringLiteral( "openedFilesMenu" ) );
    viewMenu->addSeparator();
    viewMenu->addAction( overviewVisibleAction );
    viewMenu->addSeparator();
    viewMenu->addAction( lineNumbersVisibleInMainAction );
    viewMenu->addAction( lineNumbersVisibleInFilteredAction );
    viewMenu->addSeparator();
    viewMenu->addAction( textWrapAction );
    viewMenu->addAction( showTabsBarAction );
    viewMenu->addSeparator();
    viewMenu->addAction( followAction );
    viewMenu->addSeparator();
    viewMenu->addAction( reloadAction );

    toolsMenu = menuBar()->addMenu( tr( menu::toolsTitle ) );
    toolsMenu->setObjectName( QStringLiteral( "toolsMenu" ) );

    highlightersMenu = new HighlightersMenu( tr( menu::highlightersTitle ), menuBar() );
    highlightersMenu->setObjectName( QStringLiteral( "highlightersMenu" ) );
    menuBar()->addMenu( highlightersMenu );
    highlightersMenu->setApplyChange( [ this ]() {
        auto crawler = currentCrawlerWidget();
        if ( crawler != nullptr ) {
            crawler->applyConfiguration();
        }
    } );

    toolsMenu->addAction( predefinedFiltersDialogAction );
    toolsMenu->addAction( importPreviewsAction );
    toolsMenu->addAction( importActionsAction );
    toolsMenu->addSeparator();
    toolsMenu->addAction( showPreviewerAction );
    toolsMenu->addAction( showActionsResponsesAction );
    toolsMenu->addAction( showScriptRunnerAction );
    toolsMenu->addAction( showScenarioRunnerAction );
    toolsMenu->addAction( showLabQueueAction );
    toolsMenu->addAction( showScratchPadAction );

    menuBar()->addMenu( EncodingMenu::generate( encodingGroup ) );
    menuBar()->addSeparator();

    favoritesMenu = menuBar()->addMenu( tr( menu::favoritesTitle ) );
    favoritesMenu->setObjectName( QStringLiteral( "favoritesMenu" ) );
    favoritesMenu->setToolTipsVisible( true );

    helpMenu = menuBar()->addMenu( tr( menu::helpTitle ) );
    helpMenu->setObjectName( QStringLiteral( "helpMenu" ) );
    helpMenu->addAction( showDocumentationAction );
    helpMenu->addSeparator();
    helpMenu->addAction( reportIssueAction );
    helpMenu->addAction( joinDiscordAction );
    helpMenu->addAction( joinTelegramAction );
    helpMenu->addSeparator();
    helpMenu->addAction( generateDumpAction );
    helpMenu->addSeparator();
    helpMenu->addAction( aboutQtAction );
    helpMenu->addAction( aboutAction );
}

void MainWindow::createToolBars()
{
    infoLine = new PathLine();
    infoLine->setObjectName( QStringLiteral( "infoLine" ) );
    infoLine->setFrameStyle( QFrame::StyledPanel );
    infoLine->setFrameShadow( QFrame::Sunken );
    infoLine->setLineWidth( 0 );
    infoLine->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );

    sizeField = new QLabel();
    sizeField->setObjectName( QStringLiteral( "sizeField" ) );
    sizeField->setAlignment( Qt::AlignHCenter | Qt::AlignVCenter );

    dateField = new QLabel();
    dateField->setObjectName( QStringLiteral( "dateField" ) );
    dateField->setAlignment( Qt::AlignHCenter | Qt::AlignVCenter );

    encodingField = new QLabel();
    encodingField->setObjectName( QStringLiteral( "encodingField" ) );
    encodingField->setAlignment( Qt::AlignHCenter | Qt::AlignVCenter );

    comPortField = new QLabel();
    comPortField->setObjectName( QStringLiteral( "comPortField" ) );
    comPortField->setAlignment( Qt::AlignHCenter | Qt::AlignVCenter );

    lineNbField = new QLabel();
    lineNbField->setObjectName( QStringLiteral( "lineNumberField" ) );
    lineNbField->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
    lineNbField->setContentsMargins( 2, 0, 2, 0 );

    toolBar = addToolBar( QApplication::translate( "klogg::mainwindow::toolbar",
                                                   klogg::mainwindow::toolbar::toolbarTitle ) );
    toolBar->setObjectName( QStringLiteral( "mainToolBar" ) );
    toolBar->setIconSize( QSize( 16, 16 ) );
    toolBar->setMovable( false );
    toolBar->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );
    toolBar->addAction( openAction );
    toolBar->addAction( reloadAction );
    toolBar->addAction( followAction );
    toolBar->addAction( addToFavoritesAction );
    toolBar->addWidget( infoLine );
    toolBar->addAction( stopAction );

    infoToolbarSeparators.reserve( 6 );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addWidget( sizeField );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addWidget( dateField );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addWidget( encodingField );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addWidget( comPortField );
    comPortField->setVisible( false );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addWidget( lineNbField );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addAction( showPreviewerAction );
    toolBar->addAction( showActionsResponsesAction );
    toolBar->addAction( showScriptRunnerAction );
    toolBar->addAction( showScratchPadAction );

    showInfoLabels( false );
}

void MainWindow::createTrayIcon()
{
    trayIcon_ = new QSystemTrayIcon( this );

    QMenu* trayMenu = new QMenu( this );
    QAction* openWindowAction = new QAction( tr( "Open window" ), this );
    QAction* quitAction = new QAction( tr( "Quit" ), this );

    trayMenu->addAction( openWindowAction );
    trayMenu->addAction( quitAction );

    connect( openWindowAction, &QAction::triggered, this, &QMainWindow::show );
    connect( quitAction, &QAction::triggered, [ this ] {
        this->isCloseFromTray_ = true;
        this->close();
    } );

    trayIcon_->setIcon( mainIcon_ );
    trayIcon_->setToolTip( tr( klogg::mainwindow::trayicon::trayiconTip ) );
    trayIcon_->setContextMenu( trayMenu );

    connect( trayIcon_, &QSystemTrayIcon::activated,
             [ this ]( QSystemTrayIcon::ActivationReason reason ) {
                 switch ( reason ) {
                 case QSystemTrayIcon::Trigger:
                     if ( !this->isVisible() ) {
                         this->show();
                     }
                     else {
                         this->hide();
                     }
                     break;
                 default:
                     break;
                 }
             } );

    if ( Configuration::get().minimizeToTray() ) {
        trayIcon_->show();
    }
}
//
// Q_SLOTS:
//

// Opens the file selection dialog to select a new log file
void MainWindow::open()
{
    QString defaultDir = ".";

    // Default to the path of the current file if there is one
    if ( auto current = currentCrawlerWidget() ) {
        QString current_file = session_.getFilename( current );
        QFileInfo fileInfo = QFileInfo( current_file );
        defaultDir = fileInfo.path();
    }

    const auto selectedFiles = QFileDialog::getOpenFileUrls(
        this, tr( "Open file" ), QUrl::fromLocalFile( defaultDir ), tr( "All files (*)" ) );

    std::vector<QUrl> localFiles;
    std::vector<QUrl> remoteFiles;

    std::partition_copy( selectedFiles.cbegin(), selectedFiles.cend(),
                         std::back_inserter( localFiles ), std::back_inserter( remoteFiles ),
                         []( const QUrl& url ) { return url.isLocalFile(); } );

    for ( const auto& localFile : localFiles ) {
        loadFile( localFile.toLocalFile() );
    }

    for ( const auto& remoteFile : remoteFiles ) {
        openRemoteFile( remoteFile );
    }
}

void MainWindow::openComPort()
{
    OpenComPortDialog dialog( this );
    if ( dialog.exec() != QDialog::Accepted ) {
        return;
    }

    auto settings = dialog.settings();
    if ( settings.portName.isEmpty() || settings.filePath.isEmpty() ) {
        return;
    }

    if ( !startComCaptureSession( settings, ComCaptureStartOptions::interactive() ) ) {
        return;
    }

    if ( !loadFile( settings.filePath, true ) ) {
        if ( auto* session = mainTabWidget_.streamSessionForPath( settings.filePath ) ) {
            session->closeConnection();
        }
        QMessageBox::warning( this, tr( "Open COM Port" ),
                              tr( "Failed to open capture file in CILogg." ) );
        return;
    }

    updateActionsSendState();
}

bool MainWindow::startComCaptureSession( SerialCaptureSettings& settings,
                                         MainWindow::ComCaptureStartOptions options,
                                         QString* errorMessage )
{
    const auto showWarning = [ this, &options, errorMessage ]( const QString& message ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = message;
        }
        if ( !options.showErrors ) {
            return;
        }
        const auto icon = options.restoreMode ? QMessageBox::Information : QMessageBox::Warning;
        showComPortMessage( this, icon, tr( "Open COM Port" ), message, options.nonBlockingErrors );
    };

    const auto showInformation = [ this, &options, errorMessage ]( const QString& message ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = message;
        }
        if ( !options.showErrors ) {
            return;
        }
        showComPortMessage( this, QMessageBox::Information, tr( "Open COM Port" ), message,
                            options.nonBlockingErrors );
    };

    if ( settings.portName.isEmpty() || settings.filePath.isEmpty() ) {
        return false;
    }

    const QFileInfo info( settings.filePath );
    const auto absolutePath = info.absoluteFilePath();
    if ( absolutePath.isEmpty() ) {
        showWarning( tr( "Invalid capture file path." ) );
        return false;
    }

    const QDir dir( info.absolutePath() );
    if ( !dir.exists() ) {
        showWarning( tr( "Capture directory does not exist." ) );
        return false;
    }

    settings.filePath = absolutePath;

    const auto availablePorts = QSerialPortInfo::availablePorts();
    const bool portExists = std::any_of( availablePorts.cbegin(), availablePorts.cend(),
                                         [ &settings ]( const QSerialPortInfo& portInfo ) {
                                             return portInfo.portName().compare( settings.portName,
                                                                                 Qt::CaseInsensitive )
                                                        == 0
                                                    || portInfo.systemLocation().compare(
                                                           settings.portName,
                                                           Qt::CaseInsensitive )
                                                           == 0;
                                         } );
    if ( !portExists ) {
        showWarning( tr( "COM port %1 was not found. The capture file remains open, but the COM "
                         "connection was not restored." )
                         .arg( settings.portName ) );
        return false;
    }

    QString captureFileError;
    if ( !ensureComCaptureFileWritable( settings.filePath, &captureFileError ) ) {
        showWarning( tr( "Failed to open capture file: %1" ).arg( captureFileError ) );
        return false;
    }

    const auto filePath = settings.filePath;
    if ( auto existingSession = mainTabWidget_.streamSessionForPath( filePath ) ) {
        if ( existingSession->isConnectionOpen() ) {
            existingSession->closeConnection();
            showInformation( tr( "The existing capture is still closing. Please try again in a moment." ) );
            return false;
        }
        if ( actionsStreamSession_ == existingSession ) {
            actionsStreamSession_.clear();
        }
        mainTabWidget_.clearStreamSessionForPath( filePath );
    }

    auto session = std::make_shared<StreamSession>( settings );
    QPointer<StreamSession> safeSession = session.get();
    connect( session.get(), &StreamSession::connectionOpened, this,
             [ this, session ] {
                 const auto currentFilePath = session->filePath();
                 mainTabWidget_.setStreamSessionForPath( currentFilePath, session );
                 publishScriptLifecycleEvent( currentFilePath, QStringLiteral( "comm_start" ) );
                 updateActionsSendState();
             } );
    connect( session.get(), &StreamSession::connectionClosed, this,
             [ this, session, safeSession ] {
                 const auto currentFilePath = session->filePath();
                 bool tabStillOpen = false;
                 for ( int index = 0; index < mainTabWidget_.count(); ++index ) {
                     auto* widget = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( index ) );
                     if ( widget != nullptr && session_.getFilename( widget ) == currentFilePath ) {
                         tabStillOpen = true;
                         break;
                     }
                 }

                 if ( tabStillOpen ) {
                     mainTabWidget_.setStreamSessionForPath( currentFilePath, session );
                 }
                 else {
                     mainTabWidget_.clearStreamSessionForPath( currentFilePath );
                 }
                 if ( actionsStreamSession_ == safeSession ) {
                     actionsStreamSession_.clear();
                 }
                 publishScriptLifecycleEvent( currentFilePath, QStringLiteral( "comm_stop" ) );
                 updateActionsSendState();
             } );
    connect( session.get(), &StreamSession::errorOccurred, this,
             [ this, filePath, safeSession, settings, options ]( const QString& message ) {
                 const bool startupFailure = safeSession && !safeSession->isConnectionOpen();
                 const auto currentFilePath = safeSession ? safeSession->filePath() : filePath;
                 if ( options.showErrors ) {
                     const auto title = options.restoreMode && startupFailure
                                            ? tr( "Restore COM Port" )
                                            : tr( "COM port capture error" );
                     const auto text = options.restoreMode && startupFailure
                                           ? tr( "Failed to restore COM port %1 for %2.\n"
                                                "The capture file remains open.\n\n%3" )
                                                 .arg( settings.portName, currentFilePath, message )
                                           : tr( "Capture stopped for %1:\n%2" )
                                                 .arg( currentFilePath, message );
                     const auto icon = options.restoreMode && startupFailure
                                           ? QMessageBox::Information
                                           : QMessageBox::Warning;
                     showComPortMessage( this, icon, title, text, options.nonBlockingErrors );
                 }
                 else {
                     LOG_WARNING << "Capture stopped for " << currentFilePath.toStdString() << ": "
                                 << message.toStdString();
                 }
                 if ( safeSession ) {
                     safeSession->closeConnection();
                 }
             } );
    connect( session.get(), &StreamSession::dataObserved, this,
             [ this, safeSession ]( const QByteArray& payloadBytes ) {
                 if ( safeSession ) {
                     publishScriptReceiveEvent( safeSession->filePath(), payloadBytes );
                 }
             } );
    connect( session.get(), &StreamSession::dataTransmitted, this,
             [ this, safeSession ]( const QByteArray& payloadBytes ) {
                 if ( safeSession ) {
                     publishScriptTxEvent( safeSession->filePath(), payloadBytes );
                 }
             } );
    connect( session.get(), &StreamSession::actionSent, this,
             [ this, safeSession ]( int actionId, const QString& actionName, int stepIndex,
                                    const QByteArray& payloadBytes ) {
                 if ( safeSession ) {
                     publishScriptActionSendEvent( safeSession->filePath(), actionId, actionName,
                                                   stepIndex, payloadBytes );
                 }
             } );
    connect( session.get(), &StreamSession::responseMatched, this,
             [ this, safeSession ]( int responseId, const QString& responseName, int counter,
                                    const QByteArray& lineBytes, const QString& matchedText ) {
                 if ( safeSession ) {
                     publishScriptResponseEvent( safeSession->filePath(), responseId, responseName,
                                                 counter, lineBytes, matchedText );
                 }
             } );
    session->start();
    mainTabWidget_.setStreamSessionForPath( settings.filePath, session );

    bool designateForActions = settings.useForActions;
    if ( designateForActions && actionsStreamSession_ && actionsStreamSession_ != session.get()
         && actionsStreamSession_->isConnectionOpen() ) {
        if ( options.allowActionsPrompt ) {
            const auto reply = QMessageBox::question(
                this, tr( "Actions COM Port" ),
                tr( "Are you sure you want to make %1 the actions COM port?" )
                    .arg( settings.portName ),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
            if ( reply != QMessageBox::Yes ) {
                designateForActions = false;
            }
        }
    }
    if ( designateForActions ) {
        actionsStreamSession_ = session.get();
    }

    updateActionsSendState();
    refreshScriptStatusIndicators();
    return true;
}

CommanderResult MainWindow::openRemoteFile( const QUrl& url, bool interactiveErrors,
                                            const QString& normalizedSourceUrl )
{
    Downloader downloader;

    QProgressDialog progressDialog;
    progressDialog.setLabelText( tr( "Downloading %1" ).arg( url.toString() ) );

    connect( &downloader, &Downloader::downloadProgress,
             [ &progressDialog ]( qint64 bytesReceived, qint64 bytesTotal ) {
                 const auto progress = calculateProgress( bytesReceived, bytesTotal );
                 progressDialog.setRange( 0, 100 );
                 progressDialog.setValue( progress );
             } );

    connect( &downloader, &Downloader::finished,
             [ &progressDialog ]( bool isOk ) { progressDialog.done( isOk ? 0 : 1 ); } );

    auto tempFile = new QTemporaryFile( tempDir_.filePath( url.fileName() ), this );
    if ( tempFile->open() ) {
        downloader.download( url, tempFile );
        if ( !progressDialog.exec() ) {
            if ( loadFile( tempFile->fileName() ) ) {
                if ( !normalizedSourceUrl.isEmpty() ) {
                    registerRemoteFileSource( tempFile->fileName(), normalizedSourceUrl );
                }
                return commanderSuccess();
            }

            const auto message = tr( "Failed to open downloaded file %1." ).arg( tempFile->fileName() );
            if ( interactiveErrors ) {
                QMessageBox::critical( this, tr( "CILogg - File download" ), message );
            }
            return commanderFailure( CommanderResultCode::ExecutionFailed, message );
        }
        else if ( interactiveErrors ) {
            QMessageBox::critical( this, tr( "CILogg - File download" ), downloader.lastError() );
        }
        return commanderFailure( CommanderResultCode::ExecutionFailed, downloader.lastError() );
    }
    else if ( interactiveErrors ) {
        QMessageBox::critical( this, tr( "CILogg - File download" ),
                               tr( "Failed to create temp file" ) );
    }
    return commanderFailure( CommanderResultCode::ExecutionFailed,
                             tr( "Failed to create temp file." ) );
}

void MainWindow::switchToOpenedFile( QAction* action )
{
    if ( !action ) {
        return;
    }

    loadFile( action->data().toString() );
}

void MainWindow::openFileFromRecent( QAction* action )
{
    if ( !action ) {
        return;
    }

    const auto filename = action->data().toString();
    if ( QFileInfo{ filename }.isReadable() ) {
        loadFile( filename );
    }
    else {
        const auto userAction = QMessageBox::question(
            this, tr( "CILogg - remove from recent" ),
            tr( "Could not read file %1. Remove it from recent files?" ).arg( filename ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

        if ( userAction == QMessageBox::Yes ) {
            removeFromRecent( filename );
        }
    }
}

void MainWindow::openFileFromFavorites( QAction* action )
{
    if ( !action ) {
        return;
    }

    const auto filename = action->data().toString();
    if ( QFileInfo{ filename }.isReadable() ) {
        loadFile( filename );
    }
    else {
        const auto userAction = QMessageBox::question(
            this, tr( "CILogg - remove from favorites" ),
            tr( "Could not read file %1. Remove it from favorites?" ).arg( filename ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

        if ( userAction == QMessageBox::Yes ) {
            removeFromFavorites( filename );
        }
    }
}

// Close current tab
void MainWindow::closeTab( ActionInitiator initiator )
{
    int currentIndex = mainTabWidget_.currentIndex();

    if ( currentIndex >= 0 ) {
        closeTab( currentIndex, initiator );
    }
    else {
        this->close();
    }
}

// Close all tabs
void MainWindow::closeAll( ActionInitiator initiator )
{
    while ( mainTabWidget_.count() ) {
        closeTab( 0, initiator );
    }
}

// Select all the text in the currently selected view
void MainWindow::selectAll()
{
    if ( infoLine->hasFocus() ) {
        infoLine->setSelection( 0, klogg::isize( infoLine->text() ) );
    }
    else if ( auto current = currentCrawlerWidget(); current != nullptr ) {
        current->selectAll();
    }
}

// Copy the currently selected line into the clipboard
void MainWindow::copy()
{
    try {
        if ( infoLine->hasFocus() && infoLine->hasSelectedText() ) {
            sendTextToClipboard( infoLine->selectedText() );
            return;
        }

        if ( auto current = currentCrawlerWidget(); current != nullptr ) {
            auto text = current->getSelectedText();
            text.replace( QChar::Null, QChar::Space );

            sendTextToClipboard( text, true );
        }
    } catch ( std::exception& err ) {
        LOG_ERROR << "failed to copy data to clipboard " << err.what();
    }
}

// Display the QuickFind bar
void MainWindow::find()
{
    displayQuickFindBar( QuickFindMux::Forward );
}

void MainWindow::clearLog()
{
    const auto current_file = session_.getFilename( currentCrawlerWidget() );
    if ( QMessageBox::warning(
             this, tr( "CILogg - clear file" ),
             tr( "Clear file %1? File content will be removed from disk, this is irreversible" )
                 .arg( current_file ) )
         == QMessageBox::Yes ) {
        QFile::resize( current_file, 0 );
    }
}

void MainWindow::copyFullPath()
{
    const auto current_file = session_.getFilename( currentCrawlerWidget() );
    sendTextToClipboard( QDir::toNativeSeparators( current_file ) );
}

void MainWindow::openContainingFolder()
{
    showPathInFileExplorer( session_.getFilename( currentCrawlerWidget() ) );
}

void MainWindow::openInEditor()
{
    openFileInDefaultApplication( session_.getFilename( currentCrawlerWidget() ) );
}

void MainWindow::tryOpenClipboard( int tryTimes )
{
    auto clipboard = QGuiApplication::clipboard();
    auto text = clipboard->text();

    if ( text.isEmpty() && tryTimes > 0 ) {
        QTimer::singleShot( 50, [ tryTimes, this ]() { tryOpenClipboard( tryTimes - 1 ); } );
    }
    else {
        auto tempFile = new QTemporaryFile( tempDir_.filePath( "cilogg_clipboard" ), this );
        if ( tempFile->open() ) {
            tempFile->write( text.toUtf8() );
            tempFile->flush();

            loadFile( tempFile->fileName() );
        }
    }
}

void MainWindow::openClipboard()
{
    tryOpenClipboard( ClipboardMaxTry );
}

void MainWindow::openUrl()
{
    bool ok;
    const auto urlInClipboard = QUrl::fromUserInput( QApplication::clipboard()->text() );
    const auto selectedUrl = urlInClipboard.isValid() ? urlInClipboard.toString() : QString{};

    QString url
        = QInputDialog::getText( this, tr( "Open URL as log file" ), tr( "URL to download:" ),
                                 QLineEdit::Normal, selectedUrl, &ok );
    if ( ok && !url.isEmpty() ) {
        openRemoteFile( url, true, normalizeCommanderUrl( url ) );
    }
}

// Opens the 'Highlighters' dialog box
void MainWindow::editHighlighters()
{
    HighlightersDialog dialog( this );
    signalMux_.connect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );

    connect( &dialog, &HighlightersDialog::optionsChanged,
             [ this ]() { updateHighlightersMenu(); } );

    dialog.exec();
    signalMux_.disconnect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
}

// Opens dialog to configure predefined filters
void MainWindow::editPredefinedFilters( const QString& newFilter )
{
    PredefinedFiltersDialog dialog( newFilter, this );

    signalMux_.connect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );

    dialog.exec();
    signalMux_.disconnect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
}

void MainWindow::openImportPreviewsDialog()
{
    ImportPreviewsDialog dialog( this );
    dialog.exec();
}

void MainWindow::openImportActionsDialog()
{
    ImportActionsDialog dialog( this );
    dialog.exec();
}

// Opens the 'Options' modal dialog box
void MainWindow::options()
{
    OptionsDialog dialog( this );
    signalMux_.connect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );

    connect( &dialog, &OptionsDialog::optionsChanged, [ this ]() {
        const auto& config = Configuration::get();
        logging::enableFileLogging( config.enableLogging(),
                                    static_cast<logging::LogLevel>( config.loggingLevel() ) );

        newWindowAction->setVisible( config.allowMultipleWindows() );
        followAction->setEnabled( config.anyFileWatchEnabled() );

        updateShortcuts();
        updateRecentFileActions();
    } );
    dialog.exec();

    signalMux_.disconnect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
}

void MainWindow::about()
{
    QMessageBox::about(
        this, tr( "About CILogg" ),
        tr( "<h2>CILogg %1</h2>"
            "<p>A fast, advanced log explorer.</p>"
            "<p>Built %2 from %3</p>"
            "<p><a href=\"https://github.com/dm17ryk/klogg\">https://github.com/dm17ryk/klogg</a></p>"
            "<p>Originally forked from glogg.</p>"
            "<p>Using icons from <a href=\"https://icons8.com\">icons8.com</a> project</p>"
            "<p>Maintained by <a href=\"https://github.com/dm17ryk\">Dmitry Kokotov</a> and contributors.</p>"
            "<p>Copyright &copy; 2026 Nicolas Bonnefon, Anton Filimonov, Dmitry Kokotov and other contributors</p>"
            "<p>You may modify and redistribute the program under the terms of the GPL (version 3 "
            "or later).</p>" )
            .arg( kloggVersion(), kloggBuildDate(), kloggCommit() ) );
}

void MainWindow::aboutQt()
{
    QMessageBox::aboutQt( this, tr( "About Qt" ) );
}

void MainWindow::documentation()
{
    QFile doc( ":/documentation.html" );
    if ( doc.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        const auto text = QString::fromUtf8( doc.readAll() );
        QTextBrowser* tb = new QTextBrowser();
        tb->setOpenExternalLinks( true );
        tb->setHtml( text );
        tb->setWindowFlags( Qt::Window );
        tb->setAttribute( Qt::WA_DeleteOnClose );
        tb->setWindowTitle( tr( "CILogg documentation" ) );
        tb->resize( this->width() / 2, this->height() );
        tb->show();
    }
    else {
        LOG_ERROR << "Can't open documentation resource";
    }
}

void MainWindow::showScratchPad()
{
    auto state = scratchPad_.windowState();
    state.setFlag( Qt::WindowMinimized, false );
    scratchPad_.setWindowState( state );

    scratchPad_.show();
    scratchPad_.activateWindow();
}

void MainWindow::showPreviewer()
{
    auto state = previewWindow_.windowState();
    state.setFlag( Qt::WindowMinimized, false );
    previewWindow_.setWindowState( state );

    previewWindow_.show();
    previewWindow_.activateWindow();
}

void MainWindow::showActionsResponses()
{
    auto state = actionsResponsesWindow_.windowState();
    state.setFlag( Qt::WindowMinimized, false );
    actionsResponsesWindow_.setWindowState( state );

    updateActionsSendState();
    actionsResponsesWindow_.show();
    actionsResponsesWindow_.activateWindow();
}

void MainWindow::showScriptRunner()
{
    if ( applicationHasMethod( "showScriptRunnerWindow()" )
         || applicationHasMethod( "showScriptRunnerWindow(QString)" ) ) {
        QMetaObject::invokeMethod( qApp, "showScriptRunnerWindow", Qt::DirectConnection );
    }
}

void MainWindow::showScenarioRunner()
{
    if ( applicationHasMethod( "showScenarioRunnerWindow()" ) ) {
        QMetaObject::invokeMethod( qApp, "showScenarioRunnerWindow", Qt::DirectConnection );
    }
}

void MainWindow::showLabQueue()
{
    if ( applicationHasMethod( "showLabQueueWindow()" ) ) {
        QMetaObject::invokeMethod( qApp, "showLabQueueWindow", Qt::DirectConnection );
    }
}

void MainWindow::sendToScratchpad( QString newData )
{
    scratchPad_.addData( newData );
    showScratchPad();
}

void MainWindow::replaceDataInScratchpad( QString newData )
{
    scratchPad_.replaceData( newData );
    showScratchPad();
}

void MainWindow::sendToPreview( QString rawLine, QString previewNameOrAuto )    
{
    if ( rawLine.isEmpty() ) {
        return;
    }
    previewWindow_.openMessageTab( rawLine, previewNameOrAuto );
    showPreviewer();
}

void MainWindow::sendActionById( int actionId )
{
    const auto* action = ActionsManager::instance().findActionById( actionId );
    if ( !action ) {
        QMessageBox::warning( this, tr( "Send action" ), tr( "Unknown action id." ) );
        return;
    }

    auto* streamSession = actionsStreamSession_.data();
    if ( !streamSession || !streamSession->isConnectionOpen() ) {
        streamSession = currentStreamSession();
    }
    if ( ( !streamSession || !streamSession->isConnectionOpen() )
         && mainTabWidget_.hasOpenStreamSession() ) {
        streamSession = mainTabWidget_.firstOpenStreamSession();
    }
    if ( !streamSession ) {
        QMessageBox::warning( this, tr( "Send action" ), tr( "No active COM port." ) );
        return;
    }

    QString errorMessage;
    if ( !sendActionDefinition( streamSession, *action, {}, -1, &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Send action" ),
                              errorMessage.isEmpty() ? tr( "Failed to send action." )
                                                     : errorMessage );
        return;
    }
}
void MainWindow::encodingChanged( QAction* action )
{
    const auto mibData = action->data();
    std::optional<int> mib;
    if ( mibData.isValid() ) {
        mib = mibData.toInt();
    }

    LOG_DEBUG << "encodingChanged, encoding " << mib.value_or( 0 );
    if ( auto crawler = currentCrawlerWidget() ) {
        crawler->setEncoding( mib );
        updateInfoLine();
    }
}

void MainWindow::toggleOverviewVisibility( bool isVisible )
{
    auto& config = Configuration::get();
    config.setOverviewVisible( isVisible );
    config.save();
    Q_EMIT optionsChanged();
}

void MainWindow::toggleMainLineNumbersVisibility( bool isVisible )
{
    auto& config = Configuration::get();

    config.setMainLineNumbersVisible( isVisible );
    config.save();
    Q_EMIT optionsChanged();
}

void MainWindow::toggleFilteredLineNumbersVisibility( bool isVisible )
{
    auto& config = Configuration::get();

    config.setFilteredLineNumbersVisible( isVisible );
    config.save();
    Q_EMIT optionsChanged();
}

void MainWindow::changeFollowMode( bool follow )
{
    auto& config = Configuration::get();
    if ( follow && !( config.nativeFileWatchEnabled() || config.pollingEnabled() ) ) {
        LOG_WARNING << "File watch disabled in settings";
    }

    followAction->setChecked( follow );
}

void MainWindow::lineNumberHandler( LineNumber startLine, LinesCount nLines, LineColumn startCol,
                                    LineLength nSymbols )
{
    // The line number received is the internal (starts at 0)
    uint64_t fileSize{};
    uint64_t fileNbLine{};
    QDateTime lastModified;

    session_.getFileInfo( currentCrawlerWidget(), &fileSize, &fileNbLine, &lastModified );

    if ( fileNbLine != 0 ) {
        if ( nSymbols.get() == 0 ) {
            lineNbField->setText( tr( "Ln:%1/%2" ).arg( startLine.get() + 1 ).arg( fileNbLine ) );
        }
        else {
            if ( nLines.get() == 1 ) {
                // portion selection on one line
                lineNbField->setText( tr( "Ln:%1/%2 Col:%3 Sel:%4|%5" )
                                          .arg( startLine.get() + 1 )
                                          .arg( fileNbLine )
                                          .arg( startCol.get() )
                                          .arg( nSymbols.get() )
                                          .arg( nLines.get() ) );
            }
            else {
                // multiple lines selection
                lineNbField->setText( tr( "Ln:%1/%2 Sel:%4|%5" )
                                          .arg( startLine.get() + 1 )
                                          .arg( fileNbLine )
                                          .arg( nSymbols.get() )
                                          .arg( nLines.get() ) );
            }
        }
    }
    else {
        lineNbField->clear();
    }
}

void MainWindow::newPredefinedFilterHandler( QString newFilter )
{
    editPredefinedFilters( newFilter );
}

void MainWindow::updateLoadingProgress( int progress )
{
    LOG_DEBUG << "Loading progress: " << progress;

    QString current_file
        = QDir::toNativeSeparators( session_.getFilename( currentCrawlerWidget() ) );

    // We ignore 0% and 100% to avoid a flash when the file (or update)
    // is very short.
    if ( progress > 0 && progress < 100 ) {
        infoLine->setText( current_file + tr( " - Indexing lines... (%1 %)" ).arg( progress ) );
        infoLine->displayGauge( progress );

        showInfoLabels( false );

        stopAction->setEnabled( true );
        reloadAction->setEnabled( false );
    }
}

void MainWindow::handleLoadingFinished( LoadingStatus status )
{
    LOG_DEBUG << "handleLoadingFinished success=" << ( status == LoadingStatus::Successful );

    // No file is loading
    loadingFileName.clear();

    if ( status == LoadingStatus::Successful ) {
        updateInfoLine();

        infoLine->hideGauge();
        showInfoLabels( true );
        stopAction->setEnabled( false );
        reloadAction->setEnabled( true );

        lineNumberHandler( 0_lnum, LinesCount( 0 ), LineColumn( 0 ), LineLength( 0 ) );

        // Now everything is ready, we can finally show the file!
        currentCrawlerWidget()->show();
    }
    else {
        if ( status == LoadingStatus::NoMemory ) {
            QMessageBox alertBox;
            alertBox.setText( tr( "Not enough memory." ) );
            alertBox.setInformativeText(
                tr( "The system does not have enough memory to hold the index for this file. The "
                    "file will now be closed." ) );
            alertBox.setIcon( QMessageBox::Critical );
            alertBox.exec();
        }

        closeTab( mainTabWidget_.currentIndex(), ActionInitiator::App );
    }

    // mainTabWidget_.setEnabled( true );
}

void MainWindow::handleFilteredViewChanged()
{
    int currentIndex = mainTabWidget_.currentIndex();
    if ( currentIndex >= 0 ) {
        auto* crawler_widget = static_cast<CrawlerWidget*>( mainTabWidget_.widget( currentIndex ) );
        quickFindMux_.registerSelector( crawler_widget );
    }
}

void MainWindow::closeTab( int index, ActionInitiator initiator )
{
    auto widget = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( index ) );

    assert( widget );

    const auto fileName = session_.getFilename( widget );
    publishScriptLifecycleEvent( fileName, QStringLiteral( "tab_close" ) );
    const auto tabId = mainTabWidget_.tabIdAt( index );
    remoteFileSources_.erase( fileName );
    if ( auto session = mainTabWidget_.streamSessionForPath( fileName ) ) {
        if ( session->isConnectionOpen() ) {
            session->closeConnection();
        }
        else {
            mainTabWidget_.clearStreamSessionForPath( fileName );
        }
    }

    widget->stopLoading();
    if ( !tabId.isEmpty() ) {
        if ( applicationHasMethod( "forgetScriptTab(QString)" ) ) {
            QMetaObject::invokeMethod( qApp, "forgetScriptTab", Qt::DirectConnection,
                                       Q_ARG( QString, tabId ) );
        }
    }
    mainTabWidget_.removeCrawler( index );

    if ( initiator == ActionInitiator::User ) {
        addRecentFile( session_.getFilename( widget ) );
    }

    session_.close( widget );

    updateOpenedFilesMenu();
    updateActionsSendState();
    refreshScriptStatusIndicators();

    widget->deleteLater();
}

void MainWindow::startNewStreamFileForTab( int tab )
{
    auto* crawler = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( tab ) );
    if ( crawler == nullptr ) {
        return;
    }

    const auto oldFilePath = session_.getFilename( crawler );
    auto* streamSession = mainTabWidget_.streamSessionForPath( oldFilePath );
    if ( streamSession == nullptr || !streamSession->isConnectionOpen() ) {
        showComPortMessage( this, QMessageBox::Warning, tr( "Start New COM File" ),
                            tr( "No active COM stream is available for this tab." ), false );
        return;
    }

    const auto context = crawler->context();
    const auto viewContext = context ? context->toString() : QString{};
    auto newSettings = streamSession->captureSettings();
    const auto newFilePath = suggestedNextComCapturePath( newSettings );

    QString captureFileError;
    if ( !ensureComCaptureFileWritable( newFilePath, &captureFileError ) ) {
        showComPortMessage( this, QMessageBox::Warning, tr( "Start New COM File" ),
                            tr( "Failed to open new capture file: %1" ).arg( captureFileError ),
                            false );
        return;
    }

    if ( !loadFile( newFilePath, true ) ) {
        showComPortMessage( this, QMessageBox::Warning, tr( "Start New COM File" ),
                            tr( "Failed to open new capture file in CILogg." ), false );
        return;
    }

    auto* newCrawler = static_cast<CrawlerWidget*>( session_.getViewIfOpen( newFilePath ) );
    QString switchError;
    if ( !streamSession->startNewCaptureFile( newFilePath, &switchError ) ) {
        if ( newCrawler != nullptr ) {
            const auto newTab = mainTabWidget_.indexOf( newCrawler );
            if ( newTab >= 0 ) {
                closeTab( newTab, ActionInitiator::App );
            }
        }
        showComPortMessage( this, QMessageBox::Warning, tr( "Start New COM File" ),
                            tr( "Failed to switch capture file: %1" ).arg( switchError ),
                            false );
        return;
    }

    mainTabWidget_.remapStreamSessionPath( oldFilePath, newFilePath );
    publishScriptLifecycleEvent( oldFilePath, QStringLiteral( "comm_stop" ) );
    publishScriptLifecycleEvent( newFilePath, QStringLiteral( "comm_start" ) );
    updateActionsSendState();
    updateComPortStatus();
    refreshComTabIndicators();
    refreshScriptStatusIndicators();
    updateOpenedFilesMenu();

    if ( newCrawler != nullptr && !viewContext.isEmpty() ) {
        QTimer::singleShot( 0, newCrawler,
                            [ newCrawler, viewContext ] { newCrawler->setViewContextLazy( viewContext ); } );
    }
}

CommanderResult MainWindow::closeTabById( const QString& tabId )
{
    const auto index = mainTabWidget_.findTabById( tabId );
    if ( index < 0 ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Open tab %1 was not found." ).arg( tabId ) );
    }

    return closeTabByIndex( index );
}

CrawlerWidget* MainWindow::crawlerWidgetByTabId( const QString& tabId ) const
{
    const auto index = mainTabWidget_.findTabById( tabId );
    return crawlerWidgetByIndex( index );
}

CrawlerWidget* MainWindow::crawlerWidgetByIndex( int tabIndex ) const
{
    if ( tabIndex < 0 || tabIndex >= mainTabWidget_.count() ) {
        return nullptr;
    }

    return qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( tabIndex ) );
}

CrawlerWidget* MainWindow::commanderTargetCrawler( const CommanderRequest& request ) const
{
    if ( !request.tabId.isEmpty() ) {
        return crawlerWidgetByTabId( request.tabId );
    }
    if ( request.tabIndex ) {
        return crawlerWidgetByIndex( *request.tabIndex );
    }
    return currentCrawlerWidget();
}

StreamSession* MainWindow::streamSessionForCrawler( const CrawlerWidget* crawler ) const
{
    if ( crawler == nullptr ) {
        return nullptr;
    }

    const auto filePath = session_.getFilename( crawler );
    if ( filePath.isEmpty() ) {
        return nullptr;
    }

    return mainTabWidget_.streamSessionForPath( filePath );
}

StreamSession* MainWindow::commanderTargetStreamSession( const CommanderRequest& request,
                                                        bool requireOpen ) const
{
    auto* crawler = commanderTargetCrawler( request );
    if ( crawler == nullptr ) {
        return nullptr;
    }

    auto* streamSession = streamSessionForCrawler( crawler );
    if ( streamSession == nullptr ) {
        return nullptr;
    }
    if ( requireOpen && !streamSession->isConnectionOpen() ) {
        return nullptr;
    }

    return streamSession;
}

QVariantList MainWindow::commanderResponseCounters( StreamSession* streamSession,
                                                    const CommanderRequest* request ) const
{
    if ( streamSession == nullptr ) {
        return {};
    }

    auto counters = streamSession->responseCounters();
    if ( request == nullptr ) {
        return counters;
    }

    if ( request->allEntities ) {
        return counters;
    }

    const auto matchesRequestedCounter = [ request ]( const QVariant& value ) {
        const auto map = value.toMap();
        if ( request->entityId ) {
            return map.value( QStringLiteral( "responseId" ) ).toInt() == *request->entityId;
        }
        if ( !request->entityName.isEmpty() ) {
            return map.value( QStringLiteral( "responseName" ) )
                       .toString()
                       .compare( request->entityName, Qt::CaseInsensitive )
                   == 0;
        }
        return true;
    };

    QVariantList filtered;
    for ( const auto& counter : counters ) {
        if ( matchesRequestedCounter( counter ) ) {
            filtered.push_back( counter );
        }
    }
    return filtered;
}

CommanderResult MainWindow::closeTabByIndex( int tabIndex )
{
    if ( tabIndex < 0 || tabIndex >= mainTabWidget_.count() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Open tab at index %1 was not found." ).arg( tabIndex ) );
    }

    closeTab( tabIndex, ActionInitiator::App );
    return commanderSuccess();
}

CommanderResult MainWindow::focusTabById( const QString& tabId )
{
    const auto index = mainTabWidget_.findTabById( tabId );
    if ( index < 0 ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Open tab %1 was not found." ).arg( tabId ) );
    }

    return focusTabByIndex( index );
}

CommanderResult MainWindow::focusTabByIndex( int tabIndex )
{
    if ( tabIndex < 0 || tabIndex >= mainTabWidget_.count() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Open tab at index %1 was not found." ).arg( tabIndex ) );
    }

    mainTabWidget_.setCurrentIndex( tabIndex );
    setWindowState( windowState() & ~Qt::WindowMinimized );
    show();
    raise();
    activateWindow();
    return commanderSuccess();
}

CommanderResult MainWindow::closeAllTabsCommander()
{
    closeAll( ActionInitiator::App );
    return commanderSuccess();
}

CommanderResult MainWindow::commanderSendAction( const CommanderRequest& request )
{
    if ( !request.entityId ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "send_action requires an action id." ) );
    }

    const auto* action = ActionsManager::instance().findActionById( *request.entityId );
    if ( action == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Action %1 was not found." ).arg( *request.entityId ) );
    }

    auto* crawler = commanderTargetCrawler( request );
    if ( crawler == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested tab was not found." ) );
    }

    auto* streamSession = streamSessionForCrawler( crawler );
    if ( streamSession == nullptr || !streamSession->isConnectionOpen() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested live communication tab was not found." ) );
    }

    QString errorMessage;
    if ( !sendActionDefinition( streamSession, *action, {}, -1, &errorMessage ) ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed,
                                 errorMessage.isEmpty() ? tr( "Failed to send action." )
                                                        : errorMessage );
    }

    return commanderSuccess();
}

CommanderResult MainWindow::commanderWaitResponse( const CommanderRequest& request )
{
    if ( !request.timeoutMs || *request.timeoutMs <= 0 ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "wait_response requires a positive timeout." ) );
    }

    const ResponseDefinition* response = nullptr;
    if ( request.entityId ) {
        response = ActionsManager::instance().findResponseById( *request.entityId );
    }
    else if ( !request.entityName.isEmpty() ) {
        response = ActionsManager::instance().findResponseByName( request.entityName );
    }

    if ( response == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested response was not found." ) );
    }

    auto* crawler = commanderTargetCrawler( request );
    if ( crawler == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested tab was not found." ) );
    }

    auto* streamSession = streamSessionForCrawler( crawler );
    if ( streamSession == nullptr || !streamSession->isConnectionOpen() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested live communication tab was not found." ) );
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot( true );

    QVariantMap payload;
    payload.insert( QStringLiteral( "responseId" ), response->id );
    payload.insert( QStringLiteral( "responseName" ), response->name );

    CommanderResult result = commanderFailure( CommanderResultCode::ExecutionFailed,
                                               tr( "wait_response did not complete." ) );

    QMetaObject::Connection lineConnection;
    QMetaObject::Connection disconnectConnection;
    QObject::connect( &timer, &QTimer::timeout, &loop, [ & ]() {
        result = commanderFailure( CommanderResultCode::NotFound,
                                   tr( "Timed out waiting for response %1." ).arg( response->name ) );
        loop.quit();
    } );
    disconnectConnection = QObject::connect( streamSession, &QObject::destroyed, &loop, [ & ]() {
        result = commanderFailure( CommanderResultCode::ExecutionFailed,
                                   tr( "COM session closed while waiting for response." ) );
        loop.quit();
    } );
    lineConnection = QObject::connect( streamSession, &StreamSession::lineObserved, &loop,
                                       [ & ]( const QByteArray& lineBytes ) {
                                           const auto match = matchResponseDefinition( *response, lineBytes );
                                           if ( !match.matched ) {
                                               return;
                                           }

                                           QVariantMap captures;
                                           for ( auto it = match.captures.cbegin();
                                                 it != match.captures.cend(); ++it ) {
                                               captures.insert( it.key(), it.value() );
                                           }
                                           payload.insert( QStringLiteral( "matchedLine" ), match.lineText );
                                           if ( !captures.isEmpty() ) {
                                               payload.insert( QStringLiteral( "captures" ), captures );
                                           }
                                           result = commanderSuccess( {}, payload );
                                           loop.quit();
                                       } );

    timer.start( *request.timeoutMs );
    loop.exec();
    QObject::disconnect( lineConnection );
    QObject::disconnect( disconnectConnection );
    return result;
}

CommanderResult MainWindow::commanderStartComm( const CommanderRequest& request )
{
    auto* streamSession = commanderTargetStreamSession( request, false );
    if ( streamSession == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested live communication tab was not found." ) );
    }

    if ( streamSession->isConnectionOpen() ) {
        return commanderSuccess();
    }

    streamSession->start();
    updateActionsSendState();
    return commanderSuccess();
}

CommanderResult MainWindow::commanderStopComm( const CommanderRequest& request )
{
    auto* streamSession = commanderTargetStreamSession( request, true );
    if ( streamSession == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested live communication tab was not found." ) );
    }

    streamSession->closeConnection();
    return commanderSuccess();
}

CommanderResult MainWindow::commanderGetCommStatus( const CommanderRequest& request ) const
{
    auto* crawler = commanderTargetCrawler( request );
    auto* streamSession = commanderTargetStreamSession( request, false );
    if ( crawler == nullptr || streamSession == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested live communication tab was not found." ) );
    }

    QVariantMap payload;
    payload.insert( QStringLiteral( "tabId" ),
                    mainTabWidget_.tabIdAt( mainTabWidget_.indexOf( crawler ) ) );
    payload.insert( QStringLiteral( "tabIndex" ), mainTabWidget_.indexOf( crawler ) );
    payload.insert( QStringLiteral( "windowId" ), session_.windowId() );
    payload.insert( QStringLiteral( "windowIndex" ), static_cast<int>( session_.windowIndex() ) );
    payload.insert( QStringLiteral( "filePath" ), session_.getFilename( crawler ) );
    payload.insert( QStringLiteral( "displayName" ),
                    mainTabWidget_.tabDisplayNameAt( mainTabWidget_.indexOf( crawler ) ) );

    const auto& settings = streamSession->captureSettings();
    QVariantMap comInfo;
    comInfo.insert( QStringLiteral( "portName" ), settings.portName );
    comInfo.insert( QStringLiteral( "baudRate" ), settings.baudRate );
    comInfo.insert( QStringLiteral( "connected" ), streamSession->isConnectionOpen() );
    comInfo.insert( QStringLiteral( "loggingEnabled" ), streamSession->isLoggingEnabled() );
    comInfo.insert( QStringLiteral( "isActionsPort" ), isActionsStreamSession( streamSession ) );
    comInfo.insert( QStringLiteral( "responseCounters" ),
                    commanderResponseCounters( streamSession ) );
    payload.insert( QStringLiteral( "com" ), comInfo );

    return commanderSuccess( {}, payload );
}

CommanderResult MainWindow::commanderSetLogging( const CommanderRequest& request, bool enabled )
{
    auto* streamSession = commanderTargetStreamSession( request, false );
    if ( streamSession == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested live communication tab was not found." ) );
    }

    streamSession->setLoggingEnabled( enabled );
    return commanderSuccess();
}

CommanderResult MainWindow::commanderAddComment( const CommanderRequest& request )
{
    auto* streamSession = commanderTargetStreamSession( request, true );
    if ( streamSession == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested live communication tab was not found." ) );
    }

    QString text = request.commentText.trimmed();
    if ( request.timestampComment ) {
        const auto timestamp = QDateTime::currentDateTime().toString( Qt::ISODateWithMs );
        text = text.isEmpty() ? timestamp : QStringLiteral( "%1 %2" ).arg( timestamp, text );
    }
    if ( text.isEmpty() ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "add_comment requires text." ) );
    }

    QByteArray output = text.toUtf8();
    output.append( "\r\n" );
    streamSession->appendToFile( output );
    return commanderSuccess();
}

CommanderResult MainWindow::commanderGetResponseCounter( const CommanderRequest& request ) const
{
    auto* streamSession = commanderTargetStreamSession( request, false );
    if ( streamSession == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested live communication tab was not found." ) );
    }

    const auto counters = commanderResponseCounters( streamSession, &request );
    if ( counters.isEmpty() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested response counter was not found." ) );
    }

    QVariantMap payload;
    payload.insert( QStringLiteral( "responseCounters" ), counters );
    return commanderSuccess( {}, payload );
}

CommanderResult MainWindow::commanderResetResponseCounter( const CommanderRequest& request )
{
    auto* streamSession = commanderTargetStreamSession( request, false );
    if ( streamSession == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested live communication tab was not found." ) );
    }

    if ( request.allEntities ) {
        streamSession->resetAllResponseCounters();
        return commanderSuccess();
    }

    const ResponseDefinition* response = nullptr;
    if ( request.entityId ) {
        response = ActionsManager::instance().findResponseById( *request.entityId );
    }
    else if ( !request.entityName.isEmpty() ) {
        response = ActionsManager::instance().findResponseByName( request.entityName );
    }

    if ( response == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested response was not found." ) );
    }

    streamSession->resetResponseCounter( response->id );
    return commanderSuccess();
}

CommanderResult MainWindow::commanderClearComm( const CommanderRequest& request )
{
    auto* crawler = commanderTargetCrawler( request );
    auto* streamSession = commanderTargetStreamSession( request, true );
    if ( crawler == nullptr || streamSession == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested live communication tab was not found." ) );
    }

    mainTabWidget_.setCurrentWidget( crawler );
    crawler->reload();
    return commanderSuccess(
        tr( "Live communication view reloaded. Non-destructive clear is not yet separate from reload." ) );
}

CommanderResult MainWindow::commanderFilters( const CommanderRequest& request ) const
{
    auto* crawler = commanderTargetCrawler( request );
    if ( crawler == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested tab was not found." ) );
    }

    const auto targetTabIndex = mainTabWidget_.indexOf( crawler );
    QVariantMap payload;
    payload.insert( QStringLiteral( "windowIndex" ), static_cast<int>( session_.windowIndex() ) );
    payload.insert( QStringLiteral( "windowId" ), session_.windowId() );
    payload.insert( QStringLiteral( "tabIndex" ), targetTabIndex );
    payload.insert( QStringLiteral( "tabId" ), mainTabWidget_.tabIdAt( targetTabIndex ) );

    auto filters = request.predefinedFilters ? crawler->commanderPredefinedFilters()
                                             : crawler->commanderFilters();
    if ( !request.filterId.isEmpty() ) {
        const auto match = std::find_if( filters.cbegin(), filters.cend(), [ &request ]( const auto& value ) {
            return value.toMap().value( QStringLiteral( "filterId" ) ).toString() == request.filterId;
        } );
        if ( match == filters.cend() ) {
            return commanderFailure( CommanderResultCode::NotFound,
                                     tr( "Requested filter was not found." ) );
        }
        filters = QVariantList{ *match };
    }
    else if ( request.filterIndex ) {
        const auto match = std::find_if( filters.cbegin(), filters.cend(), [ &request ]( const auto& value ) {
            return value.toMap().value( QStringLiteral( "filterIndex" ) ).toInt() == *request.filterIndex;
        } );
        if ( match == filters.cend() ) {
            return commanderFailure( CommanderResultCode::NotFound,
                                     tr( "Requested filter was not found." ) );
        }
        filters = QVariantList{ *match };
    }

    payload.insert( QStringLiteral( "filters" ), filters );
    payload.insert( QStringLiteral( "source" ),
                    request.predefinedFilters ? QStringLiteral( "predefined" )
                                              : QStringLiteral( "history" ) );
    return commanderSuccess( {}, payload );
}

CommanderResult MainWindow::commanderSetFilter( const CommanderRequest& request )
{
    auto* crawler = commanderTargetCrawler( request );
    if ( crawler == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested tab was not found." ) );
    }

    if ( !request.filterId.isEmpty() ) {
        const auto filter = request.predefinedFilters
                                ? crawler->commanderPredefinedFilterById( request.filterId )
                                : crawler->commanderFilterById( request.filterId );
        if ( !filter ) {
            return commanderFailure( CommanderResultCode::NotFound,
                                     tr( "Requested filter was not found." ) );
        }
        crawler->applyCommanderPredefinedFilter( *filter, request.runSearch,
                                                 request.rearmAutoRefresh );
        return commanderSuccess();
    }

    if ( request.filterIndex ) {
        const auto filter = request.predefinedFilters
                                ? crawler->commanderPredefinedFilterByIndex( *request.filterIndex )
                                : crawler->commanderFilterByIndex( *request.filterIndex );
        if ( !filter ) {
            return commanderFailure( CommanderResultCode::NotFound,
                                     tr( "Requested filter was not found." ) );
        }
        crawler->applyCommanderPredefinedFilter( *filter, request.runSearch,
                                                 request.rearmAutoRefresh );
        return commanderSuccess();
    }

    if ( !request.filterString.isEmpty() ) {
        crawler->applyCommanderSearchPattern( request.filterString, request.runSearch,
                                              request.rearmAutoRefresh );
        return commanderSuccess();
    }

    return commanderFailure( CommanderResultCode::InvalidRequest,
                             tr( "set_filter requires a filter selector." ) );
}

CommanderResult MainWindow::commanderSearch( const CommanderRequest& request )
{
    auto* crawler = commanderTargetCrawler( request );
    if ( crawler == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested tab was not found." ) );
    }

    if ( request.searchText.isEmpty() ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "search requires a search expression." ) );
    }

    const auto targetTabIndex = mainTabWidget_.indexOf( crawler );
    if ( targetTabIndex >= 0 ) {
        mainTabWidget_.setCurrentIndex( targetTabIndex );
    }

    crawler->applyCommanderAutomationSearch( request );
    return commanderSuccess( {}, automationState() );
}

CommanderResult MainWindow::commanderSetFollowMode( const CommanderRequest& request )
{
    auto* crawler = commanderTargetCrawler( request );
    if ( crawler == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Requested tab was not found." ) );
    }
    if ( !request.enabled.has_value() ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "set_follow_mode requires an explicit state." ) );
    }

    const auto targetTabIndex = mainTabWidget_.indexOf( crawler );
    if ( targetTabIndex >= 0 ) {
        mainTabWidget_.setCurrentIndex( targetTabIndex );
    }

    followAction->setChecked( *request.enabled );
    return commanderSuccess( {}, automationState() );
}

CommanderResult MainWindow::commanderInvokeAction( const CommanderRequest& request )
{
    if ( request.objectName.isEmpty() ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "invoke_action requires an object name." ) );
    }

    auto* action = findChild<QAction*>( request.objectName, Qt::FindChildrenRecursively );
    if ( action == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Action %1 was not found." ).arg( request.objectName ) );
    }
    if ( !action->isEnabled() ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed,
                                 tr( "Action %1 is disabled." ).arg( request.objectName ) );
    }

    if ( action->isCheckable() ) {
        action->toggle();
    }
    else {
        action->trigger();
    }

    QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
    return commanderSuccess( {}, automationState() );
}

CommanderResult MainWindow::closeFileByPath( const QString& filePath )
{
    for ( int index = 0; index < mainTabWidget_.count(); ++index ) {
        auto* widget = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( index ) );
        if ( widget == nullptr ) {
            continue;
        }

        if ( session_.getFilename( widget ) == filePath ) {
            closeTab( index, ActionInitiator::App );
            return commanderSuccess();
        }
    }

    return commanderFailure( CommanderResultCode::NotFound,
                             tr( "Open file %1 was not found." ).arg( filePath ) );
}

CommanderResult MainWindow::closeUrlBySource( const QString& url )
{
    const auto target = std::find_if( remoteFileSources_.cbegin(), remoteFileSources_.cend(),
                                      [ &url ]( const auto& entry ) { return entry.second == url; } );
    if ( target == remoteFileSources_.cend() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Open URL %1 was not found." ).arg( url ) );
    }

    return closeFileByPath( target->first );
}

CommanderResult MainWindow::closeComPortByName( const QString& portName )
{
    for ( int index = 0; index < mainTabWidget_.count(); ++index ) {
        auto* widget = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( index ) );
        if ( widget == nullptr ) {
            continue;
        }

        const auto fileName = session_.getFilename( widget );
        auto* streamSession = mainTabWidget_.streamSessionForPath( fileName );
        if ( streamSession == nullptr || !streamSession->isConnectionOpen() ) {
            continue;
        }

        if ( streamSession->captureSettings().portName.compare( portName, Qt::CaseInsensitive ) == 0 ) {
            streamSession->closeConnection();
            return commanderSuccess();
        }
    }

    return commanderFailure( CommanderResultCode::NotFound,
                             tr( "Open COM port %1 was not found." ).arg( portName ) );
}

void MainWindow::currentTabChanged( int index )
{
    LOG_DEBUG << "currentTabChanged";

    if ( index >= 0 ) {
        auto* crawler_widget = static_cast<CrawlerWidget*>( mainTabWidget_.widget( index ) );
        signalMux_.setCurrentDocument( crawler_widget );
        quickFindMux_.registerSelector( crawler_widget );

        // New tab is set up with fonts etc...
        Q_EMIT optionsChanged();

        updateMenuBarFromDocument( crawler_widget );
        updateTitleBar( session_.getFilename( crawler_widget ) );
        updateFavoritesMenu();

        editMenu->setEnabled( true );
    }
    else {
        // No tab left
        signalMux_.setCurrentDocument( nullptr );
        quickFindMux_.registerSelector( nullptr );

        infoLine->hideGauge();
        infoLine->clear();
        showInfoLabels( false );

        updateTitleBar( QString() );

        editMenu->setEnabled( false );
        addToFavoritesAction->setEnabled( false );
        addToFavoritesMenuAction->setEnabled( false );
    }

    updateActionsSendState();
}

void MainWindow::changeQFPattern( const QString& newPattern )
{
    quickFindWidget_.changeDisplayedPattern( newPattern, true );
}

void MainWindow::loadFileNonInteractive( const QString& file_name )
{
    LOG_DEBUG << "loadFileNonInteractive( " << file_name.toStdString() << " )";

    loadFile( file_name );

    // Try to get the window to the front
    // This is a bit of a hack but has been tested on:
    // Qt 5.3 / Gnome / Linux
    // Qt 5.11 / Win10
#ifdef Q_OS_WIN
    const auto isMaximized = isMaximized_;

    if ( isMaximized ) {
        showMaximized();
    }
    else {
        showNormal();
    }

    activateWindow();
    raise();
#else
    Qt::WindowFlags window_flags = windowFlags();
    window_flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags( window_flags );

    raise();
    activateWindow();

    window_flags = windowFlags();
    window_flags &= ~Qt::WindowStaysOnTopHint;
    setWindowFlags( window_flags );
    show();
#endif

    if ( auto currentCrawler = currentCrawlerWidget() ) {
        currentCrawler->setFocus();
    }
}

void MainWindow::registerRemoteFileSource( const QString& filePath,
                                           const QString& normalizedSourceUrl )
{
    if ( filePath.isEmpty() || normalizedSourceUrl.isEmpty() ) {
        return;
    }

    remoteFileSources_[ QFileInfo{ filePath }.absoluteFilePath() ] = normalizedSourceUrl;
}

//
// Events
//

// Closes the application
void MainWindow::closeEvent( QCloseEvent* event )
{
    if ( !isCloseFromTray_ && this->isVisible() && Configuration::get().minimizeToTray() ) {
        event->ignore();
        trayIcon_->show();
        this->hide();
    }
    else {
        scratchPad_.close();
        previewWindow_.close();
        actionsResponsesWindow_.close();

        const auto saveSettings = session_.close();
        if ( saveSettings ) {
            writeSettings();
        }

        closeAll( ActionInitiator::App );
        trayIcon_->hide();
        Q_EMIT windowClosed();

        event->accept();
    }
}

// Minimize handling the application
void MainWindow::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::WindowStateChange ) {
        isMaximized_ = windowState().testFlag( Qt::WindowMaximized );

        if ( this->windowState() & Qt::WindowMinimized ) {
            if ( Configuration::get().minimizeToTray() ) {
                dispatchToObject( [ this ] {
                    trayIcon_->show();
                    this->hide();
                }, this );
            }
        }
    }
    else if ( event->type() == QEvent::StyleChange ) {
        dispatchToObject( [ this ] {
            loadIcons();
            updateOpenedFilesMenu();
            updateFavoritesMenu();
            updateHighlightersMenu();
        }, this );
    }
    else if ( event->type() == QEvent::LanguageChange ) {
        reTranslateUI();
    }

    QMainWindow::changeEvent( event );
}

// Accepts the drag event if it looks like a filename
void MainWindow::dragEnterEvent( QDragEnterEvent* event )
{
    if ( event->mimeData()->hasFormat( "text/uri-list" ) )
        event->acceptProposedAction();
}

// Tries and loads the file if the URL dropped is local
void MainWindow::dropEvent( QDropEvent* event )
{
    const QList<QUrl> urls = event->mimeData()->urls();

    for ( const auto& url : urls ) {
        auto fileName = url.toLocalFile();
        if ( fileName.isEmpty() )
            continue;

        loadFile( fileName );
    }
}

bool MainWindow::event( QEvent* event )
{
    if ( event->type() == QEvent::WindowActivate ) {
        Q_EMIT windowActivated();
    }
    else if ( event->type() == QEvent::Show ) {
        if ( this->windowHandle() ) {
            std::call_once( screenChangesConnect_, [ this ]() {
                logScreenInfo( this->windowHandle()->screen() );
                connect( this->windowHandle(), &QWindow::screenChanged,
                         [ this ]( QScreen* screen ) { logScreenInfo( screen ); } );
            } );
        }
    }

    return QMainWindow::event( event );
}

//
// Private functions
//

bool MainWindow::extractAndLoadFile( const QString& fileName )
{
    const auto& config = Configuration::get();

    if ( !config.extractArchives() ) {
        return false;
    }

    if ( !config.extractArchivesAlways() ) {
        const auto userChoice
            = QMessageBox::question( this, tr( "CILogg" ), tr( "Extract archive to temp folder?" ) );
        if ( userChoice == QMessageBox::No ) {
            return false;
        }
    }

    const auto decompressAction = Decompressor::action( fileName );

    Decompressor decompressor;
    AtomicFlag decompressInterrupt;

    QProgressDialog progressDialog;
    progressDialog.setLabelText( tr( "Extracting %1" ).arg( fileName ) );
    progressDialog.setRange( 0, 0 );

    connect( &decompressor, &Decompressor::finished,
             [ &progressDialog ]( bool isOk ) { progressDialog.done( isOk ? 0 : 1 ); } );
    connect( &progressDialog, &QProgressDialog::canceled,
             [ &decompressInterrupt, &decompressor ]() {
                 decompressInterrupt.set();
                 decompressor.waitForResult();
             } );

    if ( decompressAction == DecompressAction::Decompress ) {

        auto tempFile = new QTemporaryFile(
            this->tempDir_.filePath( QFileInfo( fileName ).fileName() ), this );

        if ( tempFile->open() && decompressor.decompress( fileName, tempFile, decompressInterrupt )
             && !progressDialog.exec() ) {

            if ( decompressInterrupt ) {
                return false;
            }

            return this->loadFile( tempFile->fileName() );
        }
        else {
            QMessageBox::warning(
                this, tr( "CILogg" ),
                tr( "Failed to decompress %1" ).arg( QDir::toNativeSeparators( fileName ) ) );
        }
    }
    else if ( decompressAction == DecompressAction::Extract ) {
        QTemporaryDir archiveDir{ this->tempDir_.filePath( QFileInfo( fileName ).fileName() ) };
        archiveDir.setAutoRemove( false );
        if ( decompressor.extract( fileName, archiveDir.path(), decompressInterrupt )
             && !progressDialog.exec() ) {

            if ( decompressInterrupt ) {
                return false;
            }

            const auto selectedFiles = QFileDialog::getOpenFileNames(
                this, tr( "Open file from archive" ), archiveDir.path(), tr( "All files (*)" ) );

            for ( const auto& extractedFile : selectedFiles ) {
                this->loadFile( extractedFile );
            }

            return true;
        }
        else {
            QMessageBox::warning(
                this, tr( "CILogg" ),
                tr( "Failed to extract %1" ).arg( QDir::toNativeSeparators( fileName ) ) );
        }
    }

    return false;
}

// Create a CrawlerWidget for the passed file, start its loading
// and update the title bar.
// The loading is done asynchronously.
bool MainWindow::loadFile( const QString& fileName, bool followFile )
{
    LOG_DEBUG << "loadFile ( " << fileName.toStdString() << " )";

    // First check if the file is already open...
    auto* existing_crawler = static_cast<CrawlerWidget*>( session_.getViewIfOpen( fileName ) );

    if ( existing_crawler ) {
        auto* crawlerWindow = qobject_cast<MainWindow*>( existing_crawler->window() );
        crawlerWindow->mainTabWidget_.setCurrentWidget( existing_crawler );
        crawlerWindow->activateWindow();
        return true;
    }

    const auto decompressAction = Decompressor::action( fileName );

    if ( decompressAction == DecompressAction::None || !Configuration::get().extractArchives() ) {
        // Load the file
        loadingFileName = fileName;

        try {
            const auto previousViewContext = [ &fileName ]() {
                const auto& session = SessionInfo::getSynced();
                const auto& windows = session.windows();
                for ( const auto& windowId : windows ) {
                    const auto openedFiles = session.openFiles( windowId );
                    const auto existingContext
                        = std::find_if( openedFiles.begin(), openedFiles.end(),
                                        [ &fileName ]( const auto& context ) {
                                            return context.fileName == fileName;
                                        } );
                    if ( existingContext != openedFiles.end() ) {
                        return existingContext->viewContext;
                    }
                }
                return QString{};
            }();

            CrawlerWidget* crawler_widget = static_cast<CrawlerWidget*>(
                session_.open( fileName, []() { return new CrawlerWidget(); } ) );

            if ( !crawler_widget ) {
                LOG_ERROR << "Can't create crawler for " << fileName.toStdString();
                return false;
            }

            // We won't show the widget until the file is fully loaded
            crawler_widget->hide();

            if ( !previousViewContext.isEmpty() ) {
                LOG_INFO << "Found existing context";
                crawler_widget->setViewContext( previousViewContext );
            }

            // We disable the tab widget to avoid having someone switch
            // tab during loading. (maybe FIXME)
            // mainTabWidget_.setEnabled( false );

            int index = mainTabWidget_.addCrawler( crawler_widget, fileName );
            publishScriptLifecycleEvent( fileName, QStringLiteral( "tab_open" ) );

            // Setting the new tab, the user will see a blank page for the duration
            // of the loading, with no way to switch to another tab
            mainTabWidget_.setCurrentIndex( index );

            addRecentFile( fileName );
            updateOpenedFilesMenu();

            const auto& config = Configuration::get();
            if ( config.anyFileWatchEnabled() && ( followFile || config.followFileOnLoad() ) ) {
                signalCrawlerToFollowFile( crawler_widget );
                followAction->setChecked( true );
            }
        } catch ( ... ) {
            LOG_ERROR << "Can't open file " << fileName.toStdString();
            return false;
        }

        LOG_DEBUG << "Success loading file " << fileName.toStdString();
        return true;
    }
    else {
        return extractAndLoadFile( fileName );
    }
}

// Strips the passed filename from its directory part.
QString MainWindow::strippedName( const QString& fullFileName ) const
{
    return QFileInfo( fullFileName ).fileName();
}

// Return the currently active CrawlerWidget, or NULL if none
CrawlerWidget* MainWindow::currentCrawlerWidget() const
{
    auto current = qobject_cast<CrawlerWidget*>( mainTabWidget_.currentWidget() );

    return current;
}

StreamSession* MainWindow::currentStreamSession() const
{
    const auto* crawler = currentCrawlerWidget();
    if ( !crawler ) {
        return nullptr;
    }
    const auto fileName = session_.getFilename( crawler );
    if ( fileName.isEmpty() ) {
        return nullptr;
    }
    return mainTabWidget_.streamSessionForPath( fileName );
}

void MainWindow::updateActionsSendState()
{
    const auto* streamSession = actionsStreamSession_.data();
    bool available = streamSession && streamSession->isConnectionOpen();
    if ( !available ) {
        streamSession = currentStreamSession();
        available = streamSession && streamSession->isConnectionOpen();
    }
    if ( !available ) {
        available = mainTabWidget_.hasOpenStreamSession();
    }
    actionsResponsesWindow_.setSendAvailable( available );
    refreshComTabIndicators();
    updateComPortStatus();
}

void MainWindow::updateComPortStatus()
{
    if ( !comPortField ) {
        return;
    }

    const auto* streamSession = currentStreamSession();
    if ( streamSession && streamSession->isConnectionOpen() ) {
        const auto settings = streamSession->captureSettings();
        auto text = tr( "%1 @ %2" ).arg( settings.portName ).arg( settings.baudRate );
        if ( isActionsStreamSession( streamSession ) ) {
            text += ActionsPortSuffix;
        }
        comPortField->setText( text );
        comPortField->setVisible( true );
    }
    else {
        comPortField->clear();
        comPortField->setVisible( false );
    }
}

void MainWindow::refreshComTabIndicators()
{
    for ( int index = 0; index < mainTabWidget_.count(); ++index ) {
        auto* widget = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( index ) );
        if ( widget == nullptr ) {
            continue;
        }

        const auto filePath = session_.getFilename( widget );
        if ( filePath.isEmpty() ) {
            continue;
        }

        const auto* streamSession = mainTabWidget_.streamSessionForPath( filePath );
        mainTabWidget_.setTabActionsPort( filePath, isActionsStreamSession( streamSession ) );
    }
}

bool MainWindow::isActionsStreamSession( const StreamSession* streamSession ) const
{
    return streamSession != nullptr && streamSession == actionsStreamSession_.data()
           && streamSession->isConnectionOpen();
}

// Update the title bar.
void MainWindow::updateTitleBar( const QString& file_name )
{
    QString shownName = tr( "Untitled" );
    if ( !file_name.isEmpty() ) {
        shownName = strippedName( file_name );
    }

    QString indexPart = "";
    if ( session_.windowIndex() > 0 ) {
        indexPart = QString( " #%1" ).arg( session_.windowIndex() + 1 );
    }

    setWindowTitle( tr( "%1 - %2%3" ).arg( shownName, tr( "CILogg" ), indexPart ) + tr( " (build " )
                    + kloggVersion() + ")" );
}

void MainWindow::addRecentFile( const QString& fileName )
{
    auto& recentFiles = RecentFiles::getSynced();
    recentFiles.addRecent( fileName );
    recentFiles.save();
    updateRecentFileActions();
}

// Updates the actions for the recent files.
// Must be called after having added a new name to the list.
void MainWindow::updateRecentFileActions()
{
    auto& recentFiles = RecentFiles::get();
    QStringList recent_files = recentFiles.recentFiles();
    int recent_files_max_items = recentFiles.getNumberItemsToShow();

    if ( recentFiles.recentFiles().count() > 0 ) {
        recentFilesMenu->setEnabled( true );
        for ( auto j = 0; j < MAX_RECENT_FILES; ++j ) {
            const auto actionIndex = static_cast<size_t>( j );
            if ( j < recent_files_max_items ) {
                int key = j + ( ( j < 9 ) ? 0x31 : ( 0x61 - 9 ) ); // shortcuts: 1..9 next a,b...
                QString text
                    = tr( "&%1 %2" ).arg( QChar( key ) ).arg( strippedName( recent_files[ j ] ) );
                recentFileActions[ actionIndex ]->setText( text );
                recentFileActions[ actionIndex ]->setToolTip( recent_files[ j ] );
                recentFileActions[ actionIndex ]->setData( recent_files[ j ] );
                recentFileActions[ actionIndex ]->setVisible( true );
            }
            else {
                recentFileActions[ actionIndex ]->setVisible( false );
            }
        }
    }
    else {
        recentFilesMenu->setEnabled( false );
    }

    // separatorAction->setVisible(!recentFiles.isEmpty());
}

// Clear the list of the recent files
void MainWindow::clearRecentFileActions()
{
    auto& recentFiles = RecentFiles::getSynced();
    recentFiles.removeAll();
    recentFiles.save();
    updateRecentFileActions();
}
// Update our menu bar to match the settings of the crawler
// (used when the tab is changed)
void MainWindow::updateMenuBarFromDocument( const CrawlerWidget* crawler )
{
    const auto encodingMib = crawler->encodingMib();

    auto encodingActions = encodingGroup->actions();
    auto encodingItem = std::find_if( encodingActions.begin(), encodingActions.end(),
                                      [ &encodingMib ]( const auto& action ) {
                                          return ( !encodingMib && !action->data().isValid() )
                                                 || ( encodingMib && action->data().isValid()
                                                      && *encodingMib == action->data().toInt() );
                                      } );

    if ( encodingItem != encodingActions.end() ) {
        ( *encodingItem )->setChecked( true );
    }

    followAction->setChecked( crawler->isFollowEnabled() );
    textWrapAction->setChecked( crawler->isTextWrapEnabled() );
}

// Update the top info line from the session
void MainWindow::updateInfoLine()
{
    QLocale defaultLocale;

    // Following should always work as we will only receive enter
    // this slot if there is a crawler connected.
    QString current_file
        = QDir::toNativeSeparators( session_.getFilename( currentCrawlerWidget() ) );

    uint64_t fileSize;
    uint64_t fileNbLine;
    QDateTime lastModified;

    session_.getFileInfo( currentCrawlerWidget(), &fileSize, &fileNbLine, &lastModified );

    infoLine->setText( current_file );
    infoLine->setPath( current_file );
    sizeField->setText( readableSize( fileSize ) );
    encodingField->setText( currentCrawlerWidget()->encodingText() );

    if ( lastModified.isValid() ) {
        const QString date = defaultLocale.toString( lastModified, QLocale::NarrowFormat );
        dateField->setText( tr( "modified on %1" ).arg( date ) );
        dateField->show();
    }
    else {
        dateField->hide();
    }
}

void MainWindow::updateOpenedFilesMenu()
{
    openedFilesMenu->clear();

    const auto& files = session_.openedFiles();

    openedFilesMenu->setEnabled( !files.empty() );

    openedFilesMenu->addAction( selectOpenFileAction );
    openedFilesMenu->addSeparator();

    for ( const auto& file : files ) {
        const auto displayFile = DisplayFilePath{ file };
        auto action = openedFilesMenu->addAction( displayFile.displayName() );

        action->setActionGroup( openedFilesGroup );
        action->setToolTip( displayFile.nativeFullPath() );
        action->setData( displayFile.fullPath() );
    }

    selectOpenFileAction->setDisabled( files.empty() );
}

void MainWindow::updateHighlightersMenu()
{
    highlightersMenu->clearHighlightersMenu();
    highlightersMenu->createHighlightersMenu();
    highlightersMenu->addAction( editHighlightersAction, true );
    highlightersMenu->populateHighlightersMenu();
}

void MainWindow::updateFavoritesMenu()
{
    favoritesMenu->clear();

    favoritesMenu->addAction( addToFavoritesMenuAction );
    favoritesMenu->addAction( removeFromFavoritesAction );

    addToFavoritesMenuAction->setIcon( iconLoader_.load( "icons8-star" ) );

    using namespace klogg::mainwindow;

    addToFavoritesAction->setText(
        QApplication::translate( "klogg::mainwindow::action", action::addToFavoritesText ) );
    addToFavoritesAction->setIcon( iconLoader_.load( "icons8-star" ) );
    addToFavoritesAction->setData( true );

    const auto& favorites = FavoriteFiles::getSynced().favorites();
    auto crawler = currentCrawlerWidget();

    addToFavoritesAction->setEnabled( crawler != nullptr );
    addToFavoritesMenuAction->setEnabled( crawler != nullptr );
    removeFromFavoritesAction->setEnabled( !favorites.empty() );

    if ( crawler ) {
        const auto path = session_.getFilename( crawler );
        if ( std::any_of( favorites.begin(), favorites.end(), FullPathComparator( path ) ) ) {

            addToFavoritesAction->setText( QApplication::translate(
                "klogg::mainwindow::action", action::removeFromFavoritesText ) );
            addToFavoritesAction->setIcon( iconLoader_.load( "icons8-star-filled" ) );
            addToFavoritesAction->setData( false );

            addToFavoritesMenuAction->setEnabled( false );
            addToFavoritesMenuAction->setIcon( iconLoader_.load( "icons8-star-filled" ) );
        }
    }

    favoritesMenu->addSeparator();

    for ( const auto& file : favorites ) {
        auto action = favoritesMenu->addAction( file.displayName() );

        action->setActionGroup( favoritesGroup );
        action->setToolTip( file.nativeFullPath() );
        action->setData( file.fullPath() );
    }
}

void MainWindow::addToFavorites()
{
    if ( const auto crawler = currentCrawlerWidget() ) {
        auto& favorites = FavoriteFiles::get();
        const auto path = session_.getFilename( crawler );

        if ( addToFavoritesAction->data().toBool() ) {
            favorites.add( path );
        }
        else {
            favorites.remove( path );
        }

        favorites.save();

        updateFavoritesMenu();
    }
}

void MainWindow::removeFromFavorites()
{
    const auto& favoriteFiles = FavoriteFiles::get();
    const auto& favorites = favoriteFiles.favorites();
    QStringList files;
    std::transform( favorites.cbegin(), favorites.cend(), std::back_inserter( files ),
                    []( const auto& f ) { return f.nativeFullPath(); } );

    auto currentIndex = 0;

    if ( const auto crawler = currentCrawlerWidget() ) {
        const auto currentPath = session_.getFilename( crawler );
        const auto currentItem
            = std::find_if( favorites.begin(), favorites.end(), FullPathComparator( currentPath ) );
        if ( currentItem != favorites.end() ) {
            currentIndex = static_cast<int>( std::distance( favorites.begin(), currentItem ) );
        }
    }

    bool ok = false;
    const auto pathToRemove = QInputDialog::getItem( this, tr( "Remove from favorites" ),
                                                     tr( "Select item to remove from favorites" ),
                                                     files, currentIndex, false, &ok );
    if ( ok ) {
        removeFromFavorites( pathToRemove );
    }
}

void MainWindow::removeFromFavorites( const QString& pathToRemove )
{
    auto& favoriteFiles = FavoriteFiles::get();
    const auto& favorites = favoriteFiles.favorites();
    const auto selectedFile = std::find_if( favorites.begin(), favorites.end(),
                                            [ pathToRemove ]( const DisplayFilePath& f ) {
                                                return f.nativeFullPath() == pathToRemove;
                                            } );

    if ( selectedFile != favorites.end() ) {
        favoriteFiles.remove( selectedFile->fullPath() );
        favoriteFiles.save();
        updateFavoritesMenu();
    }
}

void MainWindow::removeFromRecent( const QString& pathToRemove )
{
    auto& recentFiles = RecentFiles::get();
    recentFiles.removeRecent( pathToRemove );
    recentFiles.save();
    updateRecentFileActions();
}

void MainWindow::selectOpenedFile()
{
    auto openedFilesPaths = session_.openedFiles();
    std::vector<DisplayFilePath> openedFiles;
    openedFiles.reserve( openedFilesPaths.size() );
    std::transform( openedFilesPaths.cbegin(), openedFilesPaths.cend(),
                    std::back_inserter( openedFiles ),
                    []( const auto& path ) { return DisplayFilePath{ path }; } );

    QStringList filesToShow;
    std::transform( openedFiles.cbegin(), openedFiles.cend(), std::back_inserter( filesToShow ),
                    []( const auto& f ) { return f.nativeFullPath(); } );

    auto selectFileDialog = std::make_unique<QDialog>( this );
    selectFileDialog->setWindowTitle( tr( "CILogg -- switch to file" ) );
    selectFileDialog->setMinimumWidth( 800 );
    selectFileDialog->setMinimumHeight( 600 );

    auto filesModel = std::make_unique<QStringListModel>( filesToShow, selectFileDialog.get() );
    auto filteredModel = std::make_unique<QSortFilterProxyModel>( selectFileDialog.get() );
    filteredModel->setSourceModel( filesModel.get() );

    auto filesView = std::make_unique<QListView>();
    filesView->setModel( filteredModel.get() );
    filesView->setEditTriggers( QAbstractItemView::NoEditTriggers );
    filesView->setSelectionMode( QAbstractItemView::SingleSelection );

    auto filterEdit = std::make_unique<QLineEdit>();
    auto buttonBox
        = std::make_unique<QDialogButtonBox>( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );

    connect( buttonBox.get(), &QDialogButtonBox::accepted, selectFileDialog.get(),
             &QDialog::accept );
    connect( buttonBox.get(), &QDialogButtonBox::rejected, selectFileDialog.get(),
             &QDialog::reject );

    connect( filterEdit.get(), &QLineEdit::textEdited,
             [ model = filteredModel.get(), view = filesView.get() ]( const QString& filter ) {
                 model->setFilterWildcard( filter );
                 model->invalidate();
                 view->selectionModel()->select( model->index( 0, 0 ),
                                                 QItemSelectionModel::SelectCurrent );
             } );

    dispatchToObject( [ edit = filterEdit.get() ]() { edit->setFocus(); }, filterEdit.get() );

    connect( selectFileDialog.get(), &QDialog::finished,
             [ this, openedFiles, dialog = selectFileDialog.get(), model = filteredModel.get(),
               view = filesView.get() ]( auto result ) {
                 dialog->deleteLater();
                 if ( result != QDialog::Accepted || !view->selectionModel()->hasSelection() ) {
                     return;
                 }
                 const auto& selectedPath
                     = model->data( view->selectionModel()->selectedIndexes().front() ).toString();
                 const auto selectedFile
                     = std::find_if( openedFiles.begin(), openedFiles.end(),
                                     [ selectedPath ]( const DisplayFilePath& f ) {
                                         return f.nativeFullPath() == selectedPath;
                                     } );

                 if ( selectedFile != openedFiles.end() ) {
                     loadFile( selectedFile->fullPath() );
                 }
             } );

    auto layout = std::make_unique<QVBoxLayout>();
    layout->addWidget( filesView.release() );
    layout->addWidget( filterEdit.release() );
    layout->addWidget( buttonBox.release() );

    selectFileDialog->setLayout( layout.release() );
    selectFileDialog->setModal( true );
    selectFileDialog->open();

    filesModel.release();
    filteredModel.release();
    selectFileDialog.release();
}

void MainWindow::showInfoLabels( bool show )
{
    for ( auto separator : infoToolbarSeparators ) {
        separator->setVisible( show );
    }
    if ( comPortField ) {
        comPortField->setVisible( show && !comPortField->text().isEmpty() );
    }
    if ( !show ) {
        sizeField->clear();
        dateField->clear();
        encodingField->clear();
        lineNbField->clear();
        if ( comPortField ) {
            comPortField->clear();
        }
    }
}

// Write settings to permanent storage
void MainWindow::writeSettings()
{
    // Save the session
    // Generate the ordered list of widgets and their topLine
    std::vector<SaveFileInfo> widget_list;
    for ( int i = 0; i < mainTabWidget_.count(); ++i ) {
        auto view = qobject_cast<const CrawlerWidget*>( mainTabWidget_.widget( i ) );
        if ( view == nullptr ) {
            LOG_WARNING << "Skipping non-crawler tab while saving session at index " << i;
            continue;
        }

        auto context = view->context();
        if ( !context ) {
            LOG_WARNING << "Skipping tab with missing context while saving session at index " << i;
            continue;
        }

        QString streamContext;
        QString scriptContext;
        const auto fileName = session_.getFilename( view );
        if ( auto* streamSession = mainTabWidget_.streamSessionForPath( fileName ) ) {
            streamContext = serializeSerialCaptureSettings( streamSession->captureSettings() );
        }
        scriptContext = scriptContextForTab( i );

        widget_list.emplace_back( view, 0UL, std::move( context ), streamContext, scriptContext );
    }
    SessionInfo::getSynced().setGlobalScriptContext( globalScriptContext() );
    session_.save( widget_list, saveGeometry() );
}

// Read settings from permanent storage
void MainWindow::readSettings()
{
    // Get and restore the session
    // auto& session = SessionInfo::getSynced();
    /*
     * FIXME: should be in the session
    crawlerWidget->restoreState( session.crawlerState() );
    */

    // History of recent files
    StartupProgress::advance( tr( "Loading recent files" ), tr( "Restoring recent files list" ) );
    RecentFiles::getSynced();
    updateRecentFileActions();

    StartupProgress::advance( tr( "Loading favorites" ), tr( "Restoring favorite files list" ) );
    FavoriteFiles::getSynced();
    updateFavoritesMenu();

    StartupProgress::advance( tr( "Loading highlighters" ),
                              tr( "Restoring and compiling highlighter sets" ) );
    auto& highlighterCollection = HighlighterSetCollection::getSynced();
    auto& highlighterSets = highlighterCollection.highlighterSets();
    for ( auto& highlighterSet : highlighterSets ) {
        StartupProgress::advance( tr( "Loading highlighter set" ), highlighterSet.name() );
        auto& highlighters = highlighterSet.highlighters();
        for ( auto& highlighter : highlighters ) {
            const auto highlighterName = highlighter.pattern().isEmpty()
                                             ? tr( "<empty pattern>" )
                                             : highlighter.pattern();
            StartupProgress::advance( tr( "Compiling highlighter" ), highlighterName );
            highlighter.compile();
        }
        StartupProgress::advance( tr( "Compiling highlighter set" ), highlighterSet.name() );
        highlighterSet.compile();
    }
    updateHighlightersMenu();

    StartupProgress::advance( tr( "Loading predefined filters" ),
                              tr( "Restoring predefined filters" ) );
    auto& predefinedFiltersCollection = PredefinedFiltersCollection::getSynced();
    const auto predefinedFilters = predefinedFiltersCollection.getFilters();
    for ( const auto& filter : predefinedFilters ) {
        const auto filterName
            = filter.name.isEmpty() ? filter.pattern.left( 64 ) : filter.name;
        StartupProgress::advance( tr( "Loading predefined filter" ), filterName );
        if ( filter.useRegex ) {
            QRegularExpression expression( filter.pattern,
                                           QRegularExpression::UseUnicodePropertiesOption );
            if ( !expression.isValid() ) {
                LOG_WARNING << "Invalid predefined filter regex " << filterName << ": "
                            << expression.errorString();
            }
        }
    }
}

void MainWindow::displayQuickFindBar( QuickFindMux::QFDirection direction )
{
    LOG_DEBUG << "MainWindow::displayQuickFindBar";

    // Warn crawlers so they can save the position of the focus in order
    // to do incremental search in the right view.
    Q_EMIT enteringQuickFind();

    const auto crawler = currentCrawlerWidget();
    if ( crawler != nullptr && crawler->isPartialSelection() ) {
        auto selection = crawler->getSelectedText();
        if ( !selection.isEmpty() ) {
            quickFindWidget_.changeDisplayedPattern( selection, false );
        }
    }

    quickFindMux_.setDirection( direction );
    quickFindWidget_.userActivate();
}

void MainWindow::logScreenInfo( QScreen* screen )
{
    LOG_INFO << "screen changed for " << session_.windowIndex();
    if ( screen == nullptr ) {
        return;
    }

    LOG_INFO << "screen name " << screen->name();
    LOG_INFO << "screen size " << screen->size().width() << "x" << screen->size().height();
    LOG_INFO << "screen ratio " << screen->devicePixelRatio();
    LOG_INFO << "screen logical dpi " << screen->logicalDotsPerInch();
    LOG_INFO << "screen physical dpi " << screen->physicalDotsPerInch();
}

void MainWindow::generateDump()
{
    const auto userAction = QMessageBox::warning(
        this, tr( "CILogg - generate crash dump" ),
        tr( "This will shutdown CILogg and generate diagnostic crash dump. Continue?" ),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

    if ( userAction == QMessageBox::Yes ) {
        throw std::logic_error( "test dump" );
    }
}
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
