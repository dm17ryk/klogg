// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractProxyModel>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPersistentModelIndex>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>

class QFileIconProvider;
class QFileSystemModel;
class QMenu;
class QTreeView;

// The `file_tree` element of plugin views (docs/plugin-sdk-advanced.md sections 11.2 and 28.2).
namespace pluginview {
// The plugin's roots as top-level items over one QFileSystemModel: each root shows its path, and
// the folders below it are the native model's, loaded lazily with changes on disk shown by
// themselves. A network root (//server/share) is attached only when the user expands it or a
// patch reaches into it. Also keeps the check boxes of a pick list.
class PluginFileRootsModel final : public QAbstractProxyModel {
public:
    explicit PluginFileRootsModel( QObject* parent );
    void setFileModel( QFileSystemModel* model );

    // Absolute paths, at most MaxFileTreeRoots; relative, repeated and nested ones are skipped
    // and returned in `skipped` with the reason.
    void setRoots( const QStringList& paths, QStringList* skipped );
    QStringList roots() const;
    // A network root: false when it cannot be reached (it shows greyed).
    bool attach( int row );
    bool isAttached( int row ) const;
    bool isRoot( const QModelIndex& index ) const;
    // An item's absolute path with forward slashes.
    QString pathOf( const QModelIndex& index ) const;
    bool isDir( const QModelIndex& index ) const;
    // The item of a path inside a root, attaching a network root; invalid when outside the roots
    // (also through a link that leads out of its root) or missing.
    QModelIndex indexOfPath( const QString& path );
    // Whether a path lies in one of the roots (no file system access).
    bool withinRoots( const QString& path ) const;
    // Whether a path is a root, or an existing item of an attached root once its links are
    // resolved (as indexOfPath(), without attaching a network root or loading anything): what
    // tooltips and element menus may name.
    bool reaches( const QString& path ) const;

    // What can be selected (and checked): files, folders or both.
    void setSelectable( bool files, bool dirs );
    void setCheckboxes( bool checkboxes );
    bool checkboxes() const
    {
        return checkboxes_;
    }
    QStringList checkedPaths() const
    {
        return checked_;
    }
    void setCheckedPaths( const QStringList& paths );
    bool isChecked( const QString& path ) const;
    // Set by the behaviour: whether the user may act now, and the user's check box changes.
    std::function<bool()> canEdit;
    std::function<void( const QString& path, bool checked )> onCheckRequested;

    // Paths: forward slashes, no trailing slash (but "C:/" and "/"), cleaned.
    static QString normalize( const QString& path );
    static bool isNetworkPath( const QString& path );

    QModelIndex mapToSource( const QModelIndex& proxyIndex ) const override;
    QModelIndex mapFromSource( const QModelIndex& sourceIndex ) const override;
    QModelIndex index( int row, int column, const QModelIndex& parent = {} ) const override;
    QModelIndex parent( const QModelIndex& child ) const override;
    int rowCount( const QModelIndex& parent = {} ) const override;
    int columnCount( const QModelIndex& parent = {} ) const override;
    bool hasChildren( const QModelIndex& parent = {} ) const override;
    bool canFetchMore( const QModelIndex& parent ) const override;
    void fetchMore( const QModelIndex& parent ) override;
    QVariant data( const QModelIndex& index, int role ) const override;
    QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;
    Qt::ItemFlags flags( const QModelIndex& index ) const override;
    bool setData( const QModelIndex& index, const QVariant& value, int role ) override;

private:
    struct Root {
        QString path;
        QString canonical; // the root's resolved path once attached: no link may lead out of it
        QPersistentModelIndex source; // valid once attached
        bool network = false;
        bool missing = false;
    };
    // The root a source index belongs to (its own row or an ancestor's); -1 when outside.
    int rootOf( const QModelIndex& source ) const;
    // Whether a source item below a root is a symbolic link or junction that leads out of its
    // root (or nowhere): it shows disabled and is never opened, selected or checked.
    bool escapes( const QModelIndex& source ) const;
    int rootOfPath( const QString& path ) const;
    bool attachLocal( Root& root );
    void rebuildRootPointers();
    QModelIndex sourceParentMapped( const QModelIndex& sourceParent, bool* mapped ) const;
    void connectSource();
    QString key( const QString& path ) const;

