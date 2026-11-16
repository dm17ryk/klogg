// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// U15b: one source picker for the remaining tab-bound tools (plans/.../tasks/U15.md).

#include <catch2/catch.hpp>

#include <functional>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include "analysissource.h"
#include "pluginmanager.h"
#include "pluginsourceruns.h"
#include "pluginviewhost.h"
#include "scriptrunnerwindow.h"
#include "scriptsupervisor.h"
#include "test_utils.h"

#ifndef Q_OS_MACOS
#include "../streamsession_testaccess.h"
#include "actionsresponseswindow.h"
#include "crawlerwidget.h"
#include "logpane.h"
#include "mainwindow.h"
#include "previewwindow.h"
#include "session.h"
#include "streamsession.h"
#endif

using cilogg::plugins::PluginManager;

namespace {
const QString PluginId = QStringLiteral( "org.test.u15b" );

bool waitFor( const std::function<bool()>& predicate, int timeout = 10000 )
{
    return QTest::qWaitFor( predicate, timeout );
}

QVariantMap tabSource( const QString& tabId, const QString& path, const QString& type,
                       bool focused = false )
{
    return { { "tabId", tabId },    { "filePath", path }, { "label", tabId + ": " + path },
             { "windowIndex", 0 },  { "tabIndex", 0 },    { "focused", focused },
             { "sourceType", type } };
}

bool itemEnabled( const QComboBox& combo, const QString& data )
{
    const auto index = combo.findData( data );
    REQUIRE( index >= 0 );
    const auto* model = qobject_cast<const QStandardItemModel*>( combo.model() );
    REQUIRE( model != nullptr );
    return model->item( index )->isEnabled();
}

QString itemToolTip( const QComboBox& combo, const QString& data )
{
    return combo.itemData( combo.findData( data ), Qt::ToolTipRole ).toString();
}

// A running fixture plugin with an action and a view; it records host notifications.
struct FixturePlugin {
    QTemporaryDir temp;
    QString folder;
    std::unique_ptr<PluginManager> manager;

    FixturePlugin()
    {
        const auto shipped = QDir( temp.path() ).filePath( "shipped" );
        folder = QDir( shipped ).filePath( PluginId );
        REQUIRE( QDir().mkpath( folder ) );
        const auto executable = QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName();
        const QJsonObject manifest{
            { "id", PluginId },
            { "name", "U15b fixture" },
            { "version", "1.0.0" },
            { "protocol_version", 1 },
            { "executable", executable },
            { "capabilities", QJsonArray{ "tool" } },
            { "actions",
              QJsonArray{ QJsonObject{ { "id", "summary" }, { "capability", "tool" } } } },
            { "views", QJsonArray{ QJsonObject{ { "id", "main" }, { "title", "Main" } } } }
        };
        QFile file( QDir( folder ).filePath( "plugin.json" ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( QJsonDocument( manifest ).toJson() );
        file.close();
        REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                              QDir( folder ).filePath( executable ) ) );
        manager
            = std::make_unique<PluginManager>( QDir( temp.path() ).filePath( "data" ), shipped );
        REQUIRE( manager->manage( "enable", PluginId, {} ).ok() );
    }

    QList<QJsonObject> messages( const QString& method ) const
    {
        QList<QJsonObject> result;
        QFile log( QDir( folder ).filePath( "host-messages.jsonl" ) );
        if ( log.open( QIODevice::ReadOnly ) ) {
            for ( const auto& line : log.readAll().split( '\n' ) ) {
                const auto frame = QJsonDocument::fromJson( line ).object();
                if ( frame.value( "method" ) == method ) {
                    result.append( frame.value( "params" ).toObject() );
                }
            }
        }
        return result;
    }
};

QString viewStatus( PluginViewHost& host )
{
    auto* view = host.view( PluginId, "main" );
    auto* status = view ? qobject_cast<QLabel*>( view->element( "status" ) ) : nullptr;
    return status ? status->text() : QString{};
}
} // namespace

