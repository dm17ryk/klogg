#include "actionsmanager.h"

#include <algorithm>
#include <utility>

#include "actionsconfigparser.h"
#include "log.h"
#include "persistentinfo.h"
#include "startupprogress.h"

namespace {
template <typename Definition>
int nextDefinitionId( const QVector<Definition>& definitions )
{
    int maxId = 0;
    for ( const auto& definition : definitions ) {
        maxId = qMax( maxId, definition.id );
    }
    return maxId + 1;
}

void setError( QString* errorMessage, const QString& message )
{
    if ( errorMessage != nullptr ) {
        *errorMessage = message;
    }
}

bool validateResponseActionIds( const QVector<ActionDefinition>& actions,
                                const ResponseDefinition& response,
                                QString* errorMessage )
{
    const auto hasActionId = [ &actions ]( int actionId ) {
        return std::any_of( actions.cbegin(), actions.cend(),
                            [ actionId ]( const ActionDefinition& action ) {
                                return action.id == actionId;
                            } );
    };

    for ( const auto& step : response.response.steps ) {
        if ( !hasActionId( step.actionId ) ) {
            setError( errorMessage,
                      QObject::tr( "Response linked action id %1 was not found." ).arg( step.actionId ) );
            return false;
        }
    }

    if ( response.response.hasActionId && response.response.actionId >= 0
         && !hasActionId( response.response.actionId ) ) {
        setError( errorMessage,
                  QObject::tr( "Response linked action id %1 was not found." )
                      .arg( response.response.actionId ) );
        return false;
    }

    return true;
}
} // namespace

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
    normalizeActionDefinitions( &actions_ );
    normalizeResponseDefinitions( &responses_ );
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
    normalizeActionDefinitions( &actions );
    normalizeResponseDefinitions( &responses );
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
    const auto it = std::find_if( actions_.cbegin(), actions_.cend(),
                                  [ id ]( const ActionDefinition& action ) {
                                      return action.id == id;
                                  } );
    return it == actions_.cend() ? nullptr : &(*it);
}

const ResponseDefinition* ActionsManager::findResponseById( int id ) const
{
    const auto it = std::find_if( responses_.cbegin(), responses_.cend(),
                                  [ id ]( const ResponseDefinition& response ) {
                                      return response.id == id;
                                  } );
    return it == responses_.cend() ? nullptr : &(*it);
}

const ResponseDefinition* ActionsManager::findResponseByName( const QString& name ) const
{
    const auto trimmedName = name.trimmed();
    if ( trimmedName.isEmpty() ) {
        return nullptr;
    }

    const auto it = std::find_if( responses_.cbegin(), responses_.cend(),
                                  [ &trimmedName ]( const ResponseDefinition& response ) {
                                      return response.name.compare( trimmedName, Qt::CaseInsensitive ) == 0;
                                  } );
    return it == responses_.cend() ? nullptr : &(*it);
}

int ActionsManager::nextActionId() const
{
    return nextDefinitionId( actions_ );
}

int ActionsManager::nextResponseId() const
{
    return nextDefinitionId( responses_ );
}

bool ActionsManager::createAction( ActionDefinition action, QString* errorMessage )
{
    if ( action.id <= 0 ) {
        action.id = nextActionId();
    }
    action.order = static_cast<int>( actions_.size() );
    if ( !validateActionDefinition( action, errorMessage ) ) {
        return false;
    }
    if ( findActionById( action.id ) != nullptr ) {
        setError( errorMessage, tr( "Action id %1 already exists." ).arg( action.id ) );
        return false;
    }

    auto updatedActions = actions_;
    updatedActions.push_back( action );
    normalizeActionDefinitions( &updatedActions );
    if ( !repository_.save( updatedActions, responses_ ) ) {
        setError( errorMessage, tr( "Failed to save actions to settings." ) );
        return false;
    }

    actions_ = std::move( updatedActions );
    Q_EMIT actionsChanged();
    return true;
}

