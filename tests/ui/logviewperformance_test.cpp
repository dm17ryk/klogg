#include <algorithm>
#include <catch2/catch.hpp>
#include <limits>

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMenu>
#include <QPixmap>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "abstractlogview.h"
#include "quickfindpattern.h"

namespace {
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
void activateOffscreenView( QWidget* widget )
{
    QApplication::setActiveWindow( widget );
}
QT_WARNING_POP

class CountingLogData final : public AbstractLogData {
public:
    mutable LinesCount largestRead;
    mutable size_t individualReads = 0;
    mutable size_t bulkReads = 0;
    bool forbidBulkReads = false;
    LinesCount virtualLineCount = 2000_lcount;
    bool enormousTail = false;
    bool emptySource = false;
    QString singleRecord;
    klogg::vector<QString> records;
    klogg::vector<LineNumber> sourceLines;

protected:
    QString doGetLineString( LineNumber line ) const override
    {
        ++individualReads;
        if ( !records.empty() ) {
            return records.at( static_cast<size_t>( line.get() ) );
        }
        if ( !singleRecord.isNull() ) {
            return singleRecord;
        }
        if ( enormousTail && line == 1999_lnum ) {
            return QString( 1024 * 1024, QLatin1Char( 'W' ) );
        }
        return QStringLiteral( "line %1 " ).arg( line.get() ) + QString( 200, QLatin1Char( 'x' ) );
    }
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        return doGetLineString( line );
    }
    klogg::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        REQUIRE_FALSE( forbidBulkReads );
        ++bulkReads;
        largestRead = std::max( largestRead, count );
        klogg::vector<QString> lines;
        for ( auto i = 0_lcount; i < count; ++i ) {
            lines.push_back( doGetLineString( first + i ) );
        }
        return lines;
    }
    klogg::vector<QString> doGetExpandedLines( LineNumber first, LinesCount count ) const override
    {
        return doGetLines( first, count );
    }
    LineNumber doGetLineNumber( LineNumber line ) const override
    {
        return sourceLines.empty() ? line : sourceLines.at( static_cast<size_t>( line.get() ) );
    }
    LinesCount doGetNbLine() const override
    {
        if ( emptySource ) {
            return 0_lcount;
        }
        return records.empty() ? ( singleRecord.isNull() ? virtualLineCount : 1_lcount )
                               : LinesCount{ records.size() };
    }
    LineLength doGetMaxLength() const override
    {
        return singleRecord.isNull() ? 220_length : LineLength{ singleRecord.size() };
    }
    LineLength doGetLineLength( LineNumber line ) const override
    {
        return LineLength{ doGetLineString( line ).size() };
    }
    void doSetDisplayEncoding( const char* ) override {}
    QTextCodec* doGetDisplayEncoding() const override
    {
        return QTextCodec::codecForName( "UTF-8" );
    }
    void doAttachReader() const override {}
    void doDetachReader() const override {}
};

class TestLogView final : public AbstractLogView {
public:
    TestLogView( const AbstractLogData* sourceData, const QuickFindPattern* pattern )
        : AbstractLogView( sourceData, pattern )
    {
        resize( 640, 240 );
        textWrapSet( false );
        show();
        updateDisplaySize();
        QApplication::processEvents();
    }

    void paintNow()
    {
        QPixmap pixmap( size() );
        render( &pixmap );
    }

    bool layoutPending() const
    {
        return isWrappedLayoutPending();
    }

    void openContextMenu( const QPoint& point )
    {
        QMouseEvent event( QEvent::MouseButtonPress, QPointF( point ), QPointF( point ),
                           QPointF( point ), Qt::RightButton, Qt::RightButton, Qt::NoModifier );
        mousePressEvent( &event );
    }

