// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugintree.h"

#include "log.h"
#include "pluginfeatures.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDrag>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QTreeView>

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>

namespace pluginview {
namespace {
constexpr int MaxNodes = cilogg::plugins::MaxTreeNodes;
constexpr int MaxDepth = cilogg::plugins::MaxTreeDepth;
using Node = PluginTreeModel::Node;

Node* childAt( const Node& node, int row )
{
    return row >= 0 && row < static_cast<int>( node.children.size() )
               ? node.children.at( static_cast<size_t>( row ) ).get()
               : nullptr;
}

int childCount( const Node& node )
{
    return static_cast<int>( node.children.size() );
}

void renumber( Node& node )
{
    int row = 0;
    for ( auto& child : node.children ) {
        child->parent = &node;
        child->row = row++;
    }
}

bool hasPlaceholder( const Node& node )
{
    return node.children.size() == 1 && node.children.front()->placeholder;
}

Qt::CheckState checkStateOf( const QJsonValue& value )
{
    if ( value.isNull() ) {
        return Qt::PartiallyChecked;
    }
    return value.toBool() ? Qt::Checked : Qt::Unchecked;
}
} // namespace

PluginTreeModel::PluginTreeModel( QObject* parent )
    : QAbstractItemModel( parent )
{
}

PluginTreeModel::~PluginTreeModel() = default;

Node* PluginTreeModel::nodeOf( const QModelIndex& index ) const
{
    return index.isValid() ? static_cast<Node*>( index.internalPointer() ) : nullptr;
}

const Node* PluginTreeModel::nodeAt( const QModelIndex& index ) const
{
    return nodeOf( index );
}

Node* PluginTreeModel::find( const QString& id ) const
{
    return index_.value( id, nullptr );
}

QModelIndex PluginTreeModel::indexOfNode( const Node* node, int column ) const
{
    if ( node == nullptr || node == &root_ ) {
        return {};
    }
    return createIndex( node->row, column, const_cast<Node*>( node ) );
}

QModelIndex PluginTreeModel::indexOf( const QString& id, int column ) const
{
    return indexOfNode( find( id ), column );
}

QString PluginTreeModel::idOf( const QModelIndex& index ) const
{
    const auto* node = nodeOf( index );
    return node && !node->placeholder ? node->id : QString{};
}

QModelIndex PluginTreeModel::index( int row, int column, const QModelIndex& parent ) const
{
    if ( column < 0 || column >= columnCount() || parent.column() > 0 ) {
        return {};
    }
    const Node* owner = parent.isValid() ? nodeOf( parent ) : &root_;
    auto* child = owner ? childAt( *owner, row ) : nullptr;
    return child ? createIndex( row, column, child ) : QModelIndex{};
}

QModelIndex PluginTreeModel::parent( const QModelIndex& child ) const
{
    const auto* node = nodeOf( child );
    if ( node == nullptr || node->parent == nullptr || node->parent == &root_ ) {
        return {};
    }
    return createIndex( node->parent->row, 0, node->parent );
}

int PluginTreeModel::rowCount( const QModelIndex& parent ) const
{
    if ( parent.column() > 0 ) {
        return 0;
    }
    const Node* owner = parent.isValid() ? nodeOf( parent ) : &root_;
    return owner ? childCount( *owner ) : 0;
}

int PluginTreeModel::columnCount( const QModelIndex& parent ) const
{
    Q_UNUSED( parent );
    return qMax( 1, static_cast<int>( columns_.size() ) );
}

bool PluginTreeModel::hasChildren( const QModelIndex& parent ) const
{
    if ( parent.column() > 0 ) {
        return false;
    }
    const Node* owner = parent.isValid() ? nodeOf( parent ) : &root_;
    if ( owner == nullptr ) {
        return false;
    }
    // A lazy node with children to load shows its expander before they arrive.
    return !owner->children.empty() || ( lazy_ && owner->hasChildren && !owner->placeholder );
}

int PluginTreeModel::cellLimit() const
{
    return columnCount();
}

const PluginTreeModel::Column& PluginTreeModel::column( int index ) const
{
    static const Column none;
    return index >= 0 && index < columns_.size() ? columns_.at( index ) : none;
}

int PluginTreeModel::columnOf( const QString& id ) const
{
    for ( int index = 0; index < columns_.size(); ++index ) {
        if ( columns_.at( index ).id == id ) {
            return index;
        }
    }
    return -1;
}

QString PluginTreeModel::columnId( int at ) const
{
    return column( at ).id;
}

QVariant PluginTreeModel::data( const QModelIndex& index, int role ) const
{
    const auto* node = nodeOf( index );
    if ( node == nullptr ) {
        return {};
    }
    if ( node->placeholder ) {
        // "Loading…" or "Could not load" (section 28.1).
        if ( index.column() != 0 ) {
            return {};
        }
        if ( role == Qt::DisplayRole ) {
            return node->load == Load::Failed ? QObject::tr( "Could not load" )
                                              : QObject::tr( "Loading…" );
        }
        if ( role == Qt::ForegroundRole ) {
            return toneColor( QStringLiteral( "muted" ) );
        }
        if ( role == Qt::FontRole ) {
            QFont font;
            font.setItalic( true );
            return font;
        }
        return {};
    }
    const auto at = index.column();
    const auto value = at < node->cells.size() ? node->cells.at( at ) : QString{};
    const CellStyle* style = at < node->styles.size() ? &node->styles.at( at ) : nullptr;
    switch ( role ) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return value;
    case Qt::ToolTipRole: {
        // The cell's own tooltip, else the node's, else the text.
        const auto tip = style && !style->tooltip.isEmpty() ? style->tooltip
                         : node->tooltip.isEmpty()         ? value
                                                           : node->tooltip;
        return plainToolTip( tip );
    }
    case Qt::FontRole: {
        const bool italic = node->pending.contains( at );
        const bool bold = style && style->bold;
        if ( !italic && !bold ) {
            return {};
        }
        QFont font;
        font.setItalic( italic );
        font.setBold( bold );
        return font;
    }
    case Qt::TextAlignmentRole:
        return QVariant::fromValue( column( at ).align );
    case Qt::CheckStateRole:
        return at == 0 && isCheckable( *node ) ? QVariant( node->checked ) : QVariant{};
    case Qt::ForegroundRole: {
        const auto tone = style && !style->tone.isEmpty() ? style->tone : node->tone;
        return toneColor( tone ).isValid() ? QVariant( toneColor( tone ) ) : QVariant{};
    }
    case Qt::DecorationRole:
        if ( !iconFor ) {
            return {};
        }
        if ( style && !style->icon.isUndefined() && !style->icon.isNull() ) {
            return iconFor( style->icon );
        }
        if ( at == 0 && !node->icon.isUndefined() && !node->icon.isNull() ) {
            return iconFor( node->icon );
        }
        return {};
    default:
        break;
    }
    if ( role == NodeIdRole ) {
        return node->id;
    }
    if ( role == EditorRole ) {
        if ( !column( at ).editable ) {
            return QVariant::fromValue( QJsonObject{} );
        }
        return QVariant::fromValue( node->editors.value( columnId( at ) ).toObject() );
    }
    if ( role == PendingValueRole ) {
        const auto pending = node->pending.constFind( at );
        return pending != node->pending.cend() ? QVariant::fromValue( pending->value ) : QVariant{};
    }
    return {};
}

QVariant PluginTreeModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
    if ( orientation == Qt::Horizontal && section >= 0 && section < columns_.size() ) {
        const auto& info = columns_.at( section );
        if ( role == Qt::DisplayRole ) {
            return info.title;
        }
        if ( role == Qt::ToolTipRole && !info.tooltip.isEmpty() ) {
            return plainToolTip( info.tooltip );
        }
        if ( role == Qt::DecorationRole && iconFor && !info.icon.isUndefined()
             && !info.icon.isNull() ) {
            return iconFor( info.icon );
        }
        if ( role == Qt::TextAlignmentRole ) {
            return QVariant::fromValue( info.align );
        }
    }
    return QAbstractItemModel::headerData( section, orientation, role );
}

Qt::ItemFlags PluginTreeModel::flags( const QModelIndex& index ) const
{
    const auto* node = nodeOf( index );
    if ( node == nullptr ) {
        return Qt::NoItemFlags;
    }
    if ( node->placeholder ) {
        return Qt::ItemIsEnabled | Qt::ItemNeverHasChildren;
    }
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if ( index.column() == 0 && isCheckable( *node ) ) {
        flags |= Qt::ItemIsUserCheckable;
    }
    if ( column( index.column() ).editable
         && !node->editors.value( columnId( index.column() ) ).toObject().isEmpty() ) {
        flags |= Qt::ItemIsEditable;
    }
    return flags;
}

