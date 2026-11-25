/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014, 2015 Nicolas Bonnefon
 * and other contributors
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
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#ifndef PREDEFINEDFILTERSDIALOG_H_
#define PREDEFINEDFILTERSDIALOG_H_

#include <QDialog>
#include <functional>
#include <memory>

#include "predefinedfilters.h"

class QAbstractButton;
class QToolButton;
namespace Ui {
class PredefinedFiltersDialog;
}

// The user's predefined filters, then the enabled configuration packs' (E16c, design 4.5): pack
// filters are listed with the pack's name, read only, and edited in place (Edit...) or copied into
// the user's own (Copy to My Filters). Only the user's rows are saved, exported and imported.
// The form is a member (not a base), so tests can use the dialog without its generated header.
class PredefinedFiltersDialog : public QDialog {
    Q_OBJECT

  public:
      using PackItemEditor
          = std::function<void( QWidget* parent, const QString& packId, const QString& path )>;

      PredefinedFiltersDialog( QWidget* parent = nullptr );
      PredefinedFiltersDialog( const QString& newFilter, QWidget* parent = nullptr );
      ~PredefinedFiltersDialog() override;

      // Edit... on a pack filter opens this editor on its store item (filters/<id>.json).
      void setPackItemEditor( PackItemEditor editor );
      // Packs... opens the Plugins and Packs window.
      void setPacksOpener( std::function<void()> opener );

  private Q_SLOTS:
    void addFilter();
    void removeFilter();

    void moveFilterUp();
    void moveFilterDown();

    void exportFilters();
    void importFilters();

    void resolveStandardButton( QAbstractButton* button );

    void onCurrentCellChanged( int currentRow, int currentColumn, int previousRow,
                               int previousColumn );

  Q_SIGNALS:
    void optionsChanged();

  private:
    void addFilterRow( const QString& newFilter );
    // A row of the user's at `row` (before the pack rows).
    void insertOwnRow( int row, const PredefinedFilter& filter );
    void populateFiltersTable( const PredefinedFiltersCollection::Collection& filters );
    // The enabled packs' filters after the user's rows, marked when one of the user's replaces one.
    void refreshPackRows();
    // The user's rows come first; the pack rows follow them.
    int ownRowCount() const;
    bool isPackRow( int row ) const;
    bool isReplacedRow( int row ) const;
    void copyPackFilter();
    void editPackFilter();

    void swapFilters( int currentRow, int newRow, int column );

    void saveSettings() const;
    PredefinedFiltersCollection::Collection readFiltersTable() const;

    void updateButtons();
    void updateUpDownButtons( int currentRow );

    std::unique_ptr<Ui::PredefinedFiltersDialog> ui_;
    QToolButton* copyPackFilterButton_ = nullptr;
    QToolButton* editPackFilterButton_ = nullptr;
    QToolButton* packsButton_ = nullptr;
    PackItemEditor packItemEditor_;
    std::function<void()> packsOpener_;
};

#endif