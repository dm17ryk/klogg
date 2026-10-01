#include <catch2/catch.hpp>

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QMenu>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "configurationexportdialog.h"
#include "crawlerwidget.h"
#include "mainwindow.h"
#include "session.h"
#include "sessioninfo.h"
#include "tabbedcrawlerwidget.h"

namespace {
struct SessionRestoreGuard {
    SessionInfo previous = SessionInfo::getSynced();
    ~SessionRestoreGuard()
    {
        previous.save();
        SessionInfo::getSynced();
    }
};
bool waitForLoaded( MainWindow& window )
{
    for ( int i = 0; i < 100; ++i ) {
        if ( window.isStartupReadyForDisplay() ) {
            return true;
        }
        QTest::qWait( 20 );
    }
    return false;
}
} // namespace

TEST_CASE( "Main window exposes each configuration export and project actions",
           "[ui][configurationexport]" )
{
    SessionRestoreGuard restore;
    auto session = std::make_shared<Session>();
    MainWindow window( WindowSession( session, "ProjectMenus", 0 ) );
    window.show();
    REQUIRE( window.findChild<QMenu*>( "exportConfigurationsMenu" ) );
    for ( const auto* name : { "saveProjectAction", "loadProjectAction", "exportActionsAction",
                               "exportHighlightsAction", "exportPreviewsAction",
                               "exportPredefinedFiltersAction", "exportPreferencesAction" } ) {
        const auto* action = window.findChild<QAction*>( name );
        REQUIRE( action );
        REQUIRE( action->isEnabled() );
    }
    bool dialogOpened = false;
    QTimer::singleShot( 0, [ & ] {
        auto* dialog
            = dynamic_cast<ConfigurationExportDialog*>( QApplication::activeModalWidget() );
        if ( dialog ) {
            dialogOpened = true;
            dialog->reject();
        }
    } );
    window.findChild<QAction*>( "exportActionsAction" )->trigger();
    REQUIRE( dialogOpened );
}

TEST_CASE( "Project loading restores tabs active selection and rejects missing references before "
           "closing tabs",
           "[ui][configurationexport][project]" )
{
    SessionRestoreGuard restore;
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    for ( const auto* name : { "first.log", "second.log" } ) {
        QFile file( directory.filePath( name ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( "first line\nsecond line\n" ) > 0 );
    }
    auto session = std::make_shared<Session>();
    MainWindow window( WindowSession( session, "ProjectTabs", 0 ) );
    window.show();
    auto* tabs = window.findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabs );
    window.loadFileNonInteractive( directory.filePath( "first.log" ) );
    window.loadFileNonInteractive( directory.filePath( "second.log" ) );
    REQUIRE( waitForLoaded( window ) );
    REQUIRE( tabs->count() == 2 );
    tabs->setCurrentIndex( 0 );
    const auto path = directory.filePath( "session.cilogproj" );
    QString error;
    REQUIRE( window.saveProject( path, &error ) );
    CommanderRequest request;
    request.action = CommanderAction::CloseAll;
    REQUIRE( window.executeCommanderRequest( request ).ok() );
    REQUIRE( tabs->count() == 0 );
    INFO( error.toStdString() );
    REQUIRE( window.loadProject( path, &error ) );
    REQUIRE( waitForLoaded( window ) );
    REQUIRE( tabs->count() == 2 );
    REQUIRE( tabs->currentIndex() == 0 );
    const auto info = window.commanderWindowInfo().value( "tabs" ).toList();
    REQUIRE( info.size() == 2 );
    REQUIRE( info.at( 0 ).toMap().value( "filePath" ).toString()
             == directory.filePath( "first.log" ) );
    REQUIRE_FALSE( window.loadProject( directory.filePath( "missing.cilogproj" ), &error ) );
    REQUIRE( tabs->count() == 2 );
}

TEST_CASE( "Projects exclude closed windows and close surplus windows when loaded",
           "[ui][configurationexport][project]" )
{
    SessionRestoreGuard restore;
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    auto session = std::make_shared<Session>();
    MainWindow first( WindowSession( session, "ProjectActive", 0 ) );
    first.show();
    MainWindow closed( WindowSession( session, "ProjectClosed", 1 ) );
    closed.show();
    closed.hide();
    REQUIRE( closed.close() );
    const auto path = directory.filePath( "one-window.cilogproj" );
    QString error;
    REQUIRE( first.saveProject( path, &error ) );
    ConfigurationProject project;
    REQUIRE( ConfigurationExport::readProject( path, &project, &error ) );
    REQUIRE( project.session.windows().size() == 1 );
    MainWindow surplus( WindowSession( session, "ProjectSurplus", 2 ) );
    surplus.show();
    REQUIRE( first.loadProject( path, &error ) );
    REQUIRE_FALSE( surplus.isVisible() );
    REQUIRE( first.saveProject( path, &error ) );
    REQUIRE( ConfigurationExport::readProject( path, &project, &error ) );
    REQUIRE( project.session.windows().size() == 1 );
}

TEST_CASE( "Project loading creates and restores additional saved windows",
           "[ui][configurationexport][project]" )
{
    SessionRestoreGuard restore;
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    auto session = std::make_shared<Session>();
    MainWindow first( WindowSession( session, "ProjectFirst", 0 ) );
    first.show();
    MainWindow second( WindowSession( session, "ProjectSecond", 1 ) );
    second.show();
    const auto logPath = directory.filePath( "second-window.log" );
    {
        QFile file( logPath );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( "restored in another window\n" ) > 0 );
    }
    second.loadFileNonInteractive( logPath );
    REQUIRE( waitForLoaded( second ) );
    const auto path = directory.filePath( "two-windows.cilogproj" );
    QString error;
    REQUIRE( first.saveProject( path, &error ) );
    second.hide();
    REQUIRE( second.close() );
    std::unique_ptr<MainWindow> added;
    QObject::connect( &first, &MainWindow::newWindow, &first, [ & ] {
        added = std::make_unique<MainWindow>( WindowSession( session, "ProjectAdded", 2 ) );
        added->show();
    } );
    REQUIRE( first.loadProject( path, &error ) );
    REQUIRE( added );
    REQUIRE( waitForLoaded( *added ) );
    const auto info = added->commanderWindowInfo().value( "tabs" ).toList();
    REQUIRE( info.size() == 1 );
    REQUIRE( info.front().toMap().value( "filePath" ).toString() == logPath );
    ConfigurationProject project;
    REQUIRE( first.saveProject( path, &error ) );
    REQUIRE( ConfigurationExport::readProject( path, &project, &error ) );
    REQUIRE( project.session.windows().size() == 2 );
}
