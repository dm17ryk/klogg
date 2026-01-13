#pragma once

#include <QByteArray>
#include <QString>

#include "actionsconfig.h"

class ActionsConfigParser {
  public:
    ActionsParseResult parseFile( const QString& path ) const;
    ActionsParseResult parseJson( const QByteArray& jsonBytes ) const;
};
