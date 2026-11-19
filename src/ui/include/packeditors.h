#pragma once

#include "pluginsandpacks.h"
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QWidget>
#include <functional>

// Editing configuration packs in place (docs/plugin-repositories.md 4.6). Every change goes
// through Commander (plugin_manage pack_* operations), as from the CLI, native MCP and Python.
namespace pluginsui {
// The built-in text editor of one pack item (a preview or block as JSON, a Lua or Python file),
// with Open in External Editor, History and Reset to Pack Version.
void openPackItemEditor( QWidget* parent, Executor executor, const QString& packId,
                         const QString& packName, const QString& path );
// The items of a pack with their state (pack, edited, added, deleted): Edit, History, Reset.
void openPackFiles( QWidget* parent, Executor executor, const QString& packId,
                    const QString& packName );

// The merge window for an update that failed with `needsMerge`: the conflicts of the failed
// job's result, Keep Mine / Take Theirs / Edit / Keep Both per row, for all rows, and a
// three-pane text merge for Lua and Python. On Apply `apply` gets the resolutions for the update
// option `resolutions`; Keep Both first copies the user's version of a preview into their own
// previews as "<name> (mine)". Returns true when applied.
bool openPackMerge( QWidget* parent, const QString& packName, const QVariantMap& job,
                    const std::function<void( const QVariantList& )>& apply );

// The external merge tool, configured like git mergetool: a command with $BASE, $LOCAL, $REMOTE
// and $MERGED.
QString mergeToolCommand();
void setMergeToolCommand( const QString& command );
QStringList mergeToolPresets();
// Runs the tool on three texts; returns the merged text, or nothing (with `error`) when the tool
// failed or left an unusable result (not UTF-8, larger than 1 MiB, conflict markers).
std::optional<QString> runMergeTool( QWidget* parent, const QString& base, const QString& mine,
                                     const QString& theirs, const QString& suffix,
                                     QString* error );
// True when a failed plugin_manage update or install needs the merge window.
bool needsPackMerge( const QVariantMap& job );
// Runs an install or update request from the GUI and follows its job. When the job needs a pack
// merge, opens the merge window and, on Apply, runs the request again with the resolutions.
// `done` gets the final job (the failed one when the merge was cancelled).
void runPackAwareInstall( QWidget* parent, QPointer<cilogg::plugins::PluginManager> manager,
                          Executor executor, const CommanderRequest& request, const QString& name,
                          const std::function<void( const QVariantMap& )>& done );
} // namespace pluginsui