bool PluginTreeModel::setData( const QModelIndex& index, const QVariant& value, int role )
{
    auto* node = nodeOf( index );
    if ( node == nullptr || node->placeholder || !canEdit || !canEdit() ) {
        return false;
    }
    if ( role == Qt::CheckStateRole ) {
        // The user changed a check box; the behaviour decides the nodes and reports them.
        if ( index.column() != 0 || !isCheckable( *node ) ) {
            return false;
        }
        const bool checked = value.toInt() == Qt::Checked;
        if ( node->checked == ( checked ? Qt::Checked : Qt::Unchecked ) ) {
            return false;
        }
        if ( onCheckRequested ) {
            onCheckRequested( node->id, checked );
            return true;
        }
        return !setChecked( { node->id }, checked ).isEmpty();
    }
    const auto editor = data( index, EditorRole ).value<QJsonObject>();
    if ( role != Qt::EditRole || editor.isEmpty() ) {
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
        return false;
    }
    const int at = index.column();
    const auto previous = at < node->cells.size() ? node->cells.at( at ) : QString{};
    // The same rules as table cells: an unchanged value is no edit.
    if ( const auto pending = node->pending.constFind( at ); pending != node->pending.cend() ) {
        if ( sameValue( pending->value, json ) ) {
            return false;
        }
    }
    else {
        const auto kind = editor.value( "kind" ).toString( "text" );
        if ( kind != "choice" && kind != "boolean" && kind != "multichoice"
             && cellText( json ) == previous ) {
            return false;
        }
    }
    const auto display = PluginTableModel::displayFor( editor, json );
    while ( node->cells.size() <= at ) {
        node->cells.append( QString{} );
    }
    const auto earlier = node->pending.contains( at )
                             ? std::optional<Pending>( node->pending.value( at ) )
                             : std::nullopt;
    if ( !earlier ) {
        node->pending.insert( at, { previous, {}, {} } );
    }
    auto& pending = node->pending[ at ];
    pending.display = display;
    pending.value = json;
    node->cells[ at ] = display;
    const auto nodeId = node->id;
    Q_EMIT dataChanged( index, index );
    if ( onEdited && !onEdited( nodeId, columnId( at ), json, previous ) ) {
        undoEdit( nodeId, at, display, previous, earlier );
        return false;
    }
    return true;
}

void PluginTreeModel::undoEdit( const QString& nodeId, int column, const QString& display,
                                const QString& shown, const std::optional<Pending>& earlier )
{
    auto* node = find( nodeId );
    // Skipped when something changed the cell meanwhile (the event's slots may patch the tree).
    if ( node == nullptr || column >= node->cells.size() || node->cells.at( column ) != display ) {
        return;
    }
    node->cells[ column ] = shown;
    if ( earlier ) {
        node->pending.insert( column, *earlier );
    }
    else {
        node->pending.remove( column );
    }
    const auto changed = indexOfNode( node, column );
    Q_EMIT dataChanged( changed, changed );
}

void PluginTreeModel::revertPending()
{
    forEach( [ this ]( const Node& constNode, int ) {
        auto* node = find( constNode.id );
        if ( node == nullptr || node->pending.isEmpty() ) {
            return;
        }
        for ( auto it = node->pending.cbegin(); it != node->pending.cend(); ++it ) {
            if ( it.key() < node->cells.size() ) {
                node->cells[ it.key() ] = it->previous;
            }
        }
        node->pending.clear();
        Q_EMIT dataChanged( indexOfNode( node, 0 ), indexOfNode( node, columnCount() - 1 ) );
    } );
}

void PluginTreeModel::setCheckboxes( bool checkboxes )
{
    if ( checkboxes_ == checkboxes ) {
        return;
    }
    checkboxes_ = checkboxes;
    // Every box appears or goes: one signal per parent, not one per node.
    QList<Node*> all;
    forEach( [ this, &all ]( const Node& node, int ) { all.append( find( node.id ) ); } );
    emitCheckChanges( all );
    recomputeChecks();
}

void PluginTreeModel::emitCheckChanges( const QList<Node*>& nodes )
{
    QHash<Node*, std::pair<int, int>> ranges; // parent -> first and last row
    for ( auto* node : nodes ) {
        if ( node == nullptr || node->parent == nullptr ) {
            continue;
        }
        auto found = ranges.find( node->parent );
        if ( found == ranges.end() ) {
            ranges.insert( node->parent, { node->row, node->row } );
        }
        else {
            found->first = qMin( found->first, node->row );
            found->second = qMax( found->second, node->row );
        }
    }
    for ( auto it = ranges.cbegin(); it != ranges.cend(); ++it ) {
        const auto parent = indexOfNode( it.key() );
        Q_EMIT dataChanged( index( it->first, 0, parent ), index( it->second, 0, parent ),
                            { Qt::CheckStateRole } );
    }
}

void PluginTreeModel::setAutoTristate( bool autoTristate )
{
    if ( autoTristate_ == autoTristate ) {
        return;
    }
    autoTristate_ = autoTristate;
    recomputeChecks();
}

bool PluginTreeModel::setColumns( const QJsonArray& columns )
{
    if ( columns.size() > PluginView::MaxTableColumns ) {
        return false;
    }
    QVector<Column> parsed;
    for ( const auto& value : columns ) {
        parsed.append( PluginTableModel::parseColumn( value ) );
    }
    beginResetModel();
    columns_ = parsed;
    const auto limit = cellLimit();
    for ( auto* node : std::as_const( index_ ) ) {
        node->cells = node->cells.mid( 0, limit );
        node->styles = node->styles.mid( 0, limit );
        node->pending.removeIf(
            [ limit ]( QHash<int, Pending>::iterator it ) { return it.key() >= limit; } );
    }
    endResetModel();
    return true;
}

bool PluginTreeModel::isCheckable( const Node& node ) const
{
    // A node's `checkable` applies only in a tree with `checkboxes` (section 28.1).
    if ( node.placeholder || !checkboxes_ ) {
        return false;
    }
    return node.checkable != 0;
}

void PluginTreeModel::noteExpand( const QJsonObject& object )
{
    if ( object.value( "expanded" ).isBool() ) {
        expandRequests_.insert( object.value( "id" ).toString().left( PluginView::MaxTextChars ),
                                object.value( "expanded" ).toBool() );
    }
}

QHash<QString, bool> PluginTreeModel::takeExpandRequests()
{
    return std::exchange( expandRequests_, {} );
}

void PluginTreeModel::parseInto( Node& node, const QJsonObject& object, bool whole ) const
{
    if ( whole ) {
        node.id = object.value( "id" ).toString().left( PluginView::MaxTextChars );
    }
    // `cells` wins over `text`; an update with either replaces the cells and confirms (or
    // replaces) the edits that waited for the plugin.
    if ( object.value( "cells" ).isArray() ) {
        node.cells.clear();
        node.styles.clear();
        PluginTableModel::parseCells( object.value( "cells" ).toArray(), cellLimit(), &node.cells,
                                      &node.styles );
        node.pending.clear();
    }
    else if ( object.contains( "text" ) ) {
        node.cells = QStringList{ cellText( object.value( "text" ) ) };
        node.styles.clear();
        node.pending.clear();
    }
    const auto set = [ & ]( const char* key, auto apply ) {
        const QLatin1String name( key );
        if ( whole || object.contains( name ) ) {
            apply( object.value( name ) );
        }
    };
    set( "icon", [ & ]( const QJsonValue& value ) { node.icon = value; } );
    set( "tooltip", [ & ]( const QJsonValue& value ) {
        node.tooltip = value.toString().left( PluginView::MaxTextChars );
    } );
    set( "tooltip_key", [ & ]( const QJsonValue& value ) {
        node.tooltipKey = value.toString().left( cilogg::plugins::MaxTooltipKeyChars );
    } );
    set( "tone", [ & ]( const QJsonValue& value ) { node.tone = value.toString().left( 16 ); } );
    set( "editors", [ & ]( const QJsonValue& value ) { node.editors = value.toObject(); } );
    set( "has_children",
         [ & ]( const QJsonValue& value ) { node.hasChildren = value.toBool(); } );
    set( "checkable", [ & ]( const QJsonValue& value ) {
        node.checkable = value.isBool() ? ( value.toBool() ? 1 : 0 ) : -1;
    } );
    set( "checked", [ & ]( const QJsonValue& value ) {
        node.checked = value.isUndefined() ? Qt::Unchecked : checkStateOf( value );
    } );
}

int PluginTreeModel::countNodes( const QJsonArray& nodes, int depth )
{
    if ( depth > MaxDepth ) {
        return 0;
    }
    int count = 0;
    for ( const auto& value : nodes ) {
        if ( !value.isObject() ) {
            continue;
        }
        ++count;
        const auto children = value.toObject().value( "children" );
        if ( children.isArray() ) {
            count += countNodes( children.toArray(), depth + 1 );
        }
        if ( count > MaxNodes ) {
            break; // over the limit anyway
        }
    }
    return count;
}

int PluginTreeModel::countLoaded( const Node& node ) const
{
    int count = 0;
    for ( const auto& child : node.children ) {
        if ( !child->placeholder ) {
            count += 1 + countLoaded( *child );
        }
    }
    return count;
}

int PluginTreeModel::depthOf( const QString& id ) const
{
    int depth = 0;
    for ( const auto* node = find( id ); node && node != &root_; node = node->parent ) {
        ++depth;
    }
    return depth;
}

