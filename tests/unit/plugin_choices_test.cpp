// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Choices, completion and validation (E17c, docs/plugin-sdk-advanced.md section 8): the feature
// list, completion answers, the field/complete query and the validation rules.
#include "pluginchoicewidgets.h"
#include "pluginfeatures.h"
#include "pluginmanager.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <catch2/catch.hpp>
#include <functional>
#include <memory>

using namespace cilogg::plugins;

namespace {
bool waitUntil( const std::function<bool()>& predicate, int timeout = 5000 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !predicate() && timer.elapsed() < timeout ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 2 );
    }
    return predicate();
}
} // namespace

TEST_CASE( "E17c announces choices, toggles, field kinds and validation", "[plugins][E17]" )
{
    const auto versions = hostFeatureVersions();
    for ( const auto* feature : { "choices", "toggles", "field_kinds", "validation" } ) {
        INFO( feature );
        REQUIRE( versions.value( QLatin1String( feature ) ).toInt() == 1 );
        REQUIRE( hostFeatureNames().contains( QLatin1String( feature ) ) );
    }
    REQUIRE( queryTimeouts( "field/complete" ).defaultMs == 500 );
}

TEST_CASE( "E17c completion answers are checked and capped", "[plugins][E17]" )
{
    QString error;
    REQUIRE( sanitizeCompletion( QJsonValue::Null, &error ).isEmpty() );
    auto answer = sanitizeCompletion(
        QJsonArray{ QJsonObject{ { "value", "alpha" }, { "label", "Alpha" }, { "icon", "builtin:ok" } },
                    QJsonObject{ { "value", "beta" }, { "icon", "../bad.png" } } },
        &error );
    REQUIRE( error.isEmpty() );
    const auto items = answer.value( "items" ).toArray();
    REQUIRE( items.size() == 2 );
    REQUIRE( items.at( 0 ).toObject().value( "icon" ) == "builtin:ok" );
    REQUIRE_FALSE( items.at( 1 ).toObject().contains( "icon" ) ); // an unusable icon is dropped
    QJsonArray many;
    for ( int index = 0; index < 150; ++index ) {
        many.append( QJsonObject{ { "value", QString::number( index ) } } );
    }
    REQUIRE( sanitizeCompletion( QJsonObject{ { "items", many } }, &error )
                 .value( "items" )
                 .toArray()
                 .size()
             == MaxCompletionItems );
    for ( const auto& bad : { QJsonValue( 3 ), QJsonValue( QJsonObject{ { "items", 1 } } ),
                              QJsonValue( QJsonArray{ QJsonObject{ { "label", "no value" } } } ) } ) {
        error.clear();
        sanitizeCompletion( bad, &error );
        REQUIRE_FALSE( error.isEmpty() );
    }
}

TEST_CASE( "E17c field/complete reaches the plugin and its answer is checked", "[plugins][E17]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    const QString id = "org.test.complete";
    const auto folder = QDir( shipped ).filePath( id );
    REQUIRE( QDir().mkpath( folder ) );
    const auto executable = QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName();
    const QJsonObject manifest{ { "id", id },
                                { "name", "Complete" },
                                { "version", "1.0.0" },
                                { "protocol_version", 1 },
                                { "executable", executable },
                                { "arguments", QJsonArray{} },
                                { "capabilities", QJsonArray{ "tool" } },
                                { "configuration", QJsonArray{} },
                                { "handles", QJsonArray{ "field/complete", "query/cancel" } },
                                { "actions", QJsonArray{ QJsonObject{ { "id", "summary" },
                                                                      { "capability", "tool" } } } } };
    QFile file( QDir( folder ).filePath( "plugin.json" ) );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( QJsonDocument( manifest ).toJson() );
    file.close();
    REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ), QDir( folder ).filePath( executable ) ) );
    PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
    REQUIRE( manager.manage( "enable", id, {} ).ok() );
    REQUIRE( manager.startPlugin( id ).ok() );
    REQUIRE( waitUntil( [ & ] { return manager.pluginState( id ) == "ready"; } ) );
    const auto ask = [ & ]( const QString& text ) {
        auto result = std::make_shared<PluginQueries::Outcome>();
        auto done = std::make_shared<bool>( false );
        const auto queryId = manager.query(
            id, "field/complete",
            { { "view_id", "main" }, { "window_id", "w1" }, { "element_id", "name" },
              { "text", text }, { "cursor", text.size() } },
            0, "complete:w1", "view:w1/main",
            [ result, done ]( const PluginQueries::Outcome& outcome ) {
                *result = outcome;
                *done = true;
            } );
        REQUIRE_FALSE( queryId.isEmpty() );
        REQUIRE( waitUntil( [ & ] { return *done; } ) );
        return *result;
    };
    auto outcome = ask( "al" );
    REQUIRE( outcome.status == PluginQueries::Status::Answered );
    const auto items = outcome.result.toObject().value( "items" ).toArray();
    REQUIRE( items.size() == 2 );
    REQUIRE( items.at( 0 ).toObject().value( "label" ) == "ALPHA" );
    REQUIRE( ask( "bad" ).status == PluginQueries::Status::Invalid );
}

