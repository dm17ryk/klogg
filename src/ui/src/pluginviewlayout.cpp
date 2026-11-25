// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// E17f: PluginView's layout containers, content elements, button keys, busy overlay, focus and
// default button (docs/plugin-sdk-advanced.md section 29).
#include "log.h"
#include "pluginchart.h"
#include "pluginfeatures.h"
#include "pluginlayout.h"
#include "plugintablemodel.h"
#include "plugintextview.h"
#include "pluginview.h"
#include "pluginviewnode.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

using namespace pluginview;
using cilogg::plugins::MaxChartPoints;
using cilogg::plugins::MaxChartSeries;
using cilogg::plugins::MaxGridColumns;
using cilogg::plugins::MaxImageSide;
using cilogg::plugins::MaxSparklineValues;
using cilogg::plugins::MaxTextAppendChars;
using cilogg::plugins::MaxTextViewLines;

namespace {
constexpr int MaxHintPixels = 10000;
constexpr int MaxSpacerPixels = 2000;
constexpr int MaxStretch = 100;
constexpr int MaxRowSpan = 64;
constexpr int MaxBadgeChars = 64;
constexpr int KeptSizesDelayMs = 500;
// Elements that keep their natural size in a row (a row without other children adds a stretch).
const QStringList NaturalTypes{ "button", "label", "badge", "image", "sparkline", "separator",
                                "spacer" };

Qt::Alignment alignmentOf( const QString& align )
{
    if ( align == QLatin1String( "left" ) ) {
        return Qt::AlignLeft;
    }
    if ( align == QLatin1String( "center" ) ) {
        return Qt::AlignCenter;
    }
    if ( align == QLatin1String( "right" ) ) {
        return Qt::AlignRight;
    }
    if ( align == QLatin1String( "top" ) ) {
        return Qt::AlignTop;
    }
    if ( align == QLatin1String( "bottom" ) ) {
        return Qt::AlignBottom;
    }
    return {};
}

// A whole number within [low, high]; `fallback` when the value is not a number.
int boundedInt( const QJsonValue& value, int low, int high, int fallback )
{
    if ( !value.isDouble() || !std::isfinite( value.toDouble() ) ) {
        return fallback;
    }
    return static_cast<int>(
        std::lround( std::clamp( value.toDouble(), double( low ), double( high ) ) ) );
}

// Chart points: [x, y] pairs, a number alone being y at `nextX` plus its position in the list.
QVector<QPointF> chartPoints( const QJsonArray& points, double nextX, int* skipped )
{
    QVector<QPointF> result;
    const auto first = std::max<qsizetype>( 0, points.size() - MaxChartPoints );
    result.reserve( static_cast<int>( points.size() - first ) );
    for ( auto index = first; index < points.size(); ++index ) {
        const auto point = points.at( index );
        if ( point.isDouble() ) {
            result.append( QPointF( nextX + static_cast<double>( index ), point.toDouble() ) );
            continue;
        }
        const auto pair = point.toArray();
        if ( pair.size() == 2 && pair.at( 0 ).isDouble() && pair.at( 1 ).isDouble() ) {
            result.append( QPointF( pair.at( 0 ).toDouble(), pair.at( 1 ).toDouble() ) );
            continue;
        }
        ++*skipped;
    }
    return result;
}

// An element's share of the free space in its container: its `stretch`, or 1 for a spacer
// without a size (section 29.1).
int stretchOf( const QString& type, const QJsonObject& props )
{
    const auto stretch = boundedInt( props.value( "stretch" ), 0, MaxStretch, 0 );
    const auto size = props.value( "size" );
    if ( type == QLatin1String( "spacer" ) && stretch == 0
         && !( size.isDouble() && size.toDouble() >= 0 ) ) {
        return 1;
    }
    return stretch;
}

QString shortcutText( const QKeySequence& keys )
{
    return keys.toString( QKeySequence::PortableText );
}

// Whether a shortcut belongs to a plugin view: those act only while the focus is in their own
// view, so they never conflict with another view's (section 29.3).
bool insidePluginView( const QObject* object )
{
    for ( ; object; object = object->parent() ) {
        if ( qobject_cast<const PluginView*>( object ) ) {
            return true;
        }
    }
    return false;
}
} // namespace

namespace pluginlayout {
const QStringList& containerTypes()
{
    static const QStringList types{ "column", "row",      "group", "tabs",
                                    "tab",    "splitter", "form",  "grid" };
    return types;
}

bool expands( const QString& type )
{
    static const QStringList types{ "table", "tree",     "file_tree", "text_view",
                                    "chart", "splitter", "tabs" };
    return types.contains( type );
}

QString formCaption( const QJsonObject& element )
{
    const auto type = element.value( "type" ).toString();
    if ( type == QLatin1String( "button" ) ) {
        return {};
    }
    return element.value( "label" ).toString().left( PluginView::MaxTextChars );
}

QString captionText( QString caption )
{
    // A label with a buddy reads `&` as a mnemonic, even as plain text.
    return caption.replace( QLatin1Char( '&' ), QStringLiteral( "&&" ) );
}
} // namespace pluginlayout

