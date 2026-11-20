// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugintablebehaviour.h"

#include "log.h"
#include "plugintablemodel.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QSaveFile>
#include <QTableView>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace {
constexpr int SaveDelayMs = 500;

// Text a spreadsheet would run as a formula (=, +, @, or - not followed by a number; a leading
// tab or carriage return) gets a leading apostrophe: plugin data is never a formula, whether it
// is pasted or opened as CSV.
QString formulaSafe( QString text )
{
    bool number = false;
    text.toDouble( &number );
    if ( !text.isEmpty()
         && ( text.startsWith( QLatin1Char( '=' ) ) || text.startsWith( QLatin1Char( '+' ) )
              || text.startsWith( QLatin1Char( '@' ) ) || text.startsWith( QLatin1Char( '\t' ) )
              || text.startsWith( QLatin1Char( '\r' ) )
              || ( text.startsWith( QLatin1Char( '-' ) ) && !number ) ) ) {
        text.prepend( QLatin1Char( '\'' ) );
    }
    return text;
}
} // namespace

namespace pluginview {
// A cell in tab-separated text (Copy): formula-safe, tabs and line breaks become spaces.
QString tsvField( QString text )
{
    text = formulaSafe( std::move( text ) );
    for ( const auto separator : { QLatin1Char( '\t' ), QLatin1Char( '\r' ), QLatin1Char( '\n' ) } ) {
        text.replace( separator, QLatin1Char( ' ' ) );
    }
    return text;
}

// A cell in CSV (RFC 4180), formula-safe.
QString csvField( QString text )
{
    text = formulaSafe( std::move( text ) );
    if ( text.contains( QLatin1Char( ',' ) ) || text.contains( QLatin1Char( '"' ) )
         || text.contains( QLatin1Char( '\n' ) ) || text.contains( QLatin1Char( '\r' ) ) ) {
        text.replace( QStringLiteral( "\"" ), QStringLiteral( "\"\"" ) );
        return QLatin1Char( '"' ) + text + QLatin1Char( '"' );
    }
    return text;
}
} // namespace pluginview

using pluginview::csvField;
using pluginview::MaxCopyChars;
using pluginview::tsvField;

PluginTableBehaviour::PluginTableBehaviour( QTableView* table, pluginview::PluginTableModel* model,
                                            pluginview::PluginTableProxy* proxy, Hooks hooks )
    : QObject( table )
    , table_( table )
    , model_( model )
    , proxy_( proxy )
    , hooks_( std::move( hooks ) )
{
    saveTimer_ = new QTimer( this );
    saveTimer_->setSingleShot( true );
    saveTimer_->setInterval( SaveDelayMs );
    connect( saveTimer_, &QTimer::timeout, this, [ this ] {
        if ( options_.keepColumns && hooks_.saveState ) {
            hooks_.saveState( state() );
        }
    } );
    model_->onCheckRequested = [ this ]( const QString& rowId, bool checked ) {
        requestCheck( rowId, checked );
    };
    // A press that may start a drag keeps the selection as row ranges: when the rows change
    // before the drag starts (a patch, a sort), the ranges would no longer name the same rows, so
    // the drag is called off before the change, while they still do.
    const auto cancel = [ this ] { cancelPress(); };
    connect( proxy_, &QAbstractItemModel::rowsAboutToBeInserted, this, cancel );
    connect( proxy_, &QAbstractItemModel::rowsAboutToBeRemoved, this, cancel );
    connect( proxy_, &QAbstractItemModel::rowsAboutToBeMoved, this, cancel );
    connect( proxy_, &QAbstractItemModel::modelAboutToBeReset, this, cancel );
    connect( proxy_, &QAbstractItemModel::layoutAboutToBeChanged, this, cancel );
    table->viewport()->installEventFilter( this );
    table->installEventFilter( this );
    auto* header = table->horizontalHeader();
    header->viewport()->installEventFilter( this );
    // Columns can be dragged into another order, which keep_columns keeps.
    header->setSectionsMovable( true );
    header->setContextMenuPolicy( Qt::CustomContextMenu );
    // The menus are children of the table: a plugin that stops, or an entry that closes the view,
    // deletes them during exec(), so they are only touched through a QPointer afterwards.
    connect( header, &QHeaderView::customContextMenuRequested, this, [ this ]( const QPoint& at ) {
        const QPointer<QMenu> menu( buildHeaderMenu() );
        if ( menu ) {
            menu->exec( table_->horizontalHeader()->viewport()->mapToGlobal( at ) );
        }
        if ( menu ) {
            menu->deleteLater();
        }
    } );
    connect( header, &QHeaderView::sectionResized, this, [ this ]( int logical, int, int size ) {
        enforceMinimumWidth( logical, size );
        if ( size > 0 ) {
            hiddenWidths_.remove( logical ); // shown again: its own width counts
        }
    } );
    // A column that is not sortable keeps the previous sort (PluginTableProxy::sort); the
    // indicator follows the proxy.
    connect( header, &QHeaderView::sortIndicatorChanged, this, [ this ] {
        QTimer::singleShot( 0, this, [ this ] {
            auto* view = table_.data();
            if ( view == nullptr ) {
                return;
            }
            auto* section = view->horizontalHeader();
            if ( section->sortIndicatorSection() != proxy_->sortColumn() ) {
                const QSignalBlocker blocker( section );
                section->setSortIndicator( proxy_->sortColumn(), proxy_->sortOrder() );
            }
        } );
    } );
    table->setContextMenuPolicy( Qt::CustomContextMenu );
    connect( table, &QTableView::customContextMenuRequested, this, [ this ]( const QPoint& at ) {
        const QPointer<QMenu> menu( buildMenu( at ) );
        if ( menu ) {
            menu->exec( table_->viewport()->mapToGlobal( at ) );
        }
        if ( menu ) {
            menu->deleteLater();
        }
    } );
    connect( table, &QTableView::doubleClicked, this,
             [ this ]( const QModelIndex& index ) { activate( index ); } );
}

