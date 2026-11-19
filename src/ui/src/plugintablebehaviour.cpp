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
#include <utility>

namespace {
constexpr int SaveDelayMs = 500;

// A cell in tab-separated text: tabs and line breaks become spaces.
QString tsvField( QString text )
{
    for ( const auto separator : { QLatin1Char( '\t' ), QLatin1Char( '\r' ), QLatin1Char( '\n' ) } ) {
        text.replace( separator, QLatin1Char( ' ' ) );
    }
    return text;
}

// A cell in CSV (RFC 4180). Text a spreadsheet would run as a formula (=, +, @, or - not
// followed by a number) gets a leading apostrophe: plugin data is never a formula.
QString csvField( QString text )
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
    if ( text.contains( QLatin1Char( ',' ) ) || text.contains( QLatin1Char( '"' ) )
         || text.contains( QLatin1Char( '\n' ) ) || text.contains( QLatin1Char( '\r' ) ) ) {
        text.replace( QStringLiteral( "\"" ), QStringLiteral( "\"\"" ) );
        return QLatin1Char( '"' ) + text + QLatin1Char( '"' );
    }
    return text;
}
} // namespace

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
    connect( header, &QHeaderView::sectionResized, this,
             [ this ]( int logical, int, int size ) { enforceMinimumWidth( logical, size ); } );
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

void PluginTableBehaviour::columnsChanged()
{
    if ( !table_ ) {
        return;
    }
    // A user change still waiting to be saved is saved first, so the restore below gets it.
    if ( saveTimer_->isActive() ) {
        saveTimer_->stop();
        if ( options_.keepColumns && hooks_.saveState ) {
            hooks_.saveState( pendingState_ );
        }
    }
    restoring_ = true;
    for ( int column = 0; column < model_->columnCount(); ++column ) {
        table_->setColumnHidden( column, model_->column( column ).hidden );
        enforceMinimumWidth( column, table_->horizontalHeader()->sectionSize( column ) );
    }
    if ( options_.keepColumns && hooks_.loadState ) {
        restoreState( hooks_.loadState() );
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
        table_->setColumnHidden( logical, entry.value( "hidden" ).toBool() );
        const auto width = entry.value( "width" ).toInt();
        if ( width > 0 ) {
            header->resizeSection( logical, qBound( 10, width, 2000 ) );
        }
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
    if ( current.isString() && selectable ) {
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
    int count = 0;
    const auto ids = selectedRowIds( pluginview::MaxTableIds, &count );
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
        connect( action, &QAction::toggled, this, [ this, logical ]( bool checked ) {
            if ( table_ ) {
                table_->setColumnHidden( logical, !checked );
                scheduleSave();
            }
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
    // One string, built in place: a whole table can be large (no list of lines to join).
    QString text;
    const auto line = [ & ]( const std::function<QString( int column )>& field ) {
        if ( !text.isEmpty() ) {
            text += QLatin1Char( '\n' );
        }
        for ( qsizetype at = 0; at < columns.size(); ++at ) {
            if ( at > 0 ) {
                text += QLatin1Char( '\t' );
            }
            text += tsvField( field( columns.at( at ) ) );
        }
    };
    const auto cells = [ & ]( int proxyRow ) {
        line( [ & ]( int column ) {
            return proxy_->index( proxyRow, column ).data( Qt::DisplayRole ).toString();
        } );
    };
    if ( all ) {
        line( [ & ]( int column ) {
            return model_->headerData( column, Qt::Horizontal, Qt::DisplayRole ).toString();
        } );
        for ( int row = 0; row < proxy_->rowCount(); ++row ) {
            cells( row );
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
            cells( index.row() );
        }
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
    QJsonArray ids;
    for ( const auto& id : rowIds ) {
        if ( ids.size() >= pluginview::MaxTableIds ) {
            break;
        }
        if ( model_->rowOf( id ) >= 0 ) {
            ids.append( id );
        }
    }
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
    // walked from the clicked row back toward the anchor, so a range over the id limit still
    // holds the row the user clicked.
    QStringList ids{ rowId };
    if ( ( QGuiApplication::keyboardModifiers() & Qt::ShiftModifier ) && !checkAnchor_.isEmpty()
         && checkAnchor_ != rowId ) {
        const auto from = indexOfRow( checkAnchor_ ).row();
        const auto to = indexOfRow( rowId ).row();
        if ( from >= 0 && to >= 0 ) {
            ids.clear();
            const int step = from < to ? -1 : 1;
            for ( int row = to;; row += step ) {
                ids.prepend( rowIdAt( proxy_->index( row, 0 ) ) );
                if ( row == from || ids.size() >= pluginview::MaxTableIds ) {
                    break;
                }
            }
            if ( step > 0 ) {
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

void PluginTableBehaviour::startDrag()
{
    auto ids = selectedRowIds( pluginview::MaxTableIds );
    if ( !ids.contains( pressRow_ ) ) {
        ids = QStringList{ pressRow_ };
    }
    pressRow_.clear();
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
        pressRow_.clear();
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
        pressRow_.clear();
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
