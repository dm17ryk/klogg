// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginview.h"
#include "checkcombobox.h"
#include "pluginchoicewidgets.h"
#include "pluginfeatures.h"
#include "pluginfiletree.h"
#include "plugintablebehaviour.h"
#include "plugintablemodel.h"
#include "plugintree.h"
#include "pluginviewnode.h"
#include "log.h"
#include <QMenu>
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleValidator>
#include <QEvent>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QRegularExpressionValidator>
#include <QSlider>
#include <QSpinBox>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

using namespace pluginview;

// host_limits in `initialize` announce these view limits.
static_assert( PluginView::MaxElements == cilogg::plugins::MaxViewElements );
static_assert( PluginView::MaxTableCells == cilogg::plugins::MaxViewTableCells );
static_assert( PluginView::MaxTextChars == cilogg::plugins::MaxViewTextChars );

namespace {
const QStringList ContainerTypes{ "column", "row", "group" };
const QStringList ElementTypes{ "column",   "row",      "group",       "label",
                                "badge",    "field",    "button",      "progress",
                                "table",    "checkbox", "radio_group", "check_group",
                                "tree",     "file_tree" };
// E17c adds the kinds after multichoice (docs/plugin-sdk-advanced.md section 8.5).
const QStringList FieldKinds{ "text",      "number",   "choice",  "boolean", "file_open",
                              "file_save", "directory", "multichoice", "multiline", "secret",
                              "integer",   "slider",   "color",   "date",    "time",
                              "datetime" };
// Field keys that change a field's widgets: a root that changes them rebuilds the field.
const QStringList FieldStructureKeys{ "kind", "style", "editable", "searchable", "indicator",
                                      "completion" };
constexpr int DefaultSliderMinimum = 0;
constexpr int DefaultSliderMaximum = 100;
const QString DateFormat = QStringLiteral( "yyyy-MM-dd" );
const QString TimeFormat = QStringLiteral( "HH:mm:ss" );
const QString DateTimeFormat = QStringLiteral( "yyyy-MM-dd'T'HH:mm:ss" );
// E17a adds advanced tooltips and accessibility overrides to every element
// (docs/plugin-sdk-advanced.md sections 9 and 12.4).
const QStringList CommonKeys{ "id",
                              "type",
                              "enabled",
                              "visible",
                              "tooltip",
                              "tooltip_provider",
                              "tooltip_key",
                              "tooltip_timeout_ms",
                              "accessible_name",
                              "accessible_description" };
// Properties that keep their state when a same-shaped root omits them. `rows` and `columns` are
// a table's content; on a field (multiline rows, inline choice columns) they are looks, which a
// root that omits them returns to their defaults. E17e: a tree's `nodes` and `columns` and a
// file tree's `roots` and `columns` are content too.
const QStringList KeptWhenAbsent{ "value", "kind", "children" };
const QStringList TableKeptWhenAbsent{ "rows", "columns" };
const QStringList TreeKeptWhenAbsent{ "nodes", "columns" };
const QStringList FileTreeKeptWhenAbsent{ "roots", "columns" };

QStringList knownKeys( const QString& type )
{
    if ( type == "column" || type == "row" ) {
        return { "children" };
    }
    if ( type == "group" ) {
        return { "children", "title", "collapsible", "collapsed" };
    }
    if ( type == "label" ) {
        return { "text", "tone", "bold" };
    }
    if ( type == "badge" ) {
        return { "text", "tone" };
    }
    if ( type == "field" ) {
        // E17c: choice styles, field kinds and validation (section 8).
        return { "kind",       "label",      "value",     "choices",    "filter",
                 "placeholder", "indicator", "style",     "orientation", "columns",
                 "searchable", "editable",   "completion", "rows",      "monospace",
                 "min",        "max",        "step",      "required",   "pattern",
                 "min_length", "max_length", "error",     "completion_timeout_ms" };
    }
    if ( type == "button" ) {
        return { "label", "tone",        "action",        "arguments",
                 "event", "confirm",     "save_dialog",   "requires_valid" };
    }
    if ( type == "progress" ) {
        return { "value", "busy", "text" };
    }
    if ( type == "checkbox" ) {
        return { "text", "value", "tristate", "tone" };
    }
    if ( type == "radio_group" || type == "check_group" ) {
        return { "title", "choices", "value", "orientation", "columns", "all_none" };
    }
    // E17e: trees and file trees (section 28).
    if ( type == "tree" ) {
        return { "columns",       "nodes",       "selection", "toggle_selection", "checkboxes",
                 "auto_tristate", "lazy",        "expand_events", "header",        "context_menu",
                 "copy",          "export",      "reorderable" };
    }
    if ( type == "file_tree" ) {
        return { "roots",     "filters",          "hide_filtered", "show_files", "show_hidden",
                 "columns",   "selection",        "toggle_selection", "select",  "checkboxes",
                 "value",     "context_menu",     "copy" };
    }
    // E17d: rich tables and element context menus (section 10).
    return { "columns",      "rows",   "sortable",    "filter_box",  "selection",
             "header",       "toggle_selection",      "context_menu", "copy",
             "export",       "reorderable",           "keep_columns" };
}

// Defaults restored when a same-shaped root no longer sends a property. Properties without a
// default here (button action, arguments, event, confirm, save_dialog) are simply dropped.
QJsonObject defaultsFor( const QString& type )
{
    QJsonObject defaults{ { "enabled", true },        { "visible", true },
                          { "tooltip", "" },          { "tooltip_provider", false },
                          { "tooltip_key", "" },      { "accessible_name", "" },
                          { "accessible_description", "" } };
    if ( type == "group" ) {
        defaults.insert( "title", "" );
        defaults.insert( "collapsible", false );
        defaults.insert( "collapsed", false );
    }
    else if ( type == "label" || type == "badge" ) {
        defaults.insert( "text", "" );
        defaults.insert( "tone", "normal" );
        if ( type == "label" ) {
            defaults.insert( "bold", false );
        }
    }
    else if ( type == "field" ) {
        defaults.insert( "label", "" );
        defaults.insert( "placeholder", "" );
        defaults.insert( "choices", QJsonArray{} );
        defaults.insert( "filter", "" );
        defaults.insert( "required", false );
        defaults.insert( "pattern", "" );
        defaults.insert( "error", "" );
    }
    else if ( type == "checkbox" ) {
        defaults.insert( "text", "" );
        defaults.insert( "tone", "normal" );
        defaults.insert( "tristate", false );
    }
    else if ( type == "radio_group" || type == "check_group" ) {
        defaults.insert( "title", "" );
        defaults.insert( "choices", QJsonArray{} );
        defaults.insert( "orientation", "vertical" );
        defaults.insert( "columns", 0 );
        defaults.insert( "all_none", false );
    }
    else if ( type == "button" ) {
        defaults.insert( "label", "" );
        defaults.insert( "tone", "normal" );
    }
    else if ( type == "progress" ) {
        defaults.insert( "value", 0.0 );
        defaults.insert( "busy", false );
        defaults.insert( "text", "" );
    }
    else if ( type == "table" ) {
        defaults.insert( "sortable", false );
        defaults.insert( "filter_box", false );
        defaults.insert( "selection", "none" );
        defaults.insert( "header", true );
        defaults.insert( "toggle_selection", false );
        defaults.insert( "context_menu", "" );
        defaults.insert( "copy", true );
        defaults.insert( "export", false );
        defaults.insert( "reorderable", false );
        defaults.insert( "keep_columns", true );
    }
    else if ( type == "tree" ) {
        defaults.insert( "selection", "none" );
        defaults.insert( "toggle_selection", false );
        defaults.insert( "checkboxes", false );
        defaults.insert( "auto_tristate", false );
        defaults.insert( "lazy", false );
        defaults.insert( "expand_events", false );
        defaults.insert( "header", true );
        defaults.insert( "context_menu", "" );
        defaults.insert( "copy", true );
        defaults.insert( "export", false );
        defaults.insert( "reorderable", false );
    }
    else if ( type == "file_tree" ) {
        defaults.insert( "filters", QJsonArray{} );
        defaults.insert( "hide_filtered", true );
        defaults.insert( "show_files", true );
        defaults.insert( "show_hidden", false );
        defaults.insert( "selection", "single" );
        defaults.insert( "toggle_selection", false );
        defaults.insert( "select", "both" );
        defaults.insert( "checkboxes", false );
        defaults.insert( "context_menu", "" );
        defaults.insert( "copy", true );
    }
    return defaults;
}

void setTone( QWidget* widget, const QString& tone, bool badge )
{
    widget->setProperty( "tone", tone );
    const auto color = toneColor( tone );
    if ( badge ) {
        widget->setStyleSheet(
            QStringLiteral( "QLabel { border-radius: 8px; padding: 1px 8px; color: white; "
                            "background: %1; }" )
                .arg( color.isValid() ? color.name() : QStringLiteral( "#607d8b" ) ) );
    }
    else {
        widget->setStyleSheet( color.isValid() ? QStringLiteral( "color: %1;" ).arg( color.name() )
                                               : QString{} );
    }
}

// Runs a heap dialog modally; returns false when the dialog was deleted or rejected meanwhile.
template <typename Dialog>
bool runDialog( Dialog* dialog )
{
    QPointer<Dialog> guard( dialog );
    dialog->exec();
    return !guard.isNull();
}
} // namespace

PluginViewDialogs PluginViewDialogs::native()
{
    // Heap dialogs with guards: the view (or its window) may be deleted while one is open.
    PluginViewDialogs dialogs;
    dialogs.confirm = []( QWidget* parent, const QString& objectName, const QString& title,
                          const QString& message, const QString& okLabel ) {
        auto* box = new QMessageBox( QMessageBox::Question, title, message, QMessageBox::NoButton,
                                     parent );
        box->setObjectName( objectName );
        box->setTextFormat( Qt::PlainText );
        auto* ok = box->addButton( okLabel.isEmpty() ? QObject::tr( "OK" ) : okLabel,
                                   QMessageBox::AcceptRole );
        auto* cancel = box->addButton( QMessageBox::Cancel );
        ok->setObjectName( objectName + "_ok" );
        cancel->setObjectName( objectName + "_cancel" );
        box->setDefaultButton( cancel );
        QPointer<QAbstractButton> accepted( ok );
        if ( !runDialog( box ) ) {
            return false;
        }
        const bool confirmed = accepted && box->clickedButton() == accepted.data();
        delete box;
        return confirmed;
    };
    const auto pickFile = []( QWidget* parent, const QString& objectName, const QString& title,
                              const QString& filter, const QString& suggestedName,
                              QFileDialog::AcceptMode accept, QFileDialog::FileMode mode ) {
        auto* dialog = new QFileDialog( parent, title, suggestedName, filter );
        dialog->setObjectName( objectName );
        dialog->setAcceptMode( accept );
        dialog->setFileMode( mode );
        if ( mode == QFileDialog::Directory ) {
            dialog->setOption( QFileDialog::ShowDirsOnly, true );
        }
        if ( !runDialog( dialog ) ) {
            return QString{};
        }
        const auto selected = dialog->result() == QDialog::Accepted
                                  ? dialog->selectedFiles().value( 0 )
                                  : QString{};
        delete dialog;
        return selected;
    };
    dialogs.saveFile
        = [ pickFile ]( QWidget* parent, const QString& objectName, const QString& title,
                        const QString& filter, const QString& suggestedName ) {
              return pickFile( parent, objectName, title, filter, suggestedName,
                               QFileDialog::AcceptSave, QFileDialog::AnyFile );
          };
    dialogs.openFile = [ pickFile ]( QWidget* parent, const QString& objectName,
                                     const QString& title, const QString& filter ) {
        return pickFile( parent, objectName, title, filter, {}, QFileDialog::AcceptOpen,
                         QFileDialog::ExistingFile );
    };
    dialogs.directory
        = [ pickFile ]( QWidget* parent, const QString& objectName, const QString& title ) {
              return pickFile( parent, objectName, title, {}, {}, QFileDialog::AcceptOpen,
                               QFileDialog::Directory );
          };
    dialogs.color = []( QWidget* parent, const QString& objectName, const QString& title,
                        const QString& current ) {
#if QT_VERSION >= QT_VERSION_CHECK( 6, 4, 0 )
        auto* dialog = new QColorDialog( QColor::fromString( current ), parent );
#else
        auto* dialog = new QColorDialog( QColor( current ), parent );
#endif
        dialog->setObjectName( objectName );
        dialog->setWindowTitle( title );
        if ( !runDialog( dialog ) ) {
            return QString{};
        }
        const auto chosen = dialog->result() == QDialog::Accepted
                                ? dialog->selectedColor().name( QColor::HexRgb )
                                : QString{};
        delete dialog;
        return chosen;
    };
    return dialogs;
}

