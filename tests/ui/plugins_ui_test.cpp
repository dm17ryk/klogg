#include "configuration.h"
#include "mainwindow.h"
#include "pluginmanager.h"
#include "session.h"
#include <QAction>
#include <QDialog>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenu>
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

TEST_CASE( "Plugin actions and views honour toolbar, menu and icon manifest keys",
           "[ui][plugins][E14]" )
{
    QTemporaryDir temp;
    const QString pluginId = "org.test.e14";
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    const auto folder = QDir( shipped ).filePath( pluginId );
    REQUIRE( QDir().mkpath( folder ) );
    const auto executable = QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName();
    REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                          QDir( folder ).filePath( executable ) ) );
    QImage image( 16, 16, QImage::Format_ARGB32 );
    image.fill( Qt::red );
    REQUIRE( image.save( QDir( folder ).filePath( "icon.png" ) ) );
    QFile broken( QDir( folder ).filePath( "broken.png" ) );
    REQUIRE( broken.open( QIODevice::WriteOnly ) );
    broken.write( "not a png" );
    broken.close();
    const QJsonObject description{
        { "id", pluginId },
        { "name", "E14 fixture" },
        { "version", "1.0.0" },
        { "protocol_version", 1 },
        { "executable", executable },
        { "capabilities", QJsonArray{ "tool" } },
        { "actions",
          QJsonArray{ QJsonObject{ { "id", "a1" },
                                   { "capability", "tool" },
                                   { "toolbar", false },
                                   { "menu", false } },
                      QJsonObject{ { "id", "a2" }, { "capability", "tool" } } } },
        { "views",
          QJsonArray{ QJsonObject{ { "id", "main" },
                                   { "title", "Main" },
                                   { "toolbar", true },
                                   { "icon", "icon.png" } },
                      QJsonObject{ { "id", "other" },
                                   { "title", "Other" },
                                   { "icon", "broken.png" } } } }
    };
    QFile manifest( QDir( folder ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    REQUIRE( manifest.write( QJsonDocument( description ).toJson() ) > 0 );
    manifest.close();
    cilogg::plugins::PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
    REQUIRE( manager.manage( "enable", pluginId, {} ).ok() );
    auto session = std::make_shared<Session>();
    MainWindow window( WindowSession{ session, "PluginE14", 0 } );
    window.setPluginManager( manager, []( const CommanderRequest& ) { return CommanderResult{}; } );
    window.show();

    auto* toolbar = window.findChild<QToolBar*>( "mainToolBar" );
    auto* menu = window.findChild<QMenu*>( "pluginsMenu" );
    REQUIRE( toolbar );
    REQUIRE( menu );
    REQUIRE( QTest::qWaitFor(
        [ & ] { return window.findChild<QAction*>( "pluginViewAction_org.test.e14_main" ); },
        5000 ) );
    auto* a1 = window.findChild<QAction*>( "pluginAction_org.test.e14_a1" );
    auto* a2 = window.findChild<QAction*>( "pluginAction_org.test.e14_a2" );
    auto* main = window.findChild<QAction*>( "pluginViewAction_org.test.e14_main" );
    auto* other = window.findChild<QAction*>( "pluginViewAction_org.test.e14_other" );
    CHECK( a1 == nullptr ); // neither toolbar nor menu: no QAction at all
    REQUIRE( a2 );
    CHECK( toolbar->actions().contains( a2 ) );
    CHECK( menu->actions().contains( a2 ) );
    // QStyle::standardIcon() builds a new QIcon (a new cacheKey) on every call: compare pixels.
    const auto generic
        = window.style()->standardIcon( QStyle::SP_TitleBarNormalButton ).pixmap( 16, 16 ).toImage();
    REQUIRE_FALSE( generic.isNull() );
    REQUIRE( main );
    CHECK( toolbar->actions().contains( main ) );
    CHECK( menu->actions().contains( main ) );
    CHECK_FALSE( main->icon().pixmap( 16, 16 ).isNull() );
    CHECK( main->icon().pixmap( 16, 16 ).toImage() != generic );   // the plugin's icon.png
    REQUIRE( other );
    CHECK_FALSE( toolbar->actions().contains( other ) ); // views default to toolbar:false
    CHECK_FALSE( other->icon().isNull() );               // broken file: generic icon, not empty
    CHECK( other->icon().pixmap( 16, 16 ).toImage() == generic );  // the fallback is the generic icon
}
