// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// E17f: layout containers, content elements, button keys, the busy overlay, unsaved changes,
// dialog buttons and focus patches (docs/plugin-sdk-advanced.md section 29).
#include "mainwindow.h"
#include "pluginchart.h"
#include "pluginlayout.h"
#include "pluginmanager.h"
#include "pluginmenus.h"
#include "plugintextview.h"
#include "pluginview.h"
#include "pluginviewhost.h"
#include "session.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QTabBar>
#include <QToolTip>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalSpy>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <catch2/catch.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

using cilogg::plugins::PluginManager;

namespace {
const QString PluginId = QStringLiteral( "org.test.e17f" );

bool waitFor( const std::function<bool()>& predicate, int timeout = 5000 )
{
    return QTest::qWaitFor( predicate, timeout );
}

QString nameOf( const QString& element )
{
    return PluginView::objectNameFor( PluginId, "main", element );
}

QJsonObject element( const QString& type, const QString& id, const QJsonObject& extra = {} )
{
    QJsonObject result{ { "type", type } };
    if ( !id.isEmpty() ) {
        result.insert( "id", id );
    }
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        result.insert( it.key(), it.value() );
    }
    return result;
}

// A plugin view with fake host hooks: pictures, an element menu that records its target, and
// a state store.
struct View {
    PluginView view;
    QSignalSpy events;
    QSignalSpy modified;
    QString menuId;
    QJsonObject menuTarget;
    QString menuElement;
    QJsonObject kept;
    QImage picture;
    QList<QSize> pictureSizes; // the sizes the view asked pictures for
    QList<QJsonObject> tooltipQueries; // tooltip/resolve parameters
    QList<QJsonValue> iconReferences;  // every reference the view resolved

    explicit View( const QJsonObject& root )
        : view( PluginId, "main", {} )
        , events( &view, &PluginView::eventRaised )
        , modified( &view, &PluginView::modifiedChanged )
    {
        picture = QImage( 30, 20, QImage::Format_ARGB32 );
        picture.fill( Qt::darkGreen );
        PluginViewQueries queries;
        queries.icon = [ this ]( const QJsonValue& reference ) {
            iconReferences.append( reference );
            return reference.toString().startsWith( "builtin:" ) ? QIcon( QPixmap( 16, 16 ) ) : QIcon{};
        };
        queries.picture = [ this ]( const QJsonValue& reference, const QSize& size ) {
            if ( reference.toString() == "image:pic" ) {
                pictureSizes.append( size );
            }
            return reference.toString() == "image:pic" ? picture : QImage{};
        };
        queries.elementMenu = [ this ]( QMenu* menu, const QString& elementId, const QString& id,
                                        const QJsonObject& target ) {
            menuElement = elementId;
            menuId = id;
            menuTarget = target;
            menu->addAction( "Plugin entry" )->setObjectName( "plugin_entry" );
        };
        queries.loadState = [ this ]( const QString& elementId ) {
            return kept.value( elementId ).toObject();
        };
        queries.saveState = [ this ]( const QString& elementId, const QJsonObject& state ) {
            kept.insert( elementId, state );
        };
        queries.resolveTooltip = [ this ]( const QJsonObject& params, int, bool,
                                           std::function<void( bool, const QJsonObject& )> done ) {
            tooltipQueries.append( params );
            done( true, QJsonObject{ { "text", "about " + params.value( "element_id" ).toString() } } );
            return QString( "t:%1" ).arg( tooltipQueries.size() );
        };
        view.setQueries( queries );
        view.setExtendedEvents( true );
        view.setRoot( root );
        view.resize( 640, 480 );
        view.show();
    }
    ~View()
    {
        view.setQueries( {} );
    }
    View( const View& ) = delete;
    View& operator=( const View& ) = delete;
    template <typename T>
    T* get( const QString& id ) const
    {
        return qobject_cast<T*>( view.element( id ) );
    }
    QJsonObject lastOf( const QString& type ) const
    {
        for ( auto at = events.size() - 1; at >= 0; --at ) {
            const auto event = events.at( at ).front().toJsonObject();
            if ( event.value( "type" ) == type ) {
                return event;
            }
        }
        return {};
    }
    int countOf( const QString& type ) const
    {
        int count = 0;
        for ( const auto& event : events ) {
            count += event.front().toJsonObject().value( "type" ) == type ? 1 : 0;
        }
        return count;
    }
    void patch( const QJsonArray& entries )
    {
        view.applyPatch( entries );
    }
};

QJsonObject column( const QJsonArray& children, const QJsonObject& extra = {} )
{
    auto result = element( "column", "body", extra );
    result.insert( "children", children );
    return result;
}

QJsonObject layoutRoot()
{
    const auto tab = []( const QString& id, const QString& title, const QJsonArray& children,
                         const QJsonObject& extra = {} ) {
        auto result = element( "tab", id, extra );
        result.insert( "title", title );
        result.insert( "children", children );
        return result;
    };
    return column( QJsonArray{
        element( "tabs", "tabs",
                 { { "current", "logs" },
                   { "children",
                     QJsonArray{ tab( "form_tab", "Form",
                                      QJsonArray{ element(
                                          "form", "form",
                                          { { "children",
                                              QJsonArray{
                                                  element( "field", "name",
                                                           { { "kind", "text" }, { "label", "Name" } } ),
                                                  element( "checkbox", "check",
                                                           { { "text", "Enabled" }, { "label", "Option" } } ),
                                                  element( "button", "apply", { { "label", "Apply" } } ) } } } ) } ),
                                 tab( "logs", "Logs", QJsonArray{ element( "label", "inner", { { "text", "x" } } ) },
                                      { { "badge", "3" }, { "icon", "builtin:info" } } ),
                                 element( "label", "stray", { { "text", "not a tab" } } ) } } } ),
        element( "splitter", "split",
                 { { "sizes", QJsonArray{ 100, 200 } },
                   { "children",
                     QJsonArray{ element( "label", "left", { { "text", "Left" }, { "stretch", 1 } } ),
                                 element( "label", "right", { { "text", "Right" }, { "stretch", 2 } } ) } } } ),
        element( "grid", "grid",
                 { { "columns", 3 },
                   { "children",
                     QJsonArray{ element( "label", "g1", { { "text", "1" }, { "span", 2 } } ),
                                 element( "label", "g2", { { "text", "2" } } ),
                                 element( "label", "g3", { { "text", "3" }, { "row_span", 2 } } ),
                                 element( "label", "g4", { { "text", "4" }, { "span", 9 } } ) } } } ),
        element( "row", "bar",
                 { { "children",
                     QJsonArray{ element( "label", "l1", { { "text", "A" }, { "align", "top" } } ),
                                 element( "separator", "sep" ),
                                 element( "spacer", "gap", { { "size", 24 } } ),
                                 element( "button", "wide",
                                          { { "label", "Wide" }, { "stretch", 3 }, { "min_width", 90 },
                                            { "max_width", 300 } } ) } } } ),
        element( "separator", "hsep" ),
        element( "tab", "orphan", { { "title", "Orphan" } } ) } );
}
} // namespace

