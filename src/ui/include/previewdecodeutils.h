#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

#include "previewconfig.h"

struct HexParseResult {
    bool ok = false;
    quint64 value = 0;
    int digitCount = 0;
    QString error;
};

struct HexDecodeResult {
    bool ok = false;
    QByteArray bytes;
    int digitCount = 0;
    QString error;
};

struct PreviewExpressionResult {
    bool ok = false;
    qint64 value = 0;
    QString error;
    QString missingVariable;
};

HexParseResult parseHexToU64AllowOddDigits( const QString& input );
HexDecodeResult decodeHexStringToBytes( const QString& input );
PreviewExpressionResult evaluatePreviewExpression( const PreviewValueExpr& expr,
                                                   const QMap<QString, qint64>& values );
