#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

// Three-way merge of a configuration pack's local edits with a new pack version
// (docs/plugin-repositories.md 4.6): base = the installed version, theirs = the new version,
// mine = the local edit. Structured items (previews, blocks) merge item by item and field by
// field; Lua, Python and the preview order merge as text.
namespace cilogg::plugins {
// How a conflict is resolved without a merge window: Fail leaves it to the user (the update is
// refused from automation), KeepMine and TakeTheirs pick a side.
enum class MergeStrategy { Fail, KeepMine, TakeTheirs };

struct PackConflict {
    QString path;  // store path
    QString kind;  // preview, block, order, meta or file
    QString item;  // the item's name or the file's pack-relative path
    QString field; // field path inside the item (packmerge::fieldPath); empty for a whole file
    // The field's value on each side, as compact JSON (absent: empty), or the whole text.
    QByteArray base;
    QByteArray mine;
    QByteArray theirs;
};

struct PackFileMerge {
    std::optional<QByteArray> result; // nullopt: the file is removed
    QVector<PackConflict> conflicts;
};

namespace packmerge {
// Field paths: object keys and, for arrays of named elements, `@<name>`, joined with '/'
// ('~' -> "~0", '/' -> "~1" and a leading '@' -> "~2" inside a segment).
QString fieldPath( const QString& parent, const QString& segment, bool element = false );
std::optional<QJsonValue> field( const QJsonValue& root, const QString& path );
// The value with the field at `path` replaced (removed when `value` is nullopt).
QJsonValue withField( const QJsonValue& root, const QString& path,
                      const std::optional<QJsonValue>& value );

struct JsonMerge {
    std::optional<QJsonValue> value; // nullopt: removed
    QStringList conflicts;           // field paths
};
// Structured three-way merge; a conflicting field takes `mine` (Fail, KeepMine) or `theirs`.
JsonMerge mergeJson( const std::optional<QJsonValue>& base, const std::optional<QJsonValue>& mine,
                     const std::optional<QJsonValue>& theirs, MergeStrategy strategy,
                     const QString& path = {} );

struct TextMerge {
    QByteArray text;
    bool conflict = false;
};
// Line-based three-way merge (libgit2's xdiff, as `git merge-file`). With Fail a conflict leaves
// diff3 markers; KeepMine and TakeTheirs favour a side for the conflicting hunks.
TextMerge mergeText( const QByteArray& base, const QByteArray& mine, const QByteArray& theirs,
                     MergeStrategy strategy = MergeStrategy::Fail );

// One store file; nullopt: absent on that side.
PackFileMerge mergeFile( const QString& path, const std::optional<QByteArray>& base,
                         const std::optional<QByteArray>& mine,
                         const std::optional<QByteArray>& theirs,
                         MergeStrategy strategy = MergeStrategy::Fail );
} // namespace packmerge
} // namespace cilogg::plugins
