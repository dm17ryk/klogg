#pragma once

#include <QAbstractTableModel>

#include "actionsconfig.h"

class ActionsTableModel : public QAbstractTableModel {
    Q_OBJECT
  public:
    enum Roles {
        ActionIdRole = Qt::UserRole + 1,
        SendEnabledRole,
        SequenceValueRole,
        PackIdRole,      // the pack's ID, empty for the user's own action
        NamespacedIdRole // "<pack-id>/<local-id>", empty for the user's own action
    };
    // Where a row comes from: the user's (packId empty) or an enabled configuration pack's (E16c3),
    // read only there and named "<pack-id>/<local-id>".
    struct Origin {
        QString packId;
        QString packName;
        int localId = -1;
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
    // The pack the row's action comes from (packId empty: the user's own).
    Origin originAt( int row ) const;

  private:
    QString previewSequence( const ActionSequence& sequence ) const;
    QString tooltipForAction( const ActionDefinition& action ) const;

    QVector<ActionDefinition> actions_;
    QVector<Origin> origins_;
    bool sendAvailable_ = false;
};
