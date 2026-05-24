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

constexpr auto kTelitAtConfig = R"json(
{
  "$schema": "../schemas/klogg-previews.schema.json",
  "version": 1,
  "blocks": [
    { "name": "at_command_MONI_block", "width": 0 },
    {
      "name": "at_command_SGACT_block",
      "format": "match",
      "type": "string",
      "regex": "^(?:(?<cid>\\d+),(?<stat>\\d+))?$",
      "fields": [
        { "name": "cid", "source": "capture", "capture": "cid", "format": "string" },
        {
          "name": "state",
          "source": "capture",
          "capture": "stat",
          "format": "enum",
          "enumMap": {
            "0": "deactivate / unbind PDP context",
            "1": "activate / bind PDP context"
          }
        }
      ],
      "source": "capture",
      "capture": "args"
    },
    {
      "name": "at_command_TEMPMON_block",
      "format": "match",
      "type": "string",
      "regex": "^(?:(?<mod>\\d+)(?:,(?<urcMode>\\d+)(?:,(?<action>\\d+)(?:,(?<GPIO>\\d+))?)?)?)?$",
      "fields": [
        {
          "name": "mode",
          "source": "capture",
          "capture": "mod",
          "format": "enum",
          "enumMap": {
            "0": "set monitor parameters",
            "1": "trigger temperature measurement"
          }
        },
        {
          "name": "urc_mode",
          "source": "capture",
          "capture": "urcMode",
          "format": "enum",
          "enumMap": {
            "0": "disabled / off",
            "1": "enabled / on"
          }
        },
        { "name": "action_mask", "source": "capture", "capture": "action", "format": "string" },
        { "name": "gpio", "source": "capture", "capture": "GPIO", "format": "string" }
      ],
      "source": "capture",
      "capture": "args"
    },
    {
      "name": "at_command_CGPADDR_block",
      "format": "match",
      "type": "string",
      "regex": "^(?:(?<cid>\\d+))?$",
      "fields": [
        { "name": "cid", "source": "capture", "capture": "cid", "format": "string" }
      ],
      "source": "capture",
      "capture": "args"
    },
    {
      "name": "at_command_CMGR_block",
      "format": "match",
      "type": "string",
      "regex": "^(?:(?<index>\\d+))?$",
      "fields": [
        { "name": "message_index", "source": "capture", "capture": "index", "format": "string" },
        {
          "name": "meaning",
          "source": "capture",
          "capture": "index",
          "format": "enum",
          "enumMap": {
            "1": "SMS storage index to read"
          }
        }
      ],
      "source": "capture",
      "capture": "args"
    },
    {
      "name": "at_response_CGPADDR_block",
      "format": "match",
      "type": "string",
      "regex": "^(?<cid>\\d+),\\\"(?<addr>[^\\\"]+)\\\"$",
      "fields": [
        { "name": "cid", "source": "capture", "capture": "cid", "format": "string" },
        { "name": "address", "source": "capture", "capture": "addr", "format": "string" }
      ],
      "source": "capture",
      "capture": "args"
    },
    {
      "name": "at_response_SS_block",
      "format": "match",
      "type": "string",
      "regex": "^(?<connId>\\d+),(?<state>\\d+)$",
      "fields": [
        { "name": "conn_id", "source": "capture", "capture": "connId", "format": "string" },
        {
          "name": "state",
          "source": "capture",
          "capture": "state",
          "format": "enum",
          "enumMap": {
            "0": "socket closed",
            "1": "active data transfer connection"
          }
        }
      ],
      "source": "capture",
      "capture": "args"
    },
    {
      "name": "at_response_STRINGENUM_block",
      "format": "match",
      "type": "string",
      "regex": "^(?<state>\\S+)$",
      "fields": [
        {
          "name": "state",
          "source": "capture",
          "capture": "state",
          "type": "string",
          "format": "enum",
          "enumMap": {
            "OFF": "off",
            "ON": "on"
          }
        }
      ],
      "source": "capture",
      "capture": "args"
    },
    {
      "name": "at_command_payload_known_block",
      "format": "match",
      "type": "string",
      "regex": "^(?:(?<channel>#\\d+)\\s+)?(?<token>AT(?<sigil>[+#])(?<command_key>(?:CGPADDR|MONI|RAWME|SGACT|TEMPMON|CMGR)))(?<op>=\\?|\\?|=|)(?<args>.*?)(?:\\r?#\\d+\\s*)?$",
      "fields": [
        { "name": "channel", "source": "capture", "capture": "channel", "format": "string" },
        { "name": "token", "source": "capture", "capture": "token", "format": "string" },
        { "name": "command_key", "source": "capture", "capture": "command_key", "format": "string" },
        {
          "name": "meaning",
          "source": "capture",
          "capture": "command_key",
          "format": "enum",
          "enumMap": {
            "CGPADDR": "Show PDP Address",
            "CMGR": "Read Message",
            "MONI": "Cell Monitor",
            "RAWME": "Raw Test Command",
            "SGACT": "PDP Context Activation",
            "TEMPMON": "Temperature Monitor"
          }
        },
        {
          "name": "operation",
          "source": "capture",
          "capture": "op",
          "format": "enum",
          "enumMap": {
            "": "execute",
            "=": "set",
            "?": "read",
            "=?": "test"
          }
        },
        { "name": "details", "source": "block", "block": "at_command_{command_key}_block" }
      ],
      "bufferCapture": "args"
    },
    {
      "name": "at_command_generic_args_block",
      "format": "match",
      "type": "string",
      "regex": "^(?<raw>.*)$",
      "fields": [
        { "name": "raw_args", "source": "capture", "capture": "raw", "format": "string" }
      ],
      "source": "capture",
      "capture": "args"
    },
    {
      "name": "at_response_generic_args_block",
      "format": "match",
      "type": "string",
      "regex": "^(?<raw>.*)$",
      "fields": [
        { "name": "raw_args", "source": "capture", "capture": "raw", "format": "string" }
      ],
      "source": "capture",
      "capture": "args"
    },
    {
      "name": "at_command_payload_generic_block",
      "format": "match",
      "type": "string",
      "regex": "^(?:(?<channel>#\\d+)\\s+)?(?<token>AT(?<command_key>[A-Z0-9]+))(?<op>=\\?|\\?|=|)(?<args>.*?)(?:\\r?#\\d+\\s*)?$",
      "fields": [
        { "name": "channel", "source": "capture", "capture": "channel", "format": "string" },
        { "name": "token", "source": "capture", "capture": "token", "format": "string" },
        { "name": "command_key", "source": "capture", "capture": "command_key", "format": "string" },
        {
          "name": "operation",
          "source": "capture",
          "capture": "op",
          "format": "enum",
          "enumMap": {
            "": "execute",
            "=": "set",
            "?": "read",
            "=?": "test"
          }
        },
        { "name": "raw_args", "source": "capture", "capture": "args", "format": "string" }
      ]
    },
    {
      "name": "at_response_payload_known_block",
      "format": "match",
      "type": "string",
      "regex": "^(?:(?<channel>#\\d+)\\s+)?(?<token>(?<sigil>[+#])(?<response_key>(?:CGPADDR|SS|STRINGENUM|UNKNOWN)))\\s*:\\s*(?<args>.*)$",
      "fields": [
        { "name": "channel", "source": "capture", "capture": "channel", "format": "string" },
        { "name": "token", "source": "capture", "capture": "token", "format": "string" },
        { "name": "response_key", "source": "capture", "capture": "response_key", "format": "string" },
        {
          "name": "meaning",
          "source": "capture",
          "capture": "response_key",
          "format": "enum",
          "enumMap": {
            "CGPADDR": "PDP address report",
            "SS": "Socket status",
            "STRINGENUM": "String enum test response",
            "UNKNOWN": "Unknown response with raw arguments"
          }
        },
        { "name": "details", "source": "block", "block": "at_response_{response_key}_block" }
      ],
      "bufferCapture": "args"
    }
  ],
  "previews": [
    {
      "name": "AT command (known)",
      "enabled": true,
      "regex": "^(?<hdr>\\d{2}/\\d{2}/\\d{4}\\s+\\d{2}:\\d{2}:\\d{2}\\.\\d{3}\\s+\\[(?:RX|TX)\\])\\s+-\\s+(?<payload>(?:#\\d+\\s+)?AT(?:#|\\+)(?:CGPADDR|MONI|RAWME|SGACT|TEMPMON|CMGR).*)$",
      "type": "string",
      "format": "fields",
      "bufferCapture": "payload",
      "fields": [
        { "name": "command", "source": "block", "block": "at_command_payload_known_block" }
      ]
    },
    {
      "name": "AT response / URC (known)",
      "enabled": true,
      "regex": "^(?<hdr>\\d{2}/\\d{2}/\\d{4}\\s+\\d{2}:\\d{2}:\\d{2}\\.\\d{3}\\s+\\[(?:RX|TX)\\])\\s+-\\s+(?<payload>(?:#\\d+\\s+)?(?:#|\\+)(?:CGPADDR|SS|STRINGENUM|UNKNOWN)\\s*:.*)$",
      "type": "string",
      "format": "fields",
      "bufferCapture": "payload",
      "fields": [
        { "name": "response", "source": "block", "block": "at_response_payload_known_block" }
      ]
    },
    {
      "name": "AT command (generic fallback)",
      "enabled": true,
      "regex": "^(?<hdr>\\d{2}/\\d{2}/\\d{4}\\s+\\d{2}:\\d{2}:\\d{2}\\.\\d{3}\\s+\\[(?:RX|TX)\\])\\s+-\\s+(?<payload>(?:#\\d+\\s+)?AT\\S+.*)$",
      "type": "string",
      "format": "fields",
      "bufferCapture": "payload",
      "fields": [
        { "name": "command", "source": "block", "block": "at_command_payload_generic_block" }
      ]
    },
    {
      "name": "AT CME error",
      "enabled": true,
      "regex": "^(?<hdr>\\d{2}/\\d{2}/\\d{4}\\s+\\d{2}:\\d{2}:\\d{2}\\.\\d{3}\\s+\\[(?:RX|TX)\\])\\s+-\\s+\\+CME ERROR:\\s*(?<code>\\d+)\\s*$",
      "type": "string",
      "format": "fields",
      "fields": [
        {
          "name": "meaning",
          "source": "capture",
          "capture": "code",
          "format": "enum",
          "enumMap": {
            "100": "Telit/3GPP general purpose error: unknown",
            "553": "context already activated"
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

void loadTelitAtConfig()
{
    auto& manager = PreviewManager::instance();
    manager.clearAll();

    QTemporaryFile file;
    REQUIRE( file.open() );
    const auto bytes
        = QByteArray::fromRawData( kTelitAtConfig, static_cast<int>( strlen( kTelitAtConfig ) ) );
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

bool fieldHasChild( QTreeWidget* tree, const QString& name, const QString& childName )
{
    REQUIRE( tree );
    auto* item = findFieldItem( tree->invisibleRootItem(), name );
    REQUIRE( item );
    return findChildItem( item, childName ) != nullptr;
}

bool fieldExists( QTreeWidget* tree, const QString& name )
{
    REQUIRE( tree );
    return findFieldItem( tree->invisibleRootItem(), name ) != nullptr;
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

TEST_CASE( "AT command execute operation resolves empty enum key", "[previewmatch][at]" )
{
    loadTelitAtConfig();

    const QString line
        = QString::fromLatin1( "13/05/2026 21:17:25.988 [RX] - #6 AT#MONI#5" );

    PreviewMessageTab tab( line, "AT command (known)", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "operation" ) == "execute" );
    CHECK_FALSE( fieldHasChild( tree, "operation", "Error" ) );
    CHECK_FALSE( fieldHasChild( tree, "details", "Error" ) );
}

TEST_CASE( "AT command dynamic block resolves CGPADDR details", "[previewmatch][at]" )
{
    loadTelitAtConfig();

    const QString line
        = QString::fromLatin1( "13/05/2026 21:18:16.021 [RX] - #6 AT+CGPADDR=1#5" );

    PreviewMessageTab tab( line, "AT command (known)", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "meaning" ) == "Show PDP Address" );
    CHECK( fieldValue( tree, "cid" ) == "1" );
    CHECK_FALSE( fieldHasChild( tree, "details", "Error" ) );
}

TEST_CASE( "AT command dynamic block falls back to raw arguments", "[previewmatch][at]" )
{
    loadTelitAtConfig();

    const QString line
        = QString::fromLatin1( "13/05/2026 21:18:16.021 [RX] - #6 AT+RAWME=2#5" );

    PreviewMessageTab tab( line, "AT command (known)", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "raw_args" ) == "2" );
    CHECK_FALSE( fieldHasChild( tree, "details", "Error" ) );
}

TEST_CASE( "AT response dynamic block resolves CGPADDR address", "[previewmatch][at]" )
{
    loadTelitAtConfig();

    const QString line
        = QString::fromLatin1( "13/05/2026 21:18:16.111 [RX] - +CGPADDR: 1,\"10.171.116.216\"" );

    PreviewMessageTab tab( line, "AT response / URC (known)", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "meaning" ) == "PDP address report" );
    CHECK( fieldValue( tree, "cid" ) == "1" );
    CHECK( fieldValue( tree, "address" ) == "10.171.116.216" );
    CHECK_FALSE( fieldHasChild( tree, "details", "Error" ) );
}

TEST_CASE( "AT response dynamic block falls back to generic args block", "[previewmatch][at]" )
{
    loadTelitAtConfig();

    const QString line = QString::fromLatin1( "13/05/2026 21:18:16.222 [RX] - +UNKNOWN: 1,2,3" );

    PreviewMessageTab tab( line, "AT response / URC (known)", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "raw_args" ) == "1,2,3" );
    CHECK_FALSE( fieldHasChild( tree, "details", "Error" ) );
}

TEST_CASE( "AT CME errors describe Telit standard and socket errors", "[previewmatch][at]" )
{
    loadTelitAtConfig();

    const QString unknownError
        = QString::fromLatin1( "13/05/2026 19:32:54.665 [RX] - +CME ERROR: 100" );
    PreviewMessageTab unknownTab( unknownError, "AT CME error", 1 );
    CHECK( fieldValue( unknownTab.findChild<QTreeWidget*>(), "meaning" )
           == "Telit/3GPP general purpose error: unknown" );

    const QString socketError
        = QString::fromLatin1( "13/05/2026 19:32:54.665 [RX] - +CME ERROR: 553" );
    PreviewMessageTab socketTab( socketError, "AT CME error", 1 );
    CHECK( fieldValue( socketTab.findChild<QTreeWidget*>(), "meaning" )
           == "context already activated" );
}

TEST_CASE( "AT command read form skips absent optional detail fields", "[previewmatch][at]" )
{
    loadTelitAtConfig();

    const QString line
        = QString::fromLatin1( "14/05/2026 15:56:15.399 [RX] - #6 AT#SGACT?#5" );

    PreviewMessageTab tab( line, "AT command (known)", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "operation" ) == "read" );
    CHECK_FALSE( fieldExists( tree, "state" ) );
    CHECK_FALSE( fieldHasChild( tree, "details", "Error" ) );
}

TEST_CASE( "AT TEMPMON command skips absent optional arguments", "[previewmatch][at]" )
{
    loadTelitAtConfig();

    const QString line
        = QString::fromLatin1( "14/05/2026 16:05:04.922 [RX] - #6 AT#TEMPMON=1#5" );

    PreviewMessageTab tab( line, "AT command (known)", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "mode" ) == "trigger temperature measurement" );
    CHECK_FALSE( fieldExists( tree, "urc_mode" ) );
    CHECK_FALSE( fieldExists( tree, "action_mask" ) );
    CHECK_FALSE( fieldExists( tree, "gpio" ) );
    CHECK_FALSE( fieldHasChild( tree, "details", "Error" ) );
}

TEST_CASE( "AT CMGR command describes the message index argument", "[previewmatch][at]" )
{
    loadTelitAtConfig();

    const QString line
        = QString::fromLatin1( "14/05/2026 16:05:05.265 [RX] - #6 AT+CMGR=1#5" );

    PreviewMessageTab tab( line, "AT command (known)", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "message_index" ) == "1" );
    CHECK( fieldValue( tree, "meaning" ) == "Read Message" );
    CHECK( fieldHasChild( tree, "details", "meaning" ) );
    CHECK_FALSE( fieldHasChild( tree, "details", "Error" ) );
}

