#include "importactionsdialog.h"

#include <QAbstractTableModel>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QScreen>
#include <QSplitter>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <utility>

#include "actionsconfigparser.h"
#include "actionsimportexport.h"
#include "actionsmanager.h"
#include "iconloader.h"

namespace {
QString truncateText( const QString& text, int maxLength )
{
    if ( text.size() <= maxLength ) {
        return text;
    }
    return text.left( maxLength ) + "...";
}

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
} // namespace

class ActionsImportTableModel : public QAbstractTableModel {
  public:
    explicit ActionsImportTableModel( QVector<ActionDefinition>* actions, QObject* parent = nullptr )
        : QAbstractTableModel( parent )
        , actions_( actions )
    {
    }

    void setActions( QVector<ActionDefinition>* actions )
    {
        beginResetModel();
        actions_ = actions;
        endResetModel();
    }

    int rowCount( const QModelIndex& parent = {} ) const override
    {
        if ( parent.isValid() || !actions_ ) {
            return 0;
        }
        return static_cast<int>( actions_->size() );
    }

    int columnCount( const QModelIndex& parent = {} ) const override
    {
        if ( parent.isValid() ) {
            return 0;
        }
        return 4;
    }

    QVariant data( const QModelIndex& index, int role ) const override
    {
        if ( !index.isValid() || !actions_ ) {
            return {};
        }
        const int row = index.row();
        if ( row < 0 || row >= actions_->size() ) {
            return {};
        }
        const auto& action = actions_->at( row );
        if ( role == Qt::ToolTipRole ) {
            QString tooltip = action.description;
            if ( !tooltip.isEmpty() ) {
                tooltip.append( '\n' );
            }
            tooltip.append( QObject::tr( "Sequence: %1" ).arg( action.sequence.value ) );
            return tooltip;
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
                return action.name;
            }
            break;
        case 2:
            if ( role == Qt::DisplayRole ) {
                return truncateText( action.sequence.value, 64 );
            }
            break;
        case 3:
            if ( role == Qt::CheckStateRole ) {
                return action.hidden ? Qt::Unchecked : Qt::Checked;
            }
            if ( role == Qt::TextAlignmentRole ) {
                return Qt::AlignCenter;
            }
            break;
        default:
            break;
        }

        return {};
    }

    QVariant headerData( int section, Qt::Orientation orientation, int role ) const override
    {
        if ( orientation != Qt::Horizontal || role != Qt::DisplayRole ) {
            return {};
        }
        switch ( section ) {
        case 0:
            return QObject::tr( "Id" );
        case 1:
            return QObject::tr( "Action" );
        case 2:
            return QObject::tr( "Sequence" );
        case 3:
            return QObject::tr( "Enable" );
        default:
            return {};
        }
    }

    Qt::ItemFlags flags( const QModelIndex& index ) const override
    {
        if ( !index.isValid() ) {
            return Qt::NoItemFlags;
        }
        if ( index.column() == 3 ) {
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
        }
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }

    bool setData( const QModelIndex& index, const QVariant& value, int role ) override
    {
        if ( !index.isValid() || !actions_ ) {
            return false;
        }
        if ( index.column() != 3 || role != Qt::CheckStateRole ) {
            return false;
        }
        const int row = index.row();
        if ( row < 0 || row >= actions_->size() ) {
            return false;
        }
        auto& action = ( *actions_ )[ row ];
        const bool visible = value.toInt() == Qt::Checked;
        action.hidden = !visible;
        Q_EMIT dataChanged( index, index, { Qt::CheckStateRole } );
        return true;
    }

  private:
    QVector<ActionDefinition>* actions_ = nullptr;
};

class ResponsesImportTableModel : public QAbstractTableModel {
  public:
    explicit ResponsesImportTableModel( QVector<ResponseDefinition>* responses, QObject* parent = nullptr )
        : QAbstractTableModel( parent )
        , responses_( responses )
    {
    }

    void setResponses( QVector<ResponseDefinition>* responses )
    {
        beginResetModel();
        responses_ = responses;
        endResetModel();
    }

    int rowCount( const QModelIndex& parent = {} ) const override
    {
        if ( parent.isValid() || !responses_ ) {
            return 0;
        }
        return static_cast<int>( responses_->size() );
    }

    int columnCount( const QModelIndex& parent = {} ) const override
    {
        if ( parent.isValid() ) {
            return 0;
        }
        return 5;
    }