void PluginTableBehaviour::setOptions( const Options& options )
{
    options_ = options;
    if ( !table_ ) {
        return;
    }
    table_->horizontalHeader()->setVisible( options.header );
    table_->viewport()->setAcceptDrops( options.reorderable );
}

void PluginTableBehaviour::beginColumnsChange()
{
    // Saved now, with the columns the change was made on: the reset would save the old sort
    // index (and widths) under the new columns.
    flushPendingSave();
    restoring_ = true;
    const auto sorted = proxy_->sortColumn();
    sortedColumn_ = sorted >= 0 ? model_->columnId( sorted ) : QString{};
    sortedOrder_ = proxy_->sortOrder();
}

void PluginTableBehaviour::abortColumnsChange()
{
    restoring_ = false;
    sortedColumn_.clear();
}

void PluginTableBehaviour::columnsChanged()
{
    if ( !table_ ) {
        return;
    }
    // A user change still waiting to be saved is saved first, so the restore below gets it.
    flushPendingSave();
    hiddenWidths_.clear();
    restoring_ = true;
    // The proxy's sort index belongs to the old columns: it goes, and the sort comes back by
    // column id below when that column is still there.
    const auto sortedId = std::exchange( sortedColumn_, {} );
    if ( table_->isSortingEnabled() ) {
        table_->horizontalHeader()->setSortIndicator( -1, Qt::AscendingOrder );
    }
    proxy_->sort( -1 );
    for ( int column = 0; column < model_->columnCount(); ++column ) {
        table_->setColumnHidden( column, model_->column( column ).hidden );
        enforceMinimumWidth( column, table_->horizontalHeader()->sectionSize( column ) );
    }
    if ( options_.keepColumns && hooks_.loadState ) {
        restoreState( hooks_.loadState() );
    }
    else if ( const auto column = model_->columnOf( sortedId );
              column >= 0 && table_->isSortingEnabled() && model_->column( column ).sortable ) {
        table_->sortByColumn( column, sortedOrder_ );
    }
    // Never every column hidden (the header would have nothing to right-click).
    if ( shownColumns().isEmpty() && model_->columnCount() > 0 ) {
        table_->setColumnHidden( 0, false );
    }
    restoring_ = false;
}

void PluginTableBehaviour::restoreState( const QJsonObject& state )
{
    auto* header = table_->horizontalHeader();
    // Shown order first (lowest visual index first, so later moves keep earlier ones in place).
    QList<QPair<int, int>> order; // visual, logical
    for ( const auto& value : state.value( "columns" ).toArray() ) {
        const auto entry = value.toObject();
        const auto logical = model_->columnOf( entry.value( "id" ).toString() );
        if ( logical < 0 ) {
            continue; // a column the plugin no longer has
        }
        const bool hidden = entry.value( "hidden" ).toBool();
        const auto width = entry.value( "width" ).toInt();
        if ( width > 0 && hidden ) {
            // Kept for when the user shows the column again (a hidden section has no size).
            hiddenWidths_.insert( logical, qBound( 10, width, 2000 ) );
        }
        else if ( width > 0 ) {
            table_->setColumnHidden( logical, false );
            header->resizeSection( logical, qBound( 10, width, 2000 ) );
        }
        table_->setColumnHidden( logical, hidden );
        if ( entry.value( "visual" ).isDouble() ) {
            order.append( { entry.value( "visual" ).toInt(), logical } );
        }
    }
    std::sort( order.begin(), order.end() );
    for ( const auto& [ visual, logical ] : order ) {
        if ( visual >= 0 && visual < header->count() ) {
            header->moveSection( header->visualIndex( logical ), visual );
        }
    }
    // Never every column hidden.
    if ( shownColumns().isEmpty() && model_->columnCount() > 0 ) {
        table_->setColumnHidden( 0, false );
    }
    const auto sort = state.value( "sort" ).toObject();
    const auto column = model_->columnOf( sort.value( "column" ).toString() );
    if ( column >= 0 && table_->isSortingEnabled() && model_->column( column ).sortable ) {
        table_->sortByColumn( column, sort.value( "order" ) == QLatin1String( "desc" )
                                          ? Qt::DescendingOrder
                                          : Qt::AscendingOrder );
    }
}

