#include "previewmessagetab.h"
#include "previewtree.h"
#include "previewservice.h"
#include "previewdecodersettings.h"
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QProcess>
#include <QJsonDocument>
#include <memory>

#include <QComboBox>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTextOption>
#include <QTreeWidget>
#include <QtGlobal>
#include <QVBoxLayout>
#include <limits>

#include "log.h"
#include "previewdecodeutils.h"
#include "previewmanager.h"

namespace {
constexpr int SnippetLimit = 60;
constexpr int ColumnField = 0;
constexpr int ColumnValue = 1;
constexpr int ColumnRaw = 2;
constexpr int ColumnOffset = 3;
constexpr int ColumnWidth = 4;

QString captureValue( const PreviewCaptureRef& capture, const QRegularExpressionMatch& match )
{
    if ( !capture.isSet ) {
        return {};
    }
    if ( capture.isIndex ) {
        return match.captured( capture.index );
    }
    return match.captured( capture.name );
}

int clampToInt( qsizetype value )
{
    if ( value > std::numeric_limits<int>::max() ) {
        return std::numeric_limits<int>::max();
    }
    if ( value < std::numeric_limits<int>::min() ) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>( value );
}

int captureStart( const PreviewCaptureRef& capture, const QRegularExpressionMatch& match )
{
    if ( !capture.isSet ) {
        return -1;
    }
    const auto value = capture.isIndex ? match.capturedStart( capture.index )
                                       : match.capturedStart( capture.name );
    return clampToInt( value );
}

int captureLength( const PreviewCaptureRef& capture, const QRegularExpressionMatch& match )
{
    if ( !capture.isSet ) {
        return -1;
    }
    const auto value = capture.isIndex ? match.capturedLength( capture.index )
                                       : match.capturedLength( capture.name );
    return clampToInt( value );
}

struct DecodeErrorInfo {
    QString previewName;
    QString fieldPath;
    QString source;
    int offset = -1;
    int width = -1;
    QString rawSlice;
    QString reason;
};

QString describeCaptureRef( const PreviewCaptureRef& capture, const QString& prefix )
{
    if ( !capture.isSet ) {
        return prefix;
    }
    if ( capture.isIndex ) {
        return QString( "%1 #%2" ).arg( prefix ).arg( capture.index );
    }
    if ( capture.name.isEmpty() ) {
        return prefix;
    }
    return QString( "%1 %2" ).arg( prefix, capture.name );
}

QString truncateText( const QString& text, int limit )
{
    if ( text.size() <= limit ) {
        return text;
    }
    return text.left( limit ) + "...";
}

QString sliceToLogText( const QByteArray& slice )
{
    if ( slice.isEmpty() ) {
        return {};
    }
    bool printable = true;
    for ( const auto ch : slice ) {
        const unsigned char byte = static_cast<unsigned char>( ch );
        if ( byte < 0x20 || byte > 0x7e ) {
            printable = false;
            break;
        }
    }
    const QString text = printable ? QString::fromLatin1( slice )
                                   : QString::fromLatin1( slice.toHex() );
    return truncateText( text, 64 );
}

QString decodeTooltip( const DecodeErrorInfo& info )
{
    QStringList lines;
    if ( !info.previewName.isEmpty() ) {
        lines << QObject::tr( "Preview: %1" ).arg( info.previewName );
    }
    if ( !info.fieldPath.isEmpty() ) {
        lines << QObject::tr( "Field: %1" ).arg( info.fieldPath );
    }
    if ( !info.source.isEmpty() ) {
        lines << QObject::tr( "Source: %1" ).arg( info.source );
    }
    if ( info.offset >= 0 ) {
        lines << QObject::tr( "Offset: %1" ).arg( info.offset );
    }
    if ( info.width >= 0 ) {
        lines << QObject::tr( "Width: %1" ).arg( info.width );
    }
    if ( !info.rawSlice.isEmpty() ) {
        lines << QObject::tr( "Raw: %1" ).arg( info.rawSlice );
    }
    if ( !info.reason.isEmpty() ) {
        lines << QObject::tr( "Reason: %1" ).arg( info.reason );
    }
    return lines.join( "\n" );
}

void logDecodeError( const DecodeErrorInfo& info )
{
    LOG_WARNING << "[Previewer] Decode error for '" << info.previewName.toStdString() << "'"
                << " field '" << info.fieldPath.toStdString() << "': " << info.reason.toStdString()
                << " (source=" << info.source.toStdString() << ", offset=" << info.offset
                << ", width=" << info.width << ")";
}

void setItemStatus( QTreeWidgetItem* item, const QString& text, const QString& tooltip )
{
    if ( !item ) {
        return;
    }
    item->setText( ColumnValue, text );
    if ( !tooltip.isEmpty() ) {
        item->setToolTip( ColumnValue, tooltip );
    }
}

void setItemOffsetWidth( QTreeWidgetItem* item, int offset, int width )
{
    if ( !item ) {
        return;
    }
    if ( offset >= 0 ) {
        item->setText( ColumnOffset, QString::number( offset ) );
    }
    if ( width >= 0 ) {
        item->setText( ColumnWidth, QString::number( width ) );
    }
}

void setItemDecodeError( QTreeWidgetItem* item, const DecodeErrorInfo& info )
{
    const auto shortText = QObject::tr( "Decode error: %1" ).arg( info.reason );
    setItemStatus( item, shortText, decodeTooltip( info ) );
    logDecodeError( info );
}

void setItemMatchError( QTreeWidgetItem* item, const DecodeErrorInfo& info )
{
    const auto shortText = QObject::tr( "Match failed: %1" ).arg( info.reason );
    setItemStatus( item, shortText, decodeTooltip( info ) );
    logDecodeError( info );
}

bool isBinaryString( const QString& value )
{
    if ( value.isEmpty() ) {
        return false;
    }
    for ( const auto& ch : value ) {
        if ( ch != '0' && ch != '1' ) {
            return false;
        }
    }
    return true;
}

bool parseNumericString( const QString& value, quint64* out )
{
    if ( !out ) {
        return false;
    }
    const auto trimmed = value.trimmed();
    if ( trimmed.startsWith( "0b" ) ) {
        bool ok = false;
        *out = trimmed.mid( 2 ).toULongLong( &ok, 2 );
        return ok;
    }
    if ( isBinaryString( trimmed ) ) {
        bool ok = false;
        *out = trimmed.toULongLong( &ok, 2 );
        return ok;
    }
    bool ok = false;
    *out = trimmed.toULongLong( &ok, 0 );
    return ok;
}

quint64 parseInteger( const QByteArray& data, const QString& endianness, bool* ok )
{
    if ( ok ) {
        *ok = false;
    }
    if ( data.isEmpty() ) {
        return 0;
    }

    const bool little = endianness.compare( "little", Qt::CaseInsensitive ) == 0;
    const int size = static_cast<int>( std::min<qsizetype>( data.size(), 8 ) );
    quint64 value = 0;
    if ( little ) {
        for ( int i = 0; i < size; ++i ) {
            value |= ( static_cast<quint64>( static_cast<unsigned char>( data.at( i ) ) )
                       << ( 8 * i ) );
        }
    }
    else {
        for ( int i = 0; i < size; ++i ) {
            value = ( value << 8 )
                    | static_cast<quint64>( static_cast<unsigned char>( data.at( i ) ) );
        }
    }
    if ( ok ) {
        *ok = true;
    }
    return value;
}

bool parseNumericValue( const QString& rawText,
                        const QByteArray& rawBytes,
                        const PreviewFieldSpec& field,
                        quint64* numeric,
                        QString* error );

QString formatEnum( quint64 value, const QMap<QString, QString>& enumMap )
{
    for ( auto it = enumMap.begin(); it != enumMap.end(); ++it ) {
        quint64 key = 0;
        if ( parseNumericString( it.key(), &key ) && key == value ) {
            return it.value();
        }
    }
    return QString::number( value );
}

QString formatEnumWithRaw( const QString& rawText,
                           quint64 value,
                           const QMap<QString, QString>& enumMap )
{
    const auto trimmed = rawText.trimmed();
    if ( !trimmed.isEmpty() ) {
        const auto it = enumMap.constFind( trimmed );
        if ( it != enumMap.constEnd() ) {
            return it.value();
        }
    }
    return formatEnum( value, enumMap );
}

struct EnumLookupResult {
    bool ok = false;
    QString text;
    QString error;
    bool hasNumeric = false;
    quint64 numeric = 0;
};

EnumLookupResult resolveEnumValue( const QString& rawText,
                                   const QByteArray& rawBytes,
                                   const PreviewFieldSpec& field )
{
    EnumLookupResult result;

    const auto trimmed = rawText.trimmed();
    result.hasNumeric = parseNumericString( trimmed, &result.numeric );
    if ( !result.hasNumeric ) {
        result.hasNumeric = parseNumericValue( rawText, rawBytes, field, &result.numeric, nullptr );
    }

    const auto directIt = field.enumMap.constFind( trimmed );
    if ( directIt != field.enumMap.constEnd() ) {
        result.ok = true;
        result.text = directIt.value();
        return result;
    }

    if ( result.hasNumeric ) {
        for ( auto it = field.enumMap.begin(); it != field.enumMap.end(); ++it ) {
            quint64 key = 0;
            if ( parseNumericString( it.key(), &key ) && key == result.numeric ) {
                result.ok = true;
                result.text = it.value();
                return result;
            }
        }
    }

    QString displayRaw = trimmed;
    if ( displayRaw.isEmpty() ) {
        displayRaw = sliceToLogText( rawBytes );
    }
    if ( displayRaw.isEmpty() ) {
        displayRaw = QObject::tr( "<empty>" );
    }
    result.ok = true;
    result.text = result.hasNumeric
        ? QObject::tr( "unknown/reserved (%1, numeric %2)" ).arg( displayRaw ).arg( result.numeric )
        : QObject::tr( "unknown/reserved (%1)" ).arg( displayRaw );
    return result;
}

QString formatFlags( quint64 value, const QMap<QString, QString>& flagMap )
{
    QStringList names;
    for ( auto it = flagMap.begin(); it != flagMap.end(); ++it ) {
        quint64 key = 0;
        if ( parseNumericString( it.key(), &key ) && ( value & key ) ) {
            names.push_back( it.value() );
        }
    }
    if ( names.isEmpty() ) {
        return QString( "0x%1" ).arg( value, 0, 16 );
    }
    return names.join( ", " );
}

QString formatNumber( quint64 value, PreviewFormat format, const PreviewFieldSpec& field )
{
    switch ( format ) {
    case PreviewFormat::Dig:
    case PreviewFormat::Dec:
        return QString::number( value );
    case PreviewFormat::Hex:
        return QString( "0x%1" ).arg( value, 0, 16 );
    case PreviewFormat::Bin:
        return QString::number( value, 2 );
    case PreviewFormat::Enum:
        return formatEnum( value, field.enumMap );
    case PreviewFormat::Flags:
        return formatFlags( value, field.flagMap );
    default:
        return QString::number( value );
    }
}

QString formatNumberWithRaw( const QString& rawText,
                             quint64 value,
                             PreviewFormat format,
                             const PreviewFieldSpec& field )
{
    if ( format == PreviewFormat::Enum ) {
        return formatEnumWithRaw( rawText, value, field.enumMap );
    }
    return formatNumber( value, format, field );
}

struct ExprResult {
    bool ok = false;
    int value = 0;
    QString error;
    QString missingVariable;
};

ExprResult resolveExprValue( const PreviewValueExpr& expr, const QMap<QString, qint64>& values )
{
    ExprResult result;
    if ( !expr.isSet ) {
        result.ok = true;
        result.value = 0;
        return result;
    }
    auto eval = evaluatePreviewExpression( expr, values );
    if ( !eval.ok ) {
        result.error = eval.error;
        result.missingVariable = eval.missingVariable;
        return result;
    }
    if ( eval.value < std::numeric_limits<int>::min()
         || eval.value > std::numeric_limits<int>::max() ) {
        result.error = QObject::tr( "Expression value is out of range." );
        return result;
    }
    result.ok = true;
    result.value = static_cast<int>( eval.value );
    return result;
}

struct DecodeBytesResult {
    bool ok = false;
    QByteArray bytes;
    int digitCount = 0;
    QString error;
};

DecodeBytesResult decodeBytesFromText( const QString& text, PreviewBufferType type )
{
    DecodeBytesResult result;
    if ( type == PreviewBufferType::HexString ) {
        const auto decoded = decodeHexStringToBytes( text );
        result.ok = decoded.ok;
        result.bytes = decoded.bytes;
        result.digitCount = decoded.digitCount;
        result.error = decoded.error;
        return result;
    }
    if ( type == PreviewBufferType::Base64 ) {
        result.bytes = QByteArray::fromBase64( text.toUtf8() );
        result.ok = !result.bytes.isEmpty() || text.isEmpty();
        if ( !result.ok ) {
            result.error = QObject::tr( "Failed to decode base64 data." );
        }
        return result;
    }
    result.ok = true;
    result.bytes = text.toUtf8();
    return result;
}

DecodeBytesResult decodeBytesFromSlice( const QByteArray& slice, PreviewBufferType type )
{
    if ( type == PreviewBufferType::HexString || type == PreviewBufferType::Base64 ) {
        return decodeBytesFromText( QString::fromUtf8( slice ), type );
    }
    DecodeBytesResult result;
    result.ok = true;
    result.bytes = slice;
    return result;
}

QString decodeStringValue( const QString& rawText,
                           const QByteArray& rawBytes,
                           PreviewBufferType type,
                           QString* error )
{
    if ( type == PreviewBufferType::HexString || type == PreviewBufferType::Base64 ) {
        const auto decoded = decodeBytesFromText( rawText, type );
        if ( !decoded.ok ) {
            if ( error ) {
                *error = decoded.error;
            }
            return {};
        }
        return QString::fromUtf8( decoded.bytes );
    }
    return rawText.isEmpty() ? QString::fromUtf8( rawBytes ) : rawText;
}

bool parseNumericValue( const QString& rawText,
                        const QByteArray& rawBytes,
                        const PreviewFieldSpec& field,
                        quint64* numeric,
                        QString* error )
{
    if ( !numeric ) {
        return false;
    }

    if ( field.type == PreviewBufferType::HexString ) {
        const auto parsed = parseHexToU64AllowOddDigits( rawText );
        if ( !parsed.ok ) {
            if ( error ) {
                *error = parsed.error;
            }
            return false;
        }
        if ( field.endianness.compare( "little", Qt::CaseInsensitive ) == 0
             && parsed.digitCount % 2 == 0 ) {
            const auto decoded = decodeBytesFromText( rawText, field.type );
            if ( decoded.ok ) {
                bool ok = false;
                *numeric = parseInteger( decoded.bytes, field.endianness, &ok );
                if ( ok ) {
                    return true;
                }
            }
        }
        *numeric = parsed.value;
        return true;
    }

    if ( field.type == PreviewBufferType::Base64 ) {
        const auto decoded = decodeBytesFromText( rawText, field.type );
        if ( !decoded.ok ) {
            if ( error ) {
                *error = decoded.error;
            }
            return false;
        }
        bool ok = false;
        *numeric = parseInteger( decoded.bytes, field.endianness, &ok );
        if ( !ok && error ) {
            *error = QObject::tr( "Failed to parse base64 bytes." );
        }
        return ok;
    }

    if ( field.type == PreviewBufferType::String || field.type == PreviewBufferType::Bin ) {
        if ( !parseNumericString( rawText, numeric ) ) {
            if ( error ) {
                *error = QObject::tr( "Failed to parse numeric value." );
            }
            return false;
        }
        return true;
    }

    bool ok = false;
    *numeric = parseInteger( rawBytes, field.endianness, &ok );
    if ( !ok && error ) {
        *error = QObject::tr( "Failed to parse bytes." );
    }
    return ok;
}

struct ParseContext {
    QByteArray buffer;
    int cursor = 0;
    QMap<QString, qint64>* values = nullptr;
    QMap<QString, QString>* rawValues = nullptr;
    const QRegularExpressionMatch* match = nullptr;
    const QMap<QString, PreviewFieldSpec>* blocks = nullptr;
    QStringList* blockStack = nullptr;
    QString previewName;
    QString bufferSource;
    int baseOffset = 0;
    int matchStart = 0;
    bool allowUnqualified = false;
    PreviewTreeBudget* budget = nullptr;
};

void parseFieldIntoItem( QTreeWidgetItem* item,
                         const PreviewFieldSpec& field,
                         ParseContext& context,
                         const QString& prefix,
                         const QString& displayName );
void addFieldItems( QTreeWidgetItem* parent,
                    const PreviewFieldSpec& field,
                    ParseContext& context,
                    const QString& prefix );

void insertRawValue( ParseContext& context,
                     const QString& fullName,
                     const QString& shortName,
                     const QString& rawValue )
{
    if ( !context.rawValues ) {
        return;
    }
    context.rawValues->insert( fullName, rawValue );
    if ( context.allowUnqualified && shortName != fullName ) {
        context.rawValues->insert( shortName, rawValue );
    }
}

void insertValue( ParseContext& context,
                  const QString& fullName,
                  const QString& shortName,
                  qint64 value,
                  const QString& rawValue )
{
    if ( context.values ) {
        context.values->insert( fullName, value );
        if ( context.allowUnqualified && shortName != fullName ) {
            context.values->insert( shortName, value );
        }
    }
    insertRawValue( context, fullName, shortName, rawValue );
}

void addBlockErrorItem( QTreeWidgetItem* item, const QString& message )
{
    if ( !item ) {
        return;
    }
    auto* errorItem = new QTreeWidgetItem( item );
    errorItem->setText( ColumnField, QObject::tr( "Error" ) );
    errorItem->setText( ColumnValue, message );
}

void setItemRaw( QTreeWidgetItem* item, const QString& rawText, const QByteArray& rawBytes )
{
    if ( !item ) {
        return;
    }
    if ( !rawText.isEmpty() ) {
        item->setText( ColumnRaw, rawText );
        return;
    }
    if ( !rawBytes.isEmpty() ) {
        item->setText( ColumnRaw, sliceToLogText( rawBytes ) );
    }
}

struct BlockResolution {
    const PreviewFieldSpec* block = nullptr;
    QString resolvedName;
    QString error;
};

// Maps a block-name prefix to the generic block used when a more specific
// block is not present. Add an entry here to enable fallback for a new
// protocol/role without touching the lookup logic below.
//
// This is intentionally a code-level table rather than runtime config: the
// fallback names are tied to blocks that ship with the preview definitions,
// so the two are versioned together. If/when block specs gain their own
// optional "fallback" field in the preview config (see PreviewFieldSpec),
// this table can be replaced by reading that field.
struct BlockFallbackRule {
    const char* prefix;
    const char* fallback;
};

constexpr BlockFallbackRule kBlockFallbackRules[] = {
    { "at_command_", "at_command_generic_args_block" },
    { "at_response_", "at_response_generic_args_block" },
};

QString fallbackBlockName( const QString& resolvedName )
{
    if ( !resolvedName.endsWith( "_block" ) ) {
        return {};
    }
    for ( const auto& rule : kBlockFallbackRules ) {
        if ( resolvedName.startsWith( QLatin1String( rule.prefix ) ) ) {
            return QLatin1String( rule.fallback );
        }
    }
    return {};
}

BlockResolution resolveBlockReference( const PreviewFieldSpec& field, ParseContext& context )
{
    BlockResolution result;
    if ( !context.blocks ) {
        result.error = QObject::tr( "No blocks are available." );
        return result;
    }

    const QString templateName = field.blockTemplate.trimmed();
    if ( templateName.isEmpty() ) {
        result.error = QObject::tr( "Missing block reference." );
        return result;
    }

    const QMap<QString, QString> emptyValues;
    const auto& values = context.rawValues ? *context.rawValues : emptyValues;
    QStringList missing;
    result.resolvedName = resolveTemplateString( templateName, values, &missing );

    const auto it = context.blocks->find( result.resolvedName );
    if ( it == context.blocks->end() ) {
        if ( missing.isEmpty() ) {
            const auto fallbackName = fallbackBlockName( result.resolvedName );
            if ( !fallbackName.isEmpty() ) {
                const auto fallbackIt = context.blocks->find( fallbackName );
                if ( fallbackIt != context.blocks->end() ) {
                    result.resolvedName = fallbackName;
                    result.block = &fallbackIt.value();
                    return result;
                }
            }
        }
        if ( missing.isEmpty() ) {
            result.error = QObject::tr( "Missing block: %1" ).arg( result.resolvedName );
        }
        else {
            result.error = QObject::tr( "Missing block: %1 (missing values: %2)" )
                               .arg( result.resolvedName, missing.join( ", " ) );
        }
        return result;
    }

    if ( !missing.isEmpty() ) {
        result.error = QObject::tr( "Missing template values: %1" ).arg( missing.join( ", " ) );
        return result;
    }

    if ( context.blockStack && context.blockStack->contains( result.resolvedName ) ) {
        const auto chain = context.blockStack->join( " -> " );
        result.error = QObject::tr( "Block cycle detected: %1 -> %2" )
                           .arg( chain, result.resolvedName );
        return result;
    }

    result.block = &it.value();
    return result;
}

void applyBlockReference( QTreeWidgetItem* item,
                          const PreviewFieldSpec& field,
                          ParseContext& context,
                          const QString& prefix,
                          const QString& displayName )
{
    auto resolution = resolveBlockReference( field, context );
    if ( !resolution.error.isEmpty() ) {
        addBlockErrorItem( item, resolution.error );
        return;
    }

    if ( context.blockStack ) {
        context.blockStack->push_back( resolution.resolvedName );
    }
    parseFieldIntoItem( item, *resolution.block, context, prefix, displayName );
    if ( context.blockStack ) {
        context.blockStack->removeLast();
    }
}

bool applyMatchStage( QTreeWidgetItem* item,
                      const PreviewFieldSpec& field,
                      ParseContext& context,
                      const QString& fullName,
                      const QString& rawText,
                      const QByteArray& rawBytes,
                      const QString& source,
                      int inputOffset,
                      int inputWidth )
{
    setItemRaw( item, rawText, rawBytes );
    QString decodeError;
    const auto decodedText = decodeStringValue( rawText, rawBytes, field.type, &decodeError );
    if ( !decodeError.isEmpty() ) {
        DecodeErrorInfo errorInfo{ context.previewName,
                                   fullName,
                                   source,
                                   inputOffset,
                                   inputWidth,
                                   truncateText( rawText, 64 ),
                                   QObject::tr( "Match decode failed: %1" ).arg( decodeError ) };
        setItemMatchError( item, errorInfo );
        return true;
    }

    if ( field.regex.trimmed().isEmpty() || !field.compiledRegex.isValid() ) {
        DecodeErrorInfo errorInfo{ context.previewName,
                                   fullName,
                                   source,
                                   inputOffset,
                                   inputWidth,
                                   truncateText( decodedText, 64 ),
                                   QObject::tr( "Match regex is not valid." ) };
        setItemMatchError( item, errorInfo );
        return true;
    }

    const auto match = field.compiledRegex.match( decodedText );
    if ( !match.hasMatch() ) {
        DecodeErrorInfo errorInfo{
            context.previewName,
            fullName,
            source,
            inputOffset,
            inputWidth,
            truncateText( decodedText, 64 ),
            QObject::tr( "Regex did not match: %1" ).arg( field.regex ) };
        setItemMatchError( item, errorInfo );
        return true;
    }

    const int matchOffset = clampToInt( match.capturedStart( 0 ) );
    const int matchWidth = clampToInt( match.capturedLength( 0 ) );
    const int adjustedOffset
        = ( inputOffset >= 0 && matchOffset >= 0 ) ? ( inputOffset + matchOffset ) : -1;
    setItemOffsetWidth( item, adjustedOffset, matchWidth );
    if ( matchWidth >= 0 ) {
        item->setText( ColumnValue, QObject::tr( "%1 chars" ).arg( matchWidth ) );
    }
    else {
        item->setText( ColumnValue, QObject::tr( "match" ) );
    }

    const QString bufferText
        = field.bufferCapture.isSet ? captureValue( field.bufferCapture, match )
                                    : match.captured( 0 );
    const auto bufferSource = field.bufferCapture.isSet
                                  ? describeCaptureRef( field.bufferCapture, "bufferCapture" )
                                  : QObject::tr( "match" );

    int bufferBaseOffset = 0;
    if ( field.bufferCapture.isSet ) {
        const auto captureStartOffset = captureStart( field.bufferCapture, match );
        if ( captureStartOffset >= 0 && matchOffset >= 0 ) {
            bufferBaseOffset = captureStartOffset - matchOffset;
            if ( bufferBaseOffset < 0 ) {
                bufferBaseOffset = 0;
            }
        }
    }

    ParseContext childContext{
        bufferText.toUtf8(),
        0,
        context.values,
        context.rawValues,
        &match,
        context.blocks,
        context.blockStack,
        context.previewName,
        bufferSource,
        bufferBaseOffset,
        matchOffset >= 0 ? matchOffset : 0,
        true };
    childContext.budget = context.budget;

    for ( const auto& child : field.fields ) {
        addFieldItems( item, child, childContext, fullName );
    }
    return true;
}

void addBitfieldItems( QTreeWidgetItem* parent,
                       const PreviewFieldSpec& field,
                       quint64 value,
                       int totalBits,
                       ParseContext& context,
                       const QString& prefix )
{
    int remaining = totalBits;
    for ( const auto& bitField : field.bitfieldMap ) {
        const auto widthExpr = resolveExprValue( bitField.width, *context.values );
        int width = widthExpr.ok ? widthExpr.value : 1;
        if ( width <= 0 ) {
            width = 1;
        }
        remaining -= width;
        const quint64 mask = width >= 64 ? ~0ULL : ( ( 1ULL << width ) - 1ULL );
        const quint64 bitValue = ( remaining >= 0 ) ? ( ( value >> remaining ) & mask ) : 0;

        if ( context.budget && !context.budget->take() )
            return;
        auto* item = new QTreeWidgetItem( parent );
        const auto fullName = prefix.isEmpty() ? bitField.name : prefix + "." + bitField.name;
        item->setText( ColumnField, bitField.name );
        item->setText( ColumnValue, formatNumber( bitValue, bitField.format, bitField ) );
        insertValue( context,
                     fullName,
                     bitField.name,
                     static_cast<qint64>( bitValue ),
                     QString::number( bitValue ) );
    }
}

int resolveBitfieldWidth( const PreviewFieldSpec& field, ParseContext& context )
{
    const auto totalExpr = resolveExprValue( field.width, *context.values );
    int totalBits = totalExpr.ok ? totalExpr.value : 0;
    if ( totalBits > 0 ) {
        return totalBits;
    }

    totalBits = 0;
    for ( const auto& sub : field.bitfieldMap ) {
        const auto widthExpr = resolveExprValue( sub.width, *context.values );
        totalBits += widthExpr.ok ? widthExpr.value : 1;
    }
    return totalBits;
}

void parseFieldIntoItem( QTreeWidgetItem* item,
                         const PreviewFieldSpec& field,
                         ParseContext& context,
                         const QString& prefix,
                         const QString& displayName )
{
    if ( !item ) {
        return;
    }
    item->setText( ColumnField, displayName );

    const auto fullName = prefix.isEmpty() ? displayName : prefix + "." + displayName;

    if ( field.format == PreviewFormat::Block || field.source == PreviewFieldSource::Block ) {
        applyBlockReference( item, field, context, prefix, displayName );
        return;
    }

    if ( field.source == PreviewFieldSource::Capture ) {
        const auto captured = context.match ? captureValue( field.capture, *context.match )
                                            : QString();
        const auto source = describeCaptureRef( field.capture, "capture" );
        int captureOffset = -1;
        int absoluteStart = -1;
        if ( context.match ) {
            absoluteStart = captureStart( field.capture, *context.match );
            if ( absoluteStart >= 0 ) {
                captureOffset = absoluteStart - context.matchStart;
            }
        }
        const int captureWidth
            = context.match ? captureLength( field.capture, *context.match ) : -1;
        setItemOffsetWidth( item, captureOffset, captureWidth );
        if ( !field.capture.isSet ) {
            DecodeErrorInfo errorInfo{ context.previewName,
                                       fullName,
                                       source,
                                       -1,
                                       -1,
                                       truncateText( captured, 64 ),
                                       QObject::tr( "Capture is not set." ) };
            setItemDecodeError( item, errorInfo );
            return;
        }
        if ( context.match && absoluteStart < 0 ) {
            delete item;
            return;
        }
        const auto rawText = captured;
        const auto rawBytes = captured.toUtf8();
        setItemRaw( item, rawText, rawBytes );

        if ( field.format == PreviewFormat::Match ) {
            applyMatchStage( item, field, context, fullName, rawText, rawBytes, source,
                             captureOffset, captureWidth );
            return;
        }

        if ( field.format == PreviewFormat::Fields ) {
            const auto decoded = decodeBytesFromText( rawText, field.type );
            if ( !decoded.ok ) {
                DecodeErrorInfo errorInfo{ context.previewName,
                                           fullName,
                                           source,
                                           captureOffset,
                                           captureWidth,
                                           truncateText( rawText, 64 ),
                                           decoded.error };
                setItemDecodeError( item, errorInfo );
                return;
            }
            const int decodedSize = clampToInt( decoded.bytes.size() );
            item->setText( ColumnValue,
                           QObject::tr( "%1 bytes" ).arg( decodedSize ) );
            setItemOffsetWidth( item, captureOffset, decodedSize );
            ParseContext childContext{
                decoded.bytes,
                0,
                context.values,
                context.rawValues,
                context.match,
                context.blocks,
                context.blockStack,
                context.previewName,
                source,
                captureOffset >= 0 ? captureOffset : context.baseOffset,
                context.matchStart,
                context.allowUnqualified };
            childContext.budget = context.budget;
            for ( const auto& child : field.fields ) {
                addFieldItems( item, child, childContext, fullName );
            }
            return;
        }

        if ( field.format == PreviewFormat::String ) {
            QString error;
            const auto value = decodeStringValue( rawText, rawBytes, field.type, &error );
            if ( !error.isEmpty() ) {
                DecodeErrorInfo errorInfo{ context.previewName,
                                           fullName,
                                           source,
                                           captureOffset,
                                           captureWidth,
                                           truncateText( rawText, 64 ),
                                           error };
                setItemDecodeError( item, errorInfo );
                return;
            }
            item->setText( ColumnValue, value );
            insertRawValue( context, fullName, displayName, rawText );
            if ( !field.fields.isEmpty() ) {
                const auto decoded = decodeBytesFromSlice( rawBytes, field.type );
                if ( !decoded.ok ) {
                    DecodeErrorInfo errorInfo{ context.previewName,
                                               fullName,
                                               source,
                                               captureOffset,
                                               captureWidth,
                                               truncateText( rawText, 64 ),
                                               decoded.error };
                    setItemDecodeError( item, errorInfo );
                    return;
                }
                ParseContext childContext{
                    decoded.bytes,
                    0,
                    context.values,
                    context.rawValues,
                    context.match,
                    context.blocks,
                    context.blockStack,
                    context.previewName,
                    source,
                    captureOffset >= 0 ? captureOffset : context.baseOffset,
                    context.matchStart,
                    context.allowUnqualified };
                childContext.budget = context.budget;
                for ( const auto& child : field.fields ) {
                    addFieldItems( item, child, childContext, fullName );
                }
            }
        }
        else {
            if ( field.format == PreviewFormat::Enum ) {
                const auto enumResult = resolveEnumValue( rawText, rawBytes, field );
                if ( !enumResult.ok ) {
                    DecodeErrorInfo errorInfo{ context.previewName,
                                               fullName,
                                               source,
                                               captureOffset,
                                               captureWidth,
                                               truncateText( rawText, 64 ),
                                               enumResult.error };
                    setItemDecodeError( item, errorInfo );
                    return;
                }
                item->setText( ColumnValue, enumResult.text );
                if ( enumResult.hasNumeric ) {
                    insertValue( context,
                                 fullName,
                                 displayName,
                                 static_cast<qint64>( enumResult.numeric ),
                                 rawText );
                }
                else {
                    insertRawValue( context, fullName, displayName, rawText );
                }
                return;
            }

            quint64 numeric = 0;
            QString error;
            if ( !parseNumericValue( rawText, rawBytes, field, &numeric, &error ) ) {
                DecodeErrorInfo errorInfo{ context.previewName,
                                           fullName,
                                           source,
                                           captureOffset,
                                           captureWidth,
                                           truncateText( rawText, 64 ),
                                           error };
                setItemDecodeError( item, errorInfo );
                return;
            }
            item->setText(
                ColumnValue, formatNumberWithRaw( rawText, numeric, field.format, field ) );
            insertValue( context,
                         fullName,
                         displayName,
                         static_cast<qint64>( numeric ),
                         rawText );
            if ( field.format == PreviewFormat::Bitfield ) {
                addBitfieldItems( item,
                                  field,
                                  numeric,
                                  resolveBitfieldWidth( field, context ),
                                  context,
                                  fullName );
            }
        }
        return;
    }

    auto reportExpressionIssue = [ &context, &fullName, &item ]( const ExprResult& expr,
                                                                const QString& source,
                                                                int offset,
                                                                int width ) {
        DecodeErrorInfo errorInfo{ context.previewName,
                                   fullName,
                                   source,
                                   offset,
                                   width,
                                   QString(),
                                   expr.error.isEmpty() ? QObject::tr( "Expression error." )
                                                        : expr.error };
        if ( !expr.missingVariable.isEmpty() ) {
            const auto shortText
                = QObject::tr( "Skipped: missing %1" ).arg( expr.missingVariable );
            setItemStatus( item, shortText, decodeTooltip( errorInfo ) );
            logDecodeError( errorInfo );
            return false;
        }
        setItemDecodeError( item, errorInfo );
        return false;
    };

    const auto source = context.bufferSource.isEmpty() ? QObject::tr( "buffer" )
                                                       : context.bufferSource;

    int offset = 0;
    if ( field.offset.isSet ) {
        const auto offsetExpr = resolveExprValue( field.offset, *context.values );
        if ( !offsetExpr.ok ) {
            reportExpressionIssue( offsetExpr, source, context.baseOffset + context.cursor, -1 );
            return;
        }
        offset = offsetExpr.value;
        if ( offset < 0 ) {
            DecodeErrorInfo errorInfo{ context.previewName,
                                       fullName,
                                       source,
                                       offset,
                                       -1,
                                       QString(),
                                       QObject::tr( "Offset is negative." ) };
            setItemDecodeError( item, errorInfo );
            return;
        }
        context.cursor += offset;
    }

    const int remaining = clampToInt( context.buffer.size() ) - context.cursor;
    if ( remaining < 0 ) {
        DecodeErrorInfo errorInfo{ context.previewName,
                                   fullName,
                                   source,
                                   context.baseOffset + context.cursor,
                                   -1,
                                   QString(),
                                   QObject::tr( "Offset exceeds buffer size." ) };
        setItemDecodeError( item, errorInfo );
        return;
    }

    int width = remaining;
    if ( field.width.isSet ) {
        const auto widthExpr = resolveExprValue( field.width, *context.values );
        if ( !widthExpr.ok ) {
            reportExpressionIssue( widthExpr, source, context.baseOffset + context.cursor, -1 );
            return;
        }
        width = widthExpr.value;
        if ( width < 0 ) {
            DecodeErrorInfo errorInfo{ context.previewName,
                                       fullName,
                                       source,
                                       context.cursor,
                                       width,
                                       QString(),
                                       QObject::tr( "Width is negative." ) };
            setItemDecodeError( item, errorInfo );
            return;
        }
        if ( width > remaining ) {
            DecodeErrorInfo errorInfo{ context.previewName,
                                       fullName,
                                       source,
                                       context.baseOffset + context.cursor,
                                       width,
                                       QString(),
                                       QObject::tr( "Width exceeds remaining buffer (%1 bytes)." )
                                           .arg( remaining ) };
            setItemDecodeError( item, errorInfo );
            return;
        }
    }

    const int sliceOffset = context.cursor;
    const auto slice = context.buffer.mid( sliceOffset, width );
    context.cursor += width;
    const auto rawText = QString::fromUtf8( slice );
    const int inputOffset = context.baseOffset + sliceOffset;
    setItemRaw( item, rawText, slice );

    if ( field.format == PreviewFormat::Match ) {
        applyMatchStage( item, field, context, fullName, rawText, slice, source, inputOffset,
                         width );
        return;
    }

    setItemOffsetWidth( item, inputOffset, width );

    if ( field.format == PreviewFormat::Fields ) {
        const auto decoded = decodeBytesFromSlice( slice, field.type );
        if ( !decoded.ok ) {
            DecodeErrorInfo errorInfo{ context.previewName,
                                       fullName,
                                       source,
                                       context.baseOffset + sliceOffset,
                                       width,
                                       sliceToLogText( slice ),
                                       decoded.error };
            setItemDecodeError( item, errorInfo );
            return;
        }
        item->setText( ColumnValue,
                       QObject::tr( "%1 bytes" ).arg( decoded.bytes.size() ) );
        ParseContext childContext{
            decoded.bytes,
            0,
            context.values,
            context.rawValues,
            context.match,
            context.blocks,
            context.blockStack,
            context.previewName,
            source,
            context.baseOffset + sliceOffset,
            context.matchStart,
            context.allowUnqualified };
        childContext.budget = context.budget;
        for ( const auto& child : field.fields ) {
            addFieldItems( item, child, childContext, fullName );
        }
        return;
    }

    if ( field.format == PreviewFormat::String ) {
        QString error;
        const auto value = decodeStringValue( rawText, slice, field.type, &error );
        if ( !error.isEmpty() ) {
            DecodeErrorInfo errorInfo{ context.previewName,
                                       fullName,
                                       source,
                                       context.baseOffset + sliceOffset,
                                       width,
                                       sliceToLogText( slice ),
                                       error };
            setItemDecodeError( item, errorInfo );
            return;
        }
        item->setText( ColumnValue, value );
        insertRawValue( context, fullName, displayName, rawText );
        if ( !field.fields.isEmpty() ) {
            const auto decoded = decodeBytesFromSlice( slice, field.type );
            if ( !decoded.ok ) {
                DecodeErrorInfo errorInfo{ context.previewName,
                                           fullName,
                                           source,
                                           context.baseOffset + sliceOffset,
                                           width,
                                           truncateText( rawText, 64 ),
                                           decoded.error };
                setItemDecodeError( item, errorInfo );
                return;
            }
            ParseContext childContext{
                decoded.bytes,
                0,
                context.values,
                context.rawValues,
                context.match,
                context.blocks,
                context.blockStack,
                context.previewName,
                source,
                context.baseOffset + sliceOffset,
                context.matchStart,
                context.allowUnqualified };
            childContext.budget = context.budget;
            for ( const auto& child : field.fields ) {
                addFieldItems( item, child, childContext, fullName );
            }
        }
    }
    else {
        if ( field.format == PreviewFormat::Enum ) {
            const auto enumResult = resolveEnumValue( rawText, slice, field );
            if ( !enumResult.ok ) {
                DecodeErrorInfo errorInfo{ context.previewName,
                                           fullName,
                                           source,
                                           context.baseOffset + sliceOffset,
                                           width,
                                           sliceToLogText( slice ),
                                           enumResult.error };
                setItemDecodeError( item, errorInfo );
                return;
            }
            item->setText( ColumnValue, enumResult.text );
            if ( enumResult.hasNumeric ) {
                insertValue( context,
                             fullName,
                             displayName,
                             static_cast<qint64>( enumResult.numeric ),
                             rawText );
            }
            else {
                insertRawValue( context, fullName, displayName, rawText );
            }
            return;
        }

        quint64 numeric = 0;
        QString error;
        if ( !parseNumericValue( rawText, slice, field, &numeric, &error ) ) {
            DecodeErrorInfo errorInfo{ context.previewName,
                                       fullName,
                                       source,
                                       context.baseOffset + sliceOffset,
                                       width,
                                       sliceToLogText( slice ),
                                       error };
            setItemDecodeError( item, errorInfo );
            return;
        }
        item->setText(
            ColumnValue, formatNumberWithRaw( rawText, numeric, field.format, field ) );
        insertValue( context,
                     fullName,
                     displayName,
                     static_cast<qint64>( numeric ),
                     rawText );
        if ( field.format == PreviewFormat::Bitfield ) {
            addBitfieldItems( item,
                              field,
                              numeric,
                              resolveBitfieldWidth( field, context ),
                              context,
                              fullName );
        }
    }
}

void addFieldItems( QTreeWidgetItem* parent,
                    const PreviewFieldSpec& field,
                    ParseContext& context,
                    const QString& prefix )
{
    if ( context.budget && !context.budget->take() )
        return;
    auto* item = new QTreeWidgetItem( parent );
    parseFieldIntoItem( item, field, context, prefix, field.name );
}
} // namespace

