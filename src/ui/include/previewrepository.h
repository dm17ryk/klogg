#pragma once

#include <QByteArray>
#include <QString>

#include "previewconfigparser.h"

class PreviewRepository {
public:
    PreviewParseResult load() const;
    bool save( const QVector<PreviewDefinition>& previews,
               const QMap<QString, PreviewFieldSpec>& blocks ) const;
    static QByteArray serialize( const QVector<PreviewDefinition>& previews,
                                 const QMap<QString, PreviewFieldSpec>& blocks );

private:
    QString storagePath() const;
};
