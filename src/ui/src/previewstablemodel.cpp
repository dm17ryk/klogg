#include "previewstablemodel.h"

namespace {
constexpr int NameColumn = 0;
constexpr int PatternColumn = 1;
constexpr int EnabledColumn = 2;
} // namespace

PreviewsTableModel::PreviewsTableModel( QObject* parent )
    : QAbstractTableModel( parent )
{
}

int PreviewsTableModel::rowCount( const QModelIndex& parent ) const
{
    if ( parent.isValid() ) {
        return 0;
    }
    return static_cast<int>( PreviewManager::instance().all().size() );
}

int PreviewsTableModel::columnCount( const QModelIndex& parent ) const
{
    if ( parent.isValid() ) {
        return 0;
    }
    return 3;
}

QVariant PreviewsTableModel::data( const QModelIndex& index, int role ) const
{
    if ( !index.isValid() ) {
        return {};
    }

    const auto& previews = PreviewManager::instance().all();
    if ( index.row() < 0 || index.row() >= previews.size() ) {
        return {};
    }

    const auto& preview = previews.at( index.row() );

    if ( role == Qt::DisplayRole ) {
        switch ( index.column() ) {
        case NameColumn:
            return preview.name;
        case PatternColumn:
            return preview.regex;
        default:
            return {};
        }
    }

    if ( role == Qt::CheckStateRole && index.column() == EnabledColumn ) {
        return preview.enabled ? Qt::Checked : Qt::Unchecked;
    }

    return {};
}

QVariant PreviewsTableModel::headerData( int section, Qt::Orientation orientation,
                                         int role ) const
{
    if ( role != Qt::DisplayRole || orientation != Qt::Horizontal ) {
        return {};
    }

    switch ( section ) {
    case NameColumn:
        return tr( "Name" );
    case PatternColumn:
        return tr( "Pattern" );
    case EnabledColumn:
        return tr( "Enabled" );
    default:
        return {};
    }
}

Qt::ItemFlags PreviewsTableModel::flags( const QModelIndex& index ) const
{
    if ( !index.isValid() ) {
        return Qt::NoItemFlags;
    }

    if ( index.column() == EnabledColumn ) {
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    }
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

bool PreviewsTableModel::setData( const QModelIndex& index, const QVariant& value, int role )
{
    if ( role != Qt::CheckStateRole || index.column() != EnabledColumn ) {
        return false;
    }

    const auto& previews = PreviewManager::instance().all();
    if ( index.row() < 0 || index.row() >= previews.size() ) {
        return false;
    }

    const auto enabled = value.toInt() == Qt::Checked;
    PreviewManager::instance().setEnabled( previews.at( index.row() ).name, enabled );
    Q_EMIT dataChanged( index, index, { Qt::CheckStateRole } );
    return true;
}

void PreviewsTableModel::refresh()
{
    beginResetModel();
    endResetModel();
}
