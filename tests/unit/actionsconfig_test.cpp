#include <catch2/catch.hpp>

#include "actionexpression.h"
#include "actionsconfig.h"
#include "actionsconfigparser.h"

TEST_CASE( "Response definition roundtrips linked action steps", "[actionsconfig]" )
{
    ResponseDefinition response;
    response.id = 11;
    response.name = QStringLiteral( "ready" );
    response.match.type = ResponseMatchType::String;
    response.match.value = QStringLiteral( "OK" );
    ResponseActionStep firstStep{ 101, 0 };
    firstStep.parameters
        = QVariantMap{ { "mode", QStringLiteral( "${status}" ) }, { "reset", true } };
    response.response.steps = { firstStep, { 202, 125 } };
    response.response.hasActionId = true;
    response.response.actionId = 101;
    response.response.comment = QStringLiteral( "done" );

    const auto map = responseDefinitionToVariantMap( response );
    QString errorMessage;
    const auto roundTripped = responseDefinitionFromVariantMap( map, &errorMessage );

    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( roundTripped.response.steps.size() == 2 );
    REQUIRE( roundTripped.response.steps.at( 0 ).actionId == 101 );
    REQUIRE( roundTripped.response.steps.at( 0 ).delayMs == 0 );
    REQUIRE( roundTripped.response.steps.at( 0 ).parameters == firstStep.parameters );
    REQUIRE( roundTripped.response.steps.at( 1 ).actionId == 202 );
    REQUIRE( roundTripped.response.steps.at( 1 ).delayMs == 125 );
}

TEST_CASE( "Response definition migrates legacy single action id to first step", "[actionsconfig]" )
{
    QVariantMap responseAction;
    responseAction.insert( QStringLiteral( "action_id" ), 77 );
    responseAction.insert( QStringLiteral( "comment" ), QStringLiteral( "legacy" ) );

    QVariantMap match;
    match.insert( QStringLiteral( "type" ), responseMatchTypeToString( ResponseMatchType::String ) );
    match.insert( QStringLiteral( "value" ), QStringLiteral( "READY" ) );

    QVariantMap map;
    map.insert( QStringLiteral( "id" ), 5 );
    map.insert( QStringLiteral( "name" ), QStringLiteral( "legacy response" ) );
    map.insert( QStringLiteral( "description" ), QStringLiteral( "compat" ) );
    map.insert( QStringLiteral( "match" ), match );
    map.insert( QStringLiteral( "response" ), responseAction );

    QString errorMessage;
    const auto response = responseDefinitionFromVariantMap( map, &errorMessage );

    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( response.response.steps.size() == 1 );
    REQUIRE( response.response.steps.front().actionId == 77 );
    REQUIRE( response.response.steps.front().delayMs == 0 );
    REQUIRE( response.response.hasActionId );
    REQUIRE( response.response.actionId == 77 );
}

TEST_CASE( "Response definition rejects negative step delay", "[actionsconfig]" )
{
    ResponseDefinition response;
    response.name = QStringLiteral( "invalid" );
    response.match.type = ResponseMatchType::String;
    response.match.value = QStringLiteral( "BAD" );
    response.response.steps = { { 42, -1 } };
    response.response.hasActionId = true;
    response.response.actionId = 42;

    QString errorMessage;
    REQUIRE_FALSE( validateResponseDefinition( response, &errorMessage ) );
    REQUIRE( errorMessage.contains( "delay" ) );
}

TEST_CASE( "Typed optional choice renders SCHED STATS with and without mode",
           "[actionsconfig][expression]" )
{
    ActionDefinition action;
    action.name = QStringLiteral( "SCHED STATS" );
    action.sequence.type = ActionSequenceType::String;
    action.sequence.value = QStringLiteral( "SCHED STATS:;\r\n" );
    action.expression
        = QStringLiteral( R"(concat("SCHED STATS:", mode == null ? "" : str(mode), ";\r\n"))" );

    ActionParameterDefinition mode;
    mode.name = QStringLiteral( "mode" );
    mode.label = QStringLiteral( "Mode" );
    mode.type = ActionParameterType::Choice;
    mode.presentation = ActionParameterPresentation::ComboBox;
    mode.required = false;
    mode.choices = {
        { QStringLiteral( "Queued" ), 0 },
        { QStringLiteral( "Active" ), 1 },
        { QStringLiteral( "Active + reset" ), 2 },
        { QStringLiteral( "Full" ), 3 },
    };
    action.parameters.fields.push_back( mode );

    const auto withoutMode = actionDefinitionToBytesWithParameters( action, {} );
    REQUIRE( withoutMode.ok );
    REQUIRE( withoutMode.bytes == QByteArray( "SCHED STATS:;\r\n" ) );

    const auto fullMode
        = actionDefinitionToBytesWithParameters( action, { { QStringLiteral( "mode" ), 3 } } );
    REQUIRE( fullMode.ok );
    REQUIRE( fullMode.bytes == QByteArray( "SCHED STATS:3;\r\n" ) );
}

