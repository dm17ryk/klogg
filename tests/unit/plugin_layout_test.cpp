// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Layout and content (E17f, docs/plugin-sdk-advanced.md section 29): the features, the manifest's
// dialog buttons, and the pure parts of the text view, image, sparkline and chart widgets.
#include "pluginchart.h"
#include "pluginfeatures.h"
#include "pluginmanifest.h"
#include "plugintextview.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QTextDocument>
#include <QUrl>
#include <catch2/catch.hpp>
#include <cmath>
#include <functional>
#include <limits>

using namespace cilogg::plugins;

namespace {
QJsonObject manifest( const QJsonObject& view )
{
    return { { "id", "org.test.layout" },
             { "name", "Layout" },
             { "version", "1.0.0" },
             { "protocol_version", 1 },
             { "executable", "plugin.exe" },
             { "arguments", QJsonArray{} },
             { "capabilities", QJsonArray{ "tool" } },
             { "configuration", QJsonArray{} },
             { "actions", QJsonArray{ QJsonObject{ { "id", "summary" }, { "capability", "tool" } } } },
             { "views", QJsonArray{ view } } };
}

QString parseError( const QJsonObject& view )
{
    return Manifest::parse( QJsonDocument( manifest( view ) ).toJson(), "folder" ).error;
}

QJsonObject dialog( const QJsonObject& extra )
{
    QJsonObject view{ { "id", "ask" }, { "title", "Ask" }, { "kind", "dialog" } };
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        view.insert( it.key(), it.value() );
    }
    return view;
}
} // namespace

TEST_CASE( "E17f announces layout, text_view, images, charts and view_dialogs", "[plugins][E17]" )
{
    for ( const auto* feature : { "layout", "text_view", "images", "charts", "view_dialogs" } ) {
        INFO( feature );
        REQUIRE( hostFeatureVersions().value( QLatin1String( feature ) ).toInt() == 1 );
    }
    REQUIRE( dialogButtonNames()
             == QStringList{ "ok", "cancel", "apply", "close", "yes", "no" } );
}

TEST_CASE( "E17f manifests validate dialog buttons", "[plugins][E17]" )
{
    REQUIRE( parseError( dialog( { { "buttons", QJsonArray{ "ok", "cancel", "apply" } },
                                   { "default", "ok" },
                                   { "confirm_by_plugin", true } } ) )
                 .isEmpty() );
    REQUIRE( parseError( dialog( { { "buttons", QJsonArray{ "yes", "no", "close" } } } ) ).isEmpty() );
    // Only dialogs take them.
    REQUIRE_FALSE( parseError( QJsonObject{ { "id", "dock" },
                                            { "buttons", QJsonArray{ "ok" } } } )
                       .isEmpty() );
    REQUIRE_FALSE( parseError( QJsonObject{ { "id", "window" },
                                            { "kind", "window" },
                                            { "confirm_by_plugin", true } } )
                       .isEmpty() );
    // Known names, each once; the default is one of them; flags are booleans.
    REQUIRE_FALSE( parseError( dialog( { { "buttons", QJsonArray{ "ok", "retry" } } } ) ).isEmpty() );
    REQUIRE_FALSE( parseError( dialog( { { "buttons", QJsonArray{ "ok", "ok" } } } ) ).isEmpty() );
    REQUIRE_FALSE( parseError( dialog( { { "buttons", "ok" } } ) ).isEmpty() );
    REQUIRE_FALSE( parseError( dialog( { { "buttons", QJsonArray{ 1 } } } ) ).isEmpty() );
    REQUIRE_FALSE(
        parseError( dialog( { { "buttons", QJsonArray{ "ok" } }, { "default", "cancel" } } ) )
            .isEmpty() );
    REQUIRE_FALSE( parseError( dialog( { { "default", "ok" } } ) ).isEmpty() );
    REQUIRE_FALSE(
        parseError( dialog( { { "buttons", QJsonArray{ "ok" } }, { "confirm_by_plugin", "yes" } } ) )
            .isEmpty() );
    // A dialog without them is as before.
    REQUIRE( parseError( dialog( {} ) ).isEmpty() );
}

