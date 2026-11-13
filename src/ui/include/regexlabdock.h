// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_REGEXLABDOCK_H
#define CILOGG_REGEXLABDOCK_H
#include "analysisruns.h"
#include "commander.h"
#include <QDockWidget>
#include <QVariantMap>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

class RegexLabDock final : public QDockWidget {
    Q_OBJECT
public:
    using Executor = std::function<CommanderResult( const CommanderRequest& )>;
    using Sources = std::function<QVariantList()>;
    using Navigate = std::function<CommanderResult( const QString&, quint64 )>;
    RegexLabDock( QWidget* parent, Executor executor, Sources sources, Navigate navigate );
    ~RegexLabDock() override;
    QVariantMap state();
    CommanderResult setDefinition( const QVariantMap& definition );
    // Applies the pattern to `tabId`, or to the tab of the shown results when it is empty.
    CommanderResult apply( const QString& tabId = {} );
    void prepareSelection( const QString& tabId );

private:
    void draftChanged();
    QVariantMap draft( bool fullText ) const;
    void refreshSources();
    void refresh( bool requestPage = false );
    CommanderResult run();
    CommanderResult page( const QString& cursor );
    CommanderResult activate( int index );
    CommanderResult command( CommanderAction action, QVariantMap arguments = {},
                             const QString& tabId = {} );
    void showFailure( const CommanderResult& result );
    void showDetails( int index );
    QString sourceSelection() const;
    void showRun( int index );
    void releaseRuns();
    // Starts waiting sources, polls running ones and follows the shown source's job.
    void advanceRuns();
    Executor execute_;
    Sources sources_;
    Navigate navigate_;
    QLineEdit* pattern_;
    QComboBox* source_;
    QComboBox* resultSource_;
    // One job per source of the last Run ("all open files" runs every open tab).
    AnalysisRuns runs_;
    // The request of the last Run, for the sources that start later.
    QVariantMap runRequest_;
    QString previousSource_;
    QComboBox* scope_;
    QComboBox* matchMode_;
    QCheckBox* case_;
    QCheckBox* boolean_;
    QCheckBox* plain_;
    QCheckBox* exclude_;
    QCheckBox* unmatched_;
    QPlainTextEdit* text_;
    QSpinBox* batchBudget_;
    QSpinBox* jobBudget_;
    QPushButton* run_;
    QPushButton* cancel_;
    QPushButton* apply_;
    QPushButton* next_;
    QLabel* status_;
    QTableWidget* table_;
    QPlainTextEdit* details_;
    QTimer* timer_;
    quint64 revision_ = 0;
    quint64 jobRevision_ = 0;
    QString jobId_;
    QString pageCursor_;
    QString nextCursor_;
    int fetchedRecords_ = -1;
    bool fetchedTerminal_ = false;
    QVariantList rows_;
    QVariantList sourceCache_;
    QVariantMap jobStatus_;
    QVariantMap extraLimits_;
    bool updating_ = false;
    bool refreshing_ = false;
};
#endif
