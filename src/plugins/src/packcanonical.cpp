#include "packcanonical.h"

#include "configpack.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

namespace cilogg::plugins::packcanonical {
namespace {
constexpr char PreviewsDir[] = "previews/";
constexpr char BlocksDir[] = "blocks/";
constexpr char FilesDir[] = "files/";
constexpr char OrderFile[] = "previews/order.txt";
constexpr char MetaFile[] = "previews/meta.json";
constexpr char JsonSuffix[] = ".json";

bool plain( QChar character )
{
    return ( character >= 'a' && character <= 'z' ) || ( character >= '0' && character <= '9' )
           || character == '.' || character == '_' || character == '-';
}

QByteArray readFile( const QString& path, QString* error )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) || file.size() > MaxPackFileBytes ) {
        if ( error ) {
            *error = QStringLiteral( "Cannot read pack file: %1" ).arg( QFileInfo( path ).fileName() );
        }
        return {};
    }
    return file.readAll();
}
} // namespace

QString encodeName( const QString& name )
{
    // Byte by byte of the UTF-8 form, so characters outside the BMP (two QChars) stay whole.
    QString encoded;
    for ( const auto byte : name.toUtf8() ) {
        const auto character = QChar::fromLatin1( byte );
        if ( static_cast<unsigned char>( byte ) < 0x80 && plain( character ) ) {
            encoded.append( character );
            continue;
        }
        encoded.append( QStringLiteral( "%%1" )
                            .arg( static_cast<unsigned char>( byte ), 2, 16, QChar( '0' ) )
                            .toUpper() );
    }
    // "." and ".." are not file names.
    if ( encoded == "." || encoded == ".." || encoded.isEmpty() ) {
        encoded.replace( '.', QStringLiteral( "%2E" ) );
    }
    return encoded;
}

QString decodeName( const QString& encoded )
{
    return QString::fromUtf8( QByteArray::fromPercentEncoding( encoded.toUtf8() ) );
}

QByteArray itemBytes( const QJsonObject& item )
{
    // QJsonObject keeps its keys sorted; Indented writes one member per line.
    return QJsonDocument( item ).toJson( QJsonDocument::Indented );
}

PackFiles fromPreviews( const QByteArray& previewsJson, QString* error )
{
    PackFiles files;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( previewsJson, &parseError );
    if ( parseError.error != QJsonParseError::NoError
         || ( !document.isObject() && !document.isArray() ) ) {
        if ( error ) {
            *error = QStringLiteral( "The pack's previews are not a previews document." );
        }
        return {};
    }
    auto meta = document.isObject() ? document.object() : QJsonObject{};
    // The document as it is, or an error: never a quietly cleaned-up copy (a disabled pack is
    // installed without the full preview check, so this is where a malformed one is caught).
    const auto reject = [ error ]( const QString& why ) {
        if ( error ) {
            *error = QStringLiteral( "The pack's previews document is not valid: %1" ).arg( why );
        }
        return PackFiles{};
    };
    if ( document.isObject()
         && ( !meta.value( "previews" ).isArray()
              || ( meta.contains( "blocks" ) && !meta.value( "blocks" ).isArray() ) ) ) {
        return reject( QStringLiteral( "it needs a \"previews\" list (and \"blocks\", if any, "
                                       "must be a list)." ) );
    }
    const auto previews = document.isArray() ? document.array() : meta.take( "previews" ).toArray();
    const auto blocks = meta.take( "blocks" ).toArray();
    // One item per name: a name is the item's identity and its file name.
    const auto named = [ &reject ]( const QJsonValue& value, const QString& what,
                                    const QStringList& seen, QString* name ) {
        *name = value.toObject().value( "name" ).toString();
        if ( !value.isObject() || name->isEmpty() || name->contains( '\n' )
             || name->contains( '\r' ) ) {
            reject( QStringLiteral( "a %1 without a name." ).arg( what ) );
            return false;
        }
        if ( seen.contains( *name ) ) {
            reject( QStringLiteral( "two %1s named \"%2\"." ).arg( what, name->left( 64 ) ) );
            return false;
        }
        return true;
    };
    QStringList order;
    for ( const auto& value : previews ) {
        QString name;
        if ( !named( value, QStringLiteral( "preview" ), order, &name ) ) {
            return {};
        }
        order.append( name );
        files.insert( PreviewsDir + encodeName( name ) + JsonSuffix, itemBytes( value.toObject() ) );
    }
    QStringList blockNames;
    for ( const auto& value : blocks ) {
        QString name;
        if ( !named( value, QStringLiteral( "block" ), blockNames, &name ) ) {
            return {};
        }
        blockNames.append( name );
        files.insert( BlocksDir + encodeName( name ) + JsonSuffix, itemBytes( value.toObject() ) );
    }
    files.insert( OrderFile, order.isEmpty() ? QByteArray{} : ( order.join( '\n' ) + '\n' ).toUtf8() );
    if ( !meta.isEmpty() ) {
        files.insert( MetaFile, itemBytes( meta ) );
    }
    return files;
}

