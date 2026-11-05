// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewdecodersettings.h"
#include "previewservice.h"
#include "previewtree.h"
#include "tsharkpreviewclient.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <catch2/catch.hpp>
#include <memory>

namespace {
struct RestoreDecoder {
    QVariantMap saved = PreviewDecoderSettings::instance().configuration();
    ~RestoreDecoder()
    {
        PreviewDecoderSettings::instance().setConfiguration( saved );
    }
};
struct RestoreEnvironment {
    QByteArray name, saved;
    explicit RestoreEnvironment( const char* key )
        : name( key )
        , saved( qgetenv( key ) )
    {
    }
    ~RestoreEnvironment()
    {
        if ( saved.isNull() )
            qunsetenv( name.constData() );
        else
            qputenv( name.constData(), saved );
    }
};
PreviewEvaluationRequest directRequest( const QString& text, const QString& encoding )
{
    PreviewEvaluationRequest request;
    request.text = text;
    request.decoder = "tshark";
    request.inputEncoding = encoding;
    request.dissector = "data";
    request.timeoutMs = 10000;
    request.executable = QString::fromUtf8( PREVIEW_APP_PATH );
    return request;
}
} // namespace

TEST_CASE( "Decoder settings replace atomically with strict trusted types", "[previewdecoder]" )
{
    RestoreDecoder restore;
    auto& settings = PreviewDecoderSettings::instance();
    REQUIRE( settings.setConfiguration( {} ) );
    CHECK_FALSE( settings.configuration().value( "enabled" ).toBool() );
    const auto original = settings.configuration();
    const auto revision = settings.revision();
    QString error;
    for ( const QVariantMap& invalid :
          { QVariantMap{ { "enabled", "true" } }, QVariantMap{ { "unknown", true } },
            QVariantMap{ { "enabled", true } }, QVariantMap{ { "inputEncoding", "unknown" } },
            QVariantMap{ { "defaultDissector", "data;system()" } },
            QVariantMap{ { "defaultDissector", "data\n" } },
            QVariantMap{
                { "previewRoutes",
                  QVariantMap{ { "Carrier", QVariantMap{ { "dissector", "data\n" } } } } } },
            QVariantMap{ { "lua", QVariantList{ "relative.lua" } } } } ) {
        CHECK_FALSE( settings.setConfiguration( invalid, &error ) );
        CHECK_FALSE( error.isEmpty() );
        CHECK( settings.configuration() == original );
        CHECK( settings.revision() == revision );
    }
    QVariantList tooManyScripts;
    for ( int i = 0; i < 9; ++i )
        tooManyScripts << "irrelevant.lua";
    CHECK_FALSE( settings.setConfiguration( { { "lua", tooManyScripts } }, &error ) );
    CHECK( error == "script_limit" );
    for ( const auto& key : { QString( "previewRoutes" ), QString( "protocolRoutes" ) } ) {
        QVariantMap routes;
        const int count = key == "previewRoutes" ? 129 : 33;
        for ( int i = 0; i < count; ++i )
            routes[ QString::number( i ) ] = QVariantMap{};
        CHECK_FALSE( settings.setConfiguration( { { key, routes } }, &error ) );
        CHECK( error == "route_limit" );
    }
    QVariantMap largeRoutes;
    for ( int i = 0; i < 20; ++i )
        largeRoutes[ QString( 4000, 'x' ) + QString::number( i ) ]
            = QVariantMap{ { "dissector", "data" } };
    CHECK_FALSE( settings.setConfiguration( { { "previewRoutes", largeRoutes } }, &error ) );
    CHECK( error == "configuration_limit" );
    CHECK( settings.configuration() == original );
    REQUIRE( settings.setConfiguration(
        { { "previewRoutes",
            QVariantMap{
                { "Carrier", QVariantMap{ { "dissector", "data" }, { "protocol", "" } } } } } } ) );
    CHECK( settings.revision() != revision );
    CHECK_FALSE( settings.setConfiguration(
        { { "protocolRoutes",
            QVariantMap{
                { "P", QVariantMap{ { "dissector", "data" }, { "tool", "untrusted" } } } } } } ) );
}

TEST_CASE( "Configured scripts need approval and file changes invalidate revisions",
           "[previewdecoder]" )
{
    QTemporaryDir directory;
    RestoreDecoder restore;
    REQUIRE( directory.isValid() );
    const auto script = directory.filePath( "probe.lua" );
    QFile file( script );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "-- public test" );
    file.close();
    auto& settings = PreviewDecoderSettings::instance();
    REQUIRE( settings.setConfiguration( { { "lua", QVariantList{ script } } } ) );
    auto approved = settings.configuration();
    approved[ "enabled" ] = true;
    approved[ "tshark" ] = QCoreApplication::applicationFilePath();
    approved[ "text2pcap" ] = QCoreApplication::applicationFilePath();
    QString approvalError;
    CHECK_FALSE( settings.setConfiguration( approved, &approvalError ) );
    CHECK( approvalError == "external_lua_not_approved" );
    approved[ "allowExternalLua" ] = true;
    REQUIRE( settings.setConfiguration( approved ) );
    const auto revision = settings.revision();
    QSignalSpy changed( &settings, &PreviewDecoderSettings::changed );
    QFile sibling( directory.filePath( "unrelated.log" ) );
    REQUIRE( sibling.open( QIODevice::WriteOnly ) );
    sibling.write( "unrelated directory activity" );
    sibling.close();
    QEventLoop unrelated;
    QTimer::singleShot( 200, &unrelated, &QEventLoop::quit );
    unrelated.exec();
    CHECK( settings.revision() == revision );
    CHECK( changed.count() == 0 );
    REQUIRE( file.open( QIODevice::Append ) );
    file.write( "\n-- change" );
    file.close();
    QEventLoop loop;
    QObject::connect( &settings, &PreviewDecoderSettings::changed, &loop, &QEventLoop::quit );
    QTimer::singleShot( 2000, &loop, &QEventLoop::quit );
    loop.exec();
    CHECK( changed.count() >= 1 );
    CHECK( settings.revision() != revision );
    const auto beforeReplacement = settings.revision();
    REQUIRE( QFile::remove( script ) );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "-- replacement" );
    file.close();
    QEventLoop replaced;
    QTimer::singleShot( 300, &replaced, &QEventLoop::quit );
    replaced.exec();
    CHECK( settings.revision() != beforeReplacement );
}

