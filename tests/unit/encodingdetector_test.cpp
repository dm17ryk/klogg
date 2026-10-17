#include <catch2/catch.hpp>

#include <QTextCodec>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <memory>
#include <type_traits>
#include <vector>

#include <uchardet.h>

#include "encodingdetector.h"

namespace {
QTextCodec* detect( const QByteArray& bytes )
{
    return EncodingDetector::getInstance().detectEncoding(
        klogg::vector<char>( bytes.constBegin(), bytes.constEnd() ) );
}

QTextCodec* legacyDetection( const QByteArray& bytes )
{
    std::unique_ptr<std::remove_pointer_t<uchardet_t>, decltype( &uchardet_delete )> detector(
        uchardet_new(), &uchardet_delete );
    const auto result = uchardet_handle_data( detector.get(), bytes.constData(),
                                              static_cast<std::size_t>( bytes.size() ) );
    QTextCodec* fallback = nullptr;
    if ( result == 0 ) {
        uchardet_data_end( detector.get() );
        fallback = QTextCodec::codecForName( uchardet_get_charset( detector.get() ) );
    }
    return fallback ? QTextCodec::codecForUtfText( bytes, fallback )
                    : QTextCodec::codecForUtfText( bytes );
}
} // namespace

TEST_CASE( "Encoding detection recognizes short UTF-8 and BOM precedence", "[encoding]" )
{
    const auto utf8 = QTextCodec::codecForName( "UTF-8" );
    SECTION( "A single UTF-8 emoji is sufficient" )
    {
        REQUIRE( detect( QByteArray::fromHex( "f09f9880" ) ) == utf8 );
    }
    SECTION( "UTF-8 scalar boundaries are accepted" )
    {
        REQUIRE( detect( QByteArray::fromHex( "c280dfbfe0a080ed9fbfee8080f0908080f48fbfbf" ) )
                 == utf8 );
    }
    SECTION( "UTF-16 BOM takes precedence over ASCII-looking bytes" )
    {
        REQUIRE( detect( QByteArray::fromHex( "fffe610062000a00" ) )
                 == QTextCodec::codecForName( "UTF-16LE" ) );
        REQUIRE( detect( QByteArray::fromHex( "feff00610062000a" ) )
                 == QTextCodec::codecForName( "UTF-16BE" ) );
    }
    SECTION( "UTF-32 BOM takes precedence over its UTF-16 prefix" )
    {
        REQUIRE( detect( QByteArray::fromHex( "fffe0000610000000a000000" ) )
                 == QTextCodec::codecForName( "UTF-32LE" ) );
    }
    SECTION( "Empty and ASCII input retains a usable codec" )
    {
        REQUIRE( detect( {} ) != nullptr );
        const QByteArray ascii( "plain ASCII log\n" );
        const auto* codec = detect( ascii );
        REQUIRE( codec != nullptr );
        REQUIRE( codec->toUnicode( ascii ) == QString::fromLatin1( ascii ) );
    }
}

TEST_CASE( "Independent encoding detections are safe during parallel opens", "[encoding]" )
{
    const std::vector<QByteArray> samples{
        QByteArray::fromHex( "f09f988020d7a9d79cd795d79d0a" ).repeated( 100 ),
        QByteArray::fromHex( "fffe610062000a00" ), QByteArray::fromHex( "1b2442242224241b2842" ),
        QByteArray::fromHex( "cfefeff2e8e2e5f2ecff0a" ), QByteArray::fromHex( "e282" )
    };
    std::vector<QTextCodec*> expected{ QTextCodec::codecForName( "UTF-8" ) };
    for ( size_t i = 1; i < samples.size(); ++i ) {
        expected.push_back( legacyDetection( samples[ i ] ) );
    }
    std::vector<std::future<bool>> results;
    for ( int i = 0; i < 8; ++i ) {
        results.push_back( std::async( std::launch::async, [ samples, expected ] {
            for ( int attempt = 0; attempt < 32; ++attempt ) {
                for ( size_t sample = 0; sample < samples.size(); ++sample ) {
                    if ( detect( samples[ sample ] ) != expected[ sample ] ) {
                        return false;
                    }
                }
            }
            return true;
        } ) );
    }
    for ( auto& result : results ) {
        REQUIRE( result.get() );
    }
}

TEST_CASE( "Ambiguous and invalid UTF-8 samples preserve legacy detection", "[encoding]" )
{
    const auto sample = GENERATE(
        QByteArray{}, QByteArray( "ASCII log\n" ), QByteArray::fromHex( "1b2442242224241b2842" ),
        QByteArray::fromHex( "610062000a00" ), QByteArray::fromHex( "c0af" ),
        QByteArray::fromHex( "eda080" ), QByteArray::fromHex( "f4908080" ),
        QByteArray::fromHex( "e282" ), QByteArray::fromHex( "cfefeff2e8e2e5f2ecff0a" ) );
    CAPTURE( sample.toHex().constData() );
    REQUIRE( detect( sample ) == legacyDetection( sample ) );
}

TEST_CASE( "Encoding detection benchmark", "[.][encoding][benchmark]" )
{
    const QByteArray bytes
        = QByteArray::fromHex( "f09f988020d7a9d79cd795d79d0a" ).repeated( 20000 );
    std::vector<double> samples;
    for ( int attempt = 0; attempt < 32; ++attempt ) {
        const auto start = std::chrono::steady_clock::now();
        REQUIRE( detect( bytes ) != nullptr );
        samples.push_back(
            std::chrono::duration<double, std::micro>( std::chrono::steady_clock::now() - start )
                .count() );
    }
    std::sort( samples.begin(), samples.end() );
    std::cout << "CILOGG_BENCHMARK encoding_bytes=" << bytes.size() << " samples=" << samples.size()
              << " median_us=" << samples[ 16 ] << " p95_us=" << samples[ 30 ] << '\n';
}
