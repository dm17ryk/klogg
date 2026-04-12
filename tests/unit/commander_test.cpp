#include <catch2/catch.hpp>

#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include "actionruntime.h"
#include "cli.h"
#include "commander.h"
#include "comportutils.h"
#include "configuration.h"
#include "predefinedfilters.h"

namespace {
struct ComDefaultsRestoreGuard {
    QSerialPort::BaudRate baudRate = Configuration::get().defaultComBaudRate();
    QSerialPort::DataBits dataBits = Configuration::get().defaultComDataBits();
    QSerialPort::Parity parity = Configuration::get().defaultComParity();
    QSerialPort::StopBits stopBits = Configuration::get().defaultComStopBits();
    QSerialPort::FlowControl flowControl = Configuration::get().defaultComFlowControl();
    QString logPath = Configuration::get().defaultComLogPath();
    bool timestamps = Configuration::get().defaultComTimestampEnabled();
    QString timestampFormat = Configuration::get().defaultComTimestampFormat();
    bool logTransmits = Configuration::get().defaultComLogTransmits();

    ~ComDefaultsRestoreGuard()
    {
        auto& config = Configuration::getSynced();
        config.setDefaultComBaudRate( baudRate );
        config.setDefaultComDataBits( dataBits );
        config.setDefaultComParity( parity );
        config.setDefaultComStopBits( stopBits );
        config.setDefaultComFlowControl( flowControl );
        config.setDefaultComLogPath( logPath );
        config.setDefaultComTimestampEnabled( timestamps );
        config.setDefaultComTimestampFormat( timestampFormat );
        config.setDefaultComLogTransmits( logTransmits );
    }
};
} // namespace

TEST_CASE( "Commander CLI parses open_file requests", "[commander][cli]" )
{
    CliParameters parameters(
        { "klogg", "command", "--action", "open_file", "--file", "relative.log", "--follow" } );

    REQUIRE_FALSE( parameters.parse_error );
    REQUIRE( parameters.commander_request.has_value() );
    REQUIRE( parameters.commander_request->action == CommanderAction::OpenFile );
    REQUIRE( parameters.commander_request->followFile );
    REQUIRE( parameters.commander_request->filePath.endsWith( "relative.log" ) );
}

TEST_CASE( "Commander CLI exposes command help", "[commander][cli]" )
{
    CliParameters parameters( { "klogg", "command", "--help" } );

    REQUIRE( parameters.exit_requested );
    REQUIRE( parameters.exit_code == EXIT_SUCCESS );
    REQUIRE( parameters.exit_message.contains( "open_com" ) );
    REQUIRE( parameters.exit_message.contains( "get_info" ) );
    REQUIRE( parameters.exit_message.contains( "get_actions" ) );
    REQUIRE( parameters.exit_message.contains( "get_responses" ) );
    REQUIRE( parameters.exit_message.contains( "create_action" ) );
    REQUIRE( parameters.exit_message.contains( "update_response" ) );
    REQUIRE( parameters.exit_message.contains( "send_action" ) );
    REQUIRE( parameters.exit_message.contains( "wait_response" ) );
    REQUIRE( parameters.exit_message.contains( "start_comm" ) );
    REQUIRE( parameters.exit_message.contains( "get_comm_status" ) );
    REQUIRE( parameters.exit_message.contains( "get_response_counter" ) );
    REQUIRE( parameters.exit_message.contains( "run_script" ) );
    REQUIRE( parameters.exit_message.contains( "get_script_status" ) );
    REQUIRE( parameters.exit_message.contains( "get_script_subscriptions" ) );
    REQUIRE( parameters.exit_message.contains( "clear_script_subscriptions" ) );
    REQUIRE( parameters.exit_message.contains( "run_global_script" ) );
    REQUIRE( parameters.exit_message.contains( "get_global_script_status" ) );
    REQUIRE( parameters.exit_message.contains( "get_global_script_subscriptions" ) );
    REQUIRE( parameters.exit_message.contains( "clear_global_script_subscriptions" ) );
    REQUIRE( parameters.exit_message.contains( "run_scenario" ) );
    REQUIRE( parameters.exit_message.contains( "run_suite" ) );
    REQUIRE( parameters.exit_message.contains( "stop_scenario_run" ) );
    REQUIRE( parameters.exit_message.contains( "get_scenario_status" ) );
    REQUIRE( parameters.exit_message.contains( "get_scenario_report" ) );
    REQUIRE( parameters.exit_message.contains( "get_filters" ) );
    REQUIRE( parameters.exit_message.contains( "set_filter" ) );
    REQUIRE( parameters.exit_message.contains( "close_klogg" ) );
    REQUIRE( parameters.exit_message.contains( "close_tab" ) );
    REQUIRE( parameters.exit_message.contains( "--port <name>" ) );
}

TEST_CASE( "Scenario batch CLI parses run and utility requests", "[scenario][cli]" )
{
    CliParameters runSuite( { "klogg", "scenario", "run", "--suite-file", "suite.json",
                              "--device-map-file", "devices.json", "--report-dir", "reports" } );
    REQUIRE_FALSE( runSuite.parse_error );
    REQUIRE( runSuite.scenario_batch_request.has_value() );
    REQUIRE( runSuite.scenario_batch_request->action == ScenarioBatchAction::Run );
    REQUIRE( runSuite.scenario_batch_request->suiteFilePath.endsWith( "suite.json" ) );
    REQUIRE( runSuite.scenario_batch_request->deviceMapFilePath.endsWith( "devices.json" ) );
    REQUIRE( runSuite.scenario_batch_request->reportDirPath.endsWith( "reports" ) );

    CliParameters runScenario( { "klogg", "scenario", "run", "--scenario-file", "scenario.py",
                                 "--args-json-file", "args.json" } );
    REQUIRE_FALSE( runScenario.parse_error );
    REQUIRE( runScenario.scenario_batch_request.has_value() );
    REQUIRE( runScenario.scenario_batch_request->action == ScenarioBatchAction::Run );
    REQUIRE( runScenario.scenario_batch_request->scenarioFilePath.endsWith( "scenario.py" ) );
    REQUIRE( runScenario.scenario_batch_request->argsJsonFilePath.endsWith( "args.json" ) );

    CliParameters validateSuite(
        { "klogg", "scenario", "validate", "--suite-file", "suite.json", "--device-map-file",
          "devices.json" } );
    REQUIRE_FALSE( validateSuite.parse_error );
    REQUIRE( validateSuite.scenario_batch_request.has_value() );
    REQUIRE( validateSuite.scenario_batch_request->action == ScenarioBatchAction::Validate );

    CliParameters listDevices(
        { "klogg", "scenario", "list-devices", "--suite-file", "suite.json" } );
    REQUIRE_FALSE( listDevices.parse_error );
    REQUIRE( listDevices.scenario_batch_request.has_value() );
    REQUIRE( listDevices.scenario_batch_request->action == ScenarioBatchAction::ListDevices );
}

