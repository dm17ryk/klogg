// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Trees (E17e, docs/plugin-sdk-advanced.md section 28): the features, the tree/children answer,
// the node model's operations, limits, check boxes and lazy children, and the file tree's paths.
#include "pluginfeatures.h"
#include "pluginfiletree.h"
#include "plugintree.h"
#include <QFont>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <catch2/catch.hpp>

using namespace cilogg::plugins;
using pluginview::PluginFileRootsModel;
using pluginview::PluginTreeModel;

namespace {
QJsonObject node( const QString& id, const QJsonObject& extra = {} )
{
    QJsonObject result{ { "id", id }, { "text", id.toUpper() } };
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        result.insert( it.key(), it.value() );
    }
    return result;
}

QJsonArray flat( int count, const QString& prefix )
{
    QJsonArray nodes;
    for ( int index = 0; index < count; ++index ) {
        nodes.append( QJsonObject{ { "id", prefix + QString::number( index ) } } );
    }
    return nodes;
}

// One chain of `depth` nodes, each the only child of the one before.
QJsonArray chain( int depth )
{
    QJsonObject inner{ { "id", QString( "d%1" ).arg( depth ) } };
    for ( int level = depth - 1; level >= 1; --level ) {
        inner = QJsonObject{ { "id", QString( "d%1" ).arg( level ) },
                             { "children", QJsonArray{ inner } } };
    }
    return QJsonArray{ inner };
}

QStringList ids( const PluginTreeModel& model )
{
    QStringList result;
    model.forEach( [ &result ]( const PluginTreeModel::Node& item, int depth ) {
        result.append( QString( depth, QLatin1Char( '.' ) ) + item.id );
    } );
    return result;
}
} // namespace

TEST_CASE( "E17e announces tree and file_tree", "[plugins][E17]" )
{
    REQUIRE( hostFeatureVersions().value( "tree" ).toInt() == 1 );
    REQUIRE( hostFeatureVersions().value( "file_tree" ).toInt() == 1 );
    REQUIRE( queryTimeouts( "tree/children" ).defaultMs == 10000 );
    REQUIRE( queryTimeouts( "tree/children" ).maximumMs == 30000 );
}

TEST_CASE( "E17e tree/children answers", "[plugins][E17]" )
{
    QString error;
    REQUIRE( sanitizeTreeChildren( QJsonValue::Null, &error ).isEmpty() );
    REQUIRE( sanitizeTreeChildren( QJsonObject{ { "nodes", QJsonArray{} } }, &error ).isEmpty() );
    REQUIRE( error.isEmpty() );
    const QJsonArray two{ node( "a" ), node( "b", { { "children", QJsonArray{ node( "c" ) } } } ) };
    REQUIRE( sanitizeTreeChildren( QJsonObject{ { "nodes", two } }, &error ).value( "nodes" ) == two );
    REQUIRE( sanitizeTreeChildren( two, &error ).value( "nodes" ) == two ); // a list alone
    REQUIRE( error.isEmpty() );
    // Not a list, a node that is no object, more than 1,000 nodes (nested ones count), too deep.
    for ( const auto& bad : { QJsonValue( QJsonObject{ { "nodes", 5 } } ),
                              QJsonValue( QJsonArray{ "a" } ),
                              QJsonValue( flat( MaxTreeChildrenNodes + 1, "n" ) ),
                              QJsonValue( QJsonArray{ node( "p", { { "children",
                                                                     flat( MaxTreeChildrenNodes, "c" ) } } ) } ),
                              QJsonValue( chain( MaxTreeDepth + 1 ) ) } ) {
        error.clear();
        REQUIRE( sanitizeTreeChildren( bad, &error ).isEmpty() );
        REQUIRE_FALSE( error.isEmpty() );
    }
    error.clear();
    REQUIRE_FALSE( sanitizeTreeChildren( flat( MaxTreeChildrenNodes, "n" ), &error ).isEmpty() );
    REQUIRE( error.isEmpty() );
}

