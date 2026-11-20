// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugintablemodel.h"

#include "checkcombobox.h"
#include "pluginchoicewidgets.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleValidator>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QLineEdit>
#include <QLocale>
#include <QPointer>
#include <QSet>
#include <QTextDocument>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace pluginview {
namespace {
constexpr int ForcedResetRows = 512;
// On a cell editor: the value it started with (a QJsonValue), and for a choice editor whether an
// item was chosen from its list.
constexpr char CellStartProperty[] = "pluginCellStart";
constexpr char CellChosenProperty[] = "pluginCellChosen";
// On a choice editor: its list window, recorded when the editor is made. The event filter compares
// with it instead of asking the combo box, which may be half destroyed when its list window sends
// its last events (Qt 6.12 sends a Hide from the combo box's destructor).
constexpr char CellListProperty[] = "pluginCellList";

QString tooltipKeyOf( const QJsonObject& object )
{
    return object.value( "tooltip_key" ).toString().left( 128 );
}

// Runs `done` (queued, with the parent as its context) once no mouse button is held, then deletes
// itself. A double-click opens a cell editor on its second press: a list opened at that moment would
// get the release that follows and pick the item under the mouse, so cell lists wait for it.
class MouseReleaseWaiter final : public QObject {
public:
    static constexpr int MaxWaitMs = 2000; // the release can happen outside every window
    MouseReleaseWaiter( QObject* parent, std::function<void()> done )
        : QObject( parent )
        , done_( std::move( done ) )
    {
        QCoreApplication::instance()->installEventFilter( this );
        QTimer::singleShot( MaxWaitMs, this, [ this ] { finish(); } );
    }
    ~MouseReleaseWaiter() override
    {
        if ( auto* app = QCoreApplication::instance() ) {
            app->removeEventFilter( this );
        }
    }

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override
    {
        // The application updates its button state before it delivers the release.
        if ( event->type() == QEvent::MouseButtonRelease
             && QGuiApplication::mouseButtons() == Qt::NoButton ) {
            finish();
        }
        return QObject::eventFilter( watched, event );
    }

private:
    void finish()
    {
        if ( finished_ ) {
            return;
        }
        finished_ = true;
        QCoreApplication::instance()->removeEventFilter( this );
        QTimer::singleShot( 0, parent(), done_ );
        deleteLater();
    }
    std::function<void()> done_;
    bool finished_ = false;
};
} // namespace

QColor toneColor( const QString& tone )
{
    if ( tone == "ok" ) {
        return QColor( 0x2e, 0x7d, 0x32 );
    }
    if ( tone == "warn" ) {
        return QColor( 0xb2, 0x6a, 0x00 );
    }
    if ( tone == "error" ) {
        return QColor( 0xc6, 0x28, 0x28 );
    }
    if ( tone == "muted" ) {
        return QColor( 0x80, 0x80, 0x80 );
    }
    if ( tone == "accent" ) {
        return QColor( 0x15, 0x65, 0xc0 );
    }
    return {};
}

QString cellText( const QJsonValue& value, int maximum )
{
    if ( value.isString() ) {
        return value.toString().left( maximum );
    }
    if ( value.isDouble() ) {
        return QString::number( value.toDouble(), 'g', 15 );
    }
    if ( value.isBool() ) {
        return value.toBool() ? QStringLiteral( "true" ) : QStringLiteral( "false" );
    }
    return {};
}

QString text( const QJsonObject& props, const char* key, int maximum )
{
    return props.value( QLatin1String( key ) ).toString().left( maximum );
}

QJsonValue choiceValue( const QJsonValue& choice )
{
    return choice.isObject() ? choice.toObject().value( "value" ) : choice;
}

QString choiceLabel( const QJsonValue& choice )
{
    const auto label = choice.toObject().value( "label" );
    return label.isString() ? label.toString().left( PluginView::MaxTextChars )
                            : cellText( choiceValue( choice ) );
}

QJsonArray editorChoices( const QJsonObject& editor )
{
    if ( editor.value( "kind" ) == "boolean" ) {
        return { QJsonObject{ { "value", true }, { "label", QObject::tr( "Yes" ) } },
                 QJsonObject{ { "value", false }, { "label", QObject::tr( "No" ) } } };
    }
    return editor.value( "choices" ).toArray();
}

