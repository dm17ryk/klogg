#include "actionruntime.h"

#include <QDateTime>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <optional>

#include "actionsmanager.h"
#include "actionexpression.h"
#include "log.h"
#include "previewdecodeutils.h"
#include "streamsession.h"

namespace {
int effectiveRepeatCount( const ActionDefinition& action )
{
    return qMax( action.parameters.repeat ? 2 : 1, action.parameters.repeatCount );
}

void setError( QString* errorMessage, const QString& message )
{
    if ( errorMessage != nullptr ) {
        *errorMessage = message;
    }
}

QVariantMap capturesToParameters( const QMap<QString, QString>& captures )
{
    QVariantMap parameters;
    for ( auto iterator = captures.constBegin(); iterator != captures.constEnd(); ++iterator ) {
        parameters.insert( iterator.key(), iterator.value() );
    }
    return parameters;
}

QVariant resolveCaptureReferences( const QVariant& value, const QMap<QString, QString>& captures,
                                   QStringList* missing )
{
    if ( value.metaType().id() == QMetaType::QString ) {
        return resolveTemplateString( value.toString(), captures, missing );
    }
    if ( value.metaType().id() == QMetaType::QVariantList ) {
        QVariantList resolved;
        for ( const auto& item : value.toList() ) {
            resolved.push_back( resolveCaptureReferences( item, captures, missing ) );
        }
        return resolved;
    }
    if ( value.metaType().id() == QMetaType::QVariantMap ) {
        QVariantMap resolved;
        const auto source = value.toMap();
        for ( auto iterator = source.constBegin(); iterator != source.constEnd(); ++iterator ) {
            resolved.insert( iterator.key(),
                             resolveCaptureReferences( iterator.value(), captures, missing ) );
        }
        return resolved;
    }
    return value;
}

QString expressionWithCaptureTemplates( const QString& expression,
                                        const QMap<QString, QString>& captures,
                                        QStringList* missing )
{
    static const QRegularExpression reference( QStringLiteral( R"(\$\{([^}]+)\})" ) );
    QString result;
    qsizetype cursor = 0;
    auto match = reference.match( expression );
    while ( match.hasMatch() ) {
        result += expression.mid( cursor, match.capturedStart() - cursor );
        const auto key = match.captured( 1 );
        const auto capture = captures.constFind( key );
        if ( capture == captures.constEnd() ) {
            if ( missing ) {
                missing->push_back( key );
            }
            result += QStringLiteral( "null" );
        }
        else {
            auto literal = capture.value();
            literal.replace( QLatin1Char( '\\' ), QStringLiteral( "\\\\" ) );
            literal.replace( QLatin1Char( '"' ), QStringLiteral( "\\\"" ) );
            result += QStringLiteral( "\"%1\"" ).arg( literal );
        }
        cursor = match.capturedEnd();
        match = reference.match( expression, cursor );
    }
    result += expression.mid( cursor );
    return result;
}

std::optional<QVariantMap> responseStepParameters( const ResponseActionStep& step,
                                                   const ActionDefinition& action,
                                                   const QMap<QString, QString>& captures,
                                                   QString* errorMessage )
{
    QVariantMap parameters;
    if ( step.parameters.isEmpty() ) {
        if ( action.parameters.fields.isEmpty() ) {
            parameters = capturesToParameters( captures );
            LOG_DEBUG << "Response step action_id=" << action.id
                      << " uses legacy implicit capture bindings, capture_count="
                      << captures.size();
        }
        else {
            for ( const auto& field : action.parameters.fields ) {
                const auto capture = captures.constFind( field.name );
                if ( capture != captures.constEnd() ) {
                    parameters.insert( field.name, capture.value() );
                }
            }
            LOG_DEBUG << "Response step action_id=" << action.id
                      << " matched named captures to declared typed fields, parameter_count="
                      << parameters.size();
        }
    }
    else {
        LOG_DEBUG << "Response step action_id=" << action.id
                  << " uses explicit parameter bindings, binding_count=" << step.parameters.size();
    }
    QStringList missing;
    auto expressionVariables = capturesToParameters( captures );
    QSet<QString> processed;
    const auto resolveBinding = [ & ]( const QString& name, const QVariant& rawValue ) -> bool {
        QString bindingError;
        const auto binding = responseParameterBindingFromVariant( rawValue, &bindingError );
        if ( !bindingError.isEmpty() ) {
            setError( errorMessage,
                      QObject::tr( "Response action parameter '%1' has an invalid binding: %2" )
                          .arg( name, bindingError ) );
            return false;
        }

        QVariant resolved;
        switch ( binding.source ) {
        case ResponseParameterBindingSource::Literal:
            LOG_DEBUG << "Response step action_id=" << action.id << " parameter "
                      << name.toStdString() << " uses literal binding";
            resolved = binding.value;
            break;
        case ResponseParameterBindingSource::Capture: {
            auto reference = binding.value.toString().trimmed();
            if ( !reference.startsWith( QLatin1String( "${" ) ) ) {
                reference = QStringLiteral( "${%1}" ).arg( reference );
            }
            LOG_DEBUG << "Response step action_id=" << action.id << " parameter "
                      << name.toStdString() << " resolves capture " << reference.toStdString();
            resolved = resolveCaptureReferences( reference, captures, &missing );
            break;
        }
        case ResponseParameterBindingSource::Expression: {
            LOG_DEBUG << "Response step action_id=" << action.id << " evaluates expression for "
                      << name.toStdString();
            const auto expression = expressionWithCaptureTemplates( binding.value.toString(),
                                                                     captures, &missing );
            const auto evaluated
                = evaluateActionExpression( expression, expressionVariables );
            if ( !evaluated.ok ) {
                setError( errorMessage,
                          QObject::tr( "Response action parameter '%1' expression failed: %2" )
                              .arg( name, evaluated.error ) );
                LOG_WARNING << "Response step action_id=" << action.id
                            << " expression failed for " << name.toStdString();
                return false;
            }
            resolved = evaluated.value;
            break;
        }
        case ResponseParameterBindingSource::Legacy:
        default:
            LOG_DEBUG << "Response step action_id=" << action.id << " parameter "
                      << name.toStdString() << " uses legacy binding";
            resolved = resolveCaptureReferences( binding.value, captures, &missing );
            break;
        }
        parameters.insert( name, resolved );
        expressionVariables.insert( name, resolved );
        processed.insert( name );
        return true;
    };

    // Process declared fields first, giving expressions a deterministic order and allowing
    // later fields to reference already-resolved values.
    for ( const auto& field : action.parameters.fields ) {
        if ( step.parameters.contains( field.name )
             && !resolveBinding( field.name, step.parameters.value( field.name ) ) ) {
            return std::nullopt;
        }
    }
    for ( auto iterator = step.parameters.constBegin(); iterator != step.parameters.constEnd();
          ++iterator ) {
        if ( !processed.contains( iterator.key() )
             && !resolveBinding( iterator.key(), iterator.value() ) ) {
            return std::nullopt;
        }
    }
    if ( !missing.isEmpty() ) {
        missing.removeDuplicates();
        setError( errorMessage, QObject::tr( "Response action step is missing capture(s): %1." )
                                    .arg( missing.join( QStringLiteral( ", " ) ) ) );
        return std::nullopt;
    }
    return parameters;
}

bool actionPayloadIsSensitive( const ActionDefinition& action, const QVariantMap& parameters )
{
    return std::any_of(
        action.parameters.fields.cbegin(), action.parameters.fields.cend(),
        [ &parameters ]( const ActionParameterDefinition& field ) {
            const bool hasDefault = field.defaultValue.isValid() && !field.defaultValue.isNull();
            return field.sensitive && ( parameters.contains( field.name ) || hasDefault );
        } );
}
} // namespace

