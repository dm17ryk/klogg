#include "actionsresponseswindow.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QGroupBox>
#include <QPushButton>
#include <QScrollBar>
#include <QScreen>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTableView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QVBoxLayout>
#include <functional>

#include "actioneditdialog.h"
#include "actionparametersdialog.h"
#include "actionstablemodel.h"
#include "actionsmanager.h"
#include "log.h"
#include "previewdecodeutils.h"
#include "responseeditdialog.h"
#include "responsestablemodel.h"

namespace {
class ActionsFilterProxyModel : public QSortFilterProxyModel {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setFilterText( const QString& text )
    {
        filterText_ = text.trimmed();
        refreshFilter();
    }

  protected:
    bool filterAcceptsRow( int sourceRow, const QModelIndex& sourceParent ) const override
    {
        if ( filterText_.isEmpty() ) {
            return true;
        }
        const auto nameIndex = sourceModel()->index( sourceRow, 2, sourceParent );
        const auto seqIndex = sourceModel()->index( sourceRow, 4, sourceParent );
        const auto name = sourceModel()->data( nameIndex, Qt::DisplayRole ).toString();
        const auto sequence = sourceModel()->data( seqIndex, Qt::DisplayRole ).toString();
        return name.contains( filterText_, Qt::CaseInsensitive )
               || sequence.contains( filterText_, Qt::CaseInsensitive );
    }

  private:
    void refreshFilter()
    {
#if QT_VERSION >= QT_VERSION_CHECK( 6, 9, 0 )
        beginFilterChange();
        endFilterChange( QSortFilterProxyModel::Direction::Rows );
#else
        invalidateFilter();
#endif
    }

    QString filterText_;
};

enum class ResponseFilterType { Text, Hex, Regex };

bool looksLikeHex( const QString& text )
{
    auto trimmed = text.trimmed();
    if ( trimmed.startsWith( "0x", Qt::CaseInsensitive ) ) {
        trimmed = trimmed.mid( 2 );
    }
    if ( trimmed.isEmpty() ) {
        return false;
    }
    QString normalized;
    normalized.reserve( trimmed.size() );
    for ( const auto ch : trimmed ) {
        if ( ch.isSpace() || ch == '_' ) {
            continue;
        }
        if ( !ch.isDigit() && ( ch.toLower() < 'a' || ch.toLower() > 'f' ) ) {
            return false;
        }
        normalized.append( ch );
    }
    return normalized.size() >= 2 && ( normalized.size() % 2 ) == 0;
}

bool looksLikeRegex( const QString& text )
{
    if ( text.startsWith( "re:", Qt::CaseInsensitive ) ) {
        return true;
    }
    static const QRegularExpression regexMeta( R"([.^$|()?*+\[\]\\])" );
    return regexMeta.match( text ).hasMatch();
}

class ResponsesFilterProxyModel : public QSortFilterProxyModel {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setFilterText( const QString& text )
    {
        filterText_ = text.trimmed();
        filterType_ = ResponseFilterType::Text;
        filterValid_ = true;
        hexBytes_.clear();
        regex_.setPattern( {} );

        if ( filterText_.isEmpty() ) {
            refreshFilter();
            return;
        }

        if ( looksLikeHex( filterText_ ) ) {
            filterType_ = ResponseFilterType::Hex;
            const auto decoded = decodeHexStringToBytes( filterText_ );
            filterValid_ = decoded.ok;
            if ( decoded.ok ) {
                hexBytes_ = decoded.bytes;
            }
        }
        else if ( looksLikeRegex( filterText_ ) ) {
            filterType_ = ResponseFilterType::Regex;
            QString pattern = filterText_;
            if ( pattern.startsWith( "re:", Qt::CaseInsensitive ) ) {
                pattern = pattern.mid( 3 ).trimmed();
            }
            regex_ = QRegularExpression( pattern );
            filterValid_ = regex_.isValid();
        }

        refreshFilter();
    }

  protected:
    bool filterAcceptsRow( int sourceRow, const QModelIndex& sourceParent ) const override
    {
        if ( filterText_.isEmpty() ) {
            return true;
        }
        if ( !filterValid_ ) {
            return false;
        }

        const auto nameIndex = sourceModel()->index( sourceRow, 2, sourceParent );
        const auto valueIndex = sourceModel()->index( sourceRow, 3, sourceParent );
        const auto matchValue
            = sourceModel()->data( valueIndex, ResponsesTableModel::MatchValueRole )
                  .toString();
        const auto matchTypeValue
            = sourceModel()->data( valueIndex, ResponsesTableModel::MatchTypeRole )
                  .toInt();
        const auto matchType = static_cast<ResponseMatchType>( matchTypeValue );
        const auto responseName = sourceModel()->data( nameIndex, Qt::DisplayRole ).toString();

        if ( filterType_ == ResponseFilterType::Hex ) {
            if ( matchType != ResponseMatchType::HexString ) {
                return false;
            }
            const auto decoded = decodeHexStringToBytes( matchValue );
            if ( !decoded.ok ) {
                return false;
            }
            return decoded.bytes.contains( hexBytes_ );
        }

        if ( filterType_ == ResponseFilterType::Regex ) {
            return regex_.match( matchValue ).hasMatch();
        }

        return matchValue.contains( filterText_, Qt::CaseInsensitive )
               || responseName.contains( filterText_, Qt::CaseInsensitive );
    }

