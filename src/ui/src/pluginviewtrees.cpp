// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The view's part of the E17e trees (docs/plugin-sdk-advanced.md section 28): building `tree`
// and `file_tree` elements, their properties, patches, selection events, tooltips and the
// tree/children queries of the view.
#include "pluginview.h"

#include "log.h"
#include "pluginfeatures.h"
#include "pluginfiletree.h"
#include "plugintablemodel.h"
#include "plugintree.h"
#include "pluginviewnode.h"
#include <QDir>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QScopedValueRollback>
#include <QTreeView>

#include <algorithm>

using namespace pluginview;

namespace {
const QStringList SelectionModes{ "none", "single", "multi" };
const QStringList FileSelectKinds{ "files", "dirs", "both" };
const QStringList FileColumnNames{ "name", "size", "type", "modified" };

// The strings of a list, at most `maximum`; anything else is left out.
QStringList stringList( const QJsonValue& value, int maximum )
{
    QStringList list;
    for ( const auto& item : value.toArray() ) {
        if ( list.size() >= maximum ) {
            break;
        }
        if ( item.isString() ) {
            list.append( item.toString().left( PluginView::MaxFieldValueChars ) );
        }
    }
    return list;
}

QJsonValue orNull( const QString& value )
{
    return value.isEmpty() ? QJsonValue() : QJsonValue( value );
}
} // namespace

QString PluginView::exportPath( const QPointer<PluginView>& guard, const QString& elementId,
                                const QString& title, const QString& filter,
                                const QString& suggested )
{
    if ( !guard || !guard->dialogs_.saveFile ) {
        return {};
    }
    const auto name = guard->nameFor( elementId ) + QStringLiteral( "_export_dialog" );
    guard->activeDialogs_.insert( name );
    const auto path = guard->dialogs_.saveFile( guard->dialogParent(), name, title, filter, suggested );
    if ( guard ) {
        guard->activeDialogs_.remove( name );
    }
    return path;
}

