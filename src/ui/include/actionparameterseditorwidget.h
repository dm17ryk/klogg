#pragma once

#include <QWidget>

#include "actionsconfig.h"

class QTableWidget;
class QPushButton;

class ActionParametersEditorWidget : public QWidget {
  public:
    explicit ActionParametersEditorWidget( QWidget* parent = nullptr );

    void setFields( const QVector<ActionParameterDefinition>& fields );
    QVector<ActionParameterDefinition> fields() const;

  private:
    int selectedRow() const;
    void refreshTable();
    void addField();
    void editField();
    void deleteField();
    void moveField( int offset );

    QTableWidget* table_ = nullptr;
    QPushButton* editButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QPushButton* moveUpButton_ = nullptr;
    QPushButton* moveDownButton_ = nullptr;
    QVector<ActionParameterDefinition> fields_;
};