bool sameValue( const QJsonValue& left, const QJsonValue& right )
{
    if ( !left.isArray() || !right.isArray() ) {
        return left == right;
    }
    const auto a = left.toArray();
    const auto b = right.toArray();
    return a.size() == b.size() && std::all_of( a.begin(), a.end(), [ &b ]( const QJsonValue& value ) {
               return b.contains( value );
           } );
}

QString plainToolTip( const QString& tip )
{
    return Qt::mightBeRichText( tip ) ? Qt::convertFromPlainText( tip, Qt::WhiteSpaceNormal ) : tip;
}

// PluginTableModel

PluginTableModel::PluginTableModel( QObject* parent )
    : QAbstractTableModel( parent )
{
}

int PluginTableModel::rowCount( const QModelIndex& parent ) const
{
    return parent.isValid() ? 0 : static_cast<int>( rows_.size() );
}

int PluginTableModel::columnCount( const QModelIndex& parent ) const
{
    return parent.isValid() ? 0 : static_cast<int>( columns_.size() );
}

QVariant PluginTableModel::data( const QModelIndex& index, int role ) const
{
    if ( !index.isValid() || index.row() >= rows_.size() ) {
        return {};
    }
    const auto& row = rows_.at( index.row() );
    const auto value
        = index.column() < row.cells.size() ? row.cells.at( index.column() ) : QString{};
    if ( role == Qt::DisplayRole || role == Qt::EditRole ) {
        return value;
    }
    if ( role == Qt::ToolTipRole ) {
        return plainToolTip( row.tooltip.isEmpty() ? value : row.tooltip );
    }
    if ( role == Qt::FontRole && row.pending.contains( index.column() ) ) {
        QFont font;
        font.setItalic( true );
        return font;
    }
    if ( role == EditorRole ) {
        return QVariant::fromValue( editorFor( index ) );
    }
    if ( role == PendingValueRole ) {
        const auto pending = row.pending.constFind( index.column() );
        return pending != row.pending.cend() ? QVariant::fromValue( pending->value ) : QVariant{};
    }
    if ( role == SortRole ) { // sort key: numbers sort numerically
        bool numeric = false;
        const auto number = value.toDouble( &numeric );
        return numeric ? QVariant( number ) : QVariant( value );
    }
    if ( role == RowIdRole ) {
        return row.id;
    }
    if ( role == Qt::ForegroundRole && toneColor( row.tone ).isValid() ) {
        return toneColor( row.tone );
    }
    if ( role == Qt::DecorationRole && iconFor ) {
        // E17c: a committed choice with an icon shows it in the cell (section 8.4).
        const auto editor = editorFor( index );
        // Labels can repeat: the choice of the edit not confirmed yet, else of the editor's
        // starting value, as when the editor opens, as long as the cell shows its label; else the
        // first choice whose label is the cell text. Separators and section titles are no choices.
        if ( editor.value( "kind" ) == "choice" ) {
            const auto pending = row.pending.constFind( index.column() );
            const auto chosen
                = pending != row.pending.cend() ? pending->value : editor.value( "value" );
            if ( !chosen.isUndefined() && !chosen.isNull() && !chosen.isArray()
                 && !chosen.isObject() ) {
                for ( const auto& choice : editor.value( "choices" ).toArray() ) {
                    const auto object = choice.toObject();
                    if ( object.contains( "type" ) || choiceValue( choice ) != chosen
                         || choiceLabel( choice ) != value ) {
                        continue;
                    }
                    if ( !object.contains( "icon" ) ) {
                        return {};
                    }
                    return iconFor( object.value( "icon" ) );
                }
            }
            for ( const auto& choice : editor.value( "choices" ).toArray() ) {
                const auto object = choice.toObject();
                if ( object.contains( "type" ) || choiceLabel( choice ) != value ) {
                    continue;
                }
                if ( !object.contains( "icon" ) ) {
                    return {};
                }
                return iconFor( object.value( "icon" ) );
            }
        }
    }
    return {};
}

