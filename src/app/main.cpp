/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2021 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "log.h"
#include <QtGlobal>
#include <qapplication.h>
#include <qthreadpool.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QFont>
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSize>
#include <QSplashScreen>
#include <QThread>
#include <algorithm>
#include <cstdio>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // _WIN32

#include <mimalloc.h>
#include <roaring.hh>

#ifdef KLOGG_HAS_HS
#include <hs.h>
#endif

#include "tbb/global_control.h"

#include "configuration.h"
#include "logger.h"
#include "mainwindow.h"
#include "styles.h"
#include "startupprogress.h"

#include "cli.h"
#include "kloggapp.h"
#include "labagentrunner.h"
#include "labbundleutils.h"
#include "labclient.h"
#include "labcontrollerservice.h"
#include "scenarioheadlessrunner.h"

#ifdef KLOGG_PORTABLE
const bool PersistentInfo::ForcePortable = true;
#else
const bool PersistentInfo::ForcePortable = false;
#endif

void setApplicationAttributes( bool enableQtHdpi, int scaleFactorRounding )
{
    // When QNetworkAccessManager is instantiated it regularly starts polling
    // all network interfaces to see if anything changes and if so, what. This
    // creates a latency spike every 10 seconds on Mac OS 10.12+ and Windows 7 >=
    // when on a wifi connection.
    // So here we disable it for lack of better measure.
    // This will also cause this message: QObject::startTimer: Timers cannot
    // have negative intervals
    // For more info see:
    // - https://bugreports.qt.io/browse/QTBUG-40332
    // - https://bugreports.qt.io/browse/QTBUG-46015
    qputenv( "QT_BEARER_POLL_TIMEOUT", QByteArray::number( std::numeric_limits<int>::max() ) );

#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
#ifdef Q_OS_WIN
    QCoreApplication::setAttribute( Qt::AA_DisableWindowContextHelpButton );
#endif

    if ( !enableQtHdpi ) {
        QCoreApplication::setAttribute( Qt::AA_DisableHighDpiScaling );
    }
    else {

#if QT_VERSION >= QT_VERSION_CHECK( 5, 14, 0 )
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
            static_cast<Qt::HighDpiScaleFactorRoundingPolicy>( scaleFactorRounding ) );
#else
        Q_UNUSED( scaleFactorRounding );
#endif

        // This attribute must be set before QGuiApplication is constructed:
        QCoreApplication::setAttribute( Qt::AA_EnableHighDpiScaling );
        // We support high-dpi (aka Retina) displays
        QCoreApplication::setAttribute( Qt::AA_UseHighDpiPixmaps );
    }
#else
    Q_UNUSED( enableQtHdpi );
    Q_UNUSED( scaleFactorRounding );
#endif

    QCoreApplication::setAttribute( Qt::AA_DontShowIconsInMenus );
}

class StartupSplashScreen final : public QSplashScreen {
  public:
    explicit StartupSplashScreen( const QPixmap& pixmap )
        : QSplashScreen( pixmap )
    {
        // Keep splash above restored main windows until startup is fully finished.
        setWindowFlag( Qt::WindowStaysOnTopHint, true );
    }

    void updateFromState( const StartupProgressState& state )
    {
        state_ = state;
        state_.maximum = std::max( state_.minimum + 1, state_.maximum );
        state_.value = std::clamp( state_.value, state_.minimum, state_.maximum );
        update();
        raise();

        // Startup can run before the main event loop starts; pump a minimal
        // event set so queued splash updates are rendered.
        if ( !processingEvents_ ) {
            QScopedValueRollback<bool> guard( processingEvents_, true );
            // Keep local IPC responsive during bootstrap so automation and
            // commander requests can reach the primary instance before the
            // main event loop starts.
            QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
        }
    }

