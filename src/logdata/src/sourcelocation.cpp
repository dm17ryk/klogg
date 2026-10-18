/*
 * Copyright (C) 2026 CILogg contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sourcelocation.h"

#include <limits>

bool SourceSnapshot::isOpen() const
{
    const auto source = lifetime.lock();
    return !sourceId.isNull() && source && !source->closed.load();
}

bool SourceSnapshot::sameRevision( const SourceSnapshot& other ) const
{
    return available && other.available && isOpen() && other.isOpen() && sourceId == other.sourceId
           && generation == other.generation && revision == other.revision;
}

std::optional<SourceLocation> SourceSnapshot::location( LineNumber line,
                                                        std::optional<SourceSpan> span ) const
{
    if ( !available || !isOpen() || line >= lineCount
         || ( span && span->length > std::numeric_limits<quint64>::max() - span->start ) ) {
        return {};
    }
    return SourceLocation{ sourceId, generation, revision, line, line >= completeLineCount, span };
}

bool SourceSnapshot::accepts( const SourceLocation& location ) const
{
    return available && isOpen() && sourceId == location.sourceId
           && generation == location.generation && location.revision <= revision
           && location.line < lineCount
           && ( !location.provisional || location.revision == revision );
}