TEST_CASE( "Shared preview carrier retains capture and initial byte offset", "[previewdecoder]" )
{
    PreviewDefinition definition;
    definition.name = "Carrier";
    definition.compiled = QRegularExpression( "^prefix:(?<bytes>[0-9a-f ]+)$" );
    definition.bufferCapture = { true, false, 0, "bytes" };
    definition.type = PreviewBufferType::HexString;
    definition.offset = { true, true, 1, {} };
    const auto carrier = preparePreviewBuffer( "prefix:aa 01 02", definition );
    REQUIRE( carrier.ok );
    CHECK( carrier.bytes.mid( carrier.initialOffset ) == QByteArray::fromHex( "0102" ) );
    CHECK_FALSE( preparePreviewBuffer( "unmatched", definition ).ok );
    definition.offset.literalValue = 100;
    CHECK_FALSE( preparePreviewBuffer( "prefix:aa 01 02", definition ).ok );
    CHECK_FALSE( preparePreviewInput( "QUJD$", "base64" ).ok );
    CHECK_FALSE( preparePreviewInput( "QQ", "base64" ).ok );
    CHECK_FALSE( preparePreviewInput( "QR==", "base64" ).ok );
    CHECK( preparePreviewInput( "QUJD", "base64" ).bytes == "ABC" );
    definition.offset.literalValue = 1;
    CHECK( preparePreviewBuffer( "prefix:aa 01 02", definition, "utf8" ).bytes == "aa 01 02" );
}

TEST_CASE( "Disabled explicit native decoder is visible and JSON override stays legacy",
           "[previewdecoder]" )
{
    RestoreDecoder restore;
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( {} ) );
    auto request = directRequest( "0102", "hex" );
    CHECK( PreviewService::evaluateAndWait( request ).value( "reason" ) == "decoder_disabled" );
    request.decoder = "json";
    request.definitions = R"({"version":1,"previews":[]})";
    CHECK( PreviewService::evaluateAndWait( request ).value( "backend" ) == "json_preview" );
    CHECK( PreviewService::evaluateAndWait( request ).value( "reason" )
           == "unsupported_decoder_options" );
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "enabled", true },
          { "tshark", QCoreApplication::applicationFilePath() },
          { "text2pcap", QCoreApplication::applicationFilePath() } } ) );
    request.decoder = "tshark";
    request.dissector.clear();
    request.definitions = QByteArray( 1024 * 1024 + 1, 'x' );
    CHECK( PreviewService::evaluateAndWait( request ).value( "reason" ) == "invalid_dissector" );
}

TEST_CASE( "Decoder revisions belong only to internal snapshots", "[previewdecoder]" )
{
    RestoreDecoder restore;
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( {} ) );
    const auto first = PreviewService::definitions();
    auto config = PreviewDecoderSettings::instance().configuration();
    config[ "defaultDissector" ] = "ip";
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( config ) );
    const auto second = PreviewService::definitions();
    CHECK( first != second );
    CHECK( QJsonDocument::fromJson( second ).object().value( "_decoderRuntimeRevision" ).toString()
           == PreviewDecoderSettings::instance().revision() );
}

TEST_CASE( "Native decoder routes retain named carrier precedence", "[previewdecoder]" )
{
    if ( qEnvironmentVariable( "CILOGG_TSHARK" ).isEmpty()
         || qEnvironmentVariable( "CILOGG_TEXT2PCAP" ).isEmpty() ) {
        WARN( "Actual route test skipped: explicit engine paths absent" );
        return;
    }
    RestoreDecoder restore;
    QVariantMap config{
        { "enabled", true },
        { "tshark", qEnvironmentVariable( "CILOGG_TSHARK" ) },
        { "text2pcap", qEnvironmentVariable( "CILOGG_TEXT2PCAP" ) },
        { "previewRoutes", QVariantMap{ { "Carrier", QVariantMap{ { "dissector", "data" } } } } },
        { "protocolRoutes",
          QVariantMap{
              { "Assembly", QVariantMap{ { "dissector", "data" }, { "inputEncoding", "hex" } } } } }
    };
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( config ) );
    auto request = directRequest( "prefix:aa 01 02", "" );
    request.decoder = "auto";
    request.dissector.clear();
    request.previewName = "Carrier";
    request.assemblyProtocol = "Assembly";
    request.definitions
        = R"({"version":1,"previews":[{"name":"Carrier","regex":"^prefix:(?<bytes>.*)$","bufferCapture":"bytes","type":"hexString","offset":1,"fields":[]}]})";
    auto result = PreviewService::evaluateAndWait( request );
    INFO( result.value( "reason" ).toString().toStdString() );
    REQUIRE( result.value( "available" ).toBool() );
    CHECK( result.value( "backend" ) == "tshark_lua" );
    CHECK( result.value( "previewName" ) == "Carrier" );
    bool exact = false;
    for ( const auto& field : result.value( "fields" ).toList() )
        if ( field.toMap().value( "attributes" ).toMap().value( "value" ) == "0102" )
            exact = true;
    CHECK( exact );
    request.previewName.clear();
    request.text = "0304";
    result = PreviewService::evaluateAndWait( request );
    REQUIRE( result.value( "available" ).toBool() );
    CHECK( result.value( "previewName" ) == "Assembly" );
    request.decoder = "json";
    request.text = "prefix:aa 01 02";
    CHECK( PreviewService::evaluateAndWait( request ).value( "backend" ) == "json_preview" );
    config[ "enabled" ] = false;
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( config ) );
    request.decoder = "auto";
    CHECK( PreviewService::evaluateAndWait( request ).value( "backend" ) == "json_preview" );
    request.inputEncoding = "hex";
    CHECK( PreviewService::evaluateAndWait( request ).value( "reason" )
           == "unsupported_decoder_options" );
}

