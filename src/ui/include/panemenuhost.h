// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_PANEMENUHOST_H
#define CILOGG_PANEMENUHOST_H

class QMenu;
class QWidget;

// A window with split panes. The log views of its tabs ask it for the pane commands of their
// context menu, so the commands are the same as in the View menu.
class PaneMenuHost {
public:
    // Fills `menu` with the pane commands for the pane that holds `origin`.
    virtual void populatePaneMenu( QMenu& menu, QWidget* origin ) = 0;

protected:
    PaneMenuHost() = default;
    ~PaneMenuHost() = default;
    PaneMenuHost( const PaneMenuHost& ) = default;
    PaneMenuHost& operator=( const PaneMenuHost& ) = default;
};

#endif
