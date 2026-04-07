#pragma once

#include <QWidget>

class ActionsTableModel;
class ResponsesTableModel;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;

class ActionsResponsesWindow : public QWidget {
    Q_OBJECT
  public:
    explicit ActionsResponsesWindow( QWidget* parent = nullptr );

    void setSendAvailable( bool available );

  Q_SIGNALS:
    void sendActionRequested( int actionId );

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

  private:
    ActionsTableModel* actionsModel_ = nullptr;
    ResponsesTableModel* responsesModel_ = nullptr;
    QSortFilterProxyModel* actionsProxy_ = nullptr;
    QSortFilterProxyModel* responsesProxy_ = nullptr;
    QLineEdit* actionsFilter_ = nullptr;
    QLineEdit* responsesFilter_ = nullptr;
    QTableView* actionsTable_ = nullptr;
    QTableView* responsesTable_ = nullptr;
    QCheckBox* autoResponsesCheck_ = nullptr;
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
};
