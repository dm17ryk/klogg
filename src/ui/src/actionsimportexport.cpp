#include "actionsimportexport.h"

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include "actionsconfigparser.h"

namespace {
void setError( QString* errorMessage, const QString& message )
{
    if ( errorMessage != nullptr ) {
        *errorMessage = message;
    }
}

QString normalizedName( const QString& name )
{
    return name.trimmed().toCaseFolded();
}

QJsonObject sequenceToJson( const ActionSequence& sequence )
{
    QJsonObject obj;
    obj.insert( "type", actionSequenceTypeToString( sequence.type ) );
    obj.insert( "value", sequence.value );
    return obj;
}

QJsonObject actionToJson( const ActionDefinition& action )
{
    QJsonObject obj;
    obj.insert( "id", action.id );
    obj.insert( "order", action.order );
    obj.insert( "enabled", action.enabled );
    obj.insert( "hidden", action.hidden );
    obj.insert( "name", action.name );
    obj.insert( "description", action.description );
    obj.insert( "sequence", sequenceToJson( action.sequence ) );

    QJsonObject params;
    params.insert( "repeat", action.parameters.repeat );
    params.insert( "delay", action.parameters.delay );
    params.insert( "repeat_count", action.parameters.repeatCount );
    params.insert( "repeat_interval", action.parameters.repeatInterval );
    params.insert( "variable_names", QJsonArray::fromStringList( action.parameters.variableNames ) );
    obj.insert( "parameters", params );

    QJsonObject checksum;
    checksum.insert( "enabled", action.checksum.enabled );
    checksum.insert( "algorithm", action.checksum.algorithm );
    checksum.insert( "placeholder", action.checksum.placeholder );
    obj.insert( "checksum", checksum );
    return obj;
}

QJsonObject responseToJson( const ResponseDefinition& response )
{
    QJsonObject matchObj;
    matchObj.insert( "type", responseMatchTypeToString( response.match.type ) );
    matchObj.insert( "value", response.match.value );

    QJsonObject responseObj;
    if ( response.response.hasActionId ) {
        responseObj.insert( "action_id", response.response.actionId );
    }
    if ( !response.response.steps.isEmpty() ) {
        QJsonArray stepsArray;
        for ( const auto& step : response.response.steps ) {
            QJsonObject stepObj;
            stepObj.insert( "action_id", step.actionId );
            stepObj.insert( "delay_ms", step.delayMs );
            stepsArray.append( stepObj );
        }
        responseObj.insert( "steps", stepsArray );
    }
    if ( response.response.hasInlineAction ) {
        responseObj.insert( "action", sequenceToJson( response.response.inlineAction ) );
    }
    responseObj.insert( "comment", response.response.comment );
    responseObj.insert( "linebreak", response.response.linebreak );
    responseObj.insert( "timestamp", response.response.timestamp );
    responseObj.insert( "snapshot", response.response.snapshot );
    responseObj.insert( "stop_communication", response.response.stopCommunication );

    QJsonObject obj;
    obj.insert( "id", response.id );
    obj.insert( "order", response.order );
    obj.insert( "enabled", response.enabled );
    obj.insert( "hidden", response.hidden );
    obj.insert( "name", response.name );
    obj.insert( "description", response.description );
    obj.insert( "match", matchObj );
    obj.insert( "response", responseObj );
    return obj;
}

bool equivalentAction( const ActionDefinition& lhs, const ActionDefinition& rhs )
{
    return lhs.enabled == rhs.enabled && lhs.hidden == rhs.hidden && lhs.name == rhs.name
           && lhs.description == rhs.description && lhs.sequence.type == rhs.sequence.type
           && lhs.sequence.value == rhs.sequence.value
           && lhs.parameters.repeat == rhs.parameters.repeat
           && lhs.parameters.delay == rhs.parameters.delay
           && lhs.parameters.repeatCount == rhs.parameters.repeatCount
           && lhs.parameters.repeatInterval == rhs.parameters.repeatInterval
           && lhs.parameters.variableNames == rhs.parameters.variableNames
           && lhs.checksum.enabled == rhs.checksum.enabled
           && lhs.checksum.algorithm == rhs.checksum.algorithm
           && lhs.checksum.placeholder == rhs.checksum.placeholder;
}

bool equivalentResponse( const ResponseDefinition& lhs, const ResponseDefinition& rhs )
{
    if ( lhs.response.steps.size() != rhs.response.steps.size() ) {
        return false;
    }
    for ( int index = 0; index < lhs.response.steps.size(); ++index ) {
        if ( lhs.response.steps.at( index ).actionId != rhs.response.steps.at( index ).actionId
             || lhs.response.steps.at( index ).delayMs != rhs.response.steps.at( index ).delayMs ) {
            return false;
        }
    }

    return lhs.enabled == rhs.enabled && lhs.hidden == rhs.hidden && lhs.name == rhs.name
           && lhs.description == rhs.description && lhs.match.type == rhs.match.type
           && lhs.match.value == rhs.match.value
           && lhs.response.hasActionId == rhs.response.hasActionId
           && lhs.response.actionId == rhs.response.actionId
           && lhs.response.hasInlineAction == rhs.response.hasInlineAction
           && lhs.response.inlineAction.type == rhs.response.inlineAction.type
           && lhs.response.inlineAction.value == rhs.response.inlineAction.value
           && lhs.response.comment == rhs.response.comment
           && lhs.response.linebreak == rhs.response.linebreak
           && lhs.response.timestamp == rhs.response.timestamp
           && lhs.response.snapshot == rhs.response.snapshot
           && lhs.response.stopCommunication == rhs.response.stopCommunication;
}

int nextActionId( const QVector<ActionDefinition>& actions )
{
    int maxId = 0;
    for ( const auto& action : actions ) {
        maxId = qMax( maxId, action.id );
    }
    return maxId + 1;
}

int nextResponseId( const QVector<ResponseDefinition>& responses )
{
    int maxId = 0;
    for ( const auto& response : responses ) {
        maxId = qMax( maxId, response.id );
    }
    return maxId + 1;
}

int findActionById( const QVector<ActionDefinition>& actions, int id )
{
    for ( int index = 0; index < actions.size(); ++index ) {
        if ( actions.at( index ).id == id ) {
            return index;
        }
    }
    return -1;
}

int findActionByName( const QVector<ActionDefinition>& actions, const QString& name )
{
    const auto normalized = normalizedName( name );
    if ( normalized.isEmpty() ) {
        return -1;
    }
    for ( int index = 0; index < actions.size(); ++index ) {
        if ( normalizedName( actions.at( index ).name ) == normalized ) {
            return index;
        }
    }
    return -1;
}

int findResponseById( const QVector<ResponseDefinition>& responses, int id )
{
    for ( int index = 0; index < responses.size(); ++index ) {
        if ( responses.at( index ).id == id ) {
            return index;
        }
    }
    return -1;
}

int findResponseByName( const QVector<ResponseDefinition>& responses, const QString& name )
{
    const auto normalized = normalizedName( name );
    if ( normalized.isEmpty() ) {
        return -1;
    }
    for ( int index = 0; index < responses.size(); ++index ) {
        if ( normalizedName( responses.at( index ).name ) == normalized ) {
            return index;
        }
    }
    return -1;
}

QString normalizeHexString( const QString& value )
{
    QStringList parts;
    for ( const auto& part : value.split( QRegularExpression( "\\s+" ), Qt::SkipEmptyParts ) ) {
        parts.push_back( part.toUpper() );
    }
    return parts.join( QLatin1Char( ' ' ) );
}

bool parseDocklightDelayMs( const QString& text, int* delayMs )
{
    auto normalized = text.trimmed();
    if ( normalized.startsWith( QLatin1Char( '.' ) ) ) {
        normalized.prepend( QLatin1Char( '0' ) );
    }

    bool ok = false;
    const double seconds = normalized.toDouble( &ok );
    if ( !ok || seconds < 0.0 ) {
        return false;
    }

    *delayMs = static_cast<int>( std::llround( seconds * 1000.0 ) );
    return true;
}

ActionsImportConflict makeActionConflict( const ActionDefinition& existing,
                                          const ActionDefinition& imported,
                                          const QString& matchType )
{
    ActionsImportConflict conflict;
    conflict.itemType = QStringLiteral( "action" );
    conflict.matchType = matchType;
    conflict.existingId = existing.id;
    conflict.importedId = imported.id;
    conflict.existingName = existing.name;
    conflict.importedName = imported.name;
    conflict.existingDefinition = actionDefinitionToVariantMap( existing );
    conflict.importedDefinition = actionDefinitionToVariantMap( imported );
    return conflict;
}

ActionsImportConflict makeResponseConflict( const ResponseDefinition& existing,
                                            const ResponseDefinition& imported,
                                            const QString& matchType )
{
    ActionsImportConflict conflict;
    conflict.itemType = QStringLiteral( "response" );
    conflict.matchType = matchType;
    conflict.existingId = existing.id;
    conflict.importedId = imported.id;
    conflict.existingName = existing.name;
    conflict.importedName = imported.name;
    conflict.existingDefinition = responseDefinitionToVariantMap( existing );
    conflict.importedDefinition = responseDefinitionToVariantMap( imported );
    return conflict;
}
} // namespace

