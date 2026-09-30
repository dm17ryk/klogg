#include "actionsconfig.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include "actionexpression.h"
#include "log.h"
#include "previewdecodeutils.h"

namespace {
ActionSequenceType parseSequenceType( const QString& text, bool* ok )
{
    if ( ok ) {
        *ok = true;
    }
    const auto normalized = text.trimmed().toLower();
    if ( normalized == "string" ) {
        return ActionSequenceType::String;
    }
    if ( normalized == "hexstring" ) {
        return ActionSequenceType::HexString;
    }
    if ( ok ) {
        *ok = false;
    }
    return ActionSequenceType::String;
}

ResponseMatchType parseMatchType( const QString& text, bool* ok )
{
    if ( ok ) {
        *ok = true;
    }
    const auto normalized = text.trimmed().toLower();
    if ( normalized == "string" ) {
        return ResponseMatchType::String;
    }
    if ( normalized == "hexstring" ) {
        return ResponseMatchType::HexString;
    }
    if ( normalized == "regex" ) {
        return ResponseMatchType::Regex;
    }
    if ( normalized == "wildcard" ) {
        return ResponseMatchType::Wildcard;
    }
    if ( ok ) {
        *ok = false;
    }
    return ResponseMatchType::String;
}

QStringList readStringListValue( const QVariant& value )
{
    if ( value.typeId() == QMetaType::QStringList ) {
        return value.toStringList();
    }

    QStringList values;
    const auto list = value.toList();
    for ( const auto& item : list ) {
        const auto text = item.toString().trimmed();
        if ( !text.isEmpty() ) {
            values.push_back( text );
        }
    }
    return values;
}

void setError( QString* errorMessage, const QString& message )
{
    if ( errorMessage != nullptr ) {
        *errorMessage = message;
    }
}

void normalizeResponseAction( ResponseActionDefinition* responseAction )
{
    if ( responseAction == nullptr ) {
        return;
    }

    responseAction->steps.erase(
        std::remove_if( responseAction->steps.begin(), responseAction->steps.end(),
                        []( const ResponseActionStep& step ) { return step.actionId < 0; } ),
        responseAction->steps.end() );

    if ( responseAction->steps.isEmpty() && responseAction->hasActionId
         && responseAction->actionId >= 0 ) {
        responseAction->steps.push_back( { responseAction->actionId, 0 } );
    }

    if ( !responseAction->steps.isEmpty() ) {
        responseAction->hasActionId = true;
        responseAction->actionId = responseAction->steps.front().actionId;
    }
    else {
        responseAction->hasActionId = false;
        responseAction->actionId = -1;
    }

    if ( !responseAction->hasInlineAction ) {
        responseAction->inlineAction = {};
    }
}

QString bytesToHexString( const QByteArray& bytes )
{
    QStringList parts;
    parts.reserve( bytes.size() );
    for ( const auto byte : bytes ) {
        parts.push_back( QStringLiteral( "%1" )
                             .arg( static_cast<quint8>( byte ), 2, 16, QLatin1Char( '0' ) )
                             .toUpper() );
    }
    return parts.join( QLatin1Char( ' ' ) );
}

QByteArray applyChecksum( const ActionDefinition& action,
                          const QByteArray& baseBytes,
                          QString* errorMessage )
{
    if ( !action.checksum.enabled ) {
        return baseBytes;
    }

    QByteArray checksumBytes;
    const auto algorithm = action.checksum.algorithm.trimmed().toLower();
    if ( algorithm == QStringLiteral( "sum8" ) ) {
        quint8 sum = 0;
        for ( const auto byte : baseBytes ) {
            sum = static_cast<quint8>( sum + static_cast<quint8>( byte ) );
        }
        checksumBytes.append( static_cast<char>( sum ) );
    }
    else if ( algorithm == QStringLiteral( "crc16_ccitt" ) ) {
        quint16 crc = 0xFFFF;
        for ( const auto byte : baseBytes ) {
            crc ^= static_cast<quint8>( byte ) << 8;
            for ( int i = 0; i < 8; ++i ) {
                if ( ( crc & 0x8000 ) != 0u ) {
                    crc = static_cast<quint16>( static_cast<quint16>( crc << 1 )
                                                ^ static_cast<quint16>( 0x1021u ) );
                }
                else {
                    crc = static_cast<quint16>( crc << 1 );
                }
            }
        }
        checksumBytes.append( static_cast<char>( ( crc >> 8 ) & 0xFF ) );
        checksumBytes.append( static_cast<char>( crc & 0xFF ) );
    }
    else {
        setError( errorMessage, QStringLiteral( "Unsupported checksum algorithm." ) );
        return {};
    }

    if ( action.sequence.type == ActionSequenceType::HexString ) {
        return bytesToHexString( checksumBytes ).toLatin1();
    }

    return checksumBytes.toHex().toUpper();
}

bool isValidActionChecksumAlgorithm( const QString& algorithm )
{
    const auto normalized = algorithm.trimmed().toLower();
    return normalized == QStringLiteral( "sum8" )
           || normalized == QStringLiteral( "crc16_ccitt" );
}

void normalizeActionOrderValues( QVector<ActionDefinition>* actions )
{
    if ( actions == nullptr ) {
        return;
    }

    std::stable_sort( actions->begin(), actions->end(), []( const auto& lhs, const auto& rhs ) {
        return lhs.order < rhs.order;
    } );

    for ( int index = 0; index < actions->size(); ++index ) {
        ( *actions )[ index ].order = index;
    }
}

void normalizeResponseOrderValues( QVector<ResponseDefinition>* responses )
{
    if ( responses == nullptr ) {
        return;
    }

    std::stable_sort( responses->begin(), responses->end(), []( const auto& lhs, const auto& rhs ) {
        return lhs.order < rhs.order;
    } );

    for ( int index = 0; index < responses->size(); ++index ) {
        ( *responses )[ index ].order = index;
    }
}
} // namespace

QString actionSequenceTypeToString( ActionSequenceType type )
{
    switch ( type ) {
    case ActionSequenceType::HexString:
        return "hexString";
    case ActionSequenceType::String:
    default:
        return "string";
    }
}

ActionSequenceType actionSequenceTypeFromString( const QString& text, bool* ok )
{
    return parseSequenceType( text, ok );
}

QString actionParameterTypeToString( ActionParameterType type )
{
    switch ( type ) {
    case ActionParameterType::Integer:
        return QStringLiteral( "integer" );
    case ActionParameterType::Decimal:
        return QStringLiteral( "decimal" );
    case ActionParameterType::Boolean:
        return QStringLiteral( "boolean" );
    case ActionParameterType::Choice:
        return QStringLiteral( "choice" );
    case ActionParameterType::MultiChoice:
        return QStringLiteral( "multi_choice" );
    case ActionParameterType::Date:
        return QStringLiteral( "date" );
    case ActionParameterType::Time:
        return QStringLiteral( "time" );
    case ActionParameterType::DateTime:
        return QStringLiteral( "datetime" );
    case ActionParameterType::IpAddress:
        return QStringLiteral( "ip" );
    case ActionParameterType::MacAddress:
        return QStringLiteral( "mac" );
    case ActionParameterType::PhoneNumber:
        return QStringLiteral( "phone" );
    case ActionParameterType::HexBytes:
        return QStringLiteral( "hex_bytes" );
    case ActionParameterType::Text:
    default:
        return QStringLiteral( "text" );
    }
}

