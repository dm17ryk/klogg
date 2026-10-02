#include "mcp.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
#include <stdexcept>

namespace cilogg::mcp {
QJsonObject parseCommandHelp( const QString& text )
{
    QJsonObject commands;
    QJsonObject options;
    QString section;
    QString last;
    const QRegularExpression action( "^  ([a-z][a-z0-9_]*)(?:\\s+(.*))?$" );
    const QRegularExpression option( "^\\s+(.*?)\\s{2,}(.*)$" );
    const QRegularExpression flag( "--([a-z][a-z0-9-]*)" );
    for ( const auto& line : text.split( '\n' ) ) {
        if ( line.trimmed() == "Actions:" || line.trimmed() == "Options:" ) {
            section = line.trimmed();
            continue;
        }
        if ( section == "Actions:" ) {
            const auto match = action.match( line );
            if ( match.hasMatch() ) {
                last = match.captured( 1 );
                commands.insert( last, match.captured( 2 ).trimmed() );
            }
            else if ( line.startsWith( "    " ) && !last.isEmpty() ) {
                commands.insert( last, commands.value( last ).toString() + ' ' + line.trimmed() );
            }
        }
        else if ( section == "Options:" ) {
            const auto match = option.match( line );
            if ( !match.hasMatch() ) {
                continue;
            }
            auto flags = flag.globalMatch( match.captured( 1 ) );
            while ( flags.hasNext() ) {
                const auto name = flags.next().captured( 1 );
                if ( QStringList{ "action", "help", "help-all", "version" }.contains( name ) ) {
                    continue;
                }
                auto key = name;
                key.replace( '-', '_' );
                options.insert(
                    key, QJsonObject{
                             { "flag", "--" + name },
                             { "type", match.captured( 1 ).contains( '<' ) ? "value" : "boolean" },
                             { "description", match.captured( 2 ).trimmed() } } );
            }
        }
    }
    if ( commands.isEmpty() || options.isEmpty() ) {
        throw std::runtime_error( "Cannot discover Commander commands from this executable." );
    }
    return { { "commands", commands }, { "options", options } };
}

QJsonObject toolResult( const QJsonObject& payload, bool failure )
{
    return { { "content", QJsonArray{ QJsonObject{
                              { "type", "text" },
                              { "text", QString::fromUtf8( QJsonDocument( payload ).toJson(
                                            QJsonDocument::Compact ) ) } } } },
             { "structuredContent", payload },
             { "isError", failure } };
}

namespace {
QJsonObject schema( const QJsonObject& properties, const QJsonArray& required = {} )
{
    return { { "type", "object" },
             { "properties", properties },
             { "required", required },
             { "additionalProperties", false } };
}
QJsonObject property( const QString& type, const QString& description = {} )
{
    return { { "type", type }, { "description", description } };
}
QJsonObject tool( const QString& name, const QString& description, const QJsonObject& input,
                  bool readOnly )
{
    return { { "name", name },
             { "description", description },
             { "inputSchema", input },
             { "annotations", QJsonObject{ { "readOnlyHint", readOnly },
                                           { "destructiveHint", !readOnly },
                                           { "idempotentHint", readOnly },
                                           { "openWorldHint", !readOnly } } } };
}
} // namespace

QJsonObject Session::tools()
{
    const auto string = property( "string" );
    return {
        { "tools",
          QJsonArray{
              tool( "cilogg_commands",
                    "Discover all Commander actions and argument names, GUI controls, "
                    "and application/scenario/lab/grep CLI help. Filter with query; area is all, "
                    "commander, gui, application, scenario, lab, lab-controller, lab-agent, grep, "
                    "or mcp.",
                    schema( { { "query", string }, { "area", string } } ), true ),
              tool( "cilogg_status",
                    "List running GUI windows, tabs, COM ports and stable tab IDs.", schema( {} ),
                    true ),
              tool( "cilogg_state",
                    "Inspect GUI actions and search/follow/loading state for a window.",
                    schema( { { "window_index", property( "integer" ) } } ), true ),
              tool( "cilogg_execute",
                    "Execute any Commander action via the same public CLI used by "
                    "the GUI. Arguments use underscore names from discovery. Inline definition and "
                    "parameters objects replace json_file and params_json_file. Use get_ui/set_ui/"
                    "activate_ui for all Qt dialogs, menus, models and mouse/keyboard gestures. "
                    "A running GUI is required; use cilogg_cli mode application to launch it. "
                    "Failed mutations may have been accepted; inspect state before retrying.",
                    schema( { { "action", string }, { "arguments", property( "object" ) } },
                            { "action" } ),
                    false ),
              tool(
                  "cilogg_cli",
                  "Run the complete CILogg CLI: application launches a detached GUI; "
                  "scenario, lab, lab-controller, lab-agent, grep and mcp setup run their normal "
                  "CLI argv. "
                  "Pass arguments as a literal string array, without the executable or mode. "
                  "Use background=true for long jobs/services, then cilogg_jobs for status/cancel. "
                  "mode command uses Commander argv and never starts a GUI.",
                  schema( { { "mode", string },
                            { "arguments", QJsonObject{ { "type", "array" },
                                                        { "items", property( "string" ) } } },
                            { "background", property( "boolean" ) },
                            { "timeout_ms", property( "integer" ) } },
                          { "mode" } ),
                  false ),
              tool( "cilogg_jobs",
                    "List/read/cancel background CLI jobs owned by this MCP server. "
                    "Cancel stops only that job. Detached GUI windows survive MCP shutdown.",
                    schema( { { "operation", string }, { "job_id", string } } ), false ) } }
    };
}

Session::Session( QString version, ToolHandler handler )
    : version_( std::move( version ) )
    , handler_( std::move( handler ) )
{
}

QJsonObject Session::error( const QJsonValue& id, int code, const QString& message )
{
    return { { "jsonrpc", "2.0" },
             { "id", id },
             { "error", QJsonObject{ { "code", code }, { "message", message } } } };
}

std::optional<QJsonObject> Session::dispatch( const QJsonObject& request )
{
    const auto id = request.value( "id" );
    const bool notification = !request.contains( "id" );
    const auto method = request.value( "method" ).toString();
    qInfo().noquote() << "MCP request:" << method << "notification" << notification;
    if ( request.value( "jsonrpc" ).toString() != "2.0" || method.isEmpty()
         || ( !notification && !id.isString() && !id.isDouble() )
         || ( request.contains( "params" ) && !request.value( "params" ).isObject() ) ) {
        return error( id.isString() || id.isDouble() ? id : QJsonValue::Null, -32600,
                      "Invalid JSON-RPC request." );
    }
    const auto params = request.value( "params" ).toObject();
    if ( notification ) {
        if ( method == "notifications/initialized" && initialized_ ) {
            ready_ = true;
        }
        return std::nullopt;
    }
    const auto response = [ &id ]( const QJsonObject& value ) {
        return QJsonObject{ { "jsonrpc", "2.0" }, { "id", id }, { "result", value } };
    };
    if ( method == "ping" ) {
        return response( {} );
    }
    if ( method == "initialize" ) {
        if ( initialized_ ) {
            return error( id, -32600, "Already initialized." );
        }
        if ( !params.value( "protocolVersion" ).isString()
             || !params.value( "capabilities" ).isObject()
             || !params.value( "clientInfo" ).isObject() ) {
            return error( id, -32602,
                          "initialize requires protocolVersion, capabilities and clientInfo." );
        }
        const auto wanted = params.value( "protocolVersion" ).toString();
        const QStringList supported{ "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05" };
        initialized_ = true;
        return response(
            { { "protocolVersion", supported.contains( wanted ) ? wanted : supported.front() },
              { "capabilities", QJsonObject{ { "tools", QJsonObject{} },
                                             { "resources", QJsonObject{} },
                                             { "prompts", QJsonObject{} } } },
              { "serverInfo", QJsonObject{ { "name", "cilogg" }, { "version", version_ } } },
              { "instructions",
                "CILogg's built-in MCP server controls the complete GUI and CLI. "
                "Start with status and discovery. Release a COM port with pause_comm and resume "
                "with play_comm; never terminate the user's monitor to free a port. "
                "UI activations are queued; inspect state to observe completion." } } );
    }
    if ( !ready_ ) {
        return error( id, -32600, "Initialize the session first." );
    }
    if ( method == "tools/list" ) {
        return response( tools() );
    }
    if ( method == "tools/call" ) {
        const auto name = params.value( "name" ).toString();
        QJsonObject input;
        for ( const auto& item : tools().value( "tools" ).toArray() ) {
            if ( item.toObject().value( "name" ).toString() == name ) {
                input = item.toObject().value( "inputSchema" ).toObject();
                break;
            }
        }
        if ( input.isEmpty() ) {
            return error( id, -32602, "Unknown tool: " + name );
        }
        if ( params.contains( "arguments" ) && !params.value( "arguments" ).isObject() ) {
            return error( id, -32602, "Tool arguments must be an object." );
        }
        try {
            const auto arguments = params.value( "arguments" ).toObject();
            for ( const auto& required : input.value( "required" ).toArray() ) {
                if ( !arguments.contains( required.toString() ) ) {
                    throw std::runtime_error(
                        ( "Missing argument: " + required.toString() ).toStdString() );
                }
            }
            const auto properties = input.value( "properties" ).toObject();
            for ( auto it = arguments.begin(); it != arguments.end(); ++it ) {
                const auto type
                    = properties.value( it.key() ).toObject().value( "type" ).toString();
                const auto value = it.value();
                const bool valid = ( type == "string" && value.isString() )
                                   || ( type == "boolean" && value.isBool() )
                                   || ( type == "object" && value.isObject() )
                                   || ( type == "array" && value.isArray() )
                                   || ( type == "integer" && value.isDouble()
                                        && std::isfinite( value.toDouble() )
                                        && value.toDouble() == std::floor( value.toDouble() ) );
                if ( !valid ) {
                    throw std::runtime_error(
                        ( "Invalid argument: " + it.key() + " (expected " + type + ')' )
                            .toStdString() );
                }
                if ( type == "array" ) {
                    for ( const auto& item : value.toArray() ) {
                        if ( !item.isString() ) {
                            throw std::runtime_error( "CLI arguments must be strings." );
                        }
                    }
                }
            }
            return response( toolResult( handler_( name, arguments ) ) );
        } catch ( const std::exception& exception ) {
            return response(
                toolResult( { { "error", QString::fromUtf8( exception.what() ) } }, true ) );
        }
    }
    if ( method == "resources/list" ) {
        return response(
            { { "resources", QJsonArray{ QJsonObject{ { "uri", "cilogg://commands" },
                                                      { "name", "CILogg command catalog" },
                                                      { "mimeType", "application/json" } } } } } );
    }
    if ( method == "resources/templates/list" ) {
        return response( { { "resourceTemplates", QJsonArray{} } } );
    }
    if ( method == "resources/read" ) {
        if ( params.value( "uri" ).toString() != "cilogg://commands" ) {
            return error( id, -32002, "Unknown resource." );
        }
        try {
            const auto catalog = handler_( "cilogg_commands", {} );
            return response(
                { { "contents", QJsonArray{ QJsonObject{
                                    { "uri", "cilogg://commands" },
                                    { "mimeType", "application/json" },
                                    { "text", QString::fromUtf8( QJsonDocument( catalog ).toJson(
                                                  QJsonDocument::Compact ) ) } } } } } );
        } catch ( const std::exception& exception ) {
            return error( id, -32603, QString::fromUtf8( exception.what() ) );
        }
    }
    if ( method == "prompts/list" ) {
        return response(
            { { "prompts", QJsonArray{ QJsonObject{
                               { "name", "inspect_cilogg" },
                               { "description", "Inspect open logs and serial capture without "
                                                "changing the session." } } } } } );
    }
    if ( method == "prompts/get" ) {
        if ( params.value( "name" ).toString() != "inspect_cilogg" ) {
            return error( id, -32602, "Unknown prompt." );
        }
        return response(
            { { "messages",
                QJsonArray{ QJsonObject{
                    { "role", "user" },
                    { "content",
                      QJsonObject{ { "type", "text" },
                                   { "text", "Call cilogg_status and "
                                             "cilogg_state, identify the requested tab and COM "
                                             "port, and report status. "
                                             "Use cilogg_commands to discover details. Do not "
                                             "mutate the session." } } } } } } } );
    }
    return error( id, -32601, "Method not found: " + method );
}
} // namespace cilogg::mcp
