#include "mcp.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <sstream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace cilogg::mcp {
namespace {
[[noreturn]] void fail( const QString& message )
{
    throw std::runtime_error( message.toStdString() );
}
QString readFile( const QString& path )
{
    if ( !QFileInfo::exists( path ) ) {
        return {};
    }
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        fail( "Cannot read " + path + ": " + file.errorString() );
    }
    auto bytes = file.readAll();
    if ( bytes.startsWith( "\xEF\xBB\xBF" ) ) {
        bytes.remove( 0, 3 );
    }
    return QString::fromUtf8( bytes );
}
QString writeFile( const QString& path, const QString& content, bool dryRun )
{
    const bool exists = QFileInfo::exists( path );
    if ( exists && readFile( path ) == content ) {
        return "unchanged";
    }
    if ( dryRun ) {
        return exists ? "would_update" : "would_create";
    }
    if ( !QDir().mkpath( QFileInfo( path ).absolutePath() ) ) {
        fail( "Cannot create directory for " + path );
    }
    if ( exists ) {
        const auto backup = path + ".cilogg-backup-"
                            + QDateTime::currentDateTimeUtc().toString( "yyyyMMddTHHmmsszzzZ" )
                            + '-' + QUuid::createUuid().toString( QUuid::Id128 );
        if ( !QFile::copy( path, backup ) ) {
            fail( "Cannot back up " + path );
        }
        qInfo().noquote() << "MCP setup: backed up" << path << "to" << backup;
    }
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        fail( "Cannot write " + path + ": " + file.errorString() );
    }
    if ( exists ) {
        file.setPermissions( QFile::permissions( path ) );
    }
    const auto bytes = content.toUtf8();
    if ( file.write( bytes ) != bytes.size() || !file.commit() ) {
        fail( "Cannot save " + path + ": " + file.errorString() );
    }
    qInfo().noquote() << "MCP setup: updated" << path;
    return exists ? "updated" : "created";
}
const QStringList clients{ "codex", "claude-code", "cursor", "vscode", "claude-desktop" };
QMap<QString, QString> paths( const QString& root )
{
    const bool isolated = !root.isEmpty();
    const auto home = isolated ? QDir( root ).absolutePath() : QDir::homePath();
    const auto environment = [ & ]( const char* name, const QString& fallback ) {
        return isolated ? fallback : qEnvironmentVariable( name, fallback );
    };
#ifdef Q_OS_WIN
    const auto appData = environment( "APPDATA", home + "/AppData/Roaming" );
#elif defined( Q_OS_MACOS )
    const auto appData = home + "/Library/Application Support";
#else
    const auto appData = environment( "XDG_CONFIG_HOME", home + "/.config" );
#endif
    return { { "codex", environment( "CODEX_HOME", home + "/.codex" ) + "/config.toml" },
             { "claude-code", home + "/.claude.json" },
             { "cursor", home + "/.cursor/mcp.json" },
             { "vscode", appData + "/Code/User/mcp.json" },
             { "claude-desktop", appData + "/Claude/claude_desktop_config.json" } };
}
void markRows( const toml::node& node, QSet<int>& rows, const QStringList& lines )
{
    const auto begin = int( node.source().begin.line ) - 1;
    auto end = int( node.source().end.line ) - 1;
    if ( node.source().end.column == 1 && end > begin ) {
        --end;
    }
    if ( const auto* table = node.as_table(); table && !table->is_inline() ) {
        // An implicit table can share the source position of an unrelated header.
        // Only remove its explicit own header; leaf assignments have exact regions.
        if ( begin >= 0 && begin < lines.size() && lines.at( begin ).trimmed().startsWith( '[' ) ) {
            rows.insert( begin );
        }
        for ( const auto& item : *table ) {
            markRows( item.second, rows, lines );
        }
    }
    else {
        for ( int row = begin; row <= end; ++row ) {
            rows.insert( row );
        }
    }
}
QByteArray normalizedJson( const QString& text )
{
    // VS Code accepts JSONC. Strip comments and trailing commas outside strings,
    // then let Qt's JSON parser validate the entire document before any write.
    auto bytes = text.toUtf8();
    bool quoted = false;
    bool escaped = false;
    for ( qsizetype index = 0; index < bytes.size(); ++index ) {
        const char current = bytes.at( index );
        if ( quoted ) {
            if ( escaped ) {
                escaped = false;
            }
            else if ( current == '\\' ) {
                escaped = true;
            }
            else if ( current == '"' ) {
                quoted = false;
            }
            continue;
        }
        if ( current == '"' ) {
            quoted = true;
            continue;
        }
        if ( current != '/' || index + 1 >= bytes.size() ) {
            continue;
        }
        const auto next = bytes.at( index + 1 );
        if ( next == '/' ) {
            while ( index < bytes.size() && bytes.at( index ) != '\n' ) {
                bytes[ index++ ] = ' ';
            }
        }
        else if ( next == '*' ) {
            const auto end = bytes.indexOf( "*/", index + 2 );
            if ( end < 0 ) {
                fail( "Unterminated JSONC comment." );
            }
            while ( index < end + 2 ) {
                if ( bytes.at( index ) != '\n' ) {
                    bytes[ index ] = ' ';
                }
                ++index;
            }
            --index;
        }
    }
    quoted = false;
    escaped = false;
    for ( qsizetype index = 0; index < bytes.size(); ++index ) {
        const auto current = bytes.at( index );
        if ( quoted ) {
            if ( escaped ) {
                escaped = false;
            }
            else if ( current == '\\' ) {
                escaped = true;
            }
            else if ( current == '"' ) {
                quoted = false;
            }
            continue;
        }
        if ( current == '"' ) {
            quoted = true;
        }
        if ( current == ',' ) {
            auto next = index + 1;
            while ( next < bytes.size() && QByteArray( " \t\r\n" ).contains( bytes.at( next ) ) ) {
                ++next;
            }
            if ( next < bytes.size() && ( bytes.at( next ) == ']' || bytes.at( next ) == '}' ) ) {
                bytes[ index ] = ' ';
            }
        }
    }
    return bytes;
}

