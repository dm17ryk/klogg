// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewroutechoices.h"

#include "log.h"
#include "previewassembly.h"
#include "previewdecodersettings.h"
#include "previewmanager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <optional>

namespace previewroutes {
namespace {
// The settings' limit for a selected Lua file.
constexpr qint64 LuaFileLimit = 1024 * 1024;
const QString PythonPrefix = QStringLiteral( "python:" );
const QString LuaSuffix = QStringLiteral( ".lua" );

QString tr( const char* text )
{
    return QCoreApplication::translate( "PreviewRouteChoices", text );
}

bool identifierStart( QChar c )
{
    return c.unicode() < 128 && ( c.isLetter() || c == '_' );
}
bool identifierChar( QChar c )
{
    return c.unicode() < 128 && ( c.isLetterOrNumber() || c == '_' );
}
qsizetype skipSpace( const QString& source, qsizetype i )
{
    while ( i < source.size() && source[ i ].isSpace() )
        ++i;
    return i;
}
// The level of the Lua long bracket opening at `i` (`[[` is 0, `[==[` is 2), or -1 for none.
qsizetype longBracket( const QString& source, qsizetype i )
{
    if ( i >= source.size() || source[ i ] != '[' )
        return -1;
    qsizetype j = i + 1;
    while ( j < source.size() && source[ j ] == '=' )
        ++j;
    return j < source.size() && source[ j ] == '[' ? j - i - 1 : -1;
}
// The index after the long bracket of `level` opening at `i` closes, or the end of the source.
qsizetype skipLong( const QString& source, qsizetype i, qsizetype level )
{
    const auto close = QStringLiteral( "]%1]" ).arg( QString( level, '=' ) );
    const auto end = source.indexOf( close, i + level + 2 );
    return end < 0 ? source.size() : end + close.size();
}
// The index after the quoted string opening at `i`. `value` takes its text, without escapes
// resolved (a dissector name has none), and `closed` whether it ends on its line.
qsizetype skipQuoted( const QString& source, qsizetype i, QString* value = nullptr,
                      bool* closed = nullptr )
{
    const QChar quote = source[ i ];
    qsizetype j = i + 1;
    // A line end (LF or CR, as Lua reads them) ends an unterminated string.
    while ( j < source.size() && source[ j ] != quote && source[ j ] != '\n'
            && source[ j ] != '\r' ) {
        if ( source[ j ] != '\\' || j + 1 >= source.size() ) {
            ++j;
            continue;
        }
        // An escape. A backslash before a line end (also CR LF or LF CR) continues the string
        // on the next line, and `\z` skips the white space that follows, line ends included.
        const QChar escaped = source[ j + 1 ];
        j += 2;
        if ( ( escaped == '\r' || escaped == '\n' ) && j < source.size()
             && ( source[ j ] == '\r' || source[ j ] == '\n' ) && source[ j ] != escaped )
            ++j;
        else if ( escaped == 'z' ) {
            while ( j < source.size() && source[ j ].isSpace() )
                ++j;
        }
    }
    const bool ended = j < source.size() && source[ j ] == quote;
    if ( value )
        *value = source.mid( i + 1, j - i - 1 );
    if ( closed )
        *closed = ended;
    return ended ? j + 1 : j;
}
// The index after the comment starting at `i` (on `--`).
qsizetype skipComment( const QString& source, qsizetype i )
{
    const auto level = longBracket( source, i + 2 );
    if ( level >= 0 )
        return skipLong( source, i + 2, level );
    // A line comment ends at a line end: LF or CR, as Lua reads them.
    auto end = i;
    while ( end < source.size() && source[ end ] != '\n' && source[ end ] != '\r' )
        ++end;
    return end < source.size() ? end + 1 : source.size();
}
// The value of a Lua short string's body with its escapes resolved, or nothing for an escape a
// dissector name cannot hold (a line end, `\z`) or a malformed one.
std::optional<QString> unescaped( const QString& raw )
{
    QString value;
    for ( qsizetype i = 0; i < raw.size(); ++i ) {
        if ( raw[ i ] != '\\' ) {
            value += raw[ i ];
            continue;
        }
        if ( ++i >= raw.size() )
            return std::nullopt;
        const auto escape = raw[ i ].unicode();
        if ( escape == 'x' ) {
            bool ok = false;
            const auto code = raw.mid( i + 1, 2 ).toUInt( &ok, 16 );
            if ( !ok || i + 2 >= raw.size() )
                return std::nullopt;
            value += QChar( code );
            i += 2;
        }
        else if ( escape >= '0' && escape <= '9' ) {
            qsizetype digits = 0;
            while ( digits < 3 && i + digits < raw.size() && raw[ i + digits ] >= '0'
                    && raw[ i + digits ] <= '9' )
                ++digits;
            const auto code = raw.mid( i, digits ).toUInt();
            if ( code > 255 )
                return std::nullopt;
            value += QChar( code );
            i += digits - 1;
        }
        else if ( escape == 'u' ) {
            const auto close = raw.indexOf( '}', i );
            bool ok = false;
            const auto code = i + 1 < raw.size() && raw[ i + 1 ] == '{' && close > i + 2
                                  ? raw.mid( i + 2, close - i - 2 ).toUInt( &ok, 16 )
                                  : 0u;
            if ( !ok || code > 0x10FFFF )
                return std::nullopt;
            const auto character = static_cast<char32_t>( code );
            value += QString::fromUcs4( &character, 1 );
            i = close;
        }
        else {
            switch ( escape ) {
            case 'a': value += QChar( 0x07 ); break;
            case 'b': value += QChar( 0x08 ); break;
            case 'f': value += QChar( 0x0C ); break;
            case 'n': value += QChar( 0x0A ); break;
            case 'r': value += QChar( 0x0D ); break;
            case 't': value += QChar( 0x09 ); break;
            case 'v': value += QChar( 0x0B ); break;
            case '\\':
            case '"':
            case '\'': value += raw[ i ]; break;
            case 'z':
                // `\z` skips the white space that follows, line ends included.
                while ( i + 1 < raw.size() && raw[ i + 1 ].isSpace() )
                    ++i;
                break;
            default: return std::nullopt;
            }
        }
    }
    return value;
}
// The index after the white space and comments from `i`: Lua allows comments between tokens.
qsizetype skipBlank( const QString& source, qsizetype i )
{
    for ( ;; ) {
        i = skipSpace( source, i );
        if ( i + 1 >= source.size() || source[ i ] != '-' || source[ i + 1 ] != '-' )
            return i;
        i = skipComment( source, i );
    }
}
// The name in `Proto("<name>", ...)` or `Proto.new("<name>", ...)`, from just after `Proto`.
QString declaredName( const QString& source, qsizetype i )
{
    auto j = skipBlank( source, i );
    if ( j < source.size() && source[ j ] == '.' ) {
        j = skipBlank( source, j + 1 );
        if ( source.mid( j, 3 ) != QLatin1String( "new" )
             || ( j + 3 < source.size() && identifierChar( source[ j + 3 ] ) ) )
            return {};
        j = skipBlank( source, j + 3 );
    }
    if ( j >= source.size() || source[ j ] != '(' )
        return {};
    j = skipBlank( source, j + 1 );
    if ( j >= source.size() || ( source[ j ] != '"' && source[ j ] != '\'' ) )
        return {};
    QString name;
    bool closed = false;
    skipQuoted( source, j, &name, &closed );
    // Lua resolves the string's escapes; Wireshark registers the lower-case name (Proto.new:
    // g_ascii_strdown).
    const auto value = closed ? unescaped( name ) : std::optional<QString>{};
    return value ? value->toLower() : QString{};
}
// Names in the order first added, each once. A set keeps adding linear: a generated Lua file may
// declare thousands of dissectors, and TShark lists some 3000 protocols.
class UniqueNames {
public:
    void add( const QString& name )
    {
        if ( !seen_.contains( name ) ) {
            seen_.insert( name );
            names_.append( name );
        }
    }
    void add( const QStringList& names )
    {
        for ( const auto& name : names )
            add( name );
    }
    const QStringList& names() const
    {
        return names_;
    }

private:
    QSet<QString> seen_;
    QStringList names_;
};
} // namespace

QStringList luaProtoNames( const QString& source )
{
    UniqueNames names;
    QChar previous; // the last character before the current token that was not a space
    qsizetype i = 0;
    while ( i < source.size() ) {
        const QChar c = source[ i ];
        if ( c == '-' && i + 1 < source.size() && source[ i + 1 ] == '-' ) {
            i = skipComment( source, i );
            continue;
        }
        if ( c == '"' || c == '\'' ) {
            i = skipQuoted( source, i );
            previous = c;
            continue;
        }
        if ( const auto level = longBracket( source, i ); level >= 0 ) {
            i = skipLong( source, i, level );
            previous = ']';
            continue;
        }
        if ( identifierStart( c ) ) {
            auto end = i;
            while ( end < source.size() && identifierChar( source[ end ] ) )
                ++end;
            // `tools.Proto(...)` or `tools:Proto(...)` is not Wireshark's global Proto.
            if ( QStringView( source ).mid( i, end - i ) == u"Proto" && previous != '.'
                 && previous != ':' ) {
                const auto name = declaredName( source, end );
                if ( PreviewDecoderSettings::isDissectorName( name ) )
                    names.add( name );
            }
            previous = source[ end - 1 ];
            i = end;
            continue;
        }
        if ( !c.isSpace() )
            previous = c;
        ++i;
    }
    return names.names();
}

QStringList luaProtoNamesInFile( const QString& path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        LOG_DEBUG << "Preview route choices could not read a Lua file";
        return {};
    }
    return luaProtoNames( QString::fromUtf8( file.read( LuaFileLimit ) ) );
}

