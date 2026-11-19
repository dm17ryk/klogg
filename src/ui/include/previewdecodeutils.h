#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

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
QString resolveTemplateString( const QString& templateText,
                               const QMap<QString, QString>& values,
                               QStringList* missing = nullptr );
// The generic blocks a missing `at_command_..._block` or `at_response_..._block` falls back to,
// in the order to try. A name in a configuration pack's `<pack-id>/` namespace tries the pack's
// own generic block first.
QStringList previewFallbackBlockNames( const QString& resolvedName );
// The regular expression of the block names a block reference (a name, or a template with
// ${value} or {value} placeholders) can resolve to.
QString previewBlockReferencePattern( const QString& reference );
