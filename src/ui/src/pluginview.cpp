// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginview.h"
#include "checkcombobox.h"
#include "log.h"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QFileDialog>
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
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
const QStringList ContainerTypes{ "column", "row", "group" };
const QStringList ElementTypes{ "column", "row",    "group",    "label", "badge",
                                "field",  "button", "progress", "table" };
const QStringList FieldKinds{ "text",      "number",    "choice",   "boolean",
                              "file_open", "file_save", "directory", "multichoice" };
const QStringList CommonKeys{ "id", "type", "enabled", "visible", "tooltip" };
// Properties that keep their state when a same-shaped root omits them.
const QStringList KeptWhenAbsent{ "value", "rows", "columns", "kind", "children" };
constexpr int ForcedResetRows = 512;

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
        return { "kind", "label", "value", "choices", "filter", "placeholder" };
    }
    if ( type == "button" ) {
        return { "label", "tone", "action", "arguments", "event", "confirm", "save_dialog" };
    }
    if ( type == "progress" ) {
        return { "value", "busy", "text" };
    }
    return { "columns", "rows", "sortable", "filter_box", "selection" };
}

// Defaults restored when a same-shaped root no longer sends a property. Properties without a
// default here (button action, arguments, event, confirm, save_dialog) are simply dropped.
QJsonObject defaultsFor( const QString& type )
{
    QJsonObject defaults{ { "enabled", true }, { "visible", true }, { "tooltip", "" } };
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
    }
    return defaults;
}

QColor toneColor( const QString& tone )
{
    if ( tone == "ok" ) {
        return QColor( 0x2e, 0x7d, 0x32 );
    }
    if ( tone == "warn" ) {
        return QColor( 0xb2, 0x6a, 0x00 );
    }
    if ( tone == "error" ) {
        return QColor( 0xc6, 0x28, 0x28 );
    }
    if ( tone == "muted" ) {
        return QColor( 0x80, 0x80, 0x80 );
    }
    return {};
}

QString cellText( const QJsonValue& value, int maximum = PluginView::MaxTextChars )
{
    if ( value.isString() ) {
        return value.toString().left( maximum );
    }
    if ( value.isDouble() ) {
        return QString::number( value.toDouble(), 'g', 15 );
    }
    if ( value.isBool() ) {
        return value.toBool() ? QStringLiteral( "true" ) : QStringLiteral( "false" );
    }
    return {};
}

QString text( const QJsonObject& props, const char* key, int maximum = PluginView::MaxTextChars )
{
    return props.value( QLatin1String( key ) ).toString().left( maximum );
}

// Plugin table rows; identity is the row id so patches never rebuild the view.
class PluginTableModel final : public QAbstractTableModel {
public:
    struct Column {
        QString id;
        QString title;
    };
    struct Row {
        QString id;
        QStringList cells;
        QString tone;
    };

    explicit PluginTableModel( QObject* parent )
        : QAbstractTableModel( parent )
    {
    }
    int rowCount( const QModelIndex& parent = {} ) const override
    {
        return parent.isValid() ? 0 : static_cast<int>( rows_.size() );
    }
    int columnCount( const QModelIndex& parent = {} ) const override
    {
        return parent.isValid() ? 0 : static_cast<int>( columns_.size() );
    }
    QVariant data( const QModelIndex& index, int role ) const override
    {
        if ( !index.isValid() || index.row() >= rows_.size() ) {
            return {};
        }
        const auto& row = rows_.at( index.row() );
        const auto value
            = index.column() < row.cells.size() ? row.cells.at( index.column() ) : QString{};
        if ( role == Qt::DisplayRole || role == Qt::ToolTipRole ) {
            return value;
        }
        if ( role == Qt::UserRole ) { // sort key: numbers sort numerically
            bool numeric = false;
            const auto number = value.toDouble( &numeric );
            return numeric ? QVariant( number ) : QVariant( value );
        }
        if ( role == Qt::UserRole + 1 ) {
            return row.id;
        }
        if ( role == Qt::ForegroundRole && toneColor( row.tone ).isValid() ) {
            return toneColor( row.tone );
        }
        return {};
    }
    QVariant headerData( int section, Qt::Orientation orientation, int role ) const override
    {
        if ( orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0
             && section < columns_.size() ) {
            return columns_.at( section ).title;
        }
        return QAbstractTableModel::headerData( section, orientation, role );
    }

