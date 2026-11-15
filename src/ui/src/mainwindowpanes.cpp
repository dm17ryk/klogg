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

// Split panes of the main window: pane menus, split / move / close / merge and the layout saved
// with the session (U16a), and the pane state and pane commands for automation (U16b).

#include "mainwindow.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <optional>

#include "crawlerwidget.h"
#include "log.h"
#include "sessioninfo.h"

namespace {
constexpr int PaneStatusTimeoutMs = 6000;

void collectPaneNodes( const PaneLayoutNode& node, std::vector<const PaneLayoutNode*>& result )
{
    if ( node.isPane ) {
        result.push_back( &node );
        return;
    }
    for ( const auto& child : node.children ) {
        collectPaneNodes( child, result );
    }
}

// Keeps the keyboard in the active pane after its tabs or neighbours changed. A programmatic
// focus change never changes the active pane.
void focusCurrentTab( const LogPaneArea& area )
{
    if ( auto* current = area.currentWidget() ) {
        current->setFocus();
    }
}
} // namespace

LogPane* MainWindow::activePane() const
{
    return paneArea_->activePane();
}

std::vector<LogPane*> MainWindow::panes() const
{
    return paneArea_->panes();
}

LogPane* MainWindow::paneForTabId( const QString& tabId ) const
{
    return paneArea_->paneForTabId( tabId );
}

void MainWindow::createPaneActions()
{
    // Texts are set by retranslatePaneActions, which reTranslateUI also calls on a language
    // change.
    splitPaneRightAction = new QAction( this );
    splitPaneRightAction->setObjectName( QStringLiteral( "splitPaneRightAction" ) );
    splitPaneRightAction->setShortcut( QKeySequence( Qt::CTRL | Qt::ALT | Qt::Key_Right ) );
    connect( splitPaneRightAction, &QAction::triggered, this,
             [ this ] { splitPane( activePane(), Qt::Horizontal ); } );

    splitPaneDownAction = new QAction( this );
    splitPaneDownAction->setObjectName( QStringLiteral( "splitPaneDownAction" ) );
    splitPaneDownAction->setShortcut( QKeySequence( Qt::CTRL | Qt::ALT | Qt::Key_Down ) );
    connect( splitPaneDownAction, &QAction::triggered, this,
             [ this ] { splitPane( activePane(), Qt::Vertical ); } );

    closePaneAction = new QAction( this );
    closePaneAction->setObjectName( QStringLiteral( "closePaneAction" ) );
    connect( closePaneAction, &QAction::triggered, this,
             [ this ] { closePane( activePane(), PaneTabsOnClose::Ask ); } );

    equalizePanesAction = new QAction( this );
    equalizePanesAction->setObjectName( QStringLiteral( "equalizePanesAction" ) );
    connect( equalizePanesAction, &QAction::triggered, this, [ this ] { paneArea_->equalize(); } );

    mergePanesAction = new QAction( this );
    mergePanesAction->setObjectName( QStringLiteral( "mergePanesAction" ) );
    connect( mergePanesAction, &QAction::triggered, this, [ this ] { mergeAllPanes(); } );

    moveTabToPaneMenu = new QMenu( this );
    moveTabToPaneMenu->setObjectName( QStringLiteral( "moveTabToPaneMenu" ) );
    connect( moveTabToPaneMenu, &QMenu::aboutToShow, this, [ this ] {
        populateMoveToPaneMenu( moveTabToPaneMenu, paneArea_->tabIdAt( paneArea_->currentIndex() ) );
    } );

    retranslatePaneActions();
}

void MainWindow::retranslatePaneActions()
{
    if ( splitPaneRightAction == nullptr ) {
        return;
    }
    splitPaneRightAction->setText( tr( "Split Right" ) );
    splitPaneRightAction->setStatusTip( tr( "Open a new pane right of the active pane" ) );
    splitPaneDownAction->setText( tr( "Split Down" ) );
    splitPaneDownAction->setStatusTip( tr( "Open a new pane below the active pane" ) );
    closePaneAction->setText( tr( "Close Pane" ) );
    closePaneAction->setStatusTip(
        tr( "Close the active pane; its tabs can be closed or moved to the neighbouring pane" ) );
    equalizePanesAction->setText( tr( "Equalize Panes" ) );
    equalizePanesAction->setStatusTip( tr( "Give all panes the same size" ) );
    mergePanesAction->setText( tr( "Merge All Panes" ) );
    mergePanesAction->setStatusTip( tr( "Move all tabs back into one pane" ) );
    moveTabToPaneMenu->setTitle( tr( "Move Tab to Pane" ) );
}

