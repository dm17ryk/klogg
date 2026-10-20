// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_ANALYSIS_COMMANDS_H
#define CILOGG_ANALYSIS_COMMANDS_H
#include "analysisjob.h"
#include <optional>

struct AnalysisCommandResult {
    bool ok = false;
    QVariantMap payload;
};

// Shared JSON contract. Widgets supply only a frozen source/scope descriptor.
AnalysisCommandResult executeAnalysisOperation( AnalysisJobService& service,
                                                const QString& operation,
                                                const QVariantMap& arguments,
                                                std::optional<AnalysisRequest> source = {},
                                                QString workerExecutable = {} );
AnalysisJobService* applicationAnalysisService( bool create = true );
// Inclusive view-row ranges mapped through a strictly increasing physical-row index.
// Binary searches skip long contiguous runs without expanding millions of rows.
std::optional<std::vector<std::pair<quint64, quint64>>>
freezeAnalysisRanges( const std::vector<std::pair<quint64, quint64>>& ranges,
                      const std::function<quint64( quint64 )>& physicalRow, QString* error );
#endif
