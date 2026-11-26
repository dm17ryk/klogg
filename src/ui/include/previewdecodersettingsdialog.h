// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "previewroutechoices.h"
#include <QDialog>
#include <QPointer>
class QCheckBox;
class QLineEdit;
class QComboBox;
class QListWidget;
class QProcess;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTableWidgetItem;
class QLabel;

class PreviewDecoderSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreviewDecoderSettingsDialog( QWidget* parent = nullptr );
    ~PreviewDecoderSettingsDialog() override;
    void reload();

protected:
    void showEvent( QShowEvent* event ) override;

private:
    bool apply();
    QTableWidget* routeTable( QTabWidget* tabs, const QString& name, const QString& title,
                              bool protocol );
    // The values a route cell offers (issue #180).
    QStringList cellChoices( const QTableWidget* table, int row, int column ) const;
    // Rereads the selected Lua files and the enabled packs, and refills the drop-downs.
    void refreshChoices();
    void showChoicesStatus();
    // Reads `tshark -G protocols` of the saved TShark once, in the background.
    void loadTsharkProtocols();
    void tsharkProtocolsLoaded( QProcess* process, const QString& key, const QString& failure );
    // An empty PDML protocol, or one that was the previous dissector, follows the dissector.
    void followDissector( QTableWidget* table, QTableWidgetItem* item );
    void showWarnings( const QVector<previewroutes::Warning>& warnings );
    void useDeclaredNames();
    QCheckBox* enabled_;
    QCheckBox* approval_;
    QLineEdit* tshark_;
    QLineEdit* text2pcap_;
    QComboBox* dissector_;
    QComboBox* encoding_;
    QLineEdit* assemblyHosts_;
    QListWidget* scripts_;
    QTableWidget* previewRoutes_;
    QTableWidget* protocolRoutes_;
    QLabel* choicesStatus_;
    QLabel* warningsLabel_;
    QPushButton* useDeclared_;
    QLabel* error_;
    previewroutes::Choices choices_;
    // The saved TShark the protocol list is for (path, size and time), and its state.
    QString tsharkKey_;
    QString tsharkFailure_;
    QPointer<QProcess> tsharkProcess_;
    QVector<previewroutes::Warning> warnings_;
};
