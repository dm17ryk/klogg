#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <algorithm>

#include "configuration.h"
#include "configurationexportdialog.h"
#include "crawlerwidget.h"
#include "log.h"
#include "logger.h"
#include "persistentinfo.h"
#include "serialcaptureworker.h"

namespace {
QString ensureSuffix( QString path, const QString& suffix )
{
    if ( !path.endsWith( suffix, Qt::CaseInsensitive ) ) {
        path += suffix;
    }
    return path;
}
} // namespace

void MainWindow::createConfigurationActions()
{
    connect( this, &MainWindow::windowClosed, this, [ this ] { projectWindowActive_ = false; } );
    auto* save = new QAction( tr( "Save project…" ), this );
    save->setObjectName( QStringLiteral( "saveProjectAction" ) );
    save->setStatusTip( tr( "Save the current session and all configurations" ) );
    fileMenu->insertAction( closeAction, save );
    auto* load = new QAction( tr( "Load project…" ), this );
    load->setObjectName( QStringLiteral( "loadProjectAction" ) );
    fileMenu->insertAction( closeAction, load );
    fileMenu->insertSeparator( closeAction );
    connect( save, &QAction::triggered, this, [ this ] {
        auto path = QFileDialog::getSaveFileName( this, tr( "Save project" ), projectPath_,
                                                  tr( "CILogg projects (*.cilogproj)" ) );
        if ( path.isEmpty() ) {
            LOG_DEBUG << "Project save cancelled";
            return;
        }
        path = ensureSuffix( path, ".cilogproj" );
        QString error;
        if ( !saveProject( path, &error ) ) {
            QMessageBox::warning( this, tr( "Save project" ), error );
        }
    } );
    connect( load, &QAction::triggered, this, [ this ] {
        const auto path = QFileDialog::getOpenFileName( this, tr( "Load project" ), projectPath_,
                                                        tr( "CILogg projects (*.cilogproj)" ) );
        if ( path.isEmpty() ) {
            LOG_DEBUG << "Project load cancelled";
            return;
        }
        QString error;
        if ( !loadProject( path, &error ) ) {
            QMessageBox::warning( this, tr( "Load project" ), error );
        }
    } );
    auto* menu = toolsMenu->addMenu( tr( "Export configurations" ) );
    menu->setObjectName( QStringLiteral( "exportConfigurationsMenu" ) );
    const std::pair<ConfigurationExportKind, const char*> kinds[]
        = { { ConfigurationExportKind::Actions, "exportActionsAction" },
            { ConfigurationExportKind::Highlights, "exportHighlightsAction" },
            { ConfigurationExportKind::Previews, "exportPreviewsAction" },
            { ConfigurationExportKind::PredefinedFilters, "exportPredefinedFiltersAction" },
            { ConfigurationExportKind::Preferences, "exportPreferencesAction" } };
    for ( const auto& entry : kinds ) {
        auto* action = menu->addAction( ConfigurationExport::title( entry.first ) + "…" );
        action->setObjectName( QString::fromLatin1( entry.second ) );
        connect( action, &QAction::triggered, this, [ this, kind = entry.first ] {
            exportConfiguration( static_cast<int>( kind ) );
        } );
    }
}

void MainWindow::exportConfiguration( int value )
{
    const auto kind = static_cast<ConfigurationExportKind>( value );
    QString error;
    ConfigurationProject snapshot;
    if ( !ConfigurationExport::captureConfiguration( &snapshot, &error ) ) {
        QMessageBox::warning( this, ConfigurationExport::title( kind ), error );
        return;
    }
    QVector<int> rows;
    bool multiple = false;
    if ( kind != ConfigurationExportKind::Preferences ) {
        ConfigurationExportDialog dialog( kind, ConfigurationExport::names( kind, &snapshot ),
                                          this );
        if ( dialog.exec() != QDialog::Accepted ) {
            LOG_DEBUG << "Configuration export cancelled: kind=" << value;
            return;
        }
        rows = dialog.selectedRows();
        multiple = dialog.multipleFiles();
    }
    QStringList savedFiles;
    bool saved = false;
    if ( multiple ) {
        const auto directory
            = QFileDialog::getExistingDirectory( this, tr( "Export highlight groups" ) );
        if ( directory.isEmpty() ) {
            LOG_DEBUG << "Highlight directory selection cancelled";
            return;
        }
        saved = ConfigurationExport::exportHighlightFiles( snapshot.highlights, rows, directory,
                                                           &error, &savedFiles );
    }
    else {
        auto path = QFileDialog::getSaveFileName( this, ConfigurationExport::title( kind ), {},
                                                  ConfigurationExport::fileFilter( kind ) );
        if ( path.isEmpty() ) {
            LOG_DEBUG << "Configuration file selection cancelled";
            return;
        }
        path = ensureSuffix( path, ConfigurationExport::suffix( kind ) );
        saved = ConfigurationExport::exportSelected( kind, rows, path, &error, nullptr, &snapshot );
        if ( saved ) {
            savedFiles.append( path );
        }
    }
    for ( const auto& path : savedFiles ) {
        registeredExports_.insert( QFileInfo( path ).absoluteFilePath(),
                                   ConfigurationExport::title( kind ) );
    }
    if ( !saved ) {
        QMessageBox::warning( this, ConfigurationExport::title( kind ), error );
    }
    else {
        statusBar()->showMessage(
            tr( "Exported %1 configuration file(s)" ).arg( savedFiles.size() ), 6000 );
    }
}

