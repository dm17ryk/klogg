#include "actionsrepository.h"

#include <QDir>
#include <QFileInfo>

#include "actionsimportexport.h"
#include "actionsconfigparser.h"
#include "persistentinfo.h"

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
    return writeActionsConfigFile( storagePath(), actions, responses );
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
