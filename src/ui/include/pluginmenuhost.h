// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_PLUGINMENUHOST_H
#define CILOGG_PLUGINMENUHOST_H

#include <QList>
#include <QPair>
#include <QString>
#include <functional>
#include <optional>

class QAction;
class QMenu;
class QWidget;

// A window whose plugins add entries to the context menus of its log views (location `log`,
// docs/plugin-sdk-advanced.md 7.2). The log view describes its selection; the window adds the
// plugin section and the view removes it when the menu has closed.
class PluginMenuHost {
public:
    struct LogSelection {
        QString view;               // "main" or "filtered"
        std::optional<qint64> line; // the 1-based file line under the pointer
        QList<QPair<qint64, qint64>> ranges; // selected file lines (first, last), at most 64
        qint64 count = 0;                    // selected lines
        bool portion = false;                // a part of one line is selected
        std::function<QString()> text;       // read only when a plugin entry is picked
    };
    // Whether any enabled plugin shows entries in the log views (the selection is described only
    // then, so a right-click costs nothing without such plugins).
    virtual bool hasPluginLogEntries() const = 0;
    // Adds the plugin entries for the log view `origin` to `menu` before `before`; returns them.
    virtual QList<QAction*> addPluginLogEntries( QMenu& menu, QAction* before, QWidget* origin,
                                                 const LogSelection& selection )
        = 0;
    // Removes and deletes the entries addPluginLogEntries() returned.
    virtual void removePluginLogEntries( QMenu& menu, const QList<QAction*>& entries ) = 0;

protected:
    PluginMenuHost() = default;
    ~PluginMenuHost() = default;
    PluginMenuHost( const PluginMenuHost& ) = default;
    PluginMenuHost& operator=( const PluginMenuHost& ) = default;
};

#endif
