#include "configurationexport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>

#include "actionsmanager.h"
#include "actionsrepository.h"
#include "configuration.h"
#include "log.h"
#include "persistentinfo.h"
#include "previewmanager.h"
#include "previewrepository.h"

namespace {
constexpr qint64 MaxConfigurationBytes = 32 * 1024 * 1024;
constexpr int MaxConfigurationItems = 4096;
bool validSettingsArray( QSettings& settings, const QString& key, QString* error );

bool fail( const QString& message, QString* error )
{
    LOG_ERROR << "Configuration export/project: " << message;
    if ( error ) {
        *error = message;
    }
    return false;
}

bool readFile( const QString& path, QByteArray* bytes, QString* error )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return fail( QObject::tr( "Cannot read %1: %2" ).arg( path, file.errorString() ), error );
    }
    if ( file.size() > MaxConfigurationBytes ) {
        return fail( QObject::tr( "Configuration file is too large: %1" ).arg( path ), error );
    }
    *bytes = file.readAll();
    if ( file.error() != QFileDevice::NoError ) {
        return fail( QObject::tr( "Cannot read %1: %2" ).arg( path, file.errorString() ), error );
    }
    LOG_DEBUG << "Read configuration: " << path << " bytes=" << bytes->size();
    return true;
}

template <typename Writer>
bool settingsBytes( Writer writer, QByteArray* bytes, QString* error )
{
    QTemporaryDir directory;
    if ( !directory.isValid() ) {
        return fail( QObject::tr( "Cannot create temporary configuration directory." ), error );
    }
    const auto path = directory.filePath( "export.conf" );
    {
        QSettings settings( path, QSettings::IniFormat );
        writer( settings );
        for ( const auto& key : settings.allKeys() ) {
            if ( key.endsWith( "/size" ) && !validSettingsArray( settings, key, error ) ) {
                return false;
            }
        }
        if ( settings.value( "Window/windows/size", 0 ).toInt() > 64 ) {
            return fail( QObject::tr( "A project can contain at most 64 windows." ), error );
        }
        settings.sync();
        if ( settings.status() != QSettings::NoError ) {
            return fail( QObject::tr( "Cannot serialize configuration settings." ), error );
        }
    }
    return readFile( path, bytes, error );
}

bool isDefinitionKey( const QString& key )
{
    return key.startsWith( "HighlighterSetCollection/" )
           || key.startsWith( "PredefinedFiltersCollection/" );
}

QVector<int> allRows( int count )
{
    QVector<int> result;
    for ( int i = 0; i < count; ++i ) {
        result.append( i );
    }
    return result;
}

bool validRows( const QVector<int>& rows, int count, QString* error )
{
    QSet<int> seen;
    for ( const auto row : rows ) {
        if ( row < 0 || row >= count || seen.contains( row ) ) {
            return fail( QObject::tr( "The export selection is no longer valid." ), error );
        }
        seen.insert( row );
    }
    return true;
}