TEST_CASE( "Cancellation after selector exit releases the native channel", "[previewdecoder]" )
{
    if ( qEnvironmentVariable( "CILOGG_TSHARK" ).isEmpty()
         || qEnvironmentVariable( "CILOGG_TEXT2PCAP" ).isEmpty() ) {
        WARN( "Actual cancellation test skipped: explicit engine paths absent" );
        return;
    }
    RestoreDecoder restore;
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "enabled", true },
          { "tshark", qEnvironmentVariable( "CILOGG_TSHARK" ) },
          { "text2pcap", qEnvironmentVariable( "CILOGG_TEXT2PCAP" ) } } ) );
    QObject channel;
    auto request = directRequest( "01020304", "hex" );
    request.sessionOwner = &channel;
    for ( const bool changeConfig : { false, true } ) {
        QEventLoop loop;
        int completions = 0;
        QVariantMap result;
        QPointer<QProcess> token
            = PreviewService::evaluate( &loop, request, [ & ]( QVariantMap value ) {
                  ++completions;
                  result = std::move( value );
                  loop.quit();
              } );
        REQUIRE( token );
        QObject::connect( token, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ),
                          &loop, [ token, changeConfig ]( int, QProcess::ExitStatus ) {
                              if ( changeConfig ) {
                                  auto config = PreviewDecoderSettings::instance().configuration();
                                  config[ "inputEncoding" ]
                                      = config.value( "inputEncoding" ) == "hex" ? "utf8" : "hex";
                                  PreviewDecoderSettings::instance().setConfiguration( config );
                              }
                              else
                                  PreviewService::cancel( token );
                          } );
        QTimer::singleShot( 12000, &loop, &QEventLoop::quit );
        loop.exec();
        REQUIRE( completions == 1 );
        CHECK( result.value( "reason" )
               == ( changeConfig ? "configuration_changed" : "cancelled" ) );
        CHECK( PreviewService::evaluateAndWait( request ).value( "available" ).toBool() );
    }
}

TEST_CASE( "Native embedded worker reuses an actual public engine", "[previewdecoder]" )
{
    if ( qEnvironmentVariable( "CILOGG_TSHARK" ).isEmpty()
         || qEnvironmentVariable( "CILOGG_TEXT2PCAP" ).isEmpty() ) {
        WARN( "Actual engine test skipped: set CILOGG_TSHARK and CILOGG_TEXT2PCAP" );
        return;
    }
    QTemporaryDir selectedScripts;
    RestoreDecoder restore;
    REQUIRE( selectedScripts.isValid() );
    const auto publicLua = selectedScripts.filePath( "public_probe.lua" );
    REQUIRE( QFile::copy( QString::fromUtf8( PREVIEW_PUBLIC_LUA_PATH ), publicLua ) );
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "enabled", true },
          { "tshark", qEnvironmentVariable( "CILOGG_TSHARK" ) },
          { "text2pcap", qEnvironmentVariable( "CILOGG_TEXT2PCAP" ) },
          { "defaultDissector", "cilogg_md01_probe" },
          { "allowExternalLua", true },
          { "lua", QVariantList{ publicLua } } } ) );
    QObject channel;
    auto request = directRequest( "01020304", "hex" );
    request.sessionOwner = &channel;
    request.dissector = "cilogg_md01_probe";
    request.text = "0102";
    const auto first = PreviewService::evaluateAndWait( request );
    INFO( first.value( "reason" ).toString().toStdString() );
    REQUIRE( first.value( "available" ).toBool() );
    REQUIRE( first.value( "enginePid" ).toLongLong() > 0 );
    const auto runtimeRevision = PreviewDecoderSettings::instance().revision();
    QFile unrelated( selectedScripts.filePath( "unrelated.log" ) );
    REQUIRE( unrelated.open( QIODevice::WriteOnly ) );
    unrelated.write( "selected-script sibling activity" );
    unrelated.close();
    QEventLoop unrelatedLoop;
    QTimer::singleShot( 200, &unrelatedLoop, &QEventLoop::quit );
    unrelatedLoop.exec();
    CHECK( PreviewDecoderSettings::instance().revision() == runtimeRevision );
    request.definitions = QByteArray( 1024 * 1024 + 1, 'x' );
    request.text = "AwQ=";
    request.inputEncoding = "base64";
    const auto second = PreviewService::evaluateAndWait( request );
    REQUIRE( second.value( "available" ).toBool() );
    CHECK( second.value( "enginePid" ) == first.value( "enginePid" ) );
    CHECK( second.value( "offsetSpace" ) == "engine_data_source_unresolved" );
    CHECK( second.value( "messageCompleteness" ) == "not_reported" );
    CHECK( PreviewService::tooltipHtml( second ).contains( "TShark" ) );
    const auto items = []( const QVariantMap& result ) {
        QStringList values;
        for ( const auto& field : result.value( "fields" ).toList() ) {
            const auto entry = field.toMap();
            if ( entry.value( "attributes" ).toMap().value( "name" ) == "cilogg_md01_probe.item" )
                values << entry.value( "columns" ).toList().at( 1 ).toString();
        }
        return values;
    };
    CHECK( items( first ) == QStringList{ "1", "2" } );
    CHECK( items( second ) == QStringList{ "3", "4" } );
    request.text = "!not base64!";
    CHECK( PreviewService::evaluateAndWait( request ).value( "reason" ) == "invalid_base64" );
    request.inputEncoding = "hex";
    request.text = "0g";
    CHECK( PreviewService::evaluateAndWait( request ).value( "reason" ) == "invalid_hex" );
}