QString PluginTreeModel::parentIdOf( const QString& id ) const
{
    const auto* node = find( id );
    return node && node->parent && node->parent != &root_ ? node->parent->id : QString{};
}

bool PluginTreeModel::isWithin( const QString& id, const QString& ancestor ) const
{
    for ( const auto* node = find( id ); node && node != &root_; node = node->parent ) {
        if ( node->id == ancestor ) {
            return true;
        }
    }
    return false;
}

std::vector<std::unique_ptr<Node>> PluginTreeModel::parseNodes( const QJsonArray& nodes,
                                                                Node* parent, int depth,
                                                                const QSet<QString>* replaced,
                                                                QSet<QString>* fresh,
                                                                bool inheritCheck )
{
    // No copy of the whole id index per operation: an id is taken when the tree keeps it.
    const auto taken = [ & ]( const QString& id ) {
        return fresh->contains( id )
               || ( index_.contains( id ) && !( replaced && replaced->contains( id ) ) );
    };
    std::vector<std::unique_ptr<Node>> parsed;
    for ( const auto& value : nodes ) {
        if ( !value.isObject() ) {
            if ( warn ) {
                warn( QStringLiteral( "tree-node" ), QStringLiteral( "tree node is not an object" ) );
            }
            continue;
        }
        const auto object = value.toObject();
        const auto id = object.value( "id" ).toString().left( PluginView::MaxTextChars );
        if ( depth > MaxDepth ) {
            if ( warn ) {
                warn( QStringLiteral( "tree-depth" ),
                      QStringLiteral( "tree nodes deeper than %1 levels dropped" ).arg( MaxDepth ) );
            }
            continue;
        }
        if ( id.isEmpty() || taken( id ) ) {
            if ( warn ) {
                warn( QStringLiteral( "tree-id:" ) + id.left( 128 ),
                      QStringLiteral( "tree node without an id or with a repeated id skipped: " )
                          + id.left( 128 ) );
            }
            continue;
        }
        fresh->insert( id );
        noteExpand( object );
        auto node = std::make_unique<Node>();
        node->parent = parent;
        parseInto( *node, object, true );
        // Children loaded below a checked or unchecked parent follow it (auto_tristate) unless
        // they set their own state.
        if ( inheritCheck && !object.contains( "checked" ) && parent
             && parent->checked != Qt::PartiallyChecked ) {
            node->checked = parent->checked;
        }
        if ( object.value( "children" ).isArray() ) {
            node->children = parseNodes( object.value( "children" ).toArray(), node.get(),
                                         depth + 1, replaced, fresh, inheritCheck );
            renumber( *node );
        }
        parsed.push_back( std::move( node ) );
    }
    return parsed;
}

void PluginTreeModel::addToIndex( Node& node )
{
    for ( auto& child : node.children ) {
        if ( !child->placeholder ) {
            index_.insert( child->id, child.get() );
            ++nodeCount_;
            addToIndex( *child );
        }
    }
}

void PluginTreeModel::removeFromIndex( Node& node )
{
    for ( auto& child : node.children ) {
        if ( !child->placeholder ) {
            index_.remove( child->id );
            --nodeCount_;
            removeFromIndex( *child );
        }
    }
}

void PluginTreeModel::collectIds( const Node& node, QStringList* ids ) const
{
    for ( const auto& child : node.children ) {
        if ( !child->placeholder ) {
            ids->append( child->id );
            collectIds( *child, ids );
        }
    }
}

void PluginTreeModel::setChildren( Node* parent, std::vector<std::unique_ptr<Node>> children )
{
    const auto owner = indexOfNode( parent );
    if ( !parent->children.empty() ) {
        beginRemoveRows( owner, 0, childCount( *parent ) - 1 );
        removeFromIndex( *parent );
        parent->children.clear();
        endRemoveRows();
    }
    if ( !children.empty() ) {
        beginInsertRows( owner, 0, static_cast<int>( children.size() ) - 1 );
        parent->children = std::move( children );
        renumber( *parent );
        addToIndex( *parent );
        endInsertRows();
    }
}

bool PluginTreeModel::replace( const QJsonArray& nodes, const QString& parentId, QString* error,
                               QStringList* changed )
{
    auto* parent = parentId.isEmpty() ? &root_ : find( parentId );
    if ( parent == nullptr ) {
        *error = QStringLiteral( "replace_nodes: no node %1" ).arg( parentId.left( 128 ) );
        return false;
    }
    const auto removing = countLoaded( *parent );
    const auto incoming = countNodes( nodes, depthOf( parentId ) + 1 );
    if ( nodeCount_ - removing + incoming > MaxNodes ) {
        *error = QStringLiteral( "replace_nodes rejected: more than %1 nodes" ).arg( MaxNodes );
        return false;
    }
    QStringList old;
    collectIds( *parent, &old );
    if ( changed ) {
        changed->append( old );
    }
    const QSet<QString> replaced( old.cbegin(), old.cend() );
    QSet<QString> fresh;
    auto parsed = parseNodes( nodes, parent, depthOf( parentId ) + 1, &replaced, &fresh, false );
    if ( parent == &root_ ) {
        // A whole new tree: one reset (the behaviour keeps expansion and selection by id).
        beginResetModel();
        removeFromIndex( root_ );
        root_.children = std::move( parsed );
        renumber( root_ );
        addToIndex( root_ );
        endResetModel();
    }
    else {
        // The plugin set the node's children: it no longer waits for tree/children.
        parent->load = Load::None;
        parent->hasChildren = false;
        setChildren( parent, std::move( parsed ) );
        const auto self = indexOfNode( parent );
        Q_EMIT dataChanged( self, self );
    }
    recomputeChecks();
    return true;
}

bool PluginTreeModel::update( const QJsonArray& nodes, QString* error, QStringList* changed )
{
    // An operation that would pass MaxNodes changes nothing: the entries' children are applied to
    // projected subtree sizes, in order, before any node changes. A replaced subtree counts as the
    // children it got, whichever entry came first. A node the tree does not hold yet, or one below
    // a node an earlier entry replaces (an earlier entry may give it again, with that id), counts
    // as adding all its children and removing nothing: never less than the change it can make.
    std::unordered_map<const Node*, int> loadedAfter;
    const std::function<int( const Node& )> loadedNow = [ & ]( const Node& node ) {
        if ( const auto found = loadedAfter.find( &node ); found != loadedAfter.end() ) {
            return found->second;
        }
        int count = 0;
        for ( const auto& child : node.children ) {
            if ( !child->placeholder ) { // a "Loading…" line is no node (it is not counted)
                count += 1 + loadedNow( *child );
            }
        }
        return count;
    };
    qint64 projected = nodeCount_;
    for ( const auto& value : nodes ) {
        const auto object = value.toObject();
        if ( !object.value( "children" ).isArray() ) {
            continue;
        }
        const auto children = object.value( "children" ).toArray();
        const auto* node = find( object.value( "id" ).toString().left( PluginView::MaxTextChars ) );
        if ( node == nullptr ) {
            projected += countNodes( children, 1 );
            continue;
        }
        bool gone = false;
        for ( const auto* above = node->parent; above != nullptr && !gone; above = above->parent ) {
            gone = loadedAfter.count( above ) > 0;
        }
        if ( gone ) {
            projected += countNodes( children, 1 );
            continue;
        }
        const auto adding = countNodes( children, depthOf( node->id ) + 1 );
        projected += adding - loadedNow( *node );
        loadedAfter[ node ] = adding;
    }
    if ( projected > MaxNodes ) {
        *error = QStringLiteral( "update_nodes rejected: more than %1 nodes" ).arg( MaxNodes );
        return false;
    }
    bool ok = true;
    // The parents' check states follow once, after every node changed.
    ++deferChecks_;
    for ( const auto& value : nodes ) {
        const auto object = value.toObject();
        const auto id = object.value( "id" ).toString().left( PluginView::MaxTextChars );
        auto* node = find( id );
        if ( node == nullptr ) {
            *error = QStringLiteral( "update_nodes: no node %1" ).arg( id.left( 128 ) );
            ok = false;
            continue;
        }
        const auto before = node->checked;
        parseInto( *node, object, false );
        noteExpand( object );
        if ( changed ) {
            changed->append( id );
        }
        Q_EMIT dataChanged( indexOfNode( node, 0 ), indexOfNode( node, columnCount() - 1 ) );
        if ( object.value( "children" ).isArray() ) {
            QString failure;
            if ( !replace( object.value( "children" ).toArray(), id, &failure, changed ) ) {
                *error = failure;
                ok = false;
            }
        }
        // With auto_tristate the plugin checking a parent checks its loaded descendants, as a
        // click does.
        if ( autoTristate_ && object.value( "checked" ).isBool() && node->checked != before ) {
            // The node has its state already; its loaded descendants follow.
            applyChecked( { id }, node->checked == Qt::Checked, nullptr, false );
        }
    }
    --deferChecks_;
    recomputeChecks();
    return ok;
}