ActionParameterType actionParameterTypeFromString( const QString& text, bool* ok )
{
    if ( ok ) {
        *ok = true;
    }
    const auto normalized = text.trimmed().toLower();
    if ( normalized == QLatin1String( "text" ) )
        return ActionParameterType::Text;
    if ( normalized == QLatin1String( "integer" ) )
        return ActionParameterType::Integer;
    if ( normalized == QLatin1String( "decimal" ) )
        return ActionParameterType::Decimal;
    if ( normalized == QLatin1String( "boolean" ) )
        return ActionParameterType::Boolean;
    if ( normalized == QLatin1String( "choice" ) )
        return ActionParameterType::Choice;
    if ( normalized == QLatin1String( "multi_choice" ) )
        return ActionParameterType::MultiChoice;
    if ( normalized == QLatin1String( "date" ) )
        return ActionParameterType::Date;
    if ( normalized == QLatin1String( "time" ) )
        return ActionParameterType::Time;
    if ( normalized == QLatin1String( "datetime" ) || normalized == QLatin1String( "date_time" ) ) {
        return ActionParameterType::DateTime;
    }
    if ( normalized == QLatin1String( "ip" ) || normalized == QLatin1String( "ip_address" ) ) {
        return ActionParameterType::IpAddress;
    }
    if ( normalized == QLatin1String( "mac" ) || normalized == QLatin1String( "mac_address" ) ) {
        return ActionParameterType::MacAddress;
    }
    if ( normalized == QLatin1String( "phone" ) || normalized == QLatin1String( "phone_number" ) ) {
        return ActionParameterType::PhoneNumber;
    }
    if ( normalized == QLatin1String( "hex_bytes" ) )
        return ActionParameterType::HexBytes;
    if ( ok ) {
        *ok = false;
    }
    return ActionParameterType::Text;
}

QString actionParameterPresentationToString( ActionParameterPresentation presentation )
{
    switch ( presentation ) {
    case ActionParameterPresentation::TextBox:
        return QStringLiteral( "text_box" );
    case ActionParameterPresentation::ComboBox:
        return QStringLiteral( "combo_box" );
    case ActionParameterPresentation::RadioButtons:
        return QStringLiteral( "radio_buttons" );
    case ActionParameterPresentation::CheckBoxes:
        return QStringLiteral( "check_boxes" );
    case ActionParameterPresentation::Automatic:
    default:
        return QStringLiteral( "auto" );
    }
}

ActionParameterPresentation actionParameterPresentationFromString( const QString& text, bool* ok )
{
    if ( ok ) {
        *ok = true;
    }
    const auto normalized = text.trimmed().toLower();
    if ( normalized == QLatin1String( "auto" ) )
        return ActionParameterPresentation::Automatic;
    if ( normalized == QLatin1String( "text" ) || normalized == QLatin1String( "text_box" ) ) {
        return ActionParameterPresentation::TextBox;
    }
    if ( normalized == QLatin1String( "combo" ) || normalized == QLatin1String( "combobox" )
         || normalized == QLatin1String( "combo_box" ) ) {
        return ActionParameterPresentation::ComboBox;
    }
    if ( normalized == QLatin1String( "radio" ) || normalized == QLatin1String( "radiobutton" )
         || normalized == QLatin1String( "radio_buttons" ) ) {
        return ActionParameterPresentation::RadioButtons;
    }
    if ( normalized == QLatin1String( "checkboxes" )
         || normalized == QLatin1String( "check_boxes" ) ) {
        return ActionParameterPresentation::CheckBoxes;
    }
    if ( ok ) {
        *ok = false;
    }
    return ActionParameterPresentation::Automatic;
}

QString actionMultiValueModeToString( ActionMultiValueMode mode )
{
    switch ( mode ) {
    case ActionMultiValueMode::BitwiseOr:
        return QStringLiteral( "bitwise_or" );
    case ActionMultiValueMode::CustomSeparator:
        return QStringLiteral( "custom_separator" );
    case ActionMultiValueMode::CommaSeparated:
    default:
        return QStringLiteral( "comma_separated" );
    }
}

ActionMultiValueMode actionMultiValueModeFromString( const QString& text, bool* ok )
{
    if ( ok ) {
        *ok = true;
    }
    const auto normalized = text.trimmed().toLower();
    if ( normalized == QLatin1String( "bitwise_or" ) )
        return ActionMultiValueMode::BitwiseOr;
    if ( normalized == QLatin1String( "comma_separated" ) ) {
        return ActionMultiValueMode::CommaSeparated;
    }
    if ( normalized == QLatin1String( "separator" )
         || normalized == QLatin1String( "custom_separator" ) ) {
        return ActionMultiValueMode::CustomSeparator;
    }
    if ( ok ) {
        *ok = false;
    }
    return ActionMultiValueMode::CommaSeparated;
}

QString responseMatchTypeToString( ResponseMatchType type )
{
    switch ( type ) {
    case ResponseMatchType::HexString:
        return "hexString";
    case ResponseMatchType::Regex:
        return "regex";
    case ResponseMatchType::Wildcard:
        return "wildcard";
    case ResponseMatchType::String:
    default:
        return "string";
    }
}

ResponseMatchType responseMatchTypeFromString( const QString& text, bool* ok )
{
    return parseMatchType( text, ok );
}

QString responseParameterBindingSourceToString( ResponseParameterBindingSource source )
{
    switch ( source ) {
    case ResponseParameterBindingSource::Literal:
        return QStringLiteral( "literal" );
    case ResponseParameterBindingSource::Capture:
        return QStringLiteral( "capture" );
    case ResponseParameterBindingSource::Expression:
        return QStringLiteral( "expression" );
    case ResponseParameterBindingSource::Legacy:
    default:
        return QStringLiteral( "legacy" );
    }
}

ResponseParameterBindingSource responseParameterBindingSourceFromString( const QString& text,
                                                                          bool* ok )
{
    if ( ok ) {
        *ok = true;
    }
    const auto normalized = text.trimmed().toLower();
    if ( normalized == QLatin1String( "literal" ) ) {
        return ResponseParameterBindingSource::Literal;
    }
    if ( normalized == QLatin1String( "capture" ) ) {
        return ResponseParameterBindingSource::Capture;
    }
    if ( normalized == QLatin1String( "expression" ) ) {
        return ResponseParameterBindingSource::Expression;
    }
    if ( normalized == QLatin1String( "legacy" ) ) {
        return ResponseParameterBindingSource::Legacy;
    }
    if ( ok ) {
        *ok = false;
    }
    return ResponseParameterBindingSource::Legacy;
}

