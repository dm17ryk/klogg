// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginfiletree.h"

#include "log.h"
#include "pluginfeatures.h"
#include "plugintablemodel.h"
#include "pluginview.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QStyle>
#include <QTreeView>

#include <algorithm>
#include <limits>
#include <utility>

namespace pluginview {
namespace {
constexpr int NameColumn = 0;
const QStringList FileColumns{ QStringLiteral( "name" ), QStringLiteral( "size" ),
                               QStringLiteral( "type" ), QStringLiteral( "modified" ) };

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity PathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity PathCase = Qt::CaseSensitive;
#endif

// Whether `path` is `root` or lies below it.
bool inside( const QString& root, const QString& path )
{
    if ( path.compare( root, PathCase ) == 0 ) {
        return true;
    }
    const auto prefix = root.endsWith( QLatin1Char( '/' ) ) ? root : root + QLatin1Char( '/' );
    return path.startsWith( prefix, PathCase );
}

// The style's folder, drive and file icons and a type from the suffix: the host never reads a
// file to find its icon or type (section 28.2). Built on the GUI thread; the model's gatherer
// thread only copies them.
class PlainFileIcons final : public QFileIconProvider {
public:
    PlainFileIcons()
    {
        auto* style = QApplication::style();
        folder_ = style->standardIcon( QStyle::SP_DirIcon );
        file_ = style->standardIcon( QStyle::SP_FileIcon );
        drive_ = style->standardIcon( QStyle::SP_DriveHDIcon );
    }
    QIcon icon( IconType type ) const override
    {
        switch ( type ) {
        case QFileIconProvider::Folder:
        case QFileIconProvider::Network:
            return folder_;
        case QFileIconProvider::Drive:
        case QFileIconProvider::Computer:
            return drive_;
        default:
            return file_;
        }
    }
    QIcon icon( const QFileInfo& info ) const override
    {
        return info.isRoot() ? drive_ : info.isDir() ? folder_ : file_;
    }
    QString type( const QFileInfo& info ) const override
    {
        if ( info.isDir() ) {
            return QObject::tr( "Folder" );
        }
        const auto suffix = info.suffix();
        return suffix.isEmpty() ? QObject::tr( "File" ) : suffix.toUpper();
    }

private:
    QIcon folder_;
    QIcon file_;
    QIcon drive_;
};

// The rows of each level from the top: sorts items in shown order.
QVector<int> rowPath( QModelIndex index )
{
    QVector<int> path;
    for ( ; index.isValid(); index = index.parent() ) {
        path.prepend( index.row() );
    }
    return path;
}
} // namespace

PluginFileRootsModel::PluginFileRootsModel( QObject* parent )
    : QAbstractProxyModel( parent )
{
}

QString PluginFileRootsModel::normalize( const QString& path )
{
    return QDir::cleanPath( QDir::fromNativeSeparators( path.trimmed() ) );
}

bool PluginFileRootsModel::isNetworkPath( const QString& path )
{
    return QDir::fromNativeSeparators( path.trimmed() ).startsWith( QStringLiteral( "//" ) );
}

QString PluginFileRootsModel::key( const QString& path ) const
{
    return PathCase == Qt::CaseInsensitive ? path.toLower() : path;
}

void PluginFileRootsModel::setFileModel( QFileSystemModel* model )
{
    files_ = model;
    setSourceModel( model );
    connectSource();
}

void PluginFileRootsModel::connectSource()
{
    connect( files_, &QAbstractItemModel::dataChanged, this,
             [ this ]( const QModelIndex& topLeft, const QModelIndex& bottomRight,
                       const QList<int>& roles ) {
                 if ( !topLeft.isValid() ) {
                     return;
                 }
                 // Roots whose own rows changed in their parent folder.
                 for ( int row = 0; row < roots_.size(); ++row ) {
                     const auto& root = roots_.at( row );
                     if ( root.source.isValid() && root.source.parent() == topLeft.parent()
                          && root.source.row() >= topLeft.row()
                          && root.source.row() <= bottomRight.row() ) {
                         Q_EMIT dataChanged( createIndex( row, topLeft.column() ),
                                             createIndex( row, bottomRight.column() ), roles );
                     }
                 }
                 bool mapped = false;
                 const auto parent = sourceParentMapped( topLeft.parent(), &mapped );
                 if ( mapped ) {
                     Q_EMIT dataChanged( index( topLeft.row(), topLeft.column(), parent ),
                                         index( bottomRight.row(), bottomRight.column(), parent ),
                                         roles );
                 }
             } );
    connect( files_, &QAbstractItemModel::rowsAboutToBeInserted, this,
             [ this ]( const QModelIndex& parent, int first, int last ) {
                 bool mapped = false;
                 const auto proxy = sourceParentMapped( parent, &mapped );
                 if ( mapped ) {
                     beginInsertRows( proxy, first, last );
                 }
                 pending_.append( mapped ? 1 : 0 );
             } );
    connect( files_, &QAbstractItemModel::rowsInserted, this, [ this ] {
        if ( !pending_.isEmpty() && pending_.takeLast() == 1 ) {
            endInsertRows();
        }
    } );
    connect( files_, &QAbstractItemModel::rowsAboutToBeRemoved, this,
             [ this ]( const QModelIndex& parent, int first, int last ) {
                 // A root or a folder above it goes away: the roots are looked at again after the
                 // removal.
                 for ( const auto& root : std::as_const( roots_ ) ) {
                     for ( QModelIndex at = root.source; at.isValid(); at = at.parent() ) {
                         if ( at.parent() == parent && at.row() >= first && at.row() <= last ) {
                             beginResetModel();
                             pending_.append( 2 );
                             return;
                         }
                     }
                 }
                 bool mapped = false;
                 const auto proxy = sourceParentMapped( parent, &mapped );
                 if ( mapped ) {
                     beginRemoveRows( proxy, first, last );
                 }
                 pending_.append( mapped ? 1 : 0 );
             } );
    connect( files_, &QAbstractItemModel::rowsRemoved, this, [ this ] {
        const auto state = pending_.isEmpty() ? 0 : pending_.takeLast();
        if ( state == 1 ) {
            endRemoveRows();
        }
        else if ( state == 2 ) {
            for ( auto& root : roots_ ) {
                root.missing = root.missing || ( !root.source.isValid() && !root.network );
            }
            rebuildRootPointers();
            endResetModel();
        }
    } );
    connect( files_, &QAbstractItemModel::layoutAboutToBeChanged, this, [ this ] {
        Q_EMIT layoutAboutToBeChanged();
        layoutProxy_.clear();
        layoutSource_.clear();
        for ( const auto& proxy : persistentIndexList() ) {
            layoutProxy_.append( proxy );
            layoutSource_.append( QPersistentModelIndex( mapToSource( proxy ) ) );
        }
    } );
    connect( files_, &QAbstractItemModel::layoutChanged, this, [ this ] {
        // The roots' source indexes may have moved: their pointers are looked up again.
        rebuildRootPointers();
        for ( qsizetype at = 0; at < layoutProxy_.size(); ++at ) {
            changePersistentIndex( layoutProxy_.at( at ), mapFromSource( layoutSource_.at( at ) ) );
        }
        layoutProxy_.clear();
        layoutSource_.clear();
        Q_EMIT layoutChanged();
    } );
    connect( files_, &QAbstractItemModel::modelAboutToBeReset, this,
             [ this ] { beginResetModel(); } );
    connect( files_, &QAbstractItemModel::modelReset, this, [ this ] {
        for ( auto& root : roots_ ) {
            if ( !root.network || root.source.isValid() ) {
                attachLocal( root );
            }
        }
        rebuildRootPointers();
        endResetModel();
    } );
}

void PluginFileRootsModel::setRoots( const QStringList& paths, QStringList* skipped )
{
    beginResetModel();
    roots_.clear();
    for ( const auto& raw : paths ) {
        const auto network = isNetworkPath( raw );
        const auto path = normalize( raw );
        if ( roots_.size() >= cilogg::plugins::MaxFileTreeRoots ) {
            skipped->append( QStringLiteral( "file tree roots past %1 skipped: %2" )
                                 .arg( cilogg::plugins::MaxFileTreeRoots )
                                 .arg( raw.left( 256 ) ) );
            continue;
        }
        if ( path.isEmpty() || ( !network && !QDir::isAbsolutePath( path ) ) ) {
            skipped->append( QStringLiteral( "file tree root is not absolute: " ) + raw.left( 256 ) );
            continue;
        }
        const bool clash
            = std::any_of( roots_.cbegin(), roots_.cend(), [ &path ]( const Root& root ) {
                  return inside( root.path, path ) || inside( path, root.path );
              } );
        if ( clash ) {
            skipped->append( QStringLiteral( "repeated or nested file tree root skipped: " )
                             + raw.left( 256 ) );
            continue;
        }
        Root root;
        root.path = path;
        root.network = network;
        if ( !network ) {
            attachLocal( root ); // a network root waits for the user (section 28.2)
        }
        roots_.append( root );
    }
    rebuildRootPointers();
    endResetModel();
}

QStringList PluginFileRootsModel::roots() const
{
    QStringList paths;
    for ( const auto& root : roots_ ) {
        paths.append( root.path );
    }
    return paths;
}

bool PluginFileRootsModel::attachLocal( Root& root )
{
    root.source = QPersistentModelIndex();
    if ( !files_ || !QFileInfo::exists( root.path ) ) {
        root.missing = true;
        return false;
    }
    const auto source = files_->index( root.path );
    root.missing = !source.isValid();
    if ( source.isValid() ) {
        root.source = QPersistentModelIndex( source.sibling( source.row(), 0 ) );
    }
    return source.isValid();
}

bool PluginFileRootsModel::attach( int row )
{
    if ( row < 0 || row >= roots_.size() || !files_ ) {
        return false;
    }
    if ( roots_.at( row ).source.isValid() ) {
        return true;
    }
    auto probe = roots_.at( row );
    const auto self = createIndex( row, NameColumn );
    if ( !attachLocal( probe ) ) {
        roots_[ row ].missing = true;
        Q_EMIT dataChanged( self, createIndex( row, columnCount() - 1 ) );
        LOG_DEBUG << "Plugin file tree root not reachable: " << probe.path.toStdString();
        return false;
    }
    const auto count = files_->rowCount( probe.source );
    if ( count > 0 ) {
        beginInsertRows( self, 0, count - 1 );
    }
    roots_[ row ] = probe;
    rebuildRootPointers();
    if ( count > 0 ) {
        endInsertRows();
    }
    if ( files_->canFetchMore( probe.source ) ) {
        files_->fetchMore( probe.source );
    }
    Q_EMIT dataChanged( self, createIndex( row, columnCount() - 1 ) );
    return true;
}

bool PluginFileRootsModel::isAttached( int row ) const
{
    return row >= 0 && row < roots_.size() && roots_.at( row ).source.isValid();
}

bool PluginFileRootsModel::isRoot( const QModelIndex& index ) const
{
    // A stale root index past the roots is no root (and maps to nothing).
    return index.isValid() && index.internalPointer() == nullptr && index.row() < roots_.size();
}

void PluginFileRootsModel::rebuildRootPointers()
{
    rootPointers_.clear();
    for ( int row = 0; row < roots_.size(); ++row ) {
        if ( roots_.at( row ).source.isValid() ) {
            rootPointers_.insert( roots_.at( row ).source.internalPointer(), row );
        }
    }
}

int PluginFileRootsModel::rootOf( const QModelIndex& source ) const
{
    for ( auto at = source.sibling( source.row(), 0 ); at.isValid(); at = at.parent() ) {
        const auto found = rootPointers_.constFind( at.internalPointer() );
        if ( found != rootPointers_.cend() ) {
            return found.value();
        }
    }
    return -1;
}

int PluginFileRootsModel::rootOfPath( const QString& path ) const
{
    for ( int row = 0; row < roots_.size(); ++row ) {
        if ( inside( roots_.at( row ).path, path ) ) {
            return row;
        }
    }
    return -1;
}

bool PluginFileRootsModel::withinRoots( const QString& path ) const
{
    return rootOfPath( normalize( path ) ) >= 0;
}

QModelIndex PluginFileRootsModel::sourceParentMapped( const QModelIndex& sourceParent,
                                                      bool* mapped ) const
{
    *mapped = false;
    if ( !sourceParent.isValid() ) {
        return {};
    }
    const auto proxy = mapFromSource( sourceParent.sibling( sourceParent.row(), 0 ) );
    *mapped = proxy.isValid();
    return proxy;
}

QModelIndex PluginFileRootsModel::mapFromSource( const QModelIndex& sourceIndex ) const
{
    if ( !sourceIndex.isValid() || !files_ ) {
        return {};
    }
    auto* pointer = sourceIndex.internalPointer();
    if ( const auto root = rootPointers_.constFind( pointer ); root != rootPointers_.cend() ) {
        return createIndex( root.value(), sourceIndex.column() );
    }
    if ( rootOf( sourceIndex ) < 0 ) {
        return {}; // outside the roots
    }
    // Below the roots the proxy mirrors the native model: same rows, same internal pointers, so
    // nothing is kept per item.
    return createIndex( sourceIndex.row(), sourceIndex.column(), pointer );
}

QModelIndex PluginFileRootsModel::mapToSource( const QModelIndex& proxyIndex ) const
{
    if ( !proxyIndex.isValid() || !files_ ) {
        return {};
    }
    if ( proxyIndex.internalPointer() == nullptr ) {
        if ( proxyIndex.row() >= roots_.size() ) {
            return {};
        }
        const auto& source = roots_.at( proxyIndex.row() ).source;
        return source.isValid() ? source.sibling( source.row(), proxyIndex.column() )
                                : QModelIndex{};
    }
    return createSourceIndex( proxyIndex.row(), proxyIndex.column(), proxyIndex.internalPointer() );
}

QModelIndex PluginFileRootsModel::index( int row, int column, const QModelIndex& parent ) const
{
    if ( row < 0 || column < 0 || column >= columnCount() || !files_ ) {
        return {};
    }
    if ( !parent.isValid() ) {
        return row < roots_.size() ? createIndex( row, column ) : QModelIndex{};
    }
    if ( parent.column() > 0 ) {
        return {};
    }
    const auto sourceParent = mapToSource( parent );
    if ( !sourceParent.isValid() ) {
        return {};
    }
    const auto source = files_->index( row, column, sourceParent );
    return source.isValid() ? createIndex( row, column, source.internalPointer() ) : QModelIndex{};
}

QModelIndex PluginFileRootsModel::parent( const QModelIndex& child ) const
{
    if ( !child.isValid() || child.internalPointer() == nullptr ) {
        return {};
    }
    const auto source = mapToSource( child );
    if ( !source.isValid() ) {
        return {};
    }
    const auto sourceParent = source.parent();
    if ( !sourceParent.isValid() ) {
        return {};
    }
    return mapFromSource( sourceParent );
}

int PluginFileRootsModel::rowCount( const QModelIndex& parent ) const
{
    if ( !parent.isValid() ) {
        return static_cast<int>( roots_.size() );
    }
    if ( parent.column() > 0 || !files_ ) {
        return 0;
    }
    const auto source = mapToSource( parent );
    return source.isValid() ? files_->rowCount( source ) : 0;
}

int PluginFileRootsModel::columnCount( const QModelIndex& parent ) const
{
    Q_UNUSED( parent );
    return static_cast<int>( FileColumns.size() );
}

bool PluginFileRootsModel::hasChildren( const QModelIndex& parent ) const
{
    if ( !parent.isValid() ) {
        return !roots_.isEmpty();
    }
    if ( parent.column() > 0 || !files_ ) {
        return false;
    }
    if ( isRoot( parent ) ) {
        const auto& root = roots_.at( parent.row() );
        // A network root shows its expander until the user opens it.
        return root.source.isValid() ? files_->hasChildren( root.source )
                                     : root.network && !root.missing;
    }
    const auto source = mapToSource( parent );
    return source.isValid() && files_->hasChildren( source );
}

bool PluginFileRootsModel::canFetchMore( const QModelIndex& parent ) const
{
    const auto source = mapToSource( parent );
    return source.isValid() && files_->canFetchMore( source );
}

void PluginFileRootsModel::fetchMore( const QModelIndex& parent )
{
    const auto source = mapToSource( parent );
    if ( source.isValid() ) {
        files_->fetchMore( source );
    }
}

QString PluginFileRootsModel::pathOf( const QModelIndex& index ) const
{
    if ( !index.isValid() || !files_ ) {
        return {};
    }
    if ( isRoot( index ) ) {
        return roots_.value( index.row() ).path;
    }
    // Never the native model's root ("") for an index that maps to nothing.
    const auto source = mapToSource( index );
    return source.isValid() ? files_->filePath( source ) : QString{};
}

bool PluginFileRootsModel::isDir( const QModelIndex& index ) const
{
    if ( isRoot( index ) ) {
        return true;
    }
    const auto source = mapToSource( index );
    return source.isValid() && files_->isDir( source );
}

QModelIndex PluginFileRootsModel::indexOfPath( const QString& path )
{
    const auto normalized = normalize( path );
    const auto row = rootOfPath( normalized );
    if ( row < 0 || !files_ ) {
        return {};
    }
    // Revealing a path in a network root is an explicit request: it may attach it. A root that
    // could not be reached is not tried again here (one path after another would stall the window
    // on a dead server): only the user's expansion tries again.
    const bool attached = roots_.at( row ).source.isValid();
    const bool unreachable = roots_.at( row ).network && roots_.at( row ).missing;
    if ( !attached && ( unreachable || !attach( row ) ) ) {
        return {};
    }
    if ( normalized.compare( roots_.at( row ).path, PathCase ) == 0 ) {
        return createIndex( row, NameColumn );
    }
    // A missing path maps to the native model's root, which is no item here.
    return mapFromSource( files_->index( normalized ) );
}

void PluginFileRootsModel::setSelectable( bool files, bool dirs )
{
    if ( selectFiles_ == files && selectDirs_ == dirs ) {
        return;
    }
    beginResetModel(); // flags change everywhere
    selectFiles_ = files;
    selectDirs_ = dirs;
    endResetModel();
}

void PluginFileRootsModel::setCheckboxes( bool checkboxes )
{
    if ( checkboxes_ == checkboxes ) {
        return;
    }
    beginResetModel();
    checkboxes_ = checkboxes;
    endResetModel();
}

bool PluginFileRootsModel::isChecked( const QString& path ) const
{
    return checkedKeys_.contains( key( path ) );
}

void PluginFileRootsModel::setCheckedPaths( const QStringList& paths )
{
    QStringList kept;
    QSet<QString> keys;
    for ( const auto& path : paths ) {
        const auto normalized = normalize( path );
        if ( !normalized.isEmpty() && !keys.contains( key( normalized ) ) ) {
            keys.insert( key( normalized ) );
            kept.append( normalized );
        }
    }
    auto touched = checked_;
    touched.append( kept );
    checked_ = kept;
    checkedKeys_ = keys;
    // Repaint the boxes of loaded items only; nothing is fetched for this.
    for ( const auto& path : std::as_const( touched ) ) {
        const auto row = rootOfPath( path );
        if ( row < 0 || !roots_.at( row ).source.isValid() || !files_ ) {
            continue;
        }
        const auto proxy = path.compare( roots_.at( row ).path, PathCase ) == 0
                               ? createIndex( row, NameColumn )
                               : mapFromSource( files_->index( path ) );
        if ( proxy.isValid() ) {
            Q_EMIT dataChanged( proxy, proxy, { Qt::CheckStateRole } );
        }
    }
}

QVariant PluginFileRootsModel::data( const QModelIndex& index, int role ) const
{
    if ( !index.isValid() || !files_ ) {
        return {};
    }
    if ( role == Qt::CheckStateRole ) {
        const auto checkable = index.column() == NameColumn
                               && flags( index ).testFlag( Qt::ItemIsUserCheckable );
        return checkable ? QVariant( isChecked( pathOf( index ) ) ? Qt::Checked : Qt::Unchecked )
                         : QVariant{};
    }
    if ( isRoot( index ) ) {
        const auto& root = roots_.at( index.row() );
        if ( index.column() == NameColumn ) {
            switch ( role ) {
            case Qt::DisplayRole:
            case Qt::ToolTipRole:
                return QDir::toNativeSeparators( root.path );
            case Qt::DecorationRole:
                return root.source.isValid() ? files_->data( root.source, role )
                                             : QVariant( files_->iconProvider()->icon(
                                                 QAbstractFileIconProvider::Folder ) );
            case Qt::ForegroundRole:
                return root.missing ? QVariant( toneColor( QStringLiteral( "muted" ) ) )
                                    : QVariant{};
            default:
                break;
            }
        }
        return root.source.isValid()
                   ? files_->data( root.source.sibling( root.source.row(), index.column() ), role )
                   : QVariant{};
    }
    const auto source = mapToSource( index );
    if ( !source.isValid() ) {
        return {};
    }
    if ( role == Qt::ToolTipRole && index.column() == NameColumn ) {
        return QDir::toNativeSeparators( files_->filePath( source ) );
    }
    return files_->data( source, role );
}

QVariant PluginFileRootsModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
    return files_ ? files_->headerData( section, orientation, role ) : QVariant{};
}

