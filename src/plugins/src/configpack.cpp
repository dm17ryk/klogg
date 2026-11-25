#include "configpack.h"

#include "packcanonical.h"
#include "pluginmanifest.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace cilogg::plugins {
namespace {
const QStringList FileKeys{ "previews", "actions", "highlights", "filters" };
const QStringList ContentKeys{ "previews", "actions",         "highlights",     "filters",
                               "lua",      "decoder_routes", "python_decoders" };
constexpr int MaxContentPathLength = 256;

bool dissectorName( const QString& value )
{
    static const QRegularExpression pattern( "\\A[A-Za-z][A-Za-z0-9_.-]{0,127}\\z" );
    return pattern.match( value ).hasMatch();
}

bool encodingName( const QString& value )
{
    return value == "utf8" || value == "hex" || value == "base64";
}

bool contentPath( const QJsonValue& value, const QString& suffix = {} )
{
    const auto text = value.toString();
    return value.isString() && text.size() <= MaxContentPathLength && safeRelativePath( text )
           && ( suffix.isEmpty() || text.endsWith( suffix, Qt::CaseInsensitive ) );
}

// The rules of PreviewDecoderSettings for the user's routes, so pack routes merge cleanly.
QString validateRoutes( const QJsonObject& routes, bool protocol )
{
    const auto limit = protocol ? MaxPackProtocolRoutes : MaxPackPreviewRoutes;
    const auto key = protocol ? QStringLiteral( "protocolRoutes" ) : QStringLiteral( "previewRoutes" );
    if ( routes.size() > limit ) {
        return QStringLiteral( "Too many decoder_routes.%1 (maximum %2)." ).arg( key ).arg( limit );
    }
    for ( auto route = routes.begin(); route != routes.end(); ++route ) {
        const auto entry = route.value().toObject();
        bool valid = !route.key().isEmpty() && route.key().size() <= ( protocol ? 128 : 4096 )
                     && !route.key().contains( QChar::Null ) && route.value().isObject()
                     && dissectorName( entry.value( "dissector" ).toString() );
        for ( auto item = entry.begin(); valid && item != entry.end(); ++item ) {
            valid = item.value().isString()
                    && ( item.key() == "dissector" || item.key() == "protocol"
                         || ( protocol && item.key() == "inputEncoding"
                              && encodingName( item.value().toString() ) ) );
        }
        const auto selector = entry.value( "protocol" ).toString();
        if ( !valid || selector.size() > 128 || selector.contains( QChar::Null ) ) {
            return QStringLiteral( "Invalid decoder_routes.%1 entry: %2" )
                .arg( key, route.key().left( 128 ) );
        }
    }
    return {};
}

QString validateLua( const QJsonValue& value, PackContent* content )
{
    const auto lua = value.toObject();
    if ( !value.isObject() || !contentPath( lua.value( "folder" ) )
         || !lua.value( "load" ).isArray() ) {
        return QStringLiteral( "content.lua needs a folder and a load list." );
    }
    for ( auto key = lua.begin(); key != lua.end(); ++key ) {
        if ( key.key() != "folder" && key.key() != "load" ) {
            return QStringLiteral( "Unknown content.lua key: %1" ).arg( key.key().left( 64 ) );
        }
    }
    const auto load = lua.value( "load" ).toArray();
    if ( load.isEmpty() || load.size() > MaxPackLuaFiles ) {
        return QStringLiteral( "content.lua.load lists 1 to %1 files." ).arg( MaxPackLuaFiles );
    }
    QSet<QString> seen;
    for ( const auto& file : load ) {
        if ( !contentPath( file, ".lua" ) || seen.contains( file.toString().toLower() ) ) {
            return QStringLiteral( "Invalid or repeated Lua file: %1" )
                .arg( file.toString().left( 128 ) );
        }
        seen.insert( file.toString().toLower() );
        content->luaLoad.append( file.toString() );
    }
    content->luaFolder = lua.value( "folder" ).toString();
    return {};
}

QString validatePythonDecoders( const QJsonValue& value, PackContent* content )
{
    static const QRegularExpression idPattern(
        QRegularExpression::anchoredPattern( "[a-z0-9][a-z0-9._-]{0,63}" ) );
    const auto decoders = value.toArray();
    if ( !value.isArray() || decoders.size() > MaxPackPythonDecoders ) {
        return QStringLiteral( "content.python_decoders is a list of at most %1 decoders." )
            .arg( MaxPackPythonDecoders );
    }
    QSet<QString> ids;
    for ( const auto& item : decoders ) {
        const auto decoder = item.toObject();
        const auto id = decoder.value( "id" ).toString();
        if ( !item.isObject() || !idPattern.match( id ).hasMatch() || ids.contains( id )
             || !contentPath( decoder.value( "module" ), ".py" ) ) {
            return QStringLiteral( "Invalid or repeated Python decoder: %1" ).arg( id.left( 64 ) );
        }
        ids.insert( id );
        content->pythonDecoders.append( { id, decoder.value( "module" ).toString() } );
    }
    return {};
}

// CILogg writes a pack's plugin.json and previews document itself (the effective content, with
// the local edits, design 4.6), so no other content file may be either of them: it would replace
// what CILogg wrote. Paths compare as the file systems of Windows and macOS do, ignoring case.
QString checkReservedPaths( const PackContent& content )
{
    const auto same = []( const QString& a, const QString& b ) {
        return QDir::cleanPath( a ).compare( QDir::cleanPath( b ), Qt::CaseInsensitive ) == 0;
    };
    const auto manifest = QStringLiteral( "plugin.json" );
    if ( !content.previews.isEmpty() && same( content.previews, manifest ) ) {
        return QStringLiteral( "content.previews cannot be plugin.json." );
    }
    // The files carried as they are; the filters file is written from the pack's filter items.
    QStringList raw{ content.actions, content.highlights };
    for ( const auto& file : content.luaLoad ) {
        raw.append( content.luaFolder + '/' + file );
    }
    for ( const auto& decoder : content.pythonDecoders ) {
        raw.append( decoder.module );
    }
    auto others = raw;
    others.append( content.filters );
    for ( const auto& path : others ) {
        if ( !path.isEmpty()
             && ( same( path, manifest ) || ( !content.previews.isEmpty() && same( path, content.previews ) ) ) ) {
            return QStringLiteral( "The content file %1 is the pack's plugin.json or previews file." )
                .arg( path.left( 128 ) );
        }
    }
    // One file is named one way: spellings that differ only in case are one file on Windows and
    // macOS but two items in the pack's store, and writing one would overwrite the other.
    for ( qsizetype first = 0; first < others.size(); ++first ) {
        for ( qsizetype second = first + 1; second < others.size(); ++second ) {
            const auto& a = others.at( first );
            const auto& b = others.at( second );
            if ( !a.isEmpty() && same( a, b ) && QDir::cleanPath( a ) != QDir::cleanPath( b ) ) {
                return QStringLiteral( "The content files %1 and %2 differ only in case." )
                    .arg( a.left( 128 ), b.left( 128 ) );
            }
        }
    }
    // CILogg writes the filters file from the pack's filter items (E16c), so no file carried as it
    // is may be that file too: written after it, it would replace the filters and their edits.
    if ( !content.filters.isEmpty() ) {
        for ( const auto& path : raw ) {
            if ( !path.isEmpty() && same( path, content.filters ) ) {
                return QStringLiteral( "The content file %1 is the pack's filters file." )
                    .arg( path.left( 128 ) );
            }
        }
    }
    return {};
}

// Whether `relative` resolves, after every link and junction on the way, to the same place
// inside the package folder: a link anywhere on the path would lead elsewhere.
bool resolvesInside( const QString& directory, const QString& relative )
{
    const auto root = QFileInfo( directory ).canonicalFilePath();
    const auto resolved = QFileInfo( QDir( directory ).filePath( relative ) ).canonicalFilePath();
    return !root.isEmpty() && !resolved.isEmpty()
           && QDir( root ).relativeFilePath( resolved ).compare( QDir::cleanPath( relative ),
                                                                Qt::CaseInsensitive )
                  == 0;
}

// A regular file inside the package (not a link, not reached through one) within the size bound.
QString checkFile( const QString& directory, const QString& relative )
{
    const QFileInfo info( QDir( directory ).filePath( relative ) );
    if ( !info.exists() || !info.isFile() || info.isSymLink() ) {
        return QStringLiteral( "Pack file missing: %1" ).arg( relative.left( 128 ) );
    }
    if ( !resolvesInside( directory, relative ) ) {
        return QStringLiteral( "Pack file outside the pack or reached through a link: %1" )
            .arg( relative.left( 128 ) );
    }
    if ( info.size() > MaxPackFileBytes ) {
        return QStringLiteral( "Pack file larger than 1 MiB: %1" ).arg( relative.left( 128 ) );
    }
    return {};
}
} // namespace