PluginView::PluginView( const QString& pluginId, const QString& viewId, PluginViewDialogs dialogs,
                        QWidget* parent )
    : QWidget( parent )
    , pluginId_( pluginId )
    , viewId_( viewId )
    , dialogs_( std::move( dialogs ) )
{
    // A distinct prefix: element ids may be any string, including "body".
    setObjectName( objectNameFor( pluginId, viewId ).replace( 0, 10, "pluginViewBody" ) );
    auto* layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    scroll_ = new QScrollArea( this );
    scroll_->setFrameShape( QFrame::NoFrame );
    scroll_->setWidgetResizable( true );
    layout->addWidget( scroll_ );
    tooltips_ = new PluginTooltips( this );
}

void PluginView::setQueries( PluginViewQueries queries )
{
    queries_ = queries; // completion and choice icons (E17c)
    tooltips_->setQueries( std::move( queries ) );
}

void PluginView::invalidateTooltips( const QString& elementId, const QStringList& rowIds,
                                     const QStringList& nodeIds )
{
    if ( elementId.isEmpty() ) {
        tooltips_->clear();
        return;
    }
    if ( rowIds.isEmpty() && nodeIds.isEmpty() ) {
        tooltips_->invalidateElement( elementId );
        return;
    }
    tooltips_->invalidateRows( elementId, rowIds );
    tooltips_->invalidateNodes( elementId, nodeIds );
}

void PluginView::resetTooltips()
{
    tooltips_->cancelPending();
    tooltips_->clear();
}

PluginTooltips::Target PluginView::tooltipTarget( const QString& elementId, const QString& rowId,
                                                  const QString& columnId, const QString& nodeId,
                                                  const QModelIndex& pointed ) const
{
    PluginTooltips::Target target;
    const auto node = nodes_.value( elementId );
    if ( !node || node->automatic || !node->widget ) {
        return target;
    }
    const auto& props = node->props;
    target.elementId = elementId;
    target.provider = props.value( "tooltip_provider" ).toBool();
    target.key = text( props, "tooltip_key", cilogg::plugins::MaxTooltipKeyChars );
    target.tooltipKey = target.key;
    target.staticTip = text( props, "tooltip" );
    target.timeoutMs = props.value( "tooltip_timeout_ms" ).toInt();
    target.rect = node->widget->rect();
    if ( node->treeModel || node->fileTree ) {
        return treeTooltipTarget( *node, target, nodeId, columnId, pointed ); // E17e
    }
    if ( node->type != "table" || !node->model ) {
        return target;
    }
    // A cell: the table, its column or its row may ask for the tooltip.
    const auto row = node->model->rowOf( rowId );
    int column = -1;
    for ( int index = 0; index < node->model->columnCount(); ++index ) {
        if ( node->model->columnId( index ) == columnId ) {
            column = index;
        }
    }
    const auto source = node->model->index( row, column );
    if ( row < 0 || column < 0 ) {
        target.provider = false;
        return target;
    }
    const auto cell = node->model->tooltipTarget( source, target.provider, target.key );
    target.rowId = cell.rowId;
    target.columnId = cell.columnId;
    target.provider = cell.provider;
    target.key = cell.key;
    target.staticTip = source.data( Qt::ToolTipRole ).toString();
    const auto* table = qobject_cast<QTableView*>( node->widget.data() );
    const auto proxied = node->proxy ? node->proxy->mapFromSource( source ) : source;
    target.rect = table ? table->visualRect( proxied ) : QRect{};
    // The most specific key goes into the query: the cell's (E17d), else the row's, else the
    // column's, else the table's.
    target.tooltipKey = cell.tooltipKey;
    return target;
}

void PluginView::watchTooltips( Node& node )
{
    const auto id = node.id;
    if ( node.treeModel || node.fileTree ) {
        watchTreeTooltips( node ); // E17e: per node or path
        return;
    }
    if ( node.type != "table" ) {
        tooltips_->watch( node.widget, [ this, id ]( const QPoint& ) {
            return tooltipTarget( id, {}, {} );
        } );
        return;
    }
    auto* table = qobject_cast<QTableView*>( node.widget.data() );
    if ( !table ) {
        return;
    }
    tooltips_->watch( table->viewport(), [ this, id, table ]( const QPoint& position ) {
        const auto found = nodes_.value( id );
        const auto index = table->indexAt( position );
        if ( !found || !found->model || !index.isValid() ) {
            return PluginTooltips::Target{};
        }
        const auto source = found->proxy ? found->proxy->mapToSource( index ) : index;
        return tooltipTarget( id, source.data( RowIdRole ).toString(),
                              found->model->columnId( source.column() ) );
    } );
}

bool PluginView::resolveTooltip( const QString& elementId, const QString& rowId,
                                 const QString& columnId, bool useCache, TooltipResolved done,
                                 QString* error )
{
    return resolveTooltip( elementId, rowId, columnId, {}, useCache, std::move( done ), error );
}

bool PluginView::resolveTooltip( const QString& elementId, const QString& rowId,
                                 const QString& columnId, const QString& nodeId, bool useCache,
                                 TooltipResolved done, QString* error )
{
    const auto fail = [ error ]( const QString& message ) {
        if ( error ) {
            *error = message;
        }
        return false;
    };
    const auto node = nodes_.value( elementId );
    if ( !node || node->automatic ) {
        return fail( tr( "The view has no element %1." ).arg( elementId.left( 128 ) ) );
    }
    // E17e: a tree node (with an optional column) or a file tree path; nothing else takes --node-id.
    const bool tree = node->treeModel || node->fileTree;
    if ( tree != !nodeId.isEmpty() ) {
        return fail( tree ? tr( "A tree tooltip needs a node id." )
                          : tr( "Only trees and file trees take a node id." ) );
    }
    if ( tree && !rowId.isEmpty() ) {
        return fail( tr( "A tree has nodes, not rows." ) );
    }
    if ( node->type == "table" && ( rowId.isEmpty() || columnId.isEmpty() ) ) {
        return fail( tr( "A table tooltip needs a row id and a column id." ) );
    }
    const auto target = tooltipTarget( elementId, rowId, columnId, nodeId );
    if ( tree && target.nodeId.isEmpty() ) {
        return fail( tr( "The tree has no node %1." ).arg( nodeId.left( 128 ) ) );
    }
    if ( node->type == "table" && target.rowId.isEmpty() ) {
        if ( error ) {
            *error = tr( "The table has no cell %1 / %2." )
                         .arg( rowId.left( 128 ), columnId.left( 128 ) );
        }
        return false;
    }
    const auto staticTip = target.staticTip;
    tooltips_->resolve( target, useCache,
                        [ staticTip, callback = std::move( done ) ](
                            const QString& source, const QJsonObject& tooltip, qint64 latencyMs ) {
                            callback( source, tooltip, staticTip, latencyMs );
                        } );
    return true;
}

PluginView::~PluginView()
{
    cancelDialogs();
    cancelAllChildren();
    flushKeptColumns();
    detachTables();
}

// A column change still waiting for its save is written while this view and its queries are
// whole: the tables are deleted later, with the widgets.
void PluginView::flushKeptColumns()
{
    for ( const auto& node : std::as_const( nodes_ ) ) {
        if ( node && node->behaviour ) {
            node->behaviour->flushPendingSave();
        }
    }
}

// Table and tree models call back into the view. The widget teardown after the destructor can
// still commit an open cell editor (closing a window moves the focus), and a replaced tree is
// deleted later: neither may reach this view any more.
void PluginView::detachTables()
{
    for ( const auto& node : std::as_const( nodes_ ) ) {
        if ( node && node->model ) {
            node->model->canEdit = {};
            node->model->onEdited = {};
        }
        if ( node && node->treeModel ) {
            node->treeModel->canEdit = {};
            node->treeModel->onEdited = {};
            node->treeModel->onCheckRequested = {};
        }
    }
}

QString PluginView::objectNameFor( const QString& pluginId, const QString& viewId,
                                   const QString& elementId )
{
    static const QRegularExpression unsafe( "[^A-Za-z0-9._-]" );
    auto name = QStringLiteral( "pluginView_" ) + QString( pluginId ).replace( unsafe, "_" ) + "_"
                + QString( viewId ).replace( unsafe, "_" );
    if ( !elementId.isEmpty() ) {
        name += "_" + QString( elementId ).replace( unsafe, "_" );
    }
    return name;
}

QString PluginView::limitText( const QString& value, int maximum )
{
    return value.left( maximum );
}

QString PluginView::nameFor( const QString& elementId ) const
{
    return objectNameFor( pluginId_, viewId_, elementId );
}

QWidget* PluginView::dialogParent()
{
    return dialogParent_ ? dialogParent_.data() : this;
}

void PluginView::cancelDialogs()
{
    if ( activeDialogs_.isEmpty() ) {
        return;
    }
    auto* parent = dialogParent_ ? dialogParent_.data() : this;
    for ( auto* dialog : parent->findChildren<QDialog*>( QString{}, Qt::FindDirectChildrenOnly ) ) {
        if ( activeDialogs_.contains( dialog->objectName() ) && dialog->isVisible() ) {
            LOG_DEBUG << "Plugin view dialog cancelled plugin=" << pluginId_.toStdString()
                      << " view=" << viewId_.toStdString();
            dialog->reject();
        }
    }
}

void PluginView::revertPendingEdits()
{
    for ( const auto& node : std::as_const( nodes_ ) ) {
        if ( node && node->model ) {
            node->model->revertPending();
        }
        if ( node && node->treeModel ) {
            node->treeModel->revertPending();
        }
    }
}

void PluginView::markEventDropped()
{
    eventDropped_ = true; // read by emitEvent once the eventRaised slots return
}

void PluginView::setDialogs( PluginViewDialogs dialogs )
{
    dialogs_ = std::move( dialogs );
}

QWidget* PluginView::element( const QString& id ) const
{
    const auto node = nodes_.value( id );
    return node && !node->automatic ? node->widget.data() : nullptr;
}

void PluginView::warnOnce( const QString& key, const QString& message )
{
    if ( warned_.contains( key ) || warningsSuppressed_ ) {
        return;
    }
    if ( warned_.size() >= MaxWarnings ) {
        warningsSuppressed_ = true;
        LOG_WARNING << "Plugin view plugin=" << pluginId_.toStdString()
                    << " view=" << viewId_.toStdString() << ": more than " << MaxWarnings
                    << " distinct problems; further warnings are suppressed";
        return;
    }
    warned_.insert( key );
    LOG_WARNING << "Plugin view plugin=" << pluginId_.toStdString()
                << " view=" << viewId_.toStdString() << ": " << message.toStdString();
}

namespace {
// Element shape for in-place root updates: types, ids, field kinds and nesting.
QString shapeOf( const QJsonObject& element )
{
    QString shape = element.value( "type" ).toString().left( 64 ) + ':'
                    + element.value( "id" ).toString().left( 128 );
    for ( const auto& key : FieldStructureKeys ) {
        shape += ':' + element.value( key ).toVariant().toString().left( 64 );
    }
    if ( ContainerTypes.contains( element.value( "type" ).toString() ) ) {
        shape += '[';
        for ( const auto& child : element.value( "children" ).toArray() ) {
            shape += shapeOf( child.toObject() ) + ',';
        }
        shape += ']';
    }
    return shape;
}
} // namespace