QJsonObject PluginTableBehaviour::state() const
{
    QJsonObject state;
    if ( !table_ ) {
        return state;
    }
    const auto* header = table_->horizontalHeader();
    QJsonArray columns;
    for ( int logical = 0; logical < model_->columnCount(); ++logical ) {
        QJsonObject entry{ { "id", model_->columnId( logical ) },
                           { "hidden", header->isSectionHidden( logical ) },
                           { "visual", header->visualIndex( logical ) } };
        if ( !header->isSectionHidden( logical ) ) {
            entry.insert( "width", header->sectionSize( logical ) );
        }
        else if ( hiddenWidths_.contains( logical ) ) {
            entry.insert( "width", hiddenWidths_.value( logical ) ); // the width before hiding
        }
        columns.append( entry );
    }
    state.insert( "columns", columns );
    if ( table_->isSortingEnabled() && proxy_->sortColumn() >= 0 ) {
        state.insert( "sort", QJsonObject{ { "column", model_->columnId( proxy_->sortColumn() ) },
                                           { "order", proxy_->sortOrder() == Qt::DescendingOrder
                                                          ? "desc"
                                                          : "asc" } } );
    }
    return state;
}

void PluginTableBehaviour::flushPendingSave()
{
    if ( saveTimer_->isActive() ) {
        saveTimer_->stop();
        if ( options_.keepColumns && hooks_.saveState ) {
            hooks_.saveState( pendingState_ );
        }
    }
}

void PluginTableBehaviour::scheduleSave()
{
    if ( !restoring_ && options_.keepColumns && hooks_.saveState ) {
        pendingState_ = state(); // what columnsChanged() saves if new columns come first
        saveTimer_->start();
    }
}

void PluginTableBehaviour::enforceMinimumWidth( int logical, int size )
{
    const auto minimum = model_->column( logical ).minWidth;
    if ( table_ && minimum > 0 && size < minimum
         && !table_->horizontalHeader()->isSectionHidden( logical ) ) {
        table_->horizontalHeader()->resizeSection( logical, minimum );
    }
}

QList<int> PluginTableBehaviour::shownColumns() const
{
    QList<int> shown;
    if ( !table_ ) {
        return shown;
    }
    const auto* header = table_->horizontalHeader();
    for ( int visual = 0; visual < header->count(); ++visual ) {
        const auto logical = header->logicalIndex( visual );
        if ( !header->isSectionHidden( logical ) ) {
            shown.append( logical );
        }
    }
    return shown;
}

QString PluginTableBehaviour::rowIdAt( const QModelIndex& index ) const
{
    return index.isValid() ? model_->rowId( proxy_->mapToSource( index ).row() ) : QString{};
}

QModelIndex PluginTableBehaviour::indexOfRow( const QString& rowId ) const
{
    const auto row = model_->rowOf( rowId );
    if ( row < 0 ) {
        return {};
    }
    // The first shown column, so the current cell is visible.
    const auto shown = shownColumns();
    return proxy_->mapFromSource( model_->index( row, shown.isEmpty() ? 0 : shown.first() ) );
}

bool PluginTableBehaviour::isCheckCell( const QModelIndex& index ) const
{
    return index.isValid() && model_->column( proxy_->mapToSource( index ).column() ).check;
}

