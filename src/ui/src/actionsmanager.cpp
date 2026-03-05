#include "actionsmanager.h"

#include <algorithm>
#include <utility>

#include "actionsconfigparser.h"
#include "log.h"
#include "persistentinfo.h"
#include "startupprogress.h"

ActionsManager& ActionsManager::instance()
{
    static ActionsManager manager;
    return manager;
}

void ActionsManager::loadFromRepository()
{
    StartupProgress::advance( QObject::tr( "Loading actions repository" ) );
    const auto result = repository_.load();
    if ( !result.errors.isEmpty() ) {
        LOG_WARNING << "Failed to load actions: " << result.errors.join( "; " ).toStdString();
    }
    actions_ = result.actions;
    responses_ = result.responses;
    for ( const auto& action : actions_ ) {
        StartupProgress::advance( QObject::tr( "Loading action" ), action.name );
    }
    for ( const auto& response : responses_ ) {
        StartupProgress::advance( QObject::tr( "Loading response" ), response.name );
    }
    autoResponsesEnabled_
        = PersistentInfo::getSettings( app_settings{} )
              .value( "Actions/autoResponsesEnabled", true )
              .toBool();
    Q_EMIT actionsChanged();
    Q_EMIT responsesChanged();
    Q_EMIT autoResponsesEnabledChanged( autoResponsesEnabled_ );
}

ActionsImportResult ActionsManager::importFromDefinitions(
    QVector<ActionDefinition> actions,
    QVector<ResponseDefinition> responses )
{
    ActionsImportResult result;
    if ( !repository_.save( actions, responses ) ) {
        result.errors.push_back( tr( "Failed to save actions to settings." ) );
        return result;
    }

    actions_ = std::move( actions );
    responses_ = std::move( responses );
    result.ok = true;
    Q_EMIT actionsChanged();
    Q_EMIT responsesChanged();
    return result;
}

ActionsImportResult ActionsManager::importFromFile( const QString& path )
{
    ActionsImportResult result;
    ActionsConfigParser parser;
    const auto parsed = parser.parseFile( path );
    result.errors = parsed.errors;
    result.warnings = parsed.warnings;
    if ( !parsed.errors.isEmpty() ) {
        return result;
    }

    auto imported = importFromDefinitions( std::move( parsed.actions ),
                                           std::move( parsed.responses ) );
    imported.warnings = result.warnings;
    return imported;
}

const QVector<ActionDefinition>& ActionsManager::actions() const
{
    return actions_;
}

const QVector<ResponseDefinition>& ActionsManager::responses() const
{
    return responses_;
}

const ActionDefinition* ActionsManager::findActionById( int id ) const
{
    const auto it = std::find_if( actions_.begin(), actions_.end(),
                                  [ id ]( const ActionDefinition& action ) {
                                      return action.id == id;
                                  } );
    if ( it == actions_.end() ) {
        return nullptr;
    }
    return &(*it);
}

bool ActionsManager::setResponseEnabled( int id, bool enabled )
{
    auto it = std::find_if( responses_.begin(), responses_.end(),
                            [ id ]( const ResponseDefinition& response ) {
                                return response.id == id;
                            } );
    if ( it == responses_.end() ) {
        return false;
    }
    if ( it->enabled == enabled ) {
        return true;
    }
    it->enabled = enabled;
    if ( !repository_.save( actions_, responses_ ) ) {
        LOG_WARNING << "Failed to save response state for id " << id;
        return false;
    }
    Q_EMIT responsesChanged();
    return true;
}

bool ActionsManager::autoResponsesEnabled() const
{
    return autoResponsesEnabled_;
}

void ActionsManager::setAutoResponsesEnabled( bool enabled )
{
    if ( autoResponsesEnabled_ == enabled ) {
        return;
    }
    autoResponsesEnabled_ = enabled;
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.setValue( "Actions/autoResponsesEnabled", enabled );
    Q_EMIT autoResponsesEnabledChanged( enabled );
}