TEST_CASE( "E17c validation rules and choice parsing", "[plugins][E17]" )
{
    using pluginfields::validationError;
    REQUIRE( validationError( { { "required", true } }, QJsonValue::Null ) == "Required" );
    REQUIRE( validationError( { { "required", true } }, QJsonArray{} ) == "Required" );
    REQUIRE( validationError( { { "required", false } }, "" ).isEmpty() );
    REQUIRE( validationError( { { "pattern", "[0-9]+" } }, "12a" ) == "Not in the expected form" );
    REQUIRE( validationError( { { "pattern", "[0-9]+" } }, "123" ).isEmpty() );
    REQUIRE( validationError( { { "pattern", "(" } }, "x" ).isEmpty() ); // an invalid pattern checks nothing
    REQUIRE( validationError( { { "min_length", 3 } }, "ab" ).startsWith( "At least 3" ) );
    REQUIRE( validationError( { { "min", 2 } }, 1 ) == "At least 2" );
    REQUIRE( validationError( { { "max", 2 } }, 3 ) == "At most 2" );
    REQUIRE( validationError( { { "error", "Taken" }, { "required", true } }, "x" ) == "Taken" );
    REQUIRE_FALSE( pluginfields::validates( { { "label", "x" }, { "error", "" } } ) );
    REQUIRE( pluginfields::validates( { { "max", 1 } } ) );
    // Rules count by value: the neutral defaults a later root leaves are no rules.
    REQUIRE_FALSE( pluginfields::validates( { { "required", false }, { "pattern", "" } } ) );
    REQUIRE_FALSE( pluginfields::validates( { { "max_length", -1 } } ) );
    REQUIRE( validationError( { { "max_length", -1 } }, "anything" ).isEmpty() );
    REQUIRE( validationError( { { "max", 1234567.0 } }, 2e6 ) == "At most 1234567" );
    // Lengths count characters: an emoji (two UTF-16 units) is one, as Python's len() says.
    const auto emoji = QString::fromUtf8( "\xF0\x9F\x98\x80" );
    REQUIRE( pluginfields::characterCount( emoji ) == 1 );
    REQUIRE( pluginfields::characterCount( emoji + "ab" + emoji ) == 4 );
    REQUIRE( pluginfields::characterCount( QString( QChar( 0xD83D ) ) ) == 1 ); // a lone surrogate
    REQUIRE( validationError( { { "max_length", 1 } }, emoji ).isEmpty() );
    REQUIRE( validationError( { { "min_length", 2 } }, emoji ).startsWith( "At least 2" ) );
    // Patterns run on the GUI thread: a long value is left to the plugin, and a pattern that
    // backtracks without end stops at PCRE2's match limit.
    REQUIRE( validationError( { { "pattern", "[0-9]+" } },
                              QString( pluginfields::MaxPatternText + 1, QLatin1Char( 'x' ) ) )
                 .isEmpty() );
    QElapsedTimer costly;
    costly.start();
    for ( int round = 0; round < 3; ++round ) {
        validationError( { { "pattern", "(a+)+b" } }, QString( 40, QLatin1Char( 'a' ) ) );
    }
    REQUIRE( costly.elapsed() < 10000 );
    const auto choices = pluginchoices::parse(
        { "plain", QJsonObject{ { "type", "separator" } },
          QJsonObject{ { "type", "section" }, { "label", "Group" } },
          QJsonObject{ { "value", 3 }, { "enabled", false }, { "tooltip", "t" } } },
        3 );
    REQUIRE( choices.size() == 3 ); // capped
    REQUIRE( choices.at( 0 ).label == "plain" );
    REQUIRE( choices.at( 1 ).type == pluginchoices::Choice::Type::Separator );
    REQUIRE( choices.at( 2 ).label == "Group" );
}
