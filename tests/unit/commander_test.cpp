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
    REQUIRE( parameters.exit_message.contains( "get_filters" ) );
    REQUIRE( parameters.exit_message.contains( "set_filter" ) );
    REQUIRE( parameters.exit_message.contains( "close_klogg" ) );
    REQUIRE( parameters.exit_message.contains( "close_tab" ) );
    REQUIRE( parameters.exit_message.contains( "--port <name>" ) );
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
          "--args-json-file", "args.json" } );
    REQUIRE_FALSE( runScript.parse_error );
    REQUIRE( runScript.commander_request.has_value() );
    REQUIRE( runScript.commander_request->action == CommanderAction::RunScript );
    REQUIRE( runScript.commander_request->scriptFilePath.endsWith( "test.py" ) );
    REQUIRE( runScript.commander_request->argsJsonFilePath.endsWith( "args.json" ) );

    CliParameters getStatus(
        { "klogg", "command", "--action", "get_script_status", "--pretty" } );
    REQUIRE_FALSE( getStatus.parse_error );
    REQUIRE( getStatus.commander_request.has_value() );
    REQUIRE( getStatus.commander_request->action == CommanderAction::GetScriptStatus );
    REQUIRE( getStatus.commander_request->prettyOutput );
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