bool configurationBytes( ConfigurationExportKind kind, const QVector<int>& rows,
                         const HighlighterSetCollection* highlights, QByteArray* bytes,
                         QString* error, const ConfigurationProject* snapshot )
{
    LOG_DEBUG << "Serializing export: kind=" << static_cast<int>( kind )
              << " selected=" << rows.size();
    switch ( kind ) {
    case ConfigurationExportKind::Actions: {
        const auto& sourceActions
            = snapshot ? snapshot->actions.actions : ActionsManager::instance().actions();
        const auto& sourceResponses
            = snapshot ? snapshot->actions.responses : ActionsManager::instance().responses();
        if ( !validRows( rows, sourceActions.size(), error ) ) {
            return false;
        }
        QVector<ActionDefinition> actions;
        QSet<int> ids;
        for ( const auto row : rows ) {
            actions.append( sourceActions.at( row ) );
            ids.insert( actions.back().id );
        }
        QVector<ResponseDefinition> responses;
        for ( const auto& response : sourceResponses ) {
            bool complete
                = !response.response.hasActionId || ids.contains( response.response.actionId );
            for ( const auto& step : response.response.steps ) {
                complete = complete && ids.contains( step.actionId );
            }
            if ( complete ) {
                responses.append( response );
            }
            else {
                LOG_DEBUG << "Skipping response with unselected action dependencies: id="
                          << response.id;
            }
        }
        *bytes = ActionsRepository::serialize( actions, responses );
        return true;
    }
    case ConfigurationExportKind::Previews: {
        const auto& sourcePreviews
            = snapshot ? snapshot->previews.previews : PreviewManager::instance().all();
        const auto& sourceBlocks
            = snapshot ? snapshot->previews.blocks : PreviewManager::instance().blocks();
        if ( !validRows( rows, sourcePreviews.size(), error ) ) {
            return false;
        }
        QVector<PreviewDefinition> previews;
        for ( const auto row : rows ) {
            previews.append( sourcePreviews.at( row ) );
        }
        // Blocks can reference other blocks recursively; keep the complete reusable block library.
        *bytes = PreviewRepository::serialize( previews, sourceBlocks );
        return true;
    }
    case ConfigurationExportKind::Highlights: {
        const auto source = highlights ? *highlights
                                       : ( snapshot ? snapshot->highlights
                                                    : HighlighterSetCollection::getSynced() );
        if ( !validRows( rows, source.highlighterSets().size(), error ) ) {
            return false;
        }
        HighlighterSetCollection selected;
        QList<HighlighterSet> sets;
        for ( const auto row : rows ) {
            sets.append( source.highlighterSets().at( row ) );
        }
        selected.setHighlighterSets( sets );
        selected.setQuickHighlighters( source.quickHighlighters() );
        for ( const auto& id : source.activeSetIds() ) {
            if ( selected.hasSet( id ) ) {
                selected.activateSet( id );
            }
        }
        return settingsBytes(
            [ &selected ]( QSettings& settings ) { selected.saveToStorage( settings ); }, bytes,
            error );
    }
    case ConfigurationExportKind::PredefinedFilters: {
        const auto filters = snapshot ? snapshot->filters.getFilters()
                                      : PredefinedFiltersCollection::getSynced().getFilters();
        if ( !validRows( rows, filters.size(), error ) ) {
            return false;
        }
        PredefinedFiltersCollection selected;
        PredefinedFiltersCollection::Collection selectedFilters;
        for ( const auto row : rows ) {
            selectedFilters.append( filters.at( row ) );
        }
        selected.setFilters( selectedFilters );
        return settingsBytes(
            [ &selected ]( QSettings& settings ) { selected.saveToStorage( settings ); }, bytes,
            error );
    }
    case ConfigurationExportKind::Preferences:
        return settingsBytes(
            [ snapshot ]( QSettings& settings ) {
                if ( snapshot ) {
                    for ( auto it = snapshot->preferences.cbegin();
                          it != snapshot->preferences.cend(); ++it ) {
                        settings.setValue( it.key(), it.value() );
                    }
                }
                else {
                    auto& source = PersistentInfo::getSettings( app_settings{} );
                    source.sync();
                    for ( const auto& key : source.allKeys() ) {
                        if ( !isDefinitionKey( key ) ) {
                            settings.setValue( key, source.value( key ) );
                        }
                    }
                    Configuration::get().saveToStorage( settings );
                }
                settings.setValue( "ConfigurationExport/version", 1 );
            },
            bytes, error );
    }
    return fail( QObject::tr( "Unknown configuration export type." ), error );
}

bool validSettingsArray( QSettings& settings, const QString& key, QString* error )
{
    bool ok = false;
    const int count = settings.value( key ).toInt( &ok );
    return ( ok && count >= 0 && count <= MaxConfigurationItems )
           || fail( QObject::tr( "Invalid configuration array: %1" ).arg( key ), error );
}
} // namespace

