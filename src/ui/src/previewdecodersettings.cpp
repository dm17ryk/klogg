// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewdecodersettings.h"
#include "configpack.h"
#include "log.h"
#include "persistentinfo.h"
#include "previewmanager.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QUuid>
#include <utility>

namespace {
QStringList configuredFiles( const QVariantMap& configuration )
{
    QStringList files;
    for ( const auto* key : { "tshark", "text2pcap" } )
        files << configuration.value( key ).toString();
    for ( const auto& script : configuration.value( "lua" ).toList() )
        files << script.toString();
    files.removeAll( QString{} );
    for ( auto& file : files )
        file = QFileInfo( file ).absoluteFilePath();
    files.removeDuplicates();
    return files;
}
QVariantMap fileMetadata( const QVariantMap& configuration )
{
    QVariantMap result;
    for ( const auto& file : configuredFiles( configuration ) ) {
        const QFileInfo info( file );
        result[ file ] = QVariantMap{ { "exists", info.exists() },
                                      { "size", info.size() },
                                      { "modified", info.lastModified().toMSecsSinceEpoch() },
                                      { "created", info.birthTime().toMSecsSinceEpoch() },
                                      { "canonical", info.canonicalFilePath() },
                                      { "symlink", info.symLinkTarget() } };
    }
    return result;
}
bool isString( const QVariant& value )
{
    return value.userType() == QMetaType::QString;
}
bool named( const QString& value, bool empty = false )
{
    return ( empty && value.isEmpty() ) || PreviewDecoderSettings::isDissectorName( value );
}
bool encoding( const QString& value )
{
    return value == "utf8" || value == "hex" || value == "base64";
}
bool httpHost( const QVariant& value )
{
    // Printable ASCII without spaces or commas: a host name, optionally with a port.
    static const QRegularExpression pattern( "\\A[\\x21-\\x2B\\x2D-\\x7E]{1,255}\\z" );
    return isString( value ) && pattern.match( value.toString() ).hasMatch();
}
bool path( const QString& value, bool executable, bool empty )
{
    if ( value.isEmpty() )
        return empty;
    if ( value.size() > 4096 || value.contains( QChar::Null ) )
        return false;
    const QFileInfo info( value );
    return info.isAbsolute() && info.isFile() && info.isReadable()
           && ( executable ? info.isExecutable() : info.size() <= 1024 * 1024 );
}
bool normalize( const QVariantMap& input, QVariantMap* output, QString* error )
{
    QVariantMap config{ { "enabled", false },
                        { "allowExternalLua", false },
                        { "tshark", "" },
                        { "text2pcap", "" },
                        { "lua", QVariantList{} },
                        { "defaultDissector", "" },
                        { "inputEncoding", "utf8" },
                        { "previewRoutes", QVariantMap{} },
                        { "protocolRoutes", QVariantMap{} },
                        { "assemblyHttpHosts", QVariantList{} } };
    const auto reject = [ error ]( const QString& reason ) {
        if ( error )
            *error = reason;
        LOG_DEBUG << "Preview decoder configuration rejected reason=" << reason;
        return false;
    };
    for ( auto item = input.cbegin(); item != input.cend(); ++item ) {
        if ( !config.contains( item.key() ) )
            return reject( "unknown_key" );
        if ( item.value().userType() != config.value( item.key() ).userType() )
            return reject( "invalid_type" );
        config[ item.key() ] = item.value();
    }
    const bool enabled = config.value( "enabled" ).toBool();
    for ( const auto* key : { "tshark", "text2pcap" } )
        if ( !path( config.value( key ).toString(), true, !enabled ) )
            return reject( "invalid_tool" );
    if ( !named( config.value( "defaultDissector" ).toString(), true ) )
        return reject( "invalid_dissector" );
    if ( !encoding( config.value( "inputEncoding" ).toString() ) )
        return reject( "invalid_encoding" );
    const auto scripts = config.value( "lua" ).toList();
    if ( scripts.size() > 8 )
        return reject( "script_limit" );
    for ( const auto& script : scripts )
        if ( !isString( script ) || !path( script.toString(), false, false ) )
            return reject( "invalid_script" );
    if ( enabled && !scripts.isEmpty() && !config.value( "allowExternalLua" ).toBool() )
        return reject( "external_lua_not_approved" );
    const auto hosts = config.value( "assemblyHttpHosts" ).toList();
    if ( hosts.size() > 16 )
        return reject( "assembly_host_limit" );
    for ( const auto& host : hosts )
        if ( !httpHost( host ) )
            return reject( "invalid_assembly_host" );
    for ( const auto* key : { "previewRoutes", "protocolRoutes" } ) {
        auto routes = config.value( key ).toMap();
        const bool protocol = QString::fromLatin1( key ) == "protocolRoutes";
        if ( routes.size() > ( protocol ? 32 : 128 ) )
            return reject( "route_limit" );
        for ( auto route = routes.begin(); route != routes.end(); ++route ) {
            if ( route.key().isEmpty() || route.key().size() > ( protocol ? 128 : 4096 )
                 || route.key().contains( QChar::Null )
                 || route.value().userType() != QMetaType::QVariantMap )
                return reject( "invalid_route" );
            auto entry = route.value().toMap();
            // A route goes to a TShark dissector, or to a pack's Python decoder (MD03b).
            const bool python = entry.contains( "python" );
            for ( auto item = entry.cbegin(); item != entry.cend(); ++item )
                if ( !isString( item.value() )
                     || ( ( python ? item.key() != "python"
                                   : item.key() != "dissector" && item.key() != "protocol" )
                          && !( protocol && item.key() == "inputEncoding" ) ) )
                    return reject( "invalid_route" );
            const auto selector = entry.value( "protocol", "" ).toString();
            if ( python ) {
                if ( !cilogg::plugins::pythonDecoderTarget( entry.value( "python" ).toString() ) )
                    return reject( "invalid_route" );
            }
            else if ( !named( entry.value( "dissector" ).toString() ) || selector.size() > 128
                      || selector.contains( QChar::Null ) )
                return reject( "invalid_route" );
            else
                entry[ "protocol" ] = entry.value( "protocol", "" );
            if ( protocol ) {
                entry[ "inputEncoding" ]
                    = entry.value( "inputEncoding", config.value( "inputEncoding" ) );
                if ( !encoding( entry.value( "inputEncoding" ).toString() ) )
                    return reject( "invalid_encoding" );
            }
            route.value() = entry;
        }
        config[ key ] = routes;
    }
    if ( QJsonDocument( QJsonObject::fromVariantMap( config ) )
             .toJson( QJsonDocument::Compact )
             .size()
         > 64 * 1024 )
        return reject( "configuration_limit" );
    *output = config;
    if ( error )
        error->clear();
    return true;
}
} // namespace

