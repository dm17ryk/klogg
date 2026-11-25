/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "predefinedfiltersdialog.h"
#include "ui_predefinedfiltersdialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QTimer>
#include <QToolButton>
#include <qboxlayout.h>
#include <qcheckbox.h>
#include <qglobal.h>
#include <qwidget.h>

#include "dispatch_to.h"
#include "iconloader.h"
#include "log.h"
#include "packcanonical.h"
#include "predefinedfilters.h"

#include <QSet>
#include <algorithm>

namespace {
constexpr auto FilterIdRole = Qt::UserRole + 1;
constexpr auto PackIdRole = Qt::UserRole + 2;   // a pack row: its pack's ID
constexpr auto ReplacedRole = Qt::UserRole + 3; // a pack row one of the user's filters replaces
constexpr int SourceColumn = 3;
}

class CenteredCheckbox : public QWidget {
  public:
    explicit CenteredCheckbox( QWidget* parent = nullptr )
        : QWidget( parent )
    {
        auto layout = new QHBoxLayout;
        layout->setAlignment( Qt::AlignCenter );
        checkbox_ = new QCheckBox;
        layout->addWidget( checkbox_ );
        this->setLayout( layout );

        QPalette palette = this->palette();
        palette.setColor( QPalette::Base, palette.color( QPalette::Window ) );
        checkbox_->setPalette( palette );
    }

    bool isChecked() const
    {
        return checkbox_->isChecked();
    }

    void setChecked( bool isChecked )
    {
        checkbox_->setChecked( isChecked );
    }

  private:
    QCheckBox* checkbox_;
};

PredefinedFiltersDialog::PredefinedFiltersDialog( QWidget* parent )
    : QDialog( parent )
    , ui_( std::make_unique<Ui::PredefinedFiltersDialog>() )
{
    ui_->setupUi( this );

    // Configuration pack filters (E16c): copied into the user's own, edited in place, and the
    // packs themselves in the Plugins and Packs window.
    const auto textButton = [ this ]( const char* name, const QString& text, const QString& tip ) {
        auto* button = new QToolButton( this );
        button->setObjectName( QLatin1String( name ) );
        button->setText( text );
        button->setAccessibleName( text );
        button->setToolTip( tip );
        button->setToolButtonStyle( Qt::ToolButtonTextOnly );
        return button;
    };
    copyPackFilterButton_ = textButton(
        "copyPackFilterButton", tr( "Copy to My Filters" ),
        tr( "Copy this pack filter into your own filters. Your copy replaces the pack's, and "
            "updates of the pack do not change it." ) );
    editPackFilterButton_ = textButton(
        "editPackFilterButton", tr( "Edit..." ),
        tr( "Edit this pack filter in place. Updates of the pack keep your edit and ask when they "
            "change the same field." ) );
    packsButton_ = textButton( "packsButton", tr( "Packs..." ),
                               tr( "Install, update and manage configuration packs." ) );
    editPackFilterButton_->setVisible( false );
    packsButton_->setVisible( false );
    const auto at = ui_->horizontalLayout_5->indexOf( ui_->exportFilterButton ) + 1;
    ui_->horizontalLayout_5->insertWidget( at, packsButton_ );
    ui_->horizontalLayout_5->insertWidget( at, editPackFilterButton_ );
    ui_->horizontalLayout_5->insertWidget( at, copyPackFilterButton_ );
    connect( copyPackFilterButton_, &QToolButton::clicked, this,
             &PredefinedFiltersDialog::copyPackFilter );
    connect( editPackFilterButton_, &QToolButton::clicked, this,
             &PredefinedFiltersDialog::editPackFilter );
    connect( packsButton_, &QToolButton::clicked, this, [ this ] {
        if ( packsOpener_ ) {
            packsOpener_();
        }
    } );
    // A pack enabled, disabled, updated or edited meanwhile: its rows follow.
    connect( &PackFilters::instance(), &PackFilters::changed, this, [ this ] {
        refreshPackRows();
        updateButtons();
    } );

    populateFiltersTable( PredefinedFiltersCollection::getSynced().getFilters() );

    connect( ui_->addFilterButton, &QToolButton::clicked, this,
             &PredefinedFiltersDialog::addFilter );
    connect( ui_->removeFilterButton, &QToolButton::clicked, this,
             &PredefinedFiltersDialog::removeFilter );
    connect( ui_->upButton, &QToolButton::clicked, this, &PredefinedFiltersDialog::moveFilterUp );
    connect( ui_->downButton, &QToolButton::clicked, this,
             &PredefinedFiltersDialog::moveFilterDown );
    connect( ui_->importFilterButton, &QToolButton::clicked, this,
             &PredefinedFiltersDialog::importFilters );
    connect( ui_->exportFilterButton, &QToolButton::clicked, this,
             &PredefinedFiltersDialog::exportFilters );

    connect( ui_->buttonBox, &QDialogButtonBox::clicked, this,
             &PredefinedFiltersDialog::resolveStandardButton );

    connect( ui_->filtersTableWidget, &QTableWidget::currentCellChanged, this,
             &PredefinedFiltersDialog::onCurrentCellChanged );

    dispatchToObject( [ this ] {
        IconLoader iconLoader( this );

        ui_->addFilterButton->setIcon( iconLoader.load( "icons8-plus-16" ) );
        ui_->removeFilterButton->setIcon( iconLoader.load( "icons8-minus-16" ) );
        ui_->upButton->setIcon( iconLoader.load( "icons8-up-16" ) );
        ui_->downButton->setIcon( iconLoader.load( "icons8-down-arrow-16" ) );
    }, this );
}

