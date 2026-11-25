#include "actionsmanager.h"

#include <QHash>
#include <QSettings>
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

// The settings key of a pack item's run-time id ("actions", "responses") or on/off state ("on").
QString packKey( const QString& packId, const char* what, int localId )
{
    return QStringLiteral( "PackActions/%1/%2/%3" )
        .arg( packId, QLatin1String( what ) )
        .arg( localId );
}

// A pack item's run-time id: the one kept in the settings, else the next free one, kept from now
// on, so a scenario or a response of the user's that names it finds it after a restart, an update
// or a disable and enable.
int packItemId( QSettings& settings, const QString& packId, bool response, int localId )
{
    const auto key = packKey( packId, response ? "responses" : "actions", localId );
    bool ok = false;
    const auto kept = settings.value( key ).toInt( &ok );
    if ( ok && kept >= PackItemIdBase ) {
        return kept;
    }
    const auto next
        = qMax( settings.value( QStringLiteral( "PackActions/nextId" ) ).toInt(), PackItemIdBase );
    settings.setValue( key, next );
    settings.setValue( QStringLiteral( "PackActions/nextId" ), next + 1 );
    return next;
}

// "<pack-id>/<local-id>" -> its parts; nothing when it is not one.
std::optional<std::pair<QString, int>> splitNamespacedId( const QString& text )
{
    const auto slash = text.lastIndexOf( '/' );
    if ( slash <= 0 ) {
        return std::nullopt;
    }
    bool ok = false;
    const auto local = text.mid( slash + 1 ).toInt( &ok );
    if ( !ok || local < 0 || QString::number( local ) != text.mid( slash + 1 ) ) {
        return std::nullopt;
    }
    return std::pair{ text.left( slash ), local };
}

// The listed items of a kind: none while the user's and the packs' would pass the limit.
template <typename Listed, typename Own>
QVector<Listed> listedPackItems( const QVector<Own>& own, const QVector<Listed>& packs )
{
    return own.size() + packs.size() > MaxListedActionItems ? QVector<Listed>{} : packs;
}
} // namespace

QString namespacedItemId( const QString& packId, int localId )
{
    return packId + QLatin1Char( '/' ) + QString::number( localId );
}

bool ActionsManager::validateResponseLinks( const ResponseDefinition& response,
                                            QString* errorMessage ) const
{
    // The user's actions and the listed packs' (a response of the user's may run a pack action).
    for ( const auto& step : response.response.steps ) {
        if ( findActionById( step.actionId ) == nullptr ) {
            setError( errorMessage,
                      QObject::tr( "Response linked action id %1 was not found." ).arg( step.actionId ) );
            return false;
        }
    }

    if ( response.response.hasActionId && response.response.actionId >= 0
         && findActionById( response.response.actionId ) == nullptr ) {
        setError( errorMessage,
                  QObject::tr( "Response linked action id %1 was not found." )
                      .arg( response.response.actionId ) );
        return false;
    }

    return true;
}

QString ActionsManager::packItemRefusal( int id, bool response ) const
{
    if ( response ) {
        const auto* listed = packResponse( id );
        return listed && findResponseById( id ) == &listed->response
                   ? tr( "Response %1 belongs to the pack \"%2\" (%3): copy it to change it as "
                         "yours, or edit the pack's item." )
                         .arg( id )
                         .arg( listed->packName,
                               namespacedItemId( listed->packId, listed->localId ) )
                   : QString{};
    }
    const auto* listed = packAction( id );
    return listed && findActionById( id ) == &listed->action
               ? tr( "Action %1 belongs to the pack \"%2\" (%3): copy it to change it as yours, "
                     "or edit the pack's item." )
                     .arg( id )
                     .arg( listed->packName, namespacedItemId( listed->packId, listed->localId ) )
               : QString{};
}

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
    // Ids from PackItemIdBase on are the configuration packs' (links to them stay: a response of
    // the user's may run a pack action).
    for ( const auto& action : actions ) {
        if ( action.id >= PackItemIdBase ) {
            result.errors.push_back( tr( "Action id %1 is kept for configuration packs (ids from "
                                         "%2 on)." )
                                         .arg( action.id )
                                         .arg( PackItemIdBase ) );
        }
    }
    for ( const auto& response : responses ) {
        if ( response.id >= PackItemIdBase ) {
            result.errors.push_back( tr( "Response id %1 is kept for configuration packs (ids "
                                         "from %2 on)." )
                                         .arg( response.id )
                                         .arg( PackItemIdBase ) );
        }
    }
    if ( !result.errors.isEmpty() ) {
        return result;
    }
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
    if ( it != actions_.cend() ) {
        return &( *it );
    }
    const auto* listed = packAction( id );
    return listed ? &listed->action : nullptr;
}

