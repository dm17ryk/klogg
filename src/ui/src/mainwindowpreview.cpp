// SPDX-License-Identifier: GPL-3.0-or-later
#include "crawlerwidget.h"
#include "log.h"
#include "logdata.h"
#include "mainwindow.h"
#include "previewdecodersettings.h"
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
    const QSet<QString> allowed{ "text",       "line",          "previewName",
                                 "reassemble", "timeoutMs",     "decoder",
                                 "dissector",  "inputEncoding", "requestRevision" };
    for ( auto it = args.cbegin(); it != args.cend(); ++it )
        if ( !allowed.contains( it.key() ) )
            return invalid( "unknown_preview_option" );
    for ( const auto* key : { "decoder", "dissector", "inputEncoding", "requestRevision" } ) {
        if ( args.contains( key )
             && ( args.value( key ).userType() != QMetaType::QString
                  || args.value( key ).toString().size() > 128
                  || args.value( key ).toString().contains( QChar::Null ) ) )
            return invalid( QString( "invalid_%1" ).arg( key ) );
    }
    const auto decoder = args.value( "decoder", "auto" ).toString();
    if ( decoder != "auto" && decoder != "json" && decoder != "tshark" )
        return invalid( "invalid_decoder" );
    const auto inputEncoding = args.value( "inputEncoding" ).toString();
    if ( args.contains( "inputEncoding" ) && inputEncoding != "utf8" && inputEncoding != "hex"
         && inputEncoding != "base64" )
        return invalid( "invalid_encoding" );
    const auto dissector = args.value( "dissector" ).toString();
    static const QRegularExpression namedDissector( "\\A[A-Za-z][A-Za-z0-9_.-]{0,127}\\z" );
    if ( args.contains( "dissector" ) && !namedDissector.match( dissector ).hasMatch() )
        return invalid( "invalid_dissector" );
    if ( decoder == "json" && ( args.contains( "dissector" ) || args.contains( "inputEncoding" ) ) )
        return invalid( "unsupported_decoder_options" );
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
        if ( !ok || timeoutMs < 100 || timeoutMs > 10000
             || args.value( "timeoutMs" ).userType() == QMetaType::Bool
             || args.value( "timeoutMs" ).toDouble() != timeoutMs )
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
        prepared.source[ "tabId" ] = paneArea_->tabIdAt( paneArea_->indexOf( crawler ) );
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
        evaluation.decoder = decoder;
        evaluation.dissector = dissector;
        evaluation.inputEncoding = inputEncoding;
        evaluation.assemblyProtocol = prepared.assembly.value( "protocol" ).toString();
        evaluation.sessionOwner = crawler ? static_cast<QObject*>( crawler.data() ) : this;
        result = PreviewService::evaluateAndWait( std::move( evaluation ), crawler );
    }
    result[ "assembly" ] = prepared.assembly;
    result[ "source" ] = prepared.source;
    if ( prepared.text.size() <= 65536 && prepared.text.toUtf8().size() <= 65536 )
        result[ "inputText" ] = prepared.text;
    else {
        result[ "inputTextOmitted" ] = true;
        result[ "inputTextBytes" ] = prepared.text.toUtf8().size();
        LOG_DEBUG << "Preview command input echo omitted bytes="
                  << result.value( "inputTextBytes" ).toLongLong();
    }
    if ( args.contains( "requestRevision" ) )
        result[ "requestRevision" ] = args.value( "requestRevision" );
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

CommanderResult MainWindow::commanderPreviewDecoder( const CommanderRequest& request )
{
    auto& settings = PreviewDecoderSettings::instance();
    if ( request.action == CommanderAction::SetPreviewDecoder ) {
        QString error;
        if ( !settings.setConfiguration( request.analysisArguments, &error ) )
            return { CommanderResultCode::InvalidRequest, error, { { "reason", error } } };
        LOG_DEBUG << "Preview decoder command applied revision=" << settings.revision();
    }
    else {
        if ( !request.analysisArguments.isEmpty() )
            return { CommanderResultCode::InvalidRequest,
                     "unknown_preview_decoder_option",
                     { { "reason", "unknown_preview_decoder_option" } } };
        if ( request.action == CommanderAction::ShowPreviewDecoderSettings )
            showPreviewDecoderSettings();
    }
    return commanderSuccess(
        {}, { { "configuration", settings.configuration() },
              { "revision", settings.revision() },
              { "status",
                settings.configuration().value( "enabled" ).toBool() ? "enabled" : "disabled" } } );
}