  private:
    void refreshFilter()
    {
#if QT_VERSION >= QT_VERSION_CHECK( 6, 9, 0 )
        beginFilterChange();
        endFilterChange( QSortFilterProxyModel::Direction::Rows );
#else
        invalidateFilter();
#endif
    }

    QString filterText_;
    ResponseFilterType filterType_ = ResponseFilterType::Text;
    bool filterValid_ = true;
    QByteArray hexBytes_;
    QRegularExpression regex_;
};

int tableWidthHint( QTableView* table )
{
    if ( !table || !table->model() ) {
        return 0;
    }
    int width = table->frameWidth() * 2;
    if ( table->verticalHeader()->isVisible() ) {
        width += table->verticalHeader()->width();
    }
    for ( int column = 0; column < table->model()->columnCount(); ++column ) {
        width += table->columnWidth( column );
    }
    if ( table->verticalScrollBar() ) {
        width += table->verticalScrollBar()->sizeHint().width();
    }
    return width;
}

void capColumnWidth( QTableView* table, int column, int maxWidth )
{
    if ( !table || !table->model() ) {
        return;
    }
    if ( column < 0 || column >= table->model()->columnCount() ) {
        return;
    }
    if ( table->columnWidth( column ) > maxWidth ) {
        table->setColumnWidth( column, maxWidth );
    }
}

int clampToScreenWidth( const QWidget* widget, int width )
{
    const auto* screen = widget ? widget->screen() : nullptr;
    if ( !screen ) {
        return width;
    }
    const int maxWidth = static_cast<int>( screen->availableGeometry().width() * 0.7 );
    if ( maxWidth <= 0 ) {
        return width;
    }
    return qMin( width, maxWidth );
}

int tableHeightForRows( QTableView* table, int rows )
{
    if ( !table ) {
        return 0;
    }
    const int rowHeight = table->verticalHeader()->defaultSectionSize();
    int height = table->frameWidth() * 2 + table->horizontalHeader()->height();
    height += rowHeight * rows;
    if ( table->horizontalScrollBar() ) {
        height += table->horizontalScrollBar()->sizeHint().height();
    }
    return height;
}

class ActionSendDelegate : public QStyledItemDelegate {
  public:
    explicit ActionSendDelegate( std::function<void( const QModelIndex& )> callback,
                                 QObject* parent = nullptr )
        : QStyledItemDelegate( parent )
        , callback_( std::move( callback ) )
    {
    }

  protected:
    void paint( QPainter* painter,
                const QStyleOptionViewItem& option,
                const QModelIndex& index ) const override
    {
        QStyleOptionButton buttonOption;
        buttonOption.rect = option.rect.adjusted( 4, 2, -4, -2 );
        buttonOption.text = QObject::tr( "Send" );
        buttonOption.state = QStyle::State_Enabled;
        const auto enabled = index.data( ActionsTableModel::SendEnabledRole ).toBool();
        if ( !enabled ) {
            buttonOption.state &= ~QStyle::State_Enabled;
        }
        if ( option.state & QStyle::State_MouseOver ) {
            buttonOption.state |= QStyle::State_MouseOver;
        }
        if ( option.state & QStyle::State_Selected ) {
            buttonOption.state |= QStyle::State_HasFocus;
        }
        if ( pressedIndex_ == index ) {
            buttonOption.state |= QStyle::State_Sunken;
        }

        if ( const auto* style = option.widget ? option.widget->style() : nullptr ) {
            style->drawControl( QStyle::CE_PushButton, &buttonOption, painter );
        }
        else {
            QStyledItemDelegate::paint( painter, option, index );
        }
    }

    bool editorEvent( QEvent* event,
                      QAbstractItemModel*,
                      const QStyleOptionViewItem& option,
                      const QModelIndex& index ) override
    {
        if ( !index.data( ActionsTableModel::SendEnabledRole ).toBool() ) {
            return false;
        }

        if ( event->type() == QEvent::MouseButtonPress ) {
            const auto* mouseEvent = static_cast<QMouseEvent*>( event );
            if ( mouseEvent->button() == Qt::LeftButton
                 && option.rect.contains( mouseEvent->pos() ) ) {
                pressedIndex_ = index;
                if ( auto* view = qobject_cast<QAbstractItemView*>(
                         const_cast<QWidget*>( option.widget ) ) ) {
                    view->viewport()->update( option.rect );
                }
                return true;
            }
        }

        if ( event->type() == QEvent::MouseButtonRelease ) {
            const auto* mouseEvent = static_cast<QMouseEvent*>( event );
            const bool wasPressed = ( pressedIndex_ == index );
            const bool shouldTrigger
                = wasPressed && mouseEvent->button() == Qt::LeftButton
                  && option.rect.contains( mouseEvent->pos() );
            if ( wasPressed ) {
                pressedIndex_ = QPersistentModelIndex();
                if ( auto* view = qobject_cast<QAbstractItemView*>(
                         const_cast<QWidget*>( option.widget ) ) ) {
                    view->viewport()->update( option.rect );
                }
            }
            if ( shouldTrigger && callback_ ) {
                callback_( index );
            }
            return wasPressed;
        }

        pressedIndex_ = QPersistentModelIndex();
        return false;
    }