PredefinedFiltersDialog::PredefinedFiltersDialog( const QString& newFilter, QWidget* parent )
    : PredefinedFiltersDialog( parent )
{
    if ( !newFilter.isEmpty() ) {
        addFilterRow( newFilter );
    }
}

PredefinedFiltersDialog::~PredefinedFiltersDialog() = default;

void PredefinedFiltersDialog::updateButtons()
{
    const auto currentRow = ui_->filtersTableWidget->currentRow();
    const bool ownRow = currentRow >= 0 && currentRow < ownRowCount();
    ui_->removeFilterButton->setEnabled( ownRow );
    const bool packRow = currentRow >= 0 && isPackRow( currentRow ) && !isReplacedRow( currentRow );
    copyPackFilterButton_->setEnabled( packRow );
    editPackFilterButton_->setEnabled( packRow && static_cast<bool>( packItemEditor_ ) );

    updateUpDownButtons( currentRow );
}

void PredefinedFiltersDialog::onCurrentCellChanged( int currentRow, int currentColumn,
                                                    int previousRow, int previousColumn )
{
    Q_UNUSED( currentRow )
    Q_UNUSED( currentColumn )
    Q_UNUSED( previousRow )
    Q_UNUSED( previousColumn )

    updateButtons();
}

void PredefinedFiltersDialog::updateUpDownButtons( int currentRow )
{
    // The user's rows move among themselves; the pack rows follow them in the packs' order.
    const auto own = ownRowCount();
    ui_->upButton->setEnabled( currentRow > 0 && currentRow < own );
    ui_->downButton->setEnabled( currentRow >= 0 && currentRow < own - 1 );
}

void PredefinedFiltersDialog::setPackItemEditor( PackItemEditor editor )
{
    packItemEditor_ = std::move( editor );
    editPackFilterButton_->setVisible( static_cast<bool>( packItemEditor_ ) );
    updateButtons();
}

void PredefinedFiltersDialog::setPacksOpener( std::function<void()> opener )
{
    packsOpener_ = std::move( opener );
    packsButton_->setVisible( static_cast<bool>( packsOpener_ ) );
}

int PredefinedFiltersDialog::ownRowCount() const
{
    const auto rows = ui_->filtersTableWidget->rowCount();
    for ( int row = 0; row < rows; ++row ) {
        if ( isPackRow( row ) ) {
            return row;
        }
    }
    return rows;
}

bool PredefinedFiltersDialog::isPackRow( int row ) const
{
    const auto* item = ui_->filtersTableWidget->item( row, 0 );
    return item && !item->data( PackIdRole ).toString().isEmpty();
}

bool PredefinedFiltersDialog::isReplacedRow( int row ) const
{
    const auto* item = ui_->filtersTableWidget->item( row, 0 );
    return isPackRow( row ) && item->data( ReplacedRole ).toBool();
}

