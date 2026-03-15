/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#ifndef KLOGG_CLI_H
#define KLOGG_CLI_H

#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <QStringList>

#include "commander.h"
#include "klogg_version.h"
#include "log.h"

struct CliParameters {
    struct CommanderParseResult {
        std::optional<CommanderRequest> request;
        bool exit_requested = false;
        int exit_code = EXIT_FAILURE;
        QString output_message;
    };

    bool new_session = false;
    bool load_session = false;
    bool multi_instance = false;
    bool log_to_file = false;
    bool follow_file = false;

    bool enable_logging = false;
    int log_level = 3;

    bool parse_error = false;
    QString parse_error_message;
    bool exit_requested = false;
    int exit_code = EXIT_SUCCESS;
    QString exit_message;

    std::vector<QString> filenames;
    std::optional<CommanderRequest> commander_request;

    int window_width = 0;
    int window_height = 0;

    QString pattern;

    CliParameters() = default;

    CliParameters( QCoreApplication& app, bool console = false )
    {
        parse( app.arguments(), console );
    }

    CliParameters( const QStringList& arguments, bool console = false )
    {
        parse( arguments, console );
    }

    bool isCommanderMode() const
    {
        return commander_request.has_value();
    }

    static QString versionText()
    {
        QString output;
        output += QStringLiteral( "klogg %1\n" ).arg( QString::fromLatin1( kloggVersion() ) );
        output += QStringLiteral( "Built %1 from %2(%3)\n" )
                      .arg( QString::fromLatin1( kloggBuildDate() ),
                            QString::fromLatin1( kloggCommit() ),
                            QString::fromLatin1( kloggGitVersion() ) );
        output += QStringLiteral(
            "Copyright (C) 2020 Nicolas Bonnefon, Anton Filimonov, Dmitry Kokotov and other contributors\n" );
        output += QStringLiteral(
            "This is free software.  You may redistribute copies of it under the terms of\n" );
        output += QStringLiteral(
            "the GNU General Public License <http://www.gnu.org/licenses/gpl.html>.\n" );
        output += QStringLiteral( "There is NO WARRANTY, to the extent permitted by law.\n" );
        return output;
    }

  private:
    static QString mainHelpDescription()
    {
        return QStringLiteral(
            "Klogg log viewer\n\n"
            "Commander mode:\n"
            "  klogg command --action open_file --file <path> [--follow]\n"
            "  klogg command --action open_url --url <url>\n"
            "  klogg command --action open_com --port <name> [serial options]\n"
            "  klogg command --action close_file --file <path>\n"
            "  klogg command --action close_url --url <url>\n"
            "  klogg command --action close_com --port <name>\n"
            "  klogg command --action close_klogg\n"
            "  klogg command --action close_all\n"
            "  klogg command --action get_info [--pretty]\n"
            "  klogg command --action start_comm [tab selector]\n"
            "  klogg command --action stop_comm [tab selector]\n"
            "  klogg command --action get_comm_status [tab selector] [--pretty]\n"
            "  klogg command --action start_logging [tab selector]\n"
            "  klogg command --action stop_logging [tab selector]\n"
            "  klogg command --action add_comment --text <value> [--timestamp] [tab selector]\n"
            "  klogg command --action get_response_counter (--id <id> | --name <name> | --all) [tab selector] [--pretty]\n"
            "  klogg command --action reset_response_counter (--id <id> | --name <name> | --all) [tab selector]\n"
            "  klogg command --action clear_comm [tab selector]\n"
            "  klogg command --action run_script --script-file <path> [--args-json-file <path>] [tab selector]\n"
            "  klogg command --action run_global_script --script-file <path> [--args-json-file <path>]\n"
            "  klogg command --action stop_script [tab selector | --all]\n"
            "  klogg command --action stop_global_script\n"
            "  klogg command --action get_script_status [tab selector | --all] [--pretty]\n"
            "  klogg command --action get_global_script_status [--pretty]\n"
            "  klogg command --action get_script_subscriptions [tab selector | --all] [--pretty]\n"
            "  klogg command --action get_global_script_subscriptions [--pretty]\n"
            "  klogg command --action clear_script_subscriptions [tab selector | --all]\n"
            "  klogg command --action clear_global_script_subscriptions\n"
            "  klogg command --action get_actions [--pretty]\n"
            "  klogg command --action get_responses [--pretty]\n"
            "  klogg command --action create_action --json-file <path>\n"
            "  klogg command --action update_action --id <id> --json-file <path>\n"
            "  klogg command --action delete_action --id <id>\n"
            "  klogg command --action create_response --json-file <path>\n"
            "  klogg command --action update_response --id <id> --json-file <path>\n"
            "  klogg command --action delete_response --id <id>\n"
            "  klogg command --action send_action --id <id> [tab selector]\n"
            "  klogg command --action wait_response (--id <id> | --name <name>) [tab selector] --timeout-ms <n>\n"
            "  klogg command --action get_filters [tab selector] [filter selector] [--predefined] [--pretty]\n"
            "  klogg command --action focus_tab (--tab-id <id> | --window-index <n> --tab-index <n>)\n"
            "  klogg command --action set_filter [tab selector] (--filter-id <id> | --filter-index <n> | --filter-string <expr>) [--predefined] [--search] [--auto-refresh]\n"
            "  klogg command --action close_tab (--tab-id <id> | --window-index <n> --tab-index <n>)\n\n"
            "Run `klogg command --help` for detailed commander options." );
    }

