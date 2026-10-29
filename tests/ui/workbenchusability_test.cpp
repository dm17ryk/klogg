// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysiscommands.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "regexlabdock.h"
#include "sessioninfo.h"
#include "valuecountdock.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QSet>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QWindow>
#include <catch2/catch.hpp>
#include <cstdio>

namespace {
const QString sourceText = QString::fromUtf8( u8"שלום 😀1\nsecond2\nthird1" );

class PaintCounter final : public QObject {
public:
    QWidget* target = nullptr;
    int paints = 0;

protected:
    bool eventFilter( QObject* object, QEvent* event ) override
    {
        auto* widget = qobject_cast<QWidget*>( object );
        if ( event->type() == QEvent::Paint && widget && widget->window() == target )
            ++paints;
        return false;
    }
};

template <typename T>
T* control( QWidget* root, const char* name )
{
    auto* widget = root->findChild<T*>( name );
    INFO( name );
    REQUIRE( widget );
    REQUIRE_FALSE( widget->accessibleName().isEmpty() );
    return widget;
}

void click( QWidget* widget, const QPoint& position )
{
    INFO( widget->objectName().toStdString() );
    REQUIRE( widget->isVisible() );
    REQUIRE( widget->isEnabled() );
    REQUIRE( widget->rect().contains( position ) );
    REQUIRE_FALSE( QApplication::activeModalWidget() );
    const auto global = widget->mapToGlobal( position );
    qInfo() << "G03c: input_target object=" << widget->objectName() << " point=" << global
            << " window=" << widget->window()->geometry()
            << " screen=" << widget->window()->windowHandle()->screen()->availableGeometry();
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            auto* hit = QApplication::widgetAt( widget->mapToGlobal( position ) );
            return hit && ( hit == widget || widget->isAncestorOf( hit ) );
        },
        5000 ) );
    auto* hit = QApplication::widgetAt( widget->mapToGlobal( position ) );
    REQUIRE( hit );
    REQUIRE( ( hit == widget || widget->isAncestorOf( hit ) ) );
    qInfo( "G03c: click object=%s", qPrintable( widget->objectName() ) );
    QTest::mouseClick( widget, Qt::LeftButton, Qt::NoModifier, position );
}

void click( QWidget* widget )
{
    click( widget, widget->rect().center() );
}

void fitWindow( QWidget* window )
{
    auto* screen = QGuiApplication::primaryScreen();
    REQUIRE( screen );
    const auto bounds = screen->availableGeometry().adjusted( 20, 20, -20, -20 );
    // These temporary probe windows must not capture or click through another app.
    window->setWindowFlags( window->windowFlags() | Qt::WindowStaysOnTopHint );
    window->resize( qMin( 1000, bounds.width() ), qMin( 850, bounds.height() ) );
    window->move( bounds.center() - window->rect().center() );
    window->show();
    window->raise();
    window->activateWindow();
    REQUIRE( QTest::qWaitForWindowExposed( window, 5000 ) );
    REQUIRE( QTest::qWaitForWindowActive( window, 5000 ) );
}

QJsonObject captureView( QWidget* top, PaintCounter& counter, const QDir& artifacts,
                         const QString& name )
{
    counter.target = top;
    const int before = counter.paints;
    top->update();
    REQUIRE( QTest::qWaitFor( [ & ] { return counter.paints > before; }, 5000 ) );
    REQUIRE( top->windowHandle() );
    REQUIRE( top->windowHandle()->isExposed() );
    auto image = top->windowHandle()->screen()->grabWindow( top->winId() ).toImage();
    REQUIRE_FALSE( image.isNull() );
    REQUIRE( image.width() >= 200 );
    REQUIRE( image.height() >= 200 );
    QSet<QRgb> colors;
    for ( int row = 0; row < 64; ++row ) {
        for ( int column = 0; column < 64; ++column )
            colors.insert( image.pixel( column * image.width() / 64, row * image.height() / 64 ) );
    }
    REQUIRE( colors.size() > 1 );
    const auto filename = name + ".png";
    REQUIRE( image.save( artifacts.filePath( filename ), "PNG" ) );
    QFile file( artifacts.filePath( filename ) );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    REQUIRE( hash.addData( &file ) );
    qInfo( "G03c: captured view=%s paints=%d size=%dx%d", qPrintable( name ),
           counter.paints - before, image.width(), image.height() );
    return { { "visible", top->isVisible() },
             { "exposed", top->windowHandle()->isExposed() },
             { "paints", counter.paints - before },
             { "sampledColors", static_cast<int>( colors.size() ) },
             { "screenshot", filename },
             { "sha256", QString::fromLatin1( hash.result().toHex() ) },
             { "width", image.width() },
             { "height", image.height() } };
}
} // namespace