bool PluginTreeModel::insert( const QJsonArray& nodes, const QString& parentId,
                              const QString& before, QString* error )
{
    auto* parent = parentId.isEmpty() ? &root_ : find( parentId );
    if ( parent == nullptr ) {
        *error = QStringLiteral( "insert_nodes: no node %1" ).arg( parentId.left( 128 ) );
        return false;
    }
    if ( nodeCount_ + countNodes( nodes, depthOf( parentId ) + 1 ) > MaxNodes ) {
        *error = QStringLiteral( "insert_nodes rejected: more than %1 nodes" ).arg( MaxNodes );
        return false;
    }
    if ( hasPlaceholder( *parent ) ) {
        // The plugin filled a node that was loading or failed to load.
        setChildren( parent, {} );
        parent->load = Load::None;
        parent->hasChildren = false;
    }
    QSet<QString> fresh;
    // As loaded children: below a checked or unchecked parent new nodes follow it.
    auto parsed
        = parseNodes( nodes, parent, depthOf( parentId ) + 1, nullptr, &fresh, autoTristate_ );
    if ( parsed.empty() ) {
        return true;
    }
    int position = childCount( *parent );
    if ( const auto* anchor = find( before ); anchor && anchor->parent == parent ) {
        position = anchor->row;
    }
    beginInsertRows( indexOfNode( parent ), position,
                     position + static_cast<int>( parsed.size() ) - 1 );
    for ( auto& node : parsed ) {
        index_.insert( node->id, node.get() );
        ++nodeCount_;
        addToIndex( *node );
    }
    // One insertion of the whole range: no shifting per node.
    parent->children.insert( parent->children.begin() + position,
                             std::make_move_iterator( parsed.begin() ),
                             std::make_move_iterator( parsed.end() ) );
    renumber( *parent );
    endInsertRows();
    recomputeChecks();
    return true;
}

QStringList PluginTreeModel::remove( const QJsonArray& ids )
{
    QStringList removed;
    if ( ids.size() > LargeRemoval ) {
        // Many nodes: one reset and one pass per parent instead of a shift per node (the
        // behaviour keeps expansion and selection by id).
        QSet<Node*> requested;
        for ( const auto& value : ids ) {
            if ( auto* node = find( value.toString() ) ) {
                requested.insert( node );
            }
        }
        // Only the topmost requested nodes: their descendants go with them, and a parent that
        // is removed itself is never touched again.
        QHash<Node*, QSet<Node*>> byParent;
        for ( auto* node : std::as_const( requested ) ) {
            bool covered = false;
            for ( auto* up = node->parent; up && up != &root_ && !covered; up = up->parent ) {
                covered = requested.contains( up );
            }
            if ( !covered ) {
                byParent[ node->parent ].insert( node );
            }
        }
        if ( byParent.isEmpty() ) {
            return removed;
        }
        beginResetModel();
        for ( auto it = byParent.begin(); it != byParent.end(); ++it ) {
            auto& children = it.key()->children;
            for ( auto* node : std::as_const( it.value() ) ) {
                removed.append( node->id );
                collectIds( *node, &removed );
                index_.remove( node->id );
                --nodeCount_;
                removeFromIndex( *node );
            }
            children.erase( std::remove_if( children.begin(), children.end(),
                                            [ &it ]( const std::unique_ptr<Node>& child ) {
                                                return it.value().contains( child.get() );
                                            } ),
                            children.end() );
            renumber( *it.key() );
        }
        endResetModel();
        recomputeChecks();
        return removed;
    }
    for ( const auto& value : ids ) {
        auto* node = find( value.toString() );
        if ( node == nullptr ) {
            continue; // unknown, or already gone with an ancestor
        }
        removed.append( node->id );
        collectIds( *node, &removed );
        auto* parent = node->parent;
        const auto row = node->row;
        beginRemoveRows( indexOfNode( parent ), row, row );
        index_.remove( node->id );
        --nodeCount_;
        removeFromIndex( *node );
        parent->children.erase( parent->children.begin() + row );
        renumber( *parent );
        endRemoveRows();
    }
    if ( !removed.isEmpty() ) {
        recomputeChecks();
    }
    return removed;
}

bool PluginTreeModel::wantsChildren( const QModelIndex& index ) const
{
    const auto* node = nodeOf( index );
    if ( node == nullptr || node->placeholder || !lazy_ || !node->hasChildren
         || node->load == Load::Loading ) {
        return false;
    }
    // Not loaded yet, or the last load failed (the next expand asks again).
    return node->children.empty() || ( node->load == Load::Failed && hasPlaceholder( *node ) );
}

void PluginTreeModel::setLoading( const QString& id )
{
    auto* node = find( id );
    if ( node == nullptr ) {
        return;
    }
    node->load = Load::Loading;
    if ( hasPlaceholder( *node ) ) {
        node->children.front()->load = Load::Loading;
        const auto changed = indexOfNode( node->children.front().get() );
        Q_EMIT dataChanged( changed, changed );
        return;
    }
    if ( !node->children.empty() ) {
        return;
    }
    beginInsertRows( indexOfNode( node ), 0, 0 );
    auto placeholder = std::make_unique<Node>();
    placeholder->placeholder = true;
    placeholder->load = Load::Loading;
    node->children.push_back( std::move( placeholder ) );
    renumber( *node );
    endInsertRows();
}

void PluginTreeModel::setLoadFailed( const QString& id )
{
    auto* node = find( id );
    if ( node == nullptr ) {
        return;
    }
    node->load = Load::Failed;
    if ( hasPlaceholder( *node ) ) {
        node->children.front()->load = Load::Failed;
        const auto changed = indexOfNode( node->children.front().get() );
        Q_EMIT dataChanged( changed, changed );
    }
}

void PluginTreeModel::cancelLoading( const QString& id )
{
    auto* node = find( id );
    if ( node == nullptr || node->load != Load::Loading ) {
        return;
    }
    // Not loaded: the "Loading…" line goes and the next expand asks again.
    node->load = Load::None;
    if ( hasPlaceholder( *node ) ) {
        beginRemoveRows( indexOfNode( node ), 0, 0 );
        node->children.clear();
        endRemoveRows();
    }
}

bool PluginTreeModel::isLoading( const QString& id ) const
{
    const auto* node = find( id );
    return node && node->load == Load::Loading;
}

bool PluginTreeModel::fillChildren( const QString& id, const QJsonArray& nodes, QString* error )
{
    auto* node = find( id );
    if ( node == nullptr ) {
        return false;
    }
    const auto depth = depthOf( id ) + 1;
    if ( nodeCount_ + countNodes( nodes, depth ) > MaxNodes ) {
        *error = QStringLiteral( "tree/children answer rejected: more than %1 nodes" ).arg( MaxNodes );
        return false;
    }
    // The node's current children (normally only the placeholder) make way for the answer.
    QStringList old;
    collectIds( *node, &old );
    const QSet<QString> replaced( old.cbegin(), old.cend() );
    QSet<QString> fresh;
    auto parsed = parseNodes( nodes, node, depth, &replaced, &fresh, autoTristate_ );
    node->load = Load::None;
    node->hasChildren = false; // the children are known now (possibly none)
    setChildren( node, std::move( parsed ) );
    const auto self = indexOfNode( node );
    Q_EMIT dataChanged( self, self );
    recomputeChecks();
    return true;
}

void PluginTreeModel::forEach( const std::function<void( const Node& node, int depth )>& visit ) const
{
    // Iterative: a tree of 50,000 nodes in one chain is only 64 deep, but stay safe.
    std::vector<std::pair<const Node*, int>> pending;
    for ( auto it = root_.children.rbegin(); it != root_.children.rend(); ++it ) {
        pending.emplace_back( it->get(), 0 );
    }
    while ( !pending.empty() ) {
        const auto [ node, depth ] = pending.back();
        pending.pop_back();
        if ( node->placeholder ) {
            continue;
        }
        visit( *node, depth );
        for ( auto it = node->children.rbegin(); it != node->children.rend(); ++it ) {
            pending.emplace_back( it->get(), depth + 1 );
        }
    }
}

QStringList PluginTreeModel::setChecked( const QStringList& ids, bool checked,
                                         QHash<QString, Qt::CheckState>* previous )
{
    return applyChecked( ids, checked, previous, true );
}

QStringList PluginTreeModel::applyChecked( const QStringList& ids, bool checked,
                                           QHash<QString, Qt::CheckState>* previous,
                                           bool recompute )
{
    const auto target = checked ? Qt::Checked : Qt::Unchecked;
    QStringList set;
    QList<Node*> changed;
    QSet<const Node*> done;
    const auto apply = [ & ]( Node& node ) {
        if ( !isCheckable( node ) || done.contains( &node ) ) {
            return;
        }
        done.insert( &node );
        if ( node.checked == target ) {
            return;
        }
        if ( previous && !previous->contains( node.id ) ) {
            previous->insert( node.id, node.checked );
        }
        node.checked = target;
        set.append( node.id );
        changed.append( &node );
    };
    // Descendants without an id lookup each: walk the loaded subtree directly.
    const std::function<void( Node& )> applyBelow = [ & ]( Node& node ) {
        for ( auto& child : node.children ) {
            if ( !child->placeholder ) {
                apply( *child );
                applyBelow( *child );
            }
        }
    };
    for ( const auto& id : ids ) {
        auto* node = find( id );
        if ( node == nullptr ) {
            continue;
        }
        apply( *node );
        if ( autoTristate_ ) {
            applyBelow( *node );
        }
    }
    emitCheckChanges( changed );
    if ( recompute ) {
        // The parents follow; their previous states are kept so a dropped event restores them.
        recomputeChecks( previous );
    }
    return set;
}

