// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewpackregistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <algorithm>

#include "actionsconfigparser.h"
#include "actionsmanager.h"
#include "configpack.h"
#include "log.h"
#include "packcanonical.h"
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

// Two preview routes that decode alike (a saved one has `protocol` "" when it had none). A route
// goes to a TShark dissector or to a pack's Python decoder (`python`, MD03b).
bool sameRoute( const QVariant& a, const QVariant& b )
{
    const auto left = a.toMap();
    const auto right = b.toMap();
    return left.value( "dissector" ).toString() == right.value( "dissector" ).toString()
           && left.value( "protocol" ).toString() == right.value( "protocol" ).toString()
           && left.value( "python" ).toString() == right.value( "python" ).toString();
}

enum class CopyRoute {
    Added,   // added to the configuration
    Nothing, // none to add: the copy already decodes as the preview does
    Taken,   // the user has another route under the copy's name: the copy would decode with it
};

// The route of the copy `target` of the pack's preview `name`: the route `name` decodes with in
// `before` (the user's own route for it, which wins, or else the pack's), added to
// `configuration` (the user's; `before` as it was, ahead of other copies made with this one).
CopyRoute addCopyRoute( const PreviewPackRegistry::Contribution& pack, const QString& name,
                        const QString& target, const QVariantMap& before,
                        QVariantMap* configuration )
{
    const auto userRoutes = before.value( "previewRoutes" ).toMap();
    const auto source = userRoutes.contains( name ) ? userRoutes.value( name )
                                                    : pack.previewRoutes.value( name );
    auto routes = configuration->value( "previewRoutes" ).toMap();
    if ( routes.contains( target ) ) {
        return source.isValid() && sameRoute( routes.value( target ), source ) ? CopyRoute::Nothing
                                                                               : CopyRoute::Taken;
    }
    if ( !source.isValid() ) {
        return CopyRoute::Nothing;
    }
    routes.insert( target, source );
    ( *configuration )[ "previewRoutes" ] = routes;
    return CopyRoute::Added;
}

QString routeTaken( const QString& target )
{
    return PreviewPackRegistry::tr( "you already have another decoder route named \"%1\", which "
                                    "the copy would decode with" )
        .arg( target );
}