ResponseParameterBinding responseParameterBindingFromVariant( const QVariant& value,
                                                               QString* errorMessage )
{
    ResponseParameterBinding binding;
    if ( value.typeId() != QMetaType::QVariantMap ) {
        binding.value = value;
        return binding;
    }

    const auto map = value.toMap();
    if ( !map.contains( QStringLiteral( "source" ) ) ) {
        binding.value = value;
        return binding;
    }

    bool sourceOk = false;
    binding.source = responseParameterBindingSourceFromString(
        map.value( QStringLiteral( "source" ) ).toString(), &sourceOk );
    if ( !sourceOk || binding.source == ResponseParameterBindingSource::Legacy ) {
        if ( errorMessage ) {
            *errorMessage = QStringLiteral( "Response parameter binding has an invalid source." );
        }
        LOG_WARNING << "Invalid response parameter binding source";
        binding.source = ResponseParameterBindingSource::Legacy;
        binding.value = value;
        return binding;
    }

    if ( !map.contains( QStringLiteral( "value" ) ) ) {
        if ( errorMessage ) {
            *errorMessage = QStringLiteral( "Response parameter binding is missing its value." );
        }
        LOG_WARNING << "Response parameter binding is missing value";
        binding.source = ResponseParameterBindingSource::Legacy;
        binding.value = value;
        return binding;
    }

    binding.value = map.value( QStringLiteral( "value" ) );
    return binding;
}

QVariant responseParameterBindingToVariant( const ResponseParameterBinding& binding )
{
    if ( binding.source == ResponseParameterBindingSource::Legacy ) {
        return binding.value;
    }

    QVariantMap map;
    map.insert( QStringLiteral( "source" ),
                responseParameterBindingSourceToString( binding.source ) );
    map.insert( QStringLiteral( "value" ), binding.value );
    return map;
}

namespace {
ActionExpressionResult normalizeParameterValue( const ActionParameterDefinition& field,
                                                const QVariant& input,
                                                const QVariantMap& currentValues )
{
    QVariant value = input;
    if ( field.type == ActionParameterType::MultiChoice ) {
        auto selected = value.toList();
        if ( value.typeId() != QMetaType::QVariantList
             && value.typeId() != QMetaType::QStringList ) {
            selected = { value };
        }
        if ( field.multiValueMode == ActionMultiValueMode::BitwiseOr ) {
            qlonglong combined = 0;
            for ( const auto& item : selected ) {
                bool ok = false;
                const auto number = item.toLongLong( &ok );
                if ( !ok ) {
                    return { false,
                             {},
                             QStringLiteral(
                                 "Parameter '%1' contains a non-integer bitmask value." )
                                 .arg( field.name ) };
                }
                combined |= number;
            }
            value = combined;
        }
        else {
            QStringList parts;
            for ( const auto& item : selected ) {
                parts.push_back( item.toString() );
            }
            value = parts.join( field.multiValueMode == ActionMultiValueMode::CommaSeparated
                                    ? QStringLiteral( "," )
                                    : field.separator );
        }
    }
    else if ( field.type == ActionParameterType::Integer ) {
        bool ok = false;
        const auto number = value.toLongLong( &ok );
        if ( !ok ) {
            return { false,
                     {},
                     QStringLiteral( "Parameter '%1' must be an integer." ).arg( field.name ) };
        }
        value = number;
    }
    else if ( field.type == ActionParameterType::Decimal ) {
        bool ok = false;
        const auto number = value.toDouble( &ok );
        if ( !ok || !std::isfinite( number ) ) {
            return {
                false,
                {},
                QStringLiteral( "Parameter '%1' must be a finite decimal." ).arg( field.name )
            };
        }
        value = number;
    }
    else if ( field.type == ActionParameterType::Boolean ) {
        if ( value.typeId() == QMetaType::QString ) {
            const auto normalized = value.toString().trimmed().toLower();
            if ( normalized == QLatin1String( "true" ) || normalized == QLatin1String( "1" )
                 || normalized == QLatin1String( "yes" ) || normalized == QLatin1String( "on" ) ) {
                value = true;
            }
            else if ( normalized == QLatin1String( "false" ) || normalized == QLatin1String( "0" )
                      || normalized == QLatin1String( "no" )
                      || normalized == QLatin1String( "off" ) ) {
                value = false;
            }
            else {
                return { false,
                         {},
                         QStringLiteral( "Parameter '%1' must be boolean." ).arg( field.name ) };
            }
        }
        else {
            value = value.toBool();
        }
    }
    else if ( field.type == ActionParameterType::Date ) {
        const auto date = value.canConvert<QDate>()
                              ? value.toDate()
                              : QDate::fromString( value.toString(), Qt::ISODate );
        if ( !date.isValid() ) {
            return { false,
                     {},
                     QStringLiteral( "Parameter '%1' must be an ISO date." ).arg( field.name ) };
        }
        value
            = field.format.isEmpty() ? date.toString( Qt::ISODate ) : date.toString( field.format );
    }
    else if ( field.type == ActionParameterType::Time ) {
        const auto time = value.canConvert<QTime>()
                              ? value.toTime()
                              : QTime::fromString( value.toString(), Qt::ISODate );
        if ( !time.isValid() ) {
            return { false,
                     {},
                     QStringLiteral( "Parameter '%1' must be an ISO time." ).arg( field.name ) };
        }
        value
            = field.format.isEmpty() ? time.toString( Qt::ISODate ) : time.toString( field.format );
    }
    else if ( field.type == ActionParameterType::DateTime ) {
        const auto dateTime = value.canConvert<QDateTime>()
                                  ? value.toDateTime()
                                  : QDateTime::fromString( value.toString(), Qt::ISODate );
        if ( !dateTime.isValid() ) {
            return {
                false,
                {},
                QStringLiteral( "Parameter '%1' must be an ISO date-time." ).arg( field.name )
            };
        }
        value = field.format.isEmpty() ? dateTime.toString( Qt::ISODate )
                                       : dateTime.toString( field.format );
    }

    const auto text = value.toString();
    if ( field.type == ActionParameterType::HexBytes
         && !QRegularExpression( QStringLiteral( "^(?:[0-9A-Fa-f]{2}[\\s,:;_-]*)*$" ) )
                 .match( text.trimmed() )
                 .hasMatch() ) {
        return {
            false,
            {},
            QStringLiteral( "Parameter '%1' must contain hexadecimal bytes." ).arg( field.name )
        };
    }
    if ( field.type == ActionParameterType::MacAddress
         && !QRegularExpression( QStringLiteral( "^(?:[0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}$" ) )
                 .match( text.trimmed() )
                 .hasMatch() ) {
        return { false,
                 {},
                 QStringLiteral( "Parameter '%1' must be a MAC address." ).arg( field.name ) };
    }
    if ( field.type == ActionParameterType::PhoneNumber
         && !QRegularExpression( QStringLiteral( "^\\+?[0-9 ()-]{3,32}$" ) )
                 .match( text.trimmed() )
                 .hasMatch() ) {
        return { false,
                 {},
                 QStringLiteral( "Parameter '%1' must be a phone number." ).arg( field.name ) };
    }
    if ( field.type == ActionParameterType::IpAddress
         && !QRegularExpression( QStringLiteral( "^[0-9A-Fa-f:.]+$" ) )
                 .match( text.trimmed() )
                 .hasMatch() ) {
        return { false,
                 {},
                 QStringLiteral( "Parameter '%1' must be an IP address." ).arg( field.name ) };
    }
    if ( !field.validationPattern.isEmpty() ) {
        const QRegularExpression validation( field.validationPattern );
        if ( !validation.isValid() ) {
            return { false,
                     {},
                     QStringLiteral( "Parameter '%1' has an invalid validation pattern: %2" )
                         .arg( field.name, validation.errorString() ) };
        }
        if ( !validation.match( text ).hasMatch() ) {
            return { false,
                     {},
                     QStringLiteral( "Parameter '%1' does not match its validation pattern." )
                         .arg( field.name ) };
        }
    }
    if ( field.type == ActionParameterType::Choice && !field.choices.isEmpty() ) {
        const auto found = std::any_of(
            field.choices.cbegin(), field.choices.cend(),
            [ &value ]( const ActionParameterChoice& choice ) { return choice.value == value; } );
        if ( !found ) {
            return { false,
                     {},
                     QStringLiteral( "Parameter '%1' is not one of the configured choices." )
                         .arg( field.name ) };
        }
    }
    if ( field.minimum.isValid() || field.maximum.isValid() ) {
        bool valueOk = false;
        const auto number = value.toDouble( &valueOk );
        if ( !valueOk ) {
            return { false,
                     {},
                     QStringLiteral( "Parameter '%1' has numeric bounds but is not numeric." )
                         .arg( field.name ) };
        }
        if ( field.minimum.isValid() && number < field.minimum.toDouble() ) {
            return { false,
                     {},
                     QStringLiteral( "Parameter '%1' is below its minimum." ).arg( field.name ) };
        }
        if ( field.maximum.isValid() && number > field.maximum.toDouble() ) {
            return { false,
                     {},
                     QStringLiteral( "Parameter '%1' is above its maximum." ).arg( field.name ) };
        }
    }
    if ( !field.expression.trimmed().isEmpty() ) {
        auto variables = currentValues;
        variables.insert( QStringLiteral( "value" ), value );
        const auto transformed = evaluateActionExpression( field.expression, variables );
        if ( !transformed.ok ) {
            return { false,
                     {},
                     QStringLiteral( "Parameter '%1' expression failed: %2" )
                         .arg( field.name, transformed.error ) };
        }
        value = transformed.value;
    }
    return { true, value, {} };
}

QString variantForLegacyTemplate( const QVariant& value )
{
    if ( value.typeId() == QMetaType::QByteArray ) {
        return QString::fromLatin1( value.toByteArray().toHex().toUpper() );
    }
    if ( value.typeId() == QMetaType::Bool ) {
        return value.toBool() ? QStringLiteral( "1" ) : QStringLiteral( "0" );
    }
    return value.toString();
}
} // namespace