bool MainWindow::saveProject( const QString& path, QString* errorMessage )
{
    SessionInfo snapshot;
    QMap<QString, int> activeTabs;
    auto exports = registeredExports_;
    QList<MainWindow*> windows{ this };
    for ( auto* widget : QApplication::topLevelWidgets() ) {
        auto* window = qobject_cast<MainWindow*>( widget );
        if ( window && window != this && window->projectWindowActive_ ) {
            windows.append( window );
        }
    }
    for ( auto* window : windows ) {
        window->writeSettings();
        const auto& session = SessionInfo::getSynced();
        const auto id = window->session_.windowId();
        snapshot.add( id );
        snapshot.setOpenFiles( id, session.openFiles( id ) );
        snapshot.setGeometry( id, session.geometry( id ) );
        activeTabs.insert( id, window->mainTabWidget_.currentIndex() );
        for ( auto it = window->registeredExports_.cbegin();
              it != window->registeredExports_.cend(); ++it ) {
            exports.insert( it.key(), it.value() );
        }
        LOG_DEBUG << "Project session captured: window=" << id
                  << " tabs=" << window->mainTabWidget_.count();
    }
    snapshot.setGlobalScriptContext( SessionInfo::getSynced().globalScriptContext() );
    if ( !ConfigurationExport::saveProject( path, snapshot, activeTabs, exports, errorMessage ) ) {
        return false;
    }
    projectPath_ = QFileInfo( path ).absoluteFilePath();
    statusBar()->showMessage( tr( "Project saved: %1" ).arg( projectPath_ ), 6000 );
    return true;
}