QVariant PluginTableModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
    if ( orientation == Qt::Horizontal && section >= 0 && section < columns_.size() ) {
        if ( role == Qt::DisplayRole ) {
            return columns_.at( section ).title;
        }
        if ( role == Qt::ToolTipRole && !columns_.at( section ).tooltip.isEmpty() ) {
            return plainToolTip( columns_.at( section ).tooltip );
        }
    }
    return QAbstractTableModel::headerData( section, orientation, role );
}

QJsonObject PluginTableModel::editorFor( const QModelIndex& index ) const
{
    if ( !index.isValid() || index.row() >= rows_.size() || index.column() >= columns_.size()
         || !columns_.at( index.column() ).editable ) {
        return {};
    }
    return rows_.at( index.row() ).editors.value( columns_.at( index.column() ).id ).toObject();
}

Qt::ItemFlags PluginTableModel::flags( const QModelIndex& index ) const
{
    const auto base = QAbstractTableModel::flags( index );
    return editorFor( index ).isEmpty() ? base : base | Qt::ItemIsEditable;
}

bool PluginTableModel::setData( const QModelIndex& index, const QVariant& value, int role )
{
    const auto editor = editorFor( index );
    if ( role != Qt::EditRole || editor.isEmpty() || !canEdit || !canEdit() ) {
        return false;
    }
    auto json = value.metaType() == QMetaType::fromType<QJsonValue>()
                    ? value.toJsonValue()
                    : QJsonValue::fromVariant( value );
    if ( json.isString() ) {
        json = json.toString().left( PluginView::MaxFieldValueChars );
    }
    const bool list = editor.value( "kind" ) == "multichoice";
    if ( list != json.isArray() || json.isObject() || json.isNull() || json.isUndefined() ) {
        return false; // a list for multichoice, a single value otherwise
    }
    const int column = index.column();
    auto& row = rows_[ index.row() ];
    const auto previous = column < row.cells.size() ? row.cells.at( column ) : QString{};
    const auto display = displayFor( editor, json );
    // An editor closed without a change commits nothing (PluginCellDelegate::setModelData
    // compares with the value the editor started with); this guard catches the same value
    // committed twice.
    if ( holds( row, column, editor, json, previous ) ) {
        return false; // nothing changed: no event
    }
    while ( row.cells.size() <= column ) {
        row.cells.append( QString{} );
    }
    // An earlier edit of this cell that still waits for the plugin.
    const auto earlier = row.pending.contains( column )
                             ? std::optional<Pending>( row.pending.value( column ) )
                             : std::nullopt;
    if ( !earlier ) {
        row.pending.insert( column, { previous, {}, {} } ); // the text to revert to
    }
    auto& pending = row.pending[ column ];
    pending.display = display;
    pending.value = json;
    row.cells[ column ] = display;
    const auto rowId = row.id;
    const auto columnId = columns_.at( column ).id;
    Q_EMIT dataChanged( index, index );
    if ( onEdited && !onEdited( rowId, columnId, json, previous ) ) {
        undoEdit( rowId, column, display, previous, earlier );
        return false;
    }
    return true;
}

bool PluginTableModel::holds( const Row& row, int column, const QJsonObject& editor,
                              const QJsonValue& value, const QString& text )
{
    if ( const auto pending = row.pending.constFind( column ); pending != row.pending.cend() ) {
        return sameValue( pending->value, value );
    }
    const auto kind = editor.value( "kind" ).toString( "text" );
    return kind != "choice" && kind != "boolean" && kind != "multichoice"
           && cellText( value ) == text;
}

QString PluginTableModel::displayFor( const QJsonObject& editor, const QJsonValue& value )
{
    const auto kind = editor.value( "kind" );
    if ( kind == "choice" || kind == "boolean" ) {
        int seen = 0;
        for ( const auto& choice : editorChoices( editor ) ) {
            if ( ++seen > PluginView::MaxElements ) {
                break;
            }
            if ( choiceValue( choice ) == value ) {
                return choiceLabel( choice );
            }
        }
        return cellText( value );
    }
    if ( kind != "multichoice" ) {
        return cellText( value );
    }
    QStringList labels;
    const auto checked = value.toArray();
    int seen = 0;
    for ( const auto& choice : editor.value( "choices" ).toArray() ) {
        if ( ++seen > PluginView::MaxElements ) {
            break; // the editor offers no more choices than this
        }
        if ( checked.contains( choiceValue( choice ) ) ) {
            labels.append( choiceLabel( choice ) );
        }
    }
    return labels.isEmpty() ? QObject::tr( "None" )
                            : labels.join( QStringLiteral( ", " ) ).left( PluginView::MaxTextChars );
}

