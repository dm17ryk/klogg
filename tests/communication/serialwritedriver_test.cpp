// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "serialwritedriver.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QThread>
#include <catch2/catch.hpp>
#include <atomic>
#include <deque>
#include <functional>
#include <vector>

using namespace cilogg::communication;
namespace {
class Device : public QIODevice {
public:
    Device()
    {
        open( QIODevice::ReadWrite | QIODevice::Unbuffered );
    }
    std::deque<qint64> returns;
    std::vector<QByteArray> calls;
    QByteArray accepted;
    qint64 early = 0;
    std::function<void()> duringWrite;
    void drain( qint64 n )
    {
        Q_EMIT bytesWritten( n );
    }
    bool isSequential() const override
    {
        return true;
    }

protected:
    qint64 readData( char*, qint64 ) override
    {
        return 0;
    }
    qint64 writeData( const char* data, qint64 size ) override
    {
        calls.emplace_back( data, static_cast<int>( size ) );
        const auto result = returns.empty() ? size : returns.front();
        if ( !returns.empty() )
            returns.pop_front();
        if ( result > 0 && result <= size )
            accepted.append( data, static_cast<int>( result ) );
        if ( early )
            drain( early );
        if ( duringWrite )
            duringWrite();
        return result;
    }
};
struct Rig {
    StreamLimits limits = *StreamLimits::fromDefinition( [] {
                               StreamLimitsDefinition d;
                               d.maximumChunkBytes = 16;
                               d.maximumWriteBytes = 512;
                               d.maximumPendingTxBytes = 1024;
                               d.maximumReceipts = 8;
                               return d;
                           }() )
                               .value;
    WriteEpoch epoch{ "serial-test", 9 };
    SerialWriteContext context{ epoch, std::chrono::steady_clock::now(),
                                std::make_shared<StreamWritePolicyPublication>() };
    Device device;
    std::unique_ptr<SharedWriter> writer;
    std::unique_ptr<SerialWriteDriver> driver;
    std::vector<BackendWriteSnapshotPtr> snapshots;
    size_t consumed = 0;
    quint64 ticket = 0, applied = 0;
    Rig()
    {
        context.policy->publish( { 9, 3, 4, true, false, false, false } );
        EndpointCapabilitiesDefinition caps;
        caps.canWrite = true;
        auto w = SharedWriter::create(
            epoch, limits, *EndpointCapabilities::fromDefinition( caps ).value, 1000000000000LL );
        REQUIRE( w.value );
        writer = std::move( *w.value );
        auto d = SerialWriteDriver::create( device, context );
        REQUIRE( d.value );
        driver = std::move( *d.value );
        QObject::connect( driver.get(), &SerialWriteDriver::snapshotReady,
                          [ this ]( BackendWriteSnapshotPtr s ) { snapshots.push_back( s ); } );
    }
    void submit( const QString& id, const QByteArray& bytes )
    {
        auto r = WriteRequest::fromDefinition( { id, bytes, false, 9, {} }, limits );
        REQUIRE( r.value );
        REQUIRE( writer->submit( epoch, *r.value, WriteProducer::Automation, 0 ).value );
    }
    SerialWriteEnvelopePtr envelope()
    {
        auto o = writer->take( epoch, 0 );
        REQUIRE( o.value );
        REQUIRE( o.value->offer );
        return SerialWriteEnvelopePtr::create(
            SerialWriteEnvelope{ *o.value->offer, { false, 3, 4 }, 1000000000000LL } );
    }
    void pump()
    {
        REQUIRE( driver->offer( envelope() ).ok );
    }
    BackendWriteSnapshotPtr apply()
    {
        REQUIRE( consumed < snapshots.size() );
        auto s = snapshots.at( consumed++ );
        if ( s->ticket.sequence != ticket ) {
            ticket = s->ticket.sequence;
            applied = 0;
            if ( s->acceptance )
                REQUIRE( writer->accepted( s->ticket, *s->acceptance, 0 ).ok );
        }
        if ( s->written > applied ) {
            REQUIRE(
                writer->written( s->ticket, static_cast<qint64>( s->written - applied ), 0 ).ok );
            applied = s->written;
        }
        REQUIRE( driver->acknowledgeProgress( epoch, s->deliverySequence ).ok );
        return s;
    }
};
} // namespace
TEST_CASE( "Serial driver preserves partial acceptance and independent drains",
           "[communication][D01c-driver]" )
{
    Rig r;
    r.device.returns = { 2, 0, 3, 1 };
    r.submit( "a", "ABCDE" );
    r.submit( "b", "Z" );
    r.pump();
    REQUIRE( r.apply()->acceptance == 2 );
    r.device.drain( 1 );
    REQUIRE( r.apply()->written == 1 );
    r.device.drain( 1 );
    REQUIRE( r.apply()->written == 2 );
    r.pump();
    REQUIRE( r.apply()->acceptance == 0 );
    REQUIRE( r.device.calls.size() == 2 );
    REQUIRE_FALSE( r.writer->take( r.epoch, 0 ).value->offer );
    REQUIRE( r.writer->ready( r.epoch, 0 ).ok );
    r.pump();
    r.apply();
    r.device.drain( 1 );
    r.apply();
    r.device.drain( 2 );
    r.apply();
    r.pump();
    r.apply();
    r.device.drain( 1 );
    r.apply();
    REQUIRE( r.device.accepted == "ABCDEZ" );
    REQUIRE( r.device.calls == std::vector<QByteArray>{ "ABCDE", "CDE", "CDE", "Z" } );
    REQUIRE( r.writer->getWrite( "a" ).value->receipt.writtenBytes() == 5 );
}
TEST_CASE( "Serial driver transports every byte in bounded offers", "[communication][D01c-driver]" )
{
    Rig r;
    QByteArray bytes;
    for ( int n = 0; n < 256; ++n )
        bytes.append( static_cast<char>( n ) );
    r.submit( "all", bytes );
    for ( int n = 0; n < 16; ++n ) {
        r.pump();
        r.apply();
        r.device.drain( 16 );
        r.apply();
    }
    REQUIRE( r.device.accepted == bytes );
    REQUIRE( r.device.calls.size() == 16 );
}
TEST_CASE( "Serial progress is coalesced and retirement bypasses pending GUI credit",
           "[communication][D01c-driver]" )
{
    Rig r;
    r.submit( "a", "ABCDE" );
    r.pump();
    r.device.drain( 1 );
    r.device.drain( 2 );
    REQUIRE( r.snapshots.size() == 1 );
    r.apply();
    REQUIRE( r.snapshots.size() == 2 );
    REQUIRE( r.snapshots.back()->written == 3 );
    REQUIRE( r.driver->beginClose( r.epoch ).ok );
    r.device.close();
    REQUIRE( r.driver->backendClosed( r.epoch ).ok );
    REQUIRE( r.snapshots.size() == 3 );
    REQUIRE( r.snapshots.back()->retired );
    REQUIRE( r.snapshots.back()->written == 3 );
    REQUIRE( r.driver->backendClosed( r.epoch ).ok );
    REQUIRE( r.snapshots.size() == 3 );
    r.device.drain( 2 );
    REQUIRE( r.snapshots.size() == 3 );
}
TEST_CASE( "Serial driver rejects policy-stale or expired offers before I/O",
           "[communication][D01c-driver]" )
{
    Rig r;
    r.submit( "a", "abc" );
    auto e = r.envelope();
    SECTION( "policy" )
    {
        r.context.policy->publish( { 9, 4, 5, true, true, true, true } );
    }
    SECTION( "deadline" )
    {
        e = SerialWriteEnvelopePtr::create( SerialWriteEnvelope{ e->offer, e->stamp, 0 } );
    }
    REQUIRE( r.driver->offer( e ).ok );
    REQUIRE( r.device.calls.empty() );
    REQUIRE( r.snapshots.size() == 1 );
    REQUIRE( r.snapshots.back()->unsubmitted != UnsubmittedReason::None );
    REQUIRE_FALSE( r.snapshots.back()->fault );
}
TEST_CASE( "Early synchronous progress and errors wait for acceptance",
           "[communication][D01c-driver]" )
{
    Rig r;
    r.submit( "a", "ABCDE" );
    r.device.returns = { 2 };
    r.device.early = 1;
    SECTION( "progress" )
    {
        r.pump();
        REQUIRE( r.snapshots.size() == 1 );
        REQUIRE( r.apply()->written == 1 );
    }
    SECTION( "error" )
    {
        r.device.duringWrite = [ & ] { REQUIRE( r.driver->backendError( r.epoch ).ok ); };
        r.pump();
        REQUIRE( r.snapshots.size() == 1 );
        REQUIRE( r.snapshots.back()->acceptance == 2 );
        REQUIRE( r.snapshots.back()->written == 1 );
        REQUIRE( r.snapshots.back()->fault );
    }
    SECTION( "invalid early credit" )
    {
        r.device.early = 3;
        r.pump();
        REQUIRE( r.snapshots.size() == 1 );
        REQUIRE( r.snapshots.back()->fault );
        REQUIRE( r.snapshots.back()->written == 0 );
    }
}
TEST_CASE( "Driver validates epochs, device lifetime and calling thread",
           "[communication][D01c-driver]" )
{
    Rig r;
    r.submit( "a", "abc" );
    auto e = r.envelope();
    REQUIRE_FALSE( r.driver->beginClose( { "serial-test", 8 } ).ok );
    QThread thread;
    QObject worker;
    worker.moveToThread( &thread );
    thread.start();
    cilogg::DataStatus result;
    REQUIRE( QMetaObject::invokeMethod(
        &worker, [ & ] { result = r.driver->offer( e ); }, Qt::BlockingQueuedConnection ) );
    thread.quit();
    REQUIRE( thread.wait( 1000 ) );
    REQUIRE_FALSE( result.ok );
    REQUIRE( r.device.calls.empty() );
    REQUIRE( r.driver->offer( e ).ok );
    REQUIRE( r.snapshots.size() == 1 );
    r.device.drain( 4 );
    REQUIRE( r.snapshots.size() == 1 );
    r.apply();
    REQUIRE( r.snapshots.size() == 2 );
    REQUIRE( r.snapshots.back()->fault );
    auto device = std::make_unique<Device>();
    auto d = SerialWriteDriver::create( *device, r.context );
    REQUIRE( d.value );
    device.reset();
    REQUIRE_FALSE( ( *d.value )->offer( e ).ok );
}
TEST_CASE( "Driver observers may delete or reenter without replay", "[communication][D01c-driver]" )
{
    Rig r;
    r.submit( "a", "abc" );
    auto e = r.envelope();
    SECTION( "write callback deletion" )
    {
        r.device.duringWrite = [ & ] { r.driver.reset(); };
        auto* driver = r.driver.get();
        REQUIRE_FALSE( driver->offer( e ).ok );
        REQUIRE_FALSE( r.driver );
    }
    SECTION( "snapshot callback deletion" )
    {
        QObject::connect( r.driver.get(), &SerialWriteDriver::snapshotReady,
                          [ & ] { r.driver.reset(); } );
        auto* driver = r.driver.get();
        REQUIRE( driver->offer( e ).ok );
        REQUIRE_FALSE( r.driver );
    }
    SECTION( "snapshot reentry" )
    {
        QObject::connect( r.driver.get(), &SerialWriteDriver::snapshotReady,
                          [ & ] { REQUIRE_FALSE( r.driver->offer( e ).ok ); } );
        REQUIRE( r.driver->offer( e ).ok );
        REQUIRE( r.device.calls.size() == 1 );
    }
}
TEST_CASE( "Cancellation without progress retires promptly and cannot replay",
           "[communication][D01c-driver]" )
{
    Rig r;
    r.submit( "a", "abc" );
    r.pump();
    r.apply();
    const auto start = std::chrono::steady_clock::now();
    REQUIRE( r.writer->cancel( r.epoch, "a", 0 ).ok );
    REQUIRE( r.writer->getWrite( "a" ).value->receipt.stage() == WriteStage::Cancelled );
    REQUIRE( std::chrono::steady_clock::now() - start < std::chrono::seconds( 1 ) );
    REQUIRE( r.writer->close( r.epoch, 0 ).ok );
    REQUIRE( r.driver->beginClose( r.epoch ).ok );
    r.device.close();
    REQUIRE( r.driver->backendClosed( r.epoch ).ok );
    REQUIRE( r.writer->retireAfterBackendClosed( r.epoch, 0 ).ok );
    REQUIRE( r.writer->getWrite( "a" ).value->ioUnconfirmed );
    REQUIRE( r.device.calls.size() == 1 );
    REQUIRE_FALSE( r.driver->offer( SerialWriteEnvelopePtr{} ).ok );
}
TEST_CASE( "A drained offer cannot be replayed by a late queued delivery",
           "[communication][D01c-driver-replay]" )
{
    Rig r;
    r.submit( "a", "abc" );
    const auto envelope = r.envelope();
    REQUIRE( r.driver->offer( envelope ).ok );
    r.apply();
    r.device.drain( 3 );
    r.apply();
    REQUIRE_FALSE( r.driver->offer( envelope ).ok );
    REQUIRE( r.device.accepted == "abc" );
}
namespace {
QStringList driverMessages;
std::atomic<QLoggingCategory::CategoryFilter> previousDriverFilter{ nullptr };
void enableDriverDiagnostics( QLoggingCategory* category )
{
    if ( const auto previous = previousDriverFilter.load( std::memory_order_acquire ) )
        previous( category );
    if ( qstrcmp( category->categoryName(), "cilogg.communication.driver" ) == 0 )
        category->setEnabled( QtDebugMsg, true );
}
void captureDriverMessage( QtMsgType, const QMessageLogContext&, const QString& message )
{
    driverMessages.push_back( message );
}
struct CaptureDriverMessages {
    QtMessageHandler previous = qInstallMessageHandler( captureDriverMessage );
    QLoggingCategory::CategoryFilter previousFilter = nullptr;
    CaptureDriverMessages()
    {
        driverMessages.clear();
        previousFilter = QLoggingCategory::installFilter( enableDriverDiagnostics );
        previousDriverFilter.store( previousFilter, std::memory_order_release );
    }
    ~CaptureDriverMessages()
    {
        QLoggingCategory::installFilter( previousFilter );
        previousDriverFilter.store( nullptr, std::memory_order_release );
        qInstallMessageHandler( previous );
    }
};
} // namespace
TEST_CASE( "Driver diagnostics contain counts and never payload encodings",
           "[communication][D01c-driver-privacy]" )
{
    CaptureDriverMessages capture;
    Rig r;
    const QByteArray secret( "private-serial-body\0\xff", 21 );
    r.submit( "secret", secret );
    r.pump();
    r.apply();
    r.device.drain( 16 );
    r.apply();
    r.pump();
    r.apply();
    r.device.drain( 5 );
    r.apply();
    REQUIRE( r.device.accepted == secret );
    const auto logs = driverMessages.join( '\n' );
    REQUIRE_FALSE( logs.isEmpty() );
    REQUIRE_FALSE( logs.contains( "private-serial-body" ) );
    REQUIRE_FALSE( logs.contains( secret.toHex() ) );
    REQUIRE_FALSE( logs.contains( secret.toBase64() ) );
    REQUIRE_FALSE(
        logs.contains( QCryptographicHash::hash( secret, QCryptographicHash::Sha256 ).toHex() ) );
    const auto receipt
        = encodeWriteReceipt( r.writer->getWrite( "secret" ).value->receipt, r.limits );
    REQUIRE( receipt.value );
    REQUIRE_FALSE( QJsonDocument( *receipt.value ).toJson().contains( "private-serial-body" ) );
}