QString actionsImportFormatToString( ActionsImportFormat format )
{
    switch ( format ) {
    case ActionsImportFormat::Json:
        return QStringLiteral( "json" );
    case ActionsImportFormat::DocklightPtp:
        return QStringLiteral( "docklight-ptp" );
    case ActionsImportFormat::Auto:
    default:
        return QStringLiteral( "auto" );
    }
}

ActionsImportFormat actionsImportFormatFromString( const QString& value, bool* ok )
{
    if ( ok ) {
        *ok = true;
    }
    const auto normalized = value.trimmed().toLower();
    if ( normalized.isEmpty() || normalized == QStringLiteral( "auto" ) ) {
        return ActionsImportFormat::Auto;
    }
    if ( normalized == QStringLiteral( "json" ) ) {
        return ActionsImportFormat::Json;
    }
    if ( normalized == QStringLiteral( "docklight-ptp" ) || normalized == QStringLiteral( "ptp" ) ) {
        return ActionsImportFormat::DocklightPtp;
    }
    if ( ok ) {
        *ok = false;
    }
    return ActionsImportFormat::Auto;
}

QString actionsConflictPolicyToString( ActionsConflictPolicy policy )
{
    switch ( policy ) {
    case ActionsConflictPolicy::KeepExisting:
        return QStringLiteral( "keep-existing" );
    case ActionsConflictPolicy::UseImported:
        return QStringLiteral( "use-imported" );
    case ActionsConflictPolicy::Fail:
    default:
        return QStringLiteral( "fail" );
    }
}

