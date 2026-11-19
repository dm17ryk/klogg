#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QSysInfo>
#include <optional>

namespace cilogg::plugins {
// One repository's plugins.json (schema 1). docs/plugin-repositories.md sections 3.2 and 3.4.
constexpr int MaxCatalogEntries = 1000;
constexpr int MaxCatalogWarnings = 50;

struct CatalogRead {
    // Entries usable on this host, each with the host's `entry_id`, `repository` and `kind`.
    QJsonArray entries;
    // Entries for this platform that this host cannot use: {id, version, reason}.
    QJsonArray hidden;
    // Invalid entries, each left out on its own; the rest of the catalog still loads.
    QStringList warnings;
    // The catalog as a whole is unreadable (bad JSON, wrong schema, too many entries).
    QString error;
};

QString platformName();
// `kind` of a catalog entry or a plugin.json; "process" when absent.
QString packageKind( const QJsonObject& json );
// Kinds this host installs. Configuration packs (`config-pack`) come with E16.
bool supportedKind( const QString& kind );
// A hash of the entry as published (without `entry_id`) and of the repository that lists it, so
// the same package in two repositories gives two entries.
QString catalogEntryId( const QJsonObject& entry, const QString& repositoryId );
// Orders host versions such as 26.04.2.1078 (`min_host_version`); nullopt for unreadable text.
std::optional<int> compareHostVersions( const QString& a, const QString& b );

CatalogRead readCatalog( const QByteArray& bytes, const QString& repositoryId,
                         const QString& hostVersion, const QString& os = platformName(),
                         const QString& arch = QSysInfo::buildCpuArchitecture() );
} // namespace cilogg::plugins