    qsizetype cellCount( qsizetype rows ) const
    {
        return rows * qMax<qsizetype>( 1, columns_.size() );
    }
    bool setColumns( const QJsonArray& columns )
    {
        QVector<Column> parsed;
        for ( const auto& value : columns ) {
            const auto column = value.toObject();
            const auto id
                = ( value.isString() ? value.toString() : column.value( "id" ).toString() )
                      .left( PluginView::MaxTextChars );
            parsed.append( { id, column.contains( "title" ) ? text( column, "title" ) : id } );
        }
        if ( qMax<qsizetype>( 1, parsed.size() ) * rows_.size() > PluginView::MaxTableCells ) {
            return false;
        }
        beginResetModel();
        columns_ = parsed;
        for ( auto& row : rows_ ) {
            row.cells = row.cells.mid( 0, cellLimit() );
        }
        endResetModel();
        return true;
    }
    Row parse( const QJsonObject& object ) const
    {
        Row row{ object.value( "id" ).toString().left( PluginView::MaxTextChars ),
                 {},
                 object.value( "tone" ).toString().left( 16 ) };
        for ( const auto& cell : object.value( "cells" ).toArray() ) {
            if ( row.cells.size() >= cellLimit() ) {
                break; // never more cells than columns
            }
            row.cells.append( cellText( cell ) );
        }
        return row;
    }
    bool replace( const QJsonArray& rows )
    {
        if ( cellCount( rows.size() ) > PluginView::MaxTableCells ) {
            return false;
        }
        beginResetModel();
        rows_.clear();
        index_.clear();
        rows_.reserve( rows.size() );
        for ( const auto& value : rows ) {
            const auto row = parse( value.toObject() );
            if ( row.id.isEmpty() || index_.contains( row.id ) ) {
                continue; // ids are unique: the first row with an id wins
            }
            index_.insert( row.id, static_cast<int>( rows_.size() ) );
            rows_.append( row );
        }
        endResetModel();
        return true;
    }
    void update( const QJsonArray& rows )
    {
        for ( const auto& value : rows ) {
            const auto object = value.toObject();
            const auto found = index_.find( object.value( "id" ).toString() );
            if ( found == index_.end() ) {
                continue;
            }
            auto& row = rows_[ *found ];
            if ( object.contains( "cells" ) ) {
                row.cells = parse( object ).cells;
            }
            if ( object.contains( "tone" ) ) {
                row.tone = object.value( "tone" ).toString().left( 16 );
            }
            Q_EMIT dataChanged( index( *found, 0 ), index( *found, qMax( 0, columnCount() - 1 ) ) );
        }
    }
    bool insert( const QJsonArray& rows, const QString& before )
    {
        QVector<Row> added;
        QSet<QString> seen;
        for ( const auto& value : rows ) {
            const auto row = parse( value.toObject() );
            if ( !row.id.isEmpty() && !index_.contains( row.id ) && !seen.contains( row.id ) ) {
                seen.insert( row.id );
                added.append( row );
            }
        }
        if ( added.isEmpty() ) {
            return true;
        }
        if ( cellCount( rows_.size() + added.size() ) > PluginView::MaxTableCells ) {
            return false;
        }
        const int position
            = index_.contains( before ) ? index_.value( before ) : static_cast<int>( rows_.size() );
        beginInsertRows( {}, position, position + static_cast<int>( added.size() ) - 1 );
        rows_.insert( position, added.size(), Row{} );
        std::copy( added.begin(), added.end(), rows_.begin() + position );
        reindex();
        endInsertRows();
        return true;
    }
    // Returns true when the model was reset (large batches) instead of removing ranges.
    bool remove( const QJsonArray& ids )
    {
        QSet<int> found;
        for ( const auto& id : ids ) {
            const auto it = index_.find( id.toString() );
            if ( it != index_.end() ) {
                found.insert( *it );
            }
        }
        if ( found.isEmpty() ) {
            return false;
        }
        if ( found.size() > ForcedResetRows ) {
            beginResetModel();
            QVector<Row> kept;
            kept.reserve( rows_.size() - found.size() );
            for ( int row = 0; row < rows_.size(); ++row ) {
                if ( !found.contains( row ) ) {
                    kept.append( rows_.at( row ) );
                }
            }
            rows_ = std::move( kept );
            reindex();
            endResetModel();
            return true;
        }
        QList<int> positions( found.begin(), found.end() );
        std::sort( positions.begin(), positions.end() );
        // Remove contiguous ranges from the end so earlier positions stay valid.
        int end = static_cast<int>( positions.size() ) - 1;
        while ( end >= 0 ) {
            int start = end;
            while ( start > 0 && positions.at( start - 1 ) == positions.at( start ) - 1 ) {
                --start;
            }
            const int first = positions.at( start );
            const int last = positions.at( end );
            beginRemoveRows( {}, first, last );
            rows_.remove( first, last - first + 1 );
            endRemoveRows();
            end = start - 1;
        }
        reindex();
        return false;
    }
    int rowOf( const QString& id ) const
    {
        return index_.value( id, -1 );
    }

private:
    int cellLimit() const
    {
        return columns_.isEmpty() ? PluginView::MaxTableColumns
                                  : static_cast<int>( columns_.size() );
    }
    void reindex()
    {
        index_.clear();
        for ( int row = 0; row < rows_.size(); ++row ) {
            index_.insert( rows_.at( row ).id, row );
        }
    }
    QVector<Column> columns_;
    QVector<Row> rows_;
    QHash<QString, int> index_;
};

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

