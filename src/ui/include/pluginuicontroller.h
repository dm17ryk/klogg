#pragma once
#include "commander.h"
#include "pluginmanager.h"
#include <QDockWidget>
#include <QMenu>
#include <QPointer>
#include <functional>

class MainWindow;
class PluginUiController final : public QObject {
    Q_OBJECT
public:
    using Executor = std::function<CommanderResult( const CommanderRequest& )>;
    PluginUiController( MainWindow* window, cilogg::plugins::PluginManager* manager,
                        Executor executor );
    ~PluginUiController() override;
    void closeFile( const QString& path );

private:
    void refreshActions();
    void manageDialog();
    void browseDialog();
    void invoke( const QString& pluginId, const QJsonObject& action );
    void panel( const QString& pluginId, const QJsonObject& panel, const QVariantMap& context );
    void queuePanel( const QString& pluginId, const QJsonObject& panel,
                     const QVariantMap& context );
    void report( const CommanderResult& result );
    QPointer<MainWindow> window_;
    QPointer<cilogg::plugins::PluginManager> manager_;
    Executor executor_;
    QMenu* menu_ = nullptr;
    QList<QAction*> actions_;
    QMap<QString, QPointer<QDockWidget>> panels_;
    QMap<QString, QPair<QJsonObject, QVariantMap>> pendingPanels_;
    QTimer* panelTimer_ = nullptr;
    QJsonArray actionDescriptions_;
};