void PredefinedFiltersDialog::insertOwnRow( int row, const PredefinedFilter& filter )
{
    ui_->filtersTableWidget->insertRow( row );
    auto* nameItem = new QTableWidgetItem( filter.name );
    nameItem->setData( FilterIdRole, filter.id );
    ui_->filtersTableWidget->setItem( row, 0, nameItem );
    ui_->filtersTableWidget->setItem( row, 1, new QTableWidgetItem( filter.pattern ) );
    auto* regexCheckbox = new CenteredCheckbox;
    regexCheckbox->setChecked( filter.useRegex );
    ui_->filtersTableWidget->setCellWidget( row, 2, regexCheckbox );
    auto* source = new QTableWidgetItem();
    source->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable );
    ui_->filtersTableWidget->setItem( row, SourceColumn, source );
}

void PredefinedFiltersDialog::populateFiltersTable(
    const PredefinedFiltersCollection::Collection& filters )
{
    ui_->filtersTableWidget->clear();

    ui_->filtersTableWidget->setRowCount( 0 );
    ui_->filtersTableWidget->setColumnCount( 4 );

    ui_->filtersTableWidget->setHorizontalHeaderLabels(
        QStringList() << tr( "Name" ) << tr( "Pattern" ) << tr( "Regex" ) << tr( "Source" ) );

    for ( const auto& filter : filters ) {
        insertOwnRow( ui_->filtersTableWidget->rowCount(), filter );
    }
    refreshPackRows();

    ui_->filtersTableWidget->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents );
    ui_->filtersTableWidget->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );
    ui_->filtersTableWidget->horizontalHeader()->setSectionResizeMode(
        SourceColumn, QHeaderView::ResizeToContents );
    ui_->filtersTableWidget->verticalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents );
    ui_->filtersTableWidget->setWordWrap( false );

    updateButtons();
}

void PredefinedFiltersDialog::refreshPackRows()
{
    const auto own = ownRowCount();
    while ( ui_->filtersTableWidget->rowCount() > own ) {
        ui_->filtersTableWidget->removeRow( ui_->filtersTableWidget->rowCount() - 1 );
    }
    // A filter of the user's with a pack filter's id replaces it (design 4.5).
    QSet<QString> ownIds;
    for ( int row = 0; row < own; ++row ) {
        if ( const auto* item = ui_->filtersTableWidget->item( row, 0 ) ) {
            ownIds.insert( item->data( FilterIdRole ).toString() );
        }
    }
    const auto readOnly = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    const auto& packFilters = PackFilters::instance().filters();
    // Past the limit with these rows of the user's, the packs' filters give way in the lists
    // (effectivePredefinedFilters): they say so here.
    const auto listedPackFilters = std::count_if( packFilters.cbegin(), packFilters.cend(),
                                                  [ &ownIds ]( const ListedFilter& listed ) {
                                                      return !ownIds.contains( listed.filter.id );
                                                  } );
    const bool givingWay = own + listedPackFilters > MaxListedFilters;
    for ( const auto& listed : packFilters ) {
        const auto& filter = listed.filter;
        const auto row = ui_->filtersTableWidget->rowCount();
        const bool replaced = ownIds.contains( filter.id );
        ui_->filtersTableWidget->insertRow( row );
        auto* nameItem = new QTableWidgetItem( filter.name );
        nameItem->setData( FilterIdRole, filter.id );
        nameItem->setData( PackIdRole, listed.packId );
        nameItem->setData( ReplacedRole, replaced );
        nameItem->setFlags( readOnly );
        ui_->filtersTableWidget->setItem( row, 0, nameItem );
        auto* patternItem = new QTableWidgetItem( filter.pattern );
        patternItem->setFlags( readOnly );
        ui_->filtersTableWidget->setItem( row, 1, patternItem );
        auto* regexCheckbox = new CenteredCheckbox;
        regexCheckbox->setChecked( filter.useRegex );
        regexCheckbox->setEnabled( false );
        ui_->filtersTableWidget->setCellWidget( row, 2, regexCheckbox );
        auto* source = new QTableWidgetItem(
            replaced    ? tr( "%1 (replaced by your filter)" ).arg( listed.packName )
            : givingWay ? tr( "%1 (not listed: more than %2 filters)" )
                              .arg( listed.packName )
                              .arg( MaxListedFilters )
                        : listed.packName );
        source->setFlags( readOnly );
        source->setToolTip(
            replaced    ? tr( "Your filter with the same id replaces this pack filter." )
            : givingWay ? tr( "Your filters and the packs' would be more than %1, so the "
                              "packs' filters are not listed until you have fewer." )
                              .arg( MaxListedFilters )
                        : tr( "From the configuration pack \"%1\". Edit it in place "
                              "or copy it to your own filters." )
                              .arg( listed.packName ) );
        ui_->filtersTableWidget->setItem( row, SourceColumn, source );
    }
}

