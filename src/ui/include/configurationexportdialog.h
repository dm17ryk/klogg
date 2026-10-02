#pragma once

#include <QDialog>
#include <QVector>

#include "configurationexport.h"

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;

class ConfigurationExportDialog : public QDialog {
public:
    ConfigurationExportDialog( ConfigurationExportKind kind, const QStringList& names,
                               QWidget* parent = nullptr );
    QVector<int> selectedRows() const;
    bool multipleFiles() const;

private:
    void selectVisible( bool checked );
    void filterNames( const QString& text );
    void updateSelection();

    QListWidget* list_;
    QLineEdit* filter_;
    QCheckBox* multipleFiles_ = nullptr;
    QLabel* count_;
    QDialogButtonBox* buttons_;
};