TEST_CASE( "E17e tree nodes: replace, update, insert, remove and ids", "[plugins][E17]" )
{
    PluginTreeModel model( nullptr );
    QStringList warnings;
    model.warn = [ &warnings ]( const QString& key, const QString& ) { warnings.append( key ); };
    REQUIRE( model.setColumns( { QJsonObject{ { "id", "name" }, { "editable", true } }, "state" } ) );
    REQUIRE( model.columnCount() == 2 );
    QString error;
    REQUIRE( model.replace( { node( "a", { { "children", QJsonArray{ node( "a1" ), node( "a2" ) } },
                                           { "expanded", true } } ),
                              node( "b", { { "cells", QJsonArray{ QJsonObject{ { "text", "Bee" },
                                                                                { "bold", true },
                                                                                { "tooltip_key", "ck" } },
                                                                   "on", "dropped" } } } ),
                              node( "a1" ), // a repeated id: skipped
                              QJsonObject{ { "text", "no id" } } },
                            {}, &error ) );
    REQUIRE( ids( model ) == QStringList{ "a", ".a1", ".a2", "b" } );
    REQUIRE( model.nodeCount() == 4 );
    REQUIRE( warnings.size() == 2 );
    REQUIRE( model.takeExpandRequests().value( "a" ) == true );
    const auto b = model.indexOf( "b" );
    REQUIRE( b.data( Qt::DisplayRole ) == "Bee" );
    REQUIRE( b.data( Qt::FontRole ).value<QFont>().bold() );
    REQUIRE( b.sibling( b.row(), 1 ).data( Qt::DisplayRole ) == "on" );
    REQUIRE( model.nodeAt( b )->cells.size() == 2 ); // never more cells than columns
    REQUIRE( model.parent( model.indexOf( "a2" ) ) == model.indexOf( "a" ) );
    REQUIRE( model.depthOf( "a2" ) == 2 );
    REQUIRE( model.isWithin( "a2", "a" ) );
    REQUIRE_FALSE( model.isWithin( "a", "a2" ) );
    // update: only the keys given; children replace the node's children.
    QStringList changed;
    REQUIRE( model.update( { QJsonObject{ { "id", "a" }, { "tone", "warn" },
                                          { "children", QJsonArray{ node( "a3" ) } } },
                             QJsonObject{ { "id", "ghost" } } },
                           &error, &changed ) == false ); // ghost is unknown
    REQUIRE( ids( model ) == QStringList{ "a", ".a3", "b" } );
    REQUIRE( changed.contains( "a" ) );
    REQUIRE( changed.contains( "a1" ) ); // the replaced children's tooltips are stale too
    REQUIRE( model.indexOf( "a" ).data( Qt::DisplayRole ) == "A" ); // text kept
    // insert: under a parent, before a sibling or at the end; remove takes descendants.
    REQUIRE( model.insert( { node( "x" ) }, "a", "a3", &error ) );
    REQUIRE( model.insert( { node( "top" ) }, {}, {}, &error ) );
    REQUIRE_FALSE( model.insert( { node( "y" ) }, "nope", {}, &error ) );
    REQUIRE( ids( model ) == QStringList{ "a", ".x", ".a3", "b", "top" } );
    REQUIRE( model.remove( { "a", "missing" } ) == QStringList{ "a", "x", "a3" } );
    REQUIRE( ids( model ) == QStringList{ "b", "top" } );
    REQUIRE( model.nodeCount() == 2 );
    // replace below a parent.
    REQUIRE( model.replace( { node( "t1" ), node( "t2" ) }, "top", &error ) );
    REQUIRE( ids( model ) == QStringList{ "b", "top", ".t1", ".t2" } );
}

TEST_CASE( "E17e tree limits: 50,000 nodes and depth 64", "[plugins][E17]" )
{
    PluginTreeModel model( nullptr );
    QStringList warnings;
    model.warn = [ &warnings ]( const QString& key, const QString& ) { warnings.append( key ); };
    QString error;
    REQUIRE_FALSE( model.replace( flat( MaxTreeNodes + 1, "n" ), {}, &error ) );
    REQUIRE( model.nodeCount() == 0 );
    REQUIRE( model.replace( flat( MaxTreeNodes, "n" ), {}, &error ) );
    REQUIRE( model.nodeCount() == MaxTreeNodes );
    REQUIRE_FALSE( model.insert( { node( "one-more" ) }, {}, {}, &error ) );
    REQUIRE( model.nodeCount() == MaxTreeNodes );
    // Nodes below depth 64 are dropped with a warning.
    REQUIRE( model.replace( chain( MaxTreeDepth + 2 ), {}, &error ) );
    REQUIRE( model.nodeCount() == MaxTreeDepth );
    REQUIRE( warnings.contains( "tree-depth" ) );
}