TEST_CASE( "Native owner and token destruction and deadline recover cleanly", "[previewdecoder]" )
{
    if ( qEnvironmentVariable( "CILOGG_TSHARK" ).isEmpty()
         || qEnvironmentVariable( "CILOGG_TEXT2PCAP" ).isEmpty() ) {
        WARN( "Actual lifecycle test skipped: explicit engine paths absent" );
        return;
    }
    RestoreDecoder restore;
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "enabled", true },
          { "tshark", qEnvironmentVariable( "CILOGG_TSHARK" ) },
          { "text2pcap", qEnvironmentVariable( "CILOGG_TEXT2PCAP" ) } } ) );
    for ( const bool destroyToken : { false, true } ) {
        QEventLoop loop;
        auto channel = std::make_unique<QObject>();
        auto request = directRequest( "0102", "hex" );
        request.sessionOwner = channel.get();
        int completions = 0;
        QVariantMap result;
        QPointer<QProcess> token
            = PreviewService::evaluate( &loop, request, [ & ]( QVariantMap value ) {
                  ++completions;
                  result = std::move( value );
                  loop.quit();
              } );
        REQUIRE( token );
        QObject::connect( token, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ),
                          &loop, [ &channel, token, destroyToken ]( int, QProcess::ExitStatus ) {
                              if ( destroyToken )
                                  token->deleteLater();
                              else
                                  channel.reset();
                          } );
        QTimer::singleShot( 12000, &loop, &QEventLoop::quit );
        loop.exec();
        CHECK( completions == 1 );
        CHECK( result.value( "reason" ) == "cancelled" );
        request.sessionOwner = nullptr;
        REQUIRE( PreviewService::evaluateAndWait( request ).value( "available" ).toBool() );
    }
    QObject coldChannel;
    auto request = directRequest( "0102", "hex" );
    request.sessionOwner = &coldChannel;
    request.timeoutMs = 100;
    const auto timed = PreviewService::evaluateAndWait( request );
    CHECK( ( timed.value( "reason" ) == "timeout"
             || timed.value( "reason" ) == "execution_timeout" ) );
    request.timeoutMs = 10000;
    REQUIRE( PreviewService::evaluateAndWait( request ).value( "available" ).toBool() );
}

TEST_CASE( "Automatic TShark prioritizes mapped carriers and configured defaults before JSON",
           "[previewdecoder]" )
{
    if ( qEnvironmentVariable( "CILOGG_TSHARK" ).isEmpty()
         || qEnvironmentVariable( "CILOGG_TEXT2PCAP" ).isEmpty() ) {
        WARN( "Actual automatic priority test skipped: explicit engine paths absent" );
        return;
    }
    RestoreDecoder restore;
    QVariantMap config{ { "enabled", true },
                        { "tshark", qEnvironmentVariable( "CILOGG_TSHARK" ) },
                        { "text2pcap", qEnvironmentVariable( "CILOGG_TEXT2PCAP" ) },
                        { "previewRoutes",
                          QVariantMap{ { "Mapped", QVariantMap{ { "dissector", "data" } } } } } };
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( config ) );
    auto request = directRequest( "prefix:aa 01 02", "" );
    request.decoder = "auto";
    request.dissector.clear();
    request.definitions = R"({"version":1,"previews":[
        {"name":"Earlier JSON","regex":"^(?<value>.*)$","fields":[{"name":"Message","source":"capture","capture":"value","type":"string","format":"string"}]},
        {"name":"Mapped","regex":"^prefix:(?<bytes>.*)$","bufferCapture":"bytes","type":"hexString","offset":1,"fields":[]}]})";
    auto result = PreviewService::evaluateAndWait( request );
    INFO( result.value( "reason" ).toString().toStdString() );
    REQUIRE( result.value( "available" ).toBool() );
    CHECK( result.value( "backend" ) == "tshark_lua" );
    CHECK( result.value( "previewName" ) == "Mapped" );
    // A route-only configuration leaves unrelated legacy previews useful.
    request.text = "ordinary text";
    result = PreviewService::evaluateAndWait( request );
    CHECK( result.value( "backend" ) == "json_preview" );
    CHECK( result.value( "fallbackReason" ) == "no_applicable_decoder" );
    CHECK( result.value( "decoderAttempts" ).toList() == QVariantList{ "json_preview" } );
    config[ "defaultDissector" ] = "data";
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( config ) );
    result = PreviewService::evaluateAndWait( request );
    REQUIRE( result.value( "available" ).toBool() );
    CHECK( result.value( "backend" ) == "tshark_lua" );
    CHECK( result.value( "previewName" ) == "Earlier JSON" );
    request.definitions = R"({"version":1,"previews":[]})";
    result = PreviewService::evaluateAndWait( request );
    REQUIRE( result.value( "available" ).toBool() );
    CHECK( result.value( "backend" ) == "tshark_lua" );
    request.previewName = "missing carrier";
    CHECK( PreviewService::evaluateAndWait( request ).value( "reason" ) == "no_match" );
}

