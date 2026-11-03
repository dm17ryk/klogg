// SPDX-License-Identifier: GPL-3.0-or-later
#include "crawlerwidget.h"
#include "log.h"
#include "logdata.h"
#include "previewservice.h"
#include "previewsource.h"
#include <QCryptographicHash>
#include <QPointer>
#include <QProcess>

QString CrawlerWidget::previewSelectionText( AbstractLogView* view ) const
{
    return preparedPreviewSelection( view ).text;
}

PreparedPreview CrawlerWidget::preparedPreviewSelection( AbstractLogView* view ) const
{
    const auto ranges = view->selectedLineRanges();
    if ( !view->isPartialSelection() && ranges.size() == 1 && ranges.front().second.get() == 1 ) {
        const auto line = view->previewPhysicalLine( ranges.front().first ).get();
        const auto prepared = preparePreviewSource( logData_, line );
        if ( prepared.error.isEmpty() ) return prepared;
    }
    return preparePreviewText( view->getSelectedText() );
}

void CrawlerWidget::handleLinePreviewRequested( quint64 physicalLine, const QPoint& globalPosition,
                                                quint64 serial )
{
    Q_UNUSED( globalPosition );
    auto* view = qobject_cast<AbstractLogView*>( sender() );
    if ( !view )
        return;
    const auto prepared = preparePreviewSource( logData_, physicalLine );
    if ( !prepared.error.isEmpty() ) {
        LOG_DEBUG << "Hover preview unavailable line=" << physicalLine
                  << " reason=" << prepared.error;
        view->clearLinePreview();
        return;
    }
    const std::weak_ptr<LogData> source = logData_;
    const QPointer<AbstractLogView> destination( view );
    const auto definitions = PreviewService::definitions();
    const auto digest = QCryptographicHash::hash( definitions, QCryptographicHash::Sha256 );
    const auto valid
        = [ source, snapshot = prepared.snapshot, line = prepared.validationPhysicalLine ] {
              return previewSourceStillCurrent( source.lock(), snapshot, line );
          };
    const auto completed = [ destination, prepared, source, physicalLine, serial,
                             digest ]( QVariantMap result ) {
        if ( !destination )
            return;
        const auto current = source.lock();
        if ( !previewSourceStillCurrent( current, prepared.snapshot,
                                         prepared.validationPhysicalLine )
             || digest
                    != QCryptographicHash::hash( PreviewService::definitions(),
                                                 QCryptographicHash::Sha256 ) ) {
            destination->presentLinePreview( serial, {} );
            return;
        }
        result[ "assembly" ] = prepared.assembly;
        result[ "source" ] = prepared.source;
        result[ "inputText" ] = prepared.text;
        result[ "requestRevision" ] = QString::number( serial );
        result[ "newerDataAvailable" ]
            = current->sourceSnapshot().revision != prepared.snapshot.revision;
        LOG_DEBUG << "Hover preview delivered request=" << result.value( "requestId" ).toString()
                  << " source=" << prepared.snapshot.sourceId << " line=" << physicalLine;
        destination->presentLinePreview( serial, result );
    };
    if ( prepared.assembly.value( "protocol" ) == "EHCI"
         && prepared.assembly.value( "state" ) != "complete" ) {
        view->trackLinePreview( serial, nullptr, valid );
        completed( { { "state", "incomplete" },
                     { "available", false },
                     { "complete", false },
                     { "backend", "json_preview" } } );
        return;
    }
    PreviewEvaluationRequest request;
    request.text = prepared.text;
    request.definitions = definitions;
    request.assemblyProtocol = prepared.assembly.value( "protocol" ).toString();
    request.sessionOwner = this;
    auto* process = PreviewService::evaluate( view, std::move( request ), completed );
    view->trackLinePreview( serial, process, valid );
}
