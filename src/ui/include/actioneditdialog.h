#pragma once

#include <QDialog>

#include "actionsconfig.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

class ActionEditDialog : public QDialog {
    Q_OBJECT
  public:
    explicit ActionEditDialog( QWidget* parent = nullptr );

    void setAction( const ActionDefinition& action );
    ActionDefinition action() const;

  protected:
    void accept() override;

  private:
    void populateFromAction( const ActionDefinition& action );

    ActionDefinition action_;
    QLineEdit* nameEdit_ = nullptr;
    QPlainTextEdit* descriptionEdit_ = nullptr;
    QComboBox* sequenceTypeCombo_ = nullptr;
    QPlainTextEdit* sequenceValueEdit_ = nullptr;
    QSpinBox* delaySpin_ = nullptr;
    QSpinBox* repeatCountSpin_ = nullptr;
    QSpinBox* repeatIntervalSpin_ = nullptr;
    QLineEdit* variableNamesEdit_ = nullptr;
    QCheckBox* checksumEnabledCheck_ = nullptr;
    QComboBox* checksumAlgorithmCombo_ = nullptr;
    QLineEdit* checksumPlaceholderEdit_ = nullptr;
};
