#include "actionsmanager.h"

#include <algorithm>

#include "actionsconfigparser.h"
#include "log.h"

ActionsManager& ActionsManager::instance()
{
    static ActionsManager manager;
    return manager;
}

void ActionsManager::loadFromRepository()
{
    const auto result = repository_.load();
    if ( !result.errors.isEmpty() ) {
        LOG_WARNING << "Failed to load actions: " << result.errors.join( "; " ).toStdString();
    }
    actions_ = result.actions;
    responses_ = result.responses;
    Q_EMIT actionsChanged();
    Q_EMIT responsesChanged();
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

    actions_ = parsed.actions;
    responses_ = parsed.responses;
    result.ok = repository_.save( actions_, responses_ );
    if ( !result.ok ) {
        result.errors.push_back( tr( "Failed to save actions to settings." ) );
        return result;
    }

    Q_EMIT actionsChanged();
    Q_EMIT responsesChanged();
    return result;
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
