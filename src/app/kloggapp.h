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
#include <QThread>
#include <QEventLoop>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QTemporaryFile>
#include <QTimer>
#include <QSize>
#include <QStyle>
#include <QUrl>
#include <QUuid>

#ifdef Q_OS_MACOS
#include <QFileOpenEvent>
#endif

#include "actionsmanager.h"
#include "commander.h"
#include "configuration.h"
#include "crashdumps.h"
#include "crashhandler.h"
#include "diagnostics.h"
#include "issuereporter.h"
#include "klogg_version.h"
#include "log.h"
#include "session.h"
#include "sessioninfo.h"
#include "uuid.h"

#include <kdsingleapplication.h>

#include "labqueuewindow.h"
#include "mainwindow.h"
#include "messagereceiver.h"
#include "pluginfeatures.h"
#include "pluginmanager.h"
#include "pluginsourceruns.h"
#include "pluginupdatechecker.h"
#include "pluginupdatenotices.h"
#include "previewpackregistry.h"
#include "scenariorunner.h"
#include "scenariorunnerwindow.h"
#include "scriptrunnerwindow.h"
#include "scriptsupervisor.h"
#include "startupprogress.h"
#include "updateuicontroller.h"
#include "versionchecker.h"

class KloggApp : public QApplication {

    Q_OBJECT

