/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#ifndef KLOGG_KLOGGAPP_H
#define KLOGG_KLOGGAPP_H

#include <algorithm>
#include <exception>
#include <cstddef>
#include <iterator>
#include <numeric>
#include <qapplication.h>
#include <stack>

#include <QApplication>
#include <vector>

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMessageBox>
#include <QNetworkProxyFactory>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryFile>
#include <QTimer>
#include <QUuid>

#ifdef Q_OS_MAC
#include <QFileOpenEvent>
#endif

#include "configuration.h"
#include "crashhandler.h"
#include "klogg_version.h"
#include "log.h"
#include "session.h"
#include "uuid.h"

#include <kdsingleapplication.h>

#include "mainwindow.h"
#include "messagereceiver.h"
#include "versionchecker.h"

class KloggApp : public QApplication {

    Q_OBJECT

  public:
    KloggApp( int& argc, char* argv[] )
        : QApplication( argc, argv)
    {
        QFontDatabase::addApplicationFont( ":/fonts/DejaVuSansMono.ttf" );

        QNetworkProxyFactory::setUseSystemConfiguration( true );

        qRegisterMetaType<LoadingStatus>( "LoadingStatus" );
        qRegisterMetaType<LinesCount>( "LinesCount" );
        qRegisterMetaType<LineNumber>( "LineNumber" );
        qRegisterMetaType<std::vector<LineNumber>>( "std::vector<LineNumber>" );
        qRegisterMetaType<klogg::vector<LineNumber>>( "klogg::vector<LineNumber>" );
        qRegisterMetaType<LineLength>( "LineLength" );
        qRegisterMetaType<Portion>( "Portion" );
        qRegisterMetaType<Selection>( "Selection" );
        qRegisterMetaType<QFNotification>( "QFNotification" );
        qRegisterMetaType<QFNotificationReachedEndOfFile>( "QFNotificationReachedEndOfFile" );
        qRegisterMetaType<QFNotificationReachedBegininningOfFile>(
            "QFNotificationReachedBegininningOfFile" );
        qRegisterMetaType<QFNotificationProgress>( "QFNotificationProgress" );
        qRegisterMetaType<QFNotificationInterrupted>( "QFNotificationInterrupted" );
        qRegisterMetaType<QuickFindMatcher>( "QuickFindMatcher" );

        if ( singleApplication_.isPrimaryInstance() ) {
            QObject::connect( &singleApplication_, &KDSingleApplication::messageReceived, &messageReceiver_,
                              &MessageReceiver::receiveMessage, Qt::QueuedConnection );

            QObject::connect( &messageReceiver_, &MessageReceiver::loadFile, this,
                              &KloggApp::loadFileNonInteractive );
            QObject::connect( &messageReceiver_, &MessageReceiver::activateWindow, this,
                              &KloggApp::activatePrimaryWindow );

            // Version checker notification
            connect( &versionChecker_, &VersionChecker::newVersionFound,
                     [ this ]( const QString& new_version, const QString& url,
                               const QStringList& changes ) {
                         newVersionNotification( new_version, url, changes );
                     } );
        }
    }

    bool isSecondary() const {
        return !singleApplication_.isPrimaryInstance();
    }

    qint64 primaryPid() const {
        return singleApplication_.primaryPid();
    }

