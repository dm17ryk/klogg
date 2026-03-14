#include "actionruntime.h"

#include <QDateTime>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

#include "actionsmanager.h"
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

bool sendActionDefinition( StreamSession* session,
                           const ActionDefinition& action,
                           const QMap<QString, QString>& substitutions,
                           QString* errorMessage )
{
    if ( session == nullptr || !session->isConnectionOpen() ) {
        setError( errorMessage, QObject::tr( "No active COM port." ) );
        return false;
    }

    QStringList missing;
    const auto encoded = actionDefinitionToBytes( action, substitutions, &missing );
    if ( !encoded.ok ) {
        setError( errorMessage,
                  encoded.error.isEmpty() ? QObject::tr( "Failed to encode action." )
                                          : encoded.error );
        return false;
    }

    const auto repeatCount = effectiveRepeatCount( action );
    const auto firstDelay = qMax( 0, action.parameters.delay );
    const auto repeatInterval = qMax( 0, action.parameters.repeatInterval );
    QPointer<StreamSession> safeSession = session;

    for ( int index = 0; index < repeatCount; ++index ) {
        const auto delayMs = firstDelay + ( index * repeatInterval );
        if ( delayMs <= 0 ) {
            if ( safeSession ) {
                safeSession->sendBytes( encoded.bytes );
            }
            continue;
        }

        QTimer::singleShot( delayMs, session, [ safeSession, bytes = encoded.bytes ]() {
            if ( safeSession && safeSession->isConnectionOpen() ) {
                safeSession->sendBytes( bytes );
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
        setError( errorMessage, QObject::tr( "No target COM session." ) );
        return false;
    }

    if ( response.response.hasInlineAction ) {
        ActionDefinition inlineAction;
        inlineAction.name = response.name;
        inlineAction.sequence = response.response.inlineAction;
        if ( !sendActionDefinition( session, inlineAction, captures, errorMessage ) ) {
            return false;
        }
    }

    QVector<ActionDefinition> linkedActions;
    QVector<int> linkedDelays;
    linkedActions.reserve( response.response.steps.size() );
    linkedDelays.reserve( response.response.steps.size() );
    for ( const auto& step : response.response.steps ) {
        const auto* action = ActionsManager::instance().findActionById( step.actionId );
        if ( action == nullptr ) {
            setError( errorMessage, QObject::tr( "Unknown action id %1." ).arg( step.actionId ) );
            return false;
        }
        linkedActions.push_back( *action );
        linkedDelays.push_back( qMax( 0, step.delayMs ) );
    }

    QPointer<StreamSession> safeSession = session;
    int cumulativeDelay = 0;
    for ( int index = 0; index < linkedActions.size(); ++index ) {
        cumulativeDelay += linkedDelays.at( index );
        const auto action = linkedActions.at( index );
        if ( cumulativeDelay <= 0 ) {
            if ( !sendActionDefinition( session, action, captures, errorMessage ) ) {
                return false;
            }
            continue;
        }

        QTimer::singleShot( cumulativeDelay, session, [ safeSession, action, captures ]() {
            if ( safeSession && safeSession->isConnectionOpen() ) {
                QString ignoredError;
                sendActionDefinition( safeSession, action, captures, &ignoredError );
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
