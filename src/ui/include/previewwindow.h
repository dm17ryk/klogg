#pragma once

#include <QWidget>
#include "previewsource.h"

class QTabWidget;
class PreviewMessageTab;

class PreviewWindow : public QWidget {
    Q_OBJECT

  public:
    explicit PreviewWindow( QWidget* parent = nullptr );

    void openMessageTab( const QString& rawLine, const QString& initialPreviewNameOrAuto );
    void openPreparedMessageTab( const PreparedPreview& prepared, const QString& initialPreviewNameOrAuto );

  private:
    void handleTabClosed( int index );
    void refreshTabs();

    QTabWidget* tabWidget_ = nullptr;
    int tabCounter_ = 0;
};
