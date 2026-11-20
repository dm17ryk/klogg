// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// E17e: trees and file trees (docs/plugin-sdk-advanced.md section 28).
#include "mainwindow.h"
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
#include <QHeaderView>
#include <QJsonDocument>
#include <QMenu>
#include <QSignalSpy>
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
        { "views", QJsonArray{ QJsonObject{ { "id", "trees" }, { "title", "Trees" } } } },
        { "menus",
          QJsonArray{ QJsonObject{
              { "id", "nodes_menu" },
              { "location", "element" },
              { "items", QJsonArray{ QJsonObject{ { "id", "reset" }, { "title", "Reset" },
                                                  { "event", true } } } } } } } };
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
