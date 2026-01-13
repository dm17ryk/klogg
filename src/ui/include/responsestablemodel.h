#pragma once

#include <QAbstractTableModel>

#include "actionsconfig.h"

class ResponsesTableModel : public QAbstractTableModel {
    Q_OBJECT
  public:
    enum Roles {
        ResponseIdRole = Qt::UserRole + 1,
        MatchValueRole,
        MatchTypeRole
    };

    explicit ResponsesTableModel( QObject* parent = nullptr );

    int rowCount( const QModelIndex& parent = {} ) const override;
    int columnCount( const QModelIndex& parent = {} ) const override;
    QVariant data( const QModelIndex& index, int role ) const override;
    QVariant headerData( int section,
                         Qt::Orientation orientation,
                         int role = Qt::DisplayRole ) const override;
    Qt::ItemFlags flags( const QModelIndex& index ) const override;
    bool setData( const QModelIndex& index, const QVariant& value, int role ) override;

    void refresh();

  private:
    QString previewMatch( const ResponseMatchDefinition& match ) const;
    QString tooltipForResponse( const ResponseDefinition& response ) const;

    QVector<ResponseDefinition> responses_;
};
