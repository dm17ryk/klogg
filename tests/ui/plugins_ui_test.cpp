#include "configuration.h"
#include "mainwindow.h"
#include "pluginmanager.h"
#include "session.h"
#include <QAction>
#include <QDialog>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <catch2/catch.hpp>

TEST_CASE( "Plugin dialogs preserve secrets and route storage and catalog selections",
           "[ui][plugins]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    const auto folder = QDir( shipped ).filePath( "org.test.ui" );
    REQUIRE( QDir().mkpath( folder ) );
    QFile manifest( QDir( folder ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    const QJsonObject description{
        { "id", "org.test.ui" },
        { "name", "UI fixture" },
        { "version", "1.0.0" },
        { "protocol_version", 1 },
        { "executable", "unused.exe" },
        { "capabilities", QJsonArray{ "tool" } },
        { "actions", QJsonArray{} },
        { "configuration",
          QJsonArray{ QJsonObject{ { "id", "token" }, { "type", "secret" } },
                      QJsonObject{ { "id", "count" }, { "type", "integer" }, { "default", 1 } } } }
    };
    REQUIRE( manifest.write( QJsonDocument( description ).toJson() ) > 0 );
    manifest.close();
    cilogg::plugins::PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
    REQUIRE( manager
                 .manage( "configure", "org.test.ui",
                          { { "configuration", QVariantMap{ { "token", "private" } } } } )
                 .ok() );
    auto session = std::make_shared<Session>();
    MainWindow window( WindowSession{ session, "PluginForms", 0 } );
    CommanderRequest captured;
    window.setPluginManager( manager, [ &captured ]( const CommanderRequest& request ) {
        captured = request;
        return CommanderResult{};
    } );
    window.show();
    window.findChild<QAction*>( "managePluginsAction" )->trigger();
    auto* manage = window.findChild<QDialog*>( "pluginManagerDialog" );
    REQUIRE( manage );
    auto* table = manage->findChild<QTableWidget*>( "pluginManagerTable" );
    REQUIRE( table->rowCount() == 1 );
    table->selectRow( 0 );
    bool masked = false;
    QTimer::singleShot( 0, manage, [ & ] {
        auto* form = manage->findChild<QDialog*>( "pluginFormDialog" );
        auto* token = form->findChild<QLineEdit*>( "pluginField_token" );
        masked = token->echoMode() == QLineEdit::Password && token->text().isEmpty();
        form->findChild<QLineEdit*>( "pluginField_count" )->setText( "2" );
        form->findChild<QPushButton*>( "pluginFormAccept" )->click();
    } );
    manage->findChild<QPushButton*>( "pluginManage_configure" )->click();
    REQUIRE( masked );
    REQUIRE_FALSE( captured.pluginArguments[ "configuration" ].toMap().contains( "token" ) );
    REQUIRE( captured.pluginArguments[ "configuration" ].toMap()[ "count" ] == 2 );
    QTimer::singleShot( 0, manage, [ & ] {
        auto* form = manage->findChild<QDialog*>( "pluginFormDialog" );
        form->findChild<QAction*>( "clearPluginSecret_token" )->trigger();
        form->findChild<QPushButton*>( "pluginFormAccept" )->click();
    } );
    manage->findChild<QPushButton*>( "pluginManage_configure" )->click();
    REQUIRE( captured.pluginArguments[ "configuration" ].toMap().contains( "token" ) );
    REQUIRE( captured.pluginArguments[ "configuration" ].toMap()[ "token" ].toString().isEmpty() );
    QTimer::singleShot( 0, manage, [ & ] {
        auto* form = manage->findChild<QDialog*>( "pluginFormDialog" );
        form->findChild<QLineEdit*>( "pluginField_max_capture_bytes" )->setText( "4096" );
        form->findChild<QPushButton*>( "pluginFormAccept" )->click();
    } );
    manage->findChild<QPushButton*>( "pluginStorageLimits" )->click();
    REQUIRE( captured.action == CommanderAction::PluginManage );
    REQUIRE( captured.pluginOperation == "configure" );
    REQUIRE( captured.pluginId.isEmpty() );
    REQUIRE( captured.pluginArguments[ "storage_limits" ].toMap()[ "max_capture_bytes" ] == 4096 );
    REQUIRE( manage->findChild<QPushButton*>( "pluginCapturesFolder" ) );
    window.findChild<QAction*>( "browsePluginsAction" )->trigger();
    auto* browse = window.findChild<QDialog*>( "pluginCatalogDialog" );
    REQUIRE( browse );
    auto* catalog = browse->findChild<QTableWidget*>( "pluginCatalogTable" );
    catalog->setRowCount( 2 );
    for ( int row = 0; row < 2; ++row ) {
        auto* item = new QTableWidgetItem( "UI fixture" );
        item->setData( Qt::UserRole, "org.test.ui" );
        item->setData( Qt::UserRole + 1, QString::number( row ) );
        catalog->setItem( row, 0, item );
    }
    catalog->selectRow( 1 );
    browse->findChild<QPushButton*>( "installCatalogPlugin" )->click();
    REQUIRE( captured.pluginId == "org.test.ui" );
    REQUIRE( captured.pluginArguments[ "catalog_entry_id" ] == "1" );
    browse->close();
    manage->close();
}

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
    wait.restart();
    while ( !second.findChild<QDockWidget*>( "pluginPanel_fixture_large" )
            && wait.elapsed() < 2000 ) {
        QTest::qWait( 10 );
    }
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