TEST_CASE( "Scenario batch CLI rejects invalid combinations", "[scenario][cli]" )
{
    CliParameters missingTarget( { "klogg", "scenario", "run" } );
    REQUIRE( missingTarget.parse_error );
    REQUIRE( missingTarget.parse_error_message.contains( "exactly one" ) );

    CliParameters conflictingTargets(
        { "klogg", "scenario", "run", "--suite-file", "suite.json", "--scenario-file",
          "scenario.py" } );
    REQUIRE( conflictingTargets.parse_error );
    REQUIRE( conflictingTargets.parse_error_message.contains( "exactly one" ) );

    CliParameters suiteWithArgs(
        { "klogg", "scenario", "run", "--suite-file", "suite.json", "--args-json-file",
          "args.json" } );
    REQUIRE( suiteWithArgs.parse_error );
    REQUIRE( suiteWithArgs.parse_error_message.contains( "--args-json-file" ) );

    CliParameters validateInvalid(
        { "klogg", "scenario", "validate", "--suite-file", "suite.json", "--report-dir",
          "reports" } );
    REQUIRE( validateInvalid.parse_error );
    REQUIRE( validateInvalid.parse_error_message.contains( "only accepts" ) );

    CliParameters listInvalid(
        { "klogg", "scenario", "list-devices", "--suite-file", "suite.json", "--device-map-file",
          "devices.json" } );
    REQUIRE( listInvalid.parse_error );
    REQUIRE( listInvalid.parse_error_message.contains( "only accepts" ) );
}

TEST_CASE( "Main CLI exposes remote lab help", "[lab][cli]" )
{
    CliParameters parameters( { "klogg", "--help" } );

    REQUIRE( parameters.exit_requested );
    REQUIRE( parameters.exit_code == EXIT_SUCCESS );
    REQUIRE( parameters.exit_message.contains( "lab-controller serve" ) );
    REQUIRE( parameters.exit_message.contains( "lab-agent run" ) );
    REQUIRE( parameters.exit_message.contains( "lab submit" ) );
    REQUIRE( parameters.exit_message.contains( "lab artifacts" ) );
}

TEST_CASE( "Lab CLI parses controller and agent requests", "[lab][cli]" )
{
    CliParameters controller( { "klogg", "lab-controller", "serve", "--listen", "127.0.0.1:5091",
                                "--state-dir", "lab-state", "--token-file", "token.txt" } );
    REQUIRE_FALSE( controller.parse_error );
    REQUIRE( controller.lab_request.has_value() );
    REQUIRE( controller.lab_request->mode == LabCliMode::ControllerServe );
    REQUIRE( controller.lab_request->listenAddress == "127.0.0.1" );
    REQUIRE( controller.lab_request->listenPort == 5091 );
    REQUIRE( controller.lab_request->stateDirPath.endsWith( "lab-state" ) );
    REQUIRE( controller.lab_request->tokenFilePath.endsWith( "token.txt" ) );

    CliParameters agent( { "klogg", "lab-agent", "run", "--controller-url", "http://127.0.0.1:5091",
                           "--agent-config", "agent.json", "--token-file", "token.txt" } );
    REQUIRE_FALSE( agent.parse_error );
    REQUIRE( agent.lab_request.has_value() );
    REQUIRE( agent.lab_request->mode == LabCliMode::AgentRun );
    REQUIRE( agent.lab_request->controllerUrl == "http://127.0.0.1:5091" );
    REQUIRE( agent.lab_request->agentConfigPath.endsWith( "agent.json" ) );
    REQUIRE( agent.lab_request->tokenFilePath.endsWith( "token.txt" ) );
}

TEST_CASE( "Lab operator CLI parses requests", "[lab][cli]" )
{
    CliParameters submitSuite( { "klogg", "lab", "submit", "--controller-url", "http://127.0.0.1:5091",
                                 "--token-file", "token.txt", "--suite-file", "suite.json",
                                 "--agent-label", "rack-a", "--report-dir", "reports" } );
    REQUIRE_FALSE( submitSuite.parse_error );
    REQUIRE( submitSuite.lab_request.has_value() );
    REQUIRE( submitSuite.lab_request->mode == LabCliMode::Submit );
    REQUIRE( submitSuite.lab_request->suiteFilePath.endsWith( "suite.json" ) );
    REQUIRE( submitSuite.lab_request->agentLabel == "rack-a" );
    REQUIRE( submitSuite.lab_request->reportDirPath.endsWith( "reports" ) );

    CliParameters queue( { "klogg", "lab", "queue", "--controller-url", "http://127.0.0.1:5091",
                           "--token-file", "token.txt" } );
    REQUIRE_FALSE( queue.parse_error );
    REQUIRE( queue.lab_request.has_value() );
    REQUIRE( queue.lab_request->mode == LabCliMode::Queue );

    CliParameters status( { "klogg", "lab", "status", "--controller-url", "http://127.0.0.1:5091",
                            "--token-file", "token.txt", "--job-id", "job-1" } );
    REQUIRE_FALSE( status.parse_error );
    REQUIRE( status.lab_request.has_value() );
    REQUIRE( status.lab_request->mode == LabCliMode::Status );
    REQUIRE( status.lab_request->jobId == "job-1" );

    CliParameters cancel( { "klogg", "lab", "cancel", "--controller-url", "http://127.0.0.1:5091",
                            "--token-file", "token.txt", "--job-id", "job-2" } );
    REQUIRE_FALSE( cancel.parse_error );
    REQUIRE( cancel.lab_request.has_value() );
    REQUIRE( cancel.lab_request->mode == LabCliMode::Cancel );
    REQUIRE( cancel.lab_request->jobId == "job-2" );

    CliParameters agents( { "klogg", "lab", "agents", "--controller-url", "http://127.0.0.1:5091",
                            "--token-file", "token.txt" } );
    REQUIRE_FALSE( agents.parse_error );
    REQUIRE( agents.lab_request.has_value() );
    REQUIRE( agents.lab_request->mode == LabCliMode::Agents );

    CliParameters artifacts( { "klogg", "lab", "artifacts", "--controller-url", "http://127.0.0.1:5091",
                               "--token-file", "token.txt", "--job-id", "job-3", "--output-dir",
                               "downloads" } );
    REQUIRE_FALSE( artifacts.parse_error );
    REQUIRE( artifacts.lab_request.has_value() );
    REQUIRE( artifacts.lab_request->mode == LabCliMode::Artifacts );
    REQUIRE( artifacts.lab_request->outputDirPath.endsWith( "downloads" ) );
}

