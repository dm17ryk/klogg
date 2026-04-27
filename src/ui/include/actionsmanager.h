#pragma once

#include <QObject>

#include "actionsconfig.h"
#include "actionsimportexport.h"
#include "actionsrepository.h"

struct ActionsImportResult {
    bool ok = false;
    int added = 0;
    int updated = 0;
    int skipped = 0;
    QVector<ActionsImportConflict> conflicts;
    QStringList errors;
    QStringList warnings;
};

class ActionsManager : public QObject {
    Q_OBJECT
  public:
    static ActionsManager& instance();

    void loadFromRepository();
    ActionsImportResult importFromDefinitions( QVector<ActionDefinition> actions,
                                               QVector<ResponseDefinition> responses );
    ActionsImportResult importFromFile( const QString& path,
                                        ActionsImportFormat format = ActionsImportFormat::Json,
                                        ActionsConflictPolicy conflictPolicy
                                        = ActionsConflictPolicy::Fail );
    bool exportToFile( const QString& path,
                       QString* errorMessage = nullptr,
                       bool pretty = true ) const;

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

  Q_SIGNALS:
    void actionsChanged();
    void responsesChanged();
    void autoResponsesEnabledChanged( bool enabled );

  private:
    ActionsManager() = default;

    ActionsRepository repository_;
    QVector<ActionDefinition> actions_;
    QVector<ResponseDefinition> responses_;
    bool autoResponsesEnabled_ = true;
};
