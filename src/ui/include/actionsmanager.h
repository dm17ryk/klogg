#pragma once

#include <QObject>
#include <optional>

#include "actionsconfig.h"
#include "actionsrepository.h"

struct ActionsImportResult {
    bool ok = false;
    QStringList errors;
    QStringList warnings;
};

// Configuration packs (E16c3, docs/plugin-repositories.md 4.5). An enabled pack's action or
// response keeps its pack-local id in the pack; at run time it has an id from PackItemIdBase on,
// assigned once per (pack, kind, local id) and kept in the settings. The user's items keep ids
// below it.
constexpr int PackItemIdBase = 1000000;
// The most actions, and the most responses, the user's and the enabled packs' make together:
// past it the packs' give way (they are not listed), and enabling a pack past it is refused.
constexpr int MaxListedActionItems = 4096;

// An enabled pack's action, with its run-time id.
struct ListedAction {
    ActionDefinition action; // action.id is the run-time id
    QString packId;
    QString packName;
    int localId = -1;
};

// An enabled pack's automatic response: its links name its own pack's actions by run-time id, and
// whether it is on is the user's state (every pack response starts off).
struct ListedResponse {
    ResponseDefinition response; // response.id is the run-time id
    QString packId;
    QString packName;
    int localId = -1;
};

// What an enabled pack brings, as read from its folder (pack-local ids and links).
struct PackActionItems {
    QString packId;
    QString packName;
    QVector<ActionDefinition> actions;
    QVector<ResponseDefinition> responses;
};

// "<pack-id>/<local-id>": a pack item's name that is the same on every PC.
QString namespacedItemId( const QString& packId, int localId );

class ActionsManager : public QObject {
    Q_OBJECT
  public:
    static ActionsManager& instance();

    void loadFromRepository();
    ActionsImportResult importFromDefinitions( QVector<ActionDefinition> actions,
                                               QVector<ResponseDefinition> responses );
    ActionsImportResult importFromFile( const QString& path );

    const QVector<ActionDefinition>& actions() const;
    const QVector<ResponseDefinition>& responses() const;

    const ActionDefinition* findActionById( int id ) const;
    const ResponseDefinition* findResponseById( int id ) const;
    const ResponseDefinition* findResponseByName( const QString& name ) const;
    int nextActionId() const;
    int nextResponseId() const;
    bool createAction( ActionDefinition action, QString* errorMessage = nullptr );
    bool updateAction( int id, ActionDefinition action, QString* errorMessage = nullptr );
    bool deleteAction( int id, QString* errorMessage = nullptr );
    bool moveAction( int id, int offset, QString* errorMessage = nullptr );
    bool createResponse( ResponseDefinition response, QString* errorMessage = nullptr );
    bool updateResponse( int id, ResponseDefinition response, QString* errorMessage = nullptr );
    bool deleteResponse( int id, QString* errorMessage = nullptr );
    bool moveResponse( int id, int offset, QString* errorMessage = nullptr );
    bool setResponseEnabled( int id, bool enabled );
    bool autoResponsesEnabled() const;
    void setAutoResponsesEnabled( bool enabled );

    // Configuration packs (E16c3). The enabled packs' items, in pack order: their ids become
    // run-time ids, a response's links follow, and a response is on as the user left it (off
    // until turned on). actions() and responses() stay the user's own (saved and exported).
    void setPackItems( const QVector<PackActionItems>& packs );
    // The packs' listed actions and responses (none of a kind while the user's and theirs would
    // pass MaxListedActionItems).
    QVector<ListedAction> packActions() const;
    QVector<ListedResponse> packResponses() const;
    const ListedAction* packAction( int id ) const;
    const ListedResponse* packResponse( int id ) const;
    // The responses an incoming line is matched against, in order: the user's, then the packs'.
    QVector<ResponseDefinition> effectiveResponses() const;
    // The run-time id of an enabled pack's action or response named "<pack-id>/<local-id>".
    std::optional<int> resolvePackAction( const QString& namespacedId ) const;
    std::optional<int> resolvePackResponse( const QString& namespacedId ) const;
    // Turns every response of an enabled pack on or off; returns how many changed.
    int setPackResponsesEnabled( const QString& packId, bool enabled );
    // Copy to My Actions / My Responses: the user's own copy with a new id. A response's linked
    // pack actions are copied with it and its links name the copies. The copy's id, or nothing
    // with `errorMessage`.
    std::optional<int> copyPackAction( int id, QString* errorMessage = nullptr );
    std::optional<int> copyPackResponse( int id, QString* errorMessage = nullptr );

  Q_SIGNALS:
    void actionsChanged();
    void responsesChanged();
    void autoResponsesEnabledChanged( bool enabled );

  private:
    ActionsManager() = default;

    // Why the user's item `id` cannot be changed or added as theirs (a pack's, or in the pack
    // range), or an empty string.
    QString packItemRefusal( int id, bool response ) const;
    bool validateResponseLinks( const ResponseDefinition& response, QString* errorMessage ) const;
    // A response of the user's that runs pack actions: each such step names its action
    // ("<pack-id>/<local-id>") and runs that action's run-time id here, or nothing while no
    // enabled pack has it. Returns the names it could not find.
    QStringList linkPackSteps( ResponseDefinition* response ) const;
    // Gives the enabled packs' items their run-time ids (never one of the user's).
    void applyPackItems();

    ActionsRepository repository_;
    QVector<ActionDefinition> actions_;
    QVector<ResponseDefinition> responses_;
    QVector<PackActionItems> packSources_;
    QVector<ListedAction> packActions_;
    QVector<ListedResponse> packResponses_;
    bool autoResponsesEnabled_ = true;
};
