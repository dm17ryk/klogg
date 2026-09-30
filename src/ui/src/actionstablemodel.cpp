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
    if ( role == SequenceValueRole ) {
        return action.sequence.value;
    }
    if ( role == Qt::ToolTipRole ) {
        return tooltipForAction( action );
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
            return action.name;
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
    const auto& actions = ActionsManager::instance().actions();
    actions_.reserve( actions.size() );
    for ( const auto& action : actions ) {
        if ( !action.hidden ) {
            actions_.push_back( action );
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
    tooltip.append( tr( "Delay: %1" ).arg( action.parameters.delay ) );
    tooltip.append( '\n' );
    tooltip.append( tr( "Parameters: %1" ).arg( parameterSummary( action ) ) );
    tooltip.append( '\n' );
    tooltip.append( tr( "Sequence: %1" ).arg( action.sequence.value ) );
    return tooltip;
}
