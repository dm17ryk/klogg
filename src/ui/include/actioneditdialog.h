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
    bool eventFilter( QObject* watched, QEvent* event ) override;

  private:
    void populateFromAction( const ActionDefinition& action );
    void syncHexFromString();
    void syncStringFromHex();
    void setSequenceEditors( const ActionSequence& sequence );

    ActionDefinition action_;
    QLineEdit* nameEdit_ = nullptr;
    QPlainTextEdit* descriptionEdit_ = nullptr;
    QComboBox* sequenceTypeCombo_ = nullptr;
    QComboBox* lineEndingCombo_ = nullptr;
    QPlainTextEdit* stringValueEdit_ = nullptr;
    QPlainTextEdit* hexValueEdit_ = nullptr;
    QSpinBox* delaySpin_ = nullptr;
    QSpinBox* repeatCountSpin_ = nullptr;
    QSpinBox* repeatIntervalSpin_ = nullptr;
    QLineEdit* variableNamesEdit_ = nullptr;
    QCheckBox* checksumEnabledCheck_ = nullptr;
    QComboBox* checksumAlgorithmCombo_ = nullptr;
    QLineEdit* checksumPlaceholderEdit_ = nullptr;
    bool syncingSequenceEditors_ = false;
};