TEST_CASE( "Native framed client rejects malformed output and recovers within bounded resources",
           "[previewdecoder]" )
{
    RestoreEnvironment python( "CILOGG_SCRIPT_PYTHON" ), mode( "CILOGG_PREVIEW_FIXTURE_MODE" ),
        pid( "CILOGG_PREVIEW_FIXTURE_PID" );
    qputenv( "CILOGG_SCRIPT_PYTHON", QByteArray( PREVIEW_DECODER_FIXTURE_PATH ) );
    qputenv( "CILOGG_PREVIEW_FIXTURE_PID",
             QByteArray::number( QCoreApplication::applicationPid() ) );
    auto& client = TsharkPreviewClient::instance();
    const QVariantMap profile{ { "tshark", QCoreApplication::applicationFilePath() },
                               { "text2pcap", QCoreApplication::applicationFilePath() },
                               { "lua", QVariantList{} },
                               { "dissector", "data" },
                               { "protocol", "" } };
    const QMap<QString, QString> cases{
        { "metadata", "worker_protocol_error" }, { "flags", "worker_protocol_error" },
        { "hash", "worker_protocol_error" },     { "hash_newline", "worker_protocol_error" },
        { "crash", "worker_failure" },           { "id", "worker_protocol_error" },
        { "version", "worker_protocol_error" },  { "attributes", "worker_protocol_error" },
        { "depth", "worker_protocol_error" },    { "count", "worker_protocol_error" },
        { "surplus", "worker_protocol_error" },  { "large", "result_limit" },
        { "stderr", "diagnostic_limit" },        { "stall", "timeout" }
    };
    int counter = 0;
    const auto run = [ & ]( QObject* channel, int timeoutMs = 5000 ) {
        QEventLoop loop;
        struct Reply {
            QVariantMap value;
            bool received = false;
        };
        const auto reply = std::make_shared<Reply>();
        const QPointer<QEventLoop> waiter( &loop );
        const auto fixtureRequestId = QString( "fixture-%1" ).arg( ++counter );
        client.evaluate( channel, profile, QByteArray::fromHex( "0102" ), timeoutMs,
                         fixtureRequestId, [ reply, waiter ]( QVariantMap value ) {
                             reply->value = std::move( value );
                             reply->received = true;
                             if ( waiter )
                                 waiter->quit();
                         } );
        QTimer::singleShot( timeoutMs + 2000, &loop, &QEventLoop::quit );
        if ( !reply->received )
            loop.exec();
        if ( !reply->received ) {
            client.cancel( fixtureRequestId );
            return QVariantMap{ { QStringLiteral( "reason" ),
                                  QStringLiteral( "test_completion_guard_timeout" ) } };
        }
        return reply->value;
    };
    for ( auto item = cases.begin(); item != cases.end(); ++item ) {
        INFO( item.key().toStdString() );
        client.reset();
        qputenv( "CILOGG_PREVIEW_FIXTURE_MODE", item.key().toLatin1() );
        QObject channel;
        // These cases test protocol validation; the stall case retains its short deadline.
        const auto rejected = run( &channel, item.key() == "stall" ? 200 : 5000 );
        INFO(
            QJsonDocument::fromVariant( rejected ).toJson( QJsonDocument::Compact ).toStdString() );
        CHECK( rejected.value( "reason" ) == item.value() );
        qputenv( "CILOGG_PREVIEW_FIXTURE_MODE", "valid" );
        const auto recovered = run( &channel );
        REQUIRE( recovered.value( "available" ).toBool() );
        CHECK( recovered.value( "enginePid" ).toLongLong() == QCoreApplication::applicationPid() );
        const auto columns
            = recovered.value( "fields" ).toList().front().toMap().value( "columns" ).toList();
        CHECK( columns.at( 1 ).toString().isEmpty() );
        CHECK( columns.at( 3 ).toString() == "9007199254740993" );
    }
    client.reset();
    qputenv( "CILOGG_PREVIEW_FIXTURE_MODE", "stall" );
    QObject firstOwner, secondOwner, thirdOwner;
    QStringList reasons;
    const auto completed
        = [ & ]( QVariantMap result ) { reasons << result.value( "reason" ).toString(); };
    client.evaluate( &firstOwner, profile, "a", 1000, "busy-first", completed );
    client.evaluate( &firstOwner, profile, "a", 1000, "busy-same-owner", completed );
    client.evaluate( &secondOwner, profile, "a", 1000, "busy-second", completed );
    client.evaluate( &thirdOwner, profile, "a", 1000, "busy-third", completed );
    CHECK( reasons.count( "preview_busy" ) == 2 );
    client.cancel( "busy-first" );
    CHECK( reasons.count( "cancelled" ) == 1 );
    CHECK( reasons.size() == 3 ); // Cancelling one channel leaves the other request active.
    client.cancel( "busy-second" );
    CHECK( reasons.count( "cancelled" ) == 2 );
    // Cancel before started delivery: a late-starting process must still be reaped.
    qputenv( "CILOGG_PREVIEW_FIXTURE_MODE", "stall" );
    QObject channel;
    QVariantMap cancelled;
    client.evaluate( &channel, profile, "a", 1000, "startup-cancel",
                     [ & ]( QVariantMap result ) { cancelled = std::move( result ); } );
    client.cancel( "startup-cancel" );
    CHECK( cancelled.value( "reason" ) == "cancelled" );
    QEventLoop cleanup;
    QTimer poll;
    QObject::connect( &poll, &QTimer::timeout, &cleanup, [ & ] {
        if ( client.findChildren<QProcess*>().isEmpty() )
            cleanup.quit();
    } );
    poll.start( 10 );
    QTimer::singleShot( 3000, &cleanup, &QEventLoop::quit );
    cleanup.exec();
    CHECK( client.findChildren<QProcess*>().isEmpty() );
    client.reset();
}

TEST_CASE( "Direct decoder display names never select colliding carrier routes",
           "[previewdecoder]" )
{
    if ( qEnvironmentVariable( "CILOGG_TSHARK" ).isEmpty()
         || qEnvironmentVariable( "CILOGG_TEXT2PCAP" ).isEmpty() ) {
        WARN( "Actual direct route identity test skipped: explicit engine paths absent" );
        return;
    }
    RestoreDecoder restore;
    QVariantMap config{ { "enabled", true },
                        { "tshark", qEnvironmentVariable( "CILOGG_TSHARK" ) },
                        { "text2pcap", qEnvironmentVariable( "CILOGG_TEXT2PCAP" ) },
                        { "defaultDissector", "data" } };
    QObject channel;
    for ( const auto& protocol : { QString{}, QString( "ip" ) } ) {
        config[ "previewRoutes" ]
            = QVariantMap{ { "EHCI",
                             QVariantMap{ { "dissector", "ip" }, { "protocol", protocol } } } };
        REQUIRE( PreviewDecoderSettings::instance().setConfiguration( config ) );
        for ( const auto& decoder : { QString( "tshark" ), QString( "auto" ) } ) {
            for ( const auto& override : { QString{}, QString( "data" ) } ) {
                INFO( "decoder=" << decoder.toStdString() << " protocol=" << protocol.toStdString()
                                 << " override=" << override.toStdString() );
                auto request = directRequest( "01020304", "hex" );
                request.decoder = decoder;
                request.dissector = override;
                request.assemblyProtocol = "EHCI";
                request.sessionOwner = &channel;
                request.definitions = R"({"version":1,"previews":[]})";
                const auto result = PreviewService::evaluateAndWait( request );
                INFO( result.value( "reason" ).toString().toStdString() );
                REQUIRE( result.value( "available" ).toBool() );
                CHECK( result.value( "backend" ) == "tshark_lua" );
                CHECK( result.value( "previewName" ) == "EHCI" );
                bool exactData = false;
                for ( const auto& field : result.value( "fields" ).toList() ) {
                    const auto attributes = field.toMap().value( "attributes" ).toMap();
                    CHECK( attributes.value( "name" ) != "ip" );
                    if ( attributes.value( "name" ) == "data.data" )
                        exactData = attributes.value( "value" ) == "01020304";
                }
                CHECK( exactData );
            }
        }
    }
}

