// E15b: Azure DevOps addresses, the credential scope and the Authorization header
// (docs/plugin-repositories.md sections 3.5, 3.6 and 8).
#include "../helpers/fake_git_environment.h"
#include "azuredevops.h"
#include "repositorycredentials.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QUrlQuery>
#include <catch2/catch.hpp>

using namespace cilogg::plugins;

namespace {
bool waitFor( const std::function<bool()>& predicate, int timeout = 15000 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !predicate() && timer.elapsed() < timeout ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 2 );
    }
    return predicate();
}

using testgit::FakeGit;

struct FillResult {
    bool done = false;
    CredentialPtr credential;
    GitCredentialHelper::Outcome outcome = GitCredentialHelper::Outcome::Failed;
};

FillResult runFill( const QString& git, const AzureDevOpsLocation& location, bool interactive,
                    int timeoutMs = 0 )
{
    QObject context;
    auto result = std::make_shared<FillResult>();
    GitCredentialHelper::fill(
        git, location, interactive, &context,
        [ result ]( CredentialPtr credential, GitCredentialHelper::Outcome outcome ) {
            result->done = true;
            result->credential = std::move( credential );
            result->outcome = outcome;
        },
        timeoutMs );
    REQUIRE( waitFor( [ result ] { return result->done; } ) );
    return *result;
}

GitCredentialHelper::Outcome runVerb( bool approve, const Credential& credential )
{
    QObject context;
    auto done = std::make_shared<std::optional<GitCredentialHelper::Outcome>>();
    const auto finished = [ done ]( GitCredentialHelper::Outcome outcome ) { *done = outcome; };
    if ( approve ) {
        GitCredentialHelper::approve( FakeGit::path(), credential, &context, finished );
    }
    else {
        GitCredentialHelper::reject( FakeGit::path(), credential, &context, finished );
    }
    REQUIRE( waitFor( [ done ] { return done->has_value(); } ) );
    return **done;
}

AzureDevOpsLocation parsed( const QString& address, const QString& branch = {},
                            const QString& path = {} )
{
    const auto result = parseAzureDevOpsAddress( address, branch, path );
    INFO( address.toStdString() << " -> " << result.error.toStdString() );
    REQUIRE( result.location.has_value() );
    return *result.location;
}

QString parseError( const QString& address, const QString& branch = {}, const QString& path = {} )
{
    const auto result = parseAzureDevOpsAddress( address, branch, path );
    INFO( address.toStdString() );
    REQUIRE_FALSE( result.location.has_value() );
    REQUIRE_FALSE( result.error.isEmpty() );
    return result.error;
}
} // namespace