    QPointer<QFileSystemModel> files_;
    QVector<Root> roots_;
    QHash<void*, int> rootPointers_; // source internal pointer of an attached root -> its row
    mutable QHash<const void*, bool> escapes_; // escapes() per source item, until it changes
    QVector<int> pending_; // begin/end pairs: 0 ignored, 1 forwarded, 2 reset
    QList<QPersistentModelIndex> layoutProxy_;
    QList<QPersistentModelIndex> layoutSource_;
    QStringList checked_;
    QSet<QString> checkedKeys_;
    bool selectFiles_ = true;
    bool selectDirs_ = true;
    bool checkboxes_ = false;
};
} // namespace pluginview

// The behaviour of a plugin view file tree: options, selection and `value`, check boxes,
// `selected` and `activated`, attaching network roots, revealing paths, the context menu with Copy
// Path and the plugin's element entries. Owns the file system model; owned by the tree view.
class PluginFileTreeBehaviour final : public QObject {
    Q_OBJECT
public:
    struct Options {
        QStringList roots;
        QStringList filters;
        bool hideFiltered = true;
        bool showFiles = true;
        bool showHidden = false;
        QStringList columns{ QStringLiteral( "name" ), QStringLiteral( "size" ),
                             QStringLiteral( "modified" ) };
        QString selection = QStringLiteral( "single" );
        bool toggleSelection = false;
        QString select = QStringLiteral( "both" );
        bool checkboxes = false;
        QString contextMenu;
        bool copy = true;
    };
    struct Hooks {
        std::function<bool( const QString& type, const QJsonValue& value, const QJsonObject& extra )>
            emitEvent;
        std::function<bool()> canAct;
        std::function<void( QMenu* menu, const QString& menuId, const QJsonObject& target )>
            elementMenu;
        std::function<void( const QString& key, const QString& message )> warn;
    };

    PluginFileTreeBehaviour( QTreeView* tree, Hooks hooks );
    ~PluginFileTreeBehaviour() override;

    void setOptions( const Options& options );
    const Options& options() const
    {
        return options_;
    }
    pluginview::PluginFileRootsModel* model() const
    {
        return roots_;
    }
    // The selected paths (with check boxes, the checked ones), at most `maximum`, and their count.
    QStringList value( int maximum, int* count = nullptr ) const;
    // Selects (or checks) the paths in the roots and reveals the first; no event.
    void setValue( const QStringList& paths );
    void expandPaths( const QStringList& paths );
    QString pathAt( const QModelIndex& index ) const;
    QJsonObject menuTarget( const QString& path ) const;
    QMenu* buildMenu( const QPoint& position );
    // Copy Path: the selected (or checked, else the current) paths, whole, at most MaxCopyChars.
    QString copyText() const;
    // Whether Copy Path has something to copy (without building the text).
    bool canCopy() const;

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    // Calls `visit` with the selected paths in shown order until it returns false; returns how
    // many rows are selected (all of them, without building their paths).
    int visitSelected( const std::function<bool( const QString& path )>& visit ) const;
    void applyColumns();
    void applySelectionMode();
    void emitSelected();
    void activate( const QModelIndex& index );
    void requestCheck( const QString& path, bool checked );
    void reveal( const QModelIndex& index );

    QPointer<QTreeView> tree_;
    Hooks hooks_;
    Options options_;
    QFileSystemModel* files_ = nullptr;
    pluginview::PluginFileRootsModel* roots_ = nullptr;
    std::unique_ptr<QFileIconProvider> icons_;
    QString selectionAnchor_;
    bool configured_ = false;
};
