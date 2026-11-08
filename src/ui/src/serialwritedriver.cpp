// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "serialwritedriver.h"
#include <QLoggingCategory>
#include <QThread>
#include <limits>

Q_LOGGING_CATEGORY( serialWriteDriverLog, "cilogg.communication.driver" )

namespace cilogg::communication {
namespace {
DataStatus reject( const char* reason )
{
    qCDebug( serialWriteDriverLog, "Driver reject reason=%s", reason );
    return { false,
             { QString::fromLatin1( reason ), "Serial driver rejected the operation", {}, {} } };
}
bool sameEpoch( const WriteEpoch& a, const WriteEpoch& b )
{
    return a.generation == b.generation && a.endpointId == b.endpointId;
}
} // namespace
SerialWriteDriver::SerialWriteDriver( QIODevice& device, SerialWriteContext context )
    : device_( &device )
    , context_( std::move( context ) )
{
    connect( &device, &QIODevice::bytesWritten, this, &SerialWriteDriver::progress );
    connect( &device, &QObject::destroyed, this, [ this ] {
        device_ = nullptr;
        fault_ = true;
        dirty_ = true;
        qCDebug( serialWriteDriverLog, "Borrowed device destroyed generation=%llu",
                 static_cast<unsigned long long>( context_.epoch.generation ) );
        publish();
    } );
}
DataResult<std::unique_ptr<SerialWriteDriver>>
SerialWriteDriver::create( QIODevice& device, const SerialWriteContext& context )
{
    if ( device.thread() != QThread::currentThread() )
        return { {}, reject( "wrong_thread" ).error };
    if ( !device.isOpen() || !device.isWritable() || device.bytesToWrite() != 0 )
        return { {}, reject( "device_unavailable" ).error };
    if ( !context.policy || context.epoch.endpointId.isEmpty() || context.epoch.generation == 0 )
        return { {}, reject( "invalid_context" ).error };
    qCDebug( serialWriteDriverLog, "Borrowed open device generation=%llu",
             static_cast<unsigned long long>( context.epoch.generation ) );
    return { std::unique_ptr<SerialWriteDriver>( new SerialWriteDriver( device, context ) ), {} };
}
DataStatus SerialWriteDriver::validate( const WriteEpoch& epoch ) const
{
    if ( thread() != QThread::currentThread() )
        return reject( "wrong_thread" );
    if ( !sameEpoch( epoch, context_.epoch ) )
        return reject( "stale_generation" );
    return { true, {} };
}
DataStatus SerialWriteDriver::offer( SerialWriteEnvelopePtr envelope )
{
    const auto valid = validate( context_.epoch );
    if ( !valid.ok )
        return valid;
    if ( !envelope || !sameEpoch( envelope->offer.ticket().epoch, context_.epoch ) )
        return reject( "stale_generation" );
    if ( !device_ || retired_ || fault_ )
        return reject( "device_unavailable" );
    if ( executing_ || current_ || inFlight_ )
        return reject( "endpoint_busy" );
    const auto size = envelope->offer.bytes().size();
    if ( size <= 0 || size > 64 * 1024 )
        return reject( "resource_limit" );
    if ( envelope->offer.ticket().sequence <= lastOffered_ )
        return reject( "stale_ticket" );
    lastOffered_ = envelope->offer.ticket().sequence;
    current_ = std::move( envelope );
    acceptance_.reset();
    written_ = 0;
    earlyWritten_ = 0;
    unsubmitted_ = UnsubmittedReason::None;
    const auto policy = context_.policy->load();
    const auto& stamp = current_->stamp;
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now() - context_.origin )
                         .count();
    if ( closing_ )
        unsubmitted_ = UnsubmittedReason::Closing;
    else if ( current_->deadlineNs <= now )
        unsubmitted_ = UnsubmittedReason::DeadlineExceeded;
    else if ( !policy || !policy->connectionReady
              || policy->connectionGeneration != context_.epoch.generation
              || policy->actionsGeneration != stamp.actionsGeneration
              || ( stamp.automaticResponse
                   && ( policy->autoResponsesGeneration != stamp.autoResponsesGeneration
                        || !policy->actionsEnabled || !policy->autoResponsesEnabled
                        || !policy->globalAutoResponsesEnabled ) ) )
        unsubmitted_ = UnsubmittedReason::PolicyChanged;
    if ( unsubmitted_ != UnsubmittedReason::None ) {
        dirty_ = true;
        qCDebug( serialWriteDriverLog, "Offer not submitted generation=%llu ticket=%llu reason=%d",
                 static_cast<unsigned long long>( context_.epoch.generation ),
                 static_cast<unsigned long long>( current_->offer.ticket().sequence ),
                 static_cast<int>( unsubmitted_ ) );
        publish();
        return { true, {} };
    }
    executing_ = true;
    QPointer<SerialWriteDriver> guard( this );
    // Keep the bounded offer alive even if a backend callback deletes this driver.
    const auto owned = current_;
    const auto count = device_->write( owned->offer.bytes() );
    if ( !guard )
        return reject( "observer_deleted" );
    executing_ = false;
    if ( count < -1 || count > size ) {
        fault_ = true;
        earlyWritten_ = 0;
        qCDebug( serialWriteDriverLog, "Invalid backend acceptance count=%lld offered=%lld",
                 static_cast<long long>( count ), static_cast<long long>( size ) );
    }
    else {
        acceptance_ = count;
        if ( count < 0 )
            fault_ = true;
    }
    dirty_ = true;
    qCDebug( serialWriteDriverLog, "Write returned generation=%llu ticket=%llu count=%lld fault=%d",
             static_cast<unsigned long long>( context_.epoch.generation ),
             static_cast<unsigned long long>( owned->offer.ticket().sequence ),
             static_cast<long long>( count ), fault_ );
    const auto early = earlyWritten_;
    earlyWritten_ = 0;
    if ( early ) {
        if ( !acceptance_ || *acceptance_ < 0 || early > static_cast<quint64>( *acceptance_ ) )
            fault_ = true;
        else {
            written_ = early;
            const auto bytes = owned->offer.bytes().left( static_cast<int>( early ) );
            Q_EMIT writtenPrefix( bytes, owned->offer.requestId() );
            if ( !guard )
                return { true, {} };
        }
    }
    publish();
    return { true, {} };
}
bool SerialWriteDriver::resolved() const
{
    return unsubmitted_ != UnsubmittedReason::None
           || ( acceptance_
                && ( *acceptance_ <= 0 || written_ == static_cast<quint64>( *acceptance_ ) ) );
}
void SerialWriteDriver::publish()
{
    if ( executing_ || retired_ || inFlight_ || !dirty_ )
        return;
    if ( delivery_ == std::numeric_limits<quint64>::max() ) {
        fault_ = true;
        closing_ = true;
        qCDebug( serialWriteDriverLog, "Delivery counter exhausted; require retirement" );
        return;
    }
    auto snapshot = BackendWriteSnapshotPtr::create( BackendWriteSnapshot{
        current_ ? current_->offer.ticket() : WriteTicket{ context_.epoch, 0 }, ++delivery_,
        acceptance_, written_, unsubmitted_, fault_, false } );
    inFlight_ = delivery_;
    dirty_ = false;
    qCDebug( serialWriteDriverLog, "Publish delivery=%llu written=%llu fault=%d",
             static_cast<unsigned long long>( delivery_ ),
             static_cast<unsigned long long>( written_ ), fault_ );
    Q_EMIT snapshotReady( snapshot ); // No member access after an observer can delete us.
}
void SerialWriteDriver::progress( qint64 count )
{
    if ( retired_ )
        return;
    if ( executing_ ) {
        if ( count <= 0 || static_cast<quint64>( count ) > 64 * 1024 - earlyWritten_ )
            fault_ = true;
        else
            earlyWritten_ += static_cast<quint64>( count );
        qCDebug( serialWriteDriverLog, "Staged early progress count=%lld fault=%d",
                 static_cast<long long>( count ), fault_ );
        return;
    }
    if ( !current_ || !acceptance_ || *acceptance_ <= 0 || count <= 0
         || static_cast<quint64>( count ) > static_cast<quint64>( *acceptance_ ) - written_ ) {
        fault_ = true;
        dirty_ = true;
        qCDebug( serialWriteDriverLog, "Rejected uncredited progress count=%lld",
                 static_cast<long long>( count ) );
        publish();
        return;
    }
    const auto bytes
        = current_->offer.bytes().mid( static_cast<int>( written_ ), static_cast<int>( count ) );
    const auto id = current_->offer.requestId();
    written_ += static_cast<quint64>( count );
    dirty_ = true;
    qCDebug( serialWriteDriverLog, "Credited progress count=%lld cumulative=%llu",
             static_cast<long long>( count ), static_cast<unsigned long long>( written_ ) );
    QPointer<SerialWriteDriver> guard( this );
    Q_EMIT writtenPrefix( bytes, id );
    if ( guard )
        publish();
}
DataStatus SerialWriteDriver::acknowledgeProgress( const WriteEpoch& epoch, quint64 sequence )
{
    const auto valid = validate( epoch );
    if ( !valid.ok )
        return valid;
    if ( !inFlight_ || sequence != inFlight_ || retired_ )
        return reject( "stale_delivery" );
    inFlight_ = 0;
    qCDebug( serialWriteDriverLog, "Acknowledged delivery=%llu dirty=%d",
             static_cast<unsigned long long>( sequence ), dirty_ );
    if ( dirty_ ) {
        publish();
        return { true, {} };
    }
    if ( resolved() ) {
        current_.reset();
        acceptance_.reset();
        written_ = 0;
        unsubmitted_ = UnsubmittedReason::None;
    }
    return { true, {} };
}
DataStatus SerialWriteDriver::beginClose( const WriteEpoch& epoch )
{
    const auto valid = validate( epoch );
    if ( !valid.ok )
        return valid;
    closing_ = true;
    qCDebug( serialWriteDriverLog, "Closing generation=%llu executing=%d",
             static_cast<unsigned long long>( epoch.generation ), executing_ );
    return { true, {} };
}
DataStatus SerialWriteDriver::backendClosed( const WriteEpoch& epoch )
{
    const auto valid = validate( epoch );
    if ( !valid.ok )
        return valid;
    if ( retired_ )
        return { true, {} };
    if ( !closing_ || executing_ || ( device_ && device_->isOpen() ) )
        return reject( "invalid_retirement" );
    if ( device_ )
        disconnect( device_, nullptr, this, nullptr );
    retired_ = true;
    dirty_ = false;
    inFlight_ = 0;
    if ( delivery_ != std::numeric_limits<quint64>::max() )
        ++delivery_;
    auto snapshot = BackendWriteSnapshotPtr::create(
        BackendWriteSnapshot{ current_ ? current_->offer.ticket() : WriteTicket{ epoch, 0 },
                              delivery_, acceptance_, written_, unsubmitted_, fault_, true } );
    current_.reset();
    qCDebug( serialWriteDriverLog, "Retired generation=%llu delivery=%llu written=%llu",
             static_cast<unsigned long long>( epoch.generation ),
             static_cast<unsigned long long>( delivery_ ),
             static_cast<unsigned long long>( written_ ) );
    // One bounded terminal control message bypasses progress credit: shutdown cannot wait on GUI
    // ACK.
    Q_EMIT snapshotReady( snapshot );
    return { true, {} };
}
DataStatus SerialWriteDriver::backendError( const WriteEpoch& epoch )
{
    const auto valid = validate( epoch );
    if ( !valid.ok )
        return valid;
    if ( retired_ )
        return reject( "stale_generation" );
    fault_ = true;
    dirty_ = true;
    qCDebug( serialWriteDriverLog, "Backend error generation=%llu executing=%d",
             static_cast<unsigned long long>( epoch.generation ), executing_ );
    publish();
    return { true, {} };
}
} // namespace cilogg::communication