QWidget* PluginView::buildTree( Node& node, QWidget* parent )
{
    const auto id = node.id;
    auto* tree = new QTreeView( parent );
    auto* model = new PluginTreeModel( tree );
    node.treeModel = model;
    tree->setModel( model );
    tree->setSelectionBehavior( QAbstractItemView::SelectRows );
    tree->setSelectionMode( QAbstractItemView::NoSelection );
    // Only cells whose flags grant editing open an editor (an `editable` column and a node editor).
    tree->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed );
    tree->setAllColumnsShowFocus( true );
    tree->header()->setStretchLastSection( true );
    tree->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    const QPointer<PluginView> guard( this );
    const auto icons = [ guard ]( const QJsonValue& reference ) {
        return guard ? guard->iconFor( reference ) : QIcon{};
    };
    auto* delegate = new PluginCellDelegate( tree );
    delegate->iconFor = icons;
    delegate->onUnknownKind = [ guard, id ]( const QString& kind ) {
        if ( guard ) {
            guard->warnOnce( "editor-kind:" + kind.left( 64 ),
                             "unknown cell editor kind treated as text in tree " + id + ": "
                                 + kind.left( 64 ) );
        }
    };
    tree->setItemDelegate( delegate );
    model->iconFor = icons;
    model->warn = [ guard, id ]( const QString& key, const QString& message ) {
        if ( guard ) {
            guard->warnOnce( "tree:" + id + ':' + key, "tree " + id + ": " + message );
        }
    };
    model->canEdit = [ guard, id, model ] {
        // This tree in a live view, outside a plugin update (as table cells).
        if ( !guard ) {
            return false;
        }
        const auto found = guard->nodes_.value( id );
        return found && found->treeModel == model && !guard->applying_ && guard->isEnabled();
    };
    model->onEdited = [ guard, id ]( const QString& nodeId, const QString& columnId,
                                     const QJsonValue& value, const QString& previous ) {
        if ( !guard ) {
            return false;
        }
        if ( const auto found = guard->nodes_.value( id ) ) {
            found->rowsDirty = true;
        }
        return guard->emitEvent( id, "edited", value,
                                 { { "node_id", nodeId },
                                   { "column_id", orNull( columnId ) },
                                   { "previous", previous } } );
    };
    connect( tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, [ this, id ] {
        // Not while a press that may start a drag holds the selection (the behaviour reports it).
        const auto found = nodes_.value( id );
        if ( !applying_ && !( found && found->tree && found->tree->holdsSelection() ) ) {
            emitTreeSelection( id );
        }
    } );
    PluginTreeBehaviour::Hooks hooks;
    // Events come only from the tree the view still shows for this id (a replaced one lives on
    // until it is deleted, and a report it deferred must not speak for its successor).
    const auto current = [ guard, id, model ] {
        const auto found = guard ? guard->nodes_.value( id ) : nullptr;
        return found && found->treeModel == model;
    };
    hooks.emitEvent = [ guard, id, current ]( const QString& type, const QJsonValue& value ) {
        return current() && guard->emitEvent( id, type, value );
    };
    hooks.canAct = [ guard, id, model ] {
        if ( !guard ) {
            return false;
        }
        const auto found = guard->nodes_.value( id );
        return found && found->treeModel == model && !guard->applying_ && guard->isEnabled();
    };
    hooks.elementMenu = [ guard, id ]( QMenu* menu, const QString& menuId,
                                       const QJsonObject& target ) {
        if ( guard && guard->queries_.elementMenu ) {
            guard->queries_.elementMenu( menu, id, menuId, target );
        }
    };
    hooks.saveFile = [ guard, id ]( const QString& title, const QString& filter,
                                    const QString& suggested ) {
        return exportPath( guard, id, title, filter, suggested );
    };
    hooks.loadChildren = [ guard, id ]( const QString& nodeId ) {
        return guard && guard->requestChildren( id, nodeId );
    };
    hooks.cancelChildren = [ guard, id ]( const QString& nodeId ) {
        if ( guard ) {
            guard->cancelChildren( id, nodeId );
        }
    };
    hooks.selectionChanged = [ guard, id, current ] {
        if ( current() ) {
            // The plugin removed selected nodes: tell it what is still selected.
            QScopedValueRollback<bool> rollback( guard->applying_, false );
            guard->emitTreeSelection( id );
        }
    };
    node.widget = tree;
    node.tree = new PluginTreeBehaviour( tree, model, std::move( hooks ) );
    return tree;
}

QWidget* PluginView::buildFileTree( Node& node, QWidget* parent )
{
    const auto id = node.id;
    auto* tree = new QTreeView( parent );
    tree->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    const QPointer<PluginView> guard( this );
    PluginFileTreeBehaviour::Hooks hooks;
    hooks.emitEvent = [ guard, id, tree ]( const QString& type, const QJsonValue& value,
                                           const QJsonObject& extra ) {
        // Only the file tree the view still shows for this id (a replaced one lives on until it
        // is deleted).
        const auto found = guard ? guard->nodes_.value( id ) : nullptr;
        return found && found->widget.data() == tree && guard->emitEvent( id, type, value, extra );
    };
    hooks.canAct = [ guard, id, tree ] {
        if ( !guard ) {
            return false;
        }
        const auto found = guard->nodes_.value( id );
        return found && found->widget.data() == tree && !guard->applying_ && guard->isEnabled();
    };
    hooks.elementMenu = [ guard, id ]( QMenu* menu, const QString& menuId,
                                       const QJsonObject& target ) {
        if ( guard && guard->queries_.elementMenu ) {
            guard->queries_.elementMenu( menu, id, menuId, target );
        }
    };
    hooks.warn = [ guard, id ]( const QString& key, const QString& message ) {
        if ( guard ) {
            guard->warnOnce( "file_tree:" + id + ':' + key, "file tree " + id + ": " + message );
        }
    };
    node.widget = tree;
    node.fileTree = new PluginFileTreeBehaviour( tree, std::move( hooks ) );
    return tree;
}

