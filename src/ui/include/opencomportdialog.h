#pragma once

#include <QDialog>

#include "serialcaptureworker.h"

class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QCheckBox;
class QPushButton;

class OpenComPortDialog : public QDialog {
    Q_OBJECT

  public:
    explicit OpenComPortDialog( QWidget* parent = nullptr );
    SerialCaptureSettings settings() const;

  private Q_SLOTS:
    void updateSuggestedFileName();
    void browseForFile();
    void validateInputs();
    void markFilePathEdited();

  private:
    void populatePorts();
    bool isPortItemEnabled( int index ) const;
    QString suggestedFileName() const;

    QComboBox* portCombo_ = nullptr;
    QComboBox* baudCombo_ = nullptr;
    QComboBox* dataBitsCombo_ = nullptr;
    QComboBox* parityCombo_ = nullptr;
    QComboBox* stopBitsCombo_ = nullptr;
    QComboBox* flowCombo_ = nullptr;
    QLineEdit* fileEdit_ = nullptr;
    QCheckBox* timestampCheck_ = nullptr;
    QLineEdit* timestampFormatEdit_ = nullptr;
    QCheckBox* logTxCheck_ = nullptr;
    QCheckBox* useForActionsCheck_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QDialogButtonBox* buttonBox_ = nullptr;
    QPushButton* openButton_ = nullptr;

    QString lastSuggestedPath_;
    bool userEditedPath_ = false;
};
