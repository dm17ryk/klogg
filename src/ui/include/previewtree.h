// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "previewconfig.h"
#include <QByteArray>
class QTreeWidgetItem;
struct PreviewPreparedBuffer {
    bool ok = false;
    QByteArray bytes;
    int initialOffset = 0;
    int bufferBaseOffset = 0;
    int matchStart = 0;
    QString source;
    QString error;
    QString reason;
    QRegularExpressionMatch match;
};
PreviewPreparedBuffer preparePreviewInput( const QString& text, const QString& encoding );
PreviewPreparedBuffer preparePreviewBuffer( const QString& rawLine,
                                            const PreviewDefinition& definition,
                                            const QString& encodingOverride = {} );
struct PreviewTreeBudget {
    int remaining = 512;
    bool limited = false;
    bool take()
    {
        if ( remaining <= 0 ) {
            limited = true;
            return false;
        }
        --remaining;
        return true;
    }
};
// Caller owns the returned data tree. No widget or GUI application is created.
QTreeWidgetItem* createLegacyPreviewTree( const QString& rawLine,
                                          const PreviewDefinition& definition,
                                          const QMap<QString, PreviewFieldSpec>& blocks,
                                          QString* error, PreviewTreeBudget* budget = nullptr );
