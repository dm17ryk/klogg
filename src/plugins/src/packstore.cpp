#include "packstore.h"

#include "configpack.h"
#include "log.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <git2.h>

namespace cilogg::plugins {
namespace {
constexpr char ManifestPath[] = "manifest.json";
constexpr char Upstream[] = "upstream";
constexpr char Local[] = "local";

void ensureLibgit2()
{
    static const int initialized = git_libgit2_init();
    Q_UNUSED( initialized );
}

QString lastError( const QString& what )
{
    const auto* error = git_error_last();
    return what + QStringLiteral( ": " )
           + ( error && error->message ? QString::fromUtf8( error->message )
                                       : QStringLiteral( "unknown error" ) );
}

QString oidText( const git_oid& oid )
{
    char text[ GIT_OID_MAX_HEXSIZE + 1 ] = {};
    git_oid_tostr( text, sizeof( text ), &oid );
    return QString::fromLatin1( text );
}

// Owns a libgit2 object for the length of a scope.
template <typename T, void ( *Free )( T* )> struct Owned {
    T* pointer = nullptr;
    Owned() = default;
    Owned( const Owned& ) = delete;
    Owned& operator=( const Owned& ) = delete;
    ~Owned()
    {
        if ( pointer ) {
            Free( pointer );
        }
    }
};
using Tree = Owned<git_tree, git_tree_free>;
using Commit = Owned<git_commit, git_commit_free>;
using Index = Owned<git_index, git_index_free>;
using Object = Owned<git_object, git_object_free>;
using Signature = Owned<git_signature, git_signature_free>;
using Walk = Owned<git_revwalk, git_revwalk_free>;
using Reference = Owned<git_reference, git_reference_free>;

struct TreeFiles {
    git_repository* repository;
    PackFiles* files;
};

int collect( const char* root, const git_tree_entry* entry, void* payload )
{
    if ( git_tree_entry_type( entry ) != GIT_OBJECT_BLOB ) {
        return 0;
    }
    auto* state = static_cast<TreeFiles*>( payload );
    Owned<git_blob, git_blob_free> blob;
    if ( git_blob_lookup( &blob.pointer, state->repository, git_tree_entry_id( entry ) ) != 0 ) {
        return -1;
    }
    const auto* data = static_cast<const char*>( git_blob_rawcontent( blob.pointer ) );
    const auto size = static_cast<qsizetype>( git_blob_rawsize( blob.pointer ) );
    state->files->insert( QString::fromUtf8( root ) + QString::fromUtf8( git_tree_entry_name( entry ) ),
                          QByteArray( data, size ) );
    return 0;
}

PackFiles withoutManifest( PackFiles files )
{
    files.remove( ManifestPath );
    return files;
}
} // namespace

PackStore::PackStore( const QString& root, const QString& packId )
    : root_( root )
    , packId_( packId )
{
    ensureLibgit2();
}

PackStore::~PackStore()
{
    if ( repository_ ) {
        git_repository_free( repository_ );
    }
}

bool PackStore::open( bool create ) const
{
    if ( repository_ ) {
        return true;
    }
    const auto path = QDir( root_ ).filePath( packId_ + QStringLiteral( ".git" ) );
    const auto native = QDir::toNativeSeparators( path ).toUtf8();
    if ( QFileInfo::exists( path ) ) {
        if ( git_repository_open_bare( &repository_, native.constData() ) != 0 ) {
            error_ = lastError( QStringLiteral( "Cannot open the pack store" ) );
            repository_ = nullptr;
            return false;
        }
        return true;
    }
    if ( !create ) {
        return false;
    }
    if ( !QDir().mkpath( path ) || git_repository_init( &repository_, native.constData(), 1 ) != 0 ) {
        error_ = lastError( QStringLiteral( "Cannot create the pack store" ) );
        repository_ = nullptr;
        return false;
    }
    return true;
}

bool PackStore::exists() const
{
    return open( false );
}

QString PackStore::contentDirectory() const
{
    const auto path = QDir( root_ ).filePath( packId_ + QStringLiteral( "/current" ) );
    return QFileInfo( path ).isDir() ? path : QString{};
}

QString PackStore::commit( const char* branch, const PackFiles& files, const QString& message,
                           const QStringList& extraParents )
{
    if ( !open( true ) ) {
        return error_;
    }
    Index index;
    if ( git_index_new( &index.pointer ) != 0 ) {
        return lastError( QStringLiteral( "Cannot prepare a pack store commit" ) );
    }
    for ( auto file = files.cbegin(); file != files.cend(); ++file ) {
        // The blob first: an in-memory index takes entries by object ID.
        const auto path = file.key().toUtf8();
        git_index_entry entry{};
        entry.mode = GIT_FILEMODE_BLOB;
        entry.path = path.constData();
        entry.file_size = static_cast<uint32_t>( file.value().size() );
        if ( git_blob_create_from_buffer( &entry.id, repository_, file.value().constData(),
                                          static_cast<size_t>( file.value().size() ) )
                 != 0
             || git_index_add( index.pointer, &entry ) != 0 ) {
            return lastError( QStringLiteral( "Cannot store %1" ).arg( file.key() ) );
        }
    }
    git_oid treeId;
    Tree tree;
    if ( git_index_write_tree_to( &treeId, index.pointer, repository_ ) != 0
         || git_tree_lookup( &tree.pointer, repository_, &treeId ) != 0 ) {
        return lastError( QStringLiteral( "Cannot write the pack store tree" ) );
    }
    // The branch tip first: libgit2 moves the branch only from its current tip.
    std::vector<std::unique_ptr<Commit>> parents;
    const auto reference = QStringLiteral( "refs/heads/%1" ).arg( QLatin1String( branch ) );
    QStringList parentIds;
    git_oid tip;
    if ( git_reference_name_to_id( &tip, repository_, reference.toUtf8().constData() ) == 0 ) {
        parentIds.append( oidText( tip ) );
    }
    parentIds += extraParents;
    std::vector<const git_commit*> parentPointers;
    for ( const auto& parentId : parentIds ) {
        git_oid oid;
        auto parent = std::make_unique<Commit>();
        if ( git_oid_fromstr( &oid, parentId.toLatin1().constData() ) != 0
             || git_commit_lookup( &parent->pointer, repository_, &oid ) != 0 ) {
            return lastError( QStringLiteral( "Cannot find a pack store commit" ) );
        }
        parentPointers.push_back( parent->pointer );
        parents.push_back( std::move( parent ) );
    }
    Signature signature;
    if ( git_signature_now( &signature.pointer, "CILogg", "cilogg@localhost" ) != 0 ) {
        return lastError( QStringLiteral( "Cannot sign a pack store commit" ) );
    }
    git_oid commitId;
    if ( git_commit_create( &commitId, repository_, reference.toUtf8().constData(),
                           signature.pointer, signature.pointer, "UTF-8",
                           message.toUtf8().constData(), tree.pointer, parentPointers.size(),
                           parentPointers.data() )
         != 0 ) {
        return lastError( QStringLiteral( "Cannot commit to the pack store" ) );
    }
    LOG_DEBUG << "Pack store commit pack=" << packId_.toStdString() << " branch=" << branch
              << " id=" << oidText( commitId ).toStdString() << " files=" << files.size();
    return {};
}

PackFiles PackStore::filesAt( const QString& reference ) const
{
    PackFiles files;
    if ( !open( false ) ) {
        return files;
    }
    Object object;
    if ( git_revparse_single( &object.pointer, repository_,
                              ( reference + QStringLiteral( "^{tree}" ) ).toUtf8().constData() )
         != 0 ) {
        return files;
    }
    TreeFiles state{ repository_, &files };
    git_tree_walk( reinterpret_cast<git_tree*>( object.pointer ), GIT_TREEWALK_PRE, collect,
                   &state );
    return files;
}

PackFiles PackStore::files() const
{
    return withoutManifest( filesAt( QStringLiteral( "refs/heads/local" ) ) );
}

PackFiles PackStore::upstreamFiles() const
{
    return withoutManifest( filesAt( QStringLiteral( "refs/heads/upstream" ) ) );
}

QStringList PackStore::localEdits() const
{
    const auto mine = files();
    const auto base = upstreamFiles();
    QStringList paths = mine.keys() + base.keys();
    paths.removeDuplicates();
    QStringList edited;
    for ( const auto& path : paths ) {
        if ( mine.value( path ) != base.value( path ) || mine.contains( path ) != base.contains( path ) ) {
            edited.append( path );
        }
    }
    return edited;
}

QString PackStore::writeContent( const PackFiles& files, const QJsonObject& manifest ) const
{
    PackContent content;
    const auto invalid = validatePackManifest( manifest, &content );
    if ( !invalid.isEmpty() ) {
        return invalid;
    }
    const QDir packDir( QDir( root_ ).filePath( packId_ ) );
    const auto staged = packDir.filePath( QStringLiteral( "current.new" ) );
    QDir( staged ).removeRecursively();
    const auto write = [ &staged ]( const QString& relative, const QByteArray& bytes ) {
        const auto path = QDir( staged ).filePath( relative );
        QDir().mkpath( QFileInfo( path ).absolutePath() );
        QSaveFile file( path );
        return file.open( QIODevice::WriteOnly ) && file.write( bytes ) == bytes.size()
               && file.commit();
    };
    bool ok = write( QStringLiteral( "plugin.json" ),
                     QJsonDocument( manifest ).toJson( QJsonDocument::Indented ) );
    if ( !content.previews.isEmpty() ) {
        ok = ok && write( content.previews, packcanonical::toPreviews( files ) );
    }
    for ( auto file = files.cbegin(); file != files.cend(); ++file ) {
        if ( packcanonical::kindOf( file.key() ) == QLatin1String( "file" ) ) {
            ok = ok && write( packcanonical::itemOf( file.key() ), file.value() );
        }
    }
    // The Lua folder exists even when every Lua file was removed.
    if ( !content.luaFolder.isEmpty() ) {
        QDir().mkpath( QDir( staged ).filePath( content.luaFolder ) );
    }
    const auto current = packDir.filePath( QStringLiteral( "current" ) );
    const auto old = packDir.filePath( QStringLiteral( "current.old" ) );
    QDir( old ).removeRecursively();
    if ( !ok || ( QFileInfo::exists( current ) && !QDir().rename( current, old ) )
         || !QDir().rename( staged, current ) ) {
        if ( !QFileInfo::exists( current ) && QFileInfo::exists( old ) ) {
            QDir().rename( old, current );
        }
        return QStringLiteral( "Cannot write the pack's content folder." );
    }
    QDir( old ).removeRecursively();
    return {};
}

QString PackStore::install( const PackFiles& files, const QJsonObject& manifest,
                            const QString& version, const QString& source )
{
    if ( exists() ) {
        return applyUpdate( files, files, manifest, version, source );
    }
    auto stored = files;
    stored.insert( ManifestPath, QJsonDocument( manifest ).toJson( QJsonDocument::Indented ) );
    const auto message = QStringLiteral( "Install %1 from %2" ).arg( version, source );
    auto error = commit( Upstream, stored, message );
    if ( error.isEmpty() ) {
        git_oid tip;
        Reference local;
        if ( git_reference_name_to_id( &tip, repository_, "refs/heads/upstream" ) != 0
             || git_reference_create( &local.pointer, repository_, "refs/heads/local", &tip, 0,
                                      message.toUtf8().constData() )
                    != 0 ) {
            error = lastError( QStringLiteral( "Cannot create the pack store" ) );
        }
    }
    return error.isEmpty() ? writeContent( files, manifest ) : error;
}

QString PackStore::commitEdit( const QString& storePath, const std::optional<QByteArray>& bytes,
                               const QString& message )
{
    auto stored = filesAt( QStringLiteral( "refs/heads/local" ) );
    if ( stored.isEmpty() ) {
        return QStringLiteral( "The pack has no store." );
    }
    if ( bytes ) {
        stored.insert( storePath, *bytes );
    }
    else {
        stored.remove( storePath );
    }
    const auto error = commit( Local, stored, message );
    return error.isEmpty() ? writeContent( withoutManifest( stored ),
                                           QJsonDocument::fromJson( stored.value( ManifestPath ) ).object() )
                           : error;
}

PackUpdatePlan PackStore::planUpdate( const PackFiles& newFiles, MergeStrategy strategy ) const
{
    PackUpdatePlan plan;
    const auto base = upstreamFiles();
    const auto mine = files();
    QStringList paths = base.keys() + mine.keys() + newFiles.keys();
    paths.removeDuplicates();
    paths.removeAll( ManifestPath );
    const auto side = []( const PackFiles& files, const QString& path ) {
        return files.contains( path ) ? std::optional<QByteArray>( files.value( path ) )
                                      : std::nullopt;
    };
    for ( const auto& path : paths ) {
        const auto merged = packmerge::mergeFile( path, side( base, path ), side( mine, path ),
                                                  side( newFiles, path ), strategy );
        if ( merged.result ) {
            plan.merged.insert( path, *merged.result );
        }
        plan.conflicts += merged.conflicts;
    }
    return plan;
}

QString PackStore::applyUpdate( const PackFiles& newFiles, const PackFiles& merged,
                                const QJsonObject& manifest, const QString& version,
                                const QString& source )
{
    const auto manifestBytes = QJsonDocument( manifest ).toJson( QJsonDocument::Indented );
    auto shipped = newFiles;
    shipped.insert( ManifestPath, manifestBytes );
    auto result = merged;
    result.insert( ManifestPath, manifestBytes );
    auto error = commit( Upstream, shipped, QStringLiteral( "Install %1 from %2" ).arg( version, source ) );
    git_oid upstreamTip;
    if ( error.isEmpty()
         && git_reference_name_to_id( &upstreamTip, repository_, "refs/heads/upstream" ) != 0 ) {
        error = lastError( QStringLiteral( "Cannot find the installed version" ) );
    }
    if ( error.isEmpty() ) {
        error = commit( Local, result, QStringLiteral( "Merge %1" ).arg( version ),
                        { oidText( upstreamTip ) } );
    }
    return error.isEmpty() ? writeContent( merged, manifest ) : error;
}

QVector<PackCommit> PackStore::history( const QString& storePath ) const
{
    QVector<PackCommit> commits;
    if ( !open( false ) ) {
        return commits;
    }
    // Commits reachable from `upstream` are versions as shipped.
    QSet<QString> shipped;
    {
        Walk walk;
        if ( git_revwalk_new( &walk.pointer, repository_ ) == 0
             && git_revwalk_push_ref( walk.pointer, "refs/heads/upstream" ) == 0 ) {
            git_oid oid;
            while ( git_revwalk_next( &oid, walk.pointer ) == 0 ) {
                shipped.insert( oidText( oid ) );
            }
        }
    }
    Walk walk;
    if ( git_revwalk_new( &walk.pointer, repository_ ) != 0
         || git_revwalk_push_ref( walk.pointer, "refs/heads/local" ) != 0 ) {
        return commits;
    }
    git_revwalk_sorting( walk.pointer, GIT_SORT_TIME | GIT_SORT_TOPOLOGICAL );
    git_oid oid;
    while ( git_revwalk_next( &oid, walk.pointer ) == 0 ) {
        Commit entry;
        if ( git_commit_lookup( &entry.pointer, repository_, &oid ) != 0 ) {
            continue;
        }
        const auto id = oidText( oid );
        if ( !storePath.isEmpty() ) {
            const auto now = filesAt( id ).value( storePath );
            const auto exists = filesAt( id ).contains( storePath );
            bool changed = true;
            if ( git_commit_parentcount( entry.pointer ) > 0 ) {
                const auto parent = oidText( *git_commit_parent_id( entry.pointer, 0 ) );
                const auto before = filesAt( parent );
                changed = before.contains( storePath ) != exists || before.value( storePath ) != now;
            }
            else {
                changed = exists;
            }
            if ( !changed ) {
                continue;
            }
        }
        commits.append( { id, QString::fromUtf8( git_commit_message( entry.pointer ) ).trimmed(),
                          QDateTime::fromSecsSinceEpoch( git_commit_time( entry.pointer ) ),
                          shipped.contains( id ) } );
    }
    return commits;
}

QString PackStore::restore( const QString& storePath, const QString& commitId,
                            const QString& message )
{
    const auto target = commitId.isEmpty() ? filesAt( QStringLiteral( "refs/heads/upstream" ) )
                                           : filesAt( commitId );
    if ( target.isEmpty() ) {
        return QStringLiteral( "That version of the pack is not in its store." );
    }
    auto stored = filesAt( QStringLiteral( "refs/heads/local" ) );
    if ( storePath.isEmpty() ) {
        const auto manifest = stored.value( ManifestPath );
        stored = target;
        stored.insert( ManifestPath, manifest );
    }
    else if ( target.contains( storePath ) ) {
        stored.insert( storePath, target.value( storePath ) );
    }
    else {
        stored.remove( storePath );
    }
    const auto error = commit( Local, stored, message );
    return error.isEmpty() ? writeContent( withoutManifest( stored ),
                                           QJsonDocument::fromJson( stored.value( ManifestPath ) ).object() )
                           : error;
}

QString PackStore::remove()
{
    if ( repository_ ) {
        git_repository_free( repository_ );
        repository_ = nullptr;
    }
    const QDir root( root_ );
    const bool removed = QDir( root.filePath( packId_ + QStringLiteral( ".git" ) ) ).removeRecursively()
                         && QDir( root.filePath( packId_ ) ).removeRecursively();
    return removed ? QString{} : QStringLiteral( "Cannot remove the pack store." );
}
} // namespace cilogg::plugins
