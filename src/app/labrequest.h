#pragma once

#include <QString>

enum class LabCliMode {
    None,
    ControllerServe,
    AgentRun,
    Submit,
    Queue,
    Status,
    Cancel,
    Agents,
    Artifacts,
};

struct LabCliRequest {
    LabCliMode mode = LabCliMode::None;
    QString controllerUrl;
    QString listenAddress;
    quint16 listenPort = 0;
    QString stateDirPath;
    QString tokenFilePath;
    QString agentConfigPath;
    QString suiteFilePath;
    QString scenarioFilePath;
    QString argsJsonFilePath;
    QString reportDirPath;
    QString outputDirPath;
    QString agentLabel;
    QString jobId;
    bool prettyOutput = false;
};
