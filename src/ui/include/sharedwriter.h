// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "communicationtypes.h"
#include <memory>

namespace cilogg::communication {

struct WriteEpoch {
    QString endpointId;
    quint64 generation = 0;
};
struct WriteTicket {
    WriteEpoch epoch;
    quint64 sequence = 0;
};
enum class WriteProducer { Automation, Manual };
enum class WriteMode { Automatic, ManualArmed };

class WriteOffer {
public:
    const WriteTicket& ticket() const
    {
        return ticket_;
    }
    const QString& requestId() const
    {
        return requestId_;
    }
    quint64 offset() const
    {
        return offset_;
    }
    const QByteArray& bytes() const
    {
        return bytes_;
    }

private:
    friend class SharedWriter;
    WriteOffer( WriteTicket ticket, QString requestId, quint64 offset, QByteArray bytes )
        : ticket_( std::move( ticket ) )
        , requestId_( std::move( requestId ) )
        , offset_( offset )
        , bytes_( std::move( bytes ) )
    {
    }
    WriteTicket ticket_;
    QString requestId_;
    quint64 offset_;
    QByteArray bytes_;
};
struct WriteDispatch {
    std::optional<WriteOffer> offer;
};
struct WriteObservation {
    WriteReceipt receipt;
    bool awaitingAcceptance = false;
    bool awaitingDrain = false;
    bool ioUnconfirmed = false;
};
struct SharedWriterState {
    WriteEpoch epoch;
    WriteMode mode = WriteMode::Automatic;
    std::optional<WriteMode> pendingMode;
    bool closed = false;
    bool faulted = false;
    bool backpressured = false;
    bool awaitingAcceptance = false;
    bool awaitingDrain = false;
    bool resetRequired = false;
    quint64 pendingBytes = 0;
    quint64 retainedBytes = 0;
    quint64 residentBytes = 0;
    quint32 pendingRequests = 0;
    quint32 retainedRequests = 0;
    std::optional<qint64> nextWakeNs;
};

// Single-owner state machine. The owner supplies clock/I/O events; this class performs no I/O.
// Origin/mode are arbitration inputs, never authorization. Offers are private backend data.
class SharedWriter final {
public:
    static DataResult<std::unique_ptr<SharedWriter>> create( const WriteEpoch&, const StreamLimits&,
                                                             const EndpointCapabilities&,
                                                             qint64 defaultTimeoutNs );
    ~SharedWriter();
    SharedWriter( const SharedWriter& ) = delete;
    SharedWriter& operator=( const SharedWriter& ) = delete;
    SharedWriter( SharedWriter&& ) = delete;
    SharedWriter& operator=( SharedWriter&& ) = delete;

    DataResult<WriteObservation> submit( const WriteEpoch&, const WriteRequest&, WriteProducer,
                                         qint64 nowNs );
    DataResult<WriteDispatch> take( const WriteEpoch&, qint64 nowNs );
    DataStatus accepted( const WriteTicket&, qint64 count, qint64 nowNs );
    DataStatus written( const WriteTicket&, qint64 count, qint64 nowNs );
    DataStatus failed( const WriteTicket&, qint64 nowNs );
    DataStatus ready( const WriteEpoch&, qint64 nowNs );
    DataStatus cancel( const WriteEpoch&, const QString& requestId, qint64 nowNs );
    DataStatus requestMode( const WriteEpoch&, WriteMode, qint64 nowNs );
    DataStatus close( const WriteEpoch&, qint64 nowNs );
    DataStatus resetAfterBackendClosed( const WriteEpoch&, quint64 nextGeneration,
                                        const EndpointCapabilities&, qint64 nowNs );
    DataStatus advance( const WriteEpoch&, qint64 nowNs );
    DataResult<WriteObservation> getWrite( const QString& requestId ) const;
    SharedWriterState state() const;

private:
    struct Impl;
    explicit SharedWriter( std::unique_ptr<Impl> );
    std::unique_ptr<Impl> impl_;
};
} // namespace cilogg::communication
