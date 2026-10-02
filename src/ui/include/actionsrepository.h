#pragma once

#include <QByteArray>
#include <QString>

#include "actionsconfig.h"

class ActionsRepository {
public:
    ActionsParseResult load() const;
    bool save( const QVector<ActionDefinition>& actions,
               const QVector<ResponseDefinition>& responses ) const;
    static QByteArray serialize( const QVector<ActionDefinition>& actions,
                                 const QVector<ResponseDefinition>& responses );

private:
    QString storagePath() const;
};
