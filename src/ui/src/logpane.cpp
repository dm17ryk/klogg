/*
 * Copyright (C) 2026 CILogg contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "logpane.h"

#include <QApplication>
#include <QFocusEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScopeGuard>
#include <QSet>
#include <QSplitter>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <optional>

#include "crawlerwidget.h"
#include "log.h"
#include "streamsession.h"

namespace {
constexpr int ActiveFrameWidth = 2;
constexpr int DefaultSplitterWeight = 1000;
constexpr int MaxPaneIdLength = 64;
constexpr int MaxSplitterSize = 1000000;

const QString PaneType = QStringLiteral( "pane" );
const QString SplitType = QStringLiteral( "split" );
const QString Horizontal = QStringLiteral( "horizontal" );
const QString Vertical = QStringLiteral( "vertical" );

std::optional<int> jsonInt( const QJsonValue& value, int minimum, int maximum )
{
    if ( !value.isDouble() ) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if ( std::floor( number ) != number || number < minimum || number > maximum ) {
        return std::nullopt;
    }
    return static_cast<int>( number );
}

struct ParseContext {
    int fileCount = 0;
    int paneCount = 0;
    QSet<int> files;
    QSet<QString> paneIds;
    QString error;
};

bool parseNode( const QJsonValue& value, int depth, ParseContext& context, PaneLayoutNode* node )
{
    const auto fail = [ &context ]( const QString& message ) {
        context.error = message;
        return false;
    };

    if ( !value.isObject() ) {
        return fail( QStringLiteral( "a layout node is not an object" ) );
    }
    if ( depth > PaneLayoutFormat::MaxPanes ) {
        return fail( QStringLiteral( "the layout is nested too deeply" ) );
    }

    const auto object = value.toObject();
    const auto type = object.value( QStringLiteral( "type" ) ).toString();
    if ( type == PaneType ) {
        if ( ++context.paneCount > PaneLayoutFormat::MaxPanes ) {
            return fail( QStringLiteral( "the layout has more than %1 panes" )
                             .arg( PaneLayoutFormat::MaxPanes ) );
        }
        node->isPane = true;
        node->paneId = object.value( QStringLiteral( "id" ) ).toString();
        if ( node->paneId.trimmed().isEmpty() || node->paneId.size() > MaxPaneIdLength
             || context.paneIds.contains( node->paneId ) ) {
            return fail( QStringLiteral( "a pane has a missing or duplicate id" ) );
        }
        context.paneIds.insert( node->paneId );

        const auto files = object.value( QStringLiteral( "files" ) );
        if ( !files.isUndefined() && !files.isArray() ) {
            return fail( QStringLiteral( "pane files are not a list" ) );
        }
        for ( const auto& fileValue : files.toArray() ) {
            const auto file = jsonInt( fileValue, 0, context.fileCount - 1 );
            if ( !file || context.files.contains( *file ) ) {
                return fail( QStringLiteral( "a pane refers to a missing or repeated file" ) );
            }
            context.files.insert( *file );
            node->files.push_back( *file );
        }

        const auto currentValue = object.value( QStringLiteral( "currentFile" ) );
        node->currentFile = -1;
        if ( !currentValue.isUndefined() ) {
            const auto current = jsonInt( currentValue, -1, context.fileCount - 1 );
            if ( !current
                 || ( *current >= 0
                      && std::find( node->files.cbegin(), node->files.cend(), *current )
                             == node->files.cend() ) ) {
                return fail( QStringLiteral( "a pane's current file is not one of its files" ) );
            }
            node->currentFile = *current;
        }
        return true;
    }

    if ( type != SplitType ) {
        return fail( QStringLiteral( "unknown layout node type '%1'" ).arg( type ) );
    }

    node->isPane = false;
    const auto orientation = object.value( QStringLiteral( "orientation" ) ).toString();
    if ( orientation == Horizontal ) {
        node->orientation = Qt::Horizontal;
    }
    else if ( orientation == Vertical ) {
        node->orientation = Qt::Vertical;
    }
    else {
        return fail( QStringLiteral( "a splitter has an unknown orientation" ) );
    }

    const auto children = object.value( QStringLiteral( "children" ) ).toArray();
    if ( children.size() < 2 || children.size() > PaneLayoutFormat::MaxPanes ) {
        return fail( QStringLiteral( "a splitter must hold between 2 and %1 nodes" )
                         .arg( PaneLayoutFormat::MaxPanes ) );
    }

    // Missing or empty sizes mean equal sizes.
    const auto sizes = object.value( QStringLiteral( "sizes" ) );
    if ( !sizes.isUndefined() ) {
        const auto sizeList = sizes.toArray();
        if ( !sizes.isArray() || ( !sizeList.isEmpty() && sizeList.size() != children.size() ) ) {
            return fail( QStringLiteral( "splitter sizes do not match its nodes" ) );
        }
        for ( const auto& sizeValue : sizeList ) {
            const auto size = jsonInt( sizeValue, 0, MaxSplitterSize );
            if ( !size ) {
                return fail( QStringLiteral( "a splitter size is invalid" ) );
            }
            node->sizes.push_back( *size );
        }
    }

    for ( const auto& childValue : children ) {
        PaneLayoutNode child;
        if ( !parseNode( childValue, depth + 1, context, &child ) ) {
            return false;
        }
        node->children.push_back( std::move( child ) );
    }
    return true;
}

QJsonObject serializeNode( const PaneLayoutNode& node )
{
    QJsonObject object;
    if ( node.isPane ) {
        QJsonArray files;
        for ( const auto file : node.files ) {
            files.append( file );
        }
        object.insert( QStringLiteral( "type" ), PaneType );
        object.insert( QStringLiteral( "id" ), node.paneId );
        object.insert( QStringLiteral( "files" ), files );
        object.insert( QStringLiteral( "currentFile" ), node.currentFile );
        return object;
    }

    QJsonArray sizes;
    for ( const auto size : node.sizes ) {
        sizes.append( size );
    }
    QJsonArray children;
    for ( const auto& child : node.children ) {
        children.append( serializeNode( child ) );
    }
    object.insert( QStringLiteral( "type" ), SplitType );
    object.insert( QStringLiteral( "orientation" ),
                   node.orientation == Qt::Horizontal ? Horizontal : Vertical );
    object.insert( QStringLiteral( "sizes" ), sizes );
    object.insert( QStringLiteral( "children" ), children );
    return object;
}

int countPanes( const PaneLayoutNode& node )
{
    if ( node.isPane ) {
        return 1;
    }
    return std::accumulate(
        node.children.cbegin(), node.children.cend(), 0,
        []( int total, const PaneLayoutNode& child ) { return total + countPanes( child ); } );
}

const PaneLayoutNode* firstPaneNode( const PaneLayoutNode& node )
{
    if ( node.isPane ) {
        return &node;
    }
    return node.children.empty() ? nullptr : firstPaneNode( node.children.front() );
}

bool hasPaneId( const PaneLayoutNode& node, const QString& paneId )
{
    if ( node.isPane ) {
        return node.paneId == paneId;
    }
    return std::any_of( node.children.cbegin(), node.children.cend(),
                        [ &paneId ]( const auto& child ) { return hasPaneId( child, paneId ); } );
}

QString newPaneId()
{
    return QUuid::createUuid().toString( QUuid::WithoutBraces );
}
} // namespace

int PaneLayout::paneCount() const
{
    return countPanes( root );
}

namespace PaneLayoutFormat {

ParseResult parse( const QString& json, int fileCount )
{
    ParseResult result;
    if ( json.trimmed().isEmpty() ) {
        return result;
    }

    const auto invalid = [ &result ]( const QString& message ) {
        result.status = Status::Invalid;
        result.error = message;
        result.layout = {};
        return result;
    };

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( json.toUtf8(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
        return invalid( QStringLiteral( "the pane layout is not a JSON object" ) );
    }

    const auto object = document.object();
    const auto version = jsonInt( object.value( QStringLiteral( "version" ) ), 1, 1000000 );
    if ( !version ) {
        return invalid( QStringLiteral( "the pane layout has no valid version" ) );
    }
    if ( *version != Version ) {
        result.status = Status::Unsupported;
        result.error = QStringLiteral( "pane layout version %1 is not supported" ).arg( *version );
        return result;
    }

    ParseContext context;
    context.fileCount = std::max( 0, fileCount );
    if ( !parseNode( object.value( QStringLiteral( "root" ) ), 0, context,
                     &result.layout.root ) ) {
        return invalid( context.error );
    }

    const auto activeValue = object.value( QStringLiteral( "activePane" ) );
    if ( activeValue.isUndefined() ) {
        result.layout.activePaneId = firstPaneNode( result.layout.root )->paneId;
    }
    else if ( !activeValue.isString()
              || !hasPaneId( result.layout.root, activeValue.toString() ) ) {
        return invalid( QStringLiteral( "the active pane is not part of the layout" ) );
    }
    else {
        result.layout.activePaneId = activeValue.toString();
    }

    result.status = Status::Valid;
    return result;
}

QString serialize( const PaneLayout& layout )
{
    QJsonObject object;
    object.insert( QStringLiteral( "version" ), Version );
    object.insert( QStringLiteral( "activePane" ), layout.activePaneId );
    object.insert( QStringLiteral( "root" ), serializeNode( layout.root ) );
    return QString::fromUtf8( QJsonDocument( object ).toJson( QJsonDocument::Compact ) );
}

} // namespace PaneLayoutFormat

// ---------------------------------------------------------------------------------------------
// LogPane

LogPane::LogPane( const QString& paneId, QWidget* parent )
    : QWidget( parent )
    , paneId_( paneId )
    , tabs_( new TabbedCrawlerWidget() )
    , placeholder_( new QLabel() )
    , layout_( new QVBoxLayout() )
{
    setObjectName( QStringLiteral( "logPane" ) );
    // An empty pane can hold the keyboard focus, so tab shortcuts never reach another pane.
    setFocusPolicy( Qt::ClickFocus );

    tabs_->setDocumentMode( true );
    tabs_->setMovable( true );
    tabs_->setTabsClosable( true );

    placeholder_->setObjectName( QStringLiteral( "logPanePlaceholder" ) );
    retranslate();
    placeholder_->setAlignment( Qt::AlignCenter );
    placeholder_->setWordWrap( true );
    placeholder_->setForegroundRole( QPalette::PlaceholderText );
    placeholder_->hide();

    layout_->setContentsMargins( 0, 0, 0, 0 );
    layout_->setSpacing( 0 );
    layout_->addWidget( tabs_ );
    layout_->addWidget( placeholder_ );
    setLayout( layout_ );

    // A click on the tab row or on the empty pane activates the pane; clicks into a tab's views
    // give them focus, which activates the pane through the focus tracking of LogPaneArea.
    tabs_->tabBar()->installEventFilter( this );
    connect( tabs_, &TabbedCrawlerWidget::tabCountChanged, this, &LogPane::updatePlaceholder );
}

void LogPane::setPaneId( const QString& paneId )
{
    paneId_ = paneId;
}

void LogPane::retranslate()
{
    placeholder_->setText(
        tr( "Empty pane.\nOpen a file here, or move a tab here with its menu Move to Pane." ) );
}

void LogPane::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::LanguageChange ) {
        retranslate();
    }
    QWidget::changeEvent( event );
}

void LogPane::setPresentation( bool active, bool multiPane )
{
    const auto changed = active != active_ || multiPane != multiPane_;
    active_ = active;
    multiPane_ = multiPane;
    setProperty( "activePane", active );

    // A single pane looks exactly like the window did before panes existed.
    const auto margin = multiPane ? ActiveFrameWidth : 0;
    layout_->setContentsMargins( margin, margin, margin, margin );
    updatePlaceholder();
    if ( changed ) {
        update();
    }
}

void LogPane::updatePlaceholder()
{
    const auto empty = tabs_->count() == 0;
    placeholder_->setVisible( multiPane_ && empty );
    tabs_->setVisible( !( multiPane_ && empty ) );
}

bool LogPane::eventFilter( QObject* watched, QEvent* event )
{
    if ( event->type() == QEvent::MouseButtonPress && watched == tabs_->tabBar() ) {
        Q_EMIT activated( this );
    }
    return QWidget::eventFilter( watched, event );
}

void LogPane::focusInEvent( QFocusEvent* event )
{
    QWidget::focusInEvent( event );
    // The pane itself only keeps the focus while it is empty; otherwise its current tab gets it,
    // so the tab widget's shortcuts (Ctrl+Tab, Ctrl+1..9) keep working.
    if ( auto* current = tabs_->currentWidget(); current != nullptr && tabs_->isVisible() ) {
        current->setFocus();
    }
}

void LogPane::mousePressEvent( QMouseEvent* event )
{
    Q_EMIT activated( this );
    QWidget::mousePressEvent( event );
}

void LogPane::paintEvent( QPaintEvent* event )
{
    QWidget::paintEvent( event );
    if ( !multiPane_ || !active_ ) {
        return;
    }

    QPainter painter( this );
    QPen pen( palette().color( QPalette::Highlight ) );
    pen.setWidth( ActiveFrameWidth );
    pen.setJoinStyle( Qt::MiterJoin );
    painter.setPen( pen );
    painter.setBrush( Qt::NoBrush );
    const auto inset = ActiveFrameWidth / 2;
    painter.drawRect( rect().adjusted( inset, inset, -inset, -inset ) );
}

// ---------------------------------------------------------------------------------------------
// LogPaneArea

// Programmatic tab and pane changes move keyboard focus between panes; they must not change
// the active pane.
class LogPaneArea::FocusGuard {
  public:
    explicit FocusGuard( LogPaneArea& area )
        : area_( area )
    {
        ++area_.focusGuard_;
    }
    ~FocusGuard()
    {
        --area_.focusGuard_;
    }
    FocusGuard( const FocusGuard& ) = delete;
    FocusGuard& operator=( const FocusGuard& ) = delete;

  private:
    LogPaneArea& area_;
};

LogPaneArea::LogPaneArea( QWidget* parent )
    : QWidget( parent )
    , layout_( new QVBoxLayout() )
{
    setObjectName( QStringLiteral( "logPaneArea" ) );
    layout_->setContentsMargins( 0, 0, 0, 0 );
    layout_->setSpacing( 0 );
    setLayout( layout_ );

    auto* pane = createPane();
    setRoot( pane );
    active_ = pane;
    updatePresentation();

    // Views also take the focus by themselves (a tab finishing its first load focuses its log
    // view), so only the user's clicks and keyboard focus moves activate a pane.
    if ( auto* application = QCoreApplication::instance() ) {
        application->installEventFilter( this );
    }
}

LogPaneArea::~LogPaneArea()
{
    // Tabs and views are destroyed with the child widgets after this body; events and tab
    // signals raised then must not reach this half-destroyed object.
    if ( auto* application = QCoreApplication::instance() ) {
        application->removeEventFilter( this );
    }
    for ( auto* pane : panes() ) {
        releasePane( pane );
    }
}

bool LogPaneArea::eventFilter( QObject* watched, QEvent* event )
{
    const auto type = event->type();
    if ( focusGuard_ == 0 && ( type == QEvent::MouseButtonPress || type == QEvent::FocusIn ) ) {
        auto userAction = type == QEvent::MouseButtonPress;
        if ( type == QEvent::FocusIn ) {
            const auto reason = static_cast<QFocusEvent*>( event )->reason();
            userAction = reason == Qt::MouseFocusReason || reason == Qt::TabFocusReason
                         || reason == Qt::BacktabFocusReason || reason == Qt::ShortcutFocusReason;
        }
        if ( userAction ) {
            activatePaneOf( qobject_cast<QWidget*>( watched ) );
        }
    }
    return QWidget::eventFilter( watched, event );
}

void LogPaneArea::activatePaneOf( QWidget* widget )
{
    if ( widget == nullptr || !isAncestorOf( widget ) ) {
        return;
    }
    for ( auto* pane : panes() ) {
        if ( pane == widget || pane->isAncestorOf( widget ) ) {
            setActivePane( pane );
            return;
        }
    }
}

LogPane* LogPaneArea::createPane( const QString& paneId )
{
    auto* pane = new LogPane( paneId.isEmpty() ? newPaneId() : paneId );
    pane->tabs()->setAlwaysShowTabBar( alwaysShowTabBar_ );
    connectPane( pane );
    return pane;
}

QSplitter* LogPaneArea::createSplitter( Qt::Orientation orientation ) const
{
    auto* splitter = new QSplitter( orientation );
    splitter->setObjectName( QStringLiteral( "paneSplitter" ) );
    splitter->setChildrenCollapsible( false );
    return splitter;
}

void LogPaneArea::connectPane( LogPane* pane )
{
    auto* tabs = pane->tabs();
    // Pane signals carry the pane's tab index; the area reports the window-wide index.
    const auto forward = [ this, pane ]( void ( LogPaneArea::*areaSignal )( int ) ) {
        return [ this, pane, areaSignal ]( int localIndex ) {
            Q_EMIT( this->*areaSignal )( globalIndex( pane, localIndex ) );
        };
    };

    connect( tabs, &TabbedCrawlerWidget::tabCloseRequested, this,
             forward( &LogPaneArea::tabCloseRequested ) );
    connect( tabs, &TabbedCrawlerWidget::startNewStreamFileRequested, this,
             forward( &LogPaneArea::startNewStreamFileRequested ) );
    connect( tabs, &TabbedCrawlerWidget::closeStreamConnectionRequested, this,
             forward( &LogPaneArea::closeStreamConnectionRequested ) );
    connect( tabs, &TabbedCrawlerWidget::pauseStreamConnectionRequested, this,
             forward( &LogPaneArea::pauseStreamConnectionRequested ) );
    connect( tabs, &TabbedCrawlerWidget::resumeStreamConnectionRequested, this,
             forward( &LogPaneArea::resumeStreamConnectionRequested ) );
    connect( tabs, &TabbedCrawlerWidget::actionsEnabledChangeRequested, this,
             &LogPaneArea::actionsEnabledChangeRequested );
    connect( tabs, &TabbedCrawlerWidget::autoResponsesEnabledChangeRequested, this,
             &LogPaneArea::autoResponsesEnabledChangeRequested );
    connect( tabs, &QTabWidget::currentChanged, this, [ this, pane ]( int localIndex ) {
        if ( pane == active_ && movingTabs_ == 0 ) {
            Q_EMIT currentChanged( localIndex < 0 ? -1 : globalIndex( pane, localIndex ) );
        }
    } );
    connect( pane, &LogPane::activated, this, &LogPaneArea::setActivePane );

    tabs->setContextMenuExtension( [ this, pane ]( QMenu& menu, int localIndex ) {
        if ( contextMenuExtension_ ) {
            contextMenuExtension_( menu, pane, localIndex );
        }
    } );
}

void LogPaneArea::releasePane( LogPane* pane )
{
    QObject::disconnect( pane->tabs(), nullptr, this, nullptr );
    QObject::disconnect( pane, nullptr, this, nullptr );
    pane->tabs()->setContextMenuExtension( {} );
}

void LogPaneArea::setRoot( QWidget* root )
{
    root_ = root;
    layout_->addWidget( root );
    root->show();
}

void LogPaneArea::collectPanes( QWidget* node, std::vector<LogPane*>& result ) const
{
    if ( auto* pane = qobject_cast<LogPane*>( node ) ) {
        result.push_back( pane );
    }
    else if ( auto* splitter = qobject_cast<QSplitter*>( node ) ) {
        for ( int index = 0; index < splitter->count(); ++index ) {
            collectPanes( splitter->widget( index ), result );
        }
    }
}

void LogPaneArea::collectSplitters( QWidget* node, std::vector<QSplitter*>& result ) const
{
    if ( auto* splitter = qobject_cast<QSplitter*>( node ) ) {
        result.push_back( splitter );
        for ( int index = 0; index < splitter->count(); ++index ) {
            collectSplitters( splitter->widget( index ), result );
        }
    }
}

std::vector<LogPane*> LogPaneArea::panes() const
{
    std::vector<LogPane*> result;
    collectPanes( root_, result );
    return result;
}

int LogPaneArea::paneCount() const
{
    return static_cast<int>( panes().size() );
}

int LogPaneArea::paneIndexOf( const LogPane* pane ) const
{
    const auto all = panes();
    const auto it = std::find( all.cbegin(), all.cend(), pane );
    return it == all.cend() ? -1 : static_cast<int>( std::distance( all.cbegin(), it ) );
}

LogPane* LogPaneArea::paneById( const QString& paneId ) const
{
    for ( auto* pane : panes() ) {
        if ( pane->paneId() == paneId ) {
            return pane;
        }
    }
    return nullptr;
}

LogPane* LogPaneArea::paneForTabId( const QString& tabId ) const
{
    if ( tabId.isEmpty() ) {
        return nullptr;
    }
    for ( auto* pane : panes() ) {
        if ( pane->tabs()->findTabById( tabId ) >= 0 ) {
            return pane;
        }
    }
    return nullptr;
}

LogPane* LogPaneArea::paneForWidget( const QWidget* widget ) const
{
    if ( widget == nullptr ) {
        return nullptr;
    }
    for ( auto* pane : panes() ) {
        if ( pane->tabs()->indexOf( const_cast<QWidget*>( widget ) ) >= 0 ) {
            return pane;
        }
    }
    return nullptr;
}

LogPane* LogPaneArea::activePane() const
{
    return active_;
}

void LogPaneArea::setActivePane( LogPane* pane )
{
    if ( pane == nullptr || pane == active_ || paneIndexOf( pane ) < 0 ) {
        return;
    }

    active_ = pane;
    updatePresentation();
    LOG_DEBUG << "Active pane: index=" << paneIndexOf( pane ) << " id=" << pane->paneId();
    Q_EMIT activePaneChanged( pane );
    Q_EMIT currentChanged( currentIndex() );
}

LogPane* LogPaneArea::split( LogPane* pane, Qt::Orientation orientation )
{
    if ( pane == nullptr || paneIndexOf( pane ) < 0 ) {
        return nullptr;
    }
    if ( paneCount() >= MaxPanes ) {
        LOG_INFO << "Pane split refused: the window already has " << MaxPanes << " panes";
        return nullptr;
    }

    FocusGuard guard( *this );
    auto* created = createPane();
    auto* parentSplitter = qobject_cast<QSplitter*>( pane->parentWidget() );
    if ( parentSplitter != nullptr && parentSplitter->orientation() == orientation ) {
        const auto at = parentSplitter->indexOf( pane );
        auto sizes = parentSplitter->sizes();
        parentSplitter->insertWidget( at + 1, created );
        if ( at >= 0 && at < sizes.size() ) {
            const auto half = sizes.at( at ) / 2;
            sizes[ at ] -= half;
            sizes.insert( at + 1, half );
            parentSplitter->setSizes( sizes );
        }
    }
    else {
        auto* splitter = createSplitter( orientation );
        if ( parentSplitter != nullptr ) {
            const auto at = parentSplitter->indexOf( pane );
            const auto sizes = parentSplitter->sizes();
            parentSplitter->replaceWidget( at, splitter );
            parentSplitter->setSizes( sizes );
        }
        else {
            layout_->removeWidget( pane );
            setRoot( splitter );
        }
        splitter->addWidget( pane );
        splitter->addWidget( created );
        splitter->setSizes( { DefaultSplitterWeight, DefaultSplitterWeight } );
        splitter->show();
    }
    pane->show();
    created->show();

    LOG_INFO << "Pane split: " << ( orientation == Qt::Horizontal ? "right" : "down" )
             << " panes=" << paneCount();
    updatePresentation();
    Q_EMIT layoutChanged();
    return created;
}

LogPane* LogPaneArea::neighbourPane( const LogPane* pane ) const
{
    auto* parentSplitter = pane ? qobject_cast<QSplitter*>( pane->parentWidget() ) : nullptr;
    if ( parentSplitter == nullptr ) {
        return nullptr;
    }

    const auto at = parentSplitter->indexOf( const_cast<LogPane*>( pane ) );
    const auto before = at > 0;
    auto* sibling = parentSplitter->widget( before ? at - 1 : at + 1 );
    std::vector<LogPane*> leaves;
    collectPanes( sibling, leaves );
    if ( leaves.empty() ) {
        return nullptr;
    }
    // The nearest pane: the last one of a node before it, the first one of a node after it.
    return before ? leaves.back() : leaves.front();
}

bool LogPaneArea::removePane( LogPane* pane )
{
    if ( pane == nullptr || paneIndexOf( pane ) < 0 || paneCount() <= 1
         || pane->tabs()->count() > 0 ) {
        return false;
    }
    auto* parentSplitter = qobject_cast<QSplitter*>( pane->parentWidget() );
    if ( parentSplitter == nullptr ) {
        return false;
    }

    FocusGuard guard( *this );
    const auto wasActive = pane == active_;
    auto* next = neighbourPane( pane );

    releasePane( pane );
    pane->hide();
    pane->setParent( nullptr );
    pane->deleteLater();
    collapse( parentSplitter );

    if ( wasActive || active_.isNull() || paneIndexOf( active_ ) < 0 ) {
        auto* fallback = next != nullptr ? next : panes().front();
        active_ = fallback;
        updatePresentation();
        Q_EMIT activePaneChanged( fallback );
        Q_EMIT currentChanged( currentIndex() );
    }
    else {
        updatePresentation();
    }

    LOG_INFO << "Pane closed: panes=" << paneCount();
    Q_EMIT layoutChanged();
    return true;
}

void LogPaneArea::collapse( QSplitter* splitter )
{
    if ( splitter == nullptr || splitter->count() != 1 ) {
        return;
    }

    auto* child = splitter->widget( 0 );
    if ( auto* outer = qobject_cast<QSplitter*>( splitter->parentWidget() ) ) {
        const auto at = outer->indexOf( splitter );
        const auto sizes = outer->sizes();
        outer->replaceWidget( at, child );
        outer->setSizes( sizes );
        child->show();
        splitter->deleteLater();
        if ( auto* inner = qobject_cast<QSplitter*>( child );
             inner != nullptr && inner->orientation() == outer->orientation() ) {
            flatten( outer, inner );
        }
        return;
    }

    // The splitter was the root: its only child becomes the root.
    layout_->removeWidget( splitter );
    splitter->hide();
    child->setParent( nullptr );
    splitter->setParent( nullptr );
    splitter->deleteLater();
    setRoot( child );
}

void LogPaneArea::flatten( QSplitter* outer, QSplitter* inner )
{
    const auto at = outer->indexOf( inner );
    const auto outerSizes = outer->sizes();
    const auto innerSizes = inner->sizes();
    const auto innerTotal = at < outerSizes.size() ? outerSizes.at( at ) : 0;
    const auto innerSum = std::accumulate( innerSizes.cbegin(), innerSizes.cend(), 0 );

    std::vector<QWidget*> children;
    for ( int index = 0; index < inner->count(); ++index ) {
        children.push_back( inner->widget( index ) );
    }

    QList<int> sizes;
    for ( int index = 0; index < outerSizes.size(); ++index ) {
        if ( index != at ) {
            sizes.push_back( outerSizes.at( index ) );
            continue;
        }
        for ( int child = 0; child < innerSizes.size(); ++child ) {
            sizes.push_back( innerSum > 0 ? innerSizes.at( child ) * innerTotal / innerSum
                                          : DefaultSplitterWeight );
        }
    }

    int insertAt = at + 1;
    for ( auto* child : children ) {
        outer->insertWidget( insertAt++, child );
        child->show();
    }
    inner->hide();
    inner->setParent( nullptr );
    inner->deleteLater();
    outer->setSizes( sizes );
}

void LogPaneArea::equalize()
{
    std::vector<QSplitter*> splitters;
    collectSplitters( root_, splitters );
    for ( auto* splitter : splitters ) {
        const auto sizes = splitter->sizes();
        const auto total = std::accumulate( sizes.cbegin(), sizes.cend(), 0 );
        const auto each = total > 0 ? total / splitter->count() : DefaultSplitterWeight;
        splitter->setSizes( QList<int>( splitter->count(), each ) );
    }
    LOG_DEBUG << "Panes equalized: splitters=" << splitters.size();
}

int LogPaneArea::moveTab( LogPane* from, int index, LogPane* to, int toIndex )
{
    if ( from == nullptr || to == nullptr || paneIndexOf( from ) < 0 || paneIndexOf( to ) < 0
         || index < 0 || index >= from->tabs()->count() ) {
        return -1;
    }
    if ( from == to ) {
        return index;
    }

    FocusGuard guard( *this );
    int inserted = -1;
    {
        ++movingTabs_;
        const auto moved = qScopeGuard( [ this ] { --movingTabs_; } );
        auto detached = from->tabs()->detachTab( index );
        if ( !detached.isValid() ) {
            return -1;
        }
        inserted = to->tabs()->attachTab( std::move( detached ), toIndex );
    }
    LOG_INFO << "Tab moved between panes: from=" << paneIndexOf( from )
             << " to=" << paneIndexOf( to ) << " index=" << inserted;
    if ( from == active_ || to == active_ ) {
        Q_EMIT currentChanged( currentIndex() );
    }
    return inserted;
}

PaneLayoutNode LogPaneArea::captureNode(
    QWidget* node, const std::function<int( const LogPane&, int )>& fileIndexForTab ) const
{
    PaneLayoutNode result;
    if ( auto* pane = qobject_cast<LogPane*>( node ) ) {
        result.isPane = true;
        result.paneId = pane->paneId();
        const auto currentLocal = pane->tabs()->currentIndex();
        for ( int index = 0; index < pane->tabs()->count(); ++index ) {
            const auto file = fileIndexForTab( *pane, index );
            if ( file < 0 ) {
                continue;
            }
            result.files.push_back( file );
            if ( index == currentLocal ) {
                result.currentFile = file;
            }
        }
        return result;
    }

    auto* splitter = qobject_cast<QSplitter*>( node );
    result.isPane = false;
    result.orientation = splitter->orientation();
    result.sizes = splitter->sizes();
    for ( int index = 0; index < splitter->count(); ++index ) {
        result.children.push_back( captureNode( splitter->widget( index ), fileIndexForTab ) );
    }
    return result;
}

PaneLayout LogPaneArea::captureLayout(
    const std::function<int( const LogPane&, int )>& fileIndexForTab ) const
{
    PaneLayout layout;
    layout.root = captureNode( root_, fileIndexForTab );
    layout.activePaneId = active_ ? active_->paneId() : QString{};
    return layout;
}

QWidget* LogPaneArea::buildNode( const PaneLayoutNode& node, LogPane*& reusable )
{
    if ( node.isPane ) {
        if ( reusable != nullptr ) {
            auto* pane = reusable;
            reusable = nullptr;
            pane->setPaneId( node.paneId );
            return pane;
        }
        return createPane( node.paneId );
    }

    auto* splitter = createSplitter( node.orientation );
    for ( const auto& child : node.children ) {
        auto* widget = buildNode( child, reusable );
        splitter->addWidget( widget );
        widget->show();
    }
    if ( node.sizes.size() == splitter->count() ) {
        splitter->setSizes( node.sizes );
    }
    else {
        splitter->setSizes( QList<int>( splitter->count(), DefaultSplitterWeight ) );
    }
    return splitter;
}

bool LogPaneArea::applyLayout( const PaneLayout& layout )
{
    if ( count() > 0 ) {
        LOG_WARNING << "Pane layout not applied: the window still has tabs";
        return false;
    }
    if ( layout.paneCount() < 1 || layout.paneCount() > MaxPanes ) {
        LOG_WARNING << "Pane layout not applied: invalid pane count " << layout.paneCount();
        return false;
    }

    FocusGuard guard( *this );
    const auto existing = panes();
    LogPane* reusable = existing.front();

    // Take the kept pane out of the old tree, then drop the old tree with its other panes.
    QWidget* oldRoot = root_;
    if ( oldRoot == reusable ) {
        layout_->removeWidget( reusable );
        oldRoot = nullptr;
    }
    else {
        reusable->setParent( nullptr );
    }
    for ( auto* pane : existing ) {
        if ( pane != reusable ) {
            releasePane( pane );
        }
    }
    if ( oldRoot != nullptr ) {
        layout_->removeWidget( oldRoot );
        oldRoot->hide();
        oldRoot->setParent( nullptr );
        oldRoot->deleteLater();
    }

    setRoot( buildNode( layout.root, reusable ) );

    auto* active = paneById( layout.activePaneId );
    active_ = active != nullptr ? active : panes().front();
    updatePresentation();
    LOG_INFO << "Pane layout applied: panes=" << paneCount();
    Q_EMIT layoutChanged();
    Q_EMIT activePaneChanged( active_ );
    Q_EMIT currentChanged( currentIndex() );
    return true;
}

bool LogPaneArea::resetToSinglePane()
{
    if ( paneCount() == 1 ) {
        return true;
    }
    PaneLayout single;
    single.root.isPane = true;
    single.root.paneId = panes().front()->paneId();
    single.activePaneId = single.root.paneId;
    return applyLayout( single );
}

void LogPaneArea::setTabContextMenuExtension( TabContextMenuExtension extension )
{
    contextMenuExtension_ = std::move( extension );
}

void LogPaneArea::setAlwaysShowTabBar( bool alwaysShow )
{
    alwaysShowTabBar_ = alwaysShow;
    updatePresentation();
}

void LogPaneArea::updatePresentation()
{
    const auto all = panes();
    const auto multiPane = all.size() > 1;
    for ( auto* pane : all ) {
        pane->setPresentation( pane == active_, multiPane );
        // With several panes every pane shows its tab row, so each pane names its files.
        pane->tabs()->setAlwaysShowTabBar( alwaysShowTabBar_ || multiPane );
    }
}

int LogPaneArea::count() const
{
    int total = 0;
    for ( const auto* pane : panes() ) {
        total += pane->tabs()->count();
    }
    return total;
}

PaneTabRef LogPaneArea::tabRef( int index ) const
{
    if ( index < 0 ) {
        return {};
    }
    for ( auto* pane : panes() ) {
        const auto tabs = pane->tabs()->count();
        if ( index < tabs ) {
            return { pane, index };
        }
        index -= tabs;
    }
    return {};
}

int LogPaneArea::globalIndex( const LogPane* pane, int localIndex ) const
{
    if ( localIndex < 0 ) {
        return -1;
    }
    int base = 0;
    for ( const auto* candidate : panes() ) {
        const auto tabs = candidate->tabs()->count();
        if ( candidate == pane ) {
            return localIndex < tabs ? base + localIndex : -1;
        }
        base += tabs;
    }
    return -1;
}

QWidget* LogPaneArea::widget( int index ) const
{
    const auto ref = tabRef( index );
    return ref.isValid() ? ref.pane->tabs()->widget( ref.index ) : nullptr;
}

int LogPaneArea::indexOf( const QWidget* widget ) const
{
    if ( widget == nullptr ) {
        return -1;
    }
    int base = 0;
    for ( const auto* pane : panes() ) {
        const auto local = pane->tabs()->indexOf( const_cast<QWidget*>( widget ) );
        if ( local >= 0 ) {
            return base + local;
        }
        base += pane->tabs()->count();
    }
    return -1;
}

QString LogPaneArea::tabIdAt( int index ) const
{
    const auto ref = tabRef( index );
    return ref.isValid() ? ref.pane->tabs()->tabIdAt( ref.index ) : QString{};
}

QString LogPaneArea::tabPathAt( int index ) const
{
    const auto ref = tabRef( index );
    return ref.isValid() ? ref.pane->tabs()->tabPathAt( ref.index ) : QString{};
}

QString LogPaneArea::tabDisplayNameAt( int index ) const
{
    const auto ref = tabRef( index );
    return ref.isValid() ? ref.pane->tabs()->tabDisplayNameAt( ref.index ) : QString{};
}

int LogPaneArea::findTabById( const QString& tabId ) const
{
    if ( tabId.isEmpty() ) {
        return -1;
    }
    int base = 0;
    for ( const auto* pane : panes() ) {
        const auto local = pane->tabs()->findTabById( tabId );
        if ( local >= 0 ) {
            return base + local;
        }
        base += pane->tabs()->count();
    }
    return -1;
}

int LogPaneArea::currentIndex() const
{
    return active_ ? globalIndex( active_, active_->tabs()->currentIndex() ) : -1;
}

QWidget* LogPaneArea::currentWidget() const
{
    return active_ ? active_->tabs()->currentWidget() : nullptr;
}

void LogPaneArea::setCurrentIndex( int index )
{
    const auto ref = tabRef( index );
    if ( !ref.isValid() ) {
        return;
    }
    // A pane that is not active ignores its own current-tab change; activating it reports the
    // new current tab once.
    ref.pane->tabs()->setCurrentIndex( ref.index );
    setActivePane( ref.pane );
}

void LogPaneArea::setCurrentWidget( QWidget* widget )
{
    setCurrentIndex( indexOf( widget ) );
}

int LogPaneArea::addCrawler( CrawlerWidget* crawler, const QString& fileName )
{
    return addCrawlerToPane( active_, crawler, fileName );
}

int LogPaneArea::addCrawlerToPane( LogPane* pane, CrawlerWidget* crawler, const QString& fileName )
{
    if ( pane == nullptr || paneIndexOf( pane ) < 0 ) {
        pane = active_ ? active_.data() : panes().front();
    }

    // A capture can start before its tab opens; its session then waits in another pane.
    for ( auto* other : panes() ) {
        if ( other != pane && other->tabs()->streamSessionForPath( fileName ) != nullptr
             && paneHoldingPath( fileName ) == nullptr ) {
            pane->tabs()->setStreamSessionForPath(
                fileName, other->tabs()->takeStreamSessionForPath( fileName ) );
        }
    }

    const auto local = pane->tabs()->addCrawler( crawler, fileName );
    return globalIndex( pane, local );
}

void LogPaneArea::removeCrawler( int index )
{
    const auto ref = tabRef( index );
    if ( !ref.isValid() ) {
        return;
    }
    FocusGuard guard( *this );
    ref.pane->tabs()->removeCrawler( ref.index );
}

LogPane* LogPaneArea::paneHoldingPath( const QString& fileName ) const
{
    for ( auto* pane : panes() ) {
        for ( int index = 0; index < pane->tabs()->count(); ++index ) {
            if ( pane->tabs()->tabPathAt( index ) == fileName ) {
                return pane;
            }
        }
    }
    return nullptr;
}

LogPane* LogPaneArea::paneWithStreamSession( const QString& fileName ) const
{
    for ( auto* pane : panes() ) {
        if ( pane->tabs()->streamSessionForPath( fileName ) != nullptr ) {
            return pane;
        }
    }
    return nullptr;
}

StreamSession* LogPaneArea::streamSessionForTab( int index ) const
{
    const auto ref = tabRef( index );
    return ref.isValid() ? ref.pane->tabs()->streamSessionForTab( ref.index ) : nullptr;
}

StreamSession* LogPaneArea::streamSessionForPath( const QString& fileName ) const
{
    auto* pane = paneWithStreamSession( fileName );
    return pane ? pane->tabs()->streamSessionForPath( fileName ) : nullptr;
}

void LogPaneArea::setStreamSessionForPath( const QString& fileName,
                                           const std::shared_ptr<StreamSession>& session )
{
    if ( fileName.isEmpty() ) {
        return;
    }
    if ( !session ) {
        clearStreamSessionForPath( fileName );
        return;
    }

    auto* target = paneHoldingPath( fileName );
    if ( target == nullptr ) {
        target = paneWithStreamSession( fileName );
    }
    if ( target == nullptr ) {
        target = active_ ? active_.data() : panes().front();
    }
    for ( auto* pane : panes() ) {
        if ( pane != target && pane->tabs()->streamSessionForPath( fileName ) != nullptr ) {
            pane->tabs()->takeStreamSessionForPath( fileName );
        }
    }
    target->tabs()->setStreamSessionForPath( fileName, session );
}

void LogPaneArea::clearStreamSessionForPath( const QString& fileName )
{
    for ( auto* pane : panes() ) {
        pane->tabs()->clearStreamSessionForPath( fileName );
    }
}

void LogPaneArea::remapStreamSessionPath( const QString& oldFileName, const QString& newFileName )
{
    if ( oldFileName.isEmpty() || newFileName.isEmpty() || oldFileName == newFileName ) {
        return;
    }
    auto* owner = paneWithStreamSession( oldFileName );
    if ( owner == nullptr ) {
        return;
    }

    auto session = owner->tabs()->takeStreamSessionForPath( oldFileName );
    if ( !session ) {
        return;
    }
    auto* target = paneHoldingPath( newFileName );
    ( target != nullptr ? target : owner )->tabs()->setStreamSessionForPath( newFileName, session );
}

void LogPaneArea::setTabActionsPort( const QString& fileName, bool isActionsPort )
{
    for ( auto* pane : panes() ) {
        pane->tabs()->setTabActionsPort( fileName, isActionsPort );
    }
}

void LogPaneArea::setTabScriptActive( const QString& tabId, bool isActive )
{
    for ( auto* pane : panes() ) {
        pane->tabs()->setTabScriptActive( tabId, isActive );
    }
}
