// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>
class QCheckBox;
class QLineEdit;
class QComboBox;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QLabel;

class PreviewDecoderSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreviewDecoderSettingsDialog( QWidget* parent = nullptr );
    void reload();

private:
    bool apply();
    // E16b: the Add Python decoder menus list the enabled packs' decoders by pack name, and each
    // route's target says which pack's decoder it is, or that no enabled pack has it.
    void refreshPythonDecoders();
    void describeTarget( QTableWidgetItem* item ) const;
    QPushButton* previewAddPython_ = nullptr;
    QPushButton* protocolAddPython_ = nullptr;
    QCheckBox* enabled_;
    QCheckBox* approval_;
    QLineEdit* tshark_;
    QLineEdit* text2pcap_;
    QLineEdit* dissector_;
    QComboBox* encoding_;
    QLineEdit* assemblyHosts_;
    QListWidget* scripts_;
    QTableWidget* previewRoutes_;
    QTableWidget* protocolRoutes_;
    QLabel* error_;
};
