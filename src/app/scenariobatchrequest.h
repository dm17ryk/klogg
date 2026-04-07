#pragma once

#include <QString>

enum class ScenarioBatchAction {
    None,
    Run,
    Validate,
    ListDevices,
};

struct ScenarioBatchRequest {
    ScenarioBatchAction action = ScenarioBatchAction::None;
    QString scenarioFilePath;
    QString suiteFilePath;
    QString argsJsonFilePath;
    QString deviceMapFilePath;
    QString reportDirPath;
};
