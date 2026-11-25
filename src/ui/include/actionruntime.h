#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVariantMap>

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
// Who asked for a send. Every caller states it: the action's initial delay applies only to
// automatic sends.
enum class ActionSendOrigin {
    // The user or a client asked for it (Send button, menu, parameter dialog, Commander).
    Manual,
    // A response scheduled it.
    Automatic
};

// How long the first send of `action` waits. A manual send goes out at once; only automatic
// sends (responses) wait for the action's initial delay.
int initialSendDelayMs( const ActionDefinition& action, ActionSendOrigin origin );
bool sendActionDefinition( StreamSession* session, const ActionDefinition& action,
                           ActionSendOrigin origin, const QVariantMap& parameters = {},
                           int stepIndex = -1, QString* errorMessage = nullptr );
bool executeResponseDefinition( StreamSession* session,
                                const ResponseDefinition& response,
                                const QMap<QString, QString>& captures = {},
                                QString* errorMessage = nullptr );
