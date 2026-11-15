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

// U16b: pane state and pane operations for Commander, CLI, native MCP and Python.

namespace {
CommanderRequest paneRequest( CommanderAction action )
{
    CommanderRequest request;
    request.action = action;
    return request;
}

QVariantMap paneEntry( const QVariantMap& windowInfo, const QString& paneId )
{
    for ( const auto& value : windowInfo.value( "panes" ).toList() ) {
        const auto pane = value.toMap();
        if ( pane.value( "paneId" ).toString() == paneId ) {
            return pane;
        }
    }
    return {};
}

QVariantList orientationPath( std::initializer_list<std::pair<const char*, int>> steps )
{
    QVariantList path;
    for ( const auto& [ orientation, position ] : steps ) {
        path.append( QVariantMap{ { "orientation", QString::fromLatin1( orientation ) },
                                  { "position", position } } );
    }
    return path;
}

// Records (and closes) any modal dialog a request opens, so the test fails instead of
// blocking.
class NoModalDialogGuard {
  public:
    NoModalDialogGuard()
    {
        timer_.setInterval( 20 );
        QObject::connect( &timer_, &QTimer::timeout, &timer_, [ this ] {
            if ( auto* modal = QApplication::activeModalWidget() ) {
                seen_ = true;
                modal->close();
            }
        } );
        timer_.start();
    }

    bool seen() const
    {
        return seen_;
    }

  private:
    QTimer timer_;
    bool seen_ = false;
};
} // namespace

TEST_CASE( "pane_fields_in_get_info: get_info and state dumps report panes and each tab's pane",
           "[ui][workbench][U16]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    PaneFixture fixture( "U16PaneInfo" );
    auto& window = fixture.window();
    const auto a = fixture.open( writeLog( directory, "a.log" ) );

    // One pane: the layout is reported without splits.
    auto info = window.commanderWindowInfo();
    auto panes = info.value( "panes" ).toList();
    REQUIRE( panes.size() == 1 );
    const auto firstId = window.panes().front()->paneId();
    REQUIRE( info.value( "activePaneId" ).toString() == firstId );
    auto entry = panes.front().toMap();
    REQUIRE( entry.value( "paneId" ).toString() == firstId );
    REQUIRE( entry.value( "paneIndex" ).toInt() == 0 );
    REQUIRE( entry.value( "orientationPath" ).toList().isEmpty() );
    REQUIRE( entry.value( "tabIds" ).toStringList() == QStringList{ a } );
    REQUIRE( entry.value( "currentTabId" ).toString() == a );
    REQUIRE( entry.value( "isActive" ).toBool() );

    const auto b = fixture.open( writeLog( directory, "b.log" ) );
    const auto c = fixture.open( writeLog( directory, "c.log" ) );
    // H[ pane 0 (a, c), V[ pane 1 (b), pane 2 (empty, active) ] ]
    auto* second = window.moveTabToNewPane( b, Qt::Horizontal );
    REQUIRE( second != nullptr );
    auto* third = window.splitPane( second, Qt::Vertical );
    REQUIRE( third != nullptr );
    auto* first = window.panes().front();

    info = window.commanderWindowInfo();
    panes = info.value( "panes" ).toList();
    REQUIRE( panes.size() == 3 );
    REQUIRE( info.value( "activePaneId" ).toString() == third->paneId() );
    REQUIRE( info.value( "currentTabId" ).toString().isEmpty() );

    const auto p0 = paneEntry( info, first->paneId() );
    const auto p1 = paneEntry( info, second->paneId() );
    const auto p2 = paneEntry( info, third->paneId() );
    REQUIRE( p0.value( "paneIndex" ).toInt() == 0 );
    REQUIRE( p1.value( "paneIndex" ).toInt() == 1 );
    REQUIRE( p2.value( "paneIndex" ).toInt() == 2 );
    REQUIRE( p0.value( "tabIds" ).toStringList() == QStringList{ a, c } );
    REQUIRE( p1.value( "tabIds" ).toStringList() == QStringList{ b } );
    REQUIRE( p2.value( "tabIds" ).toStringList().isEmpty() );
    REQUIRE( p1.value( "currentTabId" ).toString() == b );
    REQUIRE( p2.value( "currentTabId" ).toString().isEmpty() );
    REQUIRE_FALSE( p0.value( "isActive" ).toBool() );
    REQUIRE( p2.value( "isActive" ).toBool() );
    REQUIRE( p0.value( "orientationPath" ).toList() == orientationPath( { { "horizontal", 0 } } ) );
    REQUIRE( p1.value( "orientationPath" ).toList()
             == orientationPath( { { "horizontal", 1 }, { "vertical", 0 } } ) );
    REQUIRE( p2.value( "orientationPath" ).toList()
             == orientationPath( { { "horizontal", 1 }, { "vertical", 1 } } ) );

    // Every tab names its pane; tabIndex stays window-wide in layout order.
    const auto expectTab = [ & ]( const QString& tabId, int tabIndex, const LogPane* pane,
                                  int paneIndex, int paneTabIndex ) {
        const auto tab = fixture.tabInfo( tabId );
        INFO( tabId.toStdString() );
        REQUIRE( tab.value( "tabIndex" ).toInt() == tabIndex );
        REQUIRE( tab.value( "paneId" ).toString() == pane->paneId() );
        REQUIRE( tab.value( "paneIndex" ).toInt() == paneIndex );
        REQUIRE( tab.value( "paneTabIndex" ).toInt() == paneTabIndex );
    };
    expectTab( a, 0, first, 0, 0 );
    expectTab( c, 1, first, 0, 1 );
    expectTab( b, 2, second, 1, 0 );

    // State dumps carry the same pane fields.
    const auto state = window.automationState();
    REQUIRE( state.value( "activePaneId" ).toString() == third->paneId() );
    REQUIRE( state.value( "activePaneIndex" ).toInt() == 2 );
    REQUIRE( state.value( "paneCount" ).toInt() == 3 );
    const auto snapshot = window.automationSnapshot();
    REQUIRE( snapshot.value( "windowInfo" ).toMap().value( "panes" ).toList().size() == 3 );
    REQUIRE( snapshot.value( "ciloggState" ).toMap().value( "activePaneId" ).toString()
             == third->paneId() );
}

