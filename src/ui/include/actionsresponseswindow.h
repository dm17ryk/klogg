#pragma once

#include <QWidget>

class ActionsTableModel;
class ResponsesTableModel;
class QCheckBox;
class QLineEdit;
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

    void updateWindowSize();
    bool sizeInitialized_ = false;
};
