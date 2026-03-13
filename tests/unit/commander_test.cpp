#include <catch2/catch.hpp>

#include <QFileInfo>
#include <QTemporaryDir>

#include "cli.h"
#include "commander.h"
#include "comportutils.h"
#include "configuration.h"

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

TEST_CASE( "Main CLI help advertises commander mode", "[commander][cli]" )
{
    CliParameters parameters( { "klogg", "--help" } );

    REQUIRE( parameters.exit_requested );
    REQUIRE( parameters.exit_message.contains( "klogg command --action open_file" ) );
    REQUIRE( parameters.exit_message.contains( "klogg command --action close_com" ) );
    REQUIRE( parameters.exit_message.contains( "klogg command --action get_info" ) );
}

TEST_CASE( "Commander request variant roundtrip", "[commander][ipc]" )
{
    CommanderRequest request;
    request.action = CommanderAction::CloseTab;
    request.tabId = "tab-123";
    request.windowIndex = 1;
    request.tabIndex = 2;
    request.portName = "COM9";
    request.comSettings.portName = "COM9";
    request.comSettings.baudRate = 115200;
    request.comSettings.addTimestamps = false;
    request.comSettings.useForActions = true;

    QString errorMessage;
    const auto restored
        = commanderRequestFromVariantMap( commanderRequestToVariantMap( request ), &errorMessage );

    REQUIRE( restored.has_value() );
    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( restored->action == request.action );
    REQUIRE( restored->tabId == request.tabId );
    REQUIRE( restored->windowIndex == request.windowIndex );
    REQUIRE( restored->tabIndex == request.tabIndex );
    REQUIRE( restored->portName == request.portName );
    REQUIRE( restored->comSettings.baudRate.has_value() );
    REQUIRE( restored->comSettings.baudRate == request.comSettings.baudRate );
    REQUIRE( restored->comSettings.addTimestamps.has_value() );
    REQUIRE( restored->comSettings.addTimestamps == request.comSettings.addTimestamps );
    REQUIRE( restored->comSettings.useForActions.has_value() );
    REQUIRE( restored->comSettings.useForActions == request.comSettings.useForActions );
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