TEST_CASE( "Dissector overrides reject terminal newlines before worker startup",
           "[previewdecoder]" )
{
    RestoreDecoder restore;
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "enabled", true },
          { "tshark", QCoreApplication::applicationFilePath() },
          { "text2pcap", QCoreApplication::applicationFilePath() } } ) );
    auto request = directRequest( "0102", "hex" );
    request.dissector = "data\n";
    request.executable = "must-not-launch-newline-dissector";
    CHECK( PreviewService::evaluateAndWait( request ).value( "reason" ) == "invalid_dissector" );
}

TEST_CASE( "Destroyed callback owners suppress delivery and release the compatibility token",
           "[previewdecoder]" )
{
    RestoreDecoder restore;
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( {} ) );
    PreviewEvaluationRequest request;
    request.text = "stall";
    request.definitions = R"({"version":1,"previews":[]})";
    request.executable = QString::fromUtf8( PREVIEW_FIXTURE_PATH );
    request.timeoutMs = 10000;
    auto owner = std::make_unique<QObject>();
    int callbacks = 0;
    QPointer<QProcess> token
        = PreviewService::evaluate( owner.get(), request, [ & ]( QVariantMap ) { ++callbacks; } );
    REQUIRE( token );
    owner.reset();
    QEventLoop loop;
    if ( token )
        QObject::connect( token, &QObject::destroyed, &loop, &QEventLoop::quit );
    QTimer::singleShot( 3000, &loop, &QEventLoop::quit );
    if ( token )
        loop.exec();
    CHECK_FALSE( token );
    CHECK( callbacks == 0 );
    request.text = "ok";
    CHECK( PreviewService::evaluateAndWait( request ).value( "state" ) == "completed" );
}

TEST_CASE( "Only unavailable automatic dispatch falls back to supervised JSON", "[previewdecoder]" )
{
    if ( qEnvironmentVariable( "CILOGG_TSHARK" ).isEmpty()
         || qEnvironmentVariable( "CILOGG_TEXT2PCAP" ).isEmpty() ) {
        WARN( "Actual fallback test skipped: explicit engine paths absent" );
        return;
    }
    RestoreDecoder restore;
    QVariantMap config{ { "enabled", true },
                        { "tshark", qEnvironmentVariable( "CILOGG_TSHARK" ) },
                        { "text2pcap", qEnvironmentVariable( "CILOGG_TEXT2PCAP" ) },
                        { "defaultDissector", "cilogg_absent_entrypoint" } };
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( config ) );
    auto request = directRequest( "0102", "hex" );
    request.decoder = "auto";
    request.dissector.clear();
    request.definitions
        = R"({"version":1,"previews":[{"name":"JSON","regex":"^(?<value>.*)$","fields":[{"name":"Message","source":"capture","capture":"value","type":"string","format":"string"}]}]})";
    auto result = PreviewService::evaluateAndWait( request );
    INFO( result.value( "reason" ).toString().toStdString() );
    REQUIRE( result.value( "available" ).toBool() );
    CHECK( result.value( "backend" ) == "json_preview" );
    CHECK( result.value( "preferredBackend" ) == "tshark_lua" );
    CHECK( result.value( "fallbackReason" ) == "dissector_not_dispatched" );
    CHECK( result.value( "decoderAttempts" ).toList()
           == QVariantList{ "tshark_lua", "json_preview" } );
    CHECK( result.value( "fields" ).toList().front().toMap().value( "columns" ).toList().at( 1 )
           == "0102" );
    request.decoder = "tshark";
    result = PreviewService::evaluateAndWait( request );
    CHECK( result.value( "reason" ) == "dissector_not_dispatched" );
    CHECK_FALSE( result.contains( "fallbackReason" ) );
    request.decoder = "auto";
    request.text = "invalid hex";
    result = PreviewService::evaluateAndWait( request );
    CHECK( result.value( "reason" ) == "invalid_hex" );
    CHECK_FALSE( result.contains( "fallbackReason" ) );
    config[ "defaultDissector" ] = "data";
    config[ "previewRoutes" ] = QVariantMap{
        { "JSON", QVariantMap{ { "dissector", "data" }, { "protocol", "absent exact selector" } } }
    };
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( config ) );
    request.text = "0102";
    result = PreviewService::evaluateAndWait( request );
    CHECK( result.value( "reason" ) == "protocol_not_found" );
    CHECK_FALSE( result.contains( "fallbackReason" ) );
}