  private:
    std::function<void( const QModelIndex& )> callback_;
    QPersistentModelIndex pressedIndex_;
};
} // namespace

ActionsResponsesWindow::ActionsResponsesWindow( QWidget* parent )
    : QWidget( parent )
{
    setObjectName( QStringLiteral( "actionsWindow" ) );
    actionsModel_ = new ActionsTableModel( this );
    responsesModel_ = new ResponsesTableModel( this );

    auto* actionsProxy = new ActionsFilterProxyModel( this );
    actionsProxy->setSourceModel( actionsModel_ );
    actionsProxy_ = actionsProxy;

    auto* responsesProxy = new ResponsesFilterProxyModel( this );
    responsesProxy->setSourceModel( responsesModel_ );
    responsesProxy_ = responsesProxy;

    actionsFilter_ = new QLineEdit( this );
    actionsFilter_->setObjectName( QStringLiteral( "actionsFilterLineEdit" ) );
    actionsFilter_->setPlaceholderText( tr( "Filter actions..." ) );
    connect( actionsFilter_, &QLineEdit::textChanged, actionsProxy,
             &ActionsFilterProxyModel::setFilterText );

    responsesFilter_ = new QLineEdit( this );
    responsesFilter_->setObjectName( QStringLiteral( "responsesFilterLineEdit" ) );
    responsesFilter_->setPlaceholderText( tr( "Filter responses..." ) );
    connect( responsesFilter_, &QLineEdit::textChanged, responsesProxy,
             &ResponsesFilterProxyModel::setFilterText );

    actionsTable_ = new QTableView( this );
    actionsTable_->setObjectName( QStringLiteral( "actionsTableView" ) );
    actionsTable_->setModel( actionsProxy );
    actionsTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
    actionsTable_->setSelectionMode( QAbstractItemView::SingleSelection );
    actionsTable_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    actionsTable_->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    actionsTable_->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    actionsTable_->setWordWrap( false );
    auto* actionsHeader = actionsTable_->horizontalHeader();
    actionsHeader->setSectionResizeMode( QHeaderView::Interactive );
    actionsHeader->setStretchLastSection( false );
    actionsTable_->resizeColumnsToContents();
    actionsTable_->setColumnWidth( 1, actionsTable_->columnWidth( 1 ) + 5 );
    actionsTable_->setColumnWidth( 0, qMin( actionsTable_->columnWidth( 0 ), 70 ) );
    capColumnWidth( actionsTable_, 2, 350 );
    capColumnWidth( actionsTable_, 3, 320 );
    capColumnWidth( actionsTable_, 4, 600 );

    auto* sendDelegate = new ActionSendDelegate(
        [ this ]( const QModelIndex& proxyIndex ) {
            if ( !proxyIndex.isValid() ) {
                return;
            }
            actionsTable_->selectRow( proxyIndex.row() );
            updateActionParametersPanel();
            sendSelectedAction();
        },
        actionsTable_ );
    actionsTable_->setItemDelegateForColumn( 1, sendDelegate );

    responsesTable_ = new QTableView( this );
    responsesTable_->setObjectName( QStringLiteral( "responsesTableView" ) );
    responsesTable_->setModel( responsesProxy );
    responsesTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
    responsesTable_->setSelectionMode( QAbstractItemView::SingleSelection );
    responsesTable_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    responsesTable_->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    responsesTable_->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    responsesTable_->setWordWrap( false );
    auto* responsesHeader = responsesTable_->horizontalHeader();
    responsesHeader->setSectionResizeMode( QHeaderView::Interactive );
    responsesHeader->setStretchLastSection( false );
    responsesTable_->resizeColumnsToContents();
    responsesTable_->setColumnWidth( 0, qMin( responsesTable_->columnWidth( 0 ), 70 ) );
    capColumnWidth( responsesTable_, 2, 350 );
    capColumnWidth( responsesTable_, 3, 600 );

    actionsPanel_ = new QWidget( this );
    auto* actionsLayout = new QVBoxLayout( actionsPanel_ );
    actionsLayout->setContentsMargins( 0, 0, 0, 0 );
    auto* actionsButtonsLayout = new QHBoxLayout;
    auto* addActionButton = new QPushButton( tr( "Add" ), actionsPanel_ );
    editActionButton_ = new QPushButton( tr( "Edit" ), actionsPanel_ );
    duplicateActionButton_ = new QPushButton( tr( "Duplicate" ), actionsPanel_ );
    deleteActionButton_ = new QPushButton( tr( "Delete" ), actionsPanel_ );
    moveActionUpButton_ = new QPushButton( tr( "Move Up" ), actionsPanel_ );
    moveActionDownButton_ = new QPushButton( tr( "Move Down" ), actionsPanel_ );
    actionsButtonsLayout->addWidget( addActionButton );
    actionsButtonsLayout->addWidget( editActionButton_ );
    actionsButtonsLayout->addWidget( duplicateActionButton_ );
    actionsButtonsLayout->addWidget( deleteActionButton_ );
    actionsButtonsLayout->addWidget( moveActionUpButton_ );
    actionsButtonsLayout->addWidget( moveActionDownButton_ );
    actionsButtonsLayout->addStretch();
    actionsLayout->addWidget( actionsFilter_ );
    actionsLayout->addLayout( actionsButtonsLayout );
    actionsLayout->addWidget( actionsTable_ );

    auto* parameterPanel = new QGroupBox( tr( "Selected action parameters" ), actionsPanel_ );
    parameterPanel->setObjectName( QStringLiteral( "selectedActionParametersPanel" ) );
    auto* parameterLayout = new QVBoxLayout( parameterPanel );
    parameterLayout->setContentsMargins( 8, 6, 8, 6 );
    actionParametersHint_ = new QLabel(
        tr( "Select an action with parameters to expand its controls here. "
            "The Send button uses the values currently shown." ),
        parameterPanel );
    actionParametersHint_->setWordWrap( true );
    actionParametersHint_->setObjectName( QStringLiteral( "selectedActionParametersHint" ) );
    parameterLayout->addWidget( actionParametersHint_ );
    sendSelectedActionButton_ = new QPushButton( tr( "Send selected action" ), parameterPanel );
    sendSelectedActionButton_->setObjectName( QStringLiteral( "sendSelectedActionButton" ) );
    sendSelectedActionButton_->setVisible( false );
    parameterLayout->addWidget( sendSelectedActionButton_, 0, Qt::AlignRight );
    actionParametersPanel_ = parameterPanel;
    actionParametersPanel_->setVisible( false );
    actionsLayout->addWidget( actionParametersPanel_ );

    responsesPanel_ = new QWidget( this );
    auto* responsesLayout = new QVBoxLayout( responsesPanel_ );
    responsesLayout->setContentsMargins( 0, 0, 0, 0 );
    auto* responsesButtonsLayout = new QHBoxLayout;
    auto* addResponseButton = new QPushButton( tr( "Add" ), responsesPanel_ );
    editResponseButton_ = new QPushButton( tr( "Edit" ), responsesPanel_ );
    duplicateResponseButton_ = new QPushButton( tr( "Duplicate" ), responsesPanel_ );
    deleteResponseButton_ = new QPushButton( tr( "Delete" ), responsesPanel_ );
    moveResponseUpButton_ = new QPushButton( tr( "Move Up" ), responsesPanel_ );
    moveResponseDownButton_ = new QPushButton( tr( "Move Down" ), responsesPanel_ );
    responsesButtonsLayout->addWidget( addResponseButton );
    responsesButtonsLayout->addWidget( editResponseButton_ );
    responsesButtonsLayout->addWidget( duplicateResponseButton_ );
    responsesButtonsLayout->addWidget( deleteResponseButton_ );
    responsesButtonsLayout->addWidget( moveResponseUpButton_ );
    responsesButtonsLayout->addWidget( moveResponseDownButton_ );
    responsesButtonsLayout->addStretch();
    responsesLayout->addWidget( responsesFilter_ );
    responsesLayout->addLayout( responsesButtonsLayout );
    responsesLayout->addWidget( responsesTable_ );
    autoResponsesCheck_ = new QCheckBox( tr( "Auto response enabled" ), responsesPanel_ );
    autoResponsesCheck_->setObjectName( QStringLiteral( "autoResponsesCheckBox" ) );
    autoResponsesCheck_->setChecked( ActionsManager::instance().autoResponsesEnabled() );
    responsesLayout->addWidget( autoResponsesCheck_ );

    splitter_ = new QSplitter( Qt::Vertical, this );
    splitter_->setObjectName( QStringLiteral( "actionsResponsesSplitter" ) );
    splitter_->addWidget( actionsPanel_ );
    splitter_->addWidget( responsesPanel_ );
    splitter_->setStretchFactor( 0, 1 );
    splitter_->setStretchFactor( 1, 1 );

    auto* layout = new QVBoxLayout( this );
    layout->addWidget( splitter_ );
    setLayout( layout );

    refreshActions();
    refreshResponses();
    updateWindowSize();

    connect( &ActionsManager::instance(), &ActionsManager::actionsChanged, this,
             &ActionsResponsesWindow::refreshActions );
    connect( &ActionsManager::instance(), &ActionsManager::responsesChanged, this,
             &ActionsResponsesWindow::refreshResponses );
    connect( autoResponsesCheck_, &QCheckBox::toggled, this,
             []( bool enabled ) { ActionsManager::instance().setAutoResponsesEnabled( enabled ); } );
    connect( addActionButton, &QPushButton::clicked, this, &ActionsResponsesWindow::addAction );
    connect( editActionButton_, &QPushButton::clicked, this, &ActionsResponsesWindow::editSelectedAction );
    connect( duplicateActionButton_, &QPushButton::clicked, this,
             &ActionsResponsesWindow::duplicateSelectedAction );
    connect( deleteActionButton_, &QPushButton::clicked, this,
             &ActionsResponsesWindow::deleteSelectedAction );
    connect( moveActionUpButton_, &QPushButton::clicked, this,
             &ActionsResponsesWindow::moveSelectedActionUp );
    connect( moveActionDownButton_, &QPushButton::clicked, this,
             &ActionsResponsesWindow::moveSelectedActionDown );
    connect( addResponseButton, &QPushButton::clicked, this, &ActionsResponsesWindow::addResponse );
    connect( editResponseButton_, &QPushButton::clicked, this,
             &ActionsResponsesWindow::editSelectedResponse );
    connect( duplicateResponseButton_, &QPushButton::clicked, this,
             &ActionsResponsesWindow::duplicateSelectedResponse );
    connect( deleteResponseButton_, &QPushButton::clicked, this,
             &ActionsResponsesWindow::deleteSelectedResponse );
    connect( moveResponseUpButton_, &QPushButton::clicked, this,
             &ActionsResponsesWindow::moveSelectedResponseUp );
    connect( moveResponseDownButton_, &QPushButton::clicked, this,
             &ActionsResponsesWindow::moveSelectedResponseDown );
    connect( actionsTable_, &QTableView::doubleClicked, this,
             [ this ]( const QModelIndex& index ) {
                 if ( index.column() != 1 ) {
                     editSelectedAction();
                 }
             } );
    connect( responsesTable_, &QTableView::doubleClicked, this,
             [ this ]( const QModelIndex& ) { editSelectedResponse(); } );
    connect( sendSelectedActionButton_, &QPushButton::clicked, this,
             &ActionsResponsesWindow::sendSelectedAction );
    connect( actionsTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
             [ this ] {
                 updateActionButtons();
                 updateActionParametersPanel();
             } );
    connect( responsesTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
             [ this ] { updateResponseButtons(); } );
    connect( &ActionsManager::instance(), &ActionsManager::autoResponsesEnabledChanged,
             this, [ this ]( bool enabled ) {
                 if ( autoResponsesCheck_ && autoResponsesCheck_->isChecked() != enabled ) {
                     const QSignalBlocker blocker( autoResponsesCheck_ );
                     autoResponsesCheck_->setChecked( enabled );
                 }
             } );
    updateActionButtons();
    updateResponseButtons();
    updateActionParametersPanel();
    setResponsesOnly( false );
}

