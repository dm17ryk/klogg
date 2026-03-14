#pragma once

#include <QDialog>

#include "actionsconfig.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;

class ResponseEditDialog : public QDialog {
    Q_OBJECT
  public:
    explicit ResponseEditDialog( QWidget* parent = nullptr );

    void setResponse( const ResponseDefinition& response,
                      const QVector<ActionDefinition>& actions );
    ResponseDefinition response() const;

  protected:
    void accept() override;

  private:
    void populateFromResponse( const ResponseDefinition& response );
    void setLiteralEditors( const ResponseMatchDefinition& match );
    void syncHexFromString();
    void syncStringFromHex();
    void updateMatchEditorMode();
    void refreshStepTable();
    int selectedStepRow() const;
    void addStep();
    void editSelectedStep();
    void deleteSelectedStep();
    void moveSelectedStep( int offset );

    ResponseDefinition response_;
    QVector<ActionDefinition> actions_;
    QLineEdit* nameEdit_ = nullptr;
    QPlainTextEdit* descriptionEdit_ = nullptr;
    QComboBox* matchTypeCombo_ = nullptr;
    QStackedWidget* matchEditorStack_ = nullptr;
    QPlainTextEdit* stringValueEdit_ = nullptr;
    QPlainTextEdit* hexValueEdit_ = nullptr;
    QPlainTextEdit* expressionValueEdit_ = nullptr;
    QTableWidget* stepsTable_ = nullptr;
    QPushButton* addStepButton_ = nullptr;
    QPushButton* editStepButton_ = nullptr;
    QPushButton* deleteStepButton_ = nullptr;
    QPushButton* moveStepUpButton_ = nullptr;
    QPushButton* moveStepDownButton_ = nullptr;
    QCheckBox* inlineActionCheck_ = nullptr;
    QComboBox* inlineTypeCombo_ = nullptr;
    QPlainTextEdit* inlineValueEdit_ = nullptr;
    QPlainTextEdit* commentEdit_ = nullptr;
    QCheckBox* linebreakCheck_ = nullptr;
    QCheckBox* timestampCheck_ = nullptr;
    QCheckBox* snapshotCheck_ = nullptr;
    QCheckBox* stopCommunicationCheck_ = nullptr;
    QVector<ResponseActionStep> steps_;
    bool syncingLiteralEditors_ = false;
};
