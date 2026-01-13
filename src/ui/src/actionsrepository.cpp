#include "actionsrepository.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "actionsconfigparser.h"
#include "persistentinfo.h"

namespace {
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
    obj.insert( "enabled", action.enabled );
    obj.insert( "name", action.name );
    obj.insert( "description", action.description );
    obj.insert( "sequence", sequenceToJson( action.sequence ) );

    QJsonObject params;
    params.insert( "repeat", action.parameters.repeat );
    params.insert( "delay", action.parameters.delay );
    obj.insert( "parameters", params );
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
    obj.insert( "enabled", response.enabled );
    obj.insert( "name", response.name );
    obj.insert( "description", response.description );
    obj.insert( "match", matchObj );
    obj.insert( "response", responseObj );
    return obj;
}
} // namespace

ActionsParseResult ActionsRepository::load() const
{
    ActionsConfigParser parser;
    const auto path = storagePath();
    if ( !QFileInfo::exists( path ) ) {
        return {};
    }
    return parser.parseFile( path );
}

bool ActionsRepository::save( const QVector<ActionDefinition>& actions,
                              const QVector<ResponseDefinition>& responses ) const
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
    root.insert( "version", 1 );
    root.insert( "actions", actionsArray );
    root.insert( "responses", responsesArray );

    const QJsonDocument doc( root );
    QSaveFile file( storagePath() );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        return false;
    }
    file.write( doc.toJson( QJsonDocument::Indented ) );
    return file.commit();
}

QString ActionsRepository::storagePath() const
{
    const auto& settings = PersistentInfo::getSettings( app_settings{} );
    const QFileInfo settingsInfo( settings.fileName() );
    QDir dir = settingsInfo.absoluteDir();
    if ( !dir.exists() ) {
        dir.mkpath( "." );
    }
    return dir.filePath( "actions.json" );
}