void PluginTableModel::revertPending()
{
    for ( int r = 0; r < rows_.size(); ++r ) {
        auto& row = rows_[ r ];
        if ( row.pending.isEmpty() ) {
            continue;
        }
        for ( auto it = row.pending.cbegin(); it != row.pending.cend(); ++it ) {
            if ( it.key() < row.cells.size() ) {
                row.cells[ it.key() ] = it->previous;
            }
        }
        row.pending.clear();
        Q_EMIT dataChanged( index( r, 0 ), index( r, qMax( 0, columnCount() - 1 ) ) );
    }
}

qsizetype PluginTableModel::cellCount( qsizetype rows ) const
{
    return rows * qMax<qsizetype>( 1, columns_.size() );
}

bool PluginTableModel::fits( qsizetype rows ) const
{
    return cellCount( rows ) <= PluginView::MaxTableCells;
}

bool PluginTableModel::setColumns( const QJsonArray& columns )
{
    QVector<Column> parsed;
    for ( const auto& value : columns ) {
        const auto column = value.toObject();
        Column parsedColumn;
        parsedColumn.id = ( value.isString() ? value.toString() : column.value( "id" ).toString() )
                              .left( PluginView::MaxTextChars );
        parsedColumn.title = column.contains( "title" ) ? text( column, "title" ) : parsedColumn.id;
        parsedColumn.editable = column.value( "editable" ).toBool();
        parsedColumn.tooltip = text( column, "tooltip" );
        parsedColumn.tooltipProvider = column.value( "tooltip_provider" ).toBool();
        parsedColumn.tooltipKey = tooltipKeyOf( column );
        parsed.append( parsedColumn );
    }
    if ( qMax<qsizetype>( 1, parsed.size() ) * rows_.size() > PluginView::MaxTableCells ) {
        return false;
    }
    beginResetModel();
    columns_ = parsed;
    for ( auto& row : rows_ ) {
        row.cells = row.cells.mid( 0, cellLimit() );
        row.pending.removeIf( [ &row ]( QHash<int, Pending>::iterator it ) {
            return it.key() >= row.cells.size();
        } );
    }
    endResetModel();
    return true;
}

PluginTableModel::Row PluginTableModel::parse( const QJsonObject& object ) const
{
    Row row;
    row.id = object.value( "id" ).toString().left( PluginView::MaxTextChars );
    row.tone = object.value( "tone" ).toString().left( 16 );
    row.tooltip = text( object, "tooltip" );
    row.editors = object.value( "editors" ).toObject();
    row.tooltipProvider = object.value( "tooltip_provider" ).toBool();
    row.tooltipKey = tooltipKeyOf( object );
    for ( const auto& cell : object.value( "cells" ).toArray() ) {
        if ( row.cells.size() >= cellLimit() ) {
            break; // never more cells than columns
        }
        row.cells.append( cellText( cell ) );
    }
    return row;
}

bool PluginTableModel::replace( const QJsonArray& rows )
{
    if ( !fits( rows.size() ) ) {
        return false;
    }
    beginResetModel();
    rows_.clear();
    index_.clear();
    rows_.reserve( rows.size() );
    for ( const auto& value : rows ) {
        const auto row = parse( value.toObject() );
        if ( row.id.isEmpty() || index_.contains( row.id ) ) {
            continue; // ids are unique: the first row with an id wins
        }
        index_.insert( row.id, static_cast<int>( rows_.size() ) );
        rows_.append( row );
    }
    endResetModel();
    return true;
}

