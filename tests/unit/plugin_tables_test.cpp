// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rich tables and element menus (E17d, docs/plugin-sdk-advanced.md section 10): the feature list,
// the table model's cell objects, check boxes and sorting, the kept view state and the menu rules.
#include "pluginfeatures.h"
#include "pluginmanager.h"
#include "pluginmenuspec.h"
#include "plugintablemodel.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <catch2/catch.hpp>

using namespace cilogg::plugins;
using pluginview::PluginTableModel;
using pluginview::PluginTableProxy;

namespace {
QJsonArray columns()
{
    return { QJsonObject{ { "id", "name" }, { "sort_as", "natural" }, { "align", "center" } },
             QJsonObject{ { "id", "size" }, { "sort_as", "number" }, { "sortable", false } },
             QJsonObject{ { "id", "use" }, { "check", true }, { "min_width", 9999 },
                          { "icon", "builtin:info" } },
             QJsonObject{ { "id", "plain" }, { "sort_as", "bogus" }, { "hidden", true } } };
}

QJsonArray rows()
{
    return { QJsonObject{ { "id", "a" },
                          { "checked", true },
                          { "cells", QJsonArray{ QJsonObject{ { "text", "item 10" },
                                                              { "tooltip_key", "k10" },
                                                              { "progress", 7.0 } },
                                                 "12", "", "x" } } },
             QJsonObject{ { "id", "b" }, { "cells", QJsonArray{ "item 9", "n/a", "", "y" } } },
             QJsonObject{ { "id", "c" }, { "cells", QJsonArray{ "Item 1", "3", "", "z" } } } };
}
} // namespace

TEST_CASE( "E17d announces rich_tables and element_menus", "[plugins][E17]" )
{
    REQUIRE( hostFeatureVersions().value( "rich_tables" ).toInt() == 1 );
    REQUIRE( hostFeatureVersions().value( "element_menus" ).toInt() == 1 );
    REQUIRE( renderedMenuLocations().contains( "element" ) );
}

TEST_CASE( "E17d table cells, rows and columns", "[plugins][E17]" )
{
    PluginTableModel model( nullptr );
    REQUIRE( model.setColumns( columns() ) );
    REQUIRE( model.replace( rows() ) );
    const auto name = model.columnOf( "name" );
    REQUIRE( name == 0 );
    REQUIRE( model.columnOf( "missing" ) == -1 );
    REQUIRE( ( model.column( 0 ).align & Qt::AlignHCenter ) );
    REQUIRE_FALSE( model.column( 1 ).sortable );
    REQUIRE( model.column( 2 ).check );
    REQUIRE( model.column( 2 ).minWidth == 2000 ); // bounded
    REQUIRE( model.column( 3 ).hidden );
    REQUIRE( model.column( 3 ).sortAs.isEmpty() ); // unknown values are ignored
    REQUIRE( model.column( 99 ).id.isEmpty() );    // defaults for a column that is not there
    // A cell object: its text, its progress bounded to 0..1, and its tooltip key in the target.
    const auto first = model.index( 0, 0 );
    REQUIRE( first.data().toString() == "item 10" );
    REQUIRE( first.data( pluginview::ProgressRole ).toDouble() == 1.0 );
    REQUIRE( model.index( 1, 0 ).data( pluginview::ProgressRole ).isNull() );
    const auto target = model.tooltipTarget( first, false, "t" );
    REQUIRE( target.key == pluginview::joinedKey( { "t", "", "", "k10" } ) );
    REQUIRE( target.tooltipKey == "k10" );
    // Check boxes: only in check columns, from the row's `checked`.
    REQUIRE( model.index( 0, 2 ).data( Qt::CheckStateRole ) == Qt::Checked );
    REQUIRE( model.index( 1, 2 ).data( Qt::CheckStateRole ) == Qt::Unchecked );
    REQUIRE( model.index( 0, 0 ).data( Qt::CheckStateRole ).isNull() );
    REQUIRE( model.flags( model.index( 0, 2 ) ).testFlag( Qt::ItemIsUserCheckable ) );
    REQUIRE_FALSE( model.flags( model.index( 0, 0 ) ).testFlag( Qt::ItemIsUserCheckable ) );
    REQUIRE( model.hasCheckColumn() );
    QSignalSpy changed( &model, &QAbstractItemModel::dataChanged );
    REQUIRE( model.setChecked( { "a", "b", "zz" }, true ) == QStringList{ "b" } );
    REQUIRE( changed.size() == 1 );
    REQUIRE( model.isChecked( "b" ) );
    // A user change asks the receiver, which decides the rows.
    QString asked;
    model.canEdit = [] { return true; };
    model.onCheckRequested = [ & ]( const QString& rowId, bool ) { asked = rowId; };
    REQUIRE( model.setData( model.index( 2, 2 ), Qt::Checked, Qt::CheckStateRole ) );
    REQUIRE( asked == "c" );
    REQUIRE_FALSE( model.setData( model.index( 2, 0 ), Qt::Checked, Qt::CheckStateRole ) );
    // A row patch keeps cells and styles together and changes `checked` and `icon`.
    model.update( { QJsonObject{ { "id", "a" }, { "checked", false }, { "icon", "builtin:x" },
                                 { "cells", QJsonArray{ "plain now" } } } } );
    REQUIRE_FALSE( model.isChecked( "a" ) );
    REQUIRE( model.index( 0, 0 ).data( pluginview::ProgressRole ).isNull() );
}

