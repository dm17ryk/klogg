// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>
#include <optional>
#include <utility>

// Pictures and small charts of plugin views (docs/plugin-sdk-advanced.md section 29.2), drawn by
// the host without Qt Charts.

// An `image` element: a picture scaled into its box, or its `alt` text when there is none.
class PluginImage final : public QWidget {
    Q_OBJECT
public:
    enum class Scale { Fit, Fill, None };
    explicit PluginImage( QWidget* parent = nullptr );
    void setImage( const QImage& image );
    // 0: not given (the image's own size along that side).
    void setBox( int width, int height );
    void setScale( Scale scale );
    void setAlt( const QString& alt );
    const QImage& image() const
    {
        return image_;
    }
    Scale scale() const
    {
        return scale_;
    }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    // Where the image is drawn in `area` for `scale`.
    static QRectF placement( const QSize& image, const QRect& area, Scale scale );

protected:
    void paintEvent( QPaintEvent* event ) override;

private:
    QImage image_;
    QString alt_;
    int width_ = 0;
    int height_ = 0;
    Scale scale_ = Scale::Fit;
};

// A `sparkline`: a small line or bar chart of values; NaN is a gap.
class PluginSparkline final : public QWidget {
    Q_OBJECT
public:
    explicit PluginSparkline( QWidget* parent = nullptr );
    void setValues( const QVector<double>& values );
    void setRange( std::optional<double> minimum, std::optional<double> maximum );
    void setBars( bool bars );
    void setColor( const QColor& color );
    const QVector<double>& values() const
    {
        return values_;
    }
    // The range drawn: the given bounds, else the values' range.
    std::pair<double, double> range() const;
    QSize sizeHint() const override;

protected:
    void paintEvent( QPaintEvent* event ) override;

private:
    QVector<double> values_;
    std::optional<double> minimum_;
    std::optional<double> maximum_;
    QColor color_;
    bool bars_ = false;
};

// A `chart`: up to MaxChartSeries series of points as lines, bars, dots or areas, with axes,
// titles and a legend.
class PluginChart final : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Line, Bar, Scatter, Area };
    struct Series {
        QString id;
        QString title;
        QColor color; // invalid: the palette colour of its position
        QVector<QPointF> points;
    };
    explicit PluginChart( QWidget* parent = nullptr );
    void setKind( Kind kind );
    // At most MaxChartSeries; each keeps its last points within the window and MaxChartPoints.
    void setSeries( QVector<Series> series );
    // False when there is no such series.
    bool appendPoints( const QString& seriesId, const QVector<QPointF>& points );
    void clear( const QStringList& seriesIds ); // empty: every series
    void setTitles( const QString& xTitle, const QString& yTitle );
    void setTimeAxis( bool time );
    void setLegend( std::optional<bool> legend );
    void setWindow( int points ); // 0: no window
    const QVector<Series>& series() const
    {
        return series_;
    }
    int pointCount( const QString& seriesId ) const;
    Kind kind() const
    {
        return kind_;
    }
    bool legendShown() const;
    // The x of the next point appended without one: one after the series' last x (0 when empty).
    double nextX( const QString& seriesId ) const;
    QColor colorOf( int index ) const;
    // Round tick values covering [low, high], about `count` of them.
    static QVector<double> ticks( double low, double high, int count );
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent( QPaintEvent* event ) override;

private:
    void keepWindow( Series& series ) const;
    QString axisText( double value, bool xAxis, double span ) const;

    QVector<Series> series_;
    QString xTitle_;
    QString yTitle_;
    std::optional<bool> legend_;
    Kind kind_ = Kind::Line;
    int window_ = 0;
    bool time_ = false;
};