PreviewPreparedBuffer preparePreviewInput( const QString& text, const QString& encoding )
{
    PreviewPreparedBuffer result;
    PreviewBufferType type = PreviewBufferType::String;
    if ( encoding == "hex" )
        type = PreviewBufferType::HexString;
    else if ( encoding == "base64" ) {
        type = PreviewBufferType::Base64;
        const auto ascii = text.toLatin1();
        if ( QString::fromLatin1( ascii ) != text
             || QByteArray::fromBase64( ascii ).toBase64() != ascii ) {
            result.reason = "invalid_base64";
            result.error = QObject::tr( "Failed to decode base64 data." );
            return result;
        }
    }
    else if ( encoding != "utf8" ) {
        result.reason = "invalid_encoding";
        return result;
    }
    const auto decoded = decodeBytesFromText( text, type );
    result.ok = decoded.ok;
    result.bytes = decoded.bytes;
    result.error = decoded.error;
    if ( !result.ok )
        result.reason = encoding == "hex" ? "invalid_hex" : "decode_error";
    return result;
}

PreviewPreparedBuffer preparePreviewBuffer( const QString& rawLine,
                                            const PreviewDefinition& definition,
                                            const QString& encodingOverride )
{
    PreviewPreparedBuffer result;
    result.match = definition.compiled.match( rawLine );
    if ( !result.match.hasMatch() ) {
        result.error = PreviewMessageTab::tr( "No match for selected preview." );
        result.reason = "no_match";
        return result;
    }
    const auto bufferText = definition.bufferCapture.isSet
                                ? captureValue( definition.bufferCapture, result.match )
                                : rawLine;
    result.source = definition.bufferCapture.isSet
                        ? describeCaptureRef( definition.bufferCapture, "bufferCapture" )
                        : PreviewMessageTab::tr( "raw line" );
    result.matchStart = qMax( 0, clampToInt( result.match.capturedStart( 0 ) ) );
    if ( definition.bufferCapture.isSet ) {
        const auto start = captureStart( definition.bufferCapture, result.match );
        if ( start >= 0 )
            result.bufferBaseOffset = qMax( 0, start - result.matchStart );
    }
    if ( encodingOverride.isEmpty() ) {
        const auto decoded = decodeBytesFromText( bufferText, definition.type );
        result.ok = decoded.ok;
        result.bytes = decoded.bytes;
        result.error = decoded.error;
        result.reason = "decode_error";
    }
    else {
        const auto explicitInput = preparePreviewInput( bufferText, encodingOverride );
        result.ok = explicitInput.ok;
        result.bytes = explicitInput.bytes;
        result.error = explicitInput.error;
        result.reason = explicitInput.reason;
    }
    if ( !result.ok ) {
        LOG_DEBUG << "Preview carrier rejected reason=" << result.reason
                  << " source_characters=" << bufferText.size();
        result.error = PreviewMessageTab::tr( "Decode error: %1" ).arg( result.error );
        return result;
    }
    if ( definition.offset.isSet ) {
        const auto offset = resolveExprValue( definition.offset, {} );
        if ( !offset.ok ) {
            result.error = PreviewMessageTab::tr( "Decode error: %1" )
                               .arg( offset.error.isEmpty()
                                         ? PreviewMessageTab::tr( "Invalid preview offset." )
                                         : offset.error );
        }
        else if ( offset.value < 0 )
            result.error = PreviewMessageTab::tr( "Decode error: preview offset is negative." );
        else if ( offset.value > result.bytes.size() )
            result.error
                = PreviewMessageTab::tr( "Decode error: preview offset exceeds buffer size." );
        else
            result.initialOffset = offset.value;
        if ( !result.error.isEmpty() ) {
            result.ok = false;
            result.reason = "decode_error";
            LOG_DEBUG << "Preview carrier rejected reason=initial_offset buffer_bytes="
                      << result.bytes.size();
            return result;
        }
    }
    result.reason.clear();
    LOG_DEBUG << "Preview carrier prepared buffer_bytes=" << result.bytes.size()
              << " initial_offset=" << result.initialOffset;
    return result;
}