bool PluginView::sameShape( const QJsonObject& element ) const
{
    return root_ && property( "pluginShape" ).toString() == shapeOf( element );
}

void PluginView::clear()
{
    cancelDialogs();
    cancelAllChildren();
    flushKeptColumns();
    detachTables();
    if ( auto* old = scroll_->takeWidget() ) {
        old->deleteLater();
    }
    nodes_.clear();
    paths_.clear();
    order_.clear();
    limitReached_ = false;
    root_.clear();
    ++generation_;
}

void PluginView::setRoot( const QJsonObject& root )
{
    if ( !ElementTypes.contains( root.value( "type" ).toString() ) ) {
        warnOnce( "root", "view/update root is not an element" );
        return;
    }
    QScopedValueRollback<bool> guard( applying_, true );
    tooltips_->clear(); // a root describes every element again
    if ( sameShape( root ) ) {
        applyInPlace( root, QStringLiteral( "0" ) );
    }
    else {
        clear();
        auto* body = new QWidget;
        auto* layout = new QVBoxLayout( body );
        layout->setContentsMargins( 6, 6, 6, 6 );
        root_ = build( root, body, QStringLiteral( "0" ) );
        if ( root_ ) {
            layout->addWidget( root_ );
        }
        bool hasTable = false;
        for ( const auto& node : nodes_ ) {
            hasTable = hasTable || node->type == "table" || node->type == "tree"
                       || node->type == "file_tree";
        }
        if ( !hasTable ) {
            layout->addStretch( 1 );
        }
        scroll_->setWidget( body );
        setProperty( "pluginShape", shapeOf( root ) );
    }
    refreshValidity();
    ++renders_;
    LOG_DEBUG << "Plugin view rendered plugin=" << pluginId_.toStdString()
              << " view=" << viewId_.toStdString() << " elements=" << nodes_.size()
              << " renders=" << renders_;
}

void PluginView::applyInPlace( const QJsonObject& element, const QString& path )
{
    // Positions identify id-less elements too; skipped elements have no position entry.
    if ( const auto node = nodes_.value( paths_.value( path ) ) ) {
        apply( *node, element, true );
    }
    const auto children = element.value( "children" ).toArray();
    for ( int index = 0; index < children.size(); ++index ) {
        applyInPlace( children.at( index ).toObject(), path + '/' + QString::number( index ) );
    }
}

QWidget* PluginView::build( const QJsonObject& element, QWidget* parent, const QString& path )
{
    const auto type = element.value( "type" ).toString();
    auto id = element.value( "id" ).toString();
    if ( !ElementTypes.contains( type ) ) {
        warnOnce( "type:" + type.left( 64 ), "unknown element type ignored: " + type.left( 64 ) );
        return nullptr;
    }
    if ( nodes_.size() >= MaxElements ) {
        if ( !limitReached_ ) {
            limitReached_ = true;
            warnOnce( "limit", QStringLiteral( "element limit %1 reached" ).arg( MaxElements ) );
        }
        return nullptr;
    }
    if ( id.startsWith( '#' ) ) {
        warnOnce( "reserved:" + id.left( 128 ),
                  "element ids starting with # are reserved; treated as id-less: "
                      + id.left( 128 ) );
        id.clear();
    }
    const bool automatic = id.isEmpty();
    if ( automatic ) {
        id = QStringLiteral( "#%1" ).arg( order_.size() );
    }
    if ( nodes_.contains( id ) || id.size() > 128 ) {
        warnOnce( "id:" + id.left( 128 ),
                  "duplicate or invalid element id ignored: " + id.left( 128 ) );
        return nullptr;
    }
    auto node = std::make_shared<Node>();
    node->id = id;
    node->type = type;
    node->automatic = automatic;
    nodes_.insert( id, node );
    paths_.insert( path, id );
    order_.append( id );

    QWidget* outer = nullptr;
    if ( type == "column" || type == "row" || type == "group" ) {
        QWidget* box = nullptr;
        QWidget* content = nullptr;
        if ( type == "group" ) {
            auto* group = new QGroupBox( parent );
            auto* groupLayout = new QVBoxLayout( group );
            content = new QWidget( group );
            groupLayout->addWidget( content );
            connect( group, &QGroupBox::toggled, content, &QWidget::setVisible );
            box = group;
        }
        else {
            box = new QWidget( parent );
            content = box;
        }
        QBoxLayout* layout = type == "row" ? static_cast<QBoxLayout*>( new QHBoxLayout( content ) )
                                           : new QVBoxLayout( content );
        layout->setContentsMargins( 0, 0, 0, 0 );
        bool expanding = false;
        const auto children = element.value( "children" ).toArray();
        for ( int index = 0; index < children.size(); ++index ) {
            const auto childObject = children.at( index ).toObject();
            if ( auto* childWidget
                 = build( childObject, content, path + '/' + QString::number( index ) ) ) {
                layout->addWidget( childWidget );
                const auto childType = childObject.value( "type" ).toString();
                expanding
                    = expanding || !QStringList{ "button", "label", "badge" }.contains( childType );
            }
        }
        if ( type == "row" && !expanding ) {
            layout->addStretch( 1 );
        }
        node->widget = box;
        node->content = content;
        outer = box;
    }
    else if ( type == "label" || type == "badge" ) {
        auto* label = new QLabel( parent );
        label->setTextFormat( Qt::PlainText );
        label->setWordWrap( type == "label" );
        label->setTextInteractionFlags( Qt::TextSelectableByMouse );
        label->setProperty( "badge", type == "badge" );
        if ( type == "badge" ) {
            label->setSizePolicy( QSizePolicy::Maximum, QSizePolicy::Fixed );
            setTone( label, "normal", true );
        }
        node->widget = label;
        outer = label;
    }
    else if ( type == "field" ) {
        outer = buildField( *node, element, parent );
    }
    else if ( type == "checkbox" || type == "radio_group" || type == "check_group" ) {
        outer = buildToggle( *node, element, parent );
    }
    else if ( type == "button" ) {
        auto* button = new QPushButton( parent );
        connect( button, &QPushButton::clicked, this, [ this, id ] { pressButton( id ); } );
        node->widget = button;
        outer = button;
    }
    else if ( type == "progress" ) {
        auto* bar = new QProgressBar( parent );
        bar->setRange( 0, 1000 );
        bar->setValue( 0 );
        node->widget = bar;
        outer = bar;
    }
    else if ( type == "tree" ) {
        outer = buildTree( *node, parent ); // E17e
    }
    else if ( type == "file_tree" ) {
        outer = buildFileTree( *node, parent );
    }
    else {
        outer = buildTable( *node, element, parent );
    }
    node->outer = outer;
    if ( !automatic ) {
        node->widget->setObjectName( nameFor( id ) );
    }
    apply( *node, element, false );
    if ( !automatic ) {
        watchTooltips( *node );
    }
    return outer;
}

QWidget* PluginView::buildField( Node& node, const QJsonObject& element, QWidget* parent )
{
    const auto id = node.id;
    auto kind = element.value( "kind" ).toString( "text" );
    if ( !FieldKinds.contains( kind ) ) {
        warnOnce( "kind:" + kind.left( 64 ),
                  "unknown field kind treated as text: " + kind.left( 64 ) );
        kind = "text";
    }
    node.kind = kind;
    // The field row, with its validation message below it (E17c, section 8.6).
    auto* outer = new QWidget( parent );
    auto* column = new QVBoxLayout( outer );
    column->setContentsMargins( 0, 0, 0, 0 );
    column->setSpacing( 2 );
    auto* row = new QWidget( outer );
    column->addWidget( row );
    auto* layout = new QHBoxLayout( row );
    layout->setContentsMargins( 0, 0, 0, 0 );
    auto* caption = new QLabel( row );
    caption->setTextFormat( Qt::PlainText );
    caption->setObjectName( nameFor( id ) + "_label" );
    caption->setVisible( false );
    layout->addWidget( caption );
    node.content = caption;
    auto* error = new QLabel( outer );
    error->setObjectName( nameFor( id ) + "_error" );
    error->setTextFormat( Qt::PlainText );
    error->setWordWrap( true );
    error->setStyleSheet( QStringLiteral( "color: #c62828;" ) );
    error->setVisible( false );
    column->addWidget( error );
    node.error = error;
    const auto changedSoon = [ this, id ] {
        const auto found = nodes_.value( id );
        if ( !applying_ && found && found->debounce ) {
            found->debounce->start( debounceMs_ );
        }
    };
    const auto changedNow = [ this, id ] {
        const auto found = nodes_.value( id );
        if ( !applying_ && found ) {
            emitEvent( id, "changed", fieldValue( *found ) );
        }
    };
    buildFieldWidget( node, element, row, changedSoon, changedNow );
    return outer;
}