TEST_CASE( "Multi choice bitmask renders EEPROM WRITE", "[actionsconfig][expression]" )
{
    ActionDefinition action;
    action.name = QStringLiteral( "EEPROM WRITE communications" );
    action.sequence.type = ActionSequenceType::String;
    action.sequence.value = QStringLiteral( "EEPROM WRITE:FF0B,00;\r\n" );
    action.expression = QStringLiteral( R"(concat("EEPROM WRITE:FF0B,", hex(comm, 2), ";\r\n"))" );

    ActionParameterDefinition comm;
    comm.name = QStringLiteral( "comm" );
    comm.label = QStringLiteral( "Communication interfaces" );
    comm.type = ActionParameterType::MultiChoice;
    comm.presentation = ActionParameterPresentation::CheckBoxes;
    comm.multiValueMode = ActionMultiValueMode::BitwiseOr;
    comm.choices = {
        { QStringLiteral( "GSM" ), 0x01 },
        { QStringLiteral( "PSTN" ), 0x02 },
        { QStringLiteral( "Ethernet" ), 0x04 },
        { QStringLiteral( "BLE" ), 0x08 },
    };
    action.parameters.fields.push_back( comm );

    QVariantList selected;
    selected << 0x01 << 0x04;
    const auto result = actionDefinitionToBytesWithParameters(
        action, { { QStringLiteral( "comm" ), selected } } );

    REQUIRE( result.ok );
    REQUIRE( result.bytes == QByteArray( "EEPROM WRITE:FF0B,05;\r\n" ) );
}

TEST_CASE( "Typed action fields roundtrip through variant maps", "[actionsconfig]" )
{
    ActionDefinition action;
    action.id = 1001;
    action.name = QStringLiteral( "typed" );
    action.sequence.value = QStringLiteral( "noop" );
    action.expression = QStringLiteral( "utf8(value)" );

    ActionParameterDefinition field;
    field.name = QStringLiteral( "value" );
    field.label = QStringLiteral( "Secret value" );
    field.description = QStringLiteral( "A constrained value" );
    field.type = ActionParameterType::HexBytes;
    field.presentation = ActionParameterPresentation::TextBox;
    field.required = true;
    field.sensitive = true;
    field.remember = false;
    field.defaultValue = QStringLiteral( "AA" );
    field.validationPattern = QStringLiteral( "^[0-9A-Fa-f ]+$" );
    field.expression = QStringLiteral( "hex_bytes(value)" );
    action.parameters.fields.push_back( field );

    const auto map = actionDefinitionToVariantMap( action );
    QString errorMessage;
    const auto roundTripped = actionDefinitionFromVariantMap( map, &errorMessage );

    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( roundTripped.expression == action.expression );
    REQUIRE( roundTripped.parameters.fields.size() == 1 );
    const auto& restored = roundTripped.parameters.fields.front();
    REQUIRE( restored.name == field.name );
    REQUIRE( restored.type == ActionParameterType::HexBytes );
    REQUIRE( restored.presentation == ActionParameterPresentation::TextBox );
    REQUIRE( restored.required );
    REQUIRE( restored.sensitive );
    REQUIRE_FALSE( restored.remember );
    REQUIRE( restored.defaultValue == field.defaultValue );
    REQUIRE( restored.validationPattern == field.validationPattern );
    REQUIRE( restored.expression == field.expression );
}