namespace ConfigurationExport {
QString title( ConfigurationExportKind kind )
{
    switch ( kind ) {
    case ConfigurationExportKind::Actions:
        return QObject::tr( "Export actions" );
    case ConfigurationExportKind::Highlights:
        return QObject::tr( "Export highlights" );
    case ConfigurationExportKind::Previews:
        return QObject::tr( "Export previews" );
    case ConfigurationExportKind::PredefinedFilters:
        return QObject::tr( "Export predefined filters" );
    case ConfigurationExportKind::Preferences:
        return QObject::tr( "Export preferences" );
    }
    return {};
}

bool captureConfiguration( ConfigurationProject* snapshot, QString* error )
{
    if ( !snapshot ) {
        return fail( QObject::tr( "Missing configuration snapshot destination." ), error );
    }
    snapshot->actions.actions = ActionsManager::instance().actions();
    snapshot->actions.responses = ActionsManager::instance().responses();
    snapshot->previews.previews = PreviewManager::instance().all();
    snapshot->previews.blocks = PreviewManager::instance().blocks();
    snapshot->highlights = HighlighterSetCollection::getSynced();
    snapshot->filters = PredefinedFiltersCollection::getSynced();
    snapshot->preferences.clear();
    auto& source = PersistentInfo::getSettings( app_settings{} );
    source.sync();
    if ( source.status() != QSettings::NoError ) {
        return fail( QObject::tr( "Cannot read application preferences." ), error );
    }
    for ( const auto& key : source.allKeys() ) {
        if ( !isDefinitionKey( key ) ) {
            snapshot->preferences.insert( key, source.value( key ) );
        }
    }
    QTemporaryDir directory;
    if ( !directory.isValid() ) {
        return fail( QObject::tr( "Cannot capture application preferences." ), error );
    }
    QSettings current( directory.filePath( "current.conf" ), QSettings::IniFormat );
    Configuration::get().saveToStorage( current );
    for ( const auto& key : current.allKeys() ) {
        snapshot->preferences.insert( key, current.value( key ) );
    }
    LOG_DEBUG << "Captured immutable configuration snapshot: actions="
              << snapshot->actions.actions.size()
              << " previews=" << snapshot->previews.previews.size();
    return true;
}

QStringList names( ConfigurationExportKind kind, const ConfigurationProject* snapshot )
{
    QStringList result;
    switch ( kind ) {
    case ConfigurationExportKind::Actions:
        for ( const auto& action :
              ( snapshot ? snapshot->actions.actions : ActionsManager::instance().actions() ) ) {
            result.append( action.name );
        }
        break;
    case ConfigurationExportKind::Previews:
        for ( const auto& preview :
              ( snapshot ? snapshot->previews.previews : PreviewManager::instance().all() ) ) {
            result.append( preview.name );
        }
        break;
    case ConfigurationExportKind::Highlights:
        for ( const auto& set :
              ( snapshot ? snapshot->highlights.highlighterSets()
                         : HighlighterSetCollection::getSynced().highlighterSets() ) ) {
            result.append( set.name() );
        }
        break;
    case ConfigurationExportKind::PredefinedFilters:
        for ( const auto& filter :
              ( snapshot ? snapshot->filters.getFilters()
                         : PredefinedFiltersCollection::getSynced().getFilters() ) ) {
            result.append( filter.name );
        }
        break;
    case ConfigurationExportKind::Preferences:
        break;
    }
    return result;
}

QString suffix( ConfigurationExportKind kind )
{
    return kind == ConfigurationExportKind::Actions || kind == ConfigurationExportKind::Previews
               ? QStringLiteral( ".json" )
               : QStringLiteral( ".conf" );
}

QString fileFilter( ConfigurationExportKind kind )
{
    return suffix( kind ) == ".json" ? QObject::tr( "JSON configurations (*.json)" )
                                     : QObject::tr( "Configurations (*.conf)" );
}

bool writeFile( const QString& path, const QByteArray& bytes, QString* error )
{
    if ( bytes.size() > MaxConfigurationBytes ) {
        return fail( QObject::tr( "Configuration file is too large: %1" ).arg( path ), error );
    }
    LOG_DEBUG << "Writing configuration atomically: " << path << " bytes=" << bytes.size();
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) || file.write( bytes ) != bytes.size()
         || !file.commit() ) {
        return fail( QObject::tr( "Cannot save %1: %2" ).arg( path, file.errorString() ), error );
    }
    LOG_INFO << "Configuration saved: " << path;
    return true;
}

bool exportSelected( ConfigurationExportKind kind, const QVector<int>& rows, const QString& path,
                     QString* error, const HighlighterSetCollection* highlights,
                     const ConfigurationProject* snapshot )
{
    QByteArray bytes;
    return configurationBytes( kind, rows, highlights, &bytes, error, snapshot )
           && writeFile( path, bytes, error );
}

