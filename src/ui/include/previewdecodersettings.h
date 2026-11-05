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

// Trusted native application configuration. Never populated from preview definitions.
class PreviewDecoderSettings : public QObject {
    Q_OBJECT
public:
    static PreviewDecoderSettings& instance();
    QVariantMap configuration() const;
    bool setConfiguration( const QVariantMap& configuration, QString* error = nullptr );
    QString revision() const;
    PreviewDecoderSnapshot snapshot() const;
Q_SIGNALS:
    void changed();

private:
    PreviewDecoderSettings();
    void invalidate();
    void watchFiles();
    QVariantMap configuration_;
    QVariantMap fileMetadata_;
    QStringList watchedFiles_;
    QString revision_;
    QFileSystemWatcher watcher_;
    QTimer debounce_;
};