TEST_CASE( "source_picker_lists_unusable_sources_disabled: tools say why a source is unusable",
           "[ui][workbench][U15]" )
{
    QVariantList sources{ tabSource( "f1", "/logs/a.log", "file", true ),
                          tabSource( "c1", "/captures/c1.log", "com" ),
                          tabSource( "c2", "/captures/c2.log", "com" ) };
    // Script Runner: COM tabs only, no files.
    AnalysisSourcePicker script( [ &sources ] { return sources; }, SourceRules::liveComTabs() );
    REQUIRE( itemEnabled( script, "focused" ) );
    REQUIRE( itemEnabled( script, "all" ) );
    REQUIRE( script.itemText( script.findData( "all" ) ).contains( "2" ) );
    REQUIRE_FALSE( itemEnabled( script, "tab:f1" ) );
    REQUIRE_FALSE( itemToolTip( script, "tab:f1" ).isEmpty() );
    REQUIRE( itemEnabled( script, "tab:c1" ) );
    REQUIRE_FALSE( itemEnabled( script, "file:" ) );
    REQUIRE_FALSE( itemToolTip( script, "file:" ).isEmpty() );
    QSignalSpy chosen( &script, &AnalysisSourcePicker::selectionChanged );
    REQUIRE_FALSE( script.setSelection( "tab:f1" ) );
    REQUIRE( script.setSelection( "tab:c2" ) );
    REQUIRE( script.selection() == "tab:c2" );
    REQUIRE( chosen.isEmpty() );
    // A user (or UI automation) choice is announced once.
    script.setCurrentIndex( script.findData( "all" ) );
    REQUIRE( chosen.size() == 1 );
    REQUIRE( chosen.front().front().toString() == "all" );

    // Preview and plugin views: one source; "all" is listed disabled with the reason.
    AnalysisSourcePicker one( [ &sources ] { return sources; }, SourceRules::oneSource() );
    REQUIRE_FALSE( itemEnabled( one, "all" ) );
    REQUIRE_FALSE( itemToolTip( one, "all" ).isEmpty() );
    REQUIRE( itemEnabled( one, "tab:f1" ) );
    REQUIRE( itemEnabled( one, "file:" ) );
    REQUIRE( one.setSelection( "file:/logs/other.log" ) );
    REQUIRE( one.selection() == "file:/logs/other.log" );

    // A tab the lister marks unusable is disabled for every tool.
    auto marked = sources[ 1 ].toMap();
    marked[ "disabledReason" ] = "Busy";
    sources[ 1 ] = marked;
    AnalysisSourcePicker any( [ &sources ] { return sources; }, SourceRules{} );
    REQUIRE_FALSE( itemEnabled( any, "tab:c1" ) );
    REQUIRE( itemToolTip( any, "tab:c1" ) == "Busy" );
    REQUIRE( itemEnabled( any, "all" ) );
    // The list follows the open tabs when it is refreshed.
    sources.append( tabSource( "c3", "/captures/c3.log", "com" ) );
    any.refresh();
    REQUIRE( any.findData( "tab:c3" ) >= 0 );
}