void ActionsResponsesWindow::setResponsesOnly( const bool responsesOnly )
{
    responsesOnly_ = responsesOnly;
    setObjectName( responsesOnly_ ? QStringLiteral( "responsesWindow" )
                                  : QStringLiteral( "actionsWindow" ) );
    if ( actionsPanel_ != nullptr ) {
        actionsPanel_->setVisible( !responsesOnly_ );
    }
    if ( responsesPanel_ != nullptr ) {
        responsesPanel_->setVisible( responsesOnly_ );
    }
    if ( splitter_ != nullptr ) {
        splitter_->setStretchFactor( 0, responsesOnly_ ? 0 : 1 );
        splitter_->setStretchFactor( 1, responsesOnly_ ? 1 : 0 );
        splitter_->setSizes( responsesOnly_ ? QList<int>{ 0, qMax( 1, height() ) }
                                            : QList<int>{ qMax( 1, height() ), 0 } );
    }
    if ( responsesOnly_ ) {
        actionParametersPanel_->setVisible( false );
    }
    updateWindowSize();
    LOG_DEBUG << "Configured actions/responses window mode: responses_only=" << responsesOnly_;
}

void ActionsResponsesWindow::setSendAvailable( bool available )
{
    if ( actionsModel_ ) {
        actionsModel_->setSendAvailable( available );
    }
    if ( sendSelectedActionButton_ ) {
        sendSelectedActionButton_->setEnabled( available );
    }
}