void PluginTableModel::update( const QJsonArray& rows )
{
    for ( const auto& value : rows ) {
        const auto object = value.toObject();
        const auto found = index_.find( object.value( "id" ).toString() );
        if ( found == index_.end() ) {
            continue;
        }
        auto& row = rows_[ *found ];
        if ( object.contains( "cells" ) ) {
            row.cells = parse( object ).cells;
        }
        if ( object.contains( "tone" ) ) {
            row.tone = object.value( "tone" ).toString().left( 16 );
        }
        if ( object.contains( "tooltip" ) ) {
            row.tooltip = text( object, "tooltip" );
        }
        if ( object.contains( "tooltip_provider" ) ) {
            row.tooltipProvider = object.value( "tooltip_provider" ).toBool();
        }
        if ( object.contains( "tooltip_key" ) ) {
            row.tooltipKey = tooltipKeyOf( object );
        }
        if ( object.contains( "editors" ) ) {
            row.editors = object.value( "editors" ).toObject(); // the plugin's editors win
        }
        else {
            confirmStartValues( row );
        }
        row.pending.clear(); // any patch to the row ends its pending state
        Q_EMIT dataChanged( index( *found, 0 ), index( *found, qMax( 0, columnCount() - 1 ) ) );
    }
}

bool PluginTableModel::insert( const QJsonArray& rows, const QString& before )
{
    QVector<Row> added;
    QSet<QString> seen;
    for ( const auto& value : rows ) {
        const auto row = parse( value.toObject() );
        if ( !row.id.isEmpty() && !index_.contains( row.id ) && !seen.contains( row.id ) ) {
            seen.insert( row.id );
            added.append( row );
        }
    }
    if ( added.isEmpty() ) {
        return true;
    }
    if ( !fits( rows_.size() + added.size() ) ) {
        return false;
    }
    const int position
        = index_.contains( before ) ? index_.value( before ) : static_cast<int>( rows_.size() );
    beginInsertRows( {}, position, position + static_cast<int>( added.size() ) - 1 );
    rows_.insert( position, added.size(), Row{} );
    std::copy( added.begin(), added.end(), rows_.begin() + position );
    reindex();
    endInsertRows();
    return true;
}

bool PluginTableModel::remove( const QJsonArray& ids )
{
    QSet<int> found;
    for ( const auto& id : ids ) {
        const auto it = index_.find( id.toString() );
        if ( it != index_.end() ) {
            found.insert( *it );
        }
    }
    if ( found.isEmpty() ) {
        return false;
    }
    if ( found.size() > ForcedResetRows ) {
        beginResetModel();
        QVector<Row> kept;
        kept.reserve( rows_.size() - found.size() );
        for ( int row = 0; row < rows_.size(); ++row ) {
            if ( !found.contains( row ) ) {
                kept.append( rows_.at( row ) );
            }
        }
        rows_ = std::move( kept );
        reindex();
        endResetModel();
        return true;
    }
    QList<int> positions( found.begin(), found.end() );
    std::sort( positions.begin(), positions.end() );
    // Remove contiguous ranges from the end so earlier positions stay valid.
    int end = static_cast<int>( positions.size() ) - 1;
    while ( end >= 0 ) {
        int start = end;
        while ( start > 0 && positions.at( start - 1 ) == positions.at( start ) - 1 ) {
            --start;
        }
        const int first = positions.at( start );
        const int last = positions.at( end );
        beginRemoveRows( {}, first, last );
        rows_.remove( first, last - first + 1 );
        endRemoveRows();
        end = start - 1;
    }
    reindex();
    return false;
}

int PluginTableModel::rowOf( const QString& id ) const
{
    return index_.value( id, -1 );
}

QString PluginTableModel::columnId( int column ) const
{
    return column >= 0 && column < columns_.size() ? columns_.at( column ).id : QString{};
}

CellTooltipTarget PluginTableModel::tooltipTarget( const QModelIndex& index, bool tableProvider,
                                                   const QString& tableKey ) const
{
    CellTooltipTarget target;
    if ( !index.isValid() || index.row() >= rows_.size() || index.column() >= columns_.size() ) {
        return target;
    }
    const auto& row = rows_.at( index.row() );
    const auto& column = columns_.at( index.column() );
    target.rowId = row.id;
    target.columnId = column.id;
    target.provider = tableProvider || column.tooltipProvider || row.tooltipProvider;
    target.key = tableKey + '\x1f' + row.tooltipKey + '\x1f' + column.tooltipKey;
    return target;
}

