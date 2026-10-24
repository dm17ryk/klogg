// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "abstractlogview.h"
#include "analysisregex.h"
#include "logdata.h"
#include "quickfindpattern.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScrollBar>
#include <QTest>
#include <QTimer>
#include <catch2/catch.hpp>
#include <cstdio>

namespace {
class WorkbenchView final : public AbstractLogView {
public:
    WorkbenchView( const AbstractLogData* data, const QuickFindPattern* pattern )
        : AbstractLogView( data, pattern )
    {
        resize( 1000, 600 );
        textWrapSet( false );
        show();
        updateDisplaySize();
    }
    int paints = 0;

protected:
    void paintEvent( QPaintEvent* event ) override
    {
        AbstractLogView::paintEvent( event );
        ++paints;
    }
    AbstractLogData::LineType lineType( LineNumber ) const override
    {
        return {};
    }
};

struct PostedSample {
    QElapsedTimer timer;
    double dispatchMs = 0;
    double paintMs = 0;
    bool painted = false;
    bool done = false;
};

double milliseconds( const QElapsedTimer& timer )
{
    return static_cast<double>( timer.nsecsElapsed() ) / 1000000.0;
}

bool active( const QVariantMap& state )
{
    const auto name = state.value( "state" ).toString();
    return name == "queued" || name == "running";
}

QJsonArray sampleView( WorkbenchView& view, int count, AnalysisJobService* service = nullptr,
                       const QString& job = {} )
{
    QJsonArray samples;
    for ( int index = 0; index < count + 10; ++index ) {
        if ( service )
            REQUIRE( active( service->status( job ) ) );
        const int maximum = view.verticalScrollBar()->maximum();
        REQUIRE( maximum > 0 );
        int position = ( index * 7919 ) % ( maximum + 1 );
        if ( position == view.verticalScrollBar()->value() ) {
            position = ( position + 1 ) % ( maximum + 1 );
        }
        auto sample = std::make_shared<PostedSample>();
        sample->timer.start();
        QTimer::singleShot( 0, &view, [ sample, &view, position ] {
            sample->dispatchMs = milliseconds( sample->timer );
            const auto before = view.paints;
            view.verticalScrollBar()->setValue( position );
            // Measure completion of Qt's CPU painting, not compositor presentation.
            view.viewport()->repaint();
            sample->paintMs = milliseconds( sample->timer );
            sample->painted = view.paints > before;
            sample->done = true;
        } );
        REQUIRE( QTest::qWaitFor( [ sample ] { return sample->done; }, 5000 ) );
        REQUIRE( sample->painted );
        REQUIRE( view.verticalScrollBar()->value() == position );
        if ( service )
            REQUIRE( active( service->status( job ) ) );
        if ( index >= 10 )
            samples.append( QJsonArray{ sample->dispatchMs, sample->paintMs } );
    }
    return samples;
}
} // namespace

