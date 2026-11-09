// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "serialwritebridge.h"
#include <QIODevice>
#include <QObject>
#include <QPointer>

class SerialCaptureWorker;
namespace cilogg::communication {
class SerialWriteDriver final : public QObject {
    Q_OBJECT
public:
    static DataResult<std::unique_ptr<SerialWriteDriver>> create( QIODevice&,
                                                                  const SerialWriteContext& );
    DataStatus offer( SerialWriteEnvelopePtr );
    DataStatus acknowledgeProgress( const WriteEpoch&, quint64 deliverySequence );
    DataStatus beginClose( const WriteEpoch& );
    DataStatus backendClosed( const WriteEpoch& );
    DataStatus backendError( const WriteEpoch& );
Q_SIGNALS:
    void snapshotReady( cilogg::communication::BackendWriteSnapshotPtr snapshot );

private:
    friend class ::SerialCaptureWorker;
    explicit SerialWriteDriver( QIODevice&, SerialWriteContext );
    Q_SIGNAL void writtenPrefix( const QByteArray& bytes, const QString& requestId );
    DataStatus validate( const WriteEpoch& ) const;
    void progress( qint64 );
    void publish();
    bool resolved() const;
    QPointer<QIODevice> device_;
    SerialWriteContext context_;
    SerialWriteEnvelopePtr current_;
    std::optional<qint64> acceptance_;
    quint64 written_ = 0;
    quint64 earlyWritten_ = 0;
    quint64 delivery_ = 0;
    quint64 lastOffered_ = 0;
    quint64 inFlight_ = 0;
    UnsubmittedReason unsubmitted_ = UnsubmittedReason::None;
    bool executing_ = false;
    bool closing_ = false;
    bool retired_ = false;
    bool fault_ = false;
    bool dirty_ = false;
};
} // namespace cilogg::communication