QWidget* PluginView::buildContainer( Node& node, const QJsonObject& element, QWidget* parent,
                                     const QString& path )
{
    const auto type = node.type;
    const auto id = node.id;
    QWidget* outer = nullptr;
    QWidget* content = nullptr;
    if ( type == "group" ) {
        auto* group = new QGroupBox( parent );
        auto* groupLayout = new QVBoxLayout( group );
        content = new QWidget( group );
        groupLayout->addWidget( content );
        connect( group, &QGroupBox::toggled, content, &QWidget::setVisible );
        outer = group;
        node.widget = group;
    }
    else {
        // Every container holds its children in an inner widget, which the busy overlay covers.
        outer = new QWidget( parent );
        auto* wrapper = new QVBoxLayout( outer );
        wrapper->setContentsMargins( 0, 0, 0, 0 );
        if ( type == "tabs" ) {
            auto* tabs = new QTabWidget( outer );
            connect( tabs, &QTabWidget::currentChanged, this, [ this, id ]( int index ) {
                // The user changed the tab (also through set_ui); a plugin's `current` sends
                // nothing.
                if ( !applying_ && index >= 0 ) {
                    const auto tabId = tabIdOf( id, index );
                    emitEvent( id, "changed", tabId.isEmpty() ? QJsonValue() : QJsonValue( tabId ),
                               { { "index", index } } );
                }
            } );
            content = tabs;
            node.widget = tabs;
        }
        else if ( type == "splitter" ) {
            auto* splitter = new QSplitter( Qt::Horizontal, outer );
            splitter->setChildrenCollapsible( false );
            node.saveTimer = new QTimer( splitter );
            node.saveTimer->setSingleShot( true );
            node.saveTimer->setInterval( KeptSizesDelayMs );
            connect( node.saveTimer, &QTimer::timeout, this, [ this, id ] { saveSplitter( id ); } );
            connect( splitter, &QSplitter::splitterMoved, this, [ this, id ] {
                const auto found = nodes_.value( id );
                if ( found && found->saveTimer && !applying_ ) {
                    found->saveTimer->start(); // the user's sizes are kept per view
                }
            } );
            content = splitter;
            node.widget = splitter;
        }
        else {
            content = new QWidget( outer );
            node.widget = outer;
        }
        wrapper->addWidget( content );
    }
    node.content = content;
    if ( type == "row" ) {
        auto* layout = new QHBoxLayout( content );
        layout->setContentsMargins( 0, 0, 0, 0 );
    }
    else if ( type == "form" ) {
        auto* layout = new QFormLayout( content );
        layout->setContentsMargins( 0, 0, 0, 0 );
        layout->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );
    }
    else if ( type == "grid" ) {
        auto* layout = new QGridLayout( content );
        layout->setContentsMargins( 0, 0, 0, 0 );
    }
    else if ( type != "tabs" && type != "splitter" ) {
        auto* layout = new QVBoxLayout( content );
        layout->setContentsMargins( 0, 0, 0, 0 );
    }
    GridCursor cursor;
    cursor.columns = boundedInt( element.value( "columns" ), 1, MaxGridColumns, 2 );
    // Children know their container while they are built (tab, separator and spacer depend on it).
    const auto parentId = std::exchange( currentParentId_, id );
    const auto parentType = std::exchange( currentParentType_, type );
    bool natural = true;
    bool stretched = false;
    const auto children = element.value( "children" ).toArray();
    for ( int index = 0; index < children.size(); ++index ) {
        const auto child = children.at( index ).toObject();
        const auto childType = child.value( "type" ).toString();
        if ( type == "tabs" && childType != "tab" ) {
            warnOnce( "tabs-child:" + id,
                      "only tab elements go into tabs; skipped in " + id + ": " + childType.left( 64 ) );
            continue;
        }
        const auto childPath = path + '/' + QString::number( index );
        auto* childWidget = build( child, content, childPath );
        const auto childNode = nodes_.value( paths_.value( childPath ) );
        if ( !childWidget || !childNode ) {
            continue;
        }
        placeChild( node, *childNode, child, childWidget, cursor );
        natural = natural && NaturalTypes.contains( childType );
        stretched = stretched || stretchOf( childType, child ) > 0;
    }
    currentParentId_ = parentId;
    currentParentType_ = parentType;
    if ( type == "row" && natural && !stretched ) {
        qobject_cast<QBoxLayout*>( content->layout() )->addStretch( 1 );
    }
    return outer;
}

void PluginView::placeChild( Node& container, Node& child, const QJsonObject& element,
                             QWidget* widget, GridCursor& cursor )
{
    const auto& type = container.type;
    if ( type == "tabs" ) {
        auto* tabs = qobject_cast<QTabWidget*>( container.content.data() );
        tabs->addTab( widget, QString{} );
        updateTabHeader( child );
    }
    else if ( type == "splitter" ) {
        qobject_cast<QSplitter*>( container.content.data() )->addWidget( widget );
    }
    else if ( type == "form" ) {
        auto* form = qobject_cast<QFormLayout*>( container.content->layout() );
        const auto caption = pluginlayout::formCaption( element );
        if ( child.type == "field" || !caption.isEmpty() ) {
            // A caption cell: a field always has one, so a later label shows there. It is part of
            // the row: it follows the control's `visible` and `enabled`.
            auto* label = new QLabel( pluginlayout::captionText( caption ), container.content );
            label->setTextFormat( Qt::PlainText );
            if ( !child.automatic ) {
                label->setObjectName( nameFor( child.id ) + "_caption" );
            }
            label->setBuddy( child.widget );
            label->setVisible( child.props.value( "visible" ).toBool( true ) );
            label->setEnabled( child.props.value( "enabled" ).toBool( true ) );
            child.formCaption = label;
            if ( child.type == "field" && child.content ) {
                child.content->setVisible( false ); // the field's own caption
            }
            form->addRow( label, widget );
        }
        else {
            form->addRow( widget );
        }
    }
    else if ( type == "grid" ) {
        auto* grid = qobject_cast<QGridLayout*>( container.content->layout() );
        const auto span = boundedInt( element.value( "span" ), 1, cursor.columns, 1 );
        const auto rowSpan = boundedInt( element.value( "row_span" ), 1, MaxRowSpan, 1 );
        const auto free = [ &cursor, span, rowSpan ]( int row, int column ) {
            for ( int r = row; r < row + rowSpan; ++r ) {
                for ( int c = column; c < column + span; ++c ) {
                    if ( cursor.taken.contains( ( quint64( quint32( r ) ) << 32 ) | quint32( c ) ) ) {
                        return false;
                    }
                }
            }
            return true;
        };
        // The next free place from left to right, top to bottom.
        while ( true ) {
            if ( cursor.column + span > cursor.columns ) {
                ++cursor.row;
                cursor.column = 0;
            }
            if ( free( cursor.row, cursor.column ) ) {
                break;
            }
            ++cursor.column;
        }
        grid->addWidget( widget, cursor.row, cursor.column, rowSpan, span );
        for ( int r = cursor.row; r < cursor.row + rowSpan; ++r ) {
            for ( int c = cursor.column; c < cursor.column + span; ++c ) {
                cursor.taken.insert( ( quint64( quint32( r ) ) << 32 ) | quint32( c ) );
            }
        }
        cursor.column += span;
    }
    else {
        qobject_cast<QBoxLayout*>( container.content->layout() )->addWidget( widget );
    }
    // The child's stretch and alignment need its place in this container.
    applyPlacement( child );
}

