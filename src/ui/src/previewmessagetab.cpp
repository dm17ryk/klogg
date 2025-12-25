#include "previewmessagetab.h"

#include <QComboBox>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
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
                << " field '" << info.fieldPath.toStdString() << "': "
                << info.reason.toStdString() << " (source=" << info.source.toStdString()
                << ", offset=" << info.offset << ", width=" << info.width
                << ", raw=" << info.rawSlice.toStdString() << ")";
}

void setItemStatus( QTreeWidgetItem* item, const QString& text, const QString& tooltip )
{
    if ( !item ) {
        return;
    }
    item->setText( 1, text );
    if ( !tooltip.isEmpty() ) {
        item->setToolTip( 1, tooltip );
    }
}

void setItemDecodeError( QTreeWidgetItem* item, const DecodeErrorInfo& info )
{
    const auto shortText = QObject::tr( "Decode error: %1" ).arg( info.reason );
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
    const QRegularExpressionMatch* match = nullptr;
    QString previewName;
    QString bufferSource;
};

void addFieldItems( QTreeWidgetItem* parent,
                    const PreviewFieldSpec& field,
                    ParseContext& context,
                    const QString& prefix );

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

        auto* item = new QTreeWidgetItem( parent );
        const auto fullName = prefix.isEmpty() ? bitField.name : prefix + "." + bitField.name;
        item->setText( 0, bitField.name );
        item->setText( 1, formatNumber( bitValue, bitField.format, bitField ) );
        if ( context.values ) {
            context.values->insert( fullName, static_cast<qint64>( bitValue ) );
        }
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

