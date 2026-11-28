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

#ifndef KLOGG_ISSUE_REPORTER_H
#define KLOGG_ISSUE_REPORTER_H

#include <QString>

#include "diagnostics.h"

enum class IssueTemplate { Crash, Exception, Bug };

class IssueReporter {
  public:
    static void askUserAndReportIssue( IssueTemplate issueTemplate,
                                       const QString& information = {},
                                       const QString& localReportPath = {} );
    static void reportIssue( IssueTemplate issueTemplate, const QString& information = {},
                             const QString& localReportPath = {} );

    // An error caught in a background operation (GUI thread only). Journals it, writes a report
    // bundle (at most one per problem every 10 minutes) and shows a non-modal notice offering to
    // open the report folder or create a GitHub issue. In automation or headless runs only the
    // report is written.
    static void reportException( const cilogg::diagnostics::ReportRequest& request );

    // Help > Report a problem: writes a report bundle, opens its folder and a prefilled issue.
    static void reportProblem();

    // R06b, at startup (GUI thread): a report for each crash an earlier run left, and a notice for
    // the newest one (never in automation). The dump stays outside the report's zip.
    static void reportPendingCrashes();

    static void setInteractive( bool interactive );
};

#endif