    static QString commanderHelpDescription()
    {
        return QStringLiteral(
            "Klogg commander\n\n"
            "Actions:\n"
            "  open_file  --file <path> [--follow]\n"
            "  open_url   --url <url>\n"
            "  open_com   --port <name> [--file <path>] [serial options]\n"
            "  close_file --file <path>\n"
            "  close_url  --url <url>\n"
            "  close_com  --port <name>\n"
            "  close_klogg\n"
            "  close_all\n"
            "  get_info   [--pretty|--preatty]\n"
            "  start_comm [--tab-id <id> | --window-index <n> --tab-index <n>]\n"
            "  stop_comm  [--tab-id <id> | --window-index <n> --tab-index <n>]\n"
            "  get_comm_status [--tab-id <id> | --window-index <n> --tab-index <n>] [--pretty|--preatty]\n"
            "  start_logging [--tab-id <id> | --window-index <n> --tab-index <n>]\n"
            "  stop_logging  [--tab-id <id> | --window-index <n> --tab-index <n>]\n"
            "  add_comment --text <value> [--timestamp] [--tab-id <id> | --window-index <n> --tab-index <n>]\n"
            "  get_response_counter (--id <id> | --name <name> | --all) [--tab-id <id> | --window-index <n> --tab-index <n>] [--pretty|--preatty]\n"
            "  reset_response_counter (--id <id> | --name <name> | --all) [--tab-id <id> | --window-index <n> --tab-index <n>]\n"
            "  clear_comm [--tab-id <id> | --window-index <n> --tab-index <n>]\n"
            "  run_script --script-file <path> [--args-json-file <path>] (--tab-id <id> | --window-index <n> --tab-index <n>)\n"
            "  run_global_script --script-file <path> [--args-json-file <path>]\n"
            "  stop_script [--tab-id <id> | --window-index <n> --tab-index <n> | --all]\n"
            "  stop_global_script\n"
            "  get_script_status [--tab-id <id> | --window-index <n> --tab-index <n> | --all] [--pretty|--preatty]\n"
            "  get_global_script_status [--pretty|--preatty]\n"
            "  get_script_subscriptions [--tab-id <id> | --window-index <n> --tab-index <n> | --all] [--pretty|--preatty]\n"
            "  get_global_script_subscriptions [--pretty|--preatty]\n"
            "  clear_script_subscriptions [--tab-id <id> | --window-index <n> --tab-index <n> | --all]\n"
            "  clear_global_script_subscriptions\n"
            "  get_actions [--pretty|--preatty]\n"
            "  get_responses [--pretty|--preatty]\n"
            "  create_action --json-file <path>\n"
            "  update_action --id <id> --json-file <path>\n"
            "  delete_action --id <id>\n"
            "  create_response --json-file <path>\n"
            "  update_response --id <id> --json-file <path>\n"
            "  delete_response --id <id>\n"
            "  send_action --id <id> [--tab-id <id> | --window-index <n> --tab-index <n>]\n"
            "  wait_response (--id <id> | --name <name>) [--tab-id <id> | --window-index <n> --tab-index <n>] --timeout-ms <ms>\n"
            "  get_filters [--tab-id <id> | --window-index <n> --tab-index <n>] [--filter-id <id> | --filter-index <n>] [--predefined] [--pretty|--preatty]\n"
            "  focus_tab  (--tab-id <id> | --window-index <n> --tab-index <n>)\n"
            "  set_filter [--tab-id <id> | --window-index <n> --tab-index <n>] (--filter-id <id> | --filter-index <n> | --filter-string <expr>) [--predefined] [--search] [--auto-refresh]\n"
            "  close_tab  (--tab-id <id> | --window-index <n> --tab-index <n>)\n\n"
            "For open_com, omitted serial options inherit the current Preferences values." );
    }

    static QString formatParserError( const QCommandLineParser& parser, const QString& errorMessage )
    {
        auto output = errorMessage.trimmed();
        const auto helpText = parser.helpText().trimmed();
        if ( !helpText.isEmpty() ) {
            output += QStringLiteral( "\n\n" ) + helpText;
        }
        return output;
    }