TEST_CASE( "plugin_action_on_each_source: a plugin action runs on the chosen source",
           "[ui][workbench][U15]" )
{
    QVariantList sources;
    for ( int index = 0; index < 12; ++index ) {
        sources << tabSource( QString( "t%1" ).arg( index ), QString( "/logs/%1.log" ).arg( index ),
                              "file", index == 3 );
    }
    QList<CommanderRequest> invoked;
    QMap<QString, QString> states;
    bool full = false;
    QStringList opened;
    PluginSourceRuns runs(
        [ &sources ] { return sources; },
        [ & ]( const CommanderRequest& request ) {
            if ( full ) {
                return CommanderResult{ CommanderResultCode::ExecutionFailed,
                                        "Too many active plugin jobs.",
                                        { { "reason", "queue_limit" } } };
            }
            invoked.append( request );
            const auto id = QString( "job-%1" ).arg( invoked.size() );
            states[ id ] = "running";
            return commanderSuccess( {}, { { "jobId", id }, { "state", "running" } } );
        },
        [ & ]( const QString& id ) {
            return states.contains( id )
                       ? commanderSuccess( {}, { { "state", states.value( id ) } } )
                       : CommanderResult{ CommanderResultCode::NotFound,
                                          "gone",
                                          { { "reason", "job_expired" } } };
        },
        [ & ]( const QString& path ) {
            opened << path;
            sources << tabSource( "opened", path, "file" );
            return commanderSuccess();
        } );

    CommanderRequest request;
    request.action = CommanderAction::InvokePlugin;
    request.pluginId = PluginId;
    request.pluginAction = "summary";

    SECTION( "focused keeps the window's current tab" )
    {
        request.source = "focused";
        request.windowIndex = 1;
        REQUIRE( runs.invoke( request ).ok() );
        REQUIRE( invoked.size() == 1 );
        REQUIRE( invoked.front().tabId.isEmpty() );
        REQUIRE( invoked.front().windowIndex == 1 );
        REQUIRE( invoked.front().source.isEmpty() );
    }
    SECTION( "a tab, a file and an explicit tab selector" )
    {
        request.source = "tab:t7";
        request.windowIndex = 0;
        REQUIRE( runs.invoke( request ).ok() );
        REQUIRE( invoked.back().tabId == "t7" );
        REQUIRE_FALSE( invoked.back().windowIndex );
        // A file that is not open is opened first, then the action runs on its tab.
        request.source = "file:/logs/new.log";
        REQUIRE( runs.invoke( request ).ok() );
        REQUIRE( opened == QStringList{ "/logs/new.log" } );
        REQUIRE( invoked.back().tabId == "opened" );
        // An explicit tab selector wins over the source.
        request.source = "all";
        request.tabId = "t1";
        REQUIRE( runs.invoke( request ).ok() );
        REQUIRE( invoked.size() == 3 );
        REQUIRE( invoked.back().tabId == "t1" );
    }
    SECTION( "unusable sources are refused" )
    {
        request.source = "window";
        REQUIRE( runs.invoke( request ).payload.value( "reason" ) == "invalid_source" );
        request.source = "tab:closed";
        REQUIRE( runs.invoke( request ).payload.value( "reason" ) == "source_not_found" );
        REQUIRE( invoked.isEmpty() );
    }
    SECTION( "all runs once per tab, staged" )
    {
        request.source = "all";
        const auto started = runs.invoke( request );
        REQUIRE( started.ok() );
        REQUIRE( started.payload.value( "source" ) == "all" );
        REQUIRE( started.payload.value( "sources" ).toList().size() == 12 );
        // At most MaxActiveAnalysisRuns of the Run's jobs are unfinished at a time.
        REQUIRE( invoked.size() == MaxActiveAnalysisRuns );
        // A full plugin manager makes the next tabs wait instead of failing them.
        full = true;
        for ( auto it = states.begin(); it != states.end(); ++it )
            it.value() = "completed";
        runs.advance();
        REQUIRE( invoked.size() == MaxActiveAnalysisRuns );
        full = false;
        runs.advance();
        REQUIRE( waitFor( [ & ] {
            for ( auto it = states.begin(); it != states.end(); ++it )
                it.value() = "completed";
            return invoked.size() == 12 && !runs.runs().front().toMap().value( "active" ).toBool();
        } ) );
        QStringList order;
        for ( const auto& one : invoked )
            order << one.tabId;
        QStringList expected;
        for ( int index = 0; index < 12; ++index )
            expected << QString( "t%1" ).arg( index );
        REQUIRE( order == expected );
        for ( const auto& one : invoked )
            REQUIRE( one.source.isEmpty() );
    }
}