QString mergeToml( const QString& text, const QJsonObject& entry, bool remove )
{
    auto document = toml::parse( text.toStdString() );
    auto* servers = document[ "mcp_servers" ].as_table();
    if ( document.contains( "mcp_servers" ) && !servers ) {
        fail( "mcp_servers must be a TOML table." );
    }
    auto* existing = servers ? ( *servers )[ "cilogg" ].as_table() : nullptr;
    if ( servers && servers->contains( "cilogg" ) && !existing ) {
        fail( "mcp_servers.cilogg must be a table." );
    }
    const QStringList transport{
        "command", "args", "url", "http_headers", "bearer_token_env_var", "env_http_headers"
    };
    toml::table registration;
    if ( existing ) {
        registration = *existing;
    }
    for ( const auto& key : transport ) {
        registration.erase( key.toStdString() );
    }
    if ( !remove ) {
        registration.insert( "command", entry.value( "command" ).toString().toStdString() );
        registration.insert( "args", toml::array{ "mcp", "serve" } );
    }
    auto lines = text.split( '\n' );
    const int header = existing ? int( existing->source().begin.line ) - 1 : -1;
    bool standard = false;
    if ( header >= 0 && header < lines.size() && lines.at( header ).trimmed().startsWith( '[' ) ) {
        auto probe = toml::parse( lines.at( header ).toStdString() + "\n__probe__=true\n" );
        standard = probe[ "mcp_servers" ][ "cilogg" ][ "__probe__" ].value_or( false );
    }
    if ( existing && standard && !existing->is_inline() ) {
        // Patch only the registration's transport assignments. All unrelated text,
        // comments, quoted keys, nested env settings, and server tuning stay intact.
        QSet<int> removed;
        if ( remove ) {
            markRows( *existing, removed, lines );
        }
        else {
            for ( const auto& key : transport ) {
                if ( const auto* node = existing->get( key.toStdString() ) ) {
                    markRows( *node, removed, lines );
                }
            }
            removed.remove( header );
        }
        QStringList output;
        for ( int index = 0; index < lines.size(); ++index ) {
            if ( !removed.contains( index ) ) {
                output.push_back( lines.at( index ) );
            }
            if ( !remove && index == header ) {
                output.push_back(
                    "command = "
                    + QString::fromUtf8( QJsonDocument( QJsonArray{ entry.value( "command" ) } )
                                             .toJson( QJsonDocument::Compact ) )
                          .mid( 1 )
                          .chopped( 1 ) );
                output.push_back( "args = [\"mcp\", \"serve\"]" );
            }
        }
        const auto result = output.join( '\n' );
        (void)toml::parse( result.toStdString() );
        return result;
    }
    if ( !existing && !remove && ( !servers || !servers->is_inline() ) ) {
        QString result = text;
        if ( !result.endsWith( '\n' ) ) {
            result += '\n';
        }
        const auto command
            = QString::fromUtf8( QJsonDocument( QJsonArray{ entry.value( "command" ) } )
                                     .toJson( QJsonDocument::Compact ) )
                  .mid( 1 )
                  .chopped( 1 );
        result
            += "\n[mcp_servers.cilogg]\ncommand = " + command + "\nargs = [\"mcp\", \"serve\"]\n";
        (void)toml::parse( result.toStdString() );
        return result;
    }
    if ( !existing && remove ) {
        return text;
    }
    // Inline/dotted-table syntax cannot safely be edited line by line. Preserve
    // every semantic setting by using the same validated TOML tree in this case.
    if ( remove ) {
        servers->erase( "cilogg" );
    }
    else {
        servers->insert_or_assign( "cilogg", std::move( registration ) );
    }
    std::ostringstream serialized;
    serialized << document;
    return QString::fromStdString( serialized.str() ) + '\n';
}
} // namespace

