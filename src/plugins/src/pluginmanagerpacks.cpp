// Configuration packs edited in place (docs/plugin-repositories.md 4.6): the local store of each
// pack, updates that merge the edits, and the pack operations of plugin_manage.
#include "pluginmanager.h"

#include "configpack.h"
#include "log.h"
#include "packstore.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>

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

QVariantMap conflictInfo( const PackConflict& conflict )
{
    return { { "path", conflict.path },
             { "kind", conflict.kind },
             { "item", conflict.item },
             { "field", conflict.field },
             { "base", QString::fromUtf8( conflict.base ) },
             { "mine", QString::fromUtf8( conflict.mine ) },
             { "theirs", QString::fromUtf8( conflict.theirs ) } };
}

// A resolution chosen in the merge window, or given by automation: {path, field, value} or
// {path, field, remove: true} for a structured item, {path, text} or {path, remove: true} for a
// file. Returns false when it cannot be applied.
bool applyResolution( PackFiles* merged, const QVariantMap& resolution )
{
    const auto path = resolution.value( "path" ).toString();
    const auto kind = packcanonical::kindOf( path );
    if ( kind.isEmpty() ) {
        return false;
    }
    const bool remove = resolution.value( "remove" ).toBool();
    if ( kind == QLatin1String( "file" ) || kind == QLatin1String( "order" ) ) {
        if ( remove ) {
            merged->remove( path );
        }
        else if ( resolution.contains( "text" ) ) {
            const auto text = resolution.value( "text" ).toString().toUtf8();
            // A resolved file has no conflict markers left.
            if ( text.contains( "\n<<<<<<< " ) || text.startsWith( "<<<<<<< " ) ) {
                return false;
            }
            merged->insert( path, text );
        }
        else {
            return false;
        }
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
        if ( value && value->isObject() ) {
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
    const auto changed = packmerge::withField( current, field, value );
    merged->insert( path, packcanonical::itemBytes( changed.toObject() ) );
    return true;
}
} // namespace

QString PluginManager::packStoreRoot() const
{
    return QDir( dataRoot_ ).filePath( "pack-store" );
}

QString PluginManager::packContentDirectory( const QString& pluginId ) const
{
    const PackStore store( packStoreRoot(), pluginId );
    const auto content = store.exists() ? store.contentDirectory() : QString{};
    return content.isEmpty() ? pluginDirectory( pluginId ) : content;
}

QString PluginManager::preparePackInstall( const StagedPlugin& staged, const QVariantMap& context,
                                           PackFiles* shipped, std::optional<PackFiles>* merged,
                                           QVariantMap* result ) const
{
    QString error;
    *shipped = packcanonical::fromPackFolder( staged.manifest.json, staged.manifest.directory,
                                              &error );
    if ( !error.isEmpty() ) {
        return error;
    }
    const PackStore store( packStoreRoot(), staged.manifest.id );
    if ( !store.exists() ) {
        return {}; // a first install: nothing to merge
    }
    const auto strategy = strategyOf( context.value( "strategy" ) ).value_or( MergeStrategy::Fail );
    auto plan = store.planUpdate( *shipped, strategy );
    // Resolutions from the merge window (or automation) settle conflicts one by one.
    const auto resolutions = context.value( "resolutions" ).toList();
    QVector<PackConflict> open;
    for ( const auto& conflict : plan.conflicts ) {
        const auto resolved = std::any_of(
            resolutions.cbegin(), resolutions.cend(), [ &conflict ]( const QVariant& item ) {
                const auto resolution = item.toMap();
                return resolution.value( "path" ).toString() == conflict.path
                       && resolution.value( "field" ).toString() == conflict.field;
            } );
        if ( !resolved ) {
            open.append( conflict );
        }
    }
    for ( const auto& item : resolutions ) {
        if ( !applyResolution( &plan.merged, item.toMap() ) ) {
            return QStringLiteral( "Invalid merge resolution for %1." )
                .arg( item.toMap().value( "path" ).toString().left( 128 ) );
        }
    }
    QVariantList conflicts;
    for ( const auto& conflict : plan.conflicts ) {
        conflicts.append( conflictInfo( conflict ) );
    }
    ( *result )[ "conflicts" ] = conflicts;
    if ( strategy == MergeStrategy::Fail && !open.isEmpty() ) {
        QStringList items;
        for ( const auto& conflict : open ) {
            if ( !items.contains( conflict.item ) && items.size() < MaxConflictsListed ) {
                items.append( conflict.item );
            }
        }
        ( *result )[ "needsMerge" ] = true;
        return QStringLiteral( "The new version changes %1 that you edited (%2). Merge it in the "
                               "Plugins window, or update with the strategy keep_mine or "
                               "take_theirs." )
            .arg( open.size() == 1 ? QStringLiteral( "an item" )
                                   : QStringLiteral( "%1 places" ).arg( open.size() ),
                  items.join( ", " ) );
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
    settings_.remove( configurationKey( staged.manifest.id ) + "needsMerge" );
    LOG_INFO << "Pack store updated id=" << staged.manifest.id.toStdString()
             << " version=" << staged.manifest.version.toStdString() << " merged=" << bool( merged )
             << " error=" << error.toStdString();
    return error;
}

void PluginManager::removePackStore( const QString& pluginId )
{
    PackStore store( packStoreRoot(), pluginId );
    if ( store.exists() ) {
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
    PackStore store( packStoreRoot(), id );
    // A pack installed before its store existed gets one now, from its installed folder.
    if ( !store.exists() ) {
        QString error;
        const auto files
            = packcanonical::fromPackFolder( manifest.json, manifest.directory, &error );
        if ( error.isEmpty() ) {
            error = store.install( files, manifest.json, manifest.version,
                                   QStringLiteral( "the installed folder" ) );
        }
        if ( !error.isEmpty() ) {
            return { error, {} };
        }
    }
    const auto path = options.value( "path" ).toString();
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
        return { {},
                 { { "plugin", id },
                   { "path", path },
                   { "content", QString::fromUtf8( files.value( path ) ) },
                   { "edited", store.localEdits().contains( path ) } } };
    }
    if ( operation == "pack_history" ) {
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
        if ( kind.isEmpty() || kind == QLatin1String( "meta" ) ) {
            return { "Not an item of the pack.", {} };
        }
        // Lua and Python files are the ones the manifest lists: they can change, not appear.
        if ( ( kind == QLatin1String( "file" ) || kind == QLatin1String( "order" ) )
             && !files.contains( path ) && !store.upstreamFiles().contains( path ) ) {
            return { "The pack does not list that file.", {} };
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
        const auto commit = options.value( "commit" ).toString();
        const auto what = path.isEmpty() ? QStringLiteral( "the pack" ) : packcanonical::itemOf( path );
        error = store.restore( path, commit,
                               commit.isEmpty()
                                   ? QStringLiteral( "Reset %1 to the pack version" ).arg( what )
                                   : QStringLiteral( "Revert %1 to %2" ).arg( what, commit.left( 12 ) ) );
    }
    if ( !error.isEmpty() ) {
        return { error, {} };
    }
    it->second->pack = packSummary( manifest.json, store.contentDirectory() );
    it->second->pack[ "localEdits" ] = static_cast<int>( store.localEdits().size() );
    LOG_INFO << "Pack " << operation.toStdString() << " id=" << id.toStdString()
             << " path=" << path.toStdString();
    Q_EMIT changed();
    return { {}, { { "plugin", id }, { "files", listing() } } };
}
} // namespace cilogg::plugins