TEST_CASE( "plugin_view_follows_chosen_tab: a view's context is the tab it follows",
           "[ui][workbench][U15]" )
{
    FixturePlugin plugin;
    QMainWindow window;
    window.resize( 900, 600 );
    window.setCentralWidget( new QWidget( &window ) );
    window.show();
    QList<CommanderRequest> requests;
    PluginViewHost host(
        &window, "w1", [] { return QVariantMap{ { "windowId", "w1" }, { "windowIndex", 0 } }; },
        plugin.manager.get(),
        [ &requests ]( const CommanderRequest& request ) {
            requests.append( request );
            return CommanderResult{};
        } );
    host.setRenderIntervalMs( 10 );
    const QVariantList sources{ tabSource( "t1", "/logs/a.log", "file", true ),
                                tabSource( "t2", "/logs/b.log", "file" ) };
    host.setSourceProviders(
        [ &sources ] { return sources; },
        []( const QString& source, QString* error ) -> QVariantMap {
            if ( source == "tab:t1" || source == "tab:t2" ) {
                return { { "windowId", "w1" },
                         { "windowIndex", 0 },
                         { "tabId", source.mid( 4 ) },
                         { "filePath", source == "tab:t1" ? "/logs/a.log" : "/logs/b.log" } };
            }
            *error = "The selected tab is closed.";
            return {};
        } );

    // A view follows one source: "all" is refused at open.
    QString error;
    REQUIRE_FALSE( host.openView( PluginId, "main", { { "source", "all" } }, &error ) );
    REQUIRE_FALSE( error.isEmpty() );
    REQUIRE( host.openView( PluginId, "main", { { "source", "tab:t1" } }, &error ) );
    INFO( error.toStdString() );
    REQUIRE( waitFor( [ & ] { return viewStatus( host ).startsWith( "ready" ); } ) );
    REQUIRE( host.viewSource( PluginId, "main" ) == "tab:t1" );
    auto* picker
        = window.findChild<AnalysisSourcePicker*>( "pluginViewSource_" + PluginId + "_main" );
    REQUIRE( picker != nullptr );
    REQUIRE( picker->selection() == "tab:t1" );
    REQUIRE( waitFor( [ & ] { return plugin.messages( "view/opened" ).size() == 1; } ) );
    REQUIRE( plugin.messages( "view/opened" ).front()[ "context" ].toObject()[ "tabId" ] == "t1" );

    // Choosing another tab in the view's picker re-sends view/opened with that tab.
    picker->setCurrentIndex( picker->findData( "tab:t2" ) );
    REQUIRE( host.viewSource( PluginId, "main" ) == "tab:t2" );
    REQUIRE( waitFor( [ & ] { return plugin.messages( "view/opened" ).size() == 2; } ) );
    const auto updated = plugin.messages( "view/opened" ).back();
    REQUIRE( updated[ "context" ].toObject()[ "tabId" ] == "t2" );
    REQUIRE( updated[ "window_id" ] == "w1" );

    // The view's buttons run on the chosen tab, found by its id in any window.
    REQUIRE( waitFor( [ & ] { return viewStatus( host ).startsWith( "ready" ); } ) );
    auto* run = qobject_cast<QPushButton*>( host.view( PluginId, "main" )->element( "run" ) );
    REQUIRE( run != nullptr );
    REQUIRE( waitFor( [ & ] { return run->isEnabled() && run->window()->isEnabled(); } ) );
    run->click();
    REQUIRE( requests.size() == 1 );
    REQUIRE( requests.front().tabId == "t2" );
    REQUIRE_FALSE( requests.front().windowIndex );

    // A source that cannot be used keeps the previous one.
    REQUIRE_FALSE( host.setViewSource( PluginId, "main", "tab:gone", &error ) );
    REQUIRE( host.viewSource( PluginId, "main" ) == "tab:t2" );
    REQUIRE( plugin.messages( "view/opened" ).size() == 2 );
    // Back to the focused tab: the window's current tab, as before U15b.
    REQUIRE( host.setViewSource( PluginId, "main", "focused", &error ) );
    REQUIRE( waitFor( [ & ] { return plugin.messages( "view/opened" ).size() == 3; } ) );
    REQUIRE_FALSE(
        plugin.messages( "view/opened" ).back()[ "context" ].toObject().contains( "tabId" ) );
}

