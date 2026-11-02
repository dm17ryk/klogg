// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewassembly.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <catch2/catch.hpp>

namespace {
QString wrapped( const QString& text )
{
    return QStringLiteral( "01/11/2026 09:51:00.175 [RX] - " ) + text;
}
void chunk( QStringList& rows, const QStringList& payload, qint64 bytes, int port = 5975 )
{
    for ( const auto& line : payload )
        rows << wrapped( line );
    rows << wrapped( QString( "#12 100 INFO:NET Send message command: %1" ).arg( port ) );
    rows << wrapped( QString() );
    rows << wrapped(
        QString( "#12 101 DETAILS:NET SendTCPData pRemoteIp(0x1) pBuff(0x2) Len(%1) Port(%2)" )
            .arg( bytes )
            .arg( port ) );
    rows << wrapped(
        QString( "#12 102 INFO:NET TCP message was sent successfully Len(%1)" ).arg( bytes ) );
    rows << wrapped( "#12 103 DETAILS:NET return value:0" );
}
QStringList headers( qint64 length )
{
    return { "PUT sample.dat HTTP/1.1", "Host: CARE-PANELINFO",
             QString( "Content-Length: %1" ).arg( length ),
             "Content-Type: application/octet-stream", "" };
}
const QString Message = QStringLiteral( "EHCI001INF00000004TESTCRC0000" );
QStringList firstChunk( qint64 advertised = Message.size(), int bytesDelta = 0 )
{
    auto payload = headers( advertised );
    const auto headerBytes = payload.join( "\r\n" ).toUtf8().size() + 2;
    payload << Message.left( 15 );
    QStringList rows;
    chunk( rows, payload, headerBytes + 15 + bytesDelta );
    return rows;
}
} // namespace

TEST_CASE( "NET log chunks assemble only their declared EHCI body", "[previewassembly]" )
{
    auto rows = firstChunk();
    rows << wrapped( "#22 99 INFO:unrelated diagnostic" );
    chunk( rows, { "other socket" }, 12, 7000 );
    const auto lastPayload = static_cast<int>( rows.size() );
    chunk( rows, { Message.mid( 15 ) }, Message.size() - 15 );
    for ( int target : { 5, lastPayload } ) {
        const auto result = assemblePreviewMessage( rows, target, 100 );
        CHECK( result.state == "complete" );
        CHECK( result.text == Message );
        CHECK( result.expectedBytes == Message.toUtf8().size() );
        CHECK( result.receivedBytes == result.expectedBytes );
        REQUIRE( result.physicalLines.size() == 2 );
        CHECK( result.physicalLines.front() == 105 );
        CHECK( result.physicalLines.back() == static_cast<quint64>( 100 + lastPayload ) );
        CHECK_FALSE( result.metadata().value( "checksumVerified" ).toBool() );
    }
}

TEST_CASE( "NET partial and inconsistent dumps never become complete", "[previewassembly]" )
{
    SECTION( "missing continuation" )
    {
        const auto result = assemblePreviewMessage( firstChunk(), 5 );
        CHECK( result.state == "incomplete" );
        CHECK( result.receivedBytes == 15 );
    }
    SECTION( "UART bytes disagree with the send count" )
    {
        auto rows = firstChunk( Message.size(), 1 );
        chunk( rows, { Message.mid( 15 ) }, Message.size() - 15 );
        const auto result = assemblePreviewMessage( rows, 5 );
        CHECK( result.state != "complete" );
        CHECK( result.reason == "fragment_length_mismatch" );
    }
    SECTION( "same port reused for another connection" )
    {
        auto rows = firstChunk();
        rows << wrapped( "#12 99 INFO:NET TCP connect Lport:5975 Rport:5666" );
        chunk( rows, { Message.mid( 15 ) }, Message.size() - 15 );
        const auto result = assemblePreviewMessage( rows, 5 );
        CHECK( result.state == "incomplete" );
        CHECK( result.reason == "connection_changed" );
    }
    SECTION( "context and body limits" )
    {
        QStringList many;
        for ( int i = 0; i < 1025; ++i )
            many << "line";
        CHECK( assemblePreviewMessage( many, 0 ).reason == "context_limit" );
        CHECK( assemblePreviewMessage( firstChunk( 1024 * 1024 ), 5 ).reason == "message_limit" );
    }
}

