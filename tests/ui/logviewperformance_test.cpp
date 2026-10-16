#include <algorithm>
#include <catch2/catch.hpp>

#include <QApplication>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollBar>

#include "abstractlogview.h"
#include "quickfindpattern.h"

namespace {
class CountingLogData final : public AbstractLogData {
public:
    mutable LinesCount largestRead;
    bool enormousTail = false;

protected:
    QString doGetLineString( LineNumber line ) const override
    {
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
        return 2000_lcount;
    }
    LineLength doGetMaxLength() const override
    {
        return 220_length;
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
    view.paintNow();
    const int y = view.fontMetrics().height() / 2;
    LineColumn selectedColumn;
    LineLength selectedSymbols;
    QObject::connect( &view, &AbstractLogView::newSelection, &view,
                      [ & ]( LineNumber, LinesCount, LineColumn column, LineLength symbols ) {
                          selectedColumn = column;
                          selectedSymbols = symbols;
                      } );
    view.sendMouse( QEvent::MouseButtonPress, QPoint( 200, y ) );
    data.largestRead = 0_lcount;
    view.sendMouse( QEvent::MouseMove, QPoint( 70, y ) );

    REQUIRE( view.isPartialSelection() );
    REQUIRE( selectedSymbols.get() == view.getSelectedText().size() );
    REQUIRE( selectedColumn < LineColumn{ 10 } );
    REQUIRE( data.largestRead == 0_lcount );
    view.sendMouse( QEvent::MouseButtonRelease, QPoint( 70, y ) );
}

TEST_CASE( "A huge final record keeps wrap and resize tail accounting bounded",
           "[ui][wrap][performance]" )
{
    CountingLogData data;
    data.enormousTail = true;
    QuickFindPattern pattern;
    TestLogView view( &data, &pattern );
    view.textWrapSet( true );
    view.paintNow();
    const auto checkTailBound = [ & ] {
        const auto rows = static_cast<LinesCount::UnderlyingType>(
            view.viewport()->height() / view.fontMetrics().height() + 1 );
        const auto maximum
            = static_cast<LinesCount::UnderlyingType>( view.verticalScrollBar()->maximum() );
        REQUIRE( maximum <= data.getNbLine().get() + rows * rows );
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
