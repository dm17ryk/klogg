// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginchart.h"

#include "pluginfeatures.h"
#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

using cilogg::plugins::MaxChartPoints;
using cilogg::plugins::MaxChartSeries;
using cilogg::plugins::MaxImageSide;
using cilogg::plugins::MaxSparklineValues;

namespace {
// Series colours by position when a series names no tone.
const QColor& paletteColor( int index )
{
    static const QColor colors[] = { QColor( "#1565c0" ), QColor( "#2e7d32" ), QColor( "#ef6c00" ),
                                     QColor( "#6a1b9a" ), QColor( "#c62828" ), QColor( "#00838f" ),
                                     QColor( "#5d4037" ), QColor( "#455a64" ) };
    return colors[ static_cast<size_t>( index ) % std::size( colors ) ];
}

QSize boundedSize( const QSize& size )
{
    return size.boundedTo( QSize( MaxImageSide, MaxImageSide ) );
}
} // namespace

PluginImage::PluginImage( QWidget* parent )
    : QWidget( parent )
{
    setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Preferred );
}

void PluginImage::setImage( const QImage& image )
{
    image_ = image;
    updateGeometry();
    update();
}

void PluginImage::setBox( int width, int height )
{
    width_ = qBound( 0, width, MaxImageSide );
    height_ = qBound( 0, height, MaxImageSide );
    if ( width_ > 0 && height_ > 0 ) {
        setFixedSize( width_, height_ );
    }
    else {
        setMinimumSize( 0, 0 );
        setMaximumSize( QWIDGETSIZE_MAX, QWIDGETSIZE_MAX );
        if ( width_ > 0 ) {
            setFixedWidth( width_ );
        }
        if ( height_ > 0 ) {
            setFixedHeight( height_ );
        }
    }
    updateGeometry();
    update();
}

void PluginImage::setScale( Scale scale )
{
    scale_ = scale;
    update();
}

void PluginImage::setAlt( const QString& alt )
{
    alt_ = alt;
    updateGeometry();
    update();
}

QSize PluginImage::sizeHint() const
{
    QSize natural = image_.isNull()
                        ? fontMetrics().size( Qt::TextSingleLine, alt_.left( 256 ) ) + QSize( 8, 8 )
                        : boundedSize( image_.size() / qMax( 1.0, image_.devicePixelRatio() ) );
    // One side given: the other keeps the aspect ratio, within the image size limit too.
    const auto side = []( double value ) {
        return static_cast<int>( std::lround( std::clamp( value, 1.0, double( MaxImageSide ) ) ) );
    };
    if ( width_ > 0 && height_ <= 0 && !image_.isNull() && image_.width() > 0 ) {
        natural = QSize( width_, side( double( width_ ) * image_.height() / image_.width() ) );
    }
    else if ( height_ > 0 && width_ <= 0 && !image_.isNull() && image_.height() > 0 ) {
        natural = QSize( side( double( height_ ) * image_.width() / image_.height() ), height_ );
    }
    return QSize( width_ > 0 ? width_ : natural.width(), height_ > 0 ? height_ : natural.height() );
}

QSize PluginImage::minimumSizeHint() const
{
    return QSize( width_ > 0 ? width_ : 8, height_ > 0 ? height_ : 8 );
}

QRectF PluginImage::placement( const QSize& image, const QRect& area, Scale scale )
{
    if ( image.isEmpty() || area.isEmpty() ) {
        return {};
    }
    if ( scale == Scale::Fill ) {
        return QRectF( area );
    }
    QSizeF drawn( image );
    if ( scale == Scale::Fit ) {
        drawn.scale( QSizeF( area.size() ), Qt::KeepAspectRatio );
    }
    // Centered; with `none` a larger image is cut by the box.
    return QRectF( area.x() + ( area.width() - drawn.width() ) / 2,
                   area.y() + ( area.height() - drawn.height() ) / 2, drawn.width(),
                   drawn.height() );
}

