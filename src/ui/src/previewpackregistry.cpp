// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewpackregistry.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

#include "configpack.h"
#include "log.h"
#include "pluginmanager.h"
#include "previewconfigparser.h"
#include "previewdecodersettings.h"
#include "previewservice.h"

namespace {
using cilogg::plugins::PackContent;
using cilogg::plugins::validatePackFiles;
using cilogg::plugins::validatePackManifest;

// What makes a pack's contribution current: its version and the time its manifest was written,
// so an update (or a reinstall of the same version) is read again.
QString contributionKey( const QString& directory, const QJsonObject& manifest )
{
    const QFileInfo file( QDir( directory ).filePath( "plugin.json" ) );
    return manifest.value( "version" ).toString() + '|' + directory + '|'
           + QString::number( file.lastModified().toMSecsSinceEpoch() );
}

QString packLabel( const PreviewPackRegistry::Contribution& pack )
{
    return pack.name.isEmpty() ? pack.id : pack.name;
}
} // namespace

PreviewPackRegistry& PreviewPackRegistry::instance()
{
    static PreviewPackRegistry registry;
    return registry;
}

void PreviewPackRegistry::attach( cilogg::plugins::PluginManager* manager )
{
    if ( manager_ ) {
        manager_->disconnect( this );
    }
    manager_ = manager;
    if ( !manager ) {
        refresh();
        return;
    }
    manager->setPackCheck( [ this ]( const QString& id, const QString& directory,
                                     const QJsonObject& json ) {
        return check( id, directory, json );
    } );
    connect( manager, &cilogg::plugins::PluginManager::changed, this,
             &PreviewPackRegistry::refresh );
    refresh();
}

PreviewPackRegistry::Contribution PreviewPackRegistry::read( const QString& id,
                                                             const QString& directory,
                                                             const QJsonObject& manifest )
{
    Contribution pack;
    pack.id = id;
    pack.name = manifest.value( "name" ).toString();
    pack.key = contributionKey( directory, manifest );
    pack.previews.packId = id;
    pack.previews.packName = packLabel( pack );
    PackContent content;
    pack.error = validatePackManifest( manifest, &content );
    // The files are checked again whenever they are read: they may have changed since the pack
    // was installed (sizes, links, files inside the pack).
    if ( pack.error.isEmpty() ) {
        pack.error = validatePackFiles( manifest, directory );
    }
    if ( !pack.error.isEmpty() ) {
        return pack;
    }
    if ( !content.previews.isEmpty() ) {
        const auto parsed
            = PreviewConfigParser().parseFile( QDir( directory ).filePath( content.previews ) );
        if ( !parsed.errors.isEmpty() ) {
            pack.error = QObject::tr( "The pack's previews cannot be read: %1" )
                             .arg( parsed.errors.first() );
            return pack;
        }
        pack.previews.previews = parsed.previews;
        pack.previews.blocks = parsed.blocks;
    }
    pack.luaFiles = content.luaFiles( directory );
    pack.previewRoutes = content.previewRoutes.toVariantMap();
    pack.protocolRoutes = content.protocolRoutes.toVariantMap();
    pack.pythonDecoders = static_cast<int>( content.pythonDecoders.size() );
    return pack;
}