Qt::ItemFlags PluginFileRootsModel::flags( const QModelIndex& index ) const
{
    if ( !index.isValid() || !files_ ) {
        return Qt::NoItemFlags;
    }
    if ( isRoot( index ) ) {
        const auto& root = roots_.at( index.row() );
        Qt::ItemFlags flags = Qt::ItemIsEnabled;
        if ( !root.missing && selectDirs_ ) {
            flags |= Qt::ItemIsSelectable;
            if ( checkboxes_ && index.column() == NameColumn ) {
                flags |= Qt::ItemIsUserCheckable;
            }
        }
        return flags;
    }
    const auto source = mapToSource( index );
    if ( !source.isValid() ) {
        return Qt::NoItemFlags;
    }
    auto flags = files_->flags( source )
                 & ~( Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled );
    if ( !flags.testFlag( Qt::ItemIsEnabled ) ) {
        return flags & ~Qt::ItemIsSelectable; // a file greyed out by the filters
    }
    if ( !( files_->isDir( source ) ? selectDirs_ : selectFiles_ ) ) {
        return flags & ~Qt::ItemIsSelectable;
    }
    if ( checkboxes_ && index.column() == NameColumn ) {
        flags |= Qt::ItemIsUserCheckable;
    }
    return flags;
}

bool PluginFileRootsModel::setData( const QModelIndex& index, const QVariant& value, int role )
{
    if ( role != Qt::CheckStateRole || index.column() != NameColumn
         || !flags( index ).testFlag( Qt::ItemIsUserCheckable ) || ( canEdit && !canEdit() ) ) {
        return false;
    }
    const auto path = pathOf( index );
    const bool checked = value.toInt() == Qt::Checked;
    if ( isChecked( path ) == checked ) {
        return false;
    }
    if ( onCheckRequested ) {
        onCheckRequested( path, checked );
        return true;
    }
    auto paths = checked_;
    if ( checked ) {
        paths.append( path );
    }
    else {
        paths.removeIf( [ & ]( const QString& kept ) { return key( kept ) == key( path ); } );
    }
    setCheckedPaths( paths );
    return true;
}
} // namespace pluginview

