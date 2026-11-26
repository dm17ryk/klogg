// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "previewroutechoices.h"
#include <QDialog>
#include <QPointer>
#include <memory>
class QCheckBox;
class QTemporaryDir;
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
    // Lists the protocols of the saved TShark once per file, in the background, while the saved
    // decoder is enabled: `-G folders` first proves the plugin isolation, then `-G protocols`.
    // `retry` lists again after a failure.
    void loadTsharkProtocols( bool retry = false );
    void stopTsharkListing();
    void runTshark( const QString& path, const QString& key,
                    const std::shared_ptr<QTemporaryDir>& folder, const QString& report );
    void tsharkReported( QProcess* process, const QString& path, const QString& key,
                         const std::shared_ptr<QTemporaryDir>& folder, const QString& report,
                         QString failure );
    // OK closes, or stays open once when the warnings are new since `shown`.
    void closeUnlessNewWarnings( const QString& shown );
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
    QCheckBox* enabled_ = nullptr;
    QCheckBox* approval_ = nullptr;
    QLineEdit* tshark_ = nullptr;
    QLineEdit* text2pcap_ = nullptr;
    QComboBox* dissector_ = nullptr;
    QComboBox* encoding_ = nullptr;
    QLineEdit* assemblyHosts_ = nullptr;
    QListWidget* scripts_ = nullptr;
    QTableWidget* previewRoutes_ = nullptr;
    QTableWidget* protocolRoutes_ = nullptr;
    QLabel* choicesStatus_ = nullptr;
    QLabel* warningsLabel_ = nullptr;
    QPushButton* useDeclared_ = nullptr;
    QLabel* error_ = nullptr;
    previewroutes::Choices choices_;
    // The saved TShark the protocol list is for (path, size and time), and its state.
    QString tsharkKey_;
    QString tsharkFailure_;
    QPointer<QProcess> tsharkProcess_;
    // The shown warnings: always those of the saved settings (checked on opening, on Apply, when
    // a TShark list arrives and when the packs or their Python check change).
    QVector<previewroutes::Warning> warnings_;
    // OK pressed while TShark was listing: it closes (or not) once the list is there, comparing
    // with the warnings shown before it.
    bool okWaitsForList_ = false;
    QString okShownWarnings_;
};