void PredefinedFiltersDialog::copyPackFilter()
{
    const auto row = ui_->filtersTableWidget->currentRow();
    if ( !isPackRow( row ) || isReplacedRow( row ) ) {
        return;
    }
    // The user's copy keeps the id, so it replaces the pack's filter (as Copy to My Previews
    // keeps the name); it is saved with the user's other filters.
    PredefinedFilter copy;
    copy.id = ui_->filtersTableWidget->item( row, 0 )->data( FilterIdRole ).toString();
    copy.name = ui_->filtersTableWidget->item( row, 0 )->text();
    copy.pattern = ui_->filtersTableWidget->item( row, 1 )->text();
    const auto* regex
        = static_cast<CenteredCheckbox*>( ui_->filtersTableWidget->cellWidget( row, 2 ) );
    copy.useRegex = regex ? regex->isChecked() : true;
    const auto target = ownRowCount();
    insertOwnRow( target, copy );
    refreshPackRows();
    ui_->filtersTableWidget->setCurrentCell( target, 0 );
    updateButtons();
}

void PredefinedFiltersDialog::editPackFilter()
{
    const auto row = ui_->filtersTableWidget->currentRow();
    if ( !packItemEditor_ || !isPackRow( row ) || isReplacedRow( row ) ) {
        return;
    }
    const auto* item = ui_->filtersTableWidget->item( row, 0 );
    const auto packId = item->data( PackIdRole ).toString();
    const auto path = QStringLiteral( "filters/%1.json" )
                          .arg( cilogg::plugins::packcanonical::encodeName(
                              item->data( FilterIdRole ).toString() ) );
    // The editor's save changes the pack's filters, which rebuilds the pack rows.
    packItemEditor_( this, packId, path );
}

void PredefinedFiltersDialog::saveSettings() const
{
    PredefinedFiltersCollection::getSynced().saveToStorage( readFiltersTable() );
}

PredefinedFiltersCollection::Collection PredefinedFiltersDialog::readFiltersTable() const
{
    const auto rows = ui_->filtersTableWidget->rowCount();

    PredefinedFiltersCollection::Collection currentFilters;

    // The user's rows only: the pack rows after them are never saved or exported.
    const auto own = std::min( rows, ownRowCount() );
    for ( auto i = 0; i < own; ++i ) {
        if ( nullptr == ui_->filtersTableWidget->item( i, 0 )
             || nullptr == ui_->filtersTableWidget->item( i, 1 ) ) {
            continue;
        }

        const auto name = ui_->filtersTableWidget->item( i, 0 )->text();
        const auto value = ui_->filtersTableWidget->item( i, 1 )->text();
        auto filterId = ui_->filtersTableWidget->item( i, 0 )->data( FilterIdRole ).toString();
        if ( filterId.isEmpty() ) {
            filterId = createPredefinedFilterId();
        }

        const auto useRegexCheckbox
            = static_cast<CenteredCheckbox*>( ui_->filtersTableWidget->cellWidget( i, 2 ) );
        const auto useRegex = useRegexCheckbox ? useRegexCheckbox->isChecked() : false;

        if ( !name.isEmpty() && !value.isEmpty() ) {
            currentFilters.push_back( { filterId, name, value, useRegex } );
        }
    }

    return currentFilters;
}

void PredefinedFiltersDialog::addFilter()
{
    addFilterRow( {} );
}

void PredefinedFiltersDialog::addFilterRow( const QString& newFilter )
{
    // After the user's rows, before the packs'.
    const auto newRow = ownRowCount();
    insertOwnRow( newRow, { createPredefinedFilterId(), QString{}, newFilter, false } );
    refreshPackRows(); // one more of the user's can make the packs' filters give way

    ui_->filtersTableWidget->scrollToItem( ui_->filtersTableWidget->item( newRow, 0 ) );
    ui_->filtersTableWidget->setCurrentCell( newRow, 0 );
    ui_->filtersTableWidget->editItem( ui_->filtersTableWidget->item( newRow, 0 ) );

    ui_->filtersTableWidget->resizeRowToContents( newRow );
}