// PluginFileTreeBehaviour

using pluginview::PluginFileRootsModel;

PluginFileTreeBehaviour::PluginFileTreeBehaviour( QTreeView* tree, Hooks hooks )
    : QObject( tree )
    , tree_( tree )
    , hooks_( std::move( hooks ) )
{
    icons_ = std::make_unique<pluginview::PlainFileIcons>();
    files_ = new QFileSystemModel( this );
    files_->setIconProvider( icons_.get() );
    files_->setReadOnly( true );
    files_->setResolveSymlinks( false ); // a shortcut is never read to resolve it
    files_->setOption( QFileSystemModel::DontUseCustomDirectoryIcons, true );
    // The native model loads nothing until a root path is set.
    files_->setRootPath( QString() );
    roots_ = new PluginFileRootsModel( this );
    roots_->setFileModel( files_ );
    roots_->canEdit = [ this ] { return !hooks_.canAct || hooks_.canAct(); };
    roots_->onCheckRequested = [ this ]( const QString& path, bool checked ) {
        requestCheck( path, checked );
    };
    tree->setModel( roots_ );
    tree->setUniformRowHeights( true );
    tree->setSelectionBehavior( QAbstractItemView::SelectRows );
    tree->setSortingEnabled( false );
    tree->setEditTriggers( QAbstractItemView::NoEditTriggers );
    tree->header()->setStretchLastSection( false );
    tree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
    tree->viewport()->installEventFilter( this );
    tree->installEventFilter( this );
    tree->setContextMenuPolicy( Qt::CustomContextMenu );
    connect( tree, &QTreeView::customContextMenuRequested, this, [ this ]( const QPoint& at ) {
        const QPointer<QMenu> menu( buildMenu( at ) );
        if ( menu ) {
            menu->exec( tree_->viewport()->mapToGlobal( at ) );
        }
        if ( menu ) {
            menu->deleteLater();
        }
    } );
    connect( tree, &QTreeView::doubleClicked, this,
             [ this ]( const QModelIndex& index ) { activate( index ); } );
    // A network root is reached only when the user opens it.
    connect( tree, &QTreeView::expanded, this, [ this ]( const QModelIndex& index ) {
        if ( roots_->isRoot( index ) && !roots_->isAttached( index.row() ) ) {
            roots_->attach( index.row() );
        }
    } );
    connect( tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, [ this ] {
        if ( !options_.checkboxes && ( !hooks_.canAct || hooks_.canAct() ) ) {
            emitSelected();
        }
    } );
}