TEST_CASE( "E17f layout containers: tabs, splitter, form, grid, separators and spacers",
           "[ui][workbench][E17]" )
{
    View fixture( layoutRoot() );
    // Tabs: a stray child and a tab outside tabs are skipped; `current` names a tab by id.
    auto* tabs = fixture.get<QTabWidget>( "tabs" );
    REQUIRE( tabs );
    REQUIRE( tabs->count() == 2 );
    REQUIRE( tabs->tabText( 0 ) == "Form" );
    REQUIRE( tabs->tabText( 1 ) == "Logs (3)" );
    REQUIRE_FALSE( tabs->tabIcon( 1 ).isNull() );
    REQUIRE( tabs->currentIndex() == 1 );
    REQUIRE( fixture.view.element( "stray" ) == nullptr );
    REQUIRE( fixture.view.element( "orphan" ) == nullptr );
    REQUIRE( fixture.events.isEmpty() );
    // The user changing the tab sends `changed` with the tab id and index; set_ui does it too.
    tabs->setCurrentIndex( 0 );
    auto changed = fixture.lastOf( "changed" );
    REQUIRE( changed.value( "element_id" ) == "tabs" );
    REQUIRE( changed.value( "value" ) == "form_tab" );
    REQUIRE( changed.value( "index" ) == 0 );
    REQUIRE( tabs->setProperty( "currentIndex", 1 ) );
    REQUIRE( fixture.lastOf( "changed" ).value( "value" ) == "logs" );
    // A plugin's `current` (id or index) sends nothing.
    const auto before = fixture.events.size();
    fixture.patch( { QJsonObject{ { "id", "tabs" }, { "set", QJsonObject{ { "current", 0 } } } } } );
    REQUIRE( tabs->currentIndex() == 0 );
    fixture.patch( { QJsonObject{ { "id", "tabs" },
                                  { "set", QJsonObject{ { "current", "logs" }, { "position", "left" } } } } } );
    REQUIRE( tabs->currentIndex() == 1 );
    REQUIRE( tabs->tabPosition() == QTabWidget::West );
    REQUIRE( fixture.events.size() == before );
    fixture.patch( { QJsonObject{ { "id", "logs" }, { "set", QJsonObject{ { "badge", "" }, { "title", "L&gs" } } } } } );
    REQUIRE( tabs->tabText( 1 ) == "L&&gs" );
    // A same-shaped root sending the same `current` keeps the user's tab.
    tabs->setCurrentIndex( 0 );
    fixture.view.setRoot( layoutRoot() );
    REQUIRE( fixture.get<QTabWidget>( "tabs" ) == tabs );
    REQUIRE( tabs->currentIndex() == 0 );
#if QT_VERSION >= QT_VERSION_CHECK( 5, 15, 0 )
    // A tab's `visible` hides its tab; showing it again never puts its page over the current one.
    fixture.patch( { QJsonObject{ { "id", "logs" }, { "set", QJsonObject{ { "visible", false } } } } } );
    REQUIRE_FALSE( tabs->isTabVisible( 1 ) );
    fixture.patch( { QJsonObject{ { "id", "logs" }, { "set", QJsonObject{ { "visible", true } } } } } );
    REQUIRE( tabs->isTabVisible( 1 ) );
    REQUIRE( tabs->currentIndex() == 0 );
    REQUIRE_FALSE( tabs->widget( 1 )->isVisible() );
#endif
    // A disabled tab cannot be chosen: its header follows `enabled`.
    fixture.patch( { QJsonObject{ { "id", "logs" }, { "set", QJsonObject{ { "enabled", false } } } } } );
    REQUIRE_FALSE( tabs->isTabEnabled( 1 ) );
    fixture.patch( { QJsonObject{ { "id", "logs" }, { "set", QJsonObject{ { "enabled", true } } } } } );
    REQUIRE( tabs->isTabEnabled( 1 ) );
    // A tab's tooltip is asked for where the pointer is: over its header in the tab bar.
    fixture.patch( { QJsonObject{ { "id", "logs" }, { "set", QJsonObject{ { "tooltip_provider", true } } } } } );
    {
        auto* bar = tabs->tabBar();
        const auto point = bar->tabRect( 1 ).center();
        QHelpEvent help( QEvent::ToolTip, point, bar->mapToGlobal( point ) );
        QApplication::sendEvent( bar, &help );
        REQUIRE( waitFor( [ & ] { return !fixture.tooltipQueries.isEmpty(); } ) );
        REQUIRE( fixture.tooltipQueries.last().value( "element_id" ) == "logs" );
        QToolTip::hideText();
    }
    {
        // A root of another shape replaces the tabs. The old tab widget is deleted later and may
        // change its current tab as its pages go: that speaks neither for the new tree nor at all.
        View rebuilt( layoutRoot() );
        QPointer<QTabWidget> old = rebuilt.get<QTabWidget>( "tabs" );
        auto other = layoutRoot();
        auto children = other.value( "children" ).toArray();
        children.append( element( "label", "extra", { { "text", "new" } } ) );
        other.insert( "children", children );
        rebuilt.view.setRoot( other );
        REQUIRE( rebuilt.get<QTabWidget>( "tabs" ) != old );
        const auto sent = rebuilt.events.size();
        REQUIRE( old );
        old->setCurrentIndex( 0 );
        QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
        REQUIRE( old.isNull() );
        REQUIRE( rebuilt.events.size() == sent );
    }

    // Splitter: sizes and the panes' stretch.
    auto* splitter = fixture.get<QSplitter>( "split" );
    REQUIRE( splitter );
    REQUIRE( splitter->orientation() == Qt::Horizontal );
    REQUIRE( splitter->count() == 2 );
    fixture.patch( { QJsonObject{ { "id", "split" }, { "set", QJsonObject{ { "orientation", "vertical" } } } } } );
    REQUIRE( splitter->orientation() == Qt::Vertical );

    // Form: a field's caption cell shows its label (and follows it), another element's `label`
    // is its caption, a button spans the row.
    auto* form = fixture.view.element( "form" )->findChild<QFormLayout*>();
    REQUIRE( form );
    REQUIRE( form->rowCount() == 3 );
    auto* caption = fixture.view.findChild<QLabel*>( nameOf( "name" ) + "_caption" );
    REQUIRE( caption );
    REQUIRE( caption->text() == "Name" );
    REQUIRE( caption->buddy() == fixture.view.element( "name" ) );
    REQUIRE_FALSE( fixture.view.findChild<QLabel*>( nameOf( "name" ) + "_label" )->isVisibleTo( &fixture.view ) );
    fixture.patch( { QJsonObject{ { "id", "name" }, { "set", QJsonObject{ { "label", "Full name" } } } } } );
    REQUIRE( caption->text() == "Full name" );
    REQUIRE( fixture.view.findChild<QLabel*>( nameOf( "check" ) + "_caption" )->text() == "Option" );
    REQUIRE( fixture.view.findChild<QLabel*>( nameOf( "apply" ) + "_caption" ) == nullptr );
    REQUIRE( form->itemAt( 2, QFormLayout::SpanningRole ) != nullptr );
    // A caption cell is part of its row: it follows the control's visible and enabled, and
    // shows `&` as written (a label with a buddy would read it as a mnemonic).
    auto* checkCaption = fixture.view.findChild<QLabel*>( nameOf( "check" ) + "_caption" );
    fixture.patch( { QJsonObject{ { "id", "check" }, { "set", QJsonObject{ { "visible", false } } } } } );
    REQUIRE_FALSE( checkCaption->isVisibleTo( &fixture.view ) );
    fixture.patch( { QJsonObject{ { "id", "check" },
                                  { "set", QJsonObject{ { "visible", true }, { "enabled", false },
                                                        { "label", "Save & Close" } } } } } );
    REQUIRE( checkCaption->isVisibleTo( &fixture.view ) );
    REQUIRE_FALSE( checkCaption->isEnabled() );
    REQUIRE( checkCaption->text() == "Save && Close" );
    fixture.patch( { QJsonObject{ { "id", "check" }, { "set", QJsonObject{ { "enabled", true } } } } } );
    REQUIRE( checkCaption->isEnabled() );

    // Grid: spans and row spans fill the rows from left to right; a span is at most `columns`.
    auto* grid = fixture.view.element( "grid" )->findChild<QGridLayout*>();
    REQUIRE( grid );
    const auto place = [ & ]( const QString& id ) {
        int row = 0;
        int column = 0;
        int rowSpan = 0;
        int columnSpan = 0;
        grid->getItemPosition( grid->indexOf( fixture.view.element( id ) ), &row, &column, &rowSpan,
                               &columnSpan );
        return QList<int>{ row, column, rowSpan, columnSpan };
    };
    REQUIRE( place( "g1" ) == QList<int>{ 0, 0, 1, 2 } );
    REQUIRE( place( "g2" ) == QList<int>{ 0, 2, 1, 1 } );
    REQUIRE( place( "g3" ) == QList<int>{ 1, 0, 2, 1 } );
    REQUIRE( place( "g4" ) == QList<int>{ 3, 0, 1, 3 } );

    // Row: a vertical separator, a fixed spacer, size hints, stretch and alignment.
    REQUIRE( fixture.get<QFrame>( "sep" )->frameShape() == QFrame::VLine );
    REQUIRE( fixture.get<QFrame>( "hsep" )->frameShape() == QFrame::HLine );
    REQUIRE( fixture.view.element( "gap" )->minimumWidth() == 24 );
    REQUIRE( fixture.view.element( "gap" )->maximumWidth() == 24 );
    auto* wide = fixture.get<QPushButton>( "wide" );
    REQUIRE( wide->minimumWidth() == 90 );
    REQUIRE( wide->maximumWidth() == 300 );
    auto* row = qobject_cast<QHBoxLayout*>( fixture.view.element( "bar" )->findChild<QHBoxLayout*>() );
    REQUIRE( row );
    REQUIRE( row->stretch( row->indexOf( wide ) ) == 3 );
    REQUIRE( row->itemAt( row->indexOf( fixture.view.element( "l1" ) ) )->alignment() == Qt::AlignTop );
    fixture.patch( { QJsonObject{ { "id", "wide" },
                                  { "set", QJsonObject{ { "stretch", 0 }, { "max_width", 0 } } } } } );
    REQUIRE( row->stretch( row->indexOf( wide ) ) == 0 );
    REQUIRE( wide->maximumWidth() == QWIDGETSIZE_MAX );
    // A hint of the wrong type is ignored: the element keeps the previous one.
    fixture.patch( { QJsonObject{ { "id", "wide" },
                                  { "set", QJsonObject{ { "min_width", "x" }, { "stretch", true } } } } } );
    REQUIRE( wide->minimumWidth() == 90 );
    REQUIRE( row->stretch( row->indexOf( wide ) ) == 0 );
    // A grid's places are part of the shape: a patch cannot move a cell.
    fixture.patch( { QJsonObject{ { "id", "g2" }, { "set", QJsonObject{ { "span", 3 } } } } } );
    REQUIRE( place( "g2" ) == QList<int>{ 0, 2, 1, 1 } );

    // A spacer without a size takes the free space: its row adds no stretch of its own, so the
    // last button sits at the right edge. A patch of other keys keeps the spacer's size hints.
    View pushed( column( QJsonArray{ element(
        "row", "nav",
        { { "children", QJsonArray{ element( "button", "back", { { "label", "Back" } } ),
                                    element( "spacer", "push", { { "min_width", 40 } } ),
                                    element( "button", "next", { { "label", "Next" } } ) } } } ) } ) );
    auto* nav = pushed.view.element( "nav" )->findChild<QHBoxLayout*>();
    REQUIRE( nav );
    REQUIRE( nav->count() == 3 ); // the three children and no trailing stretch
    auto* push = pushed.view.element( "push" );
    REQUIRE( nav->stretch( nav->indexOf( push ) ) == 1 );
    REQUIRE( push->minimumWidth() == 40 );
    pushed.patch( { QJsonObject{ { "id", "push" }, { "set", QJsonObject{ { "visible", true } } } } } );
    REQUIRE( push->minimumWidth() == 40 );
    pushed.patch( { QJsonObject{ { "id", "push" }, { "set", QJsonObject{ { "stretch", 2 } } } } } );
    REQUIRE( nav->stretch( nav->indexOf( push ) ) == 2 );
    REQUIRE( push->minimumWidth() == 40 );
    // Patches that change who stretches change the row's own stretch after its children too.
    pushed.patch( { QJsonObject{ { "id", "push" },
                                 { "set", QJsonObject{ { "stretch", 0 }, { "size", 10 } } } } } );
    REQUIRE( nav->count() == 4 ); // nothing stretches: the row adds its own stretch
    REQUIRE( nav->stretch( 3 ) == 1 );
    pushed.patch( { QJsonObject{ { "id", "next" }, { "set", QJsonObject{ { "stretch", 1 } } } } } );
    REQUIRE( nav->count() == 3 );
    pushed.patch( { QJsonObject{ { "id", "next" }, { "set", QJsonObject{ { "stretch", 0 } } } } } );
    REQUIRE( nav->count() == 4 );
    pushed.patch( { QJsonObject{ { "id", "push" }, { "set", QJsonObject{ { "size", QJsonValue() } } } } } );
    REQUIRE( nav->count() == 3 ); // a spacer without a size stretches again
}

