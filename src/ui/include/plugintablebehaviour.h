// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
    };

    PluginTableBehaviour( QTableView* table, pluginview::PluginTableModel* model,
                          pluginview::PluginTableProxy* proxy, Hooks hooks );

    void setOptions( const Options& options );
    const Options& options() const
    {
        return options_;
    }
    // After the plugin set the columns: hidden columns, minimum widths, then the kept state.
    void columnsChanged();
    // The selection patch (section 10.2); the view applies it without a `selected` event.
    void select( const QJsonValue& ids, const QJsonValue& current, const QJsonValue& scrollTo );
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

    QPointer<QTableView> table_;
    pluginview::PluginTableModel* model_;
    pluginview::PluginTableProxy* proxy_;
    Hooks hooks_;
    Options options_;
    QTimer* saveTimer_ = nullptr;
    QJsonObject pendingState_; // the state a scheduled save writes
    QString checkAnchor_;      // the row of the last check box click (Shift ranges)
    QString selectionAnchor_;  // the row of the last click (toggle selection ranges)
    QPoint pressPosition_;
    QString pressRow_;
    bool restoring_ = false;
};
