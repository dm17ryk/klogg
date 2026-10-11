#include "configuration.h"
#include "mainwindow.h"
#include "pluginmanager.h"
#include "session.h"
#include <QAction>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <catch2/catch.hpp>

TEST_CASE( "Plugin UI routes panels to captured windows and cleans them up", "[ui][plugins]" )
{
    QTemporaryDir temp;
    struct RestoreTraySetting {
        bool previous = Configuration::get().minimizeToTray();
        ~RestoreTraySetting()
        {
            Configuration::getSynced().setMinimizeToTray( previous );
        }
    } restoreTray;
    Configuration::getSynced().setMinimizeToTray( false );
    cilogg::plugins::PluginManager manager( temp.path(), temp.path() );
    auto appSession = std::make_shared<Session>();
    MainWindow first( WindowSession{ appSession, "PluginsFirst", 0 } );
    MainWindow second( WindowSession{ appSession, "PluginsSecond", 1 } );
    const auto executor = []( const CommanderRequest& ) { return CommanderResult{}; };
    first.setPluginManager( manager, executor );
    second.setPluginManager( manager, executor );
    first.show();
    second.show();
    const auto context = second.pluginContext( {} );
    manager.panelUpdated(
        "fixture",
        { { "id", "summary" },
          { "title", "Summary" },
          { "elements", QJsonArray{ QJsonObject{ { "type", "text" }, { "text", "Second window" } },
                                    QJsonObject{ { "type", "progress" }, { "value", 1e100 } } } } },
        context );
    QElapsedTimer wait;
    wait.start();
    while ( !second.findChild<QDockWidget*>( "pluginPanel_fixture_summary" )
            && wait.elapsed() < 2000 ) {
        QTest::qWait( 10 );
    }
    REQUIRE( second.findChild<QDockWidget*>( "pluginPanel_fixture_summary" ) );
    REQUIRE_FALSE( first.findChild<QDockWidget*>( "pluginPanel_fixture_summary" ) );
    QJsonArray columns;
    QJsonArray rows;
    for ( int i = 0; i < 64; ++i ) {
        columns.append( "Column" );
    }
    for ( int i = 0; i < 1000; ++i ) {
        rows.append( QJsonArray{} );
    }
    manager.panelUpdated(
        "fixture",
        { { "id", "large" },
          { "elements", QJsonArray{ QJsonObject{
                            { "type", "table" }, { "rows", rows }, { "columns", columns } } } } },
        context );
    QTest::qWait( 150 );
    auto* bounded = second.findChild<QDockWidget*>( "pluginPanel_fixture_large" );
    REQUIRE( bounded );
    REQUIRE( bounded->findChildren<QTableWidget*>().isEmpty() );
    auto* manage = first.findChild<QAction*>( "managePluginsAction" );
    auto* browse = first.findChild<QAction*>( "browsePluginsAction" );
    REQUIRE( manage );
    REQUIRE( browse );
    auto* toolbar = first.findChild<QToolBar*>( "mainToolBar" );
    REQUIRE( toolbar );
    REQUIRE( toolbar->actions().contains( manage ) );
    REQUIRE( toolbar->actions().contains( browse ) );
    REQUIRE_FALSE( toolbar->widgetForAction( manage )->accessibleName().isEmpty() );
    manager.pluginStopped( "fixture" );
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    REQUIRE_FALSE( second.findChild<QDockWidget*>( "pluginPanel_fixture_summary" ) );
    second.close();
    manager.panelUpdated( "fixture", { { "id", "late" }, { "elements", QJsonArray{} } }, context );
    QTest::qWait( 150 );
    REQUIRE_FALSE( second.findChild<QDockWidget*>( "pluginPanel_fixture_late" ) );
}
