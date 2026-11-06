// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <catch2/catch.hpp>
#include <cilogg_vterm_limits.h>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vterm.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
#ifdef _WIN32
// Keep a failing native dependency probe from opening a host crash dialog.
const auto ErrorMode = SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX );
#endif
constexpr std::size_t MaxControlBytes = 64 * 1024;
constexpr long MaxParameter = 65535;
struct Allocations {
    std::unordered_map<void*, std::size_t> blocks;
    std::size_t live = 0;
    std::size_t peak = 0;
};
void* allocate( std::size_t bytes, void* data )
{
    auto& stats = *static_cast<Allocations*>( data );
    auto* pointer = std::calloc( 1, bytes );
    if ( !pointer )
        std::abort();
    stats.blocks.emplace( pointer, bytes );
    stats.live += bytes;
    stats.peak = std::max( stats.peak, stats.live );
    return pointer;
}
void release( void* pointer, void* data )
{
    if ( !pointer )
        return;
    auto& stats = *static_cast<Allocations*>( data );
    const auto found = stats.blocks.find( pointer );
    if ( found == stats.blocks.end() )
        std::abort();
    stats.live -= found->second;
    stats.blocks.erase( found );
    std::free( pointer );
}
const VTermAllocatorFunctions Allocator{ allocate, release };
struct Terminal {
    Allocations allocations;
    std::unique_ptr<VTerm, decltype( &vterm_free )> value{ nullptr, &vterm_free };
    VTermScreen* screen = nullptr;
    explicit Terminal( bool withScreen = true )
    {
        value.reset( vterm_new_with_allocator(
            3, 20, const_cast<VTermAllocatorFunctions*>( &Allocator ), &allocations ) );
        REQUIRE( value );
        vterm_set_utf8( value.get(), 1 );
        if ( withScreen ) {
            screen = vterm_obtain_screen( value.get() );
            REQUIRE( screen );
            vterm_screen_enable_altscreen( screen, 1 );
            vterm_screen_reset( screen, 1 );
        }
    }
    void feed( const std::string& bytes )
    {
        REQUIRE( vterm_input_write( value.get(), bytes.data(), bytes.size() ) == bytes.size() );
    }
    VTermScreenCell firstCell() const
    {
        VTermScreenCell cell{};
        REQUIRE( vterm_screen_get_cell( screen, VTermPos{ 0, 0 }, &cell ) );
        return cell;
    }
};
struct Observer {
    int oscCalls = 0;
    int csiCalls = 0;
    long parameter = -1;
    std::size_t stringBytes = 0;
    static int osc( int command, VTermStringFragment fragment, void* data )
    {
        auto& self = *static_cast<Observer*>( data );
        ++self.oscCalls;
        self.parameter = command;
        self.stringBytes += fragment.len;
        return 1;
    }
    static int csi( const char*, const long arguments[], int count, const char*, char, void* data )
    {
        auto& self = *static_cast<Observer*>( data );
        ++self.csiCalls;
        if ( count )
            self.parameter = arguments[ 0 ];
        return 1;
    }
    static int dcs( const char*, std::size_t, VTermStringFragment fragment, void* data )
    {
        auto& self = *static_cast<Observer*>( data );
        self.stringBytes += fragment.len;
        return 1;
    }
    void attach( VTerm* terminal )
    {
        static const auto callbacks = [] {
            VTermParserCallbacks result{};
            result.osc = osc;
            result.csi = csi;
            result.dcs = dcs;
            return result;
        }();
        vterm_parser_set_callbacks( terminal, &callbacks, this );
    }
};
} // namespace

TEST_CASE( "Split combining glyphs keep bounded working storage", "[terminal][UT01][vtermbounds]" )
{
    Terminal terminal;
    terminal.feed( "a" );
    const auto baseline = terminal.allocations.live;
    std::string marks;
    for ( int i = 0; i < 1024; ++i )
        marks += "\xcc\x81";
    for ( int i = 0; i < 64; ++i )
        terminal.feed( marks );
    INFO( "library_live_bytes=" << terminal.allocations.live << " baseline_bytes=" << baseline
                                << " library_peak_bytes=" << terminal.allocations.peak );
    CHECK( terminal.allocations.live <= baseline + MaxControlBytes );
    CHECK( terminal.allocations.peak <= baseline + 2 * MaxControlBytes );
    const auto cell = terminal.firstCell();
    CHECK( cell.chars[ 0 ] == static_cast<uint32_t>( 'a' ) );
    for ( std::size_t i = 1; i < VTERM_MAX_CHARS_PER_CELL; ++i )
        CHECK( cell.chars[ i ] == 0x301 );
    terminal.value.reset();
    CHECK( terminal.allocations.live == 0 );
}