void PluginView::applyTree( Node& node, const QJsonObject& props, const QJsonObject& previous,
                            bool fromRoot )
{
    auto* tree = qobject_cast<QTreeView*>( node.widget.data() );
    auto* model = node.treeModel;
    if ( tree == nullptr || model == nullptr || node.tree == nullptr ) {
        return;
    }
    const auto unchanged = [ & ]( const char* key ) {
        return fromRoot && previous.value( QLatin1String( key ) ) == props.value( QLatin1String( key ) );
    };
    // Columns and nodes may reset the model: expansion and selection are kept by id.
    bool remembered = false;
    const auto remember = [ & ] {
        if ( !std::exchange( remembered, true ) ) {
            node.tree->rememberView();
        }
    };
    if ( props.contains( "columns" ) && !unchanged( "columns" ) ) {
        remember();
        const auto columns = props.value( "columns" ).toArray();
        if ( !model->setColumns( columns ) ) {
            warnOnce( "columns:" + node.id, "tree columns rejected for " + node.id );
        }
        else {
            // Nodes keep only the cells of the columns there are: nodes sent again after the
            // columns changed are parsed again, even when they are the same as last time.
            node.rowsDirty = true;
            for ( int column = 0; column < columns.size(); ++column ) {
                const auto info = model->column( column );
                const auto width = columns.at( column ).toObject().value( "width" ).toInt();
                if ( width > 0 || info.minWidth > 0 ) {
                    tree->header()->resizeSection( column, qBound( info.minWidth, width, 2000 ) );
                }
                tree->setColumnHidden( column, info.hidden );
            }
        }
    }
    // Options before the nodes: they decide how nodes show (check boxes, lazy children).
    if ( props.contains( "checkboxes" ) ) {
        model->setCheckboxes( node.props.value( "checkboxes" ).toBool() );
    }
    if ( props.contains( "auto_tristate" ) ) {
        model->setAutoTristate( node.props.value( "auto_tristate" ).toBool() );
    }
    if ( props.contains( "lazy" ) ) {
        model->setLazy( node.props.value( "lazy" ).toBool() );
    }
    // Unchanged nodes of a same-shaped root are skipped, so the expansion and scroll stay put.
    if ( props.contains( "nodes" )
         && !( fromRoot && !node.rowsDirty && props.value( "nodes" ) == node.lastRows ) ) {
        remember();
        QString error;
        if ( model->replace( props.value( "nodes" ).toArray(), {}, &error ) ) {
            node.lastRows = props.value( "nodes" );
            node.rowsDirty = false;
        }
        else {
            warnOnce( "nodes:" + node.id, "tree " + node.id + ": " + error );
        }
    }
    if ( props.contains( "selection" ) || props.contains( "toggle_selection" ) ) {
        auto mode = node.props.value( "selection" ).toString( "none" );
        if ( !SelectionModes.contains( mode ) ) {
            warnOnce( "selection:" + node.id, "unknown tree selection treated as none: " + mode.left( 32 ) );
            mode = "none";
        }
        const bool toggle = node.props.value( "toggle_selection" ).toBool();
        tree->setSelectionMode( mode == "single"  ? QAbstractItemView::SingleSelection
                                : mode == "multi" ? ( toggle ? QAbstractItemView::MultiSelection
                                                             : QAbstractItemView::ExtendedSelection )
                                                  : QAbstractItemView::NoSelection );
    }
    PluginTreeBehaviour::Options options;
    options.header = node.props.value( "header" ).toBool( true );
    options.toggleSelection = node.props.value( "toggle_selection" ).toBool();
    options.contextMenu = text( node.props, "context_menu", 128 );
    options.copy = node.props.value( "copy" ).toBool( true );
    options.exportCsv = node.props.value( "export" ).toBool();
    options.reorderable = node.props.value( "reorderable" ).toBool();
    options.expandEvents = node.props.value( "expand_events" ).toBool();
    node.tree->setOptions( options );
    if ( remembered ) {
        node.tree->restoreView();
    }
    node.tree->applyExpandRequests();
}