TEST_CASE( "E17f the user's splitter sizes are kept per view", "[ui][workbench][E17]" )
{
    const auto root = column( QJsonArray{ element(
        "splitter", "split",
        { { "children", QJsonArray{ element( "label", "a", { { "text", "A" } } ),
                                    element( "label", "b", { { "text", "B" } } ) } } } ) } );
    QJsonObject kept;
    {
        View fixture( root );
        auto* splitter = fixture.get<QSplitter>( "split" );
        REQUIRE( waitFor( [ & ] { return splitter->width() > 100; } ) );
        // A drag (splitterMoved) is saved half a second later; a plugin's sizes are not saved.
        fixture.patch( { QJsonObject{ { "id", "split" }, { "set", QJsonObject{ { "sizes", QJsonArray{ 300, 50 } } } } } } );
        QTest::qWait( 700 );
        REQUIRE( fixture.kept.isEmpty() );
        splitter->setSizes( { 40, 560 } );
        const auto dragged = splitter->sizes();
        REQUIRE( dragged.at( 0 ) < dragged.at( 1 ) );
        Q_EMIT splitter->splitterMoved( dragged.at( 0 ), 1 );
        REQUIRE( waitFor( [ & ] { return fixture.kept.contains( "split" ); } ) );
        kept = fixture.kept;
        const auto stored = kept.value( "split" ).toObject().value( "sizes" ).toArray();
        REQUIRE( stored == QJsonArray{ dragged.at( 0 ), dragged.at( 1 ) } );
    }
    // Built again: the kept sizes win over the plugin's.
    View again( column( QJsonArray{ element( "label", "x", { { "text", "x" } } ) } ) );
    again.kept = kept;
    auto withSizes = root;
    auto children = withSizes.value( "children" ).toArray();
    auto split = children.at( 0 ).toObject();
    split.insert( "sizes", QJsonArray{ 500, 20 } );
    children.replace( 0, split );
    withSizes.insert( "children", children );
    again.view.setRoot( withSizes );
    auto* restored = again.get<QSplitter>( "split" );
    REQUIRE( waitFor( [ & ] {
        const auto sizes = restored->sizes();
        return sizes.size() == 2 && sizes.at( 0 ) > 0 && sizes.at( 0 ) < sizes.at( 1 );
    } ) );

    // An id-less splitter keeps nothing: there is no id to read its sizes back by.
    View anonymous( column( QJsonArray{ element(
        "splitter", {},
        { { "children", QJsonArray{ element( "label", "c", { { "text", "C" } } ),
                                    element( "label", "d", { { "text", "D" } } ) } } } ) } ) );
    auto* unnamed = anonymous.view.findChild<QSplitter*>();
    REQUIRE( unnamed );
    Q_EMIT unnamed->splitterMoved( 10, 1 );
    QTest::qWait( 700 ); // past the save delay of a named splitter
    REQUIRE( anonymous.kept.isEmpty() );
}

TEST_CASE( "E17f text views: plain text, Markdown, appends, limits, links and find",
           "[ui][workbench][E17]" )
{
    const auto root = [ & ]( const QJsonObject& extra ) {
        return column( QJsonArray{ element( "text_view", "log", extra ) } );
    };
    View fixture( root( { { "text", "line 1\n" }, { "monospace", true }, { "follow", true } } ) );
    auto* browser = fixture.get<QTextBrowser>( "log" );
    REQUIRE( browser );
    auto* view = qobject_cast<PluginTextView*>( browser->parentWidget() );
    REQUIRE( view );
    REQUIRE( browser->property( "plainText" ).toString() == "line 1\n" );
    REQUIRE( browser->isReadOnly() );
    // Appends add at the end; a same-shaped root with the same text keeps them.
    fixture.patch( { QJsonObject{ { "id", "log" }, { "append", "line 2\n" } } } );
    REQUIRE( view->text() == "line 1\nline 2\n" );
    fixture.view.setRoot( root( { { "text", "line 1\n" }, { "monospace", true }, { "follow", true } } ) );
    REQUIRE( view->text() == "line 1\nline 2\n" );
    // An append over 64 KiB is refused.
    fixture.patch( { QJsonObject{ { "id", "log" }, { "append", QString( 70000, 'x' ) } } } );
    REQUIRE( view->text() == "line 1\nline 2\n" );
    // `set: {text}` replaces; a root with other text replaces too.
    fixture.patch( { QJsonObject{ { "id", "log" }, { "set", QJsonObject{ { "text", "fresh\n" } } } } } );
    REQUIRE( view->text() == "fresh\n" );
    fixture.view.setRoot( root( { { "text", "other\n" } } ) );
    REQUIRE( view->text() == "other\n" );
    // A line limit drops the oldest lines.
    fixture.patch( { QJsonObject{ { "id", "log" }, { "set", QJsonObject{ { "max_lines", 3 } } } } } );
    for ( int index = 0; index < 5; ++index ) {
        fixture.patch( { QJsonObject{ { "id", "log" }, { "append", QString( "n%1\n" ).arg( index ) } } } );
    }
    REQUIRE( view->text() == "n3\nn4\n" );
    // Following new text keeps the end in view.
    fixture.patch( { QJsonObject{ { "id", "log" },
                                  { "set", QJsonObject{ { "max_lines", 0 }, { "follow", true },
                                                        { "wrap", false } } } } } );
    for ( int index = 0; index < 200; ++index ) {
        fixture.patch( { QJsonObject{ { "id", "log" }, { "append", QString( "row %1\n" ).arg( index ) } } } );
    }
    REQUIRE( waitFor( [ & ] {
        return browser->verticalScrollBar()->maximum() > 0
               && browser->verticalScrollBar()->value() == browser->verticalScrollBar()->maximum();
    } ) );
    REQUIRE( browser->lineWrapMode() == QTextEdit::NoWrap );
    // A same-shaped root that sends `follow` again with the text the plugin last set leaves the
    // reader's place; new text goes to the end again.
    auto* bar = browser->verticalScrollBar();
    QTest::qWait( 50 ); // the appends' queued scrolls to the end run first
    bar->setValue( 0 ); // the user scrolled up
    fixture.view.setRoot( root( { { "text", "other\n" }, { "follow", true }, { "wrap", false } } ) );
    REQUIRE( view->text().endsWith( "row 199\n" ) ); // the appended text stays
    QCoreApplication::processEvents();
    REQUIRE( bar->value() == 0 );
    fixture.patch( { QJsonObject{ { "id", "log" }, { "append", "row 200\n" } } } );
    REQUIRE( waitFor( [ & ] { return bar->maximum() > 0 && bar->value() == bar->maximum(); } ) );
    // Markdown: rendered with HTML off; a link click sends `link`, the host never follows it.
    fixture.patch( { QJsonObject{ { "id", "log" },
                                  { "set", QJsonObject{ { "format", "markdown" },
                                                        { "text", "# Notes\n\n[docs](https://example.com/docs) <i>x</i>\n" } } } } } );
    REQUIRE( view->markdown() );
#if QT_VERSION >= QT_VERSION_CHECK( 5, 14, 0 )
    REQUIRE( browser->toPlainText().contains( "Notes" ) );
    REQUIRE_FALSE( browser->toPlainText().contains( "# Notes" ) );
    REQUIRE( browser->toPlainText().contains( "<i>x</i>" ) );
#endif
    Q_EMIT browser->anchorClicked( QUrl( "https://example.com/docs" ) );
    const auto link = fixture.lastOf( "link" );
    REQUIRE( link.value( "element_id" ) == "log" );
    REQUIRE( link.value( "value" ).toObject().value( "href" ) == "https://example.com/docs" );
    REQUIRE( browser->source().isEmpty() );
    // Ctrl+F shows the find bar, also where the window has a Find shortcut of its own (CILogg's):
    // the text claims the key, so it is neither ambiguous nor the window's; Esc closes the bar.
    int windowFinds = 0;
    auto* windowFind = new QShortcut( QKeySequence::Find, &fixture.view );
    windowFind->setContext( Qt::WindowShortcut );
    QObject::connect( windowFind, &QShortcut::activated, [ &windowFinds ] { ++windowFinds; } );
    QObject::connect( windowFind, &QShortcut::activatedAmbiguously, [ &windowFinds ] { ++windowFinds; } );
    browser->setFocus();
    REQUIRE( QTest::qWaitForWindowActive( &fixture.view ) );
    QTest::keyClick( browser, Qt::Key_F, Qt::ControlModifier );
    REQUIRE( waitFor( [ & ] { return view->findBar()->isVisible(); } ) );
    REQUIRE( windowFinds == 0 );
    REQUIRE( view->findBar()->objectName() == nameOf( "log" ) + "_find" );
    // Again in the find bar: the key is still the text's, so the window's Find takes nothing.
    REQUIRE( waitFor( [ & ] { return view->findBar()->hasFocus(); } ) );
    QTest::keyClick( view->findBar(), Qt::Key_F, Qt::ControlModifier );
    REQUIRE( windowFinds == 0 );
    REQUIRE( view->findBar()->isVisible() );
    view->findBar()->setText( "docs" );
    QTest::keyClick( view->findBar(), Qt::Key_Escape );
    REQUIRE_FALSE( view->findBar()->isVisible() );
}