// No [ui] tag: this native desktop probe must never join ordinary offscreen CTest.
TEST_CASE( "Native docks accept input and expose usable docked and floating results",
           "[.][workbench-usability][G03]" )
{
    QFile input;
    REQUIRE( input.open( stdin, QIODevice::ReadOnly ) );
    REQUIRE( input.readLine() == "start\n" );
    const auto reportPath = qEnvironmentVariable( "CILOGG_G03_UI_REPORT" );
    REQUIRE_FALSE( reportPath.isEmpty() );
    const auto platform = QGuiApplication::platformName();
    REQUIRE( QStringList( { "windows", "cocoa", "xcb", "wayland", "wayland-egl" } )
                 .contains( platform ) );
    QFile worker( ANALYSIS_WORKER_PATH );
    REQUIRE( worker.open( QIODevice::ReadOnly ) );
    QCryptographicHash workerHash( QCryptographicHash::Sha256 );
    REQUIRE( workerHash.addData( &worker ) );
    worker.close();
    QJsonArray cases;
    auto* service = applicationAnalysisService();
    for ( const bool counts : { false, true } ) {
        const auto name = counts ? QString( "value-counts" ) : QString( "regex-lab" );
        const auto prefix = counts ? QString( "valueCounts" ) : QString( "regexLab" );
        qInfo( "G03c: case_started id=%s", qPrintable( name ) );
        QMainWindow window;
        CommanderRequest applied;
        auto execute = [ & ]( const CommanderRequest& request ) {
            if ( request.action == CommanderAction::Search
                 || request.action == CommanderAction::ApplyValueCount ) {
                applied = request;
                return commanderSuccess();
            }
            auto result
                = executeAnalysisOperation( *service, commanderActionToString( request.action ),
                                            request.analysisArguments, {}, ANALYSIS_WORKER_PATH );
            return CommanderResult{ result.ok ? CommanderResultCode::Success
                                              : CommanderResultCode::InvalidRequest,
                                    result.payload.value( "reason" ).toString(), result.payload };
        };
        RegexLabDock* lab = nullptr;
        ValueCountDock* values = nullptr;
        if ( counts )
            values = new ValueCountDock( &window, execute, [] { return QVariantList{}; } );
        else
            lab = new RegexLabDock(
                &window, execute, [] { return QVariantList{}; },
                []( const QString&, quint64 ) { return commanderSuccess(); } );
        QDockWidget* dock = counts ? static_cast<QDockWidget*>( values ) : lab;
        auto state = [ & ] { return counts ? values->state() : lab->state(); };
        window.addDockWidget( Qt::RightDockWidgetArea, dock );
        PaintCounter counter;
        counter.target = &window;
        qApp->installEventFilter( &counter );
        fitWindow( &window );
        auto* scope = control<QComboBox>( dock, qPrintable( prefix + "Scope" ) );
        click( scope );
        QTest::keyClick( scope, Qt::Key_Escape );
        QTest::keyClick( scope, Qt::Key_Home );
        const int index = scope->findData( "text" );
        REQUIRE( index >= 0 );
        for ( int step = 0; step < index; ++step )
            QTest::keyClick( scope, Qt::Key_Down );
        REQUIRE( scope->currentData() == "text" );
        auto* pattern = control<QLineEdit>( dock, qPrintable( prefix + "Pattern" ) );
        click( pattern );
        QTest::keyClick( pattern, Qt::Key_Home );
        QTest::keyClick( pattern, Qt::Key_End, Qt::ShiftModifier );
        QTest::keyClicks( pattern, "(?<value>\\d)" );
        REQUIRE( pattern->text() == "(?<value>\\d)" );
        auto* text = control<QPlainTextEdit>( dock, qPrintable( prefix + "Text" ) );
        click( text->viewport() );
        QInputMethodEvent commit;
        commit.setCommitString( sourceText );
        QApplication::sendEvent( text, &commit );
        REQUIRE( text->toPlainText() == sourceText );
        click( control<QPushButton>( dock, qPrintable( prefix + "Run" ) ) );
        REQUIRE( QTest::qWaitFor(
            [ & ] { return state().value( "job" ).toMap().value( "state" ) == "completed"; },
            15000 ) );
        REQUIRE( state().value( "job" ).toMap().value( "complete" ).toBool() );
        auto* table = control<QTableWidget>( dock, qPrintable( prefix + "Results" ) );
        REQUIRE( table->rowCount() == ( counts ? 2 : 3 ) );
        QJsonArray rows;
        for ( int row = 0; row < table->rowCount(); ++row ) {
            if ( counts )
                rows.append(
                    QJsonArray{ table->item( row, 0 )->text(), table->item( row, 1 )->text() } );
            else
                rows.append( table->item( row, 0 )->text() );
        }
        const auto cell = table->visualItemRect( table->item( 0, 0 ) );
        REQUIRE_FALSE( cell.isEmpty() );
        click( table->viewport(), cell.center() );
        REQUIRE( table->currentRow() == 0 );
        click( control<QPushButton>( dock, qPrintable( prefix + "Apply" ) ) );
        REQUIRE( applied.action
                 == ( counts ? CommanderAction::ApplyValueCount : CommanderAction::Search ) );
        if ( counts )
            REQUIRE( applied.analysisArguments.value( "rowIndex" ).toInt() == 0 );
        else
            REQUIRE( applied.searchText == pattern->text() );
        QJsonObject views;
        const QDir artifacts = QFileInfo( reportPath ).absoluteDir();
        views.insert( "docked", captureView( &window, counter, artifacts, name + "-docked" ) );
        dock->setFloating( true );
        fitWindow( dock );
        views.insert( "floating", captureView( dock, counter, artifacts, name + "-floating" ) );
        click( pattern );
        QTest::keyClick( pattern, Qt::Key_End );
        QTest::keyClicks( pattern, "x" );
        REQUIRE( state().value( "draftChanged" ).toBool() );
        REQUIRE_FALSE( control<QPushButton>( dock, qPrintable( prefix + "Apply" ) )->isEnabled() );
        const auto released = counts ? values->setDefinition( { { "operation", "release" } } )
                                     : lab->setDefinition( { { "operation", "release" } } );
        REQUIRE( released.ok() );
        REQUIRE( state().value( "jobId" ).toString().isEmpty() );
        cases.append( QJsonObject{ { "id", name },
                                   { "views", views },
                                   { "rows", rows },
                                   { "patternKeys", true },
                                   { "textInputMethod", true },
                                   { "runClicked", true },
                                   { "resultClicked", true },
                                   { "applyClicked", true },
                                   { "draftInvalidatesApply", true },
                                   { "complete", true } } );
        qInfo( "G03c: case_completed id=%s retained_job_released=true", qPrintable( name ) );
    }
    const QJsonObject report{
        { "schemaVersion", 1 },
        { "platform", platform },
        { "qt", QJsonObject{ { "compile", QT_VERSION_STR }, { "runtime", qVersion() } } },
        { "workerBinarySha256", QString::fromLatin1( workerHash.result().toHex() ) },
        { "sourceFixtureSha256",
          QString::fromLatin1(
              QCryptographicHash::hash( sourceText.toUtf8(), QCryptographicHash::Sha256 )
                  .toHex() ) },
        { "ownedJobsReleased", true },
        { "cases", cases }
    };
    QSaveFile output( reportPath );
    REQUIRE( output.open( QIODevice::WriteOnly ) );
    const auto bytes = QJsonDocument( report ).toJson();
    REQUIRE( output.write( bytes ) == bytes.size() );
    REQUIRE( output.commit() );
}

