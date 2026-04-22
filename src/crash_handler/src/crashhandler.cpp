/*
 * Copyright (C) 2020, 2021 Anton Filimonov and other contributors
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

#include "crashhandler.h"

#include <QByteArray>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdint>
#include <cstdlib>
#include <qthreadpool.h>
#include <string_view>

#ifdef KLOGG_USE_MIMALLOC
#include <mimalloc.h>
#endif

#include "client/crash_report_database.h"
#include "sentry.h"

#include "cpu_info.h"
#include "issuereporter.h"
#include "klogg_version.h"
#include "log.h"
#include "memory_info.h"
#include "openfilehelper.h"

namespace {

constexpr auto PrivacyPolicyUrl
    = "https://github.com/dm17ryk/klogg/blob/master/website/content/docs/privacy_policy/_index.md";

QString sentryDatabasePath()
{
#ifdef KLOGG_PORTABLE
    auto basePath = QCoreApplication::applicationDirPath();
#else
    auto basePath = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
#endif

    return basePath.append( "/cilogg_dump" );
}

QString processedReportMarkerPath( const QString& reportPath )
{
    return reportPath + ".github-reported";
}

bool isReportAlreadyProcessed( const QString& reportPath )
{
    return QFile::exists( processedReportMarkerPath( reportPath ) );
}

void markReportProcessed( const QString& reportPath )
{
    QFile marker{ processedReportMarkerPath( reportPath ) };
    if ( marker.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) ) {
        marker.write( "reported-on-github\n" );
    }
}

void clearProcessedMarker( const QString& reportPath )
{
    QFile::remove( processedReportMarkerPath( reportPath ) );
}

void logSentry( sentry_level_t level, const char* message, va_list args, void* userdata )
{
#if defined __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif

    Q_UNUSED( userdata );
    QString formattedMessage;
    switch ( level ) {
    case SENTRY_LEVEL_WARNING:
        qWarning( message, args );
        break;
    case SENTRY_LEVEL_ERROR:
        qCritical( message, args );
        break;
    default:
        qInfo( message, args );
        break;
    }

#if defined __clang__
#pragma clang diagnostic pop
#elif defined __GNUC__
#pragma GCC diagnostic pop
#endif
}

QDialog::DialogCode askUserConfirmation( const QString& formattedReport, const QString& reportPath )
{
    auto message = std::make_unique<QLabel>();
    message->setText( "During last run application has encountered an unexpected error." );

    auto crashReportHeader = std::make_unique<QLabel>();
    crashReportHeader->setText( "We collected the following crash report:" );

    auto report = std::make_unique<QPlainTextEdit>();
    report->setReadOnly( true );
    report->setPlainText( formattedReport );
    report->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );

    auto sendReportLabel = std::make_unique<QLabel>();
    sendReportLabel->setText( "Application can open a prefilled GitHub issue so you can report "
                              "this crash to the maintainer." );

    auto privacyPolicy = std::make_unique<QLabel>();
    privacyPolicy->setText( QString( "<a href=\"%1\">Privacy policy</a>" ).arg( PrivacyPolicyUrl ) );

    privacyPolicy->setTextFormat( Qt::RichText );
    privacyPolicy->setTextInteractionFlags( Qt::TextBrowserInteraction );
    privacyPolicy->setOpenExternalLinks( true );

    auto exploreButton = std::make_unique<QPushButton>();
    exploreButton->setText( "Open report directory" );
    exploreButton->setFlat( true );
    QObject::connect( exploreButton.get(), &QPushButton::clicked,
                      [ &reportPath ] { showPathInFileExplorer( reportPath ); } );

    auto privacyLayout = std::make_unique<QHBoxLayout>();
    privacyLayout->addWidget( privacyPolicy.release() );
    privacyLayout->addStretch();
    privacyLayout->addWidget( exploreButton.release() );

    auto buttonBox = std::make_unique<QDialogButtonBox>();
    buttonBox->addButton( "Report on GitHub", QDialogButtonBox::AcceptRole );
    buttonBox->addButton( "Discard report", QDialogButtonBox::RejectRole );

    auto confirmationDialog = std::make_unique<QDialog>();
    confirmationDialog->resize( 800, 600 );

    QObject::connect( buttonBox.get(), &QDialogButtonBox::accepted, confirmationDialog.get(),
                      &QDialog::accept );
    QObject::connect( buttonBox.get(), &QDialogButtonBox::rejected, confirmationDialog.get(),
                      &QDialog::reject );

    auto layout = std::make_unique<QVBoxLayout>();
    layout->addWidget( message.release() );
    layout->addWidget( crashReportHeader.release() );
    layout->addWidget( report.release() );
    layout->addWidget( sendReportLabel.release() );
    layout->addLayout( privacyLayout.release() );
    layout->addWidget( buttonBox.release() );

    confirmationDialog->setLayout( layout.release() );

    return static_cast<QDialog::DialogCode>( confirmationDialog->exec() );
}

void checkCrashpadReports( const QString& databasePath )
{
    using namespace crashpad;

#ifdef Q_OS_WIN
    auto database = CrashReportDatabase::InitializeWithoutCreating(
        base::FilePath{ databasePath.toStdWString() } );
#else
    auto database = CrashReportDatabase::InitializeWithoutCreating(
        base::FilePath{ databasePath.toStdString() } );
#endif

    std::vector<CrashReportDatabase::Report> pendingReports;
    database->GetCompletedReports( &pendingReports );
    LOG_INFO << "Pending reports " << pendingReports.size();

#ifdef Q_OS_WIN
    const auto stackwalker = QCoreApplication::applicationDirPath() + "/cilogg_minidump_dump.exe";
#else
    const auto stackwalker = QCoreApplication::applicationDirPath() + "/cilogg_minidump_dump";
#endif

    for ( const auto& report : pendingReports ) {
        if ( report.uploaded ) {
            continue;
        }

#ifdef Q_OS_WIN
        const auto reportFile = QString::fromStdWString( report.file_path.value() );
#else
        const auto reportFile = QString::fromStdString( report.file_path.value() );
#endif

        if ( isReportAlreadyProcessed( reportFile ) ) {
            continue;
        }

        QProcess stackProcess;
        stackProcess.start( stackwalker, QStringList() << reportFile );
        stackProcess.waitForFinished();

        QString formattedReport = reportFile;
        formattedReport.append( QChar::LineFeed )
            .append( QString::fromUtf8( stackProcess.readAllStandardOutput() ) );

        if ( QDialog::Accepted == askUserConfirmation( formattedReport, reportFile ) ) {
            IssueReporter::reportIssue( IssueTemplate::Crash, report.uuid.ToString().c_str(),
                                        reportFile );
            markReportProcessed( reportFile );
        }
        else {
            database->DeleteReport( report.uuid );
            clearProcessedMarker( reportFile );
        }
    }
}
} // namespace

CrashHandler::CrashHandler()
{
    const auto dumpPath = sentryDatabasePath();
    const auto hasDumpDir = QDir{ dumpPath }.mkpath( "." );

    if ( hasDumpDir ) {
        checkCrashpadReports( dumpPath );
    }

    sentry_options_t* sentryOptions = sentry_options_new();

    sentry_options_set_logger( sentryOptions, logSentry, nullptr );
    sentry_options_set_debug( sentryOptions, 1 );

#ifdef Q_OS_WIN
    const auto handlerPath = QCoreApplication::applicationDirPath() + "/cilogg_crashpad_handler.exe";
    sentry_options_set_database_pathw( sentryOptions, dumpPath.toStdWString().c_str() );
    sentry_options_set_handler_pathw( sentryOptions, handlerPath.toStdWString().c_str() );
#else
    const auto handlerPath = QCoreApplication::applicationDirPath() + "/cilogg_crashpad_handler";
    sentry_options_set_database_path( sentryOptions, dumpPath.toStdString().c_str() );
    sentry_options_set_handler_path( sentryOptions, handlerPath.toStdString().c_str() );
#endif

    // No DSN is configured: crash dumps stay local and reporting goes through GitHub.
    sentry_options_set_require_user_consent( sentryOptions, true );

    sentry_options_set_auto_session_tracking( sentryOptions, false );

    sentry_options_set_symbolize_stacktraces( sentryOptions, true );

    sentry_options_set_environment( sentryOptions, "development" );
    sentry_options_set_release( sentryOptions, kloggVersion().data() );

    sentry_init( sentryOptions );

    sentry_set_tag( "commit", kloggCommit().data() );
    sentry_set_tag( "qt", qVersion() );
    sentry_set_tag( "build_arch", QSysInfo::buildCpuArchitecture().toLatin1().data() );

    auto addExtra = []( const char* name, auto value ) {
        sentry_set_extra( name, sentry_value_new_string( std::to_string( value ).c_str() ) );
        LOG_INFO << "Process stats: " << name << " - " << value;
    };

    addExtra( "memory", physicalMemory() );

    addExtra( "cpuInstructions", static_cast<unsigned>( supportedCpuInstructions() ) );
    addExtra( "concurrency", QThreadPool::globalInstance()->maxThreadCount() );

    memoryUsageTimer_ = std::make_unique<QTimer>();
    QObject::connect( memoryUsageTimer_.get(), &QTimer::timeout, [ addExtra ]() {
        const auto vmUsed = usedMemory();
        addExtra( "vm_used", vmUsed );

#ifdef KLOGG_USE_MIMALLOC
        size_t elapsedMsecs, userMsecs, systemMsecs, currentRss, peakRss, currentCommit, peakCommit,
            pageFaults;

        mi_process_info( &elapsedMsecs, &userMsecs, &systemMsecs, &currentRss, &peakRss,
                         &currentCommit, &peakCommit, &pageFaults );

        addExtra( "elapsed_msecs", elapsedMsecs );
        addExtra( "user_msecs", userMsecs );
        addExtra( "system_msecs", systemMsecs );
        addExtra( "current_rss", currentRss );
        addExtra( "peak_rss", peakRss );
        addExtra( "current_commit", currentCommit );
        addExtra( "peak_commit", peakCommit );
        addExtra( "page_faults", pageFaults );
#endif
    } );
    memoryUsageTimer_->start( 10000 );
}

CrashHandler::~CrashHandler()
{
    memoryUsageTimer_->stop();
    sentry_shutdown();
}