TEST_CASE( "Lab operator CLI rejects invalid combinations", "[lab][cli]" )
{
    CliParameters missingToken( { "klogg", "lab", "queue", "--controller-url", "http://127.0.0.1:5091" } );
    REQUIRE( missingToken.parse_error );
    REQUIRE( missingToken.parse_error_message.contains( "--token-file" ) );

    CliParameters invalidSubmit(
        { "klogg", "lab", "submit", "--controller-url", "http://127.0.0.1:5091", "--token-file",
          "token.txt", "--suite-file", "suite.json", "--scenario-file", "scenario.py" } );
    REQUIRE( invalidSubmit.parse_error );
    REQUIRE( invalidSubmit.parse_error_message.contains( "exactly one" ) );

    CliParameters missingArtifactOutput(
        { "klogg", "lab", "artifacts", "--controller-url", "http://127.0.0.1:5091",
          "--token-file", "token.txt", "--job-id", "job-9" } );
    REQUIRE( missingArtifactOutput.parse_error );
    REQUIRE( missingArtifactOutput.parse_error_message.contains( "--output-dir" ) );

    CliParameters missingAgentConfig(
        { "klogg", "lab-agent", "run", "--controller-url", "http://127.0.0.1:5091",
          "--token-file", "token.txt" } );
    REQUIRE( missingAgentConfig.parse_error );
    REQUIRE( missingAgentConfig.parse_error_message.contains( "--agent-config" ) );
}

TEST_CASE( "Commander CLI rejects missing required arguments", "[commander][cli]" )
{
    CliParameters parameters( { "klogg", "command", "--action", "close_file" } );

    REQUIRE( parameters.parse_error );
    REQUIRE( parameters.parse_error_message.contains( "--file" ) );
}

TEST_CASE( "Commander CLI rejects unknown options with help text", "[commander][cli]" )
{
    CliParameters parameters(
        { "klogg", "command", "--action", "open_com", "--portss", "COM1" } );

    REQUIRE( parameters.parse_error );
    REQUIRE( parameters.parse_error_message.contains( "portss" ) );
    REQUIRE( parameters.parse_error_message.contains( "open_com" ) );
}

TEST_CASE( "Commander CLI parses open_com overrides", "[commander][cli]" )
{
    CliParameters parameters(
        { "klogg", "command", "--action", "open_com", "--port", "COM7", "--baud", "230400",
          "--data-bits", "7", "--parity", "even", "--stop-bits", "2", "--flow-control",
          "hardware", "--timestamps", "--timestamp-format", "HH:mm:ss", "--log-transmits",
          "--use-for-actions" } );

    REQUIRE_FALSE( parameters.parse_error );
    REQUIRE( parameters.commander_request.has_value() );
    REQUIRE( parameters.commander_request->action == CommanderAction::OpenCom );
    REQUIRE( parameters.commander_request->portName == "COM7" );
    REQUIRE( parameters.commander_request->comSettings.baudRate.has_value() );
    REQUIRE( *parameters.commander_request->comSettings.baudRate == 230400 );
    REQUIRE( parameters.commander_request->comSettings.dataBits.has_value() );
    REQUIRE( *parameters.commander_request->comSettings.dataBits == QSerialPort::Data7 );
    REQUIRE( parameters.commander_request->comSettings.parity.has_value() );
    REQUIRE( *parameters.commander_request->comSettings.parity == QSerialPort::EvenParity );
    REQUIRE( parameters.commander_request->comSettings.stopBits.has_value() );
    REQUIRE( *parameters.commander_request->comSettings.stopBits == QSerialPort::TwoStop );
    REQUIRE( parameters.commander_request->comSettings.flowControl.has_value() );
    REQUIRE( *parameters.commander_request->comSettings.flowControl == QSerialPort::HardwareControl );
    REQUIRE( parameters.commander_request->comSettings.addTimestamps.has_value() );
    REQUIRE( *parameters.commander_request->comSettings.addTimestamps );
    REQUIRE( parameters.commander_request->comSettings.timestampFormat.has_value() );
    REQUIRE( *parameters.commander_request->comSettings.timestampFormat == "HH:mm:ss" );
    REQUIRE( parameters.commander_request->comSettings.logTransmits.has_value() );
    REQUIRE( *parameters.commander_request->comSettings.logTransmits );
    REQUIRE( parameters.commander_request->comSettings.useForActions.has_value() );
    REQUIRE( *parameters.commander_request->comSettings.useForActions );
}

TEST_CASE( "Commander CLI parses get_info action", "[commander][cli]" )
{
    CliParameters parameters( { "klogg", "command", "--action", "get_info" } );

    REQUIRE_FALSE( parameters.parse_error );
    REQUIRE( parameters.commander_request.has_value() );
    REQUIRE( parameters.commander_request->action == CommanderAction::GetInfo );
}

TEST_CASE( "Main CLI parses dump_ui_tree automation mode", "[cli][automation]" )
{
    CliParameters parameters(
        { "klogg", "--dump-ui-tree", "--window-width", "1440", "--window-height", "900" } );

    REQUIRE_FALSE( parameters.parse_error );
    REQUIRE_FALSE( parameters.commander_request.has_value() );
    REQUIRE( parameters.dump_ui_tree );
    REQUIRE( parameters.multi_instance );
    REQUIRE( parameters.window_width == 1440 );
    REQUIRE( parameters.window_height == 900 );
}

TEST_CASE( "Main CLI parses dump_state_json automation mode", "[cli][automation]" )
{
    CliParameters parameters(
        { "klogg", "--dump-state-json", "state.json", "--window-width", "1280", "--window-height", "720" } );

    REQUIRE_FALSE( parameters.parse_error );
    REQUIRE_FALSE( parameters.commander_request.has_value() );
    REQUIRE( parameters.dump_state_json_path == "state.json" );
    REQUIRE_FALSE( parameters.multi_instance );
    REQUIRE( parameters.window_width == 1280 );
    REQUIRE( parameters.window_height == 720 );
}