void MainWindow::addPaneMenuEntries()
{
    viewMenu->addAction( splitPaneRightAction );
    viewMenu->addAction( splitPaneDownAction );
    viewMenu->addMenu( moveTabToPaneMenu );
    viewMenu->addAction( closePaneAction );
    viewMenu->addAction( equalizePanesAction );
    viewMenu->addAction( mergePanesAction );
}

void MainWindow::updatePaneActions()
{
    if ( closePaneAction == nullptr ) {
        return;
    }
    const auto severalPanes = paneArea_->paneCount() > 1;
    closePaneAction->setEnabled( severalPanes );
    equalizePanesAction->setEnabled( severalPanes );
    mergePanesAction->setEnabled( severalPanes );
}

QString MainWindow::paneTitle( const LogPane* pane ) const
{
    const auto number = paneArea_->paneIndexOf( pane ) + 1;
    const auto* tabs = pane->tabs();
    if ( tabs->count() == 0 ) {
        return tr( "Pane %1 (empty)" ).arg( number );
    }
    return tr( "Pane %1: %2 (%n tab(s))", nullptr, tabs->count() )
        .arg( number )
        .arg( tabs->tabDisplayNameAt( tabs->currentIndex() ) );
}

void MainWindow::populateMoveToPaneMenu( QMenu* menu, const QString& tabId )
{
    menu->clear();
    const auto* source = paneArea_->paneForTabId( tabId );
    if ( source == nullptr ) {
        auto* none = menu->addAction( tr( "No tab selected" ) );
        none->setEnabled( false );
        return;
    }

    for ( auto* pane : paneArea_->panes() ) {
        auto* action = menu->addAction( paneTitle( pane ) );
        action->setObjectName(
            QStringLiteral( "moveTabToPaneAction_%1" ).arg( paneArea_->paneIndexOf( pane ) ) );
        action->setEnabled( pane != source );
        QPointer<LogPane> target = pane;
        connect( action, &QAction::triggered, this, [ this, tabId, target ] {
            if ( target ) {
                moveTabToPane( tabId, target );
            }
        } );
    }

    menu->addSeparator();
    auto* right = menu->addAction( tr( "New Pane Right" ) );
    right->setObjectName( QStringLiteral( "moveTabToNewPaneRightAction" ) );
    connect( right, &QAction::triggered, this,
             [ this, tabId ] { moveTabToNewPane( tabId, Qt::Horizontal ); } );
    auto* down = menu->addAction( tr( "New Pane Down" ) );
    down->setObjectName( QStringLiteral( "moveTabToNewPaneDownAction" ) );
    connect( down, &QAction::triggered, this,
             [ this, tabId ] { moveTabToNewPane( tabId, Qt::Vertical ); } );
}

void MainWindow::extendTabContextMenu( QMenu& menu, LogPane* pane, int index )
{
    const auto tabId = pane->tabs()->tabIdAt( index );
    QPointer<LogPane> safePane = pane;

    menu.addSeparator();
    auto* moveMenu = menu.addMenu( tr( "Move to Pane" ) );
    moveMenu->setObjectName( QStringLiteral( "tabMoveToPaneMenu" ) );
    populateMoveToPaneMenu( moveMenu, tabId );

    auto* right = menu.addAction( tr( "Split Right" ) );
    connect( right, &QAction::triggered, this, [ this, safePane ] {
        if ( safePane ) {
            splitPane( safePane, Qt::Horizontal );
        }
    } );
    auto* down = menu.addAction( tr( "Split Down" ) );
    connect( down, &QAction::triggered, this, [ this, safePane ] {
        if ( safePane ) {
            splitPane( safePane, Qt::Vertical );
        }
    } );
    auto* closeEntry = menu.addAction( tr( "Close Pane" ) );
    closeEntry->setEnabled( paneArea_->paneCount() > 1 );
    connect( closeEntry, &QAction::triggered, this, [ this, safePane ] {
        if ( safePane ) {
            closePane( safePane, PaneTabsOnClose::Ask );
        }
    } );
}

