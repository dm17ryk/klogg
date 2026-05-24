#include "importpreviewsdialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtGlobal>

#include "iconloader.h"
#include "previewmanager.h"
#include "previewstablemodel.h"

ImportPreviewsDialog::ImportPreviewsDialog( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Import previews" ) );

    model_ = new PreviewsTableModel( this );

    tableView_ = new QTableView( this );
    tableView_->setObjectName( QStringLiteral( "importPreviewsTable" ) );
    tableView_->setModel( model_ );
    tableView_->setSelectionBehavior( QAbstractItemView::SelectRows );
    tableView_->setSelectionMode( QAbstractItemView::SingleSelection );
    tableView_->setEditTriggers( QAbstractItemView::AllEditTriggers );
    tableView_->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    tableView_->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    tableView_->setWordWrap( false );
    tableView_->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
    tableView_->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );
    tableView_->horizontalHeader()->setSectionResizeMode( 2, QHeaderView::ResizeToContents );
    tableView_->verticalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );

    moveUpButton_ = new QToolButton( this );
    moveUpButton_->setObjectName( QStringLiteral( "movePreviewUpButton" ) );
    moveUpButton_->setAccessibleName( tr( "Move preview up" ) );
    moveUpButton_->setToolTip( tr( "Move preview up" ) );
    moveUpButton_->setEnabled( false );
    {
        IconLoader iconLoader( this );
        moveUpButton_->setIcon( iconLoader.load( "icons8-up-16" ) );
    }

    moveDownButton_ = new QToolButton( this );
    moveDownButton_->setObjectName( QStringLiteral( "movePreviewDownButton" ) );
    moveDownButton_->setAccessibleName( tr( "Move preview down" ) );
    moveDownButton_->setToolTip( tr( "Move preview down" ) );
    moveDownButton_->setEnabled( false );
    {
        IconLoader iconLoader( this );
        moveDownButton_->setIcon( iconLoader.load( "icons8-down-arrow-16" ) );
    }

    removeButton_ = new QToolButton( this );
    removeButton_->setObjectName( QStringLiteral( "removePreviewButton" ) );
    removeButton_->setAccessibleName( tr( "Remove preview" ) );
    removeButton_->setToolTip( tr( "Remove preview" ) );
    removeButton_->setEnabled( false );
    {
        IconLoader iconLoader( this );
        removeButton_->setIcon( iconLoader.load( "icons8-minus-16" ) );
    }

    clearButton_ = new QToolButton( this );
    clearButton_->setObjectName( QStringLiteral( "clearPreviewsButton" ) );
    clearButton_->setAccessibleName( tr( "Clear all previews" ) );
    clearButton_->setToolTip( tr( "Clear all previews" ) );
    clearButton_->setEnabled( false );
    {
        IconLoader iconLoader( this );
        clearButton_->setIcon( iconLoader.load( "icons8-delete-16" ) );
    }

    buttonBox_ = new QDialogButtonBox( QDialogButtonBox::Close, this );
    auto* importButton = buttonBox_->addButton( tr( "Import" ), QDialogButtonBox::ActionRole );
    importButton->setObjectName( QStringLiteral( "importPreviewsButton" ) );
    importButton->setAccessibleName( tr( "Import previews" ) );

    connect( importButton, &QPushButton::clicked, this, &ImportPreviewsDialog::importPreviews );
    connect( moveUpButton_, &QToolButton::clicked, this,
             &ImportPreviewsDialog::moveSelectedPreviewUp );
    connect( moveDownButton_, &QToolButton::clicked, this,
             &ImportPreviewsDialog::moveSelectedPreviewDown );
    connect( removeButton_, &QToolButton::clicked, this,
             &ImportPreviewsDialog::removeSelectedPreview );
    connect( clearButton_, &QToolButton::clicked, this, &ImportPreviewsDialog::clearAllPreviews );
    connect( buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject );
    connect( &PreviewManager::instance(), &PreviewManager::previewsChanged, this,
             &ImportPreviewsDialog::refreshTable );
    connect( tableView_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
             [ this ]( const QItemSelection&, const QItemSelection& ) { updateButtons(); } );

    auto* layout = new QVBoxLayout();
    auto* headerLayout = new QHBoxLayout();
    headerLayout->addWidget( moveUpButton_ );
    headerLayout->addWidget( moveDownButton_ );
    headerLayout->addWidget( removeButton_ );
    headerLayout->addWidget( clearButton_ );
    headerLayout->addStretch();
    layout->addLayout( headerLayout );
    layout->addWidget( tableView_ );
    layout->addWidget( buttonBox_ );
    setLayout( layout );

    refreshTable();
}

void ImportPreviewsDialog::importPreviews()
{
    const auto file = QFileDialog::getOpenFileName( this, tr( "Select previews JSON" ), "",
                                                    tr( "Previews (*.json);;All files (*)" ) );
    if ( file.isEmpty() ) {
        return;
    }

    const auto result = PreviewManager::instance().importFromFile( file );
    if ( !result.errors.isEmpty() ) {
        QMessageBox::warning( this, tr( "Import previews" ), result.errors.join( "\n" ) );
        return;
    }
    if ( !result.warnings.isEmpty() ) {
        QMessageBox::information( this, tr( "Import previews" ), result.warnings.join( "\n" ) );
    }
}

