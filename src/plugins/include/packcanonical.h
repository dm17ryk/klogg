#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

// The canonical form of a configuration pack in its local store (docs/plugin-repositories.md
// 4.6): one item per file, JSON with sorted keys and one member per line, so unrelated edits
// never touch the same lines. It converts to and from the pack's file formats without loss.
//
//   previews/order.txt         preview names in evaluation order, one per line
//   previews/<name>.json       one preview
//   previews/meta.json         other top-level keys of previews.json (version, $schema, ...)
//   blocks/<name>.json         one block
//   filters/order.txt          predefined filter ids in list order, one per line
//   filters/<id>.json          one predefined filter: filter, id, name, regex
//   files/<path>               Lua and Python files, as they are, at their pack-relative path
//
// <name> and <id> are percent-encoded: every character outside [a-z0-9._-] (upper case included,
// so names that differ only in case never share a file on case-insensitive file systems).
namespace cilogg::plugins {
using PackFiles = QMap<QString, QByteArray>; // store-relative path -> bytes

namespace packcanonical {
// The preview limits (docs/plugin-repositories.md 4.2) that a pack must fit to be enabled at all.
constexpr qsizetype MaxPackPreviewItems = 128;
constexpr qsizetype MaxPackBlockItems = 4096;
// The filters a pack can have: as many as a configuration import takes.
constexpr qsizetype MaxPackFilterItems = 4096;

QString encodeName( const QString& name );
QString decodeName( const QString& encoded );
// Canonical JSON text of one item (sorted keys, indented, LF line ends).
QByteArray itemBytes( const QJsonObject& item );

// previews.json -> canonical files; `error` is set when the document is not a previews document.
PackFiles fromPreviews( const QByteArray& previewsJson, QString* error = nullptr );
// Canonical files -> previews.json (previews in the order of order.txt; previews missing from it
// follow in name order; blocks in name order).
QByteArray toPreviews( const PackFiles& files );

// A predefined filters file as CILogg exports it (INI, group PredefinedFiltersCollection, version
// 1 to 3) -> canonical files; `error` is set when it is not exactly such a file. A filter's
// identity is its id, so every filter needs a unique one, and the file holds nothing else.
PackFiles fromFilters( const QByteArray& filtersConf, QString* error = nullptr );
// Canonical files -> the filters file (version 3; filters in the order of filters/order.txt,
// filters missing from it follow in id order). Empty, with `error` set, if it cannot be written.
QByteArray toFilters( const PackFiles& files, QString* error = nullptr );

// A whole pack: its previews and filters documents and the files the manifest lists (Lua and
// Python, and actions and highlights for now), read from the installed folder.
PackFiles fromPackFolder( const QJsonObject& manifest, const QString& directory,
                          QString* error = nullptr );
// The store-relative path of a pack file such as "lua/common.lua".
QString filePath( const QString& packRelative );
// The store paths of the Lua and Python files a (valid) manifest lists: exactly the files of the
// pack, so the files of a merge result follow the new version's manifest.
QStringList listedFiles( const QJsonObject& manifest );
// True for a store path in its one canonical spelling: a known kind, an item name encoded as
// encodeName() writes it, a file path that is relative, clean and stays inside the pack.
bool isCanonical( const QString& storePath );
// The kind of a store path: "preview", "block", "order", "meta", "filter", "filter-order", "file"
// or "" (not content).
QString kindOf( const QString& storePath );
// The item name or pack-relative file path a store path stands for.
QString itemOf( const QString& storePath );
} // namespace packcanonical
} // namespace cilogg::plugins