QWidget* PluginView::buildFieldWidget( Node& node, const QJsonObject& element, QWidget* row,
                                       const std::function<void()>& changedSoon,
                                       const std::function<void()>& changedNow )
{
    const auto id = node.id;
    const auto kind = node.kind;
    auto* layout = qobject_cast<QHBoxLayout*>( row->layout() );
    const auto debounced = [ this, id, &node ]( QObject* owner ) {
        node.debounce = new QTimer( owner );
        node.debounce->setSingleShot( true );
        connect( node.debounce, &QTimer::timeout, this, [ this, id ] {
            if ( const auto found = nodes_.value( id ) ) {
                emitEvent( id, "changed", fieldValue( *found ) );
            }
        } );
    };
    const auto style = element.value( "style" ).toString();
    const auto group = [ & ]( bool exclusive ) {
        // Inline radio buttons or check boxes (style radio / checkboxes).
        auto* box = new QWidget( row );
        auto* boxLayout = new QVBoxLayout( box );
        boxLayout->setContentsMargins( 0, 0, 0, 0 );
        auto* choices = new ChoiceGroup( exclusive, nameFor( id ), box );
        if ( element.value( "searchable" ).toBool() ) {
            auto* search = new QLineEdit( box );
            search->setObjectName( nameFor( id ) + "_search" );
            search->setPlaceholderText( tr( "Filter" ) );
            const auto caption = text( element, "label" );
            search->setAccessibleName( caption.isEmpty() ? tr( "Filter" )
                                                         : tr( "Filter %1" ).arg( caption ) );
            search->setClearButtonEnabled( true );
            search->setMaxLength( MaxTextChars );
            connect( search, &QLineEdit::textChanged, choices, &ChoiceGroup::filter );
            boxLayout->addWidget( search );
        }
        boxLayout->addWidget( choices );
        connect( choices, &ChoiceGroup::changed, this, changedNow );
        layout->addWidget( box, 1 );
        node.widget = choices;
        return row;
    };
    if ( kind == "multichoice" && style == QLatin1String( "checkboxes" ) ) {
        return group( false );
    }
    if ( kind == "choice" && style == QLatin1String( "radio" ) ) {
        return group( true );
    }
    if ( kind == "multichoice" ) {
        // Skips the text-field debounce path on purpose: no typing, every toggle is final.
        auto* box = new CheckComboBox( row );
        if ( element.value( "searchable" ).toBool() ) {
            box->makeSearchable();
        }
        connect( box, &CheckComboBox::checkedChanged, this, [ this, id ] {
            const auto found = nodes_.value( id );
            if ( !applying_ && found ) {
                emitEvent( id, "changed", fieldValue( *found ) );
            }
        } );
        layout->addWidget( box, 1 );
        node.widget = box;
        return row;
    }
    if ( kind == "boolean" ) {
        auto* box = new QCheckBox( row );
        connect( box, &QCheckBox::toggled, this, [ this, id ]( bool checked ) {
            if ( !applying_ ) {
                emitEvent( id, "changed", checked );
            }
        } );
        layout->addWidget( box );
        node.widget = box;
    }
    else if ( kind == "choice" ) {
        auto* combo = new QComboBox( row );
        const bool editable = element.value( "editable" ).toBool();
        if ( editable ) {
            pluginchoices::makeEditable( combo ); // typing filters the list too
        }
        else if ( element.value( "searchable" ).toBool() ) {
            pluginchoices::makeSearchable( combo );
        }
        if ( !editable ) {
            connect( combo, qOverload<int>( &QComboBox::currentIndexChanged ), this, changedNow );
        }
        else {
            // The value is the text: typed or picked, it is reported like text, debounced, once.
            debounced( combo );
            connect( combo, &QComboBox::editTextChanged, this, changedSoon );
            if ( element.value( "completion" ).toBool() ) {
                combo->setCompleter( nullptr ); // the plugin's answers replace the list filter
                attachCompletion( node, combo->lineEdit() );
            }
        }
        layout->addWidget( combo, 1 );
        node.widget = combo;
    }
    else if ( kind == "multiline" ) {
        auto* edit = new QPlainTextEdit( row );
        edit->setTabChangesFocus( true );
        debounced( edit );
        // The same limit as a line edit, in the editor itself, so inputs and events never lose a
        // tail silently. What goes over the limit is cut from the text just inserted (a typed
        // character, the end of a paste), never from the text that was already there, wherever
        // the insertion happened. The change is recorded by contentsChange and trimmed in
        // textChanged, which follows it, outside the document's own edit.
        auto inserted = std::make_shared<std::pair<int, int>>( 0, 0 ); // position, characters
        connect( edit->document(), &QTextDocument::contentsChange, edit,
                 [ inserted ]( int position, int, int added ) { *inserted = { position, added }; } );
        connect( edit, &QPlainTextEdit::textChanged, edit, [ edit, inserted ] {
            auto* document = edit->document();
            const auto length = document->characterCount() - 1;
            const auto excess = length - MaxFieldValueChars;
            if ( excess <= 0 ) {
                return;
            }
            const auto end = qMin( inserted->first + inserted->second, length );
            const auto start = qMax( inserted->first, end - excess );
            QTextCursor cursor( document );
            if ( start < end ) {
                cursor.setPosition( start );
                cursor.setPosition( end, QTextCursor::KeepAnchor );
                cursor.removeSelectedText();
            }
            if ( document->characterCount() - 1 > MaxFieldValueChars ) { // not from one insertion
                cursor.setPosition( MaxFieldValueChars );
                cursor.movePosition( QTextCursor::End, QTextCursor::KeepAnchor );
                cursor.removeSelectedText();
            }
        } );
        connect( edit, &QPlainTextEdit::textChanged, this, changedSoon );
        layout->addWidget( edit, 1 );
        node.widget = edit;
    }
    else if ( kind == "integer" ) {
        auto* spin = new QSpinBox( row );
        spin->setRange( -std::numeric_limits<int>::max(), std::numeric_limits<int>::max() );
        debounced( spin );
        connect( spin, qOverload<int>( &QSpinBox::valueChanged ), this, changedSoon );
        layout->addWidget( spin, 1 );
        node.widget = spin;
    }
    else if ( kind == "slider" ) {
        auto* slider = new QSlider( Qt::Horizontal, row );
        slider->setRange( DefaultSliderMinimum, DefaultSliderMaximum );
        auto* shown = new QLabel( row );
        shown->setObjectName( nameFor( id ) + "_value" );
        shown->setMinimumWidth( shown->fontMetrics().horizontalAdvance( QStringLiteral( "-0000000" ) ) );
        connect( slider, &QSlider::valueChanged, shown,
                 [ shown ]( int value ) { shown->setNum( value ); } );
        shown->setNum( slider->value() );
        debounced( slider );
        connect( slider, &QSlider::valueChanged, this, changedSoon );
        layout->addWidget( slider, 1 );
        layout->addWidget( shown );
        node.widget = slider;
    }
    else if ( kind == "date" || kind == "time" || kind == "datetime" ) {
        QDateTimeEdit* edit = kind == "date"   ? new QDateEdit( QDate::currentDate(), row )
                              : kind == "time" ? static_cast<QDateTimeEdit*>(
                                    new QTimeEdit( QTime( 0, 0 ), row ) )
                                               : new QDateTimeEdit( QDateTime::currentDateTime(), row );
        edit->setDisplayFormat( kind == "date"   ? DateFormat
                                : kind == "time" ? TimeFormat
                                                 : DateTimeFormat );
        edit->setCalendarPopup( kind != "time" );
        debounced( edit );
        connect( edit, &QDateTimeEdit::dateTimeChanged, this, changedSoon );
        layout->addWidget( edit, 1 );
        node.widget = edit;
    }
    else {
        auto* edit = new QLineEdit( row );
        edit->setMaxLength( MaxFieldValueChars );
        if ( kind == "number" ) {
            auto* validator = new QDoubleValidator( edit );
            validator->setLocale( QLocale::c() );
            validator->setNotation( QDoubleValidator::ScientificNotation );
            edit->setValidator( validator );
        }
        else if ( kind == "secret" ) {
            edit->setEchoMode( QLineEdit::Password );
        }
        else if ( kind == "color" ) {
            // Blank or exactly #rrggbb; a shorter "#abc" is only an intermediate editing state,
            // which refreshValidity() reports and which keeps actions from starting.
            edit->setValidator( new QRegularExpressionValidator(
                QRegularExpression( QStringLiteral( "(#[0-9a-fA-F]{6})?" ) ), edit ) );
        }
        debounced( edit );
        connect( edit, &QLineEdit::textChanged, this, changedSoon );
        layout->addWidget( edit, 1 );
        node.widget = edit;
        if ( kind == "text" && element.value( "completion" ).toBool() ) {
            attachCompletion( node, edit );
        }
        if ( kind == "color" ) {
            auto* pick = new QPushButton( tr( "Choose…" ), row );
            pick->setObjectName( nameFor( id ) + "_pick" );
            layout->addWidget( pick );
            connect( pick, &QPushButton::clicked, this, [ this, id ] { pickColor( id ); } );
        }
        if ( kind == "file_open" || kind == "file_save" || kind == "directory" ) {
            auto* button = new QPushButton( tr( "Browse…" ), row );
            button->setObjectName( nameFor( id ) + "_browse" );
            layout->addWidget( button );
            connect( button, &QPushButton::clicked, this,
                     [ this, id, kind ] { browse( id, kind ); } );
        }
    }
    return row;
}

QWidget* PluginView::buildToggle( Node& node, const QJsonObject& element, QWidget* parent )
{
    Q_UNUSED( element );
    const auto id = node.id;
    const auto changedNow = [ this, id ] {
        const auto found = nodes_.value( id );
        if ( !applying_ && found ) {
            emitEvent( id, "changed", fieldValue( *found ) );
        }
    };
    if ( node.type == "checkbox" ) {
        auto* box = new QCheckBox( parent );
#if QT_VERSION >= QT_VERSION_CHECK( 6, 7, 0 )
        connect( box, &QCheckBox::checkStateChanged, this, changedNow );
#else
        connect( box, &QCheckBox::stateChanged, this, changedNow );
#endif
        node.widget = box;
        return box;
    }
    auto* group = new ChoiceGroup( node.type == "radio_group", nameFor( id ), parent );
    connect( group, &ChoiceGroup::changed, this, changedNow );
    node.widget = group;
    return group;
}

void PluginView::attachCompletion( Node& node, QLineEdit* edit )
{
    if ( !queries_.complete ) {
        return; // no host to ask
    }
    const auto id = node.id;
    const QPointer<PluginView> guard( this );
    new FieldCompleter(
        edit,
        [ guard, id ]( const QString& typed, int cursor,
                       std::function<void( const QJsonArray& )> done ) {
            if ( !guard || !guard->isEnabled() || !guard->queries_.complete ) {
                done( {} );
                return;
            }
            // `completion_timeout_ms` (design 5.1); the manager bounds it.
            const auto field = guard->nodes_.value( id );
            const auto timeoutMs
                = field ? field->props.value( "completion_timeout_ms" ).toInt() : 0;
            // The protocol counts characters (Unicode code points), as Python's len(text) does;
            // the line edit counts UTF-16 units, so an emoji before the cursor counts once.
            const auto text = typed.left( MaxFieldValueChars );
            const auto at = pluginfields::characterCount(
                QStringView( text ).left( qBound( 0, cursor, static_cast<int>( text.size() ) ) ) );
            const auto sent = guard->queries_.complete(
                { { "element_id", id },
                  { "text", text },
                  { "cursor", static_cast<double>( at ) } },
                timeoutMs, done );
            if ( sent.isEmpty() ) {
                done( {} ); // not sent: the plugin is stopped, busy or does not complete
            }
        },
        [ guard ]( const QJsonValue& reference ) {
            return guard ? guard->iconFor( reference ) : QIcon{};
        } );
}

QIcon PluginView::iconFor( const QJsonValue& reference ) const
{
    return queries_.icon ? queries_.icon( reference ) : QIcon{};
}

void PluginView::pickColor( const QString& id )
{
    const auto node = nodes_.value( id );
    auto* edit = node ? qobject_cast<QLineEdit*>( node->widget.data() ) : nullptr;
    if ( !edit || !dialogs_.color ) {
        return;
    }
    const QPointer<PluginView> guard( this );
    const std::weak_ptr<Node> weak = node;
    const auto generation = generation_;
    const auto title = text( node->props, "label" ).isEmpty() ? id : text( node->props, "label" );
    const auto dialogName = nameFor( id ) + "_dialog";
    activeDialogs_.insert( dialogName );
    const auto color = dialogs_.color( dialogParent(), dialogName, title, edit->text() );
    if ( guard ) {
        guard->activeDialogs_.remove( dialogName );
    }
    // The dialog's event loop may have rebuilt or deleted the view: re-check everything.
    if ( color.isEmpty() || !stillCurrent( guard, weak, generation ) ) {
        return;
    }
    const auto current = weak.lock();
    auto* currentEdit = qobject_cast<QLineEdit*>( current->widget.data() );
    if ( !currentEdit ) {
        return;
    }
    {
        QScopedValueRollback<bool> rollback( applying_, true );
        currentEdit->setText( color );
    }
    if ( current->debounce ) {
        current->debounce->stop();
    }
    emitEvent( id, "changed", color );
}

bool PluginView::stillCurrent( const QPointer<PluginView>& guard, const std::weak_ptr<Node>& node,
                               quint64 generation )
{
    // After a modal dialog: the view, the element and the tree must all still be the same, and
    // the view must still be live (it is disabled while the plugin is stopped).
    const auto locked = node.lock();
    return guard && locked && guard->generation_ == generation
           && guard->nodes_.value( locked->id ) == locked && guard->isEnabled() && locked->outer
           && locked->outer->isEnabled();
}

void PluginView::browse( const QString& id, const QString& kind )
{
    const auto node = nodes_.value( id );
    auto* edit = node ? qobject_cast<QLineEdit*>( node->widget.data() ) : nullptr;
    if ( !edit ) {
        return;
    }
    const QPointer<PluginView> guard( this );
    const std::weak_ptr<Node> weak = node;
    const auto generation = generation_;
    const auto title = text( node->props, "label" ).isEmpty() ? id : text( node->props, "label" );
    const auto filter = text( node->props, "filter" );
    const auto dialogName = nameFor( id ) + "_dialog";
    QString path;
    activeDialogs_.insert( dialogName );
    if ( kind == "directory" && dialogs_.directory ) {
        path = dialogs_.directory( dialogParent(), dialogName, title );
    }
    else if ( kind == "file_save" && dialogs_.saveFile ) {
        path = dialogs_.saveFile( dialogParent(), dialogName, title, filter, edit->text() );
    }
    else if ( kind == "file_open" && dialogs_.openFile ) {
        path = dialogs_.openFile( dialogParent(), dialogName, title, filter );
    }
    if ( guard ) {
        guard->activeDialogs_.remove( dialogName );
    }
    // The dialog's event loop may have rebuilt or deleted the view: re-check everything.
    if ( path.isEmpty() || !stillCurrent( guard, weak, generation ) ) {
        return;
    }
    const auto current = weak.lock();
    auto* currentEdit = qobject_cast<QLineEdit*>( current->widget.data() );
    if ( !currentEdit ) {
        return;
    }
    path = path.left( MaxFieldValueChars );
    {
        QScopedValueRollback<bool> rollback( applying_, true );
        currentEdit->setText( path );
    }
    if ( current->debounce ) {
        current->debounce->stop();
    }
    emitEvent( id, "changed", path );
}