  protected:
    void drawContents( QPainter* painter ) override
    {
        const auto rect = this->rect();
        const int panelHeight = 82;
        const QRect panelRect( rect.left(), rect.bottom() - panelHeight + 1, rect.width(),
                               panelHeight );
        painter->fillRect( panelRect, QColor( 0, 0, 0, 150 ) );

        QFont statusFont = painter->font();
        statusFont.setPixelSize( 14 );
        statusFont.setBold( true );
        painter->setFont( statusFont );
        painter->setPen( Qt::white );
        painter->drawText( panelRect.adjusted( 12, 8, -12, -44 ),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           state_.status.isEmpty() ? QObject::tr( "Loading..." ) : state_.status );

        QFont detailFont = painter->font();
        detailFont.setPixelSize( 12 );
        detailFont.setBold( false );
        painter->setFont( detailFont );
        painter->setPen( QColor( 230, 230, 230 ) );
        painter->drawText( panelRect.adjusted( 12, 28, -12, -24 ),
                           Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                           state_.detail );

        const QRect progressRect( panelRect.left() + 12, panelRect.bottom() - 20,
                                  panelRect.width() - 24, 10 );
        painter->setPen( QColor( 80, 80, 80 ) );
        painter->setBrush( QColor( 35, 35, 35 ) );
        painter->drawRect( progressRect );

        const int range = std::max( 1, state_.maximum - state_.minimum );
        const double ratio = static_cast<double>( state_.value - state_.minimum ) / range;
        const int width = std::max( 0, static_cast<int>( ( progressRect.width() - 2 ) * ratio ) );
        const QRect valueRect( progressRect.left() + 1, progressRect.top() + 1, width,
                               progressRect.height() - 2 );
        painter->fillRect( valueRect, QColor( 38, 140, 255 ) );
    }

  private:
    StartupProgressState state_;
    bool processingEvents_ = false;
};