void PluginImage::paintEvent( QPaintEvent* )
{
    QPainter painter( this );
    if ( image_.isNull() ) {
        painter.setPen( palette().color( QPalette::Disabled, QPalette::WindowText ) );
        painter.drawText( rect(), Qt::AlignCenter | Qt::TextWordWrap, alt_.left( 4096 ) );
        return;
    }
    painter.setRenderHint( QPainter::SmoothPixmapTransform );
    painter.setClipRect( rect() );
    const auto logical = image_.size() / qMax( 1.0, image_.devicePixelRatio() );
    painter.drawImage( placement( logical, rect(), scale_ ), image_ );
}

PluginSparkline::PluginSparkline( QWidget* parent )
    : QWidget( parent )
{
    setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Fixed );
}

void PluginSparkline::setValues( const QVector<double>& values )
{
    values_ = values.size() > MaxSparklineValues ? values.mid( values.size() - MaxSparklineValues )
                                                 : values;
    update();
}

void PluginSparkline::setRange( std::optional<double> minimum, std::optional<double> maximum )
{
    minimum_ = minimum;
    maximum_ = maximum;
    update();
}

void PluginSparkline::setBars( bool bars )
{
    bars_ = bars;
    update();
}

void PluginSparkline::setColor( const QColor& color )
{
    color_ = color;
    update();
}

std::pair<double, double> PluginSparkline::range() const
{
    auto low = std::numeric_limits<double>::infinity();
    auto high = -std::numeric_limits<double>::infinity();
    for ( const auto value : values_ ) {
        if ( std::isfinite( value ) ) {
            low = std::min( low, value );
            high = std::max( high, value );
        }
    }
    if ( !std::isfinite( low ) ) {
        low = 0;
        high = 1;
    }
    if ( minimum_ ) {
        low = *minimum_;
    }
    if ( maximum_ ) {
        high = *maximum_;
    }
    if ( bars_ && !minimum_ ) {
        low = std::min( low, 0.0 ); // bars grow from zero
    }
    if ( high <= low ) {
        high = low + 1;
    }
    return { low, high };
}

QSize PluginSparkline::sizeHint() const
{
    return QSize( 120, 24 );
}

void PluginSparkline::paintEvent( QPaintEvent* )
{
    if ( values_.isEmpty() ) {
        return;
    }
    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing );
    const auto color = color_.isValid() ? color_ : palette().color( QPalette::Highlight );
    const auto bounds = range();
    const auto low = bounds.first;
    const auto high = bounds.second;
    const QRectF area = QRectF( rect() ).adjusted( 1, 2, -1, -2 );
    const auto count = static_cast<double>( values_.size() );
    const auto yOf = [ & ]( double value ) {
        const auto clamped = std::clamp( value, low, high );
        return area.bottom() - ( clamped - low ) / ( high - low ) * area.height();
    };
    if ( bars_ ) {
        const auto barWidth = area.width() / count;
        const auto base = yOf( std::clamp( 0.0, low, high ) );
        for ( int index = 0; index < values_.size(); ++index ) {
            const auto value = values_.at( index );
            if ( !std::isfinite( value ) ) {
                continue;
            }
            const auto top = yOf( value );
            painter.fillRect( QRectF( area.left() + index * barWidth + barWidth * 0.1,
                                      std::min( top, base ), std::max( 1.0, barWidth * 0.8 ),
                                      std::max( 1.0, std::abs( base - top ) ) ),
                              color );
        }
        return;
    }
    painter.setPen( QPen( color, 1.5 ) );
    const auto step = values_.size() > 1 ? area.width() / ( count - 1 ) : 0.0;
    QPolygonF line;
    const auto flush = [ & ] {
        if ( line.size() == 1 ) {
            painter.drawPoint( line.front() );
        }
        else if ( line.size() > 1 ) {
            painter.drawPolyline( line );
        }
        line.clear();
    };
    for ( int index = 0; index < values_.size(); ++index ) {
        const auto value = values_.at( index );
        if ( !std::isfinite( value ) ) {
            flush(); // a gap
            continue;
        }
        line.append( QPointF( area.left() + index * step, yOf( value ) ) );
    }
    flush();
}

PluginChart::PluginChart( QWidget* parent )
    : QWidget( parent )
{
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    setMinimumHeight( 160 );
}