    void parse( const QStringList& arguments, bool console )
    {
        if ( !console && arguments.size() > 1
             && arguments.at( 1 ).compare( QStringLiteral( "command" ), Qt::CaseInsensitive ) == 0 ) {
            const auto commanderResult = parseCommanderArguments( arguments );
            commander_request = commanderResult.request;
            exit_requested = commanderResult.exit_requested;
            exit_code = commanderResult.exit_code;
            exit_message = commanderResult.output_message;
            if ( !commander_request && !exit_requested ) {
                parse_error = true;
                parse_error_message = commanderResult.output_message;
            }
            return;
        }

        QCommandLineParser parser;
        parser.setApplicationDescription( mainHelpDescription() );
        const auto helpOption = parser.addHelpOption();
        const auto versionOption = parser.addVersionOption();

        const QCommandLineOption multiInstanceOption(
            QStringList() << "m"
                          << "multi",
            "allow multiple instance of klogg to run simultaneously (use together with -s)" );

        const QCommandLineOption loadSessionOption(
            QStringList() << "s"
                          << "load-session",
            "load the previous session (default when no file is passed)" );

        const QCommandLineOption newSessionOption(
            QStringList() << "n"
                          << "new-session",
            "do not load the previous session (default when a file is passed)" );

        const QCommandLineOption logToFileOption( QStringList() << "l"
                                                                << "log",
                                                  "save the log to a file" );

        const QCommandLineOption followOption( QStringList() << "f"
                                                             << "follow",
                                               "follow initial opened files" );

        const QCommandLineOption patternOption( QStringList() << "e"
                                                              << "pattern",
                                                "pattern to search for", "pattern" );

        const QCommandLineOption debugOption(
            QStringList() << "d"
                          << "debug",
            "output more debug (increase number for more verbosity)", "debug_level", "0" );

        parser.addOption( debugOption );

        if ( !console ) {
            const QCommandLineOption windowWidthOption( "window-width", "new window width",
                                                        "1024" );
            const QCommandLineOption windowHeightOption( "window-height", "new window height",
                                                         "768" );
            parser.addOption( multiInstanceOption );
            parser.addOption( loadSessionOption );
            parser.addOption( newSessionOption );
            parser.addOption( logToFileOption );
            parser.addOption( followOption );
            parser.addOption( windowWidthOption );
            parser.addOption( windowHeightOption );
        }
        else {
            parser.addOption( patternOption );
        }

        if ( !parser.parse( arguments ) ) {
            parse_error = true;
            parse_error_message = formatParserError( parser, parser.errorText() );
            return;
        }

        if ( parser.isSet( helpOption ) ) {
            exit_requested = true;
            exit_code = EXIT_SUCCESS;
            exit_message = parser.helpText();
            return;
        }

        if ( parser.isSet( versionOption ) ) {
            exit_requested = true;
            exit_code = EXIT_SUCCESS;
            exit_message = versionText();
            return;
        }

        if ( parser.value( debugOption ).toInt() > 0 ) {
            enable_logging = true;
        }

        log_level += parser.value( debugOption ).toInt();

        if ( !console ) {
            if ( parser.isSet( multiInstanceOption ) ) {
                multi_instance = true;
            }

            if ( parser.isSet( loadSessionOption ) ) {
                load_session = true;
            }

            if ( parser.isSet( newSessionOption ) ) {
                new_session = true;
            }

            if ( parser.isSet( logToFileOption ) ) {
                log_to_file = true;
            }

            if ( parser.isSet( followOption ) ) {
                follow_file = true;
            }

            window_width = parser.value( QStringLiteral( "window-width" ) ).toInt();
            window_height = parser.value( QStringLiteral( "window-height" ) ).toInt();
        }
        else if ( parser.isSet( patternOption ) ) {
            pattern = parser.value( patternOption );
        }

        for ( const auto& file : parser.positionalArguments() ) {
            const auto fileInfo = QFileInfo( file );
            filenames.emplace_back( fileInfo.absoluteFilePath() );
        }
    }

    static std::optional<QSerialPort::DataBits> parseDataBits( const QString& rawValue )
    {
        bool ok = false;
        const auto value = rawValue.toInt( &ok );
        if ( !ok ) {
            return std::nullopt;
        }

        switch ( value ) {
        case 5:
            return QSerialPort::Data5;
        case 6:
            return QSerialPort::Data6;
        case 7:
            return QSerialPort::Data7;
        case 8:
            return QSerialPort::Data8;
        default:
            return std::nullopt;
        }
    }

    static std::optional<QSerialPort::Parity> parseParity( const QString& rawValue )
    {
        const auto value = rawValue.trimmed().toLower();
        if ( value == QStringLiteral( "none" ) ) {
            return QSerialPort::NoParity;
        }
        if ( value == QStringLiteral( "even" ) ) {
            return QSerialPort::EvenParity;
        }
        if ( value == QStringLiteral( "odd" ) ) {
            return QSerialPort::OddParity;
        }
        if ( value == QStringLiteral( "mark" ) ) {
            return QSerialPort::MarkParity;
        }
        if ( value == QStringLiteral( "space" ) ) {
            return QSerialPort::SpaceParity;
        }

        return std::nullopt;
    }

    static std::optional<QSerialPort::StopBits> parseStopBits( const QString& rawValue )
    {
        const auto value = rawValue.trimmed().toLower();
        if ( value == QStringLiteral( "1" ) ) {
            return QSerialPort::OneStop;
        }
        if ( value == QStringLiteral( "1.5" ) ) {
            return QSerialPort::OneAndHalfStop;
        }
        if ( value == QStringLiteral( "2" ) ) {
            return QSerialPort::TwoStop;
        }

        return std::nullopt;
    }

    static std::optional<QSerialPort::FlowControl> parseFlowControl( const QString& rawValue )
    {
        const auto value = rawValue.trimmed().toLower();
        if ( value == QStringLiteral( "none" ) ) {
            return QSerialPort::NoFlowControl;
        }
        if ( value == QStringLiteral( "hardware" ) || value == QStringLiteral( "rts/cts" ) ) {
            return QSerialPort::HardwareControl;
        }
        if ( value == QStringLiteral( "software" ) || value == QStringLiteral( "xon/xoff" ) ) {
            return QSerialPort::SoftwareControl;
        }

        return std::nullopt;
    }