void PluginView::applyFileTree( Node& node, const QJsonObject& props, const QJsonObject& previous,
                                bool fromRoot )
{
    Q_UNUSED( previous );
    if ( node.fileTree == nullptr ) {
        return;
    }
    // The whole option set from the element's properties; the behaviour applies what changed.
    const auto& all = node.props;
    PluginFileTreeBehaviour::Options options;
    options.roots = stringList( all.value( "roots" ), cilogg::plugins::MaxFileTreeRoots * 4 );
    options.filters = stringList( all.value( "filters" ), 64 );
    options.hideFiltered = all.value( "hide_filtered" ).toBool( true );
    options.showFiles = all.value( "show_files" ).toBool( true );
    options.showHidden = all.value( "show_hidden" ).toBool();
    if ( all.value( "columns" ).isArray() ) {
        options.columns.clear();
        for ( const auto& name : stringList( all.value( "columns" ), 8 ) ) {
            if ( FileColumnNames.contains( name ) ) {
                options.columns.append( name );
            }
            else {
                warnOnce( "file-column:" + name.left( 32 ),
                          "unknown file tree column ignored: " + name.left( 32 ) );
            }
        }
    }
    options.selection = all.value( "selection" ).toString( "single" );
    if ( !SelectionModes.contains( options.selection ) ) {
        warnOnce( "selection:" + node.id,
                  "unknown file tree selection treated as single: " + options.selection.left( 32 ) );
        options.selection = "single";
    }
    options.toggleSelection = all.value( "toggle_selection" ).toBool();
    options.select = all.value( "select" ).toString( "both" );
    if ( !FileSelectKinds.contains( options.select ) ) {
        warnOnce( "select:" + node.id,
                  "unknown file tree select treated as both: " + options.select.left( 32 ) );
        options.select = "both";
    }
    options.checkboxes = all.value( "checkboxes" ).toBool();
    options.contextMenu = text( all, "context_menu", 128 );
    options.copy = all.value( "copy" ).toBool( true );
    node.fileTree->setOptions( options );
    // The value after the roots it names; a same-shaped root re-sending it changes nothing.
    if ( props.contains( "value" ) && ( !fromRoot || props.value( "value" ) != node.pluginValue ) ) {
        node.pluginValue = props.value( "value" );
        node.fileTree->setValue( stringList( node.pluginValue, MaxSelectedIds ) );
    }
}

