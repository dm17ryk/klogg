// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// E17a: plain-text and advanced tooltips, accessible names, icons and get_plugin_tooltip
// (docs/plugin-sdk-advanced.md sections 5, 6, 9, 12.4 and 15).
#include "mainwindow.h"
#include "pluginicons.h"
#include "pluginmanager.h"
#include "plugintablemodel.h"
#include "pluginview.h"
#include "pluginviewhost.h"
#include "session.h"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHelpEvent>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QToolTip>
#include <catch2/catch.hpp>
#include <memory>

using cilogg::plugins::PluginManager;

namespace {
const QString PluginId = QStringLiteral( "org.test.e17" );
// The host fails a plugin that is not ready 10 s after its start (PluginProcess); waiting longer
// than that means the host's verdict ends the wait, never this timeout.
constexpr int PluginStartWaitMs = 15000;

bool waitFor( const std::function<bool()>& predicate, int timeout = 5000 )
{
    return QTest::qWaitFor( predicate, timeout );
}

QJsonObject e17Manifest( const QString& executable )
{
    return {
        { "id", PluginId },
        { "name", "E17 fixture" },
        { "version", "1.0.0" },
        { "protocol_version", 1 },
        { "executable", executable },
        { "capabilities", QJsonArray{ "tool" } },
        { "handles", QJsonArray{ "tooltip/resolve", "query/cancel" } },
        { "actions",
          QJsonArray{ QJsonObject{ { "id", "summary" },
                                   { "capability", "tool" },
                                   { "icon_ref", "builtin:play" } },
                      QJsonObject{ { "id", "icons" }, { "capability", "tool" }, { "menu", false } },
                      QJsonObject{
                          { "id", "invalidate" }, { "capability", "tool" }, { "menu", false } } } },
        { "views",
          QJsonArray{ QJsonObject{ { "id", "main" }, { "title", "Main" } },
                      QJsonObject{ { "id", "tips" },
                                   { "title", "Tips" },
                                   { "icon_ref", "status:ok" } } } },
    };
}

// The fixture plugin with E17 handlers; host-messages.jsonl records its notifications.
struct E17Plugin {
    QTemporaryDir temp;
    QString folder;
    std::unique_ptr<PluginManager> manager;

    E17Plugin()
    {
        const auto shipped = QDir( temp.path() ).filePath( "shipped" );
        folder = QDir( shipped ).filePath( PluginId );
        REQUIRE( QDir().mkpath( folder ) );
        const auto executable = QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName();
        QFile file( QDir( folder ).filePath( "plugin.json" ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( QJsonDocument( e17Manifest( executable ) ).toJson() );
        file.close();
        REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                              QDir( folder ).filePath( executable ) ) );
        manager
            = std::make_unique<PluginManager>( QDir( temp.path() ).filePath( "data" ), shipped );
        REQUIRE( manager->manage( "enable", PluginId, {} ).ok() );
    }
    // Starting the fixture process can take seconds on a busy machine (a run right after a build
    // once took over 5 s): wait for the host's ready or failed, then require ready.
    void waitUntilStarted()
    {
        REQUIRE( waitFor(
            [ & ] {
                const auto state = manager->pluginState( PluginId );
                return state == "ready" || state == "failed";
            },
            PluginStartWaitMs ) );
        REQUIRE( manager->pluginState( PluginId ) == "ready" );
    }
    void run( const QString& action )
    {
        const auto job = manager->invoke( PluginId, action, {}, {} );
        REQUIRE( job.ok() );
        const auto id = job.payload.value( "jobId" ).toString();
        REQUIRE( waitFor( [ & ] {
            const auto state = manager->job( id ).payload.value( "state" ).toString();
            return state == "completed" || state == "failed";
        } ) );
    }
};

struct TipsHost {
    E17Plugin plugin;
    QMainWindow window;
    std::unique_ptr<PluginViewHost> host;
    PluginView* view = nullptr;

