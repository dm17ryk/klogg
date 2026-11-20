#include "packstore.h"

#include "configpack.h"
#include "log.h"
#include <QCryptographicHash>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <git2.h>
#include <git2/sys/errors.h>

namespace cilogg::plugins {
namespace {
constexpr char ManifestPath[] = "manifest.json";
constexpr char Upstream[] = "upstream";
constexpr char Local[] = "local";

void ensureLibgit2()
{
    static const int initialized = [] {
        const auto count = git_libgit2_init();
        // The store is CILogg's private data: a folder created by another account (an elevated
        // run, a restored backup) is still this user's store, so it is not refused as unsafe.
        git_libgit2_opts( GIT_OPT_SET_OWNER_VALIDATION, 0 );
        return count;
    }();
    Q_UNUSED( initialized );
}

// A pack ID is a single folder name (the manifest parser checks the format; this guards the
// store's own paths).
bool safePackId( const QString& id )
{
    return !id.isEmpty() && id != QLatin1String( "." ) && id != QLatin1String( ".." )
           && !id.contains( '/' ) && !id.contains( '\\' ) && !id.contains( ':' );
}

// A path inside the content folder: relative, no "..", no drive or root.
bool safeContentPath( const QString& path )
{
    if ( path.isEmpty() || path.startsWith( '/' ) || path.contains( '\\' ) || path.contains( ':' )
         || QDir::isAbsolutePath( path ) ) {
        return false;
    }
    const auto parts = path.split( '/' );
    return std::none_of( parts.cbegin(), parts.cend(), []( const QString& part ) {
        return part.isEmpty() || part == QLatin1String( "." ) || part == QLatin1String( ".." );
    } );
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
    if ( !safePackId( packId_ ) ) {
        error_ = QStringLiteral( "Invalid pack ID for the pack store." );
        return false;
    }
    const auto path = repositoryPath();
    const auto native = QDir::toNativeSeparators( path ).toUtf8();
    if ( QFileInfo::exists( path ) ) {
        // damaged() reads the error class of this open, never an older one.
        git_error_clear();
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

QString PackStore::localTip() const
{
    git_oid tip;
    if ( !open( false ) || git_reference_name_to_id( &tip, repository_, "refs/heads/local" ) != 0 ) {
        return {};
    }
    return oidText( tip );
}

bool PackStore::hasRepository() const
{
    return safePackId( packId_ ) && QFileInfo::exists( repositoryPath() );
}

bool PackStore::damaged() const
{
    if ( !safePackId( packId_ ) || !QFileInfo::exists( repositoryPath() ) ) {
        return false;
    }
    if ( !open( false ) ) {
        // A file the system would not let us read (another CILogg, a scanner) is not damage:
        // only a repository libgit2 cannot make sense of is rebuilt.
        const auto* error = git_error_last();
        return !( error && error->klass == GIT_ERROR_OS );
    }
    // Every store has both branches, each readable whole with a manifest, from its first
    // install on.
    git_oid tip;
    if ( git_reference_name_to_id( &tip, repository_, "refs/heads/upstream" ) != 0
         || git_reference_name_to_id( &tip, repository_, "refs/heads/local" ) != 0 ) {
        return true;
    }
    bool localRead = false;
    bool upstreamRead = false;
    const auto local = filesAt( QStringLiteral( "refs/heads/local" ), &localRead );
    const auto upstream = filesAt( QStringLiteral( "refs/heads/upstream" ), &upstreamRead );
    return !localRead || !upstreamRead || !local.contains( ManifestPath )
           || !upstream.contains( ManifestPath );
}

QString PackStore::rebuild( const PackFiles& installed, const QJsonObject& manifest,
                            const QString& version )
{
    // The edits the old store had applied: its content folder, read in that folder's own layout.
    PackFiles applied;
    const auto content = contentDirectory();
    if ( !content.isEmpty() ) {
        QFile file( QDir( content ).filePath( QStringLiteral( "plugin.json" ) ) );
        const auto current = file.open( QIODevice::ReadOnly )
                                 ? QJsonDocument::fromJson( file.read( 1024 * 1024 ) ).object()
                                 : QJsonObject{};
        QString error;
        applied = current.isEmpty() ? PackFiles{}
                                    : packcanonical::fromPackFolder( current, content, &error );
        if ( !error.isEmpty() ) {
            LOG_WARNING << "Pack store rebuild cannot read the edits pack=" << packId_.toStdString();
            applied.clear();
        }
    }
    if ( repository_ ) {
        git_repository_free( repository_ );
        repository_ = nullptr;
    }
    error_.clear();
    const auto path = repositoryPath();
    if ( QFileInfo::exists( path ) ) {
        // Kept, never deleted: whatever it still holds can be recovered by hand.
        const auto aside = QStringLiteral( "%1.damaged-%2" ).arg(
            path, QDateTime::currentDateTimeUtc().toString( QStringLiteral( "yyyyMMddTHHmmsszzzZ" ) ) );
        if ( !QDir().rename( path, aside ) ) {
            return QStringLiteral( "Cannot move the damaged pack store aside." );
        }
        LOG_WARNING << "Pack store damaged, kept aside pack=" << packId_.toStdString();
    }
    // The content folder, which still holds the edits, is replaced only once both branches are
    // committed: a rebuild that fails part-way leaves the edits on disk for the next attempt.
    const auto manifestBytes = QJsonDocument( manifest ).toJson( QJsonDocument::Indented );
    auto shipped = installed;
    shipped.insert( ManifestPath, manifestBytes );
    const auto message = QStringLiteral( "Install %1 from the installed folder" ).arg( version );
    git_oid upstreamTip;
    auto error = commit( Upstream, shipped, message, {}, &upstreamTip );
    if ( error.isEmpty() ) {
        Reference local;
        if ( git_reference_create( &local.pointer, repository_, "refs/heads/local", &upstreamTip, 1,
                                   message.toUtf8().constData() )
             != 0 ) {
            error = lastError( QStringLiteral( "Cannot create the pack store" ) );
        }
    }
    const bool edited = !applied.isEmpty() && applied != installed;
    if ( error.isEmpty() && edited ) {
        auto stored = applied;
        stored.insert( ManifestPath, manifestBytes );
        error = commit( Local, stored,
                        QStringLiteral( "Re-apply the local edits after rebuilding the store" ) );
    }
    if ( error.isEmpty() ) {
        error = writeContent( edited ? applied : installed, manifest );
    }
    LOG_INFO << "Pack store rebuilt pack=" << packId_.toStdString() << " edits=" << edited
             << " error=" << error.toStdString();
    return error;
}

QString PackStore::contentDirectory() const
{
    const auto path = QDir( root_ ).filePath( packId_ + QStringLiteral( "/current" ) );
    return QFileInfo( path ).isDir() ? path : QString{};
}

QString PackStore::commit( const char* branch, const PackFiles& files, const QString& message,
                           const QStringList& extraParents, git_oid* created, bool moveBranch )
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
    const auto referenceName = reference.toUtf8();
    if ( git_commit_create( &commitId, repository_,
                            moveBranch ? referenceName.constData() : nullptr, signature.pointer,
                            signature.pointer, "UTF-8",
                           message.toUtf8().constData(), tree.pointer, parentPointers.size(),
                           parentPointers.data() )
         != 0 ) {
        return lastError( QStringLiteral( "Cannot commit to the pack store" ) );
    }
    LOG_DEBUG << "Pack store commit pack=" << packId_.toStdString() << " branch=" << branch
              << " id=" << oidText( commitId ).toStdString() << " files=" << files.size()
              << " moved=" << moveBranch;
    if ( created ) {
        *created = commitId;
    }
    return {};
}

PackFiles PackStore::filesAt( const QString& reference, bool* complete ) const
{
    PackFiles files;
    if ( complete ) {
        *complete = false;
    }
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
    // A blob that cannot be read stops the walk: the files read so far are not the tree.
    const auto walked = git_tree_walk( reinterpret_cast<git_tree*>( object.pointer ),
                                       GIT_TREEWALK_PRE, collect, &state );
    if ( complete ) {
        *complete = walked == 0;
    }
    return walked == 0 ? files : PackFiles{};
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

QString PackStore::writeFolder( const QString& folder, const PackFiles& files,
                                const QJsonObject& manifest )
{
    PackContent content;
    const auto invalid = validatePackManifest( manifest, &content );
    if ( !invalid.isEmpty() ) {
        return invalid;
    }
    QDir( folder ).removeRecursively();
    const auto write = [ &folder ]( const QString& relative, const QByteArray& bytes ) {
        // Never outside the folder, whatever a store path says.
        if ( !safeContentPath( relative ) ) {
            return false;
        }
        const auto path = QDir( folder ).filePath( relative );
        QDir().mkpath( QFileInfo( path ).absolutePath() );
        QSaveFile file( path );
        return file.open( QIODevice::WriteOnly ) && file.write( bytes ) == bytes.size()
               && file.commit();
    };
    bool ok = QDir().mkpath( folder )
              && write( QStringLiteral( "plugin.json" ),
                        QJsonDocument( manifest ).toJson( QJsonDocument::Indented ) );
    if ( !content.previews.isEmpty() ) {
        ok = ok && write( content.previews, packcanonical::toPreviews( files ) );
    }
    // Exactly the files the manifest lists: a listed file missing from `files` is an error, and
    // anything else under files/ is not written.
    for ( const auto& storePath : packcanonical::listedFiles( manifest ) ) {
        if ( !files.contains( storePath ) ) {
            return QStringLiteral( "The pack lacks a file its manifest lists: %1" )
                .arg( packcanonical::itemOf( storePath ) );
        }
        ok = ok && write( packcanonical::itemOf( storePath ), files.value( storePath ) );
    }
    // The Lua folder exists even when it has no files.
    if ( !content.luaFolder.isEmpty() ) {
        QDir().mkpath( QDir( folder ).filePath( content.luaFolder ) );
    }
    return ok ? QString{} : QStringLiteral( "Cannot write the pack's files." );
}

QString PackStore::writeContent( const PackFiles& files, const QJsonObject& manifest ) const
{
    const QDir packDir( QDir( root_ ).filePath( packId_ ) );
    const auto staged = packDir.filePath( QStringLiteral( "current.new" ) );
    const auto written = writeFolder( staged, files, manifest );
    if ( !written.isEmpty() ) {
        QDir( staged ).removeRecursively();
        return written;
    }
    const auto current = packDir.filePath( QStringLiteral( "current" ) );
    auto old = packDir.filePath( QStringLiteral( "current.old" ) );
    // A previous folder that cannot be removed (a file still open) is set aside under another name.
    if ( QFileInfo::exists( old ) && !QDir( old ).removeRecursively() ) {
        old = packDir.filePath( QStringLiteral( "current.old-%1" )
                                    .arg( QDateTime::currentMSecsSinceEpoch() ) );
    }
    if ( ( QFileInfo::exists( current ) && !QDir().rename( current, old ) )
         || !QDir().rename( staged, current ) ) {
        if ( !QFileInfo::exists( current ) && QFileInfo::exists( old ) ) {
            QDir().rename( old, current );
        }
        QDir( staged ).removeRecursively();
        return QStringLiteral( "Cannot replace the pack's content folder." );
    }
    // The previous folder, and any set aside earlier that can be removed now.
    const auto leftovers = packDir.entryList( { QStringLiteral( "current.old*" ) },
                                              QDir::Dirs | QDir::NoDotAndDotDot );
    for ( const auto& leftover : leftovers ) {
        QDir( packDir.filePath( leftover ) ).removeRecursively();
    }
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
    return error.isEmpty() ? publish( stored ) : error;
}

QString PackStore::commitEdit( const QString& storePath, const std::optional<QByteArray>& bytes,
                               const QString& message )
{
    bool complete = false;
    auto stored = filesAt( QStringLiteral( "refs/heads/local" ), &complete );
    if ( !complete || !stored.contains( ManifestPath ) ) {
        return QStringLiteral( "The pack's store cannot be read." );
    }
    if ( bytes ) {
        stored.insert( storePath, *bytes );
    }
    else {
        stored.remove( storePath );
    }
    const auto error = commit( Local, stored, message );
    return error.isEmpty() ? publish( stored ) : error;
}

QString PackStore::publish( const PackFiles& stored ) const
{
    const auto error = writeContent( withoutManifest( stored ),
                                     QJsonDocument::fromJson( stored.value( ManifestPath ) ).object() );
    // The commit is made: the edit is kept, and repairContent() writes the folder again later.
    return error.isEmpty() ? error
                           : QStringLiteral( "The change is saved, but the pack's files could not "
                                             "be written yet: %1" )
                                 .arg( error );
}

QString PackStore::repairContent()
{
    if ( !contentDirectory().isEmpty() ) {
        return {};
    }
    bool complete = false;
    const auto stored = filesAt( QStringLiteral( "refs/heads/local" ), &complete );
    if ( !complete || !stored.contains( ManifestPath ) ) {
        return QStringLiteral( "The pack's store cannot be read." );
    }
    LOG_INFO << "Pack content folder written again pack=" << packId_.toStdString();
    return publish( stored );
}

PackUpdatePlan PackStore::planUpdate( const PackFiles& newFiles, MergeStrategy strategy ) const
{
    PackUpdatePlan plan;
    bool baseRead = false;
    bool mineRead = false;
    const auto base = withoutManifest( filesAt( QStringLiteral( "refs/heads/upstream" ), &baseRead ) );
    const auto mine = withoutManifest( filesAt( QStringLiteral( "refs/heads/local" ), &mineRead ) );
    if ( !baseRead || !mineRead ) {
        plan.error = QStringLiteral( "The pack's store cannot be read." );
        return plan;
    }
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
    // Both commits first, then both branches: an update is in the store whole or not at all, so
    // `upstream` (the next merge base) never runs ahead of `local`.
    const auto message = QStringLiteral( "Install %1 from %2" ).arg( version, source );
    if ( !open( true ) ) {
        return error_;
    }
    git_oid previousUpstream;
    if ( git_reference_name_to_id( &previousUpstream, repository_, "refs/heads/upstream" ) != 0 ) {
        return lastError( QStringLiteral( "Cannot find the installed version" ) );
    }
    git_oid upstreamCommit;
    git_oid localCommit;
    auto error = commit( Upstream, shipped, message, {}, &upstreamCommit, false );
    if ( error.isEmpty() ) {
        error = commit( Local, result, QStringLiteral( "Merge %1" ).arg( version ),
                        { oidText( upstreamCommit ) }, &localCommit, false );
    }
    if ( !error.isEmpty() ) {
        return error;
    }
    Reference upstream;
    if ( git_reference_create( &upstream.pointer, repository_, "refs/heads/upstream",
                               &upstreamCommit, 1, message.toUtf8().constData() )
         != 0 ) {
        return lastError( QStringLiteral( "Cannot record the installed version" ) );
    }
    Reference local;
    if ( git_reference_create( &local.pointer, repository_, "refs/heads/local", &localCommit, 1,
                               message.toUtf8().constData() )
         != 0 ) {
        error = lastError( QStringLiteral( "Cannot record the merge" ) );
        Reference restored;
        git_reference_create( &restored.pointer, repository_, "refs/heads/upstream",
                              &previousUpstream, 1, "Undo an update that was not recorded" );
        return error;
    }
    return publish( result );
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
            const auto files = filesAt( id );
            const auto now = files.value( storePath );
            const auto exists = files.contains( storePath );
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
    // Only a commit of this pack's own history (as History lists it), never another revision.
    if ( !commitId.isEmpty() ) {
        const auto commits = history();
        if ( std::none_of( commits.cbegin(), commits.cend(),
                           [ &commitId ]( const PackCommit& commit ) { return commit.id == commitId; } ) ) {
            return QStringLiteral( "That version of the pack is not in its store." );
        }
    }
    bool targetRead = false;
    bool storedRead = false;
    const auto target = filesAt( commitId.isEmpty() ? QStringLiteral( "refs/heads/upstream" ) : commitId,
                                 &targetRead );
    const auto current = filesAt( QStringLiteral( "refs/heads/local" ), &storedRead );
    if ( !targetRead || !storedRead || !current.contains( ManifestPath ) ) {
        return QStringLiteral( "The pack's store cannot be read." );
    }
    auto stored = current;
    const auto kind = packcanonical::kindOf( storePath );
    if ( storePath.isEmpty() ) {
        // A whole earlier state only within the installed version: another version's items need
        // not match this version's manifest (a Lua file it lists, for example).
        if ( target.value( ManifestPath ) != current.value( ManifestPath ) ) {
            return QStringLiteral( "That state belongs to another version of the pack: revert its "
                                   "items one by one." );
        }
        stored = target;
    }
    else if ( target.contains( storePath ) ) {
        stored.insert( storePath, target.value( storePath ) );
    }
    else if ( kind == QLatin1String( "file" ) || kind == QLatin1String( "order" )
              || kind == QLatin1String( "meta" ) ) {
        // The manifest decides which files a pack has; they are edited, never removed.
        if ( current.contains( storePath ) ) {
            return QStringLiteral( "That version has no such file, and this version of the pack "
                                   "needs it." );
        }
    }
    else {
        stored.remove( storePath );
    }
    if ( stored == current ) {
        return {}; // nothing to restore: no empty commit in the history
    }
    const auto error = commit( Local, stored, message );
    return error.isEmpty() ? publish( stored ) : error;
}

QString PackStore::remove()
{
    if ( repository_ ) {
        git_repository_free( repository_ );
        repository_ = nullptr;
    }
    if ( !safePackId( packId_ ) ) {
        return QStringLiteral( "Invalid pack ID for the pack store." );
    }
    const bool removed = QDir( repositoryPath() ).removeRecursively()
                         && QDir( QDir( root_ ).filePath( packId_ ) ).removeRecursively();
    return removed ? QString{} : QStringLiteral( "Cannot remove the pack store." );
}

PackTips PackStore::tips() const
{
    PackTips tips;
    git_oid oid;
    if ( !open( false ) ) {
        return tips;
    }
    tips.existed = true;
    if ( git_reference_name_to_id( &oid, repository_, "refs/heads/upstream" ) == 0 ) {
        tips.upstream = oidText( oid );
    }
    if ( git_reference_name_to_id( &oid, repository_, "refs/heads/local" ) == 0 ) {
        tips.local = oidText( oid );
    }
    return tips;
}

QString PackStore::rollback( const PackTips& tips )
{
    if ( !tips.existed ) {
        return remove();
    }
    if ( !open( false ) ) {
        return error_;
    }
    for ( const auto& [ branch, id ] : { std::pair{ "refs/heads/upstream", tips.upstream },
                                         std::pair{ "refs/heads/local", tips.local } } ) {
        git_oid oid;
        Reference reference;
        if ( id.isEmpty() || git_oid_fromstr( &oid, id.toLatin1().constData() ) != 0
             || git_reference_create( &reference.pointer, repository_, branch, &oid, 1,
                                      "Undo an install whose package was not installed" )
                    != 0 ) {
            return lastError( QStringLiteral( "Cannot put the pack store back" ) );
        }
    }
    bool complete = false;
    const auto stored = filesAt( QStringLiteral( "refs/heads/local" ), &complete );
    LOG_WARNING << "Pack store put back after a failed install pack=" << packId_.toStdString();
    return complete ? publish( stored ) : QStringLiteral( "The pack's store cannot be read." );
}

QString PackStore::repositoryPath() const
{
    const auto preferred = QDir( root_ ).filePath( packId_ + QStringLiteral( ".git" ) );
#ifdef Q_OS_WIN
    // libgit2 keeps every path inside a repository under MAX_PATH on Windows (it reserves 68
    // characters for its own files), whatever core.longpaths says. A deep data folder (a
    // portable install, a test sandbox) puts the repository in a short folder of this user's,
    // named after the data folder and the pack; the content folder stays in the data folder.
    constexpr qsizetype RepositoryPathBudget = 180;
    const auto absolute = QFileInfo( preferred ).absoluteFilePath();
    if ( absolute.size() > RepositoryPathBudget ) {
        const auto key = QCryptographicHash::hash(
                             ( QFileInfo( root_ ).absoluteFilePath() + '\n' + packId_ ).toUtf8(),
                             QCryptographicHash::Sha256 )
                             .toHex()
                             .left( 24 );
        return QDir( QStandardPaths::writableLocation( QStandardPaths::GenericDataLocation ) )
            .filePath( QStringLiteral( "cilogg-pack-stores/%1.git" ).arg( QString::fromLatin1( key ) ) );
    }
#endif
    return preferred;
}
} // namespace cilogg::plugins