PluginFileTreeBehaviour::~PluginFileTreeBehaviour()
{
    // The model's gatherer thread uses the icon provider: the models go first. When the tree
    // deletes this behaviour (its child) it is a QWidget only by now: it is never called, only cut
    // off from the models' signals.
    if ( auto* tree = qobject_cast<QTreeView*>( static_cast<QObject*>( tree_.data() ) ) ) {
        tree->setModel( nullptr );
    }
    else if ( tree_ ) {
        QObject::disconnect( roots_, nullptr, static_cast<QObject*>( tree_.data() ), nullptr );
    }
    delete roots_;
    delete files_;
}

void PluginFileTreeBehaviour::setOptions( const Options& options )
{
    const auto previous = std::exchange( options_, options );
    const bool first = !std::exchange( configured_, true );
    if ( first || previous.showFiles != options.showFiles
         || previous.showHidden != options.showHidden ) {
        QDir::Filters filter = QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives;
        if ( options.showFiles ) {
            filter |= QDir::Files;
        }
        if ( options.showHidden ) {
            filter |= QDir::Hidden | QDir::System;
        }
        files_->setFilter( filter );
    }
    if ( first || previous.filters != options.filters ) {
        files_->setNameFilters( options.filters );
    }
    if ( first || previous.hideFiltered != options.hideFiltered ) {
        files_->setNameFilterDisables( !options.hideFiltered );
    }
    roots_->setSelectable( options.select != QLatin1String( "dirs" ),
                           options.select != QLatin1String( "files" ) );
    roots_->setCheckboxes( options.checkboxes );
    if ( first || previous.roots != options.roots ) {
        QStringList skipped;
        roots_->setRoots( options.roots, &skipped );
        for ( const auto& problem : std::as_const( skipped ) ) {
            if ( hooks_.warn ) {
                hooks_.warn( QStringLiteral( "file-root:" ) + problem.left( 128 ), problem );
            }
        }
    }
    applyColumns();
    applySelectionMode();
}