    TipsHost()
    {
        window.resize( 1000, 700 );
        window.setCentralWidget( new QWidget( &window ) );
        window.show();
        host = std::make_unique<PluginViewHost>(
            &window, "w1",
            [] { return QVariantMap{ { "windowId", "w1" }, { "windowIndex", 0 } }; },
            plugin.manager.get(), []( const CommanderRequest& ) { return CommanderResult{}; } );
        host->setRenderIntervalMs( 10 );
        QString error;
        REQUIRE( host->openView( PluginId, "tips", {}, &error ) ); // starts the plugin
        view = host->view( PluginId, "tips" );
        REQUIRE( view );
        plugin.waitUntilStarted();
        REQUIRE( waitFor( [ & ] {
            auto* status = qobject_cast<QLabel*>( view->element( "status" ) );
            return status && status->text() == "ready tips" && view->isEnabled();
        } ) );
    }

    struct Resolved {
        bool done = false;
        QString source;
        QJsonObject tooltip;
        QString staticTip;
        qint64 latencyMs = 0;
    };
    Resolved resolve( const QString& element, const QString& row = {}, const QString& column = {},
                      bool useCache = true )
    {
        auto result = std::make_shared<Resolved>();
        QString error;
        REQUIRE( view->resolveTooltip(
            element, row, column, useCache,
            [ result ]( const QString& source, const QJsonObject& tooltip, const QString& staticTip,
                        qint64 latencyMs ) {
                *result = { true, source, tooltip, staticTip, latencyMs };
            },
            &error ) );
        INFO( error.toStdString() );
        REQUIRE( waitFor( [ & ] { return result->done; }, 6000 ) );
        return *result;
    }
};
} // namespace

TEST_CASE( "E17 element tooltips are plain text and accessible names come from captions",
           "[ui][workbench][E17]" )
{
    TipsHost fixture;
    auto& view = *fixture.view;
    auto* plain = view.element( "plain" );
    // Text that looks like HTML is shown as written (it was rendered as rich text before E17a).
    REQUIRE( plain->toolTip().contains( "&lt;b&gt;not bold&lt;/b&gt;" ) );
    REQUIRE( plain->accessibleName() == "plain" );
    REQUIRE( plain->accessibleDescription() == "described" );
    REQUIRE( view.element( "named" )->accessibleName() == "Custom name" );
    REQUIRE( view.element( "tip" )->accessibleDescription() == "static tip" );
    REQUIRE( view.element( "go" )->accessibleName() == "Go" );
    REQUIRE( view.element( "go" )->accessibleDescription() == "Runs it" );
    REQUIRE( view.element( "name" )->accessibleName() == "Name" );
    auto* caption = view.findChild<QLabel*>( PluginView::objectNameFor( PluginId, "tips", "name" )
                                             + "_label" );
    REQUIRE( caption );
    REQUIRE( caption->buddy() == view.element( "name" ) );
    REQUIRE( view.element( "box" )->accessibleName() == "Box" );
    REQUIRE( view.element( "rows" )->accessibleName() == "rows" );
    // Column headers have static tooltips, also plain text.
    auto* table = qobject_cast<QTableView*>( view.element( "rows" ) );
    REQUIRE( table->model()->headerData( 0, Qt::Horizontal, Qt::ToolTipRole ).toString().contains(
        "&lt;b&gt;Column C&lt;/b&gt;" ) );
    REQUIRE( table->model()->headerData( 1, Qt::Horizontal, Qt::ToolTipRole ).isNull() );
}

