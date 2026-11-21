// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// E17e: trees and file trees (docs/plugin-sdk-advanced.md section 28).
#include "mainwindow.h"
#include "pluginfeatures.h"
#include "pluginfiletree.h"
#include "pluginmanager.h"
#include "pluginmenus.h"
#include "plugintree.h"
#include "pluginview.h"
#include "pluginviewhost.h"
#include "session.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QJsonDocument>
#include <QMenu>
#include <QProcess>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleOption>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>
#include <catch2/catch.hpp>
#include <functional>
#include <memory>

using cilogg::plugins::PluginManager;

namespace {
const QString PluginId = QStringLiteral( "org.test.e17e" );

bool waitFor( const std::function<bool()>& predicate, int timeout = 5000 )
{
    return QTest::qWaitFor( predicate, timeout );
}

// The native file system model lists folders on its own thread, which a busy machine (or a
// virus scanner on new temporary files) can hold up for seconds.
constexpr int FileSystemWait = 20000;

QJsonObject treeNode( const QString& id, const QJsonArray& cells, const QJsonObject& extra = {} )
{
    QJsonObject result{ { "id", id }, { "cells", cells } };
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        result.insert( it.key(), it.value() );
    }
    return result;
}

QJsonObject boardsTree( const QJsonObject& extra = {} )
{
    QJsonObject tree{
        { "type", "tree" },
        { "id", "tr" },
        { "selection", "multi" },
        { "checkboxes", true },
        { "auto_tristate", true },
        { "columns", QJsonArray{ QJsonObject{ { "id", "name" }, { "title", "Name" }, { "editable", true } },
                                 QJsonObject{ { "id", "state" }, { "title", "State" },
                                              { "tooltip_key", "colkey" } } } },
        { "nodes",
          QJsonArray{
              treeNode( "bench", { "Bench", "" },
                        { { "expanded", true },
                          { "icon", "builtin:folder" },
                          { "children",
                            QJsonArray{ treeNode( "b1", { QJsonObject{ { "text", "Board 1" },
                                                                       { "bold", true },
                                                                       { "tooltip", "First board" } },
                                                          "ok" },
                                                  { { "checked", true }, { "tone", "accent" },
                                                    { "tooltip_key", "nodekey" },
                                                    { "editors", QJsonObject{ { "name", QJsonObject{
                                                                                         { "kind", "text" } } } } } } ),
                                        treeNode( "b2", { "Board 2", "off" } ) } } } ),
              treeNode( "spare", { "Spare", "" } ) } } };
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        tree.insert( it.key(), it.value() );
    }
    return tree;
}

// A plugin view with fake host hooks: icons, an element menu that records its target, tree/children
// queries the test answers, tooltip queries it records and a save dialog.
struct View {
    struct Pending {
        QJsonObject params;
        std::function<void( bool, const QJsonArray& )> done;
    };
    PluginView view;
    QSignalSpy events;
    QJsonObject menuTarget;
    QString menuId;
    QString savePath;
    std::map<QString, Pending> children; // query id -> request
    QStringList cancelled;
    QList<QJsonObject> tooltipQueries;
    bool refuseChildren = false;
    bool answerAtOnce = false; // tree/children answers `atOnce` during the call
    QJsonArray atOnce;
    int nextQuery = 0;

    explicit View( const QJsonObject& element )
        : view( PluginId, "main", dialogs() )
        , events( &view, &PluginView::eventRaised )
    {
        PluginViewQueries queries;
        queries.icon = []( const QJsonValue& reference ) {
            return reference.toString().startsWith( "builtin:" ) ? QIcon( QPixmap( 16, 16 ) ) : QIcon{};
        };
        queries.elementMenu = [ this ]( QMenu* menu, const QString&, const QString& id,
                                        const QJsonObject& target ) {
            menuId = id;
            menuTarget = target;
            menu->addSeparator();
            menu->addAction( "Plugin entry" )->setObjectName( "plugin_entry" );
        };
        queries.treeChildren = [ this ]( const QJsonObject& params,
                                         std::function<void( bool, const QJsonArray& )> done ) {
            if ( refuseChildren ) {
                return QString{};
            }
            const auto id = QString( "q:%1" ).arg( ++nextQuery );
            if ( answerAtOnce ) {
                done( true, atOnce );
                return id;
            }
            children[ id ] = { params, std::move( done ) };
            return id;
        };
        queries.cancel = [ this ]( const QString& id ) {
            cancelled.append( id );
            children.erase( id );
        };
        queries.resolveTooltip = [ this ]( const QJsonObject& params, int, bool,
                                           std::function<void( bool, const QJsonObject& )> done ) {
            tooltipQueries.append( params );
            done( true, QJsonObject{ { "text", "answer " + params.value( "node_id" ).toString() },
                                     { "ttl_ms", 60000 } } );
            return QString( "t:%1" ).arg( tooltipQueries.size() );
        };
        view.setQueries( queries );
        view.setExtendedEvents( true );
        view.setRoot( QJsonObject{ { "type", "column" },
                                   { "id", "body" },
                                   { "children", QJsonArray{ element } } } );
        view.show();
    }
    // The hooks reach members declared after the view, which go first: the view closes without
    // them.
    ~View()
    {
        view.setQueries( {} );
    }
    View( const View& ) = delete;
    View& operator=( const View& ) = delete;
    PluginViewDialogs dialogs()
    {
        PluginViewDialogs result;
        result.saveFile = [ this ]( QWidget*, const QString&, const QString&, const QString&,
                                    const QString& ) { return savePath; };
        return result;
    }
    QTreeView* tree( const QString& id = "tr" ) const
    {
        return qobject_cast<QTreeView*>( view.element( id ) );
    }
    pluginview::PluginTreeModel* model() const
    {
        return dynamic_cast<pluginview::PluginTreeModel*>( tree()->model() );
    }
    PluginTreeBehaviour* behaviour() const
    {
        return tree()->findChild<PluginTreeBehaviour*>();
    }
    QModelIndex index( const QString& nodeId, int column = 0 ) const
    {
        return model()->indexOf( nodeId, column );
    }
    QPoint center( const QString& nodeId, int column = 0 ) const
    {
        return tree()->visualRect( index( nodeId, column ) ).center();
    }
    QJsonObject lastOf( const QString& type ) const
    {
        for ( auto at = events.size() - 1; at >= 0; --at ) {
            const auto event = events.at( at ).front().toJsonObject();
            if ( event.value( "type" ) == type ) {
                return event;
            }
        }
        return {};
    }
    int countOf( const QString& type ) const
    {
        int count = 0;
        for ( const auto& event : events ) {
            count += event.front().toJsonObject().value( "type" ) == type ? 1 : 0;
        }
        return count;
    }
    // The pending tree/children query of a node.
    QString queryOf( const QString& nodeId ) const
    {
        for ( const auto& [ id, pending ] : children ) {
            if ( pending.params.value( "node_id" ) == nodeId ) {
                return id;
            }
        }
        return {};
    }
    void answer( const QString& nodeId, bool answered, const QJsonArray& nodes = {} )
    {
        const auto id = queryOf( nodeId );
        REQUIRE_FALSE( id.isEmpty() );
        auto done = children[ id ].done;
        children.erase( id );
        done( answered, nodes );
    }
    void patch( const QJsonObject& entry )
    {
        view.applyPatch( { entry } );
    }
};

QStringList menuNames( const QMenu& menu )
{
    QStringList names;
    for ( const auto* action : menu.actions() ) {
        if ( !action->isSeparator() ) {
            names.append( action->objectName() );
        }
    }
    return names;
}
} // namespace

TEST_CASE( "E17e tree nodes, cells, columns, expansion and check boxes", "[ui][workbench][E17]" )
{
    View fixture( boardsTree() );
    auto* tree = fixture.tree();
    REQUIRE( tree );
    auto* model = fixture.model();
    REQUIRE( model->rowCount() == 2 );
    REQUIRE( tree->isExpanded( fixture.index( "bench" ) ) ); // `expanded: true`
    REQUIRE_FALSE( tree->header()->isHidden() );
    const auto b1 = fixture.index( "b1" );
    REQUIRE( b1.data( Qt::DisplayRole ) == "Board 1" );
    REQUIRE( b1.data( Qt::ToolTipRole ) == "First board" );
    REQUIRE( b1.data( Qt::FontRole ).value<QFont>().bold() );
    REQUIRE( b1.data( Qt::ForegroundRole ).value<QColor>() == pluginview::toneColor( "accent" ) );
    REQUIRE_FALSE( fixture.index( "bench" ).data( Qt::DecorationRole ).value<QIcon>().isNull() );
    REQUIRE( fixture.index( "b1", 1 ).data( Qt::DisplayRole ) == "ok" );
    // auto_tristate: the bench follows its boards.
    REQUIRE( fixture.index( "bench" ).data( Qt::CheckStateRole ) == Qt::PartiallyChecked );
    REQUIRE( fixture.index( "spare" ).data( Qt::CheckStateRole ) == Qt::Unchecked );
    // Checking the bench sets every board and reports the nodes it set, the clicked one first.
    REQUIRE( model->setData( fixture.index( "bench" ), Qt::Checked, Qt::CheckStateRole ) );
    auto value = fixture.lastOf( "checked" ).value( "value" ).toObject();
    REQUIRE( value.value( "node_id" ) == "bench" );
    REQUIRE( value.value( "checked" ) == true );
    REQUIRE( value.value( "node_ids" ).toArray() == QJsonArray{ "bench", "b2" } );
    REQUIRE( fixture.index( "b2" ).data( Qt::CheckStateRole ) == Qt::Checked );
    // An event that cannot be delivered puts the boxes back.
    QObject::connect( &fixture.view, &PluginView::eventRaised, &fixture.view,
                      [ &fixture ]( const QJsonObject& event ) {
                          if ( event.value( "type" ) == "checked" ) {
                              fixture.view.markEventDropped();
                          }
                      } );
    REQUIRE( model->setData( fixture.index( "b2" ), Qt::Unchecked, Qt::CheckStateRole ) );
    REQUIRE( fixture.index( "b2" ).data( Qt::CheckStateRole ) == Qt::Checked );
    REQUIRE( fixture.index( "bench" ).data( Qt::CheckStateRole ) == Qt::Checked );
    // Without columns a tree has one column and no header.
    View list( QJsonObject{ { "type", "tree" },
                            { "id", "tr" },
                            { "nodes", QJsonArray{ QJsonObject{ { "id", "x" }, { "text", "X" } } } } } );
    REQUIRE( list.tree()->header()->isHidden() );
    REQUIRE( list.model()->columnCount() == 1 );
    REQUIRE( list.index( "x" ).data( Qt::DisplayRole ) == "X" );
}

