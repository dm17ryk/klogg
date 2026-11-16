// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_PLUGINSOURCERUNS_H
#define CILOGG_PLUGINSOURCERUNS_H

#include "analysisruns.h"
#include "commander.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <map>

// invoke_plugin with a U15 source (focused, tab:<id>, file:<path> or all). "all" invokes the
// action once per open tab, staged like the analysis docks (AnalysisRuns): at most
// MaxActiveAnalysisRuns of a Run's jobs are unfinished at a time, and a tab refused because the
// plugin manager is full waits for the next attempt, until every tab ran.
class PluginSourceRuns : public QObject {
    Q_OBJECT

public:
    // Every open tab of every window (the analysis source list).
    using Sources = std::function<QVariantList()>;
    // Invokes one plugin action; the request names its tab (tabId) or none (the focused tab).
    using Invoke = std::function<CommanderResult( const CommanderRequest& )>;
    // The job payload ("state", ...) or a failure once the job is gone.
    using JobStatus = std::function<CommanderResult( const QString& jobId )>;
    // Opens a file in a tab of the active window.
    using OpenFile = std::function<CommanderResult( const QString& path )>;

    PluginSourceRuns( Sources sources, Invoke invoke, JobStatus status, OpenFile openFile,
                      QObject* parent = nullptr );

    // Resolves request.source (an explicit tab selector wins) and invokes the action.
    CommanderResult invoke( const CommanderRequest& request );
    // The staged "all" Runs that are still active, then the finished ones (newest last).
    QVariantList runs() const;
    // Polls the jobs and starts waiting tabs; the timer calls it while a Run is active.
    void advance();

private:
    struct Run {
        CommanderRequest request;
        AnalysisRuns sources;
    };

    Sources sources_;
    Invoke invoke_;
    JobStatus status_;
    OpenFile openFile_;
    QTimer timer_;
    std::map<quint64, Run> runs_;
    quint64 nextRun_ = 0;
};

#endif