void PluginTreeModel::restoreChecks( const QHash<QString, Qt::CheckState>& states )
{
    QList<Node*> changed;
    for ( auto it = states.cbegin(); it != states.cend(); ++it ) {
        auto* node = find( it.key() );
        if ( node == nullptr || node->checked == it.value() ) {
            continue;
        }
        node->checked = it.value();
        changed.append( node );
    }
    emitCheckChanges( changed );
}

Qt::CheckState PluginTreeModel::derive( Node& node, QList<Node*>* changed,
                                        QHash<QString, Qt::CheckState>* previous )
{
    bool any = false;
    bool allChecked = true;
    bool allUnchecked = true;
    for ( auto& child : node.children ) {
        if ( child->placeholder ) {
            continue;
        }
        const auto state = derive( *child, changed, previous );
        if ( !isCheckable( *child ) ) {
            continue;
        }
        any = true;
        allChecked = allChecked && state == Qt::Checked;
        allUnchecked = allUnchecked && state == Qt::Unchecked;
    }
    if ( !any || &node == &root_ || !isCheckable( node ) ) {
        return node.checked;
    }
    const auto derived = allChecked     ? Qt::Checked
                         : allUnchecked ? Qt::Unchecked
                                        : Qt::PartiallyChecked;
    if ( derived != node.checked ) {
        if ( previous && !previous->contains( node.id ) ) {
            previous->insert( node.id, node.checked );
        }
        node.checked = derived;
        changed->append( &node );
    }
    return derived;
}

void PluginTreeModel::recomputeChecks( QHash<QString, Qt::CheckState>* previous )
{
    if ( !autoTristate_ || deferChecks_ > 0 ) {
        return;
    }
    QList<Node*> changed;
    derive( root_, &changed, previous );
    emitCheckChanges( changed );
}
} // namespace pluginview

// PluginTreeBehaviour

using pluginview::PluginTreeModel;

PluginTreeBehaviour::PluginTreeBehaviour( QTreeView* tree, PluginTreeModel* model, Hooks hooks )
    : QObject( tree )
    , tree_( tree )
    , model_( model )
    , hooks_( std::move( hooks ) )
{
    model_->onCheckRequested = [ this ]( const QString& nodeId, bool checked ) {
        requestCheck( nodeId, checked );
    };
    tree->viewport()->installEventFilter( this );
    tree->installEventFilter( this );
    tree->setContextMenuPolicy( Qt::CustomContextMenu );
    // The menu is a child of the tree: a plugin that stops, or an entry that closes the view,
    // deletes it during exec(), so it is only touched through a QPointer afterwards.
    connect( tree, &QTreeView::customContextMenuRequested, this, [ this ]( const QPoint& at ) {
        const QPointer<QMenu> menu( buildMenu( at ) );
        if ( menu ) {
            menu->exec( tree_->viewport()->mapToGlobal( at ) );
        }
        if ( menu ) {
            menu->deleteLater();
        }
    } );
    connect( tree, &QTreeView::doubleClicked, this,
             [ this ]( const QModelIndex& index ) { activate( index ); } );
    connect( tree, &QTreeView::expanded, this,
             [ this ]( const QModelIndex& index ) { expandedChanged( index, true ); } );
    connect( tree, &QTreeView::collapsed, this,
             [ this ]( const QModelIndex& index ) { expandedChanged( index, false ); } );
    // Nodes inserted, removed, moved or re-laid out under a press that may start a drag: the
    // press is called off before the change, while its selection ranges still name its nodes.
    const auto cancel = [ this ] { cancelPress(); };
    connect( model_, &QAbstractItemModel::rowsAboutToBeInserted, this, cancel );
    connect( model_, &QAbstractItemModel::rowsAboutToBeRemoved, this, cancel );
    connect( model_, &QAbstractItemModel::rowsAboutToBeMoved, this, cancel );
    connect( model_, &QAbstractItemModel::modelAboutToBeReset, this, cancel );
    connect( model_, &QAbstractItemModel::layoutAboutToBeChanged, this, cancel );
}

void PluginTreeBehaviour::setOptions( const Options& options )
{
    options_ = options;
    if ( !tree_ ) {
        return;
    }
    tree_->header()->setVisible( options.header && model_->hasColumns() );
    tree_->viewport()->setAcceptDrops( options.reorderable );
}

QList<int> PluginTreeBehaviour::shownColumns() const
{
    QList<int> shown;
    if ( !tree_ ) {
        return shown;
    }
    const auto* header = tree_->header();
    for ( int visual = 0; visual < header->count(); ++visual ) {
        const auto logical = header->logicalIndex( visual );
        if ( !header->isSectionHidden( logical ) ) {
            shown.append( logical );
        }
    }
    return shown;
}

void PluginTreeBehaviour::rememberView( bool expansion )
{
    if ( !tree_ ) {
        return;
    }
    keptExpanded_.clear();
    keptExpansion_ = expansion;
    if ( expansion ) {
        // Only operations that reset the model or replace children walk every node.
        model_->forEach( [ this ]( const PluginTreeModel::Node& node, int ) {
            if ( !node.children.empty() && tree_->isExpanded( model_->indexOf( node.id ) ) ) {
                keptExpanded_.append( node.id );
            }
        } );
        keptScroll_ = tree_->verticalScrollBar()->value();
    }
    keptSelected_ = selectedNodeIds( std::numeric_limits<int>::max() );
    keptCurrent_ = currentNodeId();
}

