// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dataresult.h"

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <optional>
#include <utility>

enum class EndpointKind { Serial, TcpClient, TcpServer, Udp, NamedPipe, Hid, Replay };

struct StreamLimitsDefinition {
    quint32 maximumEndpoints = 16;
    quint32 maximumReceipts = 256;
    quint64 maximumRxBytes = 8 * 1024 * 1024;
    quint64 maximumPendingTxBytes = 8 * 1024 * 1024;
    quint64 maximumChunkBytes = 64 * 1024;
    quint64 maximumWriteBytes = 1024 * 1024;
    qint64 receiptRetentionMs = 600000;
};

class StreamLimits {
public:
    static DataResult<StreamLimits> fromDefinition( const StreamLimitsDefinition& definition );
    quint32 maximumEndpoints() const
    {
        return definition_.maximumEndpoints;
    }
    quint32 maximumReceipts() const
    {
        return definition_.maximumReceipts;
    }
    quint64 maximumRxBytes() const
    {
        return definition_.maximumRxBytes;
    }
    quint64 maximumPendingTxBytes() const
    {
        return definition_.maximumPendingTxBytes;
    }
    quint64 maximumChunkBytes() const
    {
        return definition_.maximumChunkBytes;
    }
    quint64 maximumWriteBytes() const
    {
        return definition_.maximumWriteBytes;
    }
    qint64 receiptRetentionMs() const
    {
        return definition_.receiptRetentionMs;
    }

private:
    explicit StreamLimits( StreamLimitsDefinition definition )
        : definition_( definition )
    {
    }
    StreamLimitsDefinition definition_;
};

struct EndpointCapabilitiesDefinition {
    bool canRead = false;
    bool canWrite = false;
    bool messageBoundaries = false;
    bool canControlSignals = false;
    bool canSendBreak = false;
    bool canObserveBreak = false;
    bool lineErrors = false;
    bool hardwareTimestamps = false;
    QStringList limitations;
};

class EndpointCapabilities {
public:
    static DataResult<EndpointCapabilities>
    fromDefinition( const EndpointCapabilitiesDefinition& definition );
    bool canRead() const
    {
        return definition_.canRead;
    }
    bool canWrite() const
    {
        return definition_.canWrite;
    }
    bool messageBoundaries() const
    {
        return definition_.messageBoundaries;
    }
    bool canControlSignals() const
    {
        return definition_.canControlSignals;
    }
    bool canSendBreak() const
    {
        return definition_.canSendBreak;
    }
    bool canObserveBreak() const
    {
        return definition_.canObserveBreak;
    }
    bool lineErrors() const
    {
        return definition_.lineErrors;
    }
    bool hardwareTimestamps() const
    {
        return definition_.hardwareTimestamps;
    }
    const QStringList& limitations() const
    {
        return definition_.limitations;
    }

private:
    explicit EndpointCapabilities( EndpointCapabilitiesDefinition definition )
        : definition_( std::move( definition ) )
    {
    }
    EndpointCapabilitiesDefinition definition_;
};

struct WriteRequestDefinition {
    QString requestId;
    QByteArray bytes;
    bool sensitive = false;
    quint64 connectionGeneration = 0;
    std::optional<qint64> deadlineNs;
};

class WriteRequest {
public:
    static DataResult<WriteRequest> fromDefinition( const WriteRequestDefinition& definition,
                                                    const StreamLimits& limits );
    const QString& requestId() const
    {
        return definition_.requestId;
    }
    const QByteArray& bytes() const
    {
        return definition_.bytes;
    }
    bool sensitive() const
    {
        return definition_.sensitive;
    }
    quint64 connectionGeneration() const
    {
        return definition_.connectionGeneration;
    }
    std::optional<qint64> deadlineNs() const
    {
        return definition_.deadlineNs;
    }

private:
    explicit WriteRequest( WriteRequestDefinition definition )
        : definition_( std::move( definition ) )
    {
    }
    WriteRequestDefinition definition_;
};

enum class WriteStage { Queued, Accepted, WrittenToBackend, Failed, Cancelled };
enum class PayloadDisposition { Omitted, Redacted };
enum class WriteFailure {
    None,
    UnsupportedCapability,
    EndpointBusy,
    StaleGeneration,
    QueueFull,
    DeadlineExceeded,
    Cancelled,
    PartialWrite,
    IoError
};

struct WriteReceiptDefinition {
    QString requestId;
    QString endpointId;
    quint64 connectionGeneration = 0;
    WriteStage stage = WriteStage::Queued;
    quint64 requestedBytes = 0;
    quint64 acceptedBytes = 0;
    quint64 writtenBytes = 0;
    PayloadDisposition payloadDisposition = PayloadDisposition::Omitted;
    QString errorCode;
    QString errorMessage;
};

class WriteReceipt {
public:
    static DataResult<WriteReceipt> fromDefinition( const WriteReceiptDefinition& definition,
                                                    const StreamLimits& limits );
    static DataResult<WriteReceipt> forRequest( const WriteRequest& request,
                                                const QString& endpointId, WriteStage stage,
                                                quint64 acceptedBytes, quint64 writtenBytes,
                                                WriteFailure failure, const StreamLimits& limits );
    const QString& requestId() const
    {
        return definition_.requestId;
    }
    const QString& endpointId() const
    {
        return definition_.endpointId;
    }
    quint64 connectionGeneration() const
    {
        return definition_.connectionGeneration;
    }
    WriteStage stage() const
    {
        return definition_.stage;
    }
    quint64 requestedBytes() const
    {
        return definition_.requestedBytes;
    }
    quint64 acceptedBytes() const
    {
        return definition_.acceptedBytes;
    }
    quint64 writtenBytes() const
    {
        return definition_.writtenBytes;
    }
    PayloadDisposition payloadDisposition() const
    {
        return definition_.payloadDisposition;
    }
    const QString& errorCode() const
    {
        return definition_.errorCode;
    }
    const QString& errorMessage() const
    {
        return definition_.errorMessage;
    }

private:
    explicit WriteReceipt( WriteReceiptDefinition definition )
        : definition_( std::move( definition ) )
    {
    }
    WriteReceiptDefinition definition_;
};

DataResult<QJsonObject> encodeWriteReceipt( const WriteReceipt& receipt,
                                            const StreamLimits& limits );
DataResult<WriteReceipt> decodeWriteReceipt( const QJsonObject& object,
                                             const StreamLimits& limits );
