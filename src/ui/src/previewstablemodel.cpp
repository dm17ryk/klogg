#include "previewstablemodel.h"

namespace {
constexpr int NameColumn = 0;
constexpr int PatternColumn = 1;
constexpr int EnabledColumn = 2;
// Where a preview comes from: the user's own, or an enabled configuration pack (E16).
constexpr int SourceColumn = 3;
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
    const auto& manager = PreviewManager::instance();
    return static_cast<int>( manager.all().size() + manager.packPreviews().size() );
}

int PreviewsTableModel::columnCount( const QModelIndex& parent ) const
{
    if ( parent.isValid() ) {
        return 0;
    }
    return 4;
}

const PreviewDefinition* PreviewsTableModel::previewAt( int row ) const
{
    const auto& manager = PreviewManager::instance();
    const auto& own = manager.all();
    if ( row < 0 ) {
        return nullptr;
    }
    if ( row < own.size() ) {
        return &own.at( row );
    }
    const auto packRow = row - own.size();
    return packRow < manager.packPreviews().size() ? &manager.packPreviews().at( packRow )
                                                   : nullptr;
}

bool PreviewsTableModel::isPackRow( int row ) const
{
    return row >= PreviewManager::instance().all().size() && previewAt( row ) != nullptr;
}

bool PreviewsTableModel::isReplacedRow( int row ) const
{
    const auto* preview = previewAt( row );
    return isPackRow( row ) && preview
           && PreviewManager::instance().replacedByUser( preview->name );
}

QVariant PreviewsTableModel::data( const QModelIndex& index, int role ) const
{
    if ( !index.isValid() ) {
        return {};
    }
    const auto* preview = previewAt( index.row() );
    if ( !preview ) {
        return {};
    }

    if ( role == Qt::DisplayRole ) {
        switch ( index.column() ) {
        case NameColumn:
            return preview->name;
        case PatternColumn:
            return preview->regex;
        case SourceColumn: {
            const auto origin = PreviewManager::instance().packOrigin( preview->name );
            if ( !isPackRow( index.row() ) || !origin ) {
                return tr( "My previews" );
            }
            return isReplacedRow( index.row() )
                       ? tr( "%1 (replaced by your item)" ).arg( origin->packName )
                       : origin->packName;
        }
        default:
            return {};
        }
    }

    if ( role == Qt::CheckStateRole && index.column() == EnabledColumn ) {
        return preview->enabled ? Qt::Checked : Qt::Unchecked;
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
    case SourceColumn:
        return tr( "Source" );
    default:
        return {};
    }
}

Qt::ItemFlags PreviewsTableModel::flags( const QModelIndex& index ) const
{
    if ( !index.isValid() ) {
        return Qt::NoItemFlags;
    }

    // A replaced pack preview is not used, so its on/off box would only confuse.
    if ( index.column() == EnabledColumn && !isReplacedRow( index.row() ) ) {
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    }
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

bool PreviewsTableModel::setData( const QModelIndex& index, const QVariant& value, int role )
{
    if ( role != Qt::CheckStateRole || index.column() != EnabledColumn
         || isReplacedRow( index.row() ) ) {
        return false;
    }
    const auto* preview = previewAt( index.row() );
    if ( !preview ) {
        return false;
    }

    const auto enabled = value.toInt() == Qt::Checked;
    PreviewManager::instance().setEnabled( preview->name, enabled );
    Q_EMIT dataChanged( index, index, { Qt::CheckStateRole } );
    return true;
}

void PreviewsTableModel::refresh()
{
    beginResetModel();
    endResetModel();
}