QStringList tsharkProtocolNames( const QByteArray& output )
{
    UniqueNames unique;
    for ( const auto& line : output.split( '\n' ) ) {
        const auto columns = line.split( '\t' );
        if ( columns.size() < 3 )
            continue;
        const auto name = QString::fromUtf8( columns[ 2 ] ).trimmed();
        if ( PreviewDecoderSettings::isDissectorName( name ) )
            unique.add( name );
    }
    auto names = unique.names();
    std::sort( names.begin(), names.end(), []( const QString& left, const QString& right ) {
        return left.compare( right, Qt::CaseInsensitive ) < 0;
    } );
    return names;
}

QStringList Choices::luaNames() const
{
    UniqueNames names;
    for ( const auto& file : lua )
        names.add( file.names );
    return names.names();
}
QStringList Choices::tsharkDissectors() const
{
    UniqueNames names;
    names.add( luaNames() );
    names.add( tshark );
    return names.names();
}
QStringList Choices::routeTargets() const
{
    UniqueNames names;
    names.add( luaNames() );
    names.add( python );
    names.add( tshark );
    return names.names();
}

namespace {
QHash<QString, QStringList>& luaNameCache()
{
    static QHash<QString, QStringList> cache;
    return cache;
}
// The names a Lua file declares, read once per version of it (path, size and time): the dialog
// collects its choices on opening, on Apply and when the packs change, and the selected and pack
// files together may be 32 files of up to 1 MiB.
QStringList cachedLuaNames( const QString& path )
{
    constexpr int CacheLimit = 256;
    auto& cache = luaNameCache();
    const QFileInfo info( path );
    const auto key = QStringLiteral( "%1|%2|%3" )
                         .arg( info.absoluteFilePath() )
                         .arg( info.size() )
                         .arg( info.lastModified().toMSecsSinceEpoch() );
    if ( const auto found = cache.constFind( key ); found != cache.cend() )
        return *found;
    if ( cache.size() >= CacheLimit )
        cache.clear();
    const auto names = luaProtoNamesInFile( path );
    cache.insert( key, names );
    return names;
}
} // namespace