TEST_CASE( "E17f images, sparklines and charts", "[ui][workbench][E17]" )
{
    const auto series = QJsonArray{
        QJsonObject{ { "id", "temp" }, { "title", "Temperature" }, { "tone", "error" },
                     { "points", QJsonArray{ QJsonArray{ 0, 20 }, QJsonArray{ 1, 21.5 }, "bad" } } },
        QJsonObject{ { "id", "load" }, { "points", QJsonArray{ 5, 6, 7 } } },
        QJsonObject{ { "points", QJsonArray{ 1 } } } };
    const auto root = [ & ]( const QJsonArray& chartSeries ) {
        return column( QJsonArray{
            element( "image", "pic", { { "source", "image:pic" }, { "alt", "Board photo" } } ),
            element( "image", "missing", { { "source", "image:gone" }, { "alt", "No photo" } } ),
            element( "sparkline", "spark", { { "values", QJsonArray{ 1, 3, QJsonValue::Null, 2 } } } ),
            element( "chart", "chart",
                     { { "series", chartSeries }, { "x_title", "Time" }, { "y_title", "Value" } } ) } );
    };
    View fixture( root( series ) );
    auto* picture = fixture.get<PluginImage>( "pic" );
    REQUIRE( picture );
    REQUIRE( picture->image().size() == QSize( 30, 20 ) );
    REQUIRE( picture->accessibleName() == "Board photo" );
    // Without width and height the view asks for the picture's own size (an invalid size).
    REQUIRE_FALSE( fixture.pictureSizes.isEmpty() );
    REQUIRE_FALSE( fixture.pictureSizes.back().isValid() );
    REQUIRE( fixture.get<PluginImage>( "missing" )->image().isNull() );
    // New pictures show after the plugin's images change.
    fixture.picture = QImage( 10, 10, QImage::Format_ARGB32 );
    fixture.view.refreshImages();
    REQUIRE( picture->image().size() == QSize( 10, 10 ) );
    fixture.patch( { QJsonObject{ { "id", "pic" },
                                  { "set", QJsonObject{ { "width", 40 }, { "height", 30 }, { "scale", "fill" } } } } } );
    REQUIRE( picture->size() == QSize( 40, 30 ) );
    REQUIRE( picture->scale() == PluginImage::Scale::Fill );
    REQUIRE( fixture.pictureSizes.back() == QSize( 40, 30 ) );
    // A new box keeps the element's size hints (as built, they apply over the box).
    auto* missing = fixture.get<PluginImage>( "missing" );
    fixture.patch( { QJsonObject{ { "id", "missing" }, { "set", QJsonObject{ { "max_width", 300 } } } } } );
    REQUIRE( missing->maximumWidth() == 300 );
    fixture.patch( { QJsonObject{ { "id", "missing" }, { "set", QJsonObject{ { "height", 50 } } } } } );
    REQUIRE( missing->maximumHeight() == 50 );
    REQUIRE( missing->maximumWidth() == 300 );

    auto* spark = fixture.get<PluginSparkline>( "spark" );
    REQUIRE( spark->values().size() == 4 );
    REQUIRE( std::isnan( spark->values().at( 2 ) ) );
    REQUIRE( spark->accessibleDescription().contains( "2" ) );

    // Chart: pairs, numbers alone at their position, an invalid point and an id-less series
    // skipped; the default legend shows with more than one series.
    auto* chart = fixture.get<PluginChart>( "chart" );
    REQUIRE( chart->series().size() == 2 );
    REQUIRE( chart->pointCount( "temp" ) == 2 );
    REQUIRE( chart->series().at( 1 ).points == QVector<QPointF>{ { 0, 5 }, { 1, 6 }, { 2, 7 } } );
    REQUIRE( chart->legendShown() );
    REQUIRE( chart->accessibleName() == "Value over Time" );
    // Appends continue a series' x; clear empties one series or all; window keeps the last.
    fixture.patch( { QJsonObject{ { "id", "chart" },
                                  { "append_points",
                                    QJsonArray{ QJsonObject{ { "series", "load" }, { "points", QJsonArray{ 8 } } },
                                                QJsonObject{ { "series", "ghost" }, { "points", QJsonArray{ 1 } } } } } } } );
    REQUIRE( chart->series().at( 1 ).points.back() == QPointF( 3, 8 ) );
    // One [x, y] pair (QJsonArray{ QJsonArray{...} } would copy the inner array).
    QJsonArray pair;
    pair.append( QJsonArray{ 5, 30 } );
    fixture.patch( { QJsonObject{ { "id", "chart" },
                                  { "append_points", QJsonObject{ { "series", "temp" }, { "points", pair } } } } } );
    REQUIRE( chart->pointCount( "temp" ) == 3 );
    REQUIRE( chart->series().at( 0 ).points.back() == QPointF( 5, 30 ) );
    // A same-shaped root with the same series keeps the appended points.
    fixture.view.setRoot( root( series ) );
    REQUIRE( chart->pointCount( "temp" ) == 3 );
    fixture.patch( { QJsonObject{ { "id", "chart" }, { "clear", QJsonArray{ "temp" } } } } );
    REQUIRE( chart->pointCount( "temp" ) == 0 );
    REQUIRE( chart->pointCount( "load" ) == 4 );
    fixture.patch( { QJsonObject{ { "id", "chart" }, { "set", QJsonObject{ { "window", 2 }, { "kind", "bar" } } } } } );
    REQUIRE( chart->pointCount( "load" ) == 2 );
    REQUIRE( chart->kind() == PluginChart::Kind::Bar );
    fixture.patch( { QJsonObject{ { "id", "chart" }, { "clear", true } } } );
    REQUIRE( chart->pointCount( "load" ) == 0 );
    // Each kind paints without trouble, with and without points.
    for ( const auto* kind : { "line", "bar", "scatter", "area" } ) {
        fixture.patch( { QJsonObject{ { "id", "chart" },
                                      { "set", QJsonObject{ { "kind", kind }, { "x_kind", "time" } } },
                                      { "append_points", QJsonObject{ { "series", "load" },
                                                                      { "points", QJsonArray{ QJsonArray{ 1.7e12, 1 },
                                                                                              QJsonArray{ 1.7e12 + 1000, 3 } } } } } } } );
        REQUIRE_FALSE( chart->grab().isNull() );
    }
    REQUIRE_FALSE( spark->grab().isNull() );
    // At most 8 series.
    QJsonArray many;
    for ( int index = 0; index < 10; ++index ) {
        many.append( QJsonObject{ { "id", QString( "s%1" ).arg( index ) }, { "points", QJsonArray{ index } } } );
    }
    fixture.patch( { QJsonObject{ { "id", "chart" }, { "set", QJsonObject{ { "series", many } } } } } );
    REQUIRE( chart->series().size() == 8 );
}

TEST_CASE( "E17f an empty or dropped icon removes the icon without resolving an empty reference",
           "[ui][workbench][E17]" )
{
    // #168: an empty reference used to reach the icon registry, which warned "An icon reference
    // is 1 to 256 characters" although removing the icon is what was meant.
    const auto root = []( const std::optional<QString>& icon ) {
        QJsonObject button{ { "label", "Pin" } };
        QJsonObject tab{ { "title", "Page" },
                         { "children", QJsonArray{ element( "label", "inside", { { "text", "x" } } ) } } };
        if ( icon ) {
            button.insert( "icon", *icon );
            tab.insert( "icon", *icon );
        }
        return column( QJsonArray{
            element( "button", "pin", button ),
            element( "tabs", "pages", { { "children", QJsonArray{ element( "tab", "page", tab ) } } } ) } );
    };
    View fixture( root( QStringLiteral( "builtin:pin" ) ) );
    auto* pin = fixture.get<QPushButton>( "pin" );
    auto* tabs = fixture.get<QTabWidget>( "pages" );
    REQUIRE( pin );
    REQUIRE( tabs );
    REQUIRE_FALSE( pin->icon().isNull() );
    REQUIRE_FALSE( tabs->tabIcon( 0 ).isNull() );
    const auto emptyResolved = [ & ] {
        return std::any_of( fixture.iconReferences.begin(), fixture.iconReferences.end(),
                            []( const QJsonValue& reference ) {
                                return reference.isString() && reference.toString().isEmpty();
                            } );
    };
    // A same-shaped root that drops `icon`.
    fixture.view.setRoot( root( std::nullopt ) );
    REQUIRE( fixture.get<QPushButton>( "pin" ) == pin );
    REQUIRE( pin->icon().isNull() );
    REQUIRE( tabs->tabIcon( 0 ).isNull() );
    REQUIRE_FALSE( emptyResolved() );
    // The icon back, then the plugin's own `icon: ""`, as a root and as a patch.
    fixture.view.setRoot( root( QStringLiteral( "builtin:pin" ) ) );
    REQUIRE_FALSE( pin->icon().isNull() );
    fixture.view.setRoot( root( QString() ) );
    REQUIRE( pin->icon().isNull() );
    REQUIRE( tabs->tabIcon( 0 ).isNull() );
    fixture.patch( { QJsonObject{ { "id", "pin" }, { "set", QJsonObject{ { "icon", "builtin:pin" } } } },
                     QJsonObject{ { "id", "page" }, { "set", QJsonObject{ { "icon", "builtin:pin" } } } } } );
    REQUIRE_FALSE( pin->icon().isNull() );
    fixture.patch( { QJsonObject{ { "id", "pin" }, { "set", QJsonObject{ { "icon", "" } } } },
                     QJsonObject{ { "id", "page" }, { "set", QJsonObject{ { "icon", "" } } } } } );
    REQUIRE( pin->icon().isNull() );
    REQUIRE( tabs->tabIcon( 0 ).isNull() );
    REQUIRE_FALSE( emptyResolved() );
}

