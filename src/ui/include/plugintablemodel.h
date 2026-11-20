// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pluginview.h"
#include <QAbstractTableModel>
#include <QColor>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QVector>
#include <functional>
#include <initializer_list>
#include <optional>

class QComboBox;

// The table element of plugin views (docs/plugin-views.md) and the helpers the view renderer
// shares with it. Moved out of pluginview.cpp without behaviour change (E17a).
namespace pluginview {
// Table model roles next to Qt::UserRole (sort key) and Qt::UserRole + 1 (row id).
constexpr int SortRole = Qt::UserRole;
constexpr int RowIdRole = Qt::UserRole + 1;
constexpr int EditorRole = Qt::UserRole + 2; // the cell's editor spec; empty when read-only
// The value (a QJsonValue) of an edit that waits for the plugin; invalid otherwise.
constexpr int PendingValueRole = Qt::UserRole + 3;
// E17d: a cell's progress (a double, 0..1) drawn behind its text; invalid otherwise.
constexpr int ProgressRole = Qt::UserRole + 4;
// E17d: the text sort_as "text" and "natural" compare: the cell's `sort` value as text, else the
// text as it is (SortRole turns numeric-looking text into numbers).
constexpr int SortTextRole = Qt::UserRole + 5;
// E17d: at most this many ids in a selection patch, an element menu context, `checked` and
// `moved` events (docs/plugin-sdk-advanced.md section 10).
constexpr int MaxTableIds = 1000;
// The ids of a menu context or an event stay within this many bytes of JSON, so a frame never
// reaches the 1 MiB message limit (ids may be 4,096 characters each); the count stays complete.
constexpr qsizetype MaxIdBytes = 256 * 1024;
// The leading ids that fit in `budget` bytes of JSON.
QStringList boundedIds( const QStringList& ids, qsizetype budget = MaxIdBytes );
// Marks the widgets PluginCellDelegate creates (direct children of a table's viewport).
constexpr char CellEditorProperty[] = "pluginCellEditor";

QColor toneColor( const QString& tone );
QString cellText( const QJsonValue& value, int maximum = PluginView::MaxTextChars );
QString text( const QJsonObject& props, const char* key, int maximum = PluginView::MaxTextChars );
// A cell editor choice is a plain value or {value, label}.
QJsonValue choiceValue( const QJsonValue& choice );
QString choiceLabel( const QJsonValue& choice );
// The items of a choice or boolean cell editor.
QJsonArray editorChoices( const QJsonObject& editor );
// Equal editor values; lists (multichoice) compare as sets.
bool sameValue( const QJsonValue& left, const QJsonValue& right );
// Tooltips are plain text: Qt would render text that looks like HTML as rich text.
QString plainToolTip( const QString& tip );
// Parts joined so that no two different lists give the same key (each part is length-prefixed):
// tooltip keys and ids may contain any character.
QString joinedKey( std::initializer_list<QString> parts );

// What an advanced tooltip on a table cell asks for (docs/plugin-sdk-advanced.md section 9.2).
struct CellTooltipTarget {
    QString rowId;
    QString columnId;
    QString key;        // the cache key part: the table's, row's, column's and cell's tooltip_key
    QString tooltipKey; // the one the query carries: the most specific that is set
    bool provider = false;
};

// Plugin table rows; identity is the row id so patches never rebuild the view.
class PluginTableModel final : public QAbstractTableModel {
public:
    struct Column {
        QString id;
        QString title;
        bool editable = false;
        QString tooltip; // the header's static tooltip
        bool tooltipProvider = false;
        QString tooltipKey;
        // E17d (section 10.1).
        Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter;
        bool hidden = false;
        bool sortable = true;
        QString sortAs; // "text", "number", "natural"; empty: numbers as numbers, else text
        int minWidth = 0;
        bool check = false; // a check box bound to the row's `checked`
        QJsonValue icon;
    };
    // E17d: what a cell object adds to its text (section 10.1).
    struct CellStyle {
        QJsonValue icon;
        QString tooltip;
        QString tooltipKey;
        QString tone;
        bool bold = false;
        double progress = -1; // below 0: none
        QVariant sort;        // invalid: the text sorts
    };
    // An edit the plugin has not confirmed yet.
    struct Pending {
        QString previous; // the cell's text before the first unconfirmed edit
        QString display;  // the text the edit shows
        QJsonValue value; // the committed value
    };
    struct Row {
        QString id;
        QStringList cells;
        QString tone;
        QString tooltip;
        QJsonObject editors;         // column id -> editor spec
        QHash<int, Pending> pending; // by column
        bool tooltipProvider = false;
        QString tooltipKey;
        QVector<CellStyle> styles; // E17d cell objects; empty when every cell is plain text
        QJsonValue icon;           // E17d: shown in the first column
        bool checked = false;      // E17d: shown in the check columns
    };

    explicit PluginTableModel( QObject* parent );
    // Set by the view: whether an edit may be committed now, and the report of a committed edit,
    // which returns false when the edit could not be sent to the plugin.
    std::function<bool()> canEdit;
    std::function<bool( const QString& rowId, const QString& columnId, const QJsonValue& value,
                        const QString& previous )>
        onEdited;
    // E17c: icon references of choices (a committed choice shows its icon in the cell); E17d:
    // cell, row and header icons.
    std::function<QIcon( const QJsonValue& reference )> iconFor;
    // E17d: the user changed a row's check box (a click, Space, automation). The receiver decides
    // which rows change (a Shift+click sets a range) and calls setChecked(). Without a receiver
    // the row changes alone.
    std::function<void( const QString& rowId, bool checked )> onCheckRequested;