    LineColumn topSourceColumn()
    {
        paintNow();
        LineColumn result;
        const auto connection = QObject::connect(
            this, &AbstractLogView::newSelection, this,
            [ & ]( LineNumber, LinesCount, LineColumn column, LineLength ) { result = column; } );
        const QPoint start( 60, fontMetrics().height() / 2 );
        const QPoint end( 100, start.y() );
        sendMouse( QEvent::MouseButtonPress, start );
        sendMouse( QEvent::MouseMove, end );
        sendMouse( QEvent::MouseButtonRelease, end );
        QObject::disconnect( connection );
        return result;
    }

    void sendMouse( QEvent::Type type, const QPoint& point,
                    Qt::KeyboardModifiers modifiers = Qt::NoModifier )
    {
        const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event( type, QPointF( point ), QPointF( point ), QPointF( point ), button,
                           buttons, modifiers );
        if ( type == QEvent::MouseButtonPress ) {
            mousePressEvent( &event );
        }
        else if ( type == QEvent::MouseMove ) {
            mouseMoveEvent( &event );
        }
        else {
            mouseReleaseEvent( &event );
        }
    }

protected:
    AbstractLogData::LineType lineType( LineNumber ) const override
    {
        return {};
    }
};
} // namespace

TEST_CASE( "Ctrl-click toggles separate rows and combines anchored ranges",
           "[ui][selection][discontiguous]" )
{
    CountingLogData data;
    data.records = { "zero", "one", "two", "three", "four", "five", "six" };
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.paintNow();
    const auto click = [ & ]( int row, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        const QPoint point( 60,
                            view.fontMetrics().height() * row + view.fontMetrics().height() / 2 );
        view.sendMouse( QEvent::MouseButtonPress, point, modifiers );
        view.sendMouse( QEvent::MouseButtonRelease, point, modifiers );
        view.paintNow();
    };
    const auto text = [ & ] { return view.getSelectedText().remove( QChar::CarriageReturn ); };
    click( 0 );
    click( 2, Qt::ControlModifier );
    click( 5, Qt::ControlModifier );
    CHECK( text() == "zero\ntwo\nfive" );
    click( 2, Qt::ControlModifier );
    CHECK( text() == "zero\nfive" );
    click( 4, Qt::ControlModifier | Qt::ShiftModifier );
    CHECK( text() == "zero\ntwo\nthree\nfour\nfive" );
    click( 6, Qt::ShiftModifier );
    CHECK( text() == "two\nthree\nfour\nfive\nsix" );
    click( 1 );
    CHECK( text() == "one" );
    click( 1, Qt::ControlModifier );
    CHECK( text().isEmpty() );
}

TEST_CASE( "Row-set click and keyboard gestures never assemble selected text",
           "[ui][selection][discontiguous][performance][review]" )
{
    const auto modifiers = GENERATE( Qt::KeyboardModifiers{ Qt::ControlModifier },
                                     Qt::KeyboardModifiers{ Qt::ShiftModifier },
                                     Qt::KeyboardModifiers{ Qt::ControlModifier | Qt::ShiftModifier } );
    CAPTURE( static_cast<int>( modifiers ) );
    CountingLogData data;
    data.virtualLineCount = 1000000_lcount;
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.registerShortcuts();
    activateOffscreenView( &view );
    view.setFocus();
    view.selectAll();
    view.paintNow();
    view.setUpdatesEnabled( false );
    const auto individualReads = data.individualReads;
    const auto bulkReads = data.bulkReads;
    data.forbidBulkReads = true;
    LinesCount selectedRows;
    LineLength selectedSymbols{ -1 };
    QObject::connect( &view, &AbstractLogView::newSelection, &view,
                      [ & ]( LineNumber, LinesCount rows, LineColumn, LineLength symbols ) {
                          selectedRows = rows;
                          selectedSymbols = symbols;
                      } );
    const QPoint point( 60, view.fontMetrics().height() * 2 + view.fontMetrics().height() / 2 );
    view.sendMouse( QEvent::MouseButtonPress, point, modifiers );
    view.sendMouse( QEvent::MouseButtonRelease, point, modifiers );
    CHECK( selectedRows > 1_lcount );
    CHECK( selectedSymbols == 0_length );
    CHECK( data.individualReads == individualReads );
    CHECK( data.bulkReads == bulkReads );
    QTest::keyClick( &view, Qt::Key_Up, Qt::ShiftModifier );
    CHECK( selectedSymbols == 0_length );
    CHECK( data.individualReads == individualReads );
    CHECK( data.bulkReads == bulkReads );
}