void ImportPreviewsDialog::moveSelectedPreviewUp()
{
    moveSelectedPreview( -1 );
}

void ImportPreviewsDialog::moveSelectedPreviewDown()
{
    moveSelectedPreview( 1 );
}

void ImportPreviewsDialog::moveSelectedPreview( int delta )
{
    if ( !tableView_->selectionModel() ) {
        return;
    }
    const auto index = tableView_->selectionModel()->currentIndex();
    if ( !index.isValid() ) {
        return;
    }

    const int row = index.row();
    const int targetRow = row + delta;
    if ( targetRow < 0 || targetRow >= model_->rowCount() ) {
        return;
    }

    pendingSelectionRow_ = targetRow;
    if ( !PreviewManager::instance().movePreview( row, targetRow ) ) {
        pendingSelectionRow_ = -1;
        QMessageBox::warning( this, tr( "Move preview" ), tr( "Failed to move preview." ) );
    }
}

void ImportPreviewsDialog::removeSelectedPreview()
{
    if ( !tableView_->selectionModel() ) {
        return;
    }
    const auto index = tableView_->selectionModel()->currentIndex();
    if ( !index.isValid() ) {
        return;
    }
    const auto& previews = PreviewManager::instance().all();
    if ( index.row() < 0 || index.row() >= previews.size() ) {
        return;
    }
    const auto row = index.row();
    const auto name = previews.at( row ).name;
    const int nextRow = ( row >= previews.size() - 1 ) ? row - 1 : row;
    pendingSelectionRow_ = nextRow;
    if ( !PreviewManager::instance().removeByName( name ) ) {
        QMessageBox::warning( this, tr( "Remove preview" ), tr( "Failed to remove preview." ) );
        pendingSelectionRow_ = -1;
    }
}

void ImportPreviewsDialog::clearAllPreviews()
{
    pendingSelectionRow_ = -1;
    if ( !PreviewManager::instance().clearAll() ) {
        QMessageBox::warning( this, tr( "Clear previews" ), tr( "Failed to clear previews." ) );
    }
}

void ImportPreviewsDialog::refreshTable()
{
    model_->refresh();
    tableView_->resizeColumnToContents( 0 );
    tableView_->resizeColumnToContents( 2 );
    updateDialogWidth();
    updateButtons();

    const int rowCount = model_->rowCount();
    if ( pendingSelectionRow_ >= 0 && rowCount > 0 ) {
        const int row = qMin( pendingSelectionRow_, rowCount - 1 );
        tableView_->selectRow( row );
        tableView_->setCurrentIndex( model_->index( row, 0 ) );
    }
    pendingSelectionRow_ = -1;
}

void ImportPreviewsDialog::updateButtons()
{
    const bool hasSelection
        = tableView_->selectionModel() && tableView_->selectionModel()->hasSelection();
    const auto currentIndex = tableView_->selectionModel()
                                  ? tableView_->selectionModel()->currentIndex()
                                  : QModelIndex();
    const int currentRow = currentIndex.isValid() ? currentIndex.row() : -1;
    const int rowCount = model_->rowCount();
    moveUpButton_->setEnabled( hasSelection && currentRow > 0 );
    moveDownButton_->setEnabled( hasSelection && currentRow >= 0 && currentRow < rowCount - 1 );
    removeButton_->setEnabled( hasSelection );
    clearButton_->setEnabled( rowCount > 0 );
}

void ImportPreviewsDialog::updateDialogWidth()
{
    if ( !tableView_->model() ) {
        return;
    }

    int width = tableView_->frameWidth() * 2;
    if ( tableView_->verticalHeader()->isVisible() ) {
        width += tableView_->verticalHeader()->width();
    }
    auto* header = tableView_->horizontalHeader();
    const auto mode0 = header->sectionResizeMode( 0 );
    const auto mode1 = header->sectionResizeMode( 1 );
    const auto mode2 = header->sectionResizeMode( 2 );
    header->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
    header->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
    header->setSectionResizeMode( 2, QHeaderView::ResizeToContents );
    tableView_->resizeColumnsToContents();
    for ( int column = 0; column < tableView_->model()->columnCount(); ++column ) {
        width += tableView_->columnWidth( column );
    }
    header->setSectionResizeMode( 0, mode0 );
    header->setSectionResizeMode( 1, mode1 );
    header->setSectionResizeMode( 2, mode2 );
    if ( tableView_->verticalScrollBar() ) {
        width += tableView_->verticalScrollBar()->sizeHint().width();
    }
    const auto margins = layout()->contentsMargins();
    width += margins.left() + margins.right();

    const int desiredWidth = qMax( width, minimumSizeHint().width() );
    setMinimumWidth( desiredWidth );
    if ( this->width() < desiredWidth ) {
        resize( desiredWidth, height() );
    }
}
