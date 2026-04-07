#pragma once

#include <QObject>

#include "actionsconfig.h"
#include "actionsrepository.h"

struct ActionsImportResult {
    bool ok = false;
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
