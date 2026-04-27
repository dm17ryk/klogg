#include <catch2/catch.hpp>

#include <algorithm>

#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "actionsimportexport.h"

namespace {
QString sourceFilePath( const QString& relativePath )
{
    return QDir( QStringLiteral( CILOGG_SOURCE_DIR ) ).filePath( relativePath );
}

ActionDefinition makeAction( int id, const QString& name, const QString& payload )
{
    ActionDefinition action;
    action.id = id;
    action.name = name;
    action.description = name;
    action.sequence.type = ActionSequenceType::HexString;
    action.sequence.value = payload;
    return action;
}
} // namespace

TEST_CASE( "Docklight PTP parser imports send actions", "[actions][docklight]" )
{
    for ( const auto& path : { sourceFilePath( QStringLiteral( "config/Program 90 Local 6.ptp" ) ),
                               sourceFilePath( QStringLiteral( "config/Program 90 Local 7.ptp" ) ) } ) {
        const auto parsed = parseDocklightPtpFile( path );

        REQUIRE( parsed.errors.isEmpty() );
        REQUIRE( parsed.actions.size() == 663 );

        const auto& first = parsed.actions.at( 0 );
        REQUIRE( first.id == 0 );
        REQUIRE( first.name == "Free Text" );
        REQUIRE( first.sequence.type == ActionSequenceType::HexString );
        REQUIRE( first.sequence.value == "54 72 61 63 65 3A 3B 0D 0A" );
        REQUIRE( first.parameters.repeat );
        REQUIRE( first.parameters.repeatCount == 2 );
        REQUIRE( first.parameters.delay == 10000 );

        const auto& fractionalDelay = parsed.actions.at( 15 );
        REQUIRE( fractionalDelay.id == 15 );
        REQUIRE( fractionalDelay.parameters.delay == 500 );

        const auto emptyPayload = std::find_if( parsed.actions.cbegin(),
                                                parsed.actions.cend(),
                                                []( const ActionDefinition& action ) {
                                                    return action.id == 660;
                                                } );
        REQUIRE( emptyPayload != parsed.actions.cend() );
        REQUIRE( emptyPayload->sequence.value.isEmpty() );
    }
}

TEST_CASE( "Actions merge adds missing and skips identical actions", "[actions][merge]" )
{
    QVector<ActionDefinition> existing{ makeAction( 1, "Ping", "41 54" ) };
    QVector<ActionDefinition> imported{ makeAction( 1, "Ping", "41 54" ),
                                         makeAction( 2, "Pong", "4F 4B" ) };

    const auto merged = mergeActionsConfig( existing,
                                            {},
                                            imported,
                                            {},
                                            ActionsConflictPolicy::Fail );

    REQUIRE( merged.ok );
    REQUIRE( merged.added == 1 );
    REQUIRE( merged.skipped == 1 );
    REQUIRE( merged.actions.size() == 2 );
    REQUIRE( merged.actions.at( 1 ).id == 2 );
}

TEST_CASE( "Actions merge detects id conflicts", "[actions][merge]" )
{
    QVector<ActionDefinition> existing{ makeAction( 7, "Old", "41 54" ) };
    QVector<ActionDefinition> imported{ makeAction( 7, "New", "4F 4B" ) };

    const auto merged = mergeActionsConfig( existing,
                                            {},
                                            imported,
                                            {},
                                            ActionsConflictPolicy::Fail );

    REQUIRE_FALSE( merged.ok );
    REQUIRE( merged.conflicts.size() == 1 );
    REQUIRE( merged.conflicts.front().matchType == "id" );
}

TEST_CASE( "Actions merge uses imported name conflict while preserving existing id",
           "[actions][merge]" )
{
    QVector<ActionDefinition> existing{ makeAction( 10, "Same", "41 54" ) };
    QVector<ActionDefinition> imported{ makeAction( 99, "Same", "4F 4B" ) };

    const auto merged = mergeActionsConfig( existing,
                                            {},
                                            imported,
                                            {},
                                            ActionsConflictPolicy::UseImported );

    REQUIRE( merged.ok );
    REQUIRE( merged.updated == 1 );
    REQUIRE( merged.actions.size() == 1 );
    REQUIRE( merged.actions.front().id == 10 );
    REQUIRE( merged.actions.front().sequence.value == "4F 4B" );
}

TEST_CASE( "Actions JSON export roundtrips version 3 fields", "[actions][json]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    auto action = makeAction( 3, "Templated", "41 54 ${CHECKSUM}" );
    action.parameters.repeat = true;
    action.parameters.repeatCount = 3;
    action.parameters.repeatInterval = 250;
    action.parameters.variableNames = { "value" };
    action.checksum.enabled = true;

    ResponseDefinition response;
    response.id = 4;
    response.name = "Wildcard";
    response.match.type = ResponseMatchType::Wildcard;
    response.match.value = "READY*";
    response.response.steps = { { 3, 100 } };
    response.response.hasActionId = true;
    response.response.actionId = 3;

    const auto path = dir.filePath( "actions.json" );
    QString errorMessage;
    REQUIRE( writeActionsConfigFile( path, { action }, { response }, &errorMessage ) );

    const auto parsed = parseActionsConfigFile( path, ActionsImportFormat::Json );
    REQUIRE( parsed.errors.isEmpty() );
    REQUIRE( parsed.actions.size() == 1 );
    REQUIRE( parsed.responses.size() == 1 );
    REQUIRE( parsed.actions.front().parameters.repeatCount == 3 );
    REQUIRE( parsed.actions.front().parameters.repeatInterval == 250 );
    REQUIRE( parsed.actions.front().checksum.enabled );
    REQUIRE( parsed.responses.front().match.type == ResponseMatchType::Wildcard );
    REQUIRE( parsed.responses.front().response.steps.front().delayMs == 100 );
}