void PluginChart::setKind( Kind kind )
{
    kind_ = kind;
    update();
}

void PluginChart::keepWindow( Series& series ) const
{
    const auto keep = window_ > 0 ? std::min( window_, MaxChartPoints ) : MaxChartPoints;
    if ( series.points.size() > keep ) {
        series.points.remove( 0, series.points.size() - keep );
    }
}

void PluginChart::setSeries( QVector<Series> series )
{
    if ( series.size() > MaxChartSeries ) {
        series.resize( MaxChartSeries );
    }
    for ( auto& item : series ) {
        keepWindow( item );
    }
    series_ = std::move( series );
    update();
}

bool PluginChart::appendPoints( const QString& seriesId, const QVector<QPointF>& points )
{
    for ( auto& series : series_ ) {
        if ( series.id == seriesId ) {
            series.points += points;
            keepWindow( series );
            update();
            return true;
        }
    }
    return false;
}

void PluginChart::clear( const QStringList& seriesIds )
{
    for ( auto& series : series_ ) {
        if ( seriesIds.isEmpty() || seriesIds.contains( series.id ) ) {
            series.points.clear();
        }
    }
    update();
}

void PluginChart::setTitles( const QString& xTitle, const QString& yTitle )
{
    xTitle_ = xTitle;
    yTitle_ = yTitle;
    update();
}

void PluginChart::setTimeAxis( bool time )
{
    time_ = time;
    update();
}

void PluginChart::setLegend( std::optional<bool> legend )
{
    legend_ = legend;
    update();
}

void PluginChart::setWindow( int points )
{
    window_ = qBound( 0, points, MaxChartPoints );
    for ( auto& series : series_ ) {
        keepWindow( series );
    }
    update();
}

int PluginChart::pointCount( const QString& seriesId ) const
{
    for ( const auto& series : series_ ) {
        if ( series.id == seriesId ) {
            return static_cast<int>( series.points.size() );
        }
    }
    return -1;
}

bool PluginChart::legendShown() const
{
    return legend_.value_or( series_.size() > 1 ) && !series_.isEmpty();
}

double PluginChart::nextX( const QString& seriesId ) const
{
    for ( const auto& series : series_ ) {
        if ( series.id == seriesId ) {
            return series.points.isEmpty() ? 0.0 : std::floor( series.points.back().x() ) + 1;
        }
    }
    return 0.0;
}

QColor PluginChart::colorOf( int index ) const
{
    const auto& color = series_.value( index ).color;
    return color.isValid() ? color : paletteColor( index );
}

QVector<double> PluginChart::ticks( double low, double high, int count )
{
    QVector<double> result;
    if ( !std::isfinite( low ) || !std::isfinite( high ) || high <= low || count < 1 ) {
        return result;
    }
    const auto rough = ( high - low ) / count;
    const auto magnitude = std::pow( 10.0, std::floor( std::log10( rough ) ) );
    const auto fraction = rough / magnitude;
    const auto step = magnitude * ( fraction < 1.5 ? 1 : fraction < 3 ? 2 : fraction < 7 ? 5 : 10 );
    const auto first = std::ceil( low / step - 1e-9 ) * step;
    for ( auto value = first; value <= high + step * 1e-9 && result.size() < 64; value += step ) {
        result.append( std::abs( value ) < step * 1e-9 ? 0.0 : value );
    }
    return result;
}

QString PluginChart::axisText( double value, bool xAxis, double span ) const
{
    if ( xAxis && time_ ) {
        const auto moment = QDateTime::fromMSecsSinceEpoch( static_cast<qint64>( std::llround( value ) ) );
        return moment.toString( span <= 86400000.0 ? QStringLiteral( "HH:mm:ss" )
                                                   : QStringLiteral( "MM-dd HH:mm" ) );
    }
    return QString::number( value, 'g', 6 );
}

QSize PluginChart::sizeHint() const
{
    return QSize( 360, 200 );
}

QSize PluginChart::minimumSizeHint() const
{
    return QSize( 120, 160 );
}