void PluginView::applyTreeOperation( Node& node, const QJsonObject& entry )
{
    if ( node.fileTree ) {
        // A file tree's own patch key; its value is a `set` property.
        if ( entry.value( "expand" ).isArray() ) {
            node.fileTree->expandPaths( stringList( entry.value( "expand" ), MaxSelectedIds ) );
        }
        return;
    }
    auto* model = node.treeModel;
    if ( model == nullptr || node.tree == nullptr ) {
        return;
    }
    const bool structural = entry.contains( "replace_nodes" ) || entry.contains( "update_nodes" )
                            || entry.contains( "insert_nodes" ) || entry.contains( "remove_nodes" );
    if ( structural ) {
        // Expansion and scroll are kept by id only when rows are reset or children replaced;
        // the view keeps them itself across row insertions and removals.
        const auto updatesChildren = [ & ] {
            for ( const auto& value : entry.value( "update_nodes" ).toArray() ) {
                if ( value.toObject().value( "children" ).isArray() ) {
                    return true;
                }
            }
            return false;
        };
        node.tree->rememberView(
            entry.contains( "replace_nodes" ) || updatesChildren()
            || entry.value( "remove_nodes" ).toArray().size()
                   > pluginview::PluginTreeModel::LargeRemoval );
    }
    const auto parent = entry.value( "parent" ).toString();
    QStringList changed; // their cached tooltips are stale
    QString error;
    if ( entry.contains( "replace_nodes" ) ) {
        if ( !model->replace( entry.value( "replace_nodes" ).toArray(), parent, &error, &changed ) ) {
            warnOnce( "replace_nodes:" + node.id + ':' + error.left( 64 ), "tree " + node.id + ": " + error );
        }
        else if ( parent.isEmpty() ) {
            node.lastRows = entry.value( "replace_nodes" );
            node.rowsDirty = false;
        }
        else {
            node.rowsDirty = true;
        }
    }
    if ( entry.contains( "update_nodes" ) ) {
        node.rowsDirty = true;
        if ( !model->update( entry.value( "update_nodes" ).toArray(), &error, &changed ) ) {
            warnOnce( "update_nodes:" + node.id + ':' + error.left( 64 ), "tree " + node.id + ": " + error );
        }
    }
    if ( entry.contains( "insert_nodes" ) ) {
        node.rowsDirty = true;
        if ( !model->insert( entry.value( "insert_nodes" ).toArray(), parent,
                             entry.value( "before" ).toString(), &error ) ) {
            warnOnce( "insert_nodes:" + node.id + ':' + error.left( 64 ), "tree " + node.id + ": " + error );
        }
    }
    if ( entry.contains( "remove_nodes" ) ) {
        node.rowsDirty = true;
        changed.append( model->remove( entry.value( "remove_nodes" ).toArray() ) );
    }
    if ( !changed.isEmpty() ) {
        tooltips_->invalidateNodes( node.id, changed );
    }
    if ( structural ) {
        // An entry that sets the selection itself hears nothing about removed selected nodes
        // (a selection patch is never echoed, nor anything in between).
        node.tree->restoreView( !entry.contains( "select" ) );
    }
    node.tree->applyExpandRequests();
    if ( entry.value( "expand" ).isArray() ) {
        node.tree->expandNodes( entry.value( "expand" ).toArray(), true );
    }
    if ( entry.value( "collapse" ).isArray() ) {
        node.tree->expandNodes( entry.value( "collapse" ).toArray(), false );
    }
    // The selection patch, after the node changes of the same entry; never echoed as `selected`.
    if ( entry.contains( "select" ) || entry.contains( "current" ) || entry.contains( "scroll_to" ) ) {
        node.tree->select( entry.value( "select" ), entry.value( "current" ),
                           entry.value( "scroll_to" ) );
    }
}

void PluginView::emitTreeSelection( const QString& id )
{
    const auto node = nodes_.value( id );
    if ( !node || !node->tree ) {
        return;
    }
    // Bounded like table selections: at most MaxSelectedIds ids, and MaxIdBytes of them as
    // encoded JSON (a control character takes six bytes there).
    int count = 0;
    const auto ids = QJsonArray::fromStringList(
        boundedIds( node->tree->selectedNodeIds( MaxSelectedIds, &count ) ) );
    QJsonObject extra{ { "selected_count", count },
                       { "current", orNull( node->tree->currentNodeId() ) } };
    if ( ids.size() < count ) {
        extra.insert( "truncated", true );
    }
    node->tree->selectionReported(); // a report waiting for a patch to end would repeat it
    emitEvent( id, "selected", ids, extra );
}

