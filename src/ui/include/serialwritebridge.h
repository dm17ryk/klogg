// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sharedwriter.h"
#include <QMetaType>
#include <QSharedPointer>
#include <atomic>
#include <chrono>

namespace cilogg::communication {
struct ActionWriteStamp {
    bool automaticResponse = false;
    quint64 actionsGeneration = 0;
    quint64 autoResponsesGeneration = 0;
};
struct StreamWritePolicySnapshot {
    quint64 connectionGeneration = 0;
    quint64 actionsGeneration = 0;
    quint64 autoResponsesGeneration = 0;
    bool connectionReady = false;
    bool actionsEnabled = false;
    bool autoResponsesEnabled = false;
    bool globalAutoResponsesEnabled = false;
};

// The session alone publishes. Readers see all revisions and flags together.
class StreamWritePolicyPublication final {
public:
    void publish( StreamWritePolicySnapshot snapshot )
    {
        std::atomic_store( &snapshot_,
                           std::make_shared<const StreamWritePolicySnapshot>( snapshot ) );
    }
    std::shared_ptr<const StreamWritePolicySnapshot> load() const
    {
        return std::atomic_load( &snapshot_ );
    }

private:
    std::shared_ptr<const StreamWritePolicySnapshot> snapshot_;
};

struct SerialWriteContext {
    WriteEpoch epoch;
    std::chrono::steady_clock::time_point origin;
    std::shared_ptr<StreamWritePolicyPublication> policy;
};
// Private, bounded transport data. Never expose/serialize an envelope in public automation.
struct SerialWriteEnvelope {
    WriteOffer offer;
    ActionWriteStamp stamp;
    qint64 deadlineNs;
};
enum class UnsubmittedReason { None, PolicyChanged, DeadlineExceeded, Closing };
struct BackendWriteSnapshot {
    WriteTicket ticket;
    quint64 deliverySequence = 0;
    std::optional<qint64> acceptance;
    quint64 written = 0;
    UnsubmittedReason unsubmitted = UnsubmittedReason::None;
    bool fault = false;
    bool retired = false;
};
using SerialWriteEnvelopePtr = QSharedPointer<const SerialWriteEnvelope>;
using BackendWriteSnapshotPtr = QSharedPointer<const BackendWriteSnapshot>;
} // namespace cilogg::communication

Q_DECLARE_METATYPE( cilogg::communication::SerialWriteEnvelopePtr )
Q_DECLARE_METATYPE( cilogg::communication::BackendWriteSnapshotPtr )