void PreviewPackRegistry::refresh()
{
    QVector<Contribution> packs;
    if ( manager_ ) {
        // A cheap list: the manager signals every job change, so this runs often.
        for ( const auto& id : manager_->enabledPacks() ) {
            const auto manifest = manager_->manifest( id );
            // The effective content: the installed version with the user's edits (design 4.6).
            const auto directory = manager_->packContentDirectory( id );
            // The store commit makes every edit and reset a new contribution, also when it
            // lands within the file system's time resolution.
            const auto key = contributionKey( directory, manifest ) + QLatin1Char( '|' )
                             + manager_->packRevision( id );
            const auto cached = cache_.find( id );
            auto contribution = cached != cache_.end() && cached->key == key
                                    ? cached.value()
                                    : read( id, directory, manifest );
            contribution.key = key;
            packs.append( contribution );
        }
    }
    std::sort( packs.begin(), packs.end(),
               []( const auto& left, const auto& right ) { return left.id < right.id; } );
    // The manager signals every job and state change; only a different set of enabled packs
    // (or a new version of one) is applied.
    QStringList keys;
    for ( const auto& pack : packs ) {
        keys.append( pack.id + '@' + pack.key );
    }
    if ( everApplied_ && keys == appliedKeys_ ) {
        return;
    }
    everApplied_ = true;
    appliedKeys_ = keys;
    cache_.clear();
    QVector<PreviewPackSource> previews;
    PreviewDecoderPacks decoder;
    for ( const auto& pack : packs ) {
        cache_.insert( pack.id, pack );
        if ( !pack.error.isEmpty() ) {
            LOG_WARNING << "Configuration pack not applied id=" << pack.id.toStdString()
                        << " reason=" << pack.error.toStdString();
            continue;
        }
        previews.append( pack.previews );
        if ( !pack.luaFiles.isEmpty() ) {
            decoder.lua.append( QVariantMap{ { "id", pack.id }, { "files", pack.luaFiles } } );
        }
        // Between packs there is no precedence; the pack check refuses a shared route key, so
        // this keeps the first pack's route only if a check was bypassed.
        for ( const auto* key : { "previewRoutes", "protocolRoutes" } ) {
            auto& merged = QLatin1String( key ) == QLatin1String( "previewRoutes" )
                               ? decoder.previewRoutes
                               : decoder.protocolRoutes;
            const auto& routes = QLatin1String( key ) == QLatin1String( "previewRoutes" )
                                     ? pack.previewRoutes
                                     : pack.protocolRoutes;
            for ( auto route = routes.cbegin(); route != routes.cend(); ++route ) {
                if ( !merged.contains( route.key() ) ) {
                    merged.insert( route.key(), route.value() );
                }
            }
        }
    }
    applied_ = packs;
    // Every applied change (also a new version whose Lua keeps its file names) starts new
    // TShark sessions.
    decoder.key = keys.join( '\n' );
    LOG_INFO << "Configuration packs applied packs=" << packs.size()
             << " lua_packs=" << decoder.lua.size();
    // The decoder first, so the preview definitions rebuilt next carry its new revision.
    PreviewDecoderSettings::instance().setPacks( decoder );
    PreviewManager::instance().setPackPreviews( previews );
}

QString PreviewPackRegistry::copyPreview( const QString& name, const QString& copyName )
{
    const auto origin = PreviewManager::instance().packOrigin( name );
    const auto error = PreviewManager::instance().copyPackPreview( name, copyName );
    if ( !error.isEmpty() || !origin ) {
        return error;
    }
    const auto target = copyName.isEmpty() ? name : copyName;
    for ( const auto& pack : applied_ ) {
        if ( pack.id == origin->packId ) {
            return copyRoute( pack, name, target );
        }
    }
    return {};
}

std::optional<PreviewPackRegistry::Contribution>
PreviewPackRegistry::snapshotOfPack( const QString& packId, QString* error ) const
{
    for ( const auto& pack : applied_ ) {
        if ( pack.id == packId && pack.error.isEmpty() ) {
            return pack; // enabled: as applied
        }
    }
    if ( !manager_ ) {
        *error = tr( "The pack is not available." );
        return std::nullopt;
    }
    // A disabled pack (or one not applied yet): read as it is now, with the local edits.
    auto pack = read( packId, manager_->packContentDirectory( packId ), manager_->manifest( packId ) );
    if ( !pack.error.isEmpty() ) {
        *error = pack.error;
        return std::nullopt;
    }
    return pack;
}

QString PreviewPackRegistry::copyPreviewOfSnapshot( const Contribution& pack, const QString& name,
                                                    const QString& copyName )
{
    const auto error = PreviewManager::instance().copyPreviewFrom( pack.previews, name, copyName );
    return error.isEmpty() ? copyRoute( pack, name, copyName.isEmpty() ? name : copyName ) : error;
}

QString PreviewPackRegistry::copyRoute( const Contribution& pack, const QString& name,
                                        const QString& target )
{
    // The copy has no link to the pack, so it keeps decoding without it: the pack's route
    // becomes the user's.
    if ( !pack.previewRoutes.contains( name ) ) {
        return {};
    }
    auto& settings = PreviewDecoderSettings::instance();
    auto configuration = settings.configuration();
    auto routes = configuration.value( "previewRoutes" ).toMap();
    if ( routes.contains( target ) ) {
        return {};
    }
    routes.insert( target, pack.previewRoutes.value( name ) );
    configuration[ "previewRoutes" ] = routes;
    QString reason;
    if ( !settings.setConfiguration( configuration, &reason ) ) {
        LOG_WARNING << "Copied pack preview route not added name=" << name.toStdString()
                    << " reason=" << reason.toStdString();
        return tr( "The preview was copied, but its decoder route could not be added to "
                   "your routes (%1)." )
            .arg( reason );
    }
    return {};
}