TEST_CASE( "script_runner_on_chosen_com_tabs: scripts start on the chosen COM tabs",
           "[ui][workbench][U15]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto script = directory.filePath( "noop.py" );
    {
        QFile file( script );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( "pass\n" );
    }
    const auto info = [ & ] {
        QVariantList tabs;
        const QList<QPair<QString, QString>> list{ { "f1", "file" },
                                                   { "c1", "com" },
                                                   { "c2", "com" } };
        for ( int index = 0; index < list.size(); ++index ) {
            QVariantMap value{ { "tabId", list[ index ].first },
                               { "tabIndex", index },
                               { "filePath", directory.filePath( list[ index ].first + ".log" ) },
                               { "displayName", list[ index ].first },
                               { "sourceType", list[ index ].second } };
            if ( list[ index ].second == "com" )
                value[ "com" ]
                    = QVariantMap{ { "portName", "CILOGG_TEST_" + list[ index ].first } };
            tabs << value;
        }
        return QVariantMap{ { "windows", QVariantList{ QVariantMap{ { "windowIndex", 0 },
                                                                    { "windowId", "w0" },
                                                                    { "isActiveWindow", true },
                                                                    { "currentTabId", "c1" },
                                                                    { "tabs", tabs } } } } };
    };
    ScriptSupervisor supervisor;
    supervisor.setCommanderExecutor( [ & ]( const CommanderRequest& request ) {
        return request.action == CommanderAction::GetInfo ? commanderSuccess( {}, info() )
                                                          : commanderSuccess();
    } );
    const auto runTabs = [ &supervisor ] {
        CommanderRequest status;
        status.action = CommanderAction::GetScriptStatus;
        status.allEntities = true;
        QStringList tabs;
        for ( const auto& run : supervisor.scriptStatus( status ).payload.value( "runs" ).toList() )
            tabs << run.toMap().value( "tabId" ).toString();
        tabs.sort();
        return tabs;
    };

    CommanderRequest request;
    request.action = CommanderAction::RunScript;
    request.scriptFilePath = script;

    // "all" starts one run per COM tab; the file tab is not a live session.
    request.source = "all";
    const auto all = supervisor.runScript( request );
    INFO( all.message.toStdString() );
    REQUIRE( all.ok() );
    REQUIRE( all.payload.value( "runs" ).toList().size() == 2 );
    REQUIRE( runTabs() == QStringList{ "c1", "c2" } );

    request.source = "tab:f1";
    REQUIRE( supervisor.runScript( request ).payload.value( "reason" ) == "not_com_tab" );
    request.source = "file:" + directory.filePath( "f1.log" );
    REQUIRE( supervisor.runScript( request ).payload.value( "reason" ) == "invalid_source" );
    // An explicit tab selector wins over the source.
    request.source = "tab:f1";
    request.tabId = "c2";
    REQUIRE( supervisor.runScript( request ).ok() );
    request.tabId.clear();
    request.source = "focused";
    REQUIRE( supervisor.runScript( request ).ok() );
    REQUIRE( runTabs() == QStringList{ "c1", "c2" } );

    // The Script Runner lists every tab; only COM tabs can be chosen.
    ScriptRunnerWindow window( &supervisor );
    auto* picker = window.findChild<AnalysisSourcePicker*>( "scriptRunnerSource" );
    REQUIRE( picker != nullptr );
    picker->refresh();
    REQUIRE_FALSE( itemEnabled( *picker, "tab:f1" ) );
    REQUIRE( itemEnabled( *picker, "tab:c1" ) );
    REQUIRE( itemEnabled( *picker, "all" ) );
    REQUIRE_FALSE( itemEnabled( *picker, "file:" ) );
    REQUIRE( window.findChild<QPushButton*>( "scriptRunnerRun" ) != nullptr );

    CommanderRequest stop;
    stop.action = CommanderAction::StopScript;
    stop.allEntities = true;
    supervisor.stopScript( stop );
}

