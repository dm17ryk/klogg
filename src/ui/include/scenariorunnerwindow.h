#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;

class ScenarioRunner;

class ScenarioRunnerWindow : public QWidget {
    Q_OBJECT

  public:
    explicit ScenarioRunnerWindow( ScenarioRunner* runner, QWidget* parent = nullptr );

  private Q_SLOTS:
    void browseScenarioFile();
    void browseScenarioArgsFile();
    void browseSuiteFile();
    void createSuiteFile();
    void runScenario();
    void runSuite();
    void stopScenarioRun();
    void openJsonReport();
    void openJunitReport();
    void addSuiteScenario();
    void removeSuiteScenario();
    void moveSuiteScenarioUp();
    void moveSuiteScenarioDown();
    void browseSelectedScenarioArgs();
    void clearSelectedScenarioArgs();
    void saveSuite();
    void deleteSuite();
    void suiteSelectionChanged();
    void suiteItemChanged( QListWidgetItem* item );
    void refreshFromRunner();

  private:
    void loadUiState();
    void saveUiState() const;
    void pushRecentScenario( const QString& path );
    QVariantMap suiteEntryFromItem( const QListWidgetItem* item ) const;
    void updateItemText( QListWidgetItem* item );
    void loadSuiteFile( const QString& path );
    bool saveSuiteFile( const QString& path );

    ScenarioRunner* runner_;
    QLineEdit* scenarioFileEdit_;
    QLineEdit* scenarioArgsEdit_;
    QLineEdit* suiteFileEdit_;
    QListWidget* suiteScenariosList_;
    QLabel* selectedScenarioLabel_;
    QLabel* statusLabel_;
    QLabel* summaryLabel_;
    QLabel* reportLabel_;
    QPlainTextEdit* outputEdit_;
    QPushButton* runScenarioButton_;
    QPushButton* runSuiteButton_;
    QPushButton* stopButton_;
    QPushButton* openJsonButton_;
    QPushButton* openJunitButton_;
};
