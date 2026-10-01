#include "actionsconfigparser.h"

#include <cmath>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include "log.h"

namespace {
QByteArray stripLineComments( const QByteArray& input )
{
    QByteArray output;
    output.reserve( input.size() );
    bool inString = false;
    bool escape = false;
    for ( int i = 0; i < input.size(); ++i ) {
        const char c = input.at( i );
        if ( inString ) {
            output.append( c );
            if ( escape ) {
                escape = false;
                continue;
            }
            if ( c == '\\' ) {
                escape = true;
            }
            else if ( c == '"' ) {
                inString = false;
            }
            continue;
        }

        if ( c == '"' ) {
            inString = true;
            output.append( c );
            continue;
        }

        if ( c == '/' && i + 1 < input.size() && input.at( i + 1 ) == '/' ) {
            while ( i < input.size() && input.at( i ) != '\n' ) {
                ++i;
            }
            if ( i < input.size() && input.at( i ) == '\n' ) {
                output.append( '\n' );
            }
            continue;
        }

        output.append( c );
    }
    return output;
}

QStringList unknownKeys( const QJsonObject& object, const QSet<QString>& knownKeys )
{
    QStringList unknown;
    for ( const auto& key : object.keys() ) {
        if ( !knownKeys.contains( key ) ) {
            unknown.push_back( key );
        }
    }
    return unknown;
}

bool parseSequence( const QJsonObject& object,
                    ActionSequence* out,
                    QStringList* errors,
                    QStringList* warnings,
                    const QString& context )
{
    if ( !out ) {
        return false;
    }

    const QSet<QString> knownKeys = { "type", "value" };
    const auto extraKeys = unknownKeys( object, knownKeys );
    for ( const auto& key : extraKeys ) {
        if ( warnings ) {
            warnings->push_back( QString( "Unknown sequence property '%1' at %2." )
                                     .arg( key, context ) );
        }
    }

    const auto typeValue = object.value( "type" );
    bool typeOk = true;
    if ( typeValue.isString() ) {
        out->type = actionSequenceTypeFromString( typeValue.toString(), &typeOk );
    }
    else {
        typeOk = false;
    }
    if ( !typeOk && warnings ) {
        warnings->push_back( QString( "Unknown sequence type at %1." ).arg( context ) );
    }

    const auto valueValue = object.value( "value" );
    if ( !valueValue.isString() || valueValue.toString().trimmed().isEmpty() ) {
        if ( errors ) {
            errors->push_back( QString( "Missing sequence value at %1." ).arg( context ) );
        }
        return false;
    }
    out->value = valueValue.toString();
    return true;
}

bool parseAction( const QJsonObject& object,
                  ActionDefinition* out,
                  QStringList* errors,
                  QStringList* warnings,
                  int index )
{
    if ( !out ) {
        return false;
    }

    const QSet<QString> knownKeys
        = { "id",          "order",    "enabled",    "hidden",     "hiden",   "name",
            "description", "sequence", "expression", "parameters", "checksum" };
    const auto extraKeys = unknownKeys( object, knownKeys );
    for ( const auto& key : extraKeys ) {
        if ( warnings ) {
            warnings->push_back(
                QString( "Unknown action property '%1' at index %2." ).arg( key ).arg( index ) );
        }
    }

    const auto nameValue = object.value( "name" );
    if ( !nameValue.isString() || nameValue.toString().trimmed().isEmpty() ) {
        if ( errors ) {
            errors->push_back( QString( "Missing action name at index %1." ).arg( index ) );
        }
        return false;
    }
    out->name = nameValue.toString();

    if ( object.contains( "id" ) ) {
        const auto idValue = object.value( "id" );
        if ( idValue.isDouble() ) {
            out->id = idValue.toInt();
        }
        else if ( warnings ) {
            warnings->push_back(
                QString( "Invalid action id for '%1'." ).arg( out->name ) );
        }
    }
    if ( object.contains( "order" ) ) {
        out->order = object.value( "order" ).toInt( index );
    }

    if ( object.contains( "enabled" ) ) {
        out->enabled = object.value( "enabled" ).toBool( true );
    }
    if ( object.contains( "hidden" ) ) {
        const auto hiddenValue = object.value( "hidden" );
        if ( hiddenValue.isBool() ) {
            out->hidden = hiddenValue.toBool();
        }
        else if ( warnings ) {
            warnings->push_back(
                QString( "Invalid action hidden flag for '%1'." ).arg( out->name ) );
        }
    }

    if ( object.contains( "description" ) ) {
        out->description = object.value( "description" ).toString();
    }
    if ( object.contains( "expression" ) ) {
        out->expression = object.value( "expression" ).toString();
    }

    const auto sequenceValue = object.value( "sequence" );
    if ( !sequenceValue.isObject() ) {
        if ( out->expression.trimmed().isEmpty() && errors ) {
            errors->push_back( QString( "Missing sequence for action '%1'." ).arg( out->name ) );
        }
        if ( out->expression.trimmed().isEmpty() ) {
            return false;
        }
        LOG_DEBUG << "Expression-only action '" << out->name.toStdString()
                  << "' has no legacy sequence";
    }
    else if ( !parseSequence( sequenceValue.toObject(), &out->sequence, errors, warnings,
                              QString( "action '%1' sequence" ).arg( out->name ) ) ) {
        return false;
    }

    const auto parametersValue = object.value( "parameters" );
    if ( parametersValue.isObject() ) {
        const auto paramsObj = parametersValue.toObject();
        const QSet<QString> parameterKeys
            = { "repeat", "delay", "repeat_count", "repeat_interval", "variable_names", "fields" };
        for ( const auto& key : unknownKeys( paramsObj, parameterKeys ) ) {
            if ( warnings ) {
                warnings->push_back( QString( "Unknown parameter property '%1' for action '%2'." )
                                         .arg( key, out->name ) );
            }
        }
        if ( paramsObj.contains( "repeat" ) ) {
            out->parameters.repeat = paramsObj.value( "repeat" ).toBool( false );
        }
        if ( paramsObj.contains( "delay" ) ) {
            out->parameters.delay = paramsObj.value( "delay" ).toInt();
        }
        if ( paramsObj.contains( "repeat_count" ) ) {
            out->parameters.repeatCount = paramsObj.value( "repeat_count" ).toInt( 1 );
        }
        if ( paramsObj.contains( "repeat_interval" ) ) {
            out->parameters.repeatInterval = paramsObj.value( "repeat_interval" ).toInt();
        }
        if ( paramsObj.contains( "variable_names" ) && paramsObj.value( "variable_names" ).isArray() ) {
            const auto variableArray = paramsObj.value( "variable_names" ).toArray();
            for ( const auto& item : variableArray ) {
                const auto value = item.toString().trimmed();
                if ( !value.isEmpty() ) {
                    out->parameters.variableNames.push_back( value );
                }
            }
        }
        if ( paramsObj.contains( "fields" ) ) {
            const auto fieldsValue = paramsObj.value( "fields" );
            if ( !fieldsValue.isArray() ) {
                if ( errors ) {
                    errors->push_back( QString( "Action '%1' parameter fields must be an array." )
                                           .arg( out->name ) );
                }
                return false;
            }
            const auto fieldsArray = fieldsValue.toArray();
            for ( qsizetype fieldIndex = 0; fieldIndex < fieldsArray.size(); ++fieldIndex ) {
                if ( !fieldsArray.at( fieldIndex ).isObject() ) {
                    if ( errors ) {
                        errors->push_back(
                            QString( "Action '%1' parameter field %2 must be an object." )
                                .arg( out->name )
                                .arg( fieldIndex ) );
                    }
                    return false;
                }
                const auto fieldObject = fieldsArray.at( fieldIndex ).toObject();
                const QSet<QString> fieldKeys = { "name",         "label",
                                                  "description",  "type",
                                                  "presentation", "multi_value_mode",
                                                  "required",     "sensitive",
                                                  "remember",     "default",
                                                  "minimum",      "maximum",
                                                  "step",         "validation_pattern",
                                                  "format",       "separator",
                                                  "expression",   "choices" };
                for ( const auto& key : unknownKeys( fieldObject, fieldKeys ) ) {
                    if ( warnings ) {
                        warnings->push_back(
                            QString( "Unknown property '%1' for action '%2' parameter field %3." )
                                .arg( key, out->name )
                                .arg( fieldIndex ) );
                    }
                }
                ActionParameterDefinition field;
                field.name = fieldObject.value( "name" ).toString().trimmed();
                if ( field.name.isEmpty() ) {
                    if ( errors ) {
                        errors->push_back( QString( "Action '%1' parameter field %2 has no name." )
                                               .arg( out->name )
                                               .arg( fieldIndex ) );
                    }
                    return false;
                }
                field.label = fieldObject.value( "label" ).toString( field.name );
                field.description = fieldObject.value( "description" ).toString();
                bool typeOk = false;
                field.type = actionParameterTypeFromString(
                    fieldObject.value( "type" ).toString( QStringLiteral( "text" ) ), &typeOk );
                bool presentationOk = false;
                field.presentation = actionParameterPresentationFromString(
                    fieldObject.value( "presentation" ).toString( QStringLiteral( "auto" ) ),
                    &presentationOk );
                bool multiModeOk = false;
                field.multiValueMode = actionMultiValueModeFromString(
                    fieldObject.value( "multi_value_mode" )
                        .toString( QStringLiteral( "comma_separated" ) ),
                    &multiModeOk );
                if ( !typeOk || !presentationOk || !multiModeOk ) {
                    if ( errors ) {
                        errors->push_back(
                            QString(
                                "Action '%1' parameter '%2' has an invalid type or presentation." )
                                .arg( out->name, field.name ) );
                    }
                    return false;
                }
                field.required = fieldObject.value( "required" ).toBool( false );
                field.sensitive = fieldObject.value( "sensitive" ).toBool( false );
                field.remember = fieldObject.value( "remember" ).toBool( true );
                if ( fieldObject.contains( "default" ) ) {
                    field.defaultValue = fieldObject.value( "default" ).toVariant();
                }
                if ( fieldObject.contains( "minimum" ) ) {
                    field.minimum = fieldObject.value( "minimum" ).toVariant();
                }
                if ( fieldObject.contains( "maximum" ) ) {
                    field.maximum = fieldObject.value( "maximum" ).toVariant();
                }
                if ( fieldObject.contains( "step" ) ) {
                    field.step = fieldObject.value( "step" ).toVariant();
                }
                field.validationPattern = fieldObject.value( "validation_pattern" ).toString();
                field.format = fieldObject.value( "format" ).toString();
                field.separator
                    = fieldObject.value( "separator" ).toString( QStringLiteral( "," ) );
                field.expression = fieldObject.value( "expression" ).toString();
                if ( fieldObject.contains( "choices" ) ) {
                    const auto choicesValue = fieldObject.value( "choices" );
                    if ( !choicesValue.isArray() ) {
                        if ( errors ) {
                            errors->push_back(
                                QString( "Action '%1' parameter '%2' choices must be an array." )
                                    .arg( out->name, field.name ) );
                        }
                        return false;
                    }
                    for ( const auto& choiceValue : choicesValue.toArray() ) {
                        if ( !choiceValue.isObject() ) {
                            if ( errors ) {
                                errors->push_back(
                                    QString(
                                        "Action '%1' parameter '%2' contains an invalid choice." )
                                        .arg( out->name, field.name ) );
                            }
                            return false;
                        }
                        const auto choiceObject = choiceValue.toObject();
                        field.choices.push_back( {
                            choiceObject.value( "label" ).toString(),
                            choiceObject.value( "value" ).toVariant(),
                        } );
                    }
                }
                out->parameters.fields.push_back( field );
            }
        }
    }

    const auto checksumValue = object.value( "checksum" );
    if ( checksumValue.isObject() ) {
        const auto checksumObj = checksumValue.toObject();
        out->checksum.enabled = checksumObj.value( "enabled" ).toBool( false );
        out->checksum.algorithm
            = checksumObj.value( "algorithm" ).toString( out->checksum.algorithm );
        out->checksum.placeholder
            = checksumObj.value( "placeholder" ).toString( out->checksum.placeholder );
    }

    return true;
}

bool parseResponse( const QJsonObject& object,
                    ResponseDefinition* out,
                    QStringList* errors,
                    QStringList* warnings,
                    int index )
{
    if ( !out ) {
        return false;
    }

    const QSet<QString> knownKeys = { "id",       "order",      "enabled", "hidden",
                                      "hiden",    "name",       "description",
                                      "match",    "response" };
    const auto extraKeys = unknownKeys( object, knownKeys );
    for ( const auto& key : extraKeys ) {
        if ( warnings ) {
            warnings->push_back( QString( "Unknown response property '%1' at index %2." )
                                     .arg( key )
                                     .arg( index ) );
        }
    }

    const auto nameValue = object.value( "name" );
    if ( !nameValue.isString() || nameValue.toString().trimmed().isEmpty() ) {
        if ( errors ) {
            errors->push_back( QString( "Missing response name at index %1." ).arg( index ) );
        }
        return false;
    }
    out->name = nameValue.toString();

    if ( object.contains( "id" ) ) {
        const auto idValue = object.value( "id" );
        if ( idValue.isDouble() ) {
            out->id = idValue.toInt();
        }
        else if ( warnings ) {
            warnings->push_back(
                QString( "Invalid response id for '%1'." ).arg( out->name ) );
        }
    }
    if ( object.contains( "order" ) ) {
        out->order = object.value( "order" ).toInt( index );
    }

    if ( object.contains( "enabled" ) ) {
        out->enabled = object.value( "enabled" ).toBool( true );
    }
    if ( object.contains( "hidden" ) ) {
        const auto hiddenValue = object.value( "hidden" );
        if ( hiddenValue.isBool() ) {
            out->hidden = hiddenValue.toBool();
        }
        else if ( warnings ) {
            warnings->push_back(
                QString( "Invalid response hidden flag for '%1'." ).arg( out->name ) );
        }
    }

    if ( object.contains( "description" ) ) {
        out->description = object.value( "description" ).toString();
    }

    const auto matchValue = object.value( "match" );
    if ( !matchValue.isObject() ) {
        if ( errors ) {
            errors->push_back( QString( "Missing match for response '%1'." ).arg( out->name ) );
        }
        return false;
    }
    const auto matchObj = matchValue.toObject();
    const auto matchTypeValue = matchObj.value( "type" );
    bool matchTypeOk = true;
    if ( matchTypeValue.isString() ) {
        out->match.type = responseMatchTypeFromString( matchTypeValue.toString(), &matchTypeOk );
    }
    else {
        matchTypeOk = false;
    }
    if ( !matchTypeOk && warnings ) {
        warnings->push_back(
            QString( "Unknown match type for response '%1'." ).arg( out->name ) );
    }
    const auto matchPatternValue = matchObj.value( "value" );
    if ( !matchPatternValue.isString() || matchPatternValue.toString().trimmed().isEmpty() ) {
        if ( errors ) {
            errors->push_back(
                QString( "Missing match value for response '%1'." ).arg( out->name ) );
        }
        return false;
    }
    out->match.value = matchPatternValue.toString();
    if ( out->match.type == ResponseMatchType::Regex ) {
        out->match.compiled = QRegularExpression( out->match.value );
        if ( !out->match.compiled.isValid() ) {
            if ( errors ) {
                errors->push_back(
                    QString( "Invalid response regex for '%1': %2" )
                        .arg( out->name, out->match.compiled.errorString() ) );
            }
            return false;
        }
    }
    else if ( out->match.type == ResponseMatchType::Wildcard ) {
        out->match.compiled = QRegularExpression(
            QRegularExpression::wildcardToRegularExpression( out->match.value ),
            QRegularExpression::CaseInsensitiveOption );
    }

    const auto responseValue = object.value( "response" );
    if ( !responseValue.isObject() ) {
        if ( errors ) {
            errors->push_back( QString( "Missing response action for '%1'." ).arg( out->name ) );
        }
        return false;
    }
    const auto responseObj = responseValue.toObject();
    const QSet<QString> responseKeys = { "action_id", "action", "comment", "linebreak",
                                         "timestamp", "snapshot", "stop_communication",
                                         "steps" };
    const auto responseExtraKeys = unknownKeys( responseObj, responseKeys );
    for ( const auto& key : responseExtraKeys ) {
        if ( warnings ) {
            warnings->push_back( QString( "Unknown response action property '%1' for '%2'." )
                                     .arg( key, out->name ) );
        }
    }

    if ( responseObj.contains( "action_id" ) ) {
        const auto actionIdValue = responseObj.value( "action_id" );
        if ( actionIdValue.isDouble() ) {
            out->response.hasActionId = true;
            out->response.actionId = actionIdValue.toInt();
        }
        else if ( warnings ) {
            warnings->push_back(
                QString( "Invalid action_id for response '%1'." ).arg( out->name ) );
        }
    }

    if ( responseObj.contains( "steps" ) ) {
        const auto stepsValue = responseObj.value( "steps" );
        if ( !stepsValue.isArray() ) {
            if ( warnings ) {
                warnings->push_back(
                    QString( "Invalid steps array for response '%1'." ).arg( out->name ) );
            }
        }
        else {
            const auto stepsArray = stepsValue.toArray();
            for ( qsizetype stepIndex = 0; stepIndex < stepsArray.size(); ++stepIndex ) {
                const auto stepValue = stepsArray.at( stepIndex );
                if ( !stepValue.isObject() ) {
                    if ( warnings ) {
                        warnings->push_back(
                            QString( "Invalid response step %1 for '%2'." )
                                .arg( stepIndex )
                                .arg( out->name ) );
                    }
                    continue;
                }

                const auto stepObj = stepValue.toObject();
                ResponseActionStep step;
                if ( stepObj.value( "action_id" ).isDouble() ) {
                    step.actionId = stepObj.value( "action_id" ).toInt();
                }
                else if ( warnings ) {
                    warnings->push_back(
                        QString( "Invalid action_id in response step %1 for '%2'." )
                            .arg( stepIndex )
                            .arg( out->name ) );
                    continue;
                }
                step.delayMs = stepObj.value( "delay_ms" ).toInt( 0 );
                if ( stepObj.value( "parameters" ).isObject() ) {
                    step.parameters = stepObj.value( "parameters" ).toObject().toVariantMap();
                    for ( auto parameter = step.parameters.constBegin();
                          parameter != step.parameters.constEnd(); ++parameter ) {
                        QString bindingError;
                        responseParameterBindingFromVariant( parameter.value(), &bindingError );
                        if ( !bindingError.isEmpty() && warnings ) {
                            warnings->push_back(
                                QString( "Invalid response parameter binding '%1' in step %2 for '%3': %4" )
                                    .arg( parameter.key() )
                                    .arg( stepIndex )
                                    .arg( out->name )
                                    .arg( bindingError ) );
                        }
                    }
                }
                else if ( stepObj.contains( "parameters" ) && warnings ) {
                    warnings->push_back(
                        QString( "Invalid parameters object in response step %1 for '%2'." )
                            .arg( stepIndex )
                            .arg( out->name ) );
                }
                out->response.steps.push_back( step );
            }
        }
    }

    if ( responseObj.contains( "action" ) ) {
        const auto actionValue = responseObj.value( "action" );
        if ( actionValue.isObject() ) {
            ActionSequence actionSequence;
            if ( parseSequence( actionValue.toObject(), &actionSequence, errors, warnings,
                                QString( "response '%1' action" ).arg( out->name ) ) ) {
                out->response.inlineAction = std::move( actionSequence );
                out->response.hasInlineAction = true;
            }
        }
        else if ( warnings ) {
            warnings->push_back(
                QString( "Invalid response action for '%1'." ).arg( out->name ) );
        }
    }

    if ( responseObj.contains( "comment" ) ) {
        out->response.comment = responseObj.value( "comment" ).toString();
    }
    if ( responseObj.contains( "linebreak" ) ) {
        out->response.linebreak = responseObj.value( "linebreak" ).toBool( false );
    }
    if ( responseObj.contains( "timestamp" ) ) {
        out->response.timestamp = responseObj.value( "timestamp" ).toBool( false );
    }
    if ( responseObj.contains( "snapshot" ) ) {
        out->response.snapshot = responseObj.value( "snapshot" ).toBool( false );
    }
    if ( responseObj.contains( "stop_communication" ) ) {
        out->response.stopCommunication
            = responseObj.value( "stop_communication" ).toBool( false );
    }

    if ( out->response.steps.isEmpty() && out->response.hasActionId
         && out->response.actionId >= 0 ) {
        out->response.steps.push_back( { out->response.actionId, 0 } );
    }

    return true;
}
} // namespace