TEST_CASE( "Action expression supports deterministic protocol transforms",
           "[actionsconfig][expression]" )
{
    const auto arithmetic = evaluateActionExpression(
        QStringLiteral( "hex(((a | b) << 1) + 1, 4)" ),
        { { QStringLiteral( "a" ), 1 }, { QStringLiteral( "b" ), 4 } } );
    REQUIRE( arithmetic.ok );
    REQUIRE( arithmetic.value.toString() == QStringLiteral( "000B" ) );

    const auto packed = evaluateActionExpression(
        QStringLiteral( "bytes(u16be(0x1234), u16le(0x5678), bcd(\"123\"))" ), {} );
    REQUIRE( packed.ok );
    REQUIRE( packed.value.toByteArray() == QByteArray::fromHex( "123478560123" ) );

    const auto checksum = evaluateActionExpression(
        QStringLiteral( "crc16_modbus(hex_bytes(\"010300000001\"))" ), {} );
    REQUIRE( checksum.ok );
    REQUIRE( checksum.value.toLongLong() == 0x0A84 );
}

TEST_CASE( "Action expression supports numeric list and network helpers",
           "[actionsconfig][expression]" )
{
    const auto expression = evaluateActionExpression(
        QStringLiteral(
            R"(bytes(u8(clamp(-3, 0, 10)), ipv4_bytes("192.168.1.7"), mac_bytes("B0:01:02:03:04:05"), utf8(join(unique(["b", "a", "b"]), ","))))" ),
        {} );

    REQUIRE( expression.ok );
    REQUIRE( expression.value.toByteArray()
             == QByteArray::fromHex( "00c0a80107b00102030405" ) + QByteArray( "b,a" ) );

    const auto sliced = evaluateActionExpression(
        QStringLiteral(
            R"(concat(str(abs(-4)), ":", str(round(2.6)), ":", slice("abcdef", 2, 3), ":", str(contains([1,2,3], 2))))" ),
        {} );
    REQUIRE( sliced.ok );
    REQUIRE( sliced.value.toString() == QStringLiteral( "4:3:cde:true" ) );
}

TEST_CASE( "Action expression limits and frozen registry reject unsafe expansion",
           "[actionsconfig][expression]" )
{
    ActionExpressionLimits limits;
    limits.maximumExpressionLength = 8;
    const auto tooLong
        = evaluateActionExpression( QStringLiteral( "concat(\"too long\")" ), {}, limits );
    REQUIRE_FALSE( tooLong.ok );
    REQUIRE( tooLong.error.contains( QStringLiteral( "length" ) ) );

    ActionExpressionLimits nestingLimits;
    nestingLimits.maximumNestingDepth = 2;
    const auto tooDeep = evaluateActionExpression( QStringLiteral( "(((1)))" ), {}, nestingLimits );
    REQUIRE_FALSE( tooDeep.ok );
    REQUIRE( tooDeep.error.contains( QStringLiteral( "nesting" ) ) );

    REQUIRE( ActionExpressionRegistry::instance().isFrozen() );
    QString registrationError;
    const auto registered = ActionExpressionRegistry::instance().registerFunction(
        QStringLiteral( "test.late" ),
        []( const QVector<QVariant>& ) { return ActionExpressionResult{ true, 1, {} }; },
        &registrationError );
    REQUIRE_FALSE( registered );
    REQUIRE( registrationError.contains( QStringLiteral( "frozen" ) ) );
}

TEST_CASE( "Actions parser accepts versions 1 3 and 4 and rejects unknown versions",
           "[actionsconfig]" )
{
    const auto jsonForVersion = []( int version ) {
        return QStringLiteral(
                   R"JSON({"version":%1,"actions":[{"id":1,"name":"typed","sequence":{"type":"string","value":"X"},"expression":"concat(\"X\", value)","parameters":{"fields":[{"name":"value","type":"choice","presentation":"radio","choices":[{"label":"One","value":1}]}]}}],"responses":[]})JSON" )
            .arg( version )
            .toUtf8();
    };

    ActionsConfigParser parser;
    for ( const auto version : { 1, 3, 4 } ) {
        const auto parsed = parser.parseJson( jsonForVersion( version ) );
        INFO( "version=" << version << " errors=" << parsed.errors.join( "; " ).toStdString() );
        REQUIRE( parsed.errors.isEmpty() );
        REQUIRE( parsed.actions.size() == 1 );
        REQUIRE( parsed.actions.front().parameters.fields.size() == 1 );
        REQUIRE( parsed.actions.front().parameters.fields.front().presentation
                 == ActionParameterPresentation::RadioButtons );
    }

    const auto unsupported = parser.parseJson( jsonForVersion( 2 ) );
    REQUIRE_FALSE( unsupported.errors.isEmpty() );
    REQUIRE( unsupported.errors.front().contains( QStringLiteral( "Unsupported" ) ) );
}

