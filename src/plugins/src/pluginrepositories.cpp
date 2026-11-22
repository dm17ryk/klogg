#include "pluginrepositories.h"

#include "log.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <cmath>

namespace cilogg::plugins {
namespace {
constexpr int MaxIdLength = 32;
const QString OrderKey = QStringLiteral( "repositories/order" );
const QString RetiredKey = QStringLiteral( "repositories/retired" );
const QString SchemaKey = QStringLiteral( "repositories/schema" );
const QString ItemsGroup = QStringLiteral( "repositories/items/" );

QString sourceKey( const QString& pluginId, const char* field )
{
    return QStringLiteral( "plugins/" ) + pluginId + '/' + QLatin1String( field );
}

QString unknownOption( const QVariantMap& options, const QStringList& allowed )
{
    for ( auto it = options.cbegin(); it != options.cend(); ++it ) {
        if ( !allowed.contains( it.key() ) ) {
            return QStringLiteral( "Unknown repository option: %1." ).arg( it.key().left( 64 ) );
        }
    }
    return {};
}

QString boolOption( const QVariantMap& options, const QString& name, bool* value )
{
    if ( !options.contains( name ) ) {
        return {};
    }
    const auto option = options.value( name );
    if ( option.typeId() != QMetaType::Bool ) {
        return QStringLiteral( "%1 must be true or false." ).arg( name );
    }
    *value = option.toBool();
    return {};
}

QString nameError( const QString& name )
{
    if ( name.isEmpty() ) {
        return QStringLiteral( "A repository needs a name." );
    }
    if ( name.size() > PluginRepositories::MaxNameLength ) {
        return QStringLiteral( "A repository name has at most %1 characters." )
            .arg( PluginRepositories::MaxNameLength );
    }
    // Control and format characters (bidirectional overrides, zero-width marks) could make a
    // name look like another one.
    for ( const auto character : name ) {
        if ( character.category() == QChar::Other_Control
             || character.category() == QChar::Other_Format ) {
            return QStringLiteral(
                "A repository name cannot contain control or formatting characters." );
        }
    }
    return {};
}

QString credentialSourceError( const QString& source )
{
    if ( source == CredentialSourceGitCredentialManager
         || source == CredentialSourcePersonalAccessToken ) {
        return {};
    }
    return QStringLiteral(
        "credential_source must be git-credential-manager or personal-access-token." );
}
} // namespace

std::optional<AzureDevOpsLocation> RepositoryRecord::azureDevOps() const
{
    if ( !isAzureDevOps() ) {
        return std::nullopt;
    }
    return parseAzureDevOpsAddress( url, branch, path ).location;
}

const QString PluginRepositories::DefaultCatalogUrl
    = QStringLiteral( "https://raw.githubusercontent.com/dm17ryk/klogg/master/plugins.json" );
const QString PluginRepositories::DefaultName = QStringLiteral( "CILogg examples (GitHub)" );

QVariantMap RepositoryRecord::toVariant( int position ) const
{
    // `signedIn` applies to repositories that sign in (a public one has none, null): true after
    // the last check or sign-in test was accepted.
    const auto accepted
        = lastState == QLatin1String( "loaded" ) || lastState == QLatin1String( "empty" );
    return { { "position", position },
             { "id", id },
             { "name", name },
             { "url", url },
             { "branch", isAzureDevOps() ? QVariant( branch ) : QVariant{} },
             { "path", isAzureDevOps() ? QVariant( path ) : QVariant{} },
             { "signIn", signIn },
             { "credentialSource", credentialSource },
             { "signedIn", isAzureDevOps() ? QVariant( accepted ) : QVariant{} },
             { "enabled", enabled },
             { "autoUpdate", autoUpdate },
             { "lastCheck", lastCheck },
             { "lastError", lastError },
             { "state", !enabled              ? QStringLiteral( "disabled" )
                        : lastState.isEmpty() ? QStringLiteral( "not checked" )
                                              : lastState },
             { "empty", lastState == QLatin1String( "empty" ) },
             { "pluginCount", pluginCount < 0 ? QVariant{} : QVariant( pluginCount ) },
             { "warnings", warningCount },
             { "hidden", hiddenCount } };
}

PluginRepositories::PluginRepositories( QSettings& settings )
    : settings_( settings )
{
}

// Records live in their own group, so no repository ID can collide with the list keys.
QString PluginRepositories::key( const QString& id, const char* field )
{
    return ItemsGroup + id + '/' + QLatin1String( field );
}

QStringList PluginRepositories::order() const
{
    return settings_.value( OrderKey ).toStringList();
}

QStringList PluginRepositories::retired() const
{
    return settings_.value( RetiredKey ).toStringList();
}

void PluginRepositories::setOrder( const QStringList& ids )
{
    settings_.setValue( OrderKey, ids );
}

void PluginRepositories::migrate()
{
    if ( settings_.contains( SchemaKey ) ) {
        return;
    }
    // An address the user had changed is kept, unless it is not a usable HTTPS address: the
    // address of a repository can never be edited afterwards.
    auto url = settings_.value( "catalogUrl", DefaultCatalogUrl ).toString().trimmed();
    if ( !addressError( url ).isEmpty() ) {
        LOG_WARNING << "Plugin repositories: the saved catalog address is not usable; the "
                       "default repository uses the shipped address";
        url = DefaultCatalogUrl;
    }
    const QString id = QStringLiteral( "default" );
    settings_.setValue( key( id, "name" ), DefaultName );
    settings_.setValue( key( id, "url" ), url );
    settings_.setValue( key( id, "signIn" ), QStringLiteral( "none" ) );
    settings_.setValue( key( id, "enabled" ), true );
    settings_.setValue( key( id, "autoUpdate" ), false );
    setOrder( { id } );
    settings_.setValue( RetiredKey, QStringList{} );
    settings_.setValue( SchemaKey, 1 );
    LOG_INFO << "Plugin repositories migrated the catalog address to the default repository";
}

std::optional<RepositoryRecord> PluginRepositories::find( const QString& id ) const
{
    if ( id.isEmpty() || !order().contains( id ) ) {
        return std::nullopt;
    }
    RepositoryRecord record;
    record.id = id;
    record.name = settings_.value( key( id, "name" ), id ).toString();
    record.url = settings_.value( key( id, "url" ) ).toString();
    record.signIn = settings_.value( key( id, "signIn" ), SignInNone ).toString();
    record.branch = settings_.value( key( id, "branch" ) ).toString();
    record.path = settings_.value( key( id, "path" ) ).toString();
    record.credentialSource = settings_.value( key( id, "credentialSource" ) ).toString();
    record.enabled = settings_.value( key( id, "enabled" ), true ).toBool();
    record.autoUpdate = settings_.value( key( id, "autoUpdate" ), false ).toBool();
    record.lastCheck = settings_.value( key( id, "lastCheck" ) ).toString();
    record.lastError = settings_.value( key( id, "lastError" ) ).toString();
    record.lastState = settings_.value( key( id, "lastState" ) ).toString();
    record.pluginCount = settings_.value( key( id, "pluginCount" ), -1 ).toInt();
    record.warningCount = settings_.value( key( id, "warnings" ), 0 ).toInt();
    record.hiddenCount = settings_.value( key( id, "hidden" ), 0 ).toInt();
    return record;
}

QList<RepositoryRecord> PluginRepositories::list() const
{
    QList<RepositoryRecord> result;
    for ( const auto& id : order() ) {
        if ( const auto record = find( id ) ) {
            result.append( *record );
        }
    }
    return result;
}

int PluginRepositories::position( const QString& id ) const
{
    return int( order().indexOf( id ) ) + 1;
}

QString PluginRepositories::normalizedAddress( const QString& url )
{
    QUrl address( url.trimmed(), QUrl::StrictMode );
    if ( address.port() == 443 && address.scheme() == "https" ) {
        address.setPort( -1 );
    }
    address = address.adjusted( QUrl::NormalizePathSegments | QUrl::RemoveFragment );
    if ( address.path().isEmpty() ) {
        address.setPath( QStringLiteral( "/" ) );
    }
    return QString::fromLatin1( address.toEncoded() );
}

QString PluginRepositories::addressError( const QString& url )
{
    const auto text = url.trimmed();
    if ( text.isEmpty() ) {
        return QStringLiteral( "A repository needs the HTTPS address of its plugins.json." );
    }
    if ( text.size() > MaxAddressLength ) {
        return QStringLiteral( "A repository address has at most %1 characters." )
            .arg( MaxAddressLength );
    }
    const QUrl address( text, QUrl::StrictMode );
    if ( !address.isValid() || address.scheme() != "https" || address.host().isEmpty() ) {
        return QStringLiteral( "A repository address must be an https:// address." );
    }
    if ( !address.userInfo().isEmpty() ) {
        return QStringLiteral( "A repository address cannot contain a user name or password." );
    }
    return {};
}

QString PluginRepositories::identity( const RepositoryRecord& record )
{
    if ( const auto location = record.azureDevOps() ) {
        // Azure DevOps compares organization, project, repository and ref names without regard
        // to case.
        return QStringLiteral( "azure-devops|%1|%2|%3|%4|%5" )
            .arg( location->organization.toLower(), location->project.toLower(),
                  location->repository.toLower(), location->branch.toLower(),
                  location->path.toLower() );
    }
    return QStringLiteral( "none|" ) + normalizedAddress( record.url );
}

QString PluginRepositories::idForAddress( const QString& url ) const
{
    const auto wanted = normalizedAddress( url );
    for ( const auto& record : list() ) {
        if ( normalizedAddress( record.url ) == wanted ) {
            return record.id;
        }
    }
    return {};
}

QString PluginRepositories::newId( const QString& name ) const
{
    static const QRegularExpression separators( "[^a-z0-9]+" );
    auto base = name.toLower();
    base.replace( separators, "-" );
    while ( base.startsWith( '-' ) ) {
        base.remove( 0, 1 );
    }
    base = base.left( MaxIdLength );
    while ( base.endsWith( '-' ) ) {
        base.chop( 1 );
    }
    if ( base.isEmpty() ) {
        base = QStringLiteral( "repository" );
    }
    // IDs of removed repositories are never given again, so a new repository cannot inherit
    // their install sources or stored credentials.
    const auto taken = order() + retired();
    auto candidate = base;
    for ( int suffix = 2; taken.contains( candidate ); ++suffix ) {
        candidate = base + '-' + QString::number( suffix );
    }
    return candidate;
}

QString PluginRepositories::add( const QVariantMap& options, RepositoryRecord* added )
{
    auto error = unknownOption( options, { "name", "url", "sign_in", "branch", "path",
                                           "credential_source", "enabled", "auto_update" } );
    RepositoryRecord record;
    record.signIn = options.value( "sign_in", SignInNone ).toString();
    if ( error.isEmpty() && record.signIn != SignInNone && !record.isAzureDevOps() ) {
        error = QStringLiteral( "sign_in must be none or azure-devops." );
    }
    if ( error.isEmpty() && !record.isAzureDevOps()
         && ( options.contains( "branch" ) || options.contains( "path" )
              || options.contains( "credential_source" ) ) ) {
        error = QStringLiteral(
            "branch, path and credential_source apply only to Azure DevOps repositories." );
    }
    const auto name = options.value( "name" ).toString().trimmed();
    const auto url = options.value( "url" ).toString().trimmed();
    if ( error.isEmpty() ) {
        error = nameError( name );
    }
    if ( error.isEmpty() && url.size() > MaxAddressLength ) {
        error = QStringLiteral( "A repository address has at most %1 characters." )
                    .arg( MaxAddressLength );
    }
    if ( error.isEmpty() && record.isAzureDevOps() ) {
        // Paste the repository address, a file link or an Items API address (design 3.5).
        const auto parsed = parseAzureDevOpsAddress( url, options.value( "branch" ).toString(),
                                                     options.value( "path" ).toString() );
        if ( !parsed.location ) {
            error = parsed.error;
        }
        else {
            record.url = parsed.location->repositoryAddress();
            record.branch = parsed.location->branch;
            record.path = parsed.location->path;
            record.credentialSource
                = options
                      .value( "credential_source", CredentialSourceGitCredentialManager )
                      .toString();
            error = credentialSourceError( record.credentialSource );
        }
    }
    else if ( error.isEmpty() ) {
        error = addressError( url );
        record.url = normalizedAddress( url );
    }
    if ( error.isEmpty() ) {
        error = boolOption( options, "enabled", &record.enabled );
    }
    if ( error.isEmpty() ) {
        error = boolOption( options, "auto_update", &record.autoUpdate );
    }
    if ( error.isEmpty() && order().size() >= MaxRepositories ) {
        error = QStringLiteral( "At most %1 repositories can be added." ).arg( MaxRepositories );
    }
    if ( error.isEmpty() ) {
        const auto wanted = identity( record );
        for ( const auto& existing : list() ) {
            if ( identity( existing ) == wanted ) {
                error = QStringLiteral( "The repository \"%1\" already has this address." )
                            .arg( existing.name );
                break;
            }
        }
    }
    if ( !error.isEmpty() ) {
        return error;
    }
    record.id = newId( name );
    record.name = name;
    settings_.setValue( key( record.id, "name" ), record.name );
    settings_.setValue( key( record.id, "url" ), record.url );
    settings_.setValue( key( record.id, "signIn" ), record.signIn );
    if ( record.isAzureDevOps() ) {
        settings_.setValue( key( record.id, "branch" ), record.branch );
        settings_.setValue( key( record.id, "path" ), record.path );
        settings_.setValue( key( record.id, "credentialSource" ), record.credentialSource );
    }
    settings_.setValue( key( record.id, "enabled" ), record.enabled );
    settings_.setValue( key( record.id, "autoUpdate" ), record.autoUpdate );
    setOrder( order() << record.id );
    LOG_INFO << "Plugin repository added id=" << record.id.toStdString();
    if ( added ) {
        *added = record;
    }
    return {};
}

QString PluginRepositories::update( const QString& id, const QVariantMap& options,
                                    RepositoryRecord* updated )
{
    auto record = find( id );
    if ( !record ) {
        return QStringLiteral( "Repository not found." );
    }
    for ( const auto* fixed : { "url", "branch", "path", "sign_in" } ) {
        if ( options.contains( QLatin1String( fixed ) ) ) {
            return QStringLiteral( "The address of a repository cannot be changed; add a new "
                                   "repository with the other address and remove this one." );
        }
    }
    const auto previousSource = record->credentialSource;
    auto error
        = unknownOption( options, { "name", "enabled", "auto_update", "credential_source" } );
    if ( error.isEmpty() && options.contains( "credential_source" ) ) {
        if ( !record->isAzureDevOps() ) {
            error
                = QStringLiteral( "credential_source applies only to Azure DevOps repositories." );
        }
        else {
            record->credentialSource = options.value( "credential_source" ).toString();
            error = credentialSourceError( record->credentialSource );
        }
    }
    if ( error.isEmpty() && options.contains( "name" ) ) {
        record->name = options.value( "name" ).toString().trimmed();
        error = nameError( record->name );
    }
    if ( error.isEmpty() ) {
        error = boolOption( options, "enabled", &record->enabled );
    }
    if ( error.isEmpty() ) {
        error = boolOption( options, "auto_update", &record->autoUpdate );
    }
    if ( !error.isEmpty() ) {
        return error;
    }
    settings_.setValue( key( id, "name" ), record->name );
    settings_.setValue( key( id, "enabled" ), record->enabled );
    settings_.setValue( key( id, "autoUpdate" ), record->autoUpdate );
    if ( record->isAzureDevOps() ) {
        settings_.setValue( key( id, "credentialSource" ), record->credentialSource );
        // The last check used the other source and says nothing about this one: the repository
        // is not signed in until it is checked with the new source.
        if ( record->credentialSource != previousSource && !record->lastState.isEmpty() ) {
            record->lastState = SignInRequiredState;
            record->lastError = QStringLiteral( "The credential source changed. Sign in again." );
            settings_.setValue( key( id, "lastState" ), record->lastState );
            settings_.setValue( key( id, "lastError" ), record->lastError );
        }
    }
    if ( updated ) {
        *updated = *record;
    }
    return {};
}

QString PluginRepositories::remove( const QString& id )
{
    auto ids = order();
    if ( id.isEmpty() || !ids.contains( id ) ) {
        return QStringLiteral( "Repository not found." );
    }
    ids.removeAll( id );
    setOrder( ids );
    auto old = retired();
    old.append( id );
    old.removeDuplicates();
    settings_.setValue( RetiredKey, old );
    settings_.remove( ItemsGroup + id );
    // PluginManager deletes the stored personal access token of `id`.
    LOG_INFO << "Plugin repository removed id=" << id.toStdString();
    return {};
}

QString PluginRepositories::move( const QString& id, const QVariantMap& options )
{
    auto ids = order();
    const auto from = ids.indexOf( id );
    if ( id.isEmpty() || from < 0 ) {
        return QStringLiteral( "Repository not found." );
    }
    auto error = unknownOption( options, { "position", "direction" } );
    if ( !error.isEmpty() ) {
        return error;
    }
    if ( options.contains( "position" ) == options.contains( "direction" ) ) {
        return QStringLiteral( "Give either position (1-based) or direction (up or down)." );
    }
    qsizetype to = from;
    if ( options.contains( "position" ) ) {
        bool valid = false;
        const auto wanted = options.value( "position" ).toDouble( &valid );
        if ( !valid || wanted != std::floor( wanted ) || wanted < 1
             || wanted > double( ids.size() ) ) {
            return QStringLiteral( "position must be a whole number from 1 to %1." )
                .arg( ids.size() );
        }
        to = qsizetype( wanted ) - 1;
    }
    else {
        const auto direction = options.value( "direction" ).toString();
        if ( direction != QLatin1String( "up" ) && direction != QLatin1String( "down" ) ) {
            return QStringLiteral( "direction must be up or down." );
        }
        to = qBound( qsizetype( 0 ), from + ( direction == QLatin1String( "up" ) ? -1 : 1 ),
                     ids.size() - 1 );
    }
    ids.move( from, to );
    setOrder( ids );
    return {};
}

void PluginRepositories::recordCheck( const QString& id, const QString& state, int count,
                                      int warnings, int hidden, const QString& error,
                                      const QDateTime& when )
{
    if ( !order().contains( id ) ) {
        return; // removed while it was being fetched
    }
    settings_.setValue( key( id, "lastState" ), state );
    settings_.setValue( key( id, "lastError" ), error );
    if ( state == QLatin1String( "loaded" ) || state == QLatin1String( "empty" ) ) {
        settings_.setValue( key( id, "lastCheck" ), when.toUTC().toString( Qt::ISODate ) );
        settings_.setValue( key( id, "pluginCount" ), count );
        settings_.setValue( key( id, "warnings" ), warnings );
        settings_.setValue( key( id, "hidden" ), hidden );
    }
}

void PluginRepositories::recordSignOut( const QString& id )
{
    if ( !order().contains( id ) ) {
        return;
    }
    settings_.setValue( key( id, "lastState" ), SignInRequiredState );
    settings_.setValue( key( id, "lastError" ), QStringLiteral( "Signed out." ) );
}

void PluginRepositories::setCredentialSource( const QString& id, const QString& credentialSource )
{
    if ( order().contains( id ) && credentialSourceError( credentialSource ).isEmpty() ) {
        settings_.setValue( key( id, "credentialSource" ), credentialSource );
    }
}

InstallSource PluginRepositories::source( const QString& pluginId ) const
{
    return { settings_.value( sourceKey( pluginId, "sourceRepository" ) ).toString(),
             settings_.value( sourceKey( pluginId, "sourceVersion" ) ).toString(),
             settings_.value( sourceKey( pluginId, "sourceUrl" ) ).toString() };
}

void PluginRepositories::setSource( const QString& pluginId, const InstallSource& source )
{
    if ( source.repository.isEmpty() ) {
        clearSource( pluginId );
        return;
    }
    settings_.setValue( sourceKey( pluginId, "sourceRepository" ), source.repository );
    settings_.setValue( sourceKey( pluginId, "sourceVersion" ), source.version );
    settings_.setValue( sourceKey( pluginId, "sourceUrl" ), source.url );
    settings_.remove( sourceKey( pluginId, "sourcePath" ) );
    settings_.remove( sourceKey( pluginId, "sourceKind" ) );
}

void PluginRepositories::clearSource( const QString& pluginId )
{
    settings_.remove( sourceKey( pluginId, "sourceRepository" ) );
    settings_.remove( sourceKey( pluginId, "sourceVersion" ) );
    settings_.remove( sourceKey( pluginId, "sourceUrl" ) );
    settings_.remove( sourceKey( pluginId, "sourcePath" ) );
    settings_.remove( sourceKey( pluginId, "sourceKind" ) );
}

QString PluginRepositories::localSource( const QString& pluginId ) const
{
    return settings_.value( sourceKey( pluginId, "sourcePath" ) ).toString();
}

QString PluginRepositories::localSourceKind( const QString& pluginId ) const
{
    return settings_.value( sourceKey( pluginId, "sourceKind" ) ).toString();
}

void PluginRepositories::setLocalSource( const QString& pluginId, const QString& path )
{
    settings_.setValue( sourceKey( pluginId, "sourcePath" ), path );
    // Decided here, once, by what the path is (a package file need not end in .zip): the
    // plugin list reads it often and must not touch the disk.
    settings_.setValue( sourceKey( pluginId, "sourceKind" ), QFileInfo( path ).isDir()
                                                                 ? QStringLiteral( "folder" )
                                                                 : QStringLiteral( "zip" ) );
}
} // namespace cilogg::plugins