ActionSequenceResult actionSequenceToBytes( const ActionSequence& sequence,
                                            const QMap<QString, QString>& substitutions,
                                            QStringList* missing )
{
    ActionSequenceResult result;
    const QString resolved
        = substitutions.isEmpty()
              ? sequence.value
              : resolveTemplateString( sequence.value, substitutions, missing );

    if ( sequence.type == ActionSequenceType::String ) {
        result.ok = true;
        result.bytes = resolved.toLatin1();
        return result;
    }

    const auto decoded = decodeHexStringToBytes( resolved );
    if ( !decoded.ok ) {
        result.error = decoded.error;
        return result;
    }
    result.ok = true;
    result.bytes = decoded.bytes;
    return result;
}

ActionSequenceResult actionDefinitionToBytes( const ActionDefinition& action,
                                              const QMap<QString, QString>& substitutions,
                                              QStringList* missing )
{
    ActionSequenceResult result;
    const QString resolved
        = substitutions.isEmpty()
              ? action.sequence.value
              : resolveTemplateString( action.sequence.value, substitutions, missing );

    QString checksumError;
    if ( action.sequence.type == ActionSequenceType::String ) {
        auto bytes = resolved.toLatin1();
        if ( action.checksum.enabled ) {
            const auto checksum = applyChecksum( action, bytes, &checksumError );
            if ( checksum.isEmpty() && !checksumError.isEmpty() ) {
                result.error = checksumError;
                return result;
            }

            if ( action.checksum.placeholder.isEmpty() ) {
                bytes.append( checksum );
            }
            else {
                auto encoded = resolved;
                encoded.replace( action.checksum.placeholder, QString::fromLatin1( checksum ) );
                bytes = encoded.toLatin1();
            }
        }
        result.ok = true;
        result.bytes = bytes;
        return result;
    }

    auto resolvedHex = resolved;
    if ( action.checksum.enabled ) {
        auto payloadSource = resolvedHex;
        if ( !action.checksum.placeholder.isEmpty()
             && payloadSource.contains( action.checksum.placeholder ) ) {
            payloadSource.replace( action.checksum.placeholder, QString{} );
        }
        const auto payload = decodeHexStringToBytes( payloadSource );
        if ( !payload.ok ) {
            result.error = payload.error;
            return result;
        }

        const auto checksum = applyChecksum( action, payload.bytes, &checksumError );
        if ( checksum.isEmpty() && !checksumError.isEmpty() ) {
            result.error = checksumError;
            return result;
        }

        if ( action.checksum.placeholder.isEmpty() ) {
            if ( !resolvedHex.trimmed().isEmpty() ) {
                resolvedHex.append( QLatin1Char( ' ' ) );
            }
            resolvedHex.append( QString::fromLatin1( checksum ) );
        }
        else {
            resolvedHex.replace( action.checksum.placeholder, QString::fromLatin1( checksum ) );
        }
    }

    const auto decoded = decodeHexStringToBytes( resolvedHex );
    if ( !decoded.ok ) {
        result.error = decoded.error;
        return result;
    }

    result.ok = true;
    result.bytes = decoded.bytes;
    return result;
}