#ifndef Q_OS_MACOS
namespace {
QString writeLog( const QTemporaryDir& directory, const QString& name, const QByteArray& content )
{
    const auto path = directory.filePath( name );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( content ) == content.size() );
    file.close();
    return path;
}

class WindowFixture {
public:
    explicit WindowFixture( const QString& windowId )
        : window_( WindowSession( session_, windowId, 0 ) )
    {
        window_.show();
    }

    MainWindow& window()
    {
        return window_;
    }

    QString open( const QString& path )
    {
        CommanderRequest request;
        request.action = CommanderAction::OpenFile;
        request.filePath = path;
        const auto result = window_.executeCommanderRequest( request );
        INFO( result.message.toStdString() );
        REQUIRE( result.ok() );
        REQUIRE( waitUiState( [ this ] { return window_.isStartupReadyForDisplay(); } ) );
        for ( const auto& value : window_.commanderWindowInfo().value( "tabs" ).toList() ) {
            if ( value.toMap().value( "filePath" ).toString() == path )
                return value.toMap().value( "tabId" ).toString();
        }
        FAIL( "tab not found" );
        return {};
    }

    QString currentTabId() const
    {
        return window_.commanderWindowInfo().value( "currentTabId" ).toString();
    }

    CrawlerWidget* crawler( const QString& tabId )
    {
        auto* pane = window_.paneForTabId( tabId );
        REQUIRE( pane != nullptr );
        return qobject_cast<CrawlerWidget*>(
            pane->tabs()->widget( pane->tabs()->findTabById( tabId ) ) );
    }

private:
    std::shared_ptr<Session> session_ = std::make_shared<Session>();
    MainWindow window_;
};
} // namespace

TEST_CASE( "preview_line_from_chosen_source: Preview reads a line of the chosen source",
           "[ui][workbench][U15]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto first = writeLog( directory, "first.log", "alpha one\nalpha two\nalpha three\n" );
    const auto second = writeLog( directory, "second.log", "beta one\nbeta two\n" );
    WindowFixture fixture( "U15bPreview" );
    auto& window = fixture.window();
    const auto firstTab = fixture.open( first );
    fixture.open( second );
    REQUIRE( fixture.currentTabId() != firstTab );

    CommanderRequest request;
    request.action = CommanderAction::PreviewMessage;
    request.analysisArguments = { { "line", 2 } };
    request.source = "tab:" + firstTab;
    auto result = window.executeCommanderRequest( request );
    INFO( result.message.toStdString() );
    REQUIRE( result.payload.value( "source" ).toMap().value( "tabId" ) == firstTab );
    REQUIRE( result.payload.value( "inputText" ).toString().contains( "alpha two" ) );
    // A file source names the same tab.
    request.source = "file:" + first;
    REQUIRE(
        window.executeCommanderRequest( request ).payload.value( "source" ).toMap().value( "tabId" )
        == firstTab );
    // A line number names one source.
    request.source = "all";
    result = window.executeCommanderRequest( request );
    REQUIRE( result.code == CommanderResultCode::InvalidRequest );
    REQUIRE( result.payload.value( "reason" ) == "invalid_source" );
    // A file that is not open is opened first.
    const auto third = writeLog( directory, "third.log", "gamma\n" );
    request.source = "file:" + third;
    result = window.executeCommanderRequest( request );
    REQUIRE( result.ok() );
    REQUIRE( result.payload.value( "opened" ) == third );
    // The focused tab is the window's current tab (the opened third.log), as before.
    request.source = "focused";
    request.analysisArguments = { { "line", 1 } };
    REQUIRE( waitUiState( [ & ] { return window.isStartupReadyForDisplay(); } ) );
    result = window.executeCommanderRequest( request );
    INFO( result.message.toStdString() );
    REQUIRE( result.payload.value( "inputText" ).toString().contains( "gamma" ) );
    REQUIRE( result.payload.value( "source" ).toMap().value( "tabId" ) == fixture.currentTabId() );

    // The Preview window: choose a source and a line, then Preview line.
    PreviewWindow* preview = nullptr;
    for ( auto* widget : QApplication::topLevelWidgets() ) {
        auto* candidate = qobject_cast<PreviewWindow*>( widget );
        if ( candidate && candidate->findChild<AnalysisSourcePicker*>( "previewSource" )
             && candidate->isAncestorOf(
                 candidate->findChild<AnalysisSourcePicker*>( "previewSource" ) ) ) {
            auto* candidateSource = candidate->findChild<AnalysisSourcePicker*>( "previewSource" );
            candidateSource->refresh();
            if ( candidateSource->findData( "tab:" + firstTab ) >= 0 )
                preview = candidate;
        }
    }
    REQUIRE( preview != nullptr );
    auto* source = preview->findChild<AnalysisSourcePicker*>( "previewSource" );
    REQUIRE_FALSE( itemEnabled( *source, "all" ) );
    REQUIRE( source->setSelection( "tab:" + firstTab ) );
    preview->findChild<QSpinBox*>( "previewLine" )->setValue( 3 );
    auto* tabs = preview->findChild<QTabWidget*>( "previewTabs" );
    const auto before = tabs->count();
    preview->findChild<QPushButton*>( "previewOpenLine" )->click();
    REQUIRE( tabs->count() == before + 1 );
    const auto edits = tabs->widget( tabs->count() - 1 )->findChildren<QPlainTextEdit*>();
    REQUIRE( std::any_of( edits.begin(), edits.end(), []( const QPlainTextEdit* edit ) {
        return edit->toPlainText().contains( "alpha three" );
    } ) );
}

