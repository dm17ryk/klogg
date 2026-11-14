// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_ANALYSISRUNS_H
#define CILOGG_ANALYSISRUNS_H

#include "commander.h"

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>

#include <functional>
#include <optional>
#include <vector>

// The analysis service admits 10 jobs (2 running, 8 queued) and keeps 26 finished ones for
// the whole application. One Run of a dock keeps at most this many unfinished jobs, so other
// tools can still start theirs...
inline constexpr int MaxActiveAnalysisRuns = 8;
// ...and at most this many finished jobs; older finished results are released and computed
// again when their source is shown.
inline constexpr int MaxRetainedAnalysisRuns = 16;

bool isTerminalAnalysisState( const QString& state );

// A non-negative whole number from automation JSON (an int or an integral double). Fractions,
// booleans, strings and non-finite numbers are rejected.
std::optional<int> analysisRunIndex( const QVariant& value );

// The sources of one Run in an analysis dock ("all open files" runs every open tab). Jobs
// start in source order as the service admits them.
class AnalysisRuns {
public:
    struct Run {
        QString tabId;
        QString label;
        // Empty while the run waits to start, and after its finished job was released.
        QString jobId;
        // "waiting", the job's state, or the reason the job could not start.
        QString state = QStringLiteral( "waiting" );
        bool finished = false;
        // Its finished job was released or expired, so its results must be computed again.
        bool dropped = false;
        // Order in which runs finished; the oldest finished jobs are released first.
        quint64 finishedOrder = 0;
    };

    // Starts the job of one source (payload "jobId"); queue_limit means "try again later".
    using Start = std::function<CommanderResult( const QString& tabId )>;
    // Acts on one job: reads its status (payload "state"), cancels or releases it.
    using JobAction = std::function<CommanderResult( const QString& jobId )>;

    // One waiting run per tab; labels come from the dock's source list.
    void reset( const QStringList& tabIds, const QVariantList& sources );
    void clear();
    bool empty() const;
    int size() const;
    const Run& at( int index ) const;
    // Index of the first run with a job, or 0.
    int firstStarted() const;
    // True while a run waits to start or its job has not finished.
    bool active() const;
    int started() const;

    // Starts waiting runs in order until the service is full or MaxActiveAnalysisRuns of
    // this Run are unfinished. A source that cannot start finishes with the reason; the
    // first such failure is returned.
    CommanderResult startWaiting( const Start& start );
    // Polls unfinished jobs, then releases the oldest finished jobs beyond
    // MaxRetainedAnalysisRuns, never the shown one.
    void update( const JobAction& status, const JobAction& release, int shown );
    // Cancels unfinished jobs; runs still waiting finish as cancelled.
    CommanderResult cancel( const JobAction& cancelJob );
    // Releases every job and forgets the runs.
    void releaseAll( const JobAction& release );
    // Makes a dropped run wait to start again. False if its results were not dropped.
    bool restart( int index );

    // "40 sources: 12 completed, 8 running, 20 waiting. Showing <label>." Empty for one source.
    QString summary( int shown ) const;
    QVariantList toVariantList() const;

private:
    void finish( Run& run, const QString& state );

    std::vector<Run> runs_;
    quint64 finishedCount_ = 0;
};

#endif
