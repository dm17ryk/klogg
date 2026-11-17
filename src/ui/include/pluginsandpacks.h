#pragma once

#include "commander.h"
#include "pluginmanager.h"
#include <QPointer>
#include <QWidget>
#include <functional>

// The Available and Repositories tabs of the Plugins and Packs window
// (docs/plugin-repositories.md 3.3). Every change goes through Commander, as from the CLI, native
// MCP and Python; the tabs read the plugin manager for display only.
namespace pluginsui {
using Executor = std::function<CommanderResult( const CommanderRequest& )>;

enum class KindFilter { All, Plugins, Previewers, Decoders, Actions, Highlighters, Filters };

// The kind a package is listed as: "Plugins" for process plugins, the content type of a
// configuration pack that carries one type, "Configuration pack" otherwise.
QString kindLabel( const QVariantMap& entry );
bool matchesKind( const QVariantMap& entry, KindFilter filter );
// "Shipped with CILogg", "Local file" or the repository name of an installed plugin.
QString sourceLabel( const QVariantMap& plugin );
// The repository's state for its group header and the Repositories table.
QString repositoryStateText( const QVariantMap& repository );

// Calls `done` with the job payload once job `jobId` has finished.
void followJob( QObject* owner, QPointer<cilogg::plugins::PluginManager> manager,
                const QString& jobId, std::function<void( const QVariantMap& )> done );

QWidget* createAvailableTab( QWidget* parent, cilogg::plugins::PluginManager* manager,
                             Executor executor, std::function<void()> openGuide );
QWidget* createRepositoriesTab( QWidget* parent, cilogg::plugins::PluginManager* manager,
                                Executor executor, std::function<void()> openGuide );
} // namespace pluginsui