TEST_CASE( "search_all_open_tabs: one search runs in every open tab", "[ui][workbench][U15]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto two = writeLog( directory, "two.log", "ERROR one\nok\nERROR two\n" );
    const auto one = writeLog( directory, "one.log", "fine\nERROR once\n" );
    const auto none = writeLog( directory, "none.log", "nothing here\n" );
    WindowFixture fixture( "U15bSearch" );
    auto& window = fixture.window();
    const auto twoTab = fixture.open( two );
    const auto oneTab = fixture.open( one );
    const auto noneTab = fixture.open( none );
    const auto current = fixture.currentTabId();

    CommanderRequest request;
    request.action = CommanderAction::Search;
    request.searchText = "ERROR";
    request.source = "all";
    request.waitForCompletion = true;
    request.timeoutMs = 10000;
    const auto result = window.executeCommanderRequest( request );
    INFO( result.message.toStdString() );
    REQUIRE( result.ok() );
    REQUIRE( result.payload.value( "complete" ).toBool() );
    REQUIRE( result.payload.value( "matches" ).toLongLong() == 3 );
    QMap<QString, int> counts;
    for ( const auto& value : result.payload.value( "tabs" ).toList() )
        counts[ value.toMap().value( "tabId" ).toString() ]
            = value.toMap().value( "matches" ).toInt();
    REQUIRE( counts == QMap<QString, int>{ { twoTab, 2 }, { oneTab, 1 }, { noneTab, 0 } } );
    // Searching every tab does not change the tab the user is looking at.
    REQUIRE( fixture.currentTabId() == current );

    // One chosen tab is searched like --tab-id and brought to the front.
    request.source = "tab:" + twoTab;
    request.searchText = "ok";
    REQUIRE( window.executeCommanderRequest( request ).ok() );
    REQUIRE( fixture.currentTabId() == twoTab );
    REQUIRE( waitUiState( [ & ] { return fixture.crawler( twoTab )->matchCount() == 1; } ) );
    REQUIRE( fixture.crawler( oneTab )->matchCount() == 1 ); // still "ERROR"

    // The search line's "In all open tabs" option runs the same search everywhere.
    auto* crawler = fixture.crawler( twoTab );
    auto* allTabs = crawler->findChild<QToolButton*>( "searchAllTabsButton" );
    REQUIRE( allTabs != nullptr );
    REQUIRE( allTabs->isCheckable() );
    REQUIRE_FALSE( allTabs->accessibleName().isEmpty() );
    allTabs->setChecked( true );
    crawler->findChild<QComboBox*>( "searchLineEdit" )->setEditText( "ERROR|nothing" );
    crawler->findChild<QToolButton*>( "searchButton" )->click();
    REQUIRE( waitUiState( [ & ] {
        return fixture.crawler( twoTab )->matchCount() + fixture.crawler( oneTab )->matchCount()
                   + fixture.crawler( noneTab )->matchCount()
               == 4;
    } ) );
    REQUIRE( fixture.currentTabId() == twoTab );
    REQUIRE(
        waitUiState( [ & ] { return crawler->searchStatusText().contains( "All open tabs" ); } ) );
    REQUIRE( crawler->searchStatusText().contains( "4" ) );
}

