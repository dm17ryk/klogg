#include <algorithm>
#include <catch2/catch.hpp>

#include <QApplication>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollBar>
#include <QTest>

#include "abstractlogview.h"
#include "quickfindpattern.h"

namespace {
class CountingLogData final : public AbstractLogData {
public:
    mutable LinesCount largestRead;
    mutable size_t individualReads = 0;
    mutable size_t bulkReads = 0;
    bool enormousTail = false;
    QString singleRecord;
    klogg::vector<QString> records;

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
        return line;
    }
    LinesCount doGetNbLine() const override
    {
        return records.empty() ? ( singleRecord.isNull() ? 2000_lcount : 1_lcount )
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

    void sendMouse( QEvent::Type type, const QPoint& point )
    {
        const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event( type, QPointF( point ), QPointF( point ), QPointF( point ), button,
                           buttons, Qt::NoModifier );
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
