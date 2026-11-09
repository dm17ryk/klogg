// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../streamsession_testaccess.h"
#include "actionsmanager.h"
#include <catch2/catch.hpp>
#ifdef KLOGG_SHARED_SERIAL_WRITER
#include "../serial_owner_fixture.h"
using namespace cilogg_test;
TEST_CASE( "Serial owner publishes readiness after actual open and preserves the first banner",
           "[serial][D01c-owner]" )
{
    OwnerFixture f;
    f.port->banner = "BANNER\n";
    QSignalSpy opened( f.session.get(), &StreamSession::connectionOpened );
    QSignalSpy observed( f.session.get(), &StreamSession::dataObserved );
    f.session->start();
    REQUIRE_FALSE( f.session->isConnectionOpen() );
    REQUIRE( opened.isEmpty() );
    REQUIRE( f.registry->isSerialPortActive( "TEST_OWNER" ) );
    REQUIRE( f.ready() );
    REQUIRE( opened.count() == 1 );
    REQUIRE( QTest::qWaitFor( [ & ] { return !observed.isEmpty(); }, 5000 ) );
    REQUIRE( observed.front().front().toByteArray() == "BANNER\n" );
    f.session->closeConnection();
    REQUIRE( f.stopped() );
    REQUIRE_FALSE( f.registry->isSerialPortActive( "TEST_OWNER" ) );
}
TEST_CASE( "Failed or cancelled opening never publishes late readiness", "[serial][D01c-owner]" )
{
    OwnerFixture f;
    QSignalSpy opened( f.session.get(), &StreamSession::connectionOpened );
    QSignalSpy closed( f.session.get(), &StreamSession::connectionClosed );
    SECTION( "failed open" )
    {
        f.port->opensSuccessfully = false;
        f.session->start();
    }
    SECTION( "closed while opening" )
    {
        f.session->start();
        f.session->closeConnection();
    }
    REQUIRE( f.stopped() );
    REQUIRE( opened.isEmpty() );
    REQUIRE( closed.count() == 1 );
    REQUIRE_FALSE( f.registry->isSerialPortActive( "TEST_OWNER" ) );
    f.session->closeConnection();
    REQUIRE( closed.count() == 1 );
}
TEST_CASE( "Public close retains the endpoint lease until the worker stops",
           "[serial][D01c-owner]" )
{
    OwnerFixture f;
    f.session->start();
    REQUIRE( f.ready() );
    bool held = false;
    QObject::connect( f.session.get(), &StreamSession::connectionClosed,
                      [ & ] { held = f.registry->isSerialPortActive( "TEST_OWNER" ); } );
    f.session->closeConnection();
    REQUIRE( held );
    REQUIRE( f.stopped() );
    REQUIRE_FALSE( f.registry->isSerialPortActive( "TEST_OWNER" ) );
}
TEST_CASE( "Checked serial submission preserves explicit Action eligibility and automatic policy",
           "[serial][D01c-owner]" )
{
    OwnerFixture f;
    f.session->start();
    REQUIRE( f.ready() );
    f.session->setActionsEnabled( false );
    auto explicitWrite = f.session->submitActionWrite( "PING", false, f.stamp() );
    REQUIRE( explicitWrite.value );
    REQUIRE_FALSE( f.session->submitActionWrite( "AUTO", false, f.stamp( true ) ).value );
    REQUIRE( QTest::qWaitFor( [ & ] { return f.sent() == "PING"; }, 5000 ) );
    const auto id = explicitWrite.value->receipt.requestId();
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto s = StreamSessionTestAccess::write( *f.session, id );
            return s && s->receipt.writtenBytes() == 4;
        },
        5000 ) );
}
TEST_CASE( "Serial owner retries only the unsent suffix under backpressure",
           "[serial][D01c-owner]" )
{
    OwnerFixture f;
    f.port->returns = { 2, 0, 3 };
    f.session->start();
    REQUIRE( f.ready() );
    const auto a = f.session->submitActionWrite( "ABCDE", false, f.stamp() );
    REQUIRE( a.value );
    const auto b = f.session->submitActionWrite( "Z", false, f.stamp() );
    REQUIRE( b.value );
    REQUIRE( QTest::qWaitFor( [ & ] { return f.sent() == "ABCDEZ"; }, 5000 ) );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto result
                = StreamSessionTestAccess::write( *f.session, b.value->receipt.requestId() );
            return result && result->receipt.writtenBytes() == 1;
        },
        5000 ) );
    REQUIRE( f.port->calls.load() == 4 );
    f.session->closeConnection();
    REQUIRE( f.stopped() );
    QFile capture( f.session->filePath() );
    REQUIRE( capture.open( QIODevice::ReadOnly ) );
    const auto bytes = capture.readAll();
    REQUIRE( bytes.contains( "[TX] - ABCDE\n" ) );
    REQUIRE( bytes.contains( "[TX] - Z\n" ) );
}
TEST_CASE( "An older serial epoch cannot publish RX TX readiness or completion",
           "[serial][D01c-owner]" )
{
    OwnerFixture f;
    f.session->start();
    REQUIRE( f.ready() );
    const auto old = StreamSessionTestAccess::epoch( *f.session );
    REQUIRE( f.session->pauseConnection() );
    REQUIRE( f.stopped() );
    REQUIRE( f.session->resumeConnection() );
    REQUIRE( f.ready() );
    REQUIRE( StreamSessionTestAccess::epoch( *f.session ) > old );
    QSignalSpy opened( f.session.get(), &StreamSession::connectionOpened ),
        rx( f.session.get(), &StreamSession::dataObserved ),
        tx( f.session.get(), &StreamSession::dataTransmitted );
    StreamSessionTestAccess::ready( *f.session, old );
    StreamSessionTestAccess::data( *f.session, old, "old-rx" );
    StreamSessionTestAccess::data( *f.session, old, "old-tx", true );
    StreamSessionTestAccess::snapshot(
        *f.session, BackendWriteSnapshotPtr::create( BackendWriteSnapshot{
                        { { StreamSessionTestAccess::endpoint( *f.session ), old }, 1 },
                        999,
                        3,
                        3,
                        UnsubmittedReason::None,
                        true,
                        true } ) );
    REQUIRE( opened.isEmpty() );
    REQUIRE( rx.isEmpty() );
    REQUIRE( tx.isEmpty() );
    REQUIRE( f.session->isConnectionOpen() );
}
TEST_CASE( "Disabling Actions withdraws queued writes while retaining accepted I/O",
           "[serial][D01c-owner]" )
{
    OwnerFixture f;
    f.port->autoDrain = false;
    f.session->start();
    REQUIRE( f.ready() );
    const auto first = f.session->submitActionWrite( "FIRST", false, f.stamp() );
    REQUIRE( first.value );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto w = StreamSessionTestAccess::write( *f.session, first.value->receipt.requestId() );
            return w && w->receipt.acceptedBytes() == 5;
        },
        5000 ) );
    const auto queued = f.session->submitActionWrite( "STALE", false, f.stamp() );
    REQUIRE( queued.value );
    f.session->setActionsEnabled( false );
    f.session->setActionsEnabled( true );
    auto cancelled
        = StreamSessionTestAccess::write( *f.session, queued.value->receipt.requestId() );
    REQUIRE( cancelled );
    REQUIRE( cancelled->receipt.stage() == WriteStage::Cancelled );
    StreamSessionTestAccess::postWorker( *f.session, []( SerialCaptureWorker& w ) {
        SerialCaptureWorkerTestAccess::drain( w, 5 );
    } );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto w = StreamSessionTestAccess::write( *f.session, first.value->receipt.requestId() );
            return w && !w->awaitingDrain;
        },
        5000 ) );
    REQUIRE( f.sent() == "FIRST" );
    REQUIRE( f.port->calls.load() == 1 );
}
TEST_CASE( "Deadline wakeups retain uncertainty until the backend retires", "[serial][D01c-owner]" )
{
    qint64 now = 0;
    OwnerFixture f;
    StreamSessionTestAccess::clock( *f.session, [ & ] { return now; } );
    f.port->autoDrain = false;
    f.session->start();
    REQUIRE( f.ready() );
    const auto result = f.session->submitActionWrite( "WAIT", false, f.stamp() );
    REQUIRE( result.value );
    const auto id = result.value->receipt.requestId();
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto w = StreamSessionTestAccess::write( *f.session, id );
            return w && w->awaitingDrain;
        },
        5000 ) );
    now = 1000000001;
    StreamSessionTestAccess::tick( *f.session );
    const auto expired = StreamSessionTestAccess::write( *f.session, id );
    REQUIRE( expired );
    REQUIRE( expired->receipt.errorCode() == "deadline_exceeded" );
    REQUIRE( expired->awaitingDrain );
    f.session->closeConnection();
    REQUIRE( f.stopped() );
    const auto retired = StreamSessionTestAccess::write( *f.session, id );
    REQUIRE( retired );
    REQUIRE( retired->ioUnconfirmed );
    REQUIRE_FALSE( retired->awaitingDrain );
    REQUIRE( f.sent() == "WAIT" );
}
TEST_CASE( "Capture rotation refuses an unfinished acknowledged TX record", "[serial][D01c-owner]" )
{
    OwnerFixture f;
    f.port->autoDrain = false;
    f.session->start();
    REQUIRE( f.ready() );
    const auto result = f.session->submitActionWrite( "PART", false, f.stamp() );
    REQUIRE( result.value );
    REQUIRE( QTest::qWaitFor( [ & ] { return f.port->calls.load() == 1; }, 5000 ) );
    QString error;
    const auto before = std::chrono::steady_clock::now();
    REQUIRE_FALSE( f.session->startNewCaptureFile( f.directory.filePath( "next.log" ), &error ) );
    REQUIRE_FALSE( error.isEmpty() );
    REQUIRE( std::chrono::steady_clock::now() - before < std::chrono::seconds( 1 ) );
    StreamSessionTestAccess::postWorker( *f.session, []( SerialCaptureWorker& w ) {
        SerialCaptureWorkerTestAccess::drain( w, 4 );
    } );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto w
                = StreamSessionTestAccess::write( *f.session, result.value->receipt.requestId() );
            return w && w->receipt.writtenBytes() == 4;
        },
        5000 ) );
    REQUIRE( f.session->startNewCaptureFile( f.directory.filePath( "next.log" ), &error ) );
}
TEST_CASE( "Sensitive TX is redacted once and never becomes received data", "[serial][D01c-owner]" )
{
    OwnerFixture f;
    f.port->returns = { 2, 3 };
    f.session->start();
    REQUIRE( f.ready() );
    QSignalSpy tx( f.session.get(), &StreamSession::dataTransmitted ),
        rx( f.session.get(), &StreamSession::dataObserved );
    const auto result = f.session->submitActionWrite( "ABCDE", true, f.stamp() );
    REQUIRE( result.value );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto w
                = StreamSessionTestAccess::write( *f.session, result.value->receipt.requestId() );
            return w && w->receipt.writtenBytes() == 5;
        },
        5000 ) );
    REQUIRE( tx.count() == 1 );
    REQUIRE( tx.front().front().toByteArray() == "<redacted>" );
    REQUIRE( rx.isEmpty() );
    f.session->closeConnection();
    REQUIRE( f.stopped() );
    QFile capture( f.session->filePath() );
    REQUIRE( capture.open( QIODevice::ReadOnly ) );
    const auto bytes = capture.readAll();
    REQUIRE( bytes.count( "<redacted>" ) == 1 );
    REQUIRE_FALSE( bytes.contains( "ABCDE" ) );
}
TEST_CASE( "A transmit observer may delete the serial session during progress",
           "[serial][D01c-owner]" )
{
    OwnerFixture f;
    f.session->start();
    REQUIRE( f.ready() );
    QObject::connect( f.session.get(), &StreamSession::dataTransmitted,
                      [ & ] { f.session.reset(); } );
    REQUIRE( f.session->submitActionWrite( "DELETE", false, f.stamp() ).value );
    REQUIRE( QTest::qWaitFor( [ & ] { return !f.session; }, 5000 ) );
    REQUIRE( f.port->closes.load() == 1 );
}
TEST_CASE(
    "Global automatic-response changes invalidate queued origins without changing explicit Actions",
    "[serial][D01c-owner]" )
{
    auto& manager = ActionsManager::instance();
    struct Restore {
        bool old;
        ~Restore()
        {
            ActionsManager::instance().setAutoResponsesEnabled( old );
        }
    } restore{ manager.autoResponsesEnabled() };
    manager.setAutoResponsesEnabled( true );
    OwnerFixture f;
    f.port->autoDrain = false;
    f.session->start();
    REQUIRE( f.ready() );
    const auto first = f.session->submitActionWrite( "FIRST", false, f.stamp() );
    REQUIRE( first.value );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto w = StreamSessionTestAccess::write( *f.session, first.value->receipt.requestId() );
            return w && w->awaitingDrain;
        },
        5000 ) );
    const auto automatic = f.session->submitActionWrite( "AUTO", false, f.stamp( true ) );
    REQUIRE( automatic.value );
    const auto manual = f.session->submitActionWrite( "PRESET", false, f.stamp() );
    REQUIRE( manual.value );
    manager.setAutoResponsesEnabled( false );
    manager.setAutoResponsesEnabled( true );
    const auto cancelled
        = StreamSessionTestAccess::write( *f.session, automatic.value->receipt.requestId() );
    REQUIRE( cancelled );
    REQUIRE( cancelled->receipt.stage() == WriteStage::Cancelled );
    const auto queued
        = StreamSessionTestAccess::write( *f.session, manual.value->receipt.requestId() );
    REQUIRE( queued );
    REQUIRE( queued->receipt.stage() == WriteStage::Queued );
    StreamSessionTestAccess::postWorker( *f.session, []( SerialCaptureWorker& w ) {
        SerialCaptureWorkerTestAccess::drain( w, 5 );
    } );
    REQUIRE( QTest::qWaitFor( [ & ] { return f.sent() == "FIRSTPRESET"; }, 5000 ) );
}
TEST_CASE( "Logging toggles retain only the recorded acknowledged TX prefixes",
           "[serial][D01c-owner]" )
{
    OwnerFixture f;
    f.port->autoDrain = false;
    f.port->returns = { 2, 3 };
    f.session->start();
    REQUIRE( f.ready() );
    const auto first = f.session->submitActionWrite( "ABCDE", false, f.stamp() );
    REQUIRE( first.value );
    REQUIRE( QTest::qWaitFor( [ & ] { return f.port->calls.load() == 1; }, 5000 ) );
    StreamSessionTestAccess::postWorker( *f.session, []( SerialCaptureWorker& w ) {
        SerialCaptureWorkerTestAccess::drain( w, 2 );
    } );
    REQUIRE( QTest::qWaitFor( [ & ] { return f.port->calls.load() == 2; }, 5000 ) );
    f.session->setLoggingEnabled( false );
    StreamSessionTestAccess::postWorker( *f.session, []( SerialCaptureWorker& w ) {
        SerialCaptureWorkerTestAccess::drain( w, 3 );
    } );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto w = StreamSessionTestAccess::write( *f.session, first.value->receipt.requestId() );
            return w && w->receipt.writtenBytes() == 5;
        },
        5000 ) );
    f.session->setLoggingEnabled( true );
    const auto next = f.session->submitActionWrite( "NEXT", false, f.stamp() );
    REQUIRE( next.value );
    REQUIRE( QTest::qWaitFor( [ & ] { return f.port->calls.load() == 3; }, 5000 ) );
    StreamSessionTestAccess::postWorker( *f.session, []( SerialCaptureWorker& w ) {
        SerialCaptureWorkerTestAccess::drain( w, 4 );
    } );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto w = StreamSessionTestAccess::write( *f.session, next.value->receipt.requestId() );
            return w && w->receipt.writtenBytes() == 4;
        },
        5000 ) );
    f.session->closeConnection();
    REQUIRE( f.stopped() );
    QFile capture( f.session->filePath() );
    REQUIRE( capture.open( QIODevice::ReadOnly ) );
    const auto bytes = capture.readAll();
    REQUIRE( bytes.contains( "[TX] - AB\n" ) );
    REQUIRE_FALSE( bytes.contains( "CDE" ) );
    REQUIRE( bytes.contains( "[TX] - NEXT\n" ) );
}
TEST_CASE( "A policy disable reaches an issued offer before the backend call",
           "[serial][D01c-owner]" )
{
    OwnerFixture f;
    f.session->start();
    REQUIRE( f.ready() );
    std::promise<void> release;
    const auto gate = release.get_future().share();
    const auto entered = std::make_shared<std::atomic<bool>>( false );
    StreamSessionTestAccess::postWorker( *f.session, [ gate, entered ]( SerialCaptureWorker& ) {
        entered->store( true );
        gate.wait();
    } );
    REQUIRE( QTest::qWaitFor( [ & ] { return entered->load(); }, 5000 ) );
    const auto result = f.session->submitActionWrite( "NEVER", false, f.stamp() );
    REQUIRE( result.value );
    const auto start = std::chrono::steady_clock::now();
    f.session->setActionsEnabled( false );
    auto state = StreamSessionTestAccess::write( *f.session, result.value->receipt.requestId() );
    REQUIRE( state );
    REQUIRE( state->receipt.stage() == WriteStage::Cancelled );
    REQUIRE( std::chrono::steady_clock::now() - start < std::chrono::seconds( 1 ) );
    release.set_value();
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto w
                = StreamSessionTestAccess::write( *f.session, result.value->receipt.requestId() );
            return w && !w->awaitingAcceptance;
        },
        5000 ) );
    state = StreamSessionTestAccess::write( *f.session, result.value->receipt.requestId() );
    REQUIRE( state );
    REQUIRE_FALSE( state->ioUnconfirmed );
    REQUIRE( state->receipt.acceptedBytes() == 0 );
    REQUIRE( f.port->calls.load() == 0 );
}
TEST_CASE( "Serial requests are bounded before the Qt worker queue", "[serial][D01c-owner]" )
{
    OwnerFixture f;
    StreamSessionTestAccess::clock( *f.session, [] { return qint64( 0 ); } );
    f.port->autoDrain = false;
    f.session->start();
    REQUIRE( f.ready() );
    REQUIRE_FALSE(
        f.session->submitActionWrite( QByteArray( 1024 * 1024 + 1, 'x' ), false, f.stamp() )
            .value );
    const auto first = f.session->submitActionWrite( "X", false, f.stamp() );
    REQUIRE( first.value );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto w = StreamSessionTestAccess::write( *f.session, first.value->receipt.requestId() );
            return w && w->awaitingDrain;
        },
        5000 ) );
    for ( int n = 1; n < 256; ++n )
        REQUIRE( f.session->submitActionWrite( "X", false, f.stamp() ).value );
    const auto full = f.session->submitActionWrite( "X", false, f.stamp() );
    REQUIRE_FALSE( full.value );
    REQUIRE( full.error.code == "queue_full" );
    REQUIRE( f.port->calls.load() == 1 );
}
TEST_CASE( "A lease remains held until thread-local retirement actually finishes",
           "[serial][D01c-owner][D01c-owner-retirement]" )
{
    OwnerFixture f;
    std::promise<void> release;
    f.port->retirementGate = release.get_future().share();
    f.session->start();
    REQUIRE( f.ready() );
    f.session->closeConnection();
    REQUIRE( QTest::qWaitFor( [ & ] { return f.port->retirementEntered.load(); }, 5000 ) );
    REQUIRE( f.registry->isSerialPortActive( "TEST_OWNER" ) );
    release.set_value();
    REQUIRE( f.stopped() );
    REQUIRE_FALSE( f.registry->isSerialPortActive( "TEST_OWNER" ) );
}
TEST_CASE( "A direct TX observer can submit the next serial write",
           "[serial][D01c-owner][D01c-owner-reentrant-tx]" )
{
    OwnerFixture f;
    f.session->start();
    REQUIRE( f.ready() );
    QSignalSpy errors( f.session.get(), &StreamSession::errorOccurred );
    QByteArray observed;
    QObject::connect(
        f.session.get(), &StreamSession::dataTransmitted, f.session.get(),
        [ & ]( const QByteArray& bytes ) {
            observed += bytes;
            if ( bytes == "FIRST" )
                f.session->sendBytes( "SECOND" );
        },
        Qt::DirectConnection );
    f.session->sendBytes( "FIRST" );
    REQUIRE(
        QTest::qWaitFor( [ & ] { return observed == "FIRSTSECOND" || !errors.isEmpty(); }, 5000 ) );
    REQUIRE( errors.isEmpty() );
    REQUIRE( observed == "FIRSTSECOND" );
    REQUIRE( f.sent() == "FIRSTSECOND" );
    REQUIRE( f.session->isConnectionOpen() );
}
TEST_CASE( "Expiry after zero acceptance permits a fresh logical write",
           "[serial][D01c-owner][D01c-owner-expiry]" )
{
    qint64 now = 0;
    OwnerFixture f;
    StreamSessionTestAccess::clock( *f.session, [ & ] { return now; } );
    f.port->returns = { 0, 2 };
    f.session->start();
    REQUIRE( f.ready() );
    const auto old = f.session->submitActionWrite( "OLD", false, f.stamp() );
    REQUIRE( old.value );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto w = StreamSessionTestAccess::write( *f.session, old.value->receipt.requestId() );
            return f.port->calls.load() == 1 && w && !w->awaitingAcceptance;
        },
        5000 ) );
    now = 1000000001;
    StreamSessionTestAccess::tick( *f.session );
    const auto next = f.session->submitActionWrite( "OK", false, f.stamp() );
    REQUIRE( next.value );
    REQUIRE( QTest::qWaitFor( [ & ] { return f.sent() == "OK"; }, 5000 ) );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto result
                = StreamSessionTestAccess::write( *f.session, next.value->receipt.requestId() );
            return result && result->receipt.writtenBytes() == 2;
        },
        5000 ) );
    REQUIRE( f.port->calls.load() == 2 );
}
TEST_CASE( "Logging disabled during Opening stays disabled after readiness",
           "[serial][D01c-owner][D01c-review]" )
{
    OwnerFixture f;
    f.session->start();
    REQUIRE_FALSE( f.session->isConnectionOpen() );
    f.session->setLoggingEnabled( false );
    // Let the worker process the control while this thread withholds backendReady delivery.
    auto processed = std::make_shared<std::promise<void>>();
    auto completion = processed->get_future();
    StreamSessionTestAccess::postWorker(
        *f.session, [ processed ]( SerialCaptureWorker& ) { processed->set_value(); } );
    REQUIRE( completion.wait_for( std::chrono::seconds( 5 ) ) == std::future_status::ready );
    REQUIRE( f.ready() );
    QSignalSpy received( f.session.get(), &StreamSession::dataObserved );
    StreamSessionTestAccess::postWorker( *f.session, []( SerialCaptureWorker& worker ) {
        auto* port = dynamic_cast<FakeSerialPort*>( worker.findChild<QSerialPort*>() );
        if ( port )
            port->receive( "INCOMING\n" );
    } );
    REQUIRE( QTest::qWaitFor( [ & ] { return !received.isEmpty(); }, 5000 ) );
    f.session->closeConnection();
    REQUIRE( f.stopped() );
    QFile capture( f.session->filePath() );
    REQUIRE( capture.open( QIODevice::ReadOnly ) );
    REQUIRE( capture.readAll().isEmpty() );
}
TEST_CASE( "Unexpected write expiry publishes one redacted public failure",
           "[serial][D01c-owner][D01c-review]" )
{
    qint64 now = 0;
    OwnerFixture f;
    StreamSessionTestAccess::clock( *f.session, [ & ] { return now; } );
    f.port->returns = { 0 };
    SECTION( "No bytes accepted" ) {}
    SECTION( "Accepted bytes await drain" )
    {
        f.port->returns = { 12 };
        f.port->autoDrain = false;
    }
    f.session->start();
    REQUIRE( f.ready() );
    QSignalSpy errors( f.session.get(), &StreamSession::errorOccurred );
    const auto request = f.session->submitActionWrite( "PRIVATE_BODY", true, f.stamp() );
    REQUIRE( request.value );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto w
                = StreamSessionTestAccess::write( *f.session, request.value->receipt.requestId() );
            return w && !w->awaitingAcceptance && f.port->calls.load() == 1;
        },
        5000 ) );
    now = 1000000001;
    StreamSessionTestAccess::tick( *f.session );
    REQUIRE( errors.size() == 1 );
    REQUIRE_FALSE( errors.front().front().toString().contains( "PRIVATE_BODY" ) );
    StreamSessionTestAccess::tick( *f.session );
    REQUIRE( errors.size() == 1 );
    REQUIRE( f.session->isConnectionOpen() );
}
TEST_CASE( "Queued expiry reports failure without entering a second backend call",
           "[serial][D01c-owner][D01c-review]" )
{
    qint64 now = 0;
    OwnerFixture f;
    StreamSessionTestAccess::clock( *f.session, [ & ] { return now; } );
    f.port->autoDrain = false;
    f.session->start();
    REQUIRE( f.ready() );
    QSignalSpy errors( f.session.get(), &StreamSession::errorOccurred );
    const auto first = f.session->submitActionWrite( "FIRST", false, f.stamp() );
    REQUIRE( first.value );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto w
                = StreamSessionTestAccess::write( *f.session, first.value->receipt.requestId() );
            return w && w->awaitingDrain;
        },
        5000 ) );
    const auto queued = f.session->submitActionWrite( "PRIVATE_BODY", true, f.stamp() );
    REQUIRE( queued.value );
    now = 1000000001;
    StreamSessionTestAccess::tick( *f.session );
    REQUIRE( errors.size() == 2 );
    for ( const auto& error : errors )
        REQUIRE_FALSE( error.front().toString().contains( "PRIVATE_BODY" ) );
    REQUIRE( f.port->calls.load() == 1 );
    StreamSessionTestAccess::tick( *f.session );
    REQUIRE( errors.size() == 2 );
}
TEST_CASE( "A TX observer restart fences the old fault snapshot",
           "[serial][D01c-owner][D01c-review]" )
{
    OwnerFixture f;
    f.port->synchronousFailure = true;
    f.session->start();
    REQUIRE( f.ready() );
    const auto old = StreamSessionTestAccess::epoch( *f.session );
    bool restarted = false;
    QObject::connect(
        f.session.get(), &StreamSession::dataTransmitted, f.session.get(),
        [ & ]( const QByteArray& ) {
            if ( restarted )
                return;
            restarted = true;
            f.port->synchronousFailure = false;
            f.session->stop();
            f.session->start();
        },
        Qt::DirectConnection );
    f.session->sendBytes( "PREFIX" );
    REQUIRE( QTest::qWaitFor( [ & ] { return restarted; }, 5000 ) );
    REQUIRE( f.ready() );
    REQUIRE( StreamSessionTestAccess::epoch( *f.session ) > old );
}
TEST_CASE( "A write-failure observer may delete the session safely",
           "[serial][D01c-owner][D01c-review]" )
{
    qint64 now = 0;
    OwnerFixture f;
    StreamSessionTestAccess::clock( *f.session, [ & ] { return now; } );
    f.port->returns = { 0 };
    f.session->start();
    REQUIRE( f.ready() );
    const auto request = f.session->submitActionWrite( "PRIVATE_BODY", true, f.stamp() );
    REQUIRE( request.value );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto w
                = StreamSessionTestAccess::write( *f.session, request.value->receipt.requestId() );
            return w && !w->awaitingAcceptance && f.port->calls.load() == 1;
        },
        5000 ) );
    auto* session = f.session.release();
    QPointer<StreamSession> guard( session );
    QObject::connect( session, &StreamSession::errorOccurred, session,
                      [ session ] { delete session; } );
    now = 1000000001;
    StreamSessionTestAccess::tick( *session );
    REQUIRE( guard.isNull() );
}
#endif