LogPane* MainWindow::splitPane( LogPane* pane, Qt::Orientation orientation )
{
    if ( pane == nullptr ) {
        pane = activePane();
    }
    if ( paneArea_->paneCount() >= LogPaneArea::MaxPanes ) {
        const auto message = paneLimitMessage();
        statusBar()->showMessage( message, PaneStatusTimeoutMs );
        LOG_INFO << "Split refused: " << message;
        return nullptr;
    }

    auto* created = paneArea_->split( pane, orientation );
    if ( created == nullptr ) {
        return nullptr;
    }
    paneArea_->setActivePane( created );
    // Keyboard focus follows the active pane, so tab shortcuts act on the new pane.
    created->setFocus();
    statusBar()->showMessage( tr( "New pane: files you open now open here; move tabs here with "
                                  "Move to Pane." ),
                              PaneStatusTimeoutMs );
    updatePaneActions();
    return created;
}

bool MainWindow::moveTabToPane( const QString& tabId, LogPane* target )
{
    const auto index = paneArea_->findTabById( tabId );
    const auto ref = paneArea_->tabRef( index );
    if ( !ref.isValid() || target == nullptr || paneArea_->paneIndexOf( target ) < 0 ) {
        LOG_WARNING << "Move tab to pane refused: tab_id=" << tabId;
        return false;
    }

    if ( ref.pane != target ) {
        const auto moved = paneArea_->moveTab( ref.pane, ref.index, target );
        if ( moved < 0 ) {
            return false;
        }
    }

    paneArea_->setCurrentIndex( paneArea_->findTabById( tabId ) );
    updateOpenedFilesMenu();
    updateActionsSendState();
    updatePaneActions();
    focusCurrentTab( *paneArea_ );
    return true;
}

LogPane* MainWindow::moveTabToNewPane( const QString& tabId, Qt::Orientation orientation )
{
    auto* source = paneArea_->paneForTabId( tabId );
    if ( source == nullptr ) {
        return nullptr;
    }
    auto* created = splitPane( source, orientation );
    if ( created == nullptr ) {
        return nullptr;
    }
    if ( !moveTabToPane( tabId, created ) ) {
        // Do not leave an empty pane behind for a move that did not happen.
        paneArea_->removePane( created );
        updatePaneActions();
        return nullptr;
    }
    return created;
}

QString MainWindow::paneLimitMessage() const
{
    return tr( "A window holds at most %1 panes; close or merge a pane first." )
        .arg( LogPaneArea::MaxPanes );
}

bool MainWindow::closePane( LogPane* pane, PaneTabsOnClose tabs )
{
    return closePane( pane, tabs, ActionInitiator::User );
}

