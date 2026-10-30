// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_ANALYSISSTATUS_H
#define CILOGG_ANALYSISSTATUS_H
#include <QCoreApplication>
#include <QVariantMap>

inline QString analysisWorkerMemoryText( const QVariantMap& metrics )
{
    const auto memory = metrics.value( "workerMemory" ).toMap();
    const auto status = memory.value( "status" ).toString();
    if ( status == "measured" || status == "partial" ) {
        return QCoreApplication::translate( "AnalysisWorkerMemory",
                                            "Worker peak RSS: %1 bytes (%2)" )
            .arg(
                memory.value( "peakRssBytes" ).toString(),
                status == "partial"
                    ? QCoreApplication::translate( "AnalysisWorkerMemory", "partial observations" )
                    : QCoreApplication::translate( "AnalysisWorkerMemory", "observed" ) );
    }
    if ( status == "unsupported" )
        return QCoreApplication::translate( "AnalysisWorkerMemory",
                                            "Worker peak RSS: unsupported" );
    if ( status == "unavailable" )
        return QCoreApplication::translate( "AnalysisWorkerMemory",
                                            "Worker peak RSS: unavailable" );
    return QCoreApplication::translate( "AnalysisWorkerMemory", "Worker peak RSS: not reported" );
}
#endif
