// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysisjob.h"
#include "log.h"

std::optional<SourceLocation> AnalysisLine::sourceLocation( std::optional<SourceSpan> span ) const
{
    if ( !provenance ) {
        LOG_DEBUG << "Analysis location absent reason=no_physical_origin line=" << line;
        return {};
    }
    const auto& origin = *provenance;
    const auto& snapshot = origin.snapshot;
    const auto units = static_cast<quint64>( text.size() );
    const auto terminatorSize = static_cast<quint64>( origin.terminatorBytes.size() );
    if ( snapshot.indexedBytes < 0 || line >= snapshot.lineCount.get()
         || origin.bytes.begin >= origin.bytes.endExclusive
         || origin.bytes.endExclusive > static_cast<quint64>( snapshot.indexedBytes )
         || terminatorSize > 8 || terminatorSize > origin.bytes.endExclusive - origin.bytes.begin
         || origin.encoding.isEmpty()
         || origin.provisional != ( line >= snapshot.completeLineCount.get() )
         || origin.provisional != origin.terminatorBytes.isEmpty() ) {
        LOG_DEBUG << "Analysis location rejected reason=invalid_provenance line=" << line
                  << " begin=" << origin.bytes.begin << " end=" << origin.bytes.endExclusive
                  << " watermark=" << snapshot.indexedBytes;
        return {};
    }
    if ( span && ( span->start > units || span->length > units - span->start ) ) {
        LOG_DEBUG << "Analysis location rejected reason=invalid_utf16_span line=" << line
                  << " units=" << units << " start=" << span->start << " length=" << span->length;
        return {};
    }
    const auto location
        = snapshot.location( LineNumber{ static_cast<LineNumber::UnderlyingType>( line ) }, span );
    LOG_DEBUG << "Analysis location source=" << snapshot.sourceId.toString()
              << " generation=" << snapshot.generation << " revision=" << snapshot.revision
              << " line=" << line << " provisional=" << origin.provisional
              << " accepted=" << location.has_value();
    return location;
}