bool exportHighlightFiles( const HighlighterSetCollection& highlights, const QVector<int>& rows,
                           const QString& directory, QString* error, QStringList* savedFiles )
{
    if ( !validRows( rows, highlights.highlighterSets().size(), error ) ) {
        return false;
    }
    const QDir dir( directory );
    const QRegularExpression unsafe( QStringLiteral( "[^\\p{L}\\p{N}_-]" ) );
    int number = 0;
    for ( const auto row : rows ) {
        auto name = highlights.highlighterSets().at( row ).name();
        name.replace( unsafe, "_" );
        name = name.left( 80 );
        // Numeric prefix avoids Windows device names and collisions, including duplicate group
        // names.
        auto path = dir.filePath(
            QStringLiteral( "%1_%2.conf" ).arg( ++number, 3, 10, QLatin1Char( '0' ) ).arg( name ) );
        const auto base = QFileInfo( path ).completeBaseName();
        int collision = 1;
        while ( QFileInfo::exists( path ) ) {
            path = dir.filePath( base + '-' + QString::number( ++collision ) + ".conf" );
        }
        if ( !exportSelected( ConfigurationExportKind::Highlights, { row }, path, error,
                              &highlights ) ) {
            LOG_WARNING << "Multi-file highlight export stopped after " << number - 1 << " files";
            return false;
        }
        if ( savedFiles ) {
            savedFiles->append( path );
        }
    }
    return true;
}

bool saveProject( const QString& path, const SessionInfo& session,
                  const QMap<QString, int>& activeTabs, const QMap<QString, QString>& exports,
                  QString* error )
{
    LOG_DEBUG << "Saving project: " << path << " windows=" << session.windows().size();
    if ( session.windows().isEmpty() || session.windows().size() > 64 ) {
        return fail( QObject::tr( "A project must contain between 1 and 64 windows." ), error );
    }
    ConfigurationProject snapshot;
    if ( !captureConfiguration( &snapshot, error ) ) {
        return false;
    }
    const QFileInfo info( path );
    const QDir parent = info.absoluteDir();
    QTemporaryDir staging( parent.filePath( info.completeBaseName() + ".config-XXXXXX" ) );
    if ( !staging.isValid() ) {
        return fail( QObject::tr( "Cannot create project configuration folder." ), error );
    }
    const auto folder = QFileInfo( staging.path() ).fileName();
    const QDir dir( staging.path() );
    QJsonObject references;
    const std::pair<ConfigurationExportKind, const char*> kinds[]
        = { { ConfigurationExportKind::Actions, "actions" },
            { ConfigurationExportKind::Highlights, "highlights" },
            { ConfigurationExportKind::Previews, "previews" },
            { ConfigurationExportKind::PredefinedFilters, "filters" },
            { ConfigurationExportKind::Preferences, "preferences" } };
    for ( const auto& entry : kinds ) {
        const auto fileName = QString::fromLatin1( entry.second ) + suffix( entry.first );
        if ( !exportSelected( entry.first, allRows( names( entry.first, &snapshot ).size() ),
                              dir.filePath( fileName ), error, nullptr, &snapshot ) ) {
            return false;
        }
        references.insert( entry.second, folder + '/' + fileName );
    }
    QByteArray sessionBytes;
    if ( !settingsBytes( [ &session ]( QSettings& settings ) { session.saveToStorage( settings ); },
                         &sessionBytes, error )
         || !writeFile( dir.filePath( "session.conf" ), sessionBytes, error ) ) {
        return false;
    }
    references.insert( "session", folder + "/session.conf" );
    QJsonArray registered;
    int index = 0;
    for ( auto it = exports.cbegin(); it != exports.cend(); ++it ) {
        QByteArray bytes;
        if ( !readFile( it.key(), &bytes, error ) ) {
            return false;
        }
        const auto fileName = QStringLiteral( "export-%1%2" )
                                  .arg( ++index )
                                  .arg( QFileInfo( it.key() ).suffix().isEmpty()
                                            ? QString{}
                                            : '.' + QFileInfo( it.key() ).suffix() );
        if ( !writeFile( dir.filePath( fileName ), bytes, error ) ) {
            return false;
        }
        registered.append(
            QJsonObject{ { "type", it.value() }, { "file", folder + '/' + fileName } } );
    }
    QJsonObject currentTabs;
    for ( auto it = activeTabs.cbegin(); it != activeTabs.cend(); ++it ) {
        currentTabs.insert( it.key(), it.value() );
    }
    const QJsonObject root{ { "format", "CILoggProject" },
                            { "version", 1 },
                            { "configurations", references },
                            { "exports", registered },
                            { "activeTabs", currentTabs } };
    // Write the manifest last. A failed overwrite keeps the previous project's entire snapshot
    // intact.
    if ( !writeFile( path, QJsonDocument( root ).toJson( QJsonDocument::Indented ), error ) ) {
        return false;
    }
    staging.setAutoRemove( false );
    return true;
}

