#include "mcp/mcp.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <catch2/catch.hpp>

using namespace cilogg::mcp;
namespace {
QJsonObject request( const QString& method, const QJsonObject& params = {} )
{
    return { { "jsonrpc", "2.0" }, { "id", 1 }, { "method", method }, { "params", params } };
}
void initialize( Session& session )
{
    const auto result = session.dispatch(
        request( "initialize",
                 { { "protocolVersion", "2025-11-25" },
                   { "capabilities", QJsonObject{} },
                   { "clientInfo", QJsonObject{ { "name", "tests" }, { "version", "1" } } } } ) );
    REQUIRE( result->value( "result" ).toObject().value( "serverInfo" ).toObject().value( "name" )
             == QJsonValue( "cilogg" ) );
    REQUIRE_FALSE(
        session.dispatch( { { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } } ) );
}
} // namespace
TEST_CASE( "Native MCP negotiates lifecycle and reports protocol and tool errors", "[mcp]" )
{
    int called = 0;
    Session session( "test", [ & ]( const QString&, const QJsonObject& ) {
        ++called;
        throw std::runtime_error( "CLI unavailable" );
        return QJsonObject{};
    } );
    REQUIRE( session.dispatch( request( "ping" ) )->contains( "result" ) );
    REQUIRE( session.dispatch( request( "tools/list" ) )->contains( "error" ) );
    initialize( session );
    REQUIRE( session.dispatch( request( "tools/list" ) )
                 ->value( "result" )
                 .toObject()
                 .value( "tools" )
                 .toArray()
                 .size()
             == 6 );
    REQUIRE( session.dispatch( request( "unknown" ) )
                 ->value( "error" )
                 .toObject()
                 .value( "code" )
                 .toInt()
             == -32601 );
    REQUIRE( session.dispatch( request( "tools/call", { { "name", "missing" } } ) )
                 ->contains( "error" ) );
    const auto invalid = session.dispatch( request(
        "tools/call", { { "name", "cilogg_cli" },
                        { "arguments", QJsonObject{ { "mode", "grep" },
                                                    { "arguments", QJsonArray{ 1 } } } } } ) );
    REQUIRE( invalid->value( "result" ).toObject().value( "isError" ).toBool() );
    REQUIRE( called == 0 );
    const auto missing
        = session.dispatch( request( "tools/call", { { "name", "cilogg_execute" } } ) );
    REQUIRE( missing->value( "result" ).toObject().value( "isError" ).toBool() );
    REQUIRE( called == 0 );
    const auto failure
        = session.dispatch( request( "tools/call", { { "name", "cilogg_status" } } ) );
    REQUIRE( failure->value( "result" ).toObject().value( "isError" ).toBool() );
    REQUIRE( called == 1 );
    REQUIRE( failure->value( "result" )
                 .toObject()
                 .value( "content" )
                 .toArray()
                 .first()
                 .toObject()
                 .value( "text" )
                 .toString()
                 .contains( "CLI unavailable" ) );
}

TEST_CASE( "Native MCP registration preserves TOML settings comments and tuning", "[mcp]" )
{
    const auto entry = serverEntry( "C:/Apps/CILogg/cilogg.exe" );
    const QString original
        = "# user settings\nmodel = 'custom'\n\n[mcp_servers.other]\ncommand='other'\n"
          "\n[mcp_servers.\"cilogg\"] # keep this\ncommand='python'\nargs=[\n 'old',\n "
          "'server'\n]\n"
          "startup_timeout_sec=30\n# tuning\n[mcp_servers.cilogg.env]\nMY_ENV='keep'\n";
    const auto merged = mergeClientConfig( original, "codex", entry );
    REQUIRE( merged.contains( "# user settings" ) );
    REQUIRE( merged.contains( "# keep this" ) );
    REQUIRE( merged.contains( "startup_timeout_sec=30" ) );
    REQUIRE( merged.contains( "MY_ENV='keep'" ) );
    REQUIRE( merged.contains( "command='other'" ) );
    REQUIRE_FALSE( merged.contains( "'python'" ) );
    REQUIRE( merged.contains( "args = [\"mcp\", \"serve\"]" ) );
    REQUIRE( mergeClientConfig( merged, "codex", entry ) == merged );
    const auto removed = mergeClientConfig( merged, "codex", entry, true );
    REQUIRE( removed.contains( "command='other'" ) );
    REQUIRE_FALSE( removed.contains( "[mcp_servers.cilogg.env]" ) );
    REQUIRE_THROWS( mergeClientConfig( "invalid TOML = [", "codex", entry ) );
    const auto migrated = mergeClientConfig(
        "[mcp_servers.cilogg]\nurl='https://old'\nhttp_headers={ x='old' }\n", "codex", entry );
    REQUIRE_FALSE( migrated.contains( "https://old" ) );
    REQUIRE_FALSE( migrated.contains( "http_headers" ) );
    REQUIRE( mergeClientConfig( "mcp_servers={cilogg={command='old'},other={command='keep'}}\n",
                                "codex", entry )
                 .contains( "keep" ) );
    const auto inlineAdded
        = mergeClientConfig( "mcp_servers={other={command='keep'}}\n", "codex", entry );
    REQUIRE( inlineAdded.contains( "cilogg" ) );
    REQUIRE( inlineAdded.contains( "keep" ) );
    REQUIRE( mergeClientConfig( inlineAdded, "codex", entry ) == inlineAdded );
}