QJsonObject serverEntry( const QString& executable )
{
    return { { "command", QDir::toNativeSeparators( QFileInfo( executable ).absoluteFilePath() ) },
             { "args", QJsonArray{ "mcp", "serve" } } };
}

QString generatedSkill( const QString& executable )
{
    return QStringLiteral(
               "---\nname: cilogg-mcp\ndescription: Operate CILogg logs, serial capture, "
               "actions, responses, scripts, scenarios, GUI dialogs, and its complete CLI through "
               "built-in MCP.\n"
               "---\n\nUse the cilogg MCP server. Start with cilogg_status and cilogg_state to "
               "select the "
               "correct window and stable tab ID. Use cilogg_commands(query, area) to discover "
               "Commander "
               "actions, live GUI controls, and application/scenario/lab/grep CLI modes.\n\n"
               "Call cilogg_execute(action, arguments) with underscore argument names from "
               "discovery. "
               "Inline definition and parameters objects replace json_file and params_json_file. "
               "False flags are omitted; use disabled/no_timestamps/etc. for explicit off "
               "options.\n\n"
               "For dialogs, inspect get_ui and target a unique objectName or current objectPath. "
               "set_ui edits writable properties and model indexPath/column/editText/checkState. "
               "activate_ui presses controls or uses a key sequence in text, or an inline "
               "definition "
               "for mouse click/double_click/drag/wheel gestures. Activations are queued; inspect "
               "again. "
               "Qt dialogs are used for operations launched through this interface.\n\n"
               "Use cilogg_cli with literal arguments for batch scenarios, remote lab, or grep. "
               "mode application launches the GUI; background=true starts an owned CLI job. "
               "Inspect/cancel jobs with cilogg_jobs. Detached GUI windows survive server "
               "shutdown.\n\n"
               "Release a COM port with pause_comm and resume with play_comm. Never terminate the "
               "user's "
               "monitor to free a port. Prefer existing action/response definitions. A failed or "
               "timed-out "
               "mutation may have been accepted; check state before retrying.\n\n"
               "CLI fallback executable: `%1`. Discover with `command --help`; MCP setup/serving "
               "and "
               "skill generation are built in: `mcp --help`.\n" )
        .arg( executable );
}

QString mergeClientConfig( const QString& text, const QString& client, const QJsonObject& entry,
                           bool remove )
{
    if ( client == "codex" ) {
        return mergeToml( text, entry, remove );
    }
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(
        client == "vscode" ? normalizedJson( text ) : text.toUtf8(), &error );
    if ( !text.trimmed().isEmpty()
         && ( error.error != QJsonParseError::NoError || !parsed.isObject() ) ) {
        fail( "Invalid client JSON configuration: " + error.errorString() );
    }
    auto document = parsed.object();
    const QString serversKey = client == "vscode" ? "servers" : "mcpServers";
    if ( document.contains( serversKey ) && !document.value( serversKey ).isObject() ) {
        fail( "mcpServers must be a JSON object." );
    }
    auto servers = document.value( serversKey ).toObject();
    if ( remove ) {
        servers.remove( "cilogg" );
    }
    else {
        if ( servers.contains( "cilogg" ) && !servers.value( "cilogg" ).isObject() ) {
            fail( "cilogg must be a server object." );
        }
        auto server = servers.value( "cilogg" ).toObject();
        for ( const auto& key : { "url", "serverUrl", "headers", "type", "transport" } ) {
            server.remove( key );
        }
        for ( auto it = entry.begin(); it != entry.end(); ++it ) {
            server.insert( it.key(), it.value() );
        }
        if ( client == "vscode" ) {
            server.insert( "type", "stdio" );
        }
        servers.insert( "cilogg", server );
    }
    document.insert( serversKey, servers );
    return QString::fromUtf8( QJsonDocument( document ).toJson( QJsonDocument::Indented ) );
}

