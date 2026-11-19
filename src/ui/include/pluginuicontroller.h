#pragma once
#include "commander.h"
#include "pluginmanager.h"
#include <QDialog>
#include <QDockWidget>
#include <QMenu>
#include <QPointer>
#include <QSet>
#include <QStyle>
#include <functional>

class AnalysisSourcePicker;
class MainWindow;
class PluginViewHost;
class PluginUiController final : public QObject {
    Q_OBJECT
public:
    using Executor = std::function<CommanderResult( const CommanderRequest& )>;
    PluginUiController( MainWindow* window, cilogg::plugins::PluginManager* manager,
                        Executor executor );
    ~PluginUiController() override;
    void closeFile( const QString& path );
    PluginViewHost* viewHost() const
    {
        return viewHost_;
    }
    // The source the Plugins menu's actions run on (U15b): focused, all, tab:<id> or file:<path>.
    QString actionSource() const;

private:
    // The tabs of the Plugins and Packs window (docs/plugin-repositories.md 3.3).
    enum class PluginsTab { Installed = 0, Available = 1, Repositories = 2 };
    void refreshActions();
    // Plugins > Updates (n): the updates not skipped (design 3.7).
    int pendingUpdateCount() const;
    // Asks before a configuration pack with Lua is enabled; true for anything else.
    bool confirmPackLua( QWidget* parent, const QString& id ) const;
    void refreshUpdates();
    QIcon iconFor( const QVariantMap& plugin, const QJsonObject& item,
                   QStyle::StandardPixmap fallback );
    // Opens (or raises) the one Plugins and Packs window at `tab`.
    void openPluginsWindow( PluginsTab tab );
    QWidget* createInstalledTab( QWidget* parent );
    void openGuide();
    void invoke( const QString& pluginId, const QJsonObject& action );
    void panel( const QString& pluginId, const QJsonObject& panel, const QVariantMap& context );
    void queuePanel( const QString& pluginId, const QJsonObject& panel,
                     const QVariantMap& context );
    void report( const CommanderResult& result );
    QPointer<MainWindow> window_;
    QPointer<cilogg::plugins::PluginManager> manager_;
    Executor executor_;
    QMenu* menu_ = nullptr;
    QAction* updatesAction_ = nullptr;
    QList<QAction*> actions_;
    QMap<QString, QPointer<QDockWidget>> panels_;
    QMap<QString, QPair<QJsonObject, QVariantMap>> pendingPanels_;
    QTimer* panelTimer_ = nullptr;
    QJsonArray actionDescriptions_;
    QSet<QString> iconWarnings_;
    PluginViewHost* viewHost_ = nullptr;
    AnalysisSourcePicker* sourcePicker_ = nullptr;
    QPointer<QDialog> pluginsWindow_;
};