void addFieldItems( QTreeWidgetItem* parent,
                    const PreviewFieldSpec& field,
                    ParseContext& context,
                    const QString& prefix )
{
    auto* item = new QTreeWidgetItem( parent );
    item->setText( 0, field.name );

    const auto fullName = prefix.isEmpty() ? field.name : prefix + "." + field.name;

    if ( field.source == PreviewFieldSource::Capture ) {
        const auto captured = context.match ? captureValue( field.capture, *context.match ) : QString();
        const auto source = describeCaptureRef( field.capture, "capture" );
        if ( !field.capture.isSet ) {
            DecodeErrorInfo errorInfo{ context.previewName, fullName, source, -1, -1,
                                       truncateText( captured, 64 ),
                                       QObject::tr( "Capture is not set." ) };
            setItemDecodeError( item, errorInfo );
            return;
        }
        const auto rawText = captured;
        const auto rawBytes = captured.toUtf8();

        if ( field.format == PreviewFormat::Fields ) {
            const auto decoded = decodeBytesFromText( rawText, field.type );
            if ( !decoded.ok ) {
                DecodeErrorInfo errorInfo{ context.previewName, fullName, source, -1, -1,
                                           truncateText( rawText, 64 ), decoded.error };
                setItemDecodeError( item, errorInfo );
                return;
            }
            item->setText( 1, QObject::tr( "%1 bytes" ).arg( decoded.bytes.size() ) );
            ParseContext childContext{
                decoded.bytes, 0, context.values, context.match, context.previewName, source };
            for ( const auto& child : field.fields ) {
                addFieldItems( item, child, childContext, fullName );
            }
            return;
        }

        if ( field.format == PreviewFormat::String ) {
            QString error;
            const auto value = decodeStringValue( rawText, rawBytes, field.type, &error );
            if ( !error.isEmpty() ) {
                DecodeErrorInfo errorInfo{ context.previewName, fullName, source, -1, -1,
                                           truncateText( rawText, 64 ), error };
                setItemDecodeError( item, errorInfo );
                return;
            }
            item->setText( 1, value );
        }
        else {
            quint64 numeric = 0;
            QString error;
            if ( !parseNumericValue( rawText, rawBytes, field, &numeric, &error ) ) {
                DecodeErrorInfo errorInfo{ context.previewName, fullName, source, -1, -1,
                                           truncateText( rawText, 64 ), error };
                setItemDecodeError( item, errorInfo );
                return;
            }
            item->setText( 1, formatNumber( numeric, field.format, field ) );
            if ( context.values ) {
                context.values->insert( fullName, static_cast<qint64>( numeric ) );
            }
            if ( field.format == PreviewFormat::Bitfield ) {
                addBitfieldItems( item, field, numeric,
                                  resolveBitfieldWidth( field, context ), context, fullName );
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

    const auto source = context.bufferSource.isEmpty()
                            ? QObject::tr( "buffer" )
                            : context.bufferSource;

    int offset = 0;
    if ( field.offset.isSet ) {
        const auto offsetExpr = resolveExprValue( field.offset, *context.values );
        if ( !offsetExpr.ok ) {
            reportExpressionIssue( offsetExpr, source, context.cursor, -1 );
            return;
        }
        offset = offsetExpr.value;
        if ( offset < 0 ) {
            DecodeErrorInfo errorInfo{ context.previewName, fullName, source, offset, -1,
                                       QString(), QObject::tr( "Offset is negative." ) };
            setItemDecodeError( item, errorInfo );
            return;
        }
        context.cursor += offset;
    }

    const int remaining = context.buffer.size() - context.cursor;
    if ( remaining < 0 ) {
        DecodeErrorInfo errorInfo{ context.previewName, fullName, source, context.cursor, -1,
                                   QString(), QObject::tr( "Offset exceeds buffer size." ) };
        setItemDecodeError( item, errorInfo );
        return;
    }

    int width = remaining;
    if ( field.width.isSet ) {
        const auto widthExpr = resolveExprValue( field.width, *context.values );
        if ( !widthExpr.ok ) {
            reportExpressionIssue( widthExpr, source, context.cursor, -1 );
            return;
        }
        width = widthExpr.value;
        if ( width < 0 ) {
            DecodeErrorInfo errorInfo{ context.previewName, fullName, source, context.cursor,
                                       width, QString(),
                                       QObject::tr( "Width is negative." ) };
            setItemDecodeError( item, errorInfo );
            return;
        }
        if ( width > remaining ) {
            DecodeErrorInfo errorInfo{
                context.previewName,
                fullName,
                source,
                context.cursor,
                width,
                QString(),
                QObject::tr( "Width exceeds remaining buffer (%1 bytes)." ).arg( remaining ) };
            setItemDecodeError( item, errorInfo );
            return;
        }
    }

    const int sliceOffset = context.cursor;
    const auto slice = context.buffer.mid( sliceOffset, width );
    context.cursor += width;
    const auto rawText = QString::fromUtf8( slice );

    if ( field.format == PreviewFormat::Fields ) {
        const auto decoded = decodeBytesFromSlice( slice, field.type );
        if ( !decoded.ok ) {
            DecodeErrorInfo errorInfo{ context.previewName, fullName, source, sliceOffset,
                                       width, sliceToLogText( slice ), decoded.error };
            setItemDecodeError( item, errorInfo );
            return;
        }
        item->setText( 1, QObject::tr( "%1 bytes" ).arg( decoded.bytes.size() ) );
        ParseContext childContext{
            decoded.bytes, 0, context.values, context.match, context.previewName, source };
        for ( const auto& child : field.fields ) {
            addFieldItems( item, child, childContext, fullName );
        }
        return;
    }

    if ( field.format == PreviewFormat::String ) {
        QString error;
        const auto value = decodeStringValue( rawText, slice, field.type, &error );
        if ( !error.isEmpty() ) {
            DecodeErrorInfo errorInfo{ context.previewName, fullName, source, sliceOffset, width,
                                       sliceToLogText( slice ), error };
            setItemDecodeError( item, errorInfo );
            return;
        }
        item->setText( 1, value );
    }
    else {
        quint64 numeric = 0;
        QString error;
        if ( !parseNumericValue( rawText, slice, field, &numeric, &error ) ) {
            DecodeErrorInfo errorInfo{ context.previewName, fullName, source, sliceOffset, width,
                                       sliceToLogText( slice ), error };
            setItemDecodeError( item, errorInfo );
            return;
        }
        item->setText( 1, formatNumber( numeric, field.format, field ) );
        if ( context.values ) {
            context.values->insert( fullName, static_cast<qint64>( numeric ) );
        }
        if ( field.format == PreviewFormat::Bitfield ) {
            addBitfieldItems( item, field, numeric,
                              resolveBitfieldWidth( field, context ), context, fullName );
        }
    }
}
} // namespace

PreviewMessageTab::PreviewMessageTab( const QString& rawLine,
                                      const QString& initialPreviewNameOrAuto,
                                      int tabNumber,
                                      QWidget* parent )
    : QWidget( parent )
    , rawLine_( rawLine )
    , initialPreviewName_( initialPreviewNameOrAuto )
    , tabNumber_( tabNumber )
{
    buildUi();
    rawLineEdit_->setPlainText( rawLine_ );
    updateRawLineHeight();
    refreshPreviewList();
}

void PreviewMessageTab::buildUi()
{
    previewTypeCombo_ = new QComboBox( this );
    previewTypeCombo_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );

    auto* typeLabel = new QLabel( tr( "Preview type:" ), this );

    auto* headerLayout = new QHBoxLayout();
    headerLayout->addWidget( typeLabel );
    headerLayout->addWidget( previewTypeCombo_, 1 );

    previewTree_ = new QTreeWidget( this );
    previewTree_->setColumnCount( 2 );
    previewTree_->setHeaderLabels( QStringList() << tr( "Field" ) << tr( "Value" ) );
    previewTree_->setRootIsDecorated( true );
    previewTree_->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    previewTree_->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    previewTree_->header()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
    previewTree_->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
    previewTree_->header()->setStretchLastSection( false );

    rawGroup_ = new QGroupBox( tr( "Raw line" ), this );
    rawGroup_->setCheckable( true );
    rawGroup_->setChecked( false );

    rawLineEdit_ = new QPlainTextEdit( rawGroup_ );
    rawLineEdit_->setReadOnly( true );
    rawLineEdit_->setWordWrapMode( QTextOption::NoWrap );
    rawLineEdit_->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    rawLineEdit_->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );

    auto* rawLayout = new QVBoxLayout();
    rawLayout->addWidget( rawLineEdit_ );
    rawGroup_->setLayout( rawLayout );
    rawGroup_->setFlat( true );
    rawLineEdit_->setVisible( false );

    connect( rawGroup_, &QGroupBox::toggled, rawLineEdit_, &QWidget::setVisible );
    connect( previewTypeCombo_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &PreviewMessageTab::handlePreviewSelectionChanged );

    auto* layout = new QVBoxLayout();
    layout->addLayout( headerLayout );
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
    const int height = static_cast<int>( lineCount * lineHeight + margin * 2 ) + frame * 2;
    rawLineEdit_->setFixedHeight( height );
}