ActionsConflictPolicy actionsConflictPolicyFromString( const QString& value, bool* ok )
{
    if ( ok ) {
        *ok = true;
    }
    const auto normalized = value.trimmed().toLower();
    if ( normalized.isEmpty() || normalized == QStringLiteral( "fail" ) ) {
        return ActionsConflictPolicy::Fail;
    }
    if ( normalized == QStringLiteral( "keep-existing" ) ) {
        return ActionsConflictPolicy::KeepExisting;
    }
    if ( normalized == QStringLiteral( "use-imported" ) ) {
        return ActionsConflictPolicy::UseImported;
    }
    if ( ok ) {
        *ok = false;
    }
    return ActionsConflictPolicy::Fail;
}

QJsonObject actionsConfigToJsonObject( const QVector<ActionDefinition>& actions,
                                       const QVector<ResponseDefinition>& responses )
{
    QJsonArray actionsArray;
    for ( const auto& action : actions ) {
        actionsArray.append( actionToJson( action ) );
    }

    QJsonArray responsesArray;
    for ( const auto& response : responses ) {
        responsesArray.append( responseToJson( response ) );
    }

    QJsonObject root;
    root.insert( "version", 3 );
    root.insert( "actions", actionsArray );
    root.insert( "responses", responsesArray );
    return root;
}

QByteArray actionsConfigToJson( const QVector<ActionDefinition>& actions,
                                const QVector<ResponseDefinition>& responses,
                                bool pretty )
{
    const QJsonDocument doc( actionsConfigToJsonObject( actions, responses ) );
    return doc.toJson( pretty ? QJsonDocument::Indented : QJsonDocument::Compact );
}

