// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sourcelocation.h"
#include <QByteArray>

// Half-open offsets in original source bytes, never decoded/re-encoded text.
struct RawByteRange {
    quint64 begin = 0;
    quint64 endExclusive = 0;
};

struct AnalysisRawRecord {
    // Complete physical record, including any BOM and physical terminator.
    RawByteRange bytes;
    QByteArray terminatorBytes;
    bool provisional = false;
};

struct AnalysisLineProvenance {
    SourceSnapshot snapshot;
    RawByteRange bytes;
    QByteArray terminatorBytes;
    QByteArray encoding;
    bool provisional = false;
};