ActionsParseResult ActionsConfigParser::parseFile( const QString& path ) const
{
    ActionsParseResult result;
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        result.errors.push_back( QString( "Failed to open %1." ).arg( path ) );
        return result;
    }
    return parseJson( file.readAll() );
}

ActionsParseResult ActionsConfigParser::parseJson( const QByteArray& jsonBytes ) const
{
    ActionsParseResult result;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( stripLineComments( jsonBytes ), &parseError );
    if ( parseError.error != QJsonParseError::NoError ) {
        result.errors.push_back(
            QString( "Invalid JSON: %1 at offset %2." )
                .arg( parseError.errorString() )
                .arg( parseError.offset ) );
        return result;
    }

    if ( !document.isObject() ) {
        result.errors.push_back( "Unsupported JSON root format." );
        return result;
    }

    const auto root = document.object();
    const auto versionValue = root.value( "version" );
    if ( versionValue.isDouble() ) {
        const auto versionNumber = versionValue.toDouble();
        if ( !std::isfinite( versionNumber ) || versionNumber != std::floor( versionNumber ) ) {
            result.errors.push_back( "Actions configuration version must be an integer." );
            return result;
        }
        const auto version = static_cast<int>( versionNumber );
        if ( version != 1 && version != 3 && version != 4 ) {
            result.errors.push_back( QString( "Unsupported actions configuration version %1; "
                                              "supported versions are 1, 3, and 4." )
                                         .arg( version ) );
            return result;
        }
    }
    else if ( !versionValue.isUndefined() ) {
        result.errors.push_back( "Actions configuration version must be an integer." );
        return result;
    }
    const auto actionsValue = root.value( "actions" );
    if ( !actionsValue.isArray() ) {
        result.errors.push_back( "Missing 'actions' array in JSON." );
        return result;
    }
    const auto responsesValue = root.value( "responses" );
    if ( !responsesValue.isArray() ) {
        result.errors.push_back( "Missing 'responses' array in JSON." );
        return result;
    }

    const auto actionArray = actionsValue.toArray();
    result.actions.reserve( actionArray.size() );
    int actionIndex = 0;
    for ( const auto& item : actionArray ) {
        if ( !item.isObject() ) {
            result.errors.push_back(
                QString( "Action entry %1 is not an object." ).arg( actionIndex ) );
            ++actionIndex;
            continue;
        }
        ActionDefinition action;
        if ( parseAction( item.toObject(), &action, &result.errors, &result.warnings,
                          actionIndex ) ) {
            result.actions.push_back( std::move( action ) );
        }
        ++actionIndex;
    }

    const auto responseArray = responsesValue.toArray();
    result.responses.reserve( responseArray.size() );
    int responseIndex = 0;
    for ( const auto& item : responseArray ) {
        if ( !item.isObject() ) {
            result.errors.push_back(
                QString( "Response entry %1 is not an object." ).arg( responseIndex ) );
            ++responseIndex;
            continue;
        }
        ResponseDefinition response;
        if ( parseResponse( item.toObject(), &response, &result.errors, &result.warnings,
                            responseIndex ) ) {
            result.responses.push_back( std::move( response ) );
        }
        ++responseIndex;
    }

    normalizeActionDefinitions( &result.actions );
    normalizeResponseDefinitions( &result.responses );

    return result;
}
