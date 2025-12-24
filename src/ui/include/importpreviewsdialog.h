#pragma once

#include <QDialog>

class QDialogButtonBox;
class QTableView;
class QToolButton;
class PreviewsTableModel;

class ImportPreviewsDialog : public QDialog {
    Q_OBJECT

  public:
    explicit ImportPreviewsDialog( QWidget* parent = nullptr );

  private Q_SLOTS:
    void importPreviews();
    void removeSelectedPreview();
    void refreshTable();

  private:
    void updateButtons();
    void updateDialogWidth();

    QTableView* tableView_ = nullptr;
    PreviewsTableModel* model_ = nullptr;
    QDialogButtonBox* buttonBox_ = nullptr;
    QToolButton* removeButton_ = nullptr;
};