TEST_CASE( "Azure DevOps addresses are parsed from every documented form", "[plugins][E15]" )
{
    SECTION( "repository address uses the default branch and path" )
    {
        const auto location = parsed( "https://dev.azure.com/Contoso/Tools/_git/plugin-catalog" );
        CHECK( location.organization == "Contoso" );
        CHECK( location.project == "Tools" );
        CHECK( location.repository == "plugin-catalog" );
        CHECK( location.branch == "catalog" );
        CHECK( location.path == "/plugins.json" );
        CHECK( location.repositoryAddress()
               == "https://dev.azure.com/Contoso/Tools/_git/plugin-catalog" );
        CHECK( location.credentialPath() == "Contoso/Tools/_git/plugin-catalog" );
    }
    SECTION( "the clone address with a user name drops the name" )
    {
        const auto location = parsed( "https://contoso@dev.azure.com/contoso/Tools/_git/catalog" );
        CHECK( location.organization == "contoso" );
        CHECK( location.repositoryAddress() == "https://dev.azure.com/contoso/Tools/_git/catalog" );
    }
    SECTION( "a password in the address is refused" )
    {
        CHECK( parseError( "https://me:secret@dev.azure.com/contoso/Tools/_git/catalog" )
                   .contains( "password" ) );
    }
    SECTION( "a project without its own name in the address" )
    {
        const auto location = parsed( "https://dev.azure.com/contoso/_git/Tools" );
        CHECK( location.project == "Tools" );
        CHECK( location.repository == "Tools" );
    }
    SECTION( "names with spaces stay decoded and are encoded again in addresses" )
    {
        const auto location = parsed( "https://dev.azure.com/contoso/Field%20Tools/_git/My%20Catalog" );
        CHECK( location.project == "Field Tools" );
        CHECK( location.repository == "My Catalog" );
        CHECK( location.repositoryAddress()
               == "https://dev.azure.com/contoso/Field%20Tools/_git/My%20Catalog" );
        CHECK( location.credentialPath() == "contoso/Field Tools/_git/My Catalog" );
    }
    SECTION( "a file link sets the branch and the path" )
    {
        const auto location = parsed( "https://dev.azure.com/contoso/Tools/_git/catalog"
                                      "?path=/release/plugins.json&version=GBpublished&_a=contents" );
        CHECK( location.branch == "published" );
        CHECK( location.path == "/release/plugins.json" );
        CHECK( location.repositoryAddress() == "https://dev.azure.com/contoso/Tools/_git/catalog" );
    }
    SECTION( "a branch name with a slash" )
    {
        const auto location = parsed(
            "https://dev.azure.com/contoso/Tools/_git/catalog?path=%2Fplugins.json&version=GBrelease%2Fv2" );
        CHECK( location.branch == "release/v2" );
        CHECK( location.path == "/plugins.json" );
    }
    SECTION( "a tag or commit version is refused" )
    {
        CHECK( parseError( "https://dev.azure.com/contoso/Tools/_git/catalog?version=GTv1" )
                   .contains( "branch" ) );
        CHECK( parseError( "https://dev.azure.com/contoso/Tools/_git/catalog?version=GCabc123" )
                   .contains( "branch" ) );
    }
    SECTION( "an Items API address" )
    {
        const auto location
            = parsed( "https://dev.azure.com/contoso/Tools/_apis/git/repositories/catalog/items"
                      "?path=/plugins.json&versionDescriptor.versionType=branch"
                      "&versionDescriptor.version=catalog&$format=octetStream&api-version=7.1" );
        CHECK( location.repository == "catalog" );
        CHECK( location.branch == "catalog" );
        CHECK( location.path == "/plugins.json" );
    }
    SECTION( "explicit branch and path win over the address" )
    {
        const auto location = parsed(
            "https://dev.azure.com/contoso/Tools/_git/catalog?path=/a.json&version=GBmain", "pub",
            "sub/plugins.json" );
        CHECK( location.branch == "pub" );
        CHECK( location.path == "/sub/plugins.json" );
    }
    SECTION( "visualstudio.com is converted to dev.azure.com" )
    {
        const auto location = parsed( "https://Contoso.visualstudio.com/Tools/_git/catalog" );
        CHECK( location.organization == "contoso" );
        CHECK( location.repositoryAddress() == "https://dev.azure.com/contoso/Tools/_git/catalog" );
        const auto collection
            = parsed( "https://contoso.visualstudio.com/DefaultCollection/Tools/_git/catalog" );
        CHECK( collection.project == "Tools" );
        CHECK( collection.repositoryAddress()
               == "https://dev.azure.com/contoso/Tools/_git/catalog" );
    }
    SECTION( "addresses that are not Azure DevOps repositories" )
    {
        parseError( "http://dev.azure.com/contoso/Tools/_git/catalog" );
        parseError( "https://example.com/contoso/Tools/_git/catalog" );
        parseError( "https://dev.azure.com/contoso/Tools" );
        parseError( "https://dev.azure.com/contoso/Tools/_build" );
        parseError( "https://dev.azure.com:8443/contoso/Tools/_git/catalog" );
        parseError( "https://dev.azure.com/contoso/To%2Fols/_git/catalog" );
        parseError( "not an address" );
        parseError( "" );
    }
    SECTION( "unsafe paths and branches are refused" )
    {
        parseError( "https://dev.azure.com/contoso/Tools/_git/catalog", {}, "/../secret.json" );
        parseError( "https://dev.azure.com/contoso/Tools/_git/catalog", {}, "/a\\b.json" );
        parseError( "https://dev.azure.com/contoso/Tools/_git/catalog", "bad branch" );
        parseError( "https://dev.azure.com/contoso/Tools/_git/catalog", "-x" );
    }
}

TEST_CASE( "The Items API address returns the raw file of the branch", "[plugins][E15]" )
{
    const auto location = parsed( "https://dev.azure.com/contoso/Field%20Tools/_git/catalog",
                                  "release/v2", "/plugins.json" );
    const auto url = location.itemsUrl();
    CHECK( url.scheme() == "https" );
    CHECK( url.host() == "dev.azure.com" );
    CHECK( url.path() == "/contoso/Field Tools/_apis/git/repositories/catalog/items" );
    const QUrlQuery query( url );
    CHECK( query.queryItemValue( "path", QUrl::FullyDecoded ) == "/plugins.json" );
    CHECK( query.queryItemValue( "versionDescriptor.versionType" ) == "branch" );
    CHECK( query.queryItemValue( "versionDescriptor.version", QUrl::FullyDecoded )
           == "release/v2" );
    CHECK( query.queryItemValue( "$format" ) == "octetStream" );
    CHECK( query.queryItemValue( "download" ) == "true" );
    CHECK( query.queryItemValue( "api-version" ) == "7.1" );
    CHECK( isAzureDevOpsItemsUrl( url ) );
    CHECK_FALSE( isAzureDevOpsItemsUrl( QUrl( "https://example.com/plugins.json" ) ) );
}

