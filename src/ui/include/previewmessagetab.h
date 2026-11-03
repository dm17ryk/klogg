#pragma once

#include <QWidget>
#include <QPointer>
#include "previewsource.h"

#include "previewconfig.h"

class QComboBox;
class QGroupBox;
class QPlainTextEdit;
class QTreeWidget;
class QLabel;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QProcess;

class PreviewMessageTab : public QWidget {
    Q_OBJECT

  public:
    PreviewMessageTab( const QString& rawLine,
                       const QString& initialPreviewNameOrAuto,
                       int tabNumber,
                       QWidget* parent = nullptr );
    PreviewMessageTab( const PreparedPreview& prepared, const QString& initialPreviewNameOrAuto,
                       int tabNumber, QWidget* parent = nullptr );
    ~PreviewMessageTab() override;

    void refreshPreviewList();
    QString title() const;
    QVariantMap result() const { return result_; }

  Q_SIGNALS:
    void titleChanged( const QString& title );

  private Q_SLOTS:
    void handlePreviewSelectionChanged( int index );

  private:
    void buildUi();
    void applyAutoDetection();
    void renderPreview( const QString& previewName );
    void cancelEvaluation();
    void invalidateEvaluation();
    void presentResult( const QVariantMap& result );
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
    PreparedPreview prepared_;
    QComboBox* decoderMode_ = nullptr;
    QComboBox* inputEncoding_ = nullptr;
    QLineEdit* dissector_ = nullptr;
    QSpinBox* timeout_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* run_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPointer<QProcess> process_;
    quint64 revision_ = 0;
    QVariantMap result_;
};
