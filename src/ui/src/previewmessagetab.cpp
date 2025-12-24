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

QByteArray cleanHexInput( const QByteArray& input )
{
    QByteArray cleaned;
    cleaned.reserve( input.size() );
    for ( const auto ch : input ) {
        if ( ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t' ) {
            cleaned.push_back( ch );
        }
    }
    return cleaned;
}

QByteArray decodeBuffer( const QByteArray& input, PreviewBufferType type, bool* ok )
{
    if ( ok ) {
        *ok = true;
    }
    switch ( type ) {
    case PreviewBufferType::HexString: {
        const auto cleaned = cleanHexInput( input );
        if ( cleaned.size() % 2 != 0 ) {
            if ( ok ) {
                *ok = false;
            }
            return {};
        }
        auto output = QByteArray::fromHex( cleaned );
        if ( output.isEmpty() && !cleaned.isEmpty() && ok ) {
            *ok = false;
        }
        return output;
    }
    case PreviewBufferType::Base64: {
        auto output = QByteArray::fromBase64( input );
        if ( output.isEmpty() && !input.isEmpty() && ok ) {
            *ok = false;
        }
        return output;
    }
    case PreviewBufferType::Bin:
    case PreviewBufferType::Bytes:
    case PreviewBufferType::String:
        return input;
    }
    return input;
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

int resolveExpr( const PreviewValueExpr& expr, const QMap<QString, qint64>& values, bool* ok )
{
    if ( ok ) {
        *ok = false;
    }
    if ( !expr.isSet ) {
        return 0;
    }
    if ( expr.isLiteral ) {
        if ( ok ) {
            *ok = true;
        }
        return expr.literalValue;
    }
    const auto trimmed = expr.expression.trimmed();
    if ( trimmed.startsWith( "{" ) && trimmed.endsWith( "}" ) ) {
        const auto key = trimmed.mid( 1, trimmed.size() - 2 );
        if ( values.contains( key ) ) {
            if ( ok ) {
                *ok = true;
            }
            return static_cast<int>( values.value( key ) );
        }
        return 0;
    }
    bool parsed = false;
    const auto value = trimmed.toInt( &parsed, 0 );
    if ( ok ) {
        *ok = parsed;
    }
    return parsed ? value : 0;
}

struct ParseContext {
    QByteArray buffer;
    int cursor = 0;
    QMap<QString, qint64>* values = nullptr;
    const QRegularExpressionMatch* match = nullptr;
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
        bool ok = false;
        int width = resolveExpr( bitField.width, *context.values, &ok );
        if ( !ok || width <= 0 ) {
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
    bool totalOk = false;
    int totalBits = resolveExpr( field.width, *context.values, &totalOk );
    if ( totalOk && totalBits > 0 ) {
        return totalBits;
    }

    totalBits = 0;
    for ( const auto& sub : field.bitfieldMap ) {
        bool subOk = false;
        const auto width = resolveExpr( sub.width, *context.values, &subOk );
        totalBits += subOk ? width : 1;
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
        QByteArray data = captured.toUtf8();
        bool decodeOk = true;
        data = decodeBuffer( data, field.type, &decodeOk );

        quint64 numeric = 0;
        bool numericOk = false;
        if ( field.format == PreviewFormat::String ) {
            if ( decodeOk
                 && ( field.type == PreviewBufferType::HexString
                      || field.type == PreviewBufferType::Base64 ) ) {
                item->setText( 1, QString::fromUtf8( data ) );
            }
            else {
                item->setText( 1, captured );
            }
        }
        else {
            if ( decodeOk && !data.isEmpty() ) {
                numeric = parseInteger( data, field.endianness, &numericOk );
            }
            if ( !numericOk ) {
                numericOk = parseNumericString( captured, &numeric );
            }
            item->setText( 1, numericOk ? formatNumber( numeric, field.format, field ) : captured );
        }

        if ( context.values && numericOk ) {
            context.values->insert( fullName, static_cast<qint64>( numeric ) );
        }
        if ( field.format == PreviewFormat::Bitfield ) {
            addBitfieldItems( item, field, numericOk ? numeric : 0,
                              resolveBitfieldWidth( field, context ), context, fullName );
        }
        return;
    }

    bool offsetOk = false;
    const int offset = resolveExpr( field.offset, *context.values, &offsetOk );
    if ( offsetOk && offset > 0 ) {
        context.cursor += offset;
    }

    bool widthOk = false;
    int width = resolveExpr( field.width, *context.values, &widthOk );
    if ( !widthOk || width <= 0 ) {
        width = context.buffer.size() - context.cursor;
    }
    if ( width < 0 ) {
        width = 0;
    }

    const auto slice = context.buffer.mid( context.cursor, width );
    context.cursor += width;

    bool decodeOk = true;
    auto decoded = decodeBuffer( slice, field.type, &decodeOk );
    if ( !decodeOk ) {
        item->setText( 1, "Decode error" );
        return;
    }

    if ( field.format == PreviewFormat::Fields ) {
        item->setText( 1, QString( "%1 bytes" ).arg( decoded.size() ) );
        ParseContext childContext{ decoded, 0, context.values, context.match };
        for ( const auto& child : field.fields ) {
            addFieldItems( item, child, childContext, fullName );
        }
        return;
    }

    quint64 numeric = 0;
    bool numericOk = false;
    if ( field.format == PreviewFormat::String ) {
        item->setText( 1, QString::fromUtf8( decoded ) );
    }
    else {
        if ( field.type == PreviewBufferType::Bin ) {
            numericOk = parseNumericString( QString::fromUtf8( decoded ), &numeric );
        }
        if ( !numericOk ) {
            numeric = parseInteger( decoded, field.endianness, &numericOk );
        }
        item->setText( 1, numericOk ? formatNumber( numeric, field.format, field )
                                    : QString::fromUtf8( decoded ) );
    }

    if ( context.values && numericOk ) {
        context.values->insert( fullName, static_cast<qint64>( numeric ) );
    }

    if ( field.format == PreviewFormat::Bitfield ) {
        addBitfieldItems( item, field, numericOk ? numeric : 0,
                          resolveBitfieldWidth( field, context ), context, fullName );
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

    bool decodeOk = true;
    auto buffer = decodeBuffer( bufferText.toUtf8(), definition->type, &decodeOk );
    if ( !decodeOk ) {
        showMessage( tr( "Decode error." ) );
        updateTitle( previewName );
        return;
    }

    QMap<QString, qint64> values;
    ParseContext context{ buffer, 0, &values, &match };

    if ( definition->offset.isSet ) {
        bool ok = false;
        const auto offset = resolveExpr( definition->offset, values, &ok );
        if ( ok && offset > 0 ) {
            context.cursor += offset;
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
