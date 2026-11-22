// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVariantMap>

class QLineEdit;
class QPushButton;
class QWidget;

namespace pluginsui {

// Why a plugin does not run, readable: its error and the end of its diagnostics (its stderr and
// CILogg's notes on starting it, such as the interpreter it ran). Empty when it has neither.
QString problemText( const QVariantMap& plugin );

// The "Installed from" section of a plugin's Configure dialog: where the plugin came from, its
// installed copy and, for a plugin installed from a folder or ZIP, that path with Browse and
// Reinstall buttons. A plugin with a problem also shows problemText().
struct SourceSection {
    QWidget* widget = nullptr;
    // The folder or ZIP; null unless the plugin was installed from one.
    QLineEdit* path = nullptr;
    QPushButton* reinstall = nullptr;
};
SourceSection createSourceSection( QWidget* parent, const QVariantMap& plugin );

// Asks for a plugin folder or ZIP, starting at `start`; empty when cancelled.
QString choosePluginSource( QWidget* parent, const QString& start, bool zip );

} // namespace pluginsui