TEST_CASE( "E17e lazy children load, fail, retry and keep to the per-view limit",
           "[ui][workbench][E17]" )
{
    QJsonArray lazy;
    for ( int index = 0; index < 24; ++index ) {
        lazy.append( QJsonObject{ { "id", QString( "n%1" ).arg( index ) },
                                  { "text", QString( "Node %1" ).arg( index ) },
                                  { "has_children", true } } );
    }
    View fixture( QJsonObject{ { "type", "tree" },
                               { "id", "tr" },
                               { "lazy", true },
                               { "expand_events", true },
                               { "nodes", lazy } } );
    auto* tree = fixture.tree();
    // Expanding a lazy node shows "Loading…" and asks the plugin.
    tree->expand( fixture.index( "n0" ) );
    REQUIRE( fixture.model()->rowCount( fixture.index( "n0" ) ) == 1 );
    REQUIRE( fixture.model()->index( 0, 0, fixture.index( "n0" ) ).data().toString() == "Loading…" );
    const auto query = fixture.children.at( fixture.queryOf( "n0" ) ).params;
    REQUIRE( query.value( "element_id" ) == "tr" );
    REQUIRE( query.value( "node_id" ) == "n0" );
    // The user's expansion is reported (expand_events).
    REQUIRE( fixture.lastOf( "expanded" ).value( "value" ).toObject()
             == QJsonObject{ { "node_id", "n0" } } );
    // The answer fills the node; `expanded` of a child is applied.
    fixture.answer( "n0", true,
                    { QJsonObject{ { "id", "c1" }, { "text", "Child" }, { "expanded", true },
                                   { "children", QJsonArray{ QJsonObject{ { "id", "g1" } } } } } } );
    REQUIRE( fixture.model()->rowCount( fixture.index( "n0" ) ) == 1 );
    REQUIRE( fixture.index( "c1" ).isValid() );
    REQUIRE( tree->isExpanded( fixture.index( "c1" ) ) );
    REQUIRE( fixture.countOf( "expanded" ) == 1 ); // the answer's expansion is no user event
    // A failure shows "Could not load"; expanding again asks again.
    tree->expand( fixture.index( "n1" ) );
    fixture.answer( "n1", false );
    REQUIRE( fixture.model()->index( 0, 0, fixture.index( "n1" ) ).data().toString()
             == "Could not load" );
    tree->collapse( fixture.index( "n1" ) );
    tree->expand( fixture.index( "n1" ) );
    REQUIRE_FALSE( fixture.queryOf( "n1" ).isEmpty() );
    // At most 4 queries per view are outstanding and 16 wait; more fail at once.
    for ( int index = 2; index < 24; ++index ) {
        tree->expand( fixture.index( QString( "n%1" ).arg( index ) ) );
    }
    REQUIRE( fixture.children.size() == 4 ); // n1, n2, n3, n4; n5 to n20 wait
    REQUIRE( fixture.model()->index( 0, 0, fixture.index( "n20" ) ).data().toString() == "Loading…" );
    REQUIRE( fixture.model()->index( 0, 0, fixture.index( "n21" ) ).data().toString()
             == "Could not load" );
    fixture.answer( "n2", true );
    REQUIRE( fixture.children.size() == 4 );
    REQUIRE_FALSE( fixture.queryOf( "n5" ).isEmpty() ); // the first waiting one was sent
    REQUIRE_FALSE( fixture.model()->hasChildren( fixture.index( "n2" ) ) ); // no children
    // The plugin setting a loading node's children ends its query.
    fixture.patch( QJsonObject{ { "id", "tr" },
                                { "parent", "n3" },
                                { "replace_nodes", QJsonArray{ QJsonObject{ { "id", "p1" } } } } } );
    REQUIRE( fixture.cancelled.size() == 1 );
    REQUIRE( fixture.queryOf( "n3" ).isEmpty() );
    REQUIRE( fixture.index( "p1" ).isValid() );
    // A query the host refuses (a stopped plugin) shows "Could not load" at once.
    fixture.children.clear();
    View refused( QJsonObject{ { "type", "tree" }, { "id", "tr" }, { "lazy", true }, { "nodes", lazy } } );
    refused.refuseChildren = true;
    refused.tree()->expand( refused.index( "n0" ) );
    REQUIRE( refused.model()->index( 0, 0, refused.index( "n0" ) ).data().toString()
             == "Could not load" );
}

