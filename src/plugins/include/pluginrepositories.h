#pragma once

#include "azuredevops.h"
#include <QDateTime>
#include <QList>
#include <QSettings>
#include <QString>
#include <QVariantMap>
#include <optional>

namespace cilogg::plugins {
inline const QString SignInNone = QStringLiteral( "none" );
inline const QString SignInAzureDevOps = QStringLiteral( "azure-devops" );
inline const QString CredentialSourceGitCredentialManager
    = QStringLiteral( "git-credential-manager" );
inline const QString CredentialSourcePersonalAccessToken = QStringLiteral( "personal-access-token" );
// The check state of a repository that answered 401, 203 or a web page (design 3.5, 3.6).
inline const QString SignInRequiredState = QStringLiteral( "sign_in_required" );

// The ordered list of plugin repositories in the plugin settings.ini (docs/plugin-repositories.md
// 3.1). IDs are never reused, the address cannot be edited, and the order is a priority. Keys:
// repositories/order (IDs in order), repositories/retired (removed IDs), repositories/schema
// (set by the migration) and repositories/items/<id>/<field>. Settings never hold a credential:
// only `signIn` and `credentialSource` (design 3.6).
struct RepositoryRecord {
    QString id;
    QString name;
    QString url; // the catalog address; for Azure DevOps the repository address
    QString signIn = SignInNone;
    QString branch;           // Azure DevOps only
    QString path;             // Azure DevOps only
    QString credentialSource; // Azure DevOps only
    bool enabled = true;
    bool autoUpdate = false;
    QString lastCheck;    // ISO 8601 UTC time of the last successful check
    QString lastError;    // error of the last check, empty after a success
    QString lastState;    // "", "loaded", "empty", "failed" or "sign_in_required"
    int pluginCount = -1; // usable entries at the last successful check; -1 before one
    int warningCount = 0; // entries left out at the last successful check
    int hiddenCount = 0;  // entries this host cannot use at the last successful check

    bool isAzureDevOps() const
    {
        return signIn == SignInAzureDevOps;
    }
    // The catalog location of an Azure DevOps repository; nullopt for any other one.
    std::optional<AzureDevOpsLocation> azureDevOps() const;
    QVariantMap toVariant( int position ) const;
};

// Where an installed plugin came from. An empty repository means a local file or a shipped copy.
struct InstallSource {
    QString repository;
    QString version;
    QString url;
};

class PluginRepositories {
public:
    static constexpr int MaxRepositories = 16;
    static constexpr int MaxNameLength = 128;
    static constexpr int MaxAddressLength = 2048;
    static const QString DefaultCatalogUrl;
    static const QString DefaultName;

    explicit PluginRepositories( QSettings& settings );

    // The first start with E15 turns `catalogUrl` into the repository `default`; `catalogUrl`
    // itself stays for older copies that share the folder.
    void migrate();
    QList<RepositoryRecord> list() const;
    std::optional<RepositoryRecord> find( const QString& id ) const;
    // 1-based; 0 when the repository does not exist.
    int position( const QString& id ) const;
    QString idForAddress( const QString& url ) const;

    // Each returns an error text, empty on success. The caller persists the settings.
    QString add( const QVariantMap& options, RepositoryRecord* added );
    QString update( const QString& id, const QVariantMap& options, RepositoryRecord* updated );
    QString remove( const QString& id );
    QString move( const QString& id, const QVariantMap& options );
    void recordCheck( const QString& id, const QString& state, int count, int warnings, int hidden,
                      const QString& error, const QDateTime& when );
    // After Sign Out of a repository that signs in with a personal access token.
    void recordSignOut( const QString& id );
    // Records a successful sign-in with `credentialSource`.
    void setCredentialSource( const QString& id, const QString& credentialSource );

    InstallSource source( const QString& pluginId ) const;
    void setSource( const QString& pluginId, const InstallSource& source );
    void clearSource( const QString& pluginId );

    // The form two addresses are compared in: scheme and host lower case, default port and
    // fragment removed, path segments normalized.
    static QString normalizedAddress( const QString& url );
    // Empty when `url` is an acceptable public catalog address.
    static QString addressError( const QString& url );

private:
    QStringList order() const;
    QStringList retired() const;
    void setOrder( const QStringList& ids );
    QString newId( const QString& name ) const;
    // What two repositories are compared by to refuse a duplicate: the normalized address, or
    // the Azure DevOps organization, project, repository, branch and path.
    static QString identity( const RepositoryRecord& record );
    static QString key( const QString& id, const char* field );
    QSettings& settings_;
};
} // namespace cilogg::plugins