TEST_CASE( "Context-menu mark state inspects compact ranges without expanding every row",
           "[ui][selection][discontiguous][performance][review]" )
{
    CountingLogData data;
    // Expanding this interval exceeds vector<LineNumber>::max_size(), so the old
    // metadata path throws before allocating. Its first row is already unmarked.
    data.virtualLineCount = LinesCount{ std::numeric_limits<LinesCount::UnderlyingType>::max() / 2 };
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.selectAll();
    view.paintNow();
    size_t selectionChanges = 0;
    QObject::connect( &view, &AbstractLogView::newSelection, &view,
                      [ & ]( LineNumber, LinesCount, LineColumn, LineLength ) {
                          ++selectionChanges;
                      } );
    bool opened = false;
    QTimer::singleShot( 0, &view, [ & ] {
        if ( auto* menu = qobject_cast<QMenu*>( QApplication::activePopupWidget() ) ) {
            opened = true;
            menu->close();
        }
    } );
    QTimer::singleShot( 1000, &view, [ & ] {
        for ( auto* menu : view.findChildren<QMenu*>() ) {
            menu->close();
        }
    } );
    view.openContextMenu( QPoint( 60, view.fontMetrics().height() / 2 ) );
    CHECK( opened );
    CHECK( selectionChanges == 0 );
}

TEST_CASE( "Modified mark-gutter clicks mark the row and preserve the selection",
           "[ui][selection][discontiguous][review]" )
{
    const auto modifiers = GENERATE( Qt::KeyboardModifiers{ Qt::ControlModifier },
                                     Qt::KeyboardModifiers{ Qt::ShiftModifier },
                                     Qt::KeyboardModifiers{ Qt::ControlModifier | Qt::ShiftModifier } );
    CAPTURE( static_cast<int>( modifiers ) );
    CountingLogData data;
    data.records = { "zero", "one", "two", "three", "four" };
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.selectAndDisplayLine( 1_lnum );
    view.paintNow();
    klogg::vector<LineNumber> marked;
    QObject::connect( &view, &AbstractLogView::markLines, &view,
                      [ & ]( const auto& lines ) { marked = lines; } );
    const QPoint point( 1, view.fontMetrics().height() * 3 + view.fontMetrics().height() / 2 );
    view.sendMouse( QEvent::MouseButtonPress, point, modifiers );
    view.sendMouse( QEvent::MouseButtonRelease, point, modifiers );
    REQUIRE( marked.size() == 1 );
    CHECK( marked.front() == 3_lnum );
    CHECK( view.getSelectedText() == "one" );
}

