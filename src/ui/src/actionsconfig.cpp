#include "actionsconfig.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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

bool validateActionDefinition( const ActionDefinition& action, QString* errorMessage )
{
    if ( action.name.trimmed().isEmpty() ) {
        setError( errorMessage, QStringLiteral( "Action name is required." ) );
        return false;
    }
    if ( action.sequence.value.trimmed().isEmpty() ) {
        setError( errorMessage, QStringLiteral( "Action sequence is required." ) );
        return false;
    }
    if ( action.parameters.delay < 0 || action.parameters.repeatCount < 1
         || action.parameters.repeatInterval < 0 ) {
        setError( errorMessage, QStringLiteral( "Action timing values must be non-negative." ) );
        return false;
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
    const auto result = actionDefinitionToBytes( action, {}, &missing );
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
    QVariantMap sequence;
    sequence.insert( QStringLiteral( "type" ), actionSequenceTypeToString( action.sequence.type ) );
    sequence.insert( QStringLiteral( "value" ), action.sequence.value );

    QVariantMap parameters;
    parameters.insert( QStringLiteral( "repeat" ), action.parameters.repeat );
    parameters.insert( QStringLiteral( "delay" ), action.parameters.delay );
    parameters.insert( QStringLiteral( "repeat_count" ), action.parameters.repeatCount );
    parameters.insert( QStringLiteral( "repeat_interval" ), action.parameters.repeatInterval );
    parameters.insert( QStringLiteral( "variable_names" ), action.parameters.variableNames );

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
    map.insert( QStringLiteral( "sequence" ), sequence );
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
    responseAction.insert( QStringLiteral( "action_id" ), response.response.actionId );
    responseAction.insert( QStringLiteral( "has_action_id" ), response.response.hasActionId );
    QVariantList steps;
    for ( const auto& step : response.response.steps ) {
        QVariantMap stepMap;
        stepMap.insert( QStringLiteral( "action_id" ), step.actionId );
        stepMap.insert( QStringLiteral( "delay_ms" ), step.delayMs );
        steps.push_back( stepMap );
    }
    responseAction.insert( QStringLiteral( "steps" ), steps );
    responseAction.insert( QStringLiteral( "has_inline_action" ),
                           response.response.hasInlineAction );
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

    const auto sequenceMap = map.value( QStringLiteral( "sequence" ) ).toMap();
    bool sequenceTypeOk = false;
    action.sequence.type = actionSequenceTypeFromString(
        sequenceMap.value( QStringLiteral( "type" ) ).toString(), &sequenceTypeOk );
    action.sequence.value = sequenceMap.value( QStringLiteral( "value" ) ).toString();
    if ( !sequenceTypeOk ) {
        setError( errorMessage, QStringLiteral( "Invalid action sequence type." ) );
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
