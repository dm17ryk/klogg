/*
 * Copyright (C) 2026 CILogg contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SOURCELOCATION_H
#define SOURCELOCATION_H

#include <atomic>
#include <memory>
#include <optional>
#include <utility>

#include <QUuid>

#include "linetypes.h"

// Offsets in decoded raw QString text, before tabs, ANSI or annotations are rendered.
// Both start and length count UTF-16 code units. Physical line numbers are zero-based.
struct SourceSpan {
    quint64 start = 0;
    quint64 length = 0;
};

class SourceLocation {
public:
    const QUuid& sourceId() const
    {
        return sourceId_;
    }
    quint64 generation() const
    {
        return generation_;
    }
    quint64 revision() const
    {
        return revision_;
    }
    LineNumber line() const
    {
        return line_;
    }
    bool provisional() const
    {
        return provisional_;
    }

    // Captures can add/change their span; accepts() validates it before use.
    std::optional<SourceSpan> span;

private:
    SourceLocation( QUuid sourceId, quint64 generation, quint64 revision, LineNumber line,
                    bool provisional, std::optional<SourceSpan> sourceSpan )
        : span( std::move( sourceSpan ) )
        , sourceId_( std::move( sourceId ) )
        , generation_( generation )
        , revision_( revision )
        , line_( line )
        , provisional_( provisional )
    {
    }

    QUuid sourceId_;
    quint64 generation_;
    quint64 revision_;
    LineNumber line_;
    bool provisional_;

    friend struct SourceSnapshot;
};

// Weak ownership lets results detect close without keeping LogData/files alive.
struct SourceLifetime {
    std::atomic<bool> closed{ false };
};

struct SourceSnapshot {
    QUuid sourceId;
    quint64 generation = 0;
    quint64 revision = 0;
    qint64 indexedBytes = 0;
    LinesCount lineCount = 0_lcount;
    LinesCount completeLineCount = 0_lcount;
    std::optional<qint64> provisionalLineStart;
    bool available = false;
    std::weak_ptr<const SourceLifetime> lifetime;

    bool isOpen() const;
    bool sameRevision( const SourceSnapshot& other ) const;
    std::optional<SourceLocation> location( LineNumber line,
                                            std::optional<SourceSpan> span = {} ) const;
    bool accepts( const SourceLocation& location ) const;
};

#endif
