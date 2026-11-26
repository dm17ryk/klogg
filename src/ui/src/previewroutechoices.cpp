// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewroutechoices.h"

#include "log.h"
#include "previewassembly.h"
#include "previewdecodersettings.h"
#include "previewmanager.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

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
    while ( j < source.size() && source[ j ] != quote && source[ j ] != '\n' ) {
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
    const auto end = source.indexOf( '\n', i );
    return end < 0 ? source.size() : end + 1;
}
// The name in `Proto("<name>", ...)` or `Proto.new("<name>", ...)`, from just after `Proto`.
QString declaredName( const QString& source, qsizetype i )
{
    auto j = skipSpace( source, i );
    if ( j < source.size() && source[ j ] == '.' ) {
        j = skipSpace( source, j + 1 );
        if ( source.mid( j, 3 ) != QLatin1String( "new" )
             || ( j + 3 < source.size() && identifierChar( source[ j + 3 ] ) ) )
            return {};
        j = skipSpace( source, j + 3 );
    }
    if ( j >= source.size() || source[ j ] != '(' )
        return {};
    j = skipSpace( source, j + 1 );
    if ( j >= source.size() || ( source[ j ] != '"' && source[ j ] != '\'' ) )
        return {};
    QString name;
    bool closed = false;
    skipQuoted( source, j, &name, &closed );
    // Wireshark registers the lower-case name (Proto.new: g_ascii_strdown).
    return closed ? name.toLower() : QString{};
}
void appendUnique( QStringList* list, const QStringList& values )
{
    for ( const auto& value : values )
        if ( !list->contains( value ) )
            list->append( value );
}
} // namespace

QStringList luaProtoNames( const QString& source )
{
    QStringList names;
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
                if ( PreviewDecoderSettings::isDissectorName( name ) && !names.contains( name ) )
                    names.append( name );
            }
            previous = source[ end - 1 ];
            i = end;
            continue;
        }
        if ( !c.isSpace() )
            previous = c;
        ++i;
    }
    return names;
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
    QStringList names;
    for ( const auto& line : output.split( '\n' ) ) {
        const auto columns = line.split( '\t' );
        if ( columns.size() < 3 )
            continue;
        const auto name = QString::fromUtf8( columns[ 2 ] ).trimmed();
        if ( PreviewDecoderSettings::isDissectorName( name ) && !names.contains( name ) )
            names.append( name );
    }
    std::sort( names.begin(), names.end(), []( const QString& left, const QString& right ) {
        return left.compare( right, Qt::CaseInsensitive ) < 0;
    } );
    return names;
}

QStringList Choices::luaNames() const
{
    QStringList names;
    for ( const auto& file : lua )
        appendUnique( &names, file.names );
    return names;
}
QStringList Choices::tsharkDissectors() const
{
    auto names = luaNames();
    appendUnique( &names, tshark );
    return names;
}
QStringList Choices::routeTargets() const
{
    auto names = luaNames();
    appendUnique( &names, python );
    appendUnique( &names, tshark );
    return names;
}

Choices collect( const QStringList& userLua )
{
    Choices choices;
    const auto& manager = PreviewManager::instance();
    for ( const auto& preview : manager.all() )
        appendUnique( &choices.previews, { preview.name } );
    for ( const auto& preview : manager.packPreviews() )
        appendUnique( &choices.previews, { preview.name } );
    choices.assemblyProtocols = assembledPreviewProtocols();
    for ( const auto& path : userLua )
        choices.lua.append( { path, luaProtoNamesInFile( path ) } );
    const auto packs = PreviewDecoderSettings::instance().packs();
    for ( const auto& pack : packs.lua ) {
        for ( const auto& path : pack.toMap().value( "files" ).toStringList() )
            choices.lua.append( { path, luaProtoNamesInFile( path ) } );
    }
    for ( const auto& pack : packs.python ) {
        const auto entry = pack.toMap();
        for ( const auto& decoder : entry.value( "decoders" ).toList() ) {
            appendUnique( &choices.python, { PythonPrefix + entry.value( "id" ).toString() + '/'
                                             + decoder.toMap().value( "id" ).toString() } );
        }
    }
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
    else if ( choices.tsharkPending ) {
        // TShark's list, still loading, may know it: the check runs again once it is there.
    }
    else if ( choices.tsharkLoaded ) {
        warning.text = tr( "`%1` is not declared by a selected Lua file, and TShark does not "
                           "list it as a protocol." )
                           .arg( name );
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
            if ( !choices.python.contains( PythonPrefix + target ) ) {
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