    int rowCount( const QModelIndex& parent = {} ) const override;
    int columnCount( const QModelIndex& parent = {} ) const override;
    QVariant data( const QModelIndex& index, int role ) const override;
    QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;
    // A cell is editable when its column is `editable` and its row has an editor for the column.
    QJsonObject editorFor( const QModelIndex& index ) const;
    Qt::ItemFlags flags( const QModelIndex& index ) const override;
    // The cell shows the new text at once, in italic until the plugin patches the row.
    bool setData( const QModelIndex& index, const QVariant& value, int role ) override;
    // Whether a cell already holds `value`, judged by value, not by the text it shows (choices may
    // share a label). Known for an edit the plugin has not confirmed yet (its value) and for text
    // and number cells (their text). For choice, boolean and multichoice cells the delegate decides,
    // from the value its editor started with.
    static bool holds( const Row& row, int column, const QJsonObject& editor,
                       const QJsonValue& value, const QString& text );
    // The text a committed value shows: the chosen items' labels for choice editors.
    static QString displayFor( const QJsonObject& editor, const QJsonValue& value );
    void revertPending();

    qsizetype cellCount( qsizetype rows ) const;
    // Whether a table of `rows` rows stays within PluginView::MaxTableCells.
    bool fits( qsizetype rows ) const;
    bool setColumns( const QJsonArray& columns );
    Row parse( const QJsonObject& object ) const;
    bool replace( const QJsonArray& rows );
    void update( const QJsonArray& rows );
    bool insert( const QJsonArray& rows, const QString& before );
    // Returns true when the model was reset (large batches) instead of removing ranges.
    bool remove( const QJsonArray& ids );
    int rowOf( const QString& id ) const;
    // The advanced tooltip target of a cell (source model index): the table, its column or its
    // row may ask for it.
    CellTooltipTarget tooltipTarget( const QModelIndex& index, bool tableProvider,
                                     const QString& tableKey ) const;
    QString columnId( int column ) const;
    // E17d: the column with this id, or -1; a column's options (an invalid index gives defaults).
    int columnOf( const QString& id ) const;
    const Column& column( int index ) const;
    QString rowId( int row ) const;
    bool hasCheckColumn() const;
    bool isChecked( const QString& rowId ) const;
    // Sets the check state of rows (unknown ids are skipped); returns the ids that changed.
    QStringList setChecked( const QStringList& rowIds, bool checked );

private:
    // An edit that never reached the plugin: the cell shows what it showed before it again, the
    // confirmed text or an earlier edit that still waits for the plugin. Skipped when something
    // changed the cell meanwhile.
    void undoEdit( const QString& rowId, int column, const QString& display, const QString& shown,
                   const std::optional<Pending>& earlier );
    // A row patch without `editors` that leaves a pending cell showing its edit confirms the edit:
    // the editor's starting value (when the plugin gave one) becomes the committed value, so the
    // editor does not reopen on the value before the edit.
    void confirmStartValues( Row& row ) const;
    int cellLimit() const;
    void reindex();
    QVector<Column> columns_;
    QVector<Row> rows_;
    QHash<QString, int> index_;
};

// E17d: sorting by a column's `sort_as` (section 10.1); a cell's `sort` value replaces its text.
// `text` compares case-insensitively, `number` numerically (text that is no number sorts last),
// `natural` with numbers inside the text compared as numbers. Without `sort_as`, numbers sort as
// numbers and other text as text, as before E17d.
class PluginTableProxy final : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    // A column with `sortable: false` keeps the current order (the header click is ignored).
    void sort( int column, Qt::SortOrder order = Qt::AscendingOrder ) override;

protected:
    bool lessThan( const QModelIndex& left, const QModelIndex& right ) const override;
};

// Cell editors from the row's `editors` spec (docs/plugin-views.md, editable cells).
// Enter or leaving commits, Esc cancels; an editor closed without a change commits nothing.
// Choice editors open their list at once: choosing an item (or closing a multichoice list with
// Enter or a click outside) commits and ends the edit; closing a choice list without choosing
// ends it unchanged.
class PluginCellDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // Set by the view: reports an editor kind this delegate does not know (it gets a text editor).
    std::function<void( const QString& kind )> onUnknownKind;
    // Set by the view: icons of choices (E17c).
    std::function<QIcon( const QJsonValue& reference )> iconFor;

    // E17d: a cell's progress bar behind its text.
    void paint( QPainter* painter, const QStyleOptionViewItem& option,
                const QModelIndex& index ) const override;
    QWidget* createEditor( QWidget* parent, const QStyleOptionViewItem& option,
                           const QModelIndex& index ) const override;
    void setEditorData( QWidget* widget, const QModelIndex& index ) const override;
    void setModelData( QWidget* widget, QAbstractItemModel* model,
                       const QModelIndex& index ) const override;
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    // The value an editor holds; undefined for a choice editor without choices.
    static QJsonValue editorValue( const QWidget* widget );
    // The choice editor whose list window `watched` is, or null. Uses the window recorded in
    // createChoice: no call into the combo box's view, which can be gone during its destruction.
    static QComboBox* comboOfList( QObject* watched );
    QWidget* createChecks( QWidget* parent, const QJsonObject& editor ) const;
    QWidget* createChoice( QWidget* parent, const QJsonObject& editor ) const;
    static QWidget* createLine( QWidget* parent, const QJsonObject& editor, bool number );
    // One double-click (or F2) shows the list. It opens once the view has shown the editor and no
    // mouse button is held (see MouseReleaseWaiter), so a double-click never picks the item under
    // the mouse. F2 and other keys open it at once.
    static void openPopupWhenShown( QComboBox* combo );
    // The item whose value, or else label, reads as `text` (Qt 6 QVariant equality does not
    // match a number with its text).
    static int findChoice( const QComboBox* combo, const QString& shown );
};
} // namespace pluginview