bool MainWindow::closePane( LogPane* pane, PaneTabsOnClose tabs, ActionInitiator initiator )
{
    if ( pane == nullptr || paneArea_->paneIndexOf( pane ) < 0 ) {
        return false;
    }
    if ( paneArea_->paneCount() <= 1 ) {
        statusBar()->showMessage( tr( "The last pane cannot be closed." ), PaneStatusTimeoutMs );
        return false;
    }

    QPointer<LogPane> safePane = pane;
    const auto tabCount = pane->tabs()->count();
    if ( tabCount > 0 ) {
        auto choice = tabs;
        if ( choice == PaneTabsOnClose::Ask ) {
            QMessageBox box( QMessageBox::Question, tr( "Close Pane" ),
                             tr( "Pane %1 has %n tab(s).", nullptr, tabCount )
                                 .arg( paneArea_->paneIndexOf( pane ) + 1 ),
                             QMessageBox::NoButton, this );
            box.setObjectName( QStringLiteral( "closePaneDialog" ) );
            box.setInformativeText(
                tr( "Close all its tabs, or move them to the neighbouring pane?" ) );
            auto* closeButton = box.addButton( tr( "Close All Its Tabs" ),
                                               QMessageBox::DestructiveRole );
            closeButton->setObjectName( QStringLiteral( "closePaneCloseTabsButton" ) );
            auto* moveButton = box.addButton( tr( "Move Them to the Neighbouring Pane" ),
                                              QMessageBox::AcceptRole );
            moveButton->setObjectName( QStringLiteral( "closePaneMoveTabsButton" ) );
            auto* cancelButton = box.addButton( QMessageBox::Cancel );
            cancelButton->setObjectName( QStringLiteral( "closePaneCancelButton" ) );
            box.setDefaultButton( moveButton );
            box.setEscapeButton( cancelButton );
            box.exec();

            if ( box.clickedButton() == closeButton ) {
                choice = PaneTabsOnClose::Close;
            }
            else if ( box.clickedButton() == moveButton ) {
                choice = PaneTabsOnClose::Move;
            }
            else {
                LOG_DEBUG << "Close pane cancelled";
                return false;
            }
        }
        if ( !safePane ) {
            return false;
        }

        if ( choice == PaneTabsOnClose::Move ) {
            auto* neighbour = paneArea_->neighbourPane( safePane );
            if ( neighbour == nullptr ) {
                return false;
            }
            const auto currentTabId
                = safePane->tabs()->tabIdAt( safePane->tabs()->currentIndex() );
            while ( safePane && safePane->tabs()->count() > 0 ) {
                if ( paneArea_->moveTab( safePane, 0, neighbour ) < 0 ) {
                    LOG_WARNING << "Close pane stopped: a tab could not be moved";
                    return false;
                }
            }
            paneArea_->setActivePane( neighbour );
            if ( !currentTabId.isEmpty() ) {
                paneArea_->setCurrentIndex( paneArea_->findTabById( currentTabId ) );
            }
            LOG_INFO << "Close pane: moved " << tabCount << " tab(s) to the neighbouring pane";
        }
        else {
            while ( safePane && safePane->tabs()->count() > 0 ) {
                const auto before = safePane->tabs()->count();
                closeTab( paneArea_->globalIndex( safePane, 0 ), initiator );
                if ( safePane && safePane->tabs()->count() >= before ) {
                    LOG_WARNING << "Close pane stopped: a tab could not be closed";
                    return false;
                }
            }
            LOG_INFO << "Close pane: closed " << tabCount << " tab(s)";
        }
    }

    if ( !safePane ) {
        return false;
    }
    const auto removed = paneArea_->removePane( safePane );
    updateOpenedFilesMenu();
    updateActionsSendState();
    updatePaneActions();
    focusCurrentTab( *paneArea_ );
    return removed;
}

void MainWindow::mergeAllPanes()
{
    const auto all = paneArea_->panes();
    if ( all.size() <= 1 ) {
        return;
    }

    // Appending pane by pane keeps the window-wide tab order.
    const auto currentTabId = paneArea_->tabIdAt( paneArea_->currentIndex() );
    auto* target = all.front();
    for ( size_t index = 1; index < all.size(); ++index ) {
        auto* pane = all.at( index );
        while ( pane->tabs()->count() > 0 ) {
            if ( paneArea_->moveTab( pane, 0, target ) < 0 ) {
                LOG_WARNING << "Merge panes stopped: a tab could not be moved";
                return;
            }
        }
    }
    paneArea_->setActivePane( target );
    for ( size_t index = 1; index < all.size(); ++index ) {
        paneArea_->removePane( all.at( index ) );
    }
    if ( !currentTabId.isEmpty() ) {
        paneArea_->setCurrentIndex( paneArea_->findTabById( currentTabId ) );
    }

    LOG_INFO << "Merged all panes: tabs=" << paneArea_->count();
    statusBar()->showMessage( tr( "All tabs are in one pane." ), PaneStatusTimeoutMs );
    updateOpenedFilesMenu();
    updateActionsSendState();
    updatePaneActions();
    focusCurrentTab( *paneArea_ );
}

QString
MainWindow::capturePaneLayout( const std::map<const QWidget*, int>& savedIndexByWidget ) const
{
    // A single pane saves no layout, so the session looks exactly as before split panes.
    if ( paneArea_->paneCount() <= 1 ) {
        return {};
    }

    const auto layout = paneArea_->captureLayout( [ &savedIndexByWidget ]( const LogPane& pane,
                                                                           int index ) {
        const auto saved = savedIndexByWidget.find( pane.tabs()->widget( index ) );
        return saved == savedIndexByWidget.end() ? -1 : saved->second;
    } );
    return PaneLayoutFormat::serialize( layout );
}