TEST_CASE( "QuickFind mouse and keyboard extensions share the model anchor",
           "[ui][selection][discontiguous][review]" )
{
    const auto additive = GENERATE( false, true );
    CAPTURE( additive );
    CountingLogData data;
    data.records = { "zero", "one", "two", "three", "four", "five", "six" };
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.registerShortcuts();
    activateOffscreenView( &view );
    view.setFocus();
    view.selectAndDisplayLine( 0_lnum );
    const Portion result{ 4_lnum, 0_lcol, 3_lcol };
    REQUIRE( QMetaObject::invokeMethod( &view, "setQuickFindResult", Qt::DirectConnection,
                                       Q_ARG( bool, true ), Q_ARG( Portion, result ) ) );
    QTest::keyClick( &view, Qt::Key_Down, Qt::ShiftModifier );
    CHECK( view.getSelectedText().remove( QChar::CarriageReturn ) == "four\nfive" );
    REQUIRE( QMetaObject::invokeMethod( &view, "setQuickFindResult", Qt::DirectConnection,
                                       Q_ARG( bool, true ), Q_ARG( Portion, result ) ) );
    view.paintNow();
    const QPoint point( 60, view.fontMetrics().height() * 6 + view.fontMetrics().height() / 2 );
    const auto modifiers = Qt::KeyboardModifiers{ Qt::ShiftModifier }
                           | ( additive ? Qt::ControlModifier : Qt::NoModifier );
    view.sendMouse( QEvent::MouseButtonPress, point, modifiers );
    view.sendMouse( QEvent::MouseButtonRelease, point, modifiers );
    CHECK( view.getSelectedText().remove( QChar::CarriageReturn ) == "four\nfive\nsix" );
    QTest::keyClick( &view, Qt::Key_Up, Qt::ShiftModifier );
    CHECK( view.getSelectedText().remove( QChar::CarriageReturn ) == "four\nfive" );
}

TEST_CASE( "Whole-row selection ranges merge split crop and clear without source reads",
           "[ui][selection][discontiguous]" )
{
    Selection selection;
    CHECK( selection.isEmpty() );
    selection.selectRange( 0_lnum, 1000000_lnum );
    selection.toggleLine( 500000_lnum );
    CHECK( selection.getSelectedLinesCount() == 1000000_lcount );
    CHECK( selection.getLineRanges().size() == 2 );
    CHECK_FALSE( selection.isLineSelected( 500000_lnum ) );
    CHECK( selection.isLineSelected( 499999_lnum ) );
    CHECK( selection.isLineSelected( 500001_lnum ) );
    selection.toggleLine( 500000_lnum );
    CHECK( selection.getLineRanges().size() == 1 );
    CHECK( selection.getSelectedLinesCount() == 1000001_lcount );
    selection.crop( 10_lnum );
    CHECK( selection.getSelectedLinesCount() == 11_lcount );
    CHECK_FALSE( selection.isLineSelected( 11_lnum ) );
    selection.clear();
    CHECK( selection.isEmpty() );
    CHECK( selection.getLines().empty() );
    selection.toggleLine( 5_lnum );
    CHECK( selection.isSingleLine() );
    REQUIRE( selection.selectedLine() );
    CHECK( *selection.selectedLine() == 5_lnum );
    selection.crop( 2_lnum );
    CHECK( selection.isEmpty() );
    selection.toggleLine( 2_lnum );
    selection.toggleLine( 5_lnum );
    CHECK( selection.getPreviousPosition().line() == 2_lnum );
    CHECK( selection.getNextPosition().line() == 6_lnum );
    selection.selectPortion( 3_lnum, 8_lcol, 2_lcol );
    CHECK( selection.isPortion() );
    CHECK( selection.getSelectedLinesCount() == 1_lcount );
    selection.toggleLine( 6_lnum );
    CHECK_FALSE( selection.isPortion() );
    CHECK( selection.isSingleLine() );
    CHECK_FALSE( selection.isLineSelected( 3_lnum ) );
}

TEST_CASE( "Discontiguous copy preserves blank rows and mapped source order",
           "[ui][selection][discontiguous]" )
{
    CountingLogData data;
    data.records = { "", "unselected", "", QString::fromUtf8( "\xe2\x98\x83\tUTF-8" ), "" };
    Selection selection;
    selection.selectLine( 0_lnum );
    selection.toggleLine( 2_lnum );
    selection.toggleLine( 3_lnum );
    selection.toggleLine( 4_lnum );
    const auto copied = selection.getSelectedText( &data ).remove( QChar::CarriageReturn );
    CHECK( copied == QString::fromUtf8( "\n\n\xe2\x98\x83\tUTF-8\n" ) );
    data.sourceLines = { 40_lnum, 50_lnum, 30_lnum, 10_lnum, 20_lnum };
    const auto numbered = selection.getSelectedText( &data, true ).remove( QChar::CarriageReturn );
    CHECK( numbered == QString::fromUtf8( "11: \xe2\x98\x83\tUTF-8\n21: \n31: \n41: " ) );
}