TEST_CASE( "E17f text views keep the tail of long text and their limits", "[plugins][E17]" )
{
    REQUIRE( PluginTextView::tail( "abc", 10 ) == "abc" );
    // The tail starts at a line where possible.
    REQUIRE( PluginTextView::tail( "one\ntwo\nthree\n", 8 ) == "three\n" );
    REQUIRE( PluginTextView::tail( "abcdefghij", 4 ) == "ghij" );

    PluginTextView view( "t" );
    view.setText( "first\n" );
    view.append( "second\n" );
    REQUIRE( view.text() == "first\nsecond\n" );
    view.setMaxLines( 2 );
    view.append( "third\n" );
    // Two blocks: the last line and the empty one after the final newline.
    REQUIRE( view.text() == "third\n" );
    view.setMaxLines( 0 );
    // 1 MiB in all: the oldest lines go.
    const QString line = QString( 1023, QLatin1Char( 'x' ) ) + '\n';
    QString big;
    for ( int index = 0; index < 1100; ++index ) {
        big += line;
    }
    view.setText( big );
    REQUIRE( view.text().size() <= MaxTextViewChars );
    REQUIRE( view.text().startsWith( 'x' ) );
    view.append( "tail\n" );
    REQUIRE( view.text().size() <= MaxTextViewChars + 1 );
    REQUIRE( view.text().endsWith( "tail\n" ) );
    // Markdown keeps its source and renders it; appended Markdown joins the source.
    view.setMarkdown( true );
    view.setText( "# Title\n\nSome *text*.\n" );
    view.append( "\n[link](https://example.com)\n" );
    REQUIRE( view.text().endsWith( "[link](https://example.com)\n" ) );
#if QT_VERSION >= QT_VERSION_CHECK( 5, 14, 0 )
    REQUIRE_FALSE( view.browser()->toPlainText().contains( '#' ) );
    REQUIRE( view.browser()->toPlainText().contains( "Title" ) );
#endif
    // Find wraps around.
    view.setMarkdown( false );
    view.setText( "alpha beta alpha" );
    view.findBar()->setText( "alpha" );
    REQUIRE( view.findNext( false ) );
    REQUIRE( view.findNext( false ) );
    REQUIRE( view.findNext( false ) );
    view.findBar()->setText( "missing" );
    REQUIRE_FALSE( view.findNext( false ) );
}

TEST_CASE( "E17f Markdown never loads remote or file images and has HTML off", "[plugins][E17]" )
{
    PluginTextView view( "md" );
    QStringList asked;
    view.browser()->imageFor = [ &asked ]( const QString& reference ) {
        asked.append( reference );
        QImage image( 8, 8, QImage::Format_ARGB32 );
        image.fill( Qt::red );
        return reference.startsWith( "builtin:" ) ? image : QImage{};
    };
    view.setMarkdown( true );
    view.setText( "![a](builtin:info) ![b](https://example.com/x.png) <b>bold</b> "
                  "<img src=\"file:///c:/x.png\">" );
#if QT_VERSION >= QT_VERSION_CHECK( 5, 14, 0 )
    // HTML is shown as text, never interpreted.
    REQUIRE( view.browser()->toPlainText().contains( "<b>bold</b>" ) );
    REQUIRE_FALSE( asked.contains( "file:///c:/x.png" ) );
#endif
    REQUIRE( view.browser()->loadResource( QTextDocument::ImageResource, QUrl( "builtin:info" ) )
                 .isValid() );
    REQUIRE_FALSE( view.browser()
                       ->loadResource( QTextDocument::ImageResource, QUrl( "https://example.com/x.png" ) )
                       .isValid() );
    REQUIRE_FALSE( view.browser()->loadResource( QTextDocument::StyleSheetResource, QUrl( "a.css" ) )
                       .isValid() );
}

TEST_CASE( "E17f images are placed by their scale", "[plugins][E17]" )
{
    const QRect box( 0, 0, 100, 50 );
    REQUIRE( PluginImage::placement( QSize( 200, 200 ), box, PluginImage::Scale::Fit )
             == QRectF( 25, 0, 50, 50 ) );
    REQUIRE( PluginImage::placement( QSize( 200, 200 ), box, PluginImage::Scale::Fill )
             == QRectF( box ) );
    REQUIRE( PluginImage::placement( QSize( 20, 10 ), box, PluginImage::Scale::None )
             == QRectF( 40, 20, 20, 10 ) );
    REQUIRE( PluginImage::placement( QSize(), box, PluginImage::Scale::Fit ).isNull() );
    PluginImage image;
    image.setAlt( "No picture" );
    REQUIRE( image.sizeHint().width() > 0 );
    QImage picture( 40, 20, QImage::Format_ARGB32 );
    picture.fill( Qt::blue );
    image.setImage( picture );
    REQUIRE( image.sizeHint() == QSize( 40, 20 ) );
    image.setBox( 80, 0 );
    REQUIRE( image.sizeHint() == QSize( 80, 40 ) ); // the aspect ratio stays
    image.setBox( 0, 0 );
    REQUIRE( image.sizeHint() == QSize( 40, 20 ) );
}

