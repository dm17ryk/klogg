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

#include "predefinedfilterscombobox.h"

#include <algorithm>

#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <qabstractitemview.h>
#include <QRegularExpression>

#include "log.h"

constexpr int PatternRole = Qt::UserRole + 1;
constexpr int RegexRole = PatternRole + 1;
constexpr int FilterIdRole = RegexRole + 1;

class QCheckListStyledItemDelegate : public QStyledItemDelegate {
  public:
    QCheckListStyledItemDelegate( QObject* parent = 0 )
        : QStyledItemDelegate( parent )
    {
    }

    void paint( QPainter* painter, const QStyleOptionViewItem& option,
                const QModelIndex& index ) const
    {
        QStyleOptionViewItem refToNonConstOption = option;
        refToNonConstOption.showDecorationSelected = false;
        QStyledItemDelegate::paint( painter, refToNonConstOption, index );
    }
};

PredefinedFiltersComboBox::PredefinedFiltersComboBox( QWidget* parent )
    : QComboBox( parent )
    , model_( new QStandardItemModel(this) )
    , ignoreCollecting_( false )
{
    setFocusPolicy( Qt::ClickFocus );
    setItemDelegate( new QCheckListStyledItemDelegate( this ) );
    populatePredefinedFilters();

    connect( model_, &QStandardItemModel::itemChanged, this,
             [ this ]( const QStandardItem* changedItem ) {
                 Q_UNUSED( changedItem );
                 collectFilters();
             } );

    const auto changeCheckState = [ this ]( const QModelIndex& index ) {
        auto item = model_->itemFromIndex( index );
        if ( !item || !item->isCheckable() ) {
            return;
        }
        if ( item->checkState() == Qt::Checked ) {
            item->setCheckState( Qt::Unchecked );
        }
        else {
            item->setCheckState( Qt::Checked );
        }
    };

    connect( view(), &QAbstractItemView::pressed, this, changeCheckState );
    connect( view(), &QAbstractItemView::doubleClicked, this, changeCheckState );

    QPalette palette = this->palette();
    palette.setColor( QPalette::Base, palette.color( QPalette::Window ) );
    view()->setPalette( palette );

    view()->setTextElideMode( Qt::ElideNone );
    setSizeAdjustPolicy( QComboBox::AdjustToContents );
}

void PredefinedFiltersComboBox::populatePredefinedFilters()
{
    model_->clear();
    const auto filters = filtersCollection_.getSyncedFilters();

    setTitle( tr("Predefined filters") );

    insertFilters( filters );

    this->setModel( model_ );
}

void PredefinedFiltersComboBox::updateSearchPattern( const QString newSearchPattern, bool useLogicalCombining )
{
    searchPattern_.newOne_ = newSearchPattern;
    searchPattern_.useLogicalCombining_ = useLogicalCombining;
}

QList<int> PredefinedFiltersComboBox::selectedFilterIndexes() const
{
    QList<int> selectedIndexes;

    QString searchPattern = searchPattern_.newOne_;
    QString delimiter( "\\|" );

    if ( searchPattern_.useLogicalCombining_ ) {
        delimiter = R"(" or ")";
        if ( searchPattern.size() >= 2 ) {
            searchPattern = searchPattern.mid( 1, searchPattern.size() - 2 );
        }
    }

    const auto parts = searchPattern.split( QRegularExpression( delimiter ), Qt::SkipEmptyParts );
    const auto totalRows = model_->rowCount();
    for ( int filterIndex = 1; filterIndex < totalRows; ++filterIndex ) {
        const auto item = model_->item( filterIndex );
        if ( item == nullptr || !item->isCheckable() ) {
            continue;
        }

        const auto pattern = item->data( PatternRole ).toString();
        if ( parts.contains( pattern ) ) {
            selectedIndexes.push_back( filterIndex - 1 );
        }
    }

    return selectedIndexes;
}

QVariantList PredefinedFiltersComboBox::commanderFilters() const
{
    QVariantList filters;
    const auto currentFilters = filtersCollection_.getFilters();
    const auto selectedIndexes = selectedFilterIndexes();

    filters.reserve( currentFilters.size() );
    for ( int filterIndex = 0; filterIndex < currentFilters.size(); ++filterIndex ) {
        const auto& filter = currentFilters.at( filterIndex );
        QVariantMap filterInfo;
        filterInfo.insert( QStringLiteral( "filterId" ), filter.id );
        filterInfo.insert( QStringLiteral( "filterIndex" ), filterIndex );
        filterInfo.insert( QStringLiteral( "name" ), filter.name );
        filterInfo.insert( QStringLiteral( "pattern" ), filter.pattern );
        filterInfo.insert( QStringLiteral( "useRegex" ), filter.useRegex );
        filterInfo.insert( QStringLiteral( "selected" ), selectedIndexes.contains( filterIndex ) );
        filters.push_back( filterInfo );
    }

    return filters;
}

