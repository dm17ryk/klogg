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
QJsonObject actionToJson( const ActionDefinition& action )
{
    return QJsonObject::fromVariantMap( actionDefinitionToVariantMap( action ) );
}

QJsonObject responseToJson( const ResponseDefinition& response )
{
    return QJsonObject::fromVariantMap( responseDefinitionToVariantMap( response ) );
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
    root.insert( "version", 4 );
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
