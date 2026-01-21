#pragma once

#include <QDialog>

#include "actionsconfig.h"

class ActionsImportTableModel;
class ResponsesImportTableModel;
class QDialogButtonBox;
class QPushButton;
class QTableView;
class QToolButton;

class ImportActionsDialog : public QDialog {
    Q_OBJECT

  public:
    explicit ImportActionsDialog( QWidget* parent = nullptr );

  private Q_SLOTS:
    void importActions();
    void persistChanges();
    void removeSelectedAction();
    void clearActions();
    void removeSelectedResponse();
    void clearResponses();

  private:
    void updateButtons();
    void updateDialogSize();

    QVector<ActionDefinition> actions_;
    QVector<ResponseDefinition> responses_;
    QTableView* actionsTable_ = nullptr;
    QTableView* responsesTable_ = nullptr;
    ActionsImportTableModel* actionsModel_ = nullptr;
    ResponsesImportTableModel* responsesModel_ = nullptr;
    QDialogButtonBox* buttonBox_ = nullptr;
    QPushButton* importButton_ = nullptr;
    QToolButton* removeActionButton_ = nullptr;
    QToolButton* clearActionsButton_ = nullptr;
    QToolButton* removeResponseButton_ = nullptr;
    QToolButton* clearResponsesButton_ = nullptr;
    int pendingActionSelectionRow_ = -1;
    int pendingResponseSelectionRow_ = -1;
    bool suppressPersist_ = false;
    bool sizeInitialized_ = false;
};