std::optional<PredefinedFilter> PredefinedFiltersComboBox::commanderFilterById(
    const QString& filterId ) const
{
    if ( filterId.isEmpty() ) {
        return std::nullopt;
    }

    const auto currentFilters = filtersCollection_.getFilters();
    const auto filterIt = std::find_if( currentFilters.cbegin(), currentFilters.cend(),
                                        [ &filterId ]( const auto& filter ) {
                                            return filter.id == filterId;
                                        } );
    if ( filterIt == currentFilters.cend() ) {
        return std::nullopt;
    }

    return *filterIt;
}

std::optional<PredefinedFilter> PredefinedFiltersComboBox::commanderFilterByIndex(
    int filterIndex ) const
{
    const auto currentFilters = filtersCollection_.getFilters();
    if ( filterIndex < 0 || filterIndex >= currentFilters.size() ) {
        return std::nullopt;
    }

    return currentFilters.at( filterIndex );
}

void PredefinedFiltersComboBox::showPopup()
{
    if ( searchPattern_.newOne_ == searchPattern_.lastOne_ ) {
        QComboBox::showPopup();
        return;
    }

    searchPattern_.lastOne_ = searchPattern_.newOne_;

    QString searchPattern = searchPattern_.newOne_;
    QString delimeter( "\\|" );

    if ( searchPattern_.useLogicalCombining_ ) {
        delimeter = R"(" or ")";
        // Remove " at the beginning and at the end
        searchPattern = searchPattern.mid(1, searchPattern.size() - 1);
    }

    QStringList list = searchPattern.split( QRegularExpression( delimeter ) );

    const auto totalRows = model_->rowCount();

    ignoreCollecting_ = true;

    for ( auto filterIndex = 0; filterIndex < totalRows; ++filterIndex ) {
        const auto item = model_->item( filterIndex );
        if ( item->isCheckable() ) {
            item->setCheckState( Qt::Unchecked );
        }
    }

    for ( auto &l : list ) {
        for ( auto filterIndex = 0; filterIndex < totalRows; ++filterIndex ) {
            const auto item = model_->item( filterIndex );
            if ( !item->isCheckable() ) {
                continue;
            }
            if ( l == item->data( PatternRole ).toString() ) {
                item->setCheckState( Qt::Checked );
            }
        }
    }

    ignoreCollecting_ = false;

    QComboBox::showPopup();
}

void PredefinedFiltersComboBox::setTitle( const QString& title )
{
    auto* titleItem = new QStandardItem( title );
    model_->insertRow( 0, titleItem );
}

void PredefinedFiltersComboBox::insertFilters(
    const PredefinedFiltersCollection::Collection& filters )
{
    for ( const auto& filter : filters ) {
        auto* item = new QStandardItem( filter.name );

        item->setFlags( Qt::ItemIsUserCheckable | Qt::ItemIsEnabled );
        item->setData( Qt::Unchecked, Qt::CheckStateRole );

        item->setData( filter.pattern, PatternRole );
        item->setData( filter.useRegex, RegexRole );
        item->setData( filter.id, FilterIdRole );

        model_->insertRow( model_->rowCount(), item );
    }
}

void PredefinedFiltersComboBox::collectFilters()
{
    if ( ignoreCollecting_ ) {
        return;
    }

    const auto totalRows = model_->rowCount();

    /* If multiple filters are selected connect those with "|" */

    QList<PredefinedFilter> selectedPatterns;
    selectedPatterns.reserve( totalRows );
    for ( auto filterIndex = 0; filterIndex < totalRows; ++filterIndex ) {
        const auto item = model_->item( filterIndex );

        if ( item->checkState() != Qt::Checked ) {
            continue;
        }

        selectedPatterns.append( { item->data( FilterIdRole ).toString(), item->text(),
                                   item->data( PatternRole ).toString(),
                                   item->data( RegexRole ).toBool() } );
    }

    Q_EMIT filterChanged( selectedPatterns );
}
