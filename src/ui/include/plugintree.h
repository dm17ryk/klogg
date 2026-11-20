// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "plugintablemodel.h"
#include <QAbstractItemModel>
#include <QHash>
#include <QItemSelection>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QMenu;
class QTreeView;

// The `tree` element of plugin views (docs/plugin-sdk-advanced.md sections 11.1 and 28.1).
namespace pluginview {
// The node id of a tree item, in the role tables use for row ids.
constexpr int NodeIdRole = RowIdRole;

// Plugin tree nodes. Identity is the node id, so patches never rebuild the view. Cells, cell
// objects, editors and unconfirmed edits work as in PluginTableModel.
class PluginTreeModel final : public QAbstractItemModel {
public:
    using Column = PluginTableModel::Column;
    using CellStyle = PluginTableModel::CellStyle;
    using Pending = PluginTableModel::Pending;
    enum class Load { None, Loading, Failed };
    // Above this many ids remove() resets the model once instead of removing row by row.
    static constexpr int LargeRemoval = 64;
    struct Node {
        QString id;
        QStringList cells;
        QVector<CellStyle> styles; // cell objects; empty when every cell is plain text
        QJsonValue icon;           // in the first column
        QString tooltip;
        QString tooltipKey;
        QString tone;
        QJsonObject editors;         // column id -> editor spec
        QHash<int, Pending> pending; // by column
        bool hasChildren = false;    // `has_children`: children to load (lazy trees)
        int checkable = -1;          // -1: the tree's `checkboxes`; 0 or 1: the node's own
        Qt::CheckState checked = Qt::Unchecked;
        Load load = Load::None;
        bool placeholder = false; // the "Loading…" or "Could not load" child
        Node* parent = nullptr;
        int row = 0; // the position among the parent's children
        std::vector<std::unique_ptr<Node>> children;
    };

    explicit PluginTreeModel( QObject* parent );
    ~PluginTreeModel() override;

    // Set by the view, as for tables: whether an edit may be committed now, the report of a
    // committed edit (false when it could not reach the plugin), icons, and check box clicks.
    std::function<bool()> canEdit;
    std::function<bool( const QString& nodeId, const QString& columnId, const QJsonValue& value,
                        const QString& previous )>
        onEdited;
    std::function<QIcon( const QJsonValue& reference )> iconFor;
    std::function<void( const QString& nodeId, bool checked )> onCheckRequested;
    // A problem with plugin data (duplicate ids, depth, limits); the view logs it once.
    std::function<void( const QString& key, const QString& message )> warn;

    QModelIndex index( int row, int column, const QModelIndex& parent = {} ) const override;
    QModelIndex parent( const QModelIndex& child ) const override;
    int rowCount( const QModelIndex& parent = {} ) const override;
    int columnCount( const QModelIndex& parent = {} ) const override;
    bool hasChildren( const QModelIndex& parent = {} ) const override;
    QVariant data( const QModelIndex& index, int role ) const override;
    QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;
    Qt::ItemFlags flags( const QModelIndex& index ) const override;
    bool setData( const QModelIndex& index, const QVariant& value, int role ) override;

    // Options of the tree element.
    void setCheckboxes( bool checkboxes );
    void setAutoTristate( bool autoTristate );
    void setLazy( bool lazy )
    {
        lazy_ = lazy;
    }
    bool lazy() const
    {
        return lazy_;
    }
    // Columns as table columns; false (and nothing changes) for more than 64 columns. Without
    // columns the tree has one column without an id.
    bool setColumns( const QJsonArray& columns );
    bool hasColumns() const
    {
        return !columns_.isEmpty();
    }
    const Column& column( int index ) const;
    int columnOf( const QString& id ) const;
    QString columnId( int column ) const;

    // Node operations (section 28.1). A parent id that is empty means the top level. They return
    // false, with `error`, when they change nothing (an unknown parent, more than MaxTreeNodes
    // nodes). `changed` receives the ids whose cached tooltips are stale.
    bool replace( const QJsonArray& nodes, const QString& parentId, QString* error,
                  QStringList* changed = nullptr );
    bool update( const QJsonArray& nodes, QString* error, QStringList* changed = nullptr );
    bool insert( const QJsonArray& nodes, const QString& parentId, const QString& before,
                 QString* error );
    // The removed ids, descendants included.
    QStringList remove( const QJsonArray& ids );
    int nodeCount() const
    {
        return nodeCount_;
    }

