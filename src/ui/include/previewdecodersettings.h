// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

struct PreviewDecoderSnapshot {
    QVariantMap configuration;
    QString revision;
};

// What the enabled configuration packs add to the decoder (docs/plugin-repositories.md 4.3):
// their Lua files, as `{id, files}` in pack ID order, and their routes. Never saved.
struct PreviewDecoderPacks {
    QVariantList lua;
    QVariantMap previewRoutes;
    QVariantMap protocolRoutes;
    // The packs' versions and file states: a new version whose Lua keeps its file names still
    // changes it, so sessions restart. Never sent to the worker.
    QString key;

    bool operator==( const PreviewDecoderPacks& other ) const
    {
        return lua == other.lua && previewRoutes == other.previewRoutes
               && protocolRoutes == other.protocolRoutes && key == other.key;
    }
};

// Trusted native application configuration. Never populated from preview definitions.
class PreviewDecoderSettings : public QObject {
    Q_OBJECT
public:
    // The profile limit (design 4.3): the user's 8 scripts and the packs' files together.
    static constexpr int ProfileLuaLimit = 32;
    static constexpr int PreviewRouteLimit = 128;
    static constexpr int ProtocolRouteLimit = 32;

    static PreviewDecoderSettings& instance();
    // The user's saved configuration, without pack contributions.
    QVariantMap configuration() const;
    bool setConfiguration( const QVariantMap& configuration, QString* error = nullptr );
    // Whether setConfiguration would take `configuration`, without saving it.
    bool check( const QVariantMap& configuration, QString* error = nullptr ) const;
    QString revision() const;
    // The configuration a session uses: the user's, with the packs' routes merged below the
    // user's routes and the packs' Lua under `packLua`.
    PreviewDecoderSnapshot snapshot() const;
    PreviewDecoderPacks packs() const;
    // Replaces the pack contributions; a change discards the warm TShark sessions.
    void setPacks( const PreviewDecoderPacks& packs );
Q_SIGNALS:
    void changed();

private:
    PreviewDecoderSettings();
    void invalidate();
    void watchFiles();
    PreviewDecoderPacks packs_;
    QVariantMap configuration_;
    QVariantMap fileMetadata_;
    QStringList watchedFiles_;
    QString revision_;
    QFileSystemWatcher watcher_;
    QTimer debounce_;
};
