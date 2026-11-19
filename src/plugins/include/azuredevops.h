#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>
#include <optional>

namespace cilogg::plugins {
// A catalog file in an Azure DevOps Git repository (docs/plugin-repositories.md 3.5). Names are
// kept decoded; the addresses built from them are encoded again.
struct AzureDevOpsLocation {
    QString organization;
    QString project;
    QString repository;
    QString branch;
    QString path; // always starts with '/'

    // https://dev.azure.com/<organization>/<project>/_git/<repository>
    QString repositoryAddress() const;
    // <organization>/<project>/_git/<repository>: the `path` Git sends to its credential helper.
    QString credentialPath() const;
    // The Items API address that returns the raw file at `path` on `branch`.
    QUrl itemsUrl() const;
};

struct AzureDevOpsAddress {
    std::optional<AzureDevOpsLocation> location;
    QString error; // empty when `location` is set
};

inline const QString AzureDevOpsHost = QStringLiteral( "dev.azure.com" );
inline const QString AzureDevOpsDefaultBranch = QStringLiteral( "catalog" );
inline const QString AzureDevOpsDefaultPath = QStringLiteral( "/plugins.json" );

// Accepts a repository address, a file link copied from the web portal (?path=...&version=GB...)
// or an Items API address, on dev.azure.com or on <organization>.visualstudio.com (converted to
// dev.azure.com). A user name in the address is dropped; a password is refused. A non-empty
// `branch` or `path` wins over the address; otherwise the address's own values or the defaults
// (`catalog`, `/plugins.json`) are used.
AzureDevOpsAddress parseAzureDevOpsAddress( const QString& address, const QString& branch = {},
                                            const QString& path = {} );

bool isAzureDevOpsItemsUrl( const QUrl& url );

// A package address in an Azure DevOps catalog: a file link or an Items API address becomes the
// Items API address that returns the raw file. Any other address is returned unchanged.
QUrl azureDevOpsDownloadUrl( const QUrl& address );

// Where a repository's credential may be sent (docs/plugin-repositories.md 3.6): HTTPS, the host
// and port, and on dev.azure.com the first path segment (the organization, compared without
// regard to case). Paths with "." or ".." segments, an encoded "/" or "\", or user information are
// outside every scope.
class CredentialScope {
public:
    static CredentialScope forAzureDevOps( const AzureDevOpsLocation& location );
    bool contains( const QUrl& target ) const;

private:
    QString host_;
    int port_ = 443;
    QString organization_;
};

// The lines of a `git credential fill` answer, in order, repeated keys kept.
using CredentialFields = QList<QPair<QByteArray, QByteArray>>;
CredentialFields parseCredentialFields( const QByteArray& output );

// The Authorization header value for a `git credential fill` answer: `authtype` with `credential`
// (Git 2.46 or newer), Bearer for a Microsoft Entra token given as the password, Basic otherwise.
// Empty when the answer holds no usable credential.
QByteArray authorizationFromCredential( const CredentialFields& fields );
// Azure DevOps takes a personal access token as Basic with an empty user name.
QByteArray authorizationForPersonalAccessToken( const QByteArray& token );
// A JSON Web Token: three base64url parts whose first decodes to a JSON header with "alg".
bool looksLikeJwt( const QByteArray& value );
} // namespace cilogg::plugins