    static bool validateExclusiveBooleanOptions( QCommandLineParser& parser,
                                                 const QCommandLineOption& enabledOption,
                                                 const QCommandLineOption& disabledOption,
                                                 QString* errorMessage )
    {
        if ( parser.isSet( enabledOption ) && parser.isSet( disabledOption ) ) {
            *errorMessage = QStringLiteral( "Options --%1 and --%2 cannot be used together." )
                                .arg( enabledOption.names().back(), disabledOption.names().back() );
            return false;
        }

        return true;
    }

    static std::optional<bool> parseOptionalBoolean( QCommandLineParser& parser,
                                                     const QCommandLineOption& enabledOption,
                                                     const QCommandLineOption& disabledOption )
    {
        if ( parser.isSet( enabledOption ) ) {
            return true;
        }
        if ( parser.isSet( disabledOption ) ) {
            return false;
        }

        return std::nullopt;
    }

    static std::optional<QVariantMap> loadJsonObjectFile( const QString& path,
                                                          QString* errorMessage )
    {
        QFile file( path );
        if ( !file.open( QIODevice::ReadOnly ) ) {
            if ( errorMessage != nullptr ) {
                *errorMessage
                    = QStringLiteral( "Failed to open JSON file %1: %2" ).arg( path, file.errorString() );
            }
            return std::nullopt;
        }

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson( file.readAll(), &parseError );
        if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
            if ( errorMessage != nullptr ) {
                *errorMessage = QStringLiteral( "Invalid JSON object in %1." ).arg( path );
            }
            return std::nullopt;
        }