    bool sendFilesToPrimaryInstance( const std::vector<QString>& filenames )
    {
#ifdef Q_OS_WIN
        // TODO: fix pid passing
        ::AllowSetForegroundWindow( static_cast<DWORD>( primaryPid() ) );
#endif

        QStringList filesToOpen;
        std::copy( filenames.cbegin(), filenames.cend(), std::back_inserter( filesToOpen ) );

        QString ackPath;
        if ( filesToOpen.empty() ) {
            QTemporaryFile ackFile( QDir::temp().filePath( "klogg_activate_ack_XXXXXX.tmp" ) );
            ackFile.setAutoRemove( false );
            if ( ackFile.open() ) {
                ackPath = ackFile.fileName();
                ackFile.close();
                QFile::remove( ackPath );
            }
        }

        QCborArray filesArray;
        for ( const auto& file : filesToOpen ) {
            filesArray.append( file );
        }

        QCborMap data;
        data.insert( QLatin1String( "version" ), QString::fromLatin1( kloggVersion() ) );
        data.insert( QLatin1String( "activate" ), true );
        data.insert( QLatin1String( "files" ), filesArray );
        if ( !ackPath.isEmpty() ) {
            data.insert( QLatin1String( "ackPath" ), ackPath );
        }

        const QCborValue cbor( data );
        if ( !singleApplication_.sendMessageWithTimeout( cbor.toCbor(), 5000 ) ) {
            return false;
        }

        if ( ackPath.isEmpty() ) {
            return true;
        }

        constexpr int AckTimeoutMs = 1200;
        constexpr int AckPollMs = 20;

        QElapsedTimer ackWaitTimer;
        ackWaitTimer.start();

        bool ackReceived = false;
        QEventLoop ackWaitLoop;
        QTimer ackPollTimer;
        QTimer ackTimeoutTimer;
        ackPollTimer.setInterval( AckPollMs );
        ackPollTimer.setSingleShot( false );
        ackTimeoutTimer.setSingleShot( true );

        connect( &ackPollTimer, &QTimer::timeout, &ackWaitLoop, [ & ]() {
            if ( QFileInfo::exists( ackPath ) ) {
                ackReceived = true;
                ackWaitLoop.quit();
            }
        } );
        connect( &ackTimeoutTimer, &QTimer::timeout, &ackWaitLoop, &QEventLoop::quit );

        ackPollTimer.start();
        ackTimeoutTimer.start( AckTimeoutMs );
        ackWaitLoop.exec( QEventLoop::ProcessEventsFlag::ExcludeUserInputEvents );

        if ( !ackReceived && ackWaitTimer.elapsed() < AckTimeoutMs && QFileInfo::exists( ackPath ) ) {
            ackReceived = true;
        }

        QFile::remove( ackPath );
        return ackReceived;
    }
    void initCrashHandler()
    {
        crashHandler_ = std::make_unique<CrashHandler>();
    }

    MainWindow* reloadSession()
    {
        if ( !session_ ) {
            session_ = std::make_shared<Session>();
        }

        MainWindow* lastShownWindow = nullptr;

        for ( auto&& windowSession : session_->windowSessions() ) {
            try {
                auto* window = newWindow( std::move( windowSession ) );
                window->reloadGeometry();
                window->show();
                lastShownWindow = window;

                // Keep startup responsive and avoid ending up with no visible
                // window when session restore data is malformed.
                window->reloadSession();
            } catch ( const std::exception& e ) {
                LOG_ERROR << "Failed to restore window session: " << e.what();
            } catch ( ... ) {
                LOG_ERROR << "Failed to restore window session: unknown exception";
            }
        }

        if ( lastShownWindow == nullptr ) {
            lastShownWindow = newWindow();
            lastShownWindow->show();
        }

        return lastShownWindow;
    }

    void clearInactiveSessions()
    {
        LOG_INFO << "Clear inactive sessions";

        auto existingSessions = session_->windowSessions();
        existingSessions.erase( std::remove_if( existingSessions.begin(), existingSessions.end(),
                                                [ this ]( const auto& session ) {
                                                    return std::any_of(
                                                        mainWindows_.begin(), mainWindows_.end(),
                                                        [ &session ]( const auto& window ) {
                                                            return window.first.windowId()
                                                                   == session.windowId();
                                                        } );
                                                } ),
                                existingSessions.end() );

        for ( auto& session : existingSessions ) {
            session.close();
        }
    }

    MainWindow* newWindow()
    {
        if ( !session_ ) {
            session_ = std::make_shared<Session>();
        }

        const auto previousSessions = session_->windowSessions();

        QByteArray geometry;
        if ( !previousSessions.empty() ) {
            previousSessions.back().restoreGeometry( &geometry );
        }

        auto window = newWindow( { session_, generateIdFromUuid(), nextWindowIndex() } );
        window->restoreGeometry( geometry );

        return window;
    }

    void loadFileNonInteractive( const QString& file )
    {
        while ( !activeWindows_.empty() && activeWindows_.top().isNull() ) {
            activeWindows_.pop();
        }

        if ( activeWindows_.empty() ) {
            newWindow();
        }

        activeWindows_.top()->loadFileNonInteractive( file );
    }

    void activatePrimaryWindow()
    {
        while ( !activeWindows_.empty() && activeWindows_.top().isNull() ) {
            activeWindows_.pop();
        }

        if ( activeWindows_.empty() ) {
            newWindow()->show();
        }

        if ( activeWindows_.empty() || activeWindows_.top().isNull() ) {
            return;
        }

        MainWindow* window = activeWindows_.top().data();
        if ( window->isMinimized() ) {
            window->showNormal();
        }
        else {
            window->show();
        }
        window->raise();
        window->activateWindow();
    }