TEST_CASE( "E17f buttons: icons, styles, checkable, drop-down menus, shortcuts and default",
           "[ui][workbench][E17]" )
{
    View fixture( column( QJsonArray{
        element( "field", "name", { { "kind", "text" } } ),
        element( "field", "notes", { { "kind", "multiline" } } ),
        element( "button", "pin", { { "label", "Pin" }, { "checkable", true }, { "icon", "builtin:pin" } } ),
        element( "button", "more", { { "label", "More" }, { "menu", "more_menu" } } ),
        element( "button", "link", { { "label", "Docs" }, { "style", "link" }, { "event", true } } ),
        element( "button", "go", { { "label", "Go" }, { "event", true }, { "default", true },
                                   { "shortcut", "Ctrl+Alt+G" } } ),
        element( "button", "copy", { { "label", "Copy" }, { "event", true }, { "shortcut", "Ctrl+C" } } ),
        element( "button", "twin", { { "label", "Twin" }, { "event", true }, { "shortcut", "Ctrl+Alt+G" } } ) } ) );
    // Checkable: the user's toggle sends `changed` and never `clicked`; the plugin's sends nothing.
    auto* pin = fixture.get<QPushButton>( "pin" );
    REQUIRE( pin->isCheckable() );
    REQUIRE_FALSE( pin->icon().isNull() );
    pin->click();
    REQUIRE( fixture.lastOf( "changed" ).value( "value" ) == true );
    REQUIRE( fixture.countOf( "clicked" ) == 0 );
    const auto changes = fixture.countOf( "changed" );
    fixture.patch( { QJsonObject{ { "id", "pin" }, { "set", QJsonObject{ { "checked", false } } } } } );
    REQUIRE_FALSE( pin->isChecked() );
    REQUIRE( fixture.countOf( "changed" ) == changes );
    REQUIRE( pin->setProperty( "checked", true ) ); // set_ui
    REQUIRE( fixture.countOf( "changed" ) == changes + 1 );
    // A same-shaped root that sends the `checked` the plugin last set keeps the user's toggle; a
    // root with another value sets it.
    const auto pinRoot = [ & ]( bool checked ) {
        return column( QJsonArray{ element( "button", "pin", { { "label", "Pin" },
                                                               { "checkable", true },
                                                               { "checked", checked } } ) } );
    };
    {
        // A window of its own, closed before the keys below need `fixture` active.
        View toggled( pinRoot( false ) );
        auto* toggle = toggled.get<QPushButton>( "pin" );
        toggle->click();
        REQUIRE( toggle->isChecked() );
        toggled.view.setRoot( pinRoot( false ) );
        REQUIRE( toggled.get<QPushButton>( "pin" ) == toggle );
        REQUIRE( toggle->isChecked() );
        toggled.view.setRoot( pinRoot( true ) );
        toggled.view.setRoot( pinRoot( false ) );
        REQUIRE_FALSE( toggle->isChecked() );
        REQUIRE( toggled.countOf( "changed" ) == 1 ); // the plugin's values are never echoed
    }
    {
        // A root of another shape builds the view again: the old button's shortcut goes with
        // the old tree, so the new one is the only one with its keys (two would both stay silent).
        const auto keyed = []( const QJsonArray& more ) {
            QJsonArray children{ element( "button", "go", { { "label", "Go" }, { "event", true },
                                                           { "shortcut", "Ctrl+Alt+G" } } ) };
            for ( const auto& child : more ) {
                children.append( child );
            }
            return column( children );
        };
        View rebuilt( keyed( {} ) );
        const QKeySequence keys( "Ctrl+Alt+G" );
        const auto withKeys = [ & ] {
            int count = 0;
            for ( const auto* each : rebuilt.view.findChildren<QShortcut*>() ) {
                count += each->key() == keys ? 1 : 0;
            }
            return count;
        };
        REQUIRE( withKeys() == 1 );
        rebuilt.view.setRoot( keyed( { element( "label", "note", { { "text", "x" } } ) } ) );
        REQUIRE( withKeys() == 1 );
        auto* fresh = rebuilt.view.findChild<QShortcut*>( nameOf( "go" ) + "_shortcut" );
        REQUIRE( fresh );
        Q_EMIT fresh->activated();
        REQUIRE( rebuilt.lastOf( "clicked" ).value( "element_id" ) == "go" );
        rebuilt.view.setRoot( column( QJsonArray{ element( "label", "only", { { "text", "y" } } ) } ) );
        REQUIRE( withKeys() == 0 );
    }

    // A drop-down menu: the element menu with the button as the element and no row.
    auto* more = fixture.get<QPushButton>( "more" );
    REQUIRE( more->menu() );
    REQUIRE( more->menu()->objectName() == nameOf( "more" ) + "_menu" );
    std::unique_ptr<QMenu> menu( fixture.view.buildElementMenu( "more", {} ) );
    REQUIRE( menu );
    REQUIRE( menu->findChild<QAction*>( "plugin_entry" ) );
    REQUIRE( fixture.menuElement == "more" );
    REQUIRE( fixture.menuId == "more_menu" );
    REQUIRE( fixture.menuTarget == QJsonObject{ { "button", true } } );
    QString menuId;
    QJsonObject target;
    QString error;
    REQUIRE( fixture.view.elementMenu( "more", {}, &menuId, &target, &error ) );
    REQUIRE( menuId == "more_menu" );
    REQUIRE_FALSE( fixture.view.elementMenu( "more", "r1", &menuId, &target, &error ) );
    REQUIRE_FALSE( fixture.view.elementMenu( "pin", {}, &menuId, &target, &error ) );
    // Opening it rebuilds it.
    Q_EMIT more->menu()->aboutToShow();
    REQUIRE( more->menu()->findChild<QAction*>( "plugin_entry" ) );
    REQUIRE( fixture.countOf( "clicked" ) == 0 );

    // Link style: flat and underlined.
    auto* link = fixture.get<QPushButton>( "link" );
    REQUIRE( link->isFlat() );
    REQUIRE( link->font().underline() );
    // A style is a look: a patch changes it in place.
    fixture.patch( { QJsonObject{ { "id", "link" }, { "set", QJsonObject{ { "style", "tool" } } } } } );
    REQUIRE( fixture.get<QPushButton>( "link" ) == link );
    REQUIRE( link->isFlat() );
    REQUIRE_FALSE( link->font().underline() );

    // Shortcuts: a reserved key and a key another button has are refused.
    auto* shortcut = fixture.view.findChild<QShortcut*>( nameOf( "go" ) + "_shortcut" );
    REQUIRE( shortcut );
    REQUIRE( shortcut->key() == QKeySequence( "Ctrl+Alt+G" ) );
    REQUIRE( shortcut->context() == Qt::WidgetWithChildrenShortcut );
    REQUIRE( fixture.view.findChild<QShortcut*>( nameOf( "copy" ) + "_shortcut" ) == nullptr );
    REQUIRE( fixture.view.findChild<QShortcut*>( nameOf( "twin" ) + "_shortcut" ) == nullptr );
    Q_EMIT shortcut->activated();
    REQUIRE( fixture.lastOf( "clicked" ).value( "element_id" ) == "go" );

    // Enter presses the default button, but not inside a multi-line field.
    fixture.view.activateWindow();
    REQUIRE( QTest::qWaitForWindowActive( &fixture.view ) );
    const auto clicks = fixture.countOf( "clicked" );
    auto* name = fixture.view.element( "name" );
    name->setFocus();
    QTest::keyClick( name, Qt::Key_Return );
    REQUIRE( fixture.countOf( "clicked" ) == clicks + 1 );
    auto* notes = fixture.view.element( "notes" );
    notes->setFocus();
    QTest::keyClick( notes, Qt::Key_Return );
    REQUIRE( fixture.countOf( "clicked" ) == clicks + 1 );
    // A disabled default button is not pressed.
    fixture.patch( { QJsonObject{ { "id", "go" }, { "set", QJsonObject{ { "enabled", false } } } } } );
    name->setFocus();
    QTest::keyClick( name, Qt::Key_Return );
    REQUIRE( fixture.countOf( "clicked" ) == clicks + 1 );
    // A menu button never runs: no `clicked`.
    more->click();
    REQUIRE( fixture.countOf( "clicked" ) == clicks + 1 );
}

TEST_CASE( "E17f a button shortcut gives way to CILogg's shortcuts, not to another view's",
           "[ui][workbench][E17]" )
{
    // Two views in one CILogg window, as two docks of plugins are: each button shortcut acts
    // only while the focus is in its own view, so the same keys in both are no conflict.
    QWidget window;
    const QShortcut own( QKeySequence( "Ctrl+Alt+K" ), &window ); // one of CILogg's shortcuts
    auto* first = new PluginView( PluginId, "first", {}, &window );
    auto* second = new PluginView( PluginId, "second", {}, &window );
    const auto root = []( const QString& keys ) {
        return column( QJsonArray{ element( "button", "run", { { "label", "Run" }, { "event", true },
                                                              { "shortcut", keys } } ) } );
    };
    const auto shortcutOf = []( const PluginView* view, const QString& viewId ) {
        return view->findChild<QShortcut*>( PluginView::objectNameFor( PluginId, viewId, "run" )
                                            + "_shortcut" );
    };
    for ( auto* view : { first, second } ) {
        view->setDialogParent( &window );
        view->setRoot( root( "Ctrl+Alt+R" ) );
    }
    REQUIRE( shortcutOf( first, "first" ) );
    REQUIRE( shortcutOf( second, "second" ) );
    REQUIRE( shortcutOf( second, "second" )->key() == QKeySequence( "Ctrl+Alt+R" ) );
    // CILogg's own keys are still refused.
    second->setRoot( root( "Ctrl+Alt+K" ) );
    REQUIRE( shortcutOf( second, "second" ) == nullptr );
    REQUIRE( shortcutOf( first, "first" ) );
}