    QVariant data( const QModelIndex& index, int role ) const override
    {
        if ( !index.isValid() || !responses_ ) {
            return {};
        }
        const int row = index.row();
        if ( row < 0 || row >= responses_->size() ) {
            return {};
        }
        const auto& response = responses_->at( row );
        if ( role == Qt::ToolTipRole ) {
            QString tooltip = response.description;
            if ( !tooltip.isEmpty() ) {
                tooltip.append( '\n' );
            }
            tooltip.append(
                QObject::tr( "Match: %1 (%2)" )
                    .arg( response.match.value, responseMatchTypeToString( response.match.type ) ) );
            return tooltip;
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
            if ( role == Qt::DisplayRole ) {
                return response.name;
            }
            break;
        case 2:
            if ( role == Qt::DisplayRole ) {
                return truncateText( response.match.value, 64 );
            }
            break;
        case 3:
            if ( role == Qt::CheckStateRole ) {
                return response.enabled ? Qt::Checked : Qt::Unchecked;
            }
            if ( role == Qt::TextAlignmentRole ) {
                return Qt::AlignCenter;
            }
            break;
        case 4:
            if ( role == Qt::CheckStateRole ) {
                return response.hidden ? Qt::Unchecked : Qt::Checked;
            }
            if ( role == Qt::TextAlignmentRole ) {
                return Qt::AlignCenter;
            }
            break;
        default:
            break;
        }

        return {};
    }

    QVariant headerData( int section, Qt::Orientation orientation, int role ) const override
    {
        if ( orientation != Qt::Horizontal || role != Qt::DisplayRole ) {
            return {};
        }
        switch ( section ) {
        case 0:
            return QObject::tr( "Id" );
        case 1:
            return QObject::tr( "Response" );
        case 2:
            return QObject::tr( "Match" );
        case 3:
            return QObject::tr( "Active" );
        case 4:
            return QObject::tr( "Enable" );
        default:
            return {};
        }
    }

    Qt::ItemFlags flags( const QModelIndex& index ) const override
    {
        if ( !index.isValid() ) {
            return Qt::NoItemFlags;
        }
        if ( index.column() == 3 || index.column() == 4 ) {
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
        }
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }

    bool setData( const QModelIndex& index, const QVariant& value, int role ) override
    {
        if ( !index.isValid() || !responses_ ) {
            return false;
        }
        if ( role != Qt::CheckStateRole ) {
            return false;
        }
        const int row = index.row();
        if ( row < 0 || row >= responses_->size() ) {
            return false;
        }
        auto& response = ( *responses_ )[ row ];
        const bool checked = value.toInt() == Qt::Checked;
        if ( index.column() == 3 ) {
            response.enabled = checked;
        }
        else if ( index.column() == 4 ) {
            response.hidden = !checked;
        }
        else {
            return false;
        }
        Q_EMIT dataChanged( index, index, { Qt::CheckStateRole } );
        return true;
    }

  private:
    QVector<ResponseDefinition>* responses_ = nullptr;
};