ActionSequenceResult actionDefinitionToBytesWithParameters( const ActionDefinition& action,
                                                            const QVariantMap& values,
                                                            QStringList* missing )
{
    LOG_DEBUG << "Encoding typed action id=" << action.id
              << " field_count=" << action.parameters.fields.size()
              << " supplied_count=" << values.size();
    if ( action.parameters.fields.isEmpty() && action.expression.trimmed().isEmpty() ) {
        QMap<QString, QString> substitutions;
        for ( auto iterator = values.constBegin(); iterator != values.constEnd(); ++iterator ) {
            substitutions.insert( iterator.key(), variantForLegacyTemplate( iterator.value() ) );
        }
        LOG_DEBUG << "Typed action id=" << action.id
                  << " has no typed fields or expression; using legacy template encoder";
        return actionDefinitionToBytes( action, substitutions, missing );
    }

    QVariantMap normalizedValues;
    QSet<QString> knownNames;
    for ( const auto& field : action.parameters.fields ) {
        const auto name = field.name.trimmed();
        if ( name.isEmpty() ) {
            LOG_WARNING << "Typed action id=" << action.id << " contains an unnamed field";
            return { false, {}, QStringLiteral( "Action contains an unnamed parameter." ) };
        }
        knownNames.insert( name );
        const auto hasInput = values.contains( name );
        const auto hasDefault = field.defaultValue.isValid() && !field.defaultValue.isNull();
        if ( !hasInput && !hasDefault ) {
            if ( field.required ) {
                if ( missing != nullptr ) {
                    missing->push_back( name );
                }
                LOG_WARNING << "Typed action id=" << action.id << " is missing required field "
                            << name.toStdString();
                return { false,
                         {},
                         QStringLiteral( "Missing required parameter '%1'." ).arg( name ) };
            }
            LOG_DEBUG << "Typed action id=" << action.id << " uses null for optional field "
                      << name.toStdString();
            normalizedValues.insert( name, {} );
            continue;
        }

        LOG_DEBUG << "Typed action id=" << action.id
                  << ( hasInput ? " normalizing supplied field " : " applying default for field " )
                  << name.toStdString() << ( field.sensitive ? " [sensitive]" : "" );
        const auto normalized = normalizeParameterValue(
            field, hasInput ? values.value( name ) : field.defaultValue, normalizedValues );
        if ( !normalized.ok ) {
            LOG_WARNING << "Typed action id=" << action.id << " rejected field "
                        << name.toStdString() << ": " << normalized.error.toStdString();
            return { false, {}, normalized.error };
        }
        normalizedValues.insert( name, normalized.value );
    }

    for ( auto iterator = values.constBegin(); iterator != values.constEnd(); ++iterator ) {
        if ( !knownNames.contains( iterator.key() )
             && !action.parameters.variableNames.contains( iterator.key() ) ) {
            LOG_WARNING << "Typed action id=" << action.id << " rejected unknown field "
                        << iterator.key().toStdString();
            return { false, {}, QStringLiteral( "Unknown parameter '%1'." ).arg( iterator.key() ) };
        }
        if ( !normalizedValues.contains( iterator.key() ) ) {
            normalizedValues.insert( iterator.key(), iterator.value() );
        }
    }

    if ( action.expression.trimmed().isEmpty() ) {
        QMap<QString, QString> substitutions;
        for ( auto iterator = normalizedValues.constBegin();
              iterator != normalizedValues.constEnd(); ++iterator ) {
            substitutions.insert( iterator.key(), variantForLegacyTemplate( iterator.value() ) );
        }
        LOG_DEBUG << "Typed action id=" << action.id
                  << " has no expression; using legacy template encoder";
        return actionDefinitionToBytes( action, substitutions, missing );
    }

    const auto evaluated = evaluateActionExpression( action.expression, normalizedValues );
    if ( !evaluated.ok ) {
        LOG_WARNING << "Typed action id=" << action.id
                    << " expression failed: " << evaluated.error.toStdString();
        return { false, {}, evaluated.error };
    }

    if ( evaluated.value.typeId() != QMetaType::QByteArray ) {
        ActionDefinition evaluatedAction = action;
        evaluatedAction.expression.clear();
        evaluatedAction.sequence.value = evaluated.value.toString();
        LOG_DEBUG << "Typed action id=" << action.id
                  << " expression returned text; using configured sequence encoder";
        return actionDefinitionToBytes( evaluatedAction, {}, missing );
    }

    auto converted = actionExpressionValueToBytes( evaluated.value );
    if ( !converted.ok ) {
        return { false, {}, converted.error };
    }
    auto bytes = converted.value.toByteArray();
    if ( action.checksum.enabled ) {
        QString checksumError;
        const auto checksum = applyChecksum( action, bytes, &checksumError );
        if ( !checksumError.isEmpty() ) {
            LOG_WARNING << "Typed action id=" << action.id
                        << " checksum failed: " << checksumError.toStdString();
            return { false, {}, checksumError };
        }
        if ( !action.checksum.placeholder.isEmpty() ) {
            return { false,
                     {},
                     QStringLiteral( "Checksum placeholders require a text expression result." ) };
        }
        bytes.append( checksum );
    }
    LOG_DEBUG << "Typed action id=" << action.id << " encoded " << bytes.size() << " byte(s)";
    return { true, bytes, {} };
}

bool validateActionDefinition( const ActionDefinition& action, QString* errorMessage )
{
    if ( action.name.trimmed().isEmpty() ) {
        setError( errorMessage, QStringLiteral( "Action name is required." ) );
        return false;
    }
    if ( action.sequence.value.trimmed().isEmpty() && action.expression.trimmed().isEmpty() ) {
        setError( errorMessage, QStringLiteral( "Action sequence or expression is required." ) );
        return false;
    }
    if ( action.parameters.delay < 0 || action.parameters.repeatCount < 1
         || action.parameters.repeatInterval < 0 ) {
        setError( errorMessage, QStringLiteral( "Action timing values must be non-negative." ) );
        return false;
    }
    QSet<QString> fieldNames;
    QVariantMap validationValues;
    for ( const auto& field : action.parameters.fields ) {
        const auto name = field.name.trimmed();
        if ( !QRegularExpression( QStringLiteral( "^[A-Za-z_][A-Za-z0-9_]*$" ) )
                  .match( name )
                  .hasMatch() ) {
            setError( errorMessage,
                      QStringLiteral( "Invalid parameter name '%1'." ).arg( field.name ) );
            return false;
        }
        if ( fieldNames.contains( name ) ) {
            setError( errorMessage,
                      QStringLiteral( "Duplicate parameter name '%1'." ).arg( name ) );
            return false;
        }
        fieldNames.insert( name );
        if ( ( field.type == ActionParameterType::Choice
               || field.type == ActionParameterType::MultiChoice )
             && field.choices.isEmpty() ) {
            setError(
                errorMessage,
                QStringLiteral( "Parameter '%1' requires at least one choice." ).arg( name ) );
            return false;
        }
        if ( field.type != ActionParameterType::MultiChoice
             && field.presentation == ActionParameterPresentation::CheckBoxes ) {
            setError(
                errorMessage,
                QStringLiteral( "Checkbox presentation requires a multi-choice parameter." ) );
            return false;
        }
        if ( !field.validationPattern.isEmpty() ) {
            const QRegularExpression pattern( field.validationPattern );
            if ( !pattern.isValid() ) {
                setError( errorMessage, QStringLiteral( "Invalid validation pattern for '%1': %2" )
                                            .arg( name, pattern.errorString() ) );
                return false;
            }
        }
        QVariant sample = field.defaultValue;
        // An empty multi-choice default is a valid stored value, but it is not
        // a useful type sample for expressions such as hex(value, 2).  Use the
        // same representative reduced value as runtime validation in that
        // case instead of passing QVariantList{} to the expression evaluator.
        const bool needsTypeSample
            = !sample.isValid()
              || ( field.type == ActionParameterType::MultiChoice && sample.toList().isEmpty() );
        if ( needsTypeSample ) {
            switch ( field.type ) {
            case ActionParameterType::Integer:
                sample = 0;
                break;
            case ActionParameterType::Decimal:
                sample = 0.0;
                break;
            case ActionParameterType::Boolean:
                sample = false;
                break;
            case ActionParameterType::Choice:
                sample = field.choices.isEmpty() ? QVariant( 0 ) : field.choices.front().value;
                break;
            case ActionParameterType::MultiChoice:
                // Expressions are evaluated after multi-choice values have
                // been reduced by their configured mode.  Use a representative
                // bitmask/string here as well so validation exercises the same
                // type as the runtime encoder (e.g. hex(value, 2)).
                if ( field.multiValueMode == ActionMultiValueMode::BitwiseOr ) {
                    qlonglong combined = 0;
                    for ( const auto& choice : field.choices ) {
                        bool ok = false;
                        const auto number = choice.value.toLongLong( &ok );
                        if ( ok ) {
                            combined |= number;
                        }
                    }
                    sample = combined;
                }
                else if ( !field.choices.isEmpty() ) {
                    QStringList values;
                    for ( const auto& choice : field.choices ) {
                        values.push_back( choice.value.toString() );
                    }
                    sample = values.join( field.multiValueMode == ActionMultiValueMode::CommaSeparated
                                              ? QStringLiteral( "," )
                                              : field.separator );
                }
                else {
                    sample = QString();
                }
                break;
            case ActionParameterType::Date:
                sample = QStringLiteral( "1970-01-01" );
                break;
            case ActionParameterType::Time:
                sample = QStringLiteral( "00:00:00" );
                break;
            case ActionParameterType::DateTime:
                sample = QStringLiteral( "1970-01-01T00:00:00Z" );
                break;
            case ActionParameterType::IpAddress:
                sample = QStringLiteral( "127.0.0.1" );
                break;
            case ActionParameterType::MacAddress:
                sample = QStringLiteral( "00:00:00:00:00:00" );
                break;
            case ActionParameterType::PhoneNumber:
                sample = QStringLiteral( "+1000" );
                break;
            case ActionParameterType::HexBytes:
                sample = QStringLiteral( "00" );
                break;
            case ActionParameterType::Text:
            default:
                sample = QStringLiteral( "value" );
                break;
            }
        }
        if ( !field.expression.trimmed().isEmpty() ) {
            auto fieldVariables = validationValues;
            fieldVariables.insert( QStringLiteral( "value" ), sample );
            const auto fieldResult = evaluateActionExpression( field.expression, fieldVariables );
            if ( !fieldResult.ok ) {
                setError( errorMessage,
                          QStringLiteral( "Invalid expression for parameter '%1': %2" )
                              .arg( name, fieldResult.error ) );
                return false;
            }
            sample = fieldResult.value;
        }
        validationValues.insert( name, sample );
    }
    if ( action.checksum.enabled ) {
        if ( !isValidActionChecksumAlgorithm( action.checksum.algorithm ) ) {
            setError( errorMessage, QStringLiteral( "Unsupported checksum algorithm." ) );
            return false;
        }
        if ( action.checksum.placeholder.trimmed().isEmpty() ) {
            setError( errorMessage, QStringLiteral( "Checksum placeholder cannot be empty." ) );
            return false;
        }
    }

    QStringList missing;
    const auto result
        = action.expression.trimmed().isEmpty()
              ? actionDefinitionToBytes( action, {}, &missing )
              : [ &action, &validationValues ] {
                    const auto evaluated
                        = evaluateActionExpression( action.expression, validationValues );
                    if ( !evaluated.ok ) {
                        return ActionSequenceResult{ false, {}, evaluated.error };
                    }
                    if ( evaluated.value.typeId() == QMetaType::QByteArray ) {
                        const auto bytes = actionExpressionValueToBytes( evaluated.value );
                        return bytes.ok
                                   ? ActionSequenceResult{ true, bytes.value.toByteArray(), {} }
                                   : ActionSequenceResult{ false, {}, bytes.error };
                    }
                    ActionSequence sequence = action.sequence;
                    sequence.value = evaluated.value.toString();
                    return actionSequenceToBytes( sequence );
                }();
    if ( !result.ok ) {
        setError( errorMessage,
                  result.error.isEmpty() ? QStringLiteral( "Invalid action sequence." )
                                         : result.error );
        return false;
    }

    return true;
}