ResponseMatchResult matchResponseDefinition( const ResponseDefinition& response,
                                            const QByteArray& lineBytes,
                                            const QString& providedLineText )
{
    ResponseMatchResult result;
    result.lineText = providedLineText.isEmpty() ? QString::fromLatin1( lineBytes ) : providedLineText;

    switch ( response.match.type ) {
    case ResponseMatchType::String:
        result.matched = result.lineText.contains( response.match.value, Qt::CaseInsensitive );
        break;
    case ResponseMatchType::HexString: {
        const auto decoded = decodeHexStringToBytes( response.match.value );
        if ( decoded.ok ) {
            result.matched = lineBytes.contains( decoded.bytes );
        }
        break;
    }
    case ResponseMatchType::Regex: {
        const auto regex = response.match.compiled.isValid()
                               ? response.match.compiled
                               : QRegularExpression( response.match.value );
        const auto match = regex.match( result.lineText );
        if ( match.hasMatch() ) {
            result.matched = true;
            const auto names = regex.namedCaptureGroups();
            for ( const auto& name : names ) {
                if ( !name.isEmpty() ) {
                    result.captures.insert( name, match.captured( name ) );
                }
            }
            const auto texts = match.capturedTexts();
            for ( qsizetype i = 0; i < texts.size(); ++i ) {
                result.captures.insert( QString::number( i ), texts.at( i ) );
            }
        }
        break;
    }
    case ResponseMatchType::Wildcard: {
        const auto regex = response.match.compiled.isValid()
                               ? response.match.compiled
                               : QRegularExpression(
                                     QRegularExpression::wildcardToRegularExpression(
                                         response.match.value ),
                                     QRegularExpression::CaseInsensitiveOption );
        const auto match = regex.match( result.lineText );
        if ( match.hasMatch() ) {
            result.matched = true;
            const auto texts = match.capturedTexts();
            for ( qsizetype i = 0; i < texts.size(); ++i ) {
                result.captures.insert( QString::number( i ), texts.at( i ) );
            }
        }
        break;
    }
    }

    return result;
}