TEST_CASE( "com_tools_list_only_com_tabs: COM tools offer only live COM tabs",
           "[ui][workbench][U15]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto filePath = writeLog( directory, "file.log", "plain\n" );
    const auto capturePath = writeLog( directory, "capture.log", "captured\n" );
    WindowFixture fixture( "U15bCom" );
    auto& window = fixture.window();
    const auto fileTab = fixture.open( filePath );
    const auto captureTab = fixture.open( capturePath );
    auto* pane = window.paneForTabId( captureTab );
    REQUIRE( pane != nullptr );
    SerialCaptureSettings settings;
    settings.portName = QStringLiteral( "CILOGG_TEST_U15B_COM" );
    settings.filePath = capturePath;
    settings.useForActions = false;
    auto stream = std::make_shared<StreamSession>( settings );
    StreamSessionTestAccess::openConnection( *stream );
    pane->tabs()->setStreamSessionForPath( capturePath, stream );
    CommanderRequest actions;
    actions.action = CommanderAction::SetActionsEnabled;
    actions.tabId = captureTab;
    actions.enabled = true;
    REQUIRE( window.executeCommanderRequest( actions ).ok() );

    // The window's source list says which tab is a live COM capture.
    QMap<QString, QString> types;
    for ( const auto& value : window.toolSources() )
        types[ value.toMap().value( "tabId" ).toString() ]
            = value.toMap().value( "sourceType" ).toString();
    REQUIRE( types.value( fileTab ) == "file" );
    REQUIRE( types.value( captureTab ) == "com" );

    // A COM tool's picker offers the COM tab and disables the file tab.
    AnalysisSourcePicker picker( [ &window ] { return window.toolSources(); },
                                 SourceRules::liveComTabs() );
    REQUIRE( itemEnabled( picker, "tab:" + captureTab ) );
    REQUIRE_FALSE( itemEnabled( picker, "tab:" + fileTab ) );
    REQUIRE( resolveAnalysisSource( *AnalysisSource::parse( "all" ),
                                    OpenSource::fromVariantList( window.toolSources() ),
                                    SourceRules::liveComTabs() )
                 .tabIds
             == QStringList{ captureTab } );

    // The Actions window lists only COM tabs as destinations.
    ActionsResponsesWindow* actionsWindow = nullptr;
    for ( auto* widget : QApplication::topLevelWidgets() ) {
        if ( auto* candidate = qobject_cast<ActionsResponsesWindow*>( widget );
             candidate && candidate->objectName() == "actionsWindow"
             && candidate->findChild<QComboBox*>( "actionTabsComboBox" )->findData( captureTab )
                    >= 0 )
            actionsWindow = candidate;
    }
    REQUIRE( actionsWindow != nullptr );
    auto* destinations = actionsWindow->findChild<QComboBox*>( "actionTabsComboBox" );
    REQUIRE( destinations->findData( fileTab ) < 0 );
    for ( int index = 0; index < destinations->count(); ++index )
        REQUIRE( types.value( destinations->itemData( index ).toString() ) == "com" );
}
#endif
