// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sharedwriter.h"
#include <QLoggingCategory>
#include <algorithm>
#include <deque>
#include <limits>
#include <map>

Q_LOGGING_CATEGORY( sharedWriterLog, "cilogg.communication.writer" )

namespace cilogg::communication {
namespace {
DataError rejected( const char* code )
{
    qCDebug( sharedWriterLog, "Writer decision=%s", code );
    return { QString::fromLatin1( code ), "Shared writer rejected the operation", {}, {} };
}
DataError invalid( const QString& field )
{
    qCDebug( sharedWriterLog, "Rejecting writer definition field=%s", qPrintable( field ) );
    return { "invalid_definition", "Invalid shared writer definition", field, {} };
}
bool validEndpoint( const QString& id )
{
    if ( id.isEmpty() || id.size() > 128 )
        return false;
    for ( qsizetype n = 0; n < id.size(); ++n ) {
        const auto c = id.at( n );
        if ( c.isNull() || c == QChar( '\r' ) || c == QChar( '\n' ) || c.isLowSurrogate() )
            return false;
        if ( c.isHighSurrogate() ) {
            if ( ++n == id.size() || !id.at( n ).isLowSurrogate() )
                return false;
        }
    }
    return true;
}
} // namespace
struct SharedWriter::Impl {
    WriteEpoch epoch;
    StreamLimits limits;
    EndpointCapabilities capabilities;
    qint64 defaultTimeoutNs;
    qint64 now = 0;
    WriteMode mode = WriteMode::Automatic;
    std::optional<WriteMode> pendingMode;
    bool closed = false;
    bool faulted = false;
    struct Record {
        WriteRequest request;
        WriteProducer producer;
        qint64 deadline;
        quint64 accepted = 0;
        quint64 written = 0;
        WriteFailure failure = WriteFailure::None;
        std::optional<qint64> retiredAt = {};
        bool ioUnconfirmed = false;
        bool terminal() const
        {
            return failure != WriteFailure::None
                   || written == static_cast<quint64>( request.bytes().size() );
        }
    };
    std::map<QString, Record> records;
    std::deque<QString> queue;
    std::deque<QString> finished;
    struct Pending {
        QString id;
        WriteTicket ticket;
        quint64 offered;
        bool awaitingAcceptance = true;
    };
    std::optional<Pending> pending;
    bool blocked = false;
    quint64 sequence = 0;
    Impl( WriteEpoch e, StreamLimits l, EndpointCapabilities c, qint64 timeout )
        : epoch( std::move( e ) )
        , limits( std::move( l ) )
        , capabilities( std::move( c ) )
        , defaultTimeoutNs( timeout )
    {
    }
    DataError context( const WriteEpoch& e, qint64 time ) const
    {
        if ( e.endpointId != epoch.endpointId || e.generation != epoch.generation )
            return rejected( "stale_generation" );
        if ( time < 0 || time < now )
            return rejected( "invalid_clock" );
        return {};
    }
    DataError ticket( const WriteTicket& t ) const
    {
        if ( t.epoch.endpointId != epoch.endpointId || t.epoch.generation != epoch.generation )
            return rejected( "stale_generation" );
        if ( !pending || pending->ticket.sequence != t.sequence )
            return rejected( "stale_ticket" );
        return {};
    }
    void settle()
    {
        for ( auto it = queue.begin(); it != queue.end(); ) {
            auto& r = records.at( *it );
            if ( r.terminal() && ( !pending || pending->id != *it ) ) {
                r.retiredAt = now;
                finished.push_back( *it );
                it = queue.erase( it );
                qCDebug( sharedWriterLog, "Retired write accepted=%llu written=%llu",
                         static_cast<unsigned long long>( r.accepted ),
                         static_cast<unsigned long long>( r.written ) );
            }
            else
                ++it;
        }
        if ( pendingMode && queue.empty() && !closed && !faulted ) {
            mode = *pendingMode;
            pendingMode.reset();
            qCDebug( sharedWriterLog, "Mode committed manual=%d", mode == WriteMode::ManualArmed );
        }
    }
    void evictFinished( const char* reason )
    {
        const auto& id = finished.front();
        const auto bytes = records.at( id ).request.bytes().size();
        records.erase( id );
        finished.pop_front();
        qCDebug( sharedWriterLog, "Evicted finished reason=%s bytes=%lld retained=%llu", reason,
                 static_cast<long long>( bytes ),
                 static_cast<unsigned long long>( finished.size() ) );
    }
    void advanceTime( qint64 time )
    {
        now = time;
        const auto retention = limits.receiptRetentionMs() * qint64( 1000000 );
        while ( !finished.empty() && now - *records.at( finished.front() ).retiredAt >= retention )
            evictFinished( "expiry" );
        for ( const auto& id : queue ) {
            auto& r = records.at( id );
            if ( !r.terminal() && now >= r.deadline ) {
                r.failure = WriteFailure::DeadlineExceeded;
                qCDebug( sharedWriterLog,
                         "Deadline exhausted generation=%llu nowNs=%lld deadlineNs=%lld",
                         static_cast<unsigned long long>( r.request.connectionGeneration() ),
                         static_cast<long long>( now ), static_cast<long long>( r.deadline ) );
            }
        }
        settle();
    }
    void stop( WriteFailure failure )
    {
        for ( const auto& id : queue ) {
            auto& r = records.at( id );
            if ( !r.terminal() )
                r.failure = failure;
        }
        settle();
    }
    void fault()
    {
        faulted = true;
        pendingMode.reset();
        mode = WriteMode::Automatic;
        stop( WriteFailure::IoError );
        qCDebug( sharedWriterLog, "Writer fault generation=%llu pendingIO=%d",
                 static_cast<unsigned long long>( epoch.generation ), pending.has_value() );
    }
    DataResult<WriteObservation> observe( const Record& r ) const
    {
        WriteStage stage = WriteStage::Queued;
        if ( r.failure != WriteFailure::None )
            stage
                = r.failure == WriteFailure::Cancelled ? WriteStage::Cancelled : WriteStage::Failed;
        else if ( r.written == static_cast<quint64>( r.request.bytes().size() ) )
            stage = WriteStage::WrittenToBackend;
        else if ( r.accepted > 0 )
            stage = WriteStage::Accepted;
        auto receipt = WriteReceipt::forRequest( r.request, epoch.endpointId, stage, r.accepted,
                                                 r.written, r.failure, limits );
        if ( !receipt.value )
            return { {}, receipt.error };
        const bool current = pending && pending->id == r.request.requestId();
        return { WriteObservation{ *receipt.value, current && pending->awaitingAcceptance,
                                   current && !pending->awaitingAcceptance, r.ioUnconfirmed },
                 {} };
    }
};
SharedWriter::SharedWriter( std::unique_ptr<Impl> impl )
    : impl_( std::move( impl ) )
{
}
SharedWriter::~SharedWriter() = default;
DataResult<std::unique_ptr<SharedWriter>>
SharedWriter::create( const WriteEpoch& epoch, const StreamLimits& limits,
                      const EndpointCapabilities& capabilities, qint64 timeout )
{
    if ( !validEndpoint( epoch.endpointId ) )
        return { {}, invalid( "endpointId" ) };
    if ( epoch.generation == 0 )
        return { {}, invalid( "generation" ) };
    if ( timeout <= 0 )
        return { {}, invalid( "defaultTimeoutNs" ) };
    auto impl = std::make_unique<Impl>(
        WriteEpoch{ QString( epoch.endpointId.constData(), epoch.endpointId.size() ),
                    epoch.generation },
        limits, capabilities, timeout );
    qCDebug( sharedWriterLog, "Created writer generation=%llu timeoutNs=%lld",
             static_cast<unsigned long long>( epoch.generation ),
             static_cast<long long>( timeout ) );
    return { std::unique_ptr<SharedWriter>( new SharedWriter( std::move( impl ) ) ), {} };
}
DataResult<WriteObservation> SharedWriter::submit( const WriteEpoch& epoch,
                                                   const WriteRequest& request,
                                                   WriteProducer producer, qint64 now )
{
    auto& s = *impl_;
    auto error = s.context( epoch, now );
    if ( !error.code.isEmpty() )
        return { {}, error };
    if ( request.connectionGeneration() != epoch.generation )
        return { {}, rejected( "stale_generation" ) };
    if ( producer != WriteProducer::Automation && producer != WriteProducer::Manual )
        return { {}, rejected( "invalid_producer" ) };
    if ( static_cast<quint64>( request.bytes().size() ) > s.limits.maximumWriteBytes() )
        return { {}, rejected( "resource_limit" ) };
    s.advanceTime( now );
    const auto previous = s.records.find( request.requestId() );
    if ( previous != s.records.end() ) {
        const auto& r = previous->second;
        if ( r.request.bytes() != request.bytes() || r.request.sensitive() != request.sensitive()
             || r.request.connectionGeneration() != request.connectionGeneration()
             || r.request.deadlineNs() != request.deadlineNs() || r.producer != producer )
            return { {}, rejected( "request_conflict" ) };
        qCDebug( sharedWriterLog, "Retained retry generation=%llu",
                 static_cast<unsigned long long>( epoch.generation ) );
        return s.observe( r );
    }
    if ( s.closed || s.faulted || s.pendingMode )
        return { {}, rejected( "endpoint_busy" ) };
    if ( !s.capabilities.canWrite() || s.capabilities.messageBoundaries() )
        return { {}, rejected( "unsupported_capability" ) };
    if ( ( s.mode == WriteMode::Automatic ) != ( producer == WriteProducer::Automation ) )
        return { {}, rejected( "endpoint_busy" ) };
    if ( !request.deadlineNs() && now > std::numeric_limits<qint64>::max() - s.defaultTimeoutNs )
        return { {}, rejected( "invalid_deadline" ) };
    const auto deadline = request.deadlineNs().value_or(
        now <= std::numeric_limits<qint64>::max() - s.defaultTimeoutNs ? now + s.defaultTimeoutNs
                                                                       : now );
    if ( deadline <= now )
        return { {}, rejected( "deadline_exceeded" ) };
    const auto bytes = static_cast<quint64>( request.bytes().size() );
    const auto current = state();
    // Preflight active state before evicting anything; an impossible admission must not
    // discard useful completed identities. Retained payloads share the same byte budget.
    if ( current.pendingBytes > s.limits.maximumPendingTxBytes() - bytes
         || current.pendingRequests >= s.limits.maximumReceipts() )
        return { {}, rejected( "queue_full" ) };
    auto resident = current.residentBytes;
    while ( resident > s.limits.maximumPendingTxBytes() - bytes
            || s.records.size() >= s.limits.maximumReceipts() ) {
        const auto removed
            = static_cast<quint64>( s.records.at( s.finished.front() ).request.bytes().size() );
        s.evictFinished( "admission" );
        resident -= removed;
    }
    auto inserted
        = s.records.emplace( request.requestId(), Impl::Record{ request, producer, deadline } );
    s.queue.push_back( request.requestId() );
    qCDebug( sharedWriterLog,
             "Admitted generation=%llu requests=%llu residentBytes=%llu deadlineNs=%lld",
             static_cast<unsigned long long>( epoch.generation ),
             static_cast<unsigned long long>( s.records.size() ),
             static_cast<unsigned long long>( resident + bytes ),
             static_cast<long long>( deadline ) );
    return s.observe( inserted.first->second );
}
DataResult<WriteDispatch> SharedWriter::take( const WriteEpoch& epoch, qint64 now )
{
    auto& s = *impl_;
    auto error = s.context( epoch, now );
    if ( !error.code.isEmpty() )
        return { {}, error };
    s.advanceTime( now );
    if ( s.closed || s.faulted || s.pendingMode || s.pending || s.blocked || s.queue.empty() ) {
        qCDebug(
            sharedWriterLog,
            "Dispatch idle closed=%d faulted=%d pendingMode=%d pendingIO=%d blocked=%d queued=%llu",
            s.closed, s.faulted, s.pendingMode.has_value(), s.pending.has_value(), s.blocked,
            static_cast<unsigned long long>( s.queue.size() ) );
        return { WriteDispatch{}, {} };
    }
    if ( s.sequence == std::numeric_limits<quint64>::max() )
        return { {}, rejected( "resource_limit" ) };
    const auto& id = s.queue.front();
    const auto& r = s.records.at( id );
    const auto size = std::min( s.limits.maximumChunkBytes(),
                                static_cast<quint64>( r.request.bytes().size() ) - r.accepted );
    WriteTicket ticket{ s.epoch, ++s.sequence };
    s.pending = Impl::Pending{ id, ticket, size };
    qCDebug( sharedWriterLog, "Offered generation=%llu ticket=%llu offset=%llu bytes=%llu",
             static_cast<unsigned long long>( epoch.generation ),
             static_cast<unsigned long long>( ticket.sequence ),
             static_cast<unsigned long long>( r.accepted ),
             static_cast<unsigned long long>( size ) );
    return { WriteDispatch{ WriteOffer( ticket, id, r.accepted,
                                        r.request.bytes().mid( static_cast<int>( r.accepted ),
                                                               static_cast<int>( size ) ) ) },
             {} };
}
DataStatus SharedWriter::accepted( const WriteTicket& ticket, qint64 count, qint64 now )
{
    auto& s = *impl_;
    auto error = s.ticket( ticket );
    if ( !error.code.isEmpty() )
        return { false, error };
    if ( !s.pending->awaitingAcceptance || count < -1
         || ( count >= 0 && static_cast<quint64>( count ) > s.pending->offered ) )
        return { false, rejected( "invalid_count" ) };
    error = s.context( ticket.epoch, now );
    if ( !error.code.isEmpty() )
        return { false, error };
    s.advanceTime( now );
    auto& r = s.records.at( s.pending->id );
    if ( count == -1 ) {
        // Unlike a generic error, this return value resolves acceptance for this offer.
        // Previously acknowledged prefixes remain intact and no suffix is replayed.
        s.pending.reset();
        s.fault();
        return { true, {} };
    }
    r.accepted += static_cast<quint64>( count );
    qCDebug( sharedWriterLog, "Acceptance ticket=%llu count=%lld total=%llu",
             static_cast<unsigned long long>( ticket.sequence ), static_cast<long long>( count ),
             static_cast<unsigned long long>( r.accepted ) );
    if ( count == 0 ) {
        s.blocked = true;
        s.pending.reset();
    }
    else
        s.pending->awaitingAcceptance = false;
    s.settle();
    return { true, {} };
}
DataStatus SharedWriter::written( const WriteTicket& ticket, qint64 count, qint64 now )
{
    auto& s = *impl_;
    auto error = s.ticket( ticket );
    if ( !error.code.isEmpty() )
        return { false, error };
    auto& r = s.records.at( s.pending->id );
    if ( s.pending->awaitingAcceptance || count <= 0
         || static_cast<quint64>( count ) > r.accepted - r.written )
        return { false, rejected( "invalid_count" ) };
    error = s.context( ticket.epoch, now );
    if ( !error.code.isEmpty() )
        return { false, error };
    s.advanceTime( now );
    r.written += static_cast<quint64>( count );
    qCDebug( sharedWriterLog, "Written ticket=%llu count=%lld total=%llu",
             static_cast<unsigned long long>( ticket.sequence ), static_cast<long long>( count ),
             static_cast<unsigned long long>( r.written ) );
    if ( r.written == r.accepted )
        s.pending.reset();
    s.settle();
    return { true, {} };
}
DataStatus SharedWriter::failed( const WriteTicket& ticket, qint64 now )
{
    auto& s = *impl_;
    auto error = s.ticket( ticket );
    if ( !error.code.isEmpty() )
        return { false, error };
    error = s.context( ticket.epoch, now );
    if ( !error.code.isEmpty() )
        return { false, error };
    s.advanceTime( now );
    // A device error carries no acceptance count. Keep the unresolved I/O barrier.
    s.fault();
    return { true, {} };
}
DataStatus SharedWriter::ready( const WriteEpoch& epoch, qint64 now )
{
    auto error = impl_->context( epoch, now );
    if ( !error.code.isEmpty() )
        return { false, error };
    impl_->advanceTime( now );
    impl_->blocked = false;
    qCDebug( sharedWriterLog, "Ready generation=%llu",
             static_cast<unsigned long long>( epoch.generation ) );
    return { true, {} };
}
DataStatus SharedWriter::cancel( const WriteEpoch& epoch, const QString& id, qint64 now )
{
    auto& s = *impl_;
    auto error = s.context( epoch, now );
    if ( !error.code.isEmpty() )
        return { false, error };
    s.advanceTime( now );
    const auto found = s.records.find( id );
    if ( found == s.records.end() )
        return { false, rejected( "not_found" ) };
    if ( !found->second.terminal() )
        found->second.failure = WriteFailure::Cancelled;
    s.settle();
    qCDebug( sharedWriterLog, "Cancelled generation=%llu pendingIO=%d",
             static_cast<unsigned long long>( epoch.generation ), s.pending.has_value() );
    return { true, {} };
}
DataStatus SharedWriter::requestMode( const WriteEpoch& epoch, WriteMode mode, qint64 now )
{
    auto& s = *impl_;
    auto error = s.context( epoch, now );
    if ( !error.code.isEmpty() )
        return { false, error };
    if ( mode != WriteMode::Automatic && mode != WriteMode::ManualArmed )
        return { false, rejected( "invalid_mode" ) };
    if ( s.closed || s.faulted || ( s.pendingMode && *s.pendingMode != mode ) )
        return { false, rejected( "endpoint_busy" ) };
    s.advanceTime( now );
    if ( s.pendingMode || s.mode == mode ) {
        qCDebug( sharedWriterLog, "Mode unchanged manual=%d pending=%d",
                 s.mode == WriteMode::ManualArmed, s.pendingMode.has_value() );
        return { true, {} };
    }
    s.pendingMode = mode;
    s.stop( WriteFailure::Cancelled );
    qCDebug( sharedWriterLog, "Mode requested manual=%d pending=%d", mode == WriteMode::ManualArmed,
             s.pendingMode.has_value() );
    return { true, {} };
}
DataStatus SharedWriter::close( const WriteEpoch& epoch, qint64 now )
{
    auto& s = *impl_;
    auto error = s.context( epoch, now );
    if ( !error.code.isEmpty() )
        return { false, error };
    s.advanceTime( now );
    s.closed = true;
    s.pendingMode.reset();
    s.mode = WriteMode::Automatic;
    s.stop( WriteFailure::Cancelled );
    qCDebug( sharedWriterLog, "Closed admission generation=%llu pendingIO=%d",
             static_cast<unsigned long long>( epoch.generation ), s.pending.has_value() );
    return { true, {} };
}
DataStatus SharedWriter::retireAfterBackendClosed( const WriteEpoch& epoch, qint64 now )
{
    auto& s = *impl_;
    const auto error = s.context( epoch, now );
    if ( !error.code.isEmpty() )
        return { false, error };
    if ( !s.closed )
        return { false, rejected( "invalid_retirement" ) };
    s.advanceTime( now );
    if ( s.pending ) {
        s.records.at( s.pending->id ).ioUnconfirmed = true;
        s.pending.reset();
    }
    s.settle();
    qCDebug( sharedWriterLog, "Backend retired generation=%llu closed=%d blocked=%d",
             static_cast<unsigned long long>( epoch.generation ), s.closed, s.blocked );
    return { true, {} };
}
DataStatus SharedWriter::withdrawUnsubmitted( const WriteTicket& ticket, qint64 now )
{
    auto& s = *impl_;
    auto error = s.ticket( ticket );
    if ( !error.code.isEmpty() )
        return { false, error };
    error = s.context( ticket.epoch, now );
    if ( !error.code.isEmpty() )
        return { false, error };
    const auto& r = s.records.at( s.pending->id );
    if ( !s.pending->awaitingAcceptance || ( !r.terminal() && now < r.deadline ) )
        return { false, rejected( "invalid_withdrawal" ) };
    s.advanceTime( now );
    s.pending.reset();
    s.settle();
    qCDebug( sharedWriterLog, "Withdrawn unsubmitted generation=%llu ticket=%llu blocked=%d",
             static_cast<unsigned long long>( ticket.epoch.generation ),
             static_cast<unsigned long long>( ticket.sequence ), s.blocked );
    return { true, {} };
}
DataStatus SharedWriter::resetAfterBackendClosed( const WriteEpoch& epoch, quint64 nextGeneration,
                                                  const EndpointCapabilities& capabilities,
                                                  qint64 now )
{
    auto& s = *impl_;
    auto error = s.context( epoch, now );
    if ( !error.code.isEmpty() )
        return { false, error };
    if ( !s.closed || nextGeneration <= epoch.generation )
        return { false, rejected( "invalid_reset" ) };
    const auto retirement = retireAfterBackendClosed( epoch, now );
    if ( !retirement.ok )
        return retirement;
    s.blocked = false;
    s.settle();
    s.epoch.generation = nextGeneration;
    s.sequence = 0;
    s.capabilities = capabilities;
    s.mode = WriteMode::Automatic;
    s.pendingMode.reset();
    s.closed = false;
    s.faulted = false;
    qCDebug(
        sharedWriterLog,
        "Reset after owner-acknowledged backend retirement oldGeneration=%llu newGeneration=%llu",
        static_cast<unsigned long long>( epoch.generation ),
        static_cast<unsigned long long>( nextGeneration ) );
    return { true, {} };
}
DataStatus SharedWriter::advance( const WriteEpoch& epoch, qint64 now )
{
    auto error = impl_->context( epoch, now );
    if ( !error.code.isEmpty() )
        return { false, error };
    impl_->advanceTime( now );
    qCDebug( sharedWriterLog, "Clock advanced generation=%llu nowNs=%lld",
             static_cast<unsigned long long>( epoch.generation ), static_cast<long long>( now ) );
    return { true, {} };
}
DataResult<WriteObservation> SharedWriter::getWrite( const QString& id ) const
{
    const auto found = impl_->records.find( id );
    if ( found == impl_->records.end() )
        return { {}, rejected( "not_found" ) };
    return impl_->observe( found->second );
}
SharedWriterState SharedWriter::state() const
{
    SharedWriterState result;
    const auto& s = *impl_;
    result.epoch = s.epoch;
    result.mode = s.mode;
    result.pendingMode = s.pendingMode;
    result.closed = s.closed;
    result.faulted = s.faulted;
    result.backpressured = s.blocked;
    result.awaitingAcceptance = s.pending && s.pending->awaitingAcceptance;
    result.awaitingDrain = s.pending && !s.pending->awaitingAcceptance;
    result.resetRequired = s.pending && s.records.at( s.pending->id ).terminal();
    for ( const auto& entry : s.records ) {
        const auto& r = entry.second;
        auto bytes = static_cast<quint64>( r.request.bytes().size() );
        if ( r.retiredAt ) {
            result.retainedBytes += bytes;
            ++result.retainedRequests;
            const auto retention = s.limits.receiptRetentionMs() * qint64( 1000000 );
            if ( *r.retiredAt <= std::numeric_limits<qint64>::max() - retention ) {
                const auto expiry = *r.retiredAt + retention;
                if ( !result.nextWakeNs || expiry < *result.nextWakeNs )
                    result.nextWakeNs = expiry;
            }
        }
        else {
            result.pendingBytes += bytes;
            ++result.pendingRequests;
            if ( !r.terminal() && r.deadline > s.now
                 && ( !result.nextWakeNs || r.deadline < *result.nextWakeNs ) )
                result.nextWakeNs = r.deadline;
        }
    }
    result.residentBytes = result.pendingBytes + result.retainedBytes;
    return result;
}
} // namespace cilogg::communication
