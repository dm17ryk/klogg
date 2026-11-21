#pragma once

#include <QDialog>

#include <functional>

class QDialogButtonBox;
class QTableView;
class QToolButton;
class PreviewsTableModel;

class ImportPreviewsDialog : public QDialog {
    Q_OBJECT

public:
    // Opens the text editor of one item of a configuration pack: parent, pack id, store path.
    using PackItemEditor = std::function<void( QWidget*, const QString&, const QString& )>;

    explicit ImportPreviewsDialog( QWidget* parent = nullptr );
    // Shows Edit... for pack previews (design 4.6); without an editor the button stays hidden.
    void setPackItemEditor( PackItemEditor editor );

private Q_SLOTS:
    void importPreviews();
    void moveSelectedPreviewUp();
    void moveSelectedPreviewDown();
    void removeSelectedPreview();
    void clearAllPreviews();
    void copySelectedPreview();
    void editSelectedPreview();
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
    QToolButton* copyButton_ = nullptr;
    QToolButton* editButton_ = nullptr;
    PackItemEditor packItemEditor_;
    int pendingSelectionRow_ = -1;
};