// Why the decoder settings refused the user's routes, for a message.
QString routeRefusal( const QString& reason )
{
    if ( reason == QLatin1String( "route_limit" ) ) {
        return PreviewPackRegistry::tr( "you already have %1 preview routes, the most there can be" )
            .arg( PreviewDecoderSettings::PreviewRouteLimit );
    }
    if ( reason == QLatin1String( "configuration_limit" ) ) {
        return PreviewPackRegistry::tr( "the decoder settings would pass 64 KiB" );
    }
    return reason;
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
    if ( !content.filters.isEmpty() ) {
        QFile file( QDir( directory ).filePath( content.filters ) );
        QString error;
        const auto items = file.open( QIODevice::ReadOnly )
                               ? cilogg::plugins::packcanonical::fromFilters(
                                     file.read( cilogg::plugins::MaxPackFileBytes ), &error )
                               : cilogg::plugins::PackFiles{};
        // Only this file's filters are left out when it cannot be read (see
        // Contribution::warnings).
        if ( !error.isEmpty() || !file.isOpen() ) {
            pack.warnings.append( QObject::tr( "The pack's filters cannot be read: %1" )
                                      .arg( error.isEmpty() ? file.errorString() : error ) );
        }
        // In the pack's order (filters/order.txt lists every filter once).
        const auto order = QString::fromUtf8( items.value( QStringLiteral( "filters/order.txt" ) ) )
                               .split( '\n', Qt::SkipEmptyParts );
        for ( const auto& filterId : order ) {
            const auto item
                = QJsonDocument::fromJson(
                      items.value(
                          QStringLiteral( "filters/%1.json" )
                              .arg( cilogg::plugins::packcanonical::encodeName( filterId ) ) ) )
                      .object();
            pack.filters.append( { { filterId, item.value( "name" ).toString(),
                                     item.value( "filter" ).toString(),
                                     item.value( "regex" ).toBool( true ) },
                                   id,
                                   packLabel( pack ) } );
        }
    }
    if ( !content.highlights.isEmpty() ) {
        QFile file( QDir( directory ).filePath( content.highlights ) );
        QString error;
        const auto bytes = file.open( QIODevice::ReadOnly )
                               ? file.read( cilogg::plugins::MaxPackFileBytes )
                               : QByteArray{};
        // Checked as the store reads it first: exactly a highlighters file, every set with an id.
        if ( file.isOpen() ) {
            cilogg::plugins::packcanonical::fromHighlights( bytes, &error );
        }
        // Then read as the user's highlighters are read, from a copy (QSettings reads INI files
        // only, and caches them by path).
        QTemporaryDir folder;
        const auto copy = folder.filePath( QStringLiteral( "highlights.conf" ) );
        QFile written( copy );
        if ( error.isEmpty() && file.isOpen()
             && ( !folder.isValid() || !written.open( QIODevice::WriteOnly )
                  || written.write( bytes ) != bytes.size() ) ) {
            error = QObject::tr( "no temporary folder to read it in" );
        }
        written.close();
        // Only this file's sets are left out when it cannot be read (see Contribution::warnings).
        if ( !error.isEmpty() || !file.isOpen() ) {
            pack.warnings.append( QObject::tr( "The pack's highlighters cannot be read: %1" )
                                      .arg( error.isEmpty() ? file.errorString() : error ) );
        }
        else {
            QSettings settings( copy, QSettings::IniFormat );
            HighlighterSetCollection collection;
            collection.retrieveFromStorage( settings );
            for ( const auto& set : collection.highlighterSets() ) {
                pack.highlighterSets.append( { set, id, packLabel( pack ) } );
            }
        }
    }
    if ( !content.actions.isEmpty() ) {
        QFile file( QDir( directory ).filePath( content.actions ) );
        QString error;
        const auto bytes = file.open( QIODevice::ReadOnly )
                               ? file.read( cilogg::plugins::MaxPackFileBytes )
                               : QByteArray{};
        // Checked as the store reads it first: exactly an export, every item with its id and a
        // response running its own pack's actions only.
        if ( file.isOpen() ) {
            cilogg::plugins::packcanonical::fromActions( bytes, &error );
        }
        // Then read as the user's actions are read, and every item one CILogg would accept as
        // the user's own (Actions window, import).
        ActionsParseResult parsed;
        if ( error.isEmpty() && file.isOpen() ) {
            parsed = ActionsConfigParser().parseJson( bytes );
            if ( !parsed.errors.isEmpty() ) {
                error = parsed.errors.first();
            }
        }
        for ( const auto& action : parsed.actions ) {
            QString why;
            if ( error.isEmpty() && !validateActionDefinition( action, &why ) ) {
                error = QObject::tr( "action %1 (\"%2\"): %3" )
                            .arg( action.id )
                            .arg( action.name, why );
            }
        }
        for ( const auto& response : parsed.responses ) {
            QString why;
            if ( error.isEmpty() && !validateResponseDefinition( response, &why ) ) {
                error = QObject::tr( "response %1 (\"%2\"): %3" )
                            .arg( response.id )
                            .arg( response.name, why );
            }
        }
        // Only this file's items are left out when it cannot be read (see Contribution::warnings).
        if ( !error.isEmpty() || !file.isOpen() ) {
            pack.warnings.append( QObject::tr( "The pack's actions cannot be read: %1" )
                                      .arg( error.isEmpty() ? file.errorString() : error ) );
        }
        else {
            pack.actions = parsed.actions;
            pack.responses = parsed.responses;
        }
    }
    pack.luaFiles = content.luaFiles( directory );
    pack.previewRoutes = content.previewRoutes.toVariantMap();
    pack.protocolRoutes = content.protocolRoutes.toVariantMap();
    pack.pythonDecoders = static_cast<int>( content.pythonDecoders.size() );
    pack.root = QDir::cleanPath( QDir( directory ).absolutePath() );
    for ( const auto& decoder : content.pythonDecoders ) {
        pack.pythonModules.append( QVariantMap{ { "id", decoder.id }, { "module", decoder.module } } );
    }
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
    QList<ListedFilter> filters;
    QSet<QString> filterIds;
    QList<ListedHighlighterSet> highlighterSets;
    QSet<QString> setIds;
    QVector<PackActionItems> actionItems;
    for ( const auto& pack : packs ) {
        cache_.insert( pack.id, pack );
        if ( !pack.error.isEmpty() ) {
            LOG_WARNING << "Configuration pack not applied id=" << pack.id.toStdString()
                        << " reason=" << pack.error.toStdString();
            continue;
        }
        for ( const auto& warning : pack.warnings ) {
            LOG_WARNING << "Configuration pack applied without some items id="
                        << pack.id.toStdString() << " reason=" << warning.toStdString();
        }
        previews.append( pack.previews );
        // Between packs there is no precedence; the pack check refuses a shared filter id, so a
        // repeated one (a check bypassed) keeps the first pack's filter only.
        for ( const auto& listed : pack.filters ) {
            if ( !filterIds.contains( listed.filter.id ) ) {
                filterIds.insert( listed.filter.id );
                filters.append( listed );
            }
        }
        // The same for highlighter sets (E16c2).
        for ( const auto& listed : pack.highlighterSets ) {
            if ( !setIds.contains( listed.set.id() ) ) {
                setIds.insert( listed.set.id() );
                highlighterSets.append( listed );
            }
        }
        // Actions and responses (E16c3) are namespaced by their pack: no two packs share one.
        if ( !pack.actions.isEmpty() || !pack.responses.isEmpty() ) {
            actionItems.append( { pack.id, packLabel( pack ), pack.actions, pack.responses } );
        }
        if ( !pack.luaFiles.isEmpty() ) {
            decoder.lua.append( QVariantMap{ { "id", pack.id }, { "files", pack.luaFiles } } );
        }
        // MD03b: the worker copies and hashes these modules when a session starts.
        if ( !pack.pythonModules.isEmpty() ) {
            decoder.python.append( QVariantMap{
                { "id", pack.id }, { "root", pack.root }, { "decoders", pack.pythonModules } } );
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
             << " lua_packs=" << decoder.lua.size() << " python_packs=" << decoder.python.size();
    // The decoder first, so the preview definitions rebuilt next carry its new revision.
    PreviewDecoderSettings::instance().setPacks( decoder );
    PreviewManager::instance().setPackPreviews( previews );
    PackFilters::instance().setFilters( filters );
    PackHighlighters::instance().setSets( highlighterSets );
    ActionsManager::instance().setPackItems( actionItems );
}

QString PreviewPackRegistry::copyPreview( const QString& name, const QString& copyName )
{
    const auto copy = [ &name, &copyName ] {
        return PreviewManager::instance().copyPackPreview( name, copyName );
    };
    if ( const auto origin = PreviewManager::instance().packOrigin( name ) ) {
        for ( const auto& pack : applied_ ) {
            if ( pack.id == origin->packId ) {
                return copyWithRoute( pack, name, copyName.isEmpty() ? name : copyName, copy );
            }
        }
    }
    // Not a preview of an enabled pack: copyPackPreview says so.
    return copy();
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
    return copyWithRoute( pack, name, copyName.isEmpty() ? name : copyName,
                          [ &pack, &name, &copyName ] {
                              return PreviewManager::instance().copyPreviewFrom( pack.previews,
                                                                                 name, copyName );
                          } );
}

QString PreviewPackRegistry::checkCopyRoutes( const Contribution& pack,
                                              const QMap<QString, QString>& copies ) const
{
    auto& settings = PreviewDecoderSettings::instance();
    const auto before = settings.configuration();
    auto configuration = before;
    bool added = false;
    for ( auto copy = copies.cbegin(); copy != copies.cend(); ++copy ) {
        const auto route = addCopyRoute( pack, copy.key(), copy.value(), before, &configuration );
        if ( route == CopyRoute::Taken ) {
            return tr( "The decoder routes of the copies cannot be added to your routes: %1." )
                .arg( routeTaken( copy.value() ) );
        }
        added = added || route == CopyRoute::Added;
    }
    QString reason;
    if ( added && !settings.check( configuration, &reason ) ) {
        return tr( "The decoder routes of the copies cannot be added to your routes: %1." )
            .arg( routeRefusal( reason ) );
    }
    return {};
}

QStringList PreviewPackRegistry::installedPackPreviewNames() const
{
    QStringList names;
    if ( !manager_ ) {
        return names;
    }
    // The names only, read even from a previews document that is not valid now (an edit can be
    // reset later): what loads now and what the installed version ships (a reset brings it back).
    const auto namesIn = [ &names ]( const QString& path ) {
        QFile file( path );
        if ( !file.open( QIODevice::ReadOnly ) || file.size() > cilogg::plugins::MaxPackFileBytes ) {
            return;
        }
        const auto document = QJsonDocument::fromJson( file.readAll() );
        const auto previews
            = document.isArray() ? document.array() : document.object().value( "previews" ).toArray();
        for ( const auto& preview : previews ) {
            const auto name = preview.toObject().value( "name" ).toString();
            if ( !name.isEmpty() ) {
                names.append( name );
            }
        }
    };
    for ( const auto& plugin : manager_->plugins() ) {
        const auto info = plugin.toMap();
        const auto id = info.value( "id" ).toString();
        const auto manifest = manager_->manifest( id );
        PackContent content;
        if ( !cilogg::plugins::isConfigPack( manifest )
             || !validatePackManifest( manifest, &content ).isEmpty() || content.previews.isEmpty() ) {
            continue;
        }
        namesIn( QDir( manager_->packContentDirectory( id ) ).filePath( content.previews ) );
        namesIn( QDir( info.value( "directory" ).toString() ).filePath( content.previews ) );
    }
    names.removeDuplicates();
    return names;
}

QString PreviewPackRegistry::copyWithRoute( const Contribution& pack, const QString& name,
                                            const QString& target,
                                            const std::function<QString()>& copy )
{
    // A copy under another name never takes a pack preview's name, also of a pack not enabled
    // now: it would replace that preview once the pack is enabled.
    if ( target != name && instance().installedPackPreviewNames().contains( target ) ) {
        return tr( "A pack has a preview named \"%1\", which the copy would replace." ).arg( target );
    }
    // The copy has no link to the pack, so it keeps decoding without it: the route the preview
    // decodes with becomes the user's route for the copy. The route is saved first: a route under
    // a name no preview has decodes nothing, while a copy without its route would decode wrongly.
    // A copy that then fails takes its route back.
    auto& settings = PreviewDecoderSettings::instance();
    const auto previous = settings.configuration();
    auto configuration = previous;
    QString reason;
    const auto route = addCopyRoute( pack, name, target, previous, &configuration );
    if ( route == CopyRoute::Taken
         || ( route == CopyRoute::Added && !settings.setConfiguration( configuration, &reason ) ) ) {
        LOG_WARNING << "Pack preview not copied, its route cannot be added name="
                    << name.toStdString() << " reason="
                    << ( route == CopyRoute::Taken ? std::string( "taken" ) : reason.toStdString() );
        return tr( "\"%1\" was not copied: its decoder route cannot be added to your routes: %2." )
            .arg( target, route == CopyRoute::Taken ? routeTaken( target ) : routeRefusal( reason ) );
    }
    const auto error = copy();
    if ( !error.isEmpty() && route == CopyRoute::Added
         && !settings.setConfiguration( previous, &reason ) ) {
        // Left in place it decodes nothing, and another try finds the same route there.
        LOG_WARNING << "Route of a pack preview that was not copied stays name="
                    << name.toStdString() << " reason=" << reason.toStdString();
    }
    return error;
}

QString PreviewPackRegistry::check( const QString& id, const QString& directory,
                                    const QJsonObject& manifest ) const
{
    const auto candidate = read( id, directory, manifest );
    if ( !candidate.error.isEmpty() ) {
        return candidate.error;
    }
    // A pack enabled already keeps the rest of its content; enabling or updating one is refused
    // while a file of it cannot be read, so the user learns why.
    if ( !candidate.warnings.isEmpty() ) {
        return candidate.warnings.first();
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
    // Filters: no id shared with another enabled pack, and the user's filters and every enabled
    // pack's together within the limit (design 4.5). A filter of the user's with a pack filter's
    // id replaces it, so that pack filter is not listed and not counted.
    const auto userFilters = PredefinedFiltersCollection::getSynced().getFilters();
    QSet<QString> userFilterIds;
    for ( const auto& filter : userFilters ) {
        userFilterIds.insert( filter.id );
    }
    const auto listedFilters = [ &userFilterIds ]( const QList<ListedFilter>& filters ) {
        return std::count_if( filters.cbegin(), filters.cend(),
                              [ &userFilterIds ]( const ListedFilter& listed ) {
                                  return !userFilterIds.contains( listed.filter.id );
                              } );
    };
    auto filterCount = userFilters.size() + listedFilters( candidate.filters );
    QHash<QString, const Contribution*> filterOwners; // id -> the enabled pack that has it
    for ( const auto& other : others ) {
        for ( const auto& listed : other.filters ) {
            filterOwners.insert( listed.filter.id, &other );
        }
        filterCount += listedFilters( other.filters );
    }
    for ( const auto& listed : candidate.filters ) {
        if ( const auto* owner = filterOwners.value( listed.filter.id ) ) {
            return tr( "The packs \"%1\" and \"%2\" both have a filter with the id \"%3\". "
                       "Disable one of them first." )
                .arg( packLabel( candidate ), packLabel( *owner ), listed.filter.id );
        }
    }
    if ( filterCount > FilterLimit ) {
        return tr( "Enabling \"%1\" would make %2 predefined filters (at most %3, your own "
                   "included)." )
            .arg( packLabel( candidate ) )
            .arg( filterCount )
            .arg( FilterLimit );
    }
    // Highlighter sets (E16c2): the same, no id shared with another enabled pack and the user's
    // sets and every enabled pack's listed ones (HighlighterSetCollection::listedSets) together
    // within the limit.
    const auto userSets = HighlighterSetCollection::getSynced().highlighterSets();
    QSet<QString> userSetIds;
    for ( const auto& set : userSets ) {
        userSetIds.insert( set.id() );
    }
    const auto listedSets = [ &userSetIds ]( const QList<ListedHighlighterSet>& sets ) {
        return std::count_if( sets.cbegin(), sets.cend(),
                              [ &userSetIds ]( const ListedHighlighterSet& listed ) {
                                  return !userSetIds.contains( listed.set.id() );
                              } );
    };
    auto setCount = userSets.size() + listedSets( candidate.highlighterSets );
    QHash<QString, const Contribution*> setOwners; // id -> the enabled pack that has it
    for ( const auto& other : others ) {
        for ( const auto& listed : other.highlighterSets ) {
            setOwners.insert( listed.set.id(), &other );
        }
        setCount += listedSets( other.highlighterSets );
    }
    for ( const auto& listed : candidate.highlighterSets ) {
        if ( const auto* owner = setOwners.value( listed.set.id() ) ) {
            return tr( "The packs \"%1\" and \"%2\" both have a highlighter set with the id "
                       "\"%3\". Disable one of them first." )
                .arg( packLabel( candidate ), packLabel( *owner ), listed.set.id() );
        }
    }
    if ( setCount > MaxListedHighlighterSets ) {
        return tr( "Enabling \"%1\" would make %2 highlighter sets (at most %3, your own "
                   "included)." )
            .arg( packLabel( candidate ) )
            .arg( setCount )
            .arg( MaxListedHighlighterSets );
    }
    // Actions and responses (E16c3): namespaced by their pack, so never shared, and the user's
    // and every enabled pack's together within the limit of each kind.
    auto actionCount = ActionsManager::instance().actions().size() + candidate.actions.size();
    auto responseCount = ActionsManager::instance().responses().size() + candidate.responses.size();
    for ( const auto& other : others ) {
        actionCount += other.actions.size();
        responseCount += other.responses.size();
    }
    // A pack that brings none of a kind is not refused over it (the user's own may pass the limit,
    // and then the packs' give way).
    const bool tooManyActions = !candidate.actions.isEmpty() && actionCount > MaxListedActionItems;
    const bool tooManyResponses
        = !candidate.responses.isEmpty() && responseCount > MaxListedActionItems;
    if ( tooManyActions || tooManyResponses ) {
        const bool actions = tooManyActions;
        return tr( "Enabling \"%1\" would make %2 %3 (at most %4, your own included)." )
            .arg( packLabel( candidate ) )
            .arg( actions ? actionCount : responseCount )
            .arg( actions ? tr( "actions" ) : tr( "automatic responses" ) )
            .arg( MaxListedActionItems );
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
    // Python: every enabled pack's decoder modules within the session's limit (MD03b).
    auto pythonModules = candidate.pythonModules.size();
    for ( const auto& other : others ) {
        pythonModules += other.pythonModules.size();
    }
    if ( pythonModules > PreviewDecoderSettings::ProfilePythonLimit ) {
        return tr( "Enabling \"%1\" would make %2 Python decoders in the enabled packs (at most "
                   "%3)." )
            .arg( packLabel( candidate ) )
            .arg( pythonModules )
            .arg( PreviewDecoderSettings::ProfilePythonLimit );
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