void PredefinedFiltersDialog::removeFilter()
{
    const auto row = ui_->filtersTableWidget->currentRow();
    if ( row < 0 || row >= ownRowCount() ) {
        return; // a pack's filter is not the user's to remove
    }
    ui_->filtersTableWidget->removeRow( row );
    // A pack filter the removed one replaced is no longer replaced.
    refreshPackRows();

    updateButtons();
}

void PredefinedFiltersDialog::moveFilterUp()
{
    const auto currentRow = ui_->filtersTableWidget->currentRow();
    const auto selectedColumn = ui_->filtersTableWidget->currentColumn();

    if ( currentRow >= 0 ) {
        swapFilters( currentRow, currentRow - 1, selectedColumn );
    }
}

void PredefinedFiltersDialog::moveFilterDown()
{
    const auto currentRow = ui_->filtersTableWidget->currentRow();
    const auto selectedColumn = ui_->filtersTableWidget->currentColumn();

    if ( currentRow >= 0 ) {
        swapFilters( currentRow, currentRow + 1, selectedColumn );
    }
}

void PredefinedFiltersDialog::swapFilters( int currentRow, int newRow, int selectedColumn )
{
    dispatchToObject(
        [ this, currentRow, newRow, selectedColumn ] {
            for ( int column = 0; column < ui_->filtersTableWidget->columnCount(); ++column ) {
                auto currentUseRegex = static_cast<CenteredCheckbox*>(
                    ui_->filtersTableWidget->cellWidget( currentRow, column ) );
                auto newUseRegex = static_cast<CenteredCheckbox*>(
                    ui_->filtersTableWidget->cellWidget( newRow, column ) );

                if ( currentUseRegex && newUseRegex ) {
                    const auto currentCheckState = currentUseRegex->isChecked();
                    const auto newCheckState = newUseRegex->isChecked();
                    currentUseRegex->setChecked( newCheckState );
                    newUseRegex->setChecked( currentCheckState );
                }
                else {
                    auto currentItem = ui_->filtersTableWidget->takeItem( currentRow, column );
                    auto newItem = ui_->filtersTableWidget->takeItem( newRow, column );

                    ui_->filtersTableWidget->setItem( newRow, column, currentItem );
                    ui_->filtersTableWidget->setItem( currentRow, column, newItem );
                }
            }
            ui_->filtersTableWidget->setCurrentCell( newRow, selectedColumn );
        },
        this );
}

void PredefinedFiltersDialog::importFilters()
{
    const auto file
        = QFileDialog::getOpenFileName( this, tr( "Select file to import" ), "",
                                        tr( "Predefined filters (*.conf);;All files (*)" ) );

    if ( file.isEmpty() ) {
        return;
    }

    LOG_DEBUG << "Loading predefined filters from " << file;
    QSettings settings{ file, QSettings::IniFormat };

    PredefinedFiltersCollection collection;
    collection.retrieveFromStorage( settings );
    populateFiltersTable( collection.getFilters() );
}

void PredefinedFiltersDialog::exportFilters()
{
    auto file = QFileDialog::getSaveFileName( this, tr( "Export predefined filters" ), "",
                                              tr( "Predefined filters (*.conf)" ) );

    if ( file.isEmpty() ) {
        return;
    }

    if ( !file.endsWith( ".conf" ) ) {
        file += ".conf";
    }

    QSettings settings{ file, QSettings::IniFormat };

    PredefinedFiltersCollection collection;
    collection.setFilters( readFiltersTable() );
    collection.saveToStorage( settings );
}

void PredefinedFiltersDialog::resolveStandardButton( QAbstractButton* button )
{
    LOG_DEBUG << "PredefinedFiltersDialog::resolveStandardButton";

    const auto role = ui_->buttonBox->buttonRole( button );

    switch ( role ) {
    case QDialogButtonBox::RejectRole:
        reject();
        return;

    case QDialogButtonBox::ApplyRole:
        saveSettings();
        break;

    case QDialogButtonBox::AcceptRole:
        saveSettings();
        accept();
        break;
    default:
        LOG_ERROR << "PredefinedFiltersDialog::resolveStandardButton unhandled role: " << role;
        return;
    }

    Q_EMIT optionsChanged();
}