QWidget* PluginView::buildTable( Node& node, const QJsonObject& element, QWidget* parent )
{
    Q_UNUSED( element );
    const auto id = node.id;
    auto* box = new QWidget( parent );
    auto* layout = new QVBoxLayout( box );
    layout->setContentsMargins( 0, 0, 0, 0 );
    auto* filter = new QLineEdit( box );
    filter->setObjectName( nameFor( id ) + "_filter" );
    filter->setPlaceholderText( tr( "Filter" ) );
    filter->setClearButtonEnabled( true );
    filter->setMaxLength( MaxTextChars );
    filter->setVisible( false );
    layout->addWidget( filter );
    auto* table = new QTableView( box );
    node.model = new PluginTableModel( table );
    auto* proxy = new PluginTableProxy( table ); // E17d: `sort_as` and per-column `sortable`
    node.proxy = proxy;
    node.proxy->setSourceModel( node.model );
    node.proxy->setFilterKeyColumn( -1 );
    node.proxy->setFilterCaseSensitivity( Qt::CaseInsensitive );
    node.proxy->setSortRole( Qt::UserRole );
    table->setModel( node.proxy );
    // Only cells whose flags() grant editing open an editor (an `editable` column and a row
    // editor), so tables without editable columns stay read-only.
    table->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed );
    auto* delegate = new PluginCellDelegate( table );
    const auto icons = [ guard = QPointer<PluginView>( this ) ]( const QJsonValue& reference ) {
        return guard ? guard->iconFor( reference ) : QIcon{};
    };
    delegate->iconFor = icons; // E17c: choice icons in cell editors and committed cells
    node.model->iconFor = icons;
    delegate->onUnknownKind = [ guard = QPointer<PluginView>( this ), id ]( const QString& kind ) {
        if ( guard ) {
            guard->warnOnce( "editor-kind:" + kind.left( 64 ),
                             "unknown cell editor kind treated as text in table " + id + ": "
                                 + kind.left( 64 ) );
        }
    };
    table->setItemDelegate( delegate );
    auto* model = node.model;
    model->canEdit = [ this, id, model ] {
        // This tree's table in a live view, outside a plugin update: a focus change caused by a
        // patch, a rebuild or a stopped plugin must not commit an edit nobody would report.
        const auto found = nodes_.value( id );
        return found && found->model == model && !applying_ && isEnabled();
    };
    model->onEdited = [ this, id ]( const QString& rowId, const QString& columnId,
                                    const QJsonValue& value, const QString& previous ) {
        if ( const auto found = nodes_.value( id ) ) {
            found->rowsDirty = true; // the rows no longer match the ones last set as a whole
        }
        return emitEvent( id, "edited", value,
                          { { "row_id", rowId }, { "column_id", columnId }, { "previous", previous } } );
    };
    table->setSelectionBehavior( QAbstractItemView::SelectRows );
    table->setSelectionMode( QAbstractItemView::NoSelection );
    table->setAlternatingRowColors( true );
    table->verticalHeader()->setVisible( false );
    table->verticalHeader()->setDefaultSectionSize( table->fontMetrics().height() + 6 );
    table->horizontalHeader()->setStretchLastSection( true );
    table->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    box->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    layout->addWidget( table, 1 );
    connect( filter, &QLineEdit::textChanged, node.proxy,
             &QSortFilterProxyModel::setFilterFixedString );
    connect( table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [ this, id ] {
        // Not while a press that may start a drag holds the selection (the behaviour reports it).
        const auto found = nodes_.value( id );
        if ( !applying_ && !( found && found->behaviour && found->behaviour->holdsSelection() ) ) {
            emitSelection( id );
        }
    } );
    node.widget = table;
    // E17d: toggle selection, check columns, activation, reordering, context menus, Copy and
    // Export, the selection patch and kept columns (section 10).
    PluginTableBehaviour::Hooks hooks;
    const QPointer<PluginView> guard( this );
    hooks.emitEvent = [ guard, id ]( const QString& type, const QJsonValue& value ) {
        return guard && guard->emitEvent( id, type, value );
    };
    hooks.canAct = [ guard, id, model ] {
        if ( !guard ) {
            return false;
        }
        const auto found = guard->nodes_.value( id );
        return found && found->model == model && !guard->applying_ && guard->isEnabled();
    };
    hooks.extendedEvents = [ guard ] { return guard && guard->extendedEvents_; };
    hooks.elementMenu = [ guard, id ]( QMenu* menu, const QString& menuId,
                                       const QJsonObject& target ) {
        if ( guard && guard->queries_.elementMenu ) {
            guard->queries_.elementMenu( menu, id, menuId, target );
        }
    };
    hooks.saveFile = [ guard, id ]( const QString& title, const QString& nameFilter,
                                    const QString& suggested ) {
        return exportPath( guard, id, title, nameFilter, suggested );
    };
    hooks.loadState = [ guard, id ] {
        return guard && guard->queries_.loadState ? guard->queries_.loadState( id ) : QJsonObject{};
    };
    hooks.saveState = [ guard, id ]( const QJsonObject& state ) {
        if ( guard && guard->queries_.saveState ) {
            guard->queries_.saveState( id, state );
        }
    };
    hooks.selectionSettled = [ guard, id, table ] {
        // Only for the table the view still shows for this id: a report a replaced table
        // deferred must not speak for its successor.
        const auto found = guard ? guard->nodes_.value( id ) : nullptr;
        if ( found && found->widget.data() == table && !guard->applying_ ) {
            guard->emitSelection( id );
        }
    };
    table->setObjectName( nameFor( id ) );
    node.behaviour = new PluginTableBehaviour( table, node.model, proxy, std::move( hooks ) );
    return box;
}

void PluginView::emitSelection( const QString& id )
{
    const auto node = nodes_.value( id );
    auto* table = node ? qobject_cast<QTableView*>( node->widget.data() ) : nullptr;
    if ( !table ) {
        return;
    }
    // Bounded: at most MaxSelectedIds ids, and MaxIdBytes of them measured as encoded JSON (a
    // control character takes six bytes there); the count is complete.
    const auto rows = table->selectionModel()->selectedRows();
    QStringList leading;
    for ( const auto& index : rows ) {
        if ( leading.size() >= MaxSelectedIds ) {
            break;
        }
        leading.append( index.data( Qt::UserRole + 1 ).toString() );
    }
    const auto ids = QJsonArray::fromStringList( pluginview::boundedIds( leading ) );
    QJsonObject extra{ { "selected_count", static_cast<int>( rows.size() ) } };
    if ( ids.size() < rows.size() ) {
        extra.insert( "truncated", true );
    }
    // E17d: the row with the keyboard focus, for plugins that declare `handles` (principle 11).
    if ( extendedEvents_ && node->behaviour ) {
        const auto current = node->behaviour->currentRowId();
        extra.insert( "current", current.isEmpty() ? QJsonValue() : QJsonValue( current ) );
    }
    if ( node->behaviour ) {
        node->behaviour->selectionReported(); // a report waiting for a patch to end would repeat it
    }
    emitEvent( id, "selected", ids, extra );
}