bool validateResponseDefinition( const ResponseDefinition& response, QString* errorMessage )
{
    if ( response.name.trimmed().isEmpty() ) {
        setError( errorMessage, QStringLiteral( "Response name is required." ) );
        return false;
    }
    if ( response.match.value.trimmed().isEmpty() ) {
        setError( errorMessage, QStringLiteral( "Response match value is required." ) );
        return false;
    }
    if ( response.match.type == ResponseMatchType::Regex ) {
        const auto regex = response.match.compiled.isValid()
                               ? response.match.compiled
                               : QRegularExpression( response.match.value );
        if ( !regex.isValid() ) {
            setError( errorMessage, regex.errorString() );
            return false;
        }
    }

    for ( const auto& step : response.response.steps ) {
        if ( step.actionId < 0 ) {
            setError( errorMessage, QStringLiteral( "Response linked action id is invalid." ) );
            return false;
        }
        if ( step.delayMs < 0 ) {
            setError( errorMessage,
                      QStringLiteral( "Response linked action delay must be non-negative." ) );
            return false;
        }
    }

    if ( response.response.hasInlineAction ) {
        ActionDefinition inlineAction;
        inlineAction.name = response.name;
        inlineAction.sequence = response.response.inlineAction;
        if ( !validateActionDefinition( inlineAction, errorMessage ) ) {
            return false;
        }
    }

    const auto hasLinkedSteps = !response.response.steps.isEmpty()
                                || ( response.response.hasActionId
                                     && response.response.actionId >= 0 );
    const auto hasSideEffects = !response.response.comment.isEmpty()
                                || response.response.linebreak
                                || response.response.timestamp
                                || response.response.snapshot
                                || response.response.stopCommunication;
    if ( !hasLinkedSteps && !response.response.hasInlineAction && !hasSideEffects ) {
        setError( errorMessage,
                  QStringLiteral( "Response must perform at least one action or side effect." ) );
        return false;
    }

    return true;
}

void normalizeActionDefinitions( QVector<ActionDefinition>* actions )
{
    normalizeActionOrderValues( actions );
}

void normalizeResponseDefinitions( QVector<ResponseDefinition>* responses )
{
    if ( responses != nullptr ) {
        for ( auto& response : *responses ) {
            normalizeResponseAction( &response.response );
        }
    }
    normalizeResponseOrderValues( responses );
}