  public:
    KloggApp( int& argc, char* argv[] )
        : QApplication( argc, argv)
        , updateUiController_(
              versionChecker_, [] { return activeWindow(); },
              [] { return Configuration::get().updateAction(); },
              QStringLiteral( "background" ), this )
    {
        QFontDatabase::addApplicationFont( ":/fonts/DejaVuSansMono.ttf" );

        QNetworkProxyFactory::setUseSystemConfiguration( true );
        // Commander clients reach this instance through its single-application name.
        pluginManager_.setInstanceId( singleApplication_.name() );
        // Enabled configuration packs feed previews and the TShark decoder (E16).
        PreviewPackRegistry::instance().attach( &pluginManager_ );
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
        }
        // Also for --multi instances, which run their own GUI as a secondary instance.
        setupDiagnostics();
    }

    ~KloggApp() override
    {
        cilogg::diagnostics::MemoryMonitor::stop();
        cilogg::diagnostics::DiagnosticReports::setContextProvider( {} );
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
        // On Windows the single-instance transport can report a failed send although the
        // primary read the whole message (it closes the pipe as soon as it has read it, which
        // races the sender's own write/disconnect bookkeeping; about 1 send in 75 under test).
        // The primary writes this receipt on arrival, so the sender can tell the cases apart
        // without re-sending (a re-send could run a command twice).
        const auto receiptPath = createUniqueTempPath( QStringLiteral( "klogg_command_receipt_" ),
                                                       QStringLiteral( ".tmp" ) );
        if ( !receiptPath.isEmpty() ) {
            data.insert( QLatin1String( "receiptPath" ), receiptPath );
        }

        // Cold dialogs can spend several seconds populating fonts and controls before
        // their modal event loop starts accepting the next automation command.
        const bool waitsForUi = request.action == CommanderAction::GetUi
                                || request.action == CommanderAction::SetUi
                                || request.action == CommanderAction::ActivateUi
                                || request.action == CommanderAction::InvokeAction;
        const int commanderConnectTimeoutMs = waitsForUi ? 30000 : 5000;
        constexpr int CommanderResponseTimeoutMs = 30000;

        const QCborValue cbor( data );
        QElapsedTimer connectionTimer;
        connectionTimer.start();
        LOG_DEBUG << "Commander connection: action="
                  << commanderActionToString( request.action ).toStdString()
                  << " waitingForUi=" << waitsForUi << " timeoutMs=" << commanderConnectTimeoutMs;
        const bool sent
            = singleApplication_.sendMessageWithTimeout( cbor.toCbor(), commanderConnectTimeoutMs );
        const bool delivered = sent
                               || ( !receiptPath.isEmpty()
                                    && waitForFile( receiptPath, commanderConnectTimeoutMs ) );
        QFile::remove( receiptPath );
        if ( !delivered ) {
            LOG_WARNING << "Commander connection failed after " << connectionTimer.elapsed()
                        << " ms; waitingForUi=" << waitsForUi;
            QFile::remove( resultPath );
            return commanderFailure(
                CommanderResultCode::TransportError,
                QStringLiteral( "Failed to contact the primary CILogg instance." ) );
        }
        if ( !sent ) {
            LOG_WARNING << "Commander transport reported a failed send, but the primary "
                           "confirmed receipt after "
                        << connectionTimer.elapsed() << " ms";
        }

        LOG_DEBUG << "Commander connection acknowledged after " << connectionTimer.elapsed()
                  << " ms";
        QString readError;
        const auto result = waitForCommanderResult( resultPath, CommanderResponseTimeoutMs,
                                                    &readError );
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
        // R06b: Sentry (when built in) puts its own crash handlers in place without a DSN; the
        // local crash records and dumps take precedence again.
        if ( cilogg::diagnostics::CrashDumps::installed() ) {
            cilogg::diagnostics::CrashDumps::install( cilogg::diagnostics::CrashDumps::folder() );
        }
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
        // Automated runs still write diagnostic reports, but never open a dialog.
        IssueReporter::setInteractive( !enabled );
        pluginUpdateNotices_.setInteractive( !enabled );
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

    // E16c3: "<pack-id>/<local-id>" names a configuration pack's action or response the same on
    // every PC; here it becomes the item's run-time id. The error, or an empty string.
    static QString resolvePackEntity( CommanderRequest* request )
    {
        if ( request->entityKey.isEmpty() ) {
            return {};
        }
        const auto& actions = ActionsManager::instance();
        std::optional<int> id;
        QString kind;
        switch ( request->action ) {
        case CommanderAction::SendAction:
        case CommanderAction::UpdateAction:
        case CommanderAction::DeleteAction:
            id = actions.resolvePackAction( request->entityKey );
            kind = QStringLiteral( "action" );
            break;
        case CommanderAction::WaitResponse:
        case CommanderAction::GetResponseCounter:
        case CommanderAction::ResetResponseCounter:
        case CommanderAction::UpdateResponse:
        case CommanderAction::DeleteResponse:
            id = actions.resolvePackResponse( request->entityKey );
            kind = QStringLiteral( "response" );
            break;
        default:
            return QStringLiteral( "%1 does not take an action or response id." )
                .arg( commanderActionToString( request->action ) );
        }
        if ( !id ) {
            return QStringLiteral( "There is no %1 %2 (an enabled configuration pack's item is "
                                   "named \"<pack-id>/<local-id>\")." )
                .arg( kind, request->entityKey.left( 256 ) );
        }
        request->entityId = *id;
        request->entityKey.clear();
        return {};
    }

    CommanderResult executeCommanderRequest( const CommanderRequest& request )
    {
        if ( !request.entityKey.isEmpty() ) {
            auto resolved = request;
            if ( const auto error = resolvePackEntity( &resolved ); !error.isEmpty() ) {
                return commanderFailure( CommanderResultCode::InvalidRequest, error );
            }
            return executeCommanderRequest( resolved );
        }
        using cilogg::plugins::Result;
        const auto pluginResult = []( const Result& result ) {
            return result.ok() ? commanderSuccess( {}, result.payload )
                               : commanderFailure( CommanderResultCode::ExecutionFailed, result.error );
        };
        journalCommanderRequest( request );
        if ( request.action == CommanderAction::ListReports ) {
            return commanderSuccess(
                {}, { { QStringLiteral( "root" ), QDir::toNativeSeparators(
                                                      cilogg::diagnostics::DiagnosticReports::root() ) },
                      { QStringLiteral( "reports" ),
                        cilogg::diagnostics::DiagnosticReports::list() } } );
        }
        if ( request.action == CommanderAction::CreateReport ) {
            cilogg::diagnostics::ReportRequest reportRequest;
            reportRequest.kind = QStringLiteral( "manual" );
            reportRequest.operation = QStringLiteral( "Report requested through Commander" );
            if ( !request.commentText.isEmpty() ) {
                reportRequest.details.insert( QStringLiteral( "note" ), request.commentText );
            }
            const auto report = cilogg::diagnostics::DiagnosticReports::create( reportRequest );
            if ( report.directory.isEmpty() ) {
                return commanderFailure( CommanderResultCode::ExecutionFailed, report.error );
            }
            return commanderSuccess(
                {}, { { QStringLiteral( "id" ), report.id },
                      { QStringLiteral( "directory" ), QDir::toNativeSeparators( report.directory ) },
                      { QStringLiteral( "archive" ), QDir::toNativeSeparators( report.archive ) },
                      { QStringLiteral( "summary" ), report.summary },
                      { QStringLiteral( "complete" ), report.ok },
                      { QStringLiteral( "error" ), report.error } } );
        }
        if ( request.action == CommanderAction::GetPlugins ) {
            // `hostFeatures`: what this CILogg supports, with versions, such as
            // `python_decoders` for configuration packs' Python decoders (MD03b).
            return commanderSuccess(
                {}, { { "plugins", pluginManager_.plugins() },
                      { "storage", pluginManager_.storageStatus() },
                      { "jobs", pluginManager_.jobs() },
                      { "hostFeatures",
                        cilogg::plugins::hostFeatureVersions().toVariantMap() } } );
        }
        if ( request.action == CommanderAction::PluginManage ) {
            return pluginResult( pluginManager_.manage( request.pluginOperation, request.pluginId,
                                                        request.pluginArguments, request.origin ) );
        }
        if ( request.action == CommanderAction::GetPluginJob ) { return pluginResult( pluginManager_.job( request.pluginJobId ) ); }
        if ( request.action == CommanderAction::CancelPluginJob ) { return pluginResult( pluginManager_.cancel( request.pluginJobId ) ); }
        if ( request.action == CommanderAction::InvokePlugin ) {
            // U15b: a source picks the tab(s); "all" invokes once per open tab, staged.
            return pluginSourceRuns_.invoke( request );
        }
        if ( request.action == CommanderAction::GetPluginTooltip
             || request.action == CommanderAction::GetPluginMenus
             || request.action == CommanderAction::TriggerPluginMenu ) {
            // E17a: a tooltip of a view open in this window (docs/plugin-sdk-advanced.md 9.5).
            // E17b: the plugin menus of a window and a tab (section 15).
            auto* window = request.windowIndex ? windowByIndex( *request.windowIndex )
                                               : activeWindowIfAny();
            if ( request.action != CommanderAction::GetPluginTooltip && !request.windowIndex
                 && !request.tabId.isEmpty() ) {
                // A tab of another window: that window's menus.
                for ( const auto& [ windowSession, candidate ] : mainWindows_ ) {
                    Q_UNUSED( windowSession );
                    if ( candidate != nullptr && candidate->hasTab( request.tabId ) ) {
                        window = candidate;
                    }
                }
            }
            if ( window == nullptr ) {
                return commanderFailure( CommanderResultCode::NotFound,
                                         QStringLiteral( "Requested window was not found." ) );
            }
            return window->executeCommanderRequest( request );
        }
        if ( request.action == CommanderAction::OpenPluginView ) {
            auto* window = request.windowIndex ? windowByIndex( *request.windowIndex )
                                               : activeWindowOrCreate();
            if ( window == nullptr ) {
                return commanderFailure( CommanderResultCode::NotFound,
                                         QStringLiteral( "Requested window was not found." ) );
            }
            return window->executeCommanderRequest( request );
        }
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

        // The user's items, then the enabled configuration packs' (E16c3) with packId, packName,
        // their pack-local id and "<pack-id>/<local-id>" (the same on every PC) next to the
        // run-time id.
        const auto packFields = []( QVariantMap item, const QString& packId,
                                    const QString& packName, int localId ) {
            item.insert( QStringLiteral( "packId" ), packId );
            item.insert( QStringLiteral( "packName" ), packName );
            item.insert( QStringLiteral( "localId" ), localId );
            item.insert( QStringLiteral( "namespacedId" ), namespacedItemId( packId, localId ) );
            return item;
        };
        if ( request.action == CommanderAction::GetActions ) {
            QVariantList actions;
            for ( const auto& action : ActionsManager::instance().actions() ) {
                actions.push_back( actionDefinitionToVariantMap( action ) );
            }
            for ( const auto& listed : ActionsManager::instance().packActions() ) {
                actions.push_back( packFields( actionDefinitionToVariantMap( listed.action ),
                                               listed.packId, listed.packName, listed.localId ) );
            }
            return commanderSuccess( {}, QVariantMap{ { QStringLiteral( "actions" ), actions } } );
        }

        if ( request.action == CommanderAction::GetResponses ) {
            QVariantList responses;
            for ( const auto& response : ActionsManager::instance().responses() ) {
                responses.push_back( responseDefinitionToVariantMap( response ) );
            }
            for ( const auto& listed : ActionsManager::instance().packResponses() ) {
                responses.push_back( packFields( responseDefinitionToVariantMap( listed.response ),
                                                 listed.packId, listed.packName, listed.localId ) );
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
            = isCommanderAnalysisAction( request.action ) || isCommanderPaneAction( request.action )
              || request.action == CommanderAction::CloseTab
              || request.action == CommanderAction::FocusTab
              || request.action == CommanderAction::GetFilters
              || request.action == CommanderAction::SetFilter
              || request.action == CommanderAction::Search
              || request.action == CommanderAction::SetFollowMode
              || request.action == CommanderAction::DumpState
              || request.action == CommanderAction::InvokeAction
              || request.action == CommanderAction::GetUi
              || request.action == CommanderAction::SetUi
              || request.action == CommanderAction::ActivateUi
              || request.action == CommanderAction::SendAction
              || request.action == CommanderAction::SetActionsEnabled
              || request.action == CommanderAction::SetAutoResponsesEnabled
              || request.action == CommanderAction::SelectActionsTab
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

        // Pane commands without a pane id or tab id act in the active window; with one, every
        // window is asked until the one that holds the pane or tab answers.
        const auto preferActiveWindow
            = ( isCommanderAnalysisAction( request.action )
                || ( isCommanderPaneAction( request.action ) && request.paneId.isEmpty() )
                || request.action == CommanderAction::GetFilters
                || request.action == CommanderAction::SetFilter
                || request.action == CommanderAction::Search
                || request.action == CommanderAction::SetFollowMode
                || request.action == CommanderAction::DumpState
                || request.action == CommanderAction::InvokeAction
                || request.action == CommanderAction::GetUi
                || request.action == CommanderAction::SetUi
                || request.action == CommanderAction::ActivateUi
                || request.action == CommanderAction::SendAction
                || request.action == CommanderAction::SetActionsEnabled
                || request.action == CommanderAction::SetAutoResponsesEnabled
                || request.action == CommanderAction::SelectActionsTab
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

    // An updated build confirms its start to the updater helper as early as possible: the helper
    // rolls a self-contained update back when no acknowledgement arrives within its timeout,
    // however long session restore or opening files later takes.
    void acknowledgeUpdatedStartup()
    {
        versionChecker_.acknowledgeStartup();
    }

    void startBackgroundTasks()
    {
        LOG_DEBUG << "startBackgroundTasks";
        // Only the primary GUI process binds the updater to this installation. CLI and
        // secondary processes leave the persisted update transaction alone.
        versionChecker_.activate();
        versionChecker_.startCheck();
        // Like the application updater, the plugin update check never runs in automation or
        // headless mode (docs/plugin-repositories.md 3.7).
        if ( cilogg::plugins::PluginUpdateChecker::mayRun( automationModeEnabled_,
                                                           platformName() ) ) {
            pluginUpdateChecker_.start();
        }
        else {
            LOG_INFO << "Plugin update checks are off in automation or headless mode";
        }
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

#ifdef Q_OS_MACOS
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
        window->setVersionChecker( versionChecker_ );
        window->setPluginManager( pluginManager_, [ this ]( const CommanderRequest& request ) {
            return executeCommanderRequest( request );
        } );
        window->setApplicationSources(
            [ this ] { return applicationAnalysisSources(); },
            [ this ]( const CommanderRequest& request ) { return executeCommanderRequest( request ); },
            [ this ]( const QString& tabId ) { return windowForTab( tabId ); } );
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
        // Closed widgets remain alive. Remove every activation entry so later file/command
        // requests cannot select a closed window, while retaining the remaining activation order.
        decltype( activeWindows_ ) retained;
        size_t removed = 0;
        while ( !activeWindows_.empty() ) {
            auto candidate = activeWindows_.top();
            activeWindows_.pop();
            if ( candidate && candidate.data() != &window ) {
                retained.push( candidate );
            }
            else {
                ++removed;
            }
        }
        while ( !retained.empty() ) {
            activeWindows_.push( retained.top() );
            retained.pop();
        }
        LOG_DEBUG << "Removed closed/null window activation entries: removed=" << removed
                  << " remaining=" << activeWindows_.size();
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

    // Every window's tabs, for the analysis docks' source pickers. With several windows, labels
    // name the window.
    QVariantList applicationAnalysisSources()
    {
        auto* active = activeWindowIfAny();
        const auto openWindows = std::count_if(
            mainWindows_.cbegin(), mainWindows_.cend(),
            []( const auto& entry ) { return entry.second != nullptr; } );
        QVariantList sources;
        for ( const auto& [ session, window ] : mainWindows_ ) {
            if ( window == nullptr ) {
                continue;
            }
            const auto index = static_cast<int>( session.windowIndex() );
            for ( const auto& value : window->analysisSources( index, window == active ) ) {
                auto source = value.toMap();
                if ( openWindows > 1 ) {
                    source[ "label" ] = tr( "Window %1 · %2" )
                                            .arg( index + 1 )
                                            .arg( source.value( "label" ).toString() );
                }
                sources.push_back( source );
            }
        }
        return sources;
    }

    // invoke_plugin on one tab (tabId), or on the current tab of a window (windowIndex, else the
    // active window).
    CommanderResult invokePluginOnTab( const CommanderRequest& request )
    {
        QVariantMap context;
        if ( !request.tabId.isEmpty() ) {
            for ( const auto& [ session, window ] : mainWindows_ ) {
                Q_UNUSED( session );
                if ( window ) {
                    context = window->pluginContext( request );
                    if ( !context.isEmpty() ) {
                        break;
                    }
                }
            }
        }
        else {
            auto* window = request.windowIndex ? windowByIndex( *request.windowIndex )
                                               : activeWindowOrCreate();
            if ( window ) {
                context = window->pluginContext( request );
            }
        }
        if ( context.isEmpty() ) {
            return commanderFailure( CommanderResultCode::NotFound,
                                     "Plugin target window/tab not found." );
        }
        if ( !request.pluginMenuContext.isEmpty() ) {
            context.insert( "menu", request.pluginMenuContext ); // E17b, design 7.5
        }
        const auto result = pluginManager_.invoke( request.pluginId, request.pluginAction,
                                                   request.pluginArguments, context );
        if ( result.ok() ) {
            return commanderSuccess( {}, result.payload );
        }
        // A full plugin manager admits the job later; staged "all" Runs wait for it.
        const bool full = result.error == QLatin1String( "Too many active plugin jobs." );
        return { CommanderResultCode::ExecutionFailed, result.error,
                 full ? QVariantMap{ { "reason", "queue_limit" } } : QVariantMap{} };
    }

    CommanderResult pluginJobStatus( const QString& jobId ) const
    {
        const auto job = pluginManager_.job( jobId );
        if ( job.ok() ) {
            return commanderSuccess( {}, job.payload );
        }
        return { CommanderResultCode::NotFound, job.error, { { "reason", "job_expired" } } };
    }

    // A file source that is not open yet opens in the active window.
    CommanderResult openFileForSource( const QString& path )
    {
        auto* window = activeWindowOrCreate();
        if ( window == nullptr ) {
            return commanderFailure( CommanderResultCode::ExecutionFailed,
                                     QStringLiteral( "Failed to create a CILogg window." ) );
        }
        CommanderRequest open;
        open.action = CommanderAction::OpenFile;
        open.filePath = path;
        return window->executeCommanderRequest( open );
    }

    MainWindow* windowForTab( const QString& tabId ) const
    {
        for ( const auto& [ session, window ] : mainWindows_ ) {
            Q_UNUSED( session );
            if ( window != nullptr && window->hasTab( tabId ) ) {
                return window;
            }
        }
        return nullptr;
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

    // Polls for a file another process creates; true once it exists, false after timeoutMs.
    static bool waitForFile( const QString& path, int timeoutMs )
    {
        constexpr int PollMs = 20;
        QElapsedTimer timer;
        timer.start();
        while ( !QFileInfo::exists( path ) ) {
            if ( timer.elapsed() >= timeoutMs ) {
                return false;
            }
            QThread::msleep( PollMs );
        }
        return true;
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

  private:
    // Diagnostic reports (task R06): reports live next to plugin-data (the portable folder or the
    // application data folder); memory is sampled from startup.
    void setupDiagnostics()
    {
        using namespace cilogg::diagnostics;
        const QFileInfo pluginData( cilogg::plugins::PluginRepository::defaultDataRoot() );
        DiagnosticReports::setRoot( QDir( pluginData.absolutePath() ).filePath( "reports" ) );
        DiagnosticReports::setContextProvider( [ this ]() { return diagnosticContext(); } );
        // R06b: a crash leaves a record (and on Windows a minidump) in reports/crashes; the next
        // start reports it. Every C++ throw records where it came from.
        CrashDumps::setFullMemory( Configuration::get().fullMemoryCrashDumps() );
        if ( !CrashDumps::install( QDir( DiagnosticReports::root() ).filePath( "crashes" ) ) ) {
            LOG_WARNING << "Crash dumps are off: cannot create the crash folder";
        }
        installThrowSiteCapture();
        MemoryMonitor::start();
        ActivityJournal::record( QStringLiteral( "app" ),
                                 QStringLiteral( "CILogg %1 started (pid %2)" )
                                     .arg( QString( kloggVersion() ),
                                           QString::number( applicationPid() ) ) );
    }

    // Every Commander request (GUI scripts, CLI, MCP, Python, scenarios) except read-only
    // queries. Only listed fields are kept: targets, files, search text and switches; payload
    // values (plugin and analysis arguments, definitions, action parameters) become their key
    // names and URLs lose their user info, because they may hold credentials.
    static void journalCommanderRequest( const CommanderRequest& request )
    {
        const auto name = commanderActionToString( request.action );
        if ( name.isEmpty() || name.startsWith( QLatin1String( "get_" ) )
             || name == QLatin1String( "dump_state" ) || name == QLatin1String( "list_reports" )
             || name == QLatin1String( "wait_response" ) ) {
            return;
        }
        const auto full = commanderRequestToVariantMap( request );
        QVariantMap data;
        for ( const auto* key :
              { "action", "tabId", "windowIndex", "tabIndex", "portName", "filePath", "url",
                "pluginId", "pluginAction", "pluginOperation", "pluginJobId", "entityId",
                "entityName", "objectName", "scenarioFilePath", "scriptFilePath", "suiteFilePath",
                "argsJsonFilePath", "filterId", "filterIndex", "filterString", "searchText",
                "match_mode", "followFile", "predefinedFilters", "runSearch", "enabled",
                "timeoutMs", "wait", "allEntities", "comSettings", "searchUseRegex",
                "searchCaseSensitive", "searchInverseMatch", "searchUseBoolean",
                "searchAutoRefresh", "searchKeepResults" } ) {
            const auto field = QString::fromLatin1( key );
            auto value = full.value( field );
            if ( !value.isValid()
                 || ( value.userType() == QMetaType::QString && value.toString().isEmpty() ) ) {
                continue;
            }
            if ( field == QLatin1String( "url" ) ) {
                value = QUrl( value.toString() ).adjusted( QUrl::RemoveUserInfo ).toString();
            }
            data.insert( field, value );
        }
        for ( const auto* key : { "pluginArguments", "analysisArguments", "definitionPayload",
                                  "actionParameters" } ) {
            const auto field = QString::fromLatin1( key );
            const auto keys = full.value( field ).toMap().keys();
            if ( !keys.isEmpty() ) {
                data.insert( field, QStringList( keys ) );
            }
        }

        QStringList summary{ name };
        for ( const auto* key : { "tabId", "windowIndex", "tabIndex", "portName", "filePath", "url",
                                  "pluginId", "pluginAction", "pluginOperation", "entityId",
                                  "entityName", "objectName", "scenarioFilePath",
                                  "scriptFilePath" } ) {
            const auto value = data.value( QString::fromLatin1( key ) );
            if ( value.isValid() && !value.toString().isEmpty() ) {
                summary.push_back( QStringLiteral( "%1=%2" ).arg( QString::fromLatin1( key ),
                                                                 value.toString() ) );
            }
        }
        cilogg::diagnostics::ActivityJournal::record( QStringLiteral( "command" ),
                                                      summary.join( QLatin1Char( ' ' ) ), data );
    }

    // What a report records about the running application: windows and tabs (with file sizes),
    // running automation and plugin jobs.
    QVariantMap diagnosticContext()
    {
        QVariantList windows;
        for ( const auto& [ windowSession, window ] : mainWindows_ ) {
            Q_UNUSED( windowSession );
            if ( window == nullptr ) {
                continue;
            }
            auto info = window->commanderWindowInfo();
            auto tabs = info.value( QStringLiteral( "tabs" ) ).toList();
            for ( auto& tab : tabs ) {
                auto map = tab.toMap();
                const auto path = map.value( QStringLiteral( "filePath" ) ).toString();
                if ( !path.isEmpty() ) {
                    const QFileInfo file( path );
                    map.insert( QStringLiteral( "fileBytes" ), file.exists() ? file.size() : -1 );
                }
                tab = map;
            }
            info.insert( QStringLiteral( "tabs" ), tabs );
            windows.push_back( info );
        }

        QVariantList activeJobs;
        for ( const auto& job : pluginManager_.jobs() ) {
            const auto map = job.toMap();
            const auto state = map.value( QStringLiteral( "state" ) ).toString();
            if ( state != QLatin1String( "completed" ) && state != QLatin1String( "failed" )
                 && state != QLatin1String( "cancelled" ) ) {
                activeJobs.push_back( map );
            }
        }

        return { { QStringLiteral( "windows" ), windows },
                 { QStringLiteral( "scenario" ), scenarioRunner_.status().payload },
                 { QStringLiteral( "globalScript" ), scriptSupervisor_.globalScriptStatusPayload() },
                 { QStringLiteral( "activePluginJobs" ), activeJobs },
                 { QStringLiteral( "autoResponsesEnabled" ),
                   ActionsManager::instance().autoResponsesEnabled() },
                 { QStringLiteral( "automationMode" ), automationModeEnabled_ } };
    }

    KDSingleApplication singleApplication_;
    std::unique_ptr<CrashHandler> crashHandler_;

    MessageReceiver messageReceiver_;
    cilogg::plugins::PluginManager pluginManager_;
    // Plugin update notices for every window, and the silent daily check (E15c).
    PluginUpdateNotices pluginUpdateNotices_{ &pluginManager_ };
    cilogg::plugins::PluginUpdateChecker pluginUpdateChecker_{
        &pluginManager_, [] { return Configuration::get().pluginUpdateCheckingEnabled(); }
    };
    PluginSourceRuns pluginSourceRuns_{
        [ this ] { return applicationAnalysisSources(); },
        [ this ]( const CommanderRequest& request ) { return invokePluginOnTab( request ); },
        [ this ]( const QString& jobId ) { return pluginJobStatus( jobId ); },
        [ this ]( const QString& path ) { return openFileForSource( path ); }
    };
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
    UpdateUiController updateUiController_;
};

#endif // KLOGG_KLOGGAPP_H