TEST_CASE( "Commander CLI parses actions CRUD requests", "[commander][cli]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const auto payloadPath = dir.filePath( "action.json" );
    QFile payloadFile( payloadPath );
    REQUIRE( payloadFile.open( QIODevice::WriteOnly ) );
    payloadFile.write( QJsonDocument( QJsonObject{
                           { "id", 17 },
                           { "name", "Ping" },
                           { "sequence", QJsonObject{ { "type", "String" }, { "value", "AT" } } },
                       } )
                           .toJson( QJsonDocument::Compact ) );
    payloadFile.close();

    CliParameters createParameters(
        { "klogg", "command", "--action", "create_action", "--json-file", payloadPath } );
    REQUIRE_FALSE( createParameters.parse_error );
    REQUIRE( createParameters.commander_request.has_value() );
    REQUIRE( createParameters.commander_request->action == CommanderAction::CreateAction );
    REQUIRE( createParameters.commander_request->definitionPayload.value( "name" ).toString() == "Ping" );

    CliParameters updateParameters(
        { "klogg", "command", "--action", "update_action", "--id", "17", "--json-file",
          payloadPath } );
    REQUIRE_FALSE( updateParameters.parse_error );
    REQUIRE( updateParameters.commander_request.has_value() );
    REQUIRE( updateParameters.commander_request->entityId == 17 );

    CliParameters deleteParameters(
        { "klogg", "command", "--action", "delete_response", "--id", "9" } );
    REQUIRE_FALSE( deleteParameters.parse_error );
    REQUIRE( deleteParameters.commander_request.has_value() );
    REQUIRE( deleteParameters.commander_request->action == CommanderAction::DeleteResponse );
    REQUIRE( deleteParameters.commander_request->entityId == 9 );
}

TEST_CASE( "Commander CLI parses send and wait response requests", "[commander][cli]" )
{
    CliParameters sendParameters(
        { "klogg", "command", "--action", "send_action", "--id", "12", "--tab-id", "tab-1" } );
    REQUIRE_FALSE( sendParameters.parse_error );
    REQUIRE( sendParameters.commander_request.has_value() );
    REQUIRE( sendParameters.commander_request->action == CommanderAction::SendAction );
    REQUIRE( sendParameters.commander_request->entityId == 12 );
    REQUIRE( sendParameters.commander_request->tabId == "tab-1" );

    CliParameters waitParameters(
        { "klogg", "command", "--action", "wait_response", "--name", "Ready", "--timeout-ms",
          "1500", "--window-index", "0", "--tab-index", "1" } );
    REQUIRE_FALSE( waitParameters.parse_error );
    REQUIRE( waitParameters.commander_request.has_value() );
    REQUIRE( waitParameters.commander_request->action == CommanderAction::WaitResponse );
    REQUIRE( waitParameters.commander_request->entityName == "Ready" );
    REQUIRE( waitParameters.commander_request->timeoutMs == 1500 );
    REQUIRE( waitParameters.commander_request->windowIndex == 0 );
    REQUIRE( waitParameters.commander_request->tabIndex == 1 );
}

TEST_CASE( "Commander CLI parses communication automation requests", "[commander][cli]" )
{
    CliParameters startComm(
        { "klogg", "command", "--action", "start_comm", "--tab-id", "tab-1" } );
    REQUIRE_FALSE( startComm.parse_error );
    REQUIRE( startComm.commander_request.has_value() );
    REQUIRE( startComm.commander_request->action == CommanderAction::StartComm );
    REQUIRE( startComm.commander_request->tabId == "tab-1" );

    CliParameters addComment(
        { "klogg", "command", "--action", "add_comment", "--text", "hello", "--timestamp" } );
    REQUIRE_FALSE( addComment.parse_error );
    REQUIRE( addComment.commander_request.has_value() );
    REQUIRE( addComment.commander_request->action == CommanderAction::AddComment );
    REQUIRE( addComment.commander_request->commentText == "hello" );
    REQUIRE( addComment.commander_request->timestampComment );

    CliParameters getCounter(
        { "klogg", "command", "--action", "get_response_counter", "--all", "--pretty" } );
    REQUIRE_FALSE( getCounter.parse_error );
    REQUIRE( getCounter.commander_request.has_value() );
    REQUIRE( getCounter.commander_request->action == CommanderAction::GetResponseCounter );
    REQUIRE( getCounter.commander_request->allEntities );
    REQUIRE( getCounter.commander_request->prettyOutput );
}

TEST_CASE( "Commander CLI parses script runner requests", "[commander][cli]" )
{
    CliParameters runScript(
        { "klogg", "command", "--action", "run_script", "--script-file", "test.py",
          "--args-json-file", "args.json", "--tab-id", "tab-1" } );
    REQUIRE_FALSE( runScript.parse_error );
    REQUIRE( runScript.commander_request.has_value() );
    REQUIRE( runScript.commander_request->action == CommanderAction::RunScript );
    REQUIRE( runScript.commander_request->scriptFilePath.endsWith( "test.py" ) );
    REQUIRE( runScript.commander_request->argsJsonFilePath.endsWith( "args.json" ) );
    REQUIRE( runScript.commander_request->tabId == "tab-1" );

    CliParameters getStatus(
        { "klogg", "command", "--action", "get_script_status", "--all", "--pretty" } );
    REQUIRE_FALSE( getStatus.parse_error );
    REQUIRE( getStatus.commander_request.has_value() );
    REQUIRE( getStatus.commander_request->action == CommanderAction::GetScriptStatus );
    REQUIRE( getStatus.commander_request->allEntities );
    REQUIRE( getStatus.commander_request->prettyOutput );

    CliParameters getSubscriptions(
        { "klogg", "command", "--action", "get_script_subscriptions", "--tab-id", "tab-1",
          "--pretty" } );
    REQUIRE_FALSE( getSubscriptions.parse_error );
    REQUIRE( getSubscriptions.commander_request.has_value() );
    REQUIRE( getSubscriptions.commander_request->action
             == CommanderAction::GetScriptSubscriptions );
    REQUIRE( getSubscriptions.commander_request->tabId == "tab-1" );
    REQUIRE( getSubscriptions.commander_request->prettyOutput );

    CliParameters clearSubscriptions(
        { "klogg", "command", "--action", "clear_script_subscriptions", "--all" } );
    REQUIRE_FALSE( clearSubscriptions.parse_error );
    REQUIRE( clearSubscriptions.commander_request.has_value() );
    REQUIRE( clearSubscriptions.commander_request->action
             == CommanderAction::ClearScriptSubscriptions );
    REQUIRE( clearSubscriptions.commander_request->allEntities );
}