bool ActionsManager::updateAction( int id, ActionDefinition action, QString* errorMessage )
{
    auto updatedActions = actions_;
    const auto it = std::find_if( updatedActions.begin(), updatedActions.end(),
                                  [ id ]( const ActionDefinition& candidate ) {
                                      return candidate.id == id;
                                  } );
    if ( it == updatedActions.end() ) {
        setError( errorMessage, tr( "Action id %1 was not found." ).arg( id ) );
        return false;
    }

    action.id = id;
    action.order = it->order;
    if ( !validateActionDefinition( action, errorMessage ) ) {
        return false;
    }
    *it = action;
    normalizeActionDefinitions( &updatedActions );
    if ( !repository_.save( updatedActions, responses_ ) ) {
        setError( errorMessage, tr( "Failed to save actions to settings." ) );
        return false;
    }

    actions_ = std::move( updatedActions );
    Q_EMIT actionsChanged();
    return true;
}

bool ActionsManager::deleteAction( int id, QString* errorMessage )
{
    auto updatedActions = actions_;
    const auto oldSize = updatedActions.size();
    updatedActions.erase( std::remove_if( updatedActions.begin(), updatedActions.end(),
                                          [ id ]( const ActionDefinition& action ) {
                                              return action.id == id;
                                          } ),
                          updatedActions.end() );
    if ( updatedActions.size() == oldSize ) {
        setError( errorMessage, tr( "Action id %1 was not found." ).arg( id ) );
        return false;
    }

    auto updatedResponses = responses_;
    for ( auto& response : updatedResponses ) {
        response.response.steps.erase(
            std::remove_if( response.response.steps.begin(), response.response.steps.end(),
                            [ id ]( const ResponseActionStep& step ) {
                                return step.actionId == id;
                            } ),
            response.response.steps.end() );
        response.response.hasActionId = !response.response.steps.isEmpty();
        response.response.actionId = response.response.hasActionId ? response.response.steps.front().actionId
                                                                  : -1;
    }

    normalizeActionDefinitions( &updatedActions );
    normalizeResponseDefinitions( &updatedResponses );
    if ( !repository_.save( updatedActions, updatedResponses ) ) {
        setError( errorMessage, tr( "Failed to save actions to settings." ) );
        return false;
    }

    actions_ = std::move( updatedActions );
    responses_ = std::move( updatedResponses );
    Q_EMIT actionsChanged();
    Q_EMIT responsesChanged();
    return true;
}

bool ActionsManager::moveAction( int id, int offset, QString* errorMessage )
{
    auto updatedActions = actions_;
    const auto it = std::find_if( updatedActions.begin(), updatedActions.end(),
                                  [ id ]( const ActionDefinition& action ) {
                                      return action.id == id;
                                  } );
    if ( it == updatedActions.end() ) {
        setError( errorMessage, tr( "Action id %1 was not found." ).arg( id ) );
        return false;
    }

    const auto currentIndex = static_cast<int>( std::distance( updatedActions.begin(), it ) );
    const auto newIndex = qBound( 0, currentIndex + offset, updatedActions.size() - 1 );
    if ( newIndex == currentIndex ) {
        return true;
    }

    updatedActions.move( currentIndex, newIndex );
    normalizeActionDefinitions( &updatedActions );
    if ( !repository_.save( updatedActions, responses_ ) ) {
        setError( errorMessage, tr( "Failed to save actions to settings." ) );
        return false;
    }

    actions_ = std::move( updatedActions );
    Q_EMIT actionsChanged();
    return true;
}

bool ActionsManager::createResponse( ResponseDefinition response, QString* errorMessage )
{
    if ( response.id <= 0 ) {
        response.id = nextResponseId();
    }
    response.order = static_cast<int>( responses_.size() );
    if ( !validateResponseDefinition( response, errorMessage ) ) {
        return false;
    }
    if ( !validateResponseActionIds( actions_, response, errorMessage ) ) {
        return false;
    }
    if ( findResponseById( response.id ) != nullptr ) {
        setError( errorMessage, tr( "Response id %1 already exists." ).arg( response.id ) );
        return false;
    }

    auto updatedResponses = responses_;
    updatedResponses.push_back( response );
    normalizeResponseDefinitions( &updatedResponses );
    if ( !repository_.save( actions_, updatedResponses ) ) {
        setError( errorMessage, tr( "Failed to save responses to settings." ) );
        return false;
    }

    responses_ = std::move( updatedResponses );
    Q_EMIT responsesChanged();
    return true;
}