PluginTooltips::Target PluginView::treeTooltipTarget( const Node& node,
                                                      PluginTooltips::Target target,
                                                      const QString& nodeId,
                                                      const QString& columnId,
                                                      const QModelIndex& pointed ) const
{
    if ( node.fileTree ) {
        // A path in the roots, its links resolved: its static tooltip is the path (section 28.2).
        // A missing path, or one through a link that leads out of its root, has none. The item
        // under the pointer is checked as it is, without looking its path up again.
        const auto path = PluginFileRootsModel::normalize( nodeId );
        const auto* files = node.fileTree->model();
        const bool reached
            = pointed.isValid() ? files->reachesItem( pointed ) : files->reaches( path );
        if ( path.isEmpty() || !reached ) {
            target.provider = false;
            return target;
        }
        target.nodeId = path;
        target.staticTip = QDir::toNativeSeparators( path );
        return target;
    }
    const auto* model = node.treeModel;
    const auto column = columnId.isEmpty() ? 0 : model->columnOf( columnId );
    const auto index = model->indexOf( nodeId, qMax( 0, column ) );
    const auto* item = model->nodeAt( index );
    if ( column < 0 || item == nullptr ) {
        target.provider = false;
        return target;
    }
    const auto& info = model->column( column );
    const auto* style = column < item->styles.size() ? &item->styles.at( column ) : nullptr;
    const auto cellKey = style ? style->tooltipKey : QString{};
    target.nodeId = nodeId;
    target.columnId = info.id;
    target.provider = target.provider || info.tooltipProvider;
    // The cache key holds every key that applies; the query gets the most specific one: the
    // cell's, the node's, the column's, the tree's.
    const auto elementKey = target.key;
    target.key = joinedKey( { elementKey, item->tooltipKey, info.tooltipKey, cellKey } );
    for ( const auto& candidate : { cellKey, item->tooltipKey, info.tooltipKey, elementKey } ) {
        if ( !candidate.isEmpty() ) {
            target.tooltipKey = candidate;
            break;
        }
    }
    target.staticTip = index.data( Qt::ToolTipRole ).toString();
    if ( const auto* tree = qobject_cast<const QTreeView*>( node.widget.data() ) ) {
        target.rect = tree->visualRect( index );
    }
    return target;
}

void PluginView::watchTreeTooltips( Node& node )
{
    auto* tree = qobject_cast<QTreeView*>( node.widget.data() );
    if ( tree == nullptr ) {
        return;
    }
    const auto id = node.id;
    tooltips_->watch( tree->viewport(), [ this, id, tree ]( const QPoint& position ) {
        const auto found = nodes_.value( id );
        const auto index = tree->indexAt( position );
        if ( !found || !index.isValid() ) {
            return PluginTooltips::Target{};
        }
        if ( found->fileTree ) {
            auto target = tooltipTarget( id, {}, {}, found->fileTree->pathAt( index ),
                                         index.sibling( index.row(), 0 ) );
            target.rect = tree->visualRect( index );
            return target;
        }
        const auto nodeId = found->treeModel->idOf( index );
        if ( nodeId.isEmpty() ) {
            return PluginTooltips::Target{}; // a "Loading…" line
        }
        return tooltipTarget( id, {}, found->treeModel->columnId( index.column() ), nodeId );
    } );
}

// tree/children: at most MaxChildrenQueries outstanding per view and MaxWaitingChildrenQueries
// waiting (docs/plugin-sdk-advanced.md section 28.1).

namespace {
// The id of a query that is being sent: it counts as sent and is never started twice.
const QString SendingQuery = QStringLiteral( "\x01sending" );
} // namespace

bool PluginView::startChildrenQuery( quint64 token )
{
    const auto find = [ this, token ] {
        return std::find_if( childrenQueries_.begin(), childrenQueries_.end(),
                             [ token ]( const ChildrenQuery& query ) { return query.token == token; } );
    };
    auto found = find();
    if ( found == childrenQueries_.end() ) {
        return false;
    }
    found->queryId = SendingQuery;
    const auto elementId = found->elementId;
    const auto nodeId = found->nodeId;
    const QPointer<PluginView> guard( this );
    const auto id = queries_.treeChildren(
        { { "element_id", elementId }, { "node_id", nodeId } },
        [ guard, token ]( bool answered, const QJsonArray& nodes ) {
            if ( guard ) {
                guard->childrenFinished( token, answered, nodes );
            }
        } );
    // The call may have answered the query, or changed the list: look again.
    found = find();
    if ( found == childrenQueries_.end() ) {
        return true; // answered meanwhile
    }
    if ( id.isEmpty() ) {
        childrenQueries_.erase( found );
        return false;
    }
    found->queryId = id;
    return true;
}