TEST_CASE( "Commander CLI parses global script runner requests", "[commander][cli]" )
{
    CliParameters runGlobal(
        { "klogg", "command", "--action", "run_global_script", "--script-file", "global.py",
          "--args-json-file", "args.json" } );
    REQUIRE_FALSE( runGlobal.parse_error );
    REQUIRE( runGlobal.commander_request.has_value() );
    REQUIRE( runGlobal.commander_request->action == CommanderAction::RunGlobalScript );
    REQUIRE( runGlobal.commander_request->scriptFilePath.endsWith( "global.py" ) );
    REQUIRE( runGlobal.commander_request->argsJsonFilePath.endsWith( "args.json" ) );

    CliParameters getGlobalStatus(
        { "klogg", "command", "--action", "get_global_script_status", "--pretty" } );
    REQUIRE_FALSE( getGlobalStatus.parse_error );
    REQUIRE( getGlobalStatus.commander_request.has_value() );
    REQUIRE( getGlobalStatus.commander_request->action == CommanderAction::GetGlobalScriptStatus );
    REQUIRE( getGlobalStatus.commander_request->prettyOutput );

    CliParameters getGlobalSubscriptions(
        { "klogg", "command", "--action", "get_global_script_subscriptions", "--pretty" } );
    REQUIRE_FALSE( getGlobalSubscriptions.parse_error );
    REQUIRE( getGlobalSubscriptions.commander_request.has_value() );
    REQUIRE( getGlobalSubscriptions.commander_request->action
             == CommanderAction::GetGlobalScriptSubscriptions );
    REQUIRE( getGlobalSubscriptions.commander_request->prettyOutput );

    CliParameters clearGlobalSubscriptions(
        { "klogg", "command", "--action", "clear_global_script_subscriptions" } );
    REQUIRE_FALSE( clearGlobalSubscriptions.parse_error );
    REQUIRE( clearGlobalSubscriptions.commander_request.has_value() );
    REQUIRE( clearGlobalSubscriptions.commander_request->action
             == CommanderAction::ClearGlobalScriptSubscriptions );
}

TEST_CASE( "Commander CLI parses scenario runner requests", "[commander][cli]" )
{
    CliParameters runScenario(
        { "klogg", "command", "--action", "run_scenario", "--scenario-file", "scenario.py",
          "--args-json-file", "args.json" } );
    REQUIRE_FALSE( runScenario.parse_error );
    REQUIRE( runScenario.commander_request.has_value() );
    REQUIRE( runScenario.commander_request->action == CommanderAction::RunScenario );
    REQUIRE( runScenario.commander_request->scenarioFilePath.endsWith( "scenario.py" ) );
    REQUIRE( runScenario.commander_request->argsJsonFilePath.endsWith( "args.json" ) );

    CliParameters runSuite(
        { "klogg", "command", "--action", "run_suite", "--suite-file", "suite.json" } );
    REQUIRE_FALSE( runSuite.parse_error );
    REQUIRE( runSuite.commander_request.has_value() );
    REQUIRE( runSuite.commander_request->action == CommanderAction::RunSuite );
    REQUIRE( runSuite.commander_request->suiteFilePath.endsWith( "suite.json" ) );

    CliParameters getStatus(
        { "klogg", "command", "--action", "get_scenario_status", "--pretty" } );
    REQUIRE_FALSE( getStatus.parse_error );
    REQUIRE( getStatus.commander_request.has_value() );
    REQUIRE( getStatus.commander_request->action == CommanderAction::GetScenarioStatus );
    REQUIRE( getStatus.commander_request->prettyOutput );

    CliParameters getReport(
        { "klogg", "command", "--action", "get_scenario_report", "--pretty" } );
    REQUIRE_FALSE( getReport.parse_error );
    REQUIRE( getReport.commander_request.has_value() );
    REQUIRE( getReport.commander_request->action == CommanderAction::GetScenarioReport );
    REQUIRE( getReport.commander_request->prettyOutput );
}

TEST_CASE( "Commander CLI rejects invalid scenario selectors", "[commander][cli]" )
{
    CliParameters missingScenarioFile(
        { "klogg", "command", "--action", "run_scenario" } );
    REQUIRE( missingScenarioFile.parse_error );
    REQUIRE( missingScenarioFile.parse_error_message.contains( "--scenario-file" ) );

    CliParameters missingSuiteFile(
        { "klogg", "command", "--action", "run_suite" } );
    REQUIRE( missingSuiteFile.parse_error );
    REQUIRE( missingSuiteFile.parse_error_message.contains( "--suite-file" ) );

    CliParameters scenarioWithTab(
        { "klogg", "command", "--action", "run_scenario", "--scenario-file", "scenario.py",
          "--tab-id", "tab-1" } );
    REQUIRE( scenarioWithTab.parse_error );
    REQUIRE( scenarioWithTab.parse_error_message.contains( "does not accept tab selectors or --all" ) );

    CliParameters scenarioStatusWithAll(
        { "klogg", "command", "--action", "get_scenario_status", "--all" } );
    REQUIRE( scenarioStatusWithAll.parse_error );
    REQUIRE( scenarioStatusWithAll.parse_error_message.contains( "does not accept tab selectors or --all" ) );
}

TEST_CASE( "Commander CLI rejects invalid global script selectors", "[commander][cli]" )
{
    CliParameters missingScript(
        { "klogg", "command", "--action", "run_global_script" } );
    REQUIRE( missingScript.parse_error );
    REQUIRE( missingScript.parse_error_message.contains( "--script-file" ) );

    CliParameters globalWithTab(
        { "klogg", "command", "--action", "run_global_script", "--script-file", "global.py",
          "--tab-id", "tab-1" } );
    REQUIRE( globalWithTab.parse_error );
    REQUIRE( globalWithTab.parse_error_message.contains( "does not accept tab selectors or --all" ) );

    CliParameters globalStatusWithAll(
        { "klogg", "command", "--action", "get_global_script_status", "--all" } );
    REQUIRE( globalStatusWithAll.parse_error );
    REQUIRE( globalStatusWithAll.parse_error_message.contains( "does not accept tab selectors or --all" ) );
}

TEST_CASE( "Commander CLI rejects invalid response counter selectors", "[commander][cli]" )
{
    CliParameters missingSelector(
        { "klogg", "command", "--action", "get_response_counter" } );
    REQUIRE( missingSelector.parse_error );
    REQUIRE( missingSelector.parse_error_message.contains( "exactly one of --id, --name, or --all" ) );

    CliParameters mixedSelectors(
        { "klogg", "command", "--action", "reset_response_counter", "--id", "1", "--all" } );
    REQUIRE( mixedSelectors.parse_error );
    REQUIRE( mixedSelectors.parse_error_message.contains( "exactly one of --id, --name, or --all" ) );
}

TEST_CASE( "Commander CLI rejects invalid wait_response selectors", "[commander][cli]" )
{
    CliParameters missingSelector(
        { "klogg", "command", "--action", "wait_response", "--timeout-ms", "1000" } );
    REQUIRE( missingSelector.parse_error );
    REQUIRE( missingSelector.parse_error_message.contains( "exactly one of --id or --name" ) );

    CliParameters missingTimeout(
        { "klogg", "command", "--action", "wait_response", "--id", "7" } );
    REQUIRE( missingTimeout.parse_error );
    REQUIRE( missingTimeout.parse_error_message.contains( "--timeout-ms" ) );
}

