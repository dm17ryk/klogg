// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_VALUECOUNTDOCK_H
#define CILOGG_VALUECOUNTDOCK_H
#include "analysisruns.h"
#include "commander.h"
#include <QDockWidget>
#include <functional>
class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QLabel;
class QTableWidget;
class QPushButton;
class LabTextEdit;

class ValueCountDock final : public QDockWidget {
    Q_OBJECT
public:
    using Executor = std::function<CommanderResult( const CommanderRequest& )>;
    using Sources = std::function<QVariantList()>;
    ValueCountDock( QWidget* parent, Executor executor, Sources sources );
    ~ValueCountDock() override;
    QVariantMap state();
    // `applyTabId` (an explicit Commander tab) receives "apply"; otherwise the tab of the
    // shown counts does.
    CommanderResult setDefinition( const QVariantMap& definition,
                                   const QString& applyTabId = {} );

private:
    QVariantMap draft() const;
    void draftChanged();
    void updateApplyState();
    void refresh();
    void refreshSources();
    CommanderResult run();
    CommanderResult page( const QString& cursor );
    CommanderResult selected( CommanderAction action );
    CommanderResult command( CommanderAction action, QVariantMap arguments = {},
                             const QString& tabId = {} );
    void showFailure( const CommanderResult& result );
    QString sourceSelection() const;
    QString shownTabId() const;
    // The shown counts came from a tab that is no longer open.
    bool shownSourceClosed() const;
    void showRun( int index );
    void releaseRuns();
    // Starts waiting sources, polls running ones and follows the shown source's job.
    void advanceRuns();
    Executor execute_;
    Sources sources_;
    QLineEdit* pattern_;
    QLineEdit* capture_;
    QComboBox* source_;
    QComboBox* resultSource_;
    // One job per source of the last Run ("all open files" runs every open tab).
    AnalysisRuns runs_;
    // The request of the last Run, for the sources that start later.
    QVariantMap runRequest_;
    // An explicit Commander tab for "apply" during setDefinition().
    QString applyTabId_;
    QString previousSource_;
    QComboBox* scope_;
    QComboBox* missing_;
    QComboBox* matchMode_;
    QSpinBox* subpattern_;
    QSpinBox* maxValues_;
    QCheckBox* matchCase_;
    QCheckBox* boolean_;
    QCheckBox* plain_;
    QCheckBox* exclude_;
    QCheckBox* unmatched_;
    QCheckBox* fold_;
    QCheckBox* partial_;
    LabTextEdit* text_;
    QLabel* status_;
    QTableWidget* table_;
    QPushButton* cancel_;
    QPushButton* apply_;
    QPushButton* open_;
    QPushButton* next_;
    QVariantMap limits_;
    QVariantMap extraOptions_;
    QVariant captureOverride_;
    QVariantMap job_;
    QVariantList rows_;
    QVariantList sourceCache_;
    QString id_;
    QString cursor_;
    quint64 revision_ = 0;
    quint64 jobRevision_ = 0;
    bool updating_ = false;
    bool refreshing_ = false;
    bool fetched_ = false;
};
#endif