QWidget* PluginView::buildContent( Node& node, QWidget* parent )
{
    const auto id = node.id;
    if ( node.type == "separator" ) {
        auto* line = new QFrame( parent );
        line->setFrameShadow( QFrame::Sunken );
        node.widget = line;
        return line;
    }
    if ( node.type == "spacer" ) {
        auto* space = new QWidget( parent );
        node.widget = space;
        return space;
    }
    if ( node.type == "text_view" ) {
        auto* view = new PluginTextView( node.automatic ? QString{} : nameFor( id ), parent );
        view->browser()->imageFor = [ guard = QPointer<PluginView>( this ) ]( const QString& reference ) {
            return guard ? guard->pictureFor( reference, QSize( 16, 16 ) ) : QImage{};
        };
        connect( view, &PluginTextView::linkActivated, this, [ this, id ]( const QString& href ) {
            emitEvent( id, "link", QJsonObject{ { "href", href } } );
        } );
        node.widget = view->browser();
        return view;
    }
    if ( node.type == "image" ) {
        auto* image = new PluginImage( parent );
        node.widget = image;
        return image;
    }
    if ( node.type == "sparkline" ) {
        auto* spark = new PluginSparkline( parent );
        node.widget = spark;
        return spark;
    }
    auto* chart = new PluginChart( parent );
    node.widget = chart;
    return chart;
}

void PluginView::afterBuild( Node& node )
{
    if ( node.type != "splitter" || node.automatic || !queries_.loadState ) {
        return;
    }
    // The user's kept sizes win over the plugin's when the panes are the same.
    auto* splitter = qobject_cast<QSplitter*>( node.content.data() );
    const auto kept = queries_.loadState( node.id ).value( "sizes" ).toArray();
    if ( !splitter || kept.isEmpty() || kept.size() != splitter->count() ) {
        return;
    }
    QList<int> sizes;
    for ( const auto& size : kept ) {
        sizes.append( boundedInt( size, 0, MaxHintPixels * 4, 0 ) );
    }
    splitter->setSizes( sizes );
}

void PluginView::saveSplitter( const QString& id )
{
    const auto node = nodes_.value( id );
    auto* splitter = node ? qobject_cast<QSplitter*>( node->content.data() ) : nullptr;
    // An id-less splitter keeps nothing: afterBuild() has no id to read its sizes back by.
    if ( !splitter || node->automatic || !queries_.saveState ) {
        return;
    }
    QJsonArray sizes;
    for ( const auto size : splitter->sizes() ) {
        sizes.append( size );
    }
    queries_.saveState( id, QJsonObject{ { "sizes", sizes } } );
}

QString PluginView::tabIdOf( const QString& tabsId, int index ) const
{
    const auto tabs = nodes_.value( tabsId );
    auto* widget = tabs ? qobject_cast<QTabWidget*>( tabs->content.data() ) : nullptr;
    auto* page = widget ? widget->widget( index ) : nullptr;
    for ( const auto& node : nodes_ ) {
        if ( page && node->parentId == tabsId && node->outer == page ) {
            return node->automatic ? QString{} : node->id;
        }
    }
    return {};
}

void PluginView::updateTabHeader( Node& tab )
{
    const auto parent = nodes_.value( tab.parentId );
    auto* tabs = parent ? qobject_cast<QTabWidget*>( parent->content.data() ) : nullptr;
    const auto index = tabs ? tabs->indexOf( tab.outer ) : -1;
    if ( index < 0 ) {
        return; // not placed yet
    }
    const auto title = text( tab.props, "title" );
    const auto badge = text( tab.props, "badge", MaxBadgeChars );
    auto label = badge.isEmpty() ? title : QStringLiteral( "%1 (%2)" ).arg( title, badge );
    tabs->setTabText( index, label.replace( QLatin1Char( '&' ), QStringLiteral( "&&" ) ) );
    tabs->setTabIcon( index, iconFor( tab.props.value( "icon" ) ) );
    tabs->setTabToolTip( index, plainToolTip( text( tab.props, "tooltip" ) ) );
}