void PluginTreeBehaviour::restoreView( bool reportRemoved )
{
    if ( !tree_ ) {
        return;
    }
    for ( const auto& id : std::as_const( keptExpanded_ ) ) {
        const auto index = model_->indexOf( id );
        if ( index.isValid() && !tree_->isExpanded( index ) ) {
            tree_->setExpanded( index, true );
        }
    }
    auto* selection = tree_->selectionModel();
    QItemSelection kept;
    int restored = 0;
    for ( const auto& id : std::as_const( keptSelected_ ) ) {
        const auto index = model_->indexOf( id );
        if ( index.isValid() ) {
            kept.select( index, index );
            ++restored;
        }
    }
    if ( selectedNodeIds( std::numeric_limits<int>::max() ) != keptSelected_ ) {
        selection->select( kept, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
    }
    if ( const auto current = model_->indexOf( keptCurrent_ );
         current.isValid() && currentNodeId() != keptCurrent_ ) {
        selection->setCurrentIndex( current, QItemSelectionModel::NoUpdate );
    }
    if ( keptExpansion_ ) {
        // The scroll range is only updated after layout: restore now and once more when queued.
        tree_->verticalScrollBar()->setValue( keptScroll_ );
        QTimer::singleShot( 0, tree_.data(), [ tree = tree_, scroll = keptScroll_ ] {
            if ( tree ) {
                tree->verticalScrollBar()->setValue( scroll );
            }
        } );
    }
    if ( reportRemoved && restored < keptSelected_.size() ) {
        reportSelection(); // the plugin removed selected nodes
    }
    keptExpanded_.clear();
    keptSelected_.clear();
    keptCurrent_.clear();
}

void PluginTreeBehaviour::applyExpandRequests()
{
    if ( !tree_ ) {
        return;
    }
    // Nodes whose children the plugin set no longer wait for tree/children.
    for ( const auto& id : QStringList( loading_.begin(), loading_.end() ) ) {
        if ( !model_->isLoading( id ) ) {
            loading_.remove( id );
            if ( hooks_.cancelChildren ) {
                hooks_.cancelChildren( id );
            }
        }
    }
    const auto requests = model_->takeExpandRequests();
    for ( auto it = requests.cbegin(); it != requests.cend(); ++it ) {
        const auto index = model_->indexOf( it.key() );
        if ( index.isValid() && tree_->isExpanded( index ) != it.value() ) {
            tree_->setExpanded( index, it.value() );
        }
    }
}

void PluginTreeBehaviour::ensureVisible( const QString& nodeId )
{
    QStringList ancestors;
    for ( auto parent = model_->parentIdOf( nodeId ); !parent.isEmpty();
          parent = model_->parentIdOf( parent ) ) {
        ancestors.prepend( parent );
    }
    for ( const auto& id : std::as_const( ancestors ) ) {
        const auto index = model_->indexOf( id );
        if ( !tree_->isExpanded( index ) ) {
            tree_->setExpanded( index, true );
        }
    }
}

void PluginTreeBehaviour::select( const QJsonValue& ids, const QJsonValue& current,
                                  const QJsonValue& scrollTo )
{
    if ( !tree_ ) {
        return;
    }
    auto* selection = tree_->selectionModel();
    const bool selectable = tree_->selectionMode() != QAbstractItemView::NoSelection;
    if ( ids.isArray() && selectable ) {
        // The plugin sets the selection: a press before it starts no drag, and its change (held,
        // or waiting for the patch to end) is not reported: the plugin's selection is never echoed.
        endPress();
        reportPending_ = false;
        QItemSelection chosen;
        int used = 0;
        for ( const auto& id : ids.toArray() ) {
            if ( used++ >= pluginview::MaxTableIds ) {
                break;
            }
            const auto index = model_->indexOf( id.toString() );
            if ( index.isValid() ) {
                chosen.select( index, index );
            }
            if ( tree_->selectionMode() == QAbstractItemView::SingleSelection && !chosen.isEmpty() ) {
                break; // one node in a single selection tree
            }
        }
        selection->select( chosen, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
    }
    if ( current.isString() && model_->contains( current.toString() ) ) {
        ensureVisible( current.toString() );
        if ( selectable ) {
            selection->setCurrentIndex( model_->indexOf( current.toString() ),
                                        QItemSelectionModel::NoUpdate );
        }
    }
    if ( scrollTo.isString() && model_->contains( scrollTo.toString() ) ) {
        ensureVisible( scrollTo.toString() );
        tree_->scrollTo( model_->indexOf( scrollTo.toString() ) );
    }
}

void PluginTreeBehaviour::expandNodes( const QJsonArray& ids, bool expand )
{
    if ( !tree_ ) {
        return;
    }
    for ( const auto& id : ids ) {
        const auto index = model_->indexOf( id.toString() );
        if ( index.isValid() && tree_->isExpanded( index ) != expand ) {
            tree_->setExpanded( index, expand ); // a lazy node loads (expandedChanged)
        }
    }
}

QString PluginTreeBehaviour::currentNodeId() const
{
    return tree_ ? model_->idOf( tree_->currentIndex() ) : QString{};
}

QStringList PluginTreeBehaviour::selectedNodeIds( int maximum, int* count ) const
{
    QStringList ids;
    if ( !tree_ ) {
        return ids;
    }
    auto rows = tree_->selectionModel()->selectedRows();
    // Tree order: compare the rows of each level from the top.
    const auto pathOf = []( QModelIndex index ) {
        QVector<int> path;
        for ( ; index.isValid(); index = index.parent() ) {
            path.prepend( index.row() );
        }
        return path;
    };
    std::vector<std::pair<QVector<int>, QString>> ordered;
    ordered.reserve( static_cast<size_t>( rows.size() ) );
    for ( const auto& index : std::as_const( rows ) ) {
        const auto id = model_->idOf( index );
        if ( !id.isEmpty() ) {
            ordered.emplace_back( pathOf( index ), id );
        }
    }
    std::sort( ordered.begin(), ordered.end() );
    if ( count != nullptr ) {
        *count = static_cast<int>( ordered.size() );
    }
    for ( const auto& entry : ordered ) {
        if ( ids.size() >= maximum ) {
            break;
        }
        ids.append( entry.second );
    }
    return ids;
}

QJsonObject PluginTreeBehaviour::menuTarget( const QString& nodeId ) const
{
    int count = 0;
    const auto ids = pluginview::boundedIds( selectedNodeIds( pluginview::MaxTableIds, &count ) );
    return { { "node_id", nodeId.isEmpty() ? QJsonValue() : QJsonValue( nodeId ) },
             { "selected_ids", QJsonArray::fromStringList( ids ) },
             { "selected_count", count } };
}

QMenu* PluginTreeBehaviour::buildMenu( const QPoint& position )
{
    if ( !tree_ ) {
        return nullptr;
    }
    const auto index = tree_->indexAt( position );
    const auto nodeId = model_->idOf( index );
    if ( !nodeId.isEmpty() && tree_->selectionMode() != QAbstractItemView::NoSelection ) {
        // A right-click on a node makes it current, and selects it when it was not selected.
        // The current node is set first: the `selected` event the selection sends carries it.
        auto* selection = tree_->selectionModel();
        const auto first = index.sibling( index.row(), 0 );
        selection->setCurrentIndex( first, QItemSelectionModel::NoUpdate );
        if ( !selection->isRowSelected( first.row(), first.parent() ) ) {
            selection->select( first, QItemSelectionModel::ClearAndSelect
                                          | QItemSelectionModel::Rows );
        }
    }
    auto* menu = new QMenu( tree_ );
    menu->setObjectName( tree_->objectName() + QStringLiteral( "_menu" ) );
    menu->setToolTipsVisible( true );
    if ( options_.copy ) {
        auto* copy = menu->addAction( tr( "Copy" ) );
        copy->setObjectName( tree_->objectName() + QStringLiteral( "_copy" ) );
        copy->setShortcut( QKeySequence::Copy );
        copy->setEnabled( !selectedNodeIds( 1 ).isEmpty() || !currentNodeId().isEmpty() );
        connect( copy, &QAction::triggered, this,
                 [ this ] { QApplication::clipboard()->setText( copyText( false ) ); } );
        auto* copyAll = menu->addAction( tr( "Copy All" ) );
        copyAll->setObjectName( tree_->objectName() + QStringLiteral( "_copy_all" ) );
        copyAll->setEnabled( model_->nodeCount() > 0 );
        connect( copyAll, &QAction::triggered, this,
                 [ this ] { QApplication::clipboard()->setText( copyText( true ) ); } );
    }
    if ( options_.exportCsv ) {
        auto* exporting = menu->addAction( tr( "Export CSV…" ) );
        exporting->setObjectName( tree_->objectName() + QStringLiteral( "_export" ) );
        connect( exporting, &QAction::triggered, this, [ this ] {
            if ( !hooks_.saveFile ) {
                return;
            }
            // The dialog runs its own event loop: the tree may be gone when it returns.
            const QPointer<PluginTreeBehaviour> self( this );
            const auto path = hooks_.saveFile( tr( "Export CSV" ), tr( "CSV files (*.csv)" ),
                                               QStringLiteral( "tree.csv" ) );
            if ( !self || !tree_ ) {
                return;
            }
            QString error;
            if ( !path.isEmpty() && !exportCsv( path, &error ) ) {
                LOG_WARNING << "Plugin tree export failed: " << error.toStdString();
            }
        } );
    }
    if ( !options_.contextMenu.isEmpty() && hooks_.elementMenu ) {
        hooks_.elementMenu( menu, options_.contextMenu, menuTarget( nodeId ) );
    }
    if ( menu->isEmpty() ) {
        delete menu;
        return nullptr;
    }
    return menu;
}

QString PluginTreeBehaviour::copyText( bool all ) const
{
    if ( !tree_ ) {
        return {};
    }
    const auto columns = shownColumns();
    // At most MaxCopyChars, whole lines only (as tables): 50,000 nodes can be hundreds of
    // megabytes of text.
    QString text;
    bool cut = false;
    const auto line = [ & ]( const std::function<QString( int column )>& field ) {
        if ( cut ) {
            return false;
        }
        QString row;
        for ( qsizetype at = 0; at < columns.size(); ++at ) {
            if ( at > 0 ) {
                row += QLatin1Char( '\t' );
            }
            row += pluginview::tsvField( field( columns.at( at ) ) );
        }
        if ( text.size() + row.size() + ( text.isEmpty() ? 0 : 1 ) > pluginview::MaxCopyChars ) {
            cut = true;
            LOG_WARNING << "Plugin tree copy cut at " << pluginview::MaxCopyChars
                        << " characters (whole lines)";
            return false;
        }
        if ( !text.isEmpty() ) {
            text += QLatin1Char( '\n' );
        }
        text += row;
        return true;
    };
    const auto cellOf = []( const PluginTreeModel::Node& node, int column ) {
        return column < node.cells.size() ? node.cells.at( column ) : QString{};
    };
    if ( all ) {
        if ( model_->hasColumns() ) {
            line( [ & ]( int column ) {
                return model_->headerData( column, Qt::Horizontal, Qt::DisplayRole ).toString();
            } );
        }
        // Every loaded node in tree order, the first shown column indented by its level.
        model_->forEach( [ & ]( const PluginTreeModel::Node& node, int depth ) {
            line( [ & ]( int column ) {
                const auto cell = cellOf( node, column );
                return column == columns.value( 0 ) ? QString( depth * 2, QLatin1Char( ' ' ) ) + cell
                                                    : cell;
            } );
        } );
        return text;
    }
    auto ids = selectedNodeIds( std::numeric_limits<int>::max() );
    if ( ids.isEmpty() && !currentNodeId().isEmpty() ) {
        ids.append( currentNodeId() );
    }
    for ( const auto& id : std::as_const( ids ) ) {
        const auto* node = model_->nodeAt( model_->indexOf( id ) );
        if ( node != nullptr && !line( [ & ]( int column ) { return cellOf( *node, column ); } ) ) {
            break;
        }
    }
    return text;
}

bool PluginTreeBehaviour::exportCsv( const QString& path, QString* error ) const
{
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        *error = file.errorString();
        return false;
    }
    // Written line by line, with the node's level first.
    const auto columns = shownColumns();
    const auto line = [ & ]( const QString& level, const std::function<QString( int column )>& field ) {
        QString text = pluginview::csvField( level );
        for ( const auto column : columns ) {
            text += QLatin1Char( ',' ) + pluginview::csvField( field( column ) );
        }
        text += QStringLiteral( "\r\n" );
        return file.write( text.toUtf8() ) >= 0;
    };
    bool written = line( QStringLiteral( "level" ), [ & ]( int column ) {
        const auto title = model_->headerData( column, Qt::Horizontal, Qt::DisplayRole ).toString();
        return title.isEmpty() ? QStringLiteral( "text" ) : title;
    } );
    model_->forEach( [ & ]( const PluginTreeModel::Node& node, int depth ) {
        if ( written ) {
            written = line( QString::number( depth ), [ & ]( int column ) {
                return column < node.cells.size() ? node.cells.at( column ) : QString{};
            } );
        }
    } );
    if ( !written || !file.commit() ) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool PluginTreeBehaviour::moveNodes( const QStringList& nodeIds, const QString& parent,
                                     const QString& before )
{
    if ( !options_.reorderable || ( hooks_.canAct && !hooks_.canAct() ) || !hooks_.emitEvent
         || nodeIds.isEmpty() || nodeIds.contains( before ) ) {
        return false;
    }
    if ( !parent.isEmpty() && !model_->contains( parent ) ) {
        return false;
    }
    QStringList known;
    for ( const auto& id : nodeIds ) {
        if ( known.size() >= pluginview::MaxTableIds ) {
            break;
        }
        // A node never moves into itself or one of its descendants.
        if ( !parent.isEmpty() && model_->isWithin( parent, id ) ) {
            return false;
        }
        if ( model_->contains( id ) ) {
            known.append( id );
        }
    }
    // Bounded like element-menu targets: by count and by the size of their JSON.
    const auto ids = QJsonArray::fromStringList( pluginview::boundedIds( known ) );
    if ( ids.isEmpty() ) {
        return false;
    }
    const auto orNull = []( const QString& value ) {
        return value.isEmpty() ? QJsonValue() : QJsonValue( value );
    };
    // The host does not move the nodes: the plugin confirms with a patch.
    return hooks_.emitEvent( QStringLiteral( "moved" ),
                             QJsonObject{ { "node_ids", ids },
                                          { "parent", orNull( parent ) },
                                          { "before", orNull( before ) } } );
}

void PluginTreeBehaviour::requestCheck( const QString& nodeId, bool checked )
{
    QHash<QString, Qt::CheckState> previous;
    auto changed = model_->setChecked( { nodeId }, checked, &previous );
    if ( changed.isEmpty() || !hooks_.emitEvent ) {
        return;
    }
    if ( changed.size() > pluginview::MaxTableIds ) {
        changed = changed.mid( 0, pluginview::MaxTableIds );
    }
    const QJsonObject value{ { "node_id", nodeId },
                             { "checked", checked },
                             { "node_ids",
                               QJsonArray::fromStringList( pluginview::boundedIds( changed ) ) } };
    if ( !hooks_.emitEvent( QStringLiteral( "checked" ), value ) ) {
        model_->restoreChecks( previous ); // the plugin did not get it
    }
}

void PluginTreeBehaviour::activate( const QModelIndex& index )
{
    const auto nodeId = model_->idOf( index );
    if ( std::exchange( doubleClickOnCheck_, false ) || nodeId.isEmpty() || !hooks_.emitEvent
         || ( hooks_.canAct && !hooks_.canAct() ) ) {
        return;
    }
    // An editable cell opens its editor instead.
    if ( model_->flags( index ).testFlag( Qt::ItemIsEditable ) ) {
        return;
    }
    const auto columnId = model_->columnId( index.column() );
    hooks_.emitEvent( QStringLiteral( "activated" ),
                      QJsonObject{ { "node_id", nodeId },
                                   { "column_id", columnId.isEmpty() ? QJsonValue()
                                                                    : QJsonValue( columnId ) } } );
}

bool PluginTreeBehaviour::onCheckIndicator( const QModelIndex& index, const QPoint& position ) const
{
    if ( !tree_ || !index.isValid() || index.column() != 0
         || !model_->flags( index ).testFlag( Qt::ItemIsUserCheckable ) ) {
        return false;
    }
    // The check box's place as the item delegate lays it out: first in the cell.
    QStyleOptionViewItem option;
    option.initFrom( tree_ );
    option.rect = tree_->visualRect( index );
    option.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
    option.text = index.data( Qt::DisplayRole ).toString();
    if ( !index.data( Qt::DecorationRole ).isNull() ) {
        option.features |= QStyleOptionViewItem::HasDecoration;
        option.decorationSize = tree_->iconSize().isValid()
                                    ? tree_->iconSize()
                                    : QSize( tree_->style()->pixelMetric( QStyle::PM_SmallIconSize ),
                                             tree_->style()->pixelMetric( QStyle::PM_SmallIconSize ) );
    }
    return tree_->style()
        ->subElementRect( QStyle::SE_ItemViewItemCheckIndicator, &option, tree_ )
        .contains( position );
}

void PluginTreeBehaviour::expandedChanged( const QModelIndex& index, bool expanded )
{
    const auto nodeId = model_->idOf( index );
    if ( nodeId.isEmpty() ) {
        return;
    }
    if ( expanded && model_->wantsChildren( index ) ) {
        startLoad( nodeId );
    }
    // Collapsed while it loads: the query is cancelled (query/cancel, its slot goes to the next
    // one), a late answer is dropped, and the next expand asks again.
    if ( !expanded && loading_.remove( nodeId ) ) {
        model_->cancelLoading( nodeId );
        if ( hooks_.cancelChildren ) {
            hooks_.cancelChildren( nodeId );
        }
    }
    // Only the user's expansions: patches and restoring the view run while the user cannot act,
    // and the nodes a tree/children answer expands are expanded by the plugin too.
    if ( options_.expandEvents && hooks_.emitEvent && !applyingAnswer_
         && ( !hooks_.canAct || hooks_.canAct() ) ) {
        hooks_.emitEvent( expanded ? QStringLiteral( "expanded" ) : QStringLiteral( "collapsed" ),
                          QJsonObject{ { "node_id", nodeId } } );
    }
}

void PluginTreeBehaviour::startLoad( const QString& nodeId )
{
    if ( loading_.contains( nodeId ) ) {
        return;
    }
    model_->setLoading( nodeId );
    loading_.insert( nodeId );
    if ( !hooks_.loadChildren || !hooks_.loadChildren( nodeId ) ) {
        loading_.remove( nodeId );
        model_->setLoadFailed( nodeId );
    }
}

void PluginTreeBehaviour::childrenAnswered( const QString& nodeId, bool answered,
                                            const QJsonArray& nodes )
{
    if ( !loading_.remove( nodeId ) || !model_->isLoading( nodeId ) ) {
        return; // removed, or the plugin set the children meanwhile
    }
    QString error;
    if ( !answered || !model_->fillChildren( nodeId, nodes, &error ) ) {
        if ( !error.isEmpty() ) {
            LOG_WARNING << "Plugin tree children rejected: " << error.toStdString();
        }
        model_->setLoadFailed( nodeId );
        return;
    }
    const QScopedValueRollback<bool> applying( applyingAnswer_, true );
    applyExpandRequests();
}

void PluginTreeBehaviour::failLoading()
{
    for ( const auto& id : QStringList( loading_.begin(), loading_.end() ) ) {
        model_->setLoadFailed( id );
    }
    loading_.clear();
}

QStringList PluginTreeBehaviour::draggedNodes() const
{
    if ( pressNode_.isEmpty() ) {
        return {};
    }
    if ( !pressWasSelected_ ) {
        return { pressNode_ };
    }
    // The selection as it was before the press (the press may have changed it: a toggle tree
    // deselects the pressed node), in tree order, and only the ids `moved` sends: the ranges are
    // turned into row paths, never into every id.
    const auto pathOf = []( QModelIndex index ) {
        QVector<int> path;
        for ( ; index.isValid(); index = index.parent() ) {
            path.prepend( index.row() );
        }
        return path;
    };
    std::vector<std::pair<QVector<int>, QModelIndex>> ordered;
    for ( const auto& range : pressSelection_ ) {
        for ( int row = range.top(); range.isValid() && row <= range.bottom(); ++row ) {
            const auto index = model_->index( row, 0, range.parent() );
            ordered.emplace_back( pathOf( index ), index );
        }
    }
    const auto byPath = []( const auto& left, const auto& right ) { return left.first < right.first; };
    std::sort( ordered.begin(), ordered.end(), byPath );
    ordered.erase( std::unique( ordered.begin(), ordered.end(),
                                []( const auto& left, const auto& right ) {
                                    return left.first == right.first;
                                } ),
                   ordered.end() );
    QStringList ids;
    for ( const auto& entry : ordered ) {
        if ( ids.size() >= pluginview::MaxTableIds ) {
            break;
        }
        if ( const auto id = model_->idOf( entry.second ); !id.isEmpty() ) {
            ids.append( id );
        }
    }
    return pluginview::boundedIds( ids );
}

void PluginTreeBehaviour::releaseSelection( bool report )
{
    if ( !std::exchange( holdSelection_, false ) || !report || !tree_ ) {
        return;
    }
    if ( pressChangedSelection() ) {
        reportSelection();
    }
}

void PluginTreeBehaviour::cancelPress()
{
    if ( pressNode_.isEmpty() && !holdSelection_ ) {
        return;
    }
    // Compared before the change, while the press's ranges still name the nodes they did: only a
    // change the user's press made is reported, never the one before the press.
    const bool changed = holdSelection_ && pressChangedSelection();
    endPress();
    if ( changed && !std::exchange( reportPending_, true ) ) {
        // A patch is still being applied: reported once it is done, unless it sets the selection
        // itself (select() drops the report) or the view sends `selected` anyway.
        QTimer::singleShot( 0, this, [ this ] {
            if ( reportPending_ ) {
                reportSelection();
            }
        } );
    }
}

void PluginTreeBehaviour::endPress()
{
    holdSelection_ = false;
    pressNode_.clear();
    pressSelection_.clear();
    pressWasSelected_ = false;
}

void PluginTreeBehaviour::reportSelection()
{
    reportPending_ = false;
    if ( hooks_.selectionChanged ) {
        hooks_.selectionChanged();
    }
}

bool PluginTreeBehaviour::pressChangedSelection() const
{
    if ( !tree_ ) {
        return false;
    }
    // The nodes of a selection (by their model pointers), in order: the press changed the
    // selection when they differ.
    const auto nodesOf = [ this ]( const QItemSelection& selection ) {
        std::vector<void*> nodes;
        for ( const auto& range : selection ) {
            for ( int row = range.top(); range.isValid() && row <= range.bottom(); ++row ) {
                nodes.push_back( model_->index( row, 0, range.parent() ).internalPointer() );
            }
        }
        std::sort( nodes.begin(), nodes.end() );
        nodes.erase( std::unique( nodes.begin(), nodes.end() ), nodes.end() );
        return nodes;
    };
    return nodesOf( tree_->selectionModel()->selection() ) != nodesOf( pressSelection_ );
}

void PluginTreeBehaviour::startDrag()
{
    const auto ids = draggedNodes();
    const bool wasSelected = std::exchange( pressWasSelected_, false );
    pressNode_.clear();
    if ( wasSelected ) {
        // The press changed the selection (a toggle tree deselected the node): the selection
        // before the press comes back, as Qt's own drags defer the change. It is still held, so
        // the plugin hears neither change.
        tree_->selectionModel()->select( pressSelection_, QItemSelectionModel::ClearAndSelect );
    }
    releaseSelection( true ); // the selection is the one before the press: nothing to report
    pressSelection_.clear();
    auto* mime = new QMimeData;
    mime->setData( NodesMimeType, QJsonDocument( QJsonArray::fromStringList( ids ) )
                                      .toJson( QJsonDocument::Compact ) );
    auto* drag = new QDrag( tree_->viewport() );
    drag->setMimeData( mime );
    drag->exec( Qt::MoveAction );
}

bool PluginTreeBehaviour::drop( const QPoint& position, const QByteArray& payload )
{
    QStringList ids;
    for ( const auto& id : QJsonDocument::fromJson( payload ).array() ) {
        ids.append( id.toString() );
    }
    // On the upper quarter of a node: before it; on the lower quarter: after it; in between:
    // into it as its last child. Below the last node: the end of the top level.
    QString parent;
    QString before;
    const auto index = tree_->indexAt( position );
    if ( index.isValid() ) {
        auto nodeId = model_->idOf( index );
        if ( nodeId.isEmpty() ) {
            return false; // a "Loading…" line
        }
        const auto rect = tree_->visualRect( index );
        const auto quarter = qMax( 1, rect.height() / 4 );
        if ( position.y() < rect.top() + quarter ) {
            parent = model_->parentIdOf( nodeId );
            before = nodeId;
        }
        else if ( position.y() > rect.bottom() - quarter ) {
            parent = model_->parentIdOf( nodeId );
            const auto next = index.sibling( index.row() + 1, 0 );
            before = model_->idOf( next );
        }
        else {
            parent = nodeId;
        }
    }
    return moveNodes( ids, parent, before );
}

bool PluginTreeBehaviour::eventFilter( QObject* watched, QEvent* event )
{
    if ( !tree_ ) {
        return false;
    }
    auto* viewport = tree_->viewport();
    if ( watched == tree_ && event->type() == QEvent::KeyPress ) {
        const auto* key = static_cast<QKeyEvent*>( event );
        if ( options_.copy && key->matches( QKeySequence::Copy ) ) {
            const auto text = copyText( false );
            if ( text.isEmpty() ) {
                return false;
            }
            QApplication::clipboard()->setText( text );
            return true;
        }
        if ( key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ) {
            doubleClickOnCheck_ = false;
            activate( tree_->currentIndex() );
        }
        return false;
    }
    if ( watched != viewport ) {
        return false;
    }
    switch ( event->type() ) {
    case QEvent::MouseButtonDblClick: {
        // A double-click on a check box checks it; only one beside it activates the node.
        const auto position = static_cast<QMouseEvent*>( event )->position().toPoint();
        doubleClickOnCheck_ = onCheckIndicator( tree_->indexAt( position ), position );
        return false;
    }
    case QEvent::MouseButtonPress: {
        const auto* mouse = static_cast<QMouseEvent*>( event );
        const auto position = mouse->position().toPoint();
        const auto index = tree_->indexAt( position );
        doubleClickOnCheck_ = false;
        releaseSelection( true ); // a release this behaviour never saw
        pressNode_.clear();
        pressSelection_.clear();
        pressWasSelected_ = false;
        if ( mouse->button() != Qt::LeftButton ) {
            return false;
        }
        const auto nodeId = model_->idOf( index );
        // Toggle selection: a Shift+click adds the shown nodes from the last clicked one.
        const auto anchor = model_->indexOf( selectionAnchor_ );
        if ( tree_->selectionMode() == QAbstractItemView::MultiSelection && !nodeId.isEmpty()
             && ( mouse->modifiers() & Qt::ShiftModifier ) && anchor.isValid() ) {
            const bool down = tree_->visualRect( anchor ).top() <= tree_->visualRect( index ).top();
            auto from = down ? anchor : index.sibling( index.row(), 0 );
            const auto to = down ? index.sibling( index.row(), 0 ) : anchor;
            QItemSelection range;
            for ( int guard = 0; from.isValid() && guard <= pluginview::MaxTableIds * 50; ++guard ) {
                if ( !model_->idOf( from ).isEmpty() ) {
                    range.select( from, from );
                }
                if ( from == to ) {
                    break;
                }
                from = tree_->indexBelow( from );
            }
            tree_->selectionModel()->setCurrentIndex( index, QItemSelectionModel::NoUpdate );
            tree_->selectionModel()->select( range, QItemSelectionModel::Select
                                                        | QItemSelectionModel::Rows );
            return true;
        }
        if ( !nodeId.isEmpty() ) {
            selectionAnchor_ = nodeId;
            if ( mouse->modifiers() == Qt::NoModifier && options_.reorderable
                 && ( !hooks_.canAct || hooks_.canAct() ) ) {
                pressPosition_ = position;
                pressNode_ = nodeId;
                // Before the view handles the press, which may change the selection. Ranges, not
                // ids: cheap for any selection; whether the pressed node was in it decides what
                // moves.
                pressSelection_ = tree_->selectionModel()->selection();
                pressWasSelected_ = tree_->selectionModel()->isRowSelected( index.row(),
                                                                            index.parent() );
                // The press may toggle this node off for a moment: the plugin hears nothing until
                // the release, and nothing at all when a drag puts the selection back.
                holdSelection_ = pressWasSelected_;
            }
        }
        return false;
    }
    case QEvent::MouseMove: {
        const auto* mouse = static_cast<QMouseEvent*>( event );
        if ( !pressNode_.isEmpty() && ( mouse->buttons() & Qt::LeftButton )
             && ( mouse->position().toPoint() - pressPosition_ ).manhattanLength()
                    >= QApplication::startDragDistance() ) {
            startDrag();
            return true;
        }
        return false;
    }
    case QEvent::MouseButtonRelease:
        // No drag started: a held selection change is reported once, and the snapshot goes. (A
        // change the view still makes on the release itself is no longer held.)
        releaseSelection( true );
        pressNode_.clear();
        pressSelection_.clear();
        pressWasSelected_ = false;
        return false;
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto* drag = static_cast<QDragMoveEvent*>( event );
        if ( drag->mimeData()->hasFormat( NodesMimeType ) && drag->source() == viewport
             && options_.reorderable ) {
            drag->acceptProposedAction();
        }
        else {
            drag->ignore();
        }
        return true;
    }
    case QEvent::Drop: {
        auto* dropped = static_cast<QDropEvent*>( event );
        if ( dropped->mimeData()->hasFormat( NodesMimeType ) && dropped->source() == viewport ) {
            drop( dropped->position().toPoint(), dropped->mimeData()->data( NodesMimeType ) );
            dropped->acceptProposedAction();
        }
        return true;
    }
    default:
        return false;
    }
}