TEST_CASE( "Native client setup is isolated idempotent and validates before writing", "[mcp]" )
{
    QTemporaryDir home;
    REQUIRE( home.isValid() );
    const auto entry = serverEntry( "/test/cilogg" );
    const auto jsonc
        = QJsonDocument::fromJson(
              mergeClientConfig(
                  "{ // settings\n\"servers\": {\"other\": {\"url\": "
                  "\"https://example.com/a/*b\",},}, /* keep values */\"inputs\": [],}",
                  "vscode", entry )
                  .toUtf8() )
              .object();
    REQUIRE( jsonc.value( "inputs" ).isArray() );
    REQUIRE( jsonc.value( "servers" ).toObject().value( "other" ).toObject().value( "url" )
             == QJsonValue( "https://example.com/a/*b" ) );
    REQUIRE( jsonc.value( "servers" ).toObject().value( "cilogg" ).toObject().value( "type" )
             == QJsonValue( "stdio" ) );
    for ( const auto& client : { "claude-code", "cursor", "vscode", "claude-desktop" } ) {
        const auto key = QString( client ) == "vscode" ? "servers" : "mcpServers";
        const auto text
            = QString( "{\"setting\":true,\"%1\":{\"other\":{\"command\":\"keep\"}}}" ).arg( key );
        const auto merged
            = QJsonDocument::fromJson( mergeClientConfig( text, client, entry ).toUtf8() ).object();
        REQUIRE( merged.value( "setting" ).toBool() );
        REQUIRE( merged.value( key ).toObject().value( "other" ).toObject().value( "command" )
                 == QJsonValue( "keep" ) );
        REQUIRE(
            merged.value( key ).toObject().value( "cilogg" ).toObject().value( "args" ).toArray()
            == QJsonArray{ "mcp", "serve" } );
    }
    installClients( "/test/cilogg", { "codex", "claude-code" }, home.path(), true );
    REQUIRE( QDir( home.path() )
                 .entryList( QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden )
                 .isEmpty() );
    const auto installed
        = installClients( "/test/cilogg", { "codex", "claude-code" }, home.path() );
    REQUIRE( installed.value( "clients" ).toArray().size() == 4 );
    REQUIRE( QFile::exists( home.filePath( ".codex/skills/cilogg-mcp/SKILL.md" ) ) );
    const auto repeated = installClients( "/test/cilogg", { "codex", "claude-code" }, home.path() );
    for ( const auto& result : repeated.value( "clients" ).toArray() ) {
        REQUIRE( result.toObject().value( "status" ) == QJsonValue( "unchanged" ) );
    }
    installClients( "/test/cilogg", { "codex", "claude-code" }, home.path(), false, true, true );
    REQUIRE( QFile::exists( home.filePath( ".codex/skills/cilogg-mcp/SKILL.md" ) ) );
    QFile invalid( home.filePath( ".claude.json" ) );
    REQUIRE( invalid.open( QIODevice::WriteOnly ) );
    invalid.write( "{" );
    invalid.close();
    QFile codex( home.filePath( ".codex/config.toml" ) );
    REQUIRE( codex.open( QIODevice::ReadOnly ) );
    const auto before = codex.readAll();
    codex.close();
    REQUIRE_THROWS( installClients( "/changed/cilogg", { "codex", "claude-code" }, home.path() ) );
    REQUIRE( codex.open( QIODevice::ReadOnly ) );
    REQUIRE( codex.readAll() == before );
}

TEST_CASE( "Native command discovery obtains real Commander options from CLI help", "[mcp]" )
{
    const auto parsed
        = parseCommandHelp( "Actions:\n  get_info  Inspect state\n  get_ui  Inspect controls\n"
                            "Options:\n  --action <action>  Action name\n  --object-name <name>  "
                            "Target\n  --enabled  Enable\n" );
    REQUIRE( parsed.value( "commands" ).toObject().contains( "get_ui" ) );
    REQUIRE( parsed.value( "options" ).toObject().value( "object_name" ).toObject().value( "type" )
             == QJsonValue( "value" ) );
    REQUIRE( parsed.value( "options" ).toObject().value( "enabled" ).toObject().value( "type" )
             == QJsonValue( "boolean" ) );
    REQUIRE_FALSE( parsed.value( "options" ).toObject().contains( "action" ) );
    REQUIRE_THROWS( parseCommandHelp( "empty" ) );
}