QTreeWidgetItem* createLegacyPreviewTree( const QString& rawLine, const PreviewDefinition& preview,
                                          const QMap<QString, PreviewFieldSpec>& blocks,
                                          QString* error, PreviewTreeBudget* budget )
{
    const auto prepared = preparePreviewBuffer( rawLine, preview );
    if ( !prepared.ok ) {
        *error = prepared.error;
        return nullptr;
    }
    const auto* definition = &preview;
    QMap<QString, qint64> values;
    QMap<QString, QString> rawValues;
    QStringList blockStack;
    ParseContext context{ prepared.bytes,
                          prepared.initialOffset,
                          &values,
                          &rawValues,
                          &prepared.match,
                          &blocks,
                          &blockStack,
                          preview.name,
                          prepared.source,
                          prepared.bufferBaseOffset,
                          prepared.matchStart,
                          false };
    context.budget = budget;

    auto root = std::make_unique<QTreeWidgetItem>();
    for ( const auto& field : definition->fields ) {
        addFieldItems( root.get(), field, context, QString() );
    }

    return root.release();
}

PreviewMessageTab::PreviewMessageTab( const QString& rawLine,
                                      const QString& initialPreviewNameOrAuto, int tabNumber,
                                      QWidget* parent )
    : PreviewMessageTab( preparePreviewText( rawLine ), initialPreviewNameOrAuto, tabNumber,
                         parent )
{
}

