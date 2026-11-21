#pragma once

#include "packcanonical.h"
#include "packmerge.h"
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <memory>

struct git_repository;
struct git_oid;

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

// Where a store's branches were, to put them back (PackStore::rollback).
struct PackTips {
    bool existed = false;
    QString upstream;
    QString local;
};

struct PackUpdatePlan {
    PackFiles merged;                // the merge result, conflicts resolved as the strategy says
    QVector<PackConflict> conflicts; // what needs the user (or was resolved by the strategy)
    QString error;                   // the store could not be read: nothing was merged
};

class PackStore {
public:
    PackStore( const QString& root, const QString& packId );
    ~PackStore();
    PackStore( const PackStore& ) = delete;
    PackStore& operator=( const PackStore& ) = delete;

    bool exists() const;
    // The repository folder is on disk (readable or not).
    bool hasRepository() const;
    // The commit of `local` (the effective content), or "" without a readable store: changes
    // with every edit, reset and update, unlike file times.
    QString localTip() const;
    // The store is on disk but cannot be read: the repository does not open or lacks a branch.
    bool damaged() const;
    // The folder with the effective content in the pack's layout; empty when there is no store.
    QString contentDirectory() const;
    // Rebuilds a damaged store from the installed version (design 4.6): the damaged repository is
    // kept aside as `<pack-id>.git.damaged-<time>`, the new store gets `installed` on `upstream`,
    // and the content folder's files (the edits the old store had applied) come back as one
    // `local` commit.
    QString rebuild( const PackFiles& installed, const QJsonObject& manifest,
                     const QString& version );

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
    // The effective files that restore would leave, without committing them (an enabled pack's
    // reset is checked first); `changes` is false when it would leave them as they are.
    QString planRestore( const QString& storePath, const QString& commitId, PackFiles* stored,
                         bool* changes ) const;
    // Removes the store (the pack was removed).
    QString remove();
    // The branch tips now, and putting them back: an install or update whose package commit
    // failed after the store moved returns the store (and its content folder) to `tips`, or
    // removes a store that did not exist then.
    PackTips tips() const;
    QString rollback( const PackTips& tips );
    // Writes `files` in the pack's own layout into `folder` (replaced): plugin.json, the
    // previews document and exactly the files the manifest lists. The content folder is written
    // this way, and so is a merge result that is checked before it is installed.
    static QString writeFolder( const QString& folder, const PackFiles& files,
                                const QJsonObject& manifest );
    // Writes the effective content again when its folder is missing or holds another commit (a
    // write that failed after its commit).
    QString repairContent();
    // The journal of an install or update in progress: the branch tips before it, written before
    // the store moves and removed (endUpdate) once the package folder is replaced or the store is
    // put back. Found later, it means CILogg ended part-way, whatever the versions.
    QString beginUpdate( const PackTips& before );
    void endUpdate();
    bool hasJournal() const;
    // A journal is there and the content folder does not hold the `local` commit it recorded
    // from before: the folder may hold what an unfinished install or update wrote, a version that
    // was never installed, so it is not loaded until the journal is acted on.
    bool contentFromUnfinishedUpdate() const;
    // Finishes an install or update whose journal is still there. When `installed` (the
    // installed package folder's files, with `installedManifest`) is what the store's `upstream`
    // holds, the package was replaced and only the journal was left: it goes. Otherwise (CILogg
    // ended between moving `upstream` and `local`, or before the package folder was replaced; or
    // no package is installed) the store is put back to the tips before, so it matches the
    // installed package again. Empty when there was nothing to do or it was done; the journal
    // stays when it could not be.
    QString recoverInterruptedUpdate( const PackFiles* installed = nullptr,
                                      const QJsonObject& installedManifest = {} );
    // Sets an earlier install's store aside as `<name>.orphaned-<time>` (kept, never deleted):
    // a pack installed again starts a store of its own.
    QString setAside();

    QString error() const
    {
        return error_;
    }

private:
    bool open( bool create ) const;
    // `<root>/<pack-id>.git`, or on Windows a short folder of the user's when that path is too
    // deep for libgit2.
    QString repositoryPath() const;
    // `<root>/<pack-id>.update.json`: the journal of an install or update in progress.
    QString journalPath() const;
    // A commit on `branch` (its tip is the first parent); with `moveBranch` false the branch
    // stays where it is, so several commits can be published together.
    QString commit( const char* branch, const PackFiles& files, const QString& message,
                    const QStringList& extraParents = {}, git_oid* created = nullptr,
                    bool moveBranch = true );
    // The files of a commit or branch; `complete` is false when the tree could not be read
    // whole (nothing is returned then).
    PackFiles filesAt( const QString& reference, bool* complete = nullptr ) const;
    QString writeContent( const PackFiles& files, const QJsonObject& manifest ) const;
    // Writes the content folder for committed files (with their manifest).
    QString publish( const PackFiles& stored ) const;

    QString root_;
    QString packId_;
    mutable git_repository* repository_ = nullptr;
    mutable QString error_;
};
} // namespace cilogg::plugins
