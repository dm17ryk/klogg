#pragma once

#include <QWidget>

#include "previewconfig.h"

class QComboBox;
class QGroupBox;
class QPlainTextEdit;
class QTreeWidget;

class PreviewMessageTab : public QWidget {
    Q_OBJECT

  public:
    PreviewMessageTab( const QString& rawLine,
                       const QString& initialPreviewNameOrAuto,
                       int tabNumber,
                       QWidget* parent = nullptr );

    void refreshPreviewList();
    QString title() const;

  Q_SIGNALS:
    void titleChanged( const QString& title );

  private Q_SLOTS:
    void handlePreviewSelectionChanged( int index );

  private:
    void buildUi();
    void applyAutoDetection();
    void renderPreview( const QString& previewName );
    void showMessage( const QString& message );
    void updateTitle( const QString& typeLabel );
    QString makeSnippet() const;
    void updateRawLineHeight();

    QString rawLine_;
    QString initialPreviewName_;
    int tabNumber_ = 0;
    QString currentTitle_;
    bool initialApplied_ = false;

    QComboBox* previewTypeCombo_ = nullptr;
    QTreeWidget* previewTree_ = nullptr;
    QGroupBox* rawGroup_ = nullptr;
    QPlainTextEdit* rawLineEdit_ = nullptr;
};