TEST_CASE( "E17f the busy overlay covers a container, and Cancel sends cancel",
           "[ui][workbench][E17]" )
{
    const auto root = [ & ]( const QJsonObject& busy ) {
        return column( QJsonArray{ element( "field", "name", { { "kind", "text" } } ),
                                   element( "group", "box",
                                            { { "title", "Box" },
                                              { "children", QJsonArray{ element( "button", "inner",
                                                                                 { { "label", "Inner" } } ) } } } ) },
                       busy );
    };
    View fixture( root( {} ) );
    auto* name = fixture.view.element( "name" );
    REQUIRE( QTest::qWaitForWindowActive( &fixture.view ) );
    name->setFocus();
    fixture.patch( { QJsonObject{ { "id", "body" },
                                  { "set", QJsonObject{ { "busy", true }, { "busy_text", "Flashing…" },
                                                        { "busy_cancel", true } } } } } );
    auto* overlay = fixture.view.findChild<PluginBusyOverlay*>( nameOf( "body" ) + "_busy" );
    REQUIRE( overlay );
    REQUIRE( overlay->isVisible() );
    REQUIRE( overlay->label()->text() == "Flashing…" );
    REQUIRE( overlay->cancelButton()->isVisible() );
    REQUIRE_FALSE( name->isEnabled() ); // the contents
    REQUIRE( fixture.view.element( "body" )->isEnabled() ); // not the container itself
    REQUIRE_FALSE( name->hasFocus() );
    REQUIRE( waitFor( [ & ] { return overlay->geometry().size() == fixture.view.element( "body" )->size(); } ) );
    // Cancel sends `cancel` once, then waits for the next busy period.
    overlay->cancelButton()->click();
    REQUIRE( fixture.lastOf( "cancel" ).value( "element_id" ) == "body" );
    REQUIRE( fixture.lastOf( "cancel" ).value( "value" ).isNull() );
    REQUIRE_FALSE( overlay->cancelButton()->isEnabled() );
    // A same-shaped root that still says busy leaves the pressed Cancel as it is ...
    const QJsonObject busy{ { "busy", true }, { "busy_text", "Flashing…" }, { "busy_cancel", true } };
    fixture.view.setRoot( root( busy ) );
    REQUIRE( overlay->isVisible() );
    REQUIRE_FALSE( overlay->cancelButton()->isEnabled() );
    // ... a patch that sets busy again makes Cancel available again.
    fixture.patch( { QJsonObject{ { "id", "body" }, { "set", QJsonObject{ { "busy", true } } } } } );
    REQUIRE( overlay->cancelButton()->isEnabled() );
    fixture.patch( { QJsonObject{ { "id", "body" }, { "set", QJsonObject{ { "busy", false } } } } } );
    REQUIRE_FALSE( overlay->isVisible() );
    REQUIRE( name->isEnabled() );
    // A group's contents only, and an element disabled by the plugin stays disabled.
    fixture.patch( { QJsonObject{ { "id", "inner" }, { "set", QJsonObject{ { "enabled", false } } } } } );
    fixture.patch( { QJsonObject{ { "id", "box" }, { "set", QJsonObject{ { "busy", true } } } } } );
    REQUIRE( fixture.view.findChild<PluginBusyOverlay*>( nameOf( "box" ) + "_busy" )->isVisible() );
    REQUIRE( name->isEnabled() );
    fixture.patch( { QJsonObject{ { "id", "box" }, { "set", QJsonObject{ { "busy", false } } } } } );
    REQUIRE_FALSE( fixture.view.element( "inner" )->isEnabled() );
}

TEST_CASE( "E17f modified roots and focus patches", "[ui][workbench][E17]" )
{
    View fixture( column( QJsonArray{ element( "field", "name", { { "kind", "text" } } ),
                                      element( "label", "info", { { "text", "x" }, { "modified", true } } ) },
                          { { "modified", true }, { "close_confirm", "Keep?" } } ) );
    REQUIRE( fixture.view.modified() );
    REQUIRE( fixture.view.closeConfirmText() == "Keep?" );
    REQUIRE( fixture.modified.size() == 1 );
    fixture.patch( { QJsonObject{ { "id", "body" }, { "set", QJsonObject{ { "modified", false } } } } } );
    REQUIRE_FALSE( fixture.view.modified() );
    REQUIRE( fixture.modified.size() == 2 );
    // A new tree starts unmodified.
    fixture.patch( { QJsonObject{ { "id", "body" }, { "set", QJsonObject{ { "modified", true } } } } } );
    fixture.view.setRoot( element( "row", "other", { { "children", QJsonArray{} } } ) );
    REQUIRE_FALSE( fixture.view.modified() );

    // Focus moves only while the view is active.
    View focus( column( QJsonArray{ element( "field", "a", { { "kind", "text" } } ),
                                    element( "field", "b", { { "kind", "text" } } ) } ) );
    focus.view.activateWindow();
    REQUIRE( QTest::qWaitForWindowActive( &focus.view ) );
    focus.view.element( "a" )->setFocus();
    focus.patch( { QJsonObject{ { "id", "b" }, { "focus", true } } } );
    REQUIRE( waitFor( [ & ] { return focus.view.element( "b" )->hasFocus(); } ) );
    QWidget other;
    other.show();
    other.activateWindow();
    REQUIRE( QTest::qWaitForWindowActive( &other ) );
    focus.patch( { QJsonObject{ { "id", "a" }, { "focus", true } } } );
    QCoreApplication::processEvents();
    REQUIRE( QApplication::activeWindow() == &other );
    // setFocus() in an inactive window would still move the focus that window gives back when
    // it is activated again; the refused patch leaves it on "b".
    REQUIRE( focus.view.window()->focusWidget() == focus.view.element( "b" ) );
}

namespace {
QJsonObject e17fManifest( const QString& executable )
{
    return {
        { "id", PluginId },
        { "name", "E17f fixture" },
        { "version", "1.0.0" },
        { "protocol_version", 1 },
        { "executable", executable },
        { "capabilities", QJsonArray{ "tool" } },
        { "handles", QJsonArray{ "ui/command" } },
        { "actions", QJsonArray{ QJsonObject{ { "id", "summary" }, { "capability", "tool" } } } },
        { "views",
          QJsonArray{ QJsonObject{ { "id", "layouts" }, { "title", "Layouts" } },
                      QJsonObject{ { "id", "ask" },
                                   { "title", "Ask" },
                                   { "kind", "dialog" },
                                   { "menu", false },
                                   { "buttons", QJsonArray{ "ok", "cancel", "apply" } },
                                   { "default", "ok" } },
                      QJsonObject{ { "id", "confirm" },
                                   { "title", "Confirm" },
                                   { "kind", "dialog" },
                                   { "menu", false },
                                   { "buttons", QJsonArray{ "ok", "cancel" } },
                                   { "confirm_by_plugin", true } },
                      QJsonObject{ { "id", "plain" },
                                   { "title", "Plain" },
                                   { "kind", "dialog" },
                                   { "menu", false } } } },
        { "menus",
          QJsonArray{ QJsonObject{
              { "id", "more_menu" },
              { "location", "element" },
              { "items", QJsonArray{ QJsonObject{ { "id", "again" }, { "title", "Again" },
                                                  { "event", true } } } } } } } };
}

QList<QJsonObject> recorded( const QString& folder, const QString& method )
{
    QList<QJsonObject> result;
    QFile log( QDir( folder ).filePath( "host-messages.jsonl" ) );
    if ( !log.open( QIODevice::ReadOnly ) ) {
        return result;
    }
    for ( const auto& line : log.readAll().split( '\n' ) ) {
        const auto frame = QJsonDocument::fromJson( line ).object();
        if ( frame.value( "method" ) == method ) {
            result.append( frame.value( "params" ).toObject() );
        }
    }
    return result;
}

// A MainWindow with the fixture plugin enabled; views open on demand.
struct LayoutsWindow {
    QTemporaryDir temp;
    QString folder;
    std::unique_ptr<PluginManager> manager;
    std::unique_ptr<MainWindow> window;
    QStringList confirmations; // object names of the discard questions asked
    bool discard = false;
    std::function<void()> duringQuestion; // runs while a question is open
    // When set, asks instead of the answer above (a real modal question, say).
    std::function<bool( QWidget*, const QString& )> askWith;

    LayoutsWindow()
    {
        PluginMenuRegistry::instance().forgetPlugin( PluginId );
        const auto shipped = QDir( temp.path() ).filePath( "shipped" );
        folder = QDir( shipped ).filePath( PluginId );
        REQUIRE( QDir().mkpath( folder ) );
        const auto executable = QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName();
        QFile file( QDir( folder ).filePath( "plugin.json" ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( QJsonDocument( e17fManifest( executable ) ).toJson() );
        file.close();
        REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                              QDir( folder ).filePath( executable ) ) );
        manager = std::make_unique<PluginManager>( QDir( temp.path() ).filePath( "data" ), shipped );
        REQUIRE( manager->manage( "enable", PluginId, {} ).ok() );
        auto session = std::make_shared<Session>();
        window = std::make_unique<MainWindow>( WindowSession{ session, "PluginSdkE17f", 0 } );
        window->setPluginManager( *manager, [ this ]( const CommanderRequest& request ) {
            const auto result = manager->manage( request.pluginOperation, request.pluginId,
                                                 request.pluginArguments );
            return result.ok()
                       ? commanderSuccess( {}, result.payload )
                       : commanderFailure( CommanderResultCode::ExecutionFailed, result.error );
        } );
        window->show();
        REQUIRE( waitFor( [ & ] { return host() != nullptr; } ) );
        PluginViewDialogs dialogs;
        dialogs.confirm = [ this ]( QWidget* parent, const QString& objectName, const QString&,
                                    const QString& text, const QString& ) {
            confirmations.append( objectName + ":" + text );
            if ( duringQuestion ) {
                duringQuestion();
            }
            return askWith ? askWith( parent, objectName ) : discard;
        };
        host()->setDialogs( dialogs );
    }
    ~LayoutsWindow()
    {
        Q_EMIT window->windowClosed();
        QCoreApplication::processEvents();
    }
    PluginViewHost* host() const
    {
        return window->findChild<PluginViewHost*>();
    }
    PluginView* open( const QString& viewId )
    {
        QString error;
        REQUIRE( host()->openView( PluginId, viewId, {}, &error ) );
        REQUIRE( waitFor( [ & ] { return host()->view( PluginId, viewId ) && host()->view( PluginId, viewId )->isEnabled(); },
                          10000 ) );
        return host()->view( PluginId, viewId );
    }
    QPushButton* button( const QString& viewId, const QString& name ) const
    {
        return window->findChild<QPushButton*>( PluginView::objectNameFor( PluginId, viewId ) + "_button_"
                                                + name );
    }
    CommanderResult run( CommanderRequest request )
    {
        return window->executeCommanderRequest( request );
    }
};

bool clickedWith( const QString& folder, const QString& viewId, const QString& name )
{
    const auto events = recorded( folder, "view/event" );
    return std::any_of( events.begin(), events.end(), [ & ]( const QJsonObject& event ) {
        return event.value( "view_id" ) == viewId && event.value( "element_id" ) == "#buttons"
               && event.value( "type" ) == "clicked" && event.value( "value" ) == name;
    } );
}

QJsonObject closedOf( const QString& folder, const QString& viewId )
{
    const auto closed = recorded( folder, "view/closed" );
    for ( auto it = closed.rbegin(); it != closed.rend(); ++it ) {
        if ( it->value( "view_id" ) == viewId ) {
            return *it;
        }
    }
    return {};
}
} // namespace