void PluginView::apply( Node& node, const QJsonObject& incoming, bool fromRoot )
{
    const auto known = knownKeys( node.type );
    const auto previous = node.props;
    auto props = incoming;
    if ( fromRoot ) {
        // A same-shaped root describes the whole element: properties it no longer sends return
        // to their defaults (or are dropped), except value and a table's rows and columns, which
        // keep state.
        const auto defaults = defaultsFor( node.type );
        for ( auto it = previous.begin(); it != previous.end(); ++it ) {
            if ( props.contains( it.key() ) || KeptWhenAbsent.contains( it.key() )
                 || ( node.type == "table" && TableKeptWhenAbsent.contains( it.key() ) )
                 || ( node.type == "tree" && TreeKeptWhenAbsent.contains( it.key() ) )
                 || ( node.type == "file_tree" && FileTreeKeptWhenAbsent.contains( it.key() ) ) ) {
                continue;
            }
            if ( defaults.contains( it.key() ) ) {
                props.insert( it.key(), defaults.value( it.key() ) );
            }
            else {
                node.props.remove( it.key() );
            }
        }
    }
    // A same-shaped root re-sends unchanged values; keep the widgets' state for those.
    const auto unchanged = [ & ]( const char* key ) {
        return fromRoot
               && previous.value( QLatin1String( key ) ) == props.value( QLatin1String( key ) );
    };
    for ( auto it = props.begin(); it != props.end(); ++it ) {
        if ( !CommonKeys.contains( it.key() ) && !known.contains( it.key() ) ) {
            warnOnce( "prop:" + node.type + ':' + it.key().left( 64 ),
                      "unknown " + node.type + " property ignored: " + it.key().left( 64 ) );
        }
        else if ( it.key() != "id" && it.key() != "type" && it.key() != "children" ) {
            node.props.insert( it.key(), it.value() );
        }
    }
    if ( props.contains( "enabled" ) ) {
        node.outer->setEnabled( props.value( "enabled" ).toBool( true ) );
    }
    if ( props.contains( "visible" ) ) {
        node.outer->setVisible( props.value( "visible" ).toBool( true ) );
    }
    if ( props.contains( "tooltip" ) ) {
        // Plain text, like row tooltips: text that looks like HTML is shown as written.
        node.widget->setToolTip( plainToolTip( text( props, "tooltip" ) ) );
    }
    const auto& type = node.type;
    if ( type == "group" ) {
        auto* group = qobject_cast<QGroupBox*>( node.widget.data() );
        if ( props.contains( "title" ) ) {
            group->setTitle( text( props, "title" ) );
        }
        if ( props.contains( "collapsible" ) ) {
            group->setCheckable( props.value( "collapsible" ).toBool() );
            node.content->setVisible( !group->isCheckable() || group->isChecked() );
        }
        if ( props.contains( "collapsed" ) && group->isCheckable() ) {
            group->setChecked( !props.value( "collapsed" ).toBool() );
            node.content->setVisible( group->isChecked() );
        }
    }
    else if ( type == "label" || type == "badge" ) {
        auto* label = qobject_cast<QLabel*>( node.widget.data() );
        if ( props.contains( "text" ) ) {
            label->setText( text( props, "text" ) );
        }
        if ( props.contains( "tone" ) ) {
            setTone( label, props.value( "tone" ).toString( "normal" ).left( 16 ),
                     type == "badge" );
        }
        if ( props.contains( "bold" ) ) {
            auto font = label->font();
            font.setBold( props.value( "bold" ).toBool() );
            label->setFont( font );
        }
    }
    else if ( type == "field" ) {
        auto* caption = qobject_cast<QLabel*>( node.content.data() );
        if ( props.contains( "label" ) ) {
            const auto label = text( props, "label" );
            if ( auto* box = qobject_cast<QCheckBox*>( node.widget.data() ) ) {
                box->setText( label.isEmpty() ? node.id : label );
            }
            else {
                caption->setText( label );
                caption->setVisible( !label.isEmpty() );
                caption->setBuddy( node.widget );
            }
        }
        if ( auto* box = qobject_cast<QCheckBox*>( node.widget.data() );
             box && box->text().isEmpty() ) {
            box->setText( node.id );
        }
        if ( props.contains( "placeholder" ) ) {
            if ( auto* edit = qobject_cast<QLineEdit*>( node.widget.data() ) ) {
                edit->setPlaceholderText( text( props, "placeholder" ) );
            }
            else if ( auto* plain = qobject_cast<QPlainTextEdit*>( node.widget.data() ) ) {
                plain->setPlaceholderText( text( props, "placeholder" ) );
            }
            else if ( auto* editable = qobject_cast<QComboBox*>( node.widget.data() );
                      editable && editable->lineEdit() ) {
                editable->lineEdit()->setPlaceholderText( text( props, "placeholder" ) );
            }
        }
        const auto icons = [ this ]( const QJsonValue& reference ) { return iconFor( reference ); };
        if ( props.contains( "choices" ) && !unchanged( "choices" ) ) {
            // E17c: choice objects with icons, tooltips, disabled entries, separators and sections.
            const auto choices = pluginchoices::parse( props.value( "choices" ).toArray(), MaxElements );
            if ( auto* group = qobject_cast<ChoiceGroup*>( node.widget.data() ) ) {
                group->setChoices( choices, icons );
            }
            else if ( auto* checks = qobject_cast<CheckComboBox*>( node.widget.data() ) ) {
                checks->setChoices( props.value( "choices" ).toArray(), icons );
            }
            else if ( auto* combo = qobject_cast<QComboBox*>( node.widget.data() ) ) {
                pluginchoices::fillCombo( combo, choices, icons,
                                          node.props.value( "indicator" ) == "radio" );
            }
        }
        // Set now, or dropped by a same-shaped root (which returns it to the default).
        const auto touched = [ & ]( const char* key ) {
            const QLatin1String name( key );
            return props.contains( name ) || ( previous.contains( name ) && !node.props.contains( name ) );
        };
        if ( auto* group = qobject_cast<ChoiceGroup*>( node.widget.data() );
             group && ( touched( "orientation" ) || touched( "columns" ) ) ) {
            group->setShape( node.props.value( "orientation" ).toString(),
                             node.props.value( "columns" ).toInt() );
        }
        if ( auto* plain = qobject_cast<QPlainTextEdit*>( node.widget.data() ) ) {
            if ( touched( "monospace" ) ) {
                plain->setFont( node.props.value( "monospace" ).toBool()
                                    ? QFontDatabase::systemFont( QFontDatabase::FixedFont )
                                    : QFont() );
            }
            if ( touched( "rows" ) || touched( "monospace" ) ) {
                const auto rows = qBound( 1, node.props.value( "rows" ).toInt( 4 ), 50 );
                plain->setFixedHeight( plain->fontMetrics().lineSpacing() * rows
                                       + 2 * plain->frameWidth()
                                       + static_cast<int>( plain->document()->documentMargin() * 2 ) );
            }
        }
        // integer and slider bounds; validation reports a typed value out of them (section 8.5).
        // A bound that is not given is the kind's own: any int for integer, 0 and 100 for slider.
        const auto setBounds = [ & ]( auto* range, int lowest, int highest ) {
            if ( touched( "min" ) || touched( "max" ) ) {
                const auto limit = static_cast<double>( std::numeric_limits<int>::max() );
                const auto bound = [ & ]( const char* key, int fallback ) {
                    const auto given = node.props.value( QLatin1String( key ) );
                    return given.isDouble()
                               ? static_cast<int>( qBound( -limit, given.toDouble(), limit ) )
                               : fallback;
                };
                const auto low = bound( "min", lowest );
                range->setRange( low, qMax( low, bound( "max", highest ) ) );
            }
            if ( touched( "step" ) ) {
                range->setSingleStep( qMax( 1, node.props.value( "step" ).toInt( 1 ) ) );
            }
        };
        if ( auto* spin = qobject_cast<QSpinBox*>( node.widget.data() ) ) {
            setBounds( spin, -std::numeric_limits<int>::max(), std::numeric_limits<int>::max() );
        }
        else if ( auto* slider = qobject_cast<QSlider*>( node.widget.data() ) ) {
            setBounds( slider, DefaultSliderMinimum, DefaultSliderMaximum );
            slider->setPageStep( slider->singleStep() );
        }
    }
    else if ( type == "checkbox" ) {
        auto* box = qobject_cast<QCheckBox*>( node.widget.data() );
        if ( props.contains( "text" ) ) {
            box->setText( text( props, "text" ).replace( QLatin1Char( '&' ), QStringLiteral( "&&" ) ) );
        }
        if ( props.contains( "tristate" ) ) {
            box->setTristate( props.value( "tristate" ).toBool() );
        }
        if ( props.contains( "tone" ) ) {
            setTone( box, props.value( "tone" ).toString( "normal" ).left( 16 ), false );
        }
    }
    else if ( type == "radio_group" || type == "check_group" ) {
        auto* group = qobject_cast<ChoiceGroup*>( node.widget.data() );
        if ( props.contains( "title" ) ) {
            group->setTitle( text( props, "title" ) );
        }
        if ( props.contains( "choices" ) && !unchanged( "choices" ) ) {
            group->setChoices( pluginchoices::parse( props.value( "choices" ).toArray(), MaxElements ),
                               [ this ]( const QJsonValue& reference ) { return iconFor( reference ); } );
        }
        if ( props.contains( "orientation" ) || props.contains( "columns" ) ) {
            group->setShape( node.props.value( "orientation" ).toString(),
                             node.props.value( "columns" ).toInt() );
        }
        if ( props.contains( "all_none" ) ) {
            group->setAllNone( props.value( "all_none" ).toBool() );
        }
    }
    // Values after the choices they name (fields and E17c toggles).
    if ( ( type == "field" || type == "checkbox" || type == "radio_group" || type == "check_group" )
         && props.contains( "value" )
         && ( !fromRoot || props.value( "value" ) != node.pluginValue ) ) {
        node.pluginValue = props.value( "value" );
        setFieldValue( node, node.pluginValue );
    }
    if ( type == "button" ) {
        auto* button = qobject_cast<QPushButton*>( node.widget.data() );
        if ( props.contains( "label" ) ) {
            button->setText( text( props, "label" ) );
        }
        if ( props.contains( "tone" ) ) {
            const auto tone = props.value( "tone" ).toString( "normal" ).left( 16 );
            button->setProperty( "tone", tone );
            auto font = button->font();
            font.setBold( tone == "primary" );
            button->setFont( font );
            button->setStyleSheet( tone == "danger" ? QStringLiteral( "color: #c62828;" )
                                                    : QString{} );
        }
    }
    else if ( type == "progress" ) {
        auto* bar = qobject_cast<QProgressBar*>( node.widget.data() );
        if ( props.contains( "busy" ) || props.contains( "value" ) ) {
            const bool busy = node.props.value( "busy" ).toBool();
            bar->setRange( 0, busy ? 0 : 1000 );
            if ( !busy ) {
                bar->setValue( static_cast<int>( std::lround(
                    qBound( 0.0, node.props.value( "value" ).toDouble(), 1.0 ) * 1000 ) ) );
            }
        }
        if ( props.contains( "text" ) ) {
            const auto label = text( props, "text" );
            bar->setFormat( label.isEmpty() ? QStringLiteral( "%p%" ) : label );
            bar->setTextVisible( true );
        }
    }
    else if ( type == "table" ) {
        auto* table = qobject_cast<QTableView*>( node.widget.data() );
        bool columnsSet = false;
        if ( props.contains( "columns" ) && !unchanged( "columns" ) ) {
            const auto columns = props.value( "columns" ).toArray();
            if ( node.behaviour ) {
                node.behaviour->beginColumnsChange(); // before the model resets
            }
            if ( columns.size() > MaxTableColumns || !node.model->setColumns( columns ) ) {
                warnOnce( "columns:" + node.id, "table columns rejected for " + node.id );
                if ( node.behaviour ) {
                    node.behaviour->abortColumnsChange();
                }
            }
            else {
                columnsSet = true;
                for ( int column = 0; column < columns.size(); ++column ) {
                    const auto width = columns.at( column ).toObject().value( "width" ).toInt();
                    if ( width > 0 ) {
                        table->horizontalHeader()->resizeSection( column, qMin( width, 2000 ) );
                    }
                }
            }
        }
        // Skip unchanged rows of a same-shaped root, so the selection and scroll stay put.
        if ( props.contains( "rows" )
             && !( fromRoot && !node.rowsDirty && props.value( "rows" ) == node.lastRows ) ) {
            applyTableOperation( node, { { "replace_rows", props.value( "rows" ) } } );
        }
        if ( props.contains( "sortable" ) ) {
            const bool sortable = props.value( "sortable" ).toBool();
            if ( sortable && !table->isSortingEnabled() ) {
                // Keep the plugin's row order until the user clicks a column header.
                table->horizontalHeader()->setSortIndicator( -1, Qt::AscendingOrder );
                table->setSortingEnabled( true );
            }
            else if ( !sortable && table->isSortingEnabled() ) {
                table->setSortingEnabled( false );
                node.proxy->sort( -1 );
            }
        }
        if ( props.contains( "filter_box" ) ) {
            auto* filter = node.outer->findChild<QLineEdit*>( nameFor( node.id ) + "_filter" );
            filter->setVisible( props.value( "filter_box" ).toBool() );
        }
        if ( props.contains( "selection" ) || props.contains( "toggle_selection" ) ) {
            const auto mode = node.props.value( "selection" ).toString();
            // E17d `toggle_selection`: every click adds or removes a row (section 10.2).
            const bool toggle = node.props.value( "toggle_selection" ).toBool();
            table->setSelectionMode( mode == "single" ? QAbstractItemView::SingleSelection
                                     : mode == "multi"
                                         ? ( toggle ? QAbstractItemView::MultiSelection
                                                    : QAbstractItemView::ExtendedSelection )
                                         : QAbstractItemView::NoSelection );
        }
        // E17d table options (section 10.1); hidden columns, minimum widths and the kept
        // columns follow the columns, after `sortable` so a kept sort applies.
        if ( node.behaviour ) {
            PluginTableBehaviour::Options options;
            options.header = node.props.value( "header" ).toBool( true );
            options.toggleSelection = node.props.value( "toggle_selection" ).toBool();
            options.contextMenu = text( node.props, "context_menu", 128 );
            options.copy = node.props.value( "copy" ).toBool( true );
            options.exportCsv = node.props.value( "export" ).toBool();
            options.reorderable = node.props.value( "reorderable" ).toBool();
            options.keepColumns = node.props.value( "keep_columns" ).toBool( true );
            node.behaviour->setOptions( options );
            if ( columnsSet ) {
                node.behaviour->columnsChanged();
            }
        }
    }
    else if ( type == "tree" ) {
        applyTree( node, props, previous, fromRoot ); // E17e
    }
    else if ( type == "file_tree" ) {
        applyFileTree( node, props, previous, fromRoot );
    }
    updateAccessibility( node );
}

// docs/plugin-sdk-advanced.md section 12.4: the caption or text names an element (falling back to
// its id), `accessible_name` and `accessible_description` override, and a static tooltip is the
// description when none is given.
void PluginView::updateAccessibility( Node& node )
{
    if ( node.automatic || !node.widget ) {
        return;
    }
    QString caption;
    if ( node.type == "field" ) {
        caption = text( node.props, "label" );
    }
    else if ( node.type == "checkbox" ) {
        caption = text( node.props, "text" );
    }
    else if ( node.type == "radio_group" || node.type == "check_group" ) {
        caption = text( node.props, "title" );
    }
    else if ( node.type == "button" ) {
        caption = text( node.props, "label" );
    }
    else if ( node.type == "group" ) {
        caption = text( node.props, "title" );
    }
    else if ( node.type == "label" || node.type == "badge" || node.type == "progress" ) {
        caption = text( node.props, "text" );
    }
    const auto name = text( node.props, "accessible_name" );
    node.widget->setAccessibleName( !name.isEmpty()      ? name
                                     : !caption.isEmpty() ? caption.left( 256 )
                                                          : node.id );
    // The validation message describes an invalid field (section 8.6).
    const auto description = text( node.props, "accessible_description" );
    node.widget->setAccessibleDescription( !description.isEmpty() ? description
                                           : !node.invalid.isEmpty() ? node.invalid
                                                                     : text( node.props, "tooltip" ) );
}

