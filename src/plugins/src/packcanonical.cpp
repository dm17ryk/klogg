#include "packcanonical.h"

#include "configpack.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <algorithm>

namespace cilogg::plugins::packcanonical {
namespace {
constexpr char PreviewsDir[] = "previews/";
constexpr char BlocksDir[] = "blocks/";
constexpr char FiltersDir[] = "filters/";
constexpr char FilesDir[] = "files/";
constexpr char OrderFile[] = "previews/order.txt";
constexpr char MetaFile[] = "previews/meta.json";
constexpr char FilterOrderFile[] = "filters/order.txt";
constexpr char JsonSuffix[] = ".json";
// The filters file's group and the newest version this CILogg writes (PredefinedFiltersCollection).
constexpr char FiltersGroup[] = "PredefinedFiltersCollection";
constexpr int FiltersVersion = 3;

bool plain( QChar character )
{
    return ( character >= 'a' && character <= 'z' ) || ( character >= '0' && character <= '9' )
           || character == '.' || character == '_' || character == '-';
}

// The names of an order file, one per line. An order edited on Windows or merged from such an edit
// may end its lines with CR LF: no name or id has a CR (fromPreviews and fromFilters refuse one),
// so it is the line end's.
QStringList orderLines( const QByteArray& order )
{
    return QString::fromUtf8( order ).remove( '\r' ).split( '\n' );
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
    // "meta" is the previews settings' file (previews/meta.json): an item of that name is spelled
    // with its first letter escaped, so it never takes that file's place.
    if ( encoded == QLatin1String( "meta" ) ) {
        encoded = QStringLiteral( "%6Deta" );
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
    // The preview limits (128 previews, 4,096 blocks) that a pack must fit to be enabled at all:
    // a pack over them is refused here, before it becomes thousands of store items.
    if ( previews.size() > MaxPackPreviewItems || blocks.size() > MaxPackBlockItems ) {
        return reject( QStringLiteral( "more than %1 previews or %2 blocks." )
                           .arg( MaxPackPreviewItems )
                           .arg( MaxPackBlockItems ) );
    }
    // One item per name: a name is the item's identity and its file name. The names seen are a
    // set: a 1 MiB document can hold tens of thousands of small items.
    const auto named = [ &reject ]( const QJsonValue& value, const QString& what,
                                    QSet<QString>* seen, QString* name ) {
        *name = value.toObject().value( "name" ).toString();
        if ( !value.isObject() || name->isEmpty() || name->contains( '\n' )
             || name->contains( '\r' ) ) {
            reject( QStringLiteral( "a %1 without a name." ).arg( what ) );
            return false;
        }
        if ( seen->contains( *name ) ) {
            reject( QStringLiteral( "two %1s named \"%2\"." ).arg( what, name->left( 64 ) ) );
            return false;
        }
        seen->insert( *name );
        return true;
    };
    QStringList order;
    QSet<QString> previewNames;
    for ( const auto& value : previews ) {
        QString name;
        if ( !named( value, QStringLiteral( "preview" ), &previewNames, &name ) ) {
            return {};
        }
        order.append( name );
        files.insert( PreviewsDir + encodeName( name ) + JsonSuffix, itemBytes( value.toObject() ) );
    }
    QSet<QString> blockNames;
    for ( const auto& value : blocks ) {
        QString name;
        if ( !named( value, QStringLiteral( "block" ), &blockNames, &name ) ) {
            return {};
        }
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
    for ( const auto& line : orderLines( files.value( OrderFile ) ) ) {
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

PackFiles fromFilters( const QByteArray& filtersConf, QString* error )
{
    const auto reject = [ error ]( const QString& why ) {
        if ( error ) {
            *error = QStringLiteral( "The pack's filters file is not valid: %1" ).arg( why );
        }
        return PackFiles{};
    };
    // QSettings reads INI only from a file, and caches what it read by path: a folder of its own
    // each time, so a new file never meets an earlier one's cache.
    QTemporaryDir folder;
    const auto path = folder.filePath( QStringLiteral( "filters.conf" ) );
    {
        QFile file( path );
        if ( !folder.isValid() || !file.open( QIODevice::WriteOnly )
             || file.write( filtersConf ) != filtersConf.size() ) {
            return reject( QStringLiteral( "it could not be read (no temporary folder)." ) );
        }
    }
    QSettings settings( path, QSettings::IniFormat );
    const QString group = QLatin1String( FiltersGroup ) + '/';
    bool ok = false;
    const auto version = settings.value( group + QStringLiteral( "version" ) ).toInt( &ok );
    if ( settings.status() != QSettings::NoError || !ok || version < 1
         || version > FiltersVersion ) {
        return reject(
            QStringLiteral( "it needs [%1] with a version from 1 to %2, as CILogg exports "
                            "predefined filters." )
                .arg( QLatin1String( FiltersGroup ) )
                .arg( FiltersVersion ) );
    }
    const auto sizeValue = settings.value( group + QStringLiteral( "filters/size" ) );
    const auto size = sizeValue.isValid() ? sizeValue.toInt( &ok ) : 0;
    if ( ( sizeValue.isValid() && !ok ) || size < 0 ) {
        return reject( QStringLiteral( "its filters/size is not a count." ) );
    }
    if ( size > MaxPackFilterItems ) {
        return reject( QStringLiteral( "more than %1 filters." ).arg( MaxPackFilterItems ) );
    }
    // Exactly the filters: anything else in the file would be lost on the way to the store.
    static const QRegularExpression keyPattern( QRegularExpression::anchoredPattern(
        QStringLiteral(
            "%1/(?:version|filters/size|filters/([1-9][0-9]{0,4})/(?:id|name|filter|regex))" )
            .arg( QLatin1String( FiltersGroup ) ) ) );
    for ( const auto& key : settings.allKeys() ) {
        const auto match = keyPattern.match( key );
        if ( !match.hasMatch()
             || ( match.capturedLength( 1 ) > 0 && match.captured( 1 ).toInt() > size ) ) {
            return reject(
                QStringLiteral( "it holds more than the filters (%1)." ).arg( key.left( 64 ) ) );
        }
    }
    PackFiles files;
    QStringList order;
    QSet<QString> ids;
    for ( int index = 1; index <= size; ++index ) {
        const auto prefix = group + QStringLiteral( "filters/%1/" ).arg( index );
        const auto id = settings.value( prefix + QStringLiteral( "id" ) ).toString();
        const auto name = settings.value( prefix + QStringLiteral( "name" ) ).toString();
        const auto pattern = settings.value( prefix + QStringLiteral( "filter" ) ).toString();
        if ( id.isEmpty() || id.contains( '\n' ) || id.contains( '\r' ) ) {
            return reject(
                QStringLiteral( "filter %1 has no id (a pack's filters are known by their "
                                "ids)." )
                    .arg( index ) );
        }
        if ( ids.contains( id ) ) {
            return reject(
                QStringLiteral( "two filters with the id \"%1\"." ).arg( id.left( 64 ) ) );
        }
        if ( name.isEmpty() || pattern.isEmpty() ) {
            return reject( QStringLiteral( "filter \"%1\" needs a name and a pattern." )
                               .arg( id.left( 64 ) ) );
        }
        // The import reads a missing `regex` as a regular expression.
        const auto regexText
            = settings.value( prefix + QStringLiteral( "regex" ), QStringLiteral( "true" ) )
                  .toString()
                  .toLower();
        if ( regexText != QLatin1String( "true" ) && regexText != QLatin1String( "false" ) ) {
            return reject(
                QStringLiteral( "filter \"%1\" has a regex that is neither true nor false." )
                    .arg( id.left( 64 ) ) );
        }
        ids.insert( id );
        order.append( id );
        files.insert( FiltersDir + encodeName( id ) + JsonSuffix,
                      itemBytes( { { "id", id },
                                   { "name", name },
                                   { "filter", pattern },
                                   { "regex", regexText == QLatin1String( "true" ) } } ) );
    }
    files.insert( FilterOrderFile,
                  order.isEmpty() ? QByteArray{} : ( order.join( '\n' ) + '\n' ).toUtf8() );
    return files;
}

QByteArray toFilters( const PackFiles& files, QString* error )
{
    QMap<QString, QJsonObject> filters; // by id: the ones missing from the order follow in id order
    for ( auto file = files.cbegin(); file != files.cend(); ++file ) {
        if ( kindOf( file.key() ) == QLatin1String( "filter" ) ) {
            filters.insert( itemOf( file.key() ),
                            QJsonDocument::fromJson( file.value() ).object() );
        }
    }
    // A filter's id is its store path's (pack_edit keeps the two the same).
    QVector<std::pair<QString, QJsonObject>> ordered;
    for ( const auto& line : orderLines( files.value( FilterOrderFile ) ) ) {
        if ( filters.contains( line ) ) {
            ordered.append( { line, filters.take( line ) } );
        }
    }
    for ( auto filter = filters.cbegin(); filter != filters.cend(); ++filter ) {
        ordered.append( { filter.key(), filter.value() } );
    }
    const auto failed = [ error ] {
        if ( error ) {
            *error = QStringLiteral( "The pack's filters file could not be written." );
        }
        return QByteArray{};
    };
    QTemporaryDir folder;
    if ( !folder.isValid() ) {
        return failed();
    }
    const auto path = folder.filePath( QStringLiteral( "filters.conf" ) );
    {
        QSettings settings( path, QSettings::IniFormat );
        settings.beginGroup( QLatin1String( FiltersGroup ) );
        settings.setValue( QStringLiteral( "version" ), FiltersVersion );
        settings.beginWriteArray( QStringLiteral( "filters" ), static_cast<int>( ordered.size() ) );
        for ( int index = 0; index < ordered.size(); ++index ) {
            const auto& [ id, filter ] = ordered.at( index );
            settings.setArrayIndex( index );
            settings.setValue( QStringLiteral( "id" ), id );
            settings.setValue( QStringLiteral( "name" ), filter.value( "name" ).toString() );
            settings.setValue( QStringLiteral( "filter" ), filter.value( "filter" ).toString() );
            settings.setValue( QStringLiteral( "regex" ), filter.value( "regex" ).toBool( true ) );
        }
        settings.endArray();
        settings.endGroup();
        settings.sync();
        if ( settings.status() != QSettings::NoError ) {
            return failed();
        }
    }
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return failed();
    }
    return file.readAll();
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
    // The manifest check keeps these paths relative and inside the pack. Actions and highlights
    // are carried as files until E16c gives them a canonical form of their own, as filters have.
    QStringList paths;
    for ( const auto& file : content.luaLoad ) {
        paths.append( filePath( content.luaFolder + '/' + file ) );
    }
    for ( const auto& decoder : content.pythonDecoders ) {
        paths.append( filePath( decoder.module ) );
    }
    for ( const auto* other : { &content.actions, &content.highlights } ) {
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
    if ( kind == QLatin1String( "filter" ) ) {
        return storePath == FiltersDir + encodeName( itemOf( storePath ) ) + JsonSuffix;
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
    if ( !content.filters.isEmpty() && readError.isEmpty() ) {
        const auto filters = fromFilters(
            readFile( QDir( directory ).filePath( content.filters ), &readError ), &readError );
        for ( auto filter = filters.cbegin(); filter != filters.cend(); ++filter ) {
            files.insert( filter.key(), filter.value() );
        }
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
    if ( storePath == FilterOrderFile ) {
        return QStringLiteral( "filter-order" );
    }
    if ( storePath.startsWith( FiltersDir ) && storePath.endsWith( JsonSuffix )
         && !storePath.mid( 8 ).contains( '/' ) ) {
        return QStringLiteral( "filter" );
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
    if ( kind == "filter" ) {
        return decodeName( storePath.mid( 8, storePath.size() - 8 - 5 ) );
    }
    if ( kind == "file" ) {
        return storePath.mid( 6 );
    }
    return storePath;
}
} // namespace cilogg::plugins::packcanonical
