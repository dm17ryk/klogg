#include "mcp.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <memory>
#include <stdexcept>
#include <thread>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace cilogg::mcp {
namespace {
constexpr qsizetype outputLimit = 8 * 1024 * 1024;
[[noreturn]] void fail( const QString& message )
{
    throw std::runtime_error( message.toStdString() );
}
void writeBytes( const QByteArray& bytes, bool diagnostics = false )
{
    qsizetype offset = 0;
    while ( offset < bytes.size() ) {
#ifdef Q_OS_WIN
        const auto handle = GetStdHandle( diagnostics ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE );
        DWORD written = 0;
        if ( !WriteFile( handle, bytes.constData() + offset, DWORD( bytes.size() - offset ),
                         &written, nullptr )
             || !written ) {
            return;
        }
#else
        const auto written = ::write( diagnostics ? STDERR_FILENO : STDOUT_FILENO,
                                      bytes.constData() + offset, size_t( bytes.size() - offset ) );
        if ( written < 0 && errno == EINTR ) {
            continue;
        }
        if ( written <= 0 ) {
            return;
        }
#endif
        offset += written;
    }
}
void print( const QJsonObject& value )
{
    writeBytes( QJsonDocument( value ).toJson( QJsonDocument::Compact ) + '\n' );
}
void appendBounded( QByteArray& buffer, const QByteArray& value, bool& truncated )
{
    buffer += value;
    if ( buffer.size() > outputLimit ) {
        buffer.remove( 0, buffer.size() - outputLimit );
        truncated = true;
    }
}
struct ProcessResult {
    QByteArray output;
    QByteArray diagnostics;
    int code = -1;
    bool truncated = false;
};
struct Job {
    std::unique_ptr<QProcess> process;
    ProcessResult result;
    QString state = "running";
    QString mode;
};

class Runtime {
public:
    QString executable = QCoreApplication::applicationFilePath();
    QProcess* active = nullptr;
    bool cancelled = false;
    bool busy = false;
    QJsonValue activeId;
    QMap<QString, std::shared_ptr<Job>> jobs;
    QMap<QString, QString> help;
    ~Runtime()
    {
        shutdown();
    }
    void shutdown()
    {
        if ( active ) {
            cancelled = true;
            active->kill();
        }
        for ( const auto& job : jobs ) {
            if ( job->process->state() != QProcess::NotRunning ) {
                job->process->kill();
                job->process->waitForFinished( 3000 );
            }
        }
    }
    QString program( const QString& mode ) const
    {
        if ( mode != "grep" ) {
            return executable;
        }
        const auto path = QFileInfo( executable ).absoluteDir().filePath( CILOGG_MCP_GREP_NAME );
        if ( !QFileInfo::exists( path ) ) {
            fail( "The grep executable is not installed: " + path );
        }
        return path;
    }
    ProcessResult run( const QString& path, const QStringList& arguments, int timeout = 30000 )
    {
        QProcess process;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot( true );
        ProcessResult result;
        bool expired = false;
        cancelled = false;
        QScopedValueRollback<QProcess*> activeGuard( active, &process );
        const auto drain = [ & ] {
            appendBounded( result.output, process.readAllStandardOutput(), result.truncated );
            appendBounded( result.diagnostics, process.readAllStandardError(), result.truncated );
        };
        QObject::connect( &process, &QProcess::readyReadStandardOutput, &loop, drain );
        QObject::connect( &process, &QProcess::readyReadStandardError, &loop, drain );
        QObject::connect( &process, qOverload<int, QProcess::ExitStatus>( &QProcess::finished ),
                          &loop, [ & ]( int code, QProcess::ExitStatus status ) {
                              result.code = status == QProcess::NormalExit ? code : -1;
                              loop.quit();
                          } );
        QObject::connect( &process, &QProcess::errorOccurred, &loop,
                          [ & ]( QProcess::ProcessError error ) {
                              if ( error == QProcess::FailedToStart ) {
                                  loop.quit();
                              }
                          } );
        QObject::connect( &timer, &QTimer::timeout, &loop, [ & ] {
            expired = true;
            process.kill();
        } );
        qInfo().noquote() << "MCP CLI: starting" << QFileInfo( path ).fileName() << "mode"
                          << arguments.value( 0 ) << "timeout_ms" << timeout;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert( "QT_QPA_PLATFORM", "offscreen" );
        process.setProcessEnvironment( environment );
        process.start( path, arguments );
        timer.start( timeout );
        loop.exec();
        drain();
        if ( expired ) {
            fail( "CLI timed out. A GUI mutation may already be accepted; inspect state before "
                  "retrying." );
        }
        if ( cancelled ) {
            fail( "CLI request cancelled; inspect state before retrying a mutation." );
        }
        if ( process.error() == QProcess::FailedToStart ) {
            fail( "Cannot start CLI: " + process.errorString() );
        }
        qInfo() << "MCP CLI: finished with exit code" << result.code << "truncated"
                << result.truncated;
        return result;
    }
    QJsonObject commander( const QString& action, const QJsonObject& arguments )
    {
        const auto catalog = commandCatalog();
        if ( !catalog.value( "commands" ).toObject().contains( action ) ) {
            fail( "Unknown Commander action: " + action );
        }
        const auto options = catalog.value( "options" ).toObject();
        QStringList argv{ "command", "--action", action, "--require-running" };
        QTemporaryDir temporary;
        int timeout = 40000;
        for ( auto it = arguments.begin(); it != arguments.end(); ++it ) {
            auto key = it.key();
            QJsonValue value = it.value();
            if ( key == "definition" || key == "parameters" ) {
                if ( !value.isObject() ) {
                    fail( key + " must be an object." );
                }
                if ( !temporary.isValid() ) {
                    fail( "Cannot create temporary JSON directory." );
                }
                const auto path = temporary.filePath( key + ".json" );
                QFile file( path );
                const auto bytes
                    = QJsonDocument( value.toObject() ).toJson( QJsonDocument::Compact );
                if ( !file.open( QIODevice::WriteOnly ) || file.write( bytes ) != bytes.size() ) {
                    fail( "Cannot write inline JSON definition." );
                }
                key = key == "definition" ? "json_file" : "params_json_file";
                if ( arguments.contains( key ) ) {
                    fail( "Do not combine inline JSON and " + key );
                }
                value = path;
            }
            if ( key == "require_running" ) {
                if ( value != QJsonValue( true ) ) {
                    fail( "require_running cannot be disabled." );
                }
                continue;
            }
            if ( !options.contains( key ) ) {
                fail( "Unknown Commander argument: " + key );
            }
            const auto option = options.value( key ).toObject();
            const auto flag = option.value( "flag" ).toString();
            if ( option.value( "type" ).toString() == "boolean" ) {
                if ( !value.isBool() ) {
                    fail( key + " must be boolean." );
                }
                if ( value.toBool() ) {
                    argv.push_back( flag );
                }
            }
            else {
                if ( !value.isString() && !value.isDouble() ) {
                    fail( key + " must be a string or number." );
                }
                const auto text = value.isString() ? value.toString()
                                                   : QString::number( value.toDouble(), 'g', 16 );
                argv.push_back( flag + '=' + text );
                if ( key == "timeout_ms" ) {
                    bool valid = false;
                    const auto requested = text.toLongLong( &valid );
                    if ( !valid || requested < 1 || requested > 86400000 ) {
                        fail( "timeout_ms must be between 1 and 86400000." );
                    }
                    timeout = int( requested ) + 10000;
                }
            }
        }
        const auto result = run( executable, argv, timeout );
        if ( result.code != 0 ) {
            fail( "Commander failed: "
                  + QString::fromUtf8( result.diagnostics + result.output ).left( 4096 ) );
        }
        // Commander prints its payload directly and deliberately emits nothing
        // for successful commands with no payload (e.g. close_tab or set_ui).
        if ( result.output.trimmed().isEmpty() ) {
            return { { "action", action }, { "payload", QJsonObject{} } };
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson( result.output.trimmed(), &error );
        if ( !document.isObject() ) {
            fail( "Commander failed: "
                  + QString::fromUtf8( result.diagnostics.isEmpty() ? result.output
                                                                    : result.diagnostics )
                        .left( 4096 ) );
        }
        return { { "action", action }, { "payload", document.object() } };
    }
    QString modeHelp( const QString& mode )
    {
        if ( help.contains( mode ) ) {
            return help.value( mode );
        }
        QStringList arguments;
        if ( mode != "application" && mode != "grep" ) {
            arguments.push_back( mode );
        }
        arguments.push_back( "--help" );
        const auto result = run( program( mode ), arguments );
        if ( result.code != 0 || result.output.isEmpty() ) {
            fail( "Cannot discover CLI help for " + mode + ": "
                  + QString::fromUtf8( result.diagnostics ).left( 2048 ) );
        }
        const auto text = QString::fromUtf8( result.output );
        help.insert( mode, text );
        return text;
    }
    QJsonObject commandCatalog()
    {
        return parseCommandHelp( modeHelp( "command" ) );
    }
    QJsonObject discovery( const QJsonObject& arguments )
    {
        const auto area = arguments.value( "area" ).toString( "all" );
        const auto query = arguments.value( "query" ).toString();
        if ( area == "gui" ) {
            return commander( "get_ui", {} );
        }
        QJsonObject result;
        if ( area == "all" || area == "commander" ) {
            auto catalog = commandCatalog();
            if ( !query.isEmpty() ) {
                QJsonObject selected;
                const auto commands = catalog.value( "commands" ).toObject();
                for ( auto it = commands.begin(); it != commands.end(); ++it ) {
                    if ( ( it.key() + ' ' + it.value().toString() )
                             .contains( query, Qt::CaseInsensitive ) ) {
                        selected.insert( it.key(), it.value() );
                    }
                }
                catalog.insert( "commands", selected );
            }
            result.insert( "commander", catalog );
        }
        const QStringList modes{ "application", "scenario", "lab", "lab-controller",
                                 "lab-agent",   "grep",     "mcp" };
        if ( area != "all" && area != "commander" && !modes.contains( area ) ) {
            fail( "Unknown discovery area: " + area );
        }
        QJsonObject cli;
        for ( const auto& mode : modes ) {
            if ( area != "all" && area != mode ) {
                continue;
            }
            auto text = modeHelp( mode );
            if ( !query.isEmpty() ) {
                QStringList lines;
                for ( const auto& line : text.split( '\n' ) ) {
                    if ( line.contains( query, Qt::CaseInsensitive ) ) {
                        lines.push_back( line );
                    }
                }
                text = lines.join( '\n' );
            }
            cli.insert( mode, text );
        }
        result.insert( "cli", cli );
        result.insert( "gui",
                       "get_ui discovers every Qt widget/action/model; set_ui edits controls and "
                       "models; activate_ui invokes controls, shortcuts and mouse gestures." );
        return result;
    }
    QJsonObject jobState( const QString& id, const std::shared_ptr<Job>& job ) const
    {
        return { { "job_id", id },
                 { "mode", job->mode },
                 { "state", job->state },
                 { "pid", double( job->process->processId() ) },
                 { "exit_code", job->result.code },
                 { "stdout", QString::fromUtf8( job->result.output ) },
                 { "stderr", QString::fromUtf8( job->result.diagnostics ) },
                 { "output_truncated", job->result.truncated } };
    }
    QJsonObject call( const QString& name, const QJsonObject& arguments )
    {
        if ( name == "cilogg_commands" ) {
            return discovery( arguments );
        }
        if ( name == "cilogg_status" ) {
            return commander( "get_info", {} );
        }
        if ( name == "cilogg_state" ) {
            return commander( "dump_state", arguments );
        }
        if ( name == "cilogg_execute" ) {
            return commander( arguments.value( "action" ).toString(),
                              arguments.value( "arguments" ).toObject() );
        }
        if ( name == "cilogg_jobs" ) {
            const auto operation = arguments.value( "operation" ).toString( "list" );
            if ( operation == "list" ) {
                QJsonArray list;
                for ( auto it = jobs.begin(); it != jobs.end(); ++it ) {
                    list.push_back( jobState( it.key(), it.value() ) );
                }
                return { { "jobs", list } };
            }
            const auto id = arguments.value( "job_id" ).toString();
            if ( !jobs.contains( id ) ) {
                fail( "Unknown job_id: " + id );
            }
            const auto job = jobs.value( id );
            if ( operation == "cancel" && job->process->state() != QProcess::NotRunning ) {
                job->state = "cancelled";
                job->process->kill();
                job->process->waitForFinished( 3000 );
            }
            else if ( operation != "read" && operation != "cancel" ) {
                fail( "operation must be list, read or cancel." );
            }
            return jobState( id, job );
        }
        if ( name != "cilogg_cli" ) {
            fail( "Unknown tool." );
        }
        const auto mode = arguments.value( "mode" ).toString();
        const QStringList allowed{ "application",    "command",   "scenario", "lab",
                                   "lab-controller", "lab-agent", "grep",     "mcp" };
        if ( !allowed.contains( mode ) ) {
            fail( "Unsupported CLI mode: " + mode );
        }
        QStringList argv;
        for ( const auto& value : arguments.value( "arguments" ).toArray() ) {
            argv.push_back( value.toString() );
        }
        if ( mode == "mcp" && !argv.isEmpty() && argv.front() == "serve" ) {
            fail( "Connect to this MCP server directly; nested stdio serving is not a CLI job." );
        }
        if ( mode == "application" ) {
            // Application mode is specifically for detached GUI startup. A mode
            // prefix could otherwise start an unowned lab service or MCP server.
            if ( !argv.isEmpty()
                 && QStringList{ "command", "scenario", "lab", "lab-controller", "lab-agent",
                                 "mcp" }
                        .contains( argv.front().toLower() ) ) {
                fail( "Use a dedicated CLI mode for " + argv.front() );
            }
            qint64 pid = 0;
            QProcess launcher;
            launcher.setProgram( executable );
            launcher.setArguments( argv );
            // Detached children must neither read the protocol pipe nor inherit
            // its output: even --help/--version print non-JSON text. GUI logging
            // continues through the application's normal file logging options.
            launcher.setStandardInputFile( QProcess::nullDevice() );
            launcher.setStandardOutputFile( QProcess::nullDevice() );
            launcher.setStandardErrorFile( QProcess::nullDevice() );
            if ( !launcher.startDetached( &pid ) ) {
                fail( "Cannot launch the GUI: " + launcher.errorString() );
            }
            qInfo() << "MCP application: detached launch accepted, pid" << pid;
            return { { "started", true }, { "pid", double( pid ) }, { "detached", true } };
        }
        if ( mode != "grep" ) {
            argv.prepend( mode );
        }
        if ( mode == "command" && !argv.contains( "--require-running" ) ) {
            argv.push_back( "--require-running" );
        }
        const auto path = program( mode );
        if ( arguments.value( "background" ).toBool() ) {
            // Bound both active processes and retained output for a long session.
            if ( jobs.size() >= 32 ) {
                fail( "Maximum 32 jobs per session reached; reconnect to clear completed jobs." );
            }
            const auto id = QUuid::createUuid().toString( QUuid::WithoutBraces );
            auto job = std::make_shared<Job>();
            job->process = std::make_unique<QProcess>();
            job->mode = mode;
            const std::weak_ptr<Job> weak = job;
            const auto drain = [ weak ] {
                if ( const auto current = weak.lock() ) {
                    appendBounded( current->result.output,
                                   current->process->readAllStandardOutput(),
                                   current->result.truncated );
                    appendBounded( current->result.diagnostics,
                                   current->process->readAllStandardError(),
                                   current->result.truncated );
                }
            };
            QObject::connect( job->process.get(), &QProcess::readyReadStandardOutput,
                              job->process.get(), drain );
            QObject::connect( job->process.get(), &QProcess::readyReadStandardError,
                              job->process.get(), drain );
            QObject::connect(
                job->process.get(), qOverload<int, QProcess::ExitStatus>( &QProcess::finished ),
                job->process.get(), [ weak, drain ]( int code, QProcess::ExitStatus status ) {
                    drain();
                    if ( const auto current = weak.lock() ) {
                        current->result.code = status == QProcess::NormalExit ? code : -1;
                        if ( current->state != "cancelled" ) {
                            current->state = code == 0 && status == QProcess::NormalExit
                                                 ? "completed"
                                                 : "failed";
                        }
                    }
                } );
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert( "QT_QPA_PLATFORM", "offscreen" );
            job->process->setProcessEnvironment( environment );
            job->process->start( path, argv );
            if ( !job->process->waitForStarted( 5000 ) ) {
                fail( "Cannot start CLI job: " + job->process->errorString() );
            }
            jobs.insert( id, job );
            qInfo() << "MCP CLI: background job started" << id << mode;
            return jobState( id, job );
        }
        const auto requested = arguments.value( "timeout_ms" ).toDouble( 30000 );
        if ( requested < 1 || requested > 86400000 ) {
            fail( "timeout_ms must be between 1 and 86400000." );
        }
        const auto result = run( path, argv, int( requested ) );
        if ( result.code != 0 ) {
            fail( "CLI exit " + QString::number( result.code ) + ": "
                  + QString::fromUtf8( result.diagnostics + result.output ).left( 8192 ) );
        }
        return { { "exit_code", result.code },
                 { "stdout", QString::fromUtf8( result.output ) },
                 { "stderr", QString::fromUtf8( result.diagnostics ) },
                 { "output_truncated", result.truncated } };
    }
};

int serve( const QString& version )
{
    Runtime runtime;
    Session session( version, [ & ]( const QString& name, const QJsonObject& arguments ) {
        return runtime.call( name, arguments );
    } );
    QObject inbox;
    bool closed = false;
    const auto receive = [ & ]( const QByteArray& line ) {
        if ( closed || line.trimmed().isEmpty() ) {
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson( line, &error );
        if ( error.error != QJsonParseError::NoError ) {
            print( Session::error( QJsonValue::Null, -32700, "Invalid JSON." ) );
            return;
        }
        if ( !document.isObject() ) {
            print( Session::error( QJsonValue::Null, -32600, "Expected one JSON-RPC object." ) );
            return;
        }
        const auto request = document.object();
        if ( request.value( "jsonrpc" ) == QJsonValue( "2.0" ) && !request.contains( "id" )
             && request.value( "method" ) == QJsonValue( "notifications/cancelled" ) ) {
            if ( runtime.active
                 && request.value( "params" ).toObject().value( "requestId" )
                        == runtime.activeId ) {
                runtime.cancelled = true;
                runtime.active->kill();
                qInfo() << "MCP: cancelled active CLI request";
            }
            return;
        }
        const auto method = request.value( "method" ).toString();
        const bool executes = method == "tools/call" || method == "resources/read";
        if ( executes && runtime.busy ) {
            if ( request.contains( "id" ) ) {
                print( Session::error( request.value( "id" ), -32000,
                                       "Another CLI operation is active; wait or cancel it." ) );
            }
            return;
        }
        QScopedValueRollback<bool> busyGuard( runtime.busy, runtime.busy || executes );
        QScopedValueRollback<QJsonValue> idGuard( runtime.activeId, executes ? request.value( "id" )
                                                                             : runtime.activeId );
        const auto response = session.dispatch( request );
        if ( response && !closed ) {
            print( *response );
        }
    };
    std::thread reader( [ & ] {
        QByteArray pending;
        char buffer[ 8192 ];
        while ( true ) {
#ifdef Q_OS_WIN
            DWORD count = 0;
            if ( !ReadFile( GetStdHandle( STD_INPUT_HANDLE ), buffer, sizeof( buffer ), &count,
                            nullptr )
                 || !count ) {
                break;
            }
#else
            const auto count = ::read( STDIN_FILENO, buffer, sizeof( buffer ) );
            if ( count < 0 && errno == EINTR ) {
                continue;
            }
            if ( count <= 0 ) {
                break;
            }
#endif
            pending.append( buffer, int( count ) );
            qsizetype newline;
            while ( ( newline = pending.indexOf( '\n' ) ) >= 0 ) {
                const auto line = pending.left( newline );
                pending.remove( 0, newline + 1 );
                QMetaObject::invokeMethod(
                    &inbox, [ receive, line ] { receive( line ); }, Qt::QueuedConnection );
            }
            if ( pending.size() > outputLimit ) {
                break;
            }
        }
        QMetaObject::invokeMethod(
            &inbox,
            [ & ] {
                closed = true;
                runtime.shutdown();
                QCoreApplication::quit();
            },
            Qt::QueuedConnection );
    } );
    const auto code = QCoreApplication::exec();
    reader.join();
    return code;
}
} // namespace

int runCli( int argc, char* argv[], const QString& version )
{
#ifdef Q_OS_WIN
    // GUI subsystem binaries have no console by default. Redirected MCP pipes
    // are already valid; attach only for interactive command-line output.
    const auto output = GetStdHandle( STD_OUTPUT_HANDLE );
    if ( output == nullptr || output == INVALID_HANDLE_VALUE ) {
        AttachConsole( ATTACH_PARENT_PROCESS );
    }
#endif
    QCoreApplication app( argc, argv );
    QCoreApplication::setApplicationName( "cilogg" );
    QCoreApplication::setApplicationVersion( version );
    qInstallMessageHandler( []( QtMsgType, const QMessageLogContext&, const QString& text ) {
        writeBytes( text.toUtf8() + '\n', true );
    } );
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "CILogg built-in MCP server and automatic client setup.\n"
        "Subcommands: serve, install, uninstall, config, skill, commands, execute.\n"
        "Install detects clients by default; repeat --client or use --client all.\n"
        "Clients: codex, claude-code, cursor, vscode, claude-desktop.\n"
        "Skill files are generated automatically for supported clients." );
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        "subcommand", "serve | install | uninstall | config | skill | commands | execute" );
    parser.addPositionalArgument(
        "action", "Commander action for execute; inline arguments through --json." );
    parser.addOption( { "client", "Client name (repeatable), auto, or all.", "name" } );
    parser.addOption( { "home", "Use an isolated home directory for setup.", "directory" } );
    parser.addOption( { "dry-run", "Preview setup without writing files." } );
    parser.addOption( { "no-skills", "Register MCP without installing skills." } );
    parser.addOption( { "output", "Skill directory for skill subcommand.", "directory" } );
    parser.addOption( { "json", "Inline Commander argument JSON object.", "object", "{}" } );
    auto arguments = app.arguments();
    arguments.removeAt( 1 ); // 'mcp' was handled before normal application startup.
    if ( !parser.parse( arguments ) ) {
        writeBytes( parser.errorText().toUtf8() + '\n', true );
        return 1;
    }
    if ( parser.isSet( "version" ) ) {
        writeBytes( version.toUtf8() + '\n' );
        return 0;
    }
    if ( parser.isSet( "help" ) || parser.positionalArguments().isEmpty() ) {
        writeBytes( parser.helpText().toUtf8() );
        return 0;
    }
    if ( parser.isSet( "version" ) ) {
        writeBytes( version.toUtf8() + '\n' );
        return 0;
    }
    const auto command = parser.positionalArguments().front();
    const auto executable = app.applicationFilePath();
    try {
        if ( command == "serve" ) {
            return serve( version );
        }
        if ( command == "install" || command == "uninstall" ) {
            print( installClients( executable, parser.values( "client" ), parser.value( "home" ),
                                   parser.isSet( "dry-run" ), !parser.isSet( "no-skills" ),
                                   command == "uninstall" ) );
        }
        else if ( command == "config" ) {
            print( serverEntry( executable ) );
        }
        else if ( command == "skill" ) {
            if ( !parser.isSet( "output" ) ) {
                writeBytes( generatedSkill( executable ).toUtf8() );
            }
            else {
                print( { { "status", saveSkill( executable, parser.value( "output" ),
                                                parser.isSet( "dry-run" ) ) },
                         { "path", parser.value( "output" ) } } );
            }
        }
        else if ( command == "commands" ) {
            Runtime runtime;
            print( runtime.discovery( {} ) );
        }
        else if ( command == "execute" ) {
            QJsonParseError error;
            const auto data = QJsonDocument::fromJson( parser.value( "json" ).toUtf8(), &error );
            if ( !data.isObject() || parser.positionalArguments().size() != 2 ) {
                fail( "execute requires an action and --json containing an argument object." );
            }
            Runtime runtime;
            print( runtime.commander( parser.positionalArguments().at( 1 ), data.object() ) );
        }
        else {
            fail( "Unknown MCP subcommand: " + command );
        }
        return 0;
    } catch ( const std::exception& exception ) {
        writeBytes( QByteArray( exception.what() ) + '\n', true );
        return 1;
    }
}
} // namespace cilogg::mcp
