// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "actionsmanager.h"
#include "streamsession.h"
#include <QLoggingCategory>
#include <QPointer>
#include <algorithm>
#include <limits>

Q_LOGGING_CATEGORY( serialSessionLog, "cilogg.communication.session" )
using namespace cilogg::communication;
namespace {
DataError writeError( const char* code )
{
    qCDebug( serialSessionLog, "Session decision=%s", code );
    return { QString::fromLatin1( code ), "Serial write could not be admitted", {}, {} };
}
constexpr qint64 WriteBudgetNs = 1000000000;
qint64 after( qint64 now, qint64 delay )
{
    return now > std::numeric_limits<qint64>::max() - delay ? std::numeric_limits<qint64>::max()
                                                            : now + delay;
}
bool matches( const WriteEpoch& epoch, const QString& id, quint64 generation )
{
    return epoch.endpointId == id && epoch.generation == generation;
}
} // namespace
qint64 StreamSession::writerNow() const
{
    return writerClock_ ? writerClock_()
                        : std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now() - writerOrigin_ )
                              .count();
}
void StreamSession::publishWritePolicy()
{
    const bool valid = !closePublished_ && !stopping_ && !writePolicyExhausted_
                       && ( started_ || connectionOpen_ );
    writePolicy_->publish( { valid ? workerGeneration_ : 0, actionsGeneration_,
                             autoResponsesGeneration_, valid && connectionOpen_,
                             settings_.useForActions, settings_.autoResponsesEnabled,
                             ActionsManager::instance().autoResponsesEnabled() } );
    qCDebug( serialSessionLog,
             "Published policy generation=%llu ready=%d actionsRevision=%llu "
             "responsesRevision=%llu valid=%d",
             static_cast<unsigned long long>( workerGeneration_ ), valid && connectionOpen_,
             static_cast<unsigned long long>( actionsGeneration_ ),
             static_cast<unsigned long long>( autoResponsesGeneration_ ), valid );
}
bool StreamSession::initializeWriter( quint64 generation )
{
    EndpointCapabilitiesDefinition definition;
    definition.canRead = true;
    definition.canWrite = true;
    const auto capabilities = EndpointCapabilities::fromDefinition( definition );
    if ( !capabilities.value )
        return false;
    const auto now = writerNow();
    if ( writer_ ) {
        const auto old = writer_->state();
        if ( !backendRetired_ || !old.closed
             || !writer_->resetAfterBackendClosed( old.epoch, generation, *capabilities.value, now )
                     .ok )
            return false;
    }
    else {
        auto created = SharedWriter::create( { writerEndpointId_, generation }, writeLimits_,
                                             *capabilities.value, WriteBudgetNs );
        if ( !created.value )
            return false;
        writer_ = std::move( *created.value );
    }
    lastWriteDelivery_ = 0;
    acceptedTicket_ = 0;
    appliedWritten_ = 0;
    currentOffer_.reset();
    retryAtNs_ = 0;
    retryDelayMs_ = 10;
    qCDebug( serialSessionLog, "Initialized writer generation=%llu nowNs=%lld retained=%u",
             static_cast<unsigned long long>( generation ), static_cast<long long>( now ),
             writer_->state().retainedRequests );
    return true;
}
void StreamSession::startChecked( bool resetCounters )
{
    if ( started_ || stopping_ || worker_ || thread_.isRunning() ) {
        qCDebug( serialSessionLog, "Start deferred: predecessor still owns backend" );
        return;
    }
    if ( writePolicyExhausted_ || workerGeneration_ == std::numeric_limits<quint64>::max() ) {
        qCDebug( serialSessionLog, "Start rejected: generation exhausted" );
        Q_EMIT errorOccurred( tr( "Serial connection generation exhausted." ) );
        return;
    }
    closePublished_ = false;
    paused_ = false;
    auto lease = registry_->tryAcquireSerialPort( settings_.portName, writeLimits_ );
    if ( !lease.value ) {
        paused_ = true;
        QPointer<StreamSession> guard( this );
        setConnectionClosed();
        if ( guard )
            Q_EMIT errorOccurred( tr( "Serial endpoint unavailable: %1" ).arg( lease.error.code ) );
        return;
    }
    serialLease_ = *lease.value;
    const auto next = workerGeneration_ + 1;
    if ( !initializeWriter( next ) ) {
        registry_->releaseSerialPort( *serialLease_ );
        serialLease_.reset();
        paused_ = true;
        QPointer<StreamSession> guard( this );
        setConnectionClosed();
        if ( guard )
            Q_EMIT errorOccurred( tr( "Serial writer could not be initialized." ) );
        return;
    }
    workerGeneration_ = next;
    started_ = true;
    stopping_ = false;
    connectionOpen_ = false;
    backendRetired_ = false;
    lineBuffer_.clear();
    lineBufferActionsSegments_.clear();
    if ( resetCounters )
        responseCounters_.clear();
    publishWritePolicy();
    setupWorker();
    qCDebug( serialSessionLog, "Opening generation=%llu lease=%llu",
             static_cast<unsigned long long>( next ),
             static_cast<unsigned long long>( serialLease_->leaseId() ) );
    thread_.start();
}
void StreamSession::setupCheckedWorker()
{
    if ( worker_ )
        return;
    const auto generation = workerGeneration_;
    worker_ = workerFactory_ ? workerFactory_( settings_ ) : new SerialCaptureWorker( settings_ );
    Q_ASSERT( worker_ );
    worker_->writeContext_
        = SerialWriteContext{ { writerEndpointId_, generation }, writerOrigin_, writePolicy_ };
    threadRetirement_ = std::make_shared<BackendThreadRetirement>();
    worker_->threadRetirement_ = threadRetirement_;
    worker_->loggingEnabled_ = loggingEnabled_;
    worker_->moveToThread( &thread_ );
    connect( &thread_, &QThread::started, worker_, &SerialCaptureWorker::start );
    connect( worker_, &SerialCaptureWorker::backendReady, this,
             [ this, generation ] { acceptBackendReady( generation ); } );
    connect( worker_, &SerialCaptureWorker::backendSnapshot, this,
             &StreamSession::acceptWriteSnapshot );
    connect( worker_, &SerialCaptureWorker::dataReceived, this,
             [ this, generation ]( const QByteArray& data ) {
                 acceptBackendData( generation, data, false );
             } );
    connect( worker_, &SerialCaptureWorker::dataTransmitted, this,
             [ this, generation ]( const QByteArray& data ) {
                 acceptBackendData( generation, data, true );
             } );
    connect( worker_, &SerialCaptureWorker::errorOccurred, this,
             [ this, generation ]( const QString& message ) {
                 if ( generation != workerGeneration_ || stopping_ || closePublished_ ) {
                     qCDebug( serialSessionLog,
                              "Discarded stale/closing backend error generation=%llu",
                              static_cast<unsigned long long>( generation ) );
                     return;
                 }
                 paused_ = true;
                 QPointer<StreamSession> guard( this );
                 stop( false );
                 if ( guard )
                     Q_EMIT errorOccurred( message );
             } );
    connect( worker_, &SerialCaptureWorker::finished, this, [ this, generation ] {
        if ( generation != workerGeneration_ )
            return;
        qCDebug( serialSessionLog, "Worker finished generation=%llu",
                 static_cast<unsigned long long>( generation ) );
        worker_ = nullptr;
        thread_.quit();
    } );
    connect( &thread_, &QThread::finished, worker_, &QObject::deleteLater );
    disconnect( threadFinishedConnection_ );
    threadFinishedConnection_ = connect( &thread_, &QThread::finished, this, [ this, generation ] {
        completeThreadRetirement( generation );
    } );
}
void StreamSession::completeThreadRetirement( quint64 generation )
{
    if ( generation != workerGeneration_ || ( !started_ && !serialLease_ ) )
        return;
    // finished() precedes CRT thread-local destructors on Windows. Poll a retained native
    // witness as well: Qt can already have discarded its own handle in its Finished state.
    if ( !thread_.wait( 0 ) || ( threadRetirement_ && !threadRetirement_->complete() ) ) {
        qCDebug( serialSessionLog, "Waiting for thread retirement generation=%llu",
                 static_cast<unsigned long long>( generation ) );
        QTimer::singleShot( 10, this,
                            [ this, generation ] { completeThreadRetirement( generation ); } );
        return;
    }
    QPointer<StreamSession> guard( this );
    worker_ = nullptr;
    started_ = false;
    finishWriterBackend();
    if ( !guard )
        return;
    if ( serialLease_ ) {
        registry_->releaseSerialPort( *serialLease_ );
        serialLease_.reset();
    }
    stopping_ = false;
    threadRetirement_.reset();
    publishWritePolicy();
    qCDebug( serialSessionLog, "Shutdown acknowledged generation=%llu",
             static_cast<unsigned long long>( generation ) );
    driveWrites();
}
void StreamSession::acceptBackendReady( quint64 generation )
{
    if ( generation != workerGeneration_ || stopping_ || closePublished_ || !started_
         || connectionOpen_ ) {
        qCDebug( serialSessionLog, "Ready discarded generation=%llu current=%llu closing=%d",
                 static_cast<unsigned long long>( generation ),
                 static_cast<unsigned long long>( workerGeneration_ ), stopping_ );
        return;
    }
    connectionOpen_ = true;
    publishWritePolicy();
    QPointer<StreamSession> guard( this );
    Q_EMIT connectionOpened();
    if ( guard )
        driveWrites();
}
void StreamSession::acceptBackendData( quint64 generation, const QByteArray& bytes,
                                       bool transmitted )
{
    if ( generation != workerGeneration_ || stopping_ || !connectionOpen_ || transmitted ) {
        qCDebug( serialSessionLog, "Backend data discarded generation=%llu tx=%d bytes=%lld",
                 static_cast<unsigned long long>( generation ), transmitted,
                 static_cast<long long>( bytes.size() ) );
        return;
    }
    handleDataReceived( bytes );
}
void StreamSession::closeCheckedConnection()
{
    if ( closePublished_ && !connectionOpen_ )
        return;
    if ( writer_ && !writer_->state().closed )
        writer_->close( writer_->state().epoch, writerNow() );
    closePublished_ = true;
    connectionOpen_ = false;
    if ( actionsGeneration_ != std::numeric_limits<quint64>::max() )
        ++actionsGeneration_;
    else
        writePolicyExhausted_ = true;
    if ( autoResponsesGeneration_ != std::numeric_limits<quint64>::max() )
        ++autoResponsesGeneration_;
    else
        writePolicyExhausted_ = true;
    lineBuffer_.clear();
    lineBufferActionsSegments_.clear();
    publishWritePolicy();
    qCDebug( serialSessionLog, "Admission closed generation=%llu leaseHeld=%d",
             static_cast<unsigned long long>( workerGeneration_ ), serialLease_.has_value() );
    if ( !destroying_ )
        Q_EMIT connectionClosed();
}
void StreamSession::finishWriterBackend()
{
    QPointer<StreamSession> guard( this );
    setConnectionClosed();
    if ( !guard )
        return;
    if ( writer_ && !backendRetired_ ) {
        const auto retired
            = writer_->retireAfterBackendClosed( writer_->state().epoch, writerNow() );
        qCDebug( serialSessionLog, "Core retirement ok=%d", retired.ok );
    }
    backendRetired_ = true;
    currentOffer_.reset();
}
void StreamSession::stopChecked( bool wait )
{
    if ( stopping_ && !wait )
        return;
    stopping_ = true;
    publishWritePolicy();
    QPointer<StreamSession> guard( this );
    setConnectionClosed();
    if ( !guard )
        return;
    if ( thread_.isRunning() ) {
        if ( worker_ )
            QMetaObject::invokeMethod( worker_, "stop",
                                       wait ? Qt::BlockingQueuedConnection : Qt::QueuedConnection );
        if ( !wait )
            return;
    }
    else if ( started_ && !wait ) {
        // A started QThread normally reports running immediately; keep its lease until completion.
        if ( worker_ )
            QMetaObject::invokeMethod( worker_, "stop", Qt::QueuedConnection );
        return;
    }
    if ( wait ) {
        thread_.quit();
        thread_.wait();
        if ( threadRetirement_ )
            threadRetirement_->complete( true );
    }
    if ( !started_ || wait ) {
        started_ = false;
        worker_ = nullptr;
        finishWriterBackend();
        if ( !guard )
            return;
        if ( serialLease_ ) {
            registry_->releaseSerialPort( *serialLease_ );
            serialLease_.reset();
        }
        stopping_ = false;
        threadRetirement_.reset();
        publishWritePolicy();
    }
    if ( destroying_ )
        writeTimer_.stop();
    else
        driveWrites();
}
DataResult<WriteObservation> StreamSession::submitActionWrite( const QByteArray& bytes,
                                                               bool sensitive,
                                                               const ActionWriteStamp& stamp )
{
    if ( thread() != QThread::currentThread() )
        return { {}, writeError( "wrong_thread" ) };
    if ( !connectionOpen_ || stopping_ || !writer_ || writePolicyExhausted_ )
        return { {}, writeError( "endpoint_busy" ) };
    publishWritePolicy();
    const auto policy = writePolicy_->load();
    if ( stamp.actionsGeneration != actionsGeneration_
         || ( stamp.automaticResponse
              && ( stamp.autoResponsesGeneration != autoResponsesGeneration_
                   || !settings_.useForActions || !settings_.autoResponsesEnabled
                   || !policy->globalAutoResponsesEnabled ) ) )
        return { {}, writeError( "stale_policy" ) };
    const auto now = writerNow();
    if ( now < 0 || now > std::numeric_limits<qint64>::max() - WriteBudgetNs
         || writeRequestSequence_ == std::numeric_limits<quint64>::max() )
        return { {}, writeError( "resource_limit" ) };
    const auto id = QStringLiteral( "write-%1" ).arg( ++writeRequestSequence_ );
    const auto deadline = now + WriteBudgetNs;
    auto request = WriteRequest::fromDefinition(
        { id, bytes, sensitive, workerGeneration_, deadline }, writeLimits_ );
    if ( !request.value )
        return { {}, request.error };
    auto result = writer_->submit( { writerEndpointId_, workerGeneration_ }, *request.value,
                                   WriteProducer::Automation, now );
    if ( !result.value )
        return result;
    for ( auto it = writeMetadata_.begin(); it != writeMetadata_.end(); )
        if ( !writer_->getWrite( it.key() ).value )
            it = writeMetadata_.erase( it );
        else
            ++it;
    writeMetadata_.insert( id, { stamp, deadline, sensitive, false } );
    qCDebug( serialSessionLog,
             "Admitted id=%s generation=%llu bytes=%lld deadlineNs=%lld resident=%llu",
             qPrintable( id ), static_cast<unsigned long long>( workerGeneration_ ),
             static_cast<long long>( bytes.size() ), static_cast<long long>( deadline ),
             static_cast<unsigned long long>( writer_->state().residentBytes ) );
    driveWrites();
    return result;
}
void StreamSession::finalizeResolvedWrite( const QString& id, const WriteObservation& result )
{
    const auto stage = result.receipt.stage();
    if ( result.awaitingAcceptance || result.awaitingDrain || stage == WriteStage::Queued
         || stage == WriteStage::Accepted )
        return;
    auto metadata = writeMetadata_.find( id );
    if ( metadata == writeMetadata_.end() || !metadata->offered || metadata->finalizationQueued )
        return;
    metadata->finalizationQueued = true;
    if ( worker_ ) {
        QPointer<SerialCaptureWorker> worker( worker_ );
        const auto epoch = writer_->state().epoch;
        QMetaObject::invokeMethod(
            worker,
            [ worker, epoch, id ] {
                if ( worker )
                    worker->finalizeWritePresentation( epoch, id );
            },
            Qt::QueuedConnection );
    }
    qCDebug( serialSessionLog, "Resolved presentation id=%s", qPrintable( id ) );
}
bool StreamSession::publishWriteFailures()
{
    const auto epoch = writer_->state().epoch;
    const auto retired = backendRetired_;
    QStringList messages;
    // Mark all pending notifications before any observer can reenter or prune the cache.
    for ( auto it = writeMetadata_.begin(); it != writeMetadata_.end(); ++it ) {
        if ( it->failurePublished )
            continue;
        const auto result = writer_->getWrite( it.key() );
        if ( !result.value )
            continue;
        const auto& observation = *result.value;
        const auto stage = observation.receipt.stage();
        if ( stage != WriteStage::Failed )
            continue;
        it->failurePublished = true;
        const bool outstanding = observation.ioUnconfirmed || observation.awaitingAcceptance
                                 || observation.awaitingDrain;
        messages.append( tr( "Serial write %1 %2: %3 bytes accepted, %4 bytes written; "
                             "outstanding I/O: %5." )
                             .arg( it.key(), observation.receipt.errorCode() == "deadline_exceeded"
                                                 ? tr( "expired" )
                                                 : tr( "failed" ) )
                             .arg( observation.receipt.acceptedBytes() )
                             .arg( observation.receipt.writtenBytes() )
                             .arg( outstanding ? tr( "yes" ) : tr( "no" ) ) );
        qCDebug( serialSessionLog, "Publishing write failure id=%s stage=%d outstanding=%d",
                 qPrintable( it.key() ), static_cast<int>( stage ), outstanding );
    }
    QPointer<StreamSession> guard( this );
    for ( const auto& message : messages ) {
        Q_EMIT errorOccurred( message );
        if ( !guard || !matches( epoch, writerEndpointId_, workerGeneration_ )
             || backendRetired_ != retired )
            return false;
    }
    return true;
}
void StreamSession::driveWrites( bool retry )
{
    if ( !writer_ || destroying_ )
        return;
    const auto now = writerNow();
    const auto epoch = writer_->state().epoch;
    if ( !writer_->advance( epoch, now ).ok )
        return;
    publishWritePolicy();
    const bool global = ActionsManager::instance().autoResponsesEnabled();
    for ( auto it = writeMetadata_.begin(); it != writeMetadata_.end(); ) {
        const auto observation = writer_->getWrite( it.key() );
        if ( !observation.value ) {
            it = writeMetadata_.erase( it );
            continue;
        }
        const auto& stamp = it->stamp;
        if ( stamp.actionsGeneration != actionsGeneration_
             || ( stamp.automaticResponse
                  && ( stamp.autoResponsesGeneration != autoResponsesGeneration_ || !global
                       || !settings_.useForActions || !settings_.autoResponsesEnabled ) ) )
            writer_->cancel( epoch, it.key(), now );
        const auto resolved = writer_->getWrite( it.key() );
        if ( resolved.value )
            finalizeResolvedWrite( it.key(), *resolved.value );
        ++it;
    }
    auto state = writer_->state();
    if ( connectionOpen_ && !stopping_ && worker_ && !currentOffer_ ) {
        if ( state.backpressured && retry && now >= retryAtNs_ )
            writer_->ready( epoch, now );
        auto dispatch = writer_->take( epoch, now );
        if ( dispatch.value && dispatch.value->offer ) {
            const auto& offer = *dispatch.value->offer;
            const auto metadata = writeMetadata_.find( offer.requestId() );
            if ( metadata == writeMetadata_.end() ) {
                qCDebug( serialSessionLog, "Missing bounded write metadata; closing" );
                stop( false );
                return;
            }
            metadata->offered = true;
            currentOffer_ = SerialWriteEnvelopePtr::create(
                SerialWriteEnvelope{ offer, metadata->stamp, metadata->deadlineNs } );
            acceptedTicket_ = 0;
            appliedWritten_ = 0;
            QPointer<SerialCaptureWorker> worker( worker_ );
            const auto envelope = currentOffer_;
            const auto sensitive = metadata->sensitive;
            if ( !QMetaObject::invokeMethod(
                     worker,
                     [ worker, envelope, sensitive ] {
                         if ( worker )
                             worker->submitWriteOffer( envelope, sensitive );
                     },
                     Qt::QueuedConnection ) ) {
                qCDebug( serialSessionLog, "Offer delivery failed" );
                stop( false );
                return;
            }
        }
    }
    state = writer_->state();
    auto wake = state.nextWakeNs;
    if ( state.backpressured && connectionOpen_ && !stopping_ && !currentOffer_
         && state.pendingRequests ) {
        const auto probe = std::max( after( now, 1000000 ), retryAtNs_ );
        if ( !wake || probe < *wake )
            wake = probe;
    }
    if ( wake ) {
        const auto remaining = std::max( qint64( 1 ), *wake - now );
        const auto delay = std::min<qint64>( std::numeric_limits<int>::max(),
                                             remaining / 1000000 + ( remaining % 1000000 != 0 ) );
        writeTimer_.start( static_cast<int>( std::max( qint64( 1 ), delay ) ) );
    }
    else
        writeTimer_.stop();
    publishWriteFailures(); // Last operation: an error observer may delete or replace this owner.
}
void StreamSession::acceptWriteSnapshot( BackendWriteSnapshotPtr snapshot )
{
    if ( !snapshot || !writer_ || backendRetired_
         || !matches( snapshot->ticket.epoch, writerEndpointId_, workerGeneration_ )
         || snapshot->deliverySequence <= lastWriteDelivery_ ) {
        qCDebug( serialSessionLog, "Discarded stale write snapshot" );
        return;
    }
    const auto now = writerNow();
    const auto epoch = writer_->state().epoch;
    if ( !writer_->advance( epoch, now ).ok )
        return;
    lastWriteDelivery_ = snapshot->deliverySequence;
    const auto envelope = currentOffer_;
    QByteArray observable;
    if ( envelope && snapshot->ticket.sequence == envelope->offer.ticket().sequence ) {
        const auto id = envelope->offer.requestId();
        if ( snapshot->unsubmitted != UnsubmittedReason::None ) {
            if ( snapshot->unsubmitted == UnsubmittedReason::DeadlineExceeded )
                writer_->advance( epoch, now );
            else
                writer_->cancel( epoch, id, now );
            writer_->withdrawUnsubmitted( snapshot->ticket, now );
        }
        else {
            if ( snapshot->fault
                 && ( writer_->state().awaitingAcceptance || writer_->state().awaitingDrain ) )
                writer_->failed( snapshot->ticket, now );
            if ( snapshot->acceptance && acceptedTicket_ != snapshot->ticket.sequence ) {
                const auto accepted
                    = writer_->accepted( snapshot->ticket, *snapshot->acceptance, now );
                if ( accepted.ok )
                    acceptedTicket_ = snapshot->ticket.sequence;
                if ( *snapshot->acceptance == 0 ) {
                    retryAtNs_ = after( now, qint64( retryDelayMs_ ) * 1000000 );
                    retryDelayMs_ = std::min( 100, retryDelayMs_ * 2 );
                }
            }
            if ( snapshot->written > appliedWritten_
                 && snapshot->written <= static_cast<quint64>( envelope->offer.bytes().size() ) ) {
                const auto delta = snapshot->written - appliedWritten_;
                if ( writer_->written( snapshot->ticket, static_cast<qint64>( delta ), now ).ok ) {
                    auto metadata = writeMetadata_.find( id );
                    if ( metadata != writeMetadata_.end() ) {
                        if ( !metadata->sensitive )
                            observable = envelope->offer.bytes().mid(
                                static_cast<int>( appliedWritten_ ), static_cast<int>( delta ) );
                        else if ( !metadata->redactionPublished ) {
                            observable = "<redacted>";
                            metadata->redactionPublished = true;
                        }
                    }
                    appliedWritten_ = snapshot->written;
                    retryDelayMs_ = 10;
                }
            }
        }
        const auto result = writer_->getWrite( id );
        if ( result.value && !result.value->awaitingAcceptance && !result.value->awaitingDrain ) {
            finalizeResolvedWrite( id, *result.value );
            currentOffer_.reset();
        }
    }
    qCDebug( serialSessionLog,
             "Snapshot generation=%llu delivery=%llu cumulative=%llu fault=%d retired=%d",
             static_cast<unsigned long long>( workerGeneration_ ),
             static_cast<unsigned long long>( snapshot->deliverySequence ),
             static_cast<unsigned long long>( snapshot->written ), snapshot->fault,
             snapshot->retired );
    QPointer<StreamSession> guard( this );
    // Release transport credit before an observer can admit and queue its successor.
    if ( !snapshot->retired && worker_ ) {
        QPointer<SerialCaptureWorker> worker( worker_ );
        const auto sequence = snapshot->deliverySequence;
        QMetaObject::invokeMethod(
            worker,
            [ worker, epoch, sequence ] {
                if ( worker )
                    worker->acknowledgeWriteProgress( epoch, sequence );
            },
            Qt::QueuedConnection );
    }
    if ( !observable.isEmpty() && connectionOpen_ && !stopping_ ) {
        Q_EMIT dataTransmitted( observable );
        if ( !guard || !matches( epoch, writerEndpointId_, workerGeneration_ ) || backendRetired_ )
            return;
    }
    if ( !publishWriteFailures() )
        return;
    if ( snapshot->retired ) {
        finishWriterBackend();
        return;
    }
    if ( snapshot->fault ) {
        paused_ = true;
        stop( false );
        return;
    }
    driveWrites();
}