bool MainWindow::applySavedPaneLayout( PaneLayout& layout,
                                       std::map<int, LogPane*>& paneBySavedIndex )
{
    if ( paneArea_->count() > 0 ) {
        LOG_DEBUG << "Session restored into a window with tabs: its panes are kept";
        return false;
    }

    const auto& sessionInfo = SessionInfo::getSynced();
    const auto windowId = session_.windowId();
    const auto fileCount = static_cast<int>( sessionInfo.openFiles( windowId ).size() );
    const auto parsed = PaneLayoutFormat::parse( sessionInfo.paneLayout( windowId ), fileCount );
    if ( parsed.status != PaneLayoutFormat::Status::Valid ) {
        if ( parsed.status != PaneLayoutFormat::Status::Absent ) {
            LOG_WARNING << "Ignoring the saved pane layout of window " << windowId << ": "
                        << parsed.error;
        }
        paneArea_->resetToSinglePane();
        return false;
    }
    if ( !paneArea_->applyLayout( parsed.layout ) ) {
        return false;
    }

    layout = parsed.layout;
    std::vector<const PaneLayoutNode*> nodes;
    collectPaneNodes( layout.root, nodes );
    for ( const auto* node : nodes ) {
        auto* pane = paneArea_->paneById( node->paneId );
        for ( const auto file : node->files ) {
            paneBySavedIndex.emplace( file, pane );
        }
    }
    LOG_INFO << "Restoring pane layout of window " << windowId
             << ": panes=" << paneArea_->paneCount();
    return true;
}

void MainWindow::restorePaneSelection( const PaneLayout& layout,
                                       const OpenedFilesList& openedFiles )
{
    std::map<int, QWidget*> crawlerBySavedIndex;
    for ( const auto& file : openedFiles ) {
        crawlerBySavedIndex.emplace( file.savedIndex, static_cast<CrawlerWidget*>( file.view ) );
    }

    std::vector<const PaneLayoutNode*> nodes;
    collectPaneNodes( layout.root, nodes );
    for ( const auto* node : nodes ) {
        auto* pane = paneArea_->paneById( node->paneId );
        if ( pane == nullptr || pane->tabs()->count() == 0 ) {
            continue;
        }
        const auto crawler = crawlerBySavedIndex.find( node->currentFile );
        if ( crawler != crawlerBySavedIndex.end()
             && pane->tabs()->indexOf( crawler->second ) >= 0 ) {
            pane->tabs()->setCurrentWidget( crawler->second );
        }
        else {
            // The pane's current file was not restored: select its last tab, as before panes.
            pane->tabs()->setCurrentIndex( pane->tabs()->count() - 1 );
        }
    }
    paneArea_->setActivePane( paneArea_->paneById( layout.activePaneId ) );
    currentTabChanged( paneArea_->currentIndex() );
}

// Pane state and pane commands for Commander, CLI, native MCP and Python (U16b).

namespace {
QString paneOrientationName( Qt::Orientation orientation )
{
    return orientation == Qt::Horizontal ? QStringLiteral( "horizontal" )
                                         : QStringLiteral( "vertical" );
}

// "right" (the default) opens the new pane right of the pane, "down" below it.
std::optional<Qt::Orientation> paneSplitOrientation( const QString& direction )
{
    const auto side = direction.trimmed().toLower();
    if ( side.isEmpty() || side == QStringLiteral( "right" ) ) {
        return Qt::Horizontal;
    }
    if ( side == QStringLiteral( "down" ) ) {
        return Qt::Vertical;
    }
    return std::nullopt;
}

QStringList paneTabIdList( const LogPane& pane )
{
    QStringList ids;
    const auto* tabs = pane.tabs();
    for ( int index = 0; index < tabs->count(); ++index ) {
        ids.append( tabs->tabIdAt( index ) );
    }
    return ids;
}

QString paneCurrentTabId( const LogPane& pane )
{
    const auto* tabs = pane.tabs();
    const auto current = tabs->currentIndex();
    return current >= 0 ? tabs->tabIdAt( current ) : QString{};
}
} // namespace

