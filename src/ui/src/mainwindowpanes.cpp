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

// Split panes of the main window (U16a): pane menus, split / move / close / merge, and the
// layout saved with the session.

#include "mainwindow.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>

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
    splitPaneRightAction = new QAction( tr( "Split Right" ), this );
    splitPaneRightAction->setObjectName( QStringLiteral( "splitPaneRightAction" ) );
    splitPaneRightAction->setShortcut( QKeySequence( Qt::CTRL | Qt::ALT | Qt::Key_Right ) );
    splitPaneRightAction->setStatusTip( tr( "Open a new pane right of the active pane" ) );
    connect( splitPaneRightAction, &QAction::triggered, this,
             [ this ] { splitPane( activePane(), Qt::Horizontal ); } );

    splitPaneDownAction = new QAction( tr( "Split Down" ), this );
    splitPaneDownAction->setObjectName( QStringLiteral( "splitPaneDownAction" ) );
    splitPaneDownAction->setShortcut( QKeySequence( Qt::CTRL | Qt::ALT | Qt::Key_Down ) );
    splitPaneDownAction->setStatusTip( tr( "Open a new pane below the active pane" ) );
    connect( splitPaneDownAction, &QAction::triggered, this,
             [ this ] { splitPane( activePane(), Qt::Vertical ); } );

    closePaneAction = new QAction( tr( "Close Pane" ), this );
    closePaneAction->setObjectName( QStringLiteral( "closePaneAction" ) );
    closePaneAction->setStatusTip(
        tr( "Close the active pane; its tabs can be closed or moved to the neighbouring pane" ) );
    connect( closePaneAction, &QAction::triggered, this,
             [ this ] { closePane( activePane(), PaneTabsOnClose::Ask ); } );

    equalizePanesAction = new QAction( tr( "Equalize Panes" ), this );
    equalizePanesAction->setObjectName( QStringLiteral( "equalizePanesAction" ) );
    equalizePanesAction->setStatusTip( tr( "Give all panes the same size" ) );
    connect( equalizePanesAction, &QAction::triggered, this, [ this ] { paneArea_->equalize(); } );

    mergePanesAction = new QAction( tr( "Merge All Panes" ), this );
    mergePanesAction->setObjectName( QStringLiteral( "mergePanesAction" ) );
    mergePanesAction->setStatusTip( tr( "Move all tabs back into one pane" ) );
    connect( mergePanesAction, &QAction::triggered, this, [ this ] { mergeAllPanes(); } );

    moveTabToPaneMenu = new QMenu( tr( "Move Tab to Pane" ), this );
    moveTabToPaneMenu->setObjectName( QStringLiteral( "moveTabToPaneMenu" ) );
    connect( moveTabToPaneMenu, &QMenu::aboutToShow, this, [ this ] {
        populateMoveToPaneMenu( moveTabToPaneMenu, paneArea_->tabIdAt( paneArea_->currentIndex() ) );
    } );
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
        const auto message
            = tr( "A window holds at most %1 panes; close or merge a pane first." )
                  .arg( LogPaneArea::MaxPanes );
        statusBar()->showMessage( message, PaneStatusTimeoutMs );
        LOG_INFO << "Split refused: " << message;
        return nullptr;
    }

    auto* created = paneArea_->split( pane, orientation );
    if ( created == nullptr ) {
        return nullptr;
    }
    paneArea_->setActivePane( created );
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
    moveTabToPane( tabId, created );
    return created;
}

bool MainWindow::closePane( LogPane* pane, PaneTabsOnClose tabs )
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
                closeTab( paneArea_->globalIndex( safePane, 0 ), ActionInitiator::User );
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
