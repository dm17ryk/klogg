#pragma once

#include <QMap>
#include <QStringList>
#include <QVector>

#include "actionsconfigparser.h"
#include "highlighterset.h"
#include "predefinedfilters.h"
#include "previewconfigparser.h"
#include "sessioninfo.h"

enum class ConfigurationExportKind {
    Actions,
    Highlights,
    Previews,
    PredefinedFilters,
    Preferences
};

// A validated snapshot. Reading a project never changes live settings or managers.
struct ConfigurationProject {
    SessionInfo session;
    QMap<QString, int> activeTabs;
    QMap<QString, QVariant> preferences;
    HighlighterSetCollection highlights;
    PredefinedFiltersCollection filters;
    ActionsParseResult actions;
    PreviewParseResult previews;
    QMap<QString, QString> exports;
};

namespace ConfigurationExport {
QString title( ConfigurationExportKind kind );
QStringList names( ConfigurationExportKind kind, const ConfigurationProject* snapshot = nullptr );
bool captureConfiguration( ConfigurationProject* snapshot, QString* error );
QString fileFilter( ConfigurationExportKind kind );
QString suffix( ConfigurationExportKind kind );
bool writeFile( const QString& path, const QByteArray& bytes, QString* error );
bool exportSelected( ConfigurationExportKind kind, const QVector<int>& rows, const QString& path,
                     QString* error, const HighlighterSetCollection* highlights = nullptr,
                     const ConfigurationProject* snapshot = nullptr );
bool exportHighlightFiles( const HighlighterSetCollection& highlights, const QVector<int>& rows,
                           const QString& directory, QString* error,
                           QStringList* savedFiles = nullptr );
bool saveProject( const QString& path, const SessionInfo& session,
                  const QMap<QString, int>& activeTabs, const QMap<QString, QString>& exports,
                  QString* error );
bool readProject( const QString& path, ConfigurationProject* project, QString* error );
bool applyProjectConfiguration( const ConfigurationProject& project, QString* error );
} // namespace ConfigurationExport