namespace {
void ensureCliConsoleAttached()
{
#ifdef Q_OS_WIN
    const auto hasUsableStdHandle = []( DWORD handleId ) {
        const HANDLE handle = GetStdHandle( handleId );
        if ( handle == nullptr || handle == INVALID_HANDLE_VALUE ) {
            return false;
        }

        return GetFileType( handle ) != FILE_TYPE_UNKNOWN;
    };

    static bool consoleInitialized = false;
    if ( !consoleInitialized ) {
        consoleInitialized = true;
        if ( hasUsableStdHandle( STD_OUTPUT_HANDLE ) || hasUsableStdHandle( STD_ERROR_HANDLE ) ) {
            return;
        }

        if ( GetConsoleWindow() == nullptr && AttachConsole( ATTACH_PARENT_PROCESS ) ) {
            FILE* stream = nullptr;
            freopen_s( &stream, "CONOUT$", "w", stdout );
            freopen_s( &stream, "CONOUT$", "w", stderr );
            freopen_s( &stream, "CONIN$", "r", stdin );
        }
    }
#endif
}

void writeCliMessage( const QString& message, bool toStderr = false )
{
    if ( message.isEmpty() ) {
        return;
    }

    ensureCliConsoleAttached();

    auto output = message;
    if ( !output.endsWith( '\n' ) ) {
        output += '\n';
    }

    const auto bytes = output.toLocal8Bit();
    auto* stream = toStderr ? stderr : stdout;
    std::fwrite( bytes.constData(), 1, static_cast<size_t>( bytes.size() ), stream );
    std::fflush( stream );
#ifdef Q_OS_WIN
    if ( !toStderr ) {
        std::fwrite( bytes.constData(), 1, static_cast<size_t>( bytes.size() ), stderr );
        std::fflush( stderr );
    }
#endif
}

void writeCliBytes( const QByteArray& bytes, bool toStderr = false, bool mirrorStdoutToStderr = false )
{
    Q_UNUSED( mirrorStdoutToStderr );

    if ( bytes.isEmpty() ) {
        return;
    }

    ensureCliConsoleAttached();

#ifdef Q_OS_WIN
    const auto writeToHandle = [ &bytes ]( DWORD stdHandleId ) {
        const HANDLE handle = GetStdHandle( stdHandleId );
        if ( handle == nullptr || handle == INVALID_HANDLE_VALUE ) {
            return false;
        }

        DWORD bytesWritten = 0;
        return WriteFile( handle, bytes.constData(), static_cast<DWORD>( bytes.size() ), &bytesWritten,
                          nullptr )
               != FALSE;
    };

    const bool wroteToPrimaryHandle = writeToHandle( toStderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE );
    if ( mirrorStdoutToStderr && !toStderr ) {
        writeToHandle( STD_ERROR_HANDLE );
    }
    if ( wroteToPrimaryHandle ) {
        return;
    }
#endif

    auto* stream = toStderr ? stderr : stdout;
    std::fwrite( bytes.constData(), 1, static_cast<size_t>( bytes.size() ), stream );
    std::fflush( stream );
#ifdef Q_OS_WIN
    if ( mirrorStdoutToStderr && !toStderr ) {
        std::fwrite( bytes.constData(), 1, static_cast<size_t>( bytes.size() ), stderr );
        std::fflush( stderr );
    }
#endif
}

QByteArray jsonIndent( int depth )
{
    return QByteArray( depth * 2, ' ' );
}

QByteArray jsonString( const QString& value )
{
    QByteArray result;
    result.reserve( value.size() + 2 );
    result.append( '"' );

    for ( const auto ch : value ) {
        switch ( ch.unicode() ) {
        case '\"':
            result.append( "\\\"" );
            break;
        case '\\':
            result.append( "\\\\" );
            break;
        case '\b':
            result.append( "\\b" );
            break;
        case '\f':
            result.append( "\\f" );
            break;
        case '\n':
            result.append( "\\n" );
            break;
        case '\r':
            result.append( "\\r" );
            break;
        case '\t':
            result.append( "\\t" );
            break;
        default:
            if ( ch.unicode() < 0x20 ) {
                result.append( QStringLiteral( "\\u%1" )
                                   .arg( static_cast<unsigned int>( ch.unicode() ), 4, 16,
                                         QLatin1Char( '0' ) )
                                   .toLatin1() );
            }
            else {
                result.append( QString( ch ).toUtf8() );
            }
            break;
        }
    }

    result.append( '"' );
    return result;
}

void appendJsonValue( QByteArray& output, const QVariant& value, bool pretty, int depth );

void appendJsonObject( QByteArray& output, const QVariantMap& object, bool pretty, int depth )
{
    output.append( '{' );
    if ( object.isEmpty() ) {
        output.append( '}' );
        return;
    }

    bool first = true;
    for ( auto it = object.cbegin(); it != object.cend(); ++it ) {
        if ( first ) {
            first = false;
        }
        else {
            output.append( ',' );
        }

        if ( pretty ) {
            output.append( '\n' );
            output.append( jsonIndent( depth + 1 ) );
        }

        output.append( jsonString( it.key() ) );
        output.append( pretty ? ": " : ":" );
        appendJsonValue( output, it.value(), pretty, depth + 1 );
    }

    if ( pretty ) {
        output.append( '\n' );
        output.append( jsonIndent( depth ) );
    }
    output.append( '}' );
}

void appendJsonArray( QByteArray& output, const QVariantList& array, bool pretty, int depth )
{
    output.append( '[' );
    if ( array.isEmpty() ) {
        output.append( ']' );
        return;
    }

    for ( qsizetype i = 0; i < array.size(); ++i ) {
        if ( i > 0 ) {
            output.append( ',' );
        }

        if ( pretty ) {
            output.append( '\n' );
            output.append( jsonIndent( depth + 1 ) );
        }

        appendJsonValue( output, array.at( i ), pretty, depth + 1 );
    }

    if ( pretty ) {
        output.append( '\n' );
        output.append( jsonIndent( depth ) );
    }
    output.append( ']' );
}

void appendJsonValue( QByteArray& output, const QVariant& value, bool pretty, int depth )
{
    if ( !value.isValid() || value.isNull() ) {
        output.append( "null" );
        return;
    }

    switch ( value.typeId() ) {
    case QMetaType::Bool:
        output.append( value.toBool() ? "true" : "false" );
        return;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        output.append( QByteArray::number( value.toLongLong() ) );
        return;
    case QMetaType::Float:
    case QMetaType::Double:
        output.append( QByteArray::number( value.toDouble(), 'g', 16 ) );
        return;
    case QMetaType::QString:
        output.append( jsonString( value.toString() ) );
        return;
    case QMetaType::QVariantList:
        appendJsonArray( output, value.toList(), pretty, depth );
        return;
    case QMetaType::QVariantMap:
        appendJsonObject( output, value.toMap(), pretty, depth );
        return;
    default:
        output.append( jsonString( value.toString() ) );
        return;
    }
}

void writeCliPayload( const QVariantMap& payload, bool pretty = false )
{
    if ( payload.isEmpty() ) {
        return;
    }

    QByteArray bytes;
    appendJsonObject( bytes, payload, pretty, 0 );
    bytes.append( '\n' );
    writeCliBytes( bytes, false, true );
}

bool writeCliPayloadToFile( const QString& path, const QVariantMap& payload, bool pretty = false )
{
    if ( path.isEmpty() ) {
        return false;
    }

    QByteArray bytes;
    appendJsonObject( bytes, payload, pretty, 0 );
    bytes.append( '\n' );

    QSaveFile outputFile{ path };
    if ( !outputFile.open( QIODevice::WriteOnly ) ) {
        return false;
    }

    const auto written = outputFile.write( bytes );
    if ( written != bytes.size() ) {
        outputFile.cancelWriting();
        return false;
    }

    return outputFile.commit();
}

bool writeLabArtifacts( const QVariantMap& payload, const QString& outputDirPath, QString* errorMessage )
{
    QDir().mkpath( outputDirPath );
    for ( const auto& artifactValue : payload.value( QStringLiteral( "artifacts" ) ).toList() ) {
        const auto artifact = artifactValue.toMap();
        const auto name = artifact.value( QStringLiteral( "name" ) ).toString();
        if ( name.isEmpty() ) {
            continue;
        }

        QFile file( QDir( outputDirPath ).filePath( name ) );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
            if ( errorMessage != nullptr ) {
                *errorMessage = QObject::tr( "Failed to write artifact %1: %2" )
                                    .arg( file.fileName(), file.errorString() );
            }
            return false;
        }
        file.write( QByteArray::fromBase64(
            artifact.value( QStringLiteral( "dataBase64" ) ).toByteArray(),
            QByteArray::Base64Encoding ) );
    }
    return true;
}
} // namespace