QStringList PackContent::luaFiles( const QString& directory ) const
{
    QStringList files;
    const QDir folder( QDir( directory ).filePath( luaFolder ) );
    for ( const auto& file : luaLoad ) {
        files.append( QDir::cleanPath( folder.absoluteFilePath( file ) ) );
    }
    return files;
}

bool isConfigPack( const QJsonObject& manifest )
{
    return manifest.value( "kind" ).toString() == QLatin1String( ConfigPackKind );
}

QStringList packProcessKeys()
{
    return { "capabilities", "executable", "arguments",         "actions",
             "views",        "configuration", "enabled_by_default" };
}

QString validatePackManifest( const QJsonObject& manifest, PackContent* content )
{
    PackContent parsed;
    for ( const auto& key : packProcessKeys() ) {
        if ( manifest.contains( key ) ) {
            return QStringLiteral( "A configuration pack cannot declare %1: it has no process." )
                .arg( key );
        }
    }
    if ( manifest.value( "pack_format" ) != QJsonValue( PackFormat ) ) {
        return QStringLiteral( "Unsupported pack_format (this CILogg reads pack_format %1)." )
            .arg( PackFormat );
    }
    const auto body = manifest.value( "content" ).toObject();
    if ( !manifest.value( "content" ).isObject() || body.isEmpty() ) {
        return QStringLiteral( "A configuration pack needs a content object." );
    }
    for ( auto item = body.begin(); item != body.end(); ++item ) {
        if ( !ContentKeys.contains( item.key() ) ) {
            return QStringLiteral( "Unknown pack content: %1" ).arg( item.key().left( 64 ) );
        }
    }
    for ( const auto& key : FileKeys ) {
        if ( body.contains( key ) && !contentPath( body.value( key ) ) ) {
            return QStringLiteral( "content.%1 must be a file inside the pack." ).arg( key );
        }
    }
    parsed.previews = body.value( "previews" ).toString();
    parsed.actions = body.value( "actions" ).toString();
    parsed.highlights = body.value( "highlights" ).toString();
    parsed.filters = body.value( "filters" ).toString();
    if ( body.contains( "lua" ) ) {
        const auto error = validateLua( body.value( "lua" ), &parsed );
        if ( !error.isEmpty() ) {
            return error;
        }
    }
    if ( body.contains( "decoder_routes" ) ) {
        const auto routes = body.value( "decoder_routes" ).toObject();
        if ( !body.value( "decoder_routes" ).isObject() ) {
            return QStringLiteral( "content.decoder_routes must be an object." );
        }
        for ( auto item = routes.begin(); item != routes.end(); ++item ) {
            if ( ( item.key() != "previewRoutes" && item.key() != "protocolRoutes" )
                 || !item.value().isObject() ) {
                return QStringLiteral( "Invalid content.decoder_routes key: %1" )
                    .arg( item.key().left( 64 ) );
            }
        }
        parsed.previewRoutes = routes.value( "previewRoutes" ).toObject();
        parsed.protocolRoutes = routes.value( "protocolRoutes" ).toObject();
        for ( const bool protocol : { false, true } ) {
            const auto error
                = validateRoutes( protocol ? parsed.protocolRoutes : parsed.previewRoutes, protocol );
            if ( !error.isEmpty() ) {
                return error;
            }
        }
        if ( !parsed.previewRoutes.isEmpty() && parsed.previews.isEmpty() ) {
            return QStringLiteral( "decoder_routes.previewRoutes needs the pack's previews." );
        }
    }
    if ( body.contains( "python_decoders" ) ) {
        const auto error = validatePythonDecoders( body.value( "python_decoders" ), &parsed );
        if ( !error.isEmpty() ) {
            return error;
        }
    }
    if ( const auto error = checkReservedPaths( parsed ); !error.isEmpty() ) {
        return error;
    }
    if ( content ) {
        *content = parsed;
    }
    return {};
}

