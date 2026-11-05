#include "terminalemulator.h"
#include <catch2/catch.hpp>
#include <thread>

using namespace terminal;
namespace {
void feedAll( TerminalEmulator& model, QByteArray bytes )
{
    while ( !bytes.isEmpty() ) {
        const auto result = model.feed( bytes.left( TerminalEmulator::MaxInputBytes ) );
        REQUIRE( ( result.status == Status::Ok || result.status == Status::Yielded ) );
        REQUIRE( result.consumedBytes > 0 );
        bytes.remove( 0, result.consumedBytes );
    }
}
QString rowText( const Snapshot& page, int row )
{
    QString text;
    for ( int col = 0; col < page.size.columns; ++col ) {
        const int at = row * page.size.columns + col - page.firstCell;
        if ( at >= 0 && at < page.cells.size() )
            text += page.cells.at( at ).text;
    }
    return text;
}
} // namespace
TEST_CASE( "Split UTF8 is decoded once", "[terminal][UT01][model]" )
{
    const auto bytes = QString::fromUtf8( "Aé中😀" ).toUtf8();
    for ( int split = 0; split <= bytes.size(); ++split ) {
        TerminalEmulator model( { 20, 3 } );
        feedAll( model, bytes.left( split ) );
        feedAll( model, bytes.mid( split ) );
        CHECK( rowText( model.snapshot(), 0 ).startsWith( QString::fromUtf8( "Aé中😀" ) ) );
    }
}
TEST_CASE( "CSI split at every boundary is invariant", "[terminal][UT01][model]" )
{
    const QByteArray bytes( "abc\x1b[2D\x1b[31;1mZ\x1b[0m" );
    for ( int split = 0; split <= bytes.size(); ++split ) {
        TerminalEmulator model( { 20, 3 } );
        feedAll( model, bytes.left( split ) );
        feedAll( model, bytes.mid( split ) );
        const auto page = model.snapshot();
        REQUIRE( page.cells.size() >= 3 );
        CHECK( page.cells.at( 0 ).text == "a" );
        CHECK( page.cells.at( 1 ).text == "Z" );
        CHECK( page.cells.at( 1 ).bold );
        CHECK( page.cells.at( 1 ).foreground != page.cells.at( 0 ).foreground );
        CHECK( page.cursorColumn == 2 );
    }
}
TEST_CASE( "Prompt without LF is visible", "[terminal][UT01][model]" )
{
    TerminalEmulator model( { 20, 3 } );
    feedAll( model, "prompt> " );
    CHECK( rowText( model.snapshot(), 0 ).startsWith( "prompt> " ) );
    const auto frozen = model.snapshot();
    feedAll( model, "x" );
    CHECK( rowText( frozen, 0 ).startsWith( "prompt> " ) );
    CHECK_FALSE( rowText( frozen, 0 ).contains( 'x' ) );
}
TEST_CASE( "CR and backspace update live cells", "[terminal][UT01][model]" )
{
    TerminalEmulator model( { 20, 3 } );
    feedAll( model, "abc\rX\bY" );
    CHECK( rowText( model.snapshot(), 0 ).startsWith( "Ybc" ) );
}
TEST_CASE( "Alternate screen restores primary", "[terminal][UT01][model]" )
{
    TerminalEmulator model( { 20, 3 } );
    feedAll( model, "primary\x1b[?1049hALT" );
    CHECK( model.snapshot().alternateScreen );
    CHECK( rowText( model.snapshot(), 0 ).contains( "ALT" ) );
    feedAll( model, "\x1b[?1049l" );
    CHECK_FALSE( model.snapshot().alternateScreen );
    CHECK( rowText( model.snapshot(), 0 ).startsWith( "primary" ) );
}
TEST_CASE( "Resize preserves valid cursor", "[terminal][UT01][model]" )
{
    TerminalEmulator model( { 20, 3 } );
    feedAll( model, "\x1b[3;20H!" );
    REQUIRE( model.resize( { 4, 2 } ).status == Status::Ok );
    auto page = model.snapshot();
    CHECK( page.size.columns == 4 );
    CHECK( page.size.rows == 2 );
    CHECK( page.cursorRow >= 0 );
    CHECK( page.cursorRow < 2 );
    CHECK( page.cursorColumn >= 0 );
    CHECK( page.cursorColumn < 4 );
    CHECK( model.resize( { 0, 2 } ).status == Status::InvalidRequest );
    CHECK( model.resize( { 1001, 500 } ).status == Status::InvalidRequest );
    CHECK( model.snapshot().size.columns == 4 );
}
TEST_CASE( "Huge OSC is bounded", "[terminal][UT01][model]" )
{
    TerminalEmulator model( { 20, 3 } );
    feedAll( model, "\x1b]2;" );
    feedAll( model, QByteArray( 65536, 'x' ) );
    CHECK( model.feed( "x" ).status == Status::ResourceLimit );
    CHECK( model.feed( "visible?" ).status == Status::ResourceLimit );
    REQUIRE( model.reset( { 20, 3 } ).status == Status::Ok );
    feedAll( model, "safe" );
    CHECK( rowText( model.snapshot(), 0 ).startsWith( "safe" ) );
}
TEST_CASE( "OSC52 cannot touch clipboard", "[terminal][UT01][model]" )
{
    TerminalEmulator model( { 20, 3 } );
    feedAll( model, "\x1b]52;c;SGVsbG8=\x07\x1b]52;c;?\x07" );
    CHECK( model.takeReplies().isEmpty() );
    CHECK( rowText( model.snapshot(), 0 ).trimmed().isEmpty() );
}
TEST_CASE( "Cell capacity and bounded revision pages are explicit", "[terminal][UT01][model]" )
{
    TerminalEmulator model( { 20, 3 } );
    feedAll( model, QString::fromUtf8( "á́́́́́́́́́" ).toUtf8() );
    auto page = model.snapshot( 0, 1 );
    REQUIRE( page.cells.size() == 1 );
    CHECK( page.cells.at( 0 ).capacityReached );
    CHECK( page.cells.at( 0 ).text.toUcs4().size() == 6 );
    CHECK( page.nextCell == 1 );
    CHECK( model.snapshot( -1, 1 ).result.status == Status::InvalidRequest );
    CHECK( model.snapshot( 0, 16385 ).result.status == Status::InvalidRequest );
    feedAll( model, "x" );
    CHECK( model.snapshot( 1, 1, page.revision ).result.status == Status::StaleRevision );
}
TEST_CASE( "Input admission and cooperative slices preserve suffix", "[terminal][UT01][model]" )
{
    TerminalEmulator model( { 20, 3 } );
    CHECK( model.feed( QByteArray( 65537, 'x' ) ).status == Status::InvalidRequest );
    auto result = model.feed( QByteArray( 1024, 'x' ) );
    CHECK( result.status == Status::Yielded );
    CHECK( result.consumedBytes == 256 );
    Status status = Status::Ok;
    std::thread other( [ & ] { status = model.feed( "wrong thread" ).status; } );
    other.join();
    CHECK( status == Status::WrongThread );
}
TEST_CASE( "Replies and key encoding never provide transport authority", "[terminal][UT01][model]" )
{
    TerminalEmulator model( { 20, 3 } );
    feedAll( model, "\x1b[6n" );
    CHECK( model.takeReplies() == "\x1b[1;1R" );
    CHECK( model.takeReplies().isEmpty() );
    CHECK( model.encodeKey( Key::Up ).bytes == "\x1b[A" );
    feedAll( model, "\x1b[?1h" );
    CHECK( model.encodeKey( Key::Up ).bytes == "\x1bOA" );
    feedAll( model, "\x1b[?2004h" );
    CHECK( model.encodePaste( QStringLiteral( "hello" ) ).bytes == "\x1b[200~hello\x1b[201~" );
    CHECK( rowText( model.snapshot(), 0 ).trimmed().isEmpty() );
    CHECK( model.encodePaste( QString( 65537, 'a' ) ).result.status == Status::InvalidRequest );
    TerminalEmulator replies( { 20, 3 } );
    bool limited = false;
    for ( int n = 0; n < 12000 && !limited; ++n )
        limited = replies.feed( "\x1b[6n" ).status == Status::ResourceLimit;
    CHECK( limited );
    CHECK( replies.takeReplies().size() <= TerminalEmulator::MaxReplyBytes );
}