void PluginFileTreeBehaviour::applyColumns()
{
    if ( !tree_ ) {
        return;
    }
    QStringList wanted{ QStringLiteral( "name" ) }; // the tree column always comes first
    for ( const auto& name : std::as_const( options_.columns ) ) {
        if ( pluginview::FileColumns.contains( name ) && !wanted.contains( name ) ) {
            wanted.append( name );
        }
    }
    auto* header = tree_->header();
    for ( int logical = 0; logical < pluginview::FileColumns.size(); ++logical ) {
        header->setSectionHidden( logical, !wanted.contains( pluginview::FileColumns.at( logical ) ) );
    }
    for ( int position = 0; position < wanted.size(); ++position ) {
        const auto logical = static_cast<int>( pluginview::FileColumns.indexOf( wanted.at( position ) ) );
        header->moveSection( header->visualIndex( logical ), position );
    }
    header->setVisible( wanted.size() > 1 );
}

void PluginFileTreeBehaviour::applySelectionMode()
{
    if ( !tree_ ) {
        return;
    }
    // With check boxes, the boxes are the selection; the highlight marks the current item.
    const auto mode = options_.checkboxes ? QAbstractItemView::SingleSelection
                      : options_.selection == QLatin1String( "none" )
                          ? QAbstractItemView::NoSelection
                      : options_.selection == QLatin1String( "multi" )
                          ? ( options_.toggleSelection ? QAbstractItemView::MultiSelection
                                                       : QAbstractItemView::ExtendedSelection )
                          : QAbstractItemView::SingleSelection;
    if ( tree_->selectionMode() != mode ) {
        tree_->setSelectionMode( mode );
    }
}

