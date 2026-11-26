// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <functional>
#include <optional>

#include "actionsconfig.h"
#include "highlighterset.h"
#include "predefinedfilters.h"
#include "previewmanager.h"

namespace cilogg::plugins {
class PluginManager;
}

// Applies the enabled configuration packs (docs/plugin-repositories.md 4.2-4.5) to previews, to
// the TShark decoder, to the predefined filters, highlighter sets, actions and automatic
// responses, and checks a pack before it is enabled or an update of it is installed: no preview
// name, filter or set id shared with another enabled pack, and the user's items and all enabled
// packs together within the preview, Lua, route, filter, highlighter and action limits.
class PreviewPackRegistry : public QObject {
    Q_OBJECT
public:
    // What a pack contributes, read from its folder.
    struct Contribution {
        QString id;
        QString name;
        QString key; // version and folder state: a change reloads the pack
        PreviewPackSource previews;
        QStringList luaFiles;
        QVariantMap previewRoutes;
        QVariantMap protocolRoutes;
        int pythonDecoders = 0;
        // MD03b: the folder the pack's content is read from and its Python decoder modules
        // (`{id, module}`, relative to it).
        QString root;
        QVariantList pythonModules;
        // E16b: the IDs of the modules that bind `decode` at their top level; the others are
        // helpers (see PreviewDecoderPacks::pythonEntries).
        QStringList pythonEntries;
        // Its predefined filters in its order, with packId and packName (E16c).
        QList<ListedFilter> filters;
        // Its highlighter sets in its order, with packId and packName (E16c2).
        QList<ListedHighlighterSet> highlighterSets;
        // Its actions and automatic responses in its order, with the pack's own ids and links
        // (E16c3; ActionsManager gives them run-time ids).
        QVector<ActionDefinition> actions;
        QVector<ResponseDefinition> responses;
        QString error;
        // A filters, highlighters or actions file that is not exactly an export (a pack enabled
        // before E16c may carry one), or an action CILogg would not run: that file's items are
        // left out, the rest of the pack applies, and enabling or updating the pack is refused
        // with this reason.
        QStringList warnings;
    };
    // The most predefined filters the user's and the enabled packs' can be together when a pack is
    // enabled or updated (MaxListedFilters).
    static constexpr int FilterLimit = MaxListedFilters;

    static PreviewPackRegistry& instance();
    // Follows the manager's changes and installs the pack check (PluginManager::setPackCheck).
    void attach( cilogg::plugins::PluginManager* manager );
    // Reloads the enabled packs that changed and applies them.
    void refresh();
    // The pack check: the refusal, or an empty string.
    QString check( const QString& id, const QString& directory,
                   const QJsonObject& manifest ) const;
    // Reads a pack from its folder.
    static Contribution read( const QString& id, const QString& directory,
                              const QJsonObject& manifest );
    // Copy to My Previews: PreviewManager::copyPackPreview, and the route the preview decodes with
    // (the user's own for it, else the pack's) becomes the user's route for the copy. A copy whose
    // route cannot be added, or that another route of the user's under its name would decode, is
    // not made. Returns the error.
    QString copyPreview( const QString& name, const QString& copyName = {} );
    // Pack `packId` as it is now (with the local edits), whether it is enabled or not: Keep Both
    // in the merge window takes it before the update replaces those previews.
    std::optional<Contribution> snapshotOfPack( const QString& packId, QString* error ) const;
    // Copy to My Previews from such a snapshot, with the pack's route for the preview, once the
    // update is installed; like copyPreview, made with its route or not at all. Returns the error.
    QString copyPreviewOfSnapshot( const Contribution& pack, const QString& name,
                                   const QString& copyName );
    // Whether the routes of copies of `pack`'s previews (preview name -> the copy's name) can all
    // be added to the user's routes: the refusal, or an empty string. Keep Both checks it at Apply.
    QString checkCopyRoutes( const Contribution& pack, const QMap<QString, QString>& copies ) const;
    // The preview names of every installed pack, enabled or not: a copy under another name never
    // takes one, which it would replace (yours win) once that pack is enabled.
    QStringList installedPackPreviewNames() const;
    // The applied contributions, in pack ID order.
    const QVector<Contribution>& contributions() const
    {
        return applied_;
    }

private:
    PreviewPackRegistry() = default;
    // Makes the copy `target` of `name` with `copy`, and the route `name` decodes with becomes the
    // user's route for `target`. The route is checked before the copy is made, so a copy does not
    // end up without its route or with another one (its name stays free for another try).
    static QString copyWithRoute( const Contribution& pack, const QString& name,
                                  const QString& target, const std::function<QString()>& copy );
    QPointer<cilogg::plugins::PluginManager> manager_;
    QVector<Contribution> applied_;
    QHash<QString, Contribution> cache_;
    QStringList appliedKeys_;
    bool everApplied_ = false;
};
