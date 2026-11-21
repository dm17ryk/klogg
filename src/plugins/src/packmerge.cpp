#include "packmerge.h"

#include "packcanonical.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
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

namespace {
// Field conflicts kept for one merged value: one with more is a single conflict of the whole
// value (as a job lists such an item), so a large item never holds thousands of field paths.
constexpr qsizetype MaxFieldConflicts = 32;

// Where the merge is, from the merged value down. It becomes a field path only for a conflict:
// a 1 MiB item can have tens of thousands of leaves under long keys.
struct PathLink {
    const PathLink* parent = nullptr;
    const QString* segment = nullptr;
    bool element = false;
};

struct MergeState {
    MergeStrategy strategy = MergeStrategy::Fail;
    QString root;
    QStringList conflicts;
    bool overflow = false;
};

QString materialize( const QString& root, const PathLink* link )
{
    QStringList parts;
    for ( ; link; link = link->parent ) {
        parts.append( fieldPath( {}, *link->segment, link->element ) );
    }
    std::reverse( parts.begin(), parts.end() );
    if ( !root.isEmpty() ) {
        parts.prepend( root );
    }
    return parts.join( '/' );
}

// A conflict at `at`: recorded (up to MaxFieldConflicts), and the side the strategy takes.
std::optional<QJsonValue> conflict( const std::optional<QJsonValue>& mine,
                                    const std::optional<QJsonValue>& theirs, const PathLink* at,
                                    MergeState& state )
{
    if ( state.conflicts.size() < MaxFieldConflicts ) {
        state.conflicts.append( materialize( state.root, at ) );
    }
    else {
        state.overflow = true;
    }
    return state.strategy == MergeStrategy::TakeTheirs ? theirs : mine;
}

// The names of an array of named elements, in its order.
QStringList namesOf( const QJsonArray& array )
{
    QStringList names;
    names.reserve( array.size() );
    for ( const auto& item : array ) {
        names.append( item.toObject().value( "name" ).toString() );
    }
    return names;
}

// Whether `a` and `b` order the names they share the same way (`inA`, `inB`: their names).
template <typename Index>
bool sameOrder( const QStringList& a, const Index& inA, const QStringList& b, const Index& inB )
{
    QStringList left;
    QStringList right;
    for ( const auto& name : a ) {
        if ( inB.contains( name ) ) {
            left.append( name );
        }
    }
    for ( const auto& name : b ) {
        if ( inA.contains( name ) ) {
            right.append( name );
        }
    }
    return left == right;
}

// `skeleton`'s order, with each name only `other` has placed after the nearest name before it in
// `other` that `skeleton` has (first when there is none): an insertion keeps its position.
QStringList interleave( const QStringList& skeleton, const QStringList& other )
{
    const QSet<QString> inSkeleton( skeleton.cbegin(), skeleton.cend() );
    QHash<QString, QStringList> after;
    QStringList first;
    const QString* anchor = nullptr;
    for ( const auto& name : other ) {
        if ( inSkeleton.contains( name ) ) {
            anchor = &name;
        }
        else {
            ( anchor ? after[ *anchor ] : first ).append( name );
        }
    }
    QStringList names = first;
    for ( const auto& name : skeleton ) {
        names.append( name );
        names += after.value( name );
    }
    return names;
}

// Each element of an array of named elements by its name.
QHash<QString, qsizetype> byName( const QJsonArray& array )
{
    QHash<QString, qsizetype> index;
    index.reserve( array.size() );
    for ( qsizetype at = 0; at < array.size(); ++at ) {
        index.insert( array.at( at ).toObject().value( "name" ).toString(), at );
    }
    return index;
}

std::optional<QJsonValue> elementNamed( const QJsonArray& array,
                                        const QHash<QString, qsizetype>& index, const QString& name )
{
    const auto at = index.constFind( name );
    return at == index.cend() ? std::nullopt : std::optional<QJsonValue>( array.at( *at ) );
}

std::optional<QJsonValue> mergeValue( const std::optional<QJsonValue>& base,
                                      const std::optional<QJsonValue>& mine,
                                      const std::optional<QJsonValue>& theirs, const PathLink* at,
                                      MergeState& state )
{
    // Not edited locally: the new version. Edited only locally: the edit. The same change on
    // both sides: no conflict.
    if ( same( mine, base ) ) {
        return theirs;
    }
    if ( same( theirs, base ) || same( mine, theirs ) ) {
        return mine;
    }
    // Changed on both sides: objects merge member by member, arrays of named elements element
    // by element (an item added on both sides merges against nothing).
    if ( mine && theirs && mine->isObject() && theirs->isObject()
         && ( !base || base->isObject() ) ) {
        QStringList keys = mine->toObject().keys() + theirs->toObject().keys();
        if ( base ) {
            keys += base->toObject().keys();
        }
        keys.removeDuplicates();
        QJsonObject object;
        for ( const auto& key : keys ) {
            const PathLink link{ at, &key, false };
            const auto part = mergeValue( member( base, key ), member( mine, key ),
                                          member( theirs, key ), &link, state );
            if ( part ) {
                object.insert( key, *part );
            }
        }
        return object;
    }
    if ( mine && theirs && mine->isArray() && theirs->isArray() && named( mine->toArray() )
         && named( theirs->toArray() ) && ( !base || ( base->isArray() && named( base->toArray() ) ) ) ) {
        const auto baseItems = base ? base->toArray() : QJsonArray{};
        const auto mineItems = mine->toArray();
        const auto theirItems = theirs->toArray();
        const auto baseIndex = byName( baseItems );
        const auto mineIndex = byName( mineItems );
        const auto theirIndex = byName( theirItems );
        const auto baseNames = namesOf( baseItems );
        const auto mineNames = namesOf( mineItems );
        const auto theirNames = namesOf( theirItems );
        // The order merges like the elements: a side that keeps the installed version's order of
        // the elements it has leaves the order to the other; two different reorderings conflict.
        const bool mineReordered = base && !sameOrder( mineNames, mineIndex, baseNames, baseIndex );
        const bool theirReordered = base && !sameOrder( theirNames, theirIndex, baseNames, baseIndex );
        // An element both sides added (the installed version has none of that name) is placed
        // by both: where they put it differently, the order conflicts too.
        const bool bothAdded = std::any_of( mineNames.cbegin(), mineNames.cend(),
                                            [ &theirIndex, &baseIndex ]( const QString& name ) {
                                                return theirIndex.contains( name )
                                                       && !baseIndex.contains( name );
                                            } );
        const bool sameShared = sameOrder( mineNames, mineIndex, theirNames, theirIndex );
        if ( ( mineReordered && theirReordered && !sameShared ) || ( bothAdded && !sameShared ) ) {
            return conflict( mine, theirs, at, state );
        }
        // One side's order, with the elements only the other has where the other put them.
        const auto names = theirReordered && !mineReordered ? interleave( theirNames, mineNames )
                                                           : interleave( mineNames, theirNames );
        QJsonArray array;
        for ( const auto& name : names ) {
            const PathLink link{ at, &name, true };
            const auto part = mergeValue( base ? elementNamed( baseItems, baseIndex, name ) : std::nullopt,
                                          elementNamed( mineItems, mineIndex, name ),
                                          elementNamed( theirItems, theirIndex, name ), &link, state );
            if ( part ) {
                array.append( *part );
            }
        }
        return array;
    }
    // The same value changed differently, or deleted on one side and changed on the other.
    return conflict( mine, theirs, at, state );
}
} // namespace

JsonMerge mergeJson( const std::optional<QJsonValue>& base, const std::optional<QJsonValue>& mine,
                     const std::optional<QJsonValue>& theirs, MergeStrategy strategy,
                     const QString& path )
{
    MergeState state{ strategy, path, {}, false };
    auto value = mergeValue( base, mine, theirs, nullptr, state );
    // More conflicting fields than are kept: the value is one conflict, settled as a whole.
    return { std::move( value ), state.overflow ? QStringList{ path } : state.conflicts };
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
