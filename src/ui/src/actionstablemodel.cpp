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

QString parameterPresentationLabel( const ActionParameterDefinition& field )
{
    switch ( field.presentation ) {
    case ActionParameterPresentation::ComboBox:
        return QObject::tr( "combo" );
    case ActionParameterPresentation::RadioButtons:
        return QObject::tr( "radio" );
    case ActionParameterPresentation::CheckBoxes:
        return QObject::tr( "checks" );
    case ActionParameterPresentation::TextBox:
        return QObject::tr( "text" );
    case ActionParameterPresentation::Automatic:
    default:
        if ( field.type == ActionParameterType::Choice ) {
            return QObject::tr( "combo" );
        }
        if ( field.type == ActionParameterType::MultiChoice ) {
            return QObject::tr( "checks" );
        }
        return QObject::tr( "text" );
    }
}

QString parameterSummary( const ActionDefinition& action )
{
    QStringList summary;
    for ( const auto& field : action.parameters.fields ) {
        const auto label = field.label.trimmed().isEmpty() ? field.name : field.label.trimmed();
        summary.push_back( QStringLiteral( "%1 (%2)" ).arg( label, parameterPresentationLabel( field ) ) );
    }
    return summary.isEmpty() ? QObject::tr( "None" ) : summary.join( QStringLiteral( ", " ) );
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
    return 5;
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
    const auto& origin = origins_.at( row );
    if ( role == PackIdRole ) {
        return origin.packId;
    }
    if ( role == NamespacedIdRole ) {
        return origin.packId.isEmpty() ? QString{}
                                       : namespacedItemId( origin.packId, origin.localId );
    }
    if ( role == SequenceValueRole ) {
        return action.sequence.value;
    }
    if ( role == Qt::ToolTipRole ) {
        // A pack's action: its pack and the name it has on every PC (E16c3).
        return origin.packId.isEmpty()
                   ? tooltipForAction( action )
                   : tr( "Pack: %1 (%2), read only: copy it to make it yours" )
                             .arg( origin.packName,
                                   namespacedItemId( origin.packId, origin.localId ) )
                         + '\n' + tooltipForAction( action );
    }
    switch ( index.column() ) {
    case 0:
        if ( role == Qt::DisplayRole ) {
            return action.id;
        }
        if ( role == Qt::TextAlignmentRole ) {
            return Qt::AlignCenter;
        }
        break;
    case 1:
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
    case 2:
        if ( role == Qt::DisplayRole ) {
            return origin.packId.isEmpty()
                       ? action.name
                       : QStringLiteral( "%1 (%2)" ).arg( action.name, origin.packName );
        }
        break;
    case 3:
        if ( role == Qt::DisplayRole ) {
            return parameterSummary( action );
        }
        break;
    case 4:
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
        return tr( "Id" );
    case 1:
        return tr( "Send" );
    case 2:
        return tr( "Action" );
    case 3:
        return tr( "Parameters" );
    case 4:
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
    actions_.clear();
    origins_.clear();
    const auto& actions = ActionsManager::instance().actions();
    actions_.reserve( actions.size() );
    for ( const auto& action : actions ) {
        if ( !action.hidden ) {
            actions_.push_back( action );
            origins_.push_back( {} );
        }
    }
    // Then the enabled packs' (E16c3).
    for ( const auto& listed : ActionsManager::instance().packActions() ) {
        if ( !listed.action.hidden ) {
            actions_.push_back( listed.action );
            origins_.push_back( { listed.packId, listed.packName, listed.localId } );
        }
    }
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

ActionsTableModel::Origin ActionsTableModel::originAt( int row ) const
{
    return row < 0 || row >= origins_.size() ? Origin{} : origins_.at( row );
}

QString ActionsTableModel::previewSequence( const ActionSequence& sequence ) const
{
    return truncateText( sequence.value, 64 );
}

QString ActionsTableModel::tooltipForAction( const ActionDefinition& action ) const
{
    QString tooltip = tr( "Id: %1" ).arg( action.id );
    if ( !action.description.isEmpty() ) {
        tooltip.append( '\n' );
        tooltip.append( action.description );
    }
    if ( !tooltip.isEmpty() ) {
        tooltip.append( '\n' );
    }
    tooltip.append( tr( "Repeat: %1" ).arg( action.parameters.repeat ? tr( "yes" ) : tr( "no" ) ) );
    tooltip.append( '\n' );
    tooltip.append( tr( "Delay before automatic sends: %1 ms" ).arg( action.parameters.delay ) );
    tooltip.append( '\n' );
    tooltip.append( tr( "Parameters: %1" ).arg( parameterSummary( action ) ) );
    tooltip.append( '\n' );
    tooltip.append( tr( "Sequence: %1" ).arg( action.sequence.value ) );
    return tooltip;
}