PreviewMessageTab::PreviewMessageTab( const PreparedPreview& prepared,
                                      const QString& initialPreviewNameOrAuto, int tabNumber,
                                      QWidget* parent )
    : QWidget( parent )
    , rawLine_( prepared.text )
    , initialPreviewName_( initialPreviewNameOrAuto )
    , tabNumber_( tabNumber )
    , prepared_( prepared )
{
    buildUi();
    rawLineEdit_->setPlainText( rawLine_ );
    updateRawLineHeight();
    refreshPreviewList();
    connect( &PreviewManager::instance(), &PreviewManager::previewsChanged, this,
             &PreviewMessageTab::refreshPreviewList );
}

PreviewMessageTab::~PreviewMessageTab()
{
    ++revision_;
    if ( process_ )
        PreviewService::cancel( process_ );
}

void PreviewMessageTab::buildUi()
{
    previewTypeCombo_ = new QComboBox( this );
    previewTypeCombo_->setObjectName( "previewDefinition" );
    previewTypeCombo_->setAccessibleName( tr( "Preview definition" ) );
    previewTypeCombo_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );

    auto* typeLabel = new QLabel( tr( "Preview type:" ), this );

    auto* headerLayout = new QHBoxLayout();
    headerLayout->addWidget( typeLabel );
    headerLayout->addWidget( previewTypeCombo_, 1 );

    decoderMode_ = new QComboBox( this );
    decoderMode_->setObjectName( "previewDecoderMode" );
    decoderMode_->setAccessibleName( tr( "Decoder mode" ) );
    decoderMode_->addItem( tr( "Automatic (TShark first)" ), "auto" );
    decoderMode_->addItem( tr( "TShark / Lua" ), "tshark" );
    decoderMode_->addItem( tr( "JSON" ), "json" );
    headerLayout->addWidget( decoderMode_ );
    auto* overrides = new QHBoxLayout;
    dissector_ = new QLineEdit( this );
    dissector_->setObjectName( "previewDissector" );
    dissector_->setAccessibleName( tr( "Named dissector override" ) );
    dissector_->setPlaceholderText( tr( "Dissector from route / settings" ) );
    dissector_->setToolTip(
        tr( "Named dissector, for example data, ip or your approved Lua entrypoint" ) );
    inputEncoding_ = new QComboBox( this );
    inputEncoding_->setObjectName( "previewInputEncoding" );
    inputEncoding_->setAccessibleName( tr( "Input encoding override" ) );
    inputEncoding_->addItem( tr( "From carrier / settings" ), "" );
    for ( const auto* encoding : { "utf8", "hex", "base64" } )
        inputEncoding_->addItem( encoding, encoding );
    timeout_ = new QSpinBox( this );
    timeout_->setObjectName( "previewTimeoutMs" );
    timeout_->setAccessibleName( tr( "Preview timeout milliseconds" ) );
    timeout_->setRange( 100, 10000 );
    timeout_->setValue( 3000 );
    timeout_->setSuffix( tr( " ms" ) );
    run_ = new QPushButton( tr( "Run / retry" ), this );
    run_->setObjectName( "previewRun" );
    run_->setAccessibleName( tr( "Run preview" ) );
    cancel_ = new QPushButton( tr( "Cancel" ), this );
    cancel_->setObjectName( "previewCancel" );
    cancel_->setAccessibleName( tr( "Cancel preview" ) );
    overrides->addWidget( dissector_, 1 );
    overrides->addWidget( inputEncoding_ );
    overrides->addWidget( timeout_ );
    overrides->addWidget( run_ );
    overrides->addWidget( cancel_ );
    status_ = new QLabel( this );
    status_->setObjectName( "previewStatus" );
    status_->setAccessibleName( tr( "Preview status" ) );
    status_->setWordWrap( true );
    status_->setTextFormat( Qt::PlainText );

    previewTree_ = new QTreeWidget( this );
    previewTree_->setObjectName( "previewFields" );
    previewTree_->setAccessibleName( tr( "Decoded fields" ) );
    previewTree_->setColumnCount( 5 );
    previewTree_->setHeaderLabels( QStringList() << tr( "Field" ) << tr( "Value" ) << tr( "Raw" )
                                                 << tr( "Offset" ) << tr( "Width" ) );
    previewTree_->setRootIsDecorated( true );
    previewTree_->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    previewTree_->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    previewTree_->header()->setSectionResizeMode( QHeaderView::Interactive );
    previewTree_->header()->setStretchLastSection( false );

    rawGroup_ = new QGroupBox( tr( "Raw line" ), this );
    rawGroup_->setCheckable( true );
    rawGroup_->setChecked( false );

    rawLineEdit_ = new QPlainTextEdit( rawGroup_ );
    rawLineEdit_->setReadOnly( true );
    rawLineEdit_->setWordWrapMode( QTextOption::NoWrap );
    rawLineEdit_->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    rawLineEdit_->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    if ( auto* bar = rawLineEdit_->horizontalScrollBar() ) {
        connect( bar, &QScrollBar::rangeChanged, this,
                 [ this ]( int, int ) { updateRawLineHeight(); } );
    }

    auto* rawLayout = new QVBoxLayout();
    rawLayout->addWidget( rawLineEdit_ );
    rawGroup_->setLayout( rawLayout );
    rawGroup_->setFlat( true );
    rawLineEdit_->setVisible( false );

    connect( rawGroup_, &QGroupBox::toggled, rawLineEdit_, &QWidget::setVisible );
    connect( rawGroup_, &QGroupBox::toggled, this, [ this ]( bool ) { updateRawLineHeight(); } );
    connect( previewTypeCombo_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &PreviewMessageTab::handlePreviewSelectionChanged );
    connect( decoderMode_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             [ this ] { invalidateEvaluation(); } );
    connect( dissector_, &QLineEdit::textChanged, this, [ this ] { invalidateEvaluation(); } );
    connect( inputEncoding_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             [ this ] { invalidateEvaluation(); } );
    connect( timeout_, QOverload<int>::of( &QSpinBox::valueChanged ), this,
             [ this ] { invalidateEvaluation(); } );
    connect( run_, &QPushButton::clicked, this,
             [ this ] { renderPreview( previewTypeCombo_->currentData().toString() ); } );
    connect( cancel_, &QPushButton::clicked, this, &PreviewMessageTab::cancelEvaluation );
    dissector_->setEnabled( false );
    inputEncoding_->setEnabled( false );
    timeout_->setEnabled( true );

    auto* layout = new QVBoxLayout();
    layout->addLayout( headerLayout );
    layout->addLayout( overrides );
    layout->addWidget( status_ );
    layout->addWidget( previewTree_, 1 );
    layout->addWidget( rawGroup_ );
    setLayout( layout );
}

