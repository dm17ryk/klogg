// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "analysisprovenance.h"
#include "dataresult.h"
#include <QVariantMap>
#include <variant>

struct EventIdentityPolicy {
    QString formatId;
    quint64 formatRevision = 0;
    quint64 assemblyRevision = 0;
    quint64 occurrenceOrdinal = 0;
};
// A frozen weak snapshot permits historical decoding without creating live locations.
struct PhysicalEventOrigin {
    SourceSnapshot snapshot;
    LineNumber first = 0_lnum;
    LineNumber last = 0_lnum;
    std::optional<RawByteRange> bytes;
};
struct InlineEventOrigin {
    QString inlineSourceId;
    quint64 firstLine = 0;
    quint64 lastLine = 0;
};
// Metadata only. F16 must verify the actual frozen prefix before materialization.
struct DeferredSourceVerification {
    QString canonicalOrigin;
    QByteArray platformFileId;
    quint64 capturedSize = 0;
    quint64 frozenBytes = 0;
    quint64 completeLineCount = 0;
    std::optional<quint64> provisionalLineStart;
    QByteArray sha256Prefix;
    QString encoding;
    quint64 decodingRevision = 0;
};
struct SourceManifestEntry {
    QString manifestId;
    QString entryId;
    quint64 manifestRevision = 0;
    quint64 contentEpoch = 0;
    DeferredSourceVerification verification;
};
struct DeferredFileOrigin {
    QString manifestId;
    QString entryId;
    quint64 manifestRevision = 0;
    quint64 contentEpoch = 0;
    quint64 firstLine = 0;
    quint64 lastLine = 0;
    std::optional<RawByteRange> bytes;
};
struct ManagedByteOrigin {
    QString resolverKind;
    QString recordingId;
    QString segmentId;
    quint64 generation = 0;
    RawByteRange bytes;
};
using EventOrigin
    = std::variant<PhysicalEventOrigin, InlineEventOrigin, DeferredFileOrigin, ManagedByteOrigin>;
class EventRef {
public:
    const QString& eventKey() const
    {
        return eventKey_;
    }
    const EventOrigin& origin() const
    {
        return origin_;
    }
    const EventIdentityPolicy& policy() const
    {
        return policy_;
    }
    const std::optional<SourceManifestEntry>& manifestEntry() const
    {
        return manifestEntry_;
    }

private:
    EventRef( QString key, EventOrigin origin, EventIdentityPolicy policy,
              std::optional<SourceManifestEntry> manifest );
    static DataResult<EventRef> checked( EventOrigin, const EventIdentityPolicy&,
                                         std::optional<SourceManifestEntry> = {} );
    QString eventKey_;
    EventOrigin origin_;
    EventIdentityPolicy policy_;
    std::optional<SourceManifestEntry> manifestEntry_;
    friend DataResult<EventRef> makeEventRef( const SourceSnapshot&, LineNumber, LineNumber,
                                              std::optional<RawByteRange>,
                                              const EventIdentityPolicy& );
    friend DataResult<EventRef> makeInlineEventRef( const InlineEventOrigin&,
                                                    const EventIdentityPolicy& );
    friend DataResult<EventRef> makeDeferredEventRef( const DeferredFileOrigin&,
                                                      const SourceManifestEntry&,
                                                      const EventIdentityPolicy& );
    friend DataResult<EventRef> makeManagedByteEventRef( const ManagedByteOrigin&,
                                                         const EventIdentityPolicy& );
    friend DataResult<EventRef> decodeEventRef( const QVariantMap& );
};
DataResult<EventRef> makeEventRef( const SourceSnapshot&, LineNumber first, LineNumber last,
                                   std::optional<RawByteRange>, const EventIdentityPolicy& );
DataResult<EventRef> makeInlineEventRef( const InlineEventOrigin&, const EventIdentityPolicy& );
DataResult<EventRef> makeDeferredEventRef( const DeferredFileOrigin&, const SourceManifestEntry&,
                                           const EventIdentityPolicy& );
DataResult<EventRef> makeManagedByteEventRef( const ManagedByteOrigin&,
                                              const EventIdentityPolicy& );
DataStatus validateEventOrigin( const EventRef& );
DataStatus validateEventSource( const SourceSnapshot&, const EventRef& );
bool acceptsEvent( const SourceSnapshot&, const EventRef& );
QVariantMap encodeEventRef( const EventRef& );
DataResult<EventRef> decodeEventRef( const QVariantMap& );