bool sendActionDefinition( StreamSession* session, const ActionDefinition& action,
                           const QVariantMap& parameters, int stepIndex, QString* errorMessage )
{
    if ( session == nullptr || !session->isConnectionOpen() ) {
        LOG_WARNING << "Action id=" << action.id << " rejected because no COM port is active";
        setError( errorMessage, QObject::tr( "No active COM port." ) );
        return false;
    }

    QStringList missing;
    const auto encoded = actionDefinitionToBytesWithParameters( action, parameters, &missing );
    if ( !encoded.ok ) {
        LOG_WARNING << "Action id=" << action.id
                    << " encoding failed; parameter_count=" << parameters.size()
                    << ", missing_count=" << missing.size();
        setError( errorMessage,
                  encoded.error.isEmpty() ? QObject::tr( "Failed to encode action." )
                                          : encoded.error );
        return false;
    }

    const bool sensitive = actionPayloadIsSensitive( action, parameters );
    const auto observableBytes = sensitive ? QByteArrayLiteral( "<redacted>" ) : encoded.bytes;
    const auto repeatCount = effectiveRepeatCount( action );
    const auto firstDelay = qMax( 0, action.parameters.delay );
    const auto repeatInterval = qMax( 0, action.parameters.repeatInterval );
    QPointer<StreamSession> safeSession = session;
    LOG_DEBUG << "Scheduling action id=" << action.id << ", repeats=" << repeatCount
              << ", encoded_bytes=" << encoded.bytes.size()
              << ( sensitive ? ", sensitive payload" : "" );

    for ( int index = 0; index < repeatCount; ++index ) {
        const auto delayMs = firstDelay + ( index * repeatInterval );
        if ( delayMs <= 0 ) {
            if ( safeSession ) {
                LOG_DEBUG << "Sending action id=" << action.id
                          << " immediately; repeat_index=" << index;
                safeSession->notifyActionSend( action.id, action.name, stepIndex, observableBytes );
                safeSession->sendBytes( encoded.bytes, sensitive );
            }
            else {
                LOG_WARNING << "Skipping immediate action id=" << action.id
                            << " because its session was destroyed";
            }
            continue;
        }

        QTimer::singleShot( delayMs, session,
                            [ safeSession, actionId = action.id, actionName = action.name,
                              stepIndex, bytes = encoded.bytes, observableBytes, sensitive,
                              repeatIndex = index ]() {
                                if ( safeSession && safeSession->isConnectionOpen() ) {
                                    LOG_DEBUG << "Sending delayed action id=" << actionId
                                              << "; repeat_index=" << repeatIndex;
                                    safeSession->notifyActionSend( actionId, actionName, stepIndex,
                                                                   observableBytes );
                                    safeSession->sendBytes( bytes, sensitive );
                                }
                                else {
                                    LOG_WARNING << "Skipping delayed action id=" << actionId
                                                << " because its session is unavailable or closed";
                                }
                            } );
    }

    return true;
}