TEST_CASE( "Split and bulk combining cells agree at the fixed cell capacity",
           "[terminal][UT01][vtermbounds]" )
{
    Terminal bulk, split;
    const std::string input = "a\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81";
    bulk.feed( input );
    for ( const char byte : input )
        split.feed( std::string( 1, byte ) );
    const auto expected = bulk.firstCell();
    const auto actual = split.firstCell();
    for ( std::size_t i = 0; i < VTERM_MAX_CHARS_PER_CELL; ++i )
        CHECK( actual.chars[ i ] == expected.chars[ i ] );
}

TEST_CASE( "Numeric control arguments respect the declared bound", "[terminal][UT01][vtermbounds]" )
{
    {
        Observer observer;
        Terminal terminal( false );
        observer.attach( terminal.value.get() );
        terminal.feed( "\x1b]65535;value\a" );
        CHECK( observer.oscCalls > 0 );
        CHECK( observer.parameter == MaxParameter );
    }
    {
        Observer observer;
        Terminal terminal( false );
        observer.attach( terminal.value.get() );
        terminal.feed( "\x1b]99999999;value\a" );
        CHECK( observer.oscCalls == 0 );
        CHECK( cilogg_vterm_resource_limit( terminal.value.get() )
               == CILOGG_VTERM_LIMIT_PARAMETER );
    }
    {
        Observer observer;
        Terminal terminal( false );
        observer.attach( terminal.value.get() );
        terminal.feed( "\x1b[99999999m" );
        CHECK( observer.csiCalls == 0 );
        CHECK( cilogg_vterm_resource_limit( terminal.value.get() )
               == CILOGG_VTERM_LIMIT_PARAMETER );
    }
}

TEST_CASE( "OSC and DCS strings stop dispatching beyond their payload bound",
           "[terminal][UT01][vtermbounds]" )
{
    for ( const auto& prefix : { std::string( "\x1b]777;" ), std::string( "\x1bP777q" ) } ) {
        Observer observer;
        Terminal terminal( false );
        observer.attach( terminal.value.get() );
        terminal.feed( prefix );
        const std::string block( 16 * 1024, 'x' );
        for ( int i = 0; i < 4; ++i )
            terminal.feed( block );
        CHECK( observer.stringBytes == MaxControlBytes );
        terminal.feed( "x" );
        terminal.feed( "\x1b\\" );
        CHECK( observer.stringBytes <= MaxControlBytes );
        CHECK( cilogg_vterm_resource_limit( terminal.value.get() ) == CILOGG_VTERM_LIMIT_STRING );
    }
}

TEST_CASE( "OSC52 has no default selection side effects or replies",
           "[terminal][UT01][vtermbounds]" )
{
    Terminal terminal;
    terminal.feed( "p" );
    terminal.feed( "\x1b]52;c;SGVsbG8=\a" );
    CHECK( vterm_output_get_buffer_current( terminal.value.get() ) == 0 );
    CHECK( terminal.firstCell().chars[ 0 ] == static_cast<uint32_t>( 'p' ) );
}

TEST_CASE( "CSI parameter count is checked before fixed-array access",
           "[terminal][UT01][vtermcount]" )
{
    {
        Observer observer;
        Terminal terminal( false );
        observer.attach( terminal.value.get() );
        std::string bytes = "\x1b[";
        for ( int i = 0; i < 15; ++i )
            bytes += "1;";
        terminal.feed( bytes + "1m" );
        CHECK( observer.csiCalls == 1 );
    }
    {
        Observer observer;
        Terminal terminal( false );
        observer.attach( terminal.value.get() );
        std::string bytes = "\x1b[";
        for ( int i = 0; i < 16; ++i )
            bytes += "1;";
        terminal.feed( bytes + "1m" );
        CHECK( observer.csiCalls == 0 );
    }
}

TEST_CASE( "Incomplete OSC command controls remain bounded no-ops",
           "[terminal][UT01][vtermbounds]" )
{
    for ( const auto& input : { std::string( "\x1b]1\0;value\a", 11 ), std::string( "\x1b]1\a" ),
                                std::string( "\x1b]1\x7f;v\a" ) } ) {
        Terminal terminal;
        terminal.feed( input );
        CHECK( cilogg_vterm_resource_limit( terminal.value.get() ) == CILOGG_VTERM_LIMIT_NONE );
    }
    CHECK( cilogg_vterm_resource_limit( nullptr ) == CILOGG_VTERM_LIMIT_INVALID );
}

