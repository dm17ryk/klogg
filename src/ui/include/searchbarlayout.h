// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_SEARCHBARLAYOUT_H
#define CILOGG_SEARCHBARLAYOUT_H

#include <vector>

#include <QComboBox>
#include <QObject>
#include <QPointer>

class QAction;
class QMenu;
class QToolButton;
class QWidget;

// A combo box whose drop-down list opens at least as wide as a reference widget (the whole
// search bar), so a combo box squeezed by a narrow pane still shows its entries in full.
class WidePopupComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit WidePopupComboBox( QWidget* parent = nullptr );

    void setPopupWidthReference( QWidget* reference );
    // The width the drop-down list opens with: the widest of the combo box, the reference
    // widget and the entries, but never wider than `availableWidth`.
    static int popupWidth( int comboWidth, int referenceWidth, int contentWidth,
                           int availableWidth );

    void showPopup() override;

private:
    QPointer<QWidget> popupWidthReference_;
};

// Keeps the search pattern box usable in a narrow pane: when the search bar is too narrow,
// the least important buttons move, in order, into a "more" menu at the end of the bar, and
// come back when there is room again.
class SearchBarCollapser : public QObject {
    Q_OBJECT
public:
    // `bar` holds the search bar; the collapsible widgets are in it. `overflowButton` is in the
    // bar too and is shown only while something is collapsed.
    SearchBarCollapser( QWidget* bar, QToolButton* overflowButton );

    // Adds a widget that may collapse, in collapse order (the first collapses first).
    // `action` stands for it in the overflow menu.
    void addCollapsible( QWidget* widget, QAction* action );

    // A checkable action that mirrors a checkable tool button in the overflow menu.
    static QAction* mirrorAction( QToolButton* button, QObject* parent );

    // How many collapsible widgets (from the first) must collapse so the bar fits in
    // `available` pixels. `fixed` is the width of everything that never collapses,
    // `collapsible` the widths of the collapsible widgets in collapse order and `overflow` the
    // width of the overflow button (needed once anything collapses).
    static int collapsedCount( int available, int fixed, const std::vector<int>& collapsible,
                               int overflow );

    // The width the bar needs with every collapsible widget collapsed.
    int compactWidth() const;
    // The width the bar needs with nothing collapsed.
    int unfoldedWidth() const;
    // The bar may get `factor` times narrower than unfolded (never narrower than compact); it
    // keeps its minimum width up to date as its controls change.
    void setMinimumWidthReduction( int factor );
    int collapsed() const
    {
        return collapsed_;
    }

    void relayout();

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    struct Entry {
        QPointer<QWidget> widget;
        QPointer<QAction> action;
    };

    bool isCollapsible( const QWidget* widget ) const;
    int fixedWidth() const;
    void syncOverflowMenu();

    QWidget* bar_;
    QToolButton* overflowButton_;
    QMenu* overflowMenu_;
    std::vector<Entry> entries_;
    int collapsed_ = 0;
    int minimumWidthReduction_ = 0;
    bool relayingOut_ = false;
};

#endif
