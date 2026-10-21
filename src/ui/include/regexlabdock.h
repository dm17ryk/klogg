// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_REGEXLABDOCK_H
#define CILOGG_REGEXLABDOCK_H
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
    CommanderResult apply();
    void prepareSelection( const QString& tabId );

private:
    void draftChanged();
    QVariantMap draft( bool fullText ) const;
    void refreshSources();
    void refresh( bool requestPage = false );
    CommanderResult run();
    CommanderResult page( const QString& cursor );
    CommanderResult activate( int index );
    CommanderResult command( CommanderAction action, QVariantMap arguments = {} );
    void showFailure( const CommanderResult& result );
    void showDetails( int index );
    Executor execute_;
    Sources sources_;
    Navigate navigate_;
    QLineEdit* pattern_;
    QComboBox* source_;
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
