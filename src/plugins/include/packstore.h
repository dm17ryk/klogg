#pragma once

#include "packcanonical.h"
#include "packmerge.h"
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <memory>

struct git_repository;

// The local store of one configuration pack (docs/plugin-repositories.md 4.6, decisions 8 and 9):
// a bare Git repository `<root>/<pack-id>.git` kept by libgit2, in the canonical form of
// packcanonical.h. Branch `upstream` gets one commit per installed version, exactly as shipped;
// branch `local` carries the user's edits on top and merges each new version. The effective
// content is written to `<root>/<pack-id>/current` in the pack's own layout, which is what
// CILogg reads. Users never see Git.
namespace cilogg::plugins {
struct PackCommit {
    QString id;
    QString message;
    QDateTime time;
    bool upstream = false; // a version as shipped (not a local edit)
};

struct PackUpdatePlan {
    PackFiles merged;                // the merge result, conflicts resolved as the strategy says
    QVector<PackConflict> conflicts; // what needs the user (or was resolved by the strategy)
};

class PackStore {
public:
    PackStore( const QString& root, const QString& packId );
    ~PackStore();
    PackStore( const PackStore& ) = delete;
    PackStore& operator=( const PackStore& ) = delete;

    bool exists() const;
    // The folder with the effective content in the pack's layout; empty when there is no store.
    QString contentDirectory() const;

    // The first install, or an install that replaces the pack without merging (the edits are
    // kept in history, not applied): `upstream` and `local` both get the version.
    QString install( const PackFiles& files, const QJsonObject& manifest, const QString& version,
                     const QString& source );
    // The effective files (`local`) and the installed version as shipped (`upstream`).
    PackFiles files() const;
    PackFiles upstreamFiles() const;
    // Store paths whose content differs between `upstream` and `local`.
    QStringList localEdits() const;

    // Saves an edit as a commit on `local`; nullopt removes the item.
    QString commitEdit( const QString& storePath, const std::optional<QByteArray>& bytes,
                        const QString& message );
    // The merge of a new version with the local edits.
    PackUpdatePlan planUpdate( const PackFiles& newFiles, MergeStrategy strategy ) const;
    // Commits the new version on `upstream` and the merge result `merged` on `local`.
    QString applyUpdate( const PackFiles& newFiles, const PackFiles& merged,
                         const QJsonObject& manifest, const QString& version,
                         const QString& source );
    // Newest first; for one store path only the commits that changed it.
    QVector<PackCommit> history( const QString& storePath = {} ) const;
    // A new `local` commit restoring a store path (all paths when empty) as it was in `commitId`
    // (the installed version when empty): Revert and Reset to Pack Version.
    QString restore( const QString& storePath, const QString& commitId, const QString& message );
    // Removes the store (the pack was removed).
    QString remove();

    QString error() const
    {
        return error_;
    }

private:
    bool open( bool create ) const;
    QString commit( const char* branch, const PackFiles& files, const QString& message,
                    const QStringList& extraParents = {} );
    PackFiles filesAt( const QString& reference ) const;
    QString writeContent( const PackFiles& files, const QJsonObject& manifest ) const;

    QString root_;
    QString packId_;
    mutable git_repository* repository_ = nullptr;
    mutable QString error_;
};
} // namespace cilogg::plugins