void PluginView::applyLayout( Node& node, const QJsonObject& props, const QJsonObject& previous,
                              bool fromRoot )
{
    const auto& type = node.type;
    if ( type == "tabs" ) {
        auto* tabs = qobject_cast<QTabWidget*>( node.content.data() );
        if ( props.contains( "position" ) ) {
            const auto position = props.value( "position" ).toString();
            tabs->setTabPosition( position == "bottom"  ? QTabWidget::South
                                  : position == "left"  ? QTabWidget::West
                                  : position == "right" ? QTabWidget::East
                                                        : QTabWidget::North );
        }
        // A same-shaped root that sends the same `current` keeps the user's tab.
        if ( props.contains( "current" )
             && !( fromRoot && previous.value( "current" ) == props.value( "current" ) ) ) {
            const auto current = props.value( "current" );
            int index = -1;
            if ( current.isString() ) {
                for ( const auto& child : nodes_ ) {
                    if ( child->parentId == node.id && !child->automatic
                         && child->id == current.toString() ) {
                        index = tabs->indexOf( child->outer );
                    }
                }
            }
            else if ( current.isDouble() ) {
                index = boundedInt( current, -1, tabs->count(), -1 );
            }
            if ( index >= 0 && index < tabs->count() ) {
                tabs->setCurrentIndex( index );
            }
            else if ( !current.isNull() ) {
                warnOnce( "current:" + node.id, "tabs " + node.id + " has no such current tab" );
            }
        }
    }
    else if ( type == "tab" ) {
        if ( props.contains( "title" ) || props.contains( "badge" ) || props.contains( "icon" )
             || props.contains( "tooltip" ) ) {
            updateTabHeader( node );
        }
    }
    else if ( type == "splitter" ) {
        auto* splitter = qobject_cast<QSplitter*>( node.content.data() );
        if ( props.contains( "orientation" ) ) {
            splitter->setOrientation( props.value( "orientation" ).toString() == "vertical"
                                          ? Qt::Vertical
                                          : Qt::Horizontal );
        }
        if ( props.contains( "sizes" ) && !( fromRoot && previous.value( "sizes" ) == props.value( "sizes" ) ) ) {
            QList<int> sizes;
            for ( const auto& size : props.value( "sizes" ).toArray() ) {
                sizes.append( boundedInt( size, 0, MaxHintPixels * 4, 0 ) );
            }
            if ( !sizes.isEmpty() ) {
                splitter->setSizes( sizes );
            }
        }
    }
    else if ( type == "separator" ) {
        auto* line = qobject_cast<QFrame*>( node.widget.data() );
        const auto orientation = node.props.value( "orientation" ).toString();
        const bool vertical = orientation == "vertical"
                              || ( orientation.isEmpty() && node.parentType == "row" );
        line->setFrameShape( vertical ? QFrame::VLine : QFrame::HLine );
    }
    else if ( type == "spacer"
              && ( previous.isEmpty() || fromRoot || props.contains( "size" ) ) ) {
        // Built, a same-shaped root, or a new `size`: other patches leave the size alone.
        auto* space = node.widget.data();
        const bool horizontal = node.parentType == "row";
        const auto size = node.props.value( "size" );
        space->setMinimumSize( 0, 0 );
        space->setMaximumSize( QWIDGETSIZE_MAX, QWIDGETSIZE_MAX );
        if ( size.isDouble() && size.toDouble() >= 0 ) {
            const auto pixels = boundedInt( size, 0, MaxSpacerPixels, 0 );
            if ( horizontal ) {
                space->setFixedWidth( pixels );
                space->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Minimum );
            }
            else {
                space->setFixedHeight( pixels );
                space->setSizePolicy( QSizePolicy::Minimum, QSizePolicy::Fixed );
            }
        }
        else {
            space->setSizePolicy( horizontal ? QSizePolicy::Expanding : QSizePolicy::Minimum,
                                  horizontal ? QSizePolicy::Minimum : QSizePolicy::Expanding );
        }
        // The reset above also cleared the element's minimum and maximum size hints.
        applySizeHints( node, node.props );
        applyPlacement( node );
    }
    if ( pluginlayout::containerTypes().contains( type )
         && ( props.contains( "busy" ) || props.contains( "busy_text" )
              || props.contains( "busy_cancel" ) ) ) {
        applyBusy( node, props, previous.value( "busy" ).toBool(), fromRoot );
    }
}

void PluginView::applyBusy( Node& node, const QJsonObject& props, bool wasBusy, bool fromRoot )
{
    const bool busy = node.props.value( "busy" ).toBool();
    if ( busy && !node.busy && node.outer && node.content ) {
        const auto id = node.id;
        node.busy = new PluginBusyOverlay( node.outer, node.content, nameFor( id ) + "_busy" );
        connect( node.busy, &PluginBusyOverlay::cancelRequested, this,
                 [ this, id ] { emitEvent( id, "cancel", QJsonValue( QJsonValue::Null ) ); } );
    }
    const auto* focused = QApplication::focusWidget();
    const bool hadFocus = busy && node.content && focused && node.content->isAncestorOf( focused );
    if ( node.content ) {
        node.content->setEnabled( !busy ); // the container's own `enabled` is not touched
    }
    if ( node.busy ) {
        node.busy->setText( text( node.props, "busy_text" ) );
        node.busy->setCancellable( node.props.value( "busy_cancel" ).toBool() );
        // Busy set again (turned on, or a patch that sets it): Cancel is available again. A
        // same-shaped root that still says busy leaves a pressed Cancel as it is.
        if ( busy && props.contains( "busy" ) && ( !wasBusy || !fromRoot ) ) {
            node.busy->resetCancel();
        }
        node.busy->setVisible( busy );
        if ( busy ) {
            node.busy->raise();
            if ( hadFocus ) {
                node.busy->setFocus( Qt::OtherFocusReason );
            }
        }
    }
}