TEST_CASE( "Zero padding and ignored string controls cannot bypass wire budgets",
           "[terminal][UT01][vtermbounds]" )
{
    for ( const auto& prefix : { std::string( "\x1b[" ), std::string( "\x1b]" ) } ) {
        Observer observer;
        Terminal terminal( false );
        observer.attach( terminal.value.get() );
        terminal.feed( prefix + std::string( 17, '0' ) + ";value\a" );
        CHECK( cilogg_vterm_resource_limit( terminal.value.get() )
               == CILOGG_VTERM_LIMIT_PARAMETER );
    }
    Terminal terminal;
    terminal.feed( "\x1b]777;" );
    const std::string ignored( 16 * 1024, '\0' );
    for ( int i = 0; i < 5; ++i )
        terminal.feed( ignored );
    CHECK( cilogg_vterm_resource_limit( terminal.value.get() ) == CILOGG_VTERM_LIMIT_STRING );
}

TEST_CASE( "Fresh repeat has no previous glyph and makes progress", "[terminal][UT01][vtermrep]" )
{
    Terminal terminal;
    terminal.feed( "\x1b[1b" );
    VTermPos cursor{};
    vterm_state_get_cursorpos( vterm_obtain_state( terminal.value.get() ), &cursor );
    CHECK( cursor.col == 0 );
    terminal.feed( "ok" );
    CHECK( terminal.firstCell().chars[ 0 ] == uint32_t( 'o' ) );
}
TEST_CASE( "Wide glyph primitives clip safely at the right screen edge",
           "[terminal][UT01][vtermwide]" )
{
    for ( int columns : { 1, 3 } ) {
        auto vt = std::unique_ptr<VTerm, decltype( &vterm_free )>( vterm_new( 1, columns ),
                                                                   &vterm_free );
        vterm_set_utf8( vt.get(), 1 );
        auto* screen = vterm_obtain_screen( vt.get() );
        vterm_screen_reset( screen, 1 );
        const std::string wide( "\xe4\xb8\xad" );
        REQUIRE( vterm_input_write( vt.get(), wide.data(), wide.size() ) == wide.size() );
        const std::string repeat = "\x1b[1;" + std::to_string( columns ) + "H\x1b[1b";
        REQUIRE( vterm_input_write( vt.get(), repeat.data(), repeat.size() ) == repeat.size() );
        VTermScreenCell cell{};
        REQUIRE( vterm_screen_get_cell( screen, { 0, columns - 1 }, &cell ) );
        CHECK( cell.chars[ 0 ] == 0x4e2d );
        CHECK( cell.width == 1 );
        VTermPos cursor{};
        vterm_state_get_cursorpos( vterm_obtain_state( vt.get() ), &cursor );
        CHECK( cursor.col >= 0 );
        CHECK( cursor.col < columns );
    }
}

TEST_CASE( "UTF8 encoded C1 characters never make cursor coordinates negative",
           "[terminal][UT01][vtermc1]" )
{
    Terminal terminal;
    terminal.feed( "\xc2\x80" );
    VTermPos cursor{};
    vterm_state_get_cursorpos( vterm_obtain_state( terminal.value.get() ), &cursor );
    REQUIRE( cursor.col >= 0 );
    terminal.feed( "\x1bH\t" );
    CHECK( terminal.firstCell().chars[ 0 ] == 0xfffd );
    vterm_state_get_cursorpos( vterm_obtain_state( terminal.value.get() ), &cursor );
    CHECK( cursor.col >= 0 );
    CHECK( cursor.col < 20 );
}

TEST_CASE( "Double-width line controls preserve a valid one-column cursor",
           "[terminal][UT01][vtermrow]" )
{
    for ( char mode : { '3', '4', '6' } ) {
        auto vt = std::unique_ptr<VTerm, decltype( &vterm_free )>( vterm_new( 1, 1 ), &vterm_free );
        auto* screen = vterm_obtain_screen( vt.get() );
        vterm_screen_reset( screen, 1 );
        const std::string sequence = std::string( "\x1b#" ) + mode + "\x1b[1G";
        REQUIRE( vterm_input_write( vt.get(), sequence.data(), sequence.size() )
                 == sequence.size() );
        VTermPos cursor{};
        vterm_state_get_cursorpos( vterm_obtain_state( vt.get() ), &cursor );
        CHECK( cursor.col == 0 );
    }
}
