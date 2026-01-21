#pragma once

#include <QString>

#include "actionsconfig.h"

class ActionsRepository {
  public:
    ActionsParseResult load() const;
    bool save( const QVector<ActionDefinition>& actions,
               const QVector<ResponseDefinition>& responses ) const;

  private:
    QString storagePath() const;
};
