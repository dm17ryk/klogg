#pragma once

#include <QWidget>

#include <functional>

#include "commander.h"
#include "previewsource.h"

class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;
class AnalysisSourcePicker;
class PreviewMessageTab;

class PreviewWindow : public QWidget {
    Q_OBJECT

  public:
    explicit PreviewWindow( QWidget* parent = nullptr );

    void openMessageTab( const QString& rawLine, const QString& initialPreviewNameOrAuto );
    void openPreparedMessageTab( const PreparedPreview& prepared, const QString& initialPreviewNameOrAuto );

    // U15b: a source picker (focused tab, a tab or a file) and a line number open that line in
    // a new message tab through `openLine`.
    using SourcesProvider = std::function<QVariantList()>;
    using LineOpener = std::function<CommanderResult( const QString& source, quint64 line )>;
    void setLineSource( SourcesProvider sources, LineOpener openLine );

private:
    void handleTabClosed( int index );
    void refreshTabs();
    void previewLine();

    QTabWidget* tabWidget_ = nullptr;
    int tabCounter_ = 0;
    QWidget* lineRow_ = nullptr;
    AnalysisSourcePicker* source_ = nullptr;
    QSpinBox* line_ = nullptr;
    QPushButton* previewLine_ = nullptr;
    QLabel* lineStatus_ = nullptr;
    LineOpener openLine_;
};