TEST_CASE( "E17f dialog buttons send clicked, close the dialog and give view/closed a result",
           "[ui][workbench][E17]" )
{
    LayoutsWindow fixture;
    // ok, cancel, apply in a button box, enabled with the view.
    fixture.open( "ask" );
    auto* apply = fixture.button( "ask", "apply" );
    REQUIRE( apply );
    REQUIRE( waitFor( [ & ] { return apply->isEnabled(); } ) );
    REQUIRE( fixture.button( "ask", "ok" )->isDefault() );
    // apply only sends the event.
    apply->click();
    REQUIRE( waitFor( [ & ] { return clickedWith( fixture.folder, "ask", "apply" ); } ) );
    REQUIRE( fixture.host()->container( PluginId, "ask" ) );
    // ok sends the event and closes; view/closed has the result and the inputs.
    fixture.button( "ask", "ok" )->click();
    REQUIRE( fixture.host()->container( PluginId, "ask" ) == nullptr );
    REQUIRE( waitFor( [ & ] { return !closedOf( fixture.folder, "ask" ).isEmpty(); } ) );
    auto closed = closedOf( fixture.folder, "ask" );
    REQUIRE( closed.value( "result" ) == "ok" );
    REQUIRE( closed.value( "inputs" ).toObject().value( "name" ) == "Ann" );
    REQUIRE( clickedWith( fixture.folder, "ask", "ok" ) );
    // Esc presses cancel.
    fixture.open( "ask" );
    auto* dialog = qobject_cast<QDialog*>( fixture.host()->container( PluginId, "ask" ) );
    REQUIRE( dialog );
    REQUIRE( waitFor( [ & ] { return fixture.button( "ask", "cancel" )->isEnabled(); } ) );
    dialog->reject();
    REQUIRE( fixture.host()->container( PluginId, "ask" ) == nullptr );
    REQUIRE( waitFor( [ & ] { return closedOf( fixture.folder, "ask" ).value( "result" ) == "cancel"; } ) );
    REQUIRE( clickedWith( fixture.folder, "ask", "cancel" ) );

    // confirm_by_plugin: ok only sends the event; the plugin closes the dialog (result null).
    fixture.open( "confirm" );
    REQUIRE( waitFor( [ & ] { return fixture.button( "confirm", "ok" )->isEnabled(); } ) );
    // It declares no `default`: Enter presses no button (a shown button box would make ok the
    // default by itself).
    auto* confirm = qobject_cast<QDialog*>( fixture.host()->container( PluginId, "confirm" ) );
    REQUIRE( confirm );
    REQUIRE( waitFor( [ & ] { return confirm->isVisible(); } ) );
    REQUIRE_FALSE( fixture.button( "confirm", "ok" )->isDefault() );
    QTest::keyClick( confirm, Qt::Key_Return );
    QTest::qWait( 300 );
    REQUIRE_FALSE( clickedWith( fixture.folder, "confirm", "ok" ) );
    REQUIRE( fixture.host()->container( PluginId, "confirm" ) == confirm );
    fixture.button( "confirm", "ok" )->click();
    REQUIRE( waitFor( [ & ] { return clickedWith( fixture.folder, "confirm", "ok" ); } ) );
    REQUIRE( waitFor( [ & ] { return fixture.host()->container( PluginId, "confirm" ) == nullptr; }, 10000 ) );
    REQUIRE( waitFor( [ & ] { return !closedOf( fixture.folder, "confirm" ).isEmpty(); } ) );
    closed = closedOf( fixture.folder, "confirm" );
    REQUIRE( closed.contains( "result" ) );
    REQUIRE( closed.value( "result" ).isNull() );

    // A dialog without buttons closes as before, without result or inputs.
    fixture.open( "plain" );
    auto* plain = qobject_cast<QDialog*>( fixture.host()->container( PluginId, "plain" ) );
    REQUIRE( plain );
    REQUIRE( plain->findChild<QDialogButtonBox*>() == nullptr );
    plain->reject();
    REQUIRE( waitFor( [ & ] { return !closedOf( fixture.folder, "plain" ).isEmpty(); } ) );
    REQUIRE_FALSE( closedOf( fixture.folder, "plain" ).contains( "result" ) );
    REQUIRE_FALSE( closedOf( fixture.folder, "plain" ).contains( "inputs" ) );
}

TEST_CASE( "E17f a modified view marks its title and asks before closing; button menus by automation",
           "[ui][workbench][E17]" )
{
    LayoutsWindow fixture;
    auto* view = fixture.open( "layouts" );
    REQUIRE( waitFor( [ & ] { return view->modified(); } ) );
    auto* dock = qobject_cast<QDockWidget*>( fixture.host()->container( PluginId, "layouts" ) );
    REQUIRE( dock );
    REQUIRE( dock->windowTitle() == "Layouts *" );
    // Closing asks; Cancel keeps the view open.
    dock->close();
    REQUIRE( waitFor( [ & ] { return !fixture.confirmations.isEmpty(); } ) );
    REQUIRE( fixture.confirmations.front()
             == PluginView::objectNameFor( PluginId, "layouts" ) + "_discard:Unsaved fixture" );
    QCoreApplication::processEvents();
    REQUIRE( fixture.host()->container( PluginId, "layouts" ) == dock );
    REQUIRE( closedOf( fixture.folder, "layouts" ).isEmpty() );

    // The menu button's element menu through get_plugin_menus and trigger_plugin_menu.
    CommanderRequest get;
    get.action = CommanderAction::GetPluginMenus;
    get.pluginId = PluginId;
    get.viewId = "layouts";
    get.pluginArguments = { { "location", "element" }, { "elementId", "more" } };
    const auto entries = fixture.run( get );
    REQUIRE( entries.ok() );
    REQUIRE( entries.payload.value( "context" ).toMap().value( "element_id" ) == "more" );
    CommanderRequest trigger;
    trigger.action = CommanderAction::TriggerPluginMenu;
    trigger.pluginId = PluginId;
    trigger.viewId = "layouts";
    trigger.pluginArguments = { { "menuId", "more_menu" }, { "itemId", "again" }, { "elementId", "more" } };
    REQUIRE( fixture.run( trigger ).ok() );
    REQUIRE( waitFor( [ & ] { return !recorded( fixture.folder, "ui/command" ).isEmpty(); } ) );
    const auto context = recorded( fixture.folder, "ui/command" ).last().value( "context" ).toObject();
    REQUIRE( context.value( "element_id" ) == "more" );
    REQUIRE_FALSE( context.contains( "row_id" ) );
    trigger.pluginArguments.insert( "rowId", "r1" );
    REQUIRE_FALSE( fixture.run( trigger ).ok() );

    // Tabs through set_ui reach the plugin as `changed`.
    auto* tabs = qobject_cast<QTabWidget*>( view->element( "pages" ) );
    REQUIRE( tabs );
    REQUIRE( tabs->tabText( 1 ) == "Second (2)" );
    tabs->setCurrentIndex( 1 );
    REQUIRE( waitFor( [ & ] {
        const auto events = recorded( fixture.folder, "view/event" );
        return std::any_of( events.begin(), events.end(), []( const QJsonObject& event ) {
            return event.value( "element_id" ) == "pages" && event.value( "type" ) == "changed"
                   && event.value( "value" ) == "second" && event.value( "index" ) == 1;
        } );
    } ) );

    // Discard closes it.
    fixture.discard = true;
    dock->close();
    REQUIRE( waitFor( [ & ] { return fixture.host()->container( PluginId, "layouts" ) == nullptr; } ) );
    REQUIRE( waitFor( [ & ] { return !closedOf( fixture.folder, "layouts" ).isEmpty(); } ) );
    REQUIRE_FALSE( closedOf( fixture.folder, "layouts" ).contains( "result" ) );
}

TEST_CASE( "E17f Enter in a dialog presses an element default, else the declared default",
           "[ui][workbench][E17]" )
{
    LayoutsWindow fixture;
    fixture.open( "ask" ); // buttons ok, cancel, apply; default ok
    auto* dialog = qobject_cast<QDialog*>( fixture.host()->container( PluginId, "ask" ) );
    REQUIRE( dialog );
    REQUIRE( waitFor( [ & ] { return dialog->isVisible() && fixture.button( "ask", "ok" )->isEnabled(); } ) );
    const auto root = []( bool elementDefault ) {
        return element( "column", "d_body",
                        { { "children", QJsonArray{ element( "field", "d_name", { { "kind", "text" } } ),
                                                    element( "button", "pick",
                                                             { { "label", "Pick" }, { "event", true },
                                                               { "default", elementDefault } } ) } } } );
    };
    const auto send = [ & ]( const QJsonObject& rootValue ) {
        Q_EMIT fixture.manager->viewRequest( PluginId, "view/update",
                                             QJsonObject{ { "view_id", "ask" }, { "root", rootValue } } );
        fixture.host()->flush();
    };
    const auto picked = [ & ] {
        const auto events = recorded( fixture.folder, "view/event" );
        return std::count_if( events.begin(), events.end(), []( const QJsonObject& event ) {
            return event.value( "element_id" ) == "pick" && event.value( "type" ) == "clicked";
        } );
    };
    send( root( true ) );
    auto* field = fixture.host()->view( PluginId, "ask" )->element( "d_name" );
    REQUIRE( field );
    dialog->activateWindow();
    REQUIRE( QTest::qWaitForWindowActive( dialog ) );
    field->setFocus();
    REQUIRE( waitFor( [ & ] { return field->hasFocus(); } ) );
    // The element default takes Enter.
    QTest::keyClick( field, Qt::Key_Return );
    REQUIRE( waitFor( [ & ] { return picked() == 1; } ) );
    REQUIRE( fixture.host()->container( PluginId, "ask" ) == dialog );
    // Without it again, Enter presses the declared default (ok), which closes the dialog.
    send( root( false ) );
    QTest::keyClick( field, Qt::Key_Return );
    REQUIRE( waitFor( [ & ] { return fixture.host()->container( PluginId, "ask" ) == nullptr; } ) );
    REQUIRE( waitFor( [ & ] { return closedOf( fixture.folder, "ask" ).value( "result" ) == "ok"; } ) );
    REQUIRE( picked() == 1 );
}

