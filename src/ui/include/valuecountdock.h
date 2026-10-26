// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_VALUECOUNTDOCK_H
#define CILOGG_VALUECOUNTDOCK_H
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
    CommanderResult setDefinition( const QVariantMap& definition );

private:
    QVariantMap draft() const;
    void draftChanged();
    void updateApplyState();
    void refresh();
    void refreshSources();
    CommanderResult run();
    CommanderResult page( const QString& cursor );
    CommanderResult selected( CommanderAction action );
    CommanderResult command( CommanderAction action, QVariantMap arguments = {} );
    void showFailure( const CommanderResult& result );
    Executor execute_;
    Sources sources_;
    QLineEdit* pattern_;
    QLineEdit* capture_;
    QComboBox* source_;
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