// Explicit opt-in: Windows' ordinary [ui] selection excludes [workbench-native].
TEST_CASE( "Measure native GUI dispatch painting and service cancellation",
           "[.][ui][workbench-native][G02]" )
{
    QFile input;
    REQUIRE( input.open( stdin, QIODevice::ReadOnly ) );
    REQUIRE( input.readLine() == "start\n" );
    const auto sourcePath = qEnvironmentVariable( "CILOGG_G02_SOURCE" );
    const auto reportPath = qEnvironmentVariable( "CILOGG_G02_REPORT" );
    bool rowsValid = false;
    const auto rows = qEnvironmentVariable( "CILOGG_G02_ROWS" ).toULongLong( &rowsValid );
    bool samplesValid = false;
    const auto count = qEnvironmentVariable( "CILOGG_G02_SAMPLES" ).toInt( &samplesValid );
    REQUIRE( rowsValid );
    REQUIRE( rows >= 1000 );
    REQUIRE( rows <= 1000000 );
    REQUIRE( samplesValid );
    REQUIRE( count >= 100 );
    REQUIRE( count <= 1000 );
    REQUIRE_FALSE( reportPath.isEmpty() );
    REQUIRE( QElapsedTimer::isMonotonic() );
    QFile source( sourcePath );
    REQUIRE( source.open( QIODevice::ReadOnly ) );
    const auto bytes = source.size();
    REQUIRE( bytes <= 128 * 1024 * 1024 );
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    REQUIRE( hash.addData( &source ) );
    const auto digest = QString::fromLatin1( hash.result().toHex() );
    source.close();

    auto data = std::make_shared<LogData>();
    data->attachFile( sourcePath );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto snapshot = data->sourceSnapshot();
            return snapshot.available && snapshot.indexedBytes == bytes
                   && data->getNbLine().get() == rows;
        },
        30000 ) );
    REQUIRE( data->getDetectedEncoding() != nullptr );
    data->setDisplayEncoding( data->getDetectedEncoding()->name().constData() );
    QuickFindPattern pattern;
    WorkbenchView view( data.get(), &pattern );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            return view.viewport()->isVisible() && view.verticalScrollBar()->maximum() > 0
                   && view.paints > 0;
        },
        5000 ) );
    qInfo( "G02b: indexed source and visible viewport ready; collecting idle samples" );
    const auto idle = sampleView( view, count );

    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = QString( 100000, QLatin1Char( 'a' ) ) + QLatin1Char( '!' );
    request.limits.batchTimeoutMs = 30000;
    auto entered = std::make_shared<std::atomic_bool>( false );
    const auto factory
        = makeRegexProcessor( { { "pattern", "(*NO_JIT)(*NO_START_OPT)(*NO_AUTO_POSSESS)a+b" } },
                              QCoreApplication::applicationFilePath() );
    const auto job = service.start( request, [ factory, entered ] {
        auto processor = factory();
        return [ processor, entered ]( const AnalysisBatch& batch, const std::atomic_bool& cancel,
                                       int timeout ) {
            if ( !batch.lines.empty() )
                entered->store( true );
            auto result = processor( batch, cancel, timeout );
            if ( !batch.lines.empty() )
                entered->store( false );
            return result;
        };
    } );
    REQUIRE_FALSE( job.isEmpty() );
    REQUIRE( QTest::qWaitFor( [ & ] { return entered->load(); }, 10000 ) );
    REQUIRE( active( service.status( job ) ) );
    qInfo( "G02b: supervised regex job=%s batch entered; collecting active-worker samples",
           qPrintable( job ) );
    const auto busy = sampleView( view, count, &service, job );
    REQUIRE( entered->load() );
    QElapsedTimer cancellation;
    cancellation.start();
    REQUIRE( service.cancel( job ) );
    const auto ackMs = milliseconds( cancellation );
    REQUIRE( QTest::qWaitFor( [ & ] { return !active( service.status( job ) ); }, 5000 ) );
    const auto terminalMs = milliseconds( cancellation );
    const auto finalState = service.status( job ).value( "state" ).toString();
    REQUIRE( finalState == "cancelled" );
    REQUIRE( service.release( job ) );
    request.text = "ok";
    const auto recovery
        = service.start( request, makeRegexProcessor( { { "pattern", "ok" } },
                                                      QCoreApplication::applicationFilePath() ) );
    REQUIRE( QTest::qWaitFor( [ & ] { return !active( service.status( recovery ) ); }, 10000 ) );
    REQUIRE( service.status( recovery ).value( "complete" ).toBool() );
    REQUIRE( service.results( recovery ).value( "rows" ).toList().size() == 1 );
    REQUIRE( service.release( recovery ) );
    qInfo( "G02b: cancellation reached terminal state and next real worker recovered" );
    const QJsonObject report{
        { "schemaVersion", 1 },
        { "scenario", "ui-service" },
        { "source", QJsonObject{ { "sha256", digest },
                                 { "bytes", QString::number( bytes ) },
                                 { "physicalLines", QString::number( rows ) } } },
        { "qt", QJsonObject{ { "compile", QT_VERSION_STR },
                             { "runtime", qVersion() },
                             { "platform", QGuiApplication::platformName() } } },
        { "contexts",
          QJsonArray{ QJsonObject{ { "name", "idle" }, { "samplesMs", idle } },
                      QJsonObject{ { "name", "analysis_active" }, { "samplesMs", busy } } } },
        { "workerActiveDuringSamples", true },
        { "recoveryComplete", true },
        { "cancellation",
          QJsonObject{ { "ackMs", ackMs }, { "terminalMs", terminalMs }, { "state", finalState } } }
    };
    QSaveFile output( reportPath );
    REQUIRE( output.open( QIODevice::WriteOnly ) );
    const auto json = QJsonDocument( report ).toJson( QJsonDocument::Compact );
    REQUIRE( output.write( json ) == json.size() );
    REQUIRE( output.commit() );
    qInfo( "G02b: native timing report committed" );
}