void PreviewMessageTab::updateRawLineHeight()
{
    if ( !rawLineEdit_ ) {
        return;
    }
    const int lineCount = qMax( 1, rawLineEdit_->document()->blockCount() );
    const QFontMetrics metrics( rawLineEdit_->font() );
    const int lineHeight = metrics.lineSpacing();
    const qreal margin = rawLineEdit_->document()->documentMargin();
    const int frame = rawLineEdit_->frameWidth();
    int scrollHeight = 0;
    if ( auto* bar = rawLineEdit_->horizontalScrollBar() ) {
        if ( bar->isVisible() || bar->maximum() > 0 ) {
            scrollHeight = bar->sizeHint().height();
        }
    }
    const int height
        = static_cast<int>( lineCount * lineHeight + margin * 2 ) + frame * 2 + scrollHeight;
    rawLineEdit_->setFixedHeight( height );
}

void PreviewMessageTab::refreshPreviewList()
{
    const bool initial = !initialApplied_;
    const auto currentSelection = previewTypeCombo_->currentData().toString();
    QSignalBlocker blocker( previewTypeCombo_ );

    previewTypeCombo_->clear();
    previewTypeCombo_->addItem( tr( "Auto" ), QString() );

    auto* model = qobject_cast<QStandardItemModel*>( previewTypeCombo_->model() );
    const auto& previews = PreviewManager::instance().all();
    for ( const auto& preview : previews ) {
        previewTypeCombo_->addItem( preview.name, preview.name );
        if ( model ) {
            if ( auto* item = model->item( previewTypeCombo_->count() - 1 ) ) {
                if ( !preview.enabled ) {
                    item->setFlags( item->flags() & ~Qt::ItemIsEnabled );
                }
            }
        }
    }

    QString desiredSelection = currentSelection;
    if ( !initialApplied_ ) {
        if ( !initialPreviewName_.isEmpty()
             && initialPreviewName_.compare( "Auto", Qt::CaseInsensitive ) == 0 ) {
            desiredSelection = QString();
        }
        else {
            desiredSelection = initialPreviewName_;
        }
        initialApplied_ = true;
    }

    const int desiredIndex = previewTypeCombo_->findData( desiredSelection );
    previewTypeCombo_->setCurrentIndex( desiredIndex >= 0 ? desiredIndex : 0 );

    blocker.unblock();
    if ( initial )
        handlePreviewSelectionChanged( previewTypeCombo_->currentIndex() );
    else
        invalidateEvaluation();
}