QVariantMap actionDefinitionToVariantMap( const ActionDefinition& action )
{
    QVariantMap parameters;
    parameters.insert( QStringLiteral( "repeat" ), action.parameters.repeat );
    parameters.insert( QStringLiteral( "delay" ), action.parameters.delay );
    parameters.insert( QStringLiteral( "repeat_count" ), action.parameters.repeatCount );
    parameters.insert( QStringLiteral( "repeat_interval" ), action.parameters.repeatInterval );
    parameters.insert( QStringLiteral( "variable_names" ), action.parameters.variableNames );
    QVariantList fields;
    for ( const auto& field : action.parameters.fields ) {
        QVariantMap fieldMap;
        fieldMap.insert( QStringLiteral( "name" ), field.name );
        fieldMap.insert( QStringLiteral( "label" ), field.label );
        fieldMap.insert( QStringLiteral( "description" ), field.description );
        fieldMap.insert( QStringLiteral( "type" ), actionParameterTypeToString( field.type ) );
        fieldMap.insert( QStringLiteral( "presentation" ),
                         actionParameterPresentationToString( field.presentation ) );
        fieldMap.insert( QStringLiteral( "multi_value_mode" ),
                         actionMultiValueModeToString( field.multiValueMode ) );
        fieldMap.insert( QStringLiteral( "required" ), field.required );
        fieldMap.insert( QStringLiteral( "sensitive" ), field.sensitive );
        fieldMap.insert( QStringLiteral( "remember" ), field.remember );
        if ( field.defaultValue.isValid() ) {
            fieldMap.insert( QStringLiteral( "default" ), field.defaultValue );
        }
        if ( field.minimum.isValid() ) {
            fieldMap.insert( QStringLiteral( "minimum" ), field.minimum );
        }
        if ( field.maximum.isValid() ) {
            fieldMap.insert( QStringLiteral( "maximum" ), field.maximum );
        }
        if ( field.step.isValid() ) {
            fieldMap.insert( QStringLiteral( "step" ), field.step );
        }
        fieldMap.insert( QStringLiteral( "validation_pattern" ), field.validationPattern );
        fieldMap.insert( QStringLiteral( "format" ), field.format );
        fieldMap.insert( QStringLiteral( "separator" ), field.separator );
        fieldMap.insert( QStringLiteral( "expression" ), field.expression );
        QVariantList choices;
        for ( const auto& choice : field.choices ) {
            QVariantMap choiceMap;
            choiceMap.insert( QStringLiteral( "label" ), choice.label );
            choiceMap.insert( QStringLiteral( "value" ), choice.value );
            choices.push_back( choiceMap );
        }
        fieldMap.insert( QStringLiteral( "choices" ), choices );
        fields.push_back( fieldMap );
    }
    parameters.insert( QStringLiteral( "fields" ), fields );

    QVariantMap checksum;
    checksum.insert( QStringLiteral( "enabled" ), action.checksum.enabled );
    checksum.insert( QStringLiteral( "algorithm" ), action.checksum.algorithm );
    checksum.insert( QStringLiteral( "placeholder" ), action.checksum.placeholder );

    QVariantMap map;
    map.insert( QStringLiteral( "id" ), action.id );
    map.insert( QStringLiteral( "order" ), action.order );
    map.insert( QStringLiteral( "enabled" ), action.enabled );
    map.insert( QStringLiteral( "hidden" ), action.hidden );
    map.insert( QStringLiteral( "name" ), action.name );
    map.insert( QStringLiteral( "description" ), action.description );
    if ( !action.sequence.value.isEmpty() ) {
        QVariantMap sequence;
        sequence.insert( QStringLiteral( "type" ), actionSequenceTypeToString( action.sequence.type ) );
        sequence.insert( QStringLiteral( "value" ), action.sequence.value );
        map.insert( QStringLiteral( "sequence" ), sequence );
    }
    if ( !action.expression.trimmed().isEmpty() ) {
        map.insert( QStringLiteral( "expression" ), action.expression );
    }
    map.insert( QStringLiteral( "parameters" ), parameters );
    map.insert( QStringLiteral( "checksum" ), checksum );
    return map;
}

QVariantMap responseDefinitionToVariantMap( const ResponseDefinition& response )
{
    QVariantMap match;
    match.insert( QStringLiteral( "type" ), responseMatchTypeToString( response.match.type ) );
    match.insert( QStringLiteral( "value" ), response.match.value );

    QVariantMap responseAction;
    if ( response.response.hasActionId && !response.response.hasInlineAction ) {
        responseAction.insert( QStringLiteral( "action_id" ), response.response.actionId );
    }
    QVariantList steps;
    for ( const auto& step : response.response.steps ) {
        QVariantMap stepMap;
        stepMap.insert( QStringLiteral( "action_id" ), step.actionId );
        stepMap.insert( QStringLiteral( "delay_ms" ), step.delayMs );
        if ( !step.parameters.isEmpty() ) {
            stepMap.insert( QStringLiteral( "parameters" ), step.parameters );
        }
        steps.push_back( stepMap );
    }
    responseAction.insert( QStringLiteral( "steps" ), steps );
    responseAction.insert( QStringLiteral( "comment" ), response.response.comment );
    responseAction.insert( QStringLiteral( "linebreak" ), response.response.linebreak );
    responseAction.insert( QStringLiteral( "timestamp" ), response.response.timestamp );
    responseAction.insert( QStringLiteral( "snapshot" ), response.response.snapshot );
    responseAction.insert( QStringLiteral( "stop_communication" ),
                           response.response.stopCommunication );
    if ( response.response.hasInlineAction ) {
        QVariantMap inlineAction;
        inlineAction.insert( QStringLiteral( "type" ),
                             actionSequenceTypeToString( response.response.inlineAction.type ) );
        inlineAction.insert( QStringLiteral( "value" ), response.response.inlineAction.value );
        responseAction.insert( QStringLiteral( "action" ), inlineAction );
    }

    QVariantMap map;
    map.insert( QStringLiteral( "id" ), response.id );
    map.insert( QStringLiteral( "order" ), response.order );
    map.insert( QStringLiteral( "enabled" ), response.enabled );
    map.insert( QStringLiteral( "hidden" ), response.hidden );
    map.insert( QStringLiteral( "name" ), response.name );
    map.insert( QStringLiteral( "description" ), response.description );
    map.insert( QStringLiteral( "match" ), match );
    map.insert( QStringLiteral( "response" ), responseAction );
    return map;
}