struct PluginView::Node {
    QString id;
    QString type;
    QString kind;
    bool automatic = false;
    QPointer<QWidget> widget; // the element itself (object name base)
    QPointer<QWidget> outer;  // what sits in the parent layout
    QPointer<QWidget> content;
    QPointer<QTimer> debounce;
    QJsonObject props;
    QJsonValue pluginValue;
    QJsonValue lastRows; // rows last set as a whole (root or replace_rows)
    bool rowsDirty = true;
    PluginTableModel* model = nullptr;
    QSortFilterProxyModel* proxy = nullptr;
};

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
}

PluginView::~PluginView()
{
    cancelDialogs();
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
                    + element.value( "id" ).toString().left( 128 ) + ':'
                    + element.value( "kind" ).toString().left( 64 );
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
            hasTable = hasTable || node->type == "table";
        }
        if ( !hasTable ) {
            layout->addStretch( 1 );
        }
        scroll_->setWidget( body );
        setProperty( "pluginShape", shapeOf( root ) );
    }
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
    else {
        outer = buildTable( *node, element, parent );
    }
    node->outer = outer;
    if ( !automatic ) {
        node->widget->setObjectName( nameFor( id ) );
        if ( node->widget->accessibleName().isEmpty() ) {
            node->widget->setAccessibleName( id );
        }
    }
    apply( *node, element, false );
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
    auto* row = new QWidget( parent );
    auto* layout = new QHBoxLayout( row );
    layout->setContentsMargins( 0, 0, 0, 0 );
    auto* caption = new QLabel( row );
    caption->setTextFormat( Qt::PlainText );
    caption->setObjectName( nameFor( id ) + "_label" );
    caption->setVisible( false );
    layout->addWidget( caption );
    node.content = caption;
    const auto changedSoon = [ this, id ] {
        const auto found = nodes_.value( id );
        if ( !applying_ && found && found->debounce ) {
            found->debounce->start( debounceMs_ );
        }
    };
    if ( kind == "multichoice" ) {
        // Skips the text-field debounce path on purpose: no typing, every toggle is final.
        auto* box = new CheckComboBox( row );
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
        connect( combo, qOverload<int>( &QComboBox::currentIndexChanged ), this, [ this, id ] {
            const auto found = nodes_.value( id );
            if ( !applying_ && found ) {
                emitEvent( id, "changed", fieldValue( *found ) );
            }
        } );
        layout->addWidget( combo, 1 );
        node.widget = combo;
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
        node.debounce = new QTimer( edit );
        node.debounce->setSingleShot( true );
        connect( node.debounce, &QTimer::timeout, this, [ this, id ] {
            if ( const auto found = nodes_.value( id ) ) {
                emitEvent( id, "changed", fieldValue( *found ) );
            }
        } );
        connect( edit, &QLineEdit::textChanged, this, changedSoon );
        layout->addWidget( edit, 1 );
        node.widget = edit;
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
    node.proxy = new QSortFilterProxyModel( table );
    node.proxy->setSourceModel( node.model );
    node.proxy->setFilterKeyColumn( -1 );
    node.proxy->setFilterCaseSensitivity( Qt::CaseInsensitive );
    node.proxy->setSortRole( Qt::UserRole );
    table->setModel( node.proxy );
    table->setEditTriggers( QAbstractItemView::NoEditTriggers );
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
        if ( !applying_ ) {
            emitSelection( id );
        }
    } );
    node.widget = table;
    return box;
}