TEST_CASE( "Package addresses in an Azure DevOps catalog are converted", "[plugins][E15]" )
{
    SECTION( "a file link becomes the Items API address of its branch" )
    {
        const auto url = azureDevOpsDownloadUrl(
            QUrl( "https://contoso.visualstudio.com/Tools/_git/catalog"
                  "?path=/packages/demo-1.0.0.zip&version=GBcatalog" ) );
        CHECK( url.host() == "dev.azure.com" );
        CHECK( url.path() == "/contoso/Tools/_apis/git/repositories/catalog/items" );
        const QUrlQuery query( url );
        CHECK( query.queryItemValue( "path", QUrl::FullyDecoded ) == "/packages/demo-1.0.0.zip" );
        CHECK( query.queryItemValue( "versionDescriptor.version" ) == "catalog" );
    }
    SECTION( "a file link without a version keeps the repository default branch" )
    {
        const auto url = azureDevOpsDownloadUrl(
            QUrl( "https://dev.azure.com/contoso/Tools/_git/catalog?path=/p.zip" ) );
        const QUrlQuery query( url );
        CHECK( query.queryItemValue( "path", QUrl::FullyDecoded ) == "/p.zip" );
        CHECK_FALSE( query.hasQueryItem( "versionDescriptor.version" ) );
        CHECK( query.queryItemValue( "$format" ) == "octetStream" );
    }
    SECTION( "other addresses are unchanged" )
    {
        const QUrl other( "https://downloads.example.com/demo-1.0.0.zip" );
        CHECK( azureDevOpsDownloadUrl( other ) == other );
        const QUrl artifact( "https://dev.azure.com/contoso/Tools/_apis/build/builds/1/artifacts" );
        CHECK( azureDevOpsDownloadUrl( artifact ) == artifact );
    }
}

