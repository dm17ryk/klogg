/*
 * Copyright (C) 2026 CILogg contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

// U16a: split panes in the main window.

#include <catch2/catch.hpp>

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTranslator>

#include "test_utils.h"

#include "crawlerwidget.h"
#include "logpane.h"
#include "mainwindow.h"
#include "serialcaptureworker.h"
#include "session.h"
#include "sessioninfo.h"
#include "streamsession.h"
#include "../streamsession_testaccess.h"

namespace {
struct SessionRestoreGuard {
    SessionInfo previous = SessionInfo::getSynced();
    ~SessionRestoreGuard()
    {
        previous.save();
        SessionInfo::getSynced();
    }
};

QString writeLog( const QTemporaryDir& directory, const QString& name,
                  const QByteArray& content = "first line\nERROR one\nthird line\nERROR two\n" )
{
    const auto path = directory.filePath( name );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( content ) == content.size() );
    file.close();
    return path;
}

class PaneFixture {
  public:
    explicit PaneFixture( const QString& windowId )
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
        const auto tabId = tabIdForPath( path );
        REQUIRE_FALSE( tabId.isEmpty() );
        return tabId;
    }

    QVariantList tabs() const
    {
        return window_.commanderWindowInfo().value( "tabs" ).toList();
    }

    QString tabIdForPath( const QString& path ) const
    {
        for ( const auto& value : tabs() ) {
            const auto tab = value.toMap();
            if ( tab.value( "filePath" ).toString() == path ) {
                return tab.value( "tabId" ).toString();
            }
        }
        return {};
    }

    QVariantMap tabInfo( const QString& tabId ) const
    {
        for ( const auto& value : tabs() ) {
            const auto tab = value.toMap();
            if ( tab.value( "tabId" ).toString() == tabId ) {
                return tab;
            }
        }
        return {};
    }

    QStringList tabOrder() const
    {
        QStringList ids;
        for ( const auto& value : tabs() ) {
            ids.append( value.toMap().value( "tabId" ).toString() );
        }
        return ids;
    }

    QString currentTabId() const
    {
        return window_.commanderWindowInfo().value( "currentTabId" ).toString();
    }

    CommanderResult focusTab( const QString& tabId, std::optional<int> tabIndex = {} )
    {
        CommanderRequest request;
        request.action = CommanderAction::FocusTab;
        request.tabId = tabId;
        request.tabIndex = tabIndex;
        return window_.executeCommanderRequest( request );
    }

    QAction* action( const char* name )
    {
        auto* found = window_.findChild<QAction*>( name );
        REQUIRE( found != nullptr );
        return found;
    }

  private:
    std::shared_ptr<Session> session_ = std::make_shared<Session>();
    MainWindow window_;
};

QStringList paneTabIds( const LogPane* pane )
{
    QStringList ids;
    for ( int index = 0; index < pane->tabs()->count(); ++index ) {
        ids.append( pane->tabs()->tabIdAt( index ) );
    }
    return ids;
}

QStringList panePaths( const LogPane* pane )
{
    QStringList paths;
    for ( int index = 0; index < pane->tabs()->count(); ++index ) {
        paths.append( pane->tabs()->tabPathAt( index ) );
    }
    return paths;
}

// Answers the next close-pane confirmation with one of its buttons. Any other modal message
// box, or a confirmation without the requested button, is rejected so a test fails instead of
// waiting forever.
class ClosePaneDialogDriver {
  public:
    explicit ClosePaneDialogDriver( const QString& answer )
    {
        timer_.setInterval( 20 );
        QObject::connect( &timer_, &QTimer::timeout, &timer_, [ this, answer ] {
            auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() );
            if ( box == nullptr ) {
                return;
            }
            if ( box->objectName() != "closePaneDialog" ) {
                box->reject();
                return;
            }
            seen_ = true;
            text_ = box->text();
            for ( const auto* name : { "closePaneCloseTabsButton", "closePaneMoveTabsButton",
                                       "closePaneCancelButton" } ) {
                if ( box->findChild<QPushButton*>( name ) != nullptr ) {
                    ++buttons_;
                }
            }
            timer_.stop();
            if ( auto* button = box->findChild<QPushButton*>( answer ) ) {
                button->click();
            }
            else {
                box->reject();
            }
        } );
        timer_.start();
    }

    bool seen() const
    {
        return seen_;
    }

    int buttons() const
    {
        return buttons_;
    }

    QString text() const
    {
        return text_;
    }

  private:
    QTimer timer_;
    bool seen_ = false;
    int buttons_ = 0;
    QString text_;
};
} // namespace

TEST_CASE( "layout_round_trip: the pane layout is saved and restored with the session",
           "[ui][workbench][U16]" )
{
    SessionRestoreGuard restoreSession;
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto a = writeLog( directory, "a.log" );
    const auto b = writeLog( directory, "b.log" );
    const auto c = writeLog( directory, "c.log" );
    const auto d = writeLog( directory, "d.log" );
    const QString windowId = "U16Layout";

    QStringList savedPaneIds;
    QString savedActivePane;
    {
        PaneFixture fixture( windowId );
        auto& window = fixture.window();
        fixture.open( a );
        const auto tabB = fixture.open( b );
        const auto tabC = fixture.open( c );
        fixture.open( d );

        // H[ pane 1 (a, d), V[ pane 2 (b), pane 3 (c) ] ]
        auto* right = window.moveTabToNewPane( tabB, Qt::Horizontal );
        REQUIRE( right != nullptr );
        auto* below = window.splitPane( right, Qt::Vertical );
        REQUIRE( below != nullptr );
        REQUIRE( window.moveTabToPane( tabC, below ) );
        auto* first = window.panes().front();
        first->tabs()->setCurrentIndex( 0 );
        REQUIRE( fixture.focusTab( tabB ).ok() );
        REQUIRE( window.activePane() == right );
        REQUIRE( window.panes().size() == 3 );

        for ( const auto* pane : window.panes() ) {
            savedPaneIds.append( pane->paneId() );
        }
        savedActivePane = right->paneId();

        QTemporaryDir projectDirectory;
        REQUIRE( projectDirectory.isValid() );
        QString error;
        REQUIRE( window.saveProject( projectDirectory.filePath( "layout.cilogproj" ), &error ) );
        const auto stored = SessionInfo::getSynced().paneLayout( windowId );
        const auto parsed = PaneLayoutFormat::parse( stored, 4 );
        INFO( stored.toStdString() );
        REQUIRE( parsed.status == PaneLayoutFormat::Status::Valid );
        REQUIRE( parsed.layout.paneCount() == 3 );
        REQUIRE( parsed.layout.activePaneId == savedActivePane );
    }

    PaneFixture restored( windowId );
    auto& window = restored.window();
    auto* firstTabs = window.findChild<TabbedCrawlerWidget*>();
    window.reloadSession();
    REQUIRE( waitUiState( [ & ] {
        return window.isStartupReadyForDisplay() && restored.tabs().size() == 4;
    } ) );

    const auto panes = window.panes();
    REQUIRE( panes.size() == 3 );
    // The first pane keeps the window's original tab widget.
    REQUIRE( panes.at( 0 )->tabs() == firstTabs );
    for ( size_t index = 0; index < panes.size(); ++index ) {
        REQUIRE( panes.at( index )->paneId() == savedPaneIds.at( static_cast<int>( index ) ) );
    }
    REQUIRE( panePaths( panes.at( 0 ) ) == QStringList{ a, d } );
    REQUIRE( panePaths( panes.at( 1 ) ) == QStringList{ b } );
    REQUIRE( panePaths( panes.at( 2 ) ) == QStringList{ c } );
    REQUIRE( panes.at( 0 )->tabs()->currentIndex() == 0 );
    REQUIRE( window.activePane()->paneId() == savedActivePane );
    REQUIRE( restored.currentTabId() == restored.tabIdForPath( b ) );

    auto* outer = qobject_cast<QSplitter*>( panes.at( 0 )->parentWidget() );
    auto* inner = qobject_cast<QSplitter*>( panes.at( 1 )->parentWidget() );
    REQUIRE( outer != nullptr );
    REQUIRE( inner != nullptr );
    REQUIRE( outer->orientation() == Qt::Horizontal );
    REQUIRE( inner->orientation() == Qt::Vertical );
    REQUIRE( inner->parentWidget() == outer );

    // Merging back to one pane keeps the tab order and saves no layout.
    REQUIRE( restored.tabOrder()
             == QStringList{ restored.tabIdForPath( a ), restored.tabIdForPath( d ),
                             restored.tabIdForPath( b ), restored.tabIdForPath( c ) } );
    const auto orderBefore = restored.tabOrder();
    restored.action( "mergePanesAction" )->trigger();
    REQUIRE( window.panes().size() == 1 );
    REQUIRE( window.panes().front()->tabs() == firstTabs );
    REQUIRE( restored.tabOrder() == orderBefore );
    QTemporaryDir projectDirectory;
    REQUIRE( projectDirectory.isValid() );
    QString error;
    REQUIRE( window.saveProject( projectDirectory.filePath( "single.cilogproj" ), &error ) );
    REQUIRE( SessionInfo::getSynced().paneLayout( windowId ).isEmpty() );
}

TEST_CASE( "moved_tab_keeps_id_search_follow_capture: moving a tab keeps everything it owns",
           "[ui][workbench][U16]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto capturePath = writeLog( directory, "capture.log" );
    const auto otherPath = writeLog( directory, "other.log" );

    PaneFixture fixture( "U16Move" );
    auto& window = fixture.window();
    const auto tabId = fixture.open( capturePath );
    fixture.open( otherPath );

    auto* source = window.paneForTabId( tabId );
    REQUIRE( source != nullptr );
    auto* sourceTabs = source->tabs();
    auto* crawler
        = qobject_cast<CrawlerWidget*>( sourceTabs->widget( sourceTabs->findTabById( tabId ) ) );
    REQUIRE( crawler != nullptr );

    // A logical COM capture: the session is open without a device or worker.
    SerialCaptureSettings settings;
    settings.portName = QStringLiteral( "CILOGG_TEST_U16_MOVE" );
    settings.filePath = capturePath;
    settings.useForActions = false;
    auto stream = std::make_shared<StreamSession>( settings );
    StreamSessionTestAccess::openConnection( *stream );
    sourceTabs->setStreamSessionForPath( capturePath, stream );

    CommanderRequest actions;
    actions.action = CommanderAction::SetActionsEnabled;
    actions.tabId = tabId;
    actions.enabled = true;
    REQUIRE( window.executeCommanderRequest( actions ).ok() );

    CommanderRequest search;
    search.action = CommanderAction::Search;
    search.tabId = tabId;
    search.searchText = QStringLiteral( "ERROR" );
    REQUIRE( window.executeCommanderRequest( search ).ok() );
    REQUIRE( waitUiState( [ & ] { return crawler->matchCount() > 0; } ) );
    const auto matches = crawler->matchCount();

    CommanderRequest follow;
    follow.action = CommanderAction::SetFollowMode;
    follow.tabId = tabId;
    follow.enabled = true;
    REQUIRE( window.executeCommanderRequest( follow ).ok() );
    REQUIRE( crawler->isFollowEnabled() );

    // A running tab script is shown in the tab's metadata.
    sourceTabs->setTabScriptActive( tabId, true );
    const auto before = sourceTabs->tabData( sourceTabs->findTabById( tabId ) );
    REQUIRE( before.value( "connected" ).toBool() );
    REQUIRE( before.value( "actionsPort" ).toBool() );
    REQUIRE( before.value( "scriptActive" ).toBool() );

    auto* target = window.moveTabToNewPane( tabId, Qt::Horizontal );
    REQUIRE( target != nullptr );
    REQUIRE( target != source );
    auto* targetTabs = target->tabs();
    const auto index = targetTabs->findTabById( tabId );
    REQUIRE( index >= 0 );
    REQUIRE( sourceTabs->findTabById( tabId ) < 0 );

    // Same tab id, the same views, the same metadata and the same live session.
    REQUIRE( targetTabs->widget( index ) == crawler );
    const auto after = targetTabs->tabData( index );
    for ( const auto* key : { "tabId", "path", "connected", "actionsPort", "scriptActive",
                              "portName", "baudRate" } ) {
        INFO( key );
        REQUIRE( after.value( key ) == before.value( key ) );
    }
    REQUIRE( targetTabs->streamSessionForPath( capturePath ) == stream.get() );
    REQUIRE( sourceTabs->streamSessionForPath( capturePath ) == nullptr );
    REQUIRE( stream->isConnectionOpen() );
    REQUIRE( stream->actionsEnabled() );
    REQUIRE( targetTabs->tabText( index ).contains( "(actions)" ) );
    REQUIRE( targetTabs->tabText( index ).contains( "(py)" ) );
    REQUIRE( crawler->searchText() == QStringLiteral( "ERROR" ) );
    REQUIRE( crawler->matchCount() == matches );
    REQUIRE( crawler->isFollowEnabled() );

    // Commander and the info payload still reach the tab by its id.
    const auto info = fixture.tabInfo( tabId );
    REQUIRE( info.value( "filePath" ).toString() == capturePath );
    REQUIRE( info.value( "com" ).toMap().value( "connected" ).toBool() );
    REQUIRE( info.value( "com" ).toMap().value( "actionsEnabled" ).toBool() );
    REQUIRE( fixture.focusTab( fixture.tabIdForPath( otherPath ) ).ok() );
    REQUIRE( window.activePane() == source );
    REQUIRE( fixture.focusTab( tabId ).ok() );
    REQUIRE( window.activePane() == target );
    REQUIRE( fixture.currentTabId() == tabId );

    // Moving it back keeps it as well.
    REQUIRE( window.moveTabToPane( tabId, source ) );
    REQUIRE( sourceTabs->streamSessionForPath( capturePath ) == stream.get() );
    REQUIRE( sourceTabs->tabData( sourceTabs->findTabById( tabId ) ).value( "tabId" ) == tabId );
}

TEST_CASE( "close_pane_asks_close_or_move: closing a pane with tabs asks what to do with them",
           "[ui][workbench][U16]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    PaneFixture fixture( "U16Close" );
    auto& window = fixture.window();
    const auto a = fixture.open( writeLog( directory, "a.log" ) );
    const auto b = fixture.open( writeLog( directory, "b.log" ) );
    const auto c = fixture.open( writeLog( directory, "c.log" ) );
    auto* closePane = fixture.action( "closePaneAction" );
    REQUIRE_FALSE( closePane->isEnabled() );

    REQUIRE( window.moveTabToNewPane( c, Qt::Horizontal ) != nullptr );
    REQUIRE( closePane->isEnabled() );

    {
        ClosePaneDialogDriver cancel( "closePaneCancelButton" );
        closePane->trigger();
        REQUIRE( cancel.seen() );
        REQUIRE( cancel.buttons() == 3 );
        REQUIRE( cancel.text().contains( "1 tab" ) );
        REQUIRE( window.panes().size() == 2 );
        REQUIRE( fixture.tabs().size() == 3 );
    }

    {
        ClosePaneDialogDriver move( "closePaneMoveTabsButton" );
        closePane->trigger();
        REQUIRE( move.seen() );
        REQUIRE( window.panes().size() == 1 );
        REQUIRE( fixture.tabs().size() == 3 );
        REQUIRE( window.paneForTabId( c ) == window.panes().front() );
        REQUIRE( fixture.currentTabId() == c );
    }

    REQUIRE( window.moveTabToNewPane( b, Qt::Vertical ) != nullptr );
    {
        ClosePaneDialogDriver closeTabs( "closePaneCloseTabsButton" );
        closePane->trigger();
        REQUIRE( closeTabs.seen() );
        REQUIRE( window.panes().size() == 1 );
        REQUIRE( fixture.tabs().size() == 2 );
        REQUIRE( fixture.tabInfo( b ).isEmpty() );
        REQUIRE_FALSE( fixture.tabInfo( a ).isEmpty() );
    }

    // An empty pane closes without asking.
    REQUIRE( window.splitPane( window.activePane(), Qt::Horizontal ) != nullptr );
    REQUIRE( window.activePane()->tabs()->count() == 0 );
    {
        ClosePaneDialogDriver unexpected( "closePaneCancelButton" );
        closePane->trigger();
        REQUIRE_FALSE( unexpected.seen() );
        REQUIRE( window.panes().size() == 1 );
    }
    REQUIRE_FALSE( closePane->isEnabled() );

    // Automation chooses explicitly and never sees the dialog.
    REQUIRE( window.moveTabToNewPane( a, Qt::Horizontal ) != nullptr );
    REQUIRE( window.closePane( window.paneForTabId( a ), MainWindow::PaneTabsOnClose::Move ) );
    REQUIRE( window.panes().size() == 1 );
    REQUIRE( fixture.tabs().size() == 2 );
}

TEST_CASE( "focused_tab_is_active_pane: the focused tab is the current tab of the active pane",
           "[ui][workbench][U16]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    PaneFixture fixture( "U16Focus" );
    auto& window = fixture.window();
    const auto a = fixture.open( writeLog( directory, "a.log" ) );
    const auto b = fixture.open( writeLog( directory, "b.log" ) );

    auto* second = window.moveTabToNewPane( b, Qt::Horizontal );
    REQUIRE( second != nullptr );
    auto* first = window.panes().front();
    REQUIRE( window.activePane() == second );
    REQUIRE( fixture.currentTabId() == b );

    // Requests without a tab selector act on the active pane's tab.
    CommanderRequest filters;
    filters.action = CommanderAction::GetFilters;
    auto result = window.executeCommanderRequest( filters );
    REQUIRE( result.ok() );
    REQUIRE( result.payload.value( "tabId" ).toString() == b );
    REQUIRE( window.automationState().value( "activeTabTitle" ).toString().startsWith( "b.log" ) );

    // A click on the other pane's tab row activates that pane.
    auto* tabBar = first->tabs()->tabBar();
    REQUIRE( tabBar->isVisible() );
    QTest::mouseClick( tabBar, Qt::LeftButton, {}, tabBar->tabRect( 0 ).center() );
    REQUIRE( waitUiState( [ & ] { return window.activePane() == first; } ) );
    REQUIRE( fixture.currentTabId() == a );
    result = window.executeCommanderRequest( filters );
    REQUIRE( result.ok() );
    REQUIRE( result.payload.value( "tabId" ).toString() == a );

    // An empty active pane has no focused tab.
    REQUIRE( window.splitPane( first, Qt::Vertical ) != nullptr );
    REQUIRE( fixture.currentTabId().isEmpty() );
    REQUIRE_FALSE( window.executeCommanderRequest( filters ).ok() );
}

TEST_CASE( "tab_index_counts_across_panes: tab indices count pane by pane in layout order",
           "[ui][workbench][U16]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    PaneFixture fixture( "U16Index" );
    auto& window = fixture.window();
    const auto a = fixture.open( writeLog( directory, "a.log" ) );
    const auto b = fixture.open( writeLog( directory, "b.log" ) );
    const auto c = fixture.open( writeLog( directory, "c.log" ) );

    // Pane 1 holds b and c, pane 2 (right of it) holds a.
    auto* second = window.moveTabToNewPane( a, Qt::Horizontal );
    REQUIRE( second != nullptr );
    auto* first = window.panes().front();
    REQUIRE( paneTabIds( first ) == QStringList{ b, c } );
    REQUIRE( fixture.tabOrder() == QStringList{ b, c, a } );
    const auto tabs = fixture.tabs();
    for ( int index = 0; index < tabs.size(); ++index ) {
        REQUIRE( tabs.at( index ).toMap().value( "tabIndex" ).toInt() == index );
    }

    REQUIRE( fixture.focusTab( {}, 0 ).ok() );
    REQUIRE( fixture.currentTabId() == b );
    REQUIRE( window.activePane() == first );
    REQUIRE( fixture.focusTab( {}, 2 ).ok() );
    REQUIRE( fixture.currentTabId() == a );
    REQUIRE( window.activePane() == second );
    REQUIRE_FALSE( fixture.focusTab( {}, 3 ).ok() );

    CommanderRequest filters;
    filters.action = CommanderAction::GetFilters;
    filters.tabIndex = 1;
    const auto result = window.executeCommanderRequest( filters );
    REQUIRE( result.ok() );
    REQUIRE( result.payload.value( "tabId" ).toString() == c );
    REQUIRE( result.payload.value( "tabIndex" ).toInt() == 1 );

    // --tab-id keeps its meaning.
    REQUIRE( fixture.focusTab( c ).ok() );
    REQUIRE( fixture.currentTabId() == c );
    REQUIRE( window.activePane() == first );
}

TEST_CASE( "ninth_pane_is_refused: a window holds at most eight panes",
           "[ui][workbench][U16]" )
{
    PaneFixture fixture( "U16Limit" );
    auto& window = fixture.window();
    for ( int split = 1; split < LogPaneArea::MaxPanes; ++split ) {
        REQUIRE( window.splitPane( window.activePane(),
                                   split % 2 == 0 ? Qt::Vertical : Qt::Horizontal )
                 != nullptr );
    }
    REQUIRE( window.panes().size() == 8 );

    fixture.action( "splitPaneRightAction" )->trigger();
    REQUIRE( window.panes().size() == 8 );
    REQUIRE( window.statusBar()->currentMessage().contains( "at most 8 panes" ) );
    REQUIRE( window.splitPane( window.activePane(), Qt::Vertical ) == nullptr );
    REQUIRE( fixture.action( "splitPaneRightAction" )->shortcut()
             == QKeySequence( Qt::CTRL | Qt::ALT | Qt::Key_Right ) );
    REQUIRE( fixture.action( "splitPaneDownAction" )->shortcut()
             == QKeySequence( Qt::CTRL | Qt::ALT | Qt::Key_Down ) );

    fixture.action( "equalizePanesAction" )->trigger();
    fixture.action( "mergePanesAction" )->trigger();
    REQUIRE( window.panes().size() == 1 );
}

TEST_CASE( "old_session_loads_into_one_pane: sessions without a usable layout open in one pane",
           "[ui][workbench][U16]" )
{
    SessionRestoreGuard restoreSession;
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto a = writeLog( directory, "a.log" );
    const auto b = writeLog( directory, "b.log" );
    const QString windowId = "U16OldSession";

    QString layout;
    SECTION( "A session saved before split panes" ) {}
    SECTION( "A malformed layout" )
    {
        layout = QStringLiteral( "{not json" );
    }
    SECTION( "A newer layout version" )
    {
        layout = QStringLiteral( R"({"version":2,"root":{"type":"pane","id":"x"}})" );
    }
    SECTION( "A layout naming a file the session does not have" )
    {
        layout = QStringLiteral(
            R"({"version":1,"root":{"type":"split","orientation":"horizontal","children":[)"
            R"({"type":"pane","id":"p1","files":[0]},{"type":"pane","id":"p2","files":[5]}]}})" );
    }

    auto& sessionInfo = SessionInfo::getSynced();
    sessionInfo.add( windowId );
    sessionInfo.setOpenFiles( windowId, { SessionInfo::OpenFile{ a, 0, {} },
                                          SessionInfo::OpenFile{ b, 0, {} } } );
    sessionInfo.setPaneLayout( windowId, layout );
    sessionInfo.save();

    PaneFixture fixture( windowId );
    auto& window = fixture.window();
    auto* tabs = window.findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabs != nullptr );
    window.reloadSession();
    REQUIRE( waitUiState( [ & ] {
        return window.isStartupReadyForDisplay() && fixture.tabs().size() == 2;
    } ) );
    REQUIRE( window.panes().size() == 1 );
    REQUIRE( tabs->count() == 2 );
    REQUIRE( tabs->currentIndex() == 1 );
}

TEST_CASE( "Keyboard focus follows the active pane and pane commands follow the language",
           "[ui][workbench][U16]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    PaneFixture fixture( "U16FocusLanguage" );
    auto& window = fixture.window();
    const auto a = fixture.open( writeLog( directory, "a.log" ) );
    const auto b = fixture.open( writeLog( directory, "b.log" ) );

    const auto focusIsIn = [ &window ]( const LogPane* pane ) {
        auto* focused = window.focusWidget();
        return focused != nullptr && ( focused == pane || pane->isAncestorOf( focused ) );
    };

    // A split moves the keyboard focus into the new, empty pane.
    fixture.action( "splitPaneRightAction" )->trigger();
    REQUIRE( window.panes().size() == 2 );
    auto* created = window.activePane();
    REQUIRE( created->tabs()->count() == 0 );
    REQUIRE( focusIsIn( created ) );

    // focus_tab activates the tab's pane and moves the focus there.
    REQUIRE( fixture.focusTab( a ).ok() );
    REQUIRE( window.activePane() == window.panes().front() );
    REQUIRE( focusIsIn( window.panes().front() ) );
    REQUIRE( window.moveTabToPane( b, created ) );
    REQUIRE( focusIsIn( created ) );

    class PaneTranslator final : public QTranslator {
      public:
        bool isEmpty() const override
        {
            return false;
        }
        QString translate( const char* context, const char* sourceText, const char* = nullptr,
                           int = -1 ) const override
        {
            return qstrcmp( context, "MainWindow" ) == 0 || qstrcmp( context, "LogPane" ) == 0
                       ? QStringLiteral( "translated: " ) + QString::fromUtf8( sourceText )
                       : QString{};
        }
    } translator;
    struct TranslatorGuard {
        QTranslator* installed;
        ~TranslatorGuard()
        {
            QApplication::removeTranslator( installed );
        }
    } guard{ &translator };
    REQUIRE( QApplication::installTranslator( &translator ) );
    QEvent languageChange( QEvent::LanguageChange );
    QApplication::sendEvent( &window, &languageChange );

    REQUIRE( fixture.action( "splitPaneRightAction" )->text()
             == QStringLiteral( "translated: Split Right" ) );
    REQUIRE( fixture.action( "mergePanesAction" )->statusTip()
             == QStringLiteral( "translated: Move all tabs back into one pane" ) );
    auto* moveMenu = window.findChild<QMenu*>( "moveTabToPaneMenu" );
    REQUIRE( moveMenu != nullptr );
    REQUIRE( moveMenu->title() == QStringLiteral( "translated: Move Tab to Pane" ) );

    REQUIRE( window.splitPane( created, Qt::Vertical ) != nullptr );
    auto* placeholder = window.activePane()->findChild<QLabel*>( "logPanePlaceholder" );
    REQUIRE( placeholder != nullptr );
    QApplication::sendEvent( &window, &languageChange );
    REQUIRE( placeholder->text().startsWith( QStringLiteral( "translated: " ) ) );
}