TEST_CASE( "pane_operations: Commander splits, focuses, moves, closes and merges panes",
           "[ui][workbench][U16]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    PaneFixture fixture( "U16PaneCommander" );
    auto& window = fixture.window();
    const auto a = fixture.open( writeLog( directory, "a.log" ) );
    const auto b = fixture.open( writeLog( directory, "b.log" ) );
    const auto c = fixture.open( writeLog( directory, "c.log" ) );
    NoModalDialogGuard noDialogs;
    auto* first = window.panes().front();

    // split_pane without a selector splits the active pane to the right.
    auto result = window.executeCommanderRequest( paneRequest( CommanderAction::SplitPane ) );
    INFO( result.message.toStdString() );
    REQUIRE( result.ok() );
    REQUIRE( window.panes().size() == 2 );
    auto* right = window.panes().at( 1 );
    REQUIRE( result.payload.value( "paneId" ).toString() == right->paneId() );
    REQUIRE( result.payload.value( "paneIndex" ).toInt() == 1 );
    REQUIRE( result.payload.value( "activePaneId" ).toString() == right->paneId() );
    REQUIRE( result.payload.value( "panes" ).toList().size() == 2 );
    REQUIRE( window.activePane() == right );
    REQUIRE( paneEntry( window.commanderWindowInfo(), right->paneId() )
                 .value( "orientationPath" )
                 .toList()
             == orientationPath( { { "horizontal", 1 } } ) );

    // A tab selector splits the tab's pane; "down" stacks the new pane below it.
    auto split = paneRequest( CommanderAction::SplitPane );
    split.tabId = a;
    split.paneDirection = "down";
    result = window.executeCommanderRequest( split );
    REQUIRE( result.ok() );
    REQUIRE( window.panes().size() == 3 );
    auto* below = window.panes().at( 1 );
    REQUIRE( result.payload.value( "paneId" ).toString() == below->paneId() );
    REQUIRE( paneEntry( window.commanderWindowInfo(), below->paneId() )
                 .value( "orientationPath" )
                 .toList()
             == orientationPath( { { "horizontal", 0 }, { "vertical", 1 } } ) );

    split.tabId.clear();
    split.paneDirection = "left";
    REQUIRE( window.executeCommanderRequest( split ).code == CommanderResultCode::InvalidRequest );
    split.paneDirection.clear();
    split.paneId = "no-such-pane";
    REQUIRE( window.executeCommanderRequest( split ).code == CommanderResultCode::NotFound );
    split.paneId.clear();
    split.paneIndex = 7;
    REQUIRE( window.executeCommanderRequest( split ).code == CommanderResultCode::NotFound );
    // One selector kind at a time, also for requests that do not come through the CLI.
    split.paneIndex.reset();
    split.paneId = first->paneId();
    split.tabId = a;
    REQUIRE( window.executeCommanderRequest( split ).code == CommanderResultCode::InvalidRequest );
    REQUIRE( window.panes().size() == 3 );

    // focus_pane activates a pane and makes its current tab the focused tab.
    auto focus = paneRequest( CommanderAction::FocusPane );
    focus.paneIndex = 0;
    result = window.executeCommanderRequest( focus );
    REQUIRE( result.ok() );
    REQUIRE( window.activePane() == first );
    REQUIRE( fixture.currentTabId() == c );
    REQUIRE( result.payload.value( "paneId" ).toString() == first->paneId() );
    REQUIRE( result.payload.value( "currentTabId" ).toString() == c );
    focus.paneIndex.reset();
    focus.paneId = right->paneId();
    REQUIRE( window.executeCommanderRequest( focus ).ok() );
    REQUIRE( window.activePane() == right );
    REQUIRE( fixture.currentTabId().isEmpty() );
    focus.paneId = "no-such-pane";
    REQUIRE( window.executeCommanderRequest( focus ).code == CommanderResultCode::NotFound );
    REQUIRE( window.executeCommanderRequest( paneRequest( CommanderAction::FocusPane ) ).code
             == CommanderResultCode::InvalidRequest );
    focus.paneId = first->paneId();
    focus.tabId = a;
    REQUIRE( window.executeCommanderRequest( focus ).code == CommanderResultCode::InvalidRequest );
    REQUIRE( window.activePane() == right );

    // move_tab keeps the tab id; the destination is a pane id, a pane index or a new pane.
    auto move = paneRequest( CommanderAction::MoveTab );
    move.tabId = b;
    move.paneId = right->paneId();
    result = window.executeCommanderRequest( move );
    REQUIRE( result.ok() );
    REQUIRE( paneTabIds( right ) == QStringList{ b } );
    REQUIRE( result.payload.value( "tabId" ).toString() == b );
    REQUIRE( result.payload.value( "paneId" ).toString() == right->paneId() );
    REQUIRE( result.payload.value( "paneIndex" ).toInt() == 2 );
    REQUIRE( result.payload.value( "paneTabIndex" ).toInt() == 0 );
    REQUIRE( result.payload.value( "tabIndex" ).toInt() == 2 );
    REQUIRE( fixture.currentTabId() == b );
    REQUIRE( window.activePane() == right );

    move.tabId = c;
    move.paneId.clear();
    move.paneIndex = 1;
    REQUIRE( window.executeCommanderRequest( move ).ok() );
    REQUIRE( paneTabIds( below ) == QStringList{ c } );
    REQUIRE( fixture.tabOrder() == QStringList{ a, c, b } );

    // Moving a tab into its own pane changes nothing.
    REQUIRE( window.executeCommanderRequest( move ).ok() );
    REQUIRE( paneTabIds( below ) == QStringList{ c } );

    move.paneIndex.reset();
    move.paneDirection = "down";
    move.tabId = b;
    result = window.executeCommanderRequest( move );
    REQUIRE( result.ok() );
    REQUIRE( window.panes().size() == 4 );
    auto* created = window.paneForTabId( b );
    REQUIRE( created != right );
    REQUIRE( right->tabs()->count() == 0 );
    REQUIRE( result.payload.value( "paneId" ).toString() == created->paneId() );
    REQUIRE( paneEntry( window.commanderWindowInfo(), created->paneId() )
                 .value( "orientationPath" )
                 .toList()
             == orientationPath( { { "horizontal", 1 }, { "vertical", 1 } } ) );

    move.paneDirection.clear();
    REQUIRE( window.executeCommanderRequest( move ).code == CommanderResultCode::InvalidRequest );
    move.paneId = first->paneId();
    move.paneDirection = "down";
    REQUIRE( window.executeCommanderRequest( move ).code == CommanderResultCode::InvalidRequest );
    move.paneDirection.clear();
    move.tabId = "no-such-tab";
    REQUIRE( window.executeCommanderRequest( move ).code == CommanderResultCode::NotFound );
    // The tab is in this window, so a missing destination pane is a wrong request: routing
    // stops here instead of asking other windows.
    move.tabId = b;
    move.paneId = "no-such-pane";
    result = window.executeCommanderRequest( move );
    REQUIRE( result.code == CommanderResultCode::InvalidRequest );
    REQUIRE( result.message.contains( "no-such-pane" ) );

    // --tab-index keeps counting tabs pane by pane in layout order.
    REQUIRE( fixture.focusTab( {}, 2 ).ok() );
    REQUIRE( fixture.currentTabId() == b );
    REQUIRE( window.activePane() == created );

    // close_pane never asks: a pane with tabs needs tabs=close or tabs=move.
    auto close = paneRequest( CommanderAction::ClosePane );
    close.paneId = below->paneId();
    result = window.executeCommanderRequest( close );
    REQUIRE( result.code == CommanderResultCode::InvalidRequest );
    REQUIRE( result.message.contains( "tabs" ) );
    REQUIRE( window.panes().size() == 4 );
    close.paneTabs = "keep";
    REQUIRE( window.executeCommanderRequest( close ).code == CommanderResultCode::InvalidRequest );

    close.paneTabs = "move";
    result = window.executeCommanderRequest( close );
    REQUIRE( result.ok() );
    REQUIRE( window.panes().size() == 3 );
    REQUIRE( result.payload.value( "closedPaneId" ).toString() == below->paneId() );
    REQUIRE( result.payload.value( "movedTabIds" ).toStringList() == QStringList{ c } );
    REQUIRE( paneTabIds( first ) == QStringList{ a, c } );

    // An empty pane closes without tabs=...
    close.paneTabs.clear();
    close.paneId = right->paneId();
    REQUIRE( window.executeCommanderRequest( close ).ok() );
    REQUIRE( window.panes().size() == 2 );

    close.paneId = created->paneId();
    close.paneTabs = "close";
    result = window.executeCommanderRequest( close );
    REQUIRE( result.ok() );
    REQUIRE( result.payload.value( "closedTabIds" ).toStringList() == QStringList{ b } );
    REQUIRE( window.panes().size() == 1 );
    REQUIRE( fixture.tabInfo( b ).isEmpty() );

    close.paneId = first->paneId();
    close.paneTabs = "move";
    result = window.executeCommanderRequest( close );
    REQUIRE( result.code == CommanderResultCode::InvalidRequest );
    REQUIRE( window.panes().size() == 1 );

    // merge_panes moves every tab back into one pane and keeps the focused tab.
    REQUIRE( window.executeCommanderRequest( paneRequest( CommanderAction::SplitPane ) ).ok() );
    move = paneRequest( CommanderAction::MoveTab );
    move.tabId = a;
    move.paneIndex = 1;
    REQUIRE( window.executeCommanderRequest( move ).ok() );
    REQUIRE( fixture.tabOrder() == QStringList{ c, a } );
    // merge_panes acts on a whole window and refuses pane or tab selectors.
    auto merge = paneRequest( CommanderAction::MergePanes );
    merge.tabId = a;
    REQUIRE( window.executeCommanderRequest( merge ).code == CommanderResultCode::InvalidRequest );
    REQUIRE( window.panes().size() == 2 );
    result = window.executeCommanderRequest( paneRequest( CommanderAction::MergePanes ) );
    REQUIRE( result.ok() );
    REQUIRE( window.panes().size() == 1 );
    REQUIRE( fixture.tabOrder() == QStringList{ c, a } );
    REQUIRE( fixture.currentTabId() == a );
    REQUIRE( result.payload.value( "panes" ).toList().size() == 1 );
    REQUIRE( window.executeCommanderRequest( paneRequest( CommanderAction::MergePanes ) ).ok() );

    // A ninth pane is refused with the reason.
    for ( int panes = 1; panes < LogPaneArea::MaxPanes; ++panes ) {
        REQUIRE( window.executeCommanderRequest( paneRequest( CommanderAction::SplitPane ) ).ok() );
    }
    result = window.executeCommanderRequest( paneRequest( CommanderAction::SplitPane ) );
    REQUIRE( result.code == CommanderResultCode::ExecutionFailed );
    REQUIRE( result.message.contains( "at most 8 panes" ) );
    REQUIRE( window.panes().size() == 8 );
    move.paneIndex.reset();
    move.paneDirection = "right";
    REQUIRE( window.executeCommanderRequest( move ).code == CommanderResultCode::ExecutionFailed );

    REQUIRE_FALSE( noDialogs.seen() );
}