void PluginView::applyContent( Node& node, const QJsonObject& props, bool fromRoot )
{
    const auto& type = node.type;
    // Streaming content: a same-shaped root replaces it only when it differs from what the plugin
    // last set as a whole, so appended text and points stay.
    const auto replaces = [ & ]( const char* key ) {
        const QLatin1String name( key );
        return props.contains( name ) && !( fromRoot && props.value( name ) == node.lastRows );
    };
    if ( type == "text_view" ) {
        auto* view = qobject_cast<PluginTextView*>( node.outer.data() );
        if ( props.contains( "format" ) ) {
            const auto format = props.value( "format" ).toString( "plain" );
            if ( format != "plain" && format != "markdown" ) {
                warnOnce( "format:" + node.id, "unknown text_view format treated as plain: "
                                                   + format.left( 64 ) );
            }
            view->setMarkdown( format == "markdown" );
        }
        if ( props.contains( "monospace" ) ) {
            view->setMonospace( props.value( "monospace" ).toBool() );
        }
        if ( props.contains( "wrap" ) ) {
            view->setWrap( props.value( "wrap" ).toBool( true ) );
        }
        if ( props.contains( "max_lines" ) ) {
            view->setMaxLines( boundedInt( props.value( "max_lines" ), 0, MaxTextViewLines, 0 ) );
        }
        if ( props.contains( "follow" ) ) {
            view->setFollow( props.value( "follow" ).toBool() );
        }
        if ( replaces( "text" ) ) {
            node.lastRows = props.value( "text" );
            view->setText( props.value( "text" ).toString() );
        }
    }
    else if ( type == "image" ) {
        auto* image = qobject_cast<PluginImage*>( node.widget.data() );
        if ( props.contains( "width" ) || props.contains( "height" ) ) {
            image->setBox( boundedInt( node.props.value( "width" ), 0, MaxImageSide, 0 ),
                           boundedInt( node.props.value( "height" ), 0, MaxImageSide, 0 ) );
            // The box resets the minimum and maximum size: the element's size hints apply over
            // it again, as when it was built.
            applySizeHints( node, node.props );
        }
        if ( props.contains( "scale" ) ) {
            const auto scale = props.value( "scale" ).toString( "fit" );
            image->setScale( scale == "fill"   ? PluginImage::Scale::Fill
                             : scale == "none" ? PluginImage::Scale::None
                                               : PluginImage::Scale::Fit );
        }
        if ( props.contains( "alt" ) ) {
            image->setAlt( text( props, "alt" ) );
        }
        if ( props.contains( "source" ) || props.contains( "width" ) || props.contains( "height" ) ) {
            applyImageSource( node );
        }
    }
    else if ( type == "sparkline" ) {
        auto* spark = qobject_cast<PluginSparkline*>( node.widget.data() );
        if ( replaces( "values" ) ) {
            node.lastRows = props.value( "values" );
            const auto values = props.value( "values" ).toArray();
            QVector<double> numbers;
            const auto first = std::max<qsizetype>( 0, values.size() - MaxSparklineValues );
            for ( auto index = first; index < values.size(); ++index ) {
                const auto value = values.at( index );
                numbers.append( value.isDouble() ? value.toDouble()
                                                 : std::numeric_limits<double>::quiet_NaN() );
            }
            spark->setValues( numbers );
        }
        if ( props.contains( "min" ) || props.contains( "max" ) ) {
            const auto bound = [ & ]( const char* key ) -> std::optional<double> {
                const auto value = node.props.value( QLatin1String( key ) );
                return value.isDouble() ? std::optional<double>( value.toDouble() ) : std::nullopt;
            };
            spark->setRange( bound( "min" ), bound( "max" ) );
        }
        if ( props.contains( "kind" ) ) {
            spark->setBars( props.value( "kind" ).toString() == "bar" );
        }
        if ( props.contains( "tone" ) ) {
            spark->setColor( toneColor( props.value( "tone" ).toString().left( 16 ) ) );
        }
    }
    else if ( type == "chart" ) {
        auto* chart = qobject_cast<PluginChart*>( node.widget.data() );
        if ( props.contains( "kind" ) ) {
            const auto kind = props.value( "kind" ).toString( "line" );
            chart->setKind( kind == "bar"       ? PluginChart::Kind::Bar
                            : kind == "scatter" ? PluginChart::Kind::Scatter
                            : kind == "area"    ? PluginChart::Kind::Area
                                                : PluginChart::Kind::Line );
        }
        if ( props.contains( "x_title" ) || props.contains( "y_title" ) ) {
            chart->setTitles( text( node.props, "x_title" ), text( node.props, "y_title" ) );
        }
        if ( props.contains( "x_kind" ) ) {
            chart->setTimeAxis( props.value( "x_kind" ).toString() == "time" );
        }
        if ( props.contains( "legend" ) ) {
            const auto legend = props.value( "legend" );
            chart->setLegend( legend.isBool() ? std::optional<bool>( legend.toBool() ) : std::nullopt );
        }
        if ( props.contains( "window" ) ) {
            chart->setWindow( boundedInt( props.value( "window" ), 0, MaxChartPoints, 0 ) );
        }
        if ( replaces( "series" ) ) {
            node.lastRows = props.value( "series" );
            const auto list = props.value( "series" ).toArray();
            if ( list.size() > MaxChartSeries ) {
                warnOnce( "series:" + node.id,
                          QStringLiteral( "chart %1 shows its first %2 series" ).arg( node.id ).arg( MaxChartSeries ) );
            }
            QVector<PluginChart::Series> series;
            QSet<QString> ids;
            int skipped = 0;
            for ( const auto& value : list ) {
                const auto object = value.toObject();
                const auto seriesId = object.value( "id" ).toString().left( 128 );
                if ( series.size() >= MaxChartSeries ) {
                    break;
                }
                if ( seriesId.isEmpty() || ids.contains( seriesId ) ) {
                    warnOnce( "series-id:" + node.id,
                              "chart series without an id, or with the id of an earlier one, "
                              "skipped in "
                                  + node.id );
                    continue;
                }
                ids.insert( seriesId );
                PluginChart::Series item;
                item.id = seriesId;
                item.title = text( object, "title" );
                item.color = toneColor( object.value( "tone" ).toString().left( 16 ) );
                item.points = chartPoints( object.value( "points" ).toArray(), 0, &skipped );
                series.append( item );
            }
            if ( skipped > 0 ) {
                warnOnce( "points:" + node.id, "chart points that are no [x, y] pair skipped in " + node.id );
            }
            chart->setSeries( series );
        }
    }
}