QString PluginFileTreeBehaviour::pathAt( const QModelIndex& index ) const
{
    return roots_->pathOf( index.sibling( index.row(), 0 ) );
}

QStringList PluginFileTreeBehaviour::value( int maximum, int* count ) const
{
    QStringList paths;
    if ( options_.checkboxes ) {
        paths = roots_->checkedPaths();
    }
    else if ( tree_ ) {
        auto rows = tree_->selectionModel()->selectedRows();
        std::vector<std::pair<QVector<int>, QString>> ordered;
        for ( const auto& index : std::as_const( rows ) ) {
            ordered.emplace_back( pluginview::rowPath( index ), pathAt( index ) );
        }
        std::sort( ordered.begin(), ordered.end() );
        for ( const auto& entry : ordered ) {
            paths.append( entry.second );
        }
    }
    if ( count != nullptr ) {
        *count = static_cast<int>( paths.size() );
    }
    return paths.mid( 0, maximum );
}

void PluginFileTreeBehaviour::reveal( const QModelIndex& index )
{
    if ( !tree_ || !index.isValid() ) {
        return;
    }
    QList<QModelIndex> ancestors;
    for ( auto parent = index.parent(); parent.isValid(); parent = parent.parent() ) {
        ancestors.prepend( parent );
    }
    for ( const auto& ancestor : std::as_const( ancestors ) ) {
        tree_->setExpanded( ancestor, true );
    }
    tree_->scrollTo( index );
}

