/*
 * Copyright (C) 2021 Anton Filimonov
 *
 * This file is part of cilogg.
 *
 * cilogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * cilogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cilogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QUrl>
#include <deque>
#include <qglobal.h>
#include <qthreadpool.h>
#include <atomic>
#include <string>
#include <tbb/version.h>

#include "klogg_version.h"
#include "log.h"

#include "issuereporter.h"

static constexpr auto DiagnosticsHeader = "#### Diagnostics\n\n";

static constexpr auto DetailsFooter = "- CILogg version: `%1` (built on `%2` from commit `%3`)\n"
                                      "- Build ABI: `%4`\n"
                                      "- OS: `%5`\n"
                                      "- Kernel: `%6 %7`\n"
                                      "- CPU architecture: `%8`\n"
                                      "- Concurrency: `%9`\n";

static constexpr auto LibraryVersionsFooter = "- Qt: `%1`\n"
                                              "- TBB: `%2`\n";

static constexpr auto CrashTemplate = "#### What were you doing when CILogg crashed?\n\n- \n\n"

                                      "#### Crash id\n\n"
                                      "`%1`\n\n";

static constexpr auto CrashReportFooter
    = "\n#### Local crash report\n\n"
      "A local crash report was generated at:\n\n"
      "`%1`\n\n"
      "Please review that report locally and paste any relevant stack trace snippets below.\n";

static constexpr auto ExceptionTemplate = "#### What did you do?\n\n- \n\n"

                                          "#### What happened?\n\n- \n\n"
                                          "#### Exception\n\n"
                                          "```\n%1\n```\n\n";

static constexpr auto BugTemplate = "#### What did you do?\n\n- \n\n"
                                    "#### What did you expect to see?\n\n- \n\n"
                                    "#### What did you see instead?\n\n- \n\n";

static constexpr auto ExceptionAskUserAction
    = "Ooops! Something unexpected happened. Create an issue on GitHub?";

static constexpr auto AskUserAction = "Create an issue on GitHub?";

namespace {
QString encodeQueryValue( const QString& value )
{
    return QString::fromLatin1( QUrl::toPercentEncoding( value ) );
}

std::atomic_bool interactiveReports{ true };

bool canShowDialogs()
{
    return interactiveReports.load()
           && QGuiApplication::platformName() != QLatin1String( "offscreen" )
           && QGuiApplication::platformName() != QLatin1String( "minimal" );
}

void openIssue( const QString& title, const QString& body )
{
    QStringList queryItems;
    queryItems << "template=bug_report.md" << "title=" + encodeQueryValue( title )
               << "body=" + encodeQueryValue( body );
    QDesktopServices::openUrl( QUrl( QStringLiteral( "https://github.com/dm17ryk/klogg/issues/new?%1" )
                                         .arg( queryItems.join( '&' ) ) ) );
}

void openFolder( const QString& directory )
{
    if ( !directory.isEmpty() ) {
        QDesktopServices::openUrl( QUrl::fromLocalFile( directory ) );
    }
}

void showReportNotice( const cilogg::diagnostics::ReportResult& result,
                       const cilogg::diagnostics::ReportRequest& request, int repeatsBefore )
{
    using cilogg::diagnostics::DiagnosticReports;

    auto text = result.summary;
    if ( repeatsBefore > 0 ) {
        text += QStringLiteral( "\n\nThe same problem happened %1 more time(s) since the previous "
                                "report." )
                    .arg( repeatsBefore );
    }
    text += result.ok || !result.directory.isEmpty()
                ? QStringLiteral( "\n\nA report with the steps that led here was saved:\n%1" )
                      .arg( QDir::toNativeSeparators( result.archive.isEmpty() ? result.directory
                                                                               : result.archive ) )
                : QStringLiteral( "\n\nThe report could not be saved: %1" ).arg( result.error );
    // R06b: a dump is never in the zip, and it must not go into the public issue.
    auto dumps = request.privateFiles;
    if ( !result.processDump.isEmpty() ) {
        dumps.push_back( QDir::toNativeSeparators( result.processDump ) );
    }
    if ( !dumps.isEmpty() ) {
        text += QStringLiteral( "\n\nMemory dump (not in the zip):\n%1\n%2" )
                    .arg( dumps.join( QLatin1Char( '\n' ) ), DiagnosticReports::dumpPrivacyNote() );
    }

    // One notice at a time: further problems are still journaled and reported, not shown again.
    static QPointer<QMessageBox> openNotice;
    if ( openNotice ) {
        return;
    }

    auto* box = new QMessageBox( QMessageBox::Warning, QStringLiteral( "CILogg" ), text,
                                 QMessageBox::NoButton, nullptr );
    openNotice = box;
    box->setAttribute( Qt::WA_DeleteOnClose );
    box->setAttribute( Qt::WA_QuitOnClose, false ); // an open notice never keeps CILogg running
    box->setWindowModality( Qt::NonModal );
    auto* issueButton
        = box->addButton( QObject::tr( "Create GitHub issue" ), QMessageBox::ActionRole );
    auto* folderButton
        = box->addButton( QObject::tr( "Open report folder" ), QMessageBox::ActionRole );
    box->addButton( QMessageBox::Close );
    folderButton->setEnabled( !result.directory.isEmpty() );

    const auto directory = result.directory;
    const auto title = DiagnosticReports::issueTitle( result, request );
    const auto body = DiagnosticReports::issueBody( result, request );
    QObject::connect( box, &QMessageBox::buttonClicked, box,
                      [ issueButton, folderButton, directory, title, body ]( QAbstractButton* button ) {
                          if ( button == issueButton ) {
                              openFolder( directory );
                              openIssue( title, body );
                          }
                          else if ( button == folderButton ) {
                              openFolder( directory );
                          }
                      } );
    box->show();
}
} // namespace

void IssueReporter::setInteractive( bool interactive )
{
    interactiveReports = interactive;
}

void IssueReporter::reportException( const cilogg::diagnostics::ReportRequest& request )
{
    using namespace cilogg::diagnostics;
    Q_ASSERT( QCoreApplication::instance() == nullptr
              || QThread::currentThread() == QCoreApplication::instance()->thread() );

    static ReportThrottle throttle;
    ActivityJournal::record( QStringLiteral( "error" ),
                             request.operation.isEmpty()
                                 ? request.error
                                 : QStringLiteral( "%1: %2" ).arg( request.operation, request.error ),
                             request.details );

    const auto key = request.operation + QLatin1Char( '|' ) + request.error;
    int repeatsBefore = 0;
    if ( !throttle.shouldReport( key, QDateTime::currentDateTimeUtc(), &repeatsBefore ) ) {
        LOG_WARNING << "Problem repeated (" << throttle.suppressed( key )
                    << " times since the last report): " << request.error;
        return;
    }

    // Many different problems at once (for example every open tab failing): at most 10 reports in
    // 10 minutes in all, so the report folder and the GUI thread are not flooded.
    static std::deque<QDateTime> recentReports;
    const auto now = QDateTime::currentDateTimeUtc();
    while ( !recentReports.empty() && recentReports.front().msecsTo( now ) > 10 * 60 * 1000 ) {
        recentReports.pop_front();
    }
    if ( recentReports.size() >= 10 ) {
        LOG_WARNING << "Too many problem reports in 10 minutes; not writing one for: "
                    << request.error;
        return;
    }
    recentReports.push_back( now );

    auto reportRequest = request;
    if ( repeatsBefore > 0 ) {
        reportRequest.details.insert( QStringLiteral( "repeatsSincePreviousReport" ), repeatsBefore );
    }
    const auto result = DiagnosticReports::create( reportRequest );
    if ( canShowDialogs() ) {
        showReportNotice( result, reportRequest, repeatsBefore );
    }
}

void IssueReporter::reportPendingCrashes()
{
    using namespace cilogg::diagnostics;
    const auto crashes = DiagnosticReports::reportPendingCrashes();
    for ( const auto& crash : crashes ) {
        ActivityJournal::record(
            QStringLiteral( "error" ),
            QStringLiteral( "An earlier run crashed: %1" ).arg( crash.request.error ) );
    }
    // The newest crash gets the notice; every crash has its own report.
    if ( !crashes.isEmpty() && canShowDialogs() ) {
        showReportNotice( crashes.back().result, crashes.back().request, 0 );
    }
}

void IssueReporter::reportProblem()
{
    using namespace cilogg::diagnostics;

    ReportRequest request;
    request.kind = QStringLiteral( "bug" );
    request.operation = QStringLiteral( "Report a problem (Help menu)" );
    const auto result = DiagnosticReports::create( request );
    if ( !result.directory.isEmpty() ) {
        openFolder( result.directory );
        openIssue( DiagnosticReports::issueTitle( result, request ),
                   DiagnosticReports::issueBody( result, request ) );
        return;
    }
    reportIssue( IssueTemplate::Bug );
}

void IssueReporter::askUserAndReportIssue( IssueTemplate issueTemplate, const QString& information,
                                           const QString& localReportPath )
{
    const auto askAction
        = issueTemplate == IssueTemplate::Exception ? ExceptionAskUserAction : AskUserAction;

    if ( QMessageBox::Yes
         == QMessageBox::question( nullptr, "CILogg", askAction, QMessageBox::Yes,
                                   QMessageBox::No ) ) {
        IssueReporter::reportIssue( issueTemplate, information, localReportPath );
    }
}

void IssueReporter::reportIssue( IssueTemplate issueTemplate, const QString& information,
                                 const QString& localReportPath )
{
    QString body;
    QString title;
    switch ( issueTemplate ) {
    case IssueTemplate::Bug:
        body.append( BugTemplate );
        title = "Bug report";
        break;
    case IssueTemplate::Crash:
        body.append( QString( CrashTemplate ).arg( information ) );
        title = QString( "Crash report: %1" ).arg( information );
        break;
    case IssueTemplate::Exception:
        body.append( QString( ExceptionTemplate ).arg( information ) );
        title = "Exception report";
        break;
    }

    body.append( DiagnosticsHeader );

    const auto version = kloggVersion();
    const auto buildDate = kloggBuildDate();
    const auto commit = kloggCommit();

    const auto os = QSysInfo::prettyProductName();
    const auto kernelType = QSysInfo::kernelType();
    const auto kernelVersion = QSysInfo::kernelVersion();
    const auto arch = QSysInfo::currentCpuArchitecture();
    const auto builtAbi = QSysInfo::buildAbi();

    const auto concurrency = QThreadPool::globalInstance()->maxThreadCount();

    body.append( QString( DetailsFooter )
                     .arg( version, buildDate, commit, builtAbi, os, kernelType, kernelVersion,
                           arch, std::to_string( concurrency ).c_str() ) );
    body.append( QString( LibraryVersionsFooter ).arg( qVersion(), TBB_runtime_version() ) );
    body.append( '\n' );

    if ( issueTemplate == IssueTemplate::Crash && !localReportPath.isEmpty() ) {
        body.append( QString( CrashReportFooter ).arg( localReportPath ) );
    }

    QStringList queryItems;
    queryItems << "template=bug_report.md" << "title=" + encodeQueryValue( title )
               << "body=" + encodeQueryValue( body );

    QUrl url( QStringLiteral( "https://github.com/dm17ryk/klogg/issues/new?%1" )
                  .arg( queryItems.join( '&' ) ) );
    QDesktopServices::openUrl( url );
}
