#pragma once

#include <QDialog>

#include "actionsconfig.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;

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
    void populateActionCombo( const QVector<ActionDefinition>& actions );
    void populateFromResponse( const ResponseDefinition& response );

    ResponseDefinition response_;
    QVector<ActionDefinition> actions_;
    QLineEdit* nameEdit_ = nullptr;
    QPlainTextEdit* descriptionEdit_ = nullptr;
    QComboBox* matchTypeCombo_ = nullptr;
    QPlainTextEdit* matchValueEdit_ = nullptr;
    QComboBox* actionCombo_ = nullptr;
    QCheckBox* inlineActionCheck_ = nullptr;
    QComboBox* inlineTypeCombo_ = nullptr;
    QPlainTextEdit* inlineValueEdit_ = nullptr;
    QPlainTextEdit* commentEdit_ = nullptr;
    QCheckBox* linebreakCheck_ = nullptr;
    QCheckBox* timestampCheck_ = nullptr;
    QCheckBox* snapshotCheck_ = nullptr;
    QCheckBox* stopCommunicationCheck_ = nullptr;
};
