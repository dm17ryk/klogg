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
#include <QTimer>
#include <QWidget>

namespace pluginview {
class PluginTableModel;
class PluginTreeModel;
} // namespace pluginview
class PluginTableBehaviour;
class PluginTreeBehaviour;
class PluginFileTreeBehaviour;

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
};
