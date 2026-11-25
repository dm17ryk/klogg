#include "packcanonical.h"

#include "configpack.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <tuple>

namespace cilogg::plugins::packcanonical {
namespace {
constexpr char PreviewsDir[] = "previews/";
constexpr char BlocksDir[] = "blocks/";
constexpr char FiltersDir[] = "filters/";
constexpr char FilesDir[] = "files/";
constexpr char OrderFile[] = "previews/order.txt";
constexpr char MetaFile[] = "previews/meta.json";
constexpr char FilterOrderFile[] = "filters/order.txt";
constexpr char HighlightsDir[] = "highlights/";
constexpr char HighlightOrderFile[] = "highlights/order.txt";
constexpr char ActionsDir[] = "actions/";
constexpr char ActionOrderFile[] = "actions/order.txt";
constexpr char ResponsesDir[] = "responses/";
constexpr char ResponseOrderFile[] = "responses/order.txt";
constexpr char JsonSuffix[] = ".json";
constexpr char SetSuffix[] = ".set";
// The actions file's version this CILogg writes (ActionsRepository); it reads 1, 3 and 4.
constexpr int ActionsVersion = 4;
// The filters file's group and the newest version this CILogg writes (PredefinedFiltersCollection).
constexpr char FiltersGroup[] = "PredefinedFiltersCollection";
constexpr int FiltersVersion = 3;
// The highlighters file's group and versions (HighlighterSetCollection, HighlighterSet).
constexpr char HighlightsGroup[] = "HighlighterSetCollection";
constexpr int HighlightsVersion = 2;
constexpr int HighlightSetVersion = 3;
// A rule's colour variance: the colours vary by this many percent either way, so it stays within
// 0..100 (the editor offers 5 to 99).
constexpr int MaxColorVariance = 100;

// One highlighter rule's members, as HighlighterSet saves them: the canonical form keeps them all.
const QStringList& ruleKeys()
{
    static const QStringList keys{
        "back_colour", "color_variance", "fore_colour", "ignore_case",
        "match_only",  "regexp",         "use_regex",   "variate_colors"
    };
    return keys;
}

bool boolKey( const QString& key )
{
    return key == QLatin1String( "ignore_case" ) || key == QLatin1String( "match_only" )
           || key == QLatin1String( "use_regex" ) || key == QLatin1String( "variate_colors" );
}

struct HighlightSet {
    QString id;
    QString name;
    QVector<QJsonObject> rules;
};

QByteArray jsonString( const QString& text )
{
    const auto array = QJsonDocument( QJsonArray{ text } ).toJson( QJsonDocument::Compact );
    return array.mid( 1, array.size() - 2 ); // ["..."] -> "..."
}

QByteArray setText( const HighlightSet& set )
{
    QByteArray text = "id " + jsonString( set.id ) + "\nname " + jsonString( set.name ) + '\n';
    for ( const auto& rule : set.rules ) {
        text += "rule " + QJsonDocument( rule ).toJson( QJsonDocument::Compact ) + '\n';
    }
    return text;
}

// A rule is exactly the members CILogg saves, each of its type.
bool validRule( const QJsonObject& rule )
{
    if ( rule.keys() != ruleKeys() ) {
        return false;
    }
    for ( auto member = rule.constBegin(); member != rule.constEnd(); ++member ) {
        const auto& value = member.value();
        const auto& key = member.key();
        const bool ok = boolKey( key ) ? value.isBool()
                        : key == QLatin1String( "color_variance" )
                            ? value.isDouble() && value.toDouble() == value.toInt()
                                  && value.toInt() >= 0 && value.toInt() <= MaxColorVariance
                            : value.isString();
        if ( !ok ) {
            return false;
        }
    }
    return true;
}

// A set's text: `id "..."`, `name "..."`, then `rule {...}` lines (CR LF line ends allowed).
std::optional<HighlightSet> parseSet( const QByteArray& bytes, QString* why )
{
    auto lines = QString::fromUtf8( bytes ).remove( '\r' ).split( '\n' );
    if ( !lines.isEmpty() && lines.last().isEmpty() ) {
        lines.removeLast();
    }
    const auto stringAfter
        = [ &lines ]( int index, const QString& keyword ) -> std::optional<QString> {
        if ( index >= lines.size() || !lines.at( index ).startsWith( keyword + ' ' ) ) {
            return std::nullopt;
        }
        const auto array = QJsonDocument::fromJson(
                               '[' + lines.at( index ).mid( keyword.size() + 1 ).toUtf8() + ']' )
                               .array();
        if ( array.size() != 1 || !array.first().isString() ) {
            return std::nullopt;
        }
        return array.first().toString();
    };
    const auto id = stringAfter( 0, QStringLiteral( "id" ) );
    const auto name = stringAfter( 1, QStringLiteral( "name" ) );
    if ( !id || !name ) {
        *why = QStringLiteral( "it does not start with its id and name lines" );
        return std::nullopt;
    }
    HighlightSet set{ *id, *name, {} };
    if ( set.id.isEmpty() || set.name.isEmpty() ) {
        *why = QStringLiteral( "its id or name is empty" );
        return std::nullopt;
    }
    for ( int index = 2; index < lines.size(); ++index ) {
        const auto& line = lines.at( index );
        QJsonParseError error;
        const auto rule = QJsonDocument::fromJson( line.mid( 5 ).toUtf8(), &error );
        if ( !line.startsWith( QLatin1String( "rule " ) ) || error.error != QJsonParseError::NoError
             || !rule.isObject() || !validRule( rule.object() ) ) {
            *why = QStringLiteral( "line %1 is not a rule with %2" )
                       .arg( index + 1 )
                       .arg( ruleKeys().join( ", " ) );
            return std::nullopt;
        }
        set.rules.append( rule.object() );
    }
    if ( set.rules.size() > MaxPackHighlightRules ) {
        *why = QStringLiteral( "more than %1 rules" ).arg( MaxPackHighlightRules );
        return std::nullopt;
    }
    return set;
}

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

// The id of an action or a response: a whole number from 0 that fits an int, as CILogg keeps it.
std::optional<int> wholeId( const QJsonValue& value )
{
    if ( !value.isDouble() ) {
        return {};
    }
    const auto number = value.toDouble();
    if ( !std::isfinite( number ) || number != std::floor( number ) || number < 0
         || number > std::numeric_limits<int>::max() ) {
        return {};
    }
    return static_cast<int>( number );
}

// The id a store path of an action or a response stands for, or nothing when the path does not
// spell one canonically (decimal, no sign, no leading zeros).
std::optional<int> pathId( const QString& item )
{
    bool ok = false;
    const auto id = item.toInt( &ok );
    if ( !ok || id < 0 || QString::number( id ) != item ) {
        return {};
    }
    return id;
}

// One list of an actions file ("actions" or "responses") in the order CILogg reads it: a stable
// sort by "order" (an item without one has order 0, one that is not a number its index), each item
// with its id and without "order". Empty, with `why` set, when an item is not an object with an id
// and a name, or two items share an id.
std::optional<QVector<std::pair<int, QJsonObject>>> actionList( const QJsonArray& list,
                                                                const QString& what, QString* why )
{
    QVector<std::tuple<int, int, QJsonObject>> read; // order, id, item
    QSet<int> ids;
    for ( qsizetype index = 0; index < list.size(); ++index ) {
        const auto value = list.at( index );
        const auto id = value.isObject() ? wholeId( value.toObject().value( "id" ) ) : std::nullopt;
        if ( !id ) {
            *why
                = QStringLiteral( "%1 entry %2 has no id (a pack's actions and responses are known "
                                  "by their ids, whole numbers from 0)." )
                      .arg( what )
                      .arg( index );
            return {};
        }
        if ( ids.contains( *id ) ) {
            *why = QStringLiteral( "two %1s with the id %2." ).arg( what ).arg( *id );
            return {};
        }
        auto item = value.toObject();
        const auto name = item.value( "name" );
        if ( !name.isString() || name.toString().trimmed().isEmpty() ) {
            *why = QStringLiteral( "%1 %2 needs a name." ).arg( what ).arg( *id );
            return {};
        }
        ids.insert( *id );
        const auto order = item.contains( "order" )
                               ? item.value( "order" ).toInt( static_cast<int>( index ) )
                               : 0;
        item.remove( "order" );
        read.append( { order, *id, item } );
    }
    std::stable_sort( read.begin(), read.end(), []( const auto& first, const auto& second ) {
        return std::get<0>( first ) < std::get<0>( second );
    } );
    QVector<std::pair<int, QJsonObject>> items;
    items.reserve( read.size() );
    for ( const auto& entry : read ) {
        items.append( { std::get<1>( entry ), std::get<2>( entry ) } );
    }
    return items;
}

// Why a response runs an action that is not in `actions`, or an empty string: its "action_id" and
// the "action_id" of each step, where they are numbers from 0 (-1 runs no action).
QString responseLinkError( int id, const QJsonObject& response, const QSet<int>& actions )
{
    const auto run = response.value( "response" ).toObject();
    QVector<QJsonValue> links;
    if ( run.contains( "action_id" ) ) {
        links.append( run.value( "action_id" ) );
    }
    for ( const auto& step : run.value( "steps" ).toArray() ) {
        if ( step.toObject().contains( "action_id" ) ) {
            links.append( step.toObject().value( "action_id" ) );
        }
    }
    for ( const auto& link : links ) {
        if ( !link.isDouble() ) {
            return QStringLiteral( "response %1 has an action_id that is not a number." ).arg( id );
        }
        const auto action = wholeId( link );
        if ( link.toDouble() >= 0 && ( !action || !actions.contains( *action ) ) ) {
            return QStringLiteral( "response %1 runs action %2, which is not in the file." )
                .arg( id )
                .arg( QString::number( link.toDouble(), 'g', 17 ) );
        }
    }
    return {};
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

PackFiles fromHighlights( const QByteArray& highlightsConf, QString* error )
{
    const auto reject = [ error ]( const QString& why ) {
        if ( error ) {
            *error = QStringLiteral( "The pack's highlighters file is not valid: %1" ).arg( why );
        }
        return PackFiles{};
    };
    // As for filters: QSettings reads INI only from a file, a folder of its own each time.
    QTemporaryDir folder;
    const auto path = folder.filePath( QStringLiteral( "highlights.conf" ) );
    {
        QFile file( path );
        if ( !folder.isValid() || !file.open( QIODevice::WriteOnly )
             || file.write( highlightsConf ) != highlightsConf.size() ) {
            return reject( QStringLiteral( "it could not be read (no temporary folder)." ) );
        }
    }
    QSettings settings( path, QSettings::IniFormat );
    const QString group = QLatin1String( HighlightsGroup ) + '/';
    bool ok = false;
    const auto version = settings.value( group + QStringLiteral( "version" ) ).toInt( &ok );
    if ( settings.status() != QSettings::NoError || !ok || version < 1
         || version > HighlightsVersion ) {
        return reject(
            QStringLiteral( "it needs [%1] with a version from 1 to %2, as CILogg exports "
                            "highlighters." )
                .arg( QLatin1String( HighlightsGroup ) )
                .arg( HighlightsVersion ) );
    }
    const auto countOf = [ &settings ]( const QString& key, int limit, int* count ) {
        const auto value = settings.value( key );
        bool isNumber = false;
        *count = value.isValid() ? value.toInt( &isNumber ) : 0;
        return ( !value.isValid() || isNumber ) && *count >= 0 && *count <= limit;
    };
    int size = 0;
    if ( !countOf( group + QStringLiteral( "sets/size" ), MaxPackHighlightSets, &size ) ) {
        return reject( QStringLiteral( "its sets/size is not a count of at most %1 sets." )
                           .arg( MaxPackHighlightSets ) );
    }
    // Exactly the sets (and the user's active sets and color labels, which are not carried):
    // anything else in the file would be lost on the way to the store.
    static const QRegularExpression keyPattern( QRegularExpression::anchoredPattern(
        QStringLiteral(
            "%1/(?:version|active_sets|current|sets/size|quick/size"
            "|quick/[1-9][0-9]{0,4}/(?:name|fore_colour|back_colour|cycle)"
            "|sets/([1-9][0-9]{0,4})/HighlighterSet/(?:version|name|id|highlighters/size"
            "|highlighters/([1-9][0-9]{0,4})/(?:%2)))" )
            .arg( QLatin1String( HighlightsGroup ), ruleKeys().join( '|' ) ) ) );
    QHash<int, int> ruleCounts; // set index -> its highlighters/size
    for ( int index = 1; index <= size; ++index ) {
        int rules = 0;
        if ( !countOf(
                 group + QStringLiteral( "sets/%1/HighlighterSet/highlighters/size" ).arg( index ),
                 MaxPackHighlightRules, &rules ) ) {
            return reject( QStringLiteral( "set %1 has more than %2 rules or no rule count." )
                               .arg( index )
                               .arg( MaxPackHighlightRules ) );
        }
        ruleCounts.insert( index, rules );
    }
    for ( const auto& key : settings.allKeys() ) {
        const auto match = keyPattern.match( key );
        const auto setIndex = match.captured( 1 ).toInt();
        const bool pastSets = match.capturedLength( 1 ) > 0 && setIndex > size;
        const bool pastRules = match.capturedLength( 2 ) > 0
                               && match.captured( 2 ).toInt() > ruleCounts.value( setIndex );
        if ( !match.hasMatch() || pastSets || pastRules ) {
            return reject( QStringLiteral( "it holds more than the highlighter sets (%1)." )
                               .arg( key.left( 64 ) ) );
        }
        // A value with an unquoted comma reads as a list, which as one value reads as nothing (a
        // pattern that matches every line): Export quotes it.
        if ( key != group + QStringLiteral( "active_sets" )
             && settings.value( key ).typeId() == QMetaType::QStringList ) {
            return reject( QStringLiteral( "%1 is a list: quote a value with commas, as Export "
                                           "writes it." )
                               .arg( key.left( 64 ) ) );
        }
    }
    PackFiles files;
    QStringList order;
    QSet<QString> ids;
    for ( int index = 1; index <= size; ++index ) {
        const auto prefix = group + QStringLiteral( "sets/%1/HighlighterSet/" ).arg( index );
        const auto setVersion = settings.value( prefix + QStringLiteral( "version" ) ).toInt( &ok );
        HighlightSet set{ settings.value( prefix + QStringLiteral( "id" ) ).toString(),
                          settings.value( prefix + QStringLiteral( "name" ) ).toString(),
                          {} };
        if ( !ok || setVersion < 1 || setVersion > HighlightSetVersion ) {
            return reject( QStringLiteral( "set %1 needs a version from 1 to %2." )
                               .arg( index )
                               .arg( HighlightSetVersion ) );
        }
        if ( set.id.isEmpty() || set.id.contains( '\n' ) || set.id.contains( '\r' ) ) {
            return reject(
                QStringLiteral( "set %1 has no id (a pack's highlighter sets are known by "
                                "their ids)." )
                    .arg( index ) );
        }
        if ( ids.contains( set.id ) ) {
            return reject(
                QStringLiteral( "two sets with the id \"%1\"." ).arg( set.id.left( 64 ) ) );
        }
        if ( set.name.isEmpty() ) {
            return reject( QStringLiteral( "set \"%1\" needs a name." ).arg( set.id.left( 64 ) ) );
        }
        for ( int rule = 1; rule <= ruleCounts.value( index ); ++rule ) {
            const auto rulePrefix = prefix + QStringLiteral( "highlighters/%1/" ).arg( rule );
            // Every counted rule is there, its pattern at least (CILogg always writes it): a count
            // alone would make thousands of rules out of a few bytes. The other members an older
            // export lacks take the defaults CILogg reads them with.
            if ( !settings.contains( rulePrefix + QStringLiteral( "regexp" ) ) ) {
                return reject( QStringLiteral( "rule %1 of set \"%2\" has no pattern (regexp)." )
                                   .arg( rule )
                                   .arg( set.id.left( 64 ) ) );
            }
            // The defaults HighlighterSet reads a missing member with.
            const QJsonObject defaults{ { "back_colour", "" },   { "color_variance", 15 },
                                        { "fore_colour", "" },   { "ignore_case", false },
                                        { "match_only", false }, { "regexp", "" },
                                        { "use_regex", true },   { "variate_colors", false } };
            QJsonObject item;
            for ( auto member = defaults.constBegin(); member != defaults.constEnd(); ++member ) {
                const auto value = settings.value( rulePrefix + member.key() );
                if ( !value.isValid() ) {
                    item.insert( member.key(), member.value() );
                    continue;
                }
                const auto text = value.toString();
                if ( boolKey( member.key() ) ) {
                    if ( text.compare( "true", Qt::CaseInsensitive ) != 0
                         && text.compare( "false", Qt::CaseInsensitive ) != 0 ) {
                        return reject(
                            QStringLiteral( "rule %1 of set \"%2\" has a %3 that is neither "
                                            "true nor false." )
                                .arg( rule )
                                .arg( set.id.left( 64 ), member.key() ) );
                    }
                    item.insert( member.key(), text.compare( "true", Qt::CaseInsensitive ) == 0 );
                }
                else if ( member.key() == QLatin1String( "color_variance" ) ) {
                    bool number = false;
                    const auto variance = text.toInt( &number );
                    if ( !number || variance < 0 || variance > MaxColorVariance ) {
                        return reject( QStringLiteral( "rule %1 of set \"%2\" has a color_variance "
                                                       "that is not a number from 0 to %3." )
                                           .arg( rule )
                                           .arg( set.id.left( 64 ) )
                                           .arg( MaxColorVariance ) );
                    }
                    item.insert( member.key(), variance );
                }
                else {
                    item.insert( member.key(), text );
                }
            }
            set.rules.append( item );
        }
        ids.insert( set.id );
        order.append( set.id );
        files.insert( HighlightsDir + encodeName( set.id ) + SetSuffix, setText( set ) );
    }
    files.insert( HighlightOrderFile,
                  order.isEmpty() ? QByteArray{} : ( order.join( '\n' ) + '\n' ).toUtf8() );
    return files;
}

QByteArray toHighlights( const PackFiles& files, QString* error )
{
    const auto failed = [ error ]( const QString& why ) {
        if ( error ) {
            *error = why;
        }
        return QByteArray{};
    };
    QMap<QString, HighlightSet> sets; // by id: the ones missing from the order follow in id order
    for ( auto file = files.cbegin(); file != files.cend(); ++file ) {
        if ( kindOf( file.key() ) != QLatin1String( "highlight" ) ) {
            continue;
        }
        QString why;
        const auto set = parseSet( file.value(), &why );
        if ( !set || set->id != itemOf( file.key() ) ) {
            return failed(
                QStringLiteral( "The pack's highlighter set \"%1\" cannot be read: %2." )
                    .arg( itemOf( file.key() ).left( 64 ),
                          set ? QStringLiteral( "its id line names another set" ) : why ) );
        }
        sets.insert( set->id, *set );
    }
    QVector<HighlightSet> ordered;
    for ( const auto& line : orderLines( files.value( HighlightOrderFile ) ) ) {
        if ( sets.contains( line ) ) {
            ordered.append( sets.take( line ) );
        }
    }
    for ( const auto& set : sets ) {
        ordered.append( set );
    }
    const auto unwritten = QStringLiteral( "The pack's highlighters file could not be written." );
    QTemporaryDir folder;
    if ( !folder.isValid() ) {
        return failed( unwritten );
    }
    const auto path = folder.filePath( QStringLiteral( "highlights.conf" ) );
    {
        QSettings settings( path, QSettings::IniFormat );
        settings.beginGroup( QLatin1String( HighlightsGroup ) );
        settings.setValue( QStringLiteral( "version" ), HighlightsVersion );
        settings.beginWriteArray( QStringLiteral( "sets" ), static_cast<int>( ordered.size() ) );
        for ( int index = 0; index < ordered.size(); ++index ) {
            const auto& set = ordered.at( index );
            settings.setArrayIndex( index );
            settings.beginGroup( QStringLiteral( "HighlighterSet" ) );
            settings.setValue( QStringLiteral( "version" ), HighlightSetVersion );
            settings.setValue( QStringLiteral( "name" ), set.name );
            settings.setValue( QStringLiteral( "id" ), set.id );
            settings.beginWriteArray( QStringLiteral( "highlighters" ),
                                      static_cast<int>( set.rules.size() ) );
            for ( int rule = 0; rule < set.rules.size(); ++rule ) {
                settings.setArrayIndex( rule );
                const auto& item = set.rules.at( rule );
                for ( auto member = item.constBegin(); member != item.constEnd(); ++member ) {
                    settings.setValue( member.key(), member.value().toVariant() );
                }
            }
            settings.endArray();
            settings.endGroup();
        }
        settings.endArray();
        settings.endGroup();
        settings.sync();
        if ( settings.status() != QSettings::NoError ) {
            return failed( unwritten );
        }
    }
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return failed( unwritten );
    }
    return file.readAll();
}

PackFiles fromActions( const QByteArray& actionsJson, QString* error )
{
    const auto reject = [ error ]( const QString& why ) {
        if ( error ) {
            *error = QStringLiteral( "The pack's actions file is not valid: %1" ).arg( why );
        }
        return PackFiles{};
    };
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( actionsJson, &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
        return reject(
            QStringLiteral( "it is not a JSON object as CILogg exports actions (a pack's "
                            "actions file has no comments)." ) );
    }
    const auto root = document.object();
    // Exactly the actions and the responses: anything else would be lost on the way to the store.
    for ( auto key = root.constBegin(); key != root.constEnd(); ++key ) {
        if ( key.key() != QLatin1String( "version" ) && key.key() != QLatin1String( "actions" )
             && key.key() != QLatin1String( "responses" ) ) {
            return reject( QStringLiteral( "it holds more than the actions and responses (%1)." )
                               .arg( key.key().left( 64 ) ) );
        }
    }
    if ( root.contains( "version" ) ) {
        const auto version = wholeId( root.value( "version" ) );
        if ( !version || ( *version != 1 && *version != 3 && *version != ActionsVersion ) ) {
            return reject( QStringLiteral( "its version is not 1, 3 or 4." ) );
        }
    }
    if ( !root.value( "actions" ).isArray() || !root.value( "responses" ).isArray() ) {
        return reject( QStringLiteral( "it needs an \"actions\" and a \"responses\" list." ) );
    }
    const auto actionValues = root.value( "actions" ).toArray();
    const auto responseValues = root.value( "responses" ).toArray();
    if ( actionValues.size() > MaxPackActionItems
         || responseValues.size() > MaxPackResponseItems ) {
        return reject( QStringLiteral( "more than %1 actions or %2 responses." )
                           .arg( MaxPackActionItems )
                           .arg( MaxPackResponseItems ) );
    }
    QString why;
    const auto actions = actionList( actionValues, QStringLiteral( "action" ), &why );
    if ( !actions ) {
        return reject( why );
    }
    const auto responses = actionList( responseValues, QStringLiteral( "response" ), &why );
    if ( !responses ) {
        return reject( why );
    }
    // A response runs actions of its own file: the pack's actions are its own.
    QSet<int> actionIds;
    for ( const auto& action : *actions ) {
        actionIds.insert( action.first );
    }
    for ( const auto& response : *responses ) {
        const auto link = responseLinkError( response.first, response.second, actionIds );
        if ( !link.isEmpty() ) {
            return reject( link );
        }
    }
    PackFiles files;
    const auto add = [ &files ]( const QVector<std::pair<int, QJsonObject>>& items,
                                 const char* folder, const char* orderFile ) {
        QStringList order;
        for ( const auto& item : items ) {
            order.append( QString::number( item.first ) );
            files.insert( folder + QString::number( item.first ) + JsonSuffix,
                          itemBytes( item.second ) );
        }
        files.insert( orderFile,
                      order.isEmpty() ? QByteArray{} : ( order.join( '\n' ) + '\n' ).toUtf8() );
    };
    add( *actions, ActionsDir, ActionOrderFile );
    add( *responses, ResponsesDir, ResponseOrderFile );
    return files;
}

QByteArray toActions( const PackFiles& files, QString* error )
{
    const auto failed = [ error ]( const QString& why ) {
        if ( error ) {
            *error
                = QStringLiteral( "The pack's actions file could not be written: %1" ).arg( why );
        }
        return QByteArray{};
    };
    QJsonObject root{ { "version", ActionsVersion } };
    const struct {
        QLatin1String kind;
        QLatin1String list;
        QLatin1String orderFile;
    } lists[] = {
        { QLatin1String( "action" ), QLatin1String( "actions" ), QLatin1String( ActionOrderFile ) },
        { QLatin1String( "response" ), QLatin1String( "responses" ),
          QLatin1String( ResponseOrderFile ) },
    };
    for ( const auto& list : lists ) {
        // By id: the items missing from the order follow in id order.
        QMap<int, QJsonObject> items;
        for ( auto file = files.cbegin(); file != files.cend(); ++file ) {
            if ( kindOf( file.key() ) != list.kind ) {
                continue;
            }
            const auto id = pathId( itemOf( file.key() ) );
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson( file.value(), &parseError );
            if ( !id || parseError.error != QJsonParseError::NoError || !document.isObject() ) {
                return failed( QStringLiteral( "%1 %2 is not a JSON object." )
                                   .arg( list.kind, itemOf( file.key() ).left( 64 ) ) );
            }
            items.insert( *id, document.object() );
        }
        QVector<std::pair<int, QJsonObject>> ordered;
        for ( const auto& line : orderLines( files.value( list.orderFile ) ) ) {
            const auto id = pathId( line );
            if ( id && items.contains( *id ) ) {
                ordered.append( { *id, items.take( *id ) } );
            }
        }
        for ( auto item = items.cbegin(); item != items.cend(); ++item ) {
            ordered.append( { item.key(), item.value() } );
        }
        // An item's id is its store path's (pack_edit keeps the two the same), and the list order
        // is the order CILogg reads.
        QJsonArray array;
        for ( qsizetype index = 0; index < ordered.size(); ++index ) {
            auto item = ordered.at( index ).second;
            item[ "id" ] = ordered.at( index ).first;
            item[ "order" ] = static_cast<int>( index );
            array.append( item );
        }
        root[ list.list ] = array;
    }
    return QJsonDocument( root ).toJson( QJsonDocument::Indented );
}

QString itemIdText( const QJsonValue& id )
{
    const auto whole = wholeId( id );
    return whole ? QString::number( *whole ) : QString{};
}

QString highlightSetError( const QByteArray& setText, const QString& id )
{
    QString why;
    const auto set = parseSet( setText, &why );
    if ( !set ) {
        return QStringLiteral( "The highlighter set is not valid: %1." ).arg( why );
    }
    if ( set->id != id ) {
        return QStringLiteral(
                   "The highlighter set's id must stay \"%1\" (copy it to make another)." )
            .arg( id.left( 64 ) );
    }
    return {};
}

QByteArray canonicalHighlightSet( const QByteArray& bytes )
{
    QString why;
    const auto set = parseSet( bytes, &why );
    return set ? setText( *set ) : QByteArray{};
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
    // The manifest check keeps these paths relative and inside the pack. Filters, highlights and
    // actions have a canonical form of their own (E16c).
    QStringList paths;
    for ( const auto& file : content.luaLoad ) {
        paths.append( filePath( content.luaFolder + '/' + file ) );
    }
    for ( const auto& decoder : content.pythonDecoders ) {
        paths.append( filePath( decoder.module ) );
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
    if ( kind == QLatin1String( "highlight" ) ) {
        return storePath == HighlightsDir + encodeName( itemOf( storePath ) ) + SetSuffix;
    }
    if ( kind == QLatin1String( "action" ) || kind == QLatin1String( "response" ) ) {
        const auto id = pathId( itemOf( storePath ) );
        const QLatin1String folder( kind == QLatin1String( "action" ) ? ActionsDir : ResponsesDir );
        return id && storePath == folder + QString::number( *id ) + JsonSuffix;
    }
    if ( kind == QLatin1String( "file" ) ) {
        const auto relative = itemOf( storePath );
        return !relative.isEmpty() && !QDir::isAbsolutePath( relative ) && !relative.contains( ':' )
               && !relative.contains( '\\' ) && filePath( relative ) == storePath
               && !relative.startsWith( QStringLiteral( "../" ) ) && relative != QLatin1String( ".." );
    }
    return !kind.isEmpty();
}

PackFiles fromLegacyDocuments( PackFiles files, const QJsonObject& manifest )
{
    PackContent content;
    if ( !validatePackManifest( manifest, &content ).isEmpty() ) {
        return files;
    }
    struct Document {
        QString path;
        QLatin1String order;
        PackFiles ( *convert )( const QByteArray&, QString* );
    };
    const Document documents[] = {
        { content.filters, QLatin1String( FilterOrderFile ), &fromFilters },
        { content.highlights, QLatin1String( HighlightOrderFile ), &fromHighlights },
        { content.actions, QLatin1String( ActionOrderFile ), &fromActions },
    };
    for ( const auto& document : documents ) {
        const auto legacy = filePath( document.path );
        if ( document.path.isEmpty() || !files.contains( legacy ) ) {
            continue;
        }
        if ( files.contains( document.order ) ) {
            files.remove( legacy );
            continue;
        }
        QString error;
        const auto items = document.convert( files.value( legacy ), &error );
        if ( !error.isEmpty() ) {
            continue;
        }
        files.remove( legacy );
        for ( auto item = items.cbegin(); item != items.cend(); ++item ) {
            files.insert( item.key(), item.value() );
        }
    }
    return files;
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
    if ( !content.highlights.isEmpty() && readError.isEmpty() ) {
        const auto sets = fromHighlights(
            readFile( QDir( directory ).filePath( content.highlights ), &readError ), &readError );
        for ( auto set = sets.cbegin(); set != sets.cend(); ++set ) {
            files.insert( set.key(), set.value() );
        }
    }
    if ( !content.actions.isEmpty() && readError.isEmpty() ) {
        const auto items = fromActions(
            readFile( QDir( directory ).filePath( content.actions ), &readError ), &readError );
        for ( auto item = items.cbegin(); item != items.cend(); ++item ) {
            files.insert( item.key(), item.value() );
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
    if ( storePath == HighlightOrderFile ) {
        return QStringLiteral( "highlight-order" );
    }
    if ( storePath.startsWith( HighlightsDir ) && storePath.endsWith( SetSuffix )
         && !storePath.mid( 11 ).contains( '/' ) ) {
        return QStringLiteral( "highlight" );
    }
    if ( storePath.startsWith( FiltersDir ) && storePath.endsWith( JsonSuffix )
         && !storePath.mid( 8 ).contains( '/' ) ) {
        return QStringLiteral( "filter" );
    }
    if ( storePath == ActionOrderFile ) {
        return QStringLiteral( "action-order" );
    }
    if ( storePath == ResponseOrderFile ) {
        return QStringLiteral( "response-order" );
    }
    if ( storePath.startsWith( ActionsDir ) && storePath.endsWith( JsonSuffix )
         && !storePath.mid( 8 ).contains( '/' ) ) {
        return QStringLiteral( "action" );
    }
    if ( storePath.startsWith( ResponsesDir ) && storePath.endsWith( JsonSuffix )
         && !storePath.mid( 10 ).contains( '/' ) ) {
        return QStringLiteral( "response" );
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
    if ( kind == "highlight" ) {
        return decodeName( storePath.mid( 11, storePath.size() - 11 - 4 ) );
    }
    if ( kind == "action" ) {
        return decodeName( storePath.mid( 8, storePath.size() - 8 - 5 ) );
    }
    if ( kind == "response" ) {
        return decodeName( storePath.mid( 10, storePath.size() - 10 - 5 ) );
    }
    if ( kind == "file" ) {
        return storePath.mid( 6 );
    }
    return storePath;
}
} // namespace cilogg::plugins::packcanonical
