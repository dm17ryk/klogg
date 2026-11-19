// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include "previewmanager.h"

namespace cilogg::plugins {
class PluginManager;
}

// Applies the enabled configuration packs (docs/plugin-repositories.md 4.2-4.4) to previews and
// to the TShark decoder, and checks a pack before it is enabled or an update of it is
// installed: no preview name shared with another enabled pack, and the user's items and all
// enabled packs together within the preview, Lua and route limits.
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
        QString error;
    };

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
    // Copy to My Previews: PreviewManager::copyPackPreview, and the pack's route for the preview
    // becomes one of the user's, unless the user has one. Returns the error.
    QString copyPreview( const QString& name );
    // The applied contributions, in pack ID order.
    const QVector<Contribution>& contributions() const
    {
        return applied_;
    }

private:
    PreviewPackRegistry() = default;
    QPointer<cilogg::plugins::PluginManager> manager_;
    QVector<Contribution> applied_;
    QHash<QString, Contribution> cache_;
    QStringList appliedKeys_;
    bool everApplied_ = false;
};
