#pragma once

#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QVector>

enum class PreviewBufferType { String, HexString, Base64, Bin, Bytes };
enum class PreviewFormat { Fields, String, Dig, Dec, Hex, Bin, Enum, Flags, Bitfield };
enum class PreviewFieldSource { Buffer, Capture };

struct PreviewValueExpr {
    bool isSet = false;
    bool isLiteral = false;
    int literalValue = 0;
    QString expression;
};

struct PreviewCaptureRef {
    bool isSet = false;
    bool isIndex = false;
    int index = 0;
    QString name;
};

struct PreviewFieldSpec {
    QString name;
    PreviewFieldSource source = PreviewFieldSource::Buffer;
    PreviewCaptureRef capture;
    PreviewValueExpr offset;
    PreviewValueExpr width;
    PreviewBufferType type = PreviewBufferType::Bytes;
    PreviewFormat format = PreviewFormat::String;
    QString endianness;
    QMap<QString, QString> enumMap;
    QMap<QString, QString> flagMap;
    QVector<PreviewFieldSpec> fields;
    QVector<PreviewFieldSpec> bitfieldMap;
};

struct PreviewDefinition {
    QString name;
    QString regex;
    QRegularExpression compiled;
    bool enabled = true;
    bool hasEnabled = false;
    PreviewCaptureRef bufferCapture;
    PreviewValueExpr offset;
    PreviewBufferType type = PreviewBufferType::String;
    PreviewFormat format = PreviewFormat::Fields;
    QVector<PreviewFieldSpec> fields;
};

QString previewBufferTypeToString( PreviewBufferType type );
QString previewFormatToString( PreviewFormat format );
QString previewFieldSourceToString( PreviewFieldSource source );
PreviewBufferType previewBufferTypeFromString( const QString& value, bool* ok );
PreviewFormat previewFormatFromString( const QString& value, bool* ok );
PreviewFieldSource previewFieldSourceFromString( const QString& value, bool* ok );
