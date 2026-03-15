#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
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
    void rerunScript();
    void stopScript();
    void openScriptFolder();
    void refreshFromSupervisor();

  private:
    ScriptSupervisor* supervisor_;
    QLineEdit* scriptPathEdit_;
    QLineEdit* argsJsonPathEdit_;
    QLabel* statusLabel_;
    QLabel* summaryLabel_;
    QPlainTextEdit* outputEdit_;
    QPushButton* runButton_;
    QPushButton* rerunButton_;
    QPushButton* stopButton_;
};
