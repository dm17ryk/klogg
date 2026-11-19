#include "azuredevops.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

namespace cilogg::plugins {
namespace {
const QString LegacyHostSuffix = QStringLiteral( ".visualstudio.com" );
constexpr int HttpsPort = 443;
constexpr int MaxNameLength = 256;
constexpr int MaxBranchLength = 250;
constexpr int MaxPathLength = 1024;

// What an address says, before defaults: an absent branch or path is not the same as the default.
struct AddressParts {
    QString organization;
    QString project;
    QString repository;
    std::optional<QString> branch;
    std::optional<QString> path;
    bool items = false;
};

bool hasControlCharacter( const QString& text )
{
    for ( const auto character : text ) {
        if ( character.category() == QChar::Other_Control ) {
            return true;
        }
    }
    return false;
}

bool hasEncodedSeparator( const QUrl& url )
{
    const auto encoded = url.path( QUrl::FullyEncoded );
    return encoded.contains( QLatin1String( "%2f" ), Qt::CaseInsensitive )
           || encoded.contains( QLatin1String( "%5c" ), Qt::CaseInsensitive )
           || encoded.contains( QLatin1Char( '\\' ) );
}

bool hasDotSegment( const QStringList& segments )
{
    for ( const auto& segment : segments ) {
        if ( segment == QLatin1String( "." ) || segment == QLatin1String( ".." ) ) {
            return true;
        }
    }
    return false;
}

QString nameError( const QString& name, const char* what )
{
    if ( name.isEmpty() || name.size() > MaxNameLength || hasControlCharacter( name ) ) {
        return QStringLiteral( "The address has no valid %1 name." ).arg( QLatin1String( what ) );
    }
    return {};
}

QString branchError( const QString& branch )
{
    if ( branch.isEmpty() || branch.size() > MaxBranchLength || hasControlCharacter( branch )
         || branch.startsWith( QLatin1Char( '-' ) ) || branch.contains( QLatin1String( ".." ) )
         || branch.contains( QLatin1Char( '\\' ) ) ) {
        return QStringLiteral( "The branch name is not valid." );
    }
    for ( const auto character : branch ) {
        if ( character.isSpace() ) {
            return QStringLiteral( "The branch name is not valid." );
        }
    }
    return {};
}

std::optional<QString> normalizedPath( QString path, QString* error )
{
    path = path.trimmed();
    if ( !path.startsWith( QLatin1Char( '/' ) ) ) {
        path.prepend( QLatin1Char( '/' ) );
    }
    if ( path.size() < 2 || path.size() > MaxPathLength || hasControlCharacter( path )
         || path.contains( QLatin1Char( '\\' ) )
         || hasDotSegment( path.split( QLatin1Char( '/' ) ) ) ) {
        *error = QStringLiteral( "The catalog path is not valid." );
        return std::nullopt;
    }
    return path;
}

QString queryValue( const QUrlQuery& query, const QString& key )
{
    return query.queryItemValue( key, QUrl::FullyDecoded );
}

// Splits an Azure DevOps address into its parts; nullopt with `error` set when it is not one.
std::optional<AddressParts> addressParts( const QUrl& url, QString* error )
{
    const auto fail = [ error ]( const QString& text ) {
        *error = text;
        return std::optional<AddressParts>{};
    };
    if ( !url.isValid() || url.scheme() != QLatin1String( "https" ) || url.host().isEmpty() ) {
        return fail( QStringLiteral( "Enter an https:// address of an Azure DevOps repository." ) );
    }
    if ( !url.password().isEmpty() ) {
        return fail( QStringLiteral( "The address cannot contain a password." ) );
    }
    if ( url.port( HttpsPort ) != HttpsPort || hasEncodedSeparator( url ) ) {
        return fail( QStringLiteral( "The address is not an Azure DevOps repository address." ) );
    }
    auto segments = url.path( QUrl::FullyDecoded ).split( QLatin1Char( '/' ), Qt::SkipEmptyParts );
    if ( hasDotSegment( segments ) ) {
        return fail( QStringLiteral( "The address is not an Azure DevOps repository address." ) );
    }

    AddressParts parts;
    const auto host = url.host().toLower();
    if ( host == AzureDevOpsHost ) {
        if ( segments.isEmpty() ) {
            return fail( QStringLiteral( "The address has no organization." ) );
        }
        parts.organization = segments.takeFirst();
    }
    else if ( host.endsWith( LegacyHostSuffix ) && host.size() > LegacyHostSuffix.size() ) {
        parts.organization = host.left( host.size() - LegacyHostSuffix.size() );
        if ( parts.organization.contains( QLatin1Char( '.' ) ) ) {
            return fail( QStringLiteral( "The address has no valid organization name." ) );
        }
        if ( !segments.isEmpty()
             && segments.first().compare( QLatin1String( "DefaultCollection" ), Qt::CaseInsensitive )
                    == 0 ) {
            segments.removeFirst();
        }
    }
    else {
        return fail( QStringLiteral(
            "Azure DevOps addresses are on dev.azure.com or <organization>.visualstudio.com." ) );
    }

    const auto is = [ &segments ]( int index, const char* text ) {
        return segments.size() > index
               && segments.at( index ).compare( QLatin1String( text ), Qt::CaseInsensitive ) == 0;
    };
    const QUrlQuery query( url );
    if ( segments.size() == 2 && is( 0, "_git" ) ) {
        parts.project = segments.at( 1 );
        parts.repository = segments.at( 1 );
    }
    else if ( segments.size() == 3 && is( 1, "_git" ) ) {
        parts.project = segments.at( 0 );
        parts.repository = segments.at( 2 );
    }
    else if ( segments.size() == 6 && is( 1, "_apis" ) && is( 2, "git" ) && is( 3, "repositories" )
              && is( 5, "items" ) ) {
        parts.project = segments.at( 0 );
        parts.repository = segments.at( 4 );
        parts.items = true;
    }
    else {
        return fail( QStringLiteral( "The address is not an Azure DevOps Git repository "
                                     "(https://dev.azure.com/<organization>/<project>/_git/"
                                     "<repository>)." ) );
    }

    for ( const auto& [ name, what ] : { std::pair{ parts.organization, "organization" },
                                         std::pair{ parts.project, "project" },
                                         std::pair{ parts.repository, "repository" } } ) {
        const auto problem = nameError( name, what );
        if ( !problem.isEmpty() ) {
            return fail( problem );
        }
    }

    if ( query.hasQueryItem( QStringLiteral( "path" ) ) ) {
        parts.path = queryValue( query, QStringLiteral( "path" ) );
    }
    if ( parts.items ) {
        const auto type = queryValue( query, QStringLiteral( "versionDescriptor.versionType" ) );
        if ( !type.isEmpty() && type.compare( QLatin1String( "branch" ), Qt::CaseInsensitive ) != 0 ) {
            return fail( QStringLiteral( "Only a branch can be used as the catalog version." ) );
        }
        const auto version = queryValue( query, QStringLiteral( "versionDescriptor.version" ) );
        if ( !version.isEmpty() ) {
            parts.branch = version;
        }
    }
    else {
        const auto version = queryValue( query, QStringLiteral( "version" ) );
        if ( version.startsWith( QLatin1String( "GB" ) ) && version.size() > 2 ) {
            parts.branch = version.mid( 2 );
        }
        else if ( !version.isEmpty() ) {
            return fail( QStringLiteral(
                "Only a branch can be used as the catalog version (version=GB<branch>)." ) );
        }
    }
    return parts;
}

QUrl itemsUrlFor( const AzureDevOpsLocation& location )
{
    QUrl url;
    url.setScheme( QStringLiteral( "https" ) );
    url.setHost( AzureDevOpsHost );
    url.setPath( QLatin1Char( '/' ) + location.organization + QLatin1Char( '/' ) + location.project
                     + QLatin1String( "/_apis/git/repositories/" ) + location.repository
                     + QLatin1String( "/items" ),
                 QUrl::DecodedMode );
    QByteArray query = "path=" + QUrl::toPercentEncoding( location.path );
    if ( !location.branch.isEmpty() ) {
        query += "&versionDescriptor.versionType=branch&versionDescriptor.version="
                 + QUrl::toPercentEncoding( location.branch );
    }
    query += "&$format=octetStream&download=true&api-version=7.1";
    url.setQuery( QString::fromLatin1( query ) );
    return url;
}
} // namespace

QString AzureDevOpsLocation::repositoryAddress() const
{
    QUrl url;
    url.setScheme( QStringLiteral( "https" ) );
    url.setHost( AzureDevOpsHost );
    url.setPath( QLatin1Char( '/' ) + organization + QLatin1Char( '/' ) + project
                     + QLatin1String( "/_git/" ) + repository,
                 QUrl::DecodedMode );
    return url.toString( QUrl::FullyEncoded );
}

QString AzureDevOpsLocation::credentialPath() const
{
    return organization + QLatin1Char( '/' ) + project + QLatin1String( "/_git/" ) + repository;
}

QUrl AzureDevOpsLocation::itemsUrl() const
{
    return itemsUrlFor( *this );
}

AzureDevOpsAddress parseAzureDevOpsAddress( const QString& address, const QString& branch,
                                            const QString& path )
{
    AzureDevOpsAddress result;
    const auto trimmed = address.trimmed();
    if ( trimmed.isEmpty() ) {
        result.error = QStringLiteral( "Enter the address of the Azure DevOps repository." );
        return result;
    }
    const auto parts = addressParts( QUrl( trimmed, QUrl::StrictMode ), &result.error );
    if ( !parts ) {
        return result;
    }
    AzureDevOpsLocation location;
    location.organization = parts->organization;
    location.project = parts->project;
    location.repository = parts->repository;
    location.branch = branch.trimmed().isEmpty() ? parts->branch.value_or( AzureDevOpsDefaultBranch )
                                                 : branch.trimmed();
    result.error = branchError( location.branch );
    if ( !result.error.isEmpty() ) {
        return result;
    }
    const auto normalized = normalizedPath(
        path.trimmed().isEmpty() ? parts->path.value_or( AzureDevOpsDefaultPath ) : path,
        &result.error );
    if ( !normalized ) {
        return result;
    }
    location.path = *normalized;
    result.location = location;
    return result;
}

bool isAzureDevOpsItemsUrl( const QUrl& url )
{
    QString error;
    const auto parts = addressParts( url, &error );
    return parts && parts->items;
}

QUrl azureDevOpsDownloadUrl( const QUrl& address )
{
    QString error;
    const auto parts = addressParts( address, &error );
    if ( !parts || !parts->path ) {
        return address;
    }
    AzureDevOpsLocation location;
    location.organization = parts->organization;
    location.project = parts->project;
    location.repository = parts->repository;
    location.branch = parts->branch.value_or( QString{} );
    if ( !location.branch.isEmpty() && !branchError( location.branch ).isEmpty() ) {
        return address;
    }
    const auto normalized = normalizedPath( *parts->path, &error );
    if ( !normalized ) {
        return address;
    }
    location.path = *normalized;
    return itemsUrlFor( location );
}

CredentialScope CredentialScope::forAzureDevOps( const AzureDevOpsLocation& location )
{
    CredentialScope scope;
    scope.host_ = AzureDevOpsHost;
    scope.port_ = HttpsPort;
    scope.organization_ = location.organization;
    return scope;
}

bool CredentialScope::contains( const QUrl& target ) const
{
    if ( host_.isEmpty() || !target.isValid() || target.scheme() != QLatin1String( "https" )
         || !target.userInfo().isEmpty()
         || target.host().compare( host_, Qt::CaseInsensitive ) != 0
         || target.port( HttpsPort ) != port_ || hasEncodedSeparator( target ) ) {
        return false;
    }
    const auto segments = target.path( QUrl::FullyDecoded ).split( QLatin1Char( '/' ) );
    if ( hasDotSegment( segments ) ) {
        return false;
    }
    if ( organization_.isEmpty() ) {
        return true;
    }
    // "/<organization>/...": segment 0 is the empty text before the leading slash.
    return segments.size() >= 2 && segments.at( 0 ).isEmpty()
           && segments.at( 1 ).compare( organization_, Qt::CaseInsensitive ) == 0;
}

CredentialFields parseCredentialFields( const QByteArray& output )
{
    CredentialFields fields;
    for ( auto line : output.split( '\n' ) ) {
        if ( line.endsWith( '\r' ) ) {
            line.chop( 1 );
        }
        const auto separator = line.indexOf( '=' );
        if ( separator <= 0 ) {
            continue;
        }
        fields.append( { line.left( separator ), line.mid( separator + 1 ) } );
    }
    return fields;
}

namespace {
QByteArray lastValue( const CredentialFields& fields, const char* key )
{
    QByteArray value;
    for ( const auto& field : fields ) {
        if ( field.first == key ) {
            value = field.second;
        }
    }
    return value;
}

bool isBase64Url( const QByteArray& text )
{
    for ( const auto character : text ) {
        const auto isAlphaNumeric = ( character >= 'A' && character <= 'Z' )
                                    || ( character >= 'a' && character <= 'z' )
                                    || ( character >= '0' && character <= '9' );
        if ( !isAlphaNumeric && character != '-' && character != '_' ) {
            return false;
        }
    }
    return true;
}
} // namespace

bool looksLikeJwt( const QByteArray& value )
{
    const auto parts = value.split( '.' );
    if ( parts.size() != 3 || parts.at( 0 ).isEmpty() || parts.at( 1 ).isEmpty() ) {
        return false;
    }
    for ( const auto& part : parts ) {
        if ( !isBase64Url( part ) ) {
            return false;
        }
    }
    const auto header = QByteArray::fromBase64Encoding(
        parts.at( 0 ), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors );
    if ( !header ) {
        return false;
    }
    const auto document = QJsonDocument::fromJson( *header );
    return document.isObject() && document.object().contains( QLatin1String( "alg" ) );
}

QByteArray authorizationFromCredential( const CredentialFields& fields )
{
    const auto authType = lastValue( fields, "authtype" );
    const auto credential = lastValue( fields, "credential" );
    if ( !authType.isEmpty() && !credential.isEmpty() ) {
        return authType + ' ' + credential;
    }
    const auto password = lastValue( fields, "password" );
    if ( password.isEmpty() ) {
        return {};
    }
    if ( looksLikeJwt( password ) ) {
        return "Bearer " + password;
    }
    return "Basic " + ( lastValue( fields, "username" ) + ':' + password ).toBase64();
}

QByteArray authorizationForPersonalAccessToken( const QByteArray& token )
{
    return "Basic " + ( ':' + token ).toBase64();
}
} // namespace cilogg::plugins