void PreviewMessageTab::refreshPreviewList()
{
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
    handlePreviewSelectionChanged( previewTypeCombo_->currentIndex() );
}

QString PreviewMessageTab::title() const
{
    return currentTitle_;
}

void PreviewMessageTab::handlePreviewSelectionChanged( int )
{
    const auto selection = previewTypeCombo_->currentData().toString();
    if ( selection.isEmpty() ) {
        applyAutoDetection();
        return;
    }
    renderPreview( selection );
}

void PreviewMessageTab::applyAutoDetection()
{
    if ( PreviewManager::instance().all().isEmpty() ) {
        showMessage( tr( "No preview definitions loaded." ) );
        updateTitle( tr( "Auto" ) );
        return;
    }

    const auto matchName = PreviewManager::instance().findFirstMatchingEnabledPreview( rawLine_ );
    if ( matchName.isEmpty() ) {
        showMessage( tr( "No preview matched." ) );
        updateTitle( tr( "Auto" ) );
        return;
    }

    {
        QSignalBlocker blocker( previewTypeCombo_ );
        const auto index = previewTypeCombo_->findData( matchName );
        if ( index >= 0 ) {
            previewTypeCombo_->setCurrentIndex( index );
        }
    }

    renderPreview( matchName );
}

void PreviewMessageTab::renderPreview( const QString& previewName )
{
    previewTree_->clear();

    const auto& manager = PreviewManager::instance();
    if ( manager.all().isEmpty() ) {
        showMessage( tr( "No preview definitions loaded." ) );
        updateTitle( tr( "Auto" ) );
        return;
    }

    const auto* definition = manager.findByName( previewName );
    if ( !definition ) {
        showMessage( tr( "Selected preview is not available." ) );
        updateTitle( previewName );
        return;
    }

    const auto match = definition->compiled.match( rawLine_ );
    if ( !match.hasMatch() ) {
        showMessage( tr( "No match for selected preview." ) );
        updateTitle( previewName );
        return;
    }

    QString bufferText;
    if ( definition->bufferCapture.isSet ) {
        bufferText = captureValue( definition->bufferCapture, match );
    }
    else {
        bufferText = rawLine_;
    }

    const auto bufferSource = definition->bufferCapture.isSet
                                  ? describeCaptureRef( definition->bufferCapture, "bufferCapture" )
                                  : tr( "raw line" );
    const auto decodedBuffer = decodeBytesFromText( bufferText, definition->type );
    if ( !decodedBuffer.ok ) {
        DecodeErrorInfo errorInfo{ previewName,
                                   QObject::tr( "buffer" ),
                                   bufferSource,
                                   0,
                                   -1,
                                   truncateText( bufferText, 64 ),
                                   decodedBuffer.error };
        logDecodeError( errorInfo );
        showMessage( tr( "Decode error: %1" ).arg( decodedBuffer.error ) );
        updateTitle( previewName );
        return;
    }

    QMap<QString, qint64> values;
    ParseContext context{ decodedBuffer.bytes, 0, &values, &match, previewName, bufferSource };

    if ( definition->offset.isSet ) {
        const auto offsetExpr = resolveExprValue( definition->offset, values );
        if ( !offsetExpr.ok ) {
            DecodeErrorInfo errorInfo{ previewName,
                                       QObject::tr( "buffer" ),
                                       bufferSource,
                                       0,
                                       -1,
                                       truncateText( bufferText, 64 ),
                                       offsetExpr.error.isEmpty()
                                           ? tr( "Invalid preview offset." )
                                           : offsetExpr.error };
            logDecodeError( errorInfo );
            showMessage( tr( "Decode error: %1" ).arg( errorInfo.reason ) );
            updateTitle( previewName );
            return;
        }
        if ( offsetExpr.value < 0 ) {
            DecodeErrorInfo errorInfo{ previewName,
                                       QObject::tr( "buffer" ),
                                       bufferSource,
                                       offsetExpr.value,
                                       -1,
                                       truncateText( bufferText, 64 ),
                                       tr( "Preview offset is negative." ) };
            logDecodeError( errorInfo );
            showMessage( tr( "Decode error: preview offset is negative." ) );
            updateTitle( previewName );
            return;
        }
        context.cursor += offsetExpr.value;
        if ( context.cursor > decodedBuffer.bytes.size() ) {
            DecodeErrorInfo errorInfo{ previewName,
                                       QObject::tr( "buffer" ),
                                       bufferSource,
                                       offsetExpr.value,
                                       -1,
                                       truncateText( bufferText, 64 ),
                                       tr( "Preview offset exceeds buffer size." ) };
            logDecodeError( errorInfo );
            showMessage( tr( "Decode error: preview offset exceeds buffer size." ) );
            updateTitle( previewName );
            return;
        }
    }

    auto* root = previewTree_->invisibleRootItem();
    for ( const auto& field : definition->fields ) {
        addFieldItems( root, field, context, QString() );
    }

    previewTree_->expandAll();
    updateTitle( previewName );
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
