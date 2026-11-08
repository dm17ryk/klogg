// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "streamsourceregistry.h"
#include <catch2/catch.hpp>
#include <future>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

struct StreamSourceRegistryTestAccess {
    static std::unique_ptr<StreamSourceRegistry>
    create( bool windows, QVector<SerialPortAlias> ports = {},
            std::function<QString( const QString& )> paths = {} )
    {
        if ( !paths )
            paths = []( const QString& p ) { return p; };
        return std::unique_ptr<StreamSourceRegistry>(
            new StreamSourceRegistry( windows, [ ports ] { return ports; }, std::move( paths ) ) );
    }
    static void exhaust( StreamSourceRegistry& r )
    {
        r.nextLeaseId_ = std::numeric_limits<quint64>::max();
    }
};
namespace {
StreamLimits leaseLimits()
{
    return *StreamLimits::fromDefinition( {} ).value;
}
struct Leases {
    std::unique_ptr<StreamSourceRegistry> registry;
    std::vector<SerialEndpointLease> owned;
    explicit Leases( bool windows = true, QVector<SerialPortAlias> ports = {},
                     std::function<QString( const QString& )> paths = {} )
        : registry( StreamSourceRegistryTestAccess::create( windows, ports, std::move( paths ) ) )
    {
    }
    ~Leases()
    {
        for ( const auto& l : owned )
            registry->releaseSerialPort( l );
    }
    DataResult<SerialEndpointLease> acquire( const QString& port )
    {
        auto r = registry->tryAcquireSerialPort( port, leaseLimits() );
        if ( r.value )
            owned.push_back( *r.value );
        return r;
    }
};
} // namespace
TEST_CASE( "Endpoint acquisition has one winner under contention", "[communication][D01c-lease]" )
{
    Leases leases;
    std::promise<void> gate;
    auto ready = gate.get_future().share();
    DataResult<SerialEndpointLease> a, b;
    std::thread one( [ & ] {
        ready.wait();
        a = leases.registry->tryAcquireSerialPort( "COM3", leaseLimits() );
    } );
    std::thread two( [ & ] {
        ready.wait();
        b = leases.registry->tryAcquireSerialPort( " com3 ", leaseLimits() );
    } );
    gate.set_value();
    one.join();
    two.join();
    if ( a.value )
        leases.owned.push_back( *a.value );
    if ( b.value )
        leases.owned.push_back( *b.value );
    REQUIRE( static_cast<int>( a.value.has_value() ) + static_cast<int>( b.value.has_value() )
             == 1 );
    REQUIRE( ( a.value ? b.error.code : a.error.code ) == "endpoint_busy" );
}
TEST_CASE( "Endpoint lease canonicalizes Windows aliases", "[communication][D01c-lease]" )
{
    Leases leases( true, { { "COM3", "\\\\.\\COM3" } } );
    auto a = leases.acquire( " com3 " );
    REQUIRE( a.value );
    REQUIRE_FALSE( leases.acquire( "\\\\.\\Com3" ).value );
    REQUIRE( leases.registry->isSerialPortActive( "\\\\.\\COM3" ) );
    leases.registry->registerSerialPort( "COM3" );
    leases.registry->unregisterSerialPort( "COM3" );
    REQUIRE( leases.registry->isSerialPortActive( "COM3" ) );
    REQUIRE( leases.registry->releaseSerialPort( *a.value ).ok );
    REQUIRE_FALSE( leases.registry->isSerialPortActive( "COM3" ) );
}
TEST_CASE( "Unix device identity preserves case and resolves symlink aliases",
           "[communication][D01c-lease]" )
{
    Leases leases( false, { { "ttyUSB0", "/dev/ttyUSB0" } }, []( const QString& p ) {
        return p == "/dev/by-id/example" ? QString( "/dev/ttyUSB0" ) : p;
    } );
    auto a = leases.acquire( "ttyUSB0" );
    REQUIRE( a.value );
    REQUIRE_FALSE( leases.acquire( "/dev/ttyUSB0" ).value );
    REQUIRE_FALSE( leases.acquire( "/dev/by-id/example" ).value );
    auto distinct = leases.acquire( "/dev/ttyusb0" );
    REQUIRE( distinct.value );
    REQUIRE( a.value->canonicalKey() != distinct.value->canonicalKey() );
}
TEST_CASE( "Ambiguous endpoint selectors fail without reserving", "[communication][D01c-lease]" )
{
    Leases leases( false, { { "same", "/dev/one" }, { "same", "/dev/two" } } );
    auto a = leases.acquire( "same" );
    REQUIRE_FALSE( a.value );
    REQUIRE( a.error.code == "ambiguous_endpoint" );
    REQUIRE( leases.acquire( "/dev/one" ).value );
    REQUIRE( leases.acquire( "/dev/two" ).value );
}
TEST_CASE( "Foreign and stale lease tokens cannot remove a replacement",
           "[communication][D01c-lease]" )
{
    Leases leases, foreign;
    auto old = leases.acquire( "COM3" );
    auto other = foreign.acquire( "COM3" );
    REQUIRE( old.value );
    REQUIRE( other.value );
    REQUIRE_FALSE( leases.registry->releaseSerialPort( *other.value ).ok );
    REQUIRE( leases.registry->releaseSerialPort( *old.value ).ok );
    REQUIRE( leases.registry->releaseSerialPort( *old.value ).ok );
    auto current = leases.acquire( "COM3" );
    REQUIRE( current.value );
    REQUIRE( current.value->leaseId() > old.value->leaseId() );
    REQUIRE_FALSE( leases.registry->releaseSerialPort( *old.value ).ok );
    REQUIRE( leases.registry->isSerialPortActive( "COM3" ) );
}
TEST_CASE( "Endpoint leases enforce capacity before growing", "[communication][D01c-lease]" )
{
    Leases leases;
    for ( int n = 1; n <= 16; ++n )
        REQUIRE( leases.acquire( QString( "COM%1" ).arg( n ) ).value );
    auto full = leases.acquire( "COM17" );
    REQUIRE_FALSE( full.value );
    REQUIRE( full.error.code == "resource_limit" );
    REQUIRE( leases.registry->releaseSerialPort( leases.owned.front() ).ok );
    REQUIRE( leases.acquire( "COM17" ).value );
}
TEST_CASE( "Legacy registration shares capacity and cannot overwrite leases",
           "[communication][D01c-lease]" )
{
    Leases leases;
    leases.registry->registerSerialPort( " COM2 " );
    REQUIRE( leases.registry->isSerialPortActive( "com2" ) );
    REQUIRE_FALSE( leases.acquire( "COM2" ).value );
    for ( int n = 3; n <= 17; ++n )
        REQUIRE( leases.acquire( QString( "COM%1" ).arg( n ) ).value );
    REQUIRE_FALSE( leases.acquire( "COM18" ).value );
    leases.registry->unregisterSerialPort( "com2" );
    REQUIRE( leases.acquire( "COM18" ).value );
    leases.registry->registerSerialPort( "" );
    REQUIRE_FALSE( leases.registry->isSerialPortActive( "" ) );
}
TEST_CASE( "Lease identity exhaustion never wraps or reserves", "[communication][D01c-lease]" )
{
    Leases leases;
    StreamSourceRegistryTestAccess::exhaust( *leases.registry );
    auto a = leases.acquire( "COM3" );
    REQUIRE_FALSE( a.value );
    REQUIRE( a.error.code == "resource_limit" );
    REQUIRE_FALSE( leases.registry->isSerialPortActive( "COM3" ) );
}