QVariantList MainWindow::paneOrientationPath( LogPane* pane ) const
{
    // From the outermost splitter down to the pane: each splitter's orientation and the position
    // of the branch that holds the pane. A window with one pane has an empty path.
    QVariantList path;
    QWidget* child = pane;
    for ( auto* parent = pane->parentWidget(); parent != nullptr && parent != paneArea_;
          parent = parent->parentWidget() ) {
        if ( const auto* splitter = qobject_cast<QSplitter*>( parent ) ) {
            path.prepend( QVariantMap{
                { QStringLiteral( "orientation" ), paneOrientationName( splitter->orientation() ) },
                { QStringLiteral( "position" ), splitter->indexOf( child ) } } );
        }
        child = parent;
    }
    return path;
}

QVariantList MainWindow::commanderPaneList() const
{
    QVariantList result;
    const auto* active = paneArea_->activePane();
    const auto all = paneArea_->panes();
    for ( size_t position = 0; position < all.size(); ++position ) {
        auto* pane = all.at( position );
        result.append( QVariantMap{
            { QStringLiteral( "paneId" ), pane->paneId() },
            { QStringLiteral( "paneIndex" ), static_cast<int>( position ) },
            { QStringLiteral( "orientationPath" ), paneOrientationPath( pane ) },
            { QStringLiteral( "tabIds" ), paneTabIdList( *pane ) },
            { QStringLiteral( "currentTabId" ), paneCurrentTabId( *pane ) },
            { QStringLiteral( "isActive" ), pane == active } } );
    }
    return result;
}

QVariantMap MainWindow::commanderPaneLayout() const
{
    const auto* active = paneArea_->activePane();
    return { { QStringLiteral( "windowIndex" ), static_cast<int>( session_.windowIndex() ) },
             { QStringLiteral( "activePaneId" ),
               active != nullptr ? active->paneId() : QString{} },
             { QStringLiteral( "panes" ), commanderPaneList() } };
}

QVariantMap MainWindow::commanderPanePayload( LogPane* pane ) const
{
    auto payload = commanderPaneLayout();
    payload.insert( QStringLiteral( "paneId" ), pane->paneId() );
    payload.insert( QStringLiteral( "paneIndex" ), paneArea_->paneIndexOf( pane ) );
    payload.insert( QStringLiteral( "currentTabId" ), paneCurrentTabId( *pane ) );
    return payload;
}

LogPane* MainWindow::commanderTargetPane( const CommanderRequest& request, bool allowTab,
                                          bool allowActive, CommanderResult* failure ) const
{
    // One selector kind only: the CLI enforces this, scripts and the IPC transport rely on it.
    const auto hasPane = !request.paneId.isEmpty() || request.paneIndex.has_value();
    const auto hasTab = !request.tabId.isEmpty() || request.tabIndex.has_value();
    if ( !request.paneId.isEmpty() && request.paneIndex ) {
        *failure = commanderFailure( CommanderResultCode::InvalidRequest,
                                     tr( "Use either a pane id or a pane index." ) );
        return nullptr;
    }
    if ( allowTab && hasPane && hasTab ) {
        *failure = commanderFailure( CommanderResultCode::InvalidRequest,
                                     tr( "Use either a pane selector or a tab selector." ) );
        return nullptr;
    }

    if ( !request.paneId.isEmpty() ) {
        if ( auto* pane = paneArea_->paneById( request.paneId ) ) {
            return pane;
        }
        *failure = commanderFailure( CommanderResultCode::NotFound,
                                     tr( "Pane %1 was not found." ).arg( request.paneId ) );
        return nullptr;
    }
    if ( request.paneIndex ) {
        const auto all = paneArea_->panes();
        const auto index = *request.paneIndex;
        if ( index >= 0 && static_cast<size_t>( index ) < all.size() ) {
            return all.at( static_cast<size_t>( index ) );
        }
        *failure = commanderFailure( CommanderResultCode::NotFound,
                                     tr( "Pane at index %1 was not found." ).arg( index ) );
        return nullptr;
    }
    if ( allowTab && ( !request.tabId.isEmpty() || request.tabIndex ) ) {
        const auto index = request.tabId.isEmpty() ? *request.tabIndex
                                                   : paneArea_->findTabById( request.tabId );
        const auto ref = paneArea_->tabRef( index );
        if ( ref.isValid() ) {
            return ref.pane;
        }
        *failure = commanderFailure(
            CommanderResultCode::NotFound,
            tr( "Open tab %1 was not found." )
                .arg( request.tabId.isEmpty() ? QString::number( index ) : request.tabId ) );
        return nullptr;
    }
    if ( allowActive && paneArea_->activePane() != nullptr ) {
        return paneArea_->activePane();
    }
    *failure = commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "%1 requires a pane id or a pane index." )
                                     .arg( commanderActionToString( request.action ) ) );
    return nullptr;
}

