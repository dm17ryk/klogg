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
    void moveSelectedPreviewUp();
    void moveSelectedPreviewDown();
    void removeSelectedPreview();
    void clearAllPreviews();
    void refreshTable();

private:
    void moveSelectedPreview( int delta );
    void updateButtons();
    void updateDialogWidth();

    QTableView* tableView_ = nullptr;
    PreviewsTableModel* model_ = nullptr;
    QDialogButtonBox* buttonBox_ = nullptr;
    QToolButton* moveUpButton_ = nullptr;
    QToolButton* moveDownButton_ = nullptr;
    QToolButton* removeButton_ = nullptr;
    QToolButton* clearButton_ = nullptr;
    int pendingSelectionRow_ = -1;
};