    void startBackgroundTasks()
    {
        LOG_DEBUG << "startBackgroundTasks";
        versionChecker_.startCheck();
    }

    bool hasVisibleMainWindow() const
    {
        return std::any_of( mainWindows_.cbegin(), mainWindows_.cend(),
                            []( const auto& data ) {
                                return data.second != nullptr && data.second->isVisible();
                            } );
    }

    void ensureMainWindowVisible()
    {
        if ( hasVisibleMainWindow() ) {
            return;
        }

        LOG_WARNING << "No visible main window after startup, opening fallback window";
        auto* fallbackWindow = newWindow();
        fallbackWindow->reloadGeometry();
        fallbackWindow->show();
    }

#ifdef Q_OS_MAC
    bool event( QEvent* event ) override
    {
        if ( event->type() == QEvent::FileOpen ) {
            QFileOpenEvent* openEvent = static_cast<QFileOpenEvent*>( event );
            LOG_INFO << "File open request " << openEvent->file();

            if ( !isSecondary() ) {
                loadFileNonInteractive( openEvent->file() );
            }
            else {
                sendFilesToPrimaryInstance( { openEvent->file() } );
            }
        }

        return QApplication::event( event );
    }
#endif

  private:
    MainWindow* newWindow( WindowSession&& session )
    {
        mainWindows_.emplace_back( session, new MainWindow( session ) );

        auto& window = mainWindows_.back().second;

        activeWindows_.push( QPointer<MainWindow>( window ) );

        LOG_INFO << "Window " << &window << " created";
        connect( window, &MainWindow::newWindow, [ = ]() { newWindow()->show(); } );
        connect( window, &MainWindow::windowActivated,
                 [ this, window ]() { onWindowActivated( *window ); } );
        connect( window, &MainWindow::windowClosed,
                 [ this, window ]() { onWindowClosed( *window ); } );
        connect( window, &MainWindow::exitRequested, [ this ] { exitApplication(); } );

        return window;
    }

    void onWindowActivated( MainWindow& window )
    {
        LOG_INFO << "Window " << &window << " activated";
        activeWindows_.push( QPointer<MainWindow>( &window ) );
    }

    void onWindowClosed( MainWindow& window )
    {
        LOG_INFO << "Window " << &window << " closed";
        auto w = std::find_if( mainWindows_.begin(), mainWindows_.end(),
                               [ &window ]( const auto& p ) { return p.second == &window; } );

        if ( w != mainWindows_.end() ) {
            mainWindows_.erase( w );
        }
    }

    void exitApplication()
    {
        LOG_INFO << "exit application";
        session_->setExitRequested( true );
        auto mainWindows = mainWindows_;
        mainWindows.reverse();
        for ( const auto& [ session, window ] : mainWindows ) {
            Q_UNUSED( session );
            window->close();
        }

        QTimer::singleShot( 100, this, &QCoreApplication::quit );
    }

    void newVersionNotification( const QString& new_version, const QString& url,
                                 const QStringList& changes )
    {
        LOG_DEBUG << "newVersionNotification( " << new_version << " from " << url << " )";

        QString message = QString( "<p> A new version of klogg (%1) is available for download </p>"
                                   "<a href=\"%2\">%2</a>" )
                              .arg( new_version, url );

        if ( !changes.empty() ) {
            message.append( "<p>Important changes:</p><ul>" );
            for ( const auto& change : changes ) {
                message.append( QString( "<li>%1</li>" ).arg( change ) );
            }
            message.append( "</ul>" );
        }

        QMessageBox msgBox;
        msgBox.setText( message );
        msgBox.exec();
    }

    size_t nextWindowIndex() const
    {
        if ( mainWindows_.empty() ) {
            return 0;
        }
        else {
            const auto windowWithMaxIndex = std::max_element(
                mainWindows_.begin(), mainWindows_.end(), []( const auto& lhs, const auto& rhs ) {
                    return lhs.first.windowIndex() < rhs.first.windowIndex();
                } );
            return windowWithMaxIndex->first.windowIndex() + 1;
        }
    }

  private:
    KDSingleApplication singleApplication_;
    std::unique_ptr<CrashHandler> crashHandler_;

    MessageReceiver messageReceiver_;

    std::shared_ptr<Session> session_;

    std::list<std::pair<WindowSession, MainWindow*>> mainWindows_;
    std::stack<QPointer<MainWindow>> activeWindows_;

    VersionChecker versionChecker_;
};

#endif // KLOGG_KLOGGAPP_H