TEST_CASE( "E17 advanced tooltips ask the plugin, keep the answer and render it escaped",
           "[ui][workbench][E17]" )
{
    TipsHost fixture;
    const auto first = fixture.resolve( "tip" );
    REQUIRE( first.source == "plugin" );
    REQUIRE( first.tooltip.value( "title" ) == "Title <b>bold</b>" );
    REQUIRE( first.staticTip == "static tip" );
    REQUIRE( fixture.resolve( "tip" ).source == "cache" );
    REQUIRE( fixture.resolve( "tip", {}, {}, false ).source == "plugin" );
    const auto html = PluginTooltips::render( first.tooltip );
    REQUIRE( html.contains( "Title &lt;b&gt;bold&lt;/b&gt;" ) );
    REQUIRE( html.contains( "First line<br>Second &lt;i&gt;line&lt;/i&gt;" ) );
    REQUIRE( html.contains( "Firmware" ) );
    REQUIRE_FALSE( html.contains( "<i>" ) );
    // The icon of the answer is a registered image.
    fixture.plugin.run( "icons" );
    REQUIRE( waitFor( [] { return PluginIconRegistry::instance().iconCount( PluginId ) >= 1; } ) );
    REQUIRE( PluginIconRegistry::instance()
                 .iconHtml( PluginId, fixture.plugin.folder, "image:dot" )
                 .startsWith( "<img src=\"data:image/png;base64," ) );
}

TEST_CASE( "E17 nothing, errors, invalid answers and timeouts fall back to the static tooltip",
           "[ui][workbench][E17]" )
{
    TipsHost fixture;
    const auto nothing = fixture.resolve( "nulltip" );
    REQUIRE( nothing.source == "static" );
    REQUIRE( nothing.staticTip == "static null" );
    REQUIRE( fixture.resolve( "errortip" ).source == "static" );
    REQUIRE( fixture.resolve( "invalidtip" ).source == "static" );
    const auto slow = fixture.resolve( "slowtip" ); // tooltip_timeout_ms 200
    REQUIRE( slow.source == "static" );
    REQUIRE( slow.latencyMs >= 150 );
    // The element without tooltip_provider never asks.
    REQUIRE( fixture.resolve( "plain" ).source == "static" );
}

TEST_CASE( "E17 patches and tooltip/invalidate drop cached answers", "[ui][workbench][E17]" )
{
    TipsHost fixture;
    REQUIRE( fixture.resolve( "counttip" ).tooltip.value( "text" ) == "count 1" );
    const auto cached = fixture.resolve( "counttip" );
    REQUIRE( cached.source == "cache" );
    REQUIRE( cached.tooltip.value( "text" ) == "count 1" );
    fixture.view->applyPatch(
        { QJsonObject{ { "id", "counttip" }, { "set", QJsonObject{ { "text", "changed" } } } } } );
    REQUIRE( fixture.resolve( "counttip" ).tooltip.value( "text" ) == "count 2" );
    REQUIRE( fixture.resolve( "counttip" ).source == "cache" );
    fixture.plugin.run( "invalidate" );
    REQUIRE( waitFor( [ & ] { return fixture.resolve( "counttip" ).source == "plugin"; } ) );
}

TEST_CASE( "E17 table cells ask with their row and column; other cells keep the static tooltip",
           "[ui][workbench][E17]" )
{
    TipsHost fixture;
    // The row's tooltip_key wins over the column's.
    REQUIRE( fixture.resolve( "rows", "r1", "c" ).tooltip.value( "text" ) == "cell r1/c key rk1" );
    REQUIRE( fixture.resolve( "rows", "r2", "c" ).tooltip.value( "text" ) == "cell r2/c key ck" );
    const auto other = fixture.resolve( "rows", "r1", "d" );
    REQUIRE( other.source == "static" );
    REQUIRE( other.staticTip == "x" );
    QString error;
    REQUIRE_FALSE( fixture.view->resolveTooltip( "rows", {}, {}, true, {}, &error ) );
    REQUIRE_FALSE( fixture.view->resolveTooltip( "rows", "r9", "c", true, {}, &error ) );
    REQUIRE_FALSE( fixture.view->resolveTooltip( "missing", {}, {}, true, {}, &error ) );
}