void forgetLuaNames()
{
    luaNameCache().clear();
}

Choices collect( const QStringList& userLua )
{
    Choices choices;
    const auto& manager = PreviewManager::instance();
    UniqueNames previews;
    for ( const auto& preview : manager.all() )
        previews.add( preview.name );
    for ( const auto& preview : manager.packPreviews() )
        previews.add( preview.name );
    choices.previews = previews.names();
    choices.assemblyProtocols = assembledPreviewProtocols();
    for ( const auto& path : userLua )
        choices.lua.append( { path, cachedLuaNames( path ) } );
    const auto packs = PreviewDecoderSettings::instance().packs();
    for ( const auto& pack : packs.lua ) {
        for ( const auto& path : pack.toMap().value( "files" ).toStringList() )
            choices.lua.append( { path, cachedLuaNames( path ) } );
    }
    // Only the modules Python's check found a decode() in are decoders; the others are helpers
    // the pack lists so the worker copies them (E16b).
    UniqueNames modules;
    UniqueNames decoders;
    for ( const auto& pack : packs.python ) {
        const auto entry = pack.toMap();
        const auto packId = entry.value( "id" ).toString();
        const auto entries = packs.pythonEntries.value( packId ).toStringList();
        for ( const auto& decoder : entry.value( "decoders" ).toList() ) {
            const auto decoderId = decoder.toMap().value( "id" ).toString();
            const auto target = PythonPrefix + packId + '/' + decoderId;
            modules.add( target );
            if ( entries.contains( decoderId ) )
                decoders.add( target );
        }
    }
    choices.pythonModules = modules.names();
    choices.python = decoders.names();
    LOG_DEBUG << "Preview route choices collected previews=" << choices.previews.size()
              << " lua_files=" << choices.lua.size() << " lua_names=" << choices.luaNames().size()
              << " python=" << choices.python.size();
    return choices;
}