bool PluginView::requestChildren( const QString& elementId, const QString& nodeId )
{
    if ( !queries_.treeChildren ) {
        return false;
    }
    int sent = 0;
    int waiting = 0;
    for ( const auto& query : std::as_const( childrenQueries_ ) ) {
        ++( query.queryId.isEmpty() ? waiting : sent );
    }
    if ( sent >= MaxChildrenQueries && waiting >= MaxWaitingChildrenQueries ) {
        LOG_DEBUG << "Plugin tree children refused: too many waiting element="
                  << elementId.toStdString();
        return false;
    }
    const auto token = ++nextChildrenToken_;
    childrenQueries_.append( { elementId, nodeId, {}, token } );
    // Not sent (the plugin stopped): the node shows "Could not load".
    return sent >= MaxChildrenQueries || startChildrenQuery( token );
}

void PluginView::childrenFinished( quint64 token, bool answered, const QJsonArray& nodes )
{
    const auto found
        = std::find_if( childrenQueries_.begin(), childrenQueries_.end(),
                        [ token ]( const ChildrenQuery& query ) { return query.token == token; } );
    if ( found == childrenQueries_.end() ) {
        return; // cancelled: the node no longer waits for this answer
    }
    const auto query = *found;
    childrenQueries_.erase( found );
    if ( const auto node = nodes_.value( query.elementId ); node && node->tree ) {
        node->tree->childrenAnswered( query.nodeId, answered, nodes );
    }
    sendChildrenQueries();
}

void PluginView::failTreeLoads()
{
    cancelAllChildren();
    for ( const auto& node : std::as_const( nodes_ ) ) {
        if ( node && node->tree ) {
            node->tree->failLoading();
        }
    }
}

void PluginView::cancelChildren( const QString& elementId, const QString& nodeId )
{
    for ( qsizetype at = 0; at < childrenQueries_.size(); ++at ) {
        const auto query = childrenQueries_.at( at );
        if ( query.elementId == elementId && query.nodeId == nodeId ) {
            childrenQueries_.removeAt( at );
            if ( !query.queryId.isEmpty() && queries_.cancel ) {
                queries_.cancel( query.queryId );
            }
            break;
        }
    }
    sendChildrenQueries();
}

void PluginView::cancelAllChildren()
{
    const auto queries = std::exchange( childrenQueries_, {} );
    for ( const auto& query : queries ) {
        if ( !query.queryId.isEmpty() && queries_.cancel ) {
            queries_.cancel( query.queryId );
        }
    }
}

void PluginView::sendChildrenQueries()
{
    for ( qsizetype at = 0; at < childrenQueries_.size(); ++at ) {
        int sent = 0;
        for ( const auto& query : std::as_const( childrenQueries_ ) ) {
            sent += query.queryId.isEmpty() ? 0 : 1;
        }
        if ( sent >= MaxChildrenQueries ) {
            return;
        }
        if ( !childrenQueries_.at( at ).queryId.isEmpty() ) {
            continue;
        }
        const auto waiting = childrenQueries_.at( at );
        if ( startChildrenQuery( waiting.token ) ) {
            // The list may have changed during the call: start again from the top.
            at = -1;
            continue;
        }
        // Not sent (the plugin stopped meanwhile): the node shows "Could not load". The query
        // is gone from the list already.
        if ( const auto node = nodes_.value( waiting.elementId ); node && node->tree ) {
            node->tree->childrenAnswered( waiting.nodeId, false, {} );
        }
        at = -1;
    }
}