TEST_CASE( "E17 hovering shows the answer in place and leaving cancels the query",
           "[ui][workbench][E17]" )
{
    TipsHost fixture;
    const auto hover = []( QWidget* widget, const QPoint& position ) {
        QCursor::setPos( widget->mapToGlobal( position ) );
        QHelpEvent event( QEvent::ToolTip, position, widget->mapToGlobal( position ) );
        QApplication::sendEvent( widget, &event );
    };
    auto* tip = fixture.view->element( "tip" );
    hover( tip, QPoint( 2, 2 ) );
    REQUIRE( waitFor( [] { return QToolTip::text().contains( "Title &lt;b&gt;bold" ); } ) );
    // A table cell.
    auto* table = qobject_cast<QTableView*>( fixture.view->element( "rows" ) );
    const auto cell = table->visualRect( table->model()->index( 0, 0 ) ).center();
    hover( table->viewport(), cell );
    REQUIRE( waitFor( [] { return QToolTip::text().contains( "cell r1/c key rk1" ); } ) );
    // Leaving a slow target cancels its query.
    auto* slow = fixture.view->element( "slowtip" );
    hover( slow, QPoint( 2, 2 ) );
    REQUIRE( fixture.plugin.manager->queries().outstanding( PluginId ) == 1 );
    QCursor::setPos( slow->mapToGlobal( QPoint( -40, -40 ) ) );
    QEvent leave( QEvent::Leave );
    QApplication::sendEvent( slow, &leave );
    REQUIRE( fixture.plugin.manager->queries().outstanding( PluginId ) == 0 );
    QToolTip::hideText();
}

