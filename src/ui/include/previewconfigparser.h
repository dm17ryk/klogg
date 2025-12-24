#pragma once

#include <QByteArray>
#include <QStringList>
#include <QVector>

#include "previewconfig.h"

struct PreviewParseResult {
    QVector<PreviewDefinition> previews;
    QStringList errors;
    QStringList warnings;
};

class PreviewConfigParser {
  public:
    PreviewParseResult parseFile( const QString& path ) const;
    PreviewParseResult parseJson( const QByteArray& jsonBytes ) const;
};
