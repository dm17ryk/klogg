// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_ANALYSIS_REGEX_H
#define CILOGG_ANALYSIS_REGEX_H
#include "analysisjob.h"

// An optional executable/argv override supports isolated worker-protocol fixtures.
// Commander never accepts executable overrides from analysis JSON requests.
AnalysisProcessorFactory makeRegexProcessor( QVariantMap options, QString executable = {},
                                             QStringList arguments = {} );
#endif