TEST_CASE( "Ctrl-click anchors keyboard ranges and empty sources clear selection",
           "[ui][selection][discontiguous]" )
{
    CountingLogData data;
    data.records = { "zero", "one", "two", "three", "four", "five", "six" };
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.registerShortcuts();
    activateOffscreenView( &view );
    view.setFocus();
    view.paintNow();
    const QPoint row( 60, view.fontMetrics().height() * 3 + view.fontMetrics().height() / 2 );
    view.sendMouse( QEvent::MouseButtonPress, row, Qt::ControlModifier );
    view.sendMouse( QEvent::MouseButtonRelease, row, Qt::ControlModifier );
    QTest::keyClick( &view, Qt::Key_Down, Qt::ShiftModifier );
    CHECK( view.getSelectedText().remove( QChar::CarriageReturn ) == "three\nfour" );
    QTest::keyClick( &view, Qt::Key_Down, Qt::ShiftModifier );
    CHECK( view.getSelectedText().remove( QChar::CarriageReturn ) == "three\nfour\nfive" );
    data.emptySource = true;
    view.updateData();
    CHECK( view.getSelectedText().isEmpty() );
    view.selectAll();
    CHECK( view.getSelectedText().isEmpty() );
    for ( const auto key : { Qt::Key_Up, Qt::Key_Down, Qt::Key_Home, Qt::Key_End } ) {
        QTest::keyClick( &view, key, Qt::ControlModifier );
        CHECK( view.getSelectedText().isEmpty() );
    }
    QTest::keyClick( &view, Qt::Key_Up );
    QTest::keyClick( &view, Qt::Key_Down );
    CHECK( view.getSelectedText().isEmpty() );
}

TEST_CASE( "Ctrl-click on wrapped continuations toggles their physical row",
           "[ui][selection][discontiguous]" )
{
    CountingLogData data;
    data.records = { QString( 3000, QLatin1Char( 'W' ) ), "second" };
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.textWrapSet( true );
    view.paintNow();
    const auto click = [ & ]( int row, Qt::KeyboardModifiers modifiers ) {
        const QPoint point( 60,
                            view.fontMetrics().height() * row + view.fontMetrics().height() / 2 );
        view.sendMouse( QEvent::MouseButtonPress, point, modifiers );
        view.sendMouse( QEvent::MouseButtonRelease, point, modifiers );
        view.paintNow();
    };
    click( 0, Qt::NoModifier );
    CHECK( view.getSelectedText() == data.records.front() );
    click( 1, Qt::ControlModifier );
    CHECK( view.getSelectedText().isEmpty() );
    click( 2, Qt::ControlModifier );
    CHECK( view.getSelectedText() == data.records.front() );
}

