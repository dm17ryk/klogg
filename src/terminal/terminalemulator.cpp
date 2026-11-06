#include "terminalemulator.h"
#include "cilogg_vterm_limits.h"
#include <QLoggingCategory>
#include <algorithm>
#include <limits>
#include <thread>
#include <vterm.h>

Q_LOGGING_CATEGORY( terminalModelLog, "cilogg.terminal.model", QtWarningMsg )

namespace terminal {
namespace {
bool validSize( Size size )
{
    return size.columns > 0 && size.columns <= TerminalEmulator::MaxColumns && size.rows > 0
           && size.rows <= TerminalEmulator::MaxRows;
}
Result failure( Status status, const char* reason )
{
    qCDebug( terminalModelLog, "decision=rejected status=%d reason=%s", int( status ), reason );
    return { status, 0, QString::fromLatin1( reason ) };
}
quint32 rgb( VTermScreen* screen, VTermColor color )
{
    vterm_screen_convert_color_to_rgb( screen, &color );
    return ( quint32( color.rgb.red ) << 16 ) | ( quint32( color.rgb.green ) << 8 )
           | color.rgb.blue;
}
} // namespace
struct TerminalEmulator::Impl {
    const std::thread::id owner = std::this_thread::get_id();
    std::unique_ptr<VTerm, decltype( &vterm_free )> vt{ nullptr, &vterm_free };
    VTermScreen* screen = nullptr;
    Size size;
    quint64 revision = 1;
    QByteArray replies;
    QByteArray encoded;
    bool encoding = false;
    bool alternate = false;
    bool visible = true;
    QString limitReason;
    quint64 damageCells = 0;
    bool onOwner() const
    {
        return owner == std::this_thread::get_id();
    }
    Result ready() const
    {
        if ( !onOwner() )
            return failure( Status::WrongThread, "owner_thread_required" );
        if ( !vt )
            return failure( Status::InvalidRequest, "invalid_initial_size" );
        if ( !limitReason.isEmpty() )
            return { Status::ResourceLimit, 0, limitReason };
        return {};
    }
    void checkLimit()
    {
        if ( !vt || !limitReason.isEmpty() )
            return;
        switch ( cilogg_vterm_resource_limit( vt.get() ) ) {
        case CILOGG_VTERM_LIMIT_NONE:
            return;
        case CILOGG_VTERM_LIMIT_STRING:
            limitReason = QStringLiteral( "control_string_limit" );
            break;
        case CILOGG_VTERM_LIMIT_PARAMETER:
            limitReason = QStringLiteral( "numeric_parameter_limit" );
            break;
        case CILOGG_VTERM_LIMIT_ARGUMENTS:
            limitReason = QStringLiteral( "parameter_count_limit" );
            break;
        default:
            limitReason = QStringLiteral( "backend_resource_limit" );
            break;
        }
        qCDebug( terminalModelLog, "decision=limited revision=%llu reason=%s",
                 static_cast<unsigned long long>( revision ), qPrintable( limitReason ) );
    }
    static void output( const char* bytes, size_t length, void* user )
    {
        auto& self = *static_cast<Impl*>( user );
        if ( !self.limitReason.isEmpty() )
            return;
        auto& target = self.encoding ? self.encoded : self.replies;
        if ( length > size_t( MaxReplyBytes - target.size() ) ) {
            self.limitReason = QStringLiteral( "reply_queue_limit" );
            qCDebug( terminalModelLog,
                     "decision=limited reason=reply_queue_limit queued=%lld incoming=%llu",
                     static_cast<long long>( target.size() ),
                     static_cast<unsigned long long>( length ) );
            return;
        }
        target.append( bytes, int( length ) );
    }
    static int damage( VTermRect rect, void* user )
    {
        auto& self = *static_cast<Impl*>( user );
        self.damageCells += quint64( std::max( 0, rect.end_row - rect.start_row ) )
                            * quint64( std::max( 0, rect.end_col - rect.start_col ) );
        return 1;
    }
    static int move( VTermRect dest, VTermRect, void* user )
    {
        return damage( dest, user );
    }
    static int property( VTermProp prop, VTermValue* value, void* user )
    {
        auto& self = *static_cast<Impl*>( user );
        if ( prop == VTERM_PROP_ALTSCREEN )
            self.alternate = value->boolean != 0;
        if ( prop == VTERM_PROP_CURSORVISIBLE )
            self.visible = value->boolean != 0;
        // Titles, clipboard requests and other strings have no external side effects.
        return 1;
    }
    Result create( Size requested )
    {
        if ( !onOwner() )
            return failure( Status::WrongThread, "owner_thread_required" );
        if ( !validSize( requested ) )
            return failure( Status::InvalidRequest, "size_limit" );
        vt.reset( vterm_new( requested.rows, requested.columns ) );
        size = requested;
        replies.clear();
        encoded.clear();
        limitReason.clear();
        alternate = false;
        visible = true;
        encoding = false;
        vterm_set_utf8( vt.get(), 1 );
        vterm_output_set_callback( vt.get(), &Impl::output, this );
        screen = vterm_obtain_screen( vt.get() );
        static const VTermScreenCallbacks callbacks = [] {
            VTermScreenCallbacks value{};
            value.damage = &Impl::damage;
            value.moverect = &Impl::move;
            value.settermprop = &Impl::property;
            return value;
        }();
        vterm_screen_set_callbacks( screen, &callbacks, this );
        vterm_screen_set_damage_merge( screen, VTERM_DAMAGE_CELL );
        vterm_screen_enable_altscreen( screen, 1 );
        vterm_screen_enable_reflow( screen, false );
        // Do not install selection/scrollback callbacks: no clipboard or transcript owner.
        vterm_screen_reset( screen, 1 );
        ++revision;
        qCDebug( terminalModelLog, "decision=created revision=%llu columns=%d rows=%d",
                 static_cast<unsigned long long>( revision ), size.columns, size.rows );
        return {};
    }
};
TerminalEmulator::TerminalEmulator( Size size )
    : impl_( std::make_unique<Impl>() )
{
    impl_->create( size );
}
TerminalEmulator::~TerminalEmulator() = default;
Result TerminalEmulator::feed( const QByteArray& bytes )
{
    auto result = impl_->ready();
    if ( result.status != Status::Ok )
        return result;
    if ( bytes.size() > MaxInputBytes )
        return failure( Status::InvalidRequest, "input_chunk_limit" );
    const int limit = std::min( int( bytes.size() ), InputQuantumBytes );
    impl_->damageCells = 0;
    // Byte admission preserves exact suffix ownership and UTF8/parser continuation.
    for ( int n = 0; n < limit; ++n ) {
        vterm_input_write( impl_->vt.get(), bytes.constData() + n, 1 );
        ++result.consumedBytes;
        impl_->checkLimit();
        if ( !impl_->limitReason.isEmpty() || impl_->damageCells >= 1000000 )
            break;
    }
    if ( result.consumedBytes )
        ++impl_->revision;
    if ( !impl_->limitReason.isEmpty() ) {
        result.status = Status::ResourceLimit;
        result.reason = impl_->limitReason;
    }
    else if ( result.consumedBytes < bytes.size() ) {
        result.status = Status::Yielded;
        result.reason = QStringLiteral( "cooperative_slice" );
    }
    qCDebug( terminalModelLog,
             "decision=feed status=%d revision=%llu admitted=%lld consumed=%d damage=%llu",
             int( result.status ), static_cast<unsigned long long>( impl_->revision ),
             static_cast<long long>( bytes.size() ), result.consumedBytes,
             static_cast<unsigned long long>( impl_->damageCells ) );
    return result;
}
Result TerminalEmulator::resize( Size size )
{
    auto result = impl_->ready();
    if ( result.status != Status::Ok )
        return result;
    if ( !validSize( size ) )
        return failure( Status::InvalidRequest, "size_limit" );
    vterm_set_size( impl_->vt.get(), size.rows, size.columns );
    impl_->size = size;
    ++impl_->revision;
    qCDebug( terminalModelLog, "decision=resized revision=%llu columns=%d rows=%d",
             static_cast<unsigned long long>( impl_->revision ), size.columns, size.rows );
    return {};
}
Result TerminalEmulator::reset( Size size )
{
    return impl_->create( size );
}
Snapshot TerminalEmulator::snapshot( int first, int count, quint64 expected ) const
{
    Snapshot page;
    if ( !impl_->onOwner() ) {
        page.result = failure( Status::WrongThread, "owner_thread_required" );
        return page;
    }
    if ( !impl_->vt ) {
        page.result = impl_->ready();
        return page;
    }
    if ( first < 0 || first >= impl_->size.rows * impl_->size.columns || count <= 0
         || count > MaxPageCells ) {
        page.result = failure( Status::InvalidRequest, "page_limit" );
        return page;
    }
    if ( expected && expected != impl_->revision ) {
        page.result = failure( Status::StaleRevision, "snapshot_revision_changed" );
        return page;
    }
    page.result = impl_->ready();
    page.revision = impl_->revision;
    page.size = impl_->size;
    page.firstCell = first;
    page.alternateScreen = impl_->alternate;
    page.cursorVisible = impl_->visible;
    VTermPos cursor{};
    vterm_state_get_cursorpos( vterm_obtain_state( impl_->vt.get() ), &cursor );
    page.cursorRow = cursor.row;
    page.cursorColumn = cursor.col;
    const int end = std::min( first + count, impl_->size.rows * impl_->size.columns );
    page.cells.reserve( end - first );
    for ( int at = first; at < end; ++at ) {
        VTermScreenCell raw{};
        vterm_screen_get_cell( impl_->screen,
                               { at / impl_->size.columns, at % impl_->size.columns }, &raw );
        Cell cell;
        const bool continuation = raw.chars[ 0 ] == std::numeric_limits<uint32_t>::max();
        cell.width = continuation ? 0 : raw.width;
        if ( !continuation ) {
            int used = 0;
            while ( used < VTERM_MAX_CHARS_PER_CELL && raw.chars[ used ] )
                ++used;
            cell.capacityReached = used == VTERM_MAX_CHARS_PER_CELL;
            for ( int n = 0; n < used; ++n ) {
                uint32_t codepoint = raw.chars[ n ];
                if ( codepoint > 0x10ffff || ( codepoint >= 0xd800 && codepoint <= 0xdfff ) )
                    codepoint = 0xfffd;
                if ( codepoint <= 0xffff )
                    cell.text.append( QChar( ushort( codepoint ) ) );
                else {
                    cell.text.append( QChar( QChar::highSurrogate( codepoint ) ) );
                    cell.text.append( QChar( QChar::lowSurrogate( codepoint ) ) );
                }
            }
            if ( !used )
                cell.text = QStringLiteral( " " );
        }
        cell.foreground = rgb( impl_->screen, raw.fg );
        cell.background = rgb( impl_->screen, raw.bg );
        cell.bold = raw.attrs.bold;
        cell.italic = raw.attrs.italic;
        cell.reverse = raw.attrs.reverse;
        cell.conceal = raw.attrs.conceal;
        cell.underline = raw.attrs.underline;
        page.cells.append( cell );
    }
    if ( end < impl_->size.rows * impl_->size.columns )
        page.nextCell = end;
    qCDebug( terminalModelLog, "decision=snapshot revision=%llu first=%d cells=%lld",
             static_cast<unsigned long long>( page.revision ), first,
             static_cast<long long>( page.cells.size() ) );
    return page;
}
QByteArray TerminalEmulator::takeReplies()
{
    if ( !impl_->onOwner() ) {
        failure( Status::WrongThread, "owner_thread_required" );
        return {};
    }
    QByteArray replies;
    replies.swap( impl_->replies );
    qCDebug( terminalModelLog, "decision=replies_taken bytes=%lld",
             static_cast<long long>( replies.size() ) );
    return replies;
}
EncodedInput TerminalEmulator::encodeKey( Key key, bool shift, bool control, bool alt )
{
    EncodedInput input;
    input.result = impl_->ready();
    if ( input.result.status != Status::Ok )
        return input;
    VTermKey native = VTERM_KEY_NONE;
    switch ( key ) {
    case Key::Enter:
        native = VTERM_KEY_ENTER;
        break;
    case Key::Backspace:
        native = VTERM_KEY_BACKSPACE;
        break;
    case Key::Tab:
        native = VTERM_KEY_TAB;
        break;
    case Key::Escape:
        native = VTERM_KEY_ESCAPE;
        break;
    case Key::Up:
        native = VTERM_KEY_UP;
        break;
    case Key::Down:
        native = VTERM_KEY_DOWN;
        break;
    case Key::Left:
        native = VTERM_KEY_LEFT;
        break;
    case Key::Right:
        native = VTERM_KEY_RIGHT;
        break;
    case Key::Home:
        native = VTERM_KEY_HOME;
        break;
    case Key::End:
        native = VTERM_KEY_END;
        break;
    case Key::Delete:
        native = VTERM_KEY_DEL;
        break;
    default:
        input.result = failure( Status::InvalidRequest, "unsupported_key" );
        return input;
    }
    impl_->encoding = true;
    impl_->encoded.clear();
    const int mods = ( shift ? VTERM_MOD_SHIFT : 0 ) | ( control ? VTERM_MOD_CTRL : 0 )
                     | ( alt ? VTERM_MOD_ALT : 0 );
    vterm_keyboard_key( impl_->vt.get(), native, VTermModifier( mods ) );
    impl_->encoding = false;
    input.result = impl_->ready();
    if ( input.result.status == Status::Ok )
        input.bytes.swap( impl_->encoded );
    return input;
}
EncodedInput TerminalEmulator::encodePaste( const QString& text )
{
    EncodedInput input;
    input.result = impl_->ready();
    if ( input.result.status != Status::Ok )
        return input;
    if ( text.size() > MaxInputBytes || text.toUtf8().size() > MaxReplyBytes - 32 ) {
        input.result = failure( Status::InvalidRequest, "paste_byte_limit" );
        return input;
    }
    impl_->encoding = true;
    impl_->encoded.clear();
    vterm_keyboard_start_paste( impl_->vt.get() );
    for ( auto codepoint : text.toUcs4() )
        vterm_keyboard_unichar( impl_->vt.get(), uint32_t( codepoint ), VTERM_MOD_NONE );
    vterm_keyboard_end_paste( impl_->vt.get() );
    impl_->encoding = false;
    input.result = impl_->ready();
    if ( input.result.status == Status::Ok )
        input.bytes.swap( impl_->encoded );
    qCDebug( terminalModelLog, "decision=paste_encoded bytes=%lld status=%d",
             static_cast<long long>( input.bytes.size() ), int( input.result.status ) );
    return input;
}
} // namespace terminal
