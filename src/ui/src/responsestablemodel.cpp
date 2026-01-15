#include "responsestablemodel.h"

#include "actionsmanager.h"

namespace {
QString truncateText( const QString& text, int maxLength )
{
    if ( text.size() <= maxLength ) {
        return text;
    }
    return text.left( maxLength ) + "...";
}
} // namespace

ResponsesTableModel::ResponsesTableModel( QObject* parent )
    : QAbstractTableModel( parent )
{
}

int ResponsesTableModel::rowCount( const QModelIndex& parent ) const
{
    if ( parent.isValid() ) {
        return 0;
    }
    return static_cast<int>( responses_.size() );
}

int ResponsesTableModel::columnCount( const QModelIndex& parent ) const
{
    if ( parent.isValid() ) {
        return 0;
    }
    return 4;
}

QVariant ResponsesTableModel::data( const QModelIndex& index, int role ) const
{
    const int row = index.row();
    const int rowCount = static_cast<int>( responses_.size() );
    if ( !index.isValid() || row < 0 || row >= rowCount ) {
        return {};
    }

    const auto& response = responses_.at( row );
    if ( role == ResponseIdRole ) {
        return response.id;
    }
    if ( role == MatchValueRole ) {
        return response.match.value;
    }
    if ( role == MatchTypeRole ) {
        return static_cast<int>( response.match.type );
    }
    if ( role == Qt::ToolTipRole ) {
        return tooltipForResponse( response );
    }

    switch ( index.column() ) {
    case 0:
        if ( role == Qt::DisplayRole ) {
            return response.id;
        }
        if ( role == Qt::TextAlignmentRole ) {
            return Qt::AlignCenter;
        }
        break;
    case 1:
        if ( role == Qt::CheckStateRole ) {
            return response.enabled ? Qt::Checked : Qt::Unchecked;
        }
        if ( role == Qt::TextAlignmentRole ) {
            return Qt::AlignCenter;
        }
        break;
    case 2:
        if ( role == Qt::DisplayRole ) {
            return response.name;
        }
        break;
    case 3:
        if ( role == Qt::DisplayRole ) {
            return previewMatch( response.match );
        }
        break;
    default:
        break;
    }

    return {};
}

QVariant ResponsesTableModel::headerData( int section,
                                          Qt::Orientation orientation,
                                          int role ) const
{
    if ( orientation != Qt::Horizontal || role != Qt::DisplayRole ) {
        return {};
    }
    switch ( section ) {
    case 0:
        return tr( "Id" );
    case 1:
        return tr( "Enabled" );
    case 2:
        return tr( "Response" );
    case 3:
        return tr( "Match" );
    default:
        return {};
    }
}

Qt::ItemFlags ResponsesTableModel::flags( const QModelIndex& index ) const
{
    if ( !index.isValid() ) {
        return Qt::NoItemFlags;
    }
    if ( index.column() == 1 ) {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool ResponsesTableModel::setData( const QModelIndex& index, const QVariant& value, int role )
{
    const int row = index.row();
    const int rowCount = static_cast<int>( responses_.size() );
    if ( !index.isValid() || row < 0 || row >= rowCount ) {
        return false;
    }
    if ( index.column() != 1 || role != Qt::CheckStateRole ) {
        return false;
    }

    const bool enabled = value.toInt() == Qt::Checked;
    auto& response = responses_[ row ];
    if ( response.enabled == enabled ) {
        return true;
    }
    if ( !ActionsManager::instance().setResponseEnabled( response.id, enabled ) ) {
        return false;
    }
    response.enabled = enabled;
    Q_EMIT dataChanged( index, index, { Qt::CheckStateRole } );
    return true;
}

void ResponsesTableModel::refresh()
{
    beginResetModel();
    responses_.clear();
    const auto& responses = ActionsManager::instance().responses();
    responses_.reserve( responses.size() );
    for ( const auto& response : responses ) {
        if ( !response.hidden ) {
            responses_.push_back( response );
        }
    }
    endResetModel();
}

QString ResponsesTableModel::previewMatch( const ResponseMatchDefinition& match ) const
{
    return truncateText( match.value, 64 );
}

QString ResponsesTableModel::tooltipForResponse( const ResponseDefinition& response ) const
{
    QString tooltip = response.description;
    if ( !tooltip.isEmpty() ) {
        tooltip.append( '\n' );
    }
    tooltip.append( tr( "Match: %1 (%2)" )
                        .arg( response.match.value,
                              responseMatchTypeToString( response.match.type ) ) );
    if ( response.response.hasActionId ) {
        tooltip.append( '\n' );
        tooltip.append( tr( "Action id: %1" ).arg( response.response.actionId ) );
    }
    if ( response.response.hasInlineAction ) {
        tooltip.append( '\n' );
        tooltip.append( tr( "Inline action: %1" ).arg( response.response.inlineAction.value ) );
    }
    if ( !response.response.comment.isEmpty() ) {
        tooltip.append( '\n' );
        tooltip.append( tr( "Comment: %1" ).arg( response.response.comment ) );
    }
    tooltip.append( '\n' );
    tooltip.append( tr( "Linebreak: %1" ).arg( response.response.linebreak ? tr( "yes" )
                                                                           : tr( "no" ) ) );
    tooltip.append( '\n' );
    tooltip.append( tr( "Timestamp: %1" ).arg( response.response.timestamp ? tr( "yes" )
                                                                            : tr( "no" ) ) );
    tooltip.append( '\n' );
    tooltip.append( tr( "Snapshot: %1" ).arg( response.response.snapshot ? tr( "yes" )
                                                                          : tr( "no" ) ) );
    tooltip.append( '\n' );
    tooltip.append( tr( "Stop communication: %1" )
                        .arg( response.response.stopCommunication ? tr( "yes" )
                                                                  : tr( "no" ) ) );
    return tooltip;
}