ImportActionsDialog::ImportActionsDialog( QWidget* parent )
    : QDialog( parent )
    , actions_( ActionsManager::instance().actions() )
    , responses_( ActionsManager::instance().responses() )
{
    setWindowTitle( tr( "Import actions" ) );

    actionsModel_ = new ActionsImportTableModel( &actions_, this );
    responsesModel_ = new ResponsesImportTableModel( &responses_, this );

    actionsTable_ = new QTableView( this );
    actionsTable_->setModel( actionsModel_ );
    actionsTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
    actionsTable_->setSelectionMode( QAbstractItemView::SingleSelection );
    actionsTable_->setEditTriggers( QAbstractItemView::AllEditTriggers );
    actionsTable_->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    actionsTable_->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    actionsTable_->setWordWrap( false );
    auto* actionsHeader = actionsTable_->horizontalHeader();
    actionsHeader->setSectionResizeMode( QHeaderView::Interactive );
    actionsHeader->setStretchLastSection( false );
    actionsTable_->resizeColumnsToContents();
    capColumnWidth( actionsTable_, 1, 350 );
    capColumnWidth( actionsTable_, 2, 600 );

    responsesTable_ = new QTableView( this );
    responsesTable_->setModel( responsesModel_ );
    responsesTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
    responsesTable_->setSelectionMode( QAbstractItemView::SingleSelection );
    responsesTable_->setEditTriggers( QAbstractItemView::AllEditTriggers );
    responsesTable_->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    responsesTable_->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    responsesTable_->setWordWrap( false );
    auto* responsesHeader = responsesTable_->horizontalHeader();
    responsesHeader->setSectionResizeMode( QHeaderView::Interactive );
    responsesHeader->setStretchLastSection( false );
    responsesTable_->resizeColumnsToContents();
    capColumnWidth( responsesTable_, 1, 350 );
    capColumnWidth( responsesTable_, 2, 600 );

    removeActionButton_ = new QToolButton( this );
    removeActionButton_->setToolTip( tr( "Remove action" ) );
    removeActionButton_->setEnabled( false );
    {
        IconLoader iconLoader( this );
        removeActionButton_->setIcon( iconLoader.load( "icons8-minus-16" ) );
    }

    clearActionsButton_ = new QToolButton( this );
    clearActionsButton_->setToolTip( tr( "Clear all actions" ) );
    clearActionsButton_->setEnabled( false );
    {
        IconLoader iconLoader( this );
        clearActionsButton_->setIcon( iconLoader.load( "icons8-delete-16" ) );
    }

    removeResponseButton_ = new QToolButton( this );
    removeResponseButton_->setToolTip( tr( "Remove response" ) );
    removeResponseButton_->setEnabled( false );
    {
        IconLoader iconLoader( this );
        removeResponseButton_->setIcon( iconLoader.load( "icons8-minus-16" ) );
    }

    clearResponsesButton_ = new QToolButton( this );
    clearResponsesButton_->setToolTip( tr( "Clear all responses" ) );
    clearResponsesButton_->setEnabled( false );
    {
        IconLoader iconLoader( this );
        clearResponsesButton_->setIcon( iconLoader.load( "icons8-delete-16" ) );
    }

    auto* actionsGroup = new QGroupBox( tr( "Actions" ), this );
    auto* actionsLayout = new QVBoxLayout( actionsGroup );
    auto* actionsHeaderLayout = new QHBoxLayout();
    actionsHeaderLayout->addWidget( removeActionButton_ );
    actionsHeaderLayout->addWidget( clearActionsButton_ );
    actionsHeaderLayout->addStretch();
    actionsLayout->addLayout( actionsHeaderLayout );
    actionsLayout->addWidget( actionsTable_ );

    auto* responsesGroup = new QGroupBox( tr( "Responses" ), this );
    auto* responsesLayout = new QVBoxLayout( responsesGroup );
    auto* responsesHeaderLayout = new QHBoxLayout();
    responsesHeaderLayout->addWidget( removeResponseButton_ );
    responsesHeaderLayout->addWidget( clearResponsesButton_ );
    responsesHeaderLayout->addStretch();
    responsesLayout->addLayout( responsesHeaderLayout );
    responsesLayout->addWidget( responsesTable_ );

    auto* splitter = new QSplitter( Qt::Vertical, this );
    splitter->addWidget( actionsGroup );
    splitter->addWidget( responsesGroup );
    splitter->setStretchFactor( 0, 1 );
    splitter->setStretchFactor( 1, 1 );

    buttonBox_ = new QDialogButtonBox( QDialogButtonBox::Close, this );
    importButton_ = buttonBox_->addButton( tr( "Import" ), QDialogButtonBox::ActionRole );

    connect( importButton_, &QPushButton::clicked, this, &ImportActionsDialog::importActions );
    connect( buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject );
    connect( removeActionButton_, &QToolButton::clicked, this, &ImportActionsDialog::removeSelectedAction );
    connect( clearActionsButton_, &QToolButton::clicked, this, &ImportActionsDialog::clearActions );
    connect( removeResponseButton_, &QToolButton::clicked, this, &ImportActionsDialog::removeSelectedResponse );
    connect( clearResponsesButton_, &QToolButton::clicked, this, &ImportActionsDialog::clearResponses );
    connect( actionsModel_, &QAbstractItemModel::dataChanged, this, &ImportActionsDialog::persistChanges );
    connect( responsesModel_, &QAbstractItemModel::dataChanged, this, &ImportActionsDialog::persistChanges );
    if ( actionsTable_->selectionModel() ) {
        connect( actionsTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                 [ this ]( const QItemSelection&, const QItemSelection& ) { updateButtons(); } );
    }
    if ( responsesTable_->selectionModel() ) {
        connect( responsesTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                 [ this ]( const QItemSelection&, const QItemSelection& ) { updateButtons(); } );
    }

    auto* layout = new QVBoxLayout( this );
    layout->addWidget( splitter );
    layout->addWidget( buttonBox_ );
    setLayout( layout );

    updateButtons();
    updateDialogSize();
}

