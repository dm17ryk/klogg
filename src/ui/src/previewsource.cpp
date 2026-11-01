// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewsource.h"
#include "analysisjob.h"
#include "log.h"
#include "logdata.h"
#include "previewassembly.h"
#include <QTextCodec>
#include <algorithm>

namespace {
QStringList readRows( const std::shared_ptr<LogData>& source, const SourceSnapshot& snapshot,
                      quint64 first, int count, QString* error )
{
    QStringList rows;
    std::unique_ptr<QTextDecoder> decoder;
    QString pending;
    int totalBytes = 0;
    auto next = first;
    const auto end
        = std::min<quint64>( snapshot.lineCount.get(), first + static_cast<quint64>( count ) );
    while ( next < end ) {
        AnalysisLimits limits;
        limits.batchLines = static_cast<int>( std::min<quint64>( 256, end - next ) );
        limits.batchBytes = 1024 * 1024 - totalBytes;
        limits.recordBytes = std::min<qint64>( 65536, limits.batchBytes );
        if ( limits.recordBytes <= 0 ) {
            *error = "context_limit";
            return {};
        }
        const auto block = source->readAnalysisBlock( snapshot, next, limits );
        if ( block.retry || !block.error.isEmpty() || block.nextLine <= next ) {
            *error = block.retry
                         ? "source_busy"
                         : ( block.error.isEmpty() ? QString( "context_limit" ) : block.error );
            return {};
        }
        if ( !decoder ) {
            auto* codec = QTextCodec::codecForName( block.codec );
            const auto name = block.codec.toUpper();
            // Stateful codecs need decoder checkpoints before random-row reads.
            if ( !codec || name.contains( "2022" ) || name == "UTF-7" || name.startsWith( "HZ" )
                 || ( first > 0 && ( name == "UTF-16" || name == "UTF-32" ) ) ) {
                *error = "unsupported_random_access_encoding";
                return {};
            }
            decoder.reset( codec->makeDecoder() );
        }
        totalBytes += static_cast<int>( block.bytes.size() );
        pending += decoder->toUnicode( block.bytes );
        int newline = -1;
        while ( ( newline = pending.indexOf( '\n' ) ) >= 0 ) {
            rows << pending.left( newline );
            pending.remove( 0, newline + 1 );
        }
        next = block.nextLine;
    }
    if ( !pending.isEmpty() )
        rows << pending;
    if ( rows.size() != static_cast<qint64>( end - first )
         || !source->isSourceSnapshotCurrent( snapshot ) ) {
        *error = "source_changed";
        return {};
    }
    return rows;
}
QString withoutRecordEnd( QString text )
{
    while ( text.endsWith( '\r' ) )
        text.chop( 1 );
    return text;
}
} // namespace

PreparedPreview preparePreviewText( const QString& text, std::optional<quint64> lineOneBased,
                                    bool reassemble )
{
    PreparedPreview result;
    result.source = { { "kind", "text" } };
    if ( text.size() > 1024 * 1024 || text.toUtf8().size() > 1024 * 1024 ) {
        result.error = "input_limit";
        return result;
    }
    const auto rows = text.split( '\n' );
    if ( lineOneBased
         && ( *lineOneBased == 0 || *lineOneBased > static_cast<quint64>( rows.size() ) ) ) {
        result.error = "invalid_line";
        return result;
    }
    int target = lineOneBased ? static_cast<int>( *lineOneBased - 1 ) : 0;
    if ( !lineOneBased && reassemble ) {
        for ( int i = 0; i < rows.size(); ++i )
            if ( rows[ i ].startsWith( "EHCI" ) || rows[ i ].contains( "] - EHCI" ) ) {
                target = i;
                break;
            }
    }
    result.text = lineOneBased ? withoutRecordEnd( rows[ target ] ) : text;
    if ( reassemble ) {
        const auto assembled = assemblePreviewMessage( rows, target );
        result.assembly = assembled.metadata();
        if ( assembled.state == "complete" )
            result.text = assembled.text;
    }
    return result;
}

PreparedPreview preparePreviewSource( const std::shared_ptr<LogData>& source, quint64 physicalLine,
                                      bool reassemble )
{
    PreparedPreview result;
    if ( !source ) {
        result.error = "source_not_found";
        return result;
    }
    result.validationPhysicalLine = physicalLine;
    result.snapshot = source->sourceSnapshot();
    const auto& snapshot = result.snapshot;
    if ( !snapshot.available || !snapshot.isOpen() ) {
        result.error = "source_not_ready";
        return result;
    }
    if ( physicalLine >= snapshot.lineCount.get() ) {
        result.error = "invalid_line";
        return result;
    }
    result.source = { { "kind", "source" },
                      { "sourceId", snapshot.sourceId.toString( QUuid::WithoutBraces ) },
                      { "generation", QString::number( snapshot.generation ) },
                      { "revision", QString::number( snapshot.revision ) },
                      { "indexedByteWatermark", QString::number( snapshot.indexedBytes ) },
                      { "lineOneBased", QString::number( physicalLine + 1 ) } };
    const auto selected = readRows( source, snapshot, physicalLine, 1, &result.error );
    if ( !result.error.isEmpty() || selected.isEmpty() )
        return result;
    result.text = withoutRecordEnd( selected.front() );
    if ( reassemble ) {
        const auto first = physicalLine > 511 ? physicalLine - 511 : 0;
        QString contextError;
        const auto rows = readRows( source, snapshot, first, 1024, &contextError );
        if ( contextError.isEmpty() ) {
            const auto assembled
                = assemblePreviewMessage( rows, static_cast<int>( physicalLine - first ), first );
            result.assembly = assembled.metadata();
            if ( !assembled.physicalLines.isEmpty() )
                result.validationPhysicalLine
                    = std::max( physicalLine, assembled.physicalLines.back() );
            if ( assembled.state == "complete" )
                result.text = assembled.text;
        }
        else {
            // A large unrelated adjacent record must not disable ordinary previews.
            result.assembly = { { "state", "unavailable" }, { "reason", contextError } };
            if ( result.text.contains( "EHCI" ) ) {
                result.assembly[ "state" ] = "incomplete";
                result.assembly[ "protocol" ] = "EHCI";
            }
            if ( contextError == "stale_source" || contextError == "source_changed" )
                result.error = contextError;
            LOG_DEBUG << "Preview context unavailable source=" << snapshot.sourceId
                      << " line=" << physicalLine << " reason=" << contextError;
        }
    }
    LOG_DEBUG << "Preview input frozen source=" << snapshot.sourceId
              << " generation=" << snapshot.generation << " line=" << physicalLine
              << " bytes=" << result.text.toUtf8().size()
              << " assembly=" << result.assembly.value( "state" ).toString();
    return result;
}

bool previewSourceStillCurrent( const std::shared_ptr<LogData>& source,
                                const SourceSnapshot& snapshot, quint64 physicalLine )
{
    if ( !source || !snapshot.isOpen() )
        return false;
    const auto current = source->sourceSnapshot();
    return current.sourceId == snapshot.sourceId && current.generation == snapshot.generation
           && ( physicalLine < snapshot.completeLineCount.get()
                || current.revision == snapshot.revision );
}