TEST_CASE( "E17e many removals, stale and immediate answers, a stopped plugin and inherited checks",
           "[ui][workbench][E17]" )
{
    // remove_nodes with many ids, nodes and their ancestors among them: one reset, and the nodes
    // that stay keep their expansion and selection.
    QJsonArray groups;
    for ( int group = 0; group < 40; ++group ) {
        QJsonArray leaves;
        for ( int leaf = 0; leaf < 3; ++leaf ) {
            const auto id = QString( "g%1l%2" ).arg( group ).arg( leaf );
            leaves.append( QJsonObject{ { "id", id }, { "text", id } } );
        }
        groups.append( QJsonObject{ { "id", QString( "g%1" ).arg( group ) },
                                    { "text", "Group" },
                                    { "expanded", true },
                                    { "children", leaves } } );
    }
    View fixture( QJsonObject{
        { "type", "tree" }, { "id", "tr" }, { "selection", "multi" }, { "nodes", groups } } );
    auto* tree = fixture.tree();
    tree->selectionModel()->select( fixture.index( "g39l0" ),
                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
    QJsonArray ids;
    for ( int group = 0; group < 35; ++group ) {
        ids.append( QString( "g%1l1" ).arg( group ) ); // a node first, then its parent
        ids.append( QString( "g%1" ).arg( group ) );
    }
    REQUIRE( ids.size() > pluginview::PluginTreeModel::LargeRemoval );
    fixture.patch( QJsonObject{ { "id", "tr" }, { "remove_nodes", ids } } );
    REQUIRE( fixture.model()->rowCount() == 5 );
    REQUIRE( fixture.model()->nodeCount() == 20 );
    REQUIRE_FALSE( fixture.index( "g0l0" ).isValid() );
    REQUIRE( tree->isExpanded( fixture.index( "g39" ) ) );
    REQUIRE( fixture.behaviour()->selectedNodeIds( 10 ) == QStringList{ "g39l0" } );

    // An answer whose query ended changes nothing; an answer during the call is taken.
    const QJsonArray lazy{ QJsonObject{ { "id", "a" }, { "text", "A" }, { "has_children", true } },
                           QJsonObject{ { "id", "b" }, { "text", "B" }, { "has_children", true } } };
    View loading( QJsonObject{ { "type", "tree" }, { "id", "tr" }, { "lazy", true }, { "nodes", lazy } } );
    loading.tree()->expand( loading.index( "a" ) );
    const auto first = loading.queryOf( "a" );
    REQUIRE_FALSE( first.isEmpty() );
    const auto stale = loading.children.at( first ).done;
    loading.patch( QJsonObject{ { "id", "tr" },
                                { "parent", "a" },
                                { "replace_nodes", QJsonArray{ QJsonObject{ { "id", "a1" } } } } } );
    REQUIRE( loading.cancelled == QStringList{ first } );
    stale( true, QJsonArray{ QJsonObject{ { "id", "late" } } } );
    REQUIRE_FALSE( loading.index( "late" ).isValid() );
    REQUIRE( loading.index( "a1" ).isValid() );
    loading.answerAtOnce = true;
    loading.atOnce = QJsonArray{ QJsonObject{ { "id", "b1" }, { "text", "B1" } } };
    loading.tree()->expand( loading.index( "b" ) );
    REQUIRE( loading.index( "b1" ).isValid() );
    REQUIRE( loading.model()->rowCount( loading.index( "b" ) ) == 1 );
    REQUIRE_FALSE( loading.model()->isLoading( "b" ) );
    // A stopped plugin never answers: its loading nodes show "Could not load".
    loading.answerAtOnce = false;
    loading.patch( QJsonObject{
        { "id", "tr" },
        { "insert_nodes",
          QJsonArray{ QJsonObject{ { "id", "c" }, { "text", "C" }, { "has_children", true } } } } } );
    loading.tree()->expand( loading.index( "c" ) );
    REQUIRE_FALSE( loading.queryOf( "c" ).isEmpty() );
    loading.view.failTreeLoads();
    REQUIRE( loading.model()->index( 0, 0, loading.index( "c" ) ).data().toString()
             == "Could not load" );
    REQUIRE( loading.queryOf( "c" ).isEmpty() );

    // auto_tristate: inserted nodes without their own state follow a checked parent, and the
    // plugin unchecking a parent unchecks its loaded descendants.
    View checks( boardsTree() );
    REQUIRE( checks.model()->setData( checks.index( "bench" ), Qt::Checked, Qt::CheckStateRole ) );
    checks.patch( QJsonObject{
        { "id", "tr" },
        { "parent", "bench" },
        { "insert_nodes",
          QJsonArray{ QJsonObject{ { "id", "b3" }, { "text", "Board 3" } },
                      QJsonObject{ { "id", "b4" }, { "text", "Board 4" }, { "checked", false } } } } } );
    REQUIRE( checks.index( "b3" ).data( Qt::CheckStateRole ) == Qt::Checked );
    REQUIRE( checks.index( "b4" ).data( Qt::CheckStateRole ) == Qt::Unchecked );
    REQUIRE( checks.index( "bench" ).data( Qt::CheckStateRole ) == Qt::PartiallyChecked );
    checks.patch( QJsonObject{
        { "id", "tr" },
        { "update_nodes", QJsonArray{ QJsonObject{ { "id", "bench" }, { "checked", false } } } } } );
    for ( const auto* id : { "bench", "b1", "b2", "b3", "b4" } ) {
        REQUIRE( checks.index( id ).data( Qt::CheckStateRole ) == Qt::Unchecked );
    }
}

TEST_CASE( "E17e tree patches keep expansion and selection by id", "[ui][workbench][E17]" )
{
    View fixture( boardsTree() );
    auto* tree = fixture.tree();
    tree->selectionModel()->select( fixture.index( "b2" ),
                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
    const auto selectedBefore = fixture.countOf( "selected" );
    // A same-shaped root with other nodes: the expansion and the selection stay with the ids.
    auto root = boardsTree();
    auto nodes = root.value( "nodes" ).toArray();
    auto bench = nodes.at( 0 ).toObject();
    bench.remove( "expanded" );
    bench.insert( "cells", QJsonArray{ "Bench (renamed)", "" } );
    nodes.replace( 0, bench );
    root.insert( "nodes", nodes );
    fixture.view.setRoot( QJsonObject{ { "type", "column" }, { "id", "body" },
                                       { "children", QJsonArray{ root } } } );
    REQUIRE( fixture.index( "bench" ).data().toString() == "Bench (renamed)" );
    REQUIRE( tree->isExpanded( fixture.index( "bench" ) ) );
    REQUIRE( fixture.behaviour()->selectedNodeIds( 10 ) == QStringList{ "b2" } );
    REQUIRE( fixture.countOf( "selected" ) == selectedBefore ); // nothing changed for the plugin
    // update_nodes, insert_nodes and remove_nodes in one entry, in that order.
    fixture.patch( QJsonObject{
        { "id", "tr" },
        { "parent", "bench" },
        { "update_nodes", QJsonArray{ QJsonObject{ { "id", "b1" }, { "cells", QJsonArray{ "Board one", "ok" } } } } },
        { "insert_nodes", QJsonArray{ QJsonObject{ { "id", "b0" }, { "text", "Board 0" } } } },
        { "before", "b1" },
        { "remove_nodes", QJsonArray{ "b2" } } } );
    REQUIRE( fixture.index( "b1" ).data().toString() == "Board one" );
    REQUIRE( fixture.index( "b0" ).row() == 0 );
    REQUIRE_FALSE( fixture.index( "b2" ).isValid() );
    // The selected node went away: the plugin hears what is still selected.
    REQUIRE( fixture.countOf( "selected" ) == selectedBefore + 1 );
    REQUIRE( fixture.lastOf( "selected" ).value( "value" ).toArray().isEmpty() );
    // collapse and expand; the selection patch expands the ancestors and sends nothing.
    fixture.patch( QJsonObject{ { "id", "tr" }, { "collapse", QJsonArray{ "bench" } } } );
    REQUIRE_FALSE( tree->isExpanded( fixture.index( "bench" ) ) );
    const auto before = fixture.events.size();
    fixture.patch( QJsonObject{ { "id", "tr" },
                                { "select", QJsonArray{ "b1", "spare", "ghost" } },
                                { "current", "b1" },
                                { "scroll_to", "b1" } } );
    REQUIRE( fixture.events.size() == before );
    REQUIRE( tree->isExpanded( fixture.index( "bench" ) ) );
    REQUIRE( fixture.behaviour()->selectedNodeIds( 10 ) == QStringList{ "b1", "spare" } );
    REQUIRE( fixture.behaviour()->currentNodeId() == "b1" );
    // A tree without selection takes `current` too: Enter, Space and Copy act on it.
    View unselectable( boardsTree( { { "selection", "none" } } ) );
    unselectable.patch( QJsonObject{ { "id", "tr" }, { "current", "b2" } } );
    REQUIRE( unselectable.behaviour()->currentNodeId() == "b2" );
    REQUIRE( unselectable.behaviour()->selectedNodeIds( 10 ).isEmpty() );
    // A patch changing a node drops its cached tooltips.
    QString error;
    REQUIRE( fixture.view.resolveTooltip( "tr", {}, "state", "b1", true,
                                          []( const QString&, const QJsonObject&, const QString&,
                                              qint64 ) {},
                                          &error ) );
    REQUIRE( fixture.view.tooltips()->cachedCount() == 0 ); // no provider: nothing asked or kept
    fixture.patch( QJsonObject{ { "id", "tr" }, { "set", QJsonObject{ { "tooltip_provider", true } } } } );
    REQUIRE( fixture.view.resolveTooltip( "tr", {}, "state", "b1", true,
                                          []( const QString&, const QJsonObject&, const QString&,
                                              qint64 ) {},
                                          &error ) );
    REQUIRE( fixture.view.tooltips()->cachedCount() == 1 );
    fixture.patch( QJsonObject{ { "id", "tr" },
                                { "update_nodes", QJsonArray{ QJsonObject{ { "id", "b1" }, { "tone", "warn" } } } } } );
    REQUIRE( fixture.view.tooltips()->cachedCount() == 0 );
}

TEST_CASE( "E17e drags end when nodes change under the press", "[ui][workbench][E17]" )
{
    // A toggle tree: the press deselects its node; nodes inserted before the drag call the drag
    // off, and the plugin hears the press's change once the patch is done.
    View toggle( boardsTree( { { "toggle_selection", true }, { "reorderable", true } } ) );
    auto* viewport = toggle.tree()->viewport();
    QTest::mouseClick( viewport, Qt::LeftButton, {}, toggle.center( "bench", 1 ) );
    QTest::mouseClick( viewport, Qt::LeftButton, {}, toggle.center( "b2", 1 ) );
    const auto clicks = toggle.countOf( "selected" );
    const auto at = toggle.center( "b2", 1 );
    QTest::mousePress( viewport, Qt::LeftButton, {}, at );
    REQUIRE( toggle.behaviour()->draggedNodes() == QStringList{ "bench", "b2" } );
    toggle.patch( QJsonObject{
        { "id", "tr" },
        { "parent", "bench" },
        { "insert_nodes", QJsonArray{ QJsonObject{ { "id", "bx" }, { "text", "Board x" } } } },
        { "before", "b2" } } );
    REQUIRE( toggle.behaviour()->draggedNodes().isEmpty() );
    REQUIRE( waitFor( [ & ] { return toggle.countOf( "selected" ) == clicks + 1; } ) );
    REQUIRE( toggle.lastOf( "selected" ).value( "value" ).toArray() == QJsonArray{ "bench" } );
    QTest::mouseRelease( viewport, Qt::LeftButton, {}, at );
    QTest::qWait( 20 );
    REQUIRE( toggle.countOf( "selected" ) == clicks + 1 ); // nothing more on the release

    // An extended tree: a press on a selected node changes nothing, so a change of the nodes
    // under it sends nothing either.
    View extended( boardsTree( { { "reorderable", true } } ) );
    auto* plain = extended.tree()->viewport();
    QTest::mouseClick( plain, Qt::LeftButton, {}, extended.center( "b1", 1 ) );
    const auto before = extended.countOf( "selected" );
    QTest::mousePress( plain, Qt::LeftButton, {}, extended.center( "b1", 1 ) );
    REQUIRE( extended.behaviour()->holdsSelection() );
    extended.patch( QJsonObject{
        { "id", "tr" },
        { "parent", "bench" },
        { "insert_nodes", QJsonArray{ QJsonObject{ { "id", "b0" }, { "text", "Board 0" } } } },
        { "before", "b1" } } );
    REQUIRE_FALSE( extended.behaviour()->holdsSelection() );
    REQUIRE( extended.behaviour()->draggedNodes().isEmpty() );
    QTest::mouseRelease( plain, Qt::LeftButton, {}, extended.center( "b1", 1 ) );
    QTest::qWait( 20 );
    REQUIRE( extended.countOf( "selected" ) == before );
    REQUIRE( extended.behaviour()->selectedNodeIds( 10 ) == QStringList{ "b1" } );

    // A patch that changes the nodes and sets the selection under a press that deselected its
    // node: the plugin hears nothing, neither its own selection nor the press's change.
    View patched( boardsTree( { { "toggle_selection", true }, { "reorderable", true } } ) );
    auto* area = patched.tree()->viewport();
    QTest::mouseClick( area, Qt::LeftButton, {}, patched.center( "bench", 1 ) );
    QTest::mouseClick( area, Qt::LeftButton, {}, patched.center( "b2", 1 ) );
    const auto selections = patched.countOf( "selected" );
    const auto pressed = patched.center( "b2", 1 );
    QTest::mousePress( area, Qt::LeftButton, {}, pressed );
    REQUIRE( patched.behaviour()->holdsSelection() );
    patched.patch( QJsonObject{
        { "id", "tr" },
        { "parent", "bench" },
        { "insert_nodes", QJsonArray{ QJsonObject{ { "id", "by" }, { "text", "Board y" } } } },
        { "before", "b2" },
        { "select", QJsonArray{ "spare" } } } );
    REQUIRE_FALSE( patched.behaviour()->holdsSelection() );
    REQUIRE( patched.behaviour()->draggedNodes().isEmpty() );
    QTest::mouseRelease( area, Qt::LeftButton, {}, pressed );
    QTest::qWait( 20 );
    REQUIRE( patched.countOf( "selected" ) == selections );
    REQUIRE( patched.behaviour()->selectedNodeIds( 10 ) == QStringList{ "spare" } );
}

TEST_CASE( "E17e tree selection, toggle selection, activated, edited and moved",
           "[ui][workbench][E17]" )
{
    View fixture( boardsTree( { { "toggle_selection", true }, { "reorderable", true } } ) );
    auto* tree = fixture.tree();
    REQUIRE( tree->selectionMode() == QAbstractItemView::MultiSelection );
    // Each click adds a node; Shift+click adds the shown nodes from the last click.
    QTest::mouseClick( tree->viewport(), Qt::LeftButton, {}, fixture.center( "bench", 1 ) );
    QTest::mouseClick( tree->viewport(), Qt::LeftButton, Qt::ShiftModifier, fixture.center( "b2", 1 ) );
    REQUIRE( fixture.behaviour()->selectedNodeIds( 10 ) == QStringList{ "bench", "b1", "b2" } );
    QTest::mouseClick( tree->viewport(), Qt::LeftButton, {}, fixture.center( "b1", 1 ) );
    REQUIRE( fixture.behaviour()->selectedNodeIds( 10 ) == QStringList{ "bench", "b2" } );
    auto selected = fixture.lastOf( "selected" );
    REQUIRE( selected.value( "value" ).toArray() == QJsonArray{ "bench", "b2" } );
    REQUIRE( selected.value( "selected_count" ) == 2 );
    REQUIRE( selected.value( "current" ) == "b1" );
    // A press deselects its node at once; a drag it starts still moves the nodes selected before
    // the press.
    const auto beforePress = fixture.countOf( "selected" );
    QTest::mousePress( tree->viewport(), Qt::LeftButton, {}, fixture.center( "b2", 1 ) );
    REQUIRE( fixture.behaviour()->selectedNodeIds( 10 ) == QStringList{ "bench" } );
    REQUIRE( fixture.behaviour()->draggedNodes() == QStringList{ "bench", "b2" } );
    REQUIRE( fixture.countOf( "selected" ) == beforePress ); // held while a drag may start
    QTest::mouseRelease( tree->viewport(), Qt::LeftButton, {}, fixture.center( "b2", 1 ) );
    REQUIRE( fixture.behaviour()->draggedNodes().isEmpty() );
    REQUIRE( fixture.countOf( "selected" ) == beforePress + 1 ); // no drag: one event
    REQUIRE( fixture.lastOf( "selected" ).value( "value" ).toArray() == QJsonArray{ "bench" } );
    // activated: a double-click on a cell that is not editable, or Enter. A real double-click
    // starts with a press, which QTest::mouseDClick leaves out for widgets.
    QTest::mouseClick( tree->viewport(), Qt::LeftButton, {}, fixture.center( "b2", 1 ) );
    QTest::mouseDClick( tree->viewport(), Qt::LeftButton, {}, fixture.center( "b2", 1 ) );
    REQUIRE( waitFor( [ & ] { return fixture.countOf( "activated" ) == 1; } ) );
    REQUIRE( fixture.lastOf( "activated" ).value( "value" ).toObject()
             == QJsonObject{ { "node_id", "b2" }, { "column_id", "state" } } );
    tree->setCurrentIndex( fixture.index( "spare", 1 ) );
    tree->setFocus();
    QTest::keyClick( tree, Qt::Key_Return );
    REQUIRE( fixture.countOf( "activated" ) == 2 );
    tree->setCurrentIndex( fixture.index( "b1", 0 ) ); // editable: no activated
    QTest::keyClick( tree, Qt::Key_Return );
    REQUIRE( fixture.countOf( "activated" ) == 2 );
    // edited: as table cells, with the node.
    REQUIRE( fixture.model()->setData( fixture.index( "b1" ), QVariant::fromValue( QJsonValue( "Renamed" ) ),
                                       Qt::EditRole ) );
    const auto edited = fixture.lastOf( "edited" );
    REQUIRE( edited.value( "value" ) == "Renamed" );
    REQUIRE( edited.value( "node_id" ) == "b1" );
    REQUIRE( edited.value( "column_id" ) == "name" );
    REQUIRE( edited.value( "previous" ) == "Board 1" );
    // moved: the host never moves the nodes; never into themselves or their descendants.
    REQUIRE( fixture.behaviour()->moveNodes( { "b2" }, {}, "bench" ) );
    REQUIRE( fixture.lastOf( "moved" ).value( "value" ).toObject()
             == QJsonObject{ { "node_ids", QJsonArray{ "b2" } }, { "parent", QJsonValue() },
                             { "before", "bench" } } );
    REQUIRE( fixture.behaviour()->moveNodes( { "spare" }, "bench", {} ) );
    REQUIRE( fixture.lastOf( "moved" ).value( "value" ).toObject().value( "parent" ) == "bench" );
    REQUIRE_FALSE( fixture.behaviour()->moveNodes( { "bench" }, "b1", {} ) );
    REQUIRE_FALSE( fixture.behaviour()->moveNodes( { "b1" }, "bench", "b1" ) );
    REQUIRE( fixture.index( "b2" ).parent() == fixture.index( "bench" ) );
}

TEST_CASE( "E17e tree context menu, Copy, Copy All and Export CSV", "[ui][workbench][E17]" )
{
    QTemporaryDir temp;
    View fixture( boardsTree( { { "context_menu", "nodes" }, { "export", true } } ) );
    fixture.savePath = QDir( temp.path() ).filePath( "tree.csv" );
    std::unique_ptr<QMenu> menu( fixture.view.buildElementMenu( "tr", fixture.center( "b2" ) ) );
    REQUIRE( menu );
    const auto base = PluginView::objectNameFor( PluginId, "main", "tr" );
    REQUIRE( menuNames( *menu ) == QStringList{ base + "_copy", base + "_copy_all", base + "_export",
                                                "plugin_entry" } );
    REQUIRE( fixture.menuId == "nodes" );
    REQUIRE( fixture.menuTarget == QJsonObject{ { "node_id", "b2" },
                                                { "selected_ids", QJsonArray{ "b2" } },
                                                { "selected_count", 1 } } );
    menu->findChild<QAction*>( base + "_copy" )->trigger();
    REQUIRE( QApplication::clipboard()->text() == "Board 2\toff" );
    menu->findChild<QAction*>( base + "_copy_all" )->trigger();
    REQUIRE( QApplication::clipboard()->text()
             == "Name\tState\nBench\t\n  Board 1\tok\n  Board 2\toff\nSpare\t" );
    menu->findChild<QAction*>( base + "_export" )->trigger();
    QFile csv( fixture.savePath );
    REQUIRE( csv.open( QIODevice::ReadOnly ) );
    REQUIRE( QString::fromUtf8( csv.readAll() )
             == "level,Name,State\r\n0,Bench,\r\n1,Board 1,ok\r\n1,Board 2,off\r\n0,Spare,\r\n" );
    // get_plugin_menus / trigger_plugin_menu: the element menu of a node, by id.
    QString menuId;
    QJsonObject target;
    QString error;
    REQUIRE( fixture.view.elementMenu( "tr", {}, "b1", &menuId, &target, &error ) );
    REQUIRE( target.value( "node_id" ) == "b1" );
    REQUIRE_FALSE( fixture.view.elementMenu( "tr", {}, "ghost", &menuId, &target, &error ) );
    REQUIRE_FALSE( fixture.view.elementMenu( "tr", "row", {}, &menuId, &target, &error ) );
    // Copied text is formula-safe, and Copy All stops at whole lines before 1 MiB: 300 nodes of
    // 4,000 characters would be 1.2 million.
    QJsonArray nodes{ QJsonObject{ { "id", "f" }, { "text", "=HYPERLINK(1)" } } };
    for ( int index = 0; index < 300; ++index ) {
        nodes.append( QJsonObject{ { "id", QString( "n%1" ).arg( index ) },
                                   { "text", QString( "%1" ).arg( index, 3, 10, QChar( '0' ) )
                                                 + QString( 3997, 'x' ) } } );
    }
    View big( QJsonObject{ { "type", "tree" }, { "id", "tr" }, { "nodes", nodes } } );
    const auto all = big.behaviour()->copyText( true );
    REQUIRE( all.startsWith( "'=HYPERLINK(1)\n" ) );
    REQUIRE( all.size() <= 1024 * 1024 );
    REQUIRE( all.size() > 1000 * 1000 );
    REQUIRE( all.split( QLatin1Char( '\n' ) ).back().size() == 4000 ); // whole lines only
}

TEST_CASE( "E17e tree tooltips carry the node, the column and the most specific key",
           "[ui][workbench][E17]" )
{
    View fixture( boardsTree( { { "tooltip_provider", true }, { "tooltip_key", "treekey" } } ) );
    QString source;
    QString staticTip;
    QString error;
    const auto resolve = [ & ]( const QString& node, const QString& column ) {
        return fixture.view.resolveTooltip(
            "tr", {}, column, node, false,
            [ & ]( const QString& from, const QJsonObject&, const QString& tip, qint64 ) {
                source = from;
                staticTip = tip;
            },
            &error );
    };
    REQUIRE( resolve( "b1", "name" ) );
    REQUIRE( source == "plugin" );
    auto query = fixture.tooltipQueries.last();
    REQUIRE( query.value( "node_id" ) == "b1" );
    REQUIRE( query.value( "column_id" ) == "name" );
    REQUIRE( query.value( "tooltip_key" ) == "nodekey" ); // the node's, over the tree's
    REQUIRE( staticTip == "First board" );
    REQUIRE( resolve( "b2", "state" ) );
    REQUIRE( fixture.tooltipQueries.last().value( "tooltip_key" ) == "colkey" ); // the column's
    REQUIRE( resolve( "spare", {} ) ); // no column: the first
    REQUIRE( fixture.tooltipQueries.last().value( "column_id" ) == "name" );
    REQUIRE( fixture.tooltipQueries.last().value( "tooltip_key" ) == "treekey" );
    REQUIRE_FALSE( resolve( "ghost", {} ) );
    REQUIRE_FALSE( fixture.view.resolveTooltip( "tr", "r1", "name", {}, false, {}, &error ) );
}

TEST_CASE( "E17e file trees: roots, filters, selection, value and events", "[ui][workbench][E17]" )
{
    QTemporaryDir temp;
    const auto root = QDir( temp.path() ).filePath( "root" );
    const auto other = QDir( temp.path() ).filePath( "other" );
    REQUIRE( QDir().mkpath( QDir( root ).filePath( "sub" ) ) );
    REQUIRE( QDir().mkpath( other ) );
    const auto touch = []( const QString& path ) {
        QFile file( path );
        return file.open( QIODevice::WriteOnly ) && file.write( "x" ) == 1;
    };
    REQUIRE( touch( QDir( root ).filePath( "a.bin" ) ) );
    REQUIRE( touch( QDir( root ).filePath( "b.txt" ) ) );
    REQUIRE( touch( QDir( root ).filePath( "sub/c.bin" ) ) );
    REQUIRE( touch( QDir( other ).filePath( "d.hex" ) ) );
    const auto rootPath = QDir::fromNativeSeparators( QDir::cleanPath( root ) );
    const auto otherPath = QDir::fromNativeSeparators( QDir::cleanPath( other ) );
    View fixture( QJsonObject{
        { "type", "file_tree" },
        { "id", "files" },
        { "roots", QJsonArray{ rootPath, otherPath, "relative/path", rootPath + "/sub" } },
        { "filters", QJsonArray{ "*.bin", "*.hex" } },
        { "selection", "multi" },
        { "columns", QJsonArray{ "size", "name" } },
        { "context_menu", "files_menu" },
        { "tooltip_provider", true } } );
    auto* tree = fixture.tree( "files" );
    REQUIRE( tree );
    auto* behaviour = tree->findChild<PluginFileTreeBehaviour*>();
    auto* model = behaviour->model();
    // Relative and nested roots are skipped; each root shows its path.
    REQUIRE( model->rowCount() == 2 );
    REQUIRE( model->index( 0, 0 ).data().toString() == QDir::toNativeSeparators( rootPath ) );
    REQUIRE( model->pathOf( model->index( 1, 0 ) ) == otherPath );
    // `name` comes first, then the columns in the order given.
    REQUIRE( tree->header()->visualIndex( 0 ) == 0 );
    REQUIRE_FALSE( tree->isColumnHidden( 1 ) );
    REQUIRE( tree->isColumnHidden( 2 ) );
    REQUIRE( tree->isColumnHidden( 3 ) );
    // Folders load in the background; b.txt does not match the filters and is hidden.
    tree->expand( model->index( 0, 0 ) );
    const auto names = [ & ]( const QModelIndex& parent ) {
        QStringList result;
        for ( int row = 0; row < model->rowCount( parent ); ++row ) {
            result.append( model->index( row, 0, parent ).data().toString() );
        }
        result.sort();
        return result;
    };
    REQUIRE( waitFor( [ & ] { return names( model->index( 0, 0 ) ) == QStringList{ "a.bin", "sub" }; },
                      FileSystemWait ) );
    // Selecting a file reports the paths (forward slashes) and puts them in the inputs.
    const auto file = model->indexOfPath( rootPath + "/a.bin" );
    REQUIRE( file.isValid() );
    tree->selectionModel()->select( file, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
    auto selected = fixture.lastOf( "selected" );
    REQUIRE( selected.value( "value" ).toArray() == QJsonArray{ rootPath + "/a.bin" } );
    REQUIRE( selected.value( "selected_count" ) == 1 );
    REQUIRE( fixture.view.inputs().value( "files" ).toArray() == QJsonArray{ rootPath + "/a.bin" } );
    // activated: a double-click on a file, never on a folder (with the press a real one starts
    // with).
    QTest::mouseClick( tree->viewport(), Qt::LeftButton, {}, tree->visualRect( file ).center() );
    QTest::mouseDClick( tree->viewport(), Qt::LeftButton, {}, tree->visualRect( file ).center() );
    REQUIRE( waitFor( [ & ] { return fixture.countOf( "activated" ) == 1; } ) );
    REQUIRE( fixture.lastOf( "activated" ).value( "value" ).toObject()
             == QJsonObject{ { "path", rootPath + "/a.bin" } } );
    // A patch sets the value (selects and reveals) without an event; paths outside are ignored.
    const auto before = fixture.events.size();
    fixture.patch( QJsonObject{ { "id", "files" },
                                { "set", QJsonObject{ { "value", QJsonArray{ otherPath + "/d.hex",
                                                                             "/elsewhere/x.bin" } } } } } );
    REQUIRE( fixture.events.size() == before );
    REQUIRE( behaviour->value( 10 ) == QStringList{ otherPath + "/d.hex" } );
    REQUIRE( tree->isExpanded( model->index( 1, 0 ) ) );
    // `expand` opens folders.
    fixture.patch( QJsonObject{ { "id", "files" }, { "expand", QJsonArray{ rootPath + "/sub" } } } );
    REQUIRE( tree->isExpanded( model->indexOfPath( rootPath + "/sub" ) ) );
    // The menu: Copy Path, then the plugin's entries with the clicked path.
    std::unique_ptr<QMenu> menu( fixture.view.buildElementMenu( "files", tree->visualRect( file ).center() ) );
    REQUIRE( menu );
    const auto base = PluginView::objectNameFor( PluginId, "main", "files" );
    REQUIRE( menuNames( *menu ) == QStringList{ base + "_copy", "plugin_entry" } );
    REQUIRE( fixture.menuTarget.value( "node_id" ) == rootPath + "/a.bin" );
    menu->findChild<QAction*>( base + "_copy" )->trigger();
    REQUIRE( QApplication::clipboard()->text() == QDir::toNativeSeparators( rootPath + "/a.bin" ) );
    // Tooltips: the path, and the provider gets it as node_id.
    QString staticTip;
    QString error;
    REQUIRE( fixture.view.resolveTooltip(
        "files", {}, {}, rootPath + "/a.bin", false,
        [ & ]( const QString&, const QJsonObject&, const QString& tip, qint64 ) { staticTip = tip; },
        &error ) );
    REQUIRE( staticTip == QDir::toNativeSeparators( rootPath + "/a.bin" ) );
    REQUIRE( fixture.tooltipQueries.last().value( "node_id" ) == rootPath + "/a.bin" );
    REQUIRE_FALSE( fixture.view.resolveTooltip( "files", {}, {}, "/elsewhere", false, {}, &error ) );
    // select: "files" makes folders unselectable.
    fixture.patch( QJsonObject{ { "id", "files" }, { "set", QJsonObject{ { "select", "files" } } } } );
    REQUIRE_FALSE( model->flags( model->indexOfPath( rootPath + "/sub" ) ).testFlag( Qt::ItemIsSelectable ) );
    REQUIRE( model->flags( model->indexOfPath( rootPath + "/a.bin" ) ).testFlag( Qt::ItemIsSelectable ) );
}

TEST_CASE( "E17e file tree check boxes, greyed files and network roots", "[ui][workbench][E17]" )
{
    QTemporaryDir temp;
    const auto root = QDir::fromNativeSeparators( QDir::cleanPath( temp.path() ) );
    for ( const auto* name : { "a.bin", "b.txt" } ) {
        QFile file( QDir( root ).filePath( QString::fromLatin1( name ) ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
    }
    View fixture( QJsonObject{ { "type", "file_tree" },
                               { "id", "files" },
                               { "roots", QJsonArray{ root, "//no-such-server-e17e/share" } },
                               { "filters", QJsonArray{ "*.bin" } },
                               { "hide_filtered", false },
                               { "checkboxes", true },
                               { "select", "files" } } );
    auto* tree = fixture.tree( "files" );
    auto* behaviour = tree->findChild<PluginFileTreeBehaviour*>();
    auto* model = behaviour->model();
    // The network root is listed but not touched until the user opens it.
    REQUIRE( model->rowCount() == 2 );
    REQUIRE_FALSE( model->isAttached( 1 ) );
    REQUIRE( model->hasChildren( model->index( 1, 0 ) ) );
    tree->expand( model->index( 0, 0 ) );
    REQUIRE( waitFor( [ & ] { return model->rowCount( model->index( 0, 0 ) ) == 2; }, FileSystemWait ) );
    // A file that does not match is greyed out and cannot be checked.
    const auto text = model->indexOfPath( root + "/b.txt" );
    REQUIRE_FALSE( model->flags( text ).testFlag( Qt::ItemIsEnabled ) );
    REQUIRE_FALSE( model->flags( text ).testFlag( Qt::ItemIsUserCheckable ) );
    // Checking a file reports the checked paths as the value.
    const auto binary = model->indexOfPath( root + "/a.bin" );
    REQUIRE( model->setData( binary, Qt::Checked, Qt::CheckStateRole ) );
    REQUIRE( fixture.lastOf( "selected" ).value( "value" ).toArray() == QJsonArray{ root + "/a.bin" } );
    REQUIRE( fixture.view.inputs().value( "files" ).toArray() == QJsonArray{ root + "/a.bin" } );
    REQUIRE( binary.data( Qt::CheckStateRole ) == Qt::Checked );
    // A folder cannot be checked with select "files"; a patch sets the checked paths.
    REQUIRE_FALSE( model->flags( model->index( 0, 0 ) ).testFlag( Qt::ItemIsUserCheckable ) );
    fixture.patch( QJsonObject{ { "id", "files" }, { "set", QJsonObject{ { "value", QJsonArray{} } } } } );
    REQUIRE( binary.data( Qt::CheckStateRole ) == Qt::Unchecked );
    REQUIRE( behaviour->value( 10 ).isEmpty() );
}

namespace {
QJsonObject e17eManifest( const QString& executable )
{
    return QJsonObject{
        { "id", PluginId },
        { "name", "E17e fixture" },
        { "version", "1.0.0" },
        { "protocol_version", 1 },
        { "executable", executable },
        { "capabilities", QJsonArray{ "tool" } },
        { "handles", QJsonArray{ "tree/children", "tooltip/resolve", "query/cancel", "ui/command" } },
        { "actions", QJsonArray{ QJsonObject{ { "id", "where" }, { "capability", "tool" } } } },
        { "views", QJsonArray{ QJsonObject{ { "id", "trees" }, { "title", "Trees" } } } },
        { "menus",
          QJsonArray{ QJsonObject{
              { "id", "nodes_menu" },
              { "location", "element" },
              { "items", QJsonArray{ QJsonObject{ { "id", "reset" }, { "title", "Reset" },
                                                  { "event", true } },
                                     QJsonObject{ { "id", "where" }, { "title", "Where" },
                                                  { "action", "where" } } } } } } } };
}

QList<QJsonObject> recorded( const QString& folder, const QString& method )
{
    QList<QJsonObject> result;
    QFile log( QDir( folder ).filePath( "host-messages.jsonl" ) );
    if ( !log.open( QIODevice::ReadOnly ) ) {
        return result;
    }
    for ( const auto& line : log.readAll().split( '\n' ) ) {
        const auto frame = QJsonDocument::fromJson( line ).object();
        if ( frame.value( "method" ) == method ) {
            result.append( frame.value( "params" ).toObject() );
        }
    }
    return result;
}

// A MainWindow with the fixture plugin's `trees` view open.
struct TreesWindow {
    QTemporaryDir temp;
    QString folder;
    std::unique_ptr<PluginManager> manager;
    std::unique_ptr<MainWindow> window;
    CommanderRequest invoked; // the last plugin action a menu entry ran

    TreesWindow()
    {
        PluginMenuRegistry::instance().forgetPlugin( PluginId );
        const auto shipped = QDir( temp.path() ).filePath( "shipped" );
        folder = QDir( shipped ).filePath( PluginId );
        REQUIRE( QDir().mkpath( folder ) );
        const auto executable = QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName();
        QFile file( QDir( folder ).filePath( "plugin.json" ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( QJsonDocument( e17eManifest( executable ) ).toJson() );
        file.close();
        REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                              QDir( folder ).filePath( executable ) ) );
        manager = std::make_unique<PluginManager>( QDir( temp.path() ).filePath( "data" ), shipped );
        REQUIRE( manager->manage( "enable", PluginId, {} ).ok() );
        auto session = std::make_shared<Session>();
        window = std::make_unique<MainWindow>( WindowSession{ session, "PluginSdkE17e", 0 } );
        window->setPluginManager( *manager, [ this ]( const CommanderRequest& request ) {
            if ( request.action == CommanderAction::InvokePlugin ) {
                invoked = request; // what the host asks for; the job itself is not run here
                return commanderSuccess( {}, {} );
            }
            const auto result = manager->manage( request.pluginOperation, request.pluginId,
                                                 request.pluginArguments );
            return result.ok()
                       ? commanderSuccess( {}, result.payload )
                       : commanderFailure( CommanderResultCode::ExecutionFailed, result.error );
        } );
        window->show();
        REQUIRE( waitFor( [ & ] { return host() != nullptr; } ) );
        QString error;
        REQUIRE( host()->openView( PluginId, "trees", {}, &error ) );
        REQUIRE( waitFor( [ & ] { return tree() && tree()->model()->rowCount() == 5; }, 10000 ) );
        REQUIRE( waitFor( [ & ] { return view()->isEnabled(); } ) );
    }
    ~TreesWindow()
    {
        Q_EMIT window->windowClosed();
        QCoreApplication::processEvents();
    }
    PluginViewHost* host() const
    {
        return window->findChild<PluginViewHost*>();
    }
    PluginView* view() const
    {
        return host() ? host()->view( PluginId, "trees" ) : nullptr;
    }
    QTreeView* tree() const
    {
        return view() ? qobject_cast<QTreeView*>( view()->element( "nodes" ) ) : nullptr;
    }
    pluginview::PluginTreeModel* model() const
    {
        return dynamic_cast<pluginview::PluginTreeModel*>( tree()->model() );
    }
    CommanderResult run( CommanderRequest request )
    {
        return window->executeCommanderRequest( request );
    }
};
} // namespace

TEST_CASE( "E17e a plugin answers tree/children; element menus and automation take node ids",
           "[ui][workbench][E17]" )
{
    TreesWindow fixture;
    auto* tree = fixture.tree();
    auto* model = fixture.model();
    // Children with nested children, a list alone, an invalid answer and no children.
    tree->expand( model->indexOf( "a" ) );
    REQUIRE( waitFor( [ & ] { return model->indexOf( "a1m" ).isValid(); }, 10000 ) );
    REQUIRE( model->rowCount( model->indexOf( "a" ) ) == 2 );
    tree->expand( model->indexOf( "b" ) );
    REQUIRE( waitFor( [ & ] { return model->indexOf( "b1" ).isValid(); }, 10000 ) );
    tree->expand( model->indexOf( "bad" ) );
    REQUIRE( waitFor( [ & ] {
        return model->index( 0, 0, model->indexOf( "bad" ) ).data().toString() == "Could not load";
    } ) );
    tree->expand( model->indexOf( "empty" ) );
    REQUIRE( waitFor( [ & ] { return !model->hasChildren( model->indexOf( "empty" ) ); } ) );
    // Children below a checked node follow it (auto_tristate).
    // The GUI menu: host entries, then the plugin's; picking one sends ui/command with the node.
    std::unique_ptr<QMenu> menu( fixture.view()->buildElementMenu(
        "nodes", tree->visualRect( model->indexOf( "a1" ) ).center() ) );
    REQUIRE( menu );
    auto* reset = menu->findChild<QAction*>( PluginMenus::entryObjectName( PluginId, "nodes_menu", "reset" ) );
    REQUIRE( reset );
    reset->trigger();
    REQUIRE( waitFor( [ & ] { return !recorded( fixture.folder, "ui/command" ).isEmpty(); } ) );
    auto context = recorded( fixture.folder, "ui/command" ).last().value( "context" ).toObject();
    REQUIRE( context.value( "element_id" ) == "nodes" );
    REQUIRE( context.value( "node_id" ) == "a1" );
    REQUIRE_FALSE( context.contains( "row_id" ) );
    REQUIRE( context.value( "selected_ids" ).toArray() == QJsonArray{ "a1" } );
    // Automation: --node-id for get_plugin_menus, trigger_plugin_menu and get_plugin_tooltip.
    CommanderRequest get;
    get.action = CommanderAction::GetPluginMenus;
    get.pluginId = PluginId;
    get.viewId = "trees";
    get.pluginArguments = { { "location", "element" }, { "elementId", "nodes" }, { "nodeId", "a2" } };
    const auto entries = fixture.run( get );
    REQUIRE( entries.ok() );
    REQUIRE( entries.payload.value( "context" ).toMap().value( "node_id" ) == "a2" );
    CommanderRequest trigger;
    trigger.action = CommanderAction::TriggerPluginMenu;
    trigger.pluginId = PluginId;
    trigger.viewId = "trees";
    trigger.pluginArguments = { { "menuId", "nodes_menu" }, { "itemId", "reset" },
                                { "elementId", "nodes" }, { "nodeId", "b1" } };
    REQUIRE( fixture.run( trigger ).ok() );
    REQUIRE( waitFor( [ & ] { return recorded( fixture.folder, "ui/command" ).size() == 2; } ) );
    REQUIRE( recorded( fixture.folder, "ui/command" ).last().value( "context" ).toObject().value( "node_id" )
             == "b1" );
    trigger.pluginArguments.insert( "nodeId", "ghost" );
    REQUIRE_FALSE( fixture.run( trigger ).ok() );
    CommanderRequest tip;
    tip.action = CommanderAction::GetPluginTooltip;
    tip.pluginId = PluginId;
    tip.viewId = "trees";
    tip.pluginArguments = { { "elementId", "nodes" }, { "nodeId", "a1" }, { "columnId", "state" },
                            { "noCache", true } };
    const auto tooltip = fixture.run( tip );
    REQUIRE( tooltip.ok() );
    REQUIRE( tooltip.payload.value( "source" ) == "plugin" );
    REQUIRE( tooltip.payload.value( "tooltip" ).toMap().value( "text" ).toString().startsWith(
        "node a1/state" ) );
    // An `action` entry's job gets the node in its `menu` context, as `event` entries do.
    trigger.pluginArguments = { { "menuId", "nodes_menu" }, { "itemId", "where" },
                                { "elementId", "nodes" }, { "nodeId", "b1" } };
    REQUIRE( fixture.run( trigger ).ok() );
    REQUIRE( fixture.invoked.pluginAction == "where" );
    const auto job = fixture.invoked.pluginMenuContext;
    REQUIRE( job.value( "element_id" ).toString() == "nodes" );
    REQUIRE( job.value( "node_id" ).toString() == "b1" );
    REQUIRE( job.value( "item_id" ).toString() == "where" );
    REQUIRE_FALSE( job.contains( "row_id" ) );
    // A check box change reaches the plugin.
    REQUIRE( model->setData( model->indexOf( "a2" ), Qt::Checked, Qt::CheckStateRole ) );
    REQUIRE( waitFor( [ & ] {
        const auto events = recorded( fixture.folder, "view/event" );
        return std::any_of( events.begin(), events.end(), []( const QJsonObject& event ) {
            return event.value( "type" ) == "checked"
                   && event.value( "value" ).toObject().value( "node_id" ) == "a2";
        } );
    } ) );
}

TEST_CASE( "E17e a Shift+click range reaches past Loading lines at 50,000 nodes",
           "[ui][workbench][E17]" )
{
    // 50,000 nodes; two lazy nodes between the anchor and the target show "Loading…" lines,
    // which are no nodes: the range still reaches the target and selects every node.
    QJsonArray nodes{ QJsonObject{ { "id", "L1" }, { "text", "L1" }, { "has_children", true } } };
    for ( int at = 0; at < cilogg::plugins::MaxTreeNodes - 3; ++at ) {
        nodes.append( QJsonObject{ { "id", QString( "n%1" ).arg( at ) }, { "text", "Node" } } );
    }
    nodes.append( QJsonObject{ { "id", "L2" }, { "text", "L2" }, { "has_children", true } } );
    nodes.append( QJsonObject{ { "id", "T" }, { "text", "T" } } );
    View fixture( QJsonObject{ { "type", "tree" },
                               { "id", "tr" },
                               { "lazy", true },
                               { "selection", "multi" },
                               { "toggle_selection", true },
                               { "nodes", nodes } } );
    REQUIRE( fixture.model()->nodeCount() == cilogg::plugins::MaxTreeNodes );
    auto* tree = fixture.tree();
    tree->expand( fixture.index( "L1" ) );
    tree->expand( fixture.index( "L2" ) );
    REQUIRE( fixture.model()->isLoading( "L1" ) );
    REQUIRE( fixture.model()->isLoading( "L2" ) );
    tree->scrollTo( fixture.index( "L1" ) );
    QCoreApplication::processEvents();
    QTest::mouseClick( tree->viewport(), Qt::LeftButton, {}, fixture.center( "L1" ) );
    tree->scrollTo( fixture.index( "T" ) );
    QCoreApplication::processEvents();
    QTest::mouseClick( tree->viewport(), Qt::LeftButton, Qt::ShiftModifier, fixture.center( "T" ) );
    int count = 0;
    fixture.behaviour()->selectedNodeIds( 1, &count );
    REQUIRE( count == cilogg::plugins::MaxTreeNodes );
}

TEST_CASE( "E17e boxes need checkboxes, collapsing cancels a load, new columns parse nodes again",
           "[ui][workbench][E17]" )
{
    // A node's `checkable` applies only in a tree with `checkboxes`.
    View plain( QJsonObject{
        { "type", "tree" },
        { "id", "tr" },
        { "nodes", QJsonArray{ QJsonObject{
                       { "id", "x" }, { "text", "X" }, { "checkable", true }, { "checked", true } } } } } );
    REQUIRE_FALSE( plain.model()->flags( plain.index( "x" ) ).testFlag( Qt::ItemIsUserCheckable ) );
    REQUIRE_FALSE( plain.index( "x" ).data( Qt::CheckStateRole ).isValid() );

    // Collapsing a node that is still loading cancels its query: its slot goes to a waiting
    // one, a late answer is dropped and the next expand asks again.
    QJsonArray lazyNodes;
    for ( int at = 0; at < 5; ++at ) {
        lazyNodes.append( QJsonObject{
            { "id", QString( "n%1" ).arg( at ) }, { "text", "Node" }, { "has_children", true } } );
    }
    View lazy( QJsonObject{ { "type", "tree" }, { "id", "tr" }, { "lazy", true }, { "nodes", lazyNodes } } );
    for ( int at = 0; at < 5; ++at ) {
        lazy.tree()->expand( lazy.index( QString( "n%1" ).arg( at ) ) );
    }
    REQUIRE( lazy.children.size() == 4 );
    REQUIRE( lazy.queryOf( "n4" ).isEmpty() ); // waits
    const auto first = lazy.queryOf( "n0" );
    REQUIRE_FALSE( first.isEmpty() );
    const auto late = lazy.children.at( first ).done;
    lazy.tree()->collapse( lazy.index( "n0" ) );
    REQUIRE( lazy.cancelled == QStringList{ first } );
    REQUIRE_FALSE( lazy.queryOf( "n4" ).isEmpty() ); // the freed slot went to the waiting one
    REQUIRE( lazy.model()->rowCount( lazy.index( "n0" ) ) == 0 );
    REQUIRE_FALSE( lazy.model()->isLoading( "n0" ) );
    REQUIRE( lazy.model()->hasChildren( lazy.index( "n0" ) ) );
    late( true, QJsonArray{ QJsonObject{ { "id", "late" } } } );
    REQUIRE_FALSE( lazy.index( "late" ).isValid() );
    lazy.tree()->expand( lazy.index( "n0" ) );
    REQUIRE( lazy.model()->isLoading( "n0" ) ); // asks again (it waits for a slot)

    // A root whose columns change with the same nodes parses them again: a column that comes
    // back shows its cells.
    const QJsonArray twoCells{ treeNode( "r", { "left", "right" } ) };
    const auto rootWith = [ & ]( int columnCount ) {
        QJsonArray columns{ QJsonObject{ { "id", "a" } } };
        if ( columnCount > 1 ) {
            columns.append( QJsonObject{ { "id", "b" } } );
        }
        return QJsonObject{ { "type", "column" },
                            { "id", "body" },
                            { "children", QJsonArray{ QJsonObject{ { "type", "tree" },
                                                                   { "id", "tr" },
                                                                   { "columns", columns },
                                                                   { "nodes", twoCells } } } } };
    };
    View cells( rootWith( 2 ).value( "children" ).toArray().first().toObject() );
    REQUIRE( cells.index( "r", 1 ).data().toString() == "right" );
    cells.view.setRoot( rootWith( 1 ) );
    REQUIRE( cells.model()->columnCount() == 1 );
    cells.view.setRoot( rootWith( 2 ) );
    REQUIRE( cells.model()->columnCount() == 2 );
    REQUIRE( cells.index( "r", 1 ).data().toString() == "right" );

    // A tree whose first column was hidden shows its one implicit column once the columns go.
    const auto hiddenFirst = []( const QJsonArray& columns ) {
        return QJsonObject{ { "type", "column" },
                            { "id", "body" },
                            { "children", QJsonArray{ QJsonObject{ { "type", "tree" },
                                                                   { "id", "tr" },
                                                                   { "columns", columns },
                                                                   { "nodes", QJsonArray{ treeNode( "r", { "left" } ) } } } } } };
    };
    View implicit( hiddenFirst( QJsonArray{ QJsonObject{ { "id", "a" }, { "hidden", true } },
                                            QJsonObject{ { "id", "b" } } } )
                       .value( "children" )
                       .toArray()
                       .first()
                       .toObject() );
    REQUIRE( implicit.tree()->isColumnHidden( 0 ) );
    implicit.view.setRoot( hiddenFirst( QJsonArray{} ) );
    REQUIRE( implicit.model()->columnCount() == 1 );
    REQUIRE_FALSE( implicit.tree()->isColumnHidden( 0 ) );
}

TEST_CASE( "E17e a double-click on a check box does not activate; update_nodes over the limit "
           "changes nothing",
           "[ui][workbench][E17]" )
{
    View fixture( QJsonObject{
        { "type", "tree" },
        { "id", "tr" },
        { "checkboxes", true },
        { "nodes", QJsonArray{ QJsonObject{ { "id", "x" }, { "text", "Node x" } } } } } );
    auto* tree = fixture.tree();
    const auto index = fixture.index( "x" );
    // The check box where the item delegate puts it, and a point on the text beside it.
    QStyleOptionViewItem option;
    option.initFrom( tree );
    option.rect = tree->visualRect( index );
    option.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
    option.text = index.data().toString();
    const auto box
        = tree->style()->subElementRect( QStyle::SE_ItemViewItemCheckIndicator, &option, tree );
    REQUIRE( option.rect.contains( box.center() ) );
    const QPoint text( box.right() + 24, box.center().y() );
    REQUIRE( option.rect.contains( text ) );
    QTest::mouseClick( tree->viewport(), Qt::LeftButton, {}, box.center() );
    REQUIRE( fixture.countOf( "checked" ) == 1 );
    QTest::mouseDClick( tree->viewport(), Qt::LeftButton, {}, box.center() );
    QTest::qWait( 20 );
    REQUIRE( fixture.countOf( "activated" ) == 0 );
    QTest::mouseClick( tree->viewport(), Qt::LeftButton, {}, text );
    QTest::mouseDClick( tree->viewport(), Qt::LeftButton, {}, text );
    REQUIRE( waitFor( [ & ] { return fixture.countOf( "activated" ) == 1; } ) );
    // Enter activates the current node (Space checks it).
    tree->setCurrentIndex( index );
    tree->setFocus();
    QTest::keyClick( tree, Qt::Key_Return );
    REQUIRE( fixture.countOf( "activated" ) == 2 );

    // An update that would pass 50,000 nodes changes nothing, not even the keys before its
    // `children`; one that reaches 50,000 exactly is taken.
    QJsonArray many;
    for ( int at = 0; at < cilogg::plugins::MaxTreeNodes - 1; ++at ) {
        many.append( QJsonObject{ { "id", QString( "n%1" ).arg( at ) }, { "text", "Node" } } );
    }
    View large( QJsonObject{ { "type", "tree" }, { "id", "tr" }, { "nodes", many } } );
    REQUIRE( large.model()->nodeCount() == cilogg::plugins::MaxTreeNodes - 1 );
    const auto update = [ & ]( const QJsonArray& children ) {
        large.patch( QJsonObject{
            { "id", "tr" },
            { "update_nodes", QJsonArray{ QJsonObject{ { "id", "n0" },
                                                       { "text", "Changed" },
                                                       { "children", children } } } } } );
    };
    update( QJsonArray{ QJsonObject{ { "id", "c1" } }, QJsonObject{ { "id", "c2" } } } );
    REQUIRE( large.index( "n0" ).data().toString() == "Node" );
    REQUIRE_FALSE( large.index( "c1" ).isValid() );
    REQUIRE( large.model()->nodeCount() == cilogg::plugins::MaxTreeNodes - 1 );
    update( QJsonArray{ QJsonObject{ { "id", "c1" } } } );
    REQUIRE( large.index( "n0" ).data().toString() == "Changed" );
    REQUIRE( large.index( "c1" ).isValid() );
    REQUIRE( large.model()->nodeCount() == cilogg::plugins::MaxTreeNodes );

    // Entries count in order: a node emptied first and its ancestor replaced next count the
    // ancestor's nodes as they are by then (1,000 outside, A with D with 1,000 children).
    QJsonArray outside;
    QJsonArray leaves;
    for ( int at = 0; at < 1000; ++at ) {
        outside.append( QJsonObject{ { "id", QString( "o%1" ).arg( at ) } } );
        leaves.append( QJsonObject{ { "id", QString( "g%1" ).arg( at ) } } );
    }
    outside.append( QJsonObject{
        { "id", "A" },
        { "text", "A" },
        { "children", QJsonArray{ QJsonObject{ { "id", "D" }, { "children", leaves } } } } } );
    View nested( QJsonObject{ { "type", "tree" }, { "id", "tr" }, { "nodes", outside } } );
    REQUIRE( nested.model()->nodeCount() == 2002 );
    const auto emptyThenReplace = [ & ]( int count ) {
        QJsonArray fresh;
        for ( int at = 0; at < count; ++at ) {
            fresh.append( QJsonObject{ { "id", QString( "f%1" ).arg( at ) } } );
        }
        nested.patch( QJsonObject{
            { "id", "tr" },
            { "update_nodes",
              QJsonArray{ QJsonObject{ { "id", "D" }, { "children", QJsonArray{} } },
                          QJsonObject{ { "id", "A" }, { "text", "Changed" }, { "children", fresh } } } } } );
    };
    // 1,002 nodes once D is empty, then A's one child (D) gives way to 49,000: 50,001.
    emptyThenReplace( cilogg::plugins::MaxTreeNodes - 1000 );
    REQUIRE( nested.model()->nodeCount() == 2002 );
    REQUIRE( nested.model()->rowCount( nested.index( "D" ) ) == 1000 );
    REQUIRE( nested.index( "A" ).data().toString() == "A" );
    emptyThenReplace( cilogg::plugins::MaxTreeNodes - 1001 ); // 50,000
    REQUIRE( nested.model()->nodeCount() == cilogg::plugins::MaxTreeNodes );
    REQUIRE_FALSE( nested.index( "D" ).isValid() );
    REQUIRE( nested.index( "A" ).data().toString() == "Changed" );

    // A node that an earlier entry gives again, with its id, counts its new children in full.
    View again( QJsonObject{
        { "type", "tree" },
        { "id", "tr" },
        { "nodes", QJsonArray{ QJsonObject{
                       { "id", "A" },
                       { "text", "A" },
                       { "children", QJsonArray{ QJsonObject{ { "id", "X" }, { "text", "x" } } } } } } } } );
    const auto replaceThenGrow = [ & ]( int count ) {
        QJsonArray fresh;
        for ( int at = 0; at < count; ++at ) {
            fresh.append( QJsonObject{ { "id", QString( "y%1" ).arg( at ) } } );
        }
        again.patch( QJsonObject{
            { "id", "tr" },
            { "update_nodes",
              QJsonArray{ QJsonObject{ { "id", "A" },
                                       { "children", QJsonArray{ QJsonObject{ { "id", "X" },
                                                                              { "text", "x" } } } } },
                          QJsonObject{ { "id", "X" }, { "text", "X2" }, { "children", fresh } } } } } );
    };
    replaceThenGrow( cilogg::plugins::MaxTreeNodes - 1 ); // 2 + 49,999
    REQUIRE( again.model()->nodeCount() == 2 );
    REQUIRE( again.index( "X" ).data().toString() == "x" );
    replaceThenGrow( cilogg::plugins::MaxTreeNodes - 2 ); // 50,000
    REQUIRE( again.model()->nodeCount() == cilogg::plugins::MaxTreeNodes );
    REQUIRE( again.index( "X" ).data().toString() == "X2" );

    // A "Loading…" line is no node: at 50,000 nodes, a loading node given one child and a new
    // text changes nothing.
    QJsonArray full;
    for ( int at = 0; at < cilogg::plugins::MaxTreeNodes - 1; ++at ) {
        full.append( QJsonObject{ { "id", QString( "p%1" ).arg( at ) } } );
    }
    full.append( QJsonObject{ { "id", "L" }, { "text", "L" }, { "has_children", true } } );
    View loading( QJsonObject{ { "type", "tree" }, { "id", "tr" }, { "lazy", true }, { "nodes", full } } );
    REQUIRE( loading.model()->nodeCount() == cilogg::plugins::MaxTreeNodes );
    loading.tree()->expand( loading.index( "L" ) );
    REQUIRE( loading.model()->isLoading( "L" ) );
    loading.patch( QJsonObject{
        { "id", "tr" },
        { "update_nodes",
          QJsonArray{ QJsonObject{ { "id", "L" },
                                   { "text", "Changed" },
                                   { "children", QJsonArray{ QJsonObject{ { "id", "c" } } } } } } } } );
    REQUIRE( loading.index( "L" ).data().toString() == "L" );
    REQUIRE_FALSE( loading.index( "c" ).isValid() );
    REQUIRE( loading.model()->nodeCount() == cilogg::plugins::MaxTreeNodes );
}

namespace {
// A directory link: a junction on Windows (it needs no rights), a symbolic link elsewhere.
bool linkDirectory( const QString& target, const QString& link )
{
#ifdef Q_OS_WIN
    QProcess process;
    process.start( QStringLiteral( "cmd.exe" ),
                   { QStringLiteral( "/c" ), QStringLiteral( "mklink" ), QStringLiteral( "/J" ),
                     QDir::toNativeSeparators( link ), QDir::toNativeSeparators( target ) } );
    return process.waitForFinished( 10000 ) && process.exitCode() == 0
           && QFileInfo( link ).isJunction();
#else
    return QFile::link( target, link );
#endif
}

// Removes a directory link made by linkDirectory(), never what it points to.
bool unlinkDirectory( const QString& link )
{
#ifdef Q_OS_WIN
    return QDir().rmdir( link );
#else
    return QFile::remove( link );
#endif
}
} // namespace

TEST_CASE( "E17e file trees keep links inside their roots, count selections, check what can be "
           "checked and retry network roots",
           "[ui][workbench][E17]" )
{
    QTemporaryDir temp;
    const auto base = QDir::fromNativeSeparators( QDir::cleanPath( temp.path() ) );
    const auto root = base + "/root";
    const auto outside = base + "/outside";
    REQUIRE( QDir().mkpath( root + "/inside" ) );
    REQUIRE( QDir().mkpath( root + "/many" ) );
    REQUIRE( QDir().mkpath( outside ) );
    const auto touch = []( const QString& path ) {
        QFile file( path );
        return file.open( QIODevice::WriteOnly ) && file.write( "x" ) == 1;
    };
    REQUIRE( touch( root + "/inside/x.bin" ) );
    REQUIRE( touch( root + "/b.txt" ) );
    REQUIRE( touch( outside + "/secret.bin" ) );
    for ( int at = 0; at < 30; ++at ) {
        REQUIRE( touch( root + QString( "/many/f%1.bin" ).arg( at, 2, 10, QChar( '0' ) ) ) );
    }
    const bool linked = linkDirectory( outside, root + "/out" )
                        && linkDirectory( root + "/inside", root + "/in" );
    View fixture( QJsonObject{ { "type", "file_tree" },
                               { "id", "files" },
                               { "roots", QJsonArray{ root } },
                               { "selection", "multi" },
                               { "context_menu", "files_menu" } } );
    auto* tree = fixture.tree( "files" );
    auto* behaviour = tree->findChild<PluginFileTreeBehaviour*>();
    auto* model = behaviour->model();
    // Persistent: the native model sorts its rows later on, and plain indexes would go stale.
    const QPersistentModelIndex top( model->index( 0, 0 ) );
    tree->expand( top );
    const auto child = [ & ]( const QModelIndex& parent, const QString& name ) {
        for ( int row = 0; row < model->rowCount( parent ); ++row ) {
            if ( model->index( row, 0, parent ).data().toString() == name ) {
                return model->index( row, 0, parent );
            }
        }
        return QModelIndex{};
    };
    REQUIRE( waitFor(
        [ & ] { return child( top, "many" ).isValid() && ( !linked || child( top, "out" ).isValid() ); },
        FileSystemWait ) );
    if ( linked ) {
        // A link that leads out of its root shows disabled and never opens; nothing through it is
        // found, revealed, opened or selected.
        const QPersistentModelIndex out( child( top, "out" ) );
        REQUIRE( model->flags( out ) == Qt::NoItemFlags );
        REQUIRE_FALSE( model->hasChildren( out ) );
        tree->expand( out );
        fixture.patch( QJsonObject{ { "id", "files" }, { "expand", QJsonArray{ root + "/out" } } } );
        QTest::qWait( 200 );
        REQUIRE( model->rowCount( out ) == 0 );
        REQUIRE_FALSE( model->indexOfPath( root + "/out" ).isValid() );
        REQUIRE_FALSE( model->indexOfPath( root + "/out/secret.bin" ).isValid() );
        fixture.patch( QJsonObject{
            { "id", "files" },
            { "set", QJsonObject{ { "value", QJsonArray{ root + "/out/secret.bin" } } } } } );
        REQUIRE( behaviour->value( 10 ).isEmpty() );
        // Tooltips and menus never name it or what lies through it; a right-click on it names
        // no item.
        QString error;
        QString menuId;
        QJsonObject target;
        for ( const auto& path : { root + "/out", root + "/out/secret.bin" } ) {
            REQUIRE_FALSE( fixture.view.resolveTooltip( "files", {}, {}, path, false, {}, &error ) );
            REQUIRE_FALSE( fixture.view.elementMenu( "files", {}, path, &menuId, &target, &error ) );
        }
        tree->scrollTo( out );
        QCoreApplication::processEvents();
        const auto at = tree->visualRect( out ).center();
        REQUIRE( tree->visualRect( out ).isValid() );
        REQUIRE( tree->indexAt( at ).sibling( tree->indexAt( at ).row(), 0 ) == QModelIndex( out ) );
        std::unique_ptr<QMenu> menu( fixture.view.buildElementMenu( "files", at ) );
        REQUIRE( menu );
        INFO( "node_id " << fixture.menuTarget.value( "node_id" ).toString().toStdString() );
        REQUIRE( fixture.menuTarget.value( "node_id" ).isNull() );
        // One that stays inside the root opens like its target.
        REQUIRE( model->hasChildren( child( top, "in" ) ) );
        REQUIRE( model->indexOfPath( root + "/in/x.bin" ).isValid() );
        REQUIRE( fixture.view.elementMenu( "files", {}, root + "/in/x.bin", &menuId, &target, &error ) );
        REQUIRE( target.value( "node_id" ) == root + "/in/x.bin" );

        // A path checked through a link that stays inside the root is dropped (and the plugin
        // hears it) once the link leads out of the root.
        REQUIRE( linkDirectory( root + "/inside", root + "/hop" ) );
        View hop( QJsonObject{ { "type", "file_tree" },
                               { "id", "files" },
                               { "roots", QJsonArray{ root } },
                               { "checkboxes", true },
                               { "select", "files" } } );
        auto* hopBehaviour = hop.tree( "files" )->findChild<PluginFileTreeBehaviour*>();
        hop.patch( QJsonObject{ { "id", "files" },
                                { "set", QJsonObject{ { "value", QJsonArray{ root + "/hop/x.bin" } } } } } );
        REQUIRE( hopBehaviour->value( 10 ) == QStringList{ root + "/hop/x.bin" } );
        const auto heard = hop.countOf( "selected" );
        REQUIRE( unlinkDirectory( root + "/hop" ) );
        REQUIRE( linkDirectory( outside, root + "/hop" ) );
        // The native model may see it as the item changing: tell it so at once, as it would.
        auto* files = hopBehaviour->findChild<QFileSystemModel*>();
        REQUIRE( files );
        const auto item = files->index( root + "/hop" );
        if ( item.isValid() ) {
            Q_EMIT files->dataChanged( item, item );
        }
        REQUIRE( waitFor( [ & ] { return hopBehaviour->value( 10 ).isEmpty(); }, FileSystemWait ) );
        REQUIRE( waitFor( [ & ] { return hop.countOf( "selected" ) > heard; } ) );
        REQUIRE( hop.lastOf( "selected" ).value( "value" ).toArray().isEmpty() );

        // A root that is itself a link is what it leads to now: once it leads elsewhere, the
        // links below it are judged against the new place.
        const auto targetA = base + "/rootA";
        const auto targetB = base + "/rootB";
        const auto rootLink = base + "/rootLink";
        REQUIRE( QDir().mkpath( targetA ) );
        REQUIRE( QDir().mkpath( targetB ) );
        REQUIRE( touch( targetA + "/a.bin" ) );
        REQUIRE( touch( targetB + "/b.bin" ) );
        REQUIRE( linkDirectory( targetA, targetB + "/toA" ) );
        REQUIRE( linkDirectory( targetA, rootLink ) );
        View moved( QJsonObject{ { "type", "file_tree" }, { "id", "files" }, { "roots", QJsonArray{ rootLink } } } );
        auto* movedBehaviour = moved.tree( "files" )->findChild<PluginFileTreeBehaviour*>();
        auto* movedModel = movedBehaviour->model();
        REQUIRE( movedModel->reaches( rootLink + "/a.bin" ) );
        REQUIRE( unlinkDirectory( rootLink ) );
        REQUIRE( linkDirectory( targetB, rootLink ) );
        auto* movedFiles = movedBehaviour->findChild<QFileSystemModel*>();
        REQUIRE( movedFiles );
        const auto rootItem = movedFiles->index( rootLink );
        REQUIRE( rootItem.isValid() );
        Q_EMIT movedFiles->dataChanged( rootItem, rootItem ); // as the native model reports it
        REQUIRE( waitFor( [ & ] { return movedModel->reaches( rootLink + "/b.bin" ); } ) );
        REQUIRE_FALSE( movedModel->reaches( rootLink + "/toA" ) );
        REQUIRE_FALSE( movedModel->reaches( rootLink + "/toA/a.bin" ) );

        // A root that is a link whose target goes leads nowhere: it shows missing, what was
        // checked in it goes (the plugin hears it), and it comes back by itself with the target.
        const auto vanishing = base + "/vanishing";
        const auto vanishingLink = base + "/vanishingLink";
        REQUIRE( QDir().mkpath( vanishing ) );
        REQUIRE( touch( vanishing + "/v.bin" ) );
        REQUIRE( linkDirectory( vanishing, vanishingLink ) );
        View lost( QJsonObject{ { "type", "file_tree" },
                                { "id", "files" },
                                { "roots", QJsonArray{ vanishingLink } },
                                { "checkboxes", true },
                                { "select", "files" } } );
        auto* lostBehaviour = lost.tree( "files" )->findChild<PluginFileTreeBehaviour*>();
        auto* lostModel = lostBehaviour->model();
        REQUIRE( lostModel->isAttached( 0 ) );
        lost.patch( QJsonObject{
            { "id", "files" },
            { "set", QJsonObject{ { "value", QJsonArray{ vanishingLink + "/v.bin" } } } } } );
        REQUIRE( lostBehaviour->value( 10 ) == QStringList{ vanishingLink + "/v.bin" } );
        const auto heardLost = lost.countOf( "selected" );
        if ( QDir( vanishing ).removeRecursively() ) {
            auto* lostFiles = lostBehaviour->findChild<QFileSystemModel*>();
            REQUIRE( lostFiles );
            const auto lostItem = lostFiles->index( vanishingLink );
            if ( lostItem.isValid() ) {
                Q_EMIT lostFiles->dataChanged( lostItem, lostItem ); // as the native model reports it
            }
            REQUIRE( waitFor( [ & ] { return !lostModel->isAttached( 0 ); }, FileSystemWait ) );
            const auto lostRoot = lostModel->index( 0, 0 );
            REQUIRE_FALSE( lostModel->hasChildren( lostRoot ) );
            REQUIRE_FALSE( lostModel->flags( lostRoot ).testFlag( Qt::ItemIsSelectable ) );
            REQUIRE_FALSE( lostModel->reaches( vanishingLink ) );
            REQUIRE_FALSE( lostModel->reaches( vanishingLink + "/v.bin" ) );
            REQUIRE( waitFor( [ & ] { return lostBehaviour->value( 10 ).isEmpty(); } ) );
            REQUIRE( waitFor( [ & ] { return lost.countOf( "selected" ) > heardLost; } ) );
            // The folder may stay delete-pending for a moment while a watcher lets it go.
            REQUIRE( waitFor( [ & ] { return QDir().mkpath( vanishing ); }, FileSystemWait ) );
            REQUIRE( touch( vanishing + "/w.bin" ) );
            REQUIRE( waitFor( [ & ] { return lostModel->isAttached( 0 ); }, FileSystemWait ) );
            REQUIRE( lostModel->reaches( vanishingLink ) );
            REQUIRE( lostModel->reaches( vanishingLink + "/w.bin" ) );
        }
        else {
            WARN( "The link's target could not be deleted here: the dangling root checks are skipped." );
        }
    }
    else {
        WARN( "No directory links can be made here: the link checks are skipped." );
    }
    // A missing path has no tooltip and no menu either.
    {
        QString error;
        QString menuId;
        QJsonObject target;
        REQUIRE_FALSE( fixture.view.resolveTooltip( "files", {}, {}, root + "/nope.bin", false, {}, &error ) );
        REQUIRE_FALSE( fixture.view.elementMenu( "files", {}, root + "/nope.bin", &menuId, &target, &error ) );
        REQUIRE( fixture.view.elementMenu( "files", {}, root + "/b.txt", &menuId, &target, &error ) );
    }

    // A selection is counted without a path for every row; only the first ones, in shown order,
    // get one, and a row selected twice counts once.
    const QPersistentModelIndex folder( child( top, "many" ) );
    tree->expand( folder );
    REQUIRE( waitFor( [ & ] { return model->rowCount( folder ) == 30; }, FileSystemWait ) );
    auto* selection = tree->selectionModel();
    selection->select( QItemSelection( model->index( 0, 0, folder ), model->index( 29, 0, folder ) ),
                       QItemSelectionModel::Select | QItemSelectionModel::Rows );
    selection->select( QItemSelection( model->index( 5, 0, folder ), model->index( 9, 0, folder ) ),
                       QItemSelectionModel::Select | QItemSelectionModel::Rows );
    selection->select( top, QItemSelectionModel::Select | QItemSelectionModel::Rows );
    int count = 0;
    REQUIRE( behaviour->value( 3, &count )
             == QStringList{ root, model->pathOf( model->index( 0, 0, folder ) ),
                             model->pathOf( model->index( 1, 0, folder ) ) } );
    REQUIRE( count == 31 );
    REQUIRE( fixture.lastOf( "selected" ).value( "selected_count" ) == 31 );
    // Copy Path takes the same paths in the same order.
    const auto copied = behaviour->copyText().split( QLatin1Char( '\n' ) );
    REQUIRE( copied.size() == 31 );
    REQUIRE( copied.front() == QDir::toNativeSeparators( root ) );
    REQUIRE( copied.at( 1 ) == QDir::toNativeSeparators( model->pathOf( model->index( 0, 0, folder ) ) ) );
    REQUIRE( behaviour->canCopy() );

    // Toggle selection: a Shift+click selects every shown item from the last click (as ranges,
    // with no limit of its own).
    View toggle( QJsonObject{ { "type", "file_tree" },
                              { "id", "files" },
                              { "roots", QJsonArray{ root + "/many" } },
                              { "selection", "multi" },
                              { "toggle_selection", true },
                              { "select", "files" } } );
    auto* toggleTree = toggle.tree( "files" );
    auto* toggleBehaviour = toggleTree->findChild<PluginFileTreeBehaviour*>();
    auto* toggleModel = toggleBehaviour->model();
    const QPersistentModelIndex listed( toggleModel->index( 0, 0 ) );
    toggleTree->expand( listed );
    REQUIRE( waitFor( [ & ] { return toggleModel->rowCount( listed ) == 30; }, FileSystemWait ) );
    const QPersistentModelIndex firstFile( toggleModel->index( 0, 0, listed ) );
    const QPersistentModelIndex lastFile( toggleModel->index( 29, 0, listed ) );
    toggleTree->scrollTo( firstFile );
    QCoreApplication::processEvents();
    QTest::mouseClick( toggleTree->viewport(), Qt::LeftButton, {},
                       toggleTree->visualRect( firstFile ).center() );
    toggleTree->scrollTo( lastFile );
    QCoreApplication::processEvents();
    QTest::mouseClick( toggleTree->viewport(), Qt::LeftButton, Qt::ShiftModifier,
                       toggleTree->visualRect( lastFile ).center() );
    int toggled = 0;
    toggleBehaviour->value( 1, &toggled );
    REQUIRE( toggled == 30 );

    // A pick list checks only what shows a check box: not a folder with select "files", a greyed
    // file or a missing path.
    View picks( QJsonObject{ { "type", "file_tree" },
                             { "id", "files" },
                             { "roots", QJsonArray{ root } },
                             { "filters", QJsonArray{ "*.bin" } },
                             { "hide_filtered", false },
                             { "checkboxes", true },
                             { "select", "files" } } );
    auto* picked = picks.tree( "files" )->findChild<PluginFileTreeBehaviour*>();
    picks.patch( QJsonObject{
        { "id", "files" },
        { "set", QJsonObject{ { "value", QJsonArray{ root + "/many", root + "/b.txt",
                                                     root + "/nope.bin",
                                                     root + "/many/f00.bin" } } } } } );
    REQUIRE( picked->value( 10 ) == QStringList{ root + "/many/f00.bin" } );
    // New roots drop the checked paths that are no longer in a root.
    picks.patch( QJsonObject{
        { "id", "files" },
        { "set", QJsonObject{ { "value", QJsonArray{ root + "/many/f00.bin",
                                                     root + "/inside/x.bin" } } } } } );
    REQUIRE( picked->value( 10 ) == QStringList{ root + "/many/f00.bin", root + "/inside/x.bin" } );
    picks.patch( QJsonObject{ { "id", "files" },
                              { "set", QJsonObject{ { "roots", QJsonArray{ root + "/inside" } } } } } );
    REQUIRE( picked->value( 10 ) == QStringList{ root + "/inside/x.bin" } );

    // A checked file deleted on disk is dropped and the plugin hears what is still checked;
    // options that make a checked file ineligible drop it without an event.
    View gone( QJsonObject{ { "type", "file_tree" },
                            { "id", "files" },
                            { "roots", QJsonArray{ root + "/many" } },
                            { "filters", QJsonArray{ "*.bin" } },
                            { "checkboxes", true },
                            { "select", "files" } } );
    auto* goneTree = gone.tree( "files" );
    auto* goneBehaviour = goneTree->findChild<PluginFileTreeBehaviour*>();
    auto* goneModel = goneBehaviour->model();
    goneTree->expand( goneModel->index( 0, 0 ) );
    REQUIRE( waitFor( [ & ] { return goneModel->rowCount( goneModel->index( 0, 0 ) ) == 30; },
                      FileSystemWait ) );
    gone.patch( QJsonObject{
        { "id", "files" },
        { "set", QJsonObject{ { "value", QJsonArray{ root + "/many/f10.bin",
                                                     root + "/many/f11.bin" } } } } } );
    REQUIRE( goneBehaviour->value( 10 ) == QStringList{ root + "/many/f10.bin", root + "/many/f11.bin" } );
    const auto before = gone.countOf( "selected" );
    REQUIRE( QFile::remove( root + "/many/f11.bin" ) );
    REQUIRE( waitFor( [ & ] { return goneBehaviour->value( 10 ) == QStringList{ root + "/many/f10.bin" }; },
                      FileSystemWait ) );
    REQUIRE( waitFor( [ & ] { return gone.countOf( "selected" ) == before + 1; } ) );
    REQUIRE( gone.lastOf( "selected" ).value( "value" ).toArray() == QJsonArray{ root + "/many/f10.bin" } );
    gone.patch( QJsonObject{ { "id", "files" }, { "set", QJsonObject{ { "select", "dirs" } } } } );
    REQUIRE( goneBehaviour->value( 10 ).isEmpty() );
    QTest::qWait( 20 );
    REQUIRE( gone.countOf( "selected" ) == before + 1 );

    // A report a replaced file tree had deferred does not speak for what replaced it.
    View swapped( QJsonObject{ { "type", "file_tree" },
                               { "id", "files" },
                               { "roots", QJsonArray{ root + "/many" } },
                               { "checkboxes", true },
                               { "select", "files" } } );
    QPointer<pluginview::PluginFileRootsModel> oldModel(
        swapped.tree( "files" )->findChild<PluginFileTreeBehaviour*>()->model() );
    swapped.view.setRoot( QJsonObject{
        { "type", "column" },
        { "id", "body" },
        { "children", QJsonArray{ QJsonObject{ { "type", "label" }, { "id", "files" }, { "text", "Gone" } } } } } );
    const auto heardBefore = swapped.events.size();
    if ( oldModel && oldModel->onCheckedDropped ) {
        oldModel->onCheckedDropped();
    }
    REQUIRE( swapped.events.size() == heardBefore );

    // A local root that does not exist yet shows its contents by itself once it appears.
    const auto late = base + "/late";
    View later( QJsonObject{ { "type", "file_tree" }, { "id", "files" }, { "roots", QJsonArray{ late } } } );
    auto* laterTree = later.tree( "files" );
    auto* laterModel = laterTree->findChild<PluginFileTreeBehaviour*>()->model();
    REQUIRE_FALSE( laterModel->isAttached( 0 ) );
    REQUIRE_FALSE( laterModel->hasChildren( laterModel->index( 0, 0 ) ) );
    REQUIRE( QDir().mkpath( late ) );
    REQUIRE( touch( late + "/new.bin" ) );
    REQUIRE( waitFor( [ & ] { return laterModel->isAttached( 0 ); }, FileSystemWait ) );
    laterTree->expand( laterModel->index( 0, 0 ) );
    REQUIRE( waitFor( [ & ] { return laterModel->rowCount( laterModel->index( 0, 0 ) ) == 1; },
                      FileSystemWait ) );

    // A network root that cannot be reached closes again and keeps its expander for the next try.
    View network( QJsonObject{ { "type", "file_tree" },
                               { "id", "files" },
                               { "roots", QJsonArray{ "//localhost/no-such-share-e17e" } } } );
    auto* shareTree = network.tree( "files" );
    auto* shares = shareTree->findChild<PluginFileTreeBehaviour*>()->model();
    const auto share = shares->index( 0, 0 );
    shareTree->expand( share );
    REQUIRE_FALSE( shares->isAttached( 0 ) );
    REQUIRE( waitFor( [ & ] { return !shareTree->isExpanded( share ); } ) );
    REQUIRE( shares->hasChildren( share ) );
}