TEST_CASE( "Commander CLI parses pretty JSON options", "[commander][cli]" )
{
    CliParameters getInfoPretty(
        { "klogg", "command", "--action", "get_info", "--pretty" } );

    REQUIRE_FALSE( getInfoPretty.parse_error );
    REQUIRE( getInfoPretty.commander_request.has_value() );
    REQUIRE( getInfoPretty.commander_request->prettyOutput );

    CliParameters getFiltersPretty(
        { "klogg", "command", "--action", "get_filters", "--preatty" } );

    REQUIRE_FALSE( getFiltersPretty.parse_error );
    REQUIRE( getFiltersPretty.commander_request.has_value() );
    REQUIRE( getFiltersPretty.commander_request->action == CommanderAction::GetFilters );
    REQUIRE( getFiltersPretty.commander_request->prettyOutput );
}

TEST_CASE( "Commander CLI parses close_tab selectors", "[commander][cli]" )
{
    CliParameters byId( { "klogg", "command", "--action", "close_tab", "--tab-id",
                          "abc123" } );
    REQUIRE_FALSE( byId.parse_error );
    REQUIRE( byId.commander_request.has_value() );
    REQUIRE( byId.commander_request->action == CommanderAction::CloseTab );
    REQUIRE( byId.commander_request->tabId == "abc123" );
    REQUIRE_FALSE( byId.commander_request->windowIndex.has_value() );
    REQUIRE_FALSE( byId.commander_request->tabIndex.has_value() );

    CliParameters byIndex( { "klogg", "command", "--action", "close_tab", "--window-index",
                             "2", "--tab-index", "4" } );
    REQUIRE_FALSE( byIndex.parse_error );
    REQUIRE( byIndex.commander_request.has_value() );
    REQUIRE( byIndex.commander_request->action == CommanderAction::CloseTab );
    REQUIRE( byIndex.commander_request->windowIndex == 2 );
    REQUIRE( byIndex.commander_request->tabIndex == 4 );
    REQUIRE( byIndex.commander_request->tabId.isEmpty() );
}

TEST_CASE( "Commander CLI rejects invalid close_tab selectors", "[commander][cli]" )
{
    CliParameters missingSelector( { "klogg", "command", "--action", "close_tab" } );
    REQUIRE( missingSelector.parse_error );
    REQUIRE( missingSelector.parse_error_message.contains( "close_tab requires" ) );

    CliParameters mixedSelectors(
        { "klogg", "command", "--action", "close_tab", "--tab-id", "abc123", "--window-index",
          "1", "--tab-index", "0" } );
    REQUIRE( mixedSelectors.parse_error );
    REQUIRE( mixedSelectors.parse_error_message.contains( "Use either --tab-id" ) );

    CliParameters missingTabIndex(
        { "klogg", "command", "--action", "close_tab", "--window-index", "1" } );
    REQUIRE( missingTabIndex.parse_error );
    REQUIRE( missingTabIndex.parse_error_message.contains( "--window-index and --tab-index" ) );

    CliParameters negativeTabIndex( { "klogg", "command", "--action", "close_tab",
                                      "--window-index", "1", "--tab-index", "-1" } );
    REQUIRE( negativeTabIndex.parse_error );
    REQUIRE( negativeTabIndex.parse_error_message.contains( "--tab-index" ) );
}

TEST_CASE( "Commander CLI parses focus_tab selectors", "[commander][cli]" )
{
    CliParameters byId(
        { "klogg", "command", "--action", "focus_tab", "--tab-id", "tab-42" } );
    REQUIRE_FALSE( byId.parse_error );
    REQUIRE( byId.commander_request.has_value() );
    REQUIRE( byId.commander_request->action == CommanderAction::FocusTab );
    REQUIRE( byId.commander_request->tabId == "tab-42" );

    CliParameters byIndex( { "klogg", "command", "--action", "focus_tab", "--window-index",
                             "2", "--tab-index", "1" } );
    REQUIRE_FALSE( byIndex.parse_error );
    REQUIRE( byIndex.commander_request.has_value() );
    REQUIRE( byIndex.commander_request->windowIndex == 2 );
    REQUIRE( byIndex.commander_request->tabIndex == 1 );
}

TEST_CASE( "Commander CLI parses filter actions", "[commander][cli]" )
{
    CliParameters getFilters(
        { "klogg", "command", "--action", "get_filters", "--tab-id", "tab-7",
          "--filter-index", "3", "--pretty" } );
    REQUIRE_FALSE( getFilters.parse_error );
    REQUIRE( getFilters.commander_request.has_value() );
    REQUIRE( getFilters.commander_request->action == CommanderAction::GetFilters );
    REQUIRE( getFilters.commander_request->tabId == "tab-7" );
    REQUIRE( getFilters.commander_request->filterIndex == 3 );
    REQUIRE( getFilters.commander_request->prettyOutput );
    REQUIRE_FALSE( getFilters.commander_request->predefinedFilters );

    CliParameters setFilter(
        { "klogg", "command", "--action", "set_filter", "--window-index", "0", "--tab-index",
          "1", "--filter-id", "flt-1", "--predefined", "--search", "--auto-refresh" } );
    REQUIRE_FALSE( setFilter.parse_error );
    REQUIRE( setFilter.commander_request.has_value() );
    REQUIRE( setFilter.commander_request->action == CommanderAction::SetFilter );
    REQUIRE( setFilter.commander_request->windowIndex == 0 );
    REQUIRE( setFilter.commander_request->tabIndex == 1 );
    REQUIRE( setFilter.commander_request->filterId == "flt-1" );
    REQUIRE( setFilter.commander_request->predefinedFilters );
    REQUIRE( setFilter.commander_request->runSearch );
    REQUIRE( setFilter.commander_request->rearmAutoRefresh );
}

TEST_CASE( "Commander CLI parses automation actions", "[commander][cli]" )
{
    CliParameters search(
        { "klogg", "command", "--action", "search", "--text", "ERROR", "--regex",
          "--case-sensitive", "--inverse", "--boolean", "--auto-refresh", "--keep-results" } );
    REQUIRE_FALSE( search.parse_error );
    REQUIRE( search.commander_request.has_value() );
    REQUIRE( search.commander_request->action == CommanderAction::Search );
    REQUIRE( search.commander_request->searchText == "ERROR" );
    REQUIRE( search.commander_request->searchUseRegex );
    REQUIRE( search.commander_request->searchCaseSensitive );
    REQUIRE( search.commander_request->searchInverseMatch );
    REQUIRE( search.commander_request->searchUseBoolean );
    REQUIRE( search.commander_request->searchAutoRefresh );
    REQUIRE( search.commander_request->searchKeepResults );

    CliParameters follow(
        { "klogg", "command", "--action", "set_follow_mode", "--enabled" } );
    REQUIRE_FALSE( follow.parse_error );
    REQUIRE( follow.commander_request.has_value() );
    REQUIRE( follow.commander_request->action == CommanderAction::SetFollowMode );
    REQUIRE( follow.commander_request->enabled.has_value() );
    REQUIRE( *follow.commander_request->enabled );

    CliParameters invoke(
        { "klogg", "command", "--action", "invoke_action", "--object-name", "followAction" } );
    REQUIRE_FALSE( invoke.parse_error );
    REQUIRE( invoke.commander_request.has_value() );
    REQUIRE( invoke.commander_request->action == CommanderAction::InvokeAction );
    REQUIRE( invoke.commander_request->objectName == "followAction" );
}