    // Lazy children (section 28.1): whether expanding the node asks the plugin, the placeholder
    // while it answers, and the answer.
    bool wantsChildren( const QModelIndex& index ) const;
    void setLoading( const QString& id );
    void setLoadFailed( const QString& id );
    bool isLoading( const QString& id ) const;
    bool fillChildren( const QString& id, const QJsonArray& nodes, QString* error );
    // `expanded` keys of nodes added or updated since the last call, by id.
    QHash<QString, bool> takeExpandRequests();

    QModelIndex indexOf( const QString& id, int column = 0 ) const;
    QString idOf( const QModelIndex& index ) const;
    const Node* nodeAt( const QModelIndex& index ) const;
    bool contains( const QString& id ) const
    {
        return index_.contains( id );
    }
    // Whether `ancestor` is `id` or one of its ancestors.
    bool isWithin( const QString& id, const QString& ancestor ) const;
    int depthOf( const QString& id ) const;
    QString parentIdOf( const QString& id ) const;
    // Every loaded node (placeholders left out) in tree order with its depth (0: top level).
    void forEach( const std::function<void( const Node& node, int depth )>& visit ) const;

    bool isCheckable( const Node& node ) const;
    // Sets the check state of nodes (with auto_tristate their loaded descendants too); returns the
    // ids set, the given ones first, and the previous state of every node it changed.
    QStringList setChecked( const QStringList& ids, bool checked,
                            QHash<QString, Qt::CheckState>* previous = nullptr );
    void restoreChecks( const QHash<QString, Qt::CheckState>& states );
    void revertPending();

private:
    Node* find( const QString& id ) const;
    Node* nodeOf( const QModelIndex& index ) const;
    QModelIndex indexOfNode( const Node* node, int column = 0 ) const;
    // Parses nodes below `parent` at `depth` (1: top level); skips nodes without an id, nodes
    // deeper than MaxTreeDepth and repeated ids: an id is taken when the tree has it (unless it
    // is in `replaced`, the nodes this operation removes) or this operation already used it
    // (`fresh`).
    std::vector<std::unique_ptr<Node>> parseNodes( const QJsonArray& nodes, Node* parent, int depth,
                                                   const QSet<QString>* replaced,
                                                   QSet<QString>* fresh, bool inheritCheck );
    void parseInto( Node& node, const QJsonObject& object, bool whole ) const;
    void addToIndex( Node& node );      // adds the node's descendants to the id index
    void removeFromIndex( Node& node ); // removes them
    void collectIds( const Node& node, QStringList* ids ) const;
    void setChildren( Node* parent, std::vector<std::unique_ptr<Node>> children );
    static int countNodes( const QJsonArray& nodes, int depth );
    int countLoaded( const Node& node ) const;
    // auto_tristate: parents follow their checkable children; `previous` receives the state each
    // changed node had before (unless it holds one already).
    void recomputeChecks( QHash<QString, Qt::CheckState>* previous = nullptr );
    Qt::CheckState derive( Node& node, QList<Node*>* changed,
                           QHash<QString, Qt::CheckState>* previous );
    // One dataChanged per parent for the check boxes of `nodes`: a view resolves each signal
    // separately, which is slow for thousands of single rows.
    void emitCheckChanges( const QList<Node*>& nodes );
    QStringList applyChecked( const QStringList& ids, bool checked,
                              QHash<QString, Qt::CheckState>* previous, bool recompute );
    int cellLimit() const;
    void undoEdit( const QString& nodeId, int column, const QString& display,
                   const QString& shown, const std::optional<Pending>& earlier );
    void noteExpand( const QJsonObject& object );

    QVector<Column> columns_;
    Node root_;
    QHash<QString, Node*> index_;
    QHash<QString, bool> expandRequests_;
    int nodeCount_ = 0;
    int deferChecks_ = 0; // > 0: an operation in progress derives the parents' states at its end
    bool checkboxes_ = false;
    bool autoTristate_ = false;
    bool lazy_ = false;
};
} // namespace pluginview

// The behaviour of a plugin view tree: toggle selection, check boxes, lazy children, `activated`,
// `expanded`/`collapsed`, reordering nodes by drag (`moved`), the element context menu with Copy,
// Copy All and Export CSV, the selection patch and keeping expansion and selection across a
// replacement. Owned by the tree view; reports through hooks the view sets.
class PluginTreeBehaviour final : public QObject {
    Q_OBJECT
public:
    static constexpr char NodesMimeType[] = "application/x-cilogg-plugin-nodes";

