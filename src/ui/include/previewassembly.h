// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>
#include <QVariantMap>
#include <QVector>

struct PreviewAssembly {
    QString state = QStringLiteral( "unavailable" );
    QString reason;
    QString protocol;
    QString text;
    QString flow;
    QVector<quint64> physicalLines;
    qint64 expectedBytes = 0;
    qint64 receivedBytes = 0;
    int excludedPrefixRows = 0;
    QVariantMap metadata() const;
};

// A bounded physical-row snapshot of CP NET UART diagnostics, not a TCP decoder.
PreviewAssembly assemblePreviewMessage( const QStringList& lines, int targetIndex,
                                        quint64 firstPhysicalLine = 0 );