#if !defined( Q_OS_DARWIN )
namespace {
void chooseData( QComboBox* combo, const QString& value )
{
    const int index = combo->findData( value );
    INFO( value.toStdString() );
    REQUIRE( index >= 0 );
    click( combo );
    QTest::keyClick( combo, Qt::Key_Escape );
    QTest::keyClick( combo, Qt::Key_Home );
    for ( int step = 0; step < index; ++step )
        QTest::keyClick( combo, Qt::Key_Down );
    REQUIRE( combo->currentData() == value );
}

void exerciseMainWindow()
{
    QFile input;
    REQUIRE( input.open( stdin, QIODevice::ReadOnly ) );
    REQUIRE( input.readLine() == "start\n" );
    const auto reportPath = qEnvironmentVariable( "CILOGG_G03_UI_REPORT" );
    REQUIRE_FALSE( reportPath.isEmpty() );
    const auto platform = QGuiApplication::platformName();
    REQUIRE( QStringList( { "windows", "xcb", "wayland", "wayland-egl" } ).contains( platform ) );
    // Real MainWindow requests use the current integration executable's worker mode.
    QFile worker( QCoreApplication::applicationFilePath() );
    REQUIRE( worker.open( QIODevice::ReadOnly ) );
    QCryptographicHash workerHash( QCryptographicHash::Sha256 );
    REQUIRE( workerHash.addData( &worker ) );
    worker.close();
    struct RestoreSession {
        SessionInfo previous = SessionInfo::getSynced();
        ~RestoreSession()
        {
            SessionInfo::get() = previous;
            SessionInfo::get().save();
        }
    } restoreSession;
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "native-source.log" );
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( sourceText.toUtf8() ) == sourceText.toUtf8().size() );
    }
    QJsonArray cases;
    auto* service = applicationAnalysisService();
    for ( const bool counts : { false, true } ) {
        const auto name = counts ? QString( "value-counts" ) : QString( "regex-lab" );
        const auto prefix = counts ? QString( "valueCounts" ) : QString( "regexLab" );
        const auto windowId = "G03e-" + name;
        const QDir artifacts = QFileInfo( reportPath ).absoluteDir();
        auto session = std::make_shared<Session>();
        auto& settings = SessionInfo::getSynced();
        settings = SessionInfo{};
        settings.add( windowId );
        settings.save();
        QJsonObject views;
        QVariantMap source;
        QVariantMap expectedSource;
        QVariantMap unicodeMatch;
        QString expectedRevision;
        QString factoryPattern;
        QJsonArray rows;
        const int navigationLine = counts ? 2 : 3;
        qInfo( "G03e: mainwindow_case_started id=%s", qPrintable( name ) );
        {
            MainWindow window( WindowSession{ session, windowId, 0 } );
            fitWindow( &window );
            auto* lab = window.findChild<RegexLabDock*>();
            auto* values = window.findChild<ValueCountDock*>();
            REQUIRE( lab );
            REQUIRE( values );
            QDockWidget* dock = counts ? static_cast<QDockWidget*>( values ) : lab;
            auto state = [ & ] { return counts ? values->state() : lab->state(); };
            auto* action = window.findChild<QAction*>( counts ? "showValueCountsAction"
                                                              : "showRegexLabAction" );
            factoryPattern = control<QLineEdit>( dock, qPrintable( prefix + "Pattern" ) )->text();
            REQUIRE( action );
            REQUIRE_FALSE( action->icon().isNull() );
            QWidget* toolbarButton = nullptr;
            for ( auto* toolbar : window.findChildren<QToolBar*>() ) {
                auto* button = toolbar->widgetForAction( action );
                if ( button && button->isVisible() )
                    toolbarButton = button;
            }
            REQUIRE( toolbarButton );
            REQUIRE_FALSE( dock->isVisible() );
            click( toolbarButton );
            REQUIRE( QTest::qWaitFor( [ & ] { return dock->isVisible(); }, 5000 ) );
            click( toolbarButton );
            REQUIRE( QTest::qWaitFor( [ & ] { return !dock->isVisible(); }, 5000 ) );
            QMenu* menu = nullptr;
            for ( auto* root : window.menuBar()->actions() ) {
                if ( root->menu() && root->menu()->actions().contains( action ) )
                    menu = root->menu();
            }
            REQUIRE( menu );
            click( window.menuBar(),
                   window.menuBar()->actionGeometry( menu->menuAction() ).center() );
            REQUIRE( QTest::qWaitFor( [ & ] { return menu->isVisible(); }, 5000 ) );
            click( menu, menu->actionGeometry( action ).center() );
            REQUIRE( QTest::qWaitFor( [ & ] { return dock->isVisible(); }, 5000 ) );
            qInfo( "G03e: discovery_verified id=%s toolbar=true menu=true", qPrintable( name ) );
            CommanderRequest open;
            open.action = CommanderAction::OpenFile;
            open.filePath = path;
            REQUIRE( window.executeCommanderRequest( open ).ok() );
            auto* crawler = window.findChild<CrawlerWidget*>();
            REQUIRE( crawler );
            REQUIRE( QTest::qWaitFor(
                [ & ] {
                    if ( crawler->isLoadingInProgress() )
                        return false;
                    QString error;
                    const auto request = crawler->analysisRequest( "whole_file", &error );
                    return request && request->snapshot.lineCount.get() == 3;
                },
                10000 ) );
            chooseData( control<QComboBox>( dock, qPrintable( prefix + "Scope" ) ), "whole_file" );
            auto* pattern = control<QLineEdit>( dock, qPrintable( prefix + "Pattern" ) );
            click( pattern );
            // Value Counts supplies a default capture pattern; replace that draft.
            QTest::keyClick( pattern, Qt::Key_Home );
            QTest::keyClick( pattern, Qt::Key_End, Qt::ShiftModifier );
            QTest::keyClicks( pattern, "(?<value>\\d)" );
            INFO( "Pattern bytes: " << pattern->text().toUtf8().toHex().toStdString() );
            REQUIRE( pattern->text() == "(?<value>\\d)" );
            QString snapshotError;
            const auto expectedRequest = crawler->analysisRequest( "whole_file", &snapshotError );
            REQUIRE( expectedRequest );
            const auto& snapshot = expectedRequest->snapshot;
            expectedSource = { { "type", "file" },
                               { "sourceId", snapshot.sourceId.toString( QUuid::WithoutBraces ) },
                               { "generation", QString::number( snapshot.generation ) },
                               { "revision", QString::number( snapshot.revision ) },
                               { "indexedBytes", QString::number( snapshot.indexedBytes ) } };
            expectedRevision
                = state().value( "draft" ).toMap().value( "requestRevision" ).toString();
            REQUIRE_FALSE( expectedRevision.isEmpty() );
            auto* tabs = window.findChild<TabbedCrawlerWidget*>();
            REQUIRE( tabs );
            REQUIRE( tabs->count() == 1 );
            const auto tabId = tabs->tabIdAt( 0 );
            click( control<QPushButton>( dock, qPrintable( prefix + "Run" ) ) );
            REQUIRE( QTest::qWaitFor(
                [ & ] { return state().value( "job" ).toMap().value( "state" ) == "completed"; },
                15000 ) );
            const auto status = state().value( "job" ).toMap();
            REQUIRE( status.value( "complete" ).toBool() );
            source = status.value( "source" ).toMap();
            for ( auto it = expectedSource.cbegin(); it != expectedSource.cend(); ++it ) {
                INFO( it.key().toStdString() );
                REQUIRE( source.contains( it.key() ) );
                REQUIRE( source.value( it.key() ) == it.value() );
            }
            REQUIRE( status.contains( "requestRevision" ) );
            REQUIRE( status.value( "requestRevision" ).toString() == expectedRevision );
            const auto description = status.value( "sourceDescription" ).toMap();
            REQUIRE( description.value( "tabId" ) == tabId );
            REQUIRE( description.value( "windowId" ) == windowId );
            REQUIRE( QFileInfo( description.value( "filePath" ).toString() ).canonicalFilePath()
                     == QFileInfo( path ).canonicalFilePath() );
            REQUIRE( source.value( "type" ) == "file" );
            REQUIRE_FALSE( source.value( "sourceId" ).toString().isEmpty() );
            REQUIRE( source.value( "indexedBytes" ).toString()
                     == QString::number( sourceText.toUtf8().size() ) );
            const auto jobId = state().value( "jobId" ).toString();
            REQUIRE_FALSE( jobId.isEmpty() );
            REQUIRE( status.value( "jobId" ) == jobId );
            const auto raw = service->results( jobId ).value( "rows" ).toList();
            REQUIRE( raw.size() == ( counts ? 2 : 3 ) );
            for ( int index = 0; index < raw.size(); ++index ) {
                const auto record = raw[ index ].toMap();
                REQUIRE( record.contains( "source" ) );
                REQUIRE( record.contains( "jobId" ) );
                REQUIRE( record.contains( "requestRevision" ) );
                REQUIRE( record.value( "source" ).toMap() == source );
                REQUIRE( record.value( "jobId" ) == jobId );
                REQUIRE( record.value( "requestRevision" ) == status.value( "requestRevision" ) );
                if ( !counts )
                    REQUIRE( record.value( "text" ) == sourceText.split( '\n' )[ index ] );
            }
            if ( !counts ) {
                const auto first = raw.front().toMap();
                REQUIRE( first.value( "offsetConvention" ) == "zero-based UTF-16" );
                REQUIRE( first.value( "lineNumber" ).toInt() == 1 );
                const auto subpatterns = first.value( "subpatterns" ).toList();
                REQUIRE( subpatterns.size() == 1 );
                const auto matches = subpatterns.front().toMap().value( "matches" ).toList();
                REQUIRE( matches.size() == 1 );
                unicodeMatch = matches.front().toMap();
                REQUIRE( unicodeMatch.value( "startUtf16" ).toInt() == 7 );
                REQUIRE( unicodeMatch.value( "lengthUtf16" ).toInt() == 1 );
                const auto groups = unicodeMatch.value( "groups" ).toList();
                REQUIRE( groups.size() == 2 );
                for ( int number = 0; number < 2; ++number ) {
                    const auto group = groups[ number ].toMap();
                    REQUIRE( group.value( "number" ).toInt() == number );
                    REQUIRE( group.value( "name" ) == ( number ? QString( "value" ) : QString{} ) );
                    REQUIRE( group.value( "matched" ).toBool() );
                    REQUIRE( group.value( "value" ) == "1" );
                    REQUIRE( group.value( "startUtf16" ).toInt() == 7 );
                    REQUIRE( group.value( "lengthUtf16" ).toInt() == 1 );
                }
            }
            qInfo() << "G03e: source_verified job=" << jobId << " source=" << source;
            auto* table = control<QTableWidget>( dock, qPrintable( prefix + "Results" ) );
            REQUIRE( table->rowCount() == ( counts ? 2 : 3 ) );
            for ( int row = 0; row < table->rowCount(); ++row ) {
                if ( counts )
                    rows.append( QJsonArray{ table->item( row, 0 )->text(),
                                             table->item( row, 1 )->text() } );
                else
                    rows.append( table->item( row, 0 )->text() );
            }
            const int selected = counts ? 1 : 2;
            click( table->viewport(),
                   table->visualItemRect( table->item( selected, 0 ) ).center() );
            REQUIRE( table->currentRow() == selected );
            if ( counts )
                click( control<QPushButton>( dock, "valueCountsOpen" ) );
            else
                QTest::keyClick( table, Qt::Key_Return );
            auto* viewer = crawler->findChild<LogMainView*>();
            REQUIRE( viewer );
            REQUIRE( QTest::qWaitFor(
                [ & ] {
                    return viewer->getSelectedText()
                           == sourceText.split( '\n' )[ navigationLine - 1 ];
                },
                5000 ) );
            click( control<QPushButton>( dock, qPrintable( prefix + "Apply" ) ) );
            REQUIRE( crawler->searchText() == ( counts ? QString( "2" ) : pattern->text() ) );
            REQUIRE( QTest::qWaitFor( [ & ] { return !crawler->isSearchInProgress(); }, 10000 ) );
            qInfo( "G03e: navigation_verified id=%s physical_line=%d", qPrintable( name ),
                   navigationLine );
            PaintCounter counter;
            counter.target = &window;
            qApp->installEventFilter( &counter );
            views.insert( "docked", captureView( &window, counter, artifacts, name + "-docked" ) );
            dock->setFloating( true );
            fitWindow( dock );
            views.insert( "floating", captureView( dock, counter, artifacts, name + "-floating" ) );
            click( pattern );
            QTest::keyClick( pattern, Qt::Key_A, Qt::ControlModifier );
            QTest::keyClicks( pattern, "runtime-only-pattern" );
            REQUIRE( state().value( "draftChanged" ).toBool() );
            REQUIRE_FALSE(
                control<QPushButton>( dock, qPrintable( prefix + "Apply" ) )->isEnabled() );
            chooseData( control<QComboBox>( dock, qPrintable( prefix + "Scope" ) ), "text" );
            auto* text = control<QPlainTextEdit>( dock, qPrintable( prefix + "Text" ) );
            click( text->viewport() );
            QInputMethodEvent commit;
            commit.setCommitString( "runtime-only-secret" );
            QApplication::sendEvent( text, &commit );
            REQUIRE( text->toPlainText() == "runtime-only-secret" );
            CommanderRequest release;
            release.action
                = counts ? CommanderAction::SetValueCounts : CommanderAction::SetRegexLab;
            release.definitionPayload = { { "operation", "release" } };
            REQUIRE( window.executeCommanderRequest( release ).ok() );
            REQUIRE( state().value( "jobId" ).toString().isEmpty() );
            REQUIRE( service->status( jobId ).value( "reason" ) == "job_expired" );
            qApp->removeEventFilter( &counter );
            REQUIRE( window.close() );
        }
        const auto stored = SessionInfo::getSynced().dockState( windowId );
        REQUIRE_FALSE( stored.isEmpty() );
        REQUIRE_FALSE( stored.contains( "runtime-only-pattern" ) );
        REQUIRE_FALSE( stored.contains( "runtime-only-secret" ) );
        {
            MainWindow restored( WindowSession{ session, windowId, 0 } );
            restored.show();
            auto* dock = restored.findChild<QDockWidget*>( qPrintable(
                prefix == "regexLab" ? QString( "regexLabDock" ) : QString( "valueCountsDock" ) ) );
            REQUIRE( dock );
            REQUIRE( dock->isFloating() );
            REQUIRE( dock->isVisible() );
            REQUIRE( control<QLineEdit>( dock, qPrintable( prefix + "Pattern" ) )->text()
                     == factoryPattern );
            REQUIRE( control<QPlainTextEdit>( dock, qPrintable( prefix + "Text" ) )
                         ->toPlainText()
                         .isEmpty() );
            const auto state = counts ? qobject_cast<ValueCountDock*>( dock )->state()
                                      : qobject_cast<RegexLabDock*>( dock )->state();
            REQUIRE( state.value( "jobId" ).toString().isEmpty() );
            fitWindow( dock );
            PaintCounter counter;
            qApp->installEventFilter( &counter );
            views.insert( "restored", captureView( dock, counter, artifacts, name + "-restored" ) );
            qApp->removeEventFilter( &counter );
            REQUIRE( restored.close() );
        }
        cases.append(
            QJsonObject{ { "id", name },
                         { "views", views },
                         { "rows", rows },
                         { "source", QJsonObject::fromVariantMap( source ) },
                         { "expectedSource", QJsonObject::fromVariantMap( expectedSource ) },
                         { "requestRevision", expectedRevision },
                         { "unicodeMatch", QJsonObject::fromVariantMap( unicodeMatch ) },
                         { "navigationLine", navigationLine },
                         { "patternKeys", true },
                         { "textInputMethod", true },
                         { "runClicked", true },
                         { "resultClicked", true },
                         { "applyClicked", true },
                         { "draftInvalidatesApply", true },
                         { "complete", true },
                         { "menuClicked", true },
                         { "toolbarClicked", true },
                         { "navigationVerified", true },
                         { "layoutRestored", true },
                         { "draftsRuntimeOnly", true } } );
        qInfo( "G03e: mainwindow_case_completed id=%s layout_restored=true jobs_released=true",
               qPrintable( name ) );
    }
    const QJsonObject report{
        { "schemaVersion", 1 },
        { "probeKind", "mainwindow" },
        { "platform", platform },
        { "qt", QJsonObject{ { "compile", QT_VERSION_STR }, { "runtime", qVersion() } } },
        { "workerBinarySha256", QString::fromLatin1( workerHash.result().toHex() ) },
        { "sourceFixtureSha256",
          QString::fromLatin1(
              QCryptographicHash::hash( sourceText.toUtf8(), QCryptographicHash::Sha256 )
                  .toHex() ) },
        { "ownedJobsReleased", true },
        { "cases", cases }
    };
    QSaveFile output( reportPath );
    REQUIRE( output.open( QIODevice::WriteOnly ) );
    const auto bytes = QJsonDocument( report ).toJson();
    REQUIRE( output.write( bytes ) == bytes.size() );
    REQUIRE( output.commit() );
}
} // namespace

// Separate native opt-in: never joins the ordinary offscreen CTest selection.
TEST_CASE( "Native MainWindow discovers analysis tools and restores runtime-safe layouts",
           "[.][workbench-mainwindow][G03]" )
{
    exerciseMainWindow();
}
#endif
