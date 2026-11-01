// SPDX-License-Identifier: GPL-3.0-or-later
#include "crawlerwidget.h"
#include "log.h"
#include "logdata.h"
#include "mainwindow.h"
#include "previewservice.h"
#include "previewsource.h"
#include <QPointer>
#include <QRegularExpression>
#include <QSet>

CommanderResult MainWindow::commanderPreview( const CommanderRequest& request )
{
    const auto invalid = []( const QString& reason ) {
        LOG_DEBUG << "Preview command rejected reason=" << reason;
        return CommanderResult{ CommanderResultCode::InvalidRequest,
                                reason,
                                { { "reason", reason } } };
    };
    const auto& args = request.analysisArguments;
    const QSet<QString> allowed{ "text", "line", "previewName", "reassemble", "timeoutMs" };
    for ( auto it = args.cbegin(); it != args.cend(); ++it )
        if ( !allowed.contains( it.key() ) )
            return invalid( "unknown_preview_option" );
    std::optional<quint64> line;
    if ( args.contains( "line" ) ) {
        const auto text = args.value( "line" ).toString();
        static const QRegularExpression number( "^[1-9][0-9]{0,15}$" );
        if ( !number.match( text ).hasMatch()
             || args.value( "line" ).userType() == QMetaType::Bool )
            return invalid( "invalid_line" );
        line = text.toULongLong();
    }
    if ( args.contains( "reassemble" ) && args.value( "reassemble" ).userType() != QMetaType::Bool )
        return invalid( "invalid_reassemble" );
    const bool reassemble = args.value( "reassemble", true ).toBool();
    if ( args.contains( "previewName" )
         && args.value( "previewName" ).userType() != QMetaType::QString )
        return invalid( "invalid_preview_name" );
    int timeoutMs = 3000;
    if ( args.contains( "timeoutMs" ) ) {
        bool ok = false;
        timeoutMs = args.value( "timeoutMs" ).toString().toInt( &ok );
        if ( !ok || timeoutMs < 100 || timeoutMs > 10000 )
            return invalid( "invalid_timeout" );
    }
    PreparedPreview prepared;
    QPointer<CrawlerWidget> crawler;
    std::weak_ptr<LogData> source;
    if ( args.contains( "text" ) ) {
        if ( args.value( "text" ).userType() != QMetaType::QString )
            return invalid( "invalid_text" );
        prepared = preparePreviewText( args.value( "text" ).toString(), line, reassemble );
    }
    else {
        if ( !line )
            return invalid( "line_required" );
        crawler = commanderTargetCrawler( request );
        if ( !crawler )
            return invalid( "source_not_found" );
        QString error;
        auto selected = crawler->analysisRequest( "sample", &error );
        if ( !selected )
            return invalid( error );
        source = selected->source;
        prepared = preparePreviewSource( selected->source, *line - 1, reassemble );
        prepared.source[ "windowId" ] = session_.windowId();
        prepared.source[ "tabId" ] = mainTabWidget_.tabIdAt( mainTabWidget_.indexOf( crawler ) );
    }
    if ( !prepared.error.isEmpty() )
        return invalid( prepared.error );
    QVariantMap result;
    const bool incomplete = prepared.assembly.value( "protocol" ) == "EHCI"
                            && prepared.assembly.value( "state" ) != "complete";
    if ( incomplete ) {
        result = { { "state", "incomplete" },
                   { "available", false },
                   { "complete", false },
                   { "reason", prepared.assembly.value( "reason", "more_data_required" ) },
                   { "backend", "json_preview" } };
    }
    else {
        PreviewEvaluationRequest evaluation;
        evaluation.text = prepared.text;
        evaluation.previewName = args.value( "previewName" ).toString();
        evaluation.timeoutMs = timeoutMs;
        result = PreviewService::evaluateAndWait( std::move( evaluation ), crawler );
    }
    result[ "assembly" ] = prepared.assembly;
    result[ "source" ] = prepared.source;
    result[ "inputText" ] = prepared.text;
    if ( prepared.snapshot.available ) {
        const auto current = source.lock();
        if ( !crawler
             || !previewSourceStillCurrent( current, prepared.snapshot,
                                            prepared.validationPhysicalLine ) ) {
            result[ "historical" ] = true;
            result[ "state" ] = "failed";
            result[ "complete" ] = false;
            result[ "reason" ] = "source_invalidated";
        }
        else
            result[ "newerDataAvailable" ]
                = current->sourceSnapshot().revision != prepared.snapshot.revision;
    }
    result[ "tooltipHtml" ] = PreviewService::tooltipHtml( result );
    LOG_DEBUG << "Preview command completed request=" << result.value( "requestId" ).toString()
              << " state=" << result.value( "state" ).toString()
              << " source=" << prepared.snapshot.sourceId;
    const bool failed = result.value( "state" ) == "failed";
    return { failed ? CommanderResultCode::ExecutionFailed : CommanderResultCode::Success,
             failed ? result.value( "reason" ).toString() : QString(), result };
}