namespace {
QString quoted( const QStringList& names )
{
    return QStringLiteral( "`%1`" ).arg( names.join( QStringLiteral( "`, `" ) ) );
}
// The Lua file `name` refers to by its file name or base name, among `choices`; one that declares
// a name comes first.
const LuaFile* luaFileNamed( const Choices& choices, const QString& name )
{
    const LuaFile* found = nullptr;
    for ( const auto& file : choices.lua ) {
        const QFileInfo info( file.path );
        if ( info.fileName().compare( name, Qt::CaseInsensitive ) != 0
             && info.completeBaseName().compare( name, Qt::CaseInsensitive ) != 0 )
            continue;
        if ( !file.names.isEmpty() )
            return &file;
        found = found ? found : &file;
    }
    return found;
}
// The warning for the dissector `name`, or a warning without text when it is declared or known.
Warning dissectorWarning( const QString& field, const QString& name, const Choices& choices )
{
    Warning warning{ field, name, {}, {} };
    const auto declared = choices.luaNames();
    const auto known = [ & ]( const QString& value ) {
        return declared.contains( value ) || choices.tshark.contains( value );
    };
    if ( name.isEmpty() || known( name ) )
        return warning;
    // While TShark is listing, any name but a Lua file's could be one of its protocols (also a
    // Lua file's base name, or a name in other case): it waits for the list.
    if ( choices.tsharkPending && !name.endsWith( LuaSuffix, Qt::CaseInsensitive ) )
        return warning;
    const auto fileText = tr( "`%1` looks like a file name; the Lua file registers %2." );
    if ( known( name.toLower() ) ) {
        warning.suggestion = name.toLower();
        warning.text = tr( "`%1`: dissector names are lower case; use `%2`." )
                           .arg( name, warning.suggestion );
    }
    else if ( const auto* file = luaFileNamed( choices, name ); file && !file->names.isEmpty() ) {
        warning.suggestion = file->names.first();
        warning.text = fileText.arg( name, quoted( file->names ) );
    }
    else if ( file ) {
        // A name built at run time (`Proto(NAME, ...)`) cannot be read from the file.
        warning.text = tr( "`%1` looks like a file name; use the name the Lua file registers "
                           "with Proto(\"<name>\", ...)." )
                           .arg( name );
    }
    else if ( name.endsWith( LuaSuffix, Qt::CaseInsensitive )
              && declared.contains( name.chopped( LuaSuffix.size() ).toLower() ) ) {
        warning.suggestion = name.chopped( LuaSuffix.size() ).toLower();
        warning.text = fileText.arg( name, quoted( { warning.suggestion } ) );
    }
    else if ( name.endsWith( LuaSuffix, Qt::CaseInsensitive ) ) {
        warning.text = tr( "`%1` looks like a file name, but no selected Lua file of that name "
                           "registers a dissector." )
                           .arg( name );
    }
    else if ( choices.tsharkLoaded ) {
        warning.text = tr( "`%1` is not declared by a selected Lua file, and TShark does not "
                           "list it as a protocol (it may still be a dissector name)." )
                           .arg( name );
    }
    else if ( choices.tsharkDisabled ) {
        // No TShark route runs while TShark is disabled; its list would settle the name.
    }
    else if ( !choices.tsharkFailure.isEmpty() ) {
        warning.text = tr( "`%1` is not declared by a selected Lua file; TShark's protocol list "
                           "is not loaded to check it (%2)." )
                           .arg( name, choices.tsharkFailure );
    }
    else {
        warning.text = tr( "`%1` is not declared by a selected Lua file; TShark's protocol list "
                           "is not loaded to check it." )
                           .arg( name );
    }
    return warning;
}
void checkRoutes( const QVariantMap& routes, const QString& label, const Choices& choices,
                  QVector<Warning>* warnings )
{
    for ( auto route = routes.cbegin(); route != routes.cend(); ++route ) {
        const auto entry = route.value().toMap();
        const auto field = label.arg( route.key() );
        if ( entry.contains( "python" ) ) {
            const auto target = entry.value( "python" ).toString();
            // Routing runs any module an enabled pack lists; whether Python's check offers it as
            // a decoder is told by the route's tooltip.
            if ( !choices.pythonModules.contains( PythonPrefix + target ) ) {
                warnings->append(
                    { field, target, {},
                      tr( "`python:%1`: no enabled pack has this decoder, so the route does "
                          "not apply." )
                          .arg( target ) } );
            }
            continue;
        }
        auto warning = dissectorWarning( field, entry.value( "dissector" ).toString(), choices );
        if ( !warning.text.isEmpty() )
            warnings->append( warning );
    }
}
} // namespace

QVector<Warning> check( const QVariantMap& configuration, const Choices& choices )
{
    QVector<Warning> warnings;
    const auto fallback = dissectorWarning( tr( "Default named dissector" ),
                                            configuration.value( "defaultDissector" ).toString(),
                                            choices );
    if ( !fallback.text.isEmpty() )
        warnings.append( fallback );
    checkRoutes( configuration.value( "previewRoutes" ).toMap(), tr( "Preview route \"%1\"" ),
                 choices, &warnings );
    checkRoutes( configuration.value( "protocolRoutes" ).toMap(),
                 tr( "Assembly protocol route \"%1\"" ), choices, &warnings );
    LOG_DEBUG << "Preview route check warnings=" << warnings.size()
              << " tshark_loaded=" << choices.tsharkLoaded;
    return warnings;
}

} // namespace previewroutes