QString PreviewMessageTab::title() const
{
    return currentTitle_;
}

void PreviewMessageTab::handlePreviewSelectionChanged( int )
{
    renderPreview( previewTypeCombo_->currentData().toString() );
}

void PreviewMessageTab::applyAutoDetection()
{
    renderPreview( {} );
}

void PreviewMessageTab::cancelEvaluation()
{
    ++revision_;
    if ( process_ )
        PreviewService::cancel( process_ );
    process_.clear();
    cancel_->setEnabled( false );
    result_.clear();
    showMessage( tr( "Preview cancelled." ) );
    status_->setText( tr( "Cancelled - run to retry." ) );
    LOG_DEBUG << "Preview pane cancelled revision=" << revision_;
}

void PreviewMessageTab::invalidateEvaluation()
{
    cancelEvaluation();
    const bool manual = decoderMode_->currentData() == "tshark";
    dissector_->setEnabled( manual );
    inputEncoding_->setEnabled( manual );
    timeout_->setEnabled( true );
    status_->setText( tr( "Options changed - run to preview." ) );
    LOG_DEBUG << "Preview pane draft changed revision=" << revision_
              << " decoder=" << decoderMode_->currentData().toString();
}

void PreviewMessageTab::renderPreview( const QString& previewName )
{
    ++revision_;
    if ( process_ )
        PreviewService::cancel( process_ );
    process_.clear();
    result_.clear();
    previewTree_->clear();
    const auto revision = revision_;
    const auto mode = decoderMode_->currentData().toString();
    updateTitle( previewName.isEmpty() ? tr( "Auto" ) : previewName );
    if ( !prepared_.error.isEmpty()
         || ( prepared_.assembly.value( "protocol" ) == "EHCI"
              && prepared_.assembly.value( "state" ) != "complete" ) ) {
        const auto reason
            = prepared_.error.isEmpty()
                  ? prepared_.assembly.value( "reason", "more_data_required" ).toString()
                  : prepared_.error;
        presentResult( { { "state", "incomplete" },
                         { "available", false },
                         { "complete", false },
                         { "reason", reason },
                         { "assembly", prepared_.assembly } } );
        LOG_DEBUG << "Preview pane assembly unavailable revision=" << revision
                  << " reason=" << reason;
        return;
    }
    status_->setText(
        tr( "Running - %1" ).arg( mode == "auto" ? tr( "automatic decoder" ) : mode ) );
    cancel_->setEnabled( true );
    PreviewEvaluationRequest request;
    request.text = prepared_.text;
    request.previewName = previewName;
    request.decoder = mode;
    request.assemblyProtocol = prepared_.assembly.value( "protocol" ).toString();
    request.sessionOwner = this;
    request.timeoutMs = timeout_->value();
    if ( mode == "tshark" ) {
        request.dissector = dissector_->text();
        request.inputEncoding = inputEncoding_->currentData().toString();
    }
    LOG_DEBUG << "Preview pane started revision=" << revision << " decoder=" << mode
              << " assembly=" << request.assemblyProtocol << " named=" << !previewName.isEmpty()
              << " timeout_ms=" << request.timeoutMs;
    const auto digest = PreviewService::definitions();
    request.definitions = digest;
    auto* token = PreviewService::evaluate(
        this, std::move( request ), [ this, revision, digest ]( QVariantMap result ) {
            if ( revision != revision_ || digest != PreviewService::definitions() ) {
                LOG_DEBUG << "Preview pane stale delivery revision=" << revision;
                return;
            }
            process_.clear();
            result[ "assembly" ] = prepared_.assembly;
            result[ "source" ] = prepared_.source;
            result[ "requestRevision" ] = QString::number( revision );
            presentResult( result );
        } );
    if ( revision == revision_ && result_.isEmpty() )
        process_ = token;
}