    struct Options {
        bool header = true;
        bool toggleSelection = false;
        QString contextMenu;
        bool copy = true;
        bool exportCsv = false;
        bool reorderable = false;
        bool expandEvents = false;
    };
    struct Hooks {
        // Sends a view event of this tree; false when it could not be delivered.
        std::function<bool( const QString& type, const QJsonValue& value )> emitEvent;
        // Whether the user may act now: a live view outside a plugin update.
        std::function<bool()> canAct;
        // Adds the plugin's entries of `menuId` for `target` ({node_id, selected_ids,
        // selected_count}).
        std::function<void( QMenu* menu, const QString& menuId, const QJsonObject& target )>
            elementMenu;
        std::function<QString( const QString& title, const QString& filter,
                               const QString& suggestedName )>
            saveFile;
        // Asks the plugin for a node's children (tree/children); false when it was refused.
        std::function<bool( const QString& nodeId )> loadChildren;
        // The node no longer waits (its children were set or it was removed).
        std::function<void( const QString& nodeId )> cancelChildren;
        // The selection changed for a reason other than the user (a replacement dropped nodes).
        std::function<void()> selectionChanged;
    };

    PluginTreeBehaviour( QTreeView* tree, pluginview::PluginTreeModel* model, Hooks hooks );

    void setOptions( const Options& options );
    const Options& options() const
    {
        return options_;
    }
    // Around a model operation: selection and current node, and with `expansion` (an operation
    // that resets the model or replaces children) the expanded nodes and the scroll position, are
    // kept by id. Without it the view keeps them itself; only removed selected nodes are reported.
    void rememberView( bool expansion = true );
    void restoreView();
    // After a model operation: `expanded` keys of the nodes, and loading for expanded lazy nodes.
    void applyExpandRequests();
    // The selection patch: never sends `selected`; ancestors of current and scrollTo expand.
    void select( const QJsonValue& ids, const QJsonValue& current, const QJsonValue& scrollTo );
    void expandNodes( const QJsonArray& ids, bool expand );
    QString currentNodeId() const;
    // The selected nodes in tree order (at most `maximum` ids) and their count.
    QStringList selectedNodeIds( int maximum, int* count = nullptr ) const;
    QJsonObject menuTarget( const QString& nodeId ) const;
    QMenu* buildMenu( const QPoint& position );
    QString copyText( bool all ) const;
    bool exportCsv( const QString& path, QString* error ) const;
    // Nodes dropped under `parent` (empty: the top level) before `before` (empty: the end).
    bool moveNodes( const QStringList& nodeIds, const QString& parent, const QString& before );
    // The plugin's answer to tree/children (`answered` false: failed or timed out).
    void childrenAnswered( const QString& nodeId, bool answered, const QJsonArray& nodes );
    // Loading nodes show "Could not load" (the plugin stopped).
    void failLoading();
    // The nodes a drag that starts now moves, as `moved` sends them (at most MaxTableIds and
    // MaxIdBytes of ids, in tree order): the selection before the press when the pressed node
    // was in it, else the pressed node alone; empty without a press that may start a drag.
    QStringList draggedNodes() const;

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    void requestCheck( const QString& nodeId, bool checked );
    void activate( const QModelIndex& index );
    void expandedChanged( const QModelIndex& index, bool expanded );
    void startLoad( const QString& nodeId );
    void startDrag();
    bool drop( const QPoint& position, const QByteArray& payload );
    QList<int> shownColumns() const;
    void ensureVisible( const QString& nodeId );

    QPointer<QTreeView> tree_;
    pluginview::PluginTreeModel* model_;
    Hooks hooks_;
    Options options_;
    QSet<QString> loading_; // nodes waiting for tree/children
    QStringList keptExpanded_;
    QStringList keptSelected_;
    QString keptCurrent_;
    int keptScroll_ = 0;
    bool keptExpansion_ = false;
    QString selectionAnchor_;
    QPoint pressPosition_;
    QString pressNode_;
    QItemSelection pressSelection_; // the selection when the drag's press came (ranges)
    bool pressWasSelected_ = false; // whether the pressed node was in it
};
