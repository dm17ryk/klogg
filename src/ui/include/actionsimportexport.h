#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "actionsconfig.h"

enum class ActionsImportFormat { Auto, Json, DocklightPtp };
enum class ActionsConflictPolicy { Fail, KeepExisting, UseImported };

struct ActionsImportConflict {
    QString itemType;
    QString matchType;
    int existingId = -1;
    int importedId = -1;
    QString existingName;
    QString importedName;
    QVariantMap existingDefinition;
    QVariantMap importedDefinition;
};

struct ActionsConfigMergeResult {
    bool ok = false;
    QVector<ActionDefinition> actions;
    QVector<ResponseDefinition> responses;
    int added = 0;
    int updated = 0;
    int skipped = 0;
    QVector<ActionsImportConflict> conflicts;
    QStringList warnings;
    QStringList errors;
};

QString actionsImportFormatToString( ActionsImportFormat format );
ActionsImportFormat actionsImportFormatFromString( const QString& value, bool* ok = nullptr );
QString actionsConflictPolicyToString( ActionsConflictPolicy policy );
ActionsConflictPolicy actionsConflictPolicyFromString( const QString& value,
                                                       bool* ok = nullptr );

QJsonObject actionsConfigToJsonObject( const QVector<ActionDefinition>& actions,
                                       const QVector<ResponseDefinition>& responses );
QByteArray actionsConfigToJson( const QVector<ActionDefinition>& actions,
                                const QVector<ResponseDefinition>& responses,
                                bool pretty );
bool writeActionsConfigFile( const QString& path,
                             const QVector<ActionDefinition>& actions,
                             const QVector<ResponseDefinition>& responses,
                             QString* errorMessage = nullptr,
                             bool pretty = true );

ActionsParseResult parseActionsConfigFile( const QString& path, ActionsImportFormat format );
ActionsParseResult parseDocklightPtpFile( const QString& path );

ActionsConfigMergeResult mergeActionsConfig( const QVector<ActionDefinition>& existingActions,
                                             const QVector<ResponseDefinition>& existingResponses,
                                             QVector<ActionDefinition> importedActions,
                                             QVector<ResponseDefinition> importedResponses,
                                             ActionsConflictPolicy conflictPolicy );

QStringList actionConflictSummaries( const QVector<ActionsImportConflict>& conflicts );
QVariantList actionConflictsToVariantList( const QVector<ActionsImportConflict>& conflicts );