bool MainWindow::loadProject( const QString& path, QString* errorMessage )
{
    ConfigurationProject project;
    if ( !ConfigurationExport::readProject( path, &project, errorMessage ) ) {
        return false;
    }
    QList<MainWindow*> windows{ this };
    for ( auto* widget : QApplication::topLevelWidgets() ) {
        auto* window = qobject_cast<MainWindow*>( widget );
        if ( window && window != this && window->projectWindowActive_ ) {
            windows.append( window );
        }
    }
    const auto ids = project.session.windows();
    const auto previousCount = windows.size();
    const auto cleanupAdded = [ & ] {
        for ( auto i = previousCount; i < windows.size(); ++i ) {
            auto* window = windows.at( i );
            window->isCloseFromTray_ = true;
            window->close();
            LOG_DEBUG << "Closed newly added project window after load failure";
        }
    };
    // The application's existing newWindow connection owns additional windows and their lifecycle.
    while ( windows.size() < ids.size() ) {
        Q_EMIT newWindow();
        MainWindow* added = nullptr;
        for ( auto* widget : QApplication::topLevelWidgets() ) {
            auto* window = qobject_cast<MainWindow*>( widget );
            if ( window && window->projectWindowActive_ && !windows.contains( window ) ) {
                added = window;
                break;
            }
        }
        if ( !added ) {
            if ( errorMessage ) {
                *errorMessage = tr( "Cannot create the project's additional window." );
            }
            LOG_ERROR << "Project window creation failed";
            cleanupAdded();
            return false;
        }
        windows.append( added );
    }
    if ( !ConfigurationExport::applyProjectConfiguration( project, errorMessage ) ) {
        cleanupAdded();
        return false;
    }
    // Close the old session only after all referenced configurations have validated and persisted.
    for ( auto* window : windows ) {
        window->closeAll( ActionInitiator::App );
    }
    for ( auto i = ids.size(); i < windows.size(); ++i ) {
        auto* window = windows.at( i );
        window->isCloseFromTray_ = true;
        window->close();
        LOG_DEBUG << "Closed surplus window while replacing project";
    }
    SessionInfo stored;
    stored.setGlobalScriptContext( project.session.globalScriptContext() );
    const auto projectDir = QFileInfo( path ).absoluteDir();
    for ( int i = 0; i < ids.size(); ++i ) {
        auto* window = windows.at( i );
        const auto targetId = window->session_.windowId();
        stored.add( targetId );
        auto files = project.session.openFiles( ids.at( i ) );
        // Relative log paths are resolved against the project, while existing absolute paths are
        // retained.
        for ( auto& file : files ) {
            if ( QDir::isRelativePath( file.fileName ) ) {
                file.fileName = projectDir.absoluteFilePath( file.fileName );
                LOG_DEBUG << "Resolved project log path: " << file.fileName;
            }
            auto settings = deserializeSerialCaptureSettings( file.streamContext );
            if ( settings && QDir::isRelativePath( settings->filePath ) ) {
                settings->filePath = projectDir.absoluteFilePath( settings->filePath );
                file.streamContext = serializeSerialCaptureSettings( *settings );
                LOG_DEBUG << "Resolved project COM capture path: " << settings->filePath;
            }
        }
        stored.setOpenFiles( targetId, files );
        stored.setGeometry( targetId, project.session.geometry( ids.at( i ) ) );
    }
    stored.save();
    SessionInfo::getSynced();
    for ( int i = 0; i < ids.size(); ++i ) {
        auto* window = windows.at( i );
        window->reloadGeometry();
        const int current = project.activeTabs.value( ids.at( i ), -1 );
        window->reloadSession( current );
        window->registeredExports_ = project.exports;
        window->projectPath_ = QFileInfo( path ).absoluteFilePath();
        window->updateHighlightersMenu();
        window->updateShortcuts();
        window->updateRecentFileActions();
        window->newWindowAction->setVisible( Configuration::get().allowMultipleWindows() );
        window->followAction->setEnabled( Configuration::get().anyFileWatchEnabled() );
        window->showTabsBarAction->setChecked( Configuration::get().showTabsBarByDefault() );
        window->mainTabWidget_.setAlwaysShowTabBar( Configuration::get().showTabsBarByDefault() );
        window->overviewVisibleAction->setChecked( Configuration::get().isOverviewVisible() );
        window->lineNumbersVisibleInMainAction->setChecked(
            Configuration::get().mainLineNumbersVisible() );
        window->lineNumbersVisibleInFilteredAction->setChecked(
            Configuration::get().filteredLineNumbersVisible() );
        for ( int tab = 0; tab < window->mainTabWidget_.count(); ++tab ) {
            if ( auto* crawler
                 = qobject_cast<CrawlerWidget*>( window->mainTabWidget_.widget( tab ) ) ) {
                crawler->applyConfiguration();
            }
        }
        LOG_INFO << "Project window restored: " << ids.at( i )
                 << " tabs=" << window->mainTabWidget_.count();
    }
    if ( qApp->metaObject()->indexOfMethod( "restoreGlobalScriptBindingVariant(QVariant)" ) >= 0 ) {
        QVariantMap binding;
        const auto context = project.session.globalScriptContext();
        if ( !context.trimmed().isEmpty() ) {
            const auto document = QJsonDocument::fromJson( context.toUtf8() );
            if ( document.isObject() ) {
                binding = document.object().toVariantMap();
            }
        }
        // Unlike the startup helper, this replaces the old global worker on every project load.
        QMetaObject::invokeMethod( qApp, "restoreGlobalScriptBindingVariant", Qt::DirectConnection,
                                   Q_ARG( QVariant, QVariant::fromValue( binding ) ) );
        LOG_DEBUG << "Replaced global project script binding: present=" << !binding.isEmpty();
    }
    logging::enableFileLogging(
        Configuration::get().enableLogging(),
        static_cast<logging::LogLevel>( Configuration::get().loggingLevel() ) );
    statusBar()->showMessage( tr( "Project loaded: %1" ).arg( path ), 6000 );
    return true;
}