void PluginView::applyTableOperation( Node& node, const QJsonObject& entry )
{
    auto* table = qobject_cast<QTableView*>( node.widget.data() );
    if ( entry.contains( "replace_rows" )
         && node.model->fits( entry.value( "replace_rows" ).toArray().size() ) ) {
        // Close an open cell editor first, without committing it. Cell editors are direct children
        // of the viewport; closing through the delegate's closeEditor signal (connected to the
        // view) reverts instead of committing, and also ends the view's editing state.
        // A replacement over the cell limit changes nothing (replace() rejects it below), so it
        // leaves the editor and the user's input alone.
        // remove_rows needs none of this: the view itself releases the editors of removed rows
        // (rowsAboutToBeRemoved, or a reset for large batches) without committing them, and an
        // editor in another row stays open.
        for ( auto* editor :
              table->viewport()->findChildren<QWidget*>( QString{}, Qt::FindDirectChildrenOnly ) ) {
            if ( editor->property( CellEditorProperty ).toBool() && !editor->isHidden() ) {
                Q_EMIT table->itemDelegate()->closeEditor( editor,
                                                           QAbstractItemDelegate::RevertModelCache );
            }
        }
    }
    // Model resets (replace_rows, large remove_rows) lose the view's selection and scroll
    // position: remember them by row id and restore them afterwards.
    QStringList selected;
    for ( const auto& index : table->selectionModel()->selectedRows() ) {
        selected.append( index.data( Qt::UserRole + 1 ).toString() );
    }
    const auto scroll = table->verticalScrollBar()->value();
    bool reset = false;
    if ( entry.contains( "replace_rows" ) ) {
        if ( node.model->replace( entry.value( "replace_rows" ).toArray() ) ) {
            reset = true;
            node.lastRows = entry.value( "replace_rows" );
            node.rowsDirty = false;
        }
        else {
            warnOnce( "cells:" + node.id,
                      QStringLiteral( "table %1 rows rejected (more than %2 cells)" )
                          .arg( node.id )
                          .arg( MaxTableCells ) );
        }
    }
    if ( entry.contains( "update_rows" ) ) {
        node.model->update( entry.value( "update_rows" ).toArray() );
        node.rowsDirty = true;
    }
    if ( entry.contains( "insert_rows" ) ) {
        node.rowsDirty = true;
        if ( !node.model->insert( entry.value( "insert_rows" ).toArray(),
                                  entry.value( "before" ).toString() ) ) {
            warnOnce( "cells:" + node.id,
                      QStringLiteral( "table %1 insert rejected (more than %2 cells)" )
                          .arg( node.id )
                          .arg( MaxTableCells ) );
        }
    }
    if ( entry.contains( "remove_rows" ) ) {
        node.rowsDirty = true;
        reset = node.model->remove( entry.value( "remove_rows" ).toArray() ) || reset;
    }
    if ( !reset ) {
        return;
    }
    QItemSelection selection;
    int restored = 0;
    for ( const auto& id : selected ) {
        const auto row = node.model->rowOf( id );
        if ( row >= 0 ) {
            const auto index = node.proxy->mapFromSource( node.model->index( row, 0 ) );
            selection.select( index, index );
            ++restored;
        }
    }
    if ( !selection.isEmpty() ) {
        table->selectionModel()->select( selection, QItemSelectionModel::ClearAndSelect
                                                        | QItemSelectionModel::Rows );
    }
    // The scroll range is only updated after layout: restore now and once more when queued.
    table->verticalScrollBar()->setValue( scroll );
    QTimer::singleShot( 0, table, [ view = QPointer<QTableView>( table ), scroll ] {
        if ( view ) {
            view->verticalScrollBar()->setValue( scroll );
        }
    } );
    // The plugin removed selected rows: tell it what is still selected, unless the same entry sets
    // the selection itself (a selection patch is never echoed, nor anything in between).
    if ( restored < selected.size() && !entry.contains( "select" ) ) {
        QScopedValueRollback<bool> rollback( applying_, false );
        emitSelection( node.id );
    }
}

void PluginView::applyPatch( const QJsonArray& patch )
{
    QScopedValueRollback<bool> guard( applying_, true );
    for ( const auto& value : patch ) {
        const auto entry = value.toObject();
        const auto id = entry.value( "id" ).toString();
        const auto node = nodes_.value( id );
        if ( !node || node->automatic ) {
            warnOnce( "missing:" + id.left( 128 ),
                      "patch for unknown element ignored: " + id.left( 128 ) );
            continue;
        }
        // A patch to an element, a row or a cell drops its cached advanced tooltips.
        if ( entry.contains( "update_rows" ) || entry.contains( "remove_rows" ) ) {
            QStringList rowIds;
            for ( const auto& row : entry.value( "update_rows" ).toArray() ) {
                rowIds.append( row.toObject().value( "id" ).toString() );
            }
            for ( const auto& rowId : entry.value( "remove_rows" ).toArray() ) {
                rowIds.append( rowId.toString() );
            }
            tooltips_->invalidateRows( id, rowIds );
        }
        if ( entry.value( "set" ).isObject() || entry.contains( "replace_rows" ) ) {
            tooltips_->invalidateElement( id );
        }
        if ( entry.value( "set" ).isObject() ) {
            auto set = entry.value( "set" ).toObject();
            bool structural = set.contains( "children" );
            set.remove( "children" );
            for ( const auto& key : FieldStructureKeys ) {
                structural = structural || set.contains( key );
                set.remove( key );
            }
            if ( structural ) {
                warnOnce( "structure:" + id,
                          "patches cannot change children, field kinds or field styles" );
            }
            apply( *node, set, false );
        }
        if ( node->type == "tree" || node->type == "file_tree" ) {
            applyTreeOperation( *node, entry ); // E17e (section 28)
        }
        if ( node->type == "table" ) {
            applyTableOperation( *node, entry );
            // E17d selection patch (section 10.2), after any row changes of the same entry; it
            // never echoes a `selected` event (applying_).
            if ( node->behaviour
                 && ( entry.contains( "select" ) || entry.contains( "current" )
                      || entry.contains( "scroll_to" ) ) ) {
                node->behaviour->select( entry.value( "select" ), entry.value( "current" ),
                                         entry.value( "scroll_to" ) );
            }
        }
    }
    refreshValidity();
    ++renders_;
}

bool PluginView::elementMenu( const QString& elementId, const QString& rowId, QString* menuId,
                              QJsonObject* target, QString* error ) const
{
    return elementMenu( elementId, rowId, {}, menuId, target, error );
}

bool PluginView::elementMenu( const QString& elementId, const QString& rowId,
                              const QString& nodeId, QString* menuId, QJsonObject* target,
                              QString* error ) const
{
    const auto node = nodes_.value( elementId );
    // E17e: trees and file trees name the clicked node or path.
    if ( node && !node->automatic && ( node->tree || node->fileTree ) ) {
        if ( !rowId.isEmpty() ) {
            *error = tr( "A tree has nodes, not rows: use --node-id." );
            return false;
        }
        *menuId = node->tree ? node->tree->options().contextMenu
                             : node->fileTree->options().contextMenu;
        if ( menuId->isEmpty() ) {
            *error = tr( "The tree has no context_menu." );
            return false;
        }
        if ( node->tree ) {
            if ( !nodeId.isEmpty() && !node->treeModel->contains( nodeId ) ) {
                *error = tr( "The tree has no node with this id." );
                return false;
            }
            *target = node->tree->menuTarget( nodeId );
            return true;
        }
        // As tooltips: an existing item of the roots, its links resolved.
        const auto path = PluginFileRootsModel::normalize( nodeId );
        if ( !nodeId.isEmpty() && !node->fileTree->model()->reaches( path ) ) {
            *error = tr( "The path is not an item in the file tree's roots." );
            return false;
        }
        *target = node->fileTree->menuTarget( nodeId.isEmpty() ? QString{} : path );
        return true;
    }
    if ( !nodeId.isEmpty() ) {
        *error = tr( "Only trees and file trees take --node-id." );
        return false;
    }
    if ( !node || node->automatic || !node->behaviour ) {
        *error = tr( "The view has no table or tree with this id." );
        return false;
    }
    *menuId = node->behaviour->options().contextMenu;
    if ( menuId->isEmpty() ) {
        *error = tr( "The table has no context_menu." );
        return false;
    }
    if ( !rowId.isEmpty() && node->model->rowOf( rowId ) < 0 ) {
        *error = tr( "The table has no row with this id." );
        return false;
    }
    *target = node->behaviour->menuTarget( rowId, {} );
    return true;
}

QMenu* PluginView::buildElementMenu( const QString& elementId, const QPoint& position )
{
    const auto node = nodes_.value( elementId );
    if ( node && node->tree ) {
        return node->tree->buildMenu( position ); // E17e
    }
    if ( node && node->fileTree ) {
        return node->fileTree->buildMenu( position );
    }
    return node && node->behaviour ? node->behaviour->buildMenu( position ) : nullptr;
}

QJsonValue PluginView::fieldValue( const Node& node ) const
{
    if ( node.fileTree ) {
        // E17e: the selected (or checked) paths (section 28.2).
        return QJsonArray::fromStringList( node.fileTree->value( MaxSelectedIds ) );
    }
    auto* widget = node.widget.data();
    if ( auto* group = qobject_cast<ChoiceGroup*>( widget ) ) {
        return group->value();
    }
    if ( auto* checks = qobject_cast<CheckComboBox*>( widget ) ) {
        return checks->checkedValues();
    }
    if ( auto* box = qobject_cast<QCheckBox*>( widget ) ) {
        if ( box->isTristate() && box->checkState() == Qt::PartiallyChecked ) {
            return QJsonValue::Null;
        }
        return box->isChecked();
    }
    if ( auto* combo = qobject_cast<QComboBox*>( widget ) ) {
        if ( node.props.value( "editable" ).toBool() && combo->isEditable() ) {
            // A typed value, or the choice whose label it is.
            const auto index = pluginchoices::findLabel( combo, combo->currentText() );
            return index >= 0 ? QJsonValue::fromVariant( combo->itemData( index ) )
                              : QJsonValue( combo->currentText().left( MaxFieldValueChars ) );
        }
        return QJsonValue::fromVariant( combo->currentData() );
    }
    if ( auto* plain = qobject_cast<QPlainTextEdit*>( widget ) ) {
        return plain->toPlainText().left( MaxFieldValueChars );
    }
    if ( auto* spin = qobject_cast<QSpinBox*>( widget ) ) {
        return spin->value();
    }
    if ( auto* slider = qobject_cast<QSlider*>( widget ) ) {
        return slider->value();
    }
    if ( auto* dateTime = qobject_cast<QDateTimeEdit*>( widget ) ) {
        if ( node.kind == "date" ) {
            return dateTime->date().toString( DateFormat );
        }
        if ( node.kind == "time" ) {
            return dateTime->time().toString( TimeFormat );
        }
        return dateTime->dateTime().toString( DateTimeFormat );
    }
    auto* edit = qobject_cast<QLineEdit*>( widget );
    if ( !edit ) {
        return {};
    }
    if ( node.kind == "number" ) {
        bool ok = false;
        const auto number = QLocale::c().toDouble( edit->text(), &ok );
        return ok && std::isfinite( number ) ? QJsonValue( number )
                                             : QJsonValue( QJsonValue::Null );
    }
    return edit->text().left( MaxFieldValueChars );
}

