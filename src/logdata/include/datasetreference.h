// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dataresult.h"
#include <QVariantMap>

namespace DatasetReferenceLimits {
constexpr int MaxIdUnits = 256;
constexpr int MaxTokenUnits = 256;
} // namespace DatasetReferenceLimits
class DatasetRevisionStamp {
public:
    const QString& jobId() const
    {
        return jobId_;
    }
    quint64 datasetRevision() const
    {
        return datasetRevision_;
    }
    quint64 requestRevision() const
    {
        return requestRevision_;
    }
    quint64 sourceManifestRevision() const
    {
        return sourceManifestRevision_;
    }
    const QString& schemaId() const
    {
        return schemaId_;
    }

private:
    DatasetRevisionStamp( QString, quint64, quint64, quint64, QString );
    QString jobId_;
    quint64 datasetRevision_;
    quint64 requestRevision_;
    quint64 sourceManifestRevision_;
    QString schemaId_;
    friend DataResult<DatasetRevisionStamp> makeDatasetRevisionStamp( QString, quint64, quint64,
                                                                      quint64, QString );
};
enum class DatasetReferenceKind { Cursor, Lineage };
class DatasetReference {
public:
    DatasetReferenceKind kind() const
    {
        return kind_;
    }
    const DatasetRevisionStamp& revision() const
    {
        return revision_;
    }
    const QString& token() const
    {
        return token_;
    }

private:
    DatasetReference( DatasetReferenceKind, DatasetRevisionStamp, QString );
    DatasetReferenceKind kind_;
    DatasetRevisionStamp revision_;
    QString token_;
    friend DataResult<DatasetReference> makeDatasetReference( DatasetReferenceKind,
                                                              DatasetRevisionStamp, QString );
};
// The existing job owner supplies this check. No registry or I/O is implemented here.
// Check the kind, token AND issued revision binding against bounded retained state.
class IDatasetReferenceLookup {
public:
    virtual ~IDatasetReferenceLookup() = default;
    virtual cilogg::DataStatus check( DatasetReferenceKind, const DatasetRevisionStamp&,
                                      const QString& token ) const = 0;
};
DataResult<DatasetRevisionStamp> makeDatasetRevisionStamp( QString jobId, quint64 datasetRevision,
                                                           quint64 requestRevision,
                                                           quint64 sourceManifestRevision,
                                                           QString schemaId );
bool sameDatasetRevision( const DatasetRevisionStamp&, const DatasetRevisionStamp& );
DataResult<DatasetReference> makeDatasetReference( DatasetReferenceKind, DatasetRevisionStamp,
                                                   QString token );
// Point-in-time validation only: the job owner must pin this retained immutable epoch,
// or validate and consume under the same lock, when reading any referenced data.
cilogg::DataStatus validateDatasetReference( const DatasetReference&,
                                             const DatasetRevisionStamp& current,
                                             const IDatasetReferenceLookup& );
QVariantMap encodeDatasetRevision( const DatasetRevisionStamp& );
DataResult<DatasetRevisionStamp> decodeDatasetRevision( const QVariantMap& );
QVariantMap encodeDatasetReference( const DatasetReference& );
DataResult<DatasetReference> decodeDatasetReference( const QVariantMap& );
// As with other checked value objects, moved-from values may only be destroyed or assigned.
