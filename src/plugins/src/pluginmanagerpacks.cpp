// Configuration packs edited in place (docs/plugin-repositories.md 4.6): the local store of each
// pack, updates that merge the edits, and the pack operations of plugin_manage.
#include "pluginmanager.h"

#include "configpack.h"
#include "log.h"
#include "packstore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <initializer_list>

namespace cilogg::plugins {
namespace {
constexpr int MaxConflictsListed = 3;

std::optional<MergeStrategy> strategyOf( const QVariant& value )
{
    const auto text = value.toString();
    if ( !value.isValid() || text.isEmpty() || text == QLatin1String( "fail" ) ) {
        return MergeStrategy::Fail;
    }
    if ( text == QLatin1String( "keep_mine" ) ) {
        return MergeStrategy::KeepMine;
    }
    if ( text == QLatin1String( "take_theirs" ) ) {
        return MergeStrategy::TakeTheirs;
    }
    return std::nullopt;
}

// True for bytes that are UTF-8 text (they survive a round trip through QString).
bool utf8Text( const QByteArray& bytes )
{
    return QString::fromUtf8( bytes ).toUtf8() == bytes;
}

// The texts of one job's conflicts for the merge window. Jobs are kept (up to 256), so a pack
// with many large conflicting files must not keep hundreds of MiB in them.
constexpr qsizetype MaxConflictTextBytes = 4 * 1024 * 1024;

// The previews document as the store writes it (one member per line) must stay within the 1 MiB
// of a pack file, or the content folder would not load: a minified document just under the limit
// grows when it is written again. Its items stay within the preview limits too, which a merge can
// pass (an edited preview the new version removed, kept beside the version's own) also for a
// disabled pack, whose content is not checked by enabling it. Empty when it fits.
QString previewsTooLarge( const PackFiles& files )
{
    qsizetype previews = 0;
    qsizetype blocks = 0;
    for ( auto file = files.cbegin(); file != files.cend(); ++file ) {
        const auto kind = packcanonical::kindOf( file.key() );
        previews += kind == QLatin1String( "preview" ) ? 1 : 0;
        blocks += kind == QLatin1String( "block" ) ? 1 : 0;
    }
    if ( previews > packcanonical::MaxPackPreviewItems || blocks > packcanonical::MaxPackBlockItems ) {
        return QStringLiteral( "The pack's previews would be more than %1 previews or %2 blocks, "
                               "the most a pack can have." )
            .arg( packcanonical::MaxPackPreviewItems )
            .arg( packcanonical::MaxPackBlockItems );
    }
    return packcanonical::toPreviews( files ).size() > MaxPackFileBytes
               ? QStringLiteral( "The pack's previews are larger than 1 MiB as CILogg writes them "
                                 "(one member per line)." )
               : QString{};
}

// A Lua or Python file the manifest lists, or the preview order: merged as text.
bool textKind( const QString& kind )
{
    return kind == QLatin1String( "file" ) || kind == QLatin1String( "order" );
}

// How many conflicts a job lists: a preview of 1 MiB can conflict in thousands of fields.
constexpr qsizetype MaxFieldConflictsPerItem = 32;
constexpr qsizetype MaxConflictEntries = 1024;

// The conflicts and conflicting items of a merge for its job result, bounded in number and size.
// An item with more conflicting fields than MaxFieldConflictsPerItem, or once the list holds
// MaxConflictEntries, is listed as one whole-item conflict (field ""), which a whole-item
// resolution settles. Texts go there for the merge window within `budget` bytes: previews and
// blocks first (a previews document is at most 1 MiB a side), then files, each file's sides once
// (in `items`; its conflict keeps the base). An item past the budget is listed with `omitted` and
// no texts, and is resolved as a whole side (take), like a file that is not UTF-8.
void conflictPayload( const QVector<PackConflict>& found, const PackFiles& base,
                      const PackFiles& mine, const PackFiles& theirs, qsizetype budget,
                      QVariantList* conflicts, QVariantMap* items )
{
    QStringList order;
    QHash<QString, QVector<const PackConflict*>> byItem;
    for ( const auto& conflict : found ) {
        if ( !byItem.contains( conflict.path ) ) {
            order.append( conflict.path );
        }
        byItem[ conflict.path ].append( &conflict );
    }
    QSet<QString> whole;
    qsizetype entries = 0;
    for ( const auto& path : order ) {
        const auto count = byItem.value( path ).size();
        const bool collapse = !textKind( byItem.value( path ).first()->kind )
                              && ( count > MaxFieldConflictsPerItem || entries + count > MaxConflictEntries );
        if ( collapse ) {
            whole.insert( path );
        }
        entries += collapse ? 1 : count;
    }
    // The bytes an item's conflicts and its whole sides add to the job.
    const auto cost = [ & ]( const QString& path ) {
        qsizetype bytes = mine.value( path ).size() + theirs.value( path ).size();
        const auto& list = byItem.value( path );
        // A file, or an item listed whole: its sides are in `items` once, its conflict has the base.
        if ( textKind( list.first()->kind ) || whole.contains( path ) ) {
            return bytes + base.value( path ).size();
        }
        for ( const auto* conflict : list ) {
            bytes += conflict->base.size() + conflict->mine.size() + conflict->theirs.size();
        }
        return bytes;
    };
    QSet<QString> shown;
    for ( const bool text : { false, true } ) {
        for ( const auto& path : order ) {
            if ( textKind( byItem.value( path ).first()->kind ) != text ) {
                continue;
            }
            const auto yours = mine.value( path );
            const auto shipped = theirs.value( path );
            QVariantMap item{ { "inMine", mine.contains( path ) },
                              { "inTheirs", theirs.contains( path ) },
                              // Not UTF-8: shown, but resolved only with `take`.
                              { "utf8", utf8Text( yours ) && utf8Text( shipped )
                                            && utf8Text( base.value( path ) ) } };
            if ( const auto bytes = cost( path ); bytes <= budget ) {
                budget -= bytes;
                shown.insert( path );
                item.insert( "mine", QString::fromUtf8( yours ) );
                item.insert( "theirs", QString::fromUtf8( shipped ) );
            }
            else {
                item.insert( "omitted", true );
            }
            items->insert( path, item );
        }
    }
    for ( const auto& path : order ) {
        const auto& list = byItem.value( path );
        if ( whole.contains( path ) ) {
            QVariantMap info{ { "path", path },
                              { "kind", list.first()->kind },
                              { "item", list.first()->item },
                              { "field", QString{} } };
            if ( shown.contains( path ) ) {
                info.insert( "base", QString::fromUtf8( base.value( path ) ) ); // sides: `items`
            }
            conflicts->append( info );
            continue;
        }
        for ( const auto* conflict : list ) {
            QVariantMap info{ { "path", conflict->path },
                              { "kind", conflict->kind },
                              { "item", conflict->item },
                              { "field", conflict->field } };
            if ( shown.contains( path ) ) {
                info.insert( "base", QString::fromUtf8( conflict->base ) );
                if ( !textKind( conflict->kind ) ) {
                    info.insert( "mine", QString::fromUtf8( conflict->mine ) );
                    info.insert( "theirs", QString::fromUtf8( conflict->theirs ) );
                }
            }
            conflicts->append( info );
        }
    }
}

// A resolution chosen in the merge window, or given by automation: {path, take} (a whole side)
// for any item, {path, field, value} or {path, field, remove: true} for a structured item, and
// {path, text} for a file or the preview order. Only items of the pack (`known`: installed,
// edited or new version) in their canonical spelling can be resolved. Returns false when it
// cannot be applied.
bool applyResolution( PackFiles* merged, const QVariantMap& resolution, const QSet<QString>& known,
                      const PackFiles& mine, const PackFiles& theirs )
{
    const auto path = resolution.value( "path" ).toString();
    const auto kind = packcanonical::kindOf( path );
    if ( kind.isEmpty() || !known.contains( path ) || !packcanonical::isCanonical( path ) ) {
        return false;
    }
    const bool remove = resolution.value( "remove" ).toBool();
    // A whole side, byte for byte (any encoding): a file, the preview order, or a preview or
    // block as a whole (one too large to show its fields in the merge window).
    const auto take = resolution.value( "take" ).toString();
    if ( take == QLatin1String( "mine" ) || take == QLatin1String( "theirs" ) ) {
        const auto& side = take == QLatin1String( "mine" ) ? mine : theirs;
        if ( side.contains( path ) ) {
            merged->insert( path, side.value( path ) );
        }
        else {
            merged->remove( path );
        }
        return true;
    }
    if ( kind == QLatin1String( "file" ) || kind == QLatin1String( "order" ) ) {
        // The new version's manifest lists the files it has (followManifest puts a listed one
        // back) and the preview order is edited, never deleted: a removal could not be honoured.
        if ( remove || !resolution.contains( "text" ) ) {
            return false;
        }
        // Merged text is UTF-8: a file in another encoding is resolved by `take` only, or its
        // bytes would change.
        if ( !utf8Text( mine.value( path ) ) || !utf8Text( theirs.value( path ) ) ) {
            return false;
        }
        const auto text = resolution.value( "text" ).toString().toUtf8();
        // A resolved file has no conflict markers left.
        if ( text.contains( "\n<<<<<<< " ) || text.startsWith( "<<<<<<< " ) ) {
            return false;
        }
        merged->insert( path, text );
        return true;
    }
    const auto field = resolution.value( "field" ).toString();
    const auto current = QJsonDocument::fromJson( merged->value( path ) ).object();
    std::optional<QJsonValue> value;
    if ( !remove ) {
        if ( !resolution.contains( "value" ) ) {
            return false;
        }
        value = QJsonValue::fromVariant( resolution.value( "value" ) );
    }
    if ( field.isEmpty() ) {
        // A whole preview or block keeps the name its path stands for.
        if ( value && value->isObject()
             && ( ( kind != QLatin1String( "preview" ) && kind != QLatin1String( "block" ) )
                  || value->toObject().value( "name" ).toString() == packcanonical::itemOf( path ) ) ) {
            merged->insert( path, packcanonical::itemBytes( value->toObject() ) );
        }
        else if ( !value ) {
            merged->remove( path );
        }
        else {
            return false;
        }
        return true;
    }
    // The name is the item's identity: a field resolution never renames it.
    if ( field == QLatin1String( "name" ) ) {
        return false;
    }
    const auto changed = packmerge::withField( current, field, value );
    merged->insert( path, packcanonical::itemBytes( changed.toObject() ) );
    return true;
}

// The files of a merge result are exactly those the new version's manifest lists: a file it
// dropped goes (also when kept by keep_mine), and a listed file is never missing.
void followManifest( PackFiles* merged, const PackFiles& shipped, const QJsonObject& manifest )
{
    const auto listed = packcanonical::listedFiles( manifest );
    for ( auto it = merged->begin(); it != merged->end(); ) {
        it = packcanonical::kindOf( it.key() ) == QLatin1String( "file" ) && !listed.contains( it.key() )
                 ? merged->erase( it )
                 : std::next( it );
    }
    for ( const auto& path : listed ) {
        if ( !merged->contains( path ) ) {
            merged->insert( path, shipped.value( path ) );
        }
    }
}
} // namespace

QString PluginManager::packStoreRoot() const
{
    return QDir( dataRoot_ ).filePath( "pack-store" );
}

QString PluginManager::packContentDirectory( const QString& pluginId ) const
{
    // The effective content wherever its folder is, also while the repository does not open
    // (another program holds a file of it): the installed folder only when there is none.
    const auto content = PackStore( packStoreRoot(), pluginId ).contentDirectory();
    return content.isEmpty() ? pluginDirectory( pluginId ) : content;
}

QString PluginManager::packRevision( const QString& pluginId ) const
{
    return PackStore( packStoreRoot(), pluginId ).localTip();
}

QString PluginManager::ensurePackStore( const QString& id, const Manifest& installed,
                                        bool create ) const
{
    PackStore store( packStoreRoot(), id );
    // An install or update whose journal is still there: it finished (the package folder holds
    // what the store's `upstream` holds) or stopped part-way (CILogg ended in the middle), and
    // then the store is put back to the installed version first.
    if ( store.hasJournal() ) {
        QString readError;
        const auto installedFiles
            = packcanonical::fromPackFolder( installed.json, installed.directory, &readError );
        if ( !readError.isEmpty() ) {
            return QStringLiteral( "The pack's installed folder cannot be read to finish an update "
                                   "that stopped part-way: %1" )
                .arg( readError );
        }
        if ( const auto error = store.recoverInterruptedUpdate( &installedFiles, installed.json );
             !error.isEmpty() ) {
            return error;
        }
    }
    // A store that is there but does not open now (another program holds a file of it) is not a
    // missing one: nothing is done with it until it opens, or its edits could be lost.
    if ( store.hasRepository() && !store.exists() && !store.damaged() ) {
        return QStringLiteral( "The pack's store cannot be opened now (another program may be "
                               "using it). Try again." );
    }
    // A content folder without its repository (moved, or deleted by hand) still holds the
    // edits: that is rebuilt like a damaged store, never replaced by the installed version.
    const bool damaged
        = store.damaged() || ( !store.hasRepository() && !store.contentDirectory().isEmpty() );
    if ( !damaged && store.exists() ) {
        // A content folder lost or left behind after its commit is written again.
        return store.repairContent();
    }
    if ( !damaged && !create ) {
        return {};
    }
    QString error;
    const auto files = packcanonical::fromPackFolder( installed.json, installed.directory, &error );
    if ( error.isEmpty() ) {
        error = damaged ? store.rebuild( files, installed.json, installed.version )
                        : store.install( files, installed.json, installed.version,
                                         QStringLiteral( "the installed folder" ) );
    }
    if ( !error.isEmpty() ) {
        LOG_WARNING << "Pack store not usable id=" << id.toStdString() << " damaged=" << damaged;
    }
    return error;
}

QString PluginManager::preparePackInstall( const StagedPlugin& staged, const QVariantMap& context,
                                           PackFiles* shipped, std::optional<PackFiles>* merged,
                                           QVariantMap* result ) const
{
    QString error;
    *shipped = packcanonical::fromPackFolder( staged.manifest.json, staged.manifest.directory,
                                              &error );
    if ( error.isEmpty() ) {
        error = previewsTooLarge( *shipped );
    }
    if ( !error.isEmpty() ) {
        return error;
    }
    // A damaged store is rebuilt first, so the update still merges with the local edits.
    const auto installed = entries_.find( staged.manifest.id );
    const bool packInstalled = installed != entries_.end()
                               && isConfigPack( installed->second->manifest.json )
                               && installed->second->manifest.valid();
    if ( packInstalled ) {
        error = ensurePackStore( staged.manifest.id, installed->second->manifest, false );
        if ( !error.isEmpty() ) {
            return QStringLiteral( "The pack's local store is not usable, so the installed version "
                                   "stays: %1" )
                .arg( error );
        }
    }
    PackStore store( packStoreRoot(), staged.manifest.id );
    // A store without its pack is a first install that stopped part-way (its journal removes it)
    // or what an earlier removal could not delete: that is set aside, so its edits do not come
    // back into a new install.
    if ( !packInstalled ) {
        if ( const auto interrupted = store.recoverInterruptedUpdate(); !interrupted.isEmpty() ) {
            return interrupted;
        }
    }
    if ( !packInstalled && ( store.hasRepository() || !store.contentDirectory().isEmpty() ) ) {
        if ( const auto aside = store.setAside(); !aside.isEmpty() ) {
            return aside;
        }
    }
    if ( !store.exists() ) {
        return {}; // a first install: nothing to merge
    }
    const auto strategy = strategyOf( context.value( "strategy" ) ).value_or( MergeStrategy::Fail );
    auto plan = store.planUpdate( *shipped, strategy );
    if ( !plan.error.isEmpty() ) {
        return plan.error;
    }
    const auto mine = store.files();
    const auto base = store.upstreamFiles();
    QSet<QString> known;
    for ( const auto* side :
          std::initializer_list<const PackFiles*>{ &plan.merged, shipped, &mine, &base } ) {
        for ( auto it = side->cbegin(); it != side->cend(); ++it ) {
            known.insert( it.key() );
        }
    }
    // A file the new version's manifest no longer lists goes whatever was edited: no conflict.
    const auto listed = packcanonical::listedFiles( staged.manifest.json );
    plan.conflicts.erase( std::remove_if( plan.conflicts.begin(), plan.conflicts.end(),
                                          [ &listed ]( const PackConflict& conflict ) {
                                              return conflict.kind == QLatin1String( "file" )
                                                     && !listed.contains( conflict.path );
                                          } ),
                          plan.conflicts.end() );
    // Resolutions from the merge window (or automation) settle conflicts one by one. They were
    // chosen for the edits as they were then (`revision`, the `local` commit the merge was shown
    // for): after another edit they could overwrite it, so they are dropped and the merge is
    // asked for again.
    // Both sides of the merge: the edits (the `local` commit) and the version being installed (its
    // files and manifest), so choices made for another package, a local folder changed meanwhile
    // for one, are not applied either.
    QCryptographicHash incoming( QCryptographicHash::Sha256 );
    for ( auto it = shipped->cbegin(); it != shipped->cend(); ++it ) {
        incoming.addData( it.key().toUtf8() + '\n' + QByteArray::number( it.value().size() ) + '\n' );
        incoming.addData( it.value() );
    }
    incoming.addData( QJsonDocument( staged.manifest.json ).toJson( QJsonDocument::Compact ) );
    const auto revision = QStringLiteral( "%1:%2" ).arg(
        store.localTip(), QString::fromLatin1( incoming.result().toHex().left( 24 ) ) );
    ( *result )[ "revision" ] = revision;
    if ( !context.value( "resolutions" ).toList().isEmpty() && !context.contains( "revision" ) ) {
        return QStringLiteral( "resolutions need the revision of the failed update's result: they "
                               "are applied only to the edits they were chosen for." );
    }
    const bool stale = context.contains( "revision" ) && context.value( "revision" ).toString() != revision;
    const auto resolutions = stale ? QVariantList{} : context.value( "resolutions" ).toList();
    QVector<PackConflict> open;
    for ( const auto& conflict : plan.conflicts ) {
        // A resolution without a field settles every conflict of its item (Keep Both).
        const auto resolved = std::any_of(
            resolutions.cbegin(), resolutions.cend(), [ &conflict ]( const QVariant& item ) {
                const auto resolution = item.toMap();
                const auto field = resolution.value( "field" ).toString();
                return resolution.value( "path" ).toString() == conflict.path
                       && ( field.isEmpty() || field == conflict.field );
            } );
        if ( !resolved ) {
            open.append( conflict );
        }
    }
    for ( const auto& item : resolutions ) {
        if ( !applyResolution( &plan.merged, item.toMap(), known, mine, *shipped ) ) {
            return QStringLiteral( "Invalid merge resolution for %1." )
                .arg( item.toMap().value( "path" ).toString().left( 128 ) );
        }
    }
    // Edited files the new version no longer has go with it; the user is told, and the edits
    // stay in the pack's history.
    QStringList dropped;
    for ( auto it = mine.cbegin(); it != mine.cend(); ++it ) {
        if ( packcanonical::kindOf( it.key() ) == QLatin1String( "file" ) && !listed.contains( it.key() )
             && base.value( it.key() ) != it.value() ) {
            dropped.append( packcanonical::itemOf( it.key() ) );
        }
    }
    if ( !dropped.isEmpty() ) {
        ( *result )[ "droppedEdits" ] = dropped;
        ( *result )[ "warning" ] = QStringLiteral( "The new version no longer has %1; your edits of "
                                                   "it stay in the pack's history." )
                                       .arg( dropped.join( ", " ).left( 256 ) );
    }
    followManifest( &plan.merged, *shipped, staged.manifest.json );
    // Each conflicting item whole too, for the merge window (Keep Both, the text merge). No window
    // opens for an automatic update (its notice runs the update again), so it keeps no texts.
    QVariantList conflicts;
    QVariantMap items;
    conflictPayload( plan.conflicts, base, mine, *shipped,
                     context.value( "automatic" ).toBool() ? 0 : MaxConflictTextBytes, &conflicts,
                     &items );
    ( *result )[ "conflicts" ] = conflicts;
    ( *result )[ "items" ] = items;
    // Choices made for other edits or another package are not applied, and that update does not
    // go ahead on its own either, even when it would merge without them (nor would the merge's
    // Keep Both): it is asked for again, with the merge when there are conflicts.
    if ( stale ) {
        ( *result )[ "needsMerge" ] = !plan.conflicts.isEmpty();
        return QStringLiteral( "The pack's edits or the new version changed after the merge was "
                               "shown, so its choices were not applied and nothing was installed. "
                               "Run the update again." );
    }
    if ( strategy == MergeStrategy::Fail && !open.isEmpty() ) {
        QStringList names;
        for ( const auto& conflict : open ) {
            if ( !names.contains( conflict.item ) && names.size() < MaxConflictsListed ) {
                names.append( conflict.item );
            }
        }
        ( *result )[ "needsMerge" ] = true;
        return QStringLiteral( "The new version changes %1 that you edited (%2). Merge it in the "
                               "Plugins window, or update with the strategy keep_mine or "
                               "take_theirs." )
            .arg( open.size() == 1 ? QStringLiteral( "an item" )
                                   : QStringLiteral( "%1 places" ).arg( open.size() ),
                  names.join( ", " ) );
    }
    // What would be installed must load: checked once the conflicts are settled, so a merge whose
    // open conflicts hold your side (an edited preview the new version removes, beside its own)
    // still offers the merge window instead of failing here.
    for ( auto it = plan.merged.cbegin(); it != plan.merged.cend(); ++it ) {
        if ( it.value().size() > MaxPackFileBytes ) {
            return QStringLiteral( "The merged %1 is larger than 1 MiB." )
                .arg( packcanonical::itemOf( it.key() ).left( 128 ) );
        }
    }
    if ( const auto tooLarge = previewsTooLarge( plan.merged ); !tooLarge.isEmpty() ) {
        return tooLarge;
    }
    *merged = plan.merged;
    return {};
}

QString PluginManager::finishPackInstall( const StagedPlugin& staged, const PackFiles& shipped,
                                          const std::optional<PackFiles>& merged,
                                          const InstallSource& source )
{
    PackStore store( packStoreRoot(), staged.manifest.id );
    const auto from = source.repository.isEmpty() ? QStringLiteral( "a local package" )
                                                  : source.repository;
    const auto error = merged ? store.applyUpdate( shipped, *merged, staged.manifest.json,
                                                   staged.manifest.version, from )
                              : store.install( shipped, staged.manifest.json,
                                               staged.manifest.version, from );
    LOG_INFO << "Pack store updated id=" << staged.manifest.id.toStdString()
             << " version=" << staged.manifest.version.toStdString() << " merged=" << bool( merged )
             << " error=" << error.toStdString();
    return error;
}

QString PluginManager::checkPackFiles( const QString& id, const PackFiles& files,
                                       const QJsonObject& manifest ) const
{
    if ( !packCheck_ ) {
        return {};
    }
    // Inside the pack's own store folder, so no other pack's name can meet it.
    const auto folder = QDir( packStoreRoot() ).filePath( id + QStringLiteral( "/check" ) );
    auto refusal = PackStore::writeFolder( folder, files, manifest );
    if ( refusal.isEmpty() ) {
        refusal = packCheck_( id, folder, manifest );
    }
    QDir( folder ).removeRecursively();
    return refusal;
}

void PluginManager::removePackStore( const QString& pluginId )
{
    PackStore store( packStoreRoot(), pluginId );
    // By what is on disk, not by whether the repository opens: a damaged store's content folder
    // would otherwise come back as edits when the pack is installed again.
    if ( store.hasRepository() || !store.contentDirectory().isEmpty() ) {
        const auto error = store.remove();
        if ( !error.isEmpty() ) {
            LOG_WARNING << "Pack store not removed id=" << pluginId.toStdString();
        }
    }
    settings_.remove( configurationKey( pluginId ) + "needsMerge" );
}

int PluginManager::packLocalEdits( const QString& pluginId ) const
{
    const PackStore store( packStoreRoot(), pluginId );
    return store.exists() ? static_cast<int>( store.localEdits().size() ) : 0;
}

bool PluginManager::packOperation( const QString& operation )
{
    return operation == QLatin1String( "pack_files" ) || operation == QLatin1String( "pack_read" )
           || operation == QLatin1String( "pack_edit" ) || operation == QLatin1String( "pack_edits" )
           || operation == QLatin1String( "pack_history" )
           || operation == QLatin1String( "pack_reset" );
}

Result PluginManager::managePack( const QString& operation, const QString& id,
                                  const QVariantMap& options )
{
    const auto it = entries_.find( id );
    if ( it == entries_.end() ) {
        return { "Plugin not found.", {} };
    }
    const auto& manifest = it->second->manifest;
    if ( !isConfigPack( manifest.json ) || !manifest.valid() ) {
        return { "Pack operations apply to valid configuration packs only.", {} };
    }
    // The options each operation takes; anything else is a mistake of the caller's.
    const QStringList known = operation == "pack_edit"    ? QStringList{ "path", "content", "remove",
                                                                          "message" }
                              : operation == "pack_reset" ? QStringList{ "path", "commit" }
                              : operation == "pack_read" || operation == "pack_history"
                                  ? QStringList{ "path" }
                                  : QStringList{};
    for ( auto option = options.cbegin(); option != options.cend(); ++option ) {
        if ( !known.contains( option.key() ) ) {
            return { QStringLiteral( "Unknown %1 option: %2." )
                         .arg( operation, option.key().left( 64 ) ),
                     {} };
        }
    }
    // A pack installed before its store existed gets one now, from its installed folder; a
    // damaged store is rebuilt with the local edits.
    if ( const auto error = ensurePackStore( id, manifest, true ); !error.isEmpty() ) {
        return { error, {} };
    }
    PackStore store( packStoreRoot(), id );
    const auto path = options.value( "path" ).toString();
    const bool enabled = settings_.value( configurationKey( id ) + "enabled", false ).toBool();
    const auto listing = [ &store ] {
        const auto edits = store.localEdits();
        const auto current = store.files();
        const auto base = store.upstreamFiles();
        QStringList paths = current.keys() + base.keys();
        paths.removeDuplicates();
        QVariantList files;
        for ( const auto& storePath : paths ) {
            const auto kind = packcanonical::kindOf( storePath );
            if ( kind.isEmpty() ) {
                continue;
            }
            const auto state = !edits.contains( storePath )    ? QStringLiteral( "pack" )
                               : !base.contains( storePath )   ? QStringLiteral( "added" )
                               : !current.contains( storePath ) ? QStringLiteral( "deleted" )
                                                                : QStringLiteral( "edited" );
            files.append( QVariantMap{ { "path", storePath },
                                       { "kind", kind },
                                       { "item", packcanonical::itemOf( storePath ) },
                                       { "state", state } } );
        }
        return files;
    };
    if ( operation == "pack_files" || operation == "pack_edits" ) {
        auto files = listing();
        if ( operation == "pack_edits" ) {
            files.erase( std::remove_if( files.begin(), files.end(),
                                         []( const QVariant& file ) {
                                             return file.toMap().value( "state" ) == "pack";
                                         } ),
                         files.end() );
        }
        return { {}, { { "plugin", id }, { "files", files } } };
    }
    if ( operation == "pack_read" ) {
        const auto files = store.files();
        if ( !files.contains( path ) ) {
            return { "No such item in the pack.", {} };
        }
        // Edited as text, a file in another encoding would come back changed.
        if ( !utf8Text( files.value( path ) ) ) {
            return { "The file is not UTF-8 text, so it cannot be edited in CILogg; change it in "
                     "the pack's source instead.",
                     {} };
        }
        return { {},
                 { { "plugin", id },
                   { "path", path },
                   { "content", QString::fromUtf8( files.value( path ) ) },
                   { "edited", store.localEdits().contains( path ) } } };
    }
    if ( operation == "pack_history" ) {
        if ( !path.isEmpty() && !packcanonical::isCanonical( path ) ) {
            return { "Not an item of the pack.", {} };
        }
        QVariantList commits;
        for ( const auto& commit : store.history( path ) ) {
            commits.append( QVariantMap{ { "commit", commit.id },
                                         { "message", commit.message },
                                         { "time", commit.time.toUTC().toString( Qt::ISODate ) },
                                         { "packVersion", commit.upstream } } );
        }
        return { {}, { { "plugin", id }, { "path", path }, { "commits", commits } } };
    }
    QString error;
    if ( operation == "pack_edit" ) {
        const auto kind = packcanonical::kindOf( path );
        const bool remove = options.value( "remove" ).toBool();
        const auto content = options.value( "content" ).toString().toUtf8();
        const auto files = store.files();
        if ( kind.isEmpty() || kind == QLatin1String( "meta" ) || !packcanonical::isCanonical( path ) ) {
            return { "Not an item of the pack.", {} };
        }
        // Exactly one action: a missing content is never taken as an empty file.
        if ( remove == options.contains( "content" ) ) {
            return { "pack_edit takes either content or remove: true.", {} };
        }
        // Items of the pack are edited in place: they can change or go, not appear (your own
        // items come from Copy to My Previews).
        if ( !files.contains( path ) && !store.upstreamFiles().contains( path ) ) {
            return { "No such item in the pack.", {} };
        }
        // The manifest lists the Lua and Python files a pack has; they and the preview order
        // are edited, never deleted.
        if ( remove && ( kind == QLatin1String( "file" ) || kind == QLatin1String( "order" ) ) ) {
            return { "Pack files and the preview order can be edited, not deleted.", {} };
        }
        if ( !remove && content.size() > MaxPackFileBytes ) {
            return { "The item is larger than 1 MiB.", {} };
        }
        std::optional<QByteArray> bytes;
        if ( !remove ) {
            bytes = content;
            if ( kind == QLatin1String( "preview" ) || kind == QLatin1String( "block" ) ) {
                QJsonParseError parseError;
                const auto document = QJsonDocument::fromJson( content, &parseError );
                if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
                    return { QStringLiteral( "The item is not a JSON object: %1" )
                                 .arg( parseError.errorString() ),
                             {} };
                }
                if ( document.object().value( "name" ).toString() != packcanonical::itemOf( path ) ) {
                    return { "The item's name must stay the same (copy it to rename it).", {} };
                }
                bytes = packcanonical::itemBytes( document.object() );
            }
        }
        auto candidate = files;
        if ( bytes ) {
            candidate.insert( path, *bytes );
        }
        else {
            candidate.remove( path );
        }
        // The content folder must load afterwards: the previews document as written stays within
        // 1 MiB.
        if ( const auto tooLarge = previewsTooLarge( candidate ); !tooLarge.isEmpty() ) {
            return { tooLarge, {} };
        }
        // An enabled pack loads the edit at once: it must pass the same check as enabling it.
        if ( enabled ) {
            if ( const auto refusal = checkPackFiles( id, candidate, manifest.json );
                 !refusal.isEmpty() ) {
                return { refusal, {} };
            }
        }
        // A readable message, such as "Edit preview 'Frame'" (design 4.6).
        const auto item = packcanonical::itemOf( path );
        const auto subject = kind == QLatin1String( "file" )
                                 ? item
                                 : QStringLiteral( "%1 '%2'" ).arg( kind, item );
        auto message = options.value( "message" ).toString().left( 256 );
        if ( message.isEmpty() ) {
            message = ( remove ? QStringLiteral( "Delete %1" ) : QStringLiteral( "Edit %1" ) )
                          .arg( subject );
        }
        error = store.commitEdit( path, bytes, message );
    }
    else if ( operation == "pack_reset" ) {
        if ( !path.isEmpty() && !packcanonical::isCanonical( path ) ) {
            return { "Not an item of the pack.", {} };
        }
        const auto commit = options.value( "commit" ).toString();
        // A reset brings back what an edit removed: an enabled pack loads it at once, so it
        // passes the same check as an edit.
        if ( enabled ) {
            PackFiles candidate;
            bool changes = false;
            auto refusal = store.planRestore( path, commit, &candidate, &changes );
            if ( refusal.isEmpty() && changes ) {
                refusal = checkPackFiles( id, candidate, manifest.json );
            }
            if ( !refusal.isEmpty() ) {
                return { refusal, {} };
            }
        }
        const auto what = path.isEmpty() ? QStringLiteral( "the pack" ) : packcanonical::itemOf( path );
        error = store.restore( path, commit,
                               commit.isEmpty()
                                   ? QStringLiteral( "Reset %1 to the pack version" ).arg( what )
                                   : QStringLiteral( "Revert %1 to %2" ).arg( what, commit.left( 12 ) ) );
    }
    // The store can have changed even when its files could not be written afterwards, so the
    // summary and listeners follow it in both cases.
    const auto content = store.contentDirectory();
    it->second->pack = packSummary( manifest.json, content.isEmpty() ? manifest.directory : content );
    it->second->pack[ "localEdits" ] = static_cast<int>( store.localEdits().size() );
    LOG_INFO << "Pack " << operation.toStdString() << " id=" << id.toStdString()
             << " path=" << path.toStdString() << " ok=" << error.isEmpty();
    Q_EMIT changed();
    if ( !error.isEmpty() ) {
        return { error, {} };
    }
    return { {}, { { "plugin", id }, { "files", listing() } } };
}
} // namespace cilogg::plugins