std::optional<CommanderResult> MainWindow::commanderRejectTabSelector(
    const CommanderRequest& request ) const
{
    if ( request.tabId.isEmpty() && !request.tabIndex ) {
        return std::nullopt;
    }
    return commanderFailure( CommanderResultCode::InvalidRequest,
                             tr( "%1 does not take a tab selector." )
                                 .arg( commanderActionToString( request.action ) ) );
}

CommanderResult MainWindow::commanderSplitPane( const CommanderRequest& request )
{
    const auto orientation = paneSplitOrientation( request.paneDirection );
    if ( !orientation ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "The pane direction must be right or down." ) );
    }
    CommanderResult failure;
    auto* pane = commanderTargetPane( request, true, true, &failure );
    if ( pane == nullptr ) {
        return failure;
    }
    if ( paneArea_->paneCount() >= LogPaneArea::MaxPanes ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed, paneLimitMessage() );
    }

    auto* created = splitPane( pane, *orientation );
    if ( created == nullptr ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed,
                                 tr( "The pane could not be split." ) );
    }
    LOG_INFO << "Commander split pane " << pane->paneId()
             << ( *orientation == Qt::Horizontal ? " right" : " down" ) << ": new pane "
             << created->paneId();
    return commanderSuccess( {}, commanderPanePayload( created ) );
}

CommanderResult MainWindow::commanderFocusPane( const CommanderRequest& request )
{
    if ( auto rejected = commanderRejectTabSelector( request ) ) {
        return *rejected;
    }
    CommanderResult failure;
    auto* pane = commanderTargetPane( request, false, false, &failure );
    if ( pane == nullptr ) {
        return failure;
    }

    paneArea_->setActivePane( pane );
    // Keyboard focus follows the active pane, as after a click into it.
    if ( auto* current = pane->tabs()->currentWidget() ) {
        current->setFocus();
    }
    else {
        pane->setFocus();
    }
    setWindowState( windowState() & ~Qt::WindowMinimized );
    show();
    raise();
    activateWindow();
    LOG_INFO << "Commander focused pane " << pane->paneId();
    return commanderSuccess( {}, commanderPanePayload( pane ) );
}

CommanderResult MainWindow::commanderClosePane( const CommanderRequest& request )
{
    // Request errors are answered before the pane lookup; a pane id that this window does not
    // hold is NotFound, so KloggApp offers the request to the next window.
    const auto mode = request.paneTabs.trimmed().toLower();
    if ( !mode.isEmpty() && mode != QStringLiteral( "close" ) && mode != QStringLiteral( "move" ) ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "tabs must be close or move." ) );
    }
    if ( auto rejected = commanderRejectTabSelector( request ) ) {
        return *rejected;
    }
    CommanderResult failure;
    auto* pane = commanderTargetPane( request, false, false, &failure );
    if ( pane == nullptr ) {
        return failure;
    }
    if ( paneArea_->paneCount() <= 1 ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "The last pane cannot be closed." ) );
    }

    // Automation never sees the close-pane confirmation: a pane with tabs needs an answer.
    const auto tabIds = paneTabIdList( *pane );
    if ( !tabIds.isEmpty() && mode.isEmpty() ) {
        return commanderFailure(
            CommanderResultCode::InvalidRequest,
            tr( "Pane %1 has %n tab(s): set tabs to close (close them) or move (move them to the "
                "neighbouring pane).",
                nullptr, static_cast<int>( tabIds.size() ) )
                .arg( paneArea_->paneIndexOf( pane ) + 1 ) );
    }

    const auto closedPaneId = pane->paneId();
    const auto choice
        = mode == QStringLiteral( "move" ) ? PaneTabsOnClose::Move : PaneTabsOnClose::Close;
    if ( !closePane( pane, choice, ActionInitiator::App ) ) {
        // Some tabs may have moved or closed already: report where everything is now.
        return { CommanderResultCode::ExecutionFailed,
                 tr( "Pane %1 could not be closed." ).arg( closedPaneId ), commanderPaneLayout() };
    }

    auto payload = commanderPaneLayout();
    payload.insert( QStringLiteral( "closedPaneId" ), closedPaneId );
    payload.insert( QStringLiteral( "movedTabIds" ),
                    choice == PaneTabsOnClose::Move ? tabIds : QStringList{} );
    payload.insert( QStringLiteral( "closedTabIds" ),
                    choice == PaneTabsOnClose::Close ? tabIds : QStringList{} );
    LOG_INFO << "Commander closed pane " << closedPaneId << " tabs=" << tabIds.size()
             << " mode=" << ( mode.isEmpty() ? QStringLiteral( "none" ) : mode );
    return commanderSuccess( {}, payload );
}