void ActionsResponsesWindow::updateActionParametersPanel()
{
    if ( !actionParametersPanel_ || !actionsModel_ ) {
        return;
    }

    const auto row = selectedActionRow();
    const auto* action = actionsModel_->actionAt( row );
    const bool hasParameters = action != nullptr && !action->parameters.fields.isEmpty();

    if ( hasParameters && actionParametersEditor_ != nullptr
         && actionParametersActionId_ == action->id ) {
        actionParametersPanel_->setVisible( true );
        if ( sendSelectedActionButton_ ) {
            sendSelectedActionButton_->setVisible( true );
        }
        return;
    }

    if ( actionParametersEditor_ != nullptr ) {
        if ( auto* layout = qobject_cast<QVBoxLayout*>( actionParametersPanel_->layout() ) ) {
            layout->removeWidget( actionParametersEditor_ );
        }
        actionParametersEditor_->deleteLater();
        actionParametersEditor_ = nullptr;
    }
    actionParametersActionId_ = hasParameters ? action->id : -1;

    if ( !hasParameters ) {
        actionParametersPanel_->setVisible( false );
        if ( sendSelectedActionButton_ ) {
            sendSelectedActionButton_->setVisible( false );
        }
        return;
    }

    actionParametersEditor_ = new ActionParametersDialog( *action, actionParametersPanel_, true );
    actionParametersEditor_->setObjectName( QStringLiteral( "inlineActionParametersEditor" ) );
    if ( auto* layout = qobject_cast<QVBoxLayout*>( actionParametersPanel_->layout() ) ) {
        layout->insertWidget( 1, actionParametersEditor_ );
    }
    actionParametersEditor_->show();
    if ( actionParametersHint_ ) {
        actionParametersHint_->setText(
            tr( "Choose values for %1. The row Send button and the button below use these values." )
                .arg( action->name ) );
    }
    if ( sendSelectedActionButton_ ) {
        sendSelectedActionButton_->setVisible( true );
    }
    actionParametersPanel_->setVisible( true );
    LOG_DEBUG << "Expanded inline action parameters for action id=" << action->id
              << ", field_count=" << action->parameters.fields.size();
}