TEST_CASE( "Saving separate selected ranges excludes gaps and retains complete chunks",
           "[ui][selection][discontiguous]" )
{
    const auto trailingCount = GENERATE( 4999, 5000, 5001 );
    CAPTURE( trailingCount );
    CountingLogData data;
    QString expected;
    for ( int row = 0; row < trailingCount + 3; ++row ) {
        const auto text = QStringLiteral( "row %1" ).arg( row );
        data.records.push_back( text );
        if ( row != 2 ) {
            expected += text + QChar::LineFeed;
        }
    }
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.selectAll();
    view.paintNow();
    const QPoint gap( 60, view.fontMetrics().height() * 2 + view.fontMetrics().height() / 2 );
    view.sendMouse( QEvent::MouseButtonPress, gap, Qt::ControlModifier );
    view.sendMouse( QEvent::MouseButtonRelease, gap, Qt::ControlModifier );
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "selected.txt" );
    const auto wasNonNative = QApplication::testAttribute( Qt::AA_DontUseNativeDialogs );
    QApplication::setAttribute( Qt::AA_DontUseNativeDialogs, true );
    QTimer acceptTimer;
    acceptTimer.setInterval( 100 );
    QObject::connect( &acceptTimer, &QTimer::timeout, &view, [ & ] {
        for ( auto* widget : QApplication::topLevelWidgets() ) {
            if ( auto* dialog = qobject_cast<QFileDialog*>( widget );
                 dialog && dialog->isVisible() ) {
                // selectFile() does not update a visible dialog's focused filename edit.
                // Enter the absolute path as a user would on every offscreen platform.
                auto* filenameEdit = dialog->findChild<QLineEdit*>( "fileNameEdit" );
                if ( filenameEdit == nullptr ) {
                    return;
                }
                filenameEdit->setText( path );
                QMetaObject::invokeMethod( dialog, "accept", Qt::DirectConnection );
                if ( !dialog->isVisible() ) {
                    acceptTimer.stop();
                }
            }
        }
    } );
    acceptTimer.start();
    QTimer watchdog;
    watchdog.setSingleShot( true );
    QObject::connect( &watchdog, &QTimer::timeout, &view, [] {
        for ( auto* widget : QApplication::topLevelWidgets() ) {
            if ( auto* dialog = qobject_cast<QFileDialog*>( widget ) ) {
                dialog->reject();
            }
        }
    } );
    watchdog.start( 10000 );
    const auto invoked
        = QMetaObject::invokeMethod( &view, "saveSelectedToFile", Qt::DirectConnection );
    QApplication::setAttribute( Qt::AA_DontUseNativeDialogs, wasNonNative );
    REQUIRE( invoked );
    QFile saved( path );
    REQUIRE( saved.open( QIODevice::ReadOnly ) );
    CHECK( QString::fromUtf8( saved.readAll() ).remove( QChar::CarriageReturn ) == expected );
}

TEST_CASE( "Dragging a large selection does not read the selected range",
           "[ui][selection][performance]" )
{
    CountingLogData data;
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.paintNow();
    const int rowHeight = view.fontMetrics().height();
    const QPoint start( 60, rowHeight / 2 );
    const QPoint end( 60, rowHeight * 2 + rowHeight / 2 );
    view.sendMouse( QEvent::MouseButtonPress, start );
    view.jumpToLine( 1500_lnum );
    view.paintNow();
    data.largestRead = 0_lcount;
    const auto individualReadsBeforeDrag = data.individualReads;
    const auto bulkReadsBeforeDrag = data.bulkReads;

    LinesCount selectedLines;
    LineLength selectedSymbols;
    QObject::connect( &view, &AbstractLogView::newSelection, &view,
                      [ & ]( LineNumber, LinesCount lines, LineColumn, LineLength symbols ) {
                          selectedLines = lines;
                          selectedSymbols = symbols;
                      } );
    view.sendMouse( QEvent::MouseMove, end );

    CAPTURE( selectedLines.get(), view.getTopLine().get() );
    REQUIRE( selectedLines > 1000_lcount );
    REQUIRE( data.largestRead == 0_lcount );
    REQUIRE( data.individualReads == individualReadsBeforeDrag );
    REQUIRE( data.bulkReads == bulkReadsBeforeDrag );
    REQUIRE( selectedSymbols == 0_length );

    view.sendMouse( QEvent::MouseButtonRelease, end );
    REQUIRE( selectedSymbols.get() == view.getSelectedText().size() );
    REQUIRE( selectedSymbols > 200000_length );
}