TEST_CASE( "E17f sparklines and charts keep their limits", "[plugins][E17]" )
{
    PluginSparkline spark;
    QVector<double> values;
    for ( int index = 0; index < MaxSparklineValues + 10; ++index ) {
        values.append( index );
    }
    spark.setValues( values );
    REQUIRE( spark.values().size() == MaxSparklineValues );
    REQUIRE( spark.values().front() == 10 );
    spark.setValues( { 2, std::numeric_limits<double>::quiet_NaN(), 5 } );
    REQUIRE( spark.range() == std::pair<double, double>( 2, 5 ) );
    spark.setRange( 0, std::nullopt );
    REQUIRE( spark.range() == std::pair<double, double>( 0, 5 ) );
    spark.setRange( std::nullopt, std::nullopt );
    spark.setBars( true );
    REQUIRE( spark.range().first == 0 ); // bars grow from zero
    spark.setValues( { 3, 3 } );
    spark.setBars( false );
    REQUIRE( spark.range().second > spark.range().first );

    PluginChart chart;
    QVector<PluginChart::Series> series;
    for ( int index = 0; index < MaxChartSeries + 2; ++index ) {
        series.append( { QString( "s%1" ).arg( index ), {}, {}, { QPointF( 0, index ) } } );
    }
    chart.setSeries( series );
    REQUIRE( chart.series().size() == MaxChartSeries );
    REQUIRE( chart.legendShown() );
    chart.setLegend( false );
    REQUIRE_FALSE( chart.legendShown() );
    REQUIRE( chart.nextX( "s0" ) == 1 );
    REQUIRE( chart.appendPoints( "s0", { QPointF( 1, 1 ), QPointF( 2, 2 ) } ) );
    REQUIRE_FALSE( chart.appendPoints( "ghost", { QPointF( 1, 1 ) } ) );
    REQUIRE( chart.pointCount( "s0" ) == 3 );
    chart.setWindow( 2 );
    REQUIRE( chart.pointCount( "s0" ) == 2 );
    REQUIRE( chart.series().front().points.front() == QPointF( 1, 1 ) );
    chart.setWindow( 0 );
    QVector<QPointF> many;
    for ( int index = 0; index < MaxChartPoints + 5; ++index ) {
        many.append( QPointF( index, index ) );
    }
    REQUIRE( chart.appendPoints( "s1", many ) );
    REQUIRE( chart.pointCount( "s1" ) == MaxChartPoints );
    chart.clear( { "s1" } );
    REQUIRE( chart.pointCount( "s1" ) == 0 );
    REQUIRE( chart.pointCount( "s0" ) == 2 );
    chart.clear( {} );
    REQUIRE( chart.pointCount( "s0" ) == 0 );
    // Series without a tone take the palette; distinct colours for the first ones.
    REQUIRE( chart.colorOf( 0 ) != chart.colorOf( 1 ) );
}

TEST_CASE( "E17f chart ticks are round numbers covering the range", "[plugins][E17]" )
{
    const auto ticks = PluginChart::ticks( 0, 10, 5 );
    REQUIRE( ticks == QVector<double>{ 0, 2, 4, 6, 8, 10 } );
    const auto fractions = PluginChart::ticks( 0.1, 0.9, 4 );
    REQUIRE_FALSE( fractions.isEmpty() );
    REQUIRE( fractions.front() >= 0.1 - 1e-9 );
    REQUIRE( fractions.back() <= 0.9 + 1e-9 );
    REQUIRE( PluginChart::ticks( -5, 5, 5 ).contains( 0.0 ) );
    REQUIRE( PluginChart::ticks( 1, 1, 5 ).isEmpty() );
    REQUIRE( PluginChart::ticks( 0, std::numeric_limits<double>::infinity(), 5 ).isEmpty() );
    REQUIRE( PluginChart::ticks( 0, 1e12, 5 ).size() <= 64 );
}
