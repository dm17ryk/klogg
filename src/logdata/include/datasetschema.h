// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fieldvalue.h"
#include <QVector>

enum class DatasetRole {
    Unspecified,
    Dimension,
    Measure,
    Time,
    Source,
    Event,
    LowerBound,
    UpperBound,
    Count,
    Numerator,
    Denominator,
    Duration,
    HierarchyPath,
    EdgeSource,
    EdgeTarget,
    Lineage
};
enum class DatasetShape { Table };
struct DatasetColumn {
    QString fieldId;
    QString name;
    FieldType type = FieldType::String;
    QString unit;
    DatasetRole semanticRole = DatasetRole::Unspecified;
    QString clockDomain;
    bool nullable = false;
    bool repeated = false;
};
namespace DatasetSchemaLimits {
constexpr int MaxColumns = 64;
constexpr int MaxFieldIdUnits = 128;
constexpr int MaxNameUnits = 256;
constexpr int MaxUnitUnits = 64;
constexpr int MaxClockDomainUnits = 256;
constexpr int MaxMetadataUtf8Bytes = 32 * 1024;
} // namespace DatasetSchemaLimits
// Checked metadata only: it does not enable reserved FieldValue payload types.
// A moved-from schema may only be destroyed or assigned a new checked value.
class DatasetSchema {
public:
    const QVector<DatasetColumn>& columns() const
    {
        return columns_;
    }
    const QString& schemaId() const
    {
        return schemaId_;
    }
    DatasetShape shape() const
    {
        return DatasetShape::Table;
    }
    quint32 schemaVersion() const
    {
        return 1;
    }
    quint32 shapeVersion() const
    {
        return 1;
    }

private:
    DatasetSchema( QVector<DatasetColumn>, QString id );
    QVector<DatasetColumn> columns_;
    QString schemaId_;
    friend DataResult<DatasetSchema> makeDatasetSchema( QVector<DatasetColumn>, DatasetShape,
                                                        quint32 );
};
QString datasetRoleName( DatasetRole );
DataResult<DatasetSchema> makeDatasetSchema( QVector<DatasetColumn>,
                                             DatasetShape = DatasetShape::Table,
                                             quint32 shapeVersion = 1 );
QVariantMap encodeDatasetSchema( const DatasetSchema& );
DataResult<DatasetSchema> decodeDatasetSchema( const QVariantMap& );