TEST_CASE( "An HTTP request starts a new payload after unrelated raw log text",
           "[previewassembly]" )
{
    auto rows = firstChunk();
    rows.prepend( wrapped( "ordinary earlier row" ) );
    chunk( rows, { Message.mid( 15 ) }, Message.size() - 15 );
    const auto result = assemblePreviewMessage( rows, 6 );
    CHECK( result.state == "complete" );
    CHECK( result.text == Message );
}

TEST_CASE( "Byte counts identify a unique continuation after an unrelated raw reply",
           "[previewassembly]" )
{
    auto rows = firstChunk();
    chunk( rows, { "OK", Message.mid( 15 ) }, Message.size() - 15 );
    const auto result = assemblePreviewMessage( rows, 5 );
    CHECK( result.state == "complete" );
    CHECK( result.text == Message );
    CHECK( result.excludedPrefixRows == 1 );
    REQUIRE( result.physicalLines.size() == 2 );
    CHECK( result.physicalLines.back() == static_cast<quint64>( rows.size() - 6 ) );
}

TEST_CASE( "Equal-byte continuation candidates with different physical rows remain ambiguous",
           "[previewassembly]" )
{
    auto rows = firstChunk();
    chunk( rows, { "OK", "", Message.mid( 15 ) }, Message.size() - 15 );
    const auto result = assemblePreviewMessage( rows, 5 );
    CHECK( result.state == "incomplete" );
    CHECK( result.reason == "ambiguous_fragment" );
    CHECK( result.receivedBytes == 15 );
}

TEST_CASE( "Ordinary log rows and diagnostics are not payload fragments", "[previewassembly]" )
{
    CHECK( assemblePreviewMessage( { "ordinary", "EHCP unrelated" }, 0 ).state == "unavailable" );
    CHECK( assemblePreviewMessage( firstChunk(), 6 ).state == "unavailable" );
}

TEST_CASE( "Explicit private log snapshot reassembly qualification", "[.private-preview-fixture]" )
{
    const auto path = qEnvironmentVariable( "CILOGG_PREVIEW_LOG_FIXTURE" );
    REQUIRE_FALSE( path.isEmpty() );
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const auto bytes = file.read( 1024 * 1024 );
    const auto all = QString::fromUtf8( bytes ).split( '\n' );
    int target = -1;
    for ( int i = 0; i < all.size(); ++i )
        if ( all[ i ].contains( "EHCI001" ) ) {
            target = i;
            break;
        }
    REQUIRE( target >= 0 );
    const auto first = qMax( 0, target - 128 );
    const auto result = assemblePreviewMessage( all.mid( first, 640 ), target - first,
                                                static_cast<quint64>( first ) );
    QFile output( qEnvironmentVariable( "CILOGG_PREVIEW_FIXTURE_RESULT" ) );
    REQUIRE( output.open( QIODevice::WriteOnly ) );
    auto metadata = result.metadata();
    metadata[ "inputPrefixSha256" ] = QString::fromLatin1(
        QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() );
    metadata[ "assembledSha256" ] = QString::fromLatin1(
        QCryptographicHash::hash( result.text.toUtf8(), QCryptographicHash::Sha256 ).toHex() );
    output.write( QJsonDocument( QJsonObject::fromVariantMap( metadata ) ).toJson() );
    INFO( result.reason.toStdString() );
    CHECK( result.state == "complete" );
    CHECK( result.expectedBytes == 3242 );
    CHECK( result.receivedBytes == 3242 );
}
