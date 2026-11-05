// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>
class QCheckBox;
class QLineEdit;
class QComboBox;
class QListWidget;
class QTableWidget;
class QLabel;

class PreviewDecoderSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreviewDecoderSettingsDialog( QWidget* parent = nullptr );
    void reload();

private:
    bool apply();
    QCheckBox* enabled_;
    QCheckBox* approval_;
    QLineEdit* tshark_;
    QLineEdit* text2pcap_;
    QLineEdit* dissector_;
    QComboBox* encoding_;
    QListWidget* scripts_;
    QTableWidget* previewRoutes_;
    QTableWidget* protocolRoutes_;
    QLabel* error_;
};
