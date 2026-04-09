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
#include "labrequest.h"
#include "log.h"
#include "scenariobatchrequest.h"

struct CliParameters {
    struct CommanderParseResult {
        std::optional<CommanderRequest> request;
        bool exit_requested = false;
        int exit_code = EXIT_FAILURE;
        QString output_message;
    };

    struct ScenarioBatchParseResult {
        std::optional<ScenarioBatchRequest> request;
        bool exit_requested = false;
        int exit_code = EXIT_FAILURE;
        QString output_message;
    };

    struct LabParseResult {
        std::optional<LabCliRequest> request;
        bool exit_requested = false;
        int exit_code = EXIT_FAILURE;
        QString output_message;
    };

    bool new_session = false;
    bool load_session = false;
    bool multi_instance = false;
    bool log_to_file = false;
    bool follow_file = false;
    bool dump_ui_tree = false;
    QString dump_state_json_path;

    bool enable_logging = false;
    int log_level = 3;

    bool parse_error = false;
    QString parse_error_message;
    bool exit_requested = false;
    int exit_code = EXIT_SUCCESS;
    QString exit_message;

    std::vector<QString> filenames;
    std::optional<CommanderRequest> commander_request;
    std::optional<ScenarioBatchRequest> scenario_batch_request;
    std::optional<LabCliRequest> lab_request;

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

    bool isScenarioBatchMode() const
    {
        return scenario_batch_request.has_value();
    }