void ImportActionsDialog::importActions()
{
    const auto file = QFileDialog::getOpenFileName(
        this, tr( "Select actions file" ), "",
        tr( "Actions JSON (*.json);;Docklight Project (*.ptp);;All files (*)" ) );
    if ( file.isEmpty() ) {
        return;
    }

    const auto parsed = parseActionsConfigFile( file, ActionsImportFormat::Auto );
    if ( !parsed.errors.isEmpty() ) {
        QMessageBox::warning( this, tr( "Import actions" ), parsed.errors.join( "\n" ) );
        return;
    }
    if ( !parsed.warnings.isEmpty() ) {
        QMessageBox::information( this, tr( "Import actions" ), parsed.warnings.join( "\n" ) );
    }

    auto mergeResult = mergeActionsConfig( actions_,
                                           responses_,
                                           parsed.actions,
                                           parsed.responses,
                                           ActionsConflictPolicy::Fail );
    if ( !mergeResult.conflicts.isEmpty() ) {
        QMessageBox conflictBox( QMessageBox::Question,
                                 tr( "Import actions" ),
                                 tr( "%1 conflicts were found while importing actions.\n\n%2" )
                                     .arg( mergeResult.conflicts.size() )
                                     .arg( actionConflictSummaries( mergeResult.conflicts )
                                               .join( "\n" ) ),
                                 QMessageBox::Cancel,
                                 this );
        auto* keepButton = conflictBox.addButton( tr( "Use Existing" ),
                                                  QMessageBox::AcceptRole );
        auto* importedButton = conflictBox.addButton( tr( "Use Imported" ),
                                                      QMessageBox::DestructiveRole );
        conflictBox.exec();

        if ( conflictBox.clickedButton() == keepButton ) {
            mergeResult = mergeActionsConfig( actions_,
                                              responses_,
                                              parsed.actions,
                                              parsed.responses,
                                              ActionsConflictPolicy::KeepExisting );
        }
        else if ( conflictBox.clickedButton() == importedButton ) {
            mergeResult = mergeActionsConfig( actions_,
                                              responses_,
                                              parsed.actions,
                                              parsed.responses,
                                              ActionsConflictPolicy::UseImported );
        }
        else {
            return;
        }
    }

    if ( !mergeResult.errors.isEmpty() ) {
        QMessageBox::warning( this, tr( "Import actions" ), mergeResult.errors.join( "\n" ) );
        return;
    }

    if ( QMessageBox::question(
             this, tr( "Import actions" ),
             tr( "Apply imported actions?\n\nAdded: %1\nUpdated: %2\nSkipped: %3\nConflicts: %4" )
                 .arg( mergeResult.added )
                 .arg( mergeResult.updated )
                 .arg( mergeResult.skipped )
                 .arg( mergeResult.conflicts.size() ) )
         != QMessageBox::Yes ) {
        return;
    }

    suppressPersist_ = true;
    actions_ = mergeResult.actions;
    responses_ = mergeResult.responses;
    actionsModel_->setActions( &actions_ );
    responsesModel_->setResponses( &responses_ );
    suppressPersist_ = false;

    actionsTable_->resizeColumnsToContents();
    responsesTable_->resizeColumnsToContents();
    capColumnWidth( actionsTable_, 1, 350 );
    capColumnWidth( actionsTable_, 2, 600 );
    capColumnWidth( responsesTable_, 1, 350 );
    capColumnWidth( responsesTable_, 2, 600 );
    actionsTable_->viewport()->update();
    responsesTable_->viewport()->update();
    updateButtons();
    updateDialogSize();
    persistChanges();
}

void ImportActionsDialog::removeSelectedAction()
{
    if ( !actionsTable_ || !actionsTable_->selectionModel() ) {
        return;
    }
    const auto index = actionsTable_->selectionModel()->currentIndex();
    if ( !index.isValid() ) {
        return;
    }
    const int row = index.row();
    if ( row < 0 || row >= actions_.size() ) {
        return;
    }
    actions_.removeAt( row );
    if ( actions_.isEmpty() ) {
        pendingActionSelectionRow_ = -1;
    }
    else if ( row >= actions_.size() ) {
        pendingActionSelectionRow_ = static_cast<int>( actions_.size() ) - 1;
    }
    else {
        pendingActionSelectionRow_ = row;
    }
    actionsModel_->setActions( &actions_ );
    if ( pendingActionSelectionRow_ >= 0 ) {
        actionsTable_->selectRow( pendingActionSelectionRow_ );
        actionsTable_->setCurrentIndex( actionsModel_->index( pendingActionSelectionRow_, 0 ) );
    }
    pendingActionSelectionRow_ = -1;
    updateButtons();
    updateDialogSize();
    persistChanges();
}