bool ActionsManager::updateResponse( int id, ResponseDefinition response, QString* errorMessage )
{
    auto updatedResponses = responses_;
    const auto it = std::find_if( updatedResponses.begin(), updatedResponses.end(),
                                  [ id ]( const ResponseDefinition& candidate ) {
                                      return candidate.id == id;
                                  } );
    if ( it == updatedResponses.end() ) {
        setError( errorMessage, tr( "Response id %1 was not found." ).arg( id ) );
        return false;
    }

    response.id = id;
    response.order = it->order;
    if ( !validateResponseDefinition( response, errorMessage ) ) {
        return false;
    }
    if ( !validateResponseActionIds( actions_, response, errorMessage ) ) {
        return false;
    }
    *it = response;
    normalizeResponseDefinitions( &updatedResponses );
    if ( !repository_.save( actions_, updatedResponses ) ) {
        setError( errorMessage, tr( "Failed to save responses to settings." ) );
        return false;
    }

    responses_ = std::move( updatedResponses );
    Q_EMIT responsesChanged();
    return true;
}

bool ActionsManager::deleteResponse( int id, QString* errorMessage )
{
    auto updatedResponses = responses_;
    const auto oldSize = updatedResponses.size();
    updatedResponses.erase( std::remove_if( updatedResponses.begin(), updatedResponses.end(),
                                            [ id ]( const ResponseDefinition& response ) {
                                                return response.id == id;
                                            } ),
                            updatedResponses.end() );
    if ( updatedResponses.size() == oldSize ) {
        setError( errorMessage, tr( "Response id %1 was not found." ).arg( id ) );
        return false;
    }

    normalizeResponseDefinitions( &updatedResponses );
    if ( !repository_.save( actions_, updatedResponses ) ) {
        setError( errorMessage, tr( "Failed to save responses to settings." ) );
        return false;
    }

    responses_ = std::move( updatedResponses );
    Q_EMIT responsesChanged();
    return true;
}

bool ActionsManager::moveResponse( int id, int offset, QString* errorMessage )
{
    auto updatedResponses = responses_;
    const auto it = std::find_if( updatedResponses.begin(), updatedResponses.end(),
                                  [ id ]( const ResponseDefinition& response ) {
                                      return response.id == id;
                                  } );
    if ( it == updatedResponses.end() ) {
        setError( errorMessage, tr( "Response id %1 was not found." ).arg( id ) );
        return false;
    }

    const auto currentIndex = static_cast<int>( std::distance( updatedResponses.begin(), it ) );
    const auto newIndex = qBound( 0, currentIndex + offset, updatedResponses.size() - 1 );
    if ( newIndex == currentIndex ) {
        return true;
    }

    updatedResponses.move( currentIndex, newIndex );
    normalizeResponseDefinitions( &updatedResponses );
    if ( !repository_.save( actions_, updatedResponses ) ) {
        setError( errorMessage, tr( "Failed to save responses to settings." ) );
        return false;
    }

    responses_ = std::move( updatedResponses );
    Q_EMIT responsesChanged();
    return true;
}

bool ActionsManager::setResponseEnabled( int id, bool enabled )
{
    auto updatedResponses = responses_;
    auto it = std::find_if( updatedResponses.begin(), updatedResponses.end(),
                            [ id ]( const ResponseDefinition& response ) {
                                return response.id == id;
                            } );
    if ( it == updatedResponses.end() ) {
        return false;
    }
    if ( it->enabled == enabled ) {
        return true;
    }
    it->enabled = enabled;
    if ( !repository_.save( actions_, updatedResponses ) ) {
        LOG_WARNING << "Failed to save response state for id " << id;
        return false;
    }
    responses_ = std::move( updatedResponses );
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