QJsonObject installClients( const QString& executable, const QStringList& requested,
                            const QString& home, bool dryRun, bool skills, bool remove )
{
    const auto configurations = paths( home );
    const auto base = home.isEmpty() ? QDir::homePath() : QDir( home ).absolutePath();
    QStringList selected = requested;
    if ( selected.isEmpty() || selected == QStringList{ "auto" } ) {
        selected.clear();
        for ( const auto& client : clients ) {
            const auto file = configurations.value( client );
            bool present = QFileInfo::exists( file );
            if ( client == "codex" || client == "cursor" || client == "claude-desktop" ) {
                present |= QFileInfo( file ).absoluteDir().exists();
            }
            if ( client == "claude-code" ) {
                present |= QDir( base + "/.claude" ).exists();
            }
            const QString program = client == "claude-code" ? "claude"
                                    : client == "vscode"    ? "code"
                                                            : client;
            if ( home.isEmpty() ) {
                present |= !QStandardPaths::findExecutable( program ).isEmpty();
            }
            if ( present ) {
                selected.push_back( client );
            }
        }
    }
    if ( selected == QStringList{ "all" } ) {
        selected = clients;
    }
    selected.removeDuplicates();
    if ( selected.isEmpty() ) {
        fail( "No clients detected. Specify --client codex or --client claude-code." );
    }
    const auto entry = serverEntry( executable );
    struct Write {
        QString client;
        QString path;
        QString content;
        bool skill;
    };
    QList<Write> prepared;
    QJsonArray results;
    for ( const auto& client : selected ) {
        if ( !clients.contains( client ) ) {
            fail( "Unsupported MCP client: " + client );
        }
        const auto path = configurations.value( client );
        if ( remove && !QFileInfo::exists( path ) ) {
            results.push_back(
                QJsonObject{ { "client", client }, { "path", path }, { "status", "absent" } } );
            continue;
        }
        try {
            prepared.push_back( { client, path,
                                  mergeClientConfig( readFile( path ), client, entry, remove ),
                                  false } );
        } catch ( const std::exception& exception ) {
            fail( path + ": " + QString::fromUtf8( exception.what() ) );
        }
        if ( skills && !remove ) {
            QString root;
            if ( client == "codex" ) {
                root = QFileInfo( path ).absolutePath() + "/skills";
            }
            if ( client == "claude-code" ) {
                root = base + "/.claude/skills";
            }
            if ( client == "cursor" ) {
                root = base + "/.cursor/skills";
            }
            if ( !root.isEmpty() ) {
                prepared.push_back(
                    { client, root + "/cilogg-mcp/SKILL.md", generatedSkill( executable ), true } );
            }
        }
    }
    // Every config is parsed before changing any file. Each write is atomic and
    // backs up the original; invalid settings never get replaced with defaults.
    for ( const auto& write : prepared ) {
        results.push_back(
            QJsonObject{ { "client", write.client },
                         { write.skill ? "skill" : "path", write.path },
                         { "status", writeFile( write.path, write.content, dryRun ) } } );
    }
    return { { "clients", results },
             { "server", entry },
             { "dry_run", dryRun },
             { "restart_required", !dryRun },
             { "skills_retained_on_uninstall", remove } };
}

// Used by the standalone skill CLI as well as client installation.
QString saveSkill( const QString& executable, const QString& directory, bool dryRun )
{
    return writeFile( QDir( directory ).filePath( "SKILL.md" ), generatedSkill( executable ),
                      dryRun );
}
} // namespace cilogg::mcp