TEST_CASE( "E17f view/closed says when a dialog's inputs were shortened", "[ui][workbench][E17]" )
{
    LayoutsWindow fixture;
    fixture.open( "ask" );
    REQUIRE( waitFor( [ & ] { return fixture.button( "ask", "ok" )->isEnabled(); } ) );
    // Five full fields pass the 256 KiB input budget (and stay below the 512 KiB event limit).
    QJsonArray fields;
    for ( int index = 0; index < 5; ++index ) {
        fields.append( element( "field", QString( "big%1" ).arg( index ),
                                { { "kind", "multiline" },
                                  { "value", QString( PluginView::MaxFieldValueChars, QLatin1Char( 'x' ) ) } } ) );
    }
    Q_EMIT fixture.manager->viewRequest(
        PluginId, "view/update",
        QJsonObject{ { "view_id", "ask" }, { "root", element( "column", "big_body", { { "children", fields } } ) } } );
    fixture.host()->flush();
    fixture.button( "ask", "ok" )->click();
    REQUIRE( waitFor( [ & ] { return closedOf( fixture.folder, "ask" ).value( "result" ) == "ok"; } ) );
    const auto closed = closedOf( fixture.folder, "ask" );
    REQUIRE( closed.value( "truncated" ) == true );
    REQUIRE_FALSE( closed.value( "inputs" ).toObject().isEmpty() ); // shortened, not dropped
}

TEST_CASE( "E17f a root does not drop the appends queued before it", "[ui][workbench][E17]" )
{
    LayoutsWindow fixture;
    fixture.open( "layouts" );
    const auto root = element( "column", "q_body",
                               { { "children", QJsonArray{ element( "text_view", "q_log",
                                                                    { { "text", "a\n" } } ) } } } );
    const auto send = [ & ]( QJsonObject params ) {
        params.insert( "view_id", "layouts" );
        Q_EMIT fixture.manager->viewRequest( PluginId, "view/update", params );
    };
    send( { { "root", root } } );
    fixture.host()->flush();
    auto* browser = qobject_cast<QTextBrowser*>( fixture.host()->view( PluginId, "layouts" )->element( "q_log" ) );
    REQUIRE( browser );
    auto* log = qobject_cast<PluginTextView*>( browser->parentWidget() );
    REQUIRE( log );
    // An append and then the same root, both queued for one render: the append is applied to
    // the tree it was sent for, and the root (with the text the plugin last set) keeps it.
    send( { { "patch", QJsonArray{ QJsonObject{ { "id", "q_log" }, { "append", "b\n" } } } } } );
    send( { { "root", root } } );
    fixture.host()->flush();
    REQUIRE( log->text() == "a\nb\n" );
}

TEST_CASE( "E17f the answer to Discard changes? is only for the view it asked about",
           "[ui][workbench][E17]" )
{
    LayoutsWindow fixture;
    auto* view = fixture.open( "layouts" );
    REQUIRE( waitFor( [ & ] { return view->modified(); } ) );
    QPointer<QWidget> asked = fixture.host()->container( PluginId, "layouts" );
    REQUIRE( asked );
    // While the question is open, the view is closed and opened again: Discard is not for the
    // new one.
    QPointer<QWidget> reopened;
    bool opened = false;
    bool replaced = false;
    fixture.discard = true;
    fixture.duringQuestion = [ & ] {
        if ( std::exchange( replaced, true ) ) {
            return; // only during the first question
        }
        fixture.host()->closeView( PluginId, "layouts" );
        QString error;
        opened = fixture.host()->openView( PluginId, "layouts", {}, &error );
        reopened = fixture.host()->container( PluginId, "layouts" );
    };
    asked->close();
    REQUIRE( waitFor( [ & ] { return !fixture.confirmations.isEmpty(); } ) );
    REQUIRE( waitFor( [ & ] { return recorded( fixture.folder, "view/closed" ).size() == 1; } ) );
    QTest::qWait( 300 );
    REQUIRE( opened );
    REQUIRE( reopened );
    REQUIRE( reopened != asked );
    REQUIRE( fixture.host()->container( PluginId, "layouts" ) == reopened.data() );
    REQUIRE( recorded( fixture.folder, "view/closed" ).size() == 1 );

    // A question still open when its view goes away is dismissed.
    auto* again = fixture.host()->view( PluginId, "layouts" );
    REQUIRE( again );
    REQUIRE( waitFor( [ & ] { return again->modified(); }, 10000 ) );
    bool dismissedInTime = false;
    fixture.askWith = [ & ]( QWidget* parent, const QString& objectName ) {
        QMessageBox box( QMessageBox::Question, "Discard changes?", "Unsaved", QMessageBox::Cancel,
                         parent );
        box.setObjectName( objectName );
        QTimer::singleShot( 0, &box, [ & ] { fixture.host()->closeView( PluginId, "layouts" ); } );
        QTimer::singleShot( 5000, &box, [ &box ] { box.done( QMessageBox::Ok ); } ); // a safety net
        dismissedInTime = box.exec() == QDialog::Rejected;
        return true; // "Discard", were the view still open
    };
    reopened->close();
    REQUIRE( waitFor( [ & ] { return fixture.host()->container( PluginId, "layouts" ) == nullptr; },
                      10000 ) );
    REQUIRE( dismissedInTime );
    REQUIRE( waitFor( [ & ] { return recorded( fixture.folder, "view/closed" ).size() == 2; } ) );
}

TEST_CASE( "E17f closing a view whose plugin stopped does not ask Discard changes?",
           "[ui][workbench][E17]" )
{
    // #168: nothing can save the changes of a stopped plugin, so the question is not asked.
    LayoutsWindow fixture;
    auto* view = fixture.open( "layouts" );
    REQUIRE( waitFor( [ & ] { return view->modified(); } ) );
    auto* dock = qobject_cast<QDockWidget*>( fixture.host()->container( PluginId, "layouts" ) );
    REQUIRE( dock );
    // A modified dialog too.
    auto* ask = fixture.open( "ask" );
    Q_EMIT fixture.manager->viewRequest(
        PluginId, "view/update",
        QJsonObject{ { "view_id", "ask" },
                     { "root", element( "column", "m_body",
                                        { { "modified", true },
                                          { "children", QJsonArray{ element( "field", "m_name",
                                                                             { { "kind", "text" } } ) } } } ) } } );
    fixture.host()->flush();
    REQUIRE( ask->modified() );
    auto* dialog = qobject_cast<QDialog*>( fixture.host()->container( PluginId, "ask" ) );
    REQUIRE( dialog );

    Q_EMIT fixture.manager->pluginStopped( PluginId );
    REQUIRE_FALSE( view->isEnabled() );
    REQUIRE_FALSE( ask->isEnabled() );
    REQUIRE( dock->windowTitle() == "Layouts *" ); // still marked, but nothing can be saved

    dock->close();
    REQUIRE( waitFor( [ & ] { return fixture.host()->container( PluginId, "layouts" ) == nullptr; } ) );
    dialog->reject(); // Esc
    REQUIRE( waitFor( [ & ] { return fixture.host()->container( PluginId, "ask" ) == nullptr; } ) );
    REQUIRE( fixture.confirmations.isEmpty() );
}

TEST_CASE( "E17f Enter in a dialog field presses no element button that is not the default",
           "[ui][workbench][E17]" )
{
    // #168: `confirm` declares buttons but no `default`; its element buttons are not `default`
    // either. Qt makes push buttons in a dialog auto-default, which must not give Enter a target.
    LayoutsWindow fixture;
    fixture.open( "confirm" );
    auto* dialog = qobject_cast<QDialog*>( fixture.host()->container( PluginId, "confirm" ) );
    REQUIRE( dialog );
    REQUIRE( waitFor( [ & ] { return dialog->isVisible() && fixture.button( "confirm", "ok" )->isEnabled(); } ) );
    Q_EMIT fixture.manager->viewRequest(
        PluginId, "view/update",
        QJsonObject{ { "view_id", "confirm" },
                     { "root", element( "column", "e_body",
                                        { { "children",
                                            QJsonArray{ element( "field", "e_name", { { "kind", "text" } } ),
                                                        element( "button", "e_pick",
                                                                 { { "label", "Pick" }, { "event", true } } ) } } } ) } } );
    fixture.host()->flush();
    auto* view = fixture.host()->view( PluginId, "confirm" );
    auto* field = view->element( "e_name" );
    auto* pick = qobject_cast<QPushButton*>( view->element( "e_pick" ) );
    REQUIRE( field );
    REQUIRE( pick );
    CHECK_FALSE( pick->autoDefault() );
    const auto pressed = [ & ] {
        const auto events = recorded( fixture.folder, "view/event" );
        return std::count_if( events.begin(), events.end(), []( const QJsonObject& event ) {
            return event.value( "view_id" ) == "confirm"
                   && ( event.value( "element_id" ) == "e_pick" || event.value( "element_id" ) == "#buttons" );
        } );
    };
    dialog->activateWindow();
    REQUIRE( QTest::qWaitForWindowActive( dialog ) );
    // The button has had the focus (an auto-default button would now be the dialog's default).
    pick->setFocus();
    REQUIRE( waitFor( [ & ] { return pick->hasFocus(); } ) );
    field->setFocus();
    REQUIRE( waitFor( [ & ] { return field->hasFocus(); } ) );
    QTest::keyClick( field, Qt::Key_Return );
    QTest::qWait( 300 );
    REQUIRE( pressed() == 0 );
    REQUIRE( fixture.host()->container( PluginId, "confirm" ) == dialog );
}
