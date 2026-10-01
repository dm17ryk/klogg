#pragma once

#include <QWidget>
#include <QVariantMap>

class ActionsTableModel;
class ActionParametersDialog;
class QLabel;
class ResponsesTableModel;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QSplitter;
class QTableView;

class ActionsResponsesWindow : public QWidget {
    Q_OBJECT
  public:
    explicit ActionsResponsesWindow( QWidget* parent = nullptr );

    // The same editor implementation is used for the two user-facing tools.
    // In response-only mode the action list is not shown, so responses remain
    // an editor opened independently from the Actions window.
    void setResponsesOnly( bool responsesOnly );

    void setSendAvailable( bool available );

  Q_SIGNALS:
    void sendActionRequested( int actionId );
    void sendActionWithParametersRequested( int actionId, const QVariantMap& parameters );

  private Q_SLOTS:
    void refreshActions();
    void refreshResponses();
    void addAction();
    void editSelectedAction();
    void duplicateSelectedAction();
    void deleteSelectedAction();
    void moveSelectedActionUp();
    void moveSelectedActionDown();
    void addResponse();
    void editSelectedResponse();
    void duplicateSelectedResponse();
    void deleteSelectedResponse();
    void moveSelectedResponseUp();
    void moveSelectedResponseDown();
    void updateActionParametersPanel();
    void sendSelectedAction();

  private:
    ActionsTableModel* actionsModel_ = nullptr;
    ResponsesTableModel* responsesModel_ = nullptr;
    QSortFilterProxyModel* actionsProxy_ = nullptr;
    QSortFilterProxyModel* responsesProxy_ = nullptr;
    QLineEdit* actionsFilter_ = nullptr;
    QLineEdit* responsesFilter_ = nullptr;
    QTableView* actionsTable_ = nullptr;
    QTableView* responsesTable_ = nullptr;
    QWidget* actionsPanel_ = nullptr;
    QWidget* responsesPanel_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QCheckBox* autoResponsesCheck_ = nullptr;
    QWidget* actionParametersPanel_ = nullptr;
    ActionParametersDialog* actionParametersEditor_ = nullptr;
    QLabel* actionParametersHint_ = nullptr;
    QPushButton* sendSelectedActionButton_ = nullptr;
    int actionParametersActionId_ = -1;
    QPushButton* editActionButton_ = nullptr;
    QPushButton* duplicateActionButton_ = nullptr;
    QPushButton* deleteActionButton_ = nullptr;
    QPushButton* moveActionUpButton_ = nullptr;
    QPushButton* moveActionDownButton_ = nullptr;
    QPushButton* editResponseButton_ = nullptr;
    QPushButton* duplicateResponseButton_ = nullptr;
    QPushButton* deleteResponseButton_ = nullptr;
    QPushButton* moveResponseUpButton_ = nullptr;
    QPushButton* moveResponseDownButton_ = nullptr;

    void updateWindowSize();
    void updateActionButtons();
    void updateResponseButtons();
    int selectedActionRow() const;
    int selectedResponseRow() const;
    bool sizeInitialized_ = false;
    bool responsesOnly_ = false;
};