TEST_CASE( "Reverse partial selection reports normalized coordinates without reading text",
           "[ui][selection][performance]" )
{
    CountingLogData data;
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    auto font = view.font();
    font.setPointSize( GENERATE( 6, 12, 20 ) );
    view.updateFont( font );
    view.paintNow();
    const int y = view.fontMetrics().height() / 2;
    LineColumn selectedColumn;
    LineLength selectedSymbols;
    QObject::connect( &view, &AbstractLogView::newSelection, &view,
                      [ & ]( LineNumber, LinesCount, LineColumn column, LineLength symbols ) {
                          selectedColumn = column;
                          selectedSymbols = symbols;
                      } );
    const QPoint left( 70, y );
    const QPoint right( 200, y );
    view.sendMouse( QEvent::MouseButtonPress, left );
    view.sendMouse( QEvent::MouseMove, right );
    view.sendMouse( QEvent::MouseButtonRelease, right );
    const auto forwardColumn = selectedColumn;
    const auto forwardText = view.getSelectedText();
    REQUIRE_FALSE( forwardText.isEmpty() );

    view.sendMouse( QEvent::MouseButtonPress, right );
    data.largestRead = 0_lcount;
    const auto individualReadsBeforeDrag = data.individualReads;
    const auto bulkReadsBeforeDrag = data.bulkReads;
    view.sendMouse( QEvent::MouseMove, left );

    CAPTURE( font.pointSize(), selectedColumn.get(), forwardColumn.get() );
    REQUIRE( data.individualReads == individualReadsBeforeDrag );
    REQUIRE( data.bulkReads == bulkReadsBeforeDrag );
    REQUIRE( view.isPartialSelection() );
    REQUIRE( selectedSymbols.get() == view.getSelectedText().size() );
    REQUIRE( selectedColumn == forwardColumn );
    REQUIRE( view.getSelectedText() == forwardText );
    REQUIRE( data.largestRead == 0_lcount );
    view.sendMouse( QEvent::MouseButtonRelease, left );
}

TEST_CASE( "Wrapped record continuations remain reachable at the scrollbar end",
           "[ui][wrap][continuation]" )
{
    CountingLogData data;
    const auto suffix = QStringLiteral( "WRAPPED_SUFFIX_MARKER" );
    data.singleRecord = QString( 64 * 1024, QLatin1Char( 'W' ) ) + suffix;
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.textWrapSet( true );
    view.paintNow();
    REQUIRE( QTest::qWaitFor( [ & ] { return !view.layoutPending(); }, 10000 ) );
    view.verticalScrollBar()->setValue( view.verticalScrollBar()->maximum() );
    view.paintNow();
    const QPoint start( 20, view.fontMetrics().height() / 2 );
    const QPoint end( view.viewport()->width() - 2, view.viewport()->height() - 2 );
    view.sendMouse( QEvent::MouseButtonPress, start );
    view.sendMouse( QEvent::MouseMove, end );
    REQUIRE( view.getSelectedText().contains( suffix ) );
    view.sendMouse( QEvent::MouseButtonRelease, end );
}

TEST_CASE( "A huge final record leaves continuation work pending across resize and font changes",
           "[ui][wrap][performance]" )
{
    CountingLogData data;
    data.enormousTail = true;
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.textWrapSet( true );
    view.paintNow();
    const auto checkTailBound = [ & ] {
        REQUIRE( view.layoutPending() );
        REQUIRE( view.verticalScrollBar()->maximum() > 0 );
    };
    checkTailBound();
    view.resize( 360, 180 );
    QApplication::processEvents();
    view.paintNow();
    checkTailBound();
    auto font = view.font();
    font.setPointSize( 14 );
    view.updateFont( font );
    view.paintNow();
    checkTailBound();
    view.jumpToLine( 1999_lnum );
    view.paintNow();
    REQUIRE( view.visibleLineStart() <= 1999_lnum );
    REQUIRE( view.visibleLineEnd() == 1999_lnum );
}