TEST_CASE( "Commander CLI rejects invalid set_filter selectors", "[commander][cli]" )
{
    CliParameters missingFilter( { "klogg", "command", "--action", "set_filter" } );
    REQUIRE( missingFilter.parse_error );
    REQUIRE( missingFilter.parse_error_message.contains( "exactly one" ) );

    CliParameters multipleFilters(
        { "klogg", "command", "--action", "set_filter", "--filter-id", "flt-1",
          "--filter-index", "0" } );
    REQUIRE( multipleFilters.parse_error );
    REQUIRE( multipleFilters.parse_error_message.contains( "exactly one" ) );

    CliParameters missingFocusSelector( { "klogg", "command", "--action", "focus_tab" } );
    REQUIRE( missingFocusSelector.parse_error );
    REQUIRE( missingFocusSelector.parse_error_message.contains( "focus_tab requires" ) );
}

TEST_CASE( "Main CLI help advertises commander mode", "[commander][cli]" )
{
    CliParameters parameters( { "klogg", "--help" } );

    REQUIRE( parameters.exit_requested );
    REQUIRE( parameters.exit_message.contains( "klogg command --action open_file" ) );
    REQUIRE( parameters.exit_message.contains( "klogg command --action close_com" ) );
    REQUIRE( parameters.exit_message.contains( "klogg command --action get_info" ) );
    REQUIRE( parameters.exit_message.contains( "klogg command --action start_comm" ) );
    REQUIRE( parameters.exit_message.contains( "klogg command --action get_comm_status" ) );
    REQUIRE( parameters.exit_message.contains( "klogg command --action run_script" ) );
    REQUIRE( parameters.exit_message.contains( "klogg command --action get_script_status" ) );
    REQUIRE( parameters.exit_message.contains( "klogg command --action get_actions" ) );
    REQUIRE( parameters.exit_message.contains( "klogg command --action send_action" ) );
    REQUIRE( parameters.exit_message.contains( "klogg command --action get_filters" ) );
    REQUIRE( parameters.exit_message.contains( "klogg command --action set_filter" ) );
}

TEST_CASE( "Commander request variant roundtrip", "[commander][ipc]" )
{
    CommanderRequest request;
    request.action = CommanderAction::CloseTab;
    request.tabId = "tab-123";
    request.windowIndex = 1;
    request.tabIndex = 2;
    request.filterId = "flt-1";
    request.filterIndex = 4;
    request.filterString = "alpha|beta";
    request.entityId = 7;
    request.entityName = "Ready";
    request.commentText = "manual note";
    request.scriptFilePath = "D:/scripts/demo.py";
    request.argsJsonFilePath = "D:/scripts/demo.args.json";
    request.timeoutMs = 5000;
    request.allEntities = true;
    request.timestampComment = true;
    request.prettyOutput = true;
    request.predefinedFilters = true;
    request.runSearch = true;
    request.rearmAutoRefresh = true;
    request.portName = "COM9";
    request.comSettings.portName = "COM9";
    request.comSettings.baudRate = 115200;
    request.comSettings.addTimestamps = false;
    request.comSettings.useForActions = true;
    request.definitionPayload = QVariantMap{ { "name", "Ping" }, { "order", 1 } };

    QString errorMessage;
    const auto restored
        = commanderRequestFromVariantMap( commanderRequestToVariantMap( request ), &errorMessage );

    REQUIRE( restored.has_value() );
    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( restored->action == request.action );
    REQUIRE( restored->tabId == request.tabId );
    REQUIRE( restored->windowIndex == request.windowIndex );
    REQUIRE( restored->tabIndex == request.tabIndex );
    REQUIRE( restored->filterId == request.filterId );
    REQUIRE( restored->filterIndex == request.filterIndex );
    REQUIRE( restored->filterString == request.filterString );
    REQUIRE( restored->entityId == request.entityId );
    REQUIRE( restored->entityName == request.entityName );
    REQUIRE( restored->commentText == request.commentText );
    REQUIRE( restored->scriptFilePath == request.scriptFilePath );
    REQUIRE( restored->argsJsonFilePath == request.argsJsonFilePath );
    REQUIRE( restored->timeoutMs == request.timeoutMs );
    REQUIRE( restored->allEntities == request.allEntities );
    REQUIRE( restored->timestampComment == request.timestampComment );
    REQUIRE( restored->prettyOutput == request.prettyOutput );
    REQUIRE( restored->predefinedFilters == request.predefinedFilters );
    REQUIRE( restored->runSearch == request.runSearch );
    REQUIRE( restored->rearmAutoRefresh == request.rearmAutoRefresh );
    REQUIRE( restored->portName == request.portName );
    REQUIRE( restored->comSettings.baudRate.has_value() );
    REQUIRE( restored->comSettings.baudRate == request.comSettings.baudRate );
    REQUIRE( restored->comSettings.addTimestamps.has_value() );
    REQUIRE( restored->comSettings.addTimestamps == request.comSettings.addTimestamps );
    REQUIRE( restored->comSettings.useForActions.has_value() );
    REQUIRE( restored->comSettings.useForActions == request.comSettings.useForActions );
    REQUIRE( restored->definitionPayload == request.definitionPayload );
}

TEST_CASE( "Commander dump_state variant roundtrip", "[commander][ipc]" )
{
    CommanderRequest request;
    request.action = CommanderAction::DumpState;
    request.windowIndex = 2;

    QString errorMessage;
    const auto restored
        = commanderRequestFromVariantMap( commanderRequestToVariantMap( request ), &errorMessage );

    REQUIRE( restored.has_value() );
    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( restored->action == CommanderAction::DumpState );
    REQUIRE( restored->windowIndex == 2 );
}

TEST_CASE( "Commander result file roundtrip", "[commander][ipc]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const auto resultPath = dir.filePath( "commander_result.json" );
    const auto expected = commanderFailure( CommanderResultCode::NotFound, "no such target" );

    REQUIRE( writeCommanderResult( resultPath, expected ) );

    QString errorMessage;
    const auto restored = readCommanderResult( resultPath, &errorMessage );

    REQUIRE( restored.has_value() );
    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( restored->code == expected.code );
    REQUIRE( restored->message == expected.message );
}

