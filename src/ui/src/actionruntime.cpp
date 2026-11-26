#include "actionruntime.h"

#include <QDateTime>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <optional>

#include "actionexpression.h"
#include "actionsmanager.h"
#include "diagnostics.h"
#include "log.h"
#include "previewdecodeutils.h"
#include "streamsession.h"

namespace {
int effectiveRepeatCount( const ActionDefinition& action )
{
    return qMax( action.parameters.repeat ? 2 : 1, action.parameters.repeatCount );
}
} // namespace

int initialSendDelayMs( const ActionDefinition& action, ActionSendOrigin origin )
{
    return origin == ActionSendOrigin::Automatic ? qMax( 0, action.parameters.delay ) : 0;
}

namespace {

void setError( QString* errorMessage, const QString& message )
{
    if ( errorMessage != nullptr ) {
        *errorMessage = message;
    }
}

// The activity journal of diagnostic reports (task R06) records each send as it happens, or why
// it did not happen, never the bytes: payloads may hold credentials. Repeats fold into one entry.
void journalActionSend( const QString& sessionName, int actionId, const QString& actionName,
                        bool automatic, qsizetype byteCount, const QString& outcome,
                        const QStringList& parameterNames )
{
    cilogg::diagnostics::ActivityJournal::record(
        QStringLiteral( "action" ),
        QStringLiteral( "%1 action %2 \"%3\" %4 %5 (%6 bytes)" )
            .arg( automatic ? QStringLiteral( "Automatic" ) : QStringLiteral( "Manual" ),
                  QString::number( actionId ), actionName,
                  outcome == QLatin1String( "sent" ) ? QStringLiteral( "sent to" )
                  : outcome == QLatin1String( "rejected" )
                      ? QStringLiteral( "rejected by the serial writer on" )
                      : QStringLiteral( "cancelled (its session changed) on" ),
                  sessionName, QString::number( byteCount ) ),
        { { QStringLiteral( "actionId" ), actionId },
          { QStringLiteral( "automatic" ), automatic },
          { QStringLiteral( "outcome" ), outcome },
          { QStringLiteral( "parameterNames" ), parameterNames } } );
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
            const auto expression
                = expressionWithCaptureTemplates( binding.value.toString(), captures, &missing );
            const auto evaluated = evaluateActionExpression( expression, expressionVariables );
            if ( !evaluated.ok ) {
                setError( errorMessage,
                          QObject::tr( "Response action parameter '%1' expression failed: %2" )
                              .arg( name, evaluated.error ) );
                LOG_WARNING << "Response step action_id=" << action.id << " expression failed for "
                            << name.toStdString();
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

bool actionContextIsCurrent( const QPointer<StreamSession>& session, quint64 generation,
                             bool automatic )
{
    if ( !session ) {
        LOG_DEBUG << "Scheduled action skipped: target session was destroyed";
        return false;
    }
    if ( !session->isConnectionOpen() ) {
        LOG_DEBUG << "Scheduled action skipped: target connection is closed";
        return false;
    }
    const auto currentGeneration
        = automatic ? session->autoResponsesGeneration() : session->actionsGeneration();
    if ( currentGeneration != generation ) {
        LOG_DEBUG << "Scheduled action skipped: stale generation=" << generation
                  << ", currentGeneration=" << currentGeneration;
        return false;
    }
    if ( automatic
         && ( !session->actionsEnabled() || !session->autoResponsesEnabled()
              || !ActionsManager::instance().autoResponsesEnabled() ) ) {
        LOG_DEBUG << "Automatic action skipped: tabEnabled=" << session->actionsEnabled()
                  << ", autoResponsesEnabled=" << session->autoResponsesEnabled()
                  << ", globalEnabled=" << ActionsManager::instance().autoResponsesEnabled();
        return false;
    }
    return true;
}
} // namespace

ResponseMatchResult matchResponseDefinition( const ResponseDefinition& response,
                                             const QByteArray& lineBytes,
                                             const QString& providedLineText )
{
    ResponseMatchResult result;
    result.lineText
        = providedLineText.isEmpty() ? QString::fromLatin1( lineBytes ) : providedLineText;

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
        const auto regex
            = response.match.compiled.isValid()
                  ? response.match.compiled
                  : QRegularExpression(
                        QRegularExpression::wildcardToRegularExpression( response.match.value ),
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

bool sendActionDefinition( StreamSession* session, const ActionDefinition& requestedAction,
                           ActionSendOrigin origin, const QVariantMap& parameters, int stepIndex,
                           QString* errorMessage )
{
    // Observers may edit the repository synchronously while an action is sent.
    const auto action = requestedAction;
    const bool automatic = origin == ActionSendOrigin::Automatic;
    if ( session == nullptr || !session->isConnectionOpen() ) {
        LOG_WARNING << "Action id=" << action.id << " rejected because no COM port is active";
        setError( errorMessage, QObject::tr( "No active COM port." ) );
        return false;
    }

    const QPointer<StreamSession> safeSession = session;
    const auto generation
        = automatic ? session->autoResponsesGeneration() : session->actionsGeneration();
    if ( !actionContextIsCurrent( safeSession, generation, automatic ) ) {
        setError( errorMessage,
                  QObject::tr( "Automatic actions are disabled for this COM session." ) );
        return false;
    }

    QStringList missing;
    const auto encoded = actionDefinitionToBytesWithParameters( action, parameters, &missing );
    if ( !encoded.ok ) {
        LOG_WARNING << "Action id=" << action.id
                    << " encoding failed; parameter_count=" << parameters.size()
                    << ", missing_count=" << missing.size();
        setError( errorMessage, encoded.error.isEmpty() ? QObject::tr( "Failed to encode action." )
                                                        : encoded.error );
        return false;
    }

    const bool sensitive = actionPayloadIsSensitive( action, parameters );
#ifdef KLOGG_SHARED_SERIAL_WRITER
    const cilogg::communication::ActionWriteStamp writeStamp{ automatic,
                                                              session->actionsGeneration(),
                                                              session->autoResponsesGeneration() };
#endif
    const auto observableBytes = sensitive ? QByteArrayLiteral( "<redacted>" ) : encoded.bytes;
    const auto repeatCount = effectiveRepeatCount( action );
    // A manual send (the Send button, a menu, the parameter dialog, Commander) goes out the
    // moment it is asked for (issue #142): the action's initial delay only postpones automatic
    // sends, which responses schedule. Repeats keep their interval either way.
    const auto firstDelay = initialSendDelayMs( action, origin );
    const auto repeatInterval = qMax( 0, action.parameters.repeatInterval );
    const auto sessionName = session->sourceDisplayName();
    const auto parameterNames = QStringList( parameters.keys() );
    // A configuration pack's action (E16c3): its later sends (the initial delay, the repeats) go
    // out only while the packs are as they were, so disabling, updating or removing the pack
    // stops them.
    const bool packAction = action.id >= PackItemIdBase;
    const auto packGeneration = ActionsManager::instance().packGeneration();
    LOG_DEBUG << "Scheduling action id=" << action.id << ", repeats=" << repeatCount
              << ", encoded_bytes=" << encoded.bytes.size() << ", automatic=" << automatic
              << ", generation=" << generation << ( sensitive ? ", sensitive payload" : "" );

    for ( int index = 0; index < repeatCount; ++index ) {
        const auto delayMs = firstDelay + ( index * repeatInterval );
        if ( !actionContextIsCurrent( safeSession, generation, automatic ) ) {
            journalActionSend( sessionName, action.id, action.name, automatic,
                               encoded.bytes.size(), QStringLiteral( "cancelled" ), parameterNames );
            setError( errorMessage, QObject::tr( "Action cancelled after its session changed." ) );
            return false;
        }
        if ( delayMs <= 0 ) {
            LOG_DEBUG << "Sending action id=" << action.id
                      << " immediately; repeat_index=" << index;
            safeSession->notifyActionSend( action.id, action.name, stepIndex, observableBytes );
            if ( !actionContextIsCurrent( safeSession, generation, automatic ) ) {
                journalActionSend( sessionName, action.id, action.name, automatic,
                                   encoded.bytes.size(), QStringLiteral( "cancelled" ),
                                   parameterNames );
                setError( errorMessage, QObject::tr( "Action cancelled by its send observer." ) );
                return false;
            }
#ifdef KLOGG_SHARED_SERIAL_WRITER
            const auto admitted
                = safeSession->submitActionWrite( encoded.bytes, sensitive, writeStamp );
            if ( !admitted.value ) {
                journalActionSend( sessionName, action.id, action.name, automatic,
                                   encoded.bytes.size(), QStringLiteral( "rejected" ),
                                   parameterNames );
                setError( errorMessage,
                          QObject::tr( "Serial write rejected: %1" ).arg( admitted.error.code ) );
                return false;
            }
#else
            safeSession->sendBytes( encoded.bytes, sensitive );
#endif
            journalActionSend( sessionName, action.id, action.name, automatic, encoded.bytes.size(),
                               QStringLiteral( "sent" ), parameterNames );
            continue;
        }

        QTimer::singleShot(
            delayMs, safeSession.data(),
            [ safeSession, actionId = action.id, actionName = action.name, stepIndex,
              bytes = encoded.bytes, observableBytes, sensitive, repeatIndex = index, generation,
              automatic, sessionName, parameterNames, packAction, packGeneration
#ifdef KLOGG_SHARED_SERIAL_WRITER
              ,
              writeStamp
#endif
        ]() {
                const auto journal = [ & ]( const QString& outcome ) {
                    journalActionSend( sessionName, actionId, actionName, automatic, bytes.size(),
                                       outcome, parameterNames );
                };
                if ( !actionContextIsCurrent( safeSession, generation, automatic )
                     || ( packAction
                          && ActionsManager::instance().packGeneration() != packGeneration ) ) {
                    LOG_WARNING << "Skipping delayed action id=" << actionId
                                << " because its execution context changed";
                    journal( QStringLiteral( "cancelled" ) );
                    return;
                }
                LOG_DEBUG << "Sending delayed action id=" << actionId
                          << "; repeat_index=" << repeatIndex;
                safeSession->notifyActionSend( actionId, actionName, stepIndex, observableBytes );
                if ( !actionContextIsCurrent( safeSession, generation, automatic ) ) {
                    journal( QStringLiteral( "cancelled" ) );
                    return;
                }
#ifdef KLOGG_SHARED_SERIAL_WRITER
                const auto admitted = safeSession->submitActionWrite( bytes, sensitive, writeStamp );
                if ( !admitted.value ) {
                    LOG_WARNING << "Delayed serial Action rejected reason=" << admitted.error.code;
                    journal( QStringLiteral( "rejected" ) );
                    return;
                }
#else
                safeSession->sendBytes( bytes, sensitive );
#endif
                journal( QStringLiteral( "sent" ) );
            } );
    }

    return true;
}

bool executeResponseDefinition( StreamSession* session, const ResponseDefinition& requestedResponse,
                                const QMap<QString, QString>& captures, QString* errorMessage )
{
    const auto response = requestedResponse;
    if ( session == nullptr ) {
        LOG_WARNING << "Response id=" << response.id << " rejected because target session is null";
        setError( errorMessage, QObject::tr( "No target COM session." ) );
        return false;
    }

    const QPointer<StreamSession> safeSession = session;
    const auto generation = session->autoResponsesGeneration();
    if ( !actionContextIsCurrent( safeSession, generation, true ) ) {
        setError( errorMessage, QObject::tr( "No eligible COM session for automatic responses." ) );
        return false;
    }
    // A configuration pack's response, or a step that runs a pack action (E16c3): what it
    // schedules for later runs only while the packs are as they were, so disabling, updating or
    // removing a pack stops it.
    const auto packGeneration = ActionsManager::instance().packGeneration();
    const bool packResponse = response.id >= PackItemIdBase;
    const auto packsUnchanged = [ packGeneration ] {
        return ActionsManager::instance().packGeneration() == packGeneration;
    };

    cilogg::diagnostics::ActivityJournal::record(
        QStringLiteral( "response" ),
        QStringLiteral( "Response %1 \"%2\" fired on %3" )
            .arg( QString::number( response.id ), response.name, session->sourceDisplayName() ),
        { { QStringLiteral( "responseId" ), response.id },
          { QStringLiteral( "captureNames" ), QStringList( captures.keys() ) } } );

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

    // Only once every step can run: a response that cannot run all of it (a step whose action is
    // gone, or a pack action whose pack is disabled) sends nothing, its inline action included.
    if ( response.response.hasInlineAction ) {
        ActionDefinition inlineAction;
        inlineAction.name = response.name;
        inlineAction.sequence = response.response.inlineAction;
        LOG_DEBUG << "Response id=" << response.id << " executing inline action";
        if ( !sendActionDefinition( session, inlineAction, ActionSendOrigin::Automatic,
                                    capturesToParameters( captures ), -1, errorMessage ) ) {
            return false;
        }
    }

    int cumulativeDelay = 0;
    for ( int index = 0; index < linkedActions.size(); ++index ) {
        if ( !actionContextIsCurrent( safeSession, generation, true ) ) {
            setError( errorMessage,
                      QObject::tr( "Response cancelled after its session changed." ) );
            return false;
        }
        cumulativeDelay += linkedDelays.at( index );
        const auto action = linkedActions.at( index );
        const auto parameters = linkedParameters.at( index );
        if ( cumulativeDelay <= 0 ) {
            LOG_DEBUG << "Response id=" << response.id << " sending linked action index=" << index
                      << " immediately";
            if ( !sendActionDefinition( safeSession, action, ActionSendOrigin::Automatic,
                                        parameters, index, errorMessage ) ) {
                return false;
            }
            continue;
        }

        const bool packWork = packResponse || action.id >= PackItemIdBase;
        QTimer::singleShot(
            cumulativeDelay, safeSession.data(),
            [ safeSession, action, parameters, stepIndex = index, responseId = response.id,
              generation, packWork, packsUnchanged ]() {
                if ( actionContextIsCurrent( safeSession, generation, true )
                     && ( !packWork || packsUnchanged() ) ) {
                    LOG_DEBUG << "Response id=" << responseId
                              << " sending delayed linked action index=" << stepIndex;
                    QString ignoredError;
                    if ( !sendActionDefinition( safeSession, action, ActionSendOrigin::Automatic,
                                                parameters, stepIndex, &ignoredError ) ) {
                        LOG_WARNING << "Response id=" << responseId
                                    << " delayed action failed: " << ignoredError.toStdString();
                    }
                }
                else {
                    LOG_WARNING << "Response id=" << responseId
                                << " skipped delayed action because its execution context changed";
                }
            } );
    }

    const auto finalizeResponse = [ safeSession, response, captures, generation, packResponse,
                                    packsUnchanged ]() {
        if ( !actionContextIsCurrent( safeSession, generation, true )
             || ( packResponse && !packsUnchanged() ) ) {
            LOG_DEBUG << "Response id=" << response.id << " skipped stale finalization";
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
        if ( !actionContextIsCurrent( safeSession, generation, true ) ) {
            setError( errorMessage, QObject::tr( "Response cancelled before finalization." ) );
            return false;
        }
        QTimer::singleShot( cumulativeDelay, safeSession.data(), finalizeResponse );
    }
    else {
        finalizeResponse();
    }

    return true;
}
