// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QItemSelection>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QModelIndex>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>

class QMenu;
class QTableView;
class QTimer;

namespace pluginview {
class PluginTableModel;
class PluginTableProxy;
} // namespace pluginview

// The E17d behaviour of a plugin view table (docs/plugin-sdk-advanced.md section 10): toggle
// selection, check columns with Shift ranges, `activated`, reordering rows by drag (`moved`), the
// element and header context menus with Copy, Copy All and Export CSV, the selection patch and
// kept columns. Owned by the table view; reports through hooks the view sets.
class PluginTableBehaviour final : public QObject {
    Q_OBJECT
public:
    // The MIME type of rows dragged inside a table.
    static constexpr char RowsMimeType[] = "application/x-cilogg-plugin-rows";

    struct Options {
        bool header = true;
        bool toggleSelection = false;
        QString contextMenu; // a manifest menu with location `element`
        bool copy = true;
        bool exportCsv = false;
        bool reorderable = false;
        bool keepColumns = true;
    };
    struct Hooks {
        // Sends a view event of this table; false when it could not be delivered.
        std::function<bool( const QString& type, const QJsonValue& value )> emitEvent;
        // Whether the user may act now: a live view outside a plugin update.
        std::function<bool()> canAct;
        // Whether the plugin gets the events E17 adds to existing elements (`activated`).
        std::function<bool()> extendedEvents;
        // Adds the plugin's entries of `menuId` for `target` ({row_id, column_id, selected_ids,
        // selected_count}).
        std::function<void( QMenu* menu, const QString& menuId, const QJsonObject& target )>
            elementMenu;
        // The host save dialog: the chosen path, empty when cancelled.
        std::function<QString( const QString& title, const QString& filter,
                               const QString& suggestedName )>
            saveFile;
        // Kept columns (section 10.5).
        std::function<QJsonObject()> loadState;
        std::function<void( const QJsonObject& state )> saveState;
        // The selection changed while it was held (see holdsSelection()): send `selected` now.
        std::function<void()> selectionSettled;
    };

    PluginTableBehaviour( QTableView* table, pluginview::PluginTableModel* model,
                          pluginview::PluginTableProxy* proxy, Hooks hooks );

    void setOptions( const Options& options );
    const Options& options() const
    {
        return options_;
    }
    // Before the plugin's new columns reset the model: a pending column change is saved with the
    // columns it was made on, and nothing the reset itself changes is saved.
    void beginColumnsChange();
    // The new columns were refused: nothing changed.
    void abortColumnsChange();
    // After the plugin set the columns: hidden columns, minimum widths, then the kept state; the
    // sort follows its column by id (or goes when that column is gone).
    void columnsChanged();
    // While a press on a selected row may start a drag, selection changes send no `selected`:
    // a drag restores the selection before the press, a release without a drag reports once.
    bool holdsSelection() const
    {
        return holdSelection_;
    }
    // The selection patch (section 10.2); the view applies it without a `selected` event. A
    // selection it sets ends a press that may start a drag: the press's change is not reported.
    void select( const QJsonValue& ids, const QJsonValue& current, const QJsonValue& scrollTo );
    // The view sent `selected` for this table: a report still waiting (cancelPress()) goes.
    void selectionReported()
    {
        reportPending_ = false;
    }
    QString currentRowId() const;
    // The shown rows that are selected, in shown order (at most `maximum` ids) and their count.
    QStringList selectedRowIds( int maximum, int* count = nullptr ) const;
    // What an element menu entry gets: the clicked row and column and the selection.
    QJsonObject menuTarget( const QString& rowId, const QString& columnId ) const;

    // The context menu for a click at `position` (viewport coordinates), or null when it would be
    // empty; the caller shows and deletes it. A click on a row makes it current and selects it.
    QMenu* buildMenu( const QPoint& position );
    // The header menu that shows and hides columns.
    QMenu* buildHeaderMenu();
    // Copy: the selected rows (or the current row) as tab-separated text; `all`: a header line
    // and every shown row.
    QString copyText( bool all ) const;
    // Export CSV: a header line and every shown row (RFC 4180, UTF-8), written row by row.
    bool exportCsv( const QString& path, QString* error ) const;
    // Rows dropped before `before` (empty: at the end): sends `moved` (section 10.3).
    bool moveRows( const QStringList& rowIds, const QString& before );
    bool reorderAllowed() const;
    // The kept state of the columns now.
    QJsonObject state() const;
    // Writes a column change still waiting for its save (the view closes, or new columns come).
    void flushPendingSave();

    // The rows a drag that starts now moves, as `moved` sends them (at most MaxTableIds and
    // MaxIdBytes of ids, in shown order): the selection before the press when the pressed row
    // was in it, else the pressed row alone; empty without a press that may start a drag.
    QStringList draggedRows() const;

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    void requestCheck( const QString& rowId, bool checked );
    void activate( const QModelIndex& index );
    void startDrag();
    bool drop( const QPoint& position, const QByteArray& payload );
    void restoreState( const QJsonObject& state );
    void scheduleSave();
    void enforceMinimumWidth( int logical, int size );
    QList<int> shownColumns() const; // logical indexes of the shown columns, in shown order
    QString rowIdAt( const QModelIndex& index ) const;
    QModelIndex indexOfRow( const QString& rowId ) const;
    bool isCheckCell( const QModelIndex& index ) const; // a proxy index in a `check` column
    // Ends a hold of the selection; with `report`, sends `selected` when the selection is no
    // longer the one before the press.
    void releaseSelection( bool report );
    // Whether the selection is no longer the one before the press (while the ranges are valid).
    bool pressChangedSelection() const;
    // The rows are about to change under a press that may start a drag: it will not start one,
    // and a selection change the press made is reported once the change is done.
    void cancelPress();
    // Forgets the press: no drag starts from it and nothing of it is held.
    void endPress();
    // Sends `selected` now (and a report still waiting goes).
    void reportSelection();

    QPointer<QTableView> table_;
    pluginview::PluginTableModel* model_;
    pluginview::PluginTableProxy* proxy_;
    Hooks hooks_;
    Options options_;
    QTimer* saveTimer_ = nullptr;
    QJsonObject pendingState_; // the state a scheduled save writes
    QHash<int, int> hiddenWidths_; // logical column -> its width before it was hidden
    QString checkAnchor_;      // the row of the last check box click (Shift ranges)
    QString selectionAnchor_;  // the row of the last click (toggle selection ranges)
    QPoint pressPosition_;
    QString pressRow_;
    QItemSelection pressSelection_; // the selection when the drag's press came (ranges)
    bool pressWasSelected_ = false; // whether the pressed row was in it
    bool holdSelection_ = false;    // see holdsSelection()
    bool reportPending_ = false;    // cancelPress() reports once the change is done
    bool restoring_ = false;
    QString sortedColumn_; // the sorted column's id while new columns come (beginColumnsChange)
    Qt::SortOrder sortedOrder_ = Qt::AscendingOrder;
};