bool writeActionsConfigFile( const QString& path,
                             const QVector<ActionDefinition>& actions,
                             const QVector<ResponseDefinition>& responses,
                             QString* errorMessage,
                             bool pretty )
{
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        setError( errorMessage, QObject::tr( "Failed to open %1 for writing." ).arg( path ) );
        return false;
    }
    file.write( actionsConfigToJson( actions, responses, pretty ) );
    if ( !file.commit() ) {
        setError( errorMessage, QObject::tr( "Failed to save %1." ).arg( path ) );
        return false;
    }
    return true;
}

ActionsParseResult parseActionsConfigFile( const QString& path, ActionsImportFormat format )
{
    if ( format == ActionsImportFormat::Auto ) {
        const auto suffix = QFileInfo( path ).suffix().toLower();
        format = suffix == QStringLiteral( "ptp" ) ? ActionsImportFormat::DocklightPtp
                                                   : ActionsImportFormat::Json;
    }

    if ( format == ActionsImportFormat::DocklightPtp ) {
        return parseDocklightPtpFile( path );
    }

    ActionsConfigParser parser;
    return parser.parseFile( path );
}

ActionsParseResult parseDocklightPtpFile( const QString& path )
{
    ActionsParseResult result;
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        result.errors.push_back( QString( "Failed to open %1." ).arg( path ) );
        return result;
    }

    const auto text = QString::fromUtf8( file.readAll() );
    const auto lines = text.split( QRegularExpression( "\\r\\n|\\n|\\r" ) );
    int order = 0;
    for ( int index = 0; index < lines.size(); ++index ) {
        if ( lines.at( index ).trimmed() != QStringLiteral( "SEND" ) ) {
            continue;
        }

        if ( index + 5 >= lines.size() ) {
            result.errors.push_back(
                QString( "Truncated SEND record at line %1." ).arg( index + 1 ) );
            break;
        }

        bool idOk = false;
        const auto id = lines.at( index + 1 ).trimmed().toInt( &idOk );
        const auto name = lines.at( index + 2 ).trimmed();
        const auto hex = normalizeHexString( lines.at( index + 3 ) );
        const auto repeatText = lines.at( index + 4 ).trimmed();
        const auto delayText = lines.at( index + 5 ).trimmed();

        int delayMs = 0;
        if ( !idOk ) {
            result.errors.push_back(
                QString( "Invalid SEND id at line %1." ).arg( index + 2 ) );
        }
        if ( name.isEmpty() ) {
            result.errors.push_back(
                QString( "Missing SEND name at line %1." ).arg( index + 3 ) );
        }
        if ( !parseDocklightDelayMs( delayText, &delayMs ) ) {
            result.errors.push_back(
                QString( "Invalid SEND delay at line %1." ).arg( index + 6 ) );
        }

        ActionDefinition action;
        action.id = id;
        action.order = order++;
        action.enabled = true;
        action.hidden = false;
        action.name = name;
        action.description = name;
        action.sequence.type = ActionSequenceType::HexString;
        action.sequence.value = hex;
        action.parameters.delay = delayMs;
        action.parameters.repeat = repeatText == QStringLiteral( "1" );
        action.parameters.repeatCount = action.parameters.repeat ? 2 : 1;

        QString validationError;
        if ( !validateActionDefinition( action, &validationError ) ) {
            result.errors.push_back(
                QString( "Invalid SEND record %1: %2" ).arg( id ).arg( validationError ) );
        }
        else {
            result.actions.push_back( action );
        }

        index += 5;
    }

    normalizeActionDefinitions( &result.actions );
    return result;
}

