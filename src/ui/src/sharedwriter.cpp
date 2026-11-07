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
    Impl( WriteEpoch e, StreamLimits l, EndpointCapabilities c, qint64 timeout )
        : epoch( std::move(e) ), limits(std::move(l)), capabilities(std::move(c)), defaultTimeoutNs(timeout) {}
    DataError context(const WriteEpoch& e, qint64 time) const {
        if(e.endpointId != epoch.endpointId || e.generation != epoch.generation) return rejected("stale_generation");
        if(time < 0 || time < now) return rejected("invalid_clock");
        return {};
    }
    DataResult<WriteObservation> observe(const Record& r) const {
        WriteStage stage = WriteStage::Queued;
        if(r.failure != WriteFailure::None) stage = r.failure == WriteFailure::Cancelled ? WriteStage::Cancelled : WriteStage::Failed;
        else if(r.written == static_cast<quint64>(r.request.bytes().size())) stage = WriteStage::WrittenToBackend;
        else if(r.accepted > 0) stage = WriteStage::Accepted;
        auto receipt = WriteReceipt::forRequest(r.request, epoch.endpointId, stage, r.accepted, r.written, r.failure, limits);
        if(!receipt.value) return { {}, receipt.error };
        return { WriteObservation{*receipt.value, false, false, r.ioUnconfirmed}, {} };
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
DataResult<WriteDispatch> SharedWriter::take( const WriteEpoch&, qint64 )
{ return { {}, absent() }; }
DataStatus SharedWriter::accepted( const WriteTicket&, qint64, qint64 ) { return { false, absent() }; }
DataStatus SharedWriter::written( const WriteTicket&, qint64, qint64 ) { return { false, absent() }; }
DataStatus SharedWriter::failed( const WriteTicket&, qint64 ) { return { false, absent() }; }
DataStatus SharedWriter::ready( const WriteEpoch&, qint64 ) { return { false, absent() }; }
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
