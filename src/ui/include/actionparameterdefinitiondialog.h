#pragma once

#include <QDialog>

#include "actionsconfig.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;

class ActionParameterDefinitionDialog : public QDialog {
  public:
    explicit ActionParameterDefinitionDialog( QWidget* parent = nullptr );

    void setDefinition( const ActionParameterDefinition& definition );
    ActionParameterDefinition definition() const;

  protected:
    void accept() override;

  private:
    void updateTypeState();
    void refreshChoices();
    int selectedChoiceRow() const;
    void addChoice();
    void editChoice();
    void deleteChoice();
    void moveChoice( int offset );
    QVariant parseDefaultValue( bool* ok, QString* errorMessage ) const;
    void chooseCustomValidationPattern();
    void chooseCustomFormat();
    void chooseCustomExpression();
    void updatePresetState();

    ActionParameterDefinition definition_;
    QLineEdit* nameEdit_ = nullptr;
    QLineEdit* labelEdit_ = nullptr;
    QPlainTextEdit* descriptionEdit_ = nullptr;
    QComboBox* typeCombo_ = nullptr;
    QComboBox* presentationCombo_ = nullptr;
    QComboBox* multiValueModeCombo_ = nullptr;
    QCheckBox* requiredCheck_ = nullptr;
    QCheckBox* sensitiveCheck_ = nullptr;
    QCheckBox* rememberCheck_ = nullptr;
    QLineEdit* defaultEdit_ = nullptr;
    QLineEdit* minimumEdit_ = nullptr;
    QLineEdit* maximumEdit_ = nullptr;
    QLineEdit* stepEdit_ = nullptr;
    QComboBox* validationPatternCombo_ = nullptr;
    QComboBox* formatCombo_ = nullptr;
    QLineEdit* separatorEdit_ = nullptr;
    QComboBox* expressionCombo_ = nullptr;
    QString customValidationPattern_;
    QString customFormat_;
    QString customExpression_;
    QTableWidget* choicesTable_ = nullptr;
    QVector<ActionParameterChoice> choices_;
};