CommanderResult MainWindow::commanderMergePanes( const CommanderRequest& request )
{
    // merge_panes acts on a whole window: a pane or tab selector would only pick a window by
    // accident (KloggApp offers such requests to every window), so it is refused.
    if ( !request.paneId.isEmpty() || request.paneIndex || !request.tabId.isEmpty()
         || request.tabIndex ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "merge_panes takes only a window index." ) );
    }
    mergeAllPanes();
    if ( paneArea_->paneCount() != 1 ) {
        return { CommanderResultCode::ExecutionFailed, tr( "Not every pane could be merged." ),
                 commanderPaneLayout() };
    }
    return commanderSuccess( {}, commanderPanePayload( paneArea_->activePane() ) );
}

CommanderResult MainWindow::commanderMoveTab( const CommanderRequest& request )
{
    if ( request.tabId.isEmpty() ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "move_tab requires a tab id." ) );
    }
    const auto destinations = ( request.paneId.isEmpty() ? 0 : 1 )
                              + ( request.paneIndex ? 1 : 0 )
                              + ( request.paneDirection.isEmpty() ? 0 : 1 );
    if ( destinations != 1 ) {
        return commanderFailure(
            CommanderResultCode::InvalidRequest,
            tr( "move_tab requires exactly one of a pane id, a pane index or a direction." ) );
    }
    if ( paneArea_->paneForTabId( request.tabId ) == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Open tab %1 was not found." ).arg( request.tabId ) );
    }

    LogPane* target = nullptr;
    if ( !request.paneDirection.isEmpty() ) {
        const auto orientation = paneSplitOrientation( request.paneDirection );
        if ( !orientation ) {
            return commanderFailure( CommanderResultCode::InvalidRequest,
                                     tr( "The pane direction must be right or down." ) );
        }
        if ( paneArea_->paneCount() >= LogPaneArea::MaxPanes ) {
            return commanderFailure( CommanderResultCode::ExecutionFailed, paneLimitMessage() );
        }
        target = moveTabToNewPane( request.tabId, *orientation );
    }
    else {
        CommanderResult failure;
        target = commanderTargetPane( request, false, false, &failure );
        if ( target == nullptr ) {
            // The tab is in this window, so another window cannot answer: stop here.
            return commanderFailure( CommanderResultCode::InvalidRequest, failure.message );
        }
        if ( !moveTabToPane( request.tabId, target ) ) {
            target = nullptr;
        }
    }
    if ( target == nullptr ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed,
                                 tr( "Tab %1 could not be moved." ).arg( request.tabId ) );
    }

    const auto index = paneArea_->findTabById( request.tabId );
    auto payload = commanderPanePayload( target );
    payload.insert( QStringLiteral( "tabId" ), request.tabId );
    payload.insert( QStringLiteral( "tabIndex" ), index );
    payload.insert( QStringLiteral( "paneTabIndex" ), paneArea_->tabRef( index ).index );
    LOG_INFO << "Commander moved tab " << request.tabId << " to pane " << target->paneId();
    return commanderSuccess( {}, payload );
}