TEST_CASE( "E17e tree check boxes and auto_tristate", "[plugins][E17]" )
{
    PluginTreeModel model( nullptr );
    model.setCheckboxes( true );
    model.setAutoTristate( true );
    QString error;
    REQUIRE( model.replace( { node( "p", { { "children",
                                             QJsonArray{ node( "c1", { { "checked", true } } ),
                                                         node( "c2" ),
                                                         node( "c3", { { "checkable", false } } ) } } } ),
                              node( "solo", { { "checked", QJsonValue::Null } } ) },
                            {}, &error ) );
    // A parent follows its checkable children; a leaf keeps its own state (null: partly).
    REQUIRE( model.indexOf( "p" ).data( Qt::CheckStateRole ) == Qt::PartiallyChecked );
    REQUIRE( model.indexOf( "solo" ).data( Qt::CheckStateRole ) == Qt::PartiallyChecked );
    REQUIRE_FALSE( model.indexOf( "c3" ).data( Qt::CheckStateRole ).isValid() );
    REQUIRE_FALSE( model.flags( model.indexOf( "c3" ) ).testFlag( Qt::ItemIsUserCheckable ) );
    // Checking the parent checks its checkable descendants; the previous states come back.
    QHash<QString, Qt::CheckState> previous;
    const auto set = model.setChecked( { "p" }, true, &previous );
    REQUIRE( set == QStringList{ "p", "c2" } );
    REQUIRE( model.indexOf( "c2" ).data( Qt::CheckStateRole ) == Qt::Checked );
    REQUIRE( model.indexOf( "p" ).data( Qt::CheckStateRole ) == Qt::Checked );
    model.restoreChecks( previous );
    REQUIRE( model.indexOf( "c2" ).data( Qt::CheckStateRole ) == Qt::Unchecked );
    REQUIRE( model.indexOf( "p" ).data( Qt::CheckStateRole ) == Qt::PartiallyChecked );
    // Without auto_tristate every box is independent.
    model.setAutoTristate( false );
    REQUIRE( model.setChecked( { "p" }, false ) == QStringList{ "p" } );
    REQUIRE( model.indexOf( "c1" ).data( Qt::CheckStateRole ) == Qt::Checked );
}

TEST_CASE( "E17e lazy children: placeholders, answers and failures", "[plugins][E17]" )
{
    PluginTreeModel model( nullptr );
    model.setLazy( true );
    model.setCheckboxes( true );
    model.setAutoTristate( true );
    QString error;
    REQUIRE( model.replace( { node( "lazy", { { "has_children", true }, { "checked", true } } ),
                              node( "leaf" ) },
                            {}, &error ) );
    const auto lazy = model.indexOf( "lazy" );
    REQUIRE( model.hasChildren( lazy ) );
    REQUIRE( model.rowCount( lazy ) == 0 );
    REQUIRE( model.wantsChildren( lazy ) );
    REQUIRE_FALSE( model.wantsChildren( model.indexOf( "leaf" ) ) );
    model.setLoading( "lazy" );
    REQUIRE( model.isLoading( "lazy" ) );
    REQUIRE_FALSE( model.wantsChildren( lazy ) );
    REQUIRE( model.rowCount( lazy ) == 1 );
    const auto placeholder = model.index( 0, 0, lazy );
    REQUIRE( placeholder.data( Qt::DisplayRole ) == "Loading…" );
    REQUIRE( model.idOf( placeholder ).isEmpty() );
    REQUIRE_FALSE( model.flags( placeholder ).testFlag( Qt::ItemIsSelectable ) );
    model.setLoadFailed( "lazy" );
    REQUIRE( model.index( 0, 0, lazy ).data( Qt::DisplayRole ) == "Could not load" );
    REQUIRE( model.wantsChildren( lazy ) ); // the next expand asks again
    // Children loaded below a checked parent follow it unless they say otherwise.
    model.setLoading( "lazy" );
    REQUIRE( model.fillChildren( "lazy", { node( "k1" ), node( "k2", { { "checked", false } } ) },
                                 &error ) );
    REQUIRE_FALSE( model.isLoading( "lazy" ) );
    REQUIRE( ids( model ) == QStringList{ "lazy", ".k1", ".k2", "leaf" } );
    REQUIRE( model.indexOf( "k1" ).data( Qt::CheckStateRole ) == Qt::Checked );
    REQUIRE( model.indexOf( "lazy" ).data( Qt::CheckStateRole ) == Qt::PartiallyChecked );
    REQUIRE_FALSE( model.wantsChildren( lazy ) );
    // An empty answer: no children, no expander.
    REQUIRE( model.insert( { node( "none", { { "has_children", true } } ) }, {}, {}, &error ) );
    model.setLoading( "none" );
    REQUIRE( model.fillChildren( "none", {}, &error ) );
    REQUIRE_FALSE( model.hasChildren( model.indexOf( "none" ) ) );
    // The plugin filling a loading node itself ends the wait.
    REQUIRE( model.insert( { node( "late", { { "has_children", true } } ) }, {}, {}, &error ) );
    model.setLoading( "late" );
    REQUIRE( model.replace( { node( "l1" ) }, "late", &error ) );
    REQUIRE_FALSE( model.isLoading( "late" ) );
    REQUIRE( model.rowCount( model.indexOf( "late" ) ) == 1 );
}