TEST_CASE( "E17d sorting by sort_as, sort values and sortable", "[plugins][E17]" )
{
    PluginTableModel model( nullptr );
    REQUIRE( model.setColumns( columns() ) );
    REQUIRE( model.replace( rows() ) );
    PluginTableProxy proxy;
    proxy.setSourceModel( &model );
    proxy.setSortRole( pluginview::SortRole );
    const auto order = [ & ] {
        QStringList ids;
        for ( int row = 0; row < proxy.rowCount(); ++row ) {
            ids.append( proxy.index( row, 0 ).data( pluginview::RowIdRole ).toString() );
        }
        return ids;
    };
    proxy.sort( 0, Qt::AscendingOrder ); // natural, case-insensitive
    REQUIRE( order() == QStringList{ "c", "b", "a" } );
    proxy.sort( 1, Qt::AscendingOrder ); // not sortable: the order stays
    REQUIRE( order() == QStringList{ "c", "b", "a" } );
    // number: text that is no number sorts last.
    auto numbers = columns();
    auto size = numbers[ 1 ].toObject();
    size.insert( "sortable", true );
    numbers[ 1 ] = size;
    REQUIRE( model.setColumns( numbers ) );
    REQUIRE( model.replace( rows() ) );
    proxy.sort( 1, Qt::AscendingOrder );
    REQUIRE( order() == QStringList{ "c", "a", "b" } );
}

TEST_CASE( "E17d kept view state is bounded and per view", "[plugins][E17]" )
{
    QTemporaryDir temp;
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "shipped" ) );
    const QJsonObject state{ { "elements", QJsonObject{ { "t", QJsonObject{ { "a", 1 } } } } } };
    REQUIRE( manager.setKeptViewState( "org.p", "main", state ) );
    REQUIRE( manager.keptViewState( "org.p", "main" ) == state );
    REQUIRE( manager.keptViewState( "org.p", "other" ).isEmpty() );
    REQUIRE( manager.keptViewState( "org.p", "main/../x" ).isEmpty() );
    const QJsonObject huge{ { "x", QString( PluginManager::MaxViewStateBytes, QLatin1Char( 'x' ) ) } };
    REQUIRE_FALSE( manager.setKeptViewState( "org.p", "main", huge ) );
    REQUIRE( manager.keptViewState( "org.p", "main" ) == state );
}

TEST_CASE( "E17d element menus get no window shortcuts", "[plugins][E17]" )
{
    const QJsonObject manifest{
        { "menus",
          QJsonArray{ QJsonObject{ { "id", "rows" },
                                   { "location", "element" },
                                   { "items", QJsonArray{ QJsonObject{ { "id", "go" },
                                                                       { "title", "Go" },
                                                                       { "event", true },
                                                                       { "shortcut", "Ctrl+Alt+G" } } } } },
                      QJsonObject{ { "id", "main" },
                                   { "location", "plugins" },
                                   { "items", QJsonArray{ QJsonObject{ { "id", "run" },
                                                                       { "title", "Run" },
                                                                       { "event", true },
                                                                       { "shortcut", "Ctrl+Alt+R" } } } } } } } };
    const auto shortcuts = manifestShortcuts( "org.p", "P", manifest );
    REQUIRE( shortcuts.size() == 1 );
    REQUIRE( shortcuts.front().menuId == "main" );
}

TEST_CASE( "E17d cell tooltip keys keep every character and never share a cache key",
           "[plugins][E17]" )
{
    const QString unit( QChar( 0x1f ) );
    PluginTableModel model( nullptr );
    REQUIRE( model.setColumns( QJsonArray{
        QJsonObject{ { "id", "a" }, { "tooltip_key", "c" } },
        QJsonObject{ { "id", "b" }, { "tooltip_key", "b" + unit + "c" } } } ) );
    REQUIRE( model.replace( QJsonArray{
        // The cell's key holds the separator the host once split on.
        QJsonObject{ { "id", "r1" },
                     { "cells", QJsonArray{ QJsonObject{ { "text", "x" },
                                                         { "tooltip_key", "left" + unit + "right" } },
                                            "y" } } },
        // Row "a<US>b" in column "c" and row "a" in column "b<US>c" once joined to one key.
        QJsonObject{ { "id", "r2" }, { "tooltip_key", "a" + unit + "b" }, { "cells", QJsonArray{ "x", "y" } } },
        QJsonObject{ { "id", "r3" }, { "tooltip_key", "a" }, { "cells", QJsonArray{ "x", "y" } } } } ) );
    // The whole cell key reaches the query.
    REQUIRE( model.tooltipTarget( model.index( 0, 0 ), false, "table" ).tooltipKey
             == "left" + unit + "right" );
    // The most specific key that is set: the cell's, the row's, the column's, the table's.
    REQUIRE( model.tooltipTarget( model.index( 2, 1 ), false, "table" ).tooltipKey == "a" );
    REQUIRE( model.tooltipTarget( model.index( 1, 0 ), false, "table" ).tooltipKey == "a" + unit + "b" );
    REQUIRE( model.tooltipTarget( model.index( 0, 1 ), false, "table" ).tooltipKey == "b" + unit + "c" );
    // Different parts never make the same cache key part.
    REQUIRE( model.tooltipTarget( model.index( 1, 0 ), false, {} ).key
             != model.tooltipTarget( model.index( 2, 1 ), false, {} ).key );
    REQUIRE( pluginview::joinedKey( { "a" + unit + "b", "c" } )
             != pluginview::joinedKey( { "a", "b" + unit + "c" } ) );
}
