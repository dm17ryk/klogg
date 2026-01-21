#pragma once

#include <QAbstractTableModel>

#include "actionsconfig.h"

class ActionsTableModel : public QAbstractTableModel {
    Q_OBJECT
  public:
    enum Roles {
        ActionIdRole = Qt::UserRole + 1,
        SendEnabledRole,
        SequenceValueRole
    };

    explicit ActionsTableModel( QObject* parent = nullptr );

    int rowCount( const QModelIndex& parent = {} ) const override;
    int columnCount( const QModelIndex& parent = {} ) const override;
    QVariant data( const QModelIndex& index, int role ) const override;
    QVariant headerData( int section,
                         Qt::Orientation orientation,
                         int role = Qt::DisplayRole ) const override;
    Qt::ItemFlags flags( const QModelIndex& index ) const override;

    void refresh();
    void setSendAvailable( bool available );
    const ActionDefinition* actionAt( int row ) const;

  private:
    QString previewSequence( const ActionSequence& sequence ) const;
    QString tooltipForAction( const ActionDefinition& action ) const;

    QVector<ActionDefinition> actions_;
    bool sendAvailable_ = false;
};