void PluginFileTreeBehaviour::setValue( const QStringList& paths )
{
    QStringList kept;
    for ( const auto& path : paths ) {
        const auto normalized = PluginFileRootsModel::normalize( path );
        if ( roots_->withinRoots( normalized ) && !kept.contains( normalized ) ) {
            kept.append( normalized );
        }
    }
    if ( options_.checkboxes ) {
        roots_->setCheckedPaths( kept );
        if ( !kept.isEmpty() ) {
            reveal( roots_->indexOfPath( kept.first() ) );
        }
        return;
    }
    if ( !tree_ || tree_->selectionMode() == QAbstractItemView::NoSelection ) {
        return;
    }
    QItemSelection selection;
    QModelIndex first;
    for ( const auto& path : std::as_const( kept ) ) {
        const auto index = roots_->indexOfPath( path );
        if ( !index.isValid() || !roots_->flags( index ).testFlag( Qt::ItemIsSelectable ) ) {
            continue;
        }
        selection.select( index, index );
        if ( !first.isValid() ) {
            first = index;
        }
        if ( tree_->selectionMode() == QAbstractItemView::SingleSelection ) {
            break;
        }
    }
    tree_->selectionModel()->select( selection, QItemSelectionModel::ClearAndSelect
                                                    | QItemSelectionModel::Rows );
    if ( first.isValid() ) {
        tree_->selectionModel()->setCurrentIndex( first, QItemSelectionModel::NoUpdate );
        reveal( first );
    }
}

void PluginFileTreeBehaviour::expandPaths( const QStringList& paths )
{
    if ( !tree_ ) {
        return;
    }
    for ( const auto& path : paths ) {
        const auto index = roots_->indexOfPath( path );
        if ( index.isValid() && roots_->isDir( index ) ) {
            reveal( index );
            tree_->setExpanded( index, true );
        }
    }
}

void PluginFileTreeBehaviour::emitSelected()
{
    if ( !hooks_.emitEvent ) {
        return;
    }
    // At most 1,000 paths and 256 KiB of them; the count is complete.
    int count = 0;
    const auto all = value( pluginview::MaxTableIds, &count );
    QJsonArray paths;
    qsizetype bytes = 0;
    for ( const auto& path : all ) {
        bytes += path.size() + 4;
        if ( bytes > PluginView::MaxInputsBytes ) {
            break;
        }
        paths.append( path );
    }
    QJsonObject extra{ { "selected_count", count } };
    if ( paths.size() < count ) {
        extra.insert( "truncated", true );
    }
    hooks_.emitEvent( QStringLiteral( "selected" ), paths, extra );
}

void PluginFileTreeBehaviour::requestCheck( const QString& path, bool checked )
{
    const auto previous = roots_->checkedPaths();
    auto paths = previous;
    if ( checked ) {
        paths.append( path );
    }
    else {
        paths.removeIf( [ & ]( const QString& kept ) {
            return PluginFileRootsModel::normalize( kept ).compare( path, pluginview::PathCase ) == 0;
        } );
    }
    roots_->setCheckedPaths( paths );
    if ( !hooks_.emitEvent ) {
        return;
    }
    int count = 0;
    const auto shown = pluginview::boundedIds( value( pluginview::MaxTableIds, &count ) );
    QJsonObject extra{ { "selected_count", count } };
    if ( shown.size() < count ) {
        extra.insert( "truncated", true );
    }
    if ( !hooks_.emitEvent( QStringLiteral( "selected" ), QJsonArray::fromStringList( shown ),
                            extra ) ) {
        roots_->setCheckedPaths( previous ); // the plugin did not get it
    }
}

void PluginFileTreeBehaviour::activate( const QModelIndex& index )
{
    if ( !index.isValid() || !hooks_.emitEvent || ( hooks_.canAct && !hooks_.canAct() ) ) {
        return;
    }
    const auto first = index.sibling( index.row(), 0 );
    // A folder opens (the tree's own double-click); a greyed file does nothing.
    if ( roots_->isDir( first ) || !roots_->flags( first ).testFlag( Qt::ItemIsEnabled ) ) {
        return;
    }
    hooks_.emitEvent( QStringLiteral( "activated" ), QJsonObject{ { "path", pathAt( first ) } },
                      {} );
}

QJsonObject PluginFileTreeBehaviour::menuTarget( const QString& path ) const
{
    int count = 0;
    const auto paths = pluginview::boundedIds( value( pluginview::MaxTableIds, &count ) );
    return { { "node_id", path.isEmpty() ? QJsonValue() : QJsonValue( path ) },
             { "selected_ids", QJsonArray::fromStringList( paths ) },
             { "selected_count", count } };
}

