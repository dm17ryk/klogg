// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "previewconfig.h"
class QTreeWidgetItem;
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