ActionDefinition actionDefinitionFromVariantMap( const QVariantMap& map, QString* errorMessage )
{
    ActionDefinition action;
    action.id = map.value( QStringLiteral( "id" ), -1 ).toInt();
    action.order = map.value( QStringLiteral( "order" ), 0 ).toInt();
    action.enabled = map.value( QStringLiteral( "enabled" ), true ).toBool();
    action.hidden = map.value( QStringLiteral( "hidden" ), false ).toBool();
    action.name = map.value( QStringLiteral( "name" ) ).toString();
    action.description = map.value( QStringLiteral( "description" ) ).toString();
    action.expression = map.value( QStringLiteral( "expression" ) ).toString();

    const auto sequenceMap = map.value( QStringLiteral( "sequence" ) ).toMap();
    if ( !sequenceMap.isEmpty() ) {
        bool sequenceTypeOk = false;
        action.sequence.type = actionSequenceTypeFromString(
            sequenceMap.value( QStringLiteral( "type" ) ).toString(), &sequenceTypeOk );
        action.sequence.value = sequenceMap.value( QStringLiteral( "value" ) ).toString();
        if ( !sequenceTypeOk ) {
            setError( errorMessage, QStringLiteral( "Invalid action sequence type." ) );
        }
    }

    const auto parametersMap = map.value( QStringLiteral( "parameters" ) ).toMap();
    action.parameters.repeat = parametersMap.value( QStringLiteral( "repeat" ), false ).toBool();
    action.parameters.delay = parametersMap.value( QStringLiteral( "delay" ), 0 ).toInt();
    action.parameters.repeatCount
        = parametersMap.value( QStringLiteral( "repeat_count" ), 1 ).toInt();
    action.parameters.repeatInterval
        = parametersMap.value( QStringLiteral( "repeat_interval" ), 0 ).toInt();
    action.parameters.variableNames
        = readStringListValue( parametersMap.value( QStringLiteral( "variable_names" ) ) );
    const auto fieldList = parametersMap.value( QStringLiteral( "fields" ) ).toList();
    for ( const auto& fieldValue : fieldList ) {
        const auto fieldMap = fieldValue.toMap();
        ActionParameterDefinition field;
        field.name = fieldMap.value( QStringLiteral( "name" ) ).toString();
        field.label = fieldMap.value( QStringLiteral( "label" ) ).toString();
        field.description = fieldMap.value( QStringLiteral( "description" ) ).toString();
        bool typeOk = false;
        field.type = actionParameterTypeFromString(
            fieldMap.value( QStringLiteral( "type" ), QStringLiteral( "text" ) ).toString(),
            &typeOk );
        bool presentationOk = false;
        field.presentation = actionParameterPresentationFromString(
            fieldMap.value( QStringLiteral( "presentation" ), QStringLiteral( "auto" ) ).toString(),
            &presentationOk );
        bool multiValueModeOk = false;
        field.multiValueMode = actionMultiValueModeFromString(
            fieldMap
                .value( QStringLiteral( "multi_value_mode" ), QStringLiteral( "comma_separated" ) )
                .toString(),
            &multiValueModeOk );
        if ( !typeOk || !presentationOk || !multiValueModeOk ) {
            setError( errorMessage,
                      QStringLiteral( "Invalid typed parameter configuration for '%1'." )
                          .arg( field.name ) );
        }
        field.required = fieldMap.value( QStringLiteral( "required" ), false ).toBool();
        field.sensitive = fieldMap.value( QStringLiteral( "sensitive" ), false ).toBool();
        field.remember = fieldMap.value( QStringLiteral( "remember" ), true ).toBool();
        if ( fieldMap.contains( QStringLiteral( "default" ) ) ) {
            field.defaultValue = fieldMap.value( QStringLiteral( "default" ) );
        }
        if ( fieldMap.contains( QStringLiteral( "minimum" ) ) ) {
            field.minimum = fieldMap.value( QStringLiteral( "minimum" ) );
        }
        if ( fieldMap.contains( QStringLiteral( "maximum" ) ) ) {
            field.maximum = fieldMap.value( QStringLiteral( "maximum" ) );
        }
        if ( fieldMap.contains( QStringLiteral( "step" ) ) ) {
            field.step = fieldMap.value( QStringLiteral( "step" ) );
        }
        field.validationPattern
            = fieldMap.value( QStringLiteral( "validation_pattern" ) ).toString();
        field.format = fieldMap.value( QStringLiteral( "format" ) ).toString();
        field.separator
            = fieldMap.value( QStringLiteral( "separator" ), QStringLiteral( "," ) ).toString();
        field.expression = fieldMap.value( QStringLiteral( "expression" ) ).toString();
        const auto choiceList = fieldMap.value( QStringLiteral( "choices" ) ).toList();
        for ( const auto& choiceValue : choiceList ) {
            const auto choiceMap = choiceValue.toMap();
            field.choices.push_back( { choiceMap.value( QStringLiteral( "label" ) ).toString(),
                                       choiceMap.value( QStringLiteral( "value" ) ) } );
        }
        action.parameters.fields.push_back( field );
    }

    const auto checksumMap = map.value( QStringLiteral( "checksum" ) ).toMap();
    action.checksum.enabled = checksumMap.value( QStringLiteral( "enabled" ), false ).toBool();
    action.checksum.algorithm
        = checksumMap.value( QStringLiteral( "algorithm" ), QStringLiteral( "sum8" ) ).toString();
    action.checksum.placeholder = checksumMap
                                      .value( QStringLiteral( "placeholder" ),
                                              QStringLiteral( "${CHECKSUM}" ) )
                                      .toString();

    QString validationError;
    if ( !validateActionDefinition( action, &validationError ) ) {
        setError( errorMessage, validationError );
    }

    return action;
}

ResponseDefinition responseDefinitionFromVariantMap( const QVariantMap& map,
                                                     QString* errorMessage )
{
    ResponseDefinition response;
    response.id = map.value( QStringLiteral( "id" ), -1 ).toInt();
    response.order = map.value( QStringLiteral( "order" ), 0 ).toInt();
    response.enabled = map.value( QStringLiteral( "enabled" ), true ).toBool();
    response.hidden = map.value( QStringLiteral( "hidden" ), false ).toBool();
    response.name = map.value( QStringLiteral( "name" ) ).toString();
    response.description = map.value( QStringLiteral( "description" ) ).toString();

    const auto matchMap = map.value( QStringLiteral( "match" ) ).toMap();
    bool matchTypeOk = false;
    response.match.type = responseMatchTypeFromString(
        matchMap.value( QStringLiteral( "type" ) ).toString(), &matchTypeOk );
    response.match.value = matchMap.value( QStringLiteral( "value" ) ).toString();
    if ( response.match.type == ResponseMatchType::Regex ) {
        response.match.compiled = QRegularExpression( response.match.value );
    }
    else if ( response.match.type == ResponseMatchType::Wildcard ) {
        response.match.compiled = QRegularExpression(
            QRegularExpression::wildcardToRegularExpression( response.match.value ),
            QRegularExpression::CaseInsensitiveOption );
    }
    if ( !matchTypeOk ) {
        setError( errorMessage, QStringLiteral( "Invalid response match type." ) );
    }

    const auto responseAction = map.value( QStringLiteral( "response" ) ).toMap();
    response.response.hasActionId
        = responseAction.value( QStringLiteral( "has_action_id" ),
                                responseAction.contains( QStringLiteral( "action_id" ) ) )
              .toBool();
    response.response.actionId = responseAction.value( QStringLiteral( "action_id" ), -1 ).toInt();
    const auto stepsList = responseAction.value( QStringLiteral( "steps" ) ).toList();
    for ( const auto& stepValue : stepsList ) {
        const auto stepMap = stepValue.toMap();
        ResponseActionStep step;
        step.actionId = stepMap.value( QStringLiteral( "action_id" ), -1 ).toInt();
        step.delayMs = stepMap.value( QStringLiteral( "delay_ms" ), 0 ).toInt();
        step.parameters = stepMap.value( QStringLiteral( "parameters" ) ).toMap();
        response.response.steps.push_back( step );
    }
    response.response.hasInlineAction
        = responseAction.value( QStringLiteral( "has_inline_action" ),
                                responseAction.contains( QStringLiteral( "action" ) ) )
              .toBool();
    response.response.comment = responseAction.value( QStringLiteral( "comment" ) ).toString();
    response.response.linebreak = responseAction.value( QStringLiteral( "linebreak" ), false ).toBool();
    response.response.timestamp = responseAction.value( QStringLiteral( "timestamp" ), false ).toBool();
    response.response.snapshot = responseAction.value( QStringLiteral( "snapshot" ), false ).toBool();
    response.response.stopCommunication
        = responseAction.value( QStringLiteral( "stop_communication" ), false ).toBool();

    const auto inlineActionMap = responseAction.value( QStringLiteral( "action" ) ).toMap();
    if ( !inlineActionMap.isEmpty() ) {
        bool inlineTypeOk = false;
        response.response.inlineAction.type = actionSequenceTypeFromString(
            inlineActionMap.value( QStringLiteral( "type" ) ).toString(), &inlineTypeOk );
        response.response.inlineAction.value
            = inlineActionMap.value( QStringLiteral( "value" ) ).toString();
        if ( !inlineTypeOk ) {
            setError( errorMessage, QStringLiteral( "Invalid response inline action type." ) );
        }
    }

    normalizeResponseAction( &response.response );

    QString validationError;
    if ( !validateResponseDefinition( response, &validationError ) ) {
        setError( errorMessage, validationError );
    }

    return response;
}
