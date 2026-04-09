#pragma once

#include <QByteArray>
#include <QVariantMap>

#include "commander.h"

struct LabLogicalDeviceDefinition {
    QString name;
    QStringList capabilityTags;
    CommanderComSettings settings;
};

struct LabAgentPortDefinition {
    QString portName;
    QString displayName;
    QStringList capabilityTags;
    QStringList labels;
    CommanderComSettings settings;
};

struct LabAgentConfig {
    QString agentId;
    QString displayName;
    QStringList labels;
    QList<LabAgentPortDefinition> ports;
};

struct LabBundleFile {
    QString relativePath;
    QByteArray content;
};

struct LabJobBundle {
    QString kind;
    QString suiteId;
    QString suiteName;
    QString suiteFile;
    QString scenarioFile;
    QString argsJsonFile;
    QStringList requiredDevices;
    QString agentLabel;
    QList<LabLogicalDeviceDefinition> logicalDevices;
    QList<LabBundleFile> files;
};

struct LabResolvedBinding {
    QString logicalName;
    QString portName;
    QString displayName;
    QStringList capabilityTags;
    CommanderComSettings settings;
};

struct LabStoredArtifact {
    QString name;
    QString path;
};

QVariantMap commanderComSettingsToVariantMap( const CommanderComSettings& settings );
CommanderComSettings commanderComSettingsFromVariantMap( const QVariantMap& map );

QVariantMap labLogicalDeviceToVariantMap( const LabLogicalDeviceDefinition& device );
LabLogicalDeviceDefinition labLogicalDeviceFromVariantMap( const QVariantMap& map );

QVariantMap labAgentPortToVariantMap( const LabAgentPortDefinition& port );
LabAgentPortDefinition labAgentPortFromVariantMap( const QVariantMap& map );

QVariantMap labAgentConfigToVariantMap( const LabAgentConfig& config );
LabAgentConfig labAgentConfigFromVariantMap( const QVariantMap& map );

QVariantMap labBundleFileToVariantMap( const LabBundleFile& file );
LabBundleFile labBundleFileFromVariantMap( const QVariantMap& map );

QVariantMap labJobBundleToVariantMap( const LabJobBundle& bundle );
LabJobBundle labJobBundleFromVariantMap( const QVariantMap& map );

QVariantMap labResolvedBindingToVariantMap( const LabResolvedBinding& binding );
LabResolvedBinding labResolvedBindingFromVariantMap( const QVariantMap& map );

QVariantMap labStoredArtifactToVariantMap( const LabStoredArtifact& artifact );
LabStoredArtifact labStoredArtifactFromVariantMap( const QVariantMap& map );