void PluginView::applyImageSource( Node& node )
{
    auto* image = qobject_cast<PluginImage*>( node.widget.data() );
    if ( !image ) {
        return;
    }
    const auto width = boundedInt( node.props.value( "width" ), 0, MaxImageSide, 0 );
    const auto height = boundedInt( node.props.value( "height" ), 0, MaxImageSide, 0 );
    // Without a box the image takes its own size: an invalid size asks for it.
    const auto request = width > 0 || height > 0
                             ? QSize( width > 0 ? width : MaxImageSide, height > 0 ? height : MaxImageSide )
                             : QSize{};
    const auto source = node.props.value( "source" );
    const bool given = source.isObject() || !source.toString().isEmpty();
    image->setImage( given ? pictureFor( source, request ) : QImage{} );
}

void PluginView::refreshImages()
{
    for ( const auto& node : std::as_const( nodes_ ) ) {
        if ( node && node->type == "image" ) {
            applyImageSource( *node );
        }
    }
}

QImage PluginView::pictureFor( const QJsonValue& reference, const QSize& size ) const
{
    return queries_.picture ? queries_.picture( reference, size ) : QImage{};
}

void PluginView::applySizeHints( Node& node, const QJsonObject& props )
{
    auto* widget = node.outer.data();
    if ( !widget ) {
        return;
    }
    static const QStringList Hints{ "stretch", "min_width", "max_width", "min_height",
                                    "max_height", "span", "row_span" };
    for ( const auto& key : Hints ) {
        if ( props.contains( key ) && !props.value( key ).isDouble() ) {
            warnOnce( "hint:" + node.id + ':' + key,
                      "size hint " + key + " of " + node.id + " is not a number; ignored" );
        }
    }
    const auto pixels = [ & ]( const char* key ) {
        return boundedInt( node.props.value( QLatin1String( key ) ), 0, MaxHintPixels, 0 );
    };
    if ( props.contains( "min_width" ) ) {
        widget->setMinimumWidth( pixels( "min_width" ) );
    }
    if ( props.contains( "max_width" ) ) {
        widget->setMaximumWidth( pixels( "max_width" ) > 0 ? pixels( "max_width" ) : QWIDGETSIZE_MAX );
    }
    if ( props.contains( "min_height" ) ) {
        widget->setMinimumHeight( pixels( "min_height" ) );
    }
    if ( props.contains( "max_height" ) ) {
        widget->setMaximumHeight( pixels( "max_height" ) > 0 ? pixels( "max_height" ) : QWIDGETSIZE_MAX );
    }
    if ( props.contains( "align" ) ) {
        static const QStringList Alignments{ "left", "center", "right", "top", "bottom", "fill" };
        if ( !Alignments.contains( props.value( "align" ).toString() ) ) {
            warnOnce( "align:" + node.id, "unknown align of " + node.id + " treated as fill" );
        }
    }
    if ( props.contains( "stretch" ) || props.contains( "align" ) ) {
        applyPlacement( node );
    }
}

void PluginView::applyPlacement( Node& node )
{
    const auto parent = nodes_.value( node.parentId );
    if ( !parent || !parent->content || !node.outer ) {
        return;
    }
    const auto stretch = stretchOf( node.type, node.props );
    if ( auto* splitter = qobject_cast<QSplitter*>( parent->content.data() ) ) {
        const auto index = splitter->indexOf( node.outer );
        if ( index >= 0 ) {
            splitter->setStretchFactor( index, stretch );
        }
        return;
    }
    auto* layout = parent->content->layout();
    if ( !layout || layout->indexOf( node.outer ) < 0 ) {
        return;
    }
    if ( auto* box = qobject_cast<QBoxLayout*>( layout ) ) {
        box->setStretchFactor( node.outer, stretch );
    }
    layout->setAlignment( node.outer, alignmentOf( node.props.value( "align" ).toString() ) );
}