        return document.object().toVariantMap();
    }

    static CommanderParseResult parseCommanderArguments( const QStringList& arguments )
    {
        CommanderParseResult result;
        QStringList commandArguments;
        commandArguments.reserve( arguments.size() - 1 );
        commandArguments.push_back( arguments.value( 0 ) + QStringLiteral( " command" ) );
        for ( int i = 2; i < arguments.size(); ++i ) {
            commandArguments.push_back( arguments.at( i ) );
        }

        QCommandLineParser parser;
        parser.setApplicationDescription( commanderHelpDescription() );
        const auto helpOption = parser.addHelpOption();
        const auto versionOption = parser.addVersionOption();

        const QCommandLineOption actionOption( QStringLiteral( "action" ),
                                               QStringLiteral( "Commander action to execute." ),
                                               QStringLiteral( "action" ) );
        const QCommandLineOption fileOption( QStringLiteral( "file" ),
                                             QStringLiteral( "Target file path." ),
                                             QStringLiteral( "path" ) );
        const QCommandLineOption urlOption( QStringLiteral( "url" ),
                                            QStringLiteral( "Target URL." ),
                                            QStringLiteral( "url" ) );
        const QCommandLineOption portOption( QStringLiteral( "port" ),
                                             QStringLiteral( "Target COM port name." ),
                                             QStringLiteral( "port" ) );
        const QCommandLineOption tabIdOption( QStringLiteral( "tab-id" ),
                                              QStringLiteral( "Target tab id." ),
                                              QStringLiteral( "id" ) );
        const QCommandLineOption filterIdOption( QStringLiteral( "filter-id" ),
                                                 QStringLiteral( "Target filter id." ),
                                                 QStringLiteral( "id" ) );
        const QCommandLineOption filterIndexOption( QStringLiteral( "filter-index" ),
                                                    QStringLiteral( "Target filter index." ),
                                                    QStringLiteral( "index" ) );
        const QCommandLineOption filterStringOption(
            QStringLiteral( "filter-string" ),
            QStringLiteral( "Literal filter/search expression to apply." ),
            QStringLiteral( "expr" ) );
        const QCommandLineOption textOption( QStringLiteral( "text" ),
                                             QStringLiteral( "Comment text for add_comment." ),
                                             QStringLiteral( "text" ) );
        const QCommandLineOption scriptFileOption( QStringLiteral( "script-file" ),
                                                   QStringLiteral( "Python script file to run." ),
                                                   QStringLiteral( "path" ) );
        const QCommandLineOption argsJsonFileOption(
            QStringLiteral( "args-json-file" ),
            QStringLiteral( "Optional JSON file passed to the Python script." ),
            QStringLiteral( "path" ) );
        const QCommandLineOption timestampOption(
            QStringLiteral( "timestamp" ),
            QStringLiteral( "Prefix add_comment output with a timestamp." ) );
        const QCommandLineOption allOption(
            QStringLiteral( "all" ),
            QStringLiteral( "Target all matching response counters." ) );
        const QCommandLineOption idOption( QStringLiteral( "id" ),
                                           QStringLiteral( "Action/response id." ),
                                           QStringLiteral( "id" ) );
        const QCommandLineOption nameOption( QStringLiteral( "name" ),
                                             QStringLiteral( "Action/response name." ),
                                             QStringLiteral( "name" ) );
        const QCommandLineOption jsonFileOption( QStringLiteral( "json-file" ),
                                                 QStringLiteral( "Path to a JSON object payload." ),
                                                 QStringLiteral( "path" ) );
        const QCommandLineOption timeoutMsOption( QStringLiteral( "timeout-ms" ),
                                                  QStringLiteral( "Timeout in milliseconds." ),
                                                  QStringLiteral( "ms" ) );
        const QCommandLineOption predefinedOption(
            QStringLiteral( "predefined" ),
            QStringLiteral( "Use predefined filters instead of search-history filters." ) );
        const QCommandLineOption windowIndexOption(
            QStringLiteral( "window-index" ),
            QStringLiteral( "Target window index for tab-focused actions." ),
            QStringLiteral( "index" ) );
        const QCommandLineOption tabIndexOption( QStringLiteral( "tab-index" ),
                                                 QStringLiteral( "Target tab index for tab-focused actions." ),
                                                 QStringLiteral( "index" ) );
        const QCommandLineOption followOption( QStringLiteral( "follow" ),
                                               QStringLiteral( "Follow the opened file." ) );
        const QCommandLineOption prettyOption(
            QStringList{ QStringLiteral( "pretty" ), QStringLiteral( "preatty" ) },
            QStringLiteral( "Print formatted JSON output." ) );
        const QCommandLineOption searchOption( QStringLiteral( "search" ),
                                               QStringLiteral( "Run search after setting the filter." ) );
        const QCommandLineOption autoRefreshOption(
            QStringLiteral( "auto-refresh" ),
            QStringLiteral( "Rearm auto-refresh after applying the filter." ) );
        const QCommandLineOption baudOption( QStringLiteral( "baud" ),
                                             QStringLiteral( "COM baud rate." ),
                                             QStringLiteral( "baud" ) );
        const QCommandLineOption dataBitsOption( QStringLiteral( "data-bits" ),
                                                 QStringLiteral( "COM data bits (5, 6, 7, 8)." ),
                                                 QStringLiteral( "bits" ) );
        const QCommandLineOption parityOption( QStringLiteral( "parity" ),
                                               QStringLiteral( "COM parity (none, even, odd, mark, space)." ),
                                               QStringLiteral( "parity" ) );
        const QCommandLineOption stopBitsOption( QStringLiteral( "stop-bits" ),
                                                 QStringLiteral( "COM stop bits (1, 1.5, 2)." ),
                                                 QStringLiteral( "stop_bits" ) );
        const QCommandLineOption flowControlOption(
            QStringLiteral( "flow-control" ),
            QStringLiteral( "COM flow control (none, hardware, software)." ),
            QStringLiteral( "flow_control" ) );
        const QCommandLineOption timestampsOption( QStringLiteral( "timestamps" ),
                                                   QStringLiteral( "Enable timestamps in COM capture." ) );
        const QCommandLineOption noTimestampsOption(
            QStringLiteral( "no-timestamps" ),
            QStringLiteral( "Disable timestamps in COM capture." ) );
        const QCommandLineOption timestampFormatOption(
            QStringLiteral( "timestamp-format" ),
            QStringLiteral( "Timestamp format for COM capture." ),
            QStringLiteral( "format" ) );
        const QCommandLineOption logTransmitsOption(
            QStringLiteral( "log-transmits" ),
            QStringLiteral( "Enable COM transmit logging." ) );
        const QCommandLineOption noLogTransmitsOption(
            QStringLiteral( "no-log-transmits" ),
            QStringLiteral( "Disable COM transmit logging." ) );
        const QCommandLineOption useForActionsOption(
            QStringLiteral( "use-for-actions" ),
            QStringLiteral( "Use the COM capture as the actions port." ) );
        const QCommandLineOption noUseForActionsOption(
            QStringLiteral( "no-use-for-actions" ),
            QStringLiteral( "Do not use the COM capture as the actions port." ) );

        parser.addOption( actionOption );
        parser.addOption( fileOption );
        parser.addOption( urlOption );
        parser.addOption( portOption );
        parser.addOption( tabIdOption );
        parser.addOption( filterIdOption );
        parser.addOption( filterIndexOption );
        parser.addOption( filterStringOption );
        parser.addOption( textOption );
        parser.addOption( scriptFileOption );
        parser.addOption( argsJsonFileOption );
        parser.addOption( timestampOption );
        parser.addOption( allOption );
        parser.addOption( idOption );
        parser.addOption( nameOption );
        parser.addOption( jsonFileOption );
        parser.addOption( timeoutMsOption );
        parser.addOption( predefinedOption );
        parser.addOption( windowIndexOption );
        parser.addOption( tabIndexOption );
        parser.addOption( followOption );
        parser.addOption( prettyOption );
        parser.addOption( searchOption );
        parser.addOption( autoRefreshOption );
        parser.addOption( baudOption );
        parser.addOption( dataBitsOption );
        parser.addOption( parityOption );
        parser.addOption( stopBitsOption );
        parser.addOption( flowControlOption );
        parser.addOption( timestampsOption );
        parser.addOption( noTimestampsOption );
        parser.addOption( timestampFormatOption );
        parser.addOption( logTransmitsOption );
        parser.addOption( noLogTransmitsOption );
        parser.addOption( useForActionsOption );
        parser.addOption( noUseForActionsOption );

        if ( !parser.parse( commandArguments ) ) {
            result.output_message = formatParserError( parser, parser.errorText() );
            return result;
        }

        if ( parser.isSet( helpOption ) ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = parser.helpText();
            return result;
        }
        if ( parser.isSet( versionOption ) ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = versionText();
            return result;
        }

        if ( !validateExclusiveBooleanOptions( parser, timestampsOption, noTimestampsOption,
                                               &result.output_message )
             || !validateExclusiveBooleanOptions( parser, logTransmitsOption, noLogTransmitsOption,
                                                  &result.output_message )
             || !validateExclusiveBooleanOptions( parser, useForActionsOption,
                                                  noUseForActionsOption, &result.output_message ) ) {
            result.output_message = formatParserError( parser, result.output_message );
            return result;
        }

        const auto action = commanderActionFromString( parser.value( actionOption ) );
        if ( !action ) {
            result.output_message
                = formatParserError( parser, QStringLiteral( "Missing or invalid --action value." ) );
            return result;
        }

        CommanderRequest request;
        request.action = *action;
        request.followFile = parser.isSet( followOption );
        request.prettyOutput = parser.isSet( prettyOption );
        request.runSearch = parser.isSet( searchOption );
        request.rearmAutoRefresh = parser.isSet( autoRefreshOption );
        request.predefinedFilters = parser.isSet( predefinedOption );
        request.entityName = parser.value( nameOption ).trimmed();
        request.commentText = parser.value( textOption );
        request.scriptFilePath = normalizeCommanderFilePath( parser.value( scriptFileOption ) );
        request.argsJsonFilePath
            = normalizeCommanderFilePath( parser.value( argsJsonFileOption ) );
        request.allEntities = parser.isSet( allOption );
        request.timestampComment = parser.isSet( timestampOption );
        if ( request.rearmAutoRefresh ) {
            request.runSearch = true;
        }

        if ( parser.isSet( idOption ) ) {
            bool ok = false;
            const auto parsedId = parser.value( idOption ).toInt( &ok );
            if ( !ok || parsedId < 0 ) {
                result.output_message
                    = formatParserError( parser, QStringLiteral( "Invalid --id value." ) );
                return result;
            }
            request.entityId = parsedId;
        }

        if ( parser.isSet( timeoutMsOption ) ) {
            bool ok = false;
            const auto parsedTimeout = parser.value( timeoutMsOption ).toInt( &ok );
            if ( !ok || parsedTimeout <= 0 ) {
                result.output_message = formatParserError(
                    parser, QStringLiteral( "Invalid --timeout-ms value." ) );
                return result;
            }
            request.timeoutMs = parsedTimeout;
        }

        const auto tabId = parser.value( tabIdOption ).trimmed();
        const bool hasTabId = !tabId.isEmpty();
        const bool hasWindowIndex = parser.isSet( windowIndexOption );
        const bool hasTabIndex = parser.isSet( tabIndexOption );
        const auto validateTabSelector = [ & ]( const QString& actionName,
                                                bool selectorRequired ) -> bool {
            if ( hasTabId && ( hasWindowIndex || hasTabIndex ) ) {
                result.output_message = formatParserError(
                    parser,
                    QStringLiteral( "Use either --tab-id or --window-index with --tab-index for %1." )
                        .arg( actionName ) );
                return false;
            }
            if ( hasWindowIndex != hasTabIndex ) {
                result.output_message = formatParserError(
                    parser,
                    QStringLiteral( "--window-index and --tab-index must be used together for %1." )
                        .arg( actionName ) );
                return false;
            }
            if ( selectorRequired && !hasTabId && !hasWindowIndex ) {
                result.output_message = formatParserError(
                    parser,
                    QStringLiteral( "%1 requires --tab-id or --window-index with --tab-index." )
                        .arg( actionName ) );
                return false;
            }

            if ( hasTabId ) {
                request.tabId = tabId;
                return true;
            }

            if ( hasWindowIndex ) {
                bool windowOk = false;
                const auto parsedWindowIndex = parser.value( windowIndexOption ).toInt( &windowOk );
                bool tabOk = false;
                const auto parsedTabIndex = parser.value( tabIndexOption ).toInt( &tabOk );
                if ( !windowOk || parsedWindowIndex < 0 ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "Invalid --window-index value." ) );
                    return false;
                }
                if ( !tabOk || parsedTabIndex < 0 ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "Invalid --tab-index value." ) );
                    return false;
                }

                request.windowIndex = parsedWindowIndex;
                request.tabIndex = parsedTabIndex;
            }

            return true;
        };

        switch ( *action ) {
        case CommanderAction::OpenFile:
        case CommanderAction::CloseFile:
            request.filePath = normalizeCommanderFilePath( parser.value( fileOption ) );
            if ( request.filePath.isEmpty() ) {
                result.output_message
                    = formatParserError( parser, QStringLiteral( "--file is required for this action." ) );
                return result;
            }
            break;
        case CommanderAction::OpenUrl:
        case CommanderAction::CloseUrl:
            request.url = normalizeCommanderUrl( parser.value( urlOption ) );
            if ( request.url.isEmpty() ) {
                result.output_message
                    = formatParserError( parser, QStringLiteral( "--url is required for this action." ) );
                return result;
            }
            break;
        case CommanderAction::OpenCom:
        case CommanderAction::CloseCom:
            request.portName = parser.value( portOption ).trimmed();
            if ( request.portName.isEmpty() ) {
                result.output_message
                    = formatParserError( parser, QStringLiteral( "--port is required for this action." ) );
                return result;
            }
            break;
        case CommanderAction::FocusTab:
        case CommanderAction::CloseTab:
            if ( !validateTabSelector( commanderActionToString( *action ), true ) ) {
                return result;
            }
            break;
        case CommanderAction::GetFilters:
            if ( !validateTabSelector( commanderActionToString( *action ), false ) ) {
                return result;
            }
            break;
        case CommanderAction::SetFilter:
            if ( !validateTabSelector( commanderActionToString( *action ), false ) ) {
                return result;
            }

            request.filterId = parser.value( filterIdOption ).trimmed();
            if ( parser.isSet( filterIndexOption ) ) {
                bool ok = false;
                const auto parsedFilterIndex = parser.value( filterIndexOption ).toInt( &ok );
                if ( !ok || parsedFilterIndex < 0 ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "Invalid --filter-index value." ) );
                    return result;
                }
                request.filterIndex = parsedFilterIndex;
            }
            request.filterString = parser.value( filterStringOption );

            {
                int selectors = 0;
                selectors += request.filterId.isEmpty() ? 0 : 1;
                selectors += request.filterIndex ? 1 : 0;
                selectors += request.filterString.isEmpty() ? 0 : 1;
                if ( selectors != 1 ) {
                    result.output_message = formatParserError(
                        parser,
                        QStringLiteral(
                            "set_filter requires exactly one of --filter-id, --filter-index, or --filter-string." ) );
                    return result;
                }
            }
            break;
        case CommanderAction::GetActions:
        case CommanderAction::GetResponses:
            break;
        case CommanderAction::CreateAction:
        case CommanderAction::CreateResponse:
            if ( !parser.isSet( jsonFileOption ) ) {
                result.output_message = formatParserError(
                    parser, QStringLiteral( "--json-file is required for this action." ) );
                return result;
            }
            break;
        case CommanderAction::UpdateAction:
        case CommanderAction::DeleteAction:
        case CommanderAction::UpdateResponse:
        case CommanderAction::DeleteResponse:
        case CommanderAction::SendAction:
            if ( *action == CommanderAction::SendAction
                 && !validateTabSelector( commanderActionToString( *action ), false ) ) {
                return result;
            }
            if ( !request.entityId ) {
                result.output_message = formatParserError(
                    parser, QStringLiteral( "--id is required for this action." ) );
                return result;
            }
            if ( ( *action == CommanderAction::UpdateAction
                   || *action == CommanderAction::UpdateResponse )
                 && !parser.isSet( jsonFileOption ) ) {
                result.output_message = formatParserError(
                    parser, QStringLiteral( "--json-file is required for this action." ) );
                return result;
            }
            break;
        case CommanderAction::WaitResponse:
            if ( !validateTabSelector( commanderActionToString( *action ), false ) ) {
                return result;
            }
            if ( ( request.entityId ? 1 : 0 ) + ( request.entityName.isEmpty() ? 0 : 1 ) != 1 ) {
                result.output_message = formatParserError(
                    parser, QStringLiteral( "wait_response requires exactly one of --id or --name." ) );
                return result;
            }
            if ( !request.timeoutMs ) {
                result.output_message = formatParserError(
                    parser, QStringLiteral( "--timeout-ms is required for wait_response." ) );
                return result;
            }
            break;
        case CommanderAction::StartComm:
        case CommanderAction::StopComm:
        case CommanderAction::GetCommStatus:
        case CommanderAction::StartLogging:
        case CommanderAction::StopLogging:
        case CommanderAction::ClearComm:
            if ( !validateTabSelector( commanderActionToString( *action ), false ) ) {
                return result;
            }
            break;
        case CommanderAction::AddComment:
            if ( !validateTabSelector( commanderActionToString( *action ), false ) ) {
                return result;
            }
            request.commentText = request.commentText.trimmed();
            if ( request.commentText.isEmpty() ) {
                result.output_message = formatParserError(
                    parser, QStringLiteral( "--text is required for add_comment." ) );
                return result;
            }
            break;
        case CommanderAction::GetResponseCounter:
        case CommanderAction::ResetResponseCounter:
            if ( !validateTabSelector( commanderActionToString( *action ), false ) ) {
                return result;
            }
            if ( ( request.entityId ? 1 : 0 ) + ( request.entityName.isEmpty() ? 0 : 1 )
                     + ( request.allEntities ? 1 : 0 )
                 != 1 ) {
                result.output_message = formatParserError(
                    parser,
                    QStringLiteral( "%1 requires exactly one of --id, --name, or --all." )
                        .arg( commanderActionToString( *action ) ) );
                return result;
            }
            break;
            case CommanderAction::RunScript:
                if ( !validateTabSelector( commanderActionToString( *action ), true ) ) {
                    return result;
                }
                if ( request.scriptFilePath.isEmpty() ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "--script-file is required for run_script." ) );
                    return result;
                }
                break;
            case CommanderAction::RunGlobalScript:
                if ( hasTabId || hasWindowIndex || hasTabIndex || parser.isSet( allOption ) ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "run_global_script does not accept tab selectors or --all." ) );
                    return result;
                }
                if ( request.scriptFilePath.isEmpty() ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "--script-file is required for run_global_script." ) );
                    return result;
                }
                break;
            case CommanderAction::StopScript:
            case CommanderAction::GetScriptStatus:
            case CommanderAction::GetScriptSubscriptions:
            case CommanderAction::ClearScriptSubscriptions:
                if ( parser.isSet( allOption ) && ( hasTabId || hasWindowIndex || hasTabIndex ) ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "Use either a tab selector or --all for this action." ) );
                    return result;
                }
                if ( !parser.isSet( allOption )
                     && !validateTabSelector( commanderActionToString( *action ), true ) ) {
                    return result;
                }
                break;
            case CommanderAction::StopGlobalScript:
            case CommanderAction::GetGlobalScriptStatus:
            case CommanderAction::GetGlobalScriptSubscriptions:
            case CommanderAction::ClearGlobalScriptSubscriptions:
                if ( hasTabId || hasWindowIndex || hasTabIndex || parser.isSet( allOption ) ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "%1 does not accept tab selectors or --all." )
                                    .arg( commanderActionToString( *action ) ) );
                    return result;
                }
                break;
        case CommanderAction::CloseKlogg:
        case CommanderAction::CloseAll:
            if ( parser.isSet( prettyOption ) || parser.isSet( searchOption )
                 || parser.isSet( autoRefreshOption ) ) {
                result.output_message = formatParserError(
                    parser, QStringLiteral( "Unsupported options were provided for this action." ) );
                return result;
            }
            break;
        case CommanderAction::GetInfo:
            break;
        case CommanderAction::None:
            break;
        }

        if ( *action == CommanderAction::GetFilters ) {
            request.filterId = parser.value( filterIdOption ).trimmed();
            if ( parser.isSet( filterIndexOption ) ) {
                bool ok = false;
                const auto parsedFilterIndex = parser.value( filterIndexOption ).toInt( &ok );
                if ( !ok || parsedFilterIndex < 0 ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "Invalid --filter-index value." ) );
                    return result;
                }
                request.filterIndex = parsedFilterIndex;
            }

            if ( !request.filterId.isEmpty() && request.filterIndex ) {
                result.output_message = formatParserError(
                    parser, QStringLiteral( "Use either --filter-id or --filter-index for get_filters." ) );
                return result;
            }
        }

        if ( *action == CommanderAction::OpenCom ) {
            request.comSettings.portName = request.portName;

            if ( parser.isSet( fileOption ) ) {
                const auto filePath = parser.value( fileOption ).trimmed();
                request.comSettings.filePath = filePath.isEmpty()
                                                   ? QString{}
                                                   : normalizeCommanderFilePath( filePath );
            }

            if ( parser.isSet( baudOption ) ) {
                bool ok = false;
                const auto baudRate = parser.value( baudOption ).toInt( &ok );
                if ( !ok || baudRate <= 0 ) {
                    result.output_message
                        = formatParserError( parser, QStringLiteral( "Invalid --baud value." ) );
                    return result;
                }
                request.comSettings.baudRate = baudRate;
            }

            if ( parser.isSet( dataBitsOption ) ) {
                request.comSettings.dataBits = parseDataBits( parser.value( dataBitsOption ) );
                if ( !request.comSettings.dataBits ) {
                    result.output_message
                        = formatParserError( parser, QStringLiteral( "Invalid --data-bits value." ) );
                    return result;
                }
            }

            if ( parser.isSet( parityOption ) ) {
                request.comSettings.parity = parseParity( parser.value( parityOption ) );
                if ( !request.comSettings.parity ) {
                    result.output_message
                        = formatParserError( parser, QStringLiteral( "Invalid --parity value." ) );
                    return result;
                }
            }

            if ( parser.isSet( stopBitsOption ) ) {
                request.comSettings.stopBits = parseStopBits( parser.value( stopBitsOption ) );
                if ( !request.comSettings.stopBits ) {
                    result.output_message
                        = formatParserError( parser, QStringLiteral( "Invalid --stop-bits value." ) );
                    return result;
                }
            }

            if ( parser.isSet( flowControlOption ) ) {
                request.comSettings.flowControl
                    = parseFlowControl( parser.value( flowControlOption ) );
                if ( !request.comSettings.flowControl ) {
                    result.output_message
                        = formatParserError( parser, QStringLiteral( "Invalid --flow-control value." ) );
                    return result;
                }
            }

            request.comSettings.addTimestamps
                = parseOptionalBoolean( parser, timestampsOption, noTimestampsOption );
            request.comSettings.logTransmits
                = parseOptionalBoolean( parser, logTransmitsOption, noLogTransmitsOption );
            request.comSettings.useForActions
                = parseOptionalBoolean( parser, useForActionsOption, noUseForActionsOption );

            if ( parser.isSet( timestampFormatOption ) ) {
                request.comSettings.timestampFormat
                    = parser.value( timestampFormatOption ).trimmed();
                if ( request.comSettings.timestampFormat->isEmpty() ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "Invalid --timestamp-format value." ) );
                    return result;
                }
            }
        }

        if ( *action == CommanderAction::CreateAction || *action == CommanderAction::UpdateAction
             || *action == CommanderAction::CreateResponse
             || *action == CommanderAction::UpdateResponse ) {
            QString errorMessage;
            const auto payload = loadJsonObjectFile( parser.value( jsonFileOption ), &errorMessage );
            if ( !payload ) {
                result.output_message = formatParserError( parser, errorMessage );
                return result;
            }
            request.definitionPayload = *payload;
        }

        result.request = request;
        return result;
    }
};

#endif
