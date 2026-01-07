/*
 * Copyright (C) 2025
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <catch2/catch.hpp>

#include <cstring>
#include <QTemporaryFile>
#include <QTreeWidget>

#include "previewmanager.h"
#include "previewmessagetab.h"

namespace {
constexpr auto kPreviewConfig = R"json(
{
  "$schema": "../schemas/klogg-previews.schema.json",
  "version": 1,
  "previews": [
    {
      "name": "SRING->EHCP",
      "regex": "^SRING: 1,48,(?<payload>[0-9A-F]+)$",
      "format": "fields",
      "fields": [
        {
          "name": "ehcp",
          "source": "capture",
          "capture": "payload",
          "type": "hexString",
          "format": "match",
          "regex": "^(?<header>EHCP)(?<size>[0-9A-F]{3})(?<body>.*)(?<checksum>[0-9A-F]{4})$",
          "fields": [
            { "name": "header", "source": "capture", "capture": "header", "type": "string", "format": "string" },
            { "name": "size", "source": "capture", "capture": "size", "type": "hexString", "format": "dig" },
            { "name": "checksum", "source": "capture", "capture": "checksum", "type": "hexString", "format": "hex" }
          ]
        }
      ]
    },
    {
      "name": "EHCP direct",
      "regex": "^.*\\\"(?<payload>EHCP[^\\\"]*)\\\".*$",
      "format": "fields",
      "fields": [
        {
          "name": "ehcp",
          "source": "capture",
          "capture": "payload",
          "type": "string",
          "format": "match",
          "regex": "^(?<header>EHCP)(?<size>[0-9A-F]{3})(?<body>.*)(?<checksum>[0-9A-F]{4})$",
          "fields": [
            { "name": "header", "source": "capture", "capture": "header", "type": "string", "format": "string" },
            { "name": "size", "source": "capture", "capture": "size", "type": "hexString", "format": "dig" },
            { "name": "checksum", "source": "capture", "capture": "checksum", "type": "hexString", "format": "hex" }
          ]
        }
      ]
    }
  ]
}
)json";

constexpr auto kBlockConfig = R"json(
{
  "$schema": "../schemas/klogg-previews.schema.json",
  "version": 1,
  "blocks": [
    {
      "name": "payload_P_CM",
      "format": "fields",
      "fields": [
        { "name": "payload", "width": 3, "type": "string", "format": "string" }
      ]
    },
    {
      "name": "cycle_block",
      "format": "block",
      "block": "cycle_block"
    }
  ],
  "previews": [
    {
      "name": "Block dynamic",
      "regex": "^(?<payload>.*)$",
      "bufferCapture": "payload",
      "format": "fields",
      "fields": [
        { "name": "protocol_type", "width": 1, "type": "string", "format": "string" },
        { "name": "command_id", "width": 2, "type": "string", "format": "string" },
        { "name": "payload_block", "format": "block", "block": "payload_{protocol_type}_{command_id}" }
      ]
    },
    {
      "name": "Block missing",
      "regex": "^(?<payload>.*)$",
      "bufferCapture": "payload",
      "format": "fields",
      "fields": [
        { "name": "protocol_type", "width": 1, "type": "string", "format": "string" },
        { "name": "payload_block", "format": "block", "block": "missing_{unknown}" }
      ]
    },
    {
      "name": "Block cycle",
      "regex": "^(?<payload>.*)$",
      "bufferCapture": "payload",
      "format": "fields",
      "fields": [
        { "name": "payload_block", "format": "block", "block": "cycle_block" }
      ]
    }
  ]
}
)json";

constexpr auto kEnumConfig = R"json(
{
  "$schema": "../schemas/klogg-previews.schema.json",
  "version": 1,
  "previews": [
    {
      "name": "Enum raw",
      "regex": "^(?<payload>.*)$",
      "bufferCapture": "payload",
      "format": "fields",
      "fields": [
        {
          "name": "device_type",
          "width": 2,
          "format": "enum",
          "enumMap": {
            "01": "CP",
            "02": "C7000"
          }
        },
        {
          "name": "protocol_type",
          "width": 1,
          "format": "enum",
          "enumMap": {
            "E": "Event"
          }
        }
      ]
    }
  ]
}
)json";

QTreeWidgetItem* findFieldItem( QTreeWidgetItem* root, const QString& name )
{
    if ( !root ) {
        return nullptr;
    }
    for ( int i = 0; i < root->childCount(); ++i ) {
        auto* child = root->child( i );
        if ( child->text( 0 ) == name ) {
            return child;
        }
        if ( auto* nested = findFieldItem( child, name ) ) {
            return nested;
        }
    }
    return nullptr;
}

QTreeWidgetItem* findChildItem( QTreeWidgetItem* parent, const QString& name )
{
    if ( !parent ) {
        return nullptr;
    }
    for ( int i = 0; i < parent->childCount(); ++i ) {
        auto* child = parent->child( i );
        if ( child->text( 0 ) == name ) {
            return child;
        }
    }
    return nullptr;
}

void loadMatchConfig()
{
    auto& manager = PreviewManager::instance();
    manager.clearAll();

    QTemporaryFile file;
    REQUIRE( file.open() );
    const auto bytes = QByteArray::fromRawData( kPreviewConfig, static_cast<int>( strlen( kPreviewConfig ) ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
    REQUIRE( file.flush() );

    const auto result = manager.importFromFile( file.fileName() );
    REQUIRE( result.ok );
}

void loadBlockConfig()
{
    auto& manager = PreviewManager::instance();
    manager.clearAll();

    QTemporaryFile file;
    REQUIRE( file.open() );
    const auto bytes = QByteArray::fromRawData( kBlockConfig, static_cast<int>( strlen( kBlockConfig ) ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
    REQUIRE( file.flush() );

    const auto result = manager.importFromFile( file.fileName() );
    REQUIRE( result.ok );
}

void loadEnumConfig()
{
    auto& manager = PreviewManager::instance();
    manager.clearAll();

    QTemporaryFile file;
    REQUIRE( file.open() );
    const auto bytes = QByteArray::fromRawData( kEnumConfig, static_cast<int>( strlen( kEnumConfig ) ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
    REQUIRE( file.flush() );

    const auto result = manager.importFromFile( file.fileName() );
    REQUIRE( result.ok );
}

QString fieldValue( QTreeWidget* tree, const QString& name )
{
    REQUIRE( tree );
    auto* item = findFieldItem( tree->invisibleRootItem(), name );
    REQUIRE( item );
    return item->text( 1 );
}
} // namespace

TEST_CASE( "Preview match stage decodes SRING payload", "[previewmatch]" )
{
    loadMatchConfig();

    const QString line = QString::fromLatin1(
        "SRING: 1,48,4548435030323930303030303030303431303431303830303130303235313210"
        "3130353734365A23302143343044" );

    PreviewMessageTab tab( line, "SRING->EHCP", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "header" ) == "EHCP" );
    CHECK( fieldValue( tree, "size" ) == "41" );
    CHECK( fieldValue( tree, "checksum" ) == "0xc40d" );
}

TEST_CASE( "Preview match stage parses direct EHCP payload", "[previewmatch]" )
{
    loadMatchConfig();

    const QString line = QString::fromLatin1(
        "12/10/2025 12:57:45.945 [RX] - #4 \"EHCP04202090100410411080010782512101057"
        "37E#INN000000NNNSCE0Y8D6FDIPR1!D774\"" );

    PreviewMessageTab tab( line, "EHCP direct", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "header" ) == "EHCP" );
    CHECK( fieldValue( tree, "size" ) == "66" );
    CHECK( fieldValue( tree, "checksum" ) == "0xd774" );
}

TEST_CASE( "Preview blocks resolve dynamic references", "[previewblocks]" )
{
    loadBlockConfig();

    const QString line = QString::fromLatin1( "PCMHEL" );

    PreviewMessageTab tab( line, "Block dynamic", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "payload" ) == "HEL" );
}

TEST_CASE( "Preview blocks report missing blocks", "[previewblocks]" )
{
    loadBlockConfig();

    const QString line = QString::fromLatin1( "PXX" );

    PreviewMessageTab tab( line, "Block missing", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();
    REQUIRE( tree );

    auto* payloadBlock = findFieldItem( tree->invisibleRootItem(), "payload_block" );
    REQUIRE( payloadBlock );
    auto* errorItem = findChildItem( payloadBlock, "Error" );
    REQUIRE( errorItem );
    CHECK( errorItem->text( 1 ).contains( "Missing block" ) );
}

TEST_CASE( "Preview blocks detect cycles", "[previewblocks]" )
{
    loadBlockConfig();

    const QString line = QString::fromLatin1( "data" );

    PreviewMessageTab tab( line, "Block cycle", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();
    REQUIRE( tree );

    auto* payloadBlock = findFieldItem( tree->invisibleRootItem(), "payload_block" );
    REQUIRE( payloadBlock );
    auto* errorItem = findChildItem( payloadBlock, "Error" );
    REQUIRE( errorItem );
    CHECK( errorItem->text( 1 ).contains( "cycle", Qt::CaseInsensitive ) );
}

TEST_CASE( "Preview enums use raw string keys", "[previewmatch]" )
{
    loadEnumConfig();

    const QString line = QString::fromLatin1( "02E" );

    PreviewMessageTab tab( line, "Enum raw", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "device_type" ) == "C7000" );
    CHECK( fieldValue( tree, "protocol_type" ) == "Event" );
}