void PreviewMessageTab::presentResult( const QVariantMap& result )
{
    result_ = result;
    cancel_->setEnabled( false );
    previewTree_->clear();
    const bool engine = result.value( "backend" ) == "tshark_lua";
    previewTree_->headerItem()->setText( 3, engine ? tr( "Offset (decoded buffer)" )
                                                   : tr( "Offset" ) );
    previewTree_->setToolTip(
        engine
            ? tr( "TShark offsets refer to a decoded buffer. Source / log mapping is unavailable." )
            : QString() );
    QVector<QTreeWidgetItem*> parents;
    for ( const auto& entry : result.value( "fields" ).toList() ) {
        const auto field = entry.toMap();
        const int depth = field.value( "depth" ).toInt();
        const auto columns = field.value( "columns" ).toList();
        auto* item = new QTreeWidgetItem;
        for ( int column = 0; column < 5; ++column )
            item->setText( column, columns.value( column ).toString() );
        // The service validates depth/order/quotas. Retain duplicates and empty strings.
        if ( depth > 0 && depth <= parents.size() )
            parents[ depth - 1 ]->addChild( item );
        else
            previewTree_->addTopLevelItem( item );
        if ( parents.size() > depth )
            parents.resize( depth );
        parents.push_back( item );
        const auto attributes = field.value( "attributes" ).toMap();
        if ( !attributes.isEmpty() ) {
            const auto details = QString::fromUtf8(
                QJsonDocument::fromVariant( attributes ).toJson( QJsonDocument::Indented ) );
            const auto tooltip = "<pre>" + details.toHtmlEscaped() + "</pre>";
            for ( int column = 0; column < 5; ++column )
                item->setToolTip( column, tooltip );
        }
    }
    const auto backend = result.value( "backend", "unavailable" ).toString();
    auto status = tr( "%1 - %2" ).arg( backend, result.value( "state" ).toString() );
    if ( result.value( "limited" ).toBool() )
        status += tr( " - limited" );
    const auto reason = result.value( "reason" ).toString();
    if ( !reason.isEmpty() )
        status += " - " + reason.left( 256 );
    const auto fallback = result.value( "fallbackReason" ).toString();
    if ( !fallback.isEmpty() )
        status += tr( " - fallback: %1" ).arg( fallback );
    if ( engine )
        status += tr( " - decoded-buffer offsets; source mapping unavailable" );
    status_->setText( status );
    if ( previewTree_->topLevelItemCount() == 0 )
        showMessage( status );
    previewTree_->expandAll();
    for ( int column = 0; column < 5; ++column )
        previewTree_->resizeColumnToContents( column );
    LOG_DEBUG << "Preview pane completed revision=" << revision_ << " backend=" << backend
              << " state=" << result.value( "state" ).toString() << " reason=" << reason
              << " fields=" << result.value( "fields" ).toList().size();
}

void PreviewMessageTab::showMessage( const QString& message )
{
    previewTree_->clear();
    auto* item = new QTreeWidgetItem( previewTree_ );
    item->setText( 0, message );
}

void PreviewMessageTab::updateTitle( const QString& typeLabel )
{
    const auto snippet = makeSnippet();
    currentTitle_ = tr( "Preview #%1 - %2 - %3" ).arg( tabNumber_ ).arg( typeLabel, snippet );
    Q_EMIT titleChanged( currentTitle_ );
}

QString PreviewMessageTab::makeSnippet() const
{
    auto snippet = rawLine_;
    snippet.replace( '\r', ' ' );
    snippet.replace( '\n', ' ' );
    snippet = snippet.simplified();
    if ( snippet.size() > SnippetLimit ) {
        snippet = snippet.left( SnippetLimit ) + "...";
    }
    if ( snippet.isEmpty() ) {
        return tr( "(empty)" );
    }
    return snippet;
}
