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

#ifndef LOGPANE_H
#define LOGPANE_H

#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

#include "tabbedcrawlerwidget.h"

class CrawlerWidget;
class QLabel;
class QMenu;
class QSplitter;
class QVBoxLayout;
class StreamSession;

// The split layout of a window as it is saved with sessions and projects. A node is either a
// pane (its tabs are indices into the window's saved file list) or a splitter of two or more
// nodes.
struct PaneLayoutNode {
    bool isPane = true;
    QString paneId;
    std::vector<int> files;
    int currentFile = -1;
    Qt::Orientation orientation = Qt::Horizontal;
    QList<int> sizes;
    std::vector<PaneLayoutNode> children;
};

struct PaneLayout {
    PaneLayoutNode root;
    QString activePaneId;

    int paneCount() const;
};

namespace PaneLayoutFormat {
constexpr int Version = 1;
constexpr int MaxPanes = 8;

enum class Status {
    Absent,      // no layout stored: everything opens in one pane
    Valid,       // a layout this version understands
    Unsupported, // a newer layout version: ignored, everything opens in one pane
    Invalid      // a malformed layout
};

struct ParseResult {
    Status status = Status::Absent;
    PaneLayout layout;
    QString error;
};

// fileCount is the number of entries in the window's saved file list.
ParseResult parse( const QString& json, int fileCount );
QString serialize( const PaneLayout& layout );
} // namespace PaneLayoutFormat

// One pane of a window: a row of file / COM / URL tabs (one TabbedCrawlerWidget, so every tab
// keeps its own log view, search line and filtered views) with a stable pane id.
class LogPane : public QWidget {
    Q_OBJECT

  public:
    explicit LogPane( const QString& paneId, QWidget* parent = nullptr );

    QString paneId() const
    {
        return paneId_;
    }

    TabbedCrawlerWidget* tabs() const
    {
        return tabs_;
    }

    bool isActive() const
    {
        return active_;
    }

  Q_SIGNALS:
    // The user clicked into the pane.
    void activated( LogPane* pane );

  protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;
    void mousePressEvent( QMouseEvent* event ) override;
    void paintEvent( QPaintEvent* event ) override;
    void changeEvent( QEvent* event ) override;
    void focusInEvent( QFocusEvent* event ) override;

  private:
    friend class LogPaneArea;
    void setPaneId( const QString& paneId );
    void retranslate();
    void setPresentation( bool active, bool multiPane );
    void updatePlaceholder();

    QString paneId_;
    TabbedCrawlerWidget* tabs_ = nullptr;
    QLabel* placeholder_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    bool active_ = false;
    bool multiPane_ = false;
};

struct PaneTabRef {
    LogPane* pane = nullptr;
    int index = -1;

    bool isValid() const
    {
        return pane != nullptr && index >= 0;
    }
};

// The central area of a main window: a tree of QSplitters whose leaves are LogPanes. One pane
// is active at a time; its current tab is the window's current tab.
//
// The tab functions mirror TabbedCrawlerWidget with window-wide indices: tabs are counted pane
// by pane in layout order, so a window with one pane behaves exactly as before.
class LogPaneArea : public QWidget {
    Q_OBJECT

  public:
    static constexpr int MaxPanes = PaneLayoutFormat::MaxPanes;

    explicit LogPaneArea( QWidget* parent = nullptr );
    ~LogPaneArea() override;
    LogPaneArea( const LogPaneArea& ) = delete;
    LogPaneArea& operator=( const LogPaneArea& ) = delete;

    // Panes
    std::vector<LogPane*> panes() const;
    int paneCount() const;
    int paneIndexOf( const LogPane* pane ) const;
    LogPane* paneById( const QString& paneId ) const;
    LogPane* paneForTabId( const QString& tabId ) const;
    LogPane* paneForWidget( const QWidget* widget ) const;
    LogPane* activePane() const;
    void setActivePane( LogPane* pane );

    // Splits a pane; the new, empty pane is placed right of or below it. Returns nullptr when
    // the window already has MaxPanes panes.
    LogPane* split( LogPane* pane, Qt::Orientation orientation );
    // The pane that takes the space of the passed pane when it closes.
    LogPane* neighbourPane( const LogPane* pane ) const;
    // Removes an empty pane; the last pane is never removed.
    bool removePane( LogPane* pane );
    void equalize();
    // Moves a tab with everything it owns (tab id, metadata, stream session). Returns the index
    // in the target pane or -1.
    int moveTab( LogPane* from, int index, LogPane* to, int toIndex = -1 );

