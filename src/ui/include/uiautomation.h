#pragma once

#include <QList>

#include "commander.h"

class QWidget;
class QObject;

void queueUiActivation( QObject* object, const QString& keySequence = {},
                        const QVariantMap& gesture = {} );

// Extra roots are the owner's unparented auxiliary windows. Selectors are objectName
// (when unique) or an objectPath obtained from the latest get_ui result.
CommanderResult executeUiAutomation( QWidget* owner, const CommanderRequest& request,
                                     const QList<QWidget*>& extraRoots = {} );
