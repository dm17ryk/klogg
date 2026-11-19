#pragma once
#include "pluginmanifest.h"
#include <QSettings>

namespace cilogg::plugins {
struct StagedPlugin {
    QString directory;
    Manifest manifest;
    QString error;
};

class PluginRepository {
public:
    static QString defaultDataRoot();
    static QString dataRootForSettings( const QSettings& settings );
    static QString shippedRoot();
    static StagedPlugin stage( const QString& source, const QString& installRoot );
    // The plugin.json at the root of a package folder or ZIP, read without unpacking it (at most
    // 1 MiB); empty when there is none. For questions asked before an install, never trusted.
    static QJsonObject packageManifest( const QString& source );
    static QString commit( const StagedPlugin& stage, const QString& installRoot );
    static QString remove( const QString& id, const QString& installRoot );
};
} // namespace cilogg::plugins
