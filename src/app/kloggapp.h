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
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkProxyFactory>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPoint>
#include <QRect>
#include <QTemporaryFile>
#include <QTimer>
#include <QSize>
#include <QStyle>
#include <QUuid>

#ifdef Q_OS_MAC
#include <QFileOpenEvent>
#endif

#include "configuration.h"
#include "actionsmanager.h"
#include "commander.h"
#include "crashhandler.h"
#include "klogg_version.h"
#include "log.h"
#include "session.h"
#include "sessioninfo.h"
#include "uuid.h"

#include <kdsingleapplication.h>

#include "mainwindow.h"
#include "messagereceiver.h"
#include "labqueuewindow.h"
#include "scenariorunner.h"
#include "scenariorunnerwindow.h"
#include "scriptrunnerwindow.h"
#include "scriptsupervisor.h"
#include "startupprogress.h"
#include "versionchecker.h"

class KloggApp : public QApplication {

    Q_OBJECT

  public:
    KloggApp( int& argc, char* argv[] )
        : QApplication( argc, argv)
    {
        QFontDatabase::addApplicationFont( ":/fonts/DejaVuSansMono.ttf" );

        QNetworkProxyFactory::setUseSystemConfiguration( true );
        scriptSupervisor_.setCommanderExecutor(
            [ this ]( const CommanderRequest& request ) { return executeCommanderRequest( request ); } );
        scenarioRunner_.setCommanderExecutor(
            [ this ]( const CommanderRequest& request ) { return executeCommanderRequest( request ); } );
        scriptRunnerWindow_.setWindowTitle( tr( "CILogg - script runner" ) );
        scenarioRunnerWindow_.setWindowTitle( tr( "CILogg - scenario runner" ) );
        labQueueWindow_.setWindowTitle( tr( "CILogg - lab queue" ) );
        connect( &scriptSupervisor_, &ScriptSupervisor::statusChanged, this,
                 [ this ]() {
                     for ( const auto& [ windowSession, window ] : mainWindows_ ) {
                         Q_UNUSED( windowSession );
                         if ( window != nullptr ) {
                             window->refreshScriptStatusIndicators();
                         }
                     }
                 } );

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
        qRegisterMetaType<CommanderRequest>( "CommanderRequest" );

        if ( singleApplication_.isPrimaryInstance() ) {
            QObject::connect( &singleApplication_, &KDSingleApplication::messageReceived, &messageReceiver_,
                              &MessageReceiver::receiveMessage, Qt::QueuedConnection );

            QObject::connect( &messageReceiver_, &MessageReceiver::loadFile, this,
                              &KloggApp::loadFileNonInteractive );
            QObject::connect( &messageReceiver_, &MessageReceiver::activateWindow, this,
                              &KloggApp::activatePrimaryWindow );
            QObject::connect( &messageReceiver_, &MessageReceiver::executeCommand, this,
                              &KloggApp::handleCommanderRequest );

            // Version checker notification
            connect( &versionChecker_, &VersionChecker::newVersionFound,
                     [ this ]( const QString& new_version, const QString& pageUrl,
                               const QString& assetUrl,
                               const QStringList& changes ) {
                         Q_UNUSED( pageUrl );
                         newVersionNotification( new_version, assetUrl, changes );
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
            ackPath = createUniqueTempPath( QStringLiteral( "klogg_activate_ack_" ),
                                            QStringLiteral( ".tmp" ) );
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

    CommanderResult sendCommandToPrimaryInstance( const CommanderRequest& request )
    {
#ifdef Q_OS_WIN
        ::AllowSetForegroundWindow( static_cast<DWORD>( primaryPid() ) );
#endif

        const auto resultPath = createUniqueTempPath( QStringLiteral( "klogg_command_result_" ),
                                                      QStringLiteral( ".tmp" ) );
        if ( resultPath.isEmpty() ) {
            return commanderFailure( CommanderResultCode::TransportError,
                                     QStringLiteral( "Failed to create commander response file." ) );
        }

        QCborMap data;
        data.insert( QLatin1String( "version" ), QString::fromLatin1( kloggVersion() ) );
        data.insert( QLatin1String( "type" ), QStringLiteral( "command" ) );
        data.insert( QLatin1String( "resultPath" ), resultPath );
        data.insert( QLatin1String( "command" ), QCborValue::fromVariant( commanderRequestToVariantMap( request ) ) );

        constexpr int CommanderConnectTimeoutMs = 5000;
        constexpr int CommanderResponseTimeoutMs = 30000;

        const QCborValue cbor( data );
        if ( !singleApplication_.sendMessageWithTimeout( cbor.toCbor(), CommanderConnectTimeoutMs ) ) {
            QFile::remove( resultPath );
            return commanderFailure( CommanderResultCode::TransportError,
                                     QStringLiteral( "Failed to contact the primary CILogg instance." ) );
        }

        if ( !waitForResponseFile( resultPath, CommanderResponseTimeoutMs ) ) {
            QFile::remove( resultPath );
            return commanderFailure( CommanderResultCode::TransportError,
                                     QStringLiteral( "Timed out waiting for commander response." ) );
        }

        QString readError;
        const auto result = readCommanderResult( resultPath, &readError );
        QFile::remove( resultPath );
        if ( !result ) {
            return commanderFailure( CommanderResultCode::TransportError,
                                     readError.isEmpty()
                                         ? QStringLiteral( "Invalid commander response." )
                                         : readError );
        }

        return *result;
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
        std::vector<MainWindow*> restoredWindows;

        StartupProgress::advance( QObject::tr( "Restoring window sessions" ) );
        for ( auto&& windowSession : session_->windowSessions() ) {
            try {
                auto* window = newWindow( std::move( windowSession ) );
                if ( !automationModeEnabled_ && !startupBootstrapEnabled_ ) {
                    window->reloadGeometry();
                }
                StartupProgress::advance( QObject::tr( "Restoring window" ),
                                          QString::number( windowSession.windowIndex() ) );

                // Keep startup responsive and avoid ending up with no visible
                // window when session restore data is malformed.
                window->reloadSession();
                restoredWindows.push_back( window );
                lastShownWindow = window;
            } catch ( const std::exception& e ) {
                LOG_ERROR << "Failed to restore window session: " << e.what();
            } catch ( ... ) {
                LOG_ERROR << "Failed to restore window session: unknown exception";
            }
        }

        if ( lastShownWindow == nullptr ) {
            lastShownWindow = newWindow();
            restoredWindows.push_back( lastShownWindow );
        }

        StartupProgress::advance( QObject::tr( "Preparing windows" ),
                                  QObject::tr( "Waiting for restored tabs to become ready" ) );
        constexpr int StartupUiReadyTimeoutMs = 120000;
        constexpr int StartupProbeTimeoutMs = 5000;
        constexpr int StartupUiIdleStableMs = 1500;
        QElapsedTimer startupReadyTimer;
        startupReadyTimer.start();
        int lastReportedSecond = -1;
        bool windowsShown = false;

        const auto showRestoredWindows = [ & ]() {
            if ( windowsShown ) {
                return;
            }

            for ( auto* window : restoredWindows ) {
                StartupProgress::advance( QObject::tr( "Showing window" ) );
                if ( automationModeEnabled_ ) {
                    applyAutomationGeometry( window );
                }
                else if ( startupBootstrapEnabled_ ) {
                    applyStartupBootstrapGeometry( window );
                }
                window->show();
            }

            windowsShown = true;
        };

        const auto areWindowsReadyAndVisible = [ & ]() {
            return std::all_of( restoredWindows.cbegin(), restoredWindows.cend(),
                                []( const MainWindow* window ) {
                                    return window != nullptr && window->isVisible()
                                           && window->isStartupReadyForDisplay();
                                } );
        };

        const auto runUiProbe = [ & ]() {
            const auto targetCount = static_cast<int>( std::count_if(
                restoredWindows.cbegin(), restoredWindows.cend(),
                []( const MainWindow* window ) { return window != nullptr; } ) );
            if ( targetCount == 0 ) {
                return true;
            }

            auto processedCount = std::make_shared<int>( 0 );
            for ( auto* window : restoredWindows ) {
                if ( window == nullptr ) {
                    continue;
                }

                QTimer::singleShot( 0, window, [ processedCount ]() { ++( *processedCount ); } );
            }

            QElapsedTimer probeTimer;
            probeTimer.start();
            while ( *processedCount < targetCount
                    && probeTimer.elapsed() < StartupProbeTimeoutMs
                    && startupReadyTimer.elapsed() < StartupUiReadyTimeoutMs ) {
                QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
            }

            return *processedCount == targetCount;
        };

        showRestoredWindows();

        while ( startupReadyTimer.elapsed() < StartupUiReadyTimeoutMs ) {
            if ( areWindowsReadyAndVisible() && runUiProbe() ) {
                StartupProgress::message( QObject::tr( "Preparing windows" ),
                                          QObject::tr( "Finalizing UI initialization" ) );

                QElapsedTimer idleStableTimer;
                idleStableTimer.start();

                while ( startupReadyTimer.elapsed() < StartupUiReadyTimeoutMs ) {
                    const bool windowsReady = areWindowsReadyAndVisible();
                    const bool probeReady = windowsReady && runUiProbe();

                    if ( probeReady ) {
                        if ( idleStableTimer.elapsed() >= StartupUiIdleStableMs ) {
                            StartupProgress::advance(
                                QObject::tr( "Windows ready" ),
                                QObject::tr( "Startup preparation complete" ) );
                            return lastShownWindow;
                        }
                    }
                    else {
                        idleStableTimer.restart();
                    }

                    QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
                }
            }

            const int elapsedSeconds = static_cast<int>( startupReadyTimer.elapsed() / 1000 );
            if ( elapsedSeconds != lastReportedSecond ) {
                lastReportedSecond = elapsedSeconds;
                StartupProgress::message( QObject::tr( "Preparing windows" ),
                                          QObject::tr( "Waiting for UI initialization (%1s)" )
                                              .arg( elapsedSeconds ) );
            }

            QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
        }

        LOG_WARNING << "Timed out waiting for restored windows to finish startup preparation";
        StartupProgress::advance( QObject::tr( "Preparing windows" ),
                                  QObject::tr( "Startup readiness timeout" ) );
        QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );

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
        if ( automationModeEnabled_ ) {
            applyAutomationGeometry( window );
        }
        else if ( startupBootstrapEnabled_ ) {
            applyStartupBootstrapGeometry( window );
        }
        else {
            window->restoreGeometry( geometry );
        }

        return window;
    }

    void setStartupBootstrapGeometry( const QPoint& topLeft )
    {
        startupBootstrapEnabled_ = true;
        startupBootstrapTopLeft_ = topLeft;
    }

    void setAutomationMode( bool enabled, const QPoint& topLeft = {}, const QSize& size = {} )
    {
        automationModeEnabled_ = enabled;
        if ( topLeft != QPoint() ) {
            automationTopLeft_ = topLeft;
        }
        if ( size.isValid() ) {
            automationWindowSize_ = size;
        }
    }

    bool automationModeEnabled() const { return automationModeEnabled_; }

    void finalizeStartupBootstrapGeometry()
    {
        if ( !startupBootstrapEnabled_ ) {
            return;
        }

        startupBootstrapEnabled_ = false;
        for ( auto& [ session, window ] : mainWindows_ ) {
            Q_UNUSED( session );
            if ( window != nullptr ) {
                if ( automationModeEnabled_ ) {
                    applyAutomationGeometry( window );
                }
                else {
                    window->reloadGeometry();
                }
            }
        }
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

    CommanderResult executeCommanderRequest( const CommanderRequest& request )
    {
        if ( request.action == CommanderAction::GetInfo ) {
            auto* activeWindow = activeWindowIfAny();
            QVariantList windows;
            for ( const auto& [ windowSession, window ] : mainWindows_ ) {
                Q_UNUSED( windowSession );
                if ( window == nullptr ) {
                    continue;
                }

                auto windowInfo = window->commanderWindowInfo();
                windowInfo.insert( QStringLiteral( "isActiveWindow" ), window == activeWindow );
                windows.push_back( windowInfo );
            }

            QVariantMap payload;
            payload.insert( QStringLiteral( "windows" ), windows );
            return commanderSuccess( {}, payload );
        }

        if ( request.action == CommanderAction::RunScenario ) {
            return scenarioRunner_.runScenario( request );
        }

        if ( request.action == CommanderAction::RunSuite ) {
            return scenarioRunner_.runSuite( request );
        }

        if ( request.action == CommanderAction::StopScenarioRun ) {
            return scenarioRunner_.stopRun();
        }

        if ( request.action == CommanderAction::GetScenarioStatus ) {
            return scenarioRunner_.status();
        }

        if ( request.action == CommanderAction::GetScenarioReport ) {
            return scenarioRunner_.report();
        }

        if ( request.action == CommanderAction::RunScript ) {
            return scriptSupervisor_.runScript( request );
        }

        if ( request.action == CommanderAction::RunGlobalScript ) {
            return scriptSupervisor_.runGlobalScript( request );
        }

        if ( request.action == CommanderAction::StopScript ) {
            return scriptSupervisor_.stopScript( request );
        }

        if ( request.action == CommanderAction::StopGlobalScript ) {
            return scriptSupervisor_.stopGlobalScript();
        }

        if ( request.action == CommanderAction::GetScriptStatus ) {
            return scriptSupervisor_.scriptStatus( request );
        }

        if ( request.action == CommanderAction::GetGlobalScriptStatus ) {
            return scriptSupervisor_.globalScriptStatus();
        }

        if ( request.action == CommanderAction::GetScriptSubscriptions ) {
            return scriptSupervisor_.scriptSubscriptions( request );
        }

        if ( request.action == CommanderAction::GetGlobalScriptSubscriptions ) {
            return scriptSupervisor_.globalScriptSubscriptions();
        }

        if ( request.action == CommanderAction::ClearScriptSubscriptions ) {
            return scriptSupervisor_.clearScriptSubscriptions( request );
        }

        if ( request.action == CommanderAction::ClearGlobalScriptSubscriptions ) {
            return scriptSupervisor_.clearGlobalScriptSubscriptions();
        }

        if ( request.action == CommanderAction::GetActions ) {
            QVariantList actions;
            for ( const auto& action : ActionsManager::instance().actions() ) {
                actions.push_back( actionDefinitionToVariantMap( action ) );
            }
            return commanderSuccess( {}, QVariantMap{ { QStringLiteral( "actions" ), actions } } );
        }

        if ( request.action == CommanderAction::GetResponses ) {
            QVariantList responses;
            for ( const auto& response : ActionsManager::instance().responses() ) {
                responses.push_back( responseDefinitionToVariantMap( response ) );
            }
            return commanderSuccess( {}, QVariantMap{ { QStringLiteral( "responses" ), responses } } );
        }

        if ( request.action == CommanderAction::CreateAction ) {
            QString errorMessage;
            const auto action = actionDefinitionFromVariantMap( request.definitionPayload, &errorMessage );
            if ( !errorMessage.isEmpty() ) {
                return commanderFailure( CommanderResultCode::InvalidRequest, errorMessage );
            }
            if ( !ActionsManager::instance().createAction( action, &errorMessage ) ) {
                return commanderFailure( CommanderResultCode::ExecutionFailed, errorMessage );
            }
            return commanderSuccess();
        }

        if ( request.action == CommanderAction::UpdateAction ) {
            if ( !request.entityId ) {
                return commanderFailure( CommanderResultCode::InvalidRequest,
                                         QStringLiteral( "Missing action id." ) );
            }
            QString errorMessage;
            const auto action = actionDefinitionFromVariantMap( request.definitionPayload, &errorMessage );
            if ( !errorMessage.isEmpty() ) {
                return commanderFailure( CommanderResultCode::InvalidRequest, errorMessage );
            }
            if ( !ActionsManager::instance().updateAction( *request.entityId, action, &errorMessage ) ) {
                return commanderFailure( CommanderResultCode::ExecutionFailed, errorMessage );
            }
            return commanderSuccess();
        }

        if ( request.action == CommanderAction::DeleteAction ) {
            if ( !request.entityId ) {
                return commanderFailure( CommanderResultCode::InvalidRequest,
                                         QStringLiteral( "Missing action id." ) );
            }
            QString errorMessage;
            if ( !ActionsManager::instance().deleteAction( *request.entityId, &errorMessage ) ) {
                return commanderFailure( CommanderResultCode::ExecutionFailed, errorMessage );
            }
            return commanderSuccess();
        }

        if ( request.action == CommanderAction::CreateResponse ) {
            QString errorMessage;
            const auto response = responseDefinitionFromVariantMap( request.definitionPayload, &errorMessage );
            if ( !errorMessage.isEmpty() ) {
                return commanderFailure( CommanderResultCode::InvalidRequest, errorMessage );
            }
            if ( !ActionsManager::instance().createResponse( response, &errorMessage ) ) {
                return commanderFailure( CommanderResultCode::ExecutionFailed, errorMessage );
            }
            return commanderSuccess();
        }

        if ( request.action == CommanderAction::UpdateResponse ) {
            if ( !request.entityId ) {
                return commanderFailure( CommanderResultCode::InvalidRequest,
                                         QStringLiteral( "Missing response id." ) );
            }
            QString errorMessage;
            const auto response = responseDefinitionFromVariantMap( request.definitionPayload, &errorMessage );
            if ( !errorMessage.isEmpty() ) {
                return commanderFailure( CommanderResultCode::InvalidRequest, errorMessage );
            }
            if ( !ActionsManager::instance().updateResponse( *request.entityId, response, &errorMessage ) ) {
                return commanderFailure( CommanderResultCode::ExecutionFailed, errorMessage );
            }
            return commanderSuccess();
        }

        if ( request.action == CommanderAction::DeleteResponse ) {
            if ( !request.entityId ) {
                return commanderFailure( CommanderResultCode::InvalidRequest,
                                         QStringLiteral( "Missing response id." ) );
            }
            QString errorMessage;
            if ( !ActionsManager::instance().deleteResponse( *request.entityId, &errorMessage ) ) {
                return commanderFailure( CommanderResultCode::ExecutionFailed, errorMessage );
            }
            return commanderSuccess();
        }

        if ( isCommanderOpenAction( request.action ) ) {
            auto* window = activeWindowOrCreate();
            if ( window == nullptr ) {
                return commanderFailure( CommanderResultCode::ExecutionFailed,
                                         QStringLiteral( "Failed to create a CILogg window." ) );
            }

            const auto result = window->executeCommanderRequest( request );
            if ( result.ok() ) {
                activatePrimaryWindow();
            }
            return result;
        }

        if ( request.action == CommanderAction::CloseKlogg ) {
            exitApplication();
            return commanderSuccess();
        }

        if ( request.action == CommanderAction::CloseAll ) {
            for ( const auto& [ windowSession, window ] : mainWindows_ ) {
                Q_UNUSED( windowSession );
                if ( window != nullptr ) {
                    window->executeCommanderRequest( request );
                }
            }
            return commanderSuccess();
        }

        const auto targetSpecificWindow
            = request.action == CommanderAction::CloseTab
              || request.action == CommanderAction::FocusTab
              || request.action == CommanderAction::GetFilters
              || request.action == CommanderAction::SetFilter
              || request.action == CommanderAction::Search
              || request.action == CommanderAction::SetFollowMode
              || request.action == CommanderAction::DumpState
              || request.action == CommanderAction::SendAction
              || request.action == CommanderAction::WaitResponse
              || request.action == CommanderAction::StartComm
              || request.action == CommanderAction::PlayComm
              || request.action == CommanderAction::PauseComm
              || request.action == CommanderAction::StopComm
              || request.action == CommanderAction::StartNewCommFile
              || request.action == CommanderAction::GetCommStatus
              || request.action == CommanderAction::StartLogging
              || request.action == CommanderAction::StopLogging
              || request.action == CommanderAction::AddComment
              || request.action == CommanderAction::GetResponseCounter
              || request.action == CommanderAction::ResetResponseCounter
              || request.action == CommanderAction::ClearComm;

        if ( targetSpecificWindow && request.windowIndex ) {
            auto* window = windowByIndex( *request.windowIndex );
            if ( window == nullptr ) {
                return commanderFailure( CommanderResultCode::NotFound,
                                         QStringLiteral( "Requested window was not found." ) );
            }

            return window->executeCommanderRequest( request );
        }

        const auto preferActiveWindow
            = ( request.action == CommanderAction::GetFilters
                || request.action == CommanderAction::SetFilter
                || request.action == CommanderAction::Search
                || request.action == CommanderAction::SetFollowMode
                || request.action == CommanderAction::DumpState
                || request.action == CommanderAction::SendAction
                || request.action == CommanderAction::WaitResponse
                || request.action == CommanderAction::StartComm
                || request.action == CommanderAction::PlayComm
                || request.action == CommanderAction::PauseComm
                || request.action == CommanderAction::StopComm
                || request.action == CommanderAction::StartNewCommFile
                || request.action == CommanderAction::GetCommStatus
                || request.action == CommanderAction::StartLogging
                || request.action == CommanderAction::StopLogging
                || request.action == CommanderAction::AddComment
                || request.action == CommanderAction::GetResponseCounter
                || request.action == CommanderAction::ResetResponseCounter
                || request.action == CommanderAction::ClearComm )
              && request.tabId.isEmpty() && !request.tabIndex;

        if ( preferActiveWindow ) {
            auto* window = activeWindowIfAny();
            if ( window != nullptr ) {
                return window->executeCommanderRequest( request );
            }

            if ( request.action != CommanderAction::DumpState ) {
                return commanderFailure( CommanderResultCode::NotFound,
                                         QStringLiteral( "No active CILogg window was found." ) );
            }
        }

        CommanderResult lastNotFound = commanderFailure(
            CommanderResultCode::NotFound, QStringLiteral( "Requested target was not found." ) );

        for ( const auto& [ windowSession, window ] : mainWindows_ ) {
            Q_UNUSED( windowSession );
            if ( window == nullptr ) {
                continue;
            }

            const auto result = window->executeCommanderRequest( request );
            if ( result.ok() ) {
                return result;
            }
            if ( result.code != CommanderResultCode::NotFound ) {
                return result;
            }

            lastNotFound = result;
        }

        return lastNotFound;
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

    void handleCommanderRequest( const CommanderRequest& request, const QString& resultPath )
    {
        if ( !startupCommanderReady_ ) {
            constexpr int StartupCommandRetryDelayMs = 50;
            QTimer::singleShot( StartupCommandRetryDelayMs, this,
                                [ this, request, resultPath ]() {
                                    handleCommanderRequest( request, resultPath );
                                } );
            return;
        }

        const auto result = executeCommanderRequest( request );
        if ( !writeCommanderResult( resultPath, result ) ) {
            LOG_WARNING << "Failed to write commander result to " << resultPath;
        }
    }

    ScriptSupervisor* scriptSupervisor()
    {
        return &scriptSupervisor_;
    }

    Q_INVOKABLE QVariantMap scriptStatusForTab( const QString& tabId ) const
    {
        return scriptSupervisor_.scriptStatusForTab( tabId );
    }

    Q_INVOKABLE QVariantMap globalScriptStatus() const
    {
        return scriptSupervisor_.globalScriptStatusPayload();
    }

    Q_INVOKABLE QVariantMap scriptBindingForTab( const QString& tabId ) const
    {
        return scriptSupervisor_.scriptBindingForTab( tabId );
    }

    Q_INVOKABLE QVariantMap globalScriptBinding() const
    {
        return scriptSupervisor_.globalScriptBinding();
    }

    Q_INVOKABLE bool hasActiveScriptForTab( const QString& tabId ) const
    {
        return scriptSupervisor_.hasActiveScriptForTab( tabId );
    }

    Q_INVOKABLE void restoreScriptBinding( const QVariantMap& binding )
    {
        scriptSupervisor_.restoreScriptBinding( binding );
    }

    Q_INVOKABLE void restoreScriptBindingVariant( const QVariant& binding )
    {
        scriptSupervisor_.restoreScriptBinding( binding.toMap() );
    }

    Q_INVOKABLE void restoreGlobalScriptBinding( const QVariantMap& binding )
    {
        scriptSupervisor_.restoreGlobalScriptBinding( binding );
    }

    Q_INVOKABLE void restoreGlobalScriptBindingVariant( const QVariant& binding )
    {
        scriptSupervisor_.restoreGlobalScriptBinding( binding.toMap() );
    }

    Q_INVOKABLE void forgetScriptTab( const QString& tabId )
    {
        scriptSupervisor_.forgetTab( tabId );
    }

    Q_INVOKABLE void restoreGlobalScriptBindingFromSession()
    {
        if ( globalScriptRestoreAttempted_ ) {
            return;
        }

        globalScriptRestoreAttempted_ = true;
        const auto binding = SessionInfo::getSynced().globalScriptContext();
        if ( binding.trimmed().isEmpty() ) {
            return;
        }

        const auto document = QJsonDocument::fromJson( binding.toUtf8() );
        if ( !document.isObject() ) {
            return;
        }

        scriptSupervisor_.restoreGlobalScriptBinding( document.object().toVariantMap() );
    }

    Q_INVOKABLE void showScriptRunnerWindow( const QString& scriptPath = {} )
    {
        if ( !scriptPath.isEmpty() ) {
            scriptRunnerWindow_.setScriptPath( scriptPath );
        }
        auto state = scriptRunnerWindow_.windowState();
        state.setFlag( Qt::WindowMinimized, false );
        scriptRunnerWindow_.setWindowState( state );
        scriptRunnerWindow_.show();
        scriptRunnerWindow_.raise();
        scriptRunnerWindow_.activateWindow();
    }

    Q_INVOKABLE void showScenarioRunnerWindow()
    {
        auto state = scenarioRunnerWindow_.windowState();
        state.setFlag( Qt::WindowMinimized, false );
        scenarioRunnerWindow_.setWindowState( state );
        scenarioRunnerWindow_.show();
        scenarioRunnerWindow_.raise();
        scenarioRunnerWindow_.activateWindow();
    }

    Q_INVOKABLE void showLabQueueWindow()
    {
        auto state = labQueueWindow_.windowState();
        state.setFlag( Qt::WindowMinimized, false );
        labQueueWindow_.setWindowState( state );
        labQueueWindow_.show();
        labQueueWindow_.raise();
        labQueueWindow_.activateWindow();
    }

    Q_INVOKABLE void publishScriptEvent( const QVariantMap& event )
    {
        scriptSupervisor_.publishEvent( event );
        scenarioRunner_.publishEvent( event );
    }

    void setStartupCommanderReady( bool ready = true )
    {
        startupCommanderReady_ = ready;
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
        if ( automationModeEnabled_ ) {
            applyAutomationGeometry( fallbackWindow );
        }
        else if ( !startupBootstrapEnabled_ ) {
            fallbackWindow->reloadGeometry();
        }
        else {
            applyStartupBootstrapGeometry( fallbackWindow );
        }
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
        if ( automationModeEnabled_ ) {
            applyAutomationGeometry( window );
        }
        else if ( startupBootstrapEnabled_ ) {
            applyStartupBootstrapGeometry( window );
        }

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

        QString message = QString( "<p> A new version of CILogg (%1) is available for download </p>"
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

    void applyStartupBootstrapGeometry( MainWindow* window ) const
    {
        if ( window == nullptr ) {
            return;
        }

        auto bootstrapSize = window->minimumSizeHint().expandedTo( QSize( 320, 200 ) );
        if ( !bootstrapSize.isValid() ) {
            bootstrapSize = QSize( 320, 200 );
        }

        const auto titleBarHeight = window->style()->pixelMetric( QStyle::PM_TitleBarHeight, nullptr, window );
        const auto bootstrapTopLeft
            = startupBootstrapTopLeft_ + QPoint( 0, std::max( titleBarHeight, 24 ) );

        window->setGeometry( QRect( bootstrapTopLeft, bootstrapSize ) );
    }

    void applyAutomationGeometry( MainWindow* window ) const
    {
        if ( window == nullptr ) {
            return;
        }

        const auto automationSize = automationWindowSize_.isValid() ? automationWindowSize_
                                                                    : QSize( 1600, 1000 );
        window->setGeometry( QRect( automationTopLeft_, automationSize ) );
    }

    MainWindow* activeWindowOrCreate()
    {
        auto* activeWindow = activeWindowIfAny();
        if ( activeWindow == nullptr ) {
            auto* newMainWindow = newWindow();
            newMainWindow->show();
            return newMainWindow;
        }

        return activeWindow;
    }

    MainWindow* activeWindowIfAny()
    {
        while ( !activeWindows_.empty() && activeWindows_.top().isNull() ) {
            activeWindows_.pop();
        }

        if ( activeWindows_.empty() ) {
            return nullptr;
        }

        return activeWindows_.top().data();
    }

    MainWindow* windowByIndex( int windowIndex ) const
    {
        for ( const auto& [ session, window ] : mainWindows_ ) {
            if ( window != nullptr && static_cast<int>( session.windowIndex() ) == windowIndex ) {
                return window;
            }
        }

        return nullptr;
    }

    static QString createUniqueTempPath( const QString& prefix, const QString& suffix )
    {
        const auto tempDirPath = QDir::tempPath();
        for ( auto attempt = 0; attempt < 16; ++attempt ) {
            const auto candidate = QDir{ tempDirPath }.filePath(
                prefix + QUuid::createUuid().toString( QUuid::WithoutBraces ) + suffix );
            if ( !QFileInfo::exists( candidate ) ) {
                return candidate;
            }
        }
        return {};
    }

    static bool waitForResponseFile( const QString& resultPath, int timeoutMs )
    {
        constexpr int PollIntervalMs = 20;

        bool received = false;
        QEventLoop waitLoop;
        QTimer pollTimer;
        QTimer timeoutTimer;
        pollTimer.setInterval( PollIntervalMs );
        timeoutTimer.setSingleShot( true );

        connect( &pollTimer, &QTimer::timeout, &waitLoop, [ & ]() {
            if ( QFileInfo::exists( resultPath ) ) {
                received = true;
                waitLoop.quit();
            }
        } );
        connect( &timeoutTimer, &QTimer::timeout, &waitLoop, &QEventLoop::quit );

        pollTimer.start();
        timeoutTimer.start( timeoutMs );
        waitLoop.exec( QEventLoop::ProcessEventsFlag::ExcludeUserInputEvents );

        return received || QFileInfo::exists( resultPath );
    }

  private:
    KDSingleApplication singleApplication_;
    std::unique_ptr<CrashHandler> crashHandler_;

    MessageReceiver messageReceiver_;
    ScriptSupervisor scriptSupervisor_;
    ScriptRunnerWindow scriptRunnerWindow_{ &scriptSupervisor_ };
    ScenarioRunner scenarioRunner_;
    ScenarioRunnerWindow scenarioRunnerWindow_{ &scenarioRunner_ };
    LabQueueWindow labQueueWindow_{};
    bool globalScriptRestoreAttempted_ = false;

    std::shared_ptr<Session> session_;

    std::list<std::pair<WindowSession, MainWindow*>> mainWindows_;
    std::stack<QPointer<MainWindow>> activeWindows_;
    bool startupBootstrapEnabled_ = false;
    QPoint startupBootstrapTopLeft_;
    bool automationModeEnabled_ = false;
    bool startupCommanderReady_ = false;
    QPoint automationTopLeft_ = QPoint( 40, 40 );
    QSize automationWindowSize_ = QSize( 1600, 1000 );

    VersionChecker versionChecker_;
};

#endif // KLOGG_KLOGGAPP_H