void PluginView::setFieldValue( Node& node, const QJsonValue& value )
{
    if ( node.debounce ) {
        node.debounce->stop();
    }
    auto* widget = node.widget.data();
    if ( auto* group = qobject_cast<ChoiceGroup*>( widget ) ) {
        group->setValue( value );
        return;
    }
    if ( auto* checks = qobject_cast<CheckComboBox*>( widget ) ) {
        checks->setCheckedValues( value.toArray() );
        return;
    }
    if ( auto* box = qobject_cast<QCheckBox*>( widget ) ) {
        if ( box->isTristate() && value.isNull() ) {
            box->setCheckState( Qt::PartiallyChecked );
        }
        else {
            box->setChecked( value.toBool() );
        }
    }
    else if ( auto* combo = qobject_cast<QComboBox*>( widget ) ) {
        const auto index = combo->findData( value.toVariant() );
        if ( index >= 0 ) {
            combo->setCurrentIndex( index );
        }
        else if ( node.props.value( "editable" ).toBool() && combo->isEditable() ) {
            combo->setEditText( cellText( value, MaxFieldValueChars ) );
        }
    }
    else if ( auto* plain = qobject_cast<QPlainTextEdit*>( widget ) ) {
        const auto text = cellText( value, MaxFieldValueChars );
        if ( plain->toPlainText() != text ) {
            plain->setPlainText( text );
        }
    }
    else if ( auto* spin = qobject_cast<QSpinBox*>( widget ) ) {
        spin->setValue( static_cast<int>( qBound( double( spin->minimum() ), value.toDouble(),
                                                  double( spin->maximum() ) ) ) );
    }
    else if ( auto* slider = qobject_cast<QSlider*>( widget ) ) {
        slider->setValue( static_cast<int>( qBound( double( slider->minimum() ), value.toDouble(),
                                                    double( slider->maximum() ) ) ) );
    }
    else if ( auto* dateTime = qobject_cast<QDateTimeEdit*>( widget ) ) {
        const auto text = value.toString();
        if ( node.kind == "date" ) {
            const auto date = QDate::fromString( text, DateFormat );
            if ( date.isValid() ) {
                dateTime->setDate( date );
            }
        }
        else if ( node.kind == "time" ) {
            const auto time = QTime::fromString( text, TimeFormat );
            if ( time.isValid() ) {
                dateTime->setTime( time );
            }
        }
        else {
            const auto moment = QDateTime::fromString( text.left( 19 ), DateTimeFormat );
            if ( moment.isValid() ) {
                dateTime->setDateTime( moment );
            }
        }
    }
    else if ( auto* edit = qobject_cast<QLineEdit*>( widget ) ) {
        const auto text = value.isDouble() ? QString::number( value.toDouble(), 'g', 15 )
                                           : cellText( value, MaxFieldValueChars );
        if ( edit->text() != text ) {
            edit->setText( text );
        }
    }
}

bool PluginView::kindValid( const Node& node )
{
    // The form of the field kind itself: a color is blank or #rrggbb (a rule of its own, so its
    // changed events carry `valid`).
    if ( node.kind != "color" ) {
        return true;
    }
    const auto* edit = qobject_cast<const QLineEdit*>( node.widget.data() );
    return !edit || edit->hasAcceptableInput();
}

bool PluginView::fieldValid( const Node& node ) const
{
    // As of the last refreshValidity(), which runs after each root, patch and change: the rules
    // (a pattern among them) are checked once per change, not once per reader.
    return node.invalid.isEmpty();
}

void PluginView::focusFirstInvalid()
{
    for ( const auto& id : order_ ) {
        const auto node = nodes_.value( id );
        if ( node && node->type == "field" && !node->invalid.isEmpty() && node->widget ) {
            node->widget->setFocus( Qt::OtherFocusReason );
            return;
        }
    }
}

bool PluginView::fieldsValid( const QJsonValue& requirement ) const
{
    // `true`: every field of the view; a list: those fields; otherwise nothing is required.
    if ( requirement.isBool() && requirement.toBool() ) {
        return std::all_of( nodes_.begin(), nodes_.end(), [ this ]( const auto& node ) {
            return node->type != "field" || fieldValid( *node );
        } );
    }
    for ( const auto& id : requirement.toArray() ) {
        const auto node = nodes_.value( id.toString() );
        if ( node && node->type == "field" && !fieldValid( *node ) ) {
            return false;
        }
    }
    return true;
}

void PluginView::refreshValidity()
{
    for ( const auto& node : std::as_const( nodes_ ) ) {
        if ( node->type != "field" || !node->error || !node->widget ) {
            continue;
        }
        auto message = pluginfields::validates( node->props )
                           ? pluginfields::validationError( node->props, fieldValue( *node ) )
                           : QString{};
        if ( message.isEmpty() && !kindValid( *node ) ) {
            message = tr( "Not a color (#rrggbb)" );
        }
        if ( message == node->invalid ) {
            continue;
        }
        node->invalid = message;
        node->error->setText( message );
        node->error->setVisible( !message.isEmpty() );
        // A red frame on the field itself, without restyling anything inside it.
        const auto* className = node->widget->metaObject()->className();
        node->widget->setStyleSheet(
            message.isEmpty() ? QString{}
                              : QStringLiteral( "%1 { border: 1px solid #c62828; }" )
                                    .arg( QLatin1String( className ) ) );
        node->widget->setProperty( "pluginInvalid", !message.isEmpty() );
        updateAccessibility( *node );
    }
    // Buttons with `requires_valid` follow their fields; every button is set, so one whose later
    // root dropped `requires_valid` gets its own `enabled` back.
    for ( const auto& node : std::as_const( nodes_ ) ) {
        if ( node->type == "button" && node->outer ) {
            node->outer->setEnabled( node->props.value( "enabled" ).toBool( true )
                                     && fieldsValid( node->props.value( "requires_valid" ) ) );
        }
    }
}

QJsonObject PluginView::inputs( bool* truncated ) const
{
    // Every value is at most MaxFieldValueChars; together they stay below MaxInputsBytes, so
    // user input can never push a host message over the 1 MiB transport limit.
    QJsonObject values;
    qsizetype budget = MaxInputsBytes;
    bool shortened = false;
    static const QStringList InputTypes{ "field", "checkbox", "radio_group", "check_group",
                                         "file_tree" };
    for ( const auto& id : order_ ) {
        const auto node = nodes_.value( id );
        if ( !node || !InputTypes.contains( node->type ) ) {
            continue;
        }
        auto value = fieldValue( *node );
        budget -= id.size() + 8;
        if ( value.isString() ) {
            auto string = value.toString();
            const auto bytes = string.toUtf8().size();
            if ( bytes > budget ) {
                // UTF-8 uses at most 3 bytes per UTF-16 code unit.
                string = string.left( static_cast<int>( qMax<qsizetype>( 0, budget ) / 3 ) );
                shortened = true;
            }
            budget -= string.toUtf8().size();
            value = string;
        }
        else if ( value.isArray() ) {
            // A list (multichoice) is counted by its serialized size; trailing elements that
            // no longer fit are dropped, the value stays an array.
            QJsonArray kept;
            for ( const auto& element : value.toArray() ) {
                const auto bytes = QJsonDocument( QJsonArray{ element } )
                                       .toJson( QJsonDocument::Compact )
                                       .size()
                                   - 1; // the element plus its separator
                if ( bytes > budget ) {
                    shortened = true;
                    break;
                }
                budget -= bytes;
                kept.append( element );
            }
            value = kept;
        }
        values.insert( id, value );
    }
    if ( truncated ) {
        *truncated = shortened;
    }
    return values;
}

bool PluginView::emitEvent( const QString& id, const QString& type, const QJsonValue& value,
                            const QJsonObject& extra )
{
    if ( !isEnabled() ) {
        return false; // a stopped or stale view never talks to the plugin
    }
    bool truncated = false;
    QJsonObject event{ { "element_id", id },
                       { "type", type },
                       { "value", value },
                       { "inputs", inputs( &truncated ) } };
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        event.insert( it.key(), it.value() );
    }
    if ( truncated ) {
        event.insert( "truncated", true );
    }
    if ( type == QLatin1String( "changed" ) ) {
        // E17c: the changed event of a field with validation says whether it is valid
        // (section 8.6), for plugins that declare `handles` (principle 11); other events keep the
        // shape they had before E17.
        refreshValidity();
        if ( const auto node = nodes_.value( id );
             extendedEvents_ && node && node->type == "field"
             && ( pluginfields::validates( node->props ) || node->kind == "color" ) ) {
            event.insert( "valid", fieldValid( *node ) );
        }
    }
    eventDropped_ = false;
    const QPointer<PluginView> guard( this );
    Q_EMIT eventRaised( event );
    return !guard || !std::exchange( eventDropped_, false );
}

void PluginView::pressButton( const QString& id )
{
    const auto node = nodes_.value( id );
    if ( !node || !isEnabled() ) {
        return;
    }
    refreshValidity(); // a typed change may still be waiting for its debounce
    if ( !fieldsValid( node->props.value( "requires_valid" ) ) ) {
        LOG_DEBUG << "Plugin view button ignored: required fields are not valid element="
                  << id.toStdString();
        return;
    }
    // An action never starts with invalid fields (section 8.6): refused before any confirm or
    // save dialog, with the focus on the first invalid field.
    if ( !node->props.value( "event" ).toBool() && !node->props.value( "action" ).toString().isEmpty()
         && !fieldsValid( true ) ) {
        focusFirstInvalid();
        LOG_DEBUG << "Plugin view action not started: invalid fields element=" << id.toStdString();
        return;
    }
    const QPointer<PluginView> guard( this );
    const std::weak_ptr<Node> weak = node;
    const auto generation = generation_;
    const auto props = node->props;
    const auto label = text( props, "label" ).isEmpty() ? id : text( props, "label" );
    if ( props.value( "confirm" ).isObject() ) {
        const auto confirm = props.value( "confirm" ).toObject();
        const auto title = text( confirm, "title" );
        const auto dialogName = nameFor( id ) + "_confirm";
        activeDialogs_.insert( dialogName );
        const bool confirmed
            = dialogs_.confirm
              && dialogs_.confirm( dialogParent(), dialogName, title.isEmpty() ? label : title,
                                   text( confirm, "text" ), text( confirm, "ok_label", 256 ) );
        if ( guard ) {
            guard->activeDialogs_.remove( dialogName );
        }
        if ( !confirmed ) {
            return;
        }
        // The dialog's event loop may have rebuilt, disabled or deleted the view.
        if ( !stillCurrent( guard, weak, generation ) ) {
            LOG_DEBUG << "Plugin view button dropped after confirm: view changed";
            return;
        }
    }
    QString savePath;
    if ( props.value( "save_dialog" ).isObject() ) {
        const auto save = props.value( "save_dialog" ).toObject();
        const auto dialogName = nameFor( id ) + "_save";
        activeDialogs_.insert( dialogName );
        savePath = dialogs_.saveFile
                       ? dialogs_.saveFile( dialogParent(), dialogName, label,
                                            text( save, "filter" ), text( save, "suggested_name" ) )
                       : QString{};
        if ( guard ) {
            guard->activeDialogs_.remove( dialogName );
        }
        if ( savePath.isEmpty() ) {
            return;
        }
        if ( !stillCurrent( guard, weak, generation ) ) {
            LOG_DEBUG << "Plugin view button dropped after save dialog: view changed";
            return;
        }
        savePath = savePath.left( MaxFieldValueChars );
    }
    // Read the button again: a same-shaped root may have changed it while a dialog was open.
    const auto current = weak.lock()->props;
    const auto action = current.value( "action" ).toString().left( 128 );
    if ( current.value( "event" ).toBool() || action.isEmpty() ) {
        emitEvent( id, "clicked",
                   savePath.isEmpty() ? QJsonValue( QJsonValue::Null ) : QJsonValue( savePath ) );
        return;
    }
    // Again after the dialogs, which may have let a patch change the fields.
    refreshValidity();
    if ( !fieldsValid( true ) ) {
        focusFirstInvalid();
        LOG_DEBUG << "Plugin view action not started: invalid fields element=" << id.toStdString();
        return;
    }
    bool truncated = false;
    auto arguments = current.value( "arguments" ).toObject().toVariantMap();
    arguments.insert( "inputs", inputs( &truncated ).toVariantMap() );
    if ( truncated ) {
        arguments.insert( "inputs_truncated", true );
    }
    if ( !savePath.isEmpty() ) {
        arguments.insert( "save_path", savePath );
    }
    LOG_DEBUG << "Plugin view action plugin=" << pluginId_.toStdString()
              << " view=" << viewId_.toStdString() << " element=" << id.toStdString()
              << " action=" << action.toStdString();
    Q_EMIT actionRequested( action, arguments );
}
