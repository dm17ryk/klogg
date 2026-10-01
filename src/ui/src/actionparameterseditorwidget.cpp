#include "actionparameterseditorwidget.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "actionparameterdefinitiondialog.h"
#include "log.h"

ActionParametersEditorWidget::ActionParametersEditorWidget( QWidget* parent )
    : QWidget( parent )
{
    table_ = new QTableWidget( this );
    table_->setObjectName( QStringLiteral( "actionParametersDefinitionTable" ) );
    table_->setColumnCount( 7 );
    table_->setHorizontalHeaderLabels( { tr( "#" ), tr( "Name" ), tr( "Label" ), tr( "Type" ),
                                         tr( "Presentation" ), tr( "Required" ), tr( "Choices" ) } );
    table_->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
    table_->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
    table_->horizontalHeader()->setSectionResizeMode( 2, QHeaderView::Stretch );
    table_->horizontalHeader()->setSectionResizeMode( 3, QHeaderView::ResizeToContents );
    table_->horizontalHeader()->setSectionResizeMode( 4, QHeaderView::ResizeToContents );
    table_->horizontalHeader()->setSectionResizeMode( 5, QHeaderView::ResizeToContents );
    table_->horizontalHeader()->setSectionResizeMode( 6, QHeaderView::ResizeToContents );
    table_->setSelectionBehavior( QAbstractItemView::SelectRows );
    table_->setSelectionMode( QAbstractItemView::SingleSelection );
    table_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    table_->setMinimumHeight( 150 );

    auto* buttons = new QHBoxLayout;
    auto* addButton = new QPushButton( tr( "Add" ), this );
    editButton_ = new QPushButton( tr( "Edit" ), this );
    deleteButton_ = new QPushButton( tr( "Delete" ), this );
    moveUpButton_ = new QPushButton( tr( "Move Up" ), this );
    moveDownButton_ = new QPushButton( tr( "Move Down" ), this );
    buttons->addWidget( addButton );
    buttons->addWidget( editButton_ );
    buttons->addWidget( deleteButton_ );
    buttons->addWidget( moveUpButton_ );
    buttons->addWidget( moveDownButton_ );
    buttons->addStretch();

    auto* layout = new QVBoxLayout( this );
    layout->addWidget( table_ );
    layout->addLayout( buttons );
    connect( addButton, &QPushButton::clicked, this, &ActionParametersEditorWidget::addField );
    connect( editButton_, &QPushButton::clicked, this, &ActionParametersEditorWidget::editField );
    connect( deleteButton_, &QPushButton::clicked, this, &ActionParametersEditorWidget::deleteField );
    connect( moveUpButton_, &QPushButton::clicked, this,
             [ this ] { moveField( -1 ); } );
    connect( moveDownButton_, &QPushButton::clicked, this,
             [ this ] { moveField( 1 ); } );
    connect( table_, &QTableWidget::itemSelectionChanged, this, [ this ] {
        const auto row = selectedRow();
        const auto has = row >= 0;
        editButton_->setEnabled( has );
        deleteButton_->setEnabled( has );
        moveUpButton_->setEnabled( has && row > 0 );
        moveDownButton_->setEnabled( has && row + 1 < fields_.size() );
    } );
    connect( table_, &QTableWidget::cellDoubleClicked, this,
             [ this ]( int row, int ) {
                 if ( row >= 0 && row < fields_.size() ) {
                     table_->selectRow( row );
                     LOG_DEBUG << "Opening action parameter definition from double-click: row=" << row;
                     editField();
                 }
             } );
}

void ActionParametersEditorWidget::setFields( const QVector<ActionParameterDefinition>& fields )
{
    fields_ = fields;
    refreshTable();
}

QVector<ActionParameterDefinition> ActionParametersEditorWidget::fields() const
{
    return fields_;
}

int ActionParametersEditorWidget::selectedRow() const
{
    const auto rows = table_->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.front().row();
}

void ActionParametersEditorWidget::refreshTable()
{
    table_->setRowCount( static_cast<int>( fields_.size() ) );
    for ( int row = 0; row < fields_.size(); ++row ) {
        const auto& field = fields_.at( row );
        table_->setItem( row, 0, new QTableWidgetItem( QString::number( row + 1 ) ) );
        table_->setItem( row, 1, new QTableWidgetItem( field.name ) );
        table_->setItem( row, 2, new QTableWidgetItem( field.label ) );
        table_->setItem( row, 3, new QTableWidgetItem( actionParameterTypeToString( field.type ) ) );
        table_->setItem( row, 4,
                         new QTableWidgetItem( actionParameterPresentationToString( field.presentation ) ) );
        table_->setItem( row, 5, new QTableWidgetItem( field.required ? tr( "Yes" ) : tr( "No" ) ) );
        table_->setItem( row, 6, new QTableWidgetItem( QString::number( field.choices.size() ) ) );
    }
    table_->clearSelection();
}

void ActionParametersEditorWidget::addField()
{
    ActionParameterDefinitionDialog dialog( this );
    ActionParameterDefinition field;
    field.name = QStringLiteral( "parameter%1" ).arg( fields_.size() + 1 );
    field.label = field.name;
    dialog.setDefinition( field );
    if ( dialog.exec() != QDialog::Accepted ) {
        return;
    }
    fields_.push_back( dialog.definition() );
    LOG_DEBUG << "Added action parameter field " << fields_.back().name.toStdString();
    refreshTable();
    table_->selectRow( table_->rowCount() - 1 );
}

void ActionParametersEditorWidget::editField()
{
    const auto row = selectedRow();
    if ( row < 0 || row >= fields_.size() ) {
        return;
    }
    ActionParameterDefinitionDialog dialog( this );
    dialog.setDefinition( fields_.at( row ) );
    if ( dialog.exec() != QDialog::Accepted ) {
        return;
    }
    fields_[row] = dialog.definition();
    LOG_DEBUG << "Edited action parameter field " << fields_.at( row ).name.toStdString();
    refreshTable();
    table_->selectRow( row );
}

void ActionParametersEditorWidget::deleteField()
{
    const auto row = selectedRow();
    if ( row < 0 || row >= fields_.size() ) {
        return;
    }
    LOG_DEBUG << "Deleted action parameter field " << fields_.at( row ).name.toStdString();
    fields_.removeAt( row );
    refreshTable();
}

void ActionParametersEditorWidget::moveField( int offset )
{
    const auto row = selectedRow();
    const auto target = row + offset;
    if ( row < 0 || target < 0 || target >= fields_.size() ) {
        return;
    }
    fields_.swapItemsAt( row, target );
    LOG_DEBUG << "Moved action parameter field to row " << target;
    refreshTable();
    table_->selectRow( target );
}