void PluginTableModel::undoEdit( const QString& rowId, int column, const QString& display,
                                 const QString& shown, const std::optional<Pending>& earlier )
{
    const auto found = index_.find( rowId );
    if ( found == index_.end() ) {
        return;
    }
    auto& row = rows_[ *found ];
    if ( column >= row.cells.size() || row.cells.at( column ) != display
         || !row.pending.contains( column ) ) {
        return;
    }
    row.cells[ column ] = shown;
    if ( earlier ) {
        row.pending.insert( column, *earlier );
    }
    else {
        row.pending.remove( column );
    }
    Q_EMIT dataChanged( index( *found, column ), index( *found, column ) );
}

void PluginTableModel::confirmStartValues( Row& row ) const
{
    for ( auto it = row.pending.cbegin(); it != row.pending.cend(); ++it ) {
        const int column = it.key();
        if ( column >= columns_.size() || column >= row.cells.size()
             || row.cells.at( column ) != it->display ) {
            continue;
        }
        const auto& columnIdText = columns_.at( column ).id;
        auto editor = row.editors.value( columnIdText ).toObject();
        if ( editor.contains( "value" ) ) {
            editor.insert( "value", it->value );
            row.editors.insert( columnIdText, editor );
        }
    }
}

int PluginTableModel::cellLimit() const
{
    return columns_.isEmpty() ? PluginView::MaxTableColumns : static_cast<int>( columns_.size() );
}

void PluginTableModel::reindex()
{
    index_.clear();
    for ( int row = 0; row < rows_.size(); ++row ) {
        index_.insert( rows_.at( row ).id, row );
    }
}

// PluginCellDelegate

QWidget* PluginCellDelegate::createEditor( QWidget* parent, const QStyleOptionViewItem& option,
                                           const QModelIndex& index ) const
{
    Q_UNUSED( option );
    const auto editor = index.data( EditorRole ).value<QJsonObject>();
    const auto kind = editor.value( "kind" ).toString( "text" );
    QWidget* widget = nullptr;
    if ( kind == "multichoice" ) {
        widget = createChecks( parent, editor );
    }
    else if ( kind == "choice" || kind == "boolean" ) {
        widget = createChoice( parent, editor );
    }
    else {
        if ( kind != "text" && kind != "number" && onUnknownKind ) {
            onUnknownKind( kind );
        }
        widget = createLine( parent, editor, kind == "number" );
    }
    widget->setProperty( CellEditorProperty, true );
    return widget;
}

void PluginCellDelegate::setEditorData( QWidget* widget, const QModelIndex& index ) const
{
    // Every patch to a row re-reads the open editors in it (dataChanged). Once the user has
    // changed an editor's value, keep it and the start it is compared with on commit; the model
    // still drops a second commit of the same value.
    if ( const auto recorded = widget->property( CellStartProperty );
         recorded.isValid() && !sameValue( editorValue( widget ), recorded.value<QJsonValue>() ) ) {
        return;
    }
    const auto editor = index.data( EditorRole ).value<QJsonObject>();
    // The value of an edit the plugin has not confirmed yet (its text cannot tell choices that
    // share a label apart), else the plugin's starting value.
    const auto pending = index.data( PendingValueRole );
    const auto start = pending.isValid() ? pending.value<QJsonValue>() : editor.value( "value" );
    const bool single = !start.isUndefined() && !start.isArray() && !start.isObject();
    const auto shown = single ? cellText( start ) : index.data( Qt::EditRole ).toString();
    if ( auto* box = qobject_cast<CheckComboBox*>( widget ) ) {
        QJsonArray checked = start.toArray();
        if ( !start.isArray() ) {
            const auto labels = shown.split( QStringLiteral( ", " ) );
            for ( const auto& choice : editor.value( "choices" ).toArray() ) {
                if ( labels.contains( choiceLabel( choice ) ) ) {
                    checked.append( choiceValue( choice ) );
                }
            }
        }
        box->setCheckedValues( checked );
    }
    else if ( auto* combo = qobject_cast<QComboBox*>( widget ) ) {
        auto found = findChoice( combo, shown );
        if ( found < 0 && editor.value( "kind" ) == "boolean" ) {
            static const QStringList truthy{ "1", "true", "yes", "on" };
            static const QStringList falsy{ "0", "false", "no", "off" };
            const auto spelling = shown.trimmed();
            if ( truthy.contains( spelling, Qt::CaseInsensitive ) ) {
                found = combo->findData( true );
            }
            else if ( falsy.contains( spelling, Qt::CaseInsensitive ) ) {
                found = combo->findData( false );
            }
        }
        // No matching item (an out-of-range "7", an empty boolean): start with none, so any pick
        // is a change and leaving without a pick commits nothing.
        combo->setCurrentIndex( found );
    }
    else if ( auto* line = qobject_cast<QLineEdit*>( widget ) ) {
        line->setText( shown );
    }
    // What the editor shows now, however the cell reads (a boolean "1", a label, "30 s").
    widget->setProperty( CellStartProperty, QVariant::fromValue( editorValue( widget ) ) );
}

