#include "configurationexportdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "log.h"

ConfigurationExportDialog::ConfigurationExportDialog( ConfigurationExportKind kind,
                                                      const QStringList& names, QWidget* parent )
    : QDialog( parent )
{
    setObjectName( QStringLiteral( "configurationExportDialog" ) );
    setWindowTitle( ConfigurationExport::title( kind ) );
    resize( 520, 460 );
    auto* layout = new QVBoxLayout( this );
    filter_ = new QLineEdit( this );
    filter_->setObjectName( QStringLiteral( "exportNameFilter" ) );
    filter_->setAccessibleName( tr( "Filter by name" ) );
    filter_->setPlaceholderText( tr( "Filter by name…" ) );
    filter_->setClearButtonEnabled( true );
    layout->addWidget( filter_ );
    filter_->setVisible( kind != ConfigurationExportKind::PredefinedFilters );

    auto* controls = new QHBoxLayout;
    auto* select = new QPushButton( tr( "Select all" ), this );
    select->setObjectName( QStringLiteral( "exportSelectAll" ) );
    auto* deselect = new QPushButton( tr( "Deselect all" ), this );
    deselect->setObjectName( QStringLiteral( "exportDeselectAll" ) );
    select->setToolTip( tr( "Select all items matching the name filter" ) );
    deselect->setToolTip( tr( "Deselect all items matching the name filter" ) );
    controls->addWidget( select );
    controls->addWidget( deselect );
    controls->addStretch();
    layout->addLayout( controls );

    list_ = new QListWidget( this );
    list_->setObjectName( QStringLiteral( "exportItems" ) );
    list_->setAccessibleName( tr( "Configurations to export" ) );
    for ( const auto& name : names ) {
        auto* item = new QListWidgetItem( name, list_ );
        item->setFlags( item->flags() | Qt::ItemIsUserCheckable );
        item->setCheckState( Qt::Checked );
    }
    layout->addWidget( list_ );
    count_ = new QLabel( this );
    count_->setObjectName( QStringLiteral( "exportSelectionCount" ) );
    layout->addWidget( count_ );
    if ( kind == ConfigurationExportKind::Highlights ) {
        multipleFiles_ = new QCheckBox( tr( "Save each group to a separate file" ), this );
        multipleFiles_->setObjectName( QStringLiteral( "exportMultipleFiles" ) );
        layout->addWidget( multipleFiles_ );
    }
    buttons_ = new QDialogButtonBox( QDialogButtonBox::Save | QDialogButtonBox::Cancel, this );
    buttons_->button( QDialogButtonBox::Save )->setText( tr( "Export…" ) );
    layout->addWidget( buttons_ );
    connect( select, &QPushButton::clicked, this, [ this ] { selectVisible( true ); } );
    connect( deselect, &QPushButton::clicked, this, [ this ] { selectVisible( false ); } );
    connect( filter_, &QLineEdit::textChanged, this, &ConfigurationExportDialog::filterNames );
    connect( list_, &QListWidget::itemChanged, this,
             [ this ]( QListWidgetItem* ) { updateSelection(); } );
    connect( buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept );
    connect( buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject );
    updateSelection();
    LOG_DEBUG << "Opened configuration export selection: kind=" << static_cast<int>( kind )
              << " items=" << names.size();
}

QVector<int> ConfigurationExportDialog::selectedRows() const
{
    QVector<int> rows;
    for ( int i = 0; i < list_->count(); ++i ) {
        if ( list_->item( i )->checkState() == Qt::Checked ) {
            rows.append( i );
        }
    }
    return rows;
}

bool ConfigurationExportDialog::multipleFiles() const
{
    return multipleFiles_ && multipleFiles_->isChecked();
}

void ConfigurationExportDialog::selectVisible( bool checked )
{
    LOG_DEBUG << "Export bulk selection: checked=" << checked << " filter=" << filter_->text();
    const QSignalBlocker blocker( list_ );
    for ( int i = 0; i < list_->count(); ++i ) {
        auto* item = list_->item( i );
        if ( !item->isHidden() ) {
            item->setCheckState( checked ? Qt::Checked : Qt::Unchecked );
        }
    }
    updateSelection();
}

void ConfigurationExportDialog::filterNames( const QString& text )
{
    int visible = 0;
    for ( int i = 0; i < list_->count(); ++i ) {
        auto* item = list_->item( i );
        const bool matches = item->text().contains( text, Qt::CaseInsensitive );
        item->setHidden( !matches );
        visible += matches ? 1 : 0;
    }
    LOG_DEBUG << "Export name filter changed: visible=" << visible << " total=" << list_->count();
    updateSelection();
}

void ConfigurationExportDialog::updateSelection()
{
    const auto selected = selectedRows().size();
    count_->setText(
        tr( "%1 of %2 selected (including hidden items)" ).arg( selected ).arg( list_->count() ) );
    buttons_->button( QDialogButtonBox::Save )->setEnabled( selected > 0 );
    LOG_DEBUG << "Export selection count=" << selected;
}
