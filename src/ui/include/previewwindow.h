#pragma once

#include <QWidget>

class QTabWidget;
class PreviewMessageTab;

class PreviewWindow : public QWidget {
    Q_OBJECT

  public:
    explicit PreviewWindow( QWidget* parent = nullptr );

    void openMessageTab( const QString& rawLine, const QString& initialPreviewNameOrAuto );

  private:
    void handleTabClosed( int index );
    void refreshTabs();

    QTabWidget* tabWidget_ = nullptr;
    int tabCounter_ = 0;
};