QString validatePackFiles( const QJsonObject& manifest, const QString& directory )
{
    PackContent content;
    const auto structure = validatePackManifest( manifest, &content );
    if ( !structure.isEmpty() ) {
        return structure;
    }
    for ( const auto& file :
          { content.previews, content.actions, content.highlights, content.filters } ) {
        if ( !file.isEmpty() ) {
            const auto error = checkFile( directory, file );
            if ( !error.isEmpty() ) {
                return error;
            }
        }
    }
    for ( const auto& decoder : content.pythonDecoders ) {
        const auto error = checkFile( directory, decoder.module );
        if ( !error.isEmpty() ) {
            return error;
        }
    }
    if ( !content.luaFolder.isEmpty() ) {
        const QFileInfo folder( QDir( directory ).filePath( content.luaFolder ) );
        if ( !folder.isDir() || folder.isSymLink()
             || !resolvesInside( directory, content.luaFolder ) ) {
            return QStringLiteral( "Lua folder missing or outside the pack: %1" )
                .arg( content.luaFolder.left( 128 ) );
        }
        QSet<QString> listed;
        for ( const auto& file : content.luaLoad ) {
            const auto error = checkFile( directory, content.luaFolder + '/' + file );
            if ( !error.isEmpty() ) {
                return error;
            }
            listed.insert( QDir::cleanPath( file ).toLower() );
        }
        // Every file of the folder is listed: nothing loads behind the user's back, and a
        // file meant to be loaded is never forgotten (design 4.1).
        QDirIterator it( folder.absoluteFilePath(), QDir::Files | QDir::Hidden | QDir::System,
                         QDirIterator::Subdirectories );
        const QDir root( folder.absoluteFilePath() );
        while ( it.hasNext() ) {
            const auto relative = root.relativeFilePath( it.next() );
            if ( !listed.contains( QDir::cleanPath( relative ).toLower() ) ) {
                return QStringLiteral( "Lua file not listed in content.lua.load: %1" )
                    .arg( relative.left( 128 ) );
            }
        }
    }
    if ( !content.previewRoutes.isEmpty() ) {
        QFile previews( QDir( directory ).filePath( content.previews ) );
        const auto names = previews.open( QIODevice::ReadOnly )
                               ? packPreviewNames( previews.read( MaxPackFileBytes ) )
                               : QStringList{};
        for ( auto route = content.previewRoutes.begin(); route != content.previewRoutes.end();
              ++route ) {
            if ( !names.contains( route.key() ) ) {
                return QStringLiteral( "decoder_routes.previewRoutes names a preview the pack does "
                                       "not have: %1" )
                    .arg( route.key().left( 128 ) );
            }
        }
    }
    return {};
}

