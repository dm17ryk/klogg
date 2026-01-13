#include "actionstablemodel.h"

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

ActionsTableModel::ActionsTableModel( QObject* parent )
    : QAbstractTableModel( parent )
{
}

int ActionsTableModel::rowCount( const QModelIndex& parent ) const
{
    if ( parent.isValid() ) {
        return 0;
    }
    return static_cast<int>( actions_.size() );
}

int ActionsTableModel::columnCount( const QModelIndex& parent ) const
{
    if ( parent.isValid() ) {
        return 0;
    }
    return 3;
}

QVariant ActionsTableModel::data( const QModelIndex& index, int role ) const
{
    const int row = index.row();
    const int rowCount = static_cast<int>( actions_.size() );
    if ( !index.isValid() || row < 0 || row >= rowCount ) {
        return {};
    }

    const auto& action = actions_.at( row );
    if ( role == ActionIdRole ) {
        return action.id;
    }
    if ( role == SequenceValueRole ) {
        return action.sequence.value;
    }
    if ( role == Qt::ToolTipRole ) {
        return tooltipForAction( action );
    }

    switch ( index.column() ) {
    case 0:
        if ( role == Qt::DisplayRole ) {
            return tr( "Send" );
        }
        if ( role == Qt::TextAlignmentRole ) {
            return Qt::AlignCenter;
        }
        if ( role == SendEnabledRole ) {
            return sendAvailable_;
        }
        break;
    case 1:
        if ( role == Qt::DisplayRole ) {
            return action.name;
        }
        break;
    case 2:
        if ( role == Qt::DisplayRole ) {
            return previewSequence( action.sequence );
        }
        break;
    default:
        break;
    }

    return {};
}

QVariant ActionsTableModel::headerData( int section,
                                        Qt::Orientation orientation,
                                        int role ) const
{
    if ( orientation != Qt::Horizontal || role != Qt::DisplayRole ) {
        return {};
    }
    switch ( section ) {
    case 0:
        return tr( "Send" );
    case 1:
        return tr( "Action" );
    case 2:
        return tr( "Sequence" );
    default:
        return {};
    }
}

Qt::ItemFlags ActionsTableModel::flags( const QModelIndex& index ) const
{
    if ( !index.isValid() ) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void ActionsTableModel::refresh()
{
    beginResetModel();
    actions_ = ActionsManager::instance().actions();
    endResetModel();
}

void ActionsTableModel::setSendAvailable( bool available )
{
    if ( sendAvailable_ == available ) {
        return;
    }
    sendAvailable_ = available;
    if ( actions_.isEmpty() ) {
        return;
    }
    const auto topLeft = index( 0, 0 );
    const auto bottomRight = index( static_cast<int>( actions_.size() ) - 1, 0 );
    Q_EMIT dataChanged( topLeft, bottomRight, { SendEnabledRole } );
}

const ActionDefinition* ActionsTableModel::actionAt( int row ) const
{
    if ( row < 0 || row >= actions_.size() ) {
        return nullptr;
    }
    return &actions_.at( row );
}

QString ActionsTableModel::previewSequence( const ActionSequence& sequence ) const
{
    return truncateText( sequence.value, 64 );
}

QString ActionsTableModel::tooltipForAction( const ActionDefinition& action ) const
{
    QString tooltip = action.description;
    if ( !tooltip.isEmpty() ) {
        tooltip.append( '\n' );
    }
    tooltip.append( tr( "Repeat: %1" ).arg( action.parameters.repeat ? tr( "yes" ) : tr( "no" ) ) );
    tooltip.append( '\n' );
    tooltip.append( tr( "Delay: %1" ).arg( action.parameters.delay ) );
    tooltip.append( '\n' );
    tooltip.append( tr( "Sequence: %1" ).arg( action.sequence.value ) );
    return tooltip;
}