bool executeResponseDefinition( StreamSession* session,
                                const ResponseDefinition& response,
                                const QMap<QString, QString>& captures,
                                QString* errorMessage )
{
    if ( session == nullptr ) {
        LOG_WARNING << "Response id=" << response.id << " rejected because target session is null";
        setError( errorMessage, QObject::tr( "No target COM session." ) );
        return false;
    }

    if ( response.response.hasInlineAction ) {
        ActionDefinition inlineAction;
        inlineAction.name = response.name;
        inlineAction.sequence = response.response.inlineAction;
        LOG_DEBUG << "Response id=" << response.id << " executing inline action";
        if ( !sendActionDefinition( session, inlineAction, capturesToParameters( captures ), -1,
                                    errorMessage ) ) {
            return false;
        }
    }

    QVector<ActionDefinition> linkedActions;
    QVector<int> linkedDelays;
    QVector<QVariantMap> linkedParameters;
    linkedActions.reserve( response.response.steps.size() );
    linkedDelays.reserve( response.response.steps.size() );
    linkedParameters.reserve( response.response.steps.size() );
    for ( const auto& step : response.response.steps ) {
        const auto* action = ActionsManager::instance().findActionById( step.actionId );
        if ( action == nullptr ) {
            LOG_WARNING << "Response id=" << response.id
                        << " references unknown action id=" << step.actionId;
            setError( errorMessage, QObject::tr( "Unknown action id %1." ).arg( step.actionId ) );
            return false;
        }
        linkedActions.push_back( *action );
        linkedDelays.push_back( qMax( 0, step.delayMs ) );
        const auto parameters = responseStepParameters( step, *action, captures, errorMessage );
        if ( !parameters ) {
            LOG_WARNING << "Response id=" << response.id
                        << " failed to resolve response-step parameter bindings";
            return false;
        }
        linkedParameters.push_back( *parameters );
    }

    QPointer<StreamSession> safeSession = session;
    int cumulativeDelay = 0;
    for ( int index = 0; index < linkedActions.size(); ++index ) {
        cumulativeDelay += linkedDelays.at( index );
        const auto action = linkedActions.at( index );
        const auto parameters = linkedParameters.at( index );
        if ( cumulativeDelay <= 0 ) {
            LOG_DEBUG << "Response id=" << response.id << " sending linked action index=" << index
                      << " immediately";
            if ( !sendActionDefinition( session, action, parameters, index, errorMessage ) ) {
                return false;
            }
            continue;
        }

        QTimer::singleShot(
            cumulativeDelay, session,
            [ safeSession, action, parameters, stepIndex = index, responseId = response.id ]() {
                if ( safeSession && safeSession->isConnectionOpen() ) {
                    LOG_DEBUG << "Response id=" << responseId
                              << " sending delayed linked action index=" << stepIndex;
                    QString ignoredError;
                    if ( !sendActionDefinition( safeSession, action, parameters, stepIndex,
                                                &ignoredError ) ) {
                        LOG_WARNING << "Response id=" << responseId
                                    << " delayed action failed: " << ignoredError.toStdString();
                    }
                }
                else {
                    LOG_WARNING << "Response id=" << responseId
                                << " skipped delayed action because its session is unavailable";
                }
            } );
    }

    const auto finalizeResponse = [ safeSession, response, captures ]() {
        if ( !safeSession ) {
            return;
        }

        if ( !response.response.comment.isEmpty() || response.response.linebreak ) {
            QStringList missing;
            QString comment = response.response.comment;
            if ( !captures.isEmpty() ) {
                comment = resolveTemplateString( comment, captures, &missing );
            }
            if ( response.response.timestamp ) {
                const auto timestamp = QDateTime::currentDateTime().toString( Qt::ISODateWithMs );
                comment = comment.isEmpty() ? timestamp
                                            : QStringLiteral( "%1 %2" ).arg( timestamp, comment );
            }

            QByteArray output;
            if ( !comment.isEmpty() ) {
                output.append( comment.toLatin1() );
                output.append( "\r\n" );
            }
            if ( response.response.linebreak ) {
                output.append( "\r\n" );
            }
            safeSession->appendToFile( output );
        }

        if ( response.response.stopCommunication ) {
            safeSession->closeConnection();
        }
    };

    if ( cumulativeDelay > 0 ) {
        QTimer::singleShot( cumulativeDelay, session, finalizeResponse );
    }
    else {
        finalizeResponse();
    }

    return true;
}
