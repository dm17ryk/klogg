// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dataresult.h"
#include <QByteArray>
#include <QStringView>
#include <QVariantMap>
#include <variant>

enum class FieldType {
    Missing,
    Null,
    Bool,
    Int64,
    UInt64,
    Decimal,
    Float64,
    String,
    Bytes,
    Timestamp,
    Duration,
    Array,
    Object,
    Invalid
};

struct MissingValue {};
struct NullValue {};
struct DecimalValue {
    QString coefficient;
    qint32 scale = 0;
};
struct InvalidValue {
    QString reason;
};

using ValuePayload = std::variant<MissingValue, NullValue, bool, qint64, quint64, DecimalValue,
                                  double, QString, QByteArray, InvalidValue>;

namespace FieldValueLimits {
constexpr int MaxTextUnits = 1024 * 1024;
constexpr int MaxBytes = 1024 * 1024;
constexpr int MaxCoefficientDigits = 256;
constexpr int MaxDecimalScale = 6144;
constexpr int MaxReasonUnits = 256;
} // namespace FieldValueLimits

// Only the checked factory can publish a payload. The tag is derived, never mutable.
class FieldValue {
public:
    FieldType tag() const;
    const ValuePayload& payload() const
    {
        return payload_;
    }
    const std::optional<QString>& rawLexeme() const
    {
        return rawLexeme_;
    }

private:
    FieldValue( ValuePayload payload, std::optional<QString> raw )
        : payload_( std::move( payload ) )
        , rawLexeme_( std::move( raw ) )
    {
    }
    ValuePayload payload_;
    std::optional<QString> rawLexeme_;
    friend DataResult<FieldValue> makeFieldValue( ValuePayload, std::optional<QString> );
};

struct FieldSchema {
    QString fieldId;
    QString name;
    QString path;
    QString unit;
    FieldType type = FieldType::String;
    bool nullable = false;
    bool repeated = false;
    QString semanticRole;
    quint64 profileRevision = 0;
};

QString fieldTypeName( FieldType type );
DataResult<FieldValue> makeFieldValue( ValuePayload payload, std::optional<QString> raw = {} );
DataResult<FieldValue> parseTypedValue( QStringView raw, const FieldSchema& schema );
QVariantMap encodeFieldValue( const FieldValue& value );
DataResult<FieldValue> decodeFieldValue( const QVariantMap& wire );