void PluginCellDelegate::setModelData( QWidget* widget, QAbstractItemModel* model,
                                       const QModelIndex& index ) const
{
    if ( auto* line = qobject_cast<QLineEdit*>( widget );
         line && line->validator() && !line->hasAcceptableInput() ) {
        return; // out of range: nothing is committed
    }
    const auto value = editorValue( widget );
    if ( value.isUndefined() ) {
        return; // no choices
    }
    // Closing an editor without a change is no edit.
    const auto start = widget->property( CellStartProperty );
    if ( start.isValid() && sameValue( value, start.value<QJsonValue>() ) ) {
        return;
    }
    if ( model->setData( index, QVariant::fromValue( value ), Qt::EditRole ) ) {
        // Committed: the same value committed again (Enter, then the focus leaving) is no edit.
        widget->setProperty( CellStartProperty, QVariant::fromValue( value ) );
    }
}

bool PluginCellDelegate::eventFilter( QObject* watched, QEvent* event )
{
    // A choice editor's list window (createChoice). Never passed on: the base class would take
    // the window for an editor. If the combo box is being destroyed, the queued close below is
    // dropped with it (the combo box is the timer's context object).
    if ( auto* combo = comboOfList( watched ) ) {
        if ( event->type() == QEvent::Show ) {
            combo->setProperty( CellChosenProperty, false );
        }
        else if ( event->type() == QEvent::Hide && !combo->property( CellChosenProperty ).toBool() ) {
            // Closed without choosing an item (Esc, a click outside): the edit ends unchanged.
            // Queued: when an item is chosen, its list hides just before `activated`.
            QTimer::singleShot( 0, combo, [ self = QPointer<PluginCellDelegate>( this ), combo ] {
                if ( self && !combo->isHidden() && !combo->property( CellChosenProperty ).toBool() ) {
                    Q_EMIT self->closeEditor( combo, QAbstractItemDelegate::RevertModelCache );
                }
            } );
        }
        return false;
    }
    return QStyledItemDelegate::eventFilter( watched, event );
}

QJsonValue PluginCellDelegate::editorValue( const QWidget* widget )
{
    if ( const auto* box = qobject_cast<const CheckComboBox*>( widget ) ) {
        return box->checkedValues();
    }
    if ( const auto* combo = qobject_cast<const QComboBox*>( widget ) ) {
        return combo->currentIndex() < 0 ? QJsonValue( QJsonValue::Undefined )
                                         : QJsonValue::fromVariant( combo->currentData() );
    }
    if ( const auto* line = qobject_cast<const QLineEdit*>( widget ) ) {
        return line->text();
    }
    return QJsonValue( QJsonValue::Undefined );
}

QComboBox* PluginCellDelegate::comboOfList( QObject* watched )
{
    auto* combo = qobject_cast<QComboBox*>( watched->parent() );
    return combo && !qobject_cast<CheckComboBox*>( combo )
                   && combo->property( CellListProperty ).value<QObject*>() == watched
               ? combo
               : nullptr;
}