QByteArray toPreviews( const PackFiles& files )
{
    QJsonObject document
        = QJsonDocument::fromJson( files.value( MetaFile ) ).object(); // version, $schema, ...
    QMap<QString, QJsonObject> previews;
    QJsonArray blocks;
    for ( auto file = files.cbegin(); file != files.cend(); ++file ) {
        const auto kind = kindOf( file.key() );
        const auto item = QJsonDocument::fromJson( file.value() ).object();
        if ( kind == "preview" ) {
            previews.insert( itemOf( file.key() ), item );
        }
        else if ( kind == "block" ) {
            blocks.append( item ); // QMap order: block names sorted
        }
    }
    QJsonArray ordered;
    for ( const auto& line : QString::fromUtf8( files.value( OrderFile ) ).split( '\n' ) ) {
        if ( previews.contains( line ) ) {
            ordered.append( previews.take( line ) );
        }
    }
    for ( const auto& preview : previews ) {
        ordered.append( preview );
    }
    if ( !document.contains( "version" ) ) {
        document[ "version" ] = 1;
    }
    document[ "previews" ] = ordered;
    document[ "blocks" ] = blocks;
    return QJsonDocument( document ).toJson( QJsonDocument::Indented );
}

QString filePath( const QString& packRelative )
{
    return FilesDir + QDir::cleanPath( packRelative );
}

QStringList listedFiles( const QJsonObject& manifest )
{
    PackContent content;
    if ( !validatePackManifest( manifest, &content ).isEmpty() ) {
        return {};
    }
    // The manifest check keeps these paths relative and inside the pack. Actions, highlights and
    // filters are carried as files until E16c gives them a canonical form of their own.
    QStringList paths;
    for ( const auto& file : content.luaLoad ) {
        paths.append( filePath( content.luaFolder + '/' + file ) );
    }
    for ( const auto& decoder : content.pythonDecoders ) {
        paths.append( filePath( decoder.module ) );
    }
    for ( const auto* other : { &content.actions, &content.highlights, &content.filters } ) {
        if ( !other->isEmpty() && !paths.contains( filePath( *other ) ) ) {
            paths.append( filePath( *other ) );
        }
    }
    return paths;
}

bool isCanonical( const QString& storePath )
{
    const auto kind = kindOf( storePath );
    if ( kind == QLatin1String( "preview" ) ) {
        return storePath == PreviewsDir + encodeName( itemOf( storePath ) ) + JsonSuffix;
    }
    if ( kind == QLatin1String( "block" ) ) {
        return storePath == BlocksDir + encodeName( itemOf( storePath ) ) + JsonSuffix;
    }
    if ( kind == QLatin1String( "file" ) ) {
        const auto relative = itemOf( storePath );
        return !relative.isEmpty() && !QDir::isAbsolutePath( relative ) && !relative.contains( ':' )
               && !relative.contains( '\\' ) && filePath( relative ) == storePath
               && !relative.startsWith( QStringLiteral( "../" ) ) && relative != QLatin1String( ".." );
    }
    return !kind.isEmpty();
}

PackFiles fromPackFolder( const QJsonObject& manifest, const QString& directory, QString* error )
{
    PackContent content;
    const auto invalid = validatePackManifest( manifest, &content );
    if ( !invalid.isEmpty() ) {
        if ( error ) {
            *error = invalid;
        }
        return {};
    }
    PackFiles files;
    QString readError;
    if ( !content.previews.isEmpty() ) {
        files = fromPreviews( readFile( QDir( directory ).filePath( content.previews ), &readError ),
                              &readError );
    }
    for ( const auto& storePath : listedFiles( manifest ) ) {
        files.insert( storePath,
                      readFile( QDir( directory ).filePath( itemOf( storePath ) ), &readError ) );
    }
    if ( !readError.isEmpty() ) {
        if ( error ) {
            *error = readError;
        }
        return {};
    }
    return files;
}

QString kindOf( const QString& storePath )
{
    if ( storePath == OrderFile ) {
        return QStringLiteral( "order" );
    }
    if ( storePath == MetaFile ) {
        return QStringLiteral( "meta" );
    }
    if ( storePath.startsWith( PreviewsDir ) && storePath.endsWith( JsonSuffix )
         && !storePath.mid( 9 ).contains( '/' ) ) {
        return QStringLiteral( "preview" );
    }
    if ( storePath.startsWith( BlocksDir ) && storePath.endsWith( JsonSuffix )
         && !storePath.mid( 7 ).contains( '/' ) ) {
        return QStringLiteral( "block" );
    }
    if ( storePath.startsWith( FilesDir ) ) {
        return QStringLiteral( "file" );
    }
    return {};
}

QString itemOf( const QString& storePath )
{
    const auto kind = kindOf( storePath );
    if ( kind == "preview" ) {
        return decodeName( storePath.mid( 9, storePath.size() - 9 - 5 ) );
    }
    if ( kind == "block" ) {
        return decodeName( storePath.mid( 7, storePath.size() - 7 - 5 ) );
    }
    if ( kind == "file" ) {
        return storePath.mid( 6 );
    }
    return storePath;
}
} // namespace cilogg::plugins::packcanonical