void PluginView::applyButton( Node& node, const QJsonObject& props, const QJsonObject& previous,
                              bool fromRoot )
{
    auto* button = qobject_cast<QPushButton*>( node.widget.data() );
    if ( props.contains( "tone" ) || props.contains( "style" ) ) {
        applyButtonLook( node );
    }
    if ( props.contains( "icon" ) ) {
        button->setIcon( iconFor( props.value( "icon" ) ) );
    }
    if ( props.contains( "menu" ) ) {
        const auto menuId = text( node.props, "menu", 128 );
        if ( menuId.isEmpty() ) {
            button->setMenu( nullptr );
            delete node.menu;
        }
        else if ( !node.menu ) {
            node.menu = new QMenu( button );
            if ( !node.automatic ) {
                node.menu->setObjectName( nameFor( node.id ) + "_menu" );
            }
            const auto id = node.id;
            auto* menu = node.menu.data();
            // Rebuilt each time it opens, like element context menus.
            connect( menu, &QMenu::aboutToShow, this, [ this, id, menu ] { fillButtonMenu( id, menu ); } );
            button->setMenu( menu );
        }
    }
    if ( props.contains( "checkable" ) ) {
        QScopedValueRollback<bool> rollback( applying_, true );
        button->setCheckable( props.value( "checkable" ).toBool() );
    }
    // A same-shaped root that sends the `checked` the plugin last set keeps the user's toggle.
    if ( props.contains( "checked" )
         && !( fromRoot && previous.value( "checked" ) == props.value( "checked" ) ) ) {
        QScopedValueRollback<bool> rollback( applying_, true ); // never echoed
        button->setChecked( props.value( "checked" ).toBool() );
    }
    if ( props.contains( "shortcut" ) ) {
        applyButtonShortcut( node );
    }
    if ( props.contains( "default" ) ) {
        button->setDefault( props.value( "default" ).toBool() );
    }
}

void PluginView::applyButtonLook( Node& node )
{
    auto* button = qobject_cast<QPushButton*>( node.widget.data() );
    const auto tone = node.props.value( "tone" ).toString( "normal" ).left( 16 );
    auto style = node.props.value( "style" ).toString( "push" );
    if ( style != "push" && style != "tool" && style != "link" ) {
        warnOnce( "style:" + node.id, "unknown button style treated as push: " + style.left( 64 ) );
        style = "push";
    }
    button->setProperty( "tone", tone );
    auto font = button->font();
    font.setBold( tone == "primary" );
    font.setUnderline( style == "link" );
    button->setFont( font );
    button->setFlat( style != "push" );
    if ( style == "link" ) {
        button->setCursor( Qt::PointingHandCursor );
        button->setStyleSheet(
            QStringLiteral( "QPushButton { border: none; padding: 0px; color: %1; }" )
                .arg( tone == "danger" ? QStringLiteral( "#c62828" )
                                       : palette().color( QPalette::Link ).name() ) );
    }
    else {
        button->unsetCursor();
        button->setStyleSheet( tone == "danger" ? QStringLiteral( "color: #c62828;" ) : QString{} );
    }
}

QString PluginView::shortcutRefusal( const QString& id, const QKeySequence& keys ) const
{
    // Section 7.6: reserved keys, CILogg's own shortcuts and another button's shortcut.
    static const QStringList Reserved{ "Ctrl+C", "Ctrl+V", "Ctrl+X", "Ctrl+A", "Ctrl+F", "Ctrl+Z",
                                       "F2",     "Return", "Enter",  "Esc",    "Tab",    "Del" };
    const auto wanted = shortcutText( keys );
    for ( const auto& reserved : Reserved ) {
        if ( QKeySequence( reserved, QKeySequence::PortableText ) == keys ) {
            return QStringLiteral( "%1 is reserved" ).arg( wanted );
        }
    }
    for ( const auto& node : nodes_ ) {
        if ( node->id != id && node->shortcut && node->shortcut->key() == keys ) {
            return QStringLiteral( "%1 belongs to button %2" ).arg( wanted, node->id );
        }
    }
    if ( !dialogParent_ ) {
        return {};
    }
    for ( const auto* action : dialogParent_->findChildren<QAction*>() ) {
        if ( action->shortcuts().contains( keys ) ) {
            return QStringLiteral( "%1 is a shortcut of CILogg" ).arg( wanted );
        }
    }
    for ( const auto* shortcut : dialogParent_->findChildren<QShortcut*>() ) {
        // This view's buttons are checked above; another view's shortcuts act only there.
        if ( shortcut->key() == keys && !insidePluginView( shortcut ) ) {
            return QStringLiteral( "%1 is a shortcut of CILogg" ).arg( wanted );
        }
    }
    return {};
}

void PluginView::applyButtonShortcut( Node& node )
{
    delete node.shortcut;
    const auto keys
        = QKeySequence( text( node.props, "shortcut", 64 ), QKeySequence::PortableText );
    if ( keys.isEmpty() || keys.count() != 1 ) {
        if ( !text( node.props, "shortcut", 64 ).isEmpty() ) {
            warnOnce( "shortcut:" + node.id, "button " + node.id + " shortcut is no key sequence" );
        }
        return;
    }
    const auto refusal = shortcutRefusal( node.id, keys );
    if ( !refusal.isEmpty() ) {
        warnOnce( "shortcut:" + node.id + shortcutText( keys ),
                  "button " + node.id + " shortcut refused: " + refusal );
        return;
    }
    const auto id = node.id;
    node.shortcut = new QShortcut( keys, this );
    node.shortcut->setContext( Qt::WidgetWithChildrenShortcut ); // only while the focus is here
    if ( !node.automatic ) {
        node.shortcut->setObjectName( nameFor( id ) + "_shortcut" );
    }
    connect( node.shortcut, &QShortcut::activated, this, [ this, id ] { clickButton( id ); } );
}

void PluginView::clickButton( const QString& id )
{
    const auto node = nodes_.value( id );
    auto* button = node ? qobject_cast<QPushButton*>( node->widget.data() ) : nullptr;
    if ( !button || !button->isEnabled() || !button->isVisibleTo( this ) || !isEnabled() ) {
        return;
    }
    if ( button->menu() ) {
        button->showMenu();
        return;
    }
    button->click();
}