TEST_CASE( "AT generic fallback handles basic and S-register commands", "[previewmatch][at]" )
{
    loadTelitAtConfig();

    const QStringList payloads{
        QString::fromLatin1( "#6 ATE0<CR>" ),
        QString::fromLatin1( "#21 ATZ" ),
        QString::fromLatin1( "#21 ATX1" ),
        QString::fromLatin1( "#6 ATX4<CR>" ),
        QString::fromLatin1( "#21 ATH0" ),
        QString::fromLatin1( "#6 ATS7=0;S10=20<CR>" ),
        QString::fromLatin1( "#6 ATS25=6<CR>" ),
        QString::fromLatin1( "#6 ATS30=255<CR>" ),
        QString::fromLatin1( "#6 ATD0547573349;<CR>" )
    };

    for ( const auto& payload : payloads ) {
        const QString line = QString::fromLatin1( "14/05/2026 16:05:05.265 [RX] - " ) + payload;
        PreviewMessageTab tab( line, "AT command (generic fallback)", 1 );
        auto* tree = tab.findChild<QTreeWidget*>();

        INFO( payload.toStdString() );
        CHECK( fieldExists( tree, "command_key" ) );
        CHECK_FALSE( fieldHasChild( tree, "command", "Error" ) );
    }
}

TEST_CASE( "AT previews render unknown enum values without decode errors", "[previewmatch][at]" )
{
    loadTelitAtConfig();

    const QString socketLine
        = QString::fromLatin1( "14/05/2026 16:05:05.265 [RX] - #SS: 1,99" );
    PreviewMessageTab socketTab( socketLine, "AT response / URC (known)", 1 );
    auto* socketTree = socketTab.findChild<QTreeWidget*>();
    CHECK( fieldValue( socketTree, "state" ) == "unknown/reserved (99, numeric 99)" );
    CHECK_FALSE( fieldHasChild( socketTree, "state", "Error" ) );

    const QString cmeLine
        = QString::fromLatin1( "14/05/2026 16:05:05.265 [RX] - +CME ERROR: 9999" );
    PreviewMessageTab cmeTab( cmeLine, "AT CME error", 1 );
    auto* cmeTree = cmeTab.findChild<QTreeWidget*>();
    CHECK( fieldValue( cmeTree, "meaning" ) == "unknown/reserved (9999, numeric 9999)" );
    CHECK_FALSE( fieldHasChild( cmeTree, "meaning", "Error" ) );
}

TEST_CASE( "AT previews render unknown non-numeric enum values without decode errors",
           "[previewmatch][at]" )
{
    loadTelitAtConfig();

    const QString line = QString::fromLatin1( "14/05/2026 16:05:05.265 [RX] - #STRINGENUM: MAYBE" );
    PreviewMessageTab tab( line, "AT response / URC (known)", 1 );
    auto* tree = tab.findChild<QTreeWidget*>();

    CHECK( fieldValue( tree, "state" ) == "unknown/reserved (MAYBE)" );
    CHECK_FALSE( fieldHasChild( tree, "state", "Error" ) );
}