void PluginView::emitSelection( const QString& id )
{
    const auto node = nodes_.value( id );
    auto* table = node ? qobject_cast<QTableView*>( node->widget.data() ) : nullptr;
    if ( !table ) {
        return;
    }
    // Bounded: at most MaxSelectedIds ids (and MaxInputsBytes of them); the count is complete.
    const auto rows = table->selectionModel()->selectedRows();
    QJsonArray ids;
    qsizetype bytes = 0;
    for ( const auto& index : rows ) {
        const auto rowId = index.data( Qt::UserRole + 1 ).toString();
        bytes += rowId.size() + 4;
        if ( ids.size() >= MaxSelectedIds || bytes > MaxInputsBytes ) {
            break;
        }
        ids.append( rowId );
    }
    QJsonObject extra{ { "selected_count", static_cast<int>( rows.size() ) } };
    if ( ids.size() < rows.size() ) {
        extra.insert( "truncated", true );
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
        // to their defaults (or are dropped), except value, rows and columns, which keep state.
        const auto defaults = defaultsFor( node.type );
        for ( auto it = previous.begin(); it != previous.end(); ++it ) {
            if ( props.contains( it.key() ) || KeptWhenAbsent.contains( it.key() ) ) {
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
        node.widget->setToolTip( text( props, "tooltip" ) );
    }
    const auto& type = node.type;
    if ( type == "group" ) {
        auto* group = qobject_cast<QGroupBox*>( node.widget.data() );
        if ( props.contains( "title" ) ) {
            group->setTitle( text( props, "title" ) );
            group->setAccessibleName( group->title().isEmpty() ? node.id : group->title() );
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
            }
            node.widget->setAccessibleName( label.isEmpty() ? node.id : label );
        }
        if ( auto* box = qobject_cast<QCheckBox*>( node.widget.data() );
             box && box->text().isEmpty() ) {
            box->setText( node.id );
        }
        if ( props.contains( "placeholder" ) ) {
            if ( auto* edit = qobject_cast<QLineEdit*>( node.widget.data() ) ) {
                edit->setPlaceholderText( text( props, "placeholder" ) );
            }
        }
        if ( props.contains( "choices" ) && !unchanged( "choices" ) ) {
            if ( auto* checks = qobject_cast<CheckComboBox*>( node.widget.data() ) ) {
                checks->setChoices( props.value( "choices" ).toArray() );
            }
            else if ( auto* combo = qobject_cast<QComboBox*>( node.widget.data() ) ) {
                const auto current = combo->currentData();
                combo->clear();
                for ( const auto& choice : props.value( "choices" ).toArray() ) {
                    if ( combo->count() >= MaxElements ) {
                        break;
                    }
                    const auto object = choice.toObject();
                    auto value = choice.isObject() ? object.value( "value" ) : choice;
                    if ( value.isString() ) {
                        value = value.toString().left( MaxFieldValueChars );
                    }
                    combo->addItem( object.contains( "label" ) ? text( object, "label" )
                                                               : cellText( value ),
                                    value.toVariant() );
                }
                combo->setCurrentIndex( qMax( 0, combo->findData( current ) ) );
            }
        }
        if ( props.contains( "value" )
             && ( !fromRoot || props.value( "value" ) != node.pluginValue ) ) {
            node.pluginValue = props.value( "value" );
            setFieldValue( node, node.pluginValue );
        }
    }
    else if ( type == "button" ) {
        auto* button = qobject_cast<QPushButton*>( node.widget.data() );
        if ( props.contains( "label" ) ) {
            button->setText( text( props, "label" ) );
            button->setAccessibleName( button->text().isEmpty() ? node.id : button->text() );
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
        if ( props.contains( "columns" ) && !unchanged( "columns" ) ) {
            const auto columns = props.value( "columns" ).toArray();
            if ( columns.size() > MaxTableColumns || !node.model->setColumns( columns ) ) {
                warnOnce( "columns:" + node.id, "table columns rejected for " + node.id );
            }
            else {
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
        if ( props.contains( "selection" ) ) {
            const auto mode = props.value( "selection" ).toString();
            table->setSelectionMode( mode == "single"  ? QAbstractItemView::SingleSelection
                                     : mode == "multi" ? QAbstractItemView::ExtendedSelection
                                                       : QAbstractItemView::NoSelection );
        }
    }
}

void PluginView::applyTableOperation( Node& node, const QJsonObject& entry )
{
    auto* table = qobject_cast<QTableView*>( node.widget.data() );
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
    QTimer::singleShot( 0, table, [ table = QPointer<QTableView>( table ), scroll ] {
        if ( table ) {
            table->verticalScrollBar()->setValue( scroll );
        }
    } );
    if ( restored < selected.size() ) {
        // The plugin removed selected rows: tell it what is still selected.
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
        if ( entry.value( "set" ).isObject() ) {
            auto set = entry.value( "set" ).toObject();
            if ( set.contains( "children" ) || set.contains( "kind" ) ) {
                warnOnce( "structure:" + id, "patches cannot change children or field kinds" );
                set.remove( "children" );
                set.remove( "kind" );
            }
            apply( *node, set, false );
        }
        if ( node->type == "table" ) {
            applyTableOperation( *node, entry );
        }
    }
    ++renders_;
}

QJsonValue PluginView::fieldValue( const Node& node ) const
{
    if ( auto* checks = qobject_cast<CheckComboBox*>( node.widget.data() ) ) {
        return checks->checkedValues();
    }
    if ( auto* box = qobject_cast<QCheckBox*>( node.widget.data() ) ) {
        return box->isChecked();
    }
    if ( auto* combo = qobject_cast<QComboBox*>( node.widget.data() ) ) {
        return QJsonValue::fromVariant( combo->currentData() );
    }
    auto* edit = qobject_cast<QLineEdit*>( node.widget.data() );
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
    if ( auto* checks = qobject_cast<CheckComboBox*>( node.widget.data() ) ) {
        checks->setCheckedValues( value.toArray() );
        return;
    }
    if ( auto* box = qobject_cast<QCheckBox*>( node.widget.data() ) ) {
        box->setChecked( value.toBool() );
    }
    else if ( auto* combo = qobject_cast<QComboBox*>( node.widget.data() ) ) {
        const auto index = combo->findData( value.toVariant() );
        if ( index >= 0 ) {
            combo->setCurrentIndex( index );
        }
    }
    else if ( auto* edit = qobject_cast<QLineEdit*>( node.widget.data() ) ) {
        const auto text = value.isDouble() ? QString::number( value.toDouble(), 'g', 15 )
                                           : cellText( value, MaxFieldValueChars );
        if ( edit->text() != text ) {
            edit->setText( text );
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
    for ( const auto& id : order_ ) {
        const auto node = nodes_.value( id );
        if ( !node || node->type != "field" ) {
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

void PluginView::emitEvent( const QString& id, const QString& type, const QJsonValue& value,
                            const QJsonObject& extra )
{
    if ( !isEnabled() ) {
        return; // a stopped or stale view never talks to the plugin
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
    Q_EMIT eventRaised( event );
}

void PluginView::pressButton( const QString& id )
{
    const auto node = nodes_.value( id );
    if ( !node || !isEnabled() ) {
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