TEST_CASE( "E17e cell edits of tree nodes", "[plugins][E17]" )
{
    PluginTreeModel model( nullptr );
    REQUIRE( model.setColumns( { QJsonObject{ { "id", "name" }, { "editable", true } } } ) );
    QString error;
    REQUIRE( model.replace( { node( "n", { { "editors", QJsonObject{ { "name", QJsonObject{
                                                                               { "kind", "text" } } } } } } ),
                              node( "fixed" ) },
                            {}, &error ) );
    REQUIRE( model.flags( model.indexOf( "n" ) ).testFlag( Qt::ItemIsEditable ) );
    REQUIRE_FALSE( model.flags( model.indexOf( "fixed" ) ).testFlag( Qt::ItemIsEditable ) );
    QList<QStringList> edits;
    bool deliver = true;
    model.canEdit = [] { return true; };
    model.onEdited = [ & ]( const QString& id, const QString& column, const QJsonValue& value,
                            const QString& previous ) {
        edits.append( { id, column, value.toString(), previous } );
        return deliver;
    };
    REQUIRE( model.setData( model.indexOf( "n" ), QVariant::fromValue( QJsonValue( "Renamed" ) ),
                            Qt::EditRole ) );
    REQUIRE( edits.last() == QStringList{ "n", "name", "Renamed", "N" } );
    REQUIRE( model.indexOf( "n" ).data( Qt::FontRole ).value<QFont>().italic() ); // pending
    // The same value again is no edit; an edit that cannot be delivered goes back.
    REQUIRE_FALSE( model.setData( model.indexOf( "n" ), QVariant::fromValue( QJsonValue( "Renamed" ) ),
                                  Qt::EditRole ) );
    deliver = false;
    REQUIRE_FALSE( model.setData( model.indexOf( "n" ), QVariant::fromValue( QJsonValue( "Again" ) ),
                                  Qt::EditRole ) );
    REQUIRE( model.indexOf( "n" ).data( Qt::DisplayRole ) == "Renamed" );
    // The plugin's update confirms; stopping reverts unconfirmed edits.
    REQUIRE( model.update( { QJsonObject{ { "id", "n" }, { "text", "Renamed" } } }, &error ) );
    REQUIRE_FALSE( model.indexOf( "n" ).data( Qt::FontRole ).value<QFont>().italic() );
    deliver = true;
    REQUIRE( model.setData( model.indexOf( "n" ), QVariant::fromValue( QJsonValue( "Third" ) ),
                            Qt::EditRole ) );
    model.revertPending();
    REQUIRE( model.indexOf( "n" ).data( Qt::DisplayRole ) == "Renamed" );
}

TEST_CASE( "E17e file tree paths", "[plugins][E17]" )
{
    REQUIRE( PluginFileRootsModel::normalize( " C:/Firmware/images/ " ) == "C:/Firmware/images" );
    REQUIRE( PluginFileRootsModel::normalize( "/srv/data/../logs/" ) == "/srv/logs" );
    REQUIRE( PluginFileRootsModel::isNetworkPath( "//server/share" ) );
    REQUIRE_FALSE( PluginFileRootsModel::isNetworkPath( "/srv/share" ) );
#ifdef Q_OS_WIN
    // Native separators are Windows' own.
    REQUIRE( PluginFileRootsModel::normalize( "C:\\Firmware\\images\\" ) == "C:/Firmware/images" );
    REQUIRE( PluginFileRootsModel::isNetworkPath( "\\\\server\\share" ) );
#endif
}