TEST_CASE( "Wrapped navigation reveals requested records and source columns",
           "[ui][wrap][navigation]" )
{
    CountingLogData data;
    data.records
        = { QString( 64 * 1024, QLatin1Char( 'W' ) ), QStringLiteral( "following record" ) };
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.textWrapSet( true );
    view.paintNow();

    SECTION( "Whole-line selection reveals a record hidden by wrapping" )
    {
        view.selectAndDisplayLine( 1_lnum );
        view.paintNow();
        REQUIRE( view.visibleLineStart() == 1_lnum );
    }
    SECTION( "Filtered portion navigation reveals its source column" )
    {
        view.selectPortionAndDisplayLine( 0_lnum, 1_lcount, 60000_lcol, 5_length );
        REQUIRE( QTest::qWaitFor( [ & ] { return !view.layoutPending(); }, 10000 ) );
        const auto top = view.topSourceColumn();
        REQUIRE( top > 59000_lcol );
        REQUIRE( top < 60100_lcol );
    }
    SECTION( "Home cancels deferred column restoration" )
    {
        view.selectPortionAndDisplayLine( 0_lnum, 1_lcount, 60000_lcol, 5_length );
        view.selectAndDisplayLine( 0_lnum );
        REQUIRE( QTest::qWaitFor( [ & ] { return !view.layoutPending(); }, 10000 ) );
        REQUIRE( view.topSourceColumn() < 100_lcol );
    }
    SECTION( "Rapid page steps do not skip unresolved continuations" )
    {
        for ( int i = 0; i < 40; ++i ) {
            view.verticalScrollBar()->triggerAction( QAbstractSlider::SliderPageStepAdd );
        }
        REQUIRE( view.visibleLineStart() == 0_lnum );
        REQUIRE( view.topSourceColumn() > 10000_lcol );
        for ( int i = 0; i < 40; ++i ) {
            view.verticalScrollBar()->triggerAction( QAbstractSlider::SliderPageStepSub );
        }
        REQUIRE( view.topSourceColumn() < 100_lcol );
    }
}

TEST_CASE( "Background wrapping preserves the viewport without rereading each slice",
           "[ui][wrap][performance]" )
{
    CountingLogData data;
    data.records = { QString( 128 * 1024, QLatin1Char( 'W' ) ), QStringLiteral( "tail" ) };
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.textWrapSet( true );
    view.paintNow();
    QApplication::processEvents();
    view.paintNow();
    view.verticalScrollBar()->triggerAction( QAbstractSlider::SliderPageStepAdd );
    const auto before = view.topSourceColumn();
    QApplication::processEvents();
    view.paintNow();
    const auto reads = data.individualReads;
    REQUIRE( QTest::qWaitFor( [ & ] { return !view.layoutPending(); }, 10000 ) );
    REQUIRE( data.individualReads - reads < 10 );
    const auto after = view.topSourceColumn();
    CAPTURE( before.get(), after.get() );
    REQUIRE( after == before );
    data.records.push_back( QStringLiteral( "appended record" ) );
    view.updateData();
    view.paintNow();
    REQUIRE( view.topSourceColumn() == before );
    REQUIRE_FALSE( view.layoutPending() );
}

TEST_CASE( "Horizontal scrolling draws repeated text at the viewport origin",
           "[ui][selection][drawing]" )
{
    CountingLogData data;
    data.singleRecord = QString( 4096, QLatin1Char( 'W' ) );
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.paintNow();
    const QRect sample( 100, 2, 200, view.fontMetrics().height() - 4 );
    const auto before = view.viewport()->grab().toImage().copy( sample );
    auto background = before;
    background.fill( view.palette().color( QPalette::Base ) );
    REQUIRE( before != background );
    view.horizontalScrollBar()->setValue( 100 );
    REQUIRE( view.horizontalScrollBar()->value() == 100 );
    view.paintNow();
    const auto after = view.viewport()->grab().toImage().copy( sample );
    REQUIRE( after == before );
}