PreviewDecoderSettings& PreviewDecoderSettings::instance()
{
    static PreviewDecoderSettings settings;
    return settings;
}
bool PreviewDecoderSettings::isDissectorName( const QString& value )
{
    static const QRegularExpression pattern( "\\A[A-Za-z][A-Za-z0-9_.-]{0,127}\\z" );
    return pattern.match( value ).hasMatch();
}
PreviewDecoderSettings::PreviewDecoderSettings()
    : watcher_( this )
    , debounce_( this )
{
    watcher_.setObjectName( "previewDecoderFileWatcher" );
    debounce_.setObjectName( "previewDecoderWatchDebounce" );
    normalize( {}, &configuration_, nullptr );
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    const auto bytes = settings.value( "previewDecoder/configuration" ).toByteArray();
    if ( !bytes.isEmpty() && bytes.size() <= 65536 ) {
        const auto document = QJsonDocument::fromJson( bytes );
        QVariantMap restored;
        if ( document.isObject()
             && normalize( document.object().toVariantMap(), &restored, nullptr ) )
            configuration_ = restored;
        else
            LOG_WARNING
                << "Preview decoder persisted configuration invalid; disabled defaults used";
    }
    revision_ = QUuid::createUuid().toString( QUuid::WithoutBraces );
    debounce_.setSingleShot( true );
    debounce_.setInterval( 100 );
    const auto changedFile = [ this ]( const QString& ) {
        // Invalidate synchronously: already queued deliveries must observe the new revision.
        fileMetadata_ = fileMetadata( configuration_ );
        invalidate();
        debounce_.start();
    };
    connect( &watcher_, &QFileSystemWatcher::fileChanged, this, changedFile );
    connect( &watcher_, &QFileSystemWatcher::directoryChanged, this, [ this ]( const QString& ) {
        const auto current = fileMetadata( configuration_ );
        bool droppedWatch = false;
        const auto watched = watcher_.files();
        for ( const auto& file : watchedFiles_ )
            droppedWatch = droppedWatch || !watched.contains( file );
        // Unrelated sibling writes (including settings/logs) must not invalidate or log
        // recursively.
        if ( current == fileMetadata_ && ( debounce_.isActive() || !droppedWatch ) )
            return;
        fileMetadata_ = current;
        invalidate();
        debounce_.start();
    } );
    connect( &debounce_, &QTimer::timeout, this, [ this ] {
        const auto metadataBeforeRearm = fileMetadata_;
        watchFiles();
        const bool metadataChanged = fileMetadata_ != metadataBeforeRearm;
        LOG_DEBUG << "Preview decoder watch rearmed metadata_changed=" << metadataChanged
                  << " configured_files=" << fileMetadata_.size();
        // A delayed directory notification must not lose a change observed while rearming.
        // Emit last: changed handlers may synchronously replace the configuration.
        if ( metadataChanged )
            invalidate();
    } );
    connect( this, &PreviewDecoderSettings::changed, &PreviewManager::instance(),
             &PreviewManager::previewsChanged );
    watchFiles();
}
QVariantMap PreviewDecoderSettings::configuration() const
{
    return configuration_;
}
QString PreviewDecoderSettings::revision() const
{
    return revision_;
}
PreviewDecoderSnapshot PreviewDecoderSettings::snapshot() const
{
    auto effective = configuration_;
    // Pack routes sit below the user's: a user route with the same key wins, and the merged
    // lists keep the decoder's limits (the pack check refuses packs that would exceed them).
    for ( const auto& [ key, limit ] :
          { std::pair<const char*, int>{ "previewRoutes", PreviewRouteLimit },
            std::pair<const char*, int>{ "protocolRoutes", ProtocolRouteLimit } } ) {
        auto merged = configuration_.value( key ).toMap();
        const auto& packRoutes = QLatin1String( key ) == QLatin1String( "previewRoutes" )
                                     ? packs_.previewRoutes
                                     : packs_.protocolRoutes;
        const bool previewKeys = QLatin1String( key ) == QLatin1String( "previewRoutes" );
        for ( auto route = packRoutes.cbegin(); route != packRoutes.cend(); ++route ) {
            // A preview of the user's with the pack preview's name replaces it, route included.
            if ( previewKeys && PreviewManager::instance().findByName( route.key() ) ) {
                continue;
            }
            if ( !merged.contains( route.key() ) && merged.size() < limit ) {
                merged.insert( route.key(), route.value() );
            }
            else if ( !merged.contains( route.key() ) ) {
                LOG_DEBUG << "Preview decoder pack route left out at the limit key="
                          << route.key().toStdString();
            }
        }
        effective[ key ] = merged;
    }
    effective[ "packLua" ] = packs_.lua;
    // MD03b: the packs' Python decoder modules, as `{id, root, decoders: [{id, module}]}`.
    effective[ "packPython" ] = packs_.python;
    return { effective, revision_ };
}
PreviewDecoderPacks PreviewDecoderSettings::packs() const
{
    return packs_;
}
QString PreviewDecoderSettings::pythonDecoderPack( const QString& decoder ) const
{
    const auto slash = decoder.indexOf( QLatin1Char( '/' ) );
    if ( slash <= 0 ) {
        return {};
    }
    const auto packId = decoder.left( slash );
    const auto decoderId = decoder.mid( slash + 1 );
    for ( const auto& value : packs_.python ) {
        const auto pack = value.toMap();
        if ( pack.value( "id" ).toString() != packId ) {
            continue;
        }
        for ( const auto& listed : pack.value( "decoders" ).toList() ) {
            if ( listed.toMap().value( "id" ).toString() == decoderId ) {
                const auto name = packs_.pythonNames.value( packId ).toString();
                return name.isEmpty() ? packId : name;
            }
        }
    }
    return {};
}
void PreviewDecoderSettings::setPacks( const PreviewDecoderPacks& packs )
{
    if ( packs == packs_ )
        return;
    const bool sessions = !packs.sameSessions( packs_ );
    packs_ = packs;
    if ( !sessions ) {
        LOG_DEBUG << "Preview decoder Python check changed state="
                  << packs_.pythonCheck.toStdString();
        Q_EMIT pythonChecked();
        return;
    }
    LOG_INFO << "Preview decoder pack contributions changed packs=" << packs_.lua.size()
             << " python_packs=" << packs_.python.size()
             << " preview_routes=" << packs_.previewRoutes.size()
             << " protocol_routes=" << packs_.protocolRoutes.size();
    invalidate();
}
bool PreviewDecoderSettings::setConfiguration( const QVariantMap& value, QString* error )
{
    QVariantMap normalized;
    if ( !normalize( value, &normalized, error ) )
        return false;
    if ( normalized == configuration_ )
        return true;
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.setValue( "previewDecoder/configuration",
                       QJsonDocument( QJsonObject::fromVariantMap( normalized ) )
                           .toJson( QJsonDocument::Compact ) );
    configuration_ = std::move( normalized );
    watchFiles();
    invalidate();
    return true;
}
bool PreviewDecoderSettings::check( const QVariantMap& value, QString* error ) const
{
    QVariantMap normalized;
    return normalize( value, &normalized, error );
}
void PreviewDecoderSettings::invalidate()
{
    revision_ = QUuid::createUuid().toString( QUuid::WithoutBraces );
    LOG_DEBUG << "Preview decoder settings invalidated revision=" << revision_;
    Q_EMIT changed();
}
void PreviewDecoderSettings::watchFiles()
{
    if ( !watcher_.files().isEmpty() )
        watcher_.removePaths( watcher_.files() );
    if ( !watcher_.directories().isEmpty() )
        watcher_.removePaths( watcher_.directories() );
    const auto paths = configuredFiles( configuration_ );
    QStringList watches;
    for ( const auto& path : paths ) {
        if ( path.isEmpty() )
            continue;
        const QFileInfo info( path );
        if ( info.exists() )
            watches << info.absoluteFilePath();
        watches << info.absolutePath();
    }
    watches.removeDuplicates();
    fileMetadata_ = fileMetadata( configuration_ );
    if ( !watches.isEmpty() ) {
        const auto failed = watcher_.addPaths( watches );
        LOG_DEBUG << "Preview decoder watches requested=" << watches.size()
                  << " failed=" << failed.size();
    }
    watchedFiles_ = watcher_.files();
}
