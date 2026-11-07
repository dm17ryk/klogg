// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sharedwriter.h"
#include <QLoggingCategory>
#include <deque>
#include <map>
#include <limits>

Q_LOGGING_CATEGORY( sharedWriterLog, "cilogg.communication.writer" )

namespace cilogg::communication {
namespace {
DataError absent()
{
    return { "not_implemented", "Shared writer is not implemented", {}, {} };
}
DataError rejected( const char* code )
{
    qCDebug(sharedWriterLog, "Writer decision=%s", code);
    return { QString::fromLatin1(code), "Shared writer rejected the operation", {}, {} };
}
DataError invalid( const QString& field )
{
    qCDebug( sharedWriterLog, "Rejecting writer definition field=%s", qPrintable( field ) );
    return { "invalid_definition", "Invalid shared writer definition", field, {} };
}
bool validEndpoint( const QString& id )
{
    if ( id.isEmpty() || id.size() > 128 ) return false;
    for ( qsizetype n = 0; n < id.size(); ++n ) {
        const auto c = id.at( n );
        if ( c.isNull() || c == QChar('\r') || c == QChar('\n') || c.isLowSurrogate() ) return false;
        if ( c.isHighSurrogate() ) {
            if ( ++n == id.size() || !id.at( n ).isLowSurrogate() ) return false;
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
        std::optional<qint64> retiredAt;
        bool ioUnconfirmed = false;
        bool terminal() const { return failure != WriteFailure::None || written == static_cast<quint64>(request.bytes().size()); }
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
    std::optional<QString> blocked;
    quint64 sequence = 0;
    Impl( WriteEpoch e, StreamLimits l, EndpointCapabilities c, qint64 timeout )
        : epoch( std::move(e) ), limits(std::move(l)), capabilities(std::move(c)), defaultTimeoutNs(timeout) {}
    DataError context(const WriteEpoch& e, qint64 time) const {
        if(e.endpointId != epoch.endpointId || e.generation != epoch.generation) return rejected("stale_generation");
        if(time < 0 || time < now) return rejected("invalid_clock");
        return {};
    }
    DataError ticket(const WriteTicket& t) const {
        if(t.epoch.endpointId != epoch.endpointId || t.epoch.generation != epoch.generation) return rejected("stale_generation");
        if(!pending || pending->ticket.sequence != t.sequence) return rejected("stale_ticket");
        return {};
    }
    void settle() {
        for(auto it = queue.begin(); it != queue.end();) {
            auto& r = records.at(*it);
            if(r.terminal() && (!pending || pending->id != *it)) {
                r.retiredAt = now;
                finished.push_back(*it);
                if(blocked == *it) blocked.reset();
                it = queue.erase(it);
                qCDebug(sharedWriterLog, "Retired write accepted=%llu written=%llu", static_cast<unsigned long long>(r.accepted), static_cast<unsigned long long>(r.written));
            } else ++it;
        }
    }
    DataResult<WriteObservation> observe(const Record& r) const {
        WriteStage stage = WriteStage::Queued;
        if(r.failure != WriteFailure::None) stage = r.failure == WriteFailure::Cancelled ? WriteStage::Cancelled : WriteStage::Failed;
        else if(r.written == static_cast<quint64>(r.request.bytes().size())) stage = WriteStage::WrittenToBackend;
        else if(r.accepted > 0) stage = WriteStage::Accepted;
        auto receipt = WriteReceipt::forRequest(r.request, epoch.endpointId, stage, r.accepted, r.written, r.failure, limits);
        if(!receipt.value) return { {}, receipt.error };
        const bool current = pending && pending->id == r.request.requestId();
        return { WriteObservation{*receipt.value, current && pending->awaitingAcceptance, current && !pending->awaitingAcceptance, r.ioUnconfirmed}, {} };
    }
};
SharedWriter::SharedWriter( std::unique_ptr<Impl> impl ) : impl_( std::move( impl ) ) {}
SharedWriter::~SharedWriter() = default;
DataResult<std::unique_ptr<SharedWriter>> SharedWriter::create(
    const WriteEpoch& epoch, const StreamLimits& limits, const EndpointCapabilities& capabilities, qint64 timeout )
{
    if ( !validEndpoint(epoch.endpointId) ) return { {}, invalid("endpointId") };
    if ( epoch.generation == 0 ) return { {}, invalid("generation") };
    if ( timeout <= 0 ) return { {}, invalid("defaultTimeoutNs") };
    auto impl = std::make_unique<Impl>(WriteEpoch{QString(epoch.endpointId.constData(), epoch.endpointId.size()), epoch.generation}, limits, capabilities, timeout);
    qCDebug(sharedWriterLog, "Created writer generation=%llu timeoutNs=%lld", static_cast<unsigned long long>(epoch.generation), static_cast<long long>(timeout));
    return { std::unique_ptr<SharedWriter>(new SharedWriter(std::move(impl))), {} };
}
DataResult<WriteObservation> SharedWriter::submit( const WriteEpoch& epoch, const WriteRequest& request,
                                                  WriteProducer producer, qint64 now )
{
    auto& s = *impl_;
    auto error = s.context(epoch,now);
    if(!error.code.isEmpty()) return { {}, error };
    if(request.connectionGeneration() != epoch.generation) return { {}, rejected("stale_generation") };
    if(producer != WriteProducer::Automation && producer != WriteProducer::Manual) return { {}, rejected("invalid_producer") };
    if(static_cast<quint64>(request.bytes().size()) > s.limits.maximumWriteBytes()) return { {}, rejected("resource_limit") };
    s.now = now;
    const auto previous = s.records.find(request.requestId());
    if(previous != s.records.end()) {
        const auto& r = previous->second;
        if(r.request.bytes() != request.bytes() || r.request.sensitive() != request.sensitive() ||
           r.request.connectionGeneration() != request.connectionGeneration() || r.request.deadlineNs() != request.deadlineNs() || r.producer != producer)
            return { {}, rejected("request_conflict") };
        qCDebug(sharedWriterLog, "Retained retry generation=%llu", static_cast<unsigned long long>(epoch.generation));
        return s.observe(r);
    }
    if(s.closed || s.faulted || s.pendingMode) return { {}, rejected("endpoint_busy") };
    if(!s.capabilities.canWrite() || s.capabilities.messageBoundaries()) return { {}, rejected("unsupported_capability") };
    if((s.mode == WriteMode::Automatic) != (producer == WriteProducer::Automation)) return { {}, rejected("endpoint_busy") };
    if(!request.deadlineNs() && now > std::numeric_limits<qint64>::max() - s.defaultTimeoutNs) return { {}, rejected("invalid_deadline") };
    const auto deadline = request.deadlineNs().value_or(now <= std::numeric_limits<qint64>::max()-s.defaultTimeoutNs ? now+s.defaultTimeoutNs : now);
    if(deadline <= now) return { {}, rejected("deadline_exceeded") };
    const auto bytes = static_cast<quint64>(request.bytes().size());
    const auto current = state();
    if(current.residentBytes > s.limits.maximumPendingTxBytes()-bytes || s.records.size() >= s.limits.maximumReceipts()) return { {}, rejected("queue_full") };
    auto inserted = s.records.emplace(request.requestId(), Impl::Record{request, producer, deadline});
    s.queue.push_back(request.requestId());
    qCDebug(sharedWriterLog, "Admitted generation=%llu requests=%llu residentBytes=%llu deadlineNs=%lld", static_cast<unsigned long long>(epoch.generation), static_cast<unsigned long long>(s.records.size()), static_cast<unsigned long long>(current.residentBytes+bytes), static_cast<long long>(deadline));
    return s.observe(inserted.first->second);
}
DataResult<WriteDispatch> SharedWriter::take( const WriteEpoch& epoch, qint64 now )
{
    auto& s = *impl_;
    auto error = s.context(epoch,now);
    if(!error.code.isEmpty()) return { {}, error };
    s.now = now;
    if(s.closed || s.faulted || s.pendingMode || s.pending || s.blocked || s.queue.empty()) {
        qCDebug(sharedWriterLog, "Dispatch idle closed=%d faulted=%d pendingMode=%d pendingIO=%d blocked=%d queued=%llu", s.closed,s.faulted,s.pendingMode.has_value(),s.pending.has_value(),s.blocked.has_value(),static_cast<unsigned long long>(s.queue.size()));
        return { WriteDispatch{}, {} };
    }
    if(s.sequence == std::numeric_limits<quint64>::max()) return { {}, rejected("resource_limit") };
    const auto& id = s.queue.front();
    const auto& r = s.records.at(id);
    const auto size = std::min(s.limits.maximumChunkBytes(), static_cast<quint64>(r.request.bytes().size())-r.accepted);
    WriteTicket ticket{s.epoch,++s.sequence};
    s.pending = Impl::Pending{id,ticket,size};
    qCDebug(sharedWriterLog,"Offered generation=%llu ticket=%llu offset=%llu bytes=%llu",static_cast<unsigned long long>(epoch.generation),static_cast<unsigned long long>(ticket.sequence),static_cast<unsigned long long>(r.accepted),static_cast<unsigned long long>(size));
    return { WriteDispatch{WriteOffer(ticket,id,r.accepted,r.request.bytes().mid(static_cast<int>(r.accepted),static_cast<int>(size)))}, {} };
}
DataStatus SharedWriter::accepted( const WriteTicket& ticket, qint64 count, qint64 now ) {
    auto& s = *impl_;
    auto error = s.ticket(ticket);
    if(!error.code.isEmpty()) return {false,error};
    if(!s.pending->awaitingAcceptance || count < 0 || static_cast<quint64>(count) > s.pending->offered) return {false,rejected("invalid_count")};
    error = s.context(ticket.epoch,now);
    if(!error.code.isEmpty()) return {false,error};
    s.now = now;
    auto& r = s.records.at(s.pending->id);
    r.accepted += static_cast<quint64>(count);
    qCDebug(sharedWriterLog,"Acceptance ticket=%llu count=%lld total=%llu",static_cast<unsigned long long>(ticket.sequence),static_cast<long long>(count),static_cast<unsigned long long>(r.accepted));
    if(count == 0) { s.blocked = s.pending->id; s.pending.reset(); }
    else s.pending->awaitingAcceptance = false;
    s.settle();
    return {true,{}};
}
DataStatus SharedWriter::written( const WriteTicket& ticket, qint64 count, qint64 now ) {
    auto& s = *impl_;
    auto error = s.ticket(ticket);
    if(!error.code.isEmpty()) return {false,error};
    auto& r = s.records.at(s.pending->id);
    if(s.pending->awaitingAcceptance || count <= 0 || static_cast<quint64>(count) > r.accepted-r.written) return {false,rejected("invalid_count")};
    error = s.context(ticket.epoch,now);
    if(!error.code.isEmpty()) return {false,error};
    s.now = now;
    r.written += static_cast<quint64>(count);
    qCDebug(sharedWriterLog,"Written ticket=%llu count=%lld total=%llu",static_cast<unsigned long long>(ticket.sequence),static_cast<long long>(count),static_cast<unsigned long long>(r.written));
    if(r.written == r.accepted) s.pending.reset();
    s.settle();
    return {true,{}};
}
DataStatus SharedWriter::failed( const WriteTicket&, qint64 ) { return { false, absent() }; }
DataStatus SharedWriter::ready( const WriteEpoch& epoch, qint64 now ) {
    auto error = impl_->context(epoch,now);
    if(!error.code.isEmpty()) return {false,error};
    impl_->now=now; impl_->blocked.reset();
    qCDebug(sharedWriterLog,"Ready generation=%llu",static_cast<unsigned long long>(epoch.generation));
    return {true,{}};
}
DataStatus SharedWriter::cancel( const WriteEpoch&, const QString&, qint64 ) { return { false, absent() }; }
DataStatus SharedWriter::requestMode( const WriteEpoch&, WriteMode, qint64 ) { return { false, absent() }; }
DataStatus SharedWriter::close( const WriteEpoch&, qint64 ) { return { false, absent() }; }
DataStatus SharedWriter::resetAfterBackendClosed( const WriteEpoch&, quint64,
                                                const EndpointCapabilities&, qint64 )
{ return { false, absent() }; }
DataStatus SharedWriter::advance( const WriteEpoch&, qint64 ) { return { false, absent() }; }
DataResult<WriteObservation> SharedWriter::getWrite( const QString& id ) const {
    const auto found = impl_->records.find(id);
    if(found == impl_->records.end()) return { {}, rejected("not_found") };
    return impl_->observe(found->second);
}
SharedWriterState SharedWriter::state() const {
    SharedWriterState result;
    const auto& s = *impl_;
    result.epoch = s.epoch; result.mode = s.mode; result.pendingMode = s.pendingMode;
    result.closed = s.closed; result.faulted = s.faulted;
    result.backpressured = s.blocked.has_value();
    result.awaitingAcceptance = s.pending && s.pending->awaitingAcceptance;
    result.awaitingDrain = s.pending && !s.pending->awaitingAcceptance;
    for(const auto& entry : s.records) {
        const auto& r = entry.second;
        auto bytes = static_cast<quint64>(r.request.bytes().size());
        if(r.retiredAt) { result.retainedBytes += bytes; ++result.retainedRequests; }
        else {
            result.pendingBytes += bytes; ++result.pendingRequests;
            if(!r.terminal() && r.deadline > s.now && (!result.nextWakeNs || r.deadline < *result.nextWakeNs)) result.nextWakeNs = r.deadline;
        }
    }
    result.residentBytes = result.pendingBytes + result.retainedBytes;
    return result;
}
} // namespace cilogg::communication