TEST_CASE( "Commander result payload roundtrip", "[commander][ipc]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const auto resultPath = dir.filePath( "commander_payload.json" );
    QVariantMap payload;
    payload.insert( "windows", QVariantList{ QVariantMap{ { "windowIndex", 0 } } } );
    const auto expected = commanderSuccess( {}, payload );

    REQUIRE( writeCommanderResult( resultPath, expected ) );

    QString errorMessage;
    const auto restored = readCommanderResult( resultPath, &errorMessage );

    REQUIRE( restored.has_value() );
    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( restored->ok() );
    REQUIRE( restored->payload == payload );
}

TEST_CASE( "Commander COM settings inherit preferences and support overrides", "[commander][com]" )
{
    ComDefaultsRestoreGuard restoreGuard;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    auto& config = Configuration::getSynced();
    config.setDefaultComBaudRate( QSerialPort::Baud57600 );
    config.setDefaultComDataBits( QSerialPort::Data7 );
    config.setDefaultComParity( QSerialPort::OddParity );
    config.setDefaultComStopBits( QSerialPort::TwoStop );
    config.setDefaultComFlowControl( QSerialPort::SoftwareControl );
    config.setDefaultComLogPath( dir.filePath( "preferred.log" ) );
    config.setDefaultComTimestampEnabled( true );
    config.setDefaultComTimestampFormat( "dd.MM.yyyy" );
    config.setDefaultComLogTransmits( true );

    CommanderComSettings defaultsOnly;
    defaultsOnly.portName = "COM5";

    const auto resolvedDefaults = resolveCommanderComSettings( defaultsOnly );
    REQUIRE( resolvedDefaults.portName == "COM5" );
    REQUIRE( resolvedDefaults.baudRate == QSerialPort::Baud57600 );
    REQUIRE( resolvedDefaults.dataBits == QSerialPort::Data7 );
    REQUIRE( resolvedDefaults.parity == QSerialPort::OddParity );
    REQUIRE( resolvedDefaults.stopBits == QSerialPort::TwoStop );
    REQUIRE( resolvedDefaults.flowControl == QSerialPort::SoftwareControl );
    REQUIRE( resolvedDefaults.addTimestamps );
    REQUIRE( resolvedDefaults.timestampFormat == "dd.MM.yyyy" );
    REQUIRE( resolvedDefaults.logTransmits );
    REQUIRE( QFileInfo{ resolvedDefaults.filePath }.absolutePath() == dir.path() );
    REQUIRE( QFileInfo{ resolvedDefaults.filePath }.fileName().contains( "com5_57600_" ) );

    CommanderComSettings overrides = defaultsOnly;
    overrides.filePath = dir.filePath( "manual_capture.log" );
    overrides.baudRate = 230400;
    overrides.addTimestamps = false;
    overrides.logTransmits = false;

    const auto resolvedOverrides = resolveCommanderComSettings( overrides );
    REQUIRE( resolvedOverrides.filePath == QFileInfo{ dir.filePath( "manual_capture.log" ) }.absoluteFilePath() );
    REQUIRE( resolvedOverrides.baudRate == 230400 );
    REQUIRE_FALSE( resolvedOverrides.addTimestamps );
    REQUIRE_FALSE( resolvedOverrides.logTransmits );
}

TEST_CASE( "Predefined filters migrate missing ids and persist them", "[commander][filters]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const auto settingsPath = dir.filePath( "filters.ini" );
    {
        QSettings settings( settingsPath, QSettings::IniFormat );
        settings.beginGroup( "PredefinedFiltersCollection" );
        settings.setValue( "version", 2 );
        settings.beginWriteArray( "filters" );
        settings.setArrayIndex( 0 );
        settings.setValue( "name", "Errors" );
        settings.setValue( "filter", "ERROR" );
        settings.setValue( "regex", false );
        settings.endArray();
        settings.endGroup();
    }

    PredefinedFiltersCollection collection;
    {
        QSettings settings( settingsPath, QSettings::IniFormat );
        collection.retrieveFromStorage( settings );
    }

    const auto loadedFilters = collection.getFilters();
    REQUIRE( loadedFilters.size() == 1 );
    REQUIRE_FALSE( loadedFilters.front().id.isEmpty() );
    REQUIRE( loadedFilters.front().name == "Errors" );

    {
        QSettings settings( settingsPath, QSettings::IniFormat );
        collection.saveToStorage( settings );
    }

    QSettings reloadedSettings( settingsPath, QSettings::IniFormat );
    reloadedSettings.beginGroup( "PredefinedFiltersCollection" );
    REQUIRE( reloadedSettings.value( "version" ).toInt() == 3 );
    const auto size = reloadedSettings.beginReadArray( "filters" );
    REQUIRE( size == 1 );
    reloadedSettings.setArrayIndex( 0 );
    REQUIRE( reloadedSettings.value( "id" ).toString() == loadedFilters.front().id );
    reloadedSettings.endArray();
    reloadedSettings.endGroup();
}

TEST_CASE( "Action definition variant roundtrip preserves extended fields", "[commander][actions]" )
{
    ActionDefinition action;
    action.id = 42;
    action.order = 3;
    action.name = "Ping";
    action.description = "Send ping";
    action.sequence.type = ActionSequenceType::HexString;
    action.sequence.value = "41 54 ${CHECKSUM}";
    action.parameters.repeat = true;
    action.parameters.delay = 25;
    action.parameters.repeatCount = 4;
    action.parameters.repeatInterval = 150;
    action.parameters.variableNames = { "checksum" };
    action.checksum.enabled = true;
    action.checksum.algorithm = "sum8";
    action.checksum.placeholder = "${CHECKSUM}";

    QString errorMessage;
    const auto restored = actionDefinitionFromVariantMap( actionDefinitionToVariantMap( action ),
                                                          &errorMessage );
    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( restored.id == action.id );
    REQUIRE( restored.order == action.order );
    REQUIRE( restored.parameters.repeatCount == action.parameters.repeatCount );
    REQUIRE( restored.parameters.repeatInterval == action.parameters.repeatInterval );
    REQUIRE( restored.parameters.variableNames == action.parameters.variableNames );
    REQUIRE( restored.checksum.enabled == action.checksum.enabled );
    REQUIRE( restored.checksum.algorithm == action.checksum.algorithm );
}

TEST_CASE( "Wildcard response matching captures matching lines", "[commander][actions]" )
{
    ResponseDefinition response;
    response.id = 5;
    response.name = "Wildcard";
    response.match.type = ResponseMatchType::Wildcard;
    response.match.value = "READY * OK";
    response.match.compiled = QRegularExpression(
        QRegularExpression::wildcardToRegularExpression( response.match.value ),
        QRegularExpression::CaseInsensitiveOption );

    const auto match = matchResponseDefinition( response, QByteArray( "ready 123 ok" ) );
    REQUIRE( match.matched );
    REQUIRE( match.lineText == "ready 123 ok" );
}
