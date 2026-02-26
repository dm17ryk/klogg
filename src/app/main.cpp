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
#include <QSplashScreen>
#include <QThread>
#include <algorithm>

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
    }

    void updateFromState( const StartupProgressState& state )
    {
        state_ = state;
        state_.maximum = std::max( state_.minimum + 1, state_.maximum );
        state_.value = std::clamp( state_.value, state_.minimum, state_.maximum );
        repaint();
        QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
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
};

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

    const auto logLevel
        = static_cast<logging::LogLevel>( std::max( parameters.log_level, config.loggingLevel() ) );
    logging::enableLogging( parameters.enable_logging || config.enableLogging(), logLevel );
    logging::enableFileLogging( parameters.log_to_file || config.enableLogging(), logLevel );

    app.initCrashHandler();

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

    auto startNewSession = true;
    MainWindow* mw = nullptr;
    if ( parameters.load_session
         || ( parameters.filenames.empty() && !parameters.new_session && config.loadLastSession() ) ) {
        mw = app.reloadSession();
        startNewSession = false;
    }
    else {
        mw = app.newWindow();
        mw->reloadGeometry();
        mw->show();
    }

    if ( parameters.window_width > 0 && parameters.window_height > 0 ) {
        mw->resize( parameters.window_width, parameters.window_height );
    }

    for ( const auto& filename : parameters.filenames ) {
        StartupProgress::advance( QObject::tr( "Opening startup file" ),
                                  QFileInfo( filename ).fileName() );
        mw->loadInitialFile( filename, parameters.follow_file );
    }

    app.ensureMainWindowVisible();
    StartupProgress::advance( QObject::tr( "Finalizing startup" ) );

    if ( startNewSession ) {
        app.clearInactiveSessions();
    }

    StartupProgress::setValue( 100, QObject::tr( "Ready" ), QObject::tr( "Application started" ) );
    StartupProgress::clearCallback();
    splash.finish( mw );

    app.startBackgroundTasks();

    return app.exec();
}
