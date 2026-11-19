#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

// Configuration packs (E16, docs/plugin-repositories.md section 4): plugin packages with
// `"kind": "config-pack"` that carry configuration instead of a program.
namespace cilogg::plugins {
inline constexpr char ConfigPackKind[] = "config-pack";
constexpr int PackFormat = 1;
// Lua for TShark (design 4.3): at most 8 files of 1 MiB per pack. The other content files use
// the same 1 MiB bound.
constexpr int MaxPackLuaFiles = 8;
constexpr qint64 MaxPackFileBytes = 1024 * 1024;
// The decoder's own route limits (PreviewDecoderSettings).
constexpr int MaxPackPreviewRoutes = 128;
constexpr int MaxPackProtocolRoutes = 32;
constexpr int MaxPackPythonDecoders = 32;

struct PackPythonDecoder {
    QString id;
    QString module;
};

// The `content` object of a valid pack manifest. Paths are relative to the package folder,
// Lua files relative to `luaFolder`.
struct PackContent {
    QString previews;
    QString actions;
    QString highlights;
    QString filters;
    QString luaFolder;
    QStringList luaLoad;
    QJsonObject previewRoutes;
    QJsonObject protocolRoutes;
    QVector<PackPythonDecoder> pythonDecoders;

    // Absolute Lua file paths of a pack installed in `directory`, in loading order.
    QStringList luaFiles( const QString& directory ) const;
};

bool isConfigPack( const QJsonObject& manifest );
// Keys a pack must not declare at the top level: a pack has no process and no settings.
QStringList packProcessKeys();
// The pack-specific manifest rules (pack_format, process keys, `content`), without file
// access. Returns the error, or an empty string and fills `content`.
QString validatePackManifest( const QJsonObject& manifest, PackContent* content = nullptr );
// The files of a pack in `directory`: every listed file exists (no links) within its size
// bound, every Lua file of the Lua folder is listed, and preview routes name the pack's own
// previews.
QString validatePackFiles( const QJsonObject& manifest, const QString& directory );
// Catalog `contents` types of a pack, in the order of the catalog format.
QStringList packContents( const QJsonObject& manifest );
// The preview names of a previews.json document (an object with `previews`, or a bare array).
QStringList packPreviewNames( const QByteArray& previewsJson );
// The `get_plugins` pack fields: contents and the counts that E16a knows.
QVariantMap packSummary( const QJsonObject& manifest, const QString& directory );
} // namespace cilogg::plugins