QStringList packContents( const QJsonObject& manifest )
{
    const auto body = manifest.value( "content" ).toObject();
    QStringList contents;
    for ( const auto& key : { "previews", "actions", "highlights", "filters", "lua",
                              "python_decoders" } ) {
        if ( body.contains( QLatin1String( key ) ) ) {
            contents.append( QLatin1String( key ) );
        }
    }
    return contents;
}

QStringList packPreviewNames( const QByteArray& previewsJson )
{
    const auto document = QJsonDocument::fromJson( previewsJson );
    const auto previews = document.isArray() ? document.array()
                                             : document.object().value( "previews" ).toArray();
    QStringList names;
    for ( const auto& preview : previews ) {
        const auto name = preview.toObject().value( "name" ).toString();
        if ( !name.isEmpty() ) {
            names.append( name );
        }
    }
    return names;
}

QVariantMap packSummary( const QJsonObject& manifest, const QString& directory )
{
    PackContent content;
    if ( !validatePackManifest( manifest, &content ).isEmpty() ) {
        return { { "contents", packContents( manifest ) } };
    }
    int previews = 0;
    if ( !content.previews.isEmpty() ) {
        QFile file( QDir( directory ).filePath( content.previews ) );
        if ( file.open( QIODevice::ReadOnly ) ) {
            previews = static_cast<int>( packPreviewNames( file.read( MaxPackFileBytes ) ).size() );
        }
    }
    int filters = 0;
    if ( !content.filters.isEmpty() ) {
        QFile file( QDir( directory ).filePath( content.filters ) );
        if ( file.open( QIODevice::ReadOnly ) ) {
            const auto items = packcanonical::fromFilters( file.read( MaxPackFileBytes ) );
            filters = static_cast<int>(
                std::count_if( items.keyBegin(), items.keyEnd(), []( const QString& path ) {
                    return packcanonical::kindOf( path ) == QLatin1String( "filter" );
                } ) );
        }
    }
    return { { "contents", packContents( manifest ) },
             { "previews", previews },
             { "filters", filters },
             { "luaFiles", static_cast<int>( content.luaLoad.size() ) },
             { "pythonDecoders", static_cast<int>( content.pythonDecoders.size() ) } };
}
} // namespace cilogg::plugins