void ActionsResponsesWindow::sendSelectedAction()
{
    const auto row = selectedActionRow();
    const auto* action = actionsModel_ ? actionsModel_->actionAt( row ) : nullptr;
    if ( action == nullptr ) {
        return;
    }

    if ( action->parameters.fields.isEmpty() ) {
        Q_EMIT sendActionRequested( action->id );
        return;
    }

    if ( actionParametersEditor_ == nullptr || actionParametersActionId_ != action->id ) {
        updateActionParametersPanel();
    }
    if ( actionParametersEditor_ == nullptr ) {
        return;
    }

    QString errorMessage;
    if ( !actionParametersEditor_->validateCurrentValues( &errorMessage ) ) {
        LOG_WARNING << "Inline parameter editor rejected action id=" << action->id;
        QMessageBox::warning( this, tr( "Send action" ), errorMessage );
        return;
    }

    const auto parameters = actionParametersEditor_->currentValues();
    actionParametersEditor_->rememberCurrentValues();
    LOG_DEBUG << "Sending action id=" << action->id
              << " from inline parameter editor, parameter_count=" << parameters.size();
    Q_EMIT sendActionWithParametersRequested( action->id, parameters );
}

void ActionsResponsesWindow::refreshActions()
{
    // Keep the edited action selected when ActionsManager emits actionsChanged.
    // A model refresh resets the selection model, which otherwise makes the
    // action appear to lose focus immediately after pressing OK.
    int selectedActionId = -1;
    const auto previousRow = selectedActionRow();
    if ( actionsModel_ != nullptr && previousRow >= 0 ) {
        if ( const auto* previousAction = actionsModel_->actionAt( previousRow ) ) {
            selectedActionId = previousAction->id;
        }
    }

    if ( actionsModel_ ) {
        actionsModel_->refresh();
    }
    if ( actionsTable_ ) {
        actionsTable_->resizeColumnsToContents();
        actionsTable_->setColumnWidth( 1, actionsTable_->columnWidth( 1 ) + 5 );
        actionsTable_->setColumnWidth( 0, qMin( actionsTable_->columnWidth( 0 ), 70 ) );
        capColumnWidth( actionsTable_, 2, 350 );
        capColumnWidth( actionsTable_, 3, 320 );
        capColumnWidth( actionsTable_, 4, 600 );

        if ( selectedActionId >= 0 && actionsProxy_ != nullptr
             && actionsTable_->selectionModel() != nullptr ) {
            for ( int proxyRow = 0; proxyRow < actionsProxy_->rowCount(); ++proxyRow ) {
                const auto sourceIndex = actionsProxy_->index( proxyRow, 0 );
                const auto sourceRow = actionsProxy_->mapToSource( sourceIndex ).row();
                const auto* candidate = actionsModel_->actionAt( sourceRow );
                if ( candidate != nullptr && candidate->id == selectedActionId ) {
                    actionsTable_->selectRow( proxyRow );
                    actionsTable_->setFocus( Qt::OtherFocusReason );
                    break;
                }
            }
        }
    }
    updateActionButtons();
    updateActionParametersPanel();
}

void ActionsResponsesWindow::refreshResponses()
{
    if ( responsesModel_ ) {
        responsesModel_->refresh();
    }
    if ( responsesTable_ ) {
        responsesTable_->resizeColumnsToContents();
        responsesTable_->setColumnWidth( 0, qMin( responsesTable_->columnWidth( 0 ), 70 ) );
        capColumnWidth( responsesTable_, 2, 350 );
        capColumnWidth( responsesTable_, 3, 600 );
    }
    updateResponseButtons();
}