void ImportActionsDialog::clearActions()
{
    actions_.clear();
    pendingActionSelectionRow_ = -1;
    actionsModel_->setActions( &actions_ );
    updateButtons();
    updateDialogSize();
    persistChanges();
}

void ImportActionsDialog::removeSelectedResponse()
{
    if ( !responsesTable_ || !responsesTable_->selectionModel() ) {
        return;
    }
    const auto index = responsesTable_->selectionModel()->currentIndex();
    if ( !index.isValid() ) {
        return;
    }
    const int row = index.row();
    if ( row < 0 || row >= responses_.size() ) {
        return;
    }
    responses_.removeAt( row );
    if ( responses_.isEmpty() ) {
        pendingResponseSelectionRow_ = -1;
    }
    else if ( row >= responses_.size() ) {
        pendingResponseSelectionRow_ = static_cast<int>( responses_.size() ) - 1;
    }
    else {
        pendingResponseSelectionRow_ = row;
    }
    responsesModel_->setResponses( &responses_ );
    if ( pendingResponseSelectionRow_ >= 0 ) {
        responsesTable_->selectRow( pendingResponseSelectionRow_ );
        responsesTable_->setCurrentIndex( responsesModel_->index( pendingResponseSelectionRow_, 0 ) );
    }
    pendingResponseSelectionRow_ = -1;
    updateButtons();
    updateDialogSize();
    persistChanges();
}

void ImportActionsDialog::clearResponses()
{
    responses_.clear();
    pendingResponseSelectionRow_ = -1;
    responsesModel_->setResponses( &responses_ );
    updateButtons();
    updateDialogSize();
    persistChanges();
}

void ImportActionsDialog::persistChanges()
{
    if ( suppressPersist_ ) {
        return;
    }
    const auto result
        = ActionsManager::instance().importFromDefinitions( actions_, responses_ );
    if ( !result.errors.isEmpty() ) {
        QMessageBox::warning( this, tr( "Import actions" ), result.errors.join( "\n" ) );
    }
}

void ImportActionsDialog::updateButtons()
{
    const bool hasActionsSelection = actionsTable_ && actionsTable_->selectionModel()
                                     && actionsTable_->selectionModel()->hasSelection();
    const bool hasResponsesSelection = responsesTable_ && responsesTable_->selectionModel()
                                       && responsesTable_->selectionModel()->hasSelection();
    if ( removeActionButton_ ) {
        removeActionButton_->setEnabled( hasActionsSelection );
    }
    if ( clearActionsButton_ ) {
        clearActionsButton_->setEnabled( !actions_.isEmpty() );
    }
    if ( removeResponseButton_ ) {
        removeResponseButton_->setEnabled( hasResponsesSelection );
    }
    if ( clearResponsesButton_ ) {
        clearResponsesButton_->setEnabled( !responses_.isEmpty() );
    }
    if ( importButton_ ) {
        importButton_->setEnabled( true );
    }
}

void ImportActionsDialog::updateDialogSize()
{
    if ( !actionsTable_ || !responsesTable_ ) {
        return;
    }
    const int previousActionsMin = actionsTable_->minimumHeight();
    const int previousResponsesMin = responsesTable_->minimumHeight();
    actionsTable_->setMinimumHeight( tableHeightForRows( actionsTable_, 10 ) );
    responsesTable_->setMinimumHeight( tableHeightForRows( responsesTable_, 10 ) );
    if ( layout() ) {
        layout()->activate();
    }
    const QSize hint = sizeHint();
    actionsTable_->setMinimumHeight( previousActionsMin );
    responsesTable_->setMinimumHeight( previousResponsesMin );

    const int widthHint = qMax( tableWidthHint( actionsTable_ ),
                                tableWidthHint( responsesTable_ ) );
    const int desiredWidth
        = qMax( clampToScreenWidth( this, qMax( widthHint, 925 ) ),
                minimumSizeHint().width() );
    const int desiredHeight = qMax( hint.height(), minimumSizeHint().height() );
    if ( !sizeInitialized_ ) {
        resize( desiredWidth, desiredHeight );
        sizeInitialized_ = true;
    }
}
