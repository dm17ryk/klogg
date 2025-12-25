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
#include <QtGlobal>
#include <QVBoxLayout>

#include "iconloader.h"
#include "previewmanager.h"
#include "previewstablemodel.h"

ImportPreviewsDialog::ImportPreviewsDialog( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Import previews" ) );

    model_ = new PreviewsTableModel( this );

    tableView_ = new QTableView( this );
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

    removeButton_ = new QToolButton( this );
    removeButton_->setToolTip( tr( "Remove preview" ) );
    removeButton_->setEnabled( false );
    {
        IconLoader iconLoader( this );
        removeButton_->setIcon( iconLoader.load( "icons8-minus-16" ) );
    }

    clearButton_ = new QToolButton( this );
    clearButton_->setToolTip( tr( "Clear all previews" ) );
    clearButton_->setEnabled( false );
    {
        IconLoader iconLoader( this );
        clearButton_->setIcon( iconLoader.load( "icons8-delete-16" ) );
    }

    buttonBox_ = new QDialogButtonBox( QDialogButtonBox::Close, this );
    auto* importButton = buttonBox_->addButton( tr( "Import" ), QDialogButtonBox::ActionRole );

    connect( importButton, &QPushButton::clicked, this, &ImportPreviewsDialog::importPreviews );
    connect( removeButton_, &QToolButton::clicked, this, &ImportPreviewsDialog::removeSelectedPreview );
    connect( clearButton_, &QToolButton::clicked, this, &ImportPreviewsDialog::clearAllPreviews );
    connect( buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject );
    connect( &PreviewManager::instance(), &PreviewManager::previewsChanged, this,
             &ImportPreviewsDialog::refreshTable );
    connect( tableView_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
             [ this ]( const QItemSelection&, const QItemSelection& ) { updateButtons(); } );

    auto* layout = new QVBoxLayout();
    auto* headerLayout = new QHBoxLayout();
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
        QMessageBox::warning( this, tr( "Import previews" ),
                              result.errors.join( "\n" ) );
        return;
    }
    if ( !result.warnings.isEmpty() ) {
        QMessageBox::information( this, tr( "Import previews" ),
                                  result.warnings.join( "\n" ) );
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
    const bool hasSelection = tableView_->selectionModel()
                              && tableView_->selectionModel()->hasSelection();
    removeButton_->setEnabled( hasSelection );
    clearButton_->setEnabled( model_->rowCount() > 0 );
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