void ActionsResponsesWindow::updateWindowSize()
{
    if ( !actionsTable_ || !responsesTable_ ) {
        return;
    }
    const int previousActionsMin = actionsTable_->minimumHeight();
    const int previousResponsesMin = responsesTable_->minimumHeight();
    actionsTable_->setMinimumHeight( responsesOnly_ ? 0 : tableHeightForRows( actionsTable_, 10 ) );
    responsesTable_->setMinimumHeight( tableHeightForRows( responsesTable_, 10 ) );
    if ( layout() ) {
        layout()->activate();
    }
    const QSize hint = sizeHint();
    actionsTable_->setMinimumHeight( previousActionsMin );
    responsesTable_->setMinimumHeight( previousResponsesMin );

    const int widthHint = responsesOnly_ ? tableWidthHint( responsesTable_ )
                                        : tableWidthHint( actionsTable_ );
    const int desiredWidth = qMax(
        clampToScreenWidth( this, qMax( widthHint, 925 ) ), minimumSizeHint().width() );
    const int desiredHeight = qMax( hint.height(), minimumSizeHint().height() );
    if ( !sizeInitialized_ ) {
        resize( desiredWidth, desiredHeight );
        sizeInitialized_ = true;
    }
}

int ActionsResponsesWindow::selectedActionRow() const
{
    if ( actionsTable_ == nullptr || actionsProxy_ == nullptr || actionsTable_->selectionModel() == nullptr ) {
        return -1;
    }

    const auto rows = actionsTable_->selectionModel()->selectedRows();
    if ( rows.isEmpty() ) {
        return -1;
    }

    return actionsProxy_->mapToSource( rows.front() ).row();
}

int ActionsResponsesWindow::selectedResponseRow() const
{
    if ( responsesTable_ == nullptr || responsesProxy_ == nullptr
         || responsesTable_->selectionModel() == nullptr ) {
        return -1;
    }

    const auto rows = responsesTable_->selectionModel()->selectedRows();
    if ( rows.isEmpty() ) {
        return -1;
    }

    return responsesProxy_->mapToSource( rows.front() ).row();
}

void ActionsResponsesWindow::updateActionButtons()
{
    const auto hasSelection = selectedActionRow() >= 0;
    if ( editActionButton_ != nullptr ) {
        editActionButton_->setEnabled( hasSelection );
    }
    if ( duplicateActionButton_ != nullptr ) {
        duplicateActionButton_->setEnabled( hasSelection );
    }
    if ( deleteActionButton_ != nullptr ) {
        deleteActionButton_->setEnabled( hasSelection );
    }
    if ( moveActionUpButton_ != nullptr ) {
        moveActionUpButton_->setEnabled( hasSelection );
    }
    if ( moveActionDownButton_ != nullptr ) {
        moveActionDownButton_->setEnabled( hasSelection );
    }
}

void ActionsResponsesWindow::updateResponseButtons()
{
    const auto hasSelection = selectedResponseRow() >= 0;
    if ( editResponseButton_ != nullptr ) {
        editResponseButton_->setEnabled( hasSelection );
    }
    if ( duplicateResponseButton_ != nullptr ) {
        duplicateResponseButton_->setEnabled( hasSelection );
    }
    if ( deleteResponseButton_ != nullptr ) {
        deleteResponseButton_->setEnabled( hasSelection );
    }
    if ( moveResponseUpButton_ != nullptr ) {
        moveResponseUpButton_->setEnabled( hasSelection );
    }
    if ( moveResponseDownButton_ != nullptr ) {
        moveResponseDownButton_->setEnabled( hasSelection );
    }
}