ActionsConfigMergeResult mergeActionsConfig( const QVector<ActionDefinition>& existingActions,
                                             const QVector<ResponseDefinition>& existingResponses,
                                             QVector<ActionDefinition> importedActions,
                                             QVector<ResponseDefinition> importedResponses,
                                             ActionsConflictPolicy conflictPolicy )
{
    ActionsConfigMergeResult result;
    result.actions = existingActions;
    result.responses = existingResponses;
    normalizeActionDefinitions( &importedActions );
    normalizeResponseDefinitions( &importedResponses );

    for ( auto imported : importedActions ) {
        int existingIndex = findActionById( result.actions, imported.id );
        QString matchType = QStringLiteral( "id" );
        if ( existingIndex < 0 ) {
            existingIndex = findActionByName( result.actions, imported.name );
            matchType = QStringLiteral( "name" );
        }

        if ( existingIndex < 0 ) {
            if ( imported.id < 0 || findActionById( result.actions, imported.id ) >= 0 ) {
                imported.id = nextActionId( result.actions );
            }
            imported.order = result.actions.size();
            result.actions.push_back( imported );
            ++result.added;
            continue;
        }

        const auto existing = result.actions.at( existingIndex );
        if ( equivalentAction( existing, imported ) ) {
            ++result.skipped;
            continue;
        }

        const auto conflict = makeActionConflict( existing, imported, matchType );
        result.conflicts.push_back( conflict );
        if ( conflictPolicy == ActionsConflictPolicy::Fail ) {
            continue;
        }
        if ( conflictPolicy == ActionsConflictPolicy::KeepExisting ) {
            ++result.skipped;
            continue;
        }

        imported.id = existing.id;
        imported.order = existing.order;
        result.actions[ existingIndex ] = imported;
        ++result.updated;
    }

    if ( conflictPolicy == ActionsConflictPolicy::Fail && !result.conflicts.isEmpty() ) {
        result.errors = actionConflictSummaries( result.conflicts );
        return result;
    }

    for ( auto imported : importedResponses ) {
        int existingIndex = findResponseById( result.responses, imported.id );
        QString matchType = QStringLiteral( "id" );
        if ( existingIndex < 0 ) {
            existingIndex = findResponseByName( result.responses, imported.name );
            matchType = QStringLiteral( "name" );
        }

        if ( existingIndex < 0 ) {
            if ( imported.id < 0 || findResponseById( result.responses, imported.id ) >= 0 ) {
                imported.id = nextResponseId( result.responses );
            }
            imported.order = result.responses.size();
            result.responses.push_back( imported );
            ++result.added;
            continue;
        }

        const auto existing = result.responses.at( existingIndex );
        if ( equivalentResponse( existing, imported ) ) {
            ++result.skipped;
            continue;
        }

        const auto conflict = makeResponseConflict( existing, imported, matchType );
        result.conflicts.push_back( conflict );
        if ( conflictPolicy == ActionsConflictPolicy::Fail ) {
            continue;
        }
        if ( conflictPolicy == ActionsConflictPolicy::KeepExisting ) {
            ++result.skipped;
            continue;
        }

        imported.id = existing.id;
        imported.order = existing.order;
        result.responses[ existingIndex ] = imported;
        ++result.updated;
    }

    if ( conflictPolicy == ActionsConflictPolicy::Fail && !result.conflicts.isEmpty() ) {
        result.errors = actionConflictSummaries( result.conflicts );
        return result;
    }

    normalizeActionDefinitions( &result.actions );
    normalizeResponseDefinitions( &result.responses );
    result.ok = true;
    return result;
}

QStringList actionConflictSummaries( const QVector<ActionsImportConflict>& conflicts )
{
    QStringList summaries;
    for ( const auto& conflict : conflicts ) {
        summaries.push_back(
            QObject::tr( "%1 conflict by %2: existing #%3 \"%4\", imported #%5 \"%6\"." )
                .arg( conflict.itemType,
                      conflict.matchType,
                      QString::number( conflict.existingId ),
                      conflict.existingName,
                      QString::number( conflict.importedId ),
                      conflict.importedName ) );
    }
    return summaries;
}

QVariantList actionConflictsToVariantList( const QVector<ActionsImportConflict>& conflicts )
{
    QVariantList values;
    for ( const auto& conflict : conflicts ) {
        QVariantMap map;
        map.insert( QStringLiteral( "itemType" ), conflict.itemType );
        map.insert( QStringLiteral( "matchType" ), conflict.matchType );
        map.insert( QStringLiteral( "existingId" ), conflict.existingId );
        map.insert( QStringLiteral( "importedId" ), conflict.importedId );
        map.insert( QStringLiteral( "existingName" ), conflict.existingName );
        map.insert( QStringLiteral( "importedName" ), conflict.importedName );
        map.insert( QStringLiteral( "existing" ), conflict.existingDefinition );
        map.insert( QStringLiteral( "imported" ), conflict.importedDefinition );
        values.push_back( map );
    }
    return values;
}