TEST_CASE( "E17 icons: built-in, status, files, themes, registered images and their limits",
           "[ui][workbench][E17]" )
{
    QTemporaryDir folder;
    auto& registry = PluginIconRegistry::instance();
    const QString plugin = "org.test.icons";
    REQUIRE_FALSE( registry.icon( plugin, folder.path(), "builtin:play" ).isNull() );
    REQUIRE_FALSE( registry.icon( plugin, folder.path(), "builtin:star" ).isNull() );
    REQUIRE_FALSE( registry.icon( plugin, folder.path(), "builtin:no-such-icon" ).isNull() );
    REQUIRE_FALSE( registry.icon( plugin, folder.path(), "status:ok" ).isNull() );
    REQUIRE_FALSE(
        registry
            .icon( plugin, folder.path(),
                   QJsonObject{ { "light", "status:ok" }, { "dark", "status:error" } } )
            .isNull() );
    REQUIRE( registry.icon( plugin, folder.path(), "icons/missing.png" ).isNull() );
    QImage image( 4, 4, QImage::Format_ARGB32 );
    image.fill( Qt::red );
    REQUIRE( QDir( folder.path() ).mkpath( "icons" ) );
    REQUIRE( image.save( QDir( folder.path() ).filePath( "icons/red.png" ) ) );
    REQUIRE_FALSE( registry.icon( plugin, folder.path(), "icons/red.png" ).isNull() );
    REQUIRE( registry.icon( plugin, folder.path(), "../escape.png" ).isNull() );

    QByteArray png;
    {
        QBuffer buffer( &png );
        buffer.open( QIODevice::WriteOnly );
        REQUIRE( image.save( &buffer, "PNG" ) );
    }
    QImage huge( 300, 10, QImage::Format_ARGB32 );
    huge.fill( Qt::blue );
    QByteArray hugePng;
    {
        QBuffer buffer( &hugePng );
        buffer.open( QIODevice::WriteOnly );
        REQUIRE( huge.save( &buffer, "PNG" ) );
    }
    const auto entry = []( const QByteArray& bytes ) {
        return QJsonObject{ { "png", QString::fromLatin1( bytes.toBase64() ) } };
    };
    auto refused = registry.apply(
        plugin,
        { { "icons",
            QJsonObject{
                { "ok", entry( png ) },
                { "huge", entry( hugePng ) },
                { "bad id!", entry( png ) },
                { "garbage", QJsonObject{ { "png", "@@@" } } },
                { "remote", QJsonObject{ { "svg", "<svg><image href=\"file:///etc/x\"/></svg>" } } },
                { "styled", QJsonObject{ { "svg", "<svg><style>@import url(x.css);</style></svg>" } } },
                { "picture", QJsonObject{ { "png", QString::fromLatin1( hugePng.toBase64() ) },
                                          { "picture", true } } } } } } );
    REQUIRE( refused.size() == 5 );
    REQUIRE( registry.iconCount( plugin ) == 1 );
    REQUIRE( registry.pictureCount( plugin ) == 1 );
    REQUIRE_FALSE( registry.icon( plugin, {}, "image:ok" ).isNull() );
    REQUIRE( registry.picture( plugin, "picture" ).width() == 300 );
    REQUIRE( PluginIconRegistry::safeSvg( "<svg><use href=\"#a\"/><image href=\"data:image/png;"
                                          "base64,AAAA\"/></svg>" ) );
    REQUIRE_FALSE( PluginIconRegistry::safeSvg( "<!DOCTYPE svg><svg/>" ) );
    // An embedded SVG would be decoded without this check.
    REQUIRE_FALSE( PluginIconRegistry::safeSvg(
        "<svg><image href=\"data:image/svg+xml;base64,AAAA\"/></svg>" ) );
    REQUIRE_FALSE( PluginIconRegistry::safeSvg( "<svg><image href=\"other.png\"/></svg>" ) );
    // An id registered again in the other class leaves the old one (Codex review of #130).
    registry.apply( plugin, { { "icons", QJsonObject{ { "moving", entry( png ) } } } } );
    REQUIRE( registry.iconCount( plugin ) == 2 );
    auto picture = entry( png );
    picture.insert( "picture", true );
    registry.apply( plugin, { { "icons", QJsonObject{ { "moving", picture } } } } );
    REQUIRE( registry.iconCount( plugin ) == 1 );
    REQUIRE( registry.pictureCount( plugin ) == 2 );
    REQUIRE( registry.icon( plugin, {}, "image:moving" ).isNull() );
    registry.apply( plugin, { { "icons", QJsonObject{ { "moving", entry( png ) } } } } );
    REQUIRE( registry.iconCount( plugin ) == 2 );
    REQUIRE( registry.pictureCount( plugin ) == 1 );
    registry.apply( plugin, { { "remove", QJsonArray{ "moving" } } } );
    // The rendered HTML of get_plugin_tooltip carries the icon the GUI shows.
    PluginTooltips tooltips;
    PluginViewQueries withIcons;
    withIcons.iconHtml = []( const QJsonValue& ) { return QStringLiteral( "<img src=\"x\">" ); };
    tooltips.setQueries( withIcons );
    REQUIRE( tooltips.renderWithIcon( { { "text", "t" }, { "icon", "status:ok" } } )
                 .contains( "<img src=\"x\">" ) );
    const auto generation = registry.generation( plugin );
    registry.apply( plugin, { { "remove", QJsonArray{ "ok" } } } );
    REQUIRE( registry.iconCount( plugin ) == 0 );
    REQUIRE( registry.generation( plugin ) == generation + 1 );
    registry.clear( plugin );
    REQUIRE( registry.pictureCount( plugin ) == 0 );
}

TEST_CASE( "E17 registered images end when the plugin stops", "[ui][workbench][E17]" )
{
    TipsHost fixture;
    fixture.plugin.run( "icons" );
    REQUIRE( waitFor( [] { return PluginIconRegistry::instance().iconCount( PluginId ) >= 1; } ) );
    REQUIRE( fixture.plugin.manager->manage( "disable", PluginId, {} ).ok() );
    REQUIRE( waitFor( [] { return PluginIconRegistry::instance().iconCount( PluginId ) == 0; } ) );
}

