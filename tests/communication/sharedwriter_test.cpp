// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sharedwriter.h"
#include <catch2/catch.hpp>
#include <limits>

using namespace cilogg::communication;

namespace {
StreamLimits writerLimits()
{
    StreamLimitsDefinition definition;
    definition.maximumWriteBytes = 32;
    definition.maximumChunkBytes = 16;
    definition.maximumPendingTxBytes = 64;
    definition.maximumReceipts = 3;
    auto result = StreamLimits::fromDefinition( definition );
    REQUIRE( result.value );
    return *result.value;
}

EndpointCapabilities writerCapabilities()
{
    EndpointCapabilitiesDefinition definition;
    definition.canWrite = true;
    auto result = EndpointCapabilities::fromDefinition( definition );
    REQUIRE( result.value );
    return *result.value;
}
std::unique_ptr<SharedWriter> writer( qint64 timeout = 100 )
{
    auto result = SharedWriter::create( { "ep", 7 }, writerLimits(), writerCapabilities(), timeout );
    REQUIRE(result.value);
    return std::move(*result.value);
}
WriteRequest request( const QString& id, const QByteArray& bytes = "abc", bool sensitive = false,
                      quint64 generation = 7, std::optional<qint64> deadline = {} )
{
    auto result = WriteRequest::fromDefinition({id, bytes, sensitive, generation, deadline}, writerLimits());
    REQUIRE(result.value);
    return *result.value;
}
const WriteEpoch epoch { "ep", 7 };
} // namespace

TEST_CASE( "Shared writer owns borrowed endpoint identity", "[communication][D01b-factory]" )
{
    QChar borrowed[] = { QChar( 'e' ), QChar( 'p' ) };
    auto result = SharedWriter::create(
        { QString::fromRawData( borrowed, 2 ), 7 }, writerLimits(), writerCapabilities(), 100 );
    REQUIRE( result.value );
    REQUIRE( *result.value );
    borrowed[ 0 ] = QChar( 'x' );
    const auto state = ( *result.value )->state();
    CHECK( state.epoch.endpointId == "ep" );
    CHECK( state.epoch.generation == 7 );
    CHECK( state.mode == WriteMode::Automatic );
    CHECK_FALSE( state.pendingMode );
    CHECK( state.residentBytes == 0 );
    CHECK( state.pendingRequests == 0 );
    CHECK_FALSE( state.resetRequired );
}

TEST_CASE("Writer bounds admission and preserves idempotent request identity", "[communication][D01b-admission]")
{
    auto w = writer();
    const auto a = request("a", QByteArray(32, 'a'));
    REQUIRE(w->submit(epoch, a, WriteProducer::Automation, 10).value);
    auto retry = w->submit(epoch, a, WriteProducer::Automation, 20);
    REQUIRE(retry.value);
    CHECK(retry.value->receipt.stage() == WriteStage::Queued);
    CHECK(w->state().pendingRequests == 1);
    CHECK(w->state().nextWakeNs == 110);
    CHECK(w->submit(epoch, request("a", "different"), WriteProducer::Automation, 20).error.code == "request_conflict");
    CHECK(w->submit(epoch, request("a", a.bytes(), true), WriteProducer::Automation, 20).error.code == "request_conflict");
    CHECK(w->submit(epoch, request("a", a.bytes(), false, 7, 110), WriteProducer::Automation, 20).error.code == "request_conflict");
    CHECK(w->submit(epoch, a, WriteProducer::Manual, 20).error.code == "request_conflict");
    REQUIRE(w->submit(epoch, request("b", QByteArray(32, 'b')), WriteProducer::Automation, 20).value);
    CHECK(w->state().residentBytes == 64);
    CHECK(w->submit(epoch, request("c"), WriteProducer::Automation, 20).error.code == "queue_full");
    CHECK(w->state().pendingRequests == 2);
    auto countBound = writer();
    for (const auto* id : {"a", "b", "c"}) REQUIRE(countBound->submit(epoch, request(id), WriteProducer::Automation, 0).value);
    CHECK(countBound->submit(epoch, request("d"), WriteProducer::Automation, 0).error.code == "queue_full");
    CHECK(countBound->state().residentBytes == 9);
}

TEST_CASE("Writer rejects stale contexts and unsupported byte-stream submissions", "[communication][D01b-admission]")
{
    auto w = writer();
    CHECK(w->submit({"other",7}, request("a"), WriteProducer::Automation, 90).error.code == "stale_generation");
    CHECK(w->submit({"ep",6}, request("a"), WriteProducer::Automation, 90).error.code == "stale_generation");
    CHECK(w->submit(epoch, request("a", "abc", false, 6), WriteProducer::Automation, 90).error.code == "stale_generation");
    CHECK(w->state().residentBytes == 0);
    REQUIRE(w->submit(epoch, request("a"), WriteProducer::Automation, 1).value);
    CHECK(w->submit(epoch, request("b"), WriteProducer::Manual, 1).error.code == "endpoint_busy");
    CHECK(w->submit(epoch, request("b"), WriteProducer::Automation, -1).error.code == "invalid_clock");
    CHECK(w->submit(epoch, request("b"), WriteProducer::Automation, 0).error.code == "invalid_clock");
    CHECK(w->submit(epoch, request("b", "abc", false, 7, 1), WriteProducer::Automation, 1).error.code == "deadline_exceeded");
    for(bool datagram : {false,true}) {
        EndpointCapabilitiesDefinition definition;
        definition.canWrite = datagram;
        definition.messageBoundaries = datagram;
        auto caps = EndpointCapabilities::fromDefinition(definition);
        REQUIRE(caps.value);
        auto unsupported = SharedWriter::create(epoch, writerLimits(), *caps.value, 100);
        REQUIRE(unsupported.value);
        CHECK((*unsupported.value)->submit(epoch, request("x"), WriteProducer::Automation, 0).error.code == "unsupported_capability");
    }
    auto broader = StreamLimits::fromDefinition(StreamLimitsDefinition{});
    REQUIRE(broader.value);
    auto oversized = WriteRequest::fromDefinition({"large", QByteArray(33,'x'), false,7,{}}, *broader.value);
    REQUIRE(oversized.value);
    CHECK(w->submit(epoch, *oversized.value, WriteProducer::Automation, 1).error.code == "resource_limit");
}

TEST_CASE( "Shared writer rejects invalid identity and timeout policy",
           "[communication][D01b-factory]" )
{
    const auto limits = writerLimits();
    const auto capabilities = writerCapabilities();
    const auto badSurrogate = QString( 1, QChar( 0xd800 ) );
    for ( const auto& endpoint :
          { QString{}, QString( 129, 'x' ), QString( "a\nb" ), QString( "a\rb" ),
            QString( 1, QChar::Null ), badSurrogate } ) {
        auto result = SharedWriter::create( { endpoint, 1 }, limits, capabilities, 100 );
        CHECK_FALSE( result.value );
        CHECK( result.error.code == "invalid_definition" );
        CHECK( result.error.fieldId == "endpointId" );
    }
    auto zeroGeneration = SharedWriter::create( { "ep", 0 }, limits, capabilities, 100 );
    CHECK_FALSE( zeroGeneration.value );
    CHECK( zeroGeneration.error.fieldId == "generation" );
    for ( const auto budget : { qint64( 0 ), qint64( -1 ) } ) {
        auto result = SharedWriter::create( { "ep", 1 }, limits, capabilities, budget );
        CHECK_FALSE( result.value );
        CHECK( result.error.code == "invalid_definition" );
        CHECK( result.error.fieldId == "defaultTimeoutNs" );
    }
}
