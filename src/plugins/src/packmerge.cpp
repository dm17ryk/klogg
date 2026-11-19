#include "packmerge.h"

#include "packcanonical.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <git2.h>

namespace cilogg::plugins::packmerge {
namespace {
struct Segment {
    QString name;
    bool element = false;
};

QString escape( const QString& segment )
{
    auto escaped = segment;
    escaped.replace( '~', QStringLiteral( "~0" ) ).replace( '/', QStringLiteral( "~1" ) );
    if ( escaped.startsWith( '@' ) ) {
        escaped.replace( 0, 1, QStringLiteral( "~2" ) );
    }
    return escaped;
}

QString unescape( const QString& segment )
{
    auto text = segment;
    if ( text.startsWith( QStringLiteral( "~2" ) ) ) {
        text.replace( 0, 2, QStringLiteral( "@" ) );
    }
    return text.replace( QStringLiteral( "~1" ), QStringLiteral( "/" ) )
        .replace( QStringLiteral( "~0" ), QStringLiteral( "~" ) );
}

QVector<Segment> segments( const QString& path )
{
    QVector<Segment> result;
    if ( path.isEmpty() ) {
        return result;
    }
    for ( const auto& part : path.split( '/' ) ) {
        const bool element = part.startsWith( '@' );
        result.append( { unescape( element ? part.mid( 1 ) : part ), element } );
    }
    return result;
}

bool same( const std::optional<QJsonValue>& left, const std::optional<QJsonValue>& right )
{
    return left.has_value() == right.has_value() && ( !left || *left == *right );
}

bool same( const std::optional<QByteArray>& left, const std::optional<QByteArray>& right )
{
    return left.has_value() == right.has_value() && ( !left || *left == *right );
}

// Arrays whose elements are objects with a unique, non-empty `name` (preview fields).
bool named( const QJsonArray& array )
{
    QSet<QString> names;
    for ( const auto& item : array ) {
        const auto name = item.toObject().value( "name" ).toString();
        if ( !item.isObject() || name.isEmpty() || names.contains( name ) ) {
            return false;
        }
        names.insert( name );
    }
    return true;
}

int indexOf( const QJsonArray& array, const QString& name )
{
    for ( int index = 0; index < array.size(); ++index ) {
        if ( array.at( index ).toObject().value( "name" ).toString() == name ) {
            return index;
        }
    }
    return -1;
}

std::optional<QJsonValue> element( const std::optional<QJsonValue>& array, const QString& name )
{
    if ( !array ) {
        return std::nullopt;
    }
    const auto items = array->toArray();
    const auto index = indexOf( items, name );
    return index < 0 ? std::nullopt : std::optional<QJsonValue>( items.at( index ) );
}

std::optional<QJsonValue> member( const std::optional<QJsonValue>& object, const QString& key )
{
    if ( !object || !object->toObject().contains( key ) ) {
        return std::nullopt;
    }
    return object->toObject().value( key );
}

QByteArray compact( const std::optional<QJsonValue>& value )
{
    if ( !value ) {
        return {};
    }
    // A bare value is written inside an array and unwrapped, so every JSON type round-trips.
    const auto text = QJsonDocument( QJsonArray{ *value } ).toJson( QJsonDocument::Compact );
    return text.mid( 1, text.size() - 2 );
}

void ensureLibgit2()
{
    static const int initialized = git_libgit2_init();
    Q_UNUSED( initialized );
}
} // namespace

QString fieldPath( const QString& parent, const QString& segment, bool isElement )
{
    const auto part = ( isElement ? QStringLiteral( "@" ) : QString{} ) + escape( segment );
    return parent.isEmpty() ? part : parent + '/' + part;
}

std::optional<QJsonValue> field( const QJsonValue& root, const QString& path )
{
    std::optional<QJsonValue> current = root;
    for ( const auto& segment : segments( path ) ) {
        current = segment.element ? element( current, segment.name ) : member( current, segment.name );
        if ( !current ) {
            return std::nullopt;
        }
    }
    return current;
}

QJsonValue withField( const QJsonValue& root, const QString& path,
                      const std::optional<QJsonValue>& value )
{
    const auto parts = segments( path );
    if ( parts.isEmpty() ) {
        return value.value_or( QJsonValue{} );
    }
    const auto& first = parts.first();
    QStringList restParts;
    for ( int index = 1; index < parts.size(); ++index ) {
        restParts.append( fieldPath( {}, parts.at( index ).name, parts.at( index ).element ) );
    }
    const auto rest = restParts.join( '/' );
    if ( first.element ) {
        auto array = root.toArray();
        const auto index = indexOf( array, first.name );
        if ( index < 0 ) {
            if ( value && rest.isEmpty() ) {
                array.append( *value );
            }
            return array;
        }
        if ( rest.isEmpty() && !value ) {
            array.removeAt( index );
        }
        else {
            array[ index ] = rest.isEmpty() ? *value : withField( array.at( index ), rest, value );
        }
        return array;
    }
    auto object = root.toObject();
    if ( rest.isEmpty() ) {
        if ( value ) {
            object[ first.name ] = *value;
        }
        else {
            object.remove( first.name );
        }
    }
    else {
        object[ first.name ] = withField( object.value( first.name ), rest, value );
    }
    return object;
}

JsonMerge mergeJson( const std::optional<QJsonValue>& base, const std::optional<QJsonValue>& mine,
                     const std::optional<QJsonValue>& theirs, MergeStrategy strategy,
                     const QString& path )
{
    // Not edited locally: the new version. Edited only locally: the edit. The same change on
    // both sides: no conflict.
    if ( same( mine, base ) ) {
        return { theirs, {} };
    }
    if ( same( theirs, base ) || same( mine, theirs ) ) {
        return { mine, {} };
    }
    // Changed on both sides: objects merge member by member, arrays of named elements element
    // by element (an item added on both sides merges against nothing).
    if ( mine && theirs && mine->isObject() && theirs->isObject()
         && ( !base || base->isObject() ) ) {
        JsonMerge merged{ QJsonObject{}, {} };
        QStringList keys = mine->toObject().keys() + theirs->toObject().keys();
        if ( base ) {
            keys += base->toObject().keys();
        }
        keys.removeDuplicates();
        QJsonObject object;
        for ( const auto& key : keys ) {
            const auto part = mergeJson( member( base, key ), member( mine, key ),
                                         member( theirs, key ), strategy,
                                         fieldPath( path, key ) );
            merged.conflicts += part.conflicts;
            if ( part.value ) {
                object.insert( key, *part.value );
            }
        }
        merged.value = object;
        return merged;
    }
    if ( mine && theirs && mine->isArray() && theirs->isArray() && named( mine->toArray() )
         && named( theirs->toArray() ) && ( !base || ( base->isArray() && named( base->toArray() ) ) ) ) {
        JsonMerge merged;
        QJsonArray array;
        // The local order, then elements new in the new version in its order.
        QStringList names;
        for ( const auto& item : mine->toArray() ) {
            names.append( item.toObject().value( "name" ).toString() );
        }
        for ( const auto& item : theirs->toArray() ) {
            const auto name = item.toObject().value( "name" ).toString();
            if ( !names.contains( name ) ) {
                names.append( name );
            }
        }
        for ( const auto& name : names ) {
            const auto part = mergeJson( element( base, name ), element( mine, name ),
                                         element( theirs, name ), strategy,
                                         fieldPath( path, name, true ) );
            merged.conflicts += part.conflicts;
            if ( part.value ) {
                array.append( *part.value );
            }
        }
        merged.value = array;
        return merged;
    }
    // The same value changed differently, or deleted on one side and changed on the other.
    return { strategy == MergeStrategy::TakeTheirs ? theirs : mine, { path } };
}

TextMerge mergeText( const QByteArray& base, const QByteArray& mine, const QByteArray& theirs,
                     MergeStrategy strategy )
{
    ensureLibgit2();
    const auto input = []( const QByteArray& bytes ) {
        // The init functions, not the INIT macros: those leave fields to zero-initialization,
        // which -Wmissing-field-initializers reports on the Unix builds.
        git_merge_file_input file{};
        git_merge_file_input_init( &file, GIT_MERGE_FILE_INPUT_VERSION );
        file.ptr = bytes.constData();
        file.size = static_cast<size_t>( bytes.size() );
        file.mode = 0100644;
        return file;
    };
    const auto ancestor = input( base );
    const auto ours = input( mine );
    const auto other = input( theirs );
    git_merge_file_options options{};
    git_merge_file_options_init( &options, GIT_MERGE_FILE_OPTIONS_VERSION );
    options.ancestor_label = "installed version";
    options.our_label = "your edit";
    options.their_label = "new version";
    options.flags = GIT_MERGE_FILE_STYLE_DIFF3;
    options.favor = strategy == MergeStrategy::KeepMine     ? GIT_MERGE_FILE_FAVOR_OURS
                    : strategy == MergeStrategy::TakeTheirs ? GIT_MERGE_FILE_FAVOR_THEIRS
                                                            : GIT_MERGE_FILE_FAVOR_NORMAL;
    git_merge_file_result result{};
    TextMerge merged;
    if ( git_merge_file( &result, &ancestor, &ours, &other, &options ) != 0 ) {
        // No merge was possible: the file stays as edited and is reported.
        merged.text = strategy == MergeStrategy::TakeTheirs ? theirs : mine;
        merged.conflict = true;
        return merged;
    }
    merged.text = QByteArray( result.ptr, static_cast<qsizetype>( result.len ) );
    merged.conflict = result.automergeable == 0;
    git_merge_file_result_free( &result );
    // With a favoured side the hunks were resolved, but they are still reported as conflicts.
    if ( strategy != MergeStrategy::Fail && !merged.conflict ) {
        const auto clean = mergeText( base, mine, theirs, MergeStrategy::Fail );
        merged.conflict = clean.conflict;
    }
    return merged;
}

PackFileMerge mergeFile( const QString& path, const std::optional<QByteArray>& base,
                         const std::optional<QByteArray>& mine,
                         const std::optional<QByteArray>& theirs, MergeStrategy strategy )
{
    if ( same( mine, base ) ) {
        return { theirs, {} };
    }
    if ( same( theirs, base ) || same( mine, theirs ) ) {
        return { mine, {} };
    }
    const auto kind = packcanonical::kindOf( path );
    const auto item = packcanonical::itemOf( path );
    if ( kind == "preview" || kind == "block" || kind == "meta" ) {
        const auto parse = []( const std::optional<QByteArray>& bytes ) -> std::optional<QJsonValue> {
            if ( !bytes ) {
                return std::nullopt;
            }
            return QJsonValue( QJsonDocument::fromJson( *bytes ).object() );
        };
        const auto baseValue = parse( base );
        const auto mineValue = parse( mine );
        const auto theirsValue = parse( theirs );
        const auto merged = mergeJson( baseValue, mineValue, theirsValue, strategy );
        PackFileMerge result;
        if ( merged.value ) {
            result.result = packcanonical::itemBytes( merged.value->toObject() );
        }
        for ( const auto& conflict : merged.conflicts ) {
            const auto valueAt = [ &conflict ]( const std::optional<QJsonValue>& root ) {
                return root ? compact( field( *root, conflict ) ) : QByteArray{};
            };
            result.conflicts.append( { path, kind, item, conflict, valueAt( baseValue ),
                                       valueAt( mineValue ), valueAt( theirsValue ) } );
        }
        return result;
    }
    // Text: deleted on one side and changed on the other is a conflict of the whole file.
    if ( !mine || !theirs ) {
        PackFileMerge result{ strategy == MergeStrategy::TakeTheirs ? theirs : mine, {} };
        result.conflicts.append( { path, kind, item, {}, base.value_or( QByteArray{} ),
                                   mine.value_or( QByteArray{} ), theirs.value_or( QByteArray{} ) } );
        return result;
    }
    const auto merged = mergeText( base.value_or( QByteArray{} ), *mine, *theirs, strategy );
    PackFileMerge result{ merged.text, {} };
    if ( merged.conflict ) {
        result.conflicts.append(
            { path, kind, item, {}, base.value_or( QByteArray{} ), *mine, *theirs } );
    }
    return result;
}
} // namespace cilogg::plugins::packmerge