TEST_CASE( "Native stop and worker timeout failures retain the same required envelope",
           "[previewdecoder]" )
{
    RestoreDecoder restore;
    RestoreEnvironment python( "CILOGG_SCRIPT_PYTHON" ), mode( "CILOGG_PREVIEW_FIXTURE_MODE" ),
        pid( "CILOGG_PREVIEW_FIXTURE_PID" );
    qputenv( "CILOGG_SCRIPT_PYTHON", QByteArray( PREVIEW_DECODER_FIXTURE_PATH ) );
    qputenv( "CILOGG_PREVIEW_FIXTURE_PID",
             QByteArray::number( QCoreApplication::applicationPid() ) );
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "enabled", true },
          { "tshark", QCoreApplication::applicationFilePath() },
          { "text2pcap", QCoreApplication::applicationFilePath() },
          { "previewRoutes",
            QVariantMap{ { "Mapped carrier", QVariantMap{ { "dissector", "data" } } } } } } ) );
    QObject channel;
    auto request = directRequest( "AB", "" );
    request.previewName = "Mapped carrier";
    request.sessionOwner = &channel;
    request.dissector.clear();
    request.definitions
        = R"({"version":1,"previews":[{"name":"Mapped carrier","regex":"^(?<payload>.*)$","bufferCapture":"payload","type":"string","fields":[]}]})";
    for ( const auto& producer :
          { QString( "cancel" ), QString( "reported_timeout" ), QString( "stall" ) } ) {
        INFO( producer.toStdString() );
        TsharkPreviewClient::instance().reset();
        qputenv( "CILOGG_PREVIEW_FIXTURE_MODE",
                 producer == "cancel" ? QByteArray( "stall" ) : producer.toLatin1() );
        request.timeoutMs = producer == "reported_timeout" ? 5000 : 1500;
        QVariantMap result;
        if ( producer == "cancel" ) {
            QEventLoop loop;
            QPointer<QProcess> token
                = PreviewService::evaluate( &loop, request, [ & ]( QVariantMap value ) {
                      result = std::move( value );
                      loop.quit();
                  } );
            REQUIRE( token );
            QObject::connect(
                token, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), &loop,
                [ token ]( int, QProcess::ExitStatus ) { PreviewService::cancel( token ); } );
            QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
            loop.exec();
        }
        else
            result = PreviewService::evaluateAndWait( request );
        INFO( QJsonDocument::fromVariant( result ).toJson( QJsonDocument::Compact ).toStdString() );
        CHECK( result.value( "state" ) == "failed" );
        CHECK( result.value( "reason" ) == ( producer == "cancel" ? "cancelled" : "timeout" ) );
        CHECK( result.value( "backend" ) == "tshark_lua" );
        CHECK_FALSE( result.value( "available" ).toBool() );
        CHECK_FALSE( result.value( "complete" ).toBool() );
        CHECK_FALSE( result.value( "limited" ).toBool() );
        CHECK( result.contains( "fields" ) );
        CHECK( result.value( "fields" ).userType() == QMetaType::QVariantList );
        CHECK( result.value( "fields" ).toList().isEmpty() );
        CHECK( result.value( "previewName" ) == "Mapped carrier" );
        CHECK_FALSE( result.contains( "id" ) ); // requestId is the public authoritative identifier.
        CHECK_FALSE( result.value( "requestId" ).toString().isEmpty() );
        if ( producer == "reported_timeout" ) {
            CHECK( result.value( "enginePid" ).toLongLong() == QCoreApplication::applicationPid() );
            CHECK( result.contains( "scriptSha256" ) );
        }
    }
    TsharkPreviewClient::instance().reset();
}

TEST_CASE( "A warmed real Lua stall retains its carrier and recovers after restoring public Lua",
           "[previewdecoder]" )
{
    if ( qEnvironmentVariable( "CILOGG_TSHARK" ).isEmpty()
         || qEnvironmentVariable( "CILOGG_TEXT2PCAP" ).isEmpty() ) {
        WARN( "Actual warmed stall recovery test skipped: explicit engine paths absent" );
        return;
    }
    QTemporaryDir scripts;
    RestoreDecoder restore;
    REQUIRE( scripts.isValid() );
    const auto stalledLua = scripts.filePath( "conditional_stall.lua" );
    const auto publicLua = scripts.filePath( "public_probe.lua" );
    REQUIRE( QFile::copy( QString::fromUtf8( PREVIEW_PUBLIC_LUA_PATH ), publicLua ) );
    QFile file( stalledLua );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    const QByteArray body = "local p=Proto(\"cilogg_native_timeout\",\"Native timeout\")\n"
                            "function p.dissector(tvb,pinfo,tree) "
                            "if tvb(0,1):uint()==65 then while true do end end; "
                            "tree:add(p,tvb()); return tvb:len() end\n";
    REQUIRE( file.write( body ) == body.size() );
    file.close();
    QVariantMap config{
        { "enabled", true },
        { "allowExternalLua", true },
        { "tshark", qEnvironmentVariable( "CILOGG_TSHARK" ) },
        { "text2pcap", qEnvironmentVariable( "CILOGG_TEXT2PCAP" ) },
        { "lua", QVariantList{ stalledLua } },
        { "previewRoutes",
          QVariantMap{
              { "Mapped carrier", QVariantMap{ { "dissector", "cilogg_native_timeout" },
                                               { "protocol", "cilogg_native_timeout" } } } } }
    };
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( config ) );
    QObject channel;
    auto request = directRequest( "Z", "" );
    request.decoder = "auto";
    request.dissector.clear();
    request.previewName = "Mapped carrier";
    request.sessionOwner = &channel;
    request.definitions
        = R"({"version":1,"previews":[{"name":"Mapped carrier","regex":"^(?<payload>.*)$","bufferCapture":"payload","type":"string","fields":[]}]})";
    const auto warmed = PreviewService::evaluateAndWait( request );
    INFO( warmed.value( "reason" ).toString().left( 128 ).toStdString() );
    REQUIRE( warmed.value( "available" ).toBool() );
    REQUIRE( warmed.value( "enginePid" ).toLongLong() > 0 );
    request.text = "AB";
    request.timeoutMs = 4000;
    const auto timed = PreviewService::evaluateAndWait( request );
    INFO( timed.value( "reason" ).toString().left( 128 ).toStdString() );
    CHECK( timed.value( "state" ) == "failed" );
    CHECK( ( timed.value( "reason" ) == "timeout"
             || timed.value( "reason" ) == "execution_timeout" ) );
    CHECK( timed.value( "previewName" ) == "Mapped carrier" );
    CHECK( timed.contains( "fields" ) );
    CHECK( timed.value( "fields" ).toList().isEmpty() );
    CHECK_FALSE( timed.value( "available" ).toBool() );
    CHECK_FALSE( timed.value( "complete" ).toBool() );
    config[ "lua" ] = QVariantList{ publicLua };
    config[ "previewRoutes" ]
        = QVariantMap{ { "Mapped carrier", QVariantMap{ { "dissector", "cilogg_md01_probe" },
                                                        { "protocol", "cilogg_md01_probe" } } } };
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( config ) );
    request.text = "0102";
    request.inputEncoding = "hex";
    request.timeoutMs = 10000;
    const auto recovered = PreviewService::evaluateAndWait( request );
    INFO( recovered.value( "reason" ).toString().left( 128 ).toStdString() );
    REQUIRE( recovered.value( "available" ).toBool() );
    request.text = "0304";
    const auto reused = PreviewService::evaluateAndWait( request );
    REQUIRE( reused.value( "available" ).toBool() );
    CHECK( reused.value( "enginePid" ) == recovered.value( "enginePid" ) );
    const auto items = []( const QVariantMap& result ) {
        QStringList values;
        for ( const auto& field : result.value( "fields" ).toList() ) {
            const auto entry = field.toMap();
            if ( entry.value( "attributes" ).toMap().value( "name" ) == "cilogg_md01_probe.item" )
                values << entry.value( "columns" ).toList().at( 1 ).toString();
        }
        return values;
    };
    CHECK( items( recovered ) == QStringList{ "1", "2" } );
    CHECK( items( reused ) == QStringList{ "3", "4" } );
}