TEST_CASE( "E17 get_plugin_tooltip and icon_ref in the Plugins menu", "[ui][workbench][E17]" )
{
    E17Plugin plugin;
    const auto executor = [ &plugin ]( const CommanderRequest& request ) {
        const auto result = plugin.manager->manage( request.pluginOperation, request.pluginId,
                                                    request.pluginArguments );
        return result.ok() ? commanderSuccess( {}, result.payload )
                           : commanderFailure( CommanderResultCode::ExecutionFailed, result.error );
    };
    auto session = std::make_shared<Session>();
    MainWindow window( WindowSession{ session, "PluginSdkE17", 0 } );
    window.setPluginManager( *plugin.manager, executor );
    window.show();
    // icon_ref wins over the generic icon of actions and views.
    REQUIRE( waitFor( [ & ] {
        return window.findChild<QAction*>( "pluginAction_" + PluginId + "_summary" ) != nullptr;
    } ) );
    const auto image = []( const QIcon& icon ) { return icon.pixmap( 16, 16 ).toImage(); };
    auto* summary = window.findChild<QAction*>( "pluginAction_" + PluginId + "_summary" );
    REQUIRE( image( summary->icon() )
             == image( PluginIconRegistry::instance().icon( PluginId, plugin.folder,
                                                             "builtin:play" ) ) );
    auto* tips = window.findChild<QAction*>( "pluginViewAction_" + PluginId + "_tips" );
    REQUIRE( tips );
    REQUIRE( image( tips->icon() )
             == image(
                 PluginIconRegistry::instance().icon( PluginId, plugin.folder, "status:ok" ) ) );

    CommanderRequest open;
    open.action = CommanderAction::OpenPluginView;
    open.pluginId = PluginId;
    open.viewId = "tips";
    REQUIRE( window.executeCommanderRequest( open ).ok() ); // starts the plugin
    plugin.waitUntilStarted();
    REQUIRE( waitFor( [ & ] {
        auto* status = window.findChild<QLabel*>(
            PluginView::objectNameFor( PluginId, "tips", "status" ) );
        return status && status->text() == "ready tips";
    } ) );
    CommanderRequest request;
    request.action = CommanderAction::GetPluginTooltip;
    request.pluginId = PluginId;
    request.viewId = "tips";
    request.pluginArguments = { { "elementId", "rows" }, { "rowId", "r1" }, { "columnId", "c" } };
    auto result = window.executeCommanderRequest( request );
    INFO( result.message.toStdString() );
    REQUIRE( result.ok() );
    REQUIRE( result.payload.value( "source" ) == "plugin" );
    REQUIRE( result.payload.value( "tooltip" ).toMap().value( "text" ) == "cell r1/c key rk1" );
    REQUIRE( result.payload.value( "html" ).toString().contains( "cell r1/c key rk1" ) );
    REQUIRE( window.executeCommanderRequest( request ).payload.value( "source" ) == "cache" );
    request.pluginArguments.insert( "noCache", true );
    REQUIRE( window.executeCommanderRequest( request ).payload.value( "source" ) == "plugin" );
    request.pluginArguments = { { "elementId", "plain" } };
    result = window.executeCommanderRequest( request );
    REQUIRE( result.payload.value( "source" ) == "static" );
    REQUIRE( result.payload.value( "staticTooltip" ) == "<b>not bold</b>" );
    REQUIRE( result.payload.value( "tooltip" ).isNull() );
    request.pluginArguments = { { "elementId", "missing" } };
    REQUIRE( window.executeCommanderRequest( request ).code
             == CommanderResultCode::InvalidRequest );
    request.viewId = "main";
    REQUIRE( window.executeCommanderRequest( request ).code == CommanderResultCode::NotFound );
    // As closing the window does: the views close before the window is destroyed.
    Q_EMIT window.windowClosed();
    QCoreApplication::processEvents();
}