void PluginView::fillButtonMenu( const QString& id, QMenu* menu )
{
    // Submenus of the last opening go with their actions.
    for ( auto* child : menu->findChildren<QMenu*>( QString{}, Qt::FindDirectChildrenOnly ) ) {
        child->deleteLater();
    }
    menu->clear();
    const auto node = nodes_.value( id );
    const auto menuId = node ? text( node->props, "menu", 128 ) : QString{};
    if ( node && !menuId.isEmpty() && isEnabled() && queries_.elementMenu ) {
        queries_.elementMenu( menu, id, menuId, QJsonObject{ { "button", true } } );
    }
    if ( menu->isEmpty() ) {
        menu->addAction( tr( "No entries" ) )->setEnabled( false );
    }
}

void PluginView::applyContentOperation( Node& node, const QJsonObject& entry )
{
    if ( entry.contains( "append" ) ) {
        auto* view = qobject_cast<PluginTextView*>( node.outer.data() );
        const auto appended = entry.value( "append" ).toString();
        if ( !view ) {
            warnOnce( "append:" + node.id, "append applies to text_view elements only: " + node.id );
        }
        else if ( appended.size() > MaxTextAppendChars ) {
            warnOnce( "append-size:" + node.id,
                      QStringLiteral( "text_view %1 append refused (more than %2 characters)" )
                          .arg( node.id )
                          .arg( MaxTextAppendChars ) );
        }
        else {
            view->append( appended );
        }
    }
    if ( !entry.contains( "append_points" ) && !entry.contains( "clear" ) ) {
        return;
    }
    auto* chart = qobject_cast<PluginChart*>( node.widget.data() );
    if ( !chart ) {
        warnOnce( "points:" + node.id, "append_points and clear apply to charts only: " + node.id );
        return;
    }
    // `clear` first, so one entry can replace a series' points.
    const auto clear = entry.value( "clear" );
    if ( clear.isBool() && clear.toBool() ) {
        chart->clear( {} );
    }
    else if ( clear.isArray() ) {
        QStringList ids;
        for ( const auto& seriesId : clear.toArray() ) {
            ids.append( seriesId.toString() );
        }
        chart->clear( ids );
    }
    const auto appended = entry.value( "append_points" );
    const auto batches = appended.isArray() ? appended.toArray() : QJsonArray{ appended };
    int skipped = 0;
    for ( const auto& batch : batches ) {
        if ( !batch.isObject() ) {
            continue;
        }
        const auto object = batch.toObject();
        const auto seriesId = object.value( "series" ).toString();
        const auto points
            = chartPoints( object.value( "points" ).toArray(), chart->nextX( seriesId ), &skipped );
        if ( !chart->appendPoints( seriesId, points ) ) {
            warnOnce( "series:" + node.id + ':' + seriesId.left( 128 ),
                      "chart " + node.id + " has no series " + seriesId.left( 128 ) );
        }
    }
    if ( skipped > 0 ) {
        warnOnce( "points:" + node.id, "chart points that are no [x, y] pair skipped in " + node.id );
    }
}

bool PluginView::focusAllowed() const
{
    // Only while the view is active, so a plugin never takes the focus from another window
    // (section 12.3): its own window is active, or the focus already is in the view.
    auto* top = window();
    if ( !top || !top->isActiveWindow() ) {
        return false;
    }
    if ( top != dialogParent_.data() ) {
        return true;
    }
    auto* focused = QApplication::focusWidget();
    return focused && ( focused == this || isAncestorOf( focused ) );
}

void PluginView::focusElement( Node& node )
{
    if ( !focusAllowed() ) {
        LOG_DEBUG << "Plugin view focus ignored: the view is not active plugin="
                  << pluginId_.toStdString() << " element=" << node.id.toStdString();
        return;
    }
    if ( node.type == "tab" ) {
        // A tab becomes the current one (no `changed`: the plugin asked for it).
        const auto parent = nodes_.value( node.parentId );
        if ( auto* tabs = parent ? qobject_cast<QTabWidget*>( parent->content.data() ) : nullptr ) {
            tabs->setCurrentWidget( node.outer );
            tabs->setFocus( Qt::OtherFocusReason );
        }
        return;
    }
    if ( node.widget ) {
        node.widget->setFocus( Qt::OtherFocusReason );
    }
}

QPushButton* PluginView::defaultButton() const
{
    for ( const auto& id : order_ ) {
        const auto node = nodes_.value( id );
        if ( node && node->type == "button" && node->props.value( "default" ).toBool() ) {
            auto* button = qobject_cast<QPushButton*>( node->widget.data() );
            return button && button->isEnabled() && button->isVisibleTo( this ) ? button : nullptr;
        }
    }
    return nullptr;
}

bool PluginView::enterPressesDefault() const
{
    // Not where the focused control uses Enter itself.
    for ( auto* widget = QApplication::focusWidget(); widget && widget != this;
          widget = widget->parentWidget() ) {
        if ( qobject_cast<QAbstractItemView*>( widget ) || qobject_cast<QPlainTextEdit*>( widget )
             || qobject_cast<QTextEdit*>( widget ) || qobject_cast<QAbstractButton*>( widget ) ) {
            return false;
        }
        if ( auto* combo = qobject_cast<QComboBox*>( widget );
             combo && combo->view() && combo->view()->isVisible() ) {
            return false;
        }
    }
    return true;
}

void PluginView::keyPressEvent( QKeyEvent* event )
{
    if ( ( event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter )
         && ( event->modifiers() & ~Qt::KeypadModifier ) == Qt::NoModifier && isEnabled() ) {
        if ( auto* button = defaultButton(); button && enterPressesDefault() ) {
            event->accept();
            button->click();
            return;
        }
    }
    QWidget::keyPressEvent( event );
}

bool PluginView::emitDialogButton( const QString& name )
{
    return emitEvent( QString::fromLatin1( DialogButtonsId ), QStringLiteral( "clicked" ), name );
}
