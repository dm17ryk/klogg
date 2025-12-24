#pragma once

#include <QString>

#include "previewconfigparser.h"

class PreviewRepository {
  public:
    PreviewParseResult load() const;
    bool save( const QVector<PreviewDefinition>& previews ) const;

  private:
    QString storagePath() const;
};
