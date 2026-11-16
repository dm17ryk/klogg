// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch.hpp>

#include <QAction>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QWidget>

#include "searchbarlayout.h"

TEST_CASE( "search bar folds the first collapsible controls only when needed",
           "[workbench][searchbar]" )
{
    const std::vector<int> buttons{ 100, 30, 30, 30 };
    // Everything fits: nothing folds and no overflow button is needed.
    CHECK( SearchBarCollapser::collapsedCount( 500, 300, buttons, 20 ) == 0 );
    CHECK( SearchBarCollapser::collapsedCount( 490, 300, buttons, 20 ) == 0 );
    CHECK( SearchBarCollapser::collapsedCount( 489, 300, buttons, 20 ) == 1 );
    // Folding the first control also adds the overflow button.
    CHECK( SearchBarCollapser::collapsedCount( 410, 300, buttons, 20 ) == 1 );
    CHECK( SearchBarCollapser::collapsedCount( 409, 300, buttons, 20 ) == 2 );
    CHECK( SearchBarCollapser::collapsedCount( 320, 300, buttons, 20 ) == 4 );
    // Narrower than the compact bar: everything folds, the rest is the pane's minimum.
    CHECK( SearchBarCollapser::collapsedCount( 100, 300, buttons, 20 ) == 4 );
    CHECK( SearchBarCollapser::collapsedCount( 100, 300, {}, 20 ) == 0 );
}

TEST_CASE( "a narrow search combo opens its list as wide as the search bar",
           "[workbench][searchbar]" )
{
    // A 40 px combo in a 600 px bar opens a 600 px list.
    CHECK( WidePopupComboBox::popupWidth( 40, 600, 200, 1920 ) == 600 );
    // Long entries widen it further, up to the screen.
    CHECK( WidePopupComboBox::popupWidth( 40, 600, 900, 1920 ) == 900 );
    CHECK( WidePopupComboBox::popupWidth( 40, 600, 4000, 1920 ) == 1920 );
    // Never narrower than the combo box itself.
    CHECK( WidePopupComboBox::popupWidth( 700, 600, 200, 1920 ) == 700 );
    CHECK( WidePopupComboBox::popupWidth( 40, 0, 0, 0 ) == 40 );
}

TEST_CASE( "folded controls move to the overflow menu and come back", "[workbench][searchbar]" )
{
    QWidget bar;
    auto* layout = new QHBoxLayout( &bar );
    layout->setContentsMargins( 0, 0, 0, 0 );
    auto* pattern = new QLineEdit;
    pattern->setMinimumWidth( 200 );
    layout->addWidget( pattern );
    auto* first = new QToolButton;
    first->setText( QStringLiteral( "Keep Results" ) );
    first->setCheckable( true );
    auto* second = new QToolButton;
    second->setText( QStringLiteral( "Clear Results" ) );
    layout->addWidget( first );
    layout->addWidget( second );
    auto* overflow = new QToolButton;
    overflow->setText( QStringLiteral( "»" ) );
    layout->addWidget( overflow );

    SearchBarCollapser collapser( &bar, overflow );
    auto* firstAction = SearchBarCollapser::mirrorAction( first, &collapser );
    collapser.addCollapsible( first, firstAction );
    collapser.addCollapsible( second, SearchBarCollapser::mirrorAction( second, &collapser ) );

    bar.resize( 2000, 30 );
    bar.show();
    collapser.relayout();
    CHECK( collapser.collapsed() == 0 );
    CHECK( overflow->isHidden() );

    // As in the crawler, the bar may get as narrow as its folded width.
    bar.setMinimumWidth( collapser.compactWidth() );
    bar.resize( collapser.compactWidth(), 30 );
    collapser.relayout();
    CHECK( collapser.collapsed() == 2 );
    CHECK( first->isHidden() );
    CHECK( second->isHidden() );
    CHECK_FALSE( overflow->isHidden() );
    CHECK_FALSE( pattern->isHidden() );

    // The mirror action drives the hidden button.
    firstAction->trigger();
    CHECK( first->isChecked() );
    first->setChecked( false );
    CHECK_FALSE( firstAction->isChecked() );

    bar.resize( 2000, 30 );
    collapser.relayout();
    CHECK( collapser.collapsed() == 0 );
    CHECK_FALSE( first->isHidden() );
    CHECK( overflow->isHidden() );
}