void ActionsResponsesWindow::addAction()
{
    ActionEditDialog dialog( this );
    ActionDefinition action;
    action.id = ActionsManager::instance().nextActionId();
    dialog.setAction( action );
    if ( dialog.exec() != QDialog::Accepted ) {
        return;
    }

    QString errorMessage;
    if ( !ActionsManager::instance().createAction( dialog.action(), &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Add Action" ), errorMessage );
    }
}

void ActionsResponsesWindow::editSelectedAction()
{
    const auto row = selectedActionRow();
    const auto* action = actionsModel_ ? actionsModel_->actionAt( row ) : nullptr;
    if ( action == nullptr ) {
        return;
    }

    ActionEditDialog dialog( this );
    dialog.setAction( *action );
    if ( dialog.exec() != QDialog::Accepted ) {
        return;
    }

    QString errorMessage;
    if ( !ActionsManager::instance().updateAction( action->id, dialog.action(), &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Edit Action" ), errorMessage );
    }
}

void ActionsResponsesWindow::duplicateSelectedAction()
{
    const auto row = selectedActionRow();
    const auto* action = actionsModel_ ? actionsModel_->actionAt( row ) : nullptr;
    if ( action == nullptr ) {
        return;
    }

    auto duplicate = *action;
    duplicate.id = ActionsManager::instance().nextActionId();
    duplicate.name = duplicate.name.isEmpty() ? tr( "Action %1" ).arg( duplicate.id )
                                              : tr( "%1 Copy" ).arg( duplicate.name );
    QString errorMessage;
    if ( !ActionsManager::instance().createAction( duplicate, &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Duplicate Action" ), errorMessage );
    }
}

void ActionsResponsesWindow::deleteSelectedAction()
{
    const auto row = selectedActionRow();
    const auto* action = actionsModel_ ? actionsModel_->actionAt( row ) : nullptr;
    if ( action == nullptr ) {
        return;
    }

    if ( QMessageBox::question( this, tr( "Delete Action" ),
                                tr( "Delete action \"%1\"?" ).arg( action->name ) )
         != QMessageBox::Yes ) {
        return;
    }

    QString errorMessage;
    if ( !ActionsManager::instance().deleteAction( action->id, &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Delete Action" ), errorMessage );
    }
}

void ActionsResponsesWindow::moveSelectedActionUp()
{
    const auto row = selectedActionRow();
    const auto* action = actionsModel_ ? actionsModel_->actionAt( row ) : nullptr;
    if ( action == nullptr ) {
        return;
    }

    QString errorMessage;
    if ( !ActionsManager::instance().moveAction( action->id, -1, &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Move Action" ), errorMessage );
    }
}

void ActionsResponsesWindow::moveSelectedActionDown()
{
    const auto row = selectedActionRow();
    const auto* action = actionsModel_ ? actionsModel_->actionAt( row ) : nullptr;
    if ( action == nullptr ) {
        return;
    }

    QString errorMessage;
    if ( !ActionsManager::instance().moveAction( action->id, 1, &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Move Action" ), errorMessage );
    }
}

void ActionsResponsesWindow::addResponse()
{
    ResponseEditDialog dialog( this );
    ResponseDefinition response;
    response.id = ActionsManager::instance().nextResponseId();
    dialog.setResponse( response, ActionsManager::instance().actions() );
    if ( dialog.exec() != QDialog::Accepted ) {
        return;
    }

    QString errorMessage;
    if ( !ActionsManager::instance().createResponse( dialog.response(), &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Add Response" ), errorMessage );
    }
}

void ActionsResponsesWindow::editSelectedResponse()
{
    const auto row = selectedResponseRow();
    const auto* response = responsesModel_ ? responsesModel_->responseAt( row ) : nullptr;
    if ( response == nullptr ) {
        return;
    }

    ResponseEditDialog dialog( this );
    dialog.setResponse( *response, ActionsManager::instance().actions() );
    if ( dialog.exec() != QDialog::Accepted ) {
        return;
    }

    QString errorMessage;
    if ( !ActionsManager::instance().updateResponse( response->id, dialog.response(), &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Edit Response" ), errorMessage );
    }
}

void ActionsResponsesWindow::duplicateSelectedResponse()
{
    const auto row = selectedResponseRow();
    const auto* response = responsesModel_ ? responsesModel_->responseAt( row ) : nullptr;
    if ( response == nullptr ) {
        return;
    }

    auto duplicate = *response;
    duplicate.id = ActionsManager::instance().nextResponseId();
    duplicate.name = duplicate.name.isEmpty() ? tr( "Response %1" ).arg( duplicate.id )
                                              : tr( "%1 Copy" ).arg( duplicate.name );
    QString errorMessage;
    if ( !ActionsManager::instance().createResponse( duplicate, &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Duplicate Response" ), errorMessage );
    }
}

void ActionsResponsesWindow::deleteSelectedResponse()
{
    const auto row = selectedResponseRow();
    const auto* response = responsesModel_ ? responsesModel_->responseAt( row ) : nullptr;
    if ( response == nullptr ) {
        return;
    }

    if ( QMessageBox::question( this, tr( "Delete Response" ),
                                tr( "Delete response \"%1\"?" ).arg( response->name ) )
         != QMessageBox::Yes ) {
        return;
    }

    QString errorMessage;
    if ( !ActionsManager::instance().deleteResponse( response->id, &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Delete Response" ), errorMessage );
    }
}

void ActionsResponsesWindow::moveSelectedResponseUp()
{
    const auto row = selectedResponseRow();
    const auto* response = responsesModel_ ? responsesModel_->responseAt( row ) : nullptr;
    if ( response == nullptr ) {
        return;
    }

    QString errorMessage;
    if ( !ActionsManager::instance().moveResponse( response->id, -1, &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Move Response" ), errorMessage );
    }
}

void ActionsResponsesWindow::moveSelectedResponseDown()
{
    const auto row = selectedResponseRow();
    const auto* response = responsesModel_ ? responsesModel_->responseAt( row ) : nullptr;
    if ( response == nullptr ) {
        return;
    }

    QString errorMessage;
    if ( !ActionsManager::instance().moveResponse( response->id, 1, &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Move Response" ), errorMessage );
    }
}