QString PreviewPackRegistry::check( const QString& id, const QString& directory,
                                    const QJsonObject& manifest ) const
{
    const auto candidate = read( id, directory, manifest );
    if ( !candidate.error.isEmpty() ) {
        return candidate.error;
    }
    QVector<Contribution> others;
    for ( const auto& pack : applied_ ) {
        if ( pack.id != id && pack.error.isEmpty() ) {
            others.append( pack );
        }
    }
    // Previews: no name shared with another enabled pack (design 4.2).
    for ( const auto& preview : candidate.previews.previews ) {
        for ( const auto& other : others ) {
            const auto shared = std::any_of(
                other.previews.previews.cbegin(), other.previews.previews.cend(),
                [ &preview ]( const auto& item ) { return item.name == preview.name; } );
            if ( shared ) {
                return tr( "The packs \"%1\" and \"%2\" both have a preview named \"%3\". "
                           "Disable one of them first." )
                    .arg( packLabel( candidate ), packLabel( other ), preview.name );
            }
        }
    }
    // Routes: the same key in two packs, and the merged lists within the decoder's limits.
    const auto user = PreviewDecoderSettings::instance().configuration();
    for ( const bool protocol : { false, true } ) {
        const auto& own = protocol ? candidate.protocolRoutes : candidate.previewRoutes;
        auto merged = user.value( protocol ? "protocolRoutes" : "previewRoutes" ).toMap();
        for ( const auto& other : others ) {
            const auto& routes = protocol ? other.protocolRoutes : other.previewRoutes;
            for ( auto route = routes.cbegin(); route != routes.cend(); ++route ) {
                // Between packs there is no precedence, also while a route of the user's hides
                // both: the user may remove it later.
                if ( own.contains( route.key() ) ) {
                    return tr( "The packs \"%1\" and \"%2\" both route \"%3\"." )
                        .arg( packLabel( candidate ), packLabel( other ), route.key() );
                }
                merged.insert( route.key(), route.value() );
            }
        }
        for ( auto route = own.cbegin(); route != own.cend(); ++route ) {
            merged.insert( route.key(), route.value() );
        }
        const auto limit = protocol ? PreviewDecoderSettings::ProtocolRouteLimit
                                    : PreviewDecoderSettings::PreviewRouteLimit;
        if ( merged.size() > limit ) {
            return tr( "Enabling \"%1\" would make %2 %3 decoder routes (at most %4)." )
                .arg( packLabel( candidate ) )
                .arg( merged.size() )
                .arg( protocol ? tr( "protocol" ) : tr( "preview" ) )
                .arg( limit );
        }
    }
    // Lua: the user's scripts and every pack's files within the profile limit (design 4.3).
    auto luaFiles = user.value( "lua" ).toList().size() + candidate.luaFiles.size();
    for ( const auto& other : others ) {
        luaFiles += other.luaFiles.size();
    }
    if ( luaFiles > PreviewDecoderSettings::ProfileLuaLimit ) {
        return tr( "Enabling \"%1\" would load %2 Lua files in TShark (at most %3, your own "
                   "scripts included)." )
            .arg( packLabel( candidate ) )
            .arg( luaFiles )
            .arg( PreviewDecoderSettings::ProfileLuaLimit );
    }
    // The pack's block namespace `<pack-id>/` belongs to the pack: a block of the user's in it
    // would meet the pack's blocks.
    auto& manager = PreviewManager::instance();
    const auto blockNames = manager.blocks().keys();
    const auto reserved = std::find_if( blockNames.cbegin(), blockNames.cend(),
                                        [ &id ]( const QString& block ) {
                                            return block.startsWith( id + '/' );
                                        } );
    if ( reserved != blockNames.cend() ) {
        return tr( "Your previews have a block named \"%1\", in the names reserved for the pack "
                   "\"%2\". Rename that block first." )
            .arg( *reserved, packLabel( candidate ) );
    }
    // Quotas: the preview service gives up above its limits, so a pack must never push the
    // user's previews and the other packs past them (design 4.2).
    auto previews = manager.all();
    auto blocks = manager.blocks();
    others.append( candidate );
    for ( const auto& pack : others ) {
        const auto renamed = PreviewManager::namespaced( pack.previews );
        for ( const auto& preview : renamed.previews ) {
            if ( !manager.findByName( preview.name ) ) {
                previews.append( preview );
            }
        }
        for ( auto block = renamed.blocks.cbegin(); block != renamed.blocks.cend(); ++block ) {
            blocks.insert( block.key(), block.value() ); // as PreviewManager::effectiveBlocks
        }
    }
    if ( PreviewService::definitionsFor( previews, blocks ).isEmpty() ) {
        return tr( "Enabling \"%1\" would take your previews and the enabled packs past the "
                   "preview limits (128 previews, 4,096 blocks, 1 MiB of definitions), and "
                   "previews would stop working." )
            .arg( packLabel( candidate ) );
    }
    return {};
}