bool readProject( const QString& path, ConfigurationProject* project, QString* error )
{
    if ( !project ) {
        return fail( QObject::tr( "Missing project destination." ), error );
    }
    QByteArray bytes;
    if ( !readFile( path, &bytes, error ) ) {
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( bytes, &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
        return fail( QObject::tr( "Invalid project JSON: %1" ).arg( parseError.errorString() ),
                     error );
    }
    const auto root = document.object();
    if ( root.value( "format" ).toString() != "CILoggProject"
         || root.value( "version" ) != QJsonValue( 1 ) || !root.value( "configurations" ).isObject()
         || !root.value( "activeTabs" ).isObject() || !root.value( "exports" ).isArray() ) {
        return fail( QObject::tr( "Unsupported or incomplete CILogg project." ), error );
    }
    const auto refs = root.value( "configurations" ).toObject();
    const QDir parent = QFileInfo( path ).absoluteDir();
    QMap<QString, QString> files;
    for ( const auto& key :
          { "actions", "highlights", "previews", "filters", "preferences", "session" } ) {
        const auto relative = refs.value( key ).toString();
        if ( relative.isEmpty() || QDir::isAbsolutePath( relative )
             || QDir::cleanPath( relative ).startsWith( "../" ) ) {
            return fail( QObject::tr( "Invalid project reference: %1" ).arg( key ), error );
        }
        const auto file = parent.absoluteFilePath( relative );
        if ( !readFile( file, &bytes, error ) ) {
            return false;
        }
        files.insert( key, file );
    }
    ConfigurationProject candidate;
    candidate.actions = ActionsConfigParser{}.parseFile( files.value( "actions" ) );
    candidate.previews = PreviewConfigParser{}.parseFile( files.value( "previews" ) );
    if ( !candidate.actions.errors.isEmpty() || !candidate.previews.errors.isEmpty() ) {
        return fail( ( candidate.actions.errors + candidate.previews.errors ).join( '\n' ), error );
    }
    QSettings highlights( files.value( "highlights" ), QSettings::IniFormat );
    QSettings filters( files.value( "filters" ), QSettings::IniFormat );
    QSettings preferences( files.value( "preferences" ), QSettings::IniFormat );
    QSettings session( files.value( "session" ), QSettings::IniFormat );
    if ( highlights.value( "HighlighterSetCollection/version" ).toInt() != 2
         || filters.value( "PredefinedFiltersCollection/version" ).toInt() != 3
         || preferences.value( "ConfigurationExport/version" ).toInt() != 1
         || session.value( "Window/version" ).toInt() != 2 ) {
        return fail( QObject::tr( "Invalid project configuration version." ), error );
    }
    // Validate persisted array sizes before the existing readers allocate their collections.
    for ( auto* settings : { &highlights, &filters, &preferences, &session } ) {
        for ( const auto& key : settings->allKeys() ) {
            if ( key.endsWith( "/size" ) && !validSettingsArray( *settings, key, error ) ) {
                return false;
            }
        }
        if ( settings->status() != QSettings::NoError ) {
            return fail(
                QObject::tr( "Cannot read project settings: %1" ).arg( settings->fileName() ),
                error );
        }
    }
    if ( !validSettingsArray( highlights, "HighlighterSetCollection/sets/size", error )
         || !validSettingsArray( highlights, "HighlighterSetCollection/quick/size", error )
         || !validSettingsArray( filters, "PredefinedFiltersCollection/filters/size", error ) ) {
        return false;
    }
    const int setCount = highlights.value( "HighlighterSetCollection/sets/size" ).toInt();
    for ( int i = 1; i <= setCount; ++i ) {
        const auto prefix
            = QStringLiteral( "HighlighterSetCollection/sets/%1/HighlighterSet/" ).arg( i );
        if ( highlights.value( prefix + "version" ).toInt() != 3
             || !validSettingsArray( highlights, prefix + "highlighters/size", error ) ) {
            return fail( QObject::tr( "Invalid highlight group in project at index %1." ).arg( i ),
                         error );
        }
    }
    const int windowCount = session.value( "Window/windows/size", -1 ).toInt();
    if ( windowCount < 1 || windowCount > 64 ) {
        return fail( QObject::tr( "Invalid number of project windows." ), error );
    }
    QSet<QString> windowIds;
    for ( int i = 1; i <= windowCount; ++i ) {
        const auto prefix = QStringLiteral( "Window/windows/%1/" ).arg( i );
        const auto id = session.value( prefix + "id" ).toString();
        if ( id.trimmed().isEmpty() || windowIds.contains( id )
             || session.value( prefix + "OpenFiles/version" ).toInt() != 2
             || !validSettingsArray( session, prefix + "OpenFiles/openFiles/size", error ) ) {
            return fail( QObject::tr( "Invalid project window session at index %1." ).arg( i ),
                         error );
        }
        windowIds.insert( id );
    }
    candidate.highlights.retrieveFromStorage( highlights );
    candidate.filters.retrieveFromStorage( filters );
    candidate.session.retrieveFromStorage( session );
    if ( candidate.session.windows().isEmpty() ) {
        return fail( QObject::tr( "The project has no saved window session." ), error );
    }
    for ( const auto& key : preferences.allKeys() ) {
        if ( !key.startsWith( "ConfigurationExport/" ) && !isDefinitionKey( key ) ) {
            candidate.preferences.insert( key, preferences.value( key ) );
        }
    }
    const auto tabs = root.value( "activeTabs" ).toObject();
    for ( auto it = tabs.begin(); it != tabs.end(); ++it ) {
        if ( !it.value().isDouble() || it.value().toDouble() != it.value().toInt()
             || it.value().toInt() < -1 || it.value().toInt() >= MaxConfigurationItems ) {
            return fail( QObject::tr( "Invalid active tab in project." ), error );
        }
        candidate.activeTabs.insert( it.key(), it.value().toInt() );
    }
    for ( const auto& value : root.value( "exports" ).toArray() ) {
        const auto entry = value.toObject();
        const auto relative = entry.value( "file" ).toString();
        if ( relative.isEmpty() || QDir::isAbsolutePath( relative )
             || QDir::cleanPath( relative ).startsWith( "../" )
             || entry.value( "type" ).toString().isEmpty() ) {
            return fail( QObject::tr( "Invalid registered export in project." ), error );
        }
        const auto file = parent.absoluteFilePath( relative );
        if ( !readFile( file, &bytes, error ) ) {
            return false;
        }
        candidate.exports.insert( file, entry.value( "type" ).toString() );
    }
    *project = std::move( candidate );
    LOG_INFO << "Project validated: " << path << " windows=" << project->session.windows().size();
    return true;
}

bool applyProjectConfiguration( const ConfigurationProject& project, QString* error )
{
    LOG_DEBUG << "Applying project configuration snapshot";
    const auto oldActions = ActionsManager::instance().actions();
    const auto oldResponses = ActionsManager::instance().responses();
    const auto oldPreviews = PreviewManager::instance().all();
    const auto oldBlocks = PreviewManager::instance().blocks();
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    QMap<QString, QVariant> oldSettings;
    for ( const auto& key : settings.allKeys() ) {
        oldSettings.insert( key, settings.value( key ) );
    }
    if ( !ActionsRepository{}.save( project.actions.actions, project.actions.responses ) ) {
        return fail( QObject::tr( "Cannot save project actions." ), error );
    }
    if ( !PreviewRepository{}.save( project.previews.previews, project.previews.blocks ) ) {
        const bool rolledBack = ActionsRepository{}.save( oldActions, oldResponses );
        LOG_WARNING << "Project preview save failed; actions rollback=" << rolledBack;
        return fail( QObject::tr( "Cannot save project previews." ), error );
    }
    settings.clear();
    for ( auto it = project.preferences.cbegin(); it != project.preferences.cend(); ++it ) {
        settings.setValue( it.key(), it.value() );
    }
    project.highlights.saveToStorage( settings );
    project.filters.saveToStorage( settings );
    settings.sync();
    if ( settings.status() != QSettings::NoError ) {
        settings.clear();
        for ( auto it = oldSettings.cbegin(); it != oldSettings.cend(); ++it ) {
            settings.setValue( it.key(), it.value() );
        }
        settings.sync();
        const bool actionsRestored = ActionsRepository{}.save( oldActions, oldResponses );
        const bool previewsRestored = PreviewRepository{}.save( oldPreviews, oldBlocks );
        LOG_ERROR << "Project settings save failed; rollback actions=" << actionsRestored
                  << " previews=" << previewsRestored
                  << " settings=" << static_cast<int>( settings.status() );
        return fail( QObject::tr( "Cannot save project preferences." ), error );
    }
    Configuration::getSynced();
    HighlighterSetCollection::getSynced();
    PredefinedFiltersCollection::getSynced();
    ActionsManager::instance().loadFromRepository();
    PreviewManager::instance().loadFromRepository();
    LOG_INFO << "Project configuration snapshot applied";
    return true;
}
} // namespace ConfigurationExport