TEST_CASE( "Actions parser accepts expression-only actions", "[actionsconfig]" )
{
    const QByteArray payload
        = R"JSON({"version":4,"actions":[{"id":1,"name":"raw","expression":"bytes(u8(value))","parameters":{"fields":[{"name":"value","type":"integer","required":true}]}}],"responses":[]})JSON";

    ActionsConfigParser parser;
    const auto parsed = parser.parseJson( payload );

    REQUIRE( parsed.errors.isEmpty() );
    REQUIRE( parsed.actions.size() == 1 );
    REQUIRE( parsed.actions.front().sequence.value.isEmpty() );
    REQUIRE( parsed.actions.front().expression == QStringLiteral( "bytes(u8(value))" ) );
}

TEST_CASE( "Response parameter bindings roundtrip explicit and legacy forms", "[actionsconfig]" )
{
    const ResponseParameterBinding capture{ ResponseParameterBindingSource::Capture,
                                            QStringLiteral( "status" ) };
    const auto encoded = responseParameterBindingToVariant( capture );
    QString errorMessage;
    const auto decoded = responseParameterBindingFromVariant( encoded, &errorMessage );
    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( decoded.source == ResponseParameterBindingSource::Capture );
    REQUIRE( decoded.value.toString() == QStringLiteral( "status" ) );

    const auto legacy = responseParameterBindingFromVariant( QStringLiteral( "${status}" ),
                                                              &errorMessage );
    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( legacy.source == ResponseParameterBindingSource::Legacy );
    REQUIRE( legacy.value.toString() == QStringLiteral( "${status}" ) );
}

TEST_CASE( "Response parameter bindings reject unknown explicit sources", "[actionsconfig]" )
{
    QString errorMessage;
    const auto binding = responseParameterBindingFromVariant(
        QVariantMap{ { QStringLiteral( "source" ), QStringLiteral( "unknown" ) },
                     { QStringLiteral( "value" ), QStringLiteral( "x" ) } },
        &errorMessage );
    REQUIRE( binding.source == ResponseParameterBindingSource::Legacy );
    REQUIRE_FALSE( errorMessage.isEmpty() );
}

TEST_CASE( "Inline response serialization matches the public schema", "[actionsconfig]" )
{
    ResponseDefinition response;
    response.id = 17;
    response.name = QStringLiteral( "inline" );
    response.match.value = QStringLiteral( "READY" );
    response.response.hasInlineAction = true;
    response.response.inlineAction.type = ActionSequenceType::String;
    response.response.inlineAction.value = QStringLiteral( "PING;\r\n" );

    const auto map = responseDefinitionToVariantMap( response );
    const auto responseMap = map.value( QStringLiteral( "response" ) ).toMap();
    REQUIRE_FALSE( responseMap.contains( QStringLiteral( "action_id" ) ) );
    REQUIRE_FALSE( responseMap.contains( QStringLiteral( "has_action_id" ) ) );
    REQUIRE_FALSE( responseMap.contains( QStringLiteral( "has_inline_action" ) ) );
    REQUIRE( responseMap.contains( QStringLiteral( "action" ) ) );
}

TEST_CASE( "Expression-only actions roundtrip without an empty sequence", "[actionsconfig]" )
{
    ActionDefinition action;
    action.id = 9;
    action.name = QStringLiteral( "expression-only" );
    action.expression = QStringLiteral( "utf8(value)" );

    const auto map = actionDefinitionToVariantMap( action );
    REQUIRE_FALSE( map.contains( QStringLiteral( "sequence" ) ) );

    QString errorMessage;
    const auto restored = actionDefinitionFromVariantMap( map, &errorMessage );
    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( restored.sequence.value.isEmpty() );
    REQUIRE( restored.expression == action.expression );
}