QString PluginFileTreeBehaviour::copyText() const
{
    auto paths = value( std::numeric_limits<int>::max() );
    if ( paths.isEmpty() && tree_ && tree_->currentIndex().isValid() ) {
        paths.append( pathAt( tree_->currentIndex() ) );
    }
    // Whole paths only, at most MaxCopyChars (as tables and trees).
    QString text;
    for ( const auto& path : std::as_const( paths ) ) {
        const auto shown = QDir::toNativeSeparators( path );
        if ( text.size() + shown.size() + ( text.isEmpty() ? 0 : 1 ) > pluginview::MaxCopyChars ) {
            LOG_WARNING << "Plugin file tree copy cut at " << pluginview::MaxCopyChars
                        << " characters (whole paths)";
            break;
        }
        if ( !text.isEmpty() ) {
            text += QLatin1Char( '\n' );
        }
        text += shown;
    }
    return text;
}

QMenu* PluginFileTreeBehaviour::buildMenu( const QPoint& position )
{
    if ( !tree_ ) {
        return nullptr;
    }
    const auto index = tree_->indexAt( position );
    const auto path = index.isValid() ? pathAt( index ) : QString{};
    if ( index.isValid() && !options_.checkboxes
         && tree_->selectionMode() != QAbstractItemView::NoSelection
         && roots_->flags( index ).testFlag( Qt::ItemIsSelectable ) ) {
        // A right-click makes the item current, and selects it when it was not selected.
        auto* selection = tree_->selectionModel();
        const auto first = index.sibling( index.row(), 0 );
        selection->setCurrentIndex( first, QItemSelectionModel::NoUpdate );
        if ( !selection->isRowSelected( first.row(), first.parent() ) ) {
            selection->select( first, QItemSelectionModel::ClearAndSelect
                                          | QItemSelectionModel::Rows );
        }
    }
    auto* menu = new QMenu( tree_ );
    menu->setObjectName( tree_->objectName() + QStringLiteral( "_menu" ) );
    menu->setToolTipsVisible( true );
    if ( options_.copy ) {
        auto* copy = menu->addAction( tr( "Copy Path" ) );
        copy->setObjectName( tree_->objectName() + QStringLiteral( "_copy" ) );
        copy->setShortcut( QKeySequence::Copy );
        copy->setEnabled( !copyText().isEmpty() );
        connect( copy, &QAction::triggered, this,
                 [ this ] { QApplication::clipboard()->setText( copyText() ); } );
    }
    if ( !options_.contextMenu.isEmpty() && hooks_.elementMenu ) {
        hooks_.elementMenu( menu, options_.contextMenu, menuTarget( path ) );
    }
    if ( menu->isEmpty() ) {
        delete menu;
        return nullptr;
    }
    return menu;
}

bool PluginFileTreeBehaviour::eventFilter( QObject* watched, QEvent* event )
{
    if ( !tree_ ) {
        return false;
    }
    if ( watched == tree_ && event->type() == QEvent::KeyPress ) {
        const auto* key = static_cast<QKeyEvent*>( event );
        if ( options_.copy && key->matches( QKeySequence::Copy ) ) {
            const auto text = copyText();
            if ( text.isEmpty() ) {
                return false;
            }
            QApplication::clipboard()->setText( text );
            return true;
        }
        if ( key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ) {
            const auto current = tree_->currentIndex();
            if ( current.isValid() && roots_->isDir( current.sibling( current.row(), 0 ) ) ) {
                tree_->setExpanded( current.sibling( current.row(), 0 ),
                                    !tree_->isExpanded( current.sibling( current.row(), 0 ) ) );
                return true;
            }
            activate( current );
        }
        return false;
    }
    if ( watched != tree_->viewport() || event->type() != QEvent::MouseButtonPress ) {
        return false;
    }
    const auto* mouse = static_cast<QMouseEvent*>( event );
    const auto index = tree_->indexAt( mouse->position().toPoint() );
    if ( mouse->button() != Qt::LeftButton || !index.isValid() ) {
        return false;
    }
    const auto path = pathAt( index );
    // Toggle selection: a Shift+click adds the shown items from the last clicked one.
    if ( tree_->selectionMode() == QAbstractItemView::MultiSelection
         && ( mouse->modifiers() & Qt::ShiftModifier ) && !selectionAnchor_.isEmpty() ) {
        const auto anchor = roots_->indexOfPath( selectionAnchor_ );
        if ( anchor.isValid() ) {
            const bool down = tree_->visualRect( anchor ).top() <= tree_->visualRect( index ).top();
            auto from = down ? anchor : index.sibling( index.row(), 0 );
            const auto to = down ? index.sibling( index.row(), 0 ) : anchor;
            QItemSelection range;
            for ( int guard = 0; from.isValid() && guard <= pluginview::MaxTableIds * 50; ++guard ) {
                if ( roots_->flags( from ).testFlag( Qt::ItemIsSelectable ) ) {
                    range.select( from, from );
                }
                if ( from == to ) {
                    break;
                }
                from = tree_->indexBelow( from );
            }
            tree_->selectionModel()->setCurrentIndex( index, QItemSelectionModel::NoUpdate );
            tree_->selectionModel()->select( range, QItemSelectionModel::Select
                                                        | QItemSelectionModel::Rows );
            return true;
        }
    }
    selectionAnchor_ = path;
    return false;
}
