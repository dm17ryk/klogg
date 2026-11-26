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
    // A route table's tab, with its Add Python decoder button in `addPython` (E16b).
    QTableWidget* routeTable( QTabWidget* tabs, const QString& name, const QString& title,
                              bool protocol, QPushButton** addPython );
    // Adds a route row: `target` as its dissector or decoder, the default input encoding in a
    // protocol route's encoding cell, and the other cells empty.
    int addRouteRow( QTableWidget* table, bool protocol, const QString& target = {} );
    // The values a route cell offers (issue #180).
    QStringList cellChoices( const QTableWidget* table, int row, int column ) const;
    // Rereads the selected Lua files and the enabled packs, and refills the drop-downs.
    void refreshChoices();
    void showChoicesStatus();
    // Reads `tshark -G protocols` of the saved TShark once, in the background, while the saved
    // decoder is enabled. `retry` reads it again after a failure.
    void loadTsharkProtocols( bool retry = false );
    // Drops the saved TShark's list (and a reading of it): after a settings change it may be
    // another file at the same path, size and time.
    void forgetTsharkProtocols();
    void tsharkProtocolsLoaded( QProcess* process, const QString& key, const QString& failure );
    // An empty PDML protocol, or one that was the previous dissector, follows the dissector.
    void followDissector( QTableWidget* table, QTableWidgetItem* item );
    // The warnings for the saved routes and Lua files; while TShark is listing, only those its
    // list cannot settle.
    QVector<previewroutes::Warning> savedWarnings() const;
    void showWarnings( const QVector<previewroutes::Warning>& warnings );
    // What the status says when OK stays open to show new warnings.
    QString heldOpenNote() const;
    void useDeclaredNames();
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
    // The shown warnings: always those of the saved settings (checked on opening, on Apply, when
    // a TShark list arrives and when the packs or their Python check change).
    QVector<previewroutes::Warning> warnings_;
};
