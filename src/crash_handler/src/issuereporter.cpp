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

#include <QDesktopServices>
#include <QMessageBox>
#include <QUrl>
#include <qglobal.h>
#include <qthreadpool.h>
#include <string>
#include <tbb/version.h>

#include "klogg_version.h"

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
} // namespace

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