void PluginTableBehaviour::select( const QJsonValue& ids, const QJsonValue& current,
                                   const QJsonValue& scrollTo )
{
    if ( !table_ ) {
        return;
    }
    auto* selection = table_->selectionModel();
    const bool selectable = table_->selectionMode() != QAbstractItemView::NoSelection;
    if ( ids.isArray() && selectable ) {
        // The plugin sets the selection: a press before it starts no drag, and its change (held,
        // or waiting for the patch to end) is not reported: the plugin's selection is never echoed.
        endPress();
        reportPending_ = false;
        QItemSelection chosen;
        int used = 0;
        for ( const auto& id : ids.toArray() ) {
            if ( used >= pluginview::MaxTableIds ) {
                break;
            }
            ++used;
            const auto index = indexOfRow( id.toString() );
            if ( index.isValid() ) {
                chosen.select( index, index );
            }
            if ( table_->selectionMode() == QAbstractItemView::SingleSelection && !chosen.isEmpty() ) {
                break; // one row in a single selection table
            }
        }
        selection->select( chosen, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
    }
    // The keyboard focus works without selection too (Enter then activates the row).
    if ( current.isString() ) {
        const auto index = indexOfRow( current.toString() );
        if ( index.isValid() ) {
            selection->setCurrentIndex( index, QItemSelectionModel::NoUpdate );
        }
    }
    if ( scrollTo.isString() ) {
        const auto index = indexOfRow( scrollTo.toString() );
        if ( index.isValid() ) {
            table_->scrollTo( index );
        }
    }
}

QString PluginTableBehaviour::currentRowId() const
{
    return table_ ? rowIdAt( table_->currentIndex() ) : QString{};
}

QStringList PluginTableBehaviour::selectedRowIds( int maximum, int* count ) const
{
    QStringList ids;
    if ( !table_ ) {
        return ids;
    }
    auto rows = table_->selectionModel()->selectedRows();
    std::sort( rows.begin(), rows.end(),
               []( const QModelIndex& a, const QModelIndex& b ) { return a.row() < b.row(); } );
    if ( count != nullptr ) {
        *count = static_cast<int>( rows.size() );
    }
    for ( const auto& index : std::as_const( rows ) ) {
        if ( ids.size() >= maximum ) {
            break;
        }
        ids.append( rowIdAt( index ) );
    }
    return ids;
}

QJsonObject PluginTableBehaviour::menuTarget( const QString& rowId, const QString& columnId ) const
{
    // At most 1,000 ids and MaxIdBytes of them; selected_count stays the full count.
    int count = 0;
    const auto ids = pluginview::boundedIds( selectedRowIds( pluginview::MaxTableIds, &count ) );
    return { { "row_id", rowId.isEmpty() ? QJsonValue() : QJsonValue( rowId ) },
             { "column_id", columnId.isEmpty() ? QJsonValue() : QJsonValue( columnId ) },
             { "selected_ids", QJsonArray::fromStringList( ids ) },
             { "selected_count", count } };
}

QMenu* PluginTableBehaviour::buildMenu( const QPoint& position )
{
    if ( !table_ ) {
        return nullptr;
    }
    const auto index = table_->indexAt( position );
    QString rowId;
    QString columnId;
    if ( index.isValid() ) {
        rowId = rowIdAt( index );
        columnId = model_->columnId( proxy_->mapToSource( index ).column() );
        // A right-click on a row makes it current, and selects it when it was not selected. The
        // current row is set first: the `selected` event the selection sends carries it.
        auto* selection = table_->selectionModel();
        if ( table_->selectionMode() != QAbstractItemView::NoSelection ) {
            selection->setCurrentIndex( index, QItemSelectionModel::NoUpdate );
            if ( !selection->isRowSelected( index.row(), index.parent() ) ) {
                selection->select( index, QItemSelectionModel::ClearAndSelect
                                              | QItemSelectionModel::Rows );
            }
        }
    }
    auto* menu = new QMenu( table_ );
    menu->setObjectName( table_->objectName() + QStringLiteral( "_menu" ) );
    menu->setToolTipsVisible( true );
    if ( options_.copy ) {
        auto* copy = menu->addAction( tr( "Copy" ) );
        copy->setObjectName( table_->objectName() + QStringLiteral( "_copy" ) );
        copy->setShortcut( QKeySequence::Copy );
        copy->setEnabled( !selectedRowIds( 1 ).isEmpty() || !currentRowId().isEmpty() );
        connect( copy, &QAction::triggered, this,
                 [ this ] { QApplication::clipboard()->setText( copyText( false ) ); } );
        auto* copyAll = menu->addAction( tr( "Copy All" ) );
        copyAll->setObjectName( table_->objectName() + QStringLiteral( "_copy_all" ) );
        copyAll->setEnabled( proxy_->rowCount() > 0 );
        connect( copyAll, &QAction::triggered, this,
                 [ this ] { QApplication::clipboard()->setText( copyText( true ) ); } );
    }
    if ( options_.exportCsv ) {
        auto* exporting = menu->addAction( tr( "Export CSV…" ) );
        exporting->setObjectName( table_->objectName() + QStringLiteral( "_export" ) );
        connect( exporting, &QAction::triggered, this, [ this ] {
            if ( !hooks_.saveFile ) {
                return;
            }
            // The dialog runs its own event loop: the table may be gone when it returns.
            const QPointer<PluginTableBehaviour> self( this );
            const auto path
                = hooks_.saveFile( tr( "Export CSV" ), tr( "CSV files (*.csv)" ), "table.csv" );
            if ( !self || !table_ ) {
                return;
            }
            QString error;
            if ( !path.isEmpty() && !exportCsv( path, &error ) ) {
                LOG_WARNING << "Plugin table export failed: " << error.toStdString();
            }
        } );
    }
    if ( !options_.contextMenu.isEmpty() && hooks_.elementMenu ) {
        hooks_.elementMenu( menu, options_.contextMenu, menuTarget( rowId, columnId ) );
    }
    if ( menu->isEmpty() ) {
        delete menu;
        return nullptr;
    }
    return menu;
}

QMenu* PluginTableBehaviour::buildHeaderMenu()
{
    if ( !table_ || model_->columnCount() == 0 ) {
        return nullptr;
    }
    auto* header = table_->horizontalHeader();
    auto* menu = new QMenu( table_ );
    menu->setObjectName( table_->objectName() + QStringLiteral( "_columns" ) );
    const auto shown = shownColumns();
    for ( int visual = 0; visual < header->count(); ++visual ) {
        const auto logical = header->logicalIndex( visual );
        auto title = model_->headerData( logical, Qt::Horizontal, Qt::DisplayRole ).toString();
        title.replace( QLatin1Char( '&' ), QStringLiteral( "&&" ) );
        auto* action = menu->addAction( title.isEmpty() ? model_->columnId( logical ) : title );
        action->setObjectName( table_->objectName() + QStringLiteral( "_column_" )
                               + model_->columnId( logical ) );
        action->setCheckable( true );
        action->setChecked( !header->isSectionHidden( logical ) );
        // The last shown column stays.
        action->setEnabled( !( shown.size() == 1 && shown.first() == logical ) );
        // By id, resolved when picked: the plugin may change the columns while the menu is open.
        connect( action, &QAction::toggled, this,
                 [ this, id = model_->columnId( logical ) ]( bool checked ) {
                     if ( !table_ ) {
                         return;
                     }
                     const auto column = model_->columnOf( id );
                     const auto shownNow = shownColumns();
                     // A column that is gone, or the last shown one, stays as it is.
                     if ( column < 0
                          || ( !checked && shownNow.size() == 1 && shownNow.first() == column ) ) {
                         return;
                     }
                     auto* section = table_->horizontalHeader();
                     if ( !checked ) {
                         // Its width is kept while hidden, so the saved state still holds it.
                         if ( section->sectionSize( column ) > 0 ) {
                             hiddenWidths_.insert( column, section->sectionSize( column ) );
                         }
                         table_->setColumnHidden( column, true );
                     }
                     else {
                         const auto kept = hiddenWidths_.take( column );
                         table_->setColumnHidden( column, false );
                         if ( kept > 0 ) {
                             section->resizeSection( column, kept );
                         }
                         // Showing a section does not always report a resize: the minimum
                         // applies now.
                         enforceMinimumWidth( column, section->sectionSize( column ) );
                     }
                     scheduleSave();
                 } );
    }
    return menu;
}

QString PluginTableBehaviour::copyText( bool all ) const
{
    if ( !table_ ) {
        return {};
    }
    const auto columns = shownColumns();
    // At most MaxCopyChars, whole rows only: a table of 50,000 cells of 4,096 characters would
    // be hundreds of megabytes.
    QString text;
    bool cut = false;
    const auto line = [ & ]( const std::function<QString( int column )>& field ) {
        QString row;
        for ( qsizetype at = 0; at < columns.size(); ++at ) {
            if ( at > 0 ) {
                row += QLatin1Char( '\t' );
            }
            row += tsvField( field( columns.at( at ) ) );
        }
        const auto needed = row.size() + ( text.isEmpty() ? 0 : 1 );
        if ( text.size() + needed > MaxCopyChars ) {
            cut = true;
            return false;
        }
        if ( !text.isEmpty() ) {
            text += QLatin1Char( '\n' );
        }
        text += row;
        return true;
    };
    const auto cells = [ & ]( int proxyRow ) {
        return line( [ & ]( int column ) {
            return proxy_->index( proxyRow, column ).data( Qt::DisplayRole ).toString();
        } );
    };
    if ( all ) {
        line( [ & ]( int column ) {
            return model_->headerData( column, Qt::Horizontal, Qt::DisplayRole ).toString();
        } );
        for ( int row = 0; row < proxy_->rowCount(); ++row ) {
            if ( !cells( row ) ) {
                break;
            }
        }
    }
    else {
        auto rows = table_->selectionModel()->selectedRows();
        if ( rows.isEmpty() && table_->currentIndex().isValid() ) {
            rows.append( table_->currentIndex() );
        }
        std::sort( rows.begin(), rows.end(),
                   []( const QModelIndex& a, const QModelIndex& b ) { return a.row() < b.row(); } );
        for ( const auto& index : std::as_const( rows ) ) {
            if ( !cells( index.row() ) ) {
                break;
            }
        }
    }
    if ( cut ) {
        LOG_WARNING << "Plugin table copy cut at " << MaxCopyChars << " characters (whole rows)";
    }
    return text;
}

bool PluginTableBehaviour::exportCsv( const QString& path, QString* error ) const
{
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        *error = file.errorString();
        return false;
    }
    // Written line by line: the file never sits in memory whole.
    const auto columns = shownColumns();
    const auto line = [ & ]( const std::function<QString( int column )>& field ) {
        QString text;
        for ( qsizetype at = 0; at < columns.size(); ++at ) {
            if ( at > 0 ) {
                text += QLatin1Char( ',' );
            }
            text += csvField( field( columns.at( at ) ) );
        }
        text += QStringLiteral( "\r\n" );
        return file.write( text.toUtf8() ) >= 0;
    };
    bool written = line( [ & ]( int column ) {
        return model_->headerData( column, Qt::Horizontal, Qt::DisplayRole ).toString();
    } );
    for ( int row = 0; written && row < proxy_->rowCount(); ++row ) {
        written = line( [ & ]( int column ) {
            return proxy_->index( row, column ).data( Qt::DisplayRole ).toString();
        } );
    }
    if ( !written || !file.commit() ) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool PluginTableBehaviour::reorderAllowed() const
{
    // Rows move in the plugin's order only: never while sorted or filtered.
    return options_.reorderable && proxy_->sortColumn() < 0
           && proxy_->filterRegularExpression().pattern().isEmpty()
           && ( !hooks_.canAct || hooks_.canAct() );
}

bool PluginTableBehaviour::moveRows( const QStringList& rowIds, const QString& before )
{
    if ( !reorderAllowed() || rowIds.isEmpty() || rowIds.contains( before ) || !hooks_.emitEvent ) {
        return false;
    }
    QStringList known;
    for ( const auto& id : rowIds ) {
        if ( known.size() >= pluginview::MaxTableIds ) {
            break;
        }
        if ( model_->rowOf( id ) >= 0 ) {
            known.append( id );
        }
    }
    const auto ids = QJsonArray::fromStringList( pluginview::boundedIds( known ) );
    if ( ids.isEmpty() ) {
        return false;
    }
    // The host does not move the rows: the plugin confirms with a patch.
    return hooks_.emitEvent( QStringLiteral( "moved" ),
                             QJsonObject{ { "row_ids", ids },
                                          { "before", before.isEmpty() ? QJsonValue()
                                                                       : QJsonValue( before ) } } );
}

void PluginTableBehaviour::requestCheck( const QString& rowId, bool checked )
{
    // A Shift+click sets the range from the last clicked check box, in shown order. The range is
    // walked from the clicked row back toward the anchor, so a range over the id limit (1,000
    // ids, MaxIdBytes of them) still holds the row the user clicked; the rows that change are
    // exactly the ones the event reports.
    QStringList ids{ rowId };
    if ( ( QGuiApplication::keyboardModifiers() & Qt::ShiftModifier ) && !checkAnchor_.isEmpty()
         && checkAnchor_ != rowId ) {
        const auto from = indexOfRow( checkAnchor_ ).row();
        const auto to = indexOfRow( rowId ).row();
        if ( from >= 0 && to >= 0 ) {
            ids.clear();
            const int step = from < to ? -1 : 1;
            for ( int row = to;; row += step ) {
                ids.append( rowIdAt( proxy_->index( row, 0 ) ) ); // the clicked row first
                if ( row == from || ids.size() >= pluginview::MaxTableIds ) {
                    break;
                }
            }
            ids = pluginview::boundedIds( ids );
            if ( step < 0 ) {
                std::reverse( ids.begin(), ids.end() ); // shown order
            }
        }
    }
    checkAnchor_ = rowId;
    const auto changed = model_->setChecked( ids, checked );
    if ( changed.isEmpty() || !hooks_.emitEvent ) {
        return;
    }
    const QJsonObject value{ { "row_id", rowId },
                             { "row_ids", QJsonArray::fromStringList( changed ) },
                             { "checked", checked } };
    if ( !hooks_.emitEvent( QStringLiteral( "checked" ), value ) ) {
        model_->setChecked( changed, !checked ); // the plugin did not get it
    }
}

void PluginTableBehaviour::activate( const QModelIndex& index )
{
    if ( !index.isValid() || !hooks_.emitEvent || !hooks_.extendedEvents
         || !hooks_.extendedEvents() || ( hooks_.canAct && !hooks_.canAct() ) ) {
        return;
    }
    const auto source = proxy_->mapToSource( index );
    // An editable cell opens its editor instead; a check box cell sends `checked`.
    if ( model_->flags( source ).testFlag( Qt::ItemIsEditable ) || isCheckCell( index ) ) {
        return;
    }
    hooks_.emitEvent( QStringLiteral( "activated" ),
                      QJsonObject{ { "row_id", model_->rowId( source.row() ) },
                                   { "column_id", model_->columnId( source.column() ) } } );
}

QStringList PluginTableBehaviour::draggedRows() const
{
    if ( pressRow_.isEmpty() ) {
        return {};
    }
    if ( !pressWasSelected_ ) {
        return { pressRow_ };
    }
    // The selection as it was before the press (the press may have changed it: a toggle table
    // deselects the pressed row), in shown order, and only the ids `moved` sends: the ranges are
    // turned into row numbers, never into every id.
    std::vector<int> rows;
    for ( const auto& range : pressSelection_ ) {
        for ( int row = range.top(); range.isValid() && row <= range.bottom(); ++row ) {
            rows.push_back( row );
        }
    }
    std::sort( rows.begin(), rows.end() );
    rows.erase( std::unique( rows.begin(), rows.end() ), rows.end() );
    QStringList ids;
    for ( const auto row : rows ) {
        if ( ids.size() >= pluginview::MaxTableIds ) {
            break;
        }
        ids.append( rowIdAt( proxy_->index( row, 0 ) ) );
    }
    return pluginview::boundedIds( ids );
}

void PluginTableBehaviour::cancelPress()
{
    if ( pressRow_.isEmpty() && !holdSelection_ ) {
        return;
    }
    // Compared before the change, while the press's ranges still name the rows they did: only a
    // change the user's press made is reported, never the one before the press.
    const bool changed = holdSelection_ && pressChangedSelection();
    endPress();
    if ( changed && !std::exchange( reportPending_, true ) ) {
        // A patch is still being applied: reported once it is done, unless it sets the selection
        // itself (select() drops the report) or the view sends `selected` anyway.
        QTimer::singleShot( 0, this, [ this ] {
            if ( reportPending_ ) {
                reportSelection();
            }
        } );
    }
}

void PluginTableBehaviour::endPress()
{
    holdSelection_ = false;
    pressRow_.clear();
    pressSelection_.clear();
    pressWasSelected_ = false;
}

void PluginTableBehaviour::reportSelection()
{
    reportPending_ = false;
    if ( hooks_.selectionSettled ) {
        hooks_.selectionSettled();
    }
}

void PluginTableBehaviour::releaseSelection( bool report )
{
    if ( !std::exchange( holdSelection_, false ) || !report || !table_ ) {
        return;
    }
    if ( pressChangedSelection() ) {
        reportSelection();
    }
}

bool PluginTableBehaviour::pressChangedSelection() const
{
    if ( !table_ ) {
        return false;
    }
    // The rows of a selection, in order: the press changed the selection when they differ.
    const auto rowsOf = []( const QItemSelection& selection ) {
        std::vector<int> rows;
        for ( const auto& range : selection ) {
            for ( int row = range.top(); range.isValid() && row <= range.bottom(); ++row ) {
                rows.push_back( row );
            }
        }
        std::sort( rows.begin(), rows.end() );
        rows.erase( std::unique( rows.begin(), rows.end() ), rows.end() );
        return rows;
    };
    return rowsOf( table_->selectionModel()->selection() ) != rowsOf( pressSelection_ );
}

void PluginTableBehaviour::startDrag()
{
    const auto ids = draggedRows();
    const bool wasSelected = std::exchange( pressWasSelected_, false );
    pressRow_.clear();
    if ( wasSelected ) {
        // The press changed the selection (a toggle table deselected the row): the selection
        // before the press comes back, as Qt's own drags defer the change. It is still held, so
        // the plugin hears neither change.
        table_->selectionModel()->select( pressSelection_, QItemSelectionModel::ClearAndSelect );
    }
    releaseSelection( true ); // the selection is the one before the press: nothing to report
    pressSelection_.clear();
    auto* mime = new QMimeData;
    mime->setData( RowsMimeType, QJsonDocument( QJsonArray::fromStringList( ids ) ).toJson(
                                     QJsonDocument::Compact ) );
    auto* drag = new QDrag( table_->viewport() );
    drag->setMimeData( mime );
    drag->exec( Qt::MoveAction );
}

bool PluginTableBehaviour::drop( const QPoint& position, const QByteArray& payload )
{
    QStringList ids;
    for ( const auto& id : QJsonDocument::fromJson( payload ).array() ) {
        ids.append( id.toString() );
    }
    // Before the row under the pointer, or the one after it in its lower half; the end below the
    // last row.
    QString before;
    const auto index = table_->indexAt( position );
    if ( index.isValid() ) {
        const auto rect = table_->visualRect( index );
        const auto row = position.y() > rect.center().y() ? index.row() + 1 : index.row();
        if ( row < proxy_->rowCount() ) {
            before = rowIdAt( proxy_->index( row, 0 ) );
        }
    }
    return moveRows( ids, before );
}

bool PluginTableBehaviour::eventFilter( QObject* watched, QEvent* event )
{
    if ( !table_ ) {
        return false;
    }
    auto* viewport = table_->viewport();
    if ( watched == table_->horizontalHeader()->viewport()
         && event->type() == QEvent::MouseButtonRelease ) {
        scheduleSave(); // the user resized, moved or sorted a column
        return false;
    }
    if ( watched == table_ && event->type() == QEvent::KeyPress ) {
        const auto* key = static_cast<QKeyEvent*>( event );
        if ( options_.copy && key->matches( QKeySequence::Copy ) ) {
            // Nothing selected and no current row: the clipboard keeps what it has.
            const auto text = copyText( false );
            if ( text.isEmpty() ) {
                return false;
            }
            QApplication::clipboard()->setText( text );
            return true;
        }
        // An open cell editor has the focus, so this Enter is never the one that commits it.
        if ( key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ) {
            activate( table_->currentIndex() );
            return false;
        }
        return false;
    }
    if ( watched != viewport ) {
        return false;
    }
    switch ( event->type() ) {
    case QEvent::MouseButtonPress: {
        const auto* mouse = static_cast<QMouseEvent*>( event );
        const auto position = mouse->position().toPoint();
        const auto index = table_->indexAt( position );
        releaseSelection( true ); // a release this behaviour never saw
        pressRow_.clear();
        pressSelection_.clear();
        pressWasSelected_ = false;
        if ( mouse->button() != Qt::LeftButton ) {
            return false;
        }
        // Toggle selection: a Shift+click adds the range from the last clicked row (by id, so a
        // re-sort keeps it). A Shift+click on a check box sets a check range instead.
        const auto anchor = indexOfRow( selectionAnchor_ );
        if ( table_->selectionMode() == QAbstractItemView::MultiSelection && index.isValid()
             && ( mouse->modifiers() & Qt::ShiftModifier ) && anchor.isValid()
             && !isCheckCell( index ) ) {
            QItemSelection range( proxy_->index( qMin( anchor.row(), index.row() ), 0 ),
                                  proxy_->index( qMax( anchor.row(), index.row() ), 0 ) );
            // Current first: the `selected` event the selection sends carries it.
            table_->selectionModel()->setCurrentIndex( index, QItemSelectionModel::NoUpdate );
            table_->selectionModel()->select( range, QItemSelectionModel::Select
                                                         | QItemSelectionModel::Rows );
            return true;
        }
        if ( index.isValid() ) {
            selectionAnchor_ = rowIdAt( index );
            if ( mouse->modifiers() == Qt::NoModifier && reorderAllowed() ) {
                pressPosition_ = position;
                pressRow_ = rowIdAt( index );
                // Before the view handles the press, which may change the selection. Ranges, not
                // ids: cheap for any selection; whether the pressed row was in it decides what
                // moves.
                pressSelection_ = table_->selectionModel()->selection();
                pressWasSelected_ = table_->selectionModel()->isRowSelected( index.row(),
                                                                             index.parent() );
                // The press may toggle this row off for a moment: the plugin hears nothing until
                // the release, and nothing at all when a drag puts the selection back.
                holdSelection_ = pressWasSelected_;
            }
        }
        return false;
    }
    case QEvent::MouseMove: {
        const auto* mouse = static_cast<QMouseEvent*>( event );
        if ( !pressRow_.isEmpty() && ( mouse->buttons() & Qt::LeftButton )
             && ( mouse->position().toPoint() - pressPosition_ ).manhattanLength()
                    >= QApplication::startDragDistance() ) {
            startDrag();
            return true;
        }
        return false;
    }
    case QEvent::MouseButtonRelease:
        // No drag started: a held selection change is reported once, and the snapshot goes. (A
        // change the view still makes on the release itself is no longer held.)
        releaseSelection( true );
        pressRow_.clear();
        pressSelection_.clear();
        pressWasSelected_ = false;
        return false;
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto* drag = static_cast<QDragMoveEvent*>( event );
        // Only rows of this table, while they may move.
        if ( drag->mimeData()->hasFormat( RowsMimeType ) && drag->source() == viewport
             && reorderAllowed() ) {
            drag->acceptProposedAction();
        }
        else {
            drag->ignore();
        }
        return true;
    }
    case QEvent::Drop: {
        auto* dropped = static_cast<QDropEvent*>( event );
        if ( dropped->mimeData()->hasFormat( RowsMimeType ) && dropped->source() == viewport ) {
            drop( dropped->position().toPoint(), dropped->mimeData()->data( RowsMimeType ) );
            dropped->acceptProposedAction();
        }
        return true;
    }
    default:
        return false;
    }
}