TEST_CASE( "Overdue successful delivery times out and retires only its owned channel",
           "[previewdecoder][previewdeadline]" )
{
    RestoreDecoder restore;
    RestoreEnvironment python( "CILOGG_SCRIPT_PYTHON" ), mode( "CILOGG_PREVIEW_FIXTURE_MODE" ),
        pid( "CILOGG_PREVIEW_FIXTURE_PID" );
    qputenv( "CILOGG_SCRIPT_PYTHON", QByteArray( PREVIEW_DECODER_FIXTURE_PATH ) );
    qputenv( "CILOGG_PREVIEW_FIXTURE_MODE", "valid" );
    qputenv( "CILOGG_PREVIEW_FIXTURE_PID",
             QByteArray::number( QCoreApplication::applicationPid() ) );
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "enabled", true },
          { "tshark", QCoreApplication::applicationFilePath() },
          { "text2pcap", QCoreApplication::applicationFilePath() } } ) );
    auto& client = TsharkPreviewClient::instance();
    client.reset();
    QObject channel;
    auto request = directRequest( "0102", "hex" );
    request.sessionOwner = &channel;
    REQUIRE( PreviewService::evaluateAndWait( request ).value( "available" ).toBool() );
    const auto processes = client.findChildren<QProcess*>();
    REQUIRE( processes.size() == 1 );
    QPointer<QProcess> worker = processes.front();
    const auto originalPid = worker->processId();
    REQUIRE( originalPid > 0 );
    QObject unrelatedChannel;
    auto unrelatedRequest = request;
    unrelatedRequest.sessionOwner = &unrelatedChannel;
    REQUIRE( PreviewService::evaluateAndWait( unrelatedRequest ).value( "available" ).toBool() );
    QPointer<QProcess> unrelatedWorker;
    for ( auto* process : client.findChildren<QProcess*>() )
        if ( process != worker )
            unrelatedWorker = process;
    REQUIRE( unrelatedWorker );
    const auto unrelatedPid = unrelatedWorker->processId();
    request.timeoutMs = 1000;
    QVariantMap result;
    int callbacks = 0;
    QEventLoop loop;
    auto* token = PreviewService::evaluate( &loop, request, [ & ]( QVariantMap value ) {
        ++callbacks;
        result = std::move( value );
        loop.quit();
    } );
    REQUIRE( token );
    bool forcedDelivery = false;
    // Selection's handler submits to the warm native process before this connection runs.
    // Block the application loop beyond the original budget, then synchronously dispatch
    // readable output. This orders success ahead of both overdue QTimer events, without
    // changing timers or injecting a production completion hook.
    QObject::connect( token, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), &loop,
                      [ & ]( int, QProcess::ExitStatus ) {
                          if ( !result.isEmpty() || !worker )
                              return;
                          QThread::msleep( static_cast<unsigned long>( request.timeoutMs + 50 ) );
                          forcedDelivery = worker->waitForReadyRead( 1000 );
                      } );
    QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
    if ( result.isEmpty() )
        loop.exec();
    REQUIRE( forcedDelivery );
    REQUIRE_FALSE( result.isEmpty() );
    CHECK( callbacks == 1 );
    CHECK( result.value( "reason" ) == "timeout" );
    CHECK( result.value( "state" ) == "failed" );
    CHECK( result.value( "backend" ) == "tshark_lua" );
    CHECK_FALSE( result.value( "available" ).toBool() );
    CHECK_FALSE( result.value( "complete" ).toBool() );
    CHECK_FALSE( result.value( "limited" ).toBool() );
    CHECK( result.value( "fields" ).toList().isEmpty() );
    CHECK_FALSE( result.contains( "fallbackReason" ) );
    CHECK_FALSE( result.contains( "id" ) );
    CHECK( result.value( "enginePid" ).toLongLong() == QCoreApplication::applicationPid() );
    CHECK( result.contains( "scriptSha256" ) );
    CHECK( result.value( "elapsedMs" ).toLongLong() >= request.timeoutMs );
    CHECK_FALSE( result.value( "requestId" ).toString().isEmpty() );
    CHECK( result.value( "offsetSpace" ) == "engine_data_source_unresolved" );
    CHECK( result.value( "messageCompleteness" ) == "not_reported" );
    CHECK( result.value( "messageIntegrity" ) == "not_reported" );
    request.timeoutMs = 10000;
    const auto recovered = PreviewService::evaluateAndWait( request );
    REQUIRE( recovered.value( "available" ).toBool() );
    CHECK( callbacks == 1 );
    CHECK( ( !worker || worker->state() == QProcess::NotRunning ) );
    bool replacement = false;
    for ( auto* process : client.findChildren<QProcess*>() )
        replacement
            = replacement
              || ( process->state() == QProcess::Running && process->processId() != originalPid );
    CHECK( replacement );
    REQUIRE( unrelatedWorker );
    CHECK( unrelatedWorker->state() == QProcess::Running );
    CHECK( unrelatedWorker->processId() == unrelatedPid );
    REQUIRE( PreviewService::evaluateAndWait( unrelatedRequest ).value( "available" ).toBool() );
    CHECK( unrelatedWorker->processId() == unrelatedPid );
    client.reset();
}