int main( int argc, char* argv[] )
{
#ifdef KLOGG_USE_MIMALLOC
    mi_process_init();
#endif

    const auto& config = Configuration::getSynced();
    setApplicationAttributes( config.enableQtHighDpi(), config.scaleFactorRounding() );

    KloggApp app( argc, argv );


    MainWindow::installLanguage( config.language() );
    CliParameters parameters( app );
    if ( parameters.exit_requested ) {
        writeCliMessage( parameters.exit_message, parameters.exit_code != EXIT_SUCCESS );
        return parameters.exit_code;
    }
    if ( parameters.parse_error ) {
        writeCliMessage( parameters.parse_error_message, true );
        return EXIT_FAILURE;
    }

    if ( parameters.lab_request ) {
        StyleManager::applyStyle( config.style() );

        if ( parameters.lab_request->mode == LabCliMode::ControllerServe ) {
            LabControllerService controller;
            QString errorMessage;
            if ( !controller.start( *parameters.lab_request, &errorMessage ) ) {
                writeCliMessage( errorMessage, true );
                return EXIT_FAILURE;
            }
            writeCliMessage(
                QObject::tr( "Lab controller listening on %1:%2" )
                    .arg( parameters.lab_request->listenAddress )
                    .arg( parameters.lab_request->listenPort ) );
            return app.exec();
        }

        if ( parameters.lab_request->mode == LabCliMode::AgentRun ) {
            LabAgentRunner agent( app );
            QString errorMessage;
            if ( !agent.start( *parameters.lab_request, &errorMessage ) ) {
                writeCliMessage( errorMessage, true );
                return EXIT_FAILURE;
            }
            writeCliMessage(
                QObject::tr( "Lab agent %1 connected to %2" )
                    .arg( QFileInfo( parameters.lab_request->agentConfigPath ).completeBaseName(),
                          parameters.lab_request->controllerUrl ) );
            return app.exec();
        }

        LabClient client;
        QString errorMessage;
        if ( !client.loadToken( parameters.lab_request->tokenFilePath, &errorMessage ) ) {
            writeCliMessage( errorMessage, true );
            return EXIT_FAILURE;
        }

        QVariantMap payload;
        switch ( parameters.lab_request->mode ) {
        case LabCliMode::Submit: {
            const auto bundle = loadLabJobBundle( *parameters.lab_request, &errorMessage );
            if ( !bundle ) {
                writeCliMessage( errorMessage, true );
                return 2;
            }
            payload = client.submit( parameters.lab_request->controllerUrl, *bundle, &errorMessage );
            break;
        }
        case LabCliMode::Queue:
            payload = client.queue( parameters.lab_request->controllerUrl, &errorMessage );
            break;
        case LabCliMode::Status:
            payload = client.status( parameters.lab_request->controllerUrl, parameters.lab_request->jobId,
                                     &errorMessage );
            break;
        case LabCliMode::Cancel:
            payload = client.cancel( parameters.lab_request->controllerUrl, parameters.lab_request->jobId,
                                     &errorMessage );
            break;
        case LabCliMode::Agents:
            payload = client.agents( parameters.lab_request->controllerUrl, &errorMessage );
            break;
        case LabCliMode::Artifacts:
            payload = client.artifacts( parameters.lab_request->controllerUrl,
                                        parameters.lab_request->jobId, &errorMessage );
            if ( errorMessage.isEmpty()
                 && !writeLabArtifacts( payload, parameters.lab_request->outputDirPath, &errorMessage ) ) {
                payload.clear();
            }
            else if ( errorMessage.isEmpty() ) {
                payload.insert( QStringLiteral( "outputDir" ),
                                QFileInfo( parameters.lab_request->outputDirPath ).absoluteFilePath() );
            }
            break;
        case LabCliMode::None:
        case LabCliMode::ControllerServe:
        case LabCliMode::AgentRun:
            break;
        }

        if ( payload.isEmpty() && !errorMessage.isEmpty() ) {
            writeCliMessage( errorMessage, true );
            return 2;
        }

        writeCliPayload( payload, parameters.lab_request->prettyOutput );
        return EXIT_SUCCESS;
    }

    const bool automationDumpRequested
        = parameters.dump_ui_tree || !parameters.dump_state_json_path.isEmpty();
    const bool automationMode
        = automationDumpRequested || qEnvironmentVariableIntValue( "KLOGG_AUTOMATION" ) > 0;
    const QSize automationWindowSize
        = ( parameters.window_width > 0 && parameters.window_height > 0 )
              ? QSize( parameters.window_width, parameters.window_height )
              : QSize( 1600, 1000 );
    app.setAutomationMode( automationMode, QPoint( 40, 40 ), automationWindowSize );

    const auto logLevel
        = static_cast<logging::LogLevel>( std::max( parameters.log_level, config.loggingLevel() ) );
    logging::enableLogging( parameters.enable_logging || config.enableLogging(), logLevel );
    logging::enableFileLogging( parameters.log_to_file || config.enableLogging(), logLevel );

    app.initCrashHandler();

    if ( parameters.scenario_batch_request ) {
        StyleManager::applyStyle( config.style() );

        ScenarioHeadlessRunner runner( app );
        const auto batchResult = runner.run( *parameters.scenario_batch_request );
        if ( batchResult.hasPayload() ) {
            writeCliPayload( batchResult.payload, true );
        }
        else {
            writeCliMessage( batchResult.message, batchResult.outputToStderr );
        }
        return batchResult.exitCode;
    }

    auto maxConcurrency
        = tbb::global_control::active_value( tbb::global_control::max_allowed_parallelism );

    LOG_INFO << "Klogg instance"
             << ", mimalloc v" << mi_version()
             << ", default concurrency " << maxConcurrency;


    roaring_memory_t roaring_memory_allocators;
    roaring_memory_allocators.malloc = mi_malloc;
    roaring_memory_allocators.realloc = mi_realloc;
    roaring_memory_allocators.calloc = mi_calloc;
    roaring_memory_allocators.free = mi_free;
    roaring_memory_allocators.aligned_malloc = mi_aligned_alloc;
    roaring_memory_allocators.aligned_free = mi_free;
    roaring_init_memory_hook(roaring_memory_allocators);

#ifdef KLOGG_HAS_HS
    hs_set_allocator(mi_malloc, mi_free);
#endif

    if ( maxConcurrency < 2 ) {
        maxConcurrency = 2;
        LOG_INFO << "Overriding default concurrency to " << maxConcurrency;
        tbb::global_control concurrencyControl( tbb::global_control::max_allowed_parallelism,
                                                maxConcurrency );
        QThreadPool::globalInstance()->setMaxThreadCount( static_cast<int>( maxConcurrency ) );
    }

    if ( !parameters.multi_instance && app.isSecondary() ) {
        LOG_INFO << "Found another klogg, pid " << app.primaryPid();
        if ( parameters.commander_request ) {
            const auto result = app.sendCommandToPrimaryInstance( *parameters.commander_request );
            if ( result.ok() ) {
                writeCliPayload( result.payload, parameters.commander_request->prettyOutput );
                return EXIT_SUCCESS;
            }

            writeCliMessage( result.message, true );
            return EXIT_FAILURE;
        }

        if ( !parameters.dump_state_json_path.isEmpty() ) {
            CommanderRequest request;
            request.action = CommanderAction::DumpState;

            const auto result = app.sendCommandToPrimaryInstance( request );
            if ( result.ok() ) {
                if ( !writeCliPayloadToFile( parameters.dump_state_json_path, result.payload, true ) ) {
                    writeCliMessage( QObject::tr( "Failed to write automation state to %1." )
                                         .arg( parameters.dump_state_json_path ),
                                     true );
                    return EXIT_FAILURE;
                }
                return EXIT_SUCCESS;
            }

            if ( result.code != CommanderResultCode::TransportError ) {
                writeCliMessage( result.message, true );
                return EXIT_FAILURE;
            }

            LOG_WARNING << "Failed to contact primary instance for automation state dump, starting a new window";
        }

        if ( app.sendFilesToPrimaryInstance( parameters.filenames ) ) {
            return EXIT_SUCCESS;
        }
        LOG_WARNING << "Failed to contact primary instance, starting a new window";
    }

    StyleManager::applyStyle( config.style() );

    QPixmap splashPixmap( QStringLiteral( ":/images/splash.png" ) );
    if ( splashPixmap.isNull() ) {
        splashPixmap = QPixmap( 850, 320 );
        splashPixmap.fill( QColor( 30, 30, 30 ) );
    }
    StartupSplashScreen splash( splashPixmap );
    splash.show();
    app.setStartupBootstrapGeometry( splash.frameGeometry().topLeft() );
    StartupProgress::setCallback( [ &splash ]( const StartupProgressState& state ) {
        if ( QThread::currentThread() == splash.thread() ) {
            splash.updateFromState( state );
            return;
        }

        QMetaObject::invokeMethod(
            &splash, [ &splash, state ]() { splash.updateFromState( state ); },
            Qt::QueuedConnection );
    } );
    StartupProgress::setRange( 0, 100 );
    StartupProgress::setValue( 1, QObject::tr( "Starting klogg" ),
                               QObject::tr( "Preparing application state" ) );

    if ( parameters.commander_request
         && !isCommanderOpenAction( parameters.commander_request->action ) ) {
        writeCliMessage( QObject::tr( "No running klogg instance." ), true );
        return EXIT_FAILURE;
    }

    auto startNewSession = true;
    MainWindow* mw = nullptr;
    if ( parameters.commander_request ) {
        mw = app.newWindow();
        mw->show();
    }
    else if ( !automationMode
         && ( parameters.load_session
              || ( parameters.filenames.empty() && !parameters.new_session
                   && config.loadLastSession() ) ) ) {
        mw = app.reloadSession();
        startNewSession = false;
    }
    else {
        mw = app.newWindow();
        mw->show();
    }

    if ( parameters.commander_request ) {
        const auto result = app.executeCommanderRequest( *parameters.commander_request );
        if ( !result.ok() ) {
            writeCliMessage( result.message, true );
            return EXIT_FAILURE;
        }

        writeCliPayload( result.payload, parameters.commander_request->prettyOutput );
    }
    else if ( !automationDumpRequested ) {
        for ( const auto& filename : parameters.filenames ) {
            StartupProgress::advance( QObject::tr( "Opening startup file" ),
                                      QFileInfo( filename ).fileName() );
            mw->loadInitialFile( filename, parameters.follow_file );
        }
    }

    app.ensureMainWindowVisible();
    app.setStartupCommanderReady();
    StartupProgress::advance( QObject::tr( "Finalizing startup" ) );

    if ( startNewSession ) {
        app.clearInactiveSessions();
    }

    StartupProgress::complete( QObject::tr( "Ready" ), QObject::tr( "Application started" ) );
    StartupProgress::clearCallback();
    splash.finish( mw );
    app.finalizeStartupBootstrapGeometry();

    if ( !automationMode && parameters.window_width > 0 && parameters.window_height > 0 ) {
        mw->resize( parameters.window_width, parameters.window_height );
    }

    if ( parameters.dump_ui_tree ) {
        for ( auto attempt = 0; attempt < 5; ++attempt ) {
            QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
        }
        writeCliPayload( mw->automationUiTree(), true );
        return EXIT_SUCCESS;
    }

    if ( !parameters.dump_state_json_path.isEmpty() ) {
        for ( auto attempt = 0; attempt < 5; ++attempt ) {
            QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
        }

        if ( !writeCliPayloadToFile( parameters.dump_state_json_path, mw->automationSnapshot(),
                                     true ) ) {
            writeCliMessage( QObject::tr( "Failed to write automation state to %1." )
                                 .arg( parameters.dump_state_json_path ),
                             true );
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    app.startBackgroundTasks();

    return app.exec();
}