void PluginChart::paintEvent( QPaintEvent* )
{
    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing );
    const auto text = palette().color( QPalette::WindowText );
    auto grid = text;
    grid.setAlpha( 40 );
    // The range of every point; bars and areas include zero.
    auto xLow = std::numeric_limits<double>::infinity();
    auto xHigh = -std::numeric_limits<double>::infinity();
    auto yLow = std::numeric_limits<double>::infinity();
    auto yHigh = -std::numeric_limits<double>::infinity();
    double step = std::numeric_limits<double>::infinity(); // the smallest x step, for bar widths
    for ( const auto& series : series_ ) {
        for ( int index = 0; index < series.points.size(); ++index ) {
            const auto& point = series.points.at( index );
            xLow = std::min( xLow, point.x() );
            xHigh = std::max( xHigh, point.x() );
            yLow = std::min( yLow, point.y() );
            yHigh = std::max( yHigh, point.y() );
            if ( index > 0 ) {
                const auto gap = std::abs( point.x() - series.points.at( index - 1 ).x() );
                if ( gap > 0 ) {
                    step = std::min( step, gap );
                }
            }
        }
    }
    const bool empty = !std::isfinite( xLow );
    if ( empty ) {
        xLow = 0;
        xHigh = 1;
        yLow = 0;
        yHigh = 1;
    }
    if ( kind_ == Kind::Bar || kind_ == Kind::Area ) {
        yLow = std::min( yLow, 0.0 );
        yHigh = std::max( yHigh, 0.0 );
    }
    if ( !std::isfinite( step ) ) {
        step = 1;
    }
    if ( kind_ == Kind::Bar ) {
        xLow -= step / 2;
        xHigh += step / 2;
    }
    if ( xHigh <= xLow ) {
        xLow -= 0.5;
        xHigh += 0.5;
    }
    if ( yHigh <= yLow ) {
        const auto pad = yLow == 0 ? 1.0 : std::abs( yLow ) * 0.1;
        yLow -= pad;
        yHigh += pad;
    }
    const auto metrics = fontMetrics();
    const auto yTicks = ticks( yLow, yHigh, 5 );
    const auto xTicks = ticks( xLow, xHigh, std::max( 2, width() / 90 ) );
    int labelWidth = 0;
    for ( const auto value : yTicks ) {
        labelWidth = std::max( labelWidth, metrics.horizontalAdvance( axisText( value, false, 0 ) ) );
    }
    const int line = metrics.height();
    const int left = 6 + labelWidth + ( yTitle_.isEmpty() ? 0 : line + 4 );
    const int bottom = 6 + line + ( xTitle_.isEmpty() ? 0 : line + 2 );
    const QRectF plot( left, line / 2.0 + 2, std::max( 10, width() - left - 10 ),
                       std::max( 10.0, height() - bottom - line / 2.0 - 2 ) );
    const auto xOf = [ & ]( double x ) {
        return plot.left() + ( x - xLow ) / ( xHigh - xLow ) * plot.width();
    };
    const auto yOf = [ & ]( double y ) {
        return plot.bottom() - ( y - yLow ) / ( yHigh - yLow ) * plot.height();
    };
    // Grid, ticks and titles.
    painter.setPen( grid );
    for ( const auto value : yTicks ) {
        painter.drawLine( QPointF( plot.left(), yOf( value ) ), QPointF( plot.right(), yOf( value ) ) );
    }
    painter.setPen( text );
    painter.drawRect( plot );
    for ( const auto value : yTicks ) {
        const auto label = axisText( value, false, 0 );
        painter.drawText( QRectF( plot.left() - labelWidth - 6, yOf( value ) - line / 2.0, labelWidth + 2,
                                  line ),
                          Qt::AlignRight | Qt::AlignVCenter, label );
    }
    for ( const auto value : xTicks ) {
        const auto label = axisText( value, true, xHigh - xLow );
        const auto labelPixels = metrics.horizontalAdvance( label );
        painter.drawText( QRectF( xOf( value ) - labelPixels / 2.0, plot.bottom() + 2,
                                  labelPixels + 2, line ),
                          Qt::AlignHCenter | Qt::AlignTop, label );
    }
    if ( !xTitle_.isEmpty() ) {
        painter.drawText( QRectF( plot.left(), plot.bottom() + line + 4, plot.width(), line ),
                          Qt::AlignHCenter | Qt::AlignTop, xTitle_ );
    }
    if ( !yTitle_.isEmpty() ) {
        painter.save();
        painter.translate( 4, plot.center().y() );
        painter.rotate( -90 );
        painter.drawText( QRectF( -plot.height() / 2, 0, plot.height(), line ), Qt::AlignCenter,
                          yTitle_ );
        painter.restore();
    }
    if ( empty ) {
        return;
    }
    // The series.
    painter.save();
    painter.setClipRect( plot.adjusted( -1, -1, 1, 1 ) );
    const auto base = yOf( std::clamp( 0.0, yLow, yHigh ) );
    const auto count = static_cast<double>( std::max<qsizetype>( 1, series_.size() ) );
    for ( int index = 0; index < series_.size(); ++index ) {
        const auto& series = series_.at( index );
        const auto color = colorOf( index );
        if ( kind_ == Kind::Bar ) {
            const auto barWidth = std::max( 1.0, ( xOf( xLow + step ) - xOf( xLow ) ) * 0.8 / count );
            for ( const auto& point : series.points ) {
                const auto x = xOf( point.x() ) - barWidth * count / 2 + barWidth * index;
                const auto top = yOf( point.y() );
                painter.fillRect( QRectF( x, std::min( top, base ), barWidth,
                                          std::max( 1.0, std::abs( base - top ) ) ),
                                  color );
            }
            continue;
        }
        if ( kind_ == Kind::Scatter ) {
            painter.setPen( Qt::NoPen );
            painter.setBrush( color );
            for ( const auto& point : series.points ) {
                painter.drawEllipse( QPointF( xOf( point.x() ), yOf( point.y() ) ), 2.5, 2.5 );
            }
            continue;
        }
        QPolygonF polyline;
        polyline.reserve( series.points.size() );
        for ( const auto& point : series.points ) {
            polyline.append( QPointF( xOf( point.x() ), yOf( point.y() ) ) );
        }
        if ( kind_ == Kind::Area && polyline.size() > 1 ) {
            QPolygonF area = polyline;
            area.append( QPointF( polyline.back().x(), base ) );
            area.append( QPointF( polyline.front().x(), base ) );
            auto fill = color;
            fill.setAlpha( 70 );
            painter.setPen( Qt::NoPen );
            painter.setBrush( fill );
            painter.drawPolygon( area );
        }
        painter.setBrush( Qt::NoBrush );
        painter.setPen( QPen( color, 1.5 ) );
        if ( polyline.size() == 1 ) {
            painter.drawEllipse( polyline.front(), 2.0, 2.0 );
        }
        else {
            painter.drawPolyline( polyline );
        }
    }
    painter.restore();
    if ( !legendShown() ) {
        return;
    }
    // The legend, top right inside the plot.
    int legendWidth = 0;
    for ( const auto& series : series_ ) {
        legendWidth = std::max( legendWidth,
                                metrics.horizontalAdvance( ( series.title.isEmpty() ? series.id : series.title )
                                                               .left( 128 ) ) );
    }
    const QRectF box( plot.right() - legendWidth - 26, plot.top() + 4, legendWidth + 22,
                      line * static_cast<int>( series_.size() ) + 6 );
    auto background = palette().color( QPalette::Base );
    background.setAlpha( 220 );
    painter.setPen( grid );
    painter.setBrush( background );
    painter.drawRect( box );
    for ( int index = 0; index < series_.size(); ++index ) {
        const auto& series = series_.at( index );
        const auto y = box.top() + 3 + index * line;
        painter.fillRect( QRectF( box.left() + 4, y + line / 2.0 - 4, 10, 8 ), colorOf( index ) );
        painter.setPen( text );
        painter.drawText( QRectF( box.left() + 18, y, legendWidth + 2, line ), Qt::AlignLeft | Qt::AlignVCenter,
                          ( series.title.isEmpty() ? series.id : series.title ).left( 128 ) );
    }
}
