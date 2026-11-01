// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sourcelocation.h"
#include <QVariantMap>
#include <memory>
class LogData;
struct PreparedPreview {
    QString text;
    QVariantMap assembly;
    QVariantMap source;
    SourceSnapshot snapshot;
    quint64 validationPhysicalLine = 0;
    QString error;
};
PreparedPreview preparePreviewSource( const std::shared_ptr<LogData>& source, quint64 physicalLine,
                                      bool reassemble = true );
PreparedPreview preparePreviewText( const QString& text, std::optional<quint64> lineOneBased = {},
                                    bool reassemble = true );
bool previewSourceStillCurrent( const std::shared_ptr<LogData>& source,
                                const SourceSnapshot& snapshot, quint64 physicalLine );
