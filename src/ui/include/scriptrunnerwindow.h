#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

class ScriptSupervisor;

class ScriptRunnerWindow : public QWidget {
    Q_OBJECT

  public:
    explicit ScriptRunnerWindow( ScriptSupervisor* supervisor, QWidget* parent = nullptr );

    void setScriptPath( const QString& path );

  private Q_SLOTS:
    void browseScriptFile();
    void browseArgsFile();
    void runScript();
    void runGlobalScript();
    void rerunScript();
    void rerunGlobalScript();
    void stopScript();
    void stopGlobalScript();
    void openScriptFolder();
    void refreshFromSupervisor();
    void selectedRunChanged();

  private:
    ScriptSupervisor* supervisor_;
    QLineEdit* scriptPathEdit_;
    QLineEdit* argsJsonPathEdit_;
    QListWidget* runsList_;
    QLabel* globalStatusLabel_;
    QLabel* globalSummaryLabel_;
    QLabel* globalSubscriptionsLabel_;
    QPlainTextEdit* globalOutputEdit_;
    QLabel* statusLabel_;
    QLabel* summaryLabel_;
    QLabel* subscriptionsLabel_;
    QPlainTextEdit* outputEdit_;
    QPushButton* runButton_;
    QPushButton* runGlobalButton_;
    QPushButton* rerunGlobalButton_;
    QPushButton* stopGlobalButton_;
    QPushButton* rerunButton_;
    QPushButton* stopButton_;
    QString selectedTabId_;
};