const ResponseDefinition* ActionsManager::findResponseById( int id ) const
{
    const auto it = std::find_if( responses_.cbegin(), responses_.cend(),
                                  [ id ]( const ResponseDefinition& response ) {
                                      return response.id == id;
                                  } );
    if ( it != responses_.cend() ) {
        return &( *it );
    }
    const auto* listed = packResponse( id );
    return listed ? &listed->response : nullptr;
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
    if ( it != responses_.cend() ) {
        return &( *it );
    }
    // Then the packs' (the user's first, as they are matched).
    if ( listedPackItems( responses_, packResponses_ ).isEmpty() ) {
        return nullptr;
    }
    const auto listed = std::find_if(
        packResponses_.cbegin(), packResponses_.cend(),
        [ &trimmedName ]( const ListedResponse& item ) {
            return item.response.name.compare( trimmedName, Qt::CaseInsensitive ) == 0;
        } );
    return listed == packResponses_.cend() ? nullptr : &listed->response;
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
    if ( action.id >= PackItemIdBase ) {
        setError(
            errorMessage,
            tr( "Action ids from %1 on are kept for configuration packs." ).arg( PackItemIdBase ) );
        return false;
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
    if ( const auto refusal = packItemRefusal( id, false ); !refusal.isEmpty() ) {
        setError( errorMessage, refusal );
        return false;
    }
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
    if ( const auto refusal = packItemRefusal( id, false ); !refusal.isEmpty() ) {
        setError( errorMessage, refusal );
        return false;
    }
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
    if ( const auto refusal = packItemRefusal( id, false ); !refusal.isEmpty() ) {
        setError( errorMessage, refusal );
        return false;
    }
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
    if ( response.id >= PackItemIdBase ) {
        setError( errorMessage, tr( "Response ids from %1 on are kept for configuration packs." )
                                    .arg( PackItemIdBase ) );
        return false;
    }
    response.order = static_cast<int>( responses_.size() );
    if ( !validateResponseDefinition( response, errorMessage ) ) {
        return false;
    }
    if ( !validateResponseLinks( response, errorMessage ) ) {
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
    if ( const auto refusal = packItemRefusal( id, true ); !refusal.isEmpty() ) {
        setError( errorMessage, refusal );
        return false;
    }
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
    if ( !validateResponseLinks( response, errorMessage ) ) {
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
    if ( const auto refusal = packItemRefusal( id, true ); !refusal.isEmpty() ) {
        setError( errorMessage, refusal );
        return false;
    }
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
    if ( const auto refusal = packItemRefusal( id, true ); !refusal.isEmpty() ) {
        setError( errorMessage, refusal );
        return false;
    }
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
        // A pack's response: on or off is the user's state, kept per pack and item (E16c3).
        if ( packResponse( id ) == nullptr ) {
            return false;
        }
        for ( auto& listed : packResponses_ ) {
            if ( listed.response.id == id && listed.response.enabled != enabled ) {
                listed.response.enabled = enabled;
                PersistentInfo::getSettings( app_settings{} )
                    .setValue( packKey( listed.packId, "on", listed.localId ), enabled );
                Q_EMIT responsesChanged();
            }
        }
        return true;
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

void ActionsManager::setPackItems( const QVector<PackActionItems>& packs )
{
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    QVector<ListedAction> actions;
    QVector<ListedResponse> responses;
    for ( const auto& pack : packs ) {
        QHash<int, int> runtime; // the pack's action id -> its run-time id
        for ( const auto& action : pack.actions ) {
            ListedAction listed{ action, pack.packId, pack.packName, action.id };
            listed.action.id = packItemId( settings, pack.packId, false, action.id );
            runtime.insert( action.id, listed.action.id );
            actions.append( listed );
        }
        // A pack response runs its own pack's actions only (the pack check refuses other links;
        // one that is not there runs nothing).
        const auto relink = [ &runtime ]( int localId ) { return runtime.value( localId, -1 ); };
        for ( const auto& response : pack.responses ) {
            ListedResponse listed{ response, pack.packId, pack.packName, response.id };
            listed.response.id = packItemId( settings, pack.packId, true, response.id );
            auto& run = listed.response.response;
            if ( run.hasActionId && run.actionId >= 0 ) {
                run.actionId = relink( run.actionId );
            }
            for ( auto& step : run.steps ) {
                step.actionId = relink( step.actionId );
            }
            // Every pack response starts off: it sends to a device without a click.
            listed.response.enabled
                = settings.value( packKey( pack.packId, "on", response.id ), false ).toBool();
            responses.append( listed );
        }
    }
    packActions_ = std::move( actions );
    packResponses_ = std::move( responses );
    LOG_INFO << "Pack actions applied actions=" << packActions_.size()
             << " responses=" << packResponses_.size();
    if ( actions_.size() + packActions_.size() > MaxListedActionItems
         || responses_.size() + packResponses_.size() > MaxListedActionItems ) {
        LOG_WARNING << "Pack actions or responses not listed: own actions=" << actions_.size()
                    << " responses=" << responses_.size() << " limit=" << MaxListedActionItems;
    }
    Q_EMIT actionsChanged();
    Q_EMIT responsesChanged();
}

QVector<ListedAction> ActionsManager::packActions() const
{
    return listedPackItems( actions_, packActions_ );
}

QVector<ListedResponse> ActionsManager::packResponses() const
{
    return listedPackItems( responses_, packResponses_ );
}

const ListedAction* ActionsManager::packAction( int id ) const
{
    if ( id < PackItemIdBase || listedPackItems( actions_, packActions_ ).isEmpty() ) {
        return nullptr;
    }
    const auto it
        = std::find_if( packActions_.cbegin(), packActions_.cend(),
                        [ id ]( const ListedAction& listed ) { return listed.action.id == id; } );
    return it == packActions_.cend() ? nullptr : &( *it );
}

const ListedResponse* ActionsManager::packResponse( int id ) const
{
    if ( id < PackItemIdBase || listedPackItems( responses_, packResponses_ ).isEmpty() ) {
        return nullptr;
    }
    const auto it = std::find_if(
        packResponses_.cbegin(), packResponses_.cend(),
        [ id ]( const ListedResponse& listed ) { return listed.response.id == id; } );
    return it == packResponses_.cend() ? nullptr : &( *it );
}

QVector<ResponseDefinition> ActionsManager::effectiveResponses() const
{
    auto responses = responses_;
    for ( const auto& listed : packResponses() ) {
        responses.append( listed.response );
    }
    return responses;
}

std::optional<int> ActionsManager::resolvePackAction( const QString& namespacedId ) const
{
    const auto parts = splitNamespacedId( namespacedId );
    if ( !parts ) {
        return std::nullopt;
    }
    for ( const auto& listed : packActions() ) {
        if ( listed.packId == parts->first && listed.localId == parts->second ) {
            return listed.action.id;
        }
    }
    return std::nullopt;
}

std::optional<int> ActionsManager::resolvePackResponse( const QString& namespacedId ) const
{
    const auto parts = splitNamespacedId( namespacedId );
    if ( !parts ) {
        return std::nullopt;
    }
    for ( const auto& listed : packResponses() ) {
        if ( listed.packId == parts->first && listed.localId == parts->second ) {
            return listed.response.id;
        }
    }
    return std::nullopt;
}

int ActionsManager::setPackResponsesEnabled( const QString& packId, bool enabled )
{
    if ( listedPackItems( responses_, packResponses_ ).isEmpty() ) {
        return 0;
    }
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    int changed = 0;
    for ( auto& listed : packResponses_ ) {
        if ( listed.packId == packId && listed.response.enabled != enabled ) {
            listed.response.enabled = enabled;
            settings.setValue( packKey( listed.packId, "on", listed.localId ), enabled );
            ++changed;
        }
    }
    if ( changed > 0 ) {
        LOG_INFO << "Pack responses turned " << ( enabled ? "on" : "off" )
                 << " pack=" << packId.toStdString() << " count=" << changed;
        Q_EMIT responsesChanged();
    }
    return changed;
}

std::optional<int> ActionsManager::copyPackAction( int id, QString* errorMessage )
{
    const auto* listed = packAction( id );
    if ( listed == nullptr ) {
        setError( errorMessage, tr( "There is no pack action %1." ).arg( id ) );
        return std::nullopt;
    }
    auto copy = listed->action;
    copy.id = nextActionId();
    if ( copy.id >= PackItemIdBase ) {
        setError( errorMessage,
                  tr( "There is no free action id below %1." ).arg( PackItemIdBase ) );
        return std::nullopt;
    }
    copy.order = static_cast<int>( actions_.size() );
    auto updatedActions = actions_;
    updatedActions.push_back( copy );
    normalizeActionDefinitions( &updatedActions );
    if ( !repository_.save( updatedActions, responses_ ) ) {
        setError( errorMessage, tr( "Failed to save actions to settings." ) );
        return std::nullopt;
    }
    actions_ = std::move( updatedActions );
    LOG_INFO << "Pack action copied pack=" << listed->packId.toStdString()
             << " local=" << listed->localId << " id=" << copy.id;
    Q_EMIT actionsChanged();
    return copy.id;
}

std::optional<int> ActionsManager::copyPackResponse( int id, QString* errorMessage )
{
    const auto* listed = packResponse( id );
    if ( listed == nullptr ) {
        setError( errorMessage, tr( "There is no pack response %1." ).arg( id ) );
        return std::nullopt;
    }
    // The pack actions it runs become the user's too, so the copy runs the user's own actions.
    auto updatedActions = actions_;
    auto nextAction = nextActionId();
    QHash<int, int> copies; // pack action run-time id -> its copy's id
    const auto copyLink = [ & ]( int actionId ) {
        const auto* action = actionId >= 0 ? packAction( actionId ) : nullptr;
        if ( action == nullptr ) {
            return actionId;
        }
        if ( !copies.contains( actionId ) ) {
            auto copy = action->action;
            copy.id = nextAction++;
            copy.order = static_cast<int>( updatedActions.size() );
            updatedActions.push_back( copy );
            copies.insert( actionId, copy.id );
        }
        return copies.value( actionId );
    };
    auto response = listed->response;
    auto& run = response.response;
    if ( run.hasActionId ) {
        run.actionId = copyLink( run.actionId );
    }
    for ( auto& step : run.steps ) {
        step.actionId = copyLink( step.actionId );
    }
    response.id = nextResponseId();
    response.order = static_cast<int>( responses_.size() );
    // The copy starts off, as the pack's response did: while both are on, both would answer.
    response.enabled = false;
    if ( nextAction > PackItemIdBase || response.id >= PackItemIdBase ) {
        setError( errorMessage, tr( "There is no free id below %1." ).arg( PackItemIdBase ) );
        return std::nullopt;
    }
    auto updatedResponses = responses_;
    updatedResponses.push_back( response );
    normalizeActionDefinitions( &updatedActions );
    normalizeResponseDefinitions( &updatedResponses );
    if ( !repository_.save( updatedActions, updatedResponses ) ) {
        setError( errorMessage, tr( "Failed to save responses to settings." ) );
        return std::nullopt;
    }
    LOG_INFO << "Pack response copied pack=" << listed->packId.toStdString()
             << " local=" << listed->localId << " id=" << response.id
             << " actions=" << copies.size();
    actions_ = std::move( updatedActions );
    responses_ = std::move( updatedResponses );
    if ( !copies.isEmpty() ) {
        Q_EMIT actionsChanged();
    }
    Q_EMIT responsesChanged();
    return response.id;
}
