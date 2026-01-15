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
