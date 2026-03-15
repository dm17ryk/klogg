#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

#include "actionsconfig.h"

class StreamSession;

struct ResponseMatchResult {
    bool matched = false;
    QMap<QString, QString> captures;
    QString lineText;
};

ResponseMatchResult matchResponseDefinition( const ResponseDefinition& response,
                                            const QByteArray& lineBytes,
                                            const QString& lineText = {} );
bool sendActionDefinition( StreamSession* session,
                           const ActionDefinition& action,
                           const QMap<QString, QString>& substitutions = {},
                           int stepIndex = -1,
                           QString* errorMessage = nullptr );
bool executeResponseDefinition( StreamSession* session,
                                const ResponseDefinition& response,
                                const QMap<QString, QString>& captures = {},
                                QString* errorMessage = nullptr );