TEST_CASE( "A credential is attached only inside the repository scope", "[plugins][E15]" )
{
    const auto scope
        = CredentialScope::forAzureDevOps( parsed( "https://dev.azure.com/Contoso/Tools/_git/c" ) );
    CHECK( scope.contains( QUrl( "https://dev.azure.com/Contoso/Tools/_apis/git/x" ) ) );
    CHECK( scope.contains( QUrl( "https://dev.azure.com/contoso/Other/_git/y" ) ) );
    CHECK( scope.contains( QUrl( "https://DEV.AZURE.COM:443/CONTOSO/a" ) ) );
    CHECK( scope.contains( QUrl( "https://dev.azure.com/Cont%6Fso/a" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "http://dev.azure.com/Contoso/a" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://dev.azure.com:8443/Contoso/a" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://dev.azure.com/Fabrikam/a" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://dev.azure.com/ContosoX/a" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://dev.azure.com/" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://evil.example/Contoso/a" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://dev.azure.com.evil.example/Contoso/a" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://dev.azure.com/Contoso/../Fabrikam/a" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://dev.azure.com/Contoso/%2E%2E/Fabrikam/a" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://dev.azure.com/Contoso/./a" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://dev.azure.com/Contoso%2FFabrikam/a" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://dev.azure.com/Contoso/a%2fb" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://dev.azure.com/Contoso/a%5Cb" ) ) );
    CHECK_FALSE( scope.contains( QUrl( "https://user@dev.azure.com/Contoso/a" ) ) );
    CHECK_FALSE( scope.contains( QUrl() ) );
}

TEST_CASE( "The Authorization header follows the credential the helper returned",
           "[plugins][E15]" )
{
    const QByteArray jwt = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
                           "eyJhdWQiOiI0OTliODRhYyJ9.c2lnbmF0dXJl";
    SECTION( "authtype Bearer with a credential" )
    {
        const auto fields = parseCredentialFields( "protocol=https\nhost=dev.azure.com\n"
                                                   "authtype=Bearer\ncredential=abc.def\n" );
        CHECK( authorizationFromCredential( fields ) == "Bearer abc.def" );
    }
    SECTION( "a Microsoft Entra token as the password is sent as Bearer" )
    {
        const auto fields = parseCredentialFields( "username=me@contoso.com\npassword=" + jwt
                                                   + "\n" );
        CHECK( looksLikeJwt( jwt ) );
        CHECK( authorizationFromCredential( fields ) == "Bearer " + jwt );
    }
    SECTION( "any other password is sent as Basic" )
    {
        const auto fields = parseCredentialFields( "username=me\npassword=pat-value\n" );
        CHECK( authorizationFromCredential( fields )
               == "Basic " + QByteArray( "me:pat-value" ).toBase64() );
        CHECK_FALSE( looksLikeJwt( "pat-value" ) );
        CHECK_FALSE( looksLikeJwt( "a.b.c" ) );
        CHECK_FALSE( looksLikeJwt( "eyJ.e30.x" ) );
    }
    SECTION( "a value with a control character is refused" )
    {
        CHECK( authorizationFromCredential( parseCredentialFields( "password=ab\rX-Evil: 1\n" ) )
                   .isEmpty() );
        CHECK( authorizationFromCredential(
                   parseCredentialFields( "authtype=Bearer\ncredential=a\x01" "b\n" ) )
                   .isEmpty() );
        CHECK( authorizationFromCredential(
                   parseCredentialFields( "authtype=Bearer x\ncredential=ab\n" ) )
                   .isEmpty() );
    }
    SECTION( "no usable credential" )
    {
        CHECK( authorizationFromCredential( parseCredentialFields( "username=me\n" ) ).isEmpty() );
        CHECK( authorizationFromCredential( parseCredentialFields( "" ) ).isEmpty() );
        CHECK( authorizationFromCredential( parseCredentialFields( "authtype=Bearer\n" ) )
                   .isEmpty() );
    }
    SECTION( "a personal access token is Basic with an empty user name" )
    {
        CHECK( authorizationForPersonalAccessToken( "tok" )
               == "Basic " + QByteArray( ":tok" ).toBase64() );
    }
    SECTION( "the parser keeps every line, including repeated keys" )
    {
        const auto fields
            = parseCredentialFields( "capability[]=authtype\ncapability[]=state\r\nbad line\n\n" );
        REQUIRE( fields.size() == 2 );
        CHECK( fields.at( 0 ).first == "capability[]" );
        CHECK( fields.at( 1 ).second == "state" );
    }
}

TEST_CASE( "Git is run by an absolute path only", "[plugins][E15]" )
{
    CHECK( GitCredentialHelper::resolveGit( FakeGit::path() )
           == QFileInfo( FakeGit::path() ).absoluteFilePath() );
    CHECK( GitCredentialHelper::resolveGit( "git" ).isEmpty() );
    CHECK( GitCredentialHelper::resolveGit( "C:/no/such/git.exe" ).isEmpty() );
    QTemporaryDir directory;
    const auto script = directory.filePath( "git.cmd" );
    QFile file( script );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.close();
    CHECK( GitCredentialHelper::resolveGit( script ).isEmpty() );
}

TEST_CASE( "git credential fill runs with the credential rules", "[plugins][E15]" )
{
    const auto location = parsed( "https://dev.azure.com/contoso/Tools/_git/catalog" );

    SECTION( "silent fill: no prompt variables and the request Git makes for a clone" )
    {
        FakeGit git( "bearer", "bearer-token" );
        const auto result = runFill( FakeGit::path(), location, false );
        REQUIRE( result.outcome == GitCredentialHelper::Outcome::Ok );
        REQUIRE( result.credential );
        CHECK( result.credential->authorization() == "Bearer bearer-token" );
        CHECK( result.credential->source() == Credential::Source::GitCredentialManager );
        const auto calls = git.calls();
        REQUIRE( calls.size() == 1 );
        CHECK( calls.at( 0 ).value( "arguments" ).toArray()
               == QJsonArray{ "-c", "core.askPass=", "credential", "fill" } );
        CHECK( calls.at( 0 ).value( "stdin" ).toString()
               == "protocol=https\nhost=dev.azure.com\npath=contoso/Tools/_git/catalog\n"
                  "capability[]=authtype\n\n" );
        CHECK( calls.at( 0 ).value( "GIT_TERMINAL_PROMPT" ).toString() == "0" );
        CHECK( calls.at( 0 ).value( "GCM_INTERACTIVE" ).toString() == "never" );
        CHECK( calls.at( 0 ).value( "GIT_ASKPASS" ).toString() == "<unset>" );
        CHECK( calls.at( 0 ).value( "SSH_ASKPASS" ).toString() == "<unset>" );
        // Never inside a repository whose config could name another credential helper.
        CHECK( QDir::cleanPath( calls.at( 0 ).value( "cwd" ).toString() )
                   .compare( GitCredentialHelper::workingDirectory(), Qt::CaseInsensitive )
               == 0 );
        CHECK( QDir::cleanPath( calls.at( 0 ).value( "GIT_CEILING_DIRECTORIES" ).toString() )
                   .compare( QFileInfo( GitCredentialHelper::workingDirectory() ).absolutePath(),
                             Qt::CaseInsensitive )
               == 0 );
    }
    SECTION( "an interactive fill lets the credential manager show its window" )
    {
        FakeGit git( "jwt", "entra" );
        const auto result = runFill( FakeGit::path(), location, true );
        REQUIRE( result.credential );
        CHECK( result.credential->authorization().startsWith( "Bearer eyJ" ) );
        const auto calls = git.calls();
        REQUIRE( calls.size() == 1 );
        CHECK( calls.at( 0 ).value( "GCM_INTERACTIVE" ).toString() == "<unset>" );
        CHECK( calls.at( 0 ).value( "GIT_TERMINAL_PROMPT" ).toString() == "0" );
        CHECK( calls.at( 0 ).value( "GIT_ASKPASS" ).toString() == "<unset>" );
    }
    SECTION( "a stored user name and password is Basic" )
    {
        FakeGit git( "basic", "stored-pat" );
        const auto result = runFill( FakeGit::path(), location, false );
        REQUIRE( result.credential );
        CHECK( result.credential->authorization()
               == "Basic " + QByteArray( "user:stored-pat" ).toBase64() );
    }
    SECTION( "failures" )
    {
        {
            FakeGit git( "fail" );
            const auto result = runFill( FakeGit::path(), location, false );
            CHECK( result.outcome == GitCredentialHelper::Outcome::Failed );
            CHECK_FALSE( result.credential );
            CHECK_FALSE( GitCredentialHelper::outcomeText( result.outcome ).contains( "SECRET" ) );
        }
        {
            FakeGit git( "empty" );
            const auto result = runFill( FakeGit::path(), location, false );
            CHECK( result.outcome == GitCredentialHelper::Outcome::NoCredential );
        }
        {
            FakeGit git( "hang" );
            QElapsedTimer timer;
            timer.start();
            const auto result = runFill( FakeGit::path(), location, false, 500 );
            CHECK( result.outcome == GitCredentialHelper::Outcome::TimedOut );
            CHECK( timer.elapsed() < 10000 );
        }
        {
            const auto result = runFill( QString{}, location, false );
            CHECK( result.outcome == GitCredentialHelper::Outcome::GitNotFound );
        }
    }
}

TEST_CASE( "approve and reject send the complete fill answer", "[plugins][E15]" )
{
    const auto location = parsed( "https://dev.azure.com/contoso/Tools/_git/catalog" );
    FakeGit git( "bearer", "tok" );
    const auto filled = runFill( FakeGit::path(), location, false );
    REQUIRE( filled.credential );
    const auto answer = QString::fromUtf8( filled.credential->fillOutput() );
    CHECK( answer.contains( "credential=tok" ) );

    CHECK( runVerb( true, *filled.credential ) == GitCredentialHelper::Outcome::Ok );
    CHECK( runVerb( false, *filled.credential ) == GitCredentialHelper::Outcome::Ok );
    const auto calls = git.calls();
    REQUIRE( calls.size() == 3 );
    CHECK( calls.at( 1 ).value( "verb" ).toString() == "approve" );
    CHECK( calls.at( 1 ).value( "stdin" ).toString() == answer + "\n" );
    CHECK( calls.at( 1 ).value( "GCM_INTERACTIVE" ).toString() == "never" );
    CHECK( calls.at( 2 ).value( "verb" ).toString() == "reject" );
    CHECK( calls.at( 2 ).value( "stdin" ).toString() == answer + "\n" );
}

TEST_CASE( "Personal access tokens in the session store", "[plugins][E15]" )
{
    MemoryPatStore store;
    std::optional<QByteArray> read;
    PatStore::Status status;
    store.write( "team", "secret-pat", [ & ]( PatStore::Status result ) { status = result; } );
    CHECK( status.error.isEmpty() );
    CHECK( status.sessionOnly );
    store.read( "team", [ & ]( std::optional<QByteArray> token, PatStore::Status ) {
        read = token;
    } );
    REQUIRE( read );
    CHECK( *read == "secret-pat" );
    store.remove( "team", []( PatStore::Status ) {} );
    CHECK_FALSE( store.contains( "team" ) );
    CHECK( PatStore::storeName( "team" ) == "CILogg plugin repository team" );
}
