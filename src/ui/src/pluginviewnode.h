// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PluginView's element record, shared by pluginview.cpp and pluginviewtrees.cpp (E17e).
#include "pluginview.h"
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTimer>
#include <QWidget>

namespace pluginview {
class PluginTableModel;
class PluginTreeModel;
} // namespace pluginview
class PluginTableBehaviour;
class PluginTreeBehaviour;
class PluginFileTreeBehaviour;
class PluginBusyOverlay;
class QMenu;
class QShortcut;

// E17f (docs/plugin-sdk-advanced.md section 29), shared by pluginview.cpp and pluginviewlayout.cpp.
namespace pluginlayout {
// Containers: their children are part of a view's shape.
const QStringList& containerTypes();
// Elements that take the free space of a row or of the view's body.
bool expands( const QString& type );
// A form child's caption: a field's `label`, else the element's own `label` (never a button's).
QString formCaption( const QJsonObject& element );
// A caption as the text of a label with a buddy (`&` shown as written).
QString captionText( QString caption );
// The numeric size hints every element takes (section 29.1).
const QStringList& sizeHintKeys();
} // namespace pluginlayout

struct PluginView::Node {
    QString id;
    QString type;
    QString kind;
    bool automatic = false;
    QPointer<QWidget> widget; // the element itself (object name base)
    QPointer<QWidget> outer;  // what sits in the parent layout
    QPointer<QWidget> content;
    QPointer<QLabel> error;  // E17c: the validation message below a field
    QString invalid;         // the message shown (empty: valid)
    QPointer<QTimer> debounce;
    QJsonObject props;
    QJsonValue pluginValue;
    QJsonValue lastRows; // rows (tables) or nodes (trees) last set as a whole
    bool rowsDirty = true;
    pluginview::PluginTableModel* model = nullptr;
    QSortFilterProxyModel* proxy = nullptr;
    PluginTableBehaviour* behaviour = nullptr;      // E17d
    pluginview::PluginTreeModel* treeModel = nullptr; // E17e
    PluginTreeBehaviour* tree = nullptr;
    PluginFileTreeBehaviour* fileTree = nullptr;
    // E17f: where the element sits, and the state of layouts, buttons and the busy overlay.
    // `lastRows` also holds a text_view's text, a chart's series and a sparkline's values last
    // set as a whole.
    QString parentId;   // empty for the root
    QString parentType; // the containing element's type
    bool root = false;
    QPointer<QLabel> formCaption;           // the caption cell of a form row
    QPointer<PluginBusyOverlay> busy;       // containers
    QPointer<QMenu> menu;                   // a button's drop-down
    QPointer<QShortcut> shortcut;           // a button's shortcut
    QPointer<QTimer> saveTimer;             // a splitter's kept sizes
};