    PaneLayout captureLayout(
        const std::function<int( const LogPane& pane, int index )>& fileIndexForTab ) const;
    // Rebuilds the pane tree; every pane must be empty. The first existing pane is kept for the
    // first leaf, so a single-pane window keeps its tab widget.
    bool applyLayout( const PaneLayout& layout );
    bool resetToSinglePane();

    using TabContextMenuExtension = std::function<void( QMenu& menu, LogPane* pane, int index )>;
    void setTabContextMenuExtension( TabContextMenuExtension extension );
    void setAlwaysShowTabBar( bool alwaysShow );

    // Window-wide tabs, counted pane by pane in layout order.
    int count() const;
    PaneTabRef tabRef( int index ) const;
    int globalIndex( const LogPane* pane, int localIndex ) const;
    QWidget* widget( int index ) const;
    int indexOf( const QWidget* widget ) const;
    QString tabIdAt( int index ) const;
    QString tabPathAt( int index ) const;
    QString tabDisplayNameAt( int index ) const;
    int findTabById( const QString& tabId ) const;
    int currentIndex() const;
    QWidget* currentWidget() const;
    void setCurrentIndex( int index );
    void setCurrentWidget( QWidget* widget );
    int addCrawler( CrawlerWidget* crawler, const QString& fileName );
    int addCrawlerToPane( LogPane* pane, CrawlerWidget* crawler, const QString& fileName );
    void removeCrawler( int index );

    StreamSession* streamSessionForTab( int index ) const;
    StreamSession* streamSessionForPath( const QString& fileName ) const;
    void setStreamSessionForPath( const QString& fileName,
                                  const std::shared_ptr<StreamSession>& session );
    void clearStreamSessionForPath( const QString& fileName );
    void remapStreamSessionPath( const QString& oldFileName, const QString& newFileName );
    void setTabActionsPort( const QString& fileName, bool isActionsPort );
    void setTabScriptActive( const QString& tabId, bool isActive );

  Q_SIGNALS:
    void currentChanged( int index );
    void tabCloseRequested( int index );
    void startNewStreamFileRequested( int index );
    void closeStreamConnectionRequested( int index );
    void pauseStreamConnectionRequested( int index );
    void resumeStreamConnectionRequested( int index );
    void actionsEnabledChangeRequested( const QString& tabId, bool enabled );
    void autoResponsesEnabledChangeRequested( const QString& tabId, bool enabled );
    void activePaneChanged( LogPane* pane );
    // Panes were added or removed.
    void layoutChanged();

  protected:
    // Activates the pane the user clicks into or moves the keyboard focus into.
    bool eventFilter( QObject* watched, QEvent* event ) override;

  private:
    class FocusGuard;

    LogPane* createPane( const QString& paneId = {} );
    QSplitter* createSplitter( Qt::Orientation orientation ) const;
    void connectPane( LogPane* pane );
    void releasePane( LogPane* pane );
    void setRoot( QWidget* root );
    void collectPanes( QWidget* node, std::vector<LogPane*>& result ) const;
    void collectSplitters( QWidget* node, std::vector<QSplitter*>& result ) const;
    PaneLayoutNode captureNode(
        QWidget* node,
        const std::function<int( const LogPane& pane, int index )>& fileIndexForTab ) const;
    QWidget* buildNode( const PaneLayoutNode& node, LogPane*& reusable );
    void collapse( QSplitter* splitter );
    void flatten( QSplitter* outer, QSplitter* inner );
    void updatePresentation();
    void activatePaneOf( QWidget* widget );
    LogPane* paneHoldingPath( const QString& fileName ) const;
    LogPane* paneWithStreamSession( const QString& fileName ) const;

    QVBoxLayout* layout_ = nullptr;
    QPointer<QWidget> root_;
    QPointer<LogPane> active_;
    TabContextMenuExtension contextMenuExtension_;
    bool alwaysShowTabBar_ = false;
    int focusGuard_ = 0;
    // While a tab moves it is briefly in neither pane; current-tab changes are reported once
    // the move is complete.
    int movingTabs_ = 0;
};

#endif