QWidget* PluginCellDelegate::createChecks( QWidget* parent, const QJsonObject& editor ) const
{
    auto* box = new CheckComboBox( parent );
    // At most PluginView::MaxElements; E17c entries with icons, and `searchable` (section 8.4).
    box->setChoices( editor.value( "choices" ).toArray(), iconFor );
    if ( editor.value( "searchable" ).toBool() ) {
        box->makeSearchable();
    }
    auto* self = const_cast<PluginCellDelegate*>( this );
    // popupClosed comes from inside the popup's Hide handler: only signal here, the view
    // deletes the editor later (destroyEditor uses deleteLater).
    connect( box, &CheckComboBox::popupClosed, this, [ self, box ]( bool accepted ) {
        if ( box->isHidden() ) {
            return; // the view is closing the editor itself
        }
        if ( accepted ) {
            Q_EMIT self->commitData( box );
        }
        Q_EMIT self->closeEditor( box, QAbstractItemDelegate::NoHint );
    } );
    openPopupWhenShown( box );
    return box;
}

QWidget* PluginCellDelegate::createChoice( QWidget* parent, const QJsonObject& editor ) const
{
    auto* combo = new QComboBox( parent );
    // E17c: choice objects with icons, tooltips, disabled entries, separators and sections, and
    // `indicator: "radio"` (docs/plugin-sdk-advanced.md section 8.4).
    pluginchoices::fillCombo( combo,
                              pluginchoices::parse( editorChoices( editor ), PluginView::MaxElements ),
                              iconFor, editor.value( "indicator" ) == "radio" );
    if ( editor.value( "searchable" ).toBool() ) {
        pluginchoices::makeSearchable( combo );
    }
    auto* self = const_cast<PluginCellDelegate*>( this );
    connect( combo, &QComboBox::activated, this, [ self, combo ] {
        if ( combo->isHidden() ) {
            return; // already closed
        }
        combo->setProperty( CellChosenProperty, true );
        Q_EMIT self->commitData( combo );
        Q_EMIT self->closeEditor( combo, QAbstractItemDelegate::NoHint );
    } );
    // Closing the list without choosing ends the edit (eventFilter).
    auto* list = combo->view()->window();
    combo->setProperty( CellListProperty, QVariant::fromValue<QObject*>( list ) );
    list->installEventFilter( self );
    openPopupWhenShown( combo );
    return combo;
}

QWidget* PluginCellDelegate::createLine( QWidget* parent, const QJsonObject& editor, bool number )
{
    auto* line = new QLineEdit( parent );
    // No more than a cell shows (MaxTextChars): a change beyond that would leave the cell's
    // text unchanged and commit nothing.
    line->setMaxLength( editor.contains( "max_length" )
                            ? qBound( 1, editor.value( "max_length" ).toInt(), PluginView::MaxTextChars )
                            : PluginView::MaxTextChars );
    line->setPlaceholderText( text( editor, "placeholder" ) );
    if ( number ) {
        auto* validator = new QDoubleValidator( line );
        // Plain decimal numbers only: no exponent, no group separators ("1e2", "1,000").
        auto locale = QLocale::c();
        locale.setNumberOptions( QLocale::OmitGroupSeparator | QLocale::RejectGroupSeparator );
        validator->setLocale( locale );
        validator->setNotation( QDoubleValidator::StandardNotation );
        if ( editor.contains( "min" ) ) {
            validator->setBottom( editor.value( "min" ).toDouble() );
        }
        if ( editor.contains( "max" ) ) {
            validator->setTop( editor.value( "max" ).toDouble() );
        }
        line->setValidator( validator );
    }
    return line;
}

void PluginCellDelegate::openPopupWhenShown( QComboBox* combo )
{
    auto open = [ combo ] {
        if ( combo->isVisible() && !combo->view()->isVisible() ) {
            combo->showPopup();
        }
    };
    if ( QGuiApplication::mouseButtons() == Qt::NoButton ) {
        QTimer::singleShot( 0, combo, open );
        return;
    }
    new MouseReleaseWaiter( combo, open );
}

int PluginCellDelegate::findChoice( const QComboBox* combo, const QString& shown )
{
    for ( int i = 0; i < combo->count(); ++i ) {
        if ( cellText( QJsonValue::fromVariant( combo->itemData( i ) ) ) == shown ) {
            return i;
        }
    }
    for ( int i = 0; i < combo->count(); ++i ) {
        if ( combo->itemText( i ) == shown ) {
            return i;
        }
    }
    return -1;
}
} // namespace pluginview