    bool isLabMode() const
    {
        return lab_request.has_value();
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
            "Scenario batch mode:\n"
            "  klogg scenario run --suite-file <path> [--device-map-file <path>] [--report-dir <path>]\n"
            "  klogg scenario run --scenario-file <path> [--args-json-file <path>] [--device-map-file <path>] [--report-dir <path>]\n"
            "  klogg scenario validate --suite-file <path> [--device-map-file <path>]\n"
            "  klogg scenario list-devices --suite-file <path>\n\n"
            "Remote lab mode:\n"
            "  klogg lab-controller serve --listen <host:port> --state-dir <path> --token-file <path>\n"
            "  klogg lab-agent run --controller-url <url> --agent-config <path> --token-file <path>\n"
            "  klogg lab submit --controller-url <url> --token-file <path> (--suite-file <path> | --scenario-file <path>) [--args-json-file <path>] [--agent-label <label>] [--report-dir <path>]\n"
            "  klogg lab queue --controller-url <url> --token-file <path>\n"
            "  klogg lab status --controller-url <url> --token-file <path> --job-id <id>\n"
            "  klogg lab cancel --controller-url <url> --token-file <path> --job-id <id>\n"
            "  klogg lab agents --controller-url <url> --token-file <path>\n"
            "  klogg lab artifacts --controller-url <url> --token-file <path> --job-id <id> --output-dir <path>\n\n"
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
            "  klogg command --action run_scenario --scenario-file <path> [--args-json-file <path>]\n"
            "  klogg command --action run_suite --suite-file <path>\n"
            "  klogg command --action stop_script [tab selector | --all]\n"
            "  klogg command --action stop_global_script\n"
            "  klogg command --action stop_scenario_run\n"
            "  klogg command --action get_script_status [tab selector | --all] [--pretty]\n"
            "  klogg command --action get_global_script_status [--pretty]\n"
            "  klogg command --action get_scenario_status [--pretty]\n"
            "  klogg command --action get_script_subscriptions [tab selector | --all] [--pretty]\n"
            "  klogg command --action get_global_script_subscriptions [--pretty]\n"
            "  klogg command --action clear_script_subscriptions [tab selector | --all]\n"
            "  klogg command --action clear_global_script_subscriptions\n"
            "  klogg command --action get_scenario_report [--pretty]\n"
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
            "Automation:\n"
            "  klogg --dump-ui-tree [--window-width <n> --window-height <n>]\n\n"
            "  klogg --dump-state-json <path> [--window-width <n> --window-height <n>]\n\n"
            "Run `klogg scenario --help` for detailed scenario batch options.\n"
            "Run `klogg lab --help`, `klogg lab-agent --help`, or `klogg lab-controller --help` for remote lab options.\n"
            "Run `klogg command --help` for detailed commander options." );
    }

    static QString scenarioHelpDescription()
    {
        return QStringLiteral(
            "Klogg headless scenario runner\n\n"
            "Usage:\n"
            "  klogg scenario run --suite-file <path> [--device-map-file <path>] [--report-dir <path>]\n"
            "  klogg scenario run --scenario-file <path> [--args-json-file <path>] [--device-map-file <path>] [--report-dir <path>]\n"
            "  klogg scenario validate --suite-file <path> [--device-map-file <path>]\n"
            "  klogg scenario list-devices --suite-file <path>\n\n"
            "The suite manifest declares logical device names. The optional device-map JSON\n"
            "maps those logical device names to real COM ports and runtime serial overrides." );
    }

    static QString labControllerHelpDescription()
    {
        return QStringLiteral(
            "Klogg remote lab controller\n\n"
            "Usage:\n"
            "  klogg lab-controller serve --listen <host:port> --state-dir <path> --token-file <path>\n\n"
            "The controller exposes HTTP JSON APIs for operators and a TCP agent channel on the\n"
            "next port number after the HTTP listen port." );
    }

    static QString labAgentHelpDescription()
    {
        return QStringLiteral(
            "Klogg remote lab agent\n\n"
            "Usage:\n"
            "  klogg lab-agent run --controller-url <url> --agent-config <path> --token-file <path>\n\n"
            "The agent registers local COM inventory with the controller and executes queued jobs." );
    }

    static QString labHelpDescription()
    {
        return QStringLiteral(
            "Klogg remote lab operator CLI\n\n"
            "Usage:\n"
            "  klogg lab submit --controller-url <url> --token-file <path> (--suite-file <path> | --scenario-file <path>) [--args-json-file <path>] [--agent-label <label>] [--report-dir <path>]\n"
            "  klogg lab queue --controller-url <url> --token-file <path>\n"
            "  klogg lab status --controller-url <url> --token-file <path> --job-id <id>\n"
            "  klogg lab cancel --controller-url <url> --token-file <path> --job-id <id>\n"
            "  klogg lab agents --controller-url <url> --token-file <path>\n"
            "  klogg lab artifacts --controller-url <url> --token-file <path> --job-id <id> --output-dir <path>\n\n"
            "The CLI uploads scenario bundles to the controller and fetches status, queue, and artifacts." );
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
            "  run_scenario --scenario-file <path> [--args-json-file <path>]\n"
            "  run_suite --suite-file <path>\n"
            "  stop_script [--tab-id <id> | --window-index <n> --tab-index <n> | --all]\n"
            "  stop_global_script\n"
            "  stop_scenario_run\n"
            "  get_script_status [--tab-id <id> | --window-index <n> --tab-index <n> | --all] [--pretty|--preatty]\n"
            "  get_global_script_status [--pretty|--preatty]\n"
            "  get_scenario_status [--pretty|--preatty]\n"
            "  get_script_subscriptions [--tab-id <id> | --window-index <n> --tab-index <n> | --all] [--pretty|--preatty]\n"
            "  get_global_script_subscriptions [--pretty|--preatty]\n"
            "  clear_script_subscriptions [--tab-id <id> | --window-index <n> --tab-index <n> | --all]\n"
            "  clear_global_script_subscriptions\n"
            "  get_scenario_report [--pretty|--preatty]\n"
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
            "  close_tab  (--tab-id <id> | --window-index <n> --tab-index <n>)\n"
            "  search     [--tab-id <id> | --window-index <n> --tab-index <n>] --text <expr> [--regex] [--case-sensitive] [--inverse] [--boolean] [--auto-refresh] [--keep-results]\n"
            "  set_follow_mode [--tab-id <id> | --window-index <n> --tab-index <n>] (--enabled | --disabled)\n"
            "  invoke_action --object-name <name>\n\n"
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

        if ( !console && arguments.size() > 1
             && arguments.at( 1 ).compare( QStringLiteral( "scenario" ), Qt::CaseInsensitive ) == 0 ) {
            const auto scenarioResult = parseScenarioArguments( arguments );
            scenario_batch_request = scenarioResult.request;
            exit_requested = scenarioResult.exit_requested;
            exit_code = scenarioResult.exit_code;
            exit_message = scenarioResult.output_message;
            if ( !scenario_batch_request && !exit_requested ) {
                parse_error = true;
                parse_error_message = scenarioResult.output_message;
            }
            return;
        }

        if ( !console && arguments.size() > 1
             && arguments.at( 1 ).compare( QStringLiteral( "lab-controller" ),
                                           Qt::CaseInsensitive )
                    == 0 ) {
            const auto labResult = parseLabControllerArguments( arguments );
            lab_request = labResult.request;
            exit_requested = labResult.exit_requested;
            exit_code = labResult.exit_code;
            exit_message = labResult.output_message;
            if ( !lab_request && !exit_requested ) {
                parse_error = true;
                parse_error_message = labResult.output_message;
            }
            return;
        }

        if ( !console && arguments.size() > 1
             && arguments.at( 1 ).compare( QStringLiteral( "lab-agent" ), Qt::CaseInsensitive )
                    == 0 ) {
            const auto labResult = parseLabAgentArguments( arguments );
            lab_request = labResult.request;
            exit_requested = labResult.exit_requested;
            exit_code = labResult.exit_code;
            exit_message = labResult.output_message;
            if ( !lab_request && !exit_requested ) {
                parse_error = true;
                parse_error_message = labResult.output_message;
            }
            return;
        }

        if ( !console && arguments.size() > 1
             && arguments.at( 1 ).compare( QStringLiteral( "lab" ), Qt::CaseInsensitive ) == 0 ) {
            const auto labResult = parseLabArguments( arguments );
            lab_request = labResult.request;
            exit_requested = labResult.exit_requested;
            exit_code = labResult.exit_code;
            exit_message = labResult.output_message;
            if ( !lab_request && !exit_requested ) {
                parse_error = true;
                parse_error_message = labResult.output_message;
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
            const QCommandLineOption dumpUiTreeOption(
                "dump-ui-tree", "dump the automation UI tree as JSON and exit" );
            const QCommandLineOption dumpStateJsonOption(
                "dump-state-json", "dump the automation state snapshot as JSON to the given path and exit",
                "path" );
            parser.addOption( multiInstanceOption );
            parser.addOption( loadSessionOption );
            parser.addOption( newSessionOption );
            parser.addOption( logToFileOption );
            parser.addOption( followOption );
            parser.addOption( windowWidthOption );
            parser.addOption( windowHeightOption );
            parser.addOption( dumpUiTreeOption );
            parser.addOption( dumpStateJsonOption );
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
            dump_ui_tree = parser.isSet( QStringLiteral( "dump-ui-tree" ) );
            dump_state_json_path = parser.value( QStringLiteral( "dump-state-json" ) ).trimmed();
            if ( dump_ui_tree ) {
                multi_instance = true;
            }
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

    static ScenarioBatchParseResult parseScenarioArguments( const QStringList& arguments )
    {
        ScenarioBatchParseResult result;
        if ( arguments.size() <= 2 ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = scenarioHelpDescription();
            return result;
        }

        const auto subcommand = arguments.at( 2 ).trimmed().toLower();
        if ( subcommand == QStringLiteral( "--help" ) || subcommand == QStringLiteral( "-h" ) ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = scenarioHelpDescription();
            return result;
        }
        if ( subcommand == QStringLiteral( "--version" ) || subcommand == QStringLiteral( "-v" ) ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = versionText();
            return result;
        }

        QStringList scenarioArguments;
        scenarioArguments.reserve( arguments.size() - 1 );
        scenarioArguments.push_back( arguments.value( 0 ) + QStringLiteral( " scenario " )
                                     + arguments.at( 2 ) );
        for ( int i = 3; i < arguments.size(); ++i ) {
            scenarioArguments.push_back( arguments.at( i ) );
        }

        QCommandLineParser parser;
        parser.setApplicationDescription( scenarioHelpDescription() );
        const auto helpOption = parser.addHelpOption();
        const auto versionOption = parser.addVersionOption();

        const QCommandLineOption suiteFileOption( QStringLiteral( "suite-file" ),
                                                  QStringLiteral( "Scenario suite JSON file." ),
                                                  QStringLiteral( "path" ) );
        const QCommandLineOption scenarioFileOption( QStringLiteral( "scenario-file" ),
                                                     QStringLiteral( "Python scenario file." ),
                                                     QStringLiteral( "path" ) );
        const QCommandLineOption argsJsonFileOption( QStringLiteral( "args-json-file" ),
                                                     QStringLiteral( "Optional JSON args file." ),
                                                     QStringLiteral( "path" ) );
        const QCommandLineOption deviceMapFileOption(
            QStringLiteral( "device-map-file" ),
            QStringLiteral( "JSON file mapping logical devices to real COM ports." ),
            QStringLiteral( "path" ) );
        const QCommandLineOption reportDirOption( QStringLiteral( "report-dir" ),
                                                  QStringLiteral( "Directory for scenario report artifacts." ),
                                                  QStringLiteral( "path" ) );

        parser.addOption( suiteFileOption );
        parser.addOption( scenarioFileOption );
        parser.addOption( argsJsonFileOption );
        parser.addOption( deviceMapFileOption );
        parser.addOption( reportDirOption );

        if ( !parser.parse( scenarioArguments ) ) {
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

        ScenarioBatchRequest request;
        if ( subcommand == QStringLiteral( "run" ) ) {
            request.action = ScenarioBatchAction::Run;
        }
        else if ( subcommand == QStringLiteral( "validate" ) ) {
            request.action = ScenarioBatchAction::Validate;
        }
        else if ( subcommand == QStringLiteral( "list-devices" ) ) {
            request.action = ScenarioBatchAction::ListDevices;
        }
        else {
            result.output_message = QStringLiteral( "Unknown scenario subcommand \"%1\".\n\n%2" )
                                        .arg( arguments.at( 2 ), scenarioHelpDescription() );
            return result;
        }

        request.suiteFilePath = normalizeCommanderFilePath( parser.value( suiteFileOption ) );
        request.scenarioFilePath = normalizeCommanderFilePath( parser.value( scenarioFileOption ) );
        request.argsJsonFilePath = normalizeCommanderFilePath( parser.value( argsJsonFileOption ) );
        request.deviceMapFilePath = normalizeCommanderFilePath( parser.value( deviceMapFileOption ) );
        request.reportDirPath = normalizeCommanderFilePath( parser.value( reportDirOption ) );

        const auto hasSuite = !request.suiteFilePath.isEmpty();
        const auto hasScenario = !request.scenarioFilePath.isEmpty();
        const auto hasArgs = !request.argsJsonFilePath.isEmpty();
        const auto hasReportDir = !request.reportDirPath.isEmpty();

        switch ( request.action ) {
        case ScenarioBatchAction::Run:
            if ( hasSuite == hasScenario ) {
                result.output_message
                    = QStringLiteral( "Scenario run requires exactly one of --suite-file or --scenario-file." );
                return result;
            }
            if ( hasSuite && hasArgs ) {
                result.output_message
                    = QStringLiteral( "--args-json-file is only supported with --scenario-file." );
                return result;
            }
            break;
        case ScenarioBatchAction::Validate:
            if ( !hasSuite ) {
                result.output_message = QStringLiteral( "Scenario validate requires --suite-file." );
                return result;
            }
            if ( hasScenario || hasArgs || hasReportDir ) {
                result.output_message = QStringLiteral(
                    "Scenario validate only accepts --suite-file and optional --device-map-file." );
                return result;
            }
            break;
        case ScenarioBatchAction::ListDevices:
            if ( !hasSuite ) {
                result.output_message = QStringLiteral( "Scenario list-devices requires --suite-file." );
                return result;
            }
            if ( hasScenario || hasArgs || !request.deviceMapFilePath.isEmpty() || hasReportDir ) {
                result.output_message
                    = QStringLiteral( "Scenario list-devices only accepts --suite-file." );
                return result;
            }
            break;
        case ScenarioBatchAction::None:
            break;
        }

        result.request = request;
        return result;
    }

    static bool parseListenValue( const QString& value, QString* host, quint16* port,
                                  QString* errorMessage )
    {
        const auto trimmed = value.trimmed();
        const auto separator = trimmed.lastIndexOf( ':' );
        if ( separator <= 0 || separator + 1 >= trimmed.size() ) {
            *errorMessage = QStringLiteral( "Expected --listen in host:port format." );
            return false;
        }

        bool ok = false;
        const auto parsedPort = trimmed.mid( separator + 1 ).toUShort( &ok );
        if ( !ok || parsedPort == 0 ) {
            *errorMessage = QStringLiteral( "Invalid listen port." );
            return false;
        }

        *host = trimmed.left( separator ).trimmed();
        *port = parsedPort;
        if ( host->isEmpty() ) {
            *errorMessage = QStringLiteral( "Listen host must not be empty." );
            return false;
        }

        return true;
    }

    static LabParseResult parseLabControllerArguments( const QStringList& arguments )
    {
        LabParseResult result;
        if ( arguments.size() <= 2 ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = labControllerHelpDescription();
            return result;
        }

        const auto subcommand = arguments.at( 2 ).trimmed().toLower();
        if ( subcommand == QStringLiteral( "--help" ) || subcommand == QStringLiteral( "-h" ) ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = labControllerHelpDescription();
            return result;
        }
        if ( subcommand == QStringLiteral( "--version" ) || subcommand == QStringLiteral( "-v" ) ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = versionText();
            return result;
        }

        QStringList labArguments;
        labArguments.reserve( arguments.size() - 1 );
        labArguments.push_back( arguments.value( 0 ) + QStringLiteral( " lab-controller " )
                                + arguments.at( 2 ) );
        for ( int i = 3; i < arguments.size(); ++i ) {
            labArguments.push_back( arguments.at( i ) );
        }

        QCommandLineParser parser;
        parser.setApplicationDescription( labControllerHelpDescription() );
        const auto helpOption = parser.addHelpOption();
        const auto versionOption = parser.addVersionOption();
        const QCommandLineOption listenOption( QStringLiteral( "listen" ),
                                               QStringLiteral( "HTTP listen address in host:port format." ),
                                               QStringLiteral( "host:port" ) );
        const QCommandLineOption stateDirOption( QStringLiteral( "state-dir" ),
                                                 QStringLiteral( "Controller state directory." ),
                                                 QStringLiteral( "path" ) );
        const QCommandLineOption tokenFileOption( QStringLiteral( "token-file" ),
                                                  QStringLiteral( "Shared token file." ),
                                                  QStringLiteral( "path" ) );
        parser.addOption( listenOption );
        parser.addOption( stateDirOption );
        parser.addOption( tokenFileOption );

        if ( !parser.parse( labArguments ) ) {
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

        if ( subcommand != QStringLiteral( "serve" ) ) {
            result.output_message = QStringLiteral( "Unknown lab-controller subcommand \"%1\".\n\n%2" )
                                        .arg( arguments.at( 2 ), labControllerHelpDescription() );
            return result;
        }

        LabCliRequest request;
        request.mode = LabCliMode::ControllerServe;
        request.stateDirPath = normalizeCommanderFilePath( parser.value( stateDirOption ) );
        request.tokenFilePath = normalizeCommanderFilePath( parser.value( tokenFileOption ) );
        QString errorMessage;
        if ( request.stateDirPath.isEmpty() || request.tokenFilePath.isEmpty()
             || !parseListenValue( parser.value( listenOption ), &request.listenAddress,
                                   &request.listenPort, &errorMessage ) ) {
            if ( request.stateDirPath.isEmpty() ) {
                errorMessage = QStringLiteral( "--state-dir is required." );
            }
            else if ( request.tokenFilePath.isEmpty() ) {
                errorMessage = QStringLiteral( "--token-file is required." );
            }
            result.output_message = formatParserError( parser, errorMessage );
            return result;
        }

        result.request = request;
        return result;
    }

    static LabParseResult parseLabAgentArguments( const QStringList& arguments )
    {
        LabParseResult result;
        if ( arguments.size() <= 2 ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = labAgentHelpDescription();
            return result;
        }

        const auto subcommand = arguments.at( 2 ).trimmed().toLower();
        if ( subcommand == QStringLiteral( "--help" ) || subcommand == QStringLiteral( "-h" ) ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = labAgentHelpDescription();
            return result;
        }
        if ( subcommand == QStringLiteral( "--version" ) || subcommand == QStringLiteral( "-v" ) ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = versionText();
            return result;
        }

        QStringList labArguments;
        labArguments.reserve( arguments.size() - 1 );
        labArguments.push_back( arguments.value( 0 ) + QStringLiteral( " lab-agent " )
                                + arguments.at( 2 ) );
        for ( int i = 3; i < arguments.size(); ++i ) {
            labArguments.push_back( arguments.at( i ) );
        }

        QCommandLineParser parser;
        parser.setApplicationDescription( labAgentHelpDescription() );
        const auto helpOption = parser.addHelpOption();
        const auto versionOption = parser.addVersionOption();
        const QCommandLineOption controllerUrlOption( QStringLiteral( "controller-url" ),
                                                      QStringLiteral( "Controller base URL." ),
                                                      QStringLiteral( "url" ) );
        const QCommandLineOption agentConfigOption( QStringLiteral( "agent-config" ),
                                                    QStringLiteral( "Agent inventory JSON file." ),
                                                    QStringLiteral( "path" ) );
        const QCommandLineOption tokenFileOption( QStringLiteral( "token-file" ),
                                                  QStringLiteral( "Shared token file." ),
                                                  QStringLiteral( "path" ) );
        parser.addOption( controllerUrlOption );
        parser.addOption( agentConfigOption );
        parser.addOption( tokenFileOption );

        if ( !parser.parse( labArguments ) ) {
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

        if ( subcommand != QStringLiteral( "run" ) ) {
            result.output_message = QStringLiteral( "Unknown lab-agent subcommand \"%1\".\n\n%2" )
                                        .arg( arguments.at( 2 ), labAgentHelpDescription() );
            return result;
        }

        LabCliRequest request;
        request.mode = LabCliMode::AgentRun;
        request.controllerUrl = parser.value( controllerUrlOption ).trimmed();
        request.agentConfigPath = normalizeCommanderFilePath( parser.value( agentConfigOption ) );
        request.tokenFilePath = normalizeCommanderFilePath( parser.value( tokenFileOption ) );
        QString errorMessage;
        if ( request.controllerUrl.isEmpty() ) {
            errorMessage = QStringLiteral( "--controller-url is required." );
        }
        else if ( request.agentConfigPath.isEmpty() ) {
            errorMessage = QStringLiteral( "--agent-config is required." );
        }
        else if ( request.tokenFilePath.isEmpty() ) {
            errorMessage = QStringLiteral( "--token-file is required." );
        }
        if ( !errorMessage.isEmpty() ) {
            result.output_message = formatParserError( parser, errorMessage );
            return result;
        }

        result.request = request;
        return result;
    }

    static LabParseResult parseLabArguments( const QStringList& arguments )
    {
        LabParseResult result;
        if ( arguments.size() <= 2 ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = labHelpDescription();
            return result;
        }

        const auto subcommand = arguments.at( 2 ).trimmed().toLower();
        if ( subcommand == QStringLiteral( "--help" ) || subcommand == QStringLiteral( "-h" ) ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = labHelpDescription();
            return result;
        }
        if ( subcommand == QStringLiteral( "--version" ) || subcommand == QStringLiteral( "-v" ) ) {
            result.exit_requested = true;
            result.exit_code = EXIT_SUCCESS;
            result.output_message = versionText();
            return result;
        }

        QStringList labArguments;
        labArguments.reserve( arguments.size() - 1 );
        labArguments.push_back( arguments.value( 0 ) + QStringLiteral( " lab " )
                                + arguments.at( 2 ) );
        for ( int i = 3; i < arguments.size(); ++i ) {
            labArguments.push_back( arguments.at( i ) );
        }

        QCommandLineParser parser;
        parser.setApplicationDescription( labHelpDescription() );
        const auto helpOption = parser.addHelpOption();
        const auto versionOption = parser.addVersionOption();
        const QCommandLineOption controllerUrlOption( QStringLiteral( "controller-url" ),
                                                      QStringLiteral( "Controller base URL." ),
                                                      QStringLiteral( "url" ) );
        const QCommandLineOption tokenFileOption( QStringLiteral( "token-file" ),
                                                  QStringLiteral( "Shared token file." ),
                                                  QStringLiteral( "path" ) );
        const QCommandLineOption suiteFileOption( QStringLiteral( "suite-file" ),
                                                  QStringLiteral( "Scenario suite JSON file." ),
                                                  QStringLiteral( "path" ) );
        const QCommandLineOption scenarioFileOption( QStringLiteral( "scenario-file" ),
                                                     QStringLiteral( "Python scenario file." ),
                                                     QStringLiteral( "path" ) );
        const QCommandLineOption argsJsonFileOption( QStringLiteral( "args-json-file" ),
                                                     QStringLiteral( "Optional JSON args file." ),
                                                     QStringLiteral( "path" ) );
        const QCommandLineOption reportDirOption( QStringLiteral( "report-dir" ),
                                                  QStringLiteral( "Downloaded report directory." ),
                                                  QStringLiteral( "path" ) );
        const QCommandLineOption outputDirOption( QStringLiteral( "output-dir" ),
                                                  QStringLiteral( "Artifact output directory." ),
                                                  QStringLiteral( "path" ) );
        const QCommandLineOption agentLabelOption( QStringLiteral( "agent-label" ),
                                                   QStringLiteral( "Required agent label." ),
                                                   QStringLiteral( "label" ) );
        const QCommandLineOption jobIdOption( QStringLiteral( "job-id" ),
                                              QStringLiteral( "Lab job identifier." ),
                                              QStringLiteral( "id" ) );
        const QCommandLineOption prettyOption(
            QStringList() << QStringLiteral( "pretty" ) << QStringLiteral( "preatty" ),
            QStringLiteral( "Pretty print JSON output." ) );
        parser.addOption( controllerUrlOption );
        parser.addOption( tokenFileOption );
        parser.addOption( suiteFileOption );
        parser.addOption( scenarioFileOption );
        parser.addOption( argsJsonFileOption );
        parser.addOption( reportDirOption );
        parser.addOption( outputDirOption );
        parser.addOption( agentLabelOption );
        parser.addOption( jobIdOption );
        parser.addOption( prettyOption );

        if ( !parser.parse( labArguments ) ) {
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

        LabCliRequest request;
        request.controllerUrl = parser.value( controllerUrlOption ).trimmed();
        request.tokenFilePath = normalizeCommanderFilePath( parser.value( tokenFileOption ) );
        request.suiteFilePath = normalizeCommanderFilePath( parser.value( suiteFileOption ) );
        request.scenarioFilePath = normalizeCommanderFilePath( parser.value( scenarioFileOption ) );
        request.argsJsonFilePath = normalizeCommanderFilePath( parser.value( argsJsonFileOption ) );
        request.reportDirPath = normalizeCommanderFilePath( parser.value( reportDirOption ) );
        request.outputDirPath = normalizeCommanderFilePath( parser.value( outputDirOption ) );
        request.agentLabel = parser.value( agentLabelOption ).trimmed();
        request.jobId = parser.value( jobIdOption ).trimmed();
        request.prettyOutput = parser.isSet( prettyOption );

        const auto hasSuite = !request.suiteFilePath.isEmpty();
        const auto hasScenario = !request.scenarioFilePath.isEmpty();
        const auto hasArgs = !request.argsJsonFilePath.isEmpty();
        QString errorMessage;

        if ( subcommand == QStringLiteral( "submit" ) ) {
            request.mode = LabCliMode::Submit;
            if ( hasSuite == hasScenario ) {
                errorMessage = QStringLiteral(
                    "lab submit requires exactly one of --suite-file or --scenario-file." );
            }
            else if ( hasSuite && hasArgs ) {
                errorMessage
                    = QStringLiteral( "--args-json-file is only supported with --scenario-file." );
            }
        }
        else if ( subcommand == QStringLiteral( "queue" ) ) {
            request.mode = LabCliMode::Queue;
            if ( hasSuite || hasScenario || hasArgs || !request.jobId.isEmpty()
                 || !request.outputDirPath.isEmpty() || !request.reportDirPath.isEmpty()
                 || !request.agentLabel.isEmpty() ) {
                errorMessage = QStringLiteral( "lab queue only accepts controller and token options." );
            }
        }
        else if ( subcommand == QStringLiteral( "status" ) ) {
            request.mode = LabCliMode::Status;
            if ( request.jobId.isEmpty() ) {
                errorMessage = QStringLiteral( "lab status requires --job-id." );
            }
        }
        else if ( subcommand == QStringLiteral( "cancel" ) ) {
            request.mode = LabCliMode::Cancel;
            if ( request.jobId.isEmpty() ) {
                errorMessage = QStringLiteral( "lab cancel requires --job-id." );
            }
        }
        else if ( subcommand == QStringLiteral( "agents" ) ) {
            request.mode = LabCliMode::Agents;
            if ( hasSuite || hasScenario || hasArgs || !request.jobId.isEmpty()
                 || !request.outputDirPath.isEmpty() || !request.reportDirPath.isEmpty()
                 || !request.agentLabel.isEmpty() ) {
                errorMessage = QStringLiteral( "lab agents only accepts controller and token options." );
            }
        }
        else if ( subcommand == QStringLiteral( "artifacts" ) ) {
            request.mode = LabCliMode::Artifacts;
            if ( request.jobId.isEmpty() ) {
                errorMessage = QStringLiteral( "lab artifacts requires --job-id." );
            }
            else if ( request.outputDirPath.isEmpty() ) {
                errorMessage = QStringLiteral( "lab artifacts requires --output-dir." );
            }
        }
        else {
            result.output_message = QStringLiteral( "Unknown lab subcommand \"%1\".\n\n%2" )
                                        .arg( arguments.at( 2 ), labHelpDescription() );
            return result;
        }

        if ( request.controllerUrl.isEmpty() ) {
            errorMessage = QStringLiteral( "--controller-url is required." );
        }
        else if ( request.tokenFilePath.isEmpty() ) {
            errorMessage = QStringLiteral( "--token-file is required." );
        }

        if ( !errorMessage.isEmpty() ) {
            result.output_message = formatParserError( parser, errorMessage );
            return result;
        }

        result.request = request;
        return result;
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
                                             QStringLiteral( "Search text expression or comment text." ),
                                             QStringLiteral( "text" ) );
        const QCommandLineOption objectNameOption( QStringLiteral( "object-name" ),
                                                   QStringLiteral( "Target automation object name." ),
                                                   QStringLiteral( "name" ) );
        const QCommandLineOption scriptFileOption( QStringLiteral( "script-file" ),
                                                   QStringLiteral( "Python script file to run." ),
                                                   QStringLiteral( "path" ) );
        const QCommandLineOption argsJsonFileOption(
            QStringLiteral( "args-json-file" ),
            QStringLiteral( "Optional JSON file passed to the Python script." ),
            QStringLiteral( "path" ) );
        const QCommandLineOption scenarioFileOption( QStringLiteral( "scenario-file" ),
                                                     QStringLiteral( "Python scenario file to run." ),
                                                     QStringLiteral( "path" ) );
        const QCommandLineOption suiteFileOption( QStringLiteral( "suite-file" ),
                                                  QStringLiteral( "Scenario suite JSON file to run." ),
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
        const QCommandLineOption regexOption( QStringLiteral( "regex" ),
                                              QStringLiteral( "Use regex search semantics." ) );
        const QCommandLineOption caseSensitiveOption(
            QStringLiteral( "case-sensitive" ),
            QStringLiteral( "Use case-sensitive search semantics." ) );
        const QCommandLineOption inverseOption( QStringLiteral( "inverse" ),
                                                QStringLiteral( "Invert the search match." ) );
        const QCommandLineOption booleanOption( QStringLiteral( "boolean" ),
                                                QStringLiteral( "Use boolean search semantics." ) );
        const QCommandLineOption keepResultsOption(
            QStringLiteral( "keep-results" ),
            QStringLiteral( "Keep the current search results in a separate filtered view." ) );
        const QCommandLineOption enabledOption( QStringLiteral( "enabled" ),
                                                QStringLiteral( "Enable the requested toggle action." ) );
        const QCommandLineOption disabledOption( QStringLiteral( "disabled" ),
                                                 QStringLiteral( "Disable the requested toggle action." ) );
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
        parser.addOption( objectNameOption );
        parser.addOption( scriptFileOption );
        parser.addOption( argsJsonFileOption );
        parser.addOption( scenarioFileOption );
        parser.addOption( suiteFileOption );
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
        parser.addOption( regexOption );
        parser.addOption( caseSensitiveOption );
        parser.addOption( inverseOption );
        parser.addOption( booleanOption );
        parser.addOption( keepResultsOption );
        parser.addOption( enabledOption );
        parser.addOption( disabledOption );
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
                                                  noUseForActionsOption, &result.output_message )
             || !validateExclusiveBooleanOptions( parser, enabledOption, disabledOption,
                                                  &result.output_message ) ) {
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
          request.scenarioFilePath
              = normalizeCommanderFilePath( parser.value( scenarioFileOption ) );
          request.suiteFilePath = normalizeCommanderFilePath( parser.value( suiteFileOption ) );
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
            case CommanderAction::RunScenario:
                if ( hasTabId || hasWindowIndex || hasTabIndex || parser.isSet( allOption ) ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "run_scenario does not accept tab selectors or --all." ) );
                    return result;
                }
                if ( request.scenarioFilePath.isEmpty() ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "--scenario-file is required for run_scenario." ) );
                    return result;
                }
                break;
            case CommanderAction::RunSuite:
                if ( hasTabId || hasWindowIndex || hasTabIndex || parser.isSet( allOption ) ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "run_suite does not accept tab selectors or --all." ) );
                    return result;
                }
                if ( request.suiteFilePath.isEmpty() ) {
                    result.output_message = formatParserError(
                        parser, QStringLiteral( "--suite-file is required for run_suite." ) );
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
            case CommanderAction::StopScenarioRun:
            case CommanderAction::GetScenarioStatus:
            case CommanderAction::GetScenarioReport:
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
        case CommanderAction::Search:
            if ( !validateTabSelector( commanderActionToString( *action ), false ) ) {
                return result;
            }
            request.searchText = parser.value( textOption );
            if ( request.searchText.isEmpty() ) {
                result.output_message
                    = formatParserError( parser, QStringLiteral( "--text is required for search." ) );
                return result;
            }
            request.searchUseRegex = parser.isSet( regexOption );
            request.searchCaseSensitive = parser.isSet( caseSensitiveOption );
            request.searchInverseMatch = parser.isSet( inverseOption );
            request.searchUseBoolean = parser.isSet( booleanOption );
            request.searchAutoRefresh = parser.isSet( autoRefreshOption );
            request.searchKeepResults = parser.isSet( keepResultsOption );
            break;
        case CommanderAction::SetFollowMode:
            if ( !validateTabSelector( commanderActionToString( *action ), false ) ) {
                return result;
            }
            request.enabled = parseOptionalBoolean( parser, enabledOption, disabledOption );
            if ( !request.enabled.has_value() ) {
                result.output_message = formatParserError(
                    parser, QStringLiteral( "set_follow_mode requires --enabled or --disabled." ) );
                return result;
            }
            break;
        case CommanderAction::InvokeAction:
            request.objectName = parser.value( objectNameOption ).trimmed();
            if ( request.objectName.isEmpty() ) {
                result.output_message = formatParserError(
                    parser, QStringLiteral( "--object-name is required for invoke_action." ) );
                return result;
            }
            break;
        case CommanderAction::DumpState:
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
