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

#ifndef FILTERSDIALOG_H
#define FILTERSDIALOG_H

#include <functional>
#include <memory>

#include <QDialog>

#include "highlighterset.h"

class HighlighterSetEdit;
class QAbstractButton;
class QToolButton;
namespace Ui {
class HighlightersDialog;
}

// The user's highlighter sets, then the enabled configuration packs' (E16c2, design 4.5): pack
// sets are listed with the pack's name, shown read only, and edited in place (Edit...) or copied
// into the user's own (Copy to My Highlighters). Only the user's sets are saved and exported.
// The form is a member (not a base), so tests can use the dialog without its generated header.
class HighlightersDialog : public QDialog {
    Q_OBJECT

  public:
    using PackItemEditor
        = std::function<void( QWidget* parent, const QString& packId, const QString& path )>;

    explicit HighlightersDialog( QWidget* parent = nullptr );
    ~HighlightersDialog() override;

    // Edit... on a pack set opens this editor on its store item (highlights/<id>.set).
    void setPackItemEditor( PackItemEditor editor );
    // Packs... opens the Plugins and Packs window.
    void setPacksOpener( std::function<void()> opener );

  Q_SIGNALS:
    // Is emitted when new settings must be used
    void optionsChanged();
    void configurationsExported( const QStringList& paths );

  private Q_SLOTS:
    void addHighlighterSet();
    void removeHighlighterSet();

    void moveHighlighterSetUp();
    void moveHighlighterSetDown();

    void resolveDialog( QAbstractButton* button );

    // Update the edit fields from the selected HighlighterSet.
    void updatePropertyFields();

    // Update the selected HighlighterSet from the values in the property fields.
    void updateHighlighterProperties();

    void exportHighlighters();
    void importHighlighters();

  private:
    void populateHighlighterList();
    void setCurrentRow( int row );
    // The enabled packs' sets after the user's rows, marked when one of the user's replaces one.
    void refreshPackRows();
    // The user's rows come first (one per set of highlighterSetCollection_); the pack rows follow.
    int ownRowCount() const;
    bool isReplacedRow( int row ) const;
    // The pack set of a pack row, or null.
    const ListedHighlighterSet* packSetAt( int row ) const;
    void copyPackSet();
    void editPackSet();
    void updatePackButtons();

  private:
    std::unique_ptr<Ui::HighlightersDialog> ui_;
    QToolButton* copyPackSetButton_ = nullptr;
    QToolButton* editPackSetButton_ = nullptr;
    QToolButton* packsButton_ = nullptr;
    PackItemEditor packItemEditor_;
    std::function<void()> packsOpener_;
    // The pack sets the pack rows show, in row order.
    QList<ListedHighlighterSet> packRows_;

    HighlighterSetEdit* highlighterSetEdit_ = nullptr;

    // Temporary HighlighterSetCollection modified by the dialog
    // it is copied from the one in Config()
    HighlighterSetCollection highlighterSetCollection_;

    // Index of the row currently selected or -1 if none.
    int selectedRow_ = -1;
};

#endif
