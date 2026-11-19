// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QIcon>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QStringList>

namespace cilogg::plugins {
class PluginManager;
}

// Plugin icon references and the images plugins register with `ui/icons`
// (docs/plugin-sdk-advanced.md section 6). One registry serves every window; a plugin's images
// live until it stops.
class PluginIconRegistry final : public QObject {
    Q_OBJECT
public:
    static constexpr int MaxIcons = 256;
    static constexpr int MaxIconBytes = 64 * 1024;
    static constexpr int MaxIconTotalBytes = 4 * 1024 * 1024;
    static constexpr int MaxIconPixels = 256;
    static constexpr int MaxPictures = 16;
    static constexpr int MaxPictureBytes = 1024 * 1024;
    static constexpr int MaxPicturePixels = 4096;
    // Decoded images of one plugin (icons and pictures): a 1 MiB PNG can decode to 64 MiB.
    static constexpr qsizetype MaxDecodedBytes = qsizetype( 128 ) * 1024 * 1024;
    static constexpr int IconSize = 16;

    static PluginIconRegistry& instance();

    // Applies the manager's `ui/icons` notifications and forgets a plugin's images when it stops.
    // Every window calls it; a manager is connected once.
    void attach( cilogg::plugins::PluginManager* manager );

    // Applies a `ui/icons` notification. Returns one message per refused entry (the others are
    // applied).
    QStringList apply( const QString& pluginId, const QJsonObject& params );
    // The plugin stopped: its images are gone.
    void clear( const QString& pluginId );
    int iconCount( const QString& pluginId ) const;
    int pictureCount( const QString& pluginId ) const;
    // Increases whenever the plugin's images change (menus and toolbars rebuild their icons).
    quint64 generation( const QString& pluginId ) const;

    // The icon a reference names; `fallback` when it cannot be used (logged once per plugin and
    // reference). `directory` is the plugin folder for file references.
    QIcon icon( const QString& pluginId, const QString& directory, const QJsonValue& reference,
                const QIcon& fallback = {} );
    // The icon as an <img> element with a data: URL, for host-built rich text (tooltips). Empty
    // when the reference cannot be used.
    QString iconHtml( const QString& pluginId, const QString& directory,
                      const QJsonValue& reference );
    // A registered picture (`"picture": true`), for the image element.
    QImage picture( const QString& pluginId, const QString& id ) const;

    // SVG text that refers to anything but data: URIs (or fragments of itself) is refused.
    static bool safeSvg( const QByteArray& svg );
    static bool darkTheme();

Q_SIGNALS:
    void changed( const QString& pluginId );

private:
    PluginIconRegistry() = default;
    struct Image {
        QImage image;
        qsizetype bytes = 0;
        bool mask = false;
    };
    struct PluginImages {
        QHash<QString, Image> icons;
        QHash<QString, Image> pictures;
        qsizetype iconBytes = 0;
        qsizetype decodedBytes = 0;
        quint64 generation = 0;
    };
    QIcon resolve( const QString& pluginId, const QString& directory, const QString& reference );
    QIcon load( const QString& pluginId, const QString& directory, const QString& reference );
    QIcon builtin( const QString& name ) const;
    static QIcon statusDot( const QString& tone );
    void warnOnce( const QString& pluginId, const QString& reference, const QString& reason );

    QHash<QString, PluginImages> images_;
    // Resolved icons per plugin (null ones too), keyed by directory, reference and palette:
    // tables ask for icons on every paint, and a file icon would otherwise be read and checked
    // each time. Dropped when the plugin's images change or it stops; at most MaxResolved each.
    QHash<QString, QHash<QString, QIcon>> resolved_;
    static constexpr int MaxResolved = 512;
    QSet<QString> warned_;
    QSet<QObject*> attached_;
};
