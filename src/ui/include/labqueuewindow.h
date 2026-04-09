#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QPlainTextEdit;

class LabQueueWindow : public QWidget {
    Q_OBJECT

  public:
    explicit LabQueueWindow( QWidget* parent = nullptr );

  private Q_SLOTS:
    void refresh();
    void selectedJobChanged();
    void browseTokenFile();
    void openArtifactsFolder();

  private:
    void loadState();
    void saveState() const;

    QLineEdit* controllerUrlEdit_;
    QLineEdit* tokenFileEdit_;
    QListWidget* agentsList_;
    QListWidget* jobsList_;
    QLabel* summaryLabel_;
    QPlainTextEdit* detailsEdit_;
    QPushButton* refreshButton_;
    QPushButton* openArtifactsButton_;
};
