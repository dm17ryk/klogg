// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "commander.h"
#include "pluginview.h"
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <map>
#include <optional>

class QLabel;
class QMainWindow;
class QMenu;
class QMessageBox;
struct PluginMenuContext;
namespace cilogg::plugins {
class PluginManager;
}

// Owns the plugin views (docks, windows, dialogs) and message boxes of one CILogg window and
// connects them to the PluginManager protocol (docs/plugin-views.md).
class PluginViewHost final : public QObject {
    Q_OBJECT
public:
    using Executor = std::function<CommanderResult( const CommanderRequest& )>;
    using ContextProvider = std::function<QVariantMap()>;
    // U15b: the tabs a view can follow, and the plugin context of a source for a view of this
    // window (an empty map and *error when the source cannot be used).
    using SourcesProvider = std::function<QVariantList()>;
    using SourceContext = std::function<QVariantMap( const QString& source, QString* error )>;

    PluginViewHost( QMainWindow* window, QString windowId, ContextProvider context,
                    cilogg::plugins::PluginManager* manager, Executor executor,
                    PluginViewDialogs dialogs = PluginViewDialogs::native() );
    ~PluginViewHost() override;

    // Opens (or raises) a declared view; `overrides` are view/show keys (area, floating,
    // title, focus, and with source providers `source`). Returns false and an error message
    // when the plugin cannot open it.
    bool openView( const QString& pluginId, const QString& viewId,
                   const QJsonObject& overrides = {}, QString* error = nullptr );
    // U15b: views get a source picker (focused tab, a tab or a file). The view's context, sent
    // in view/opened and used by its buttons, is that source's tab; without providers it is the
    // window's current tab, as before.
    void setSourceProviders( SourcesProvider sources, SourceContext context );
    QString viewSource( const QString& pluginId, const QString& viewId ) const;
    // Re-sends view/opened with the new source's context. False (and *error) if the source
    // cannot be used; the view keeps its previous source.
    bool setViewSource( const QString& pluginId, const QString& viewId, const QString& source,
                        QString* error = nullptr );
    void closeView( const QString& pluginId, const QString& viewId );
    QWidget* container( const QString& pluginId, const QString& viewId ) const;
    PluginView* view( const QString& pluginId, const QString& viewId ) const;
    QMessageBox* messageBox( const QString& pluginId, const QString& messageId ) const;
    QStringList openViews() const;
    // Applies queued view/update messages now (normally coalesced to at most 10 renders/s).
    void flush();
    void setRenderIntervalMs( int milliseconds );
    // Message-box countdowns run `scale` times faster in tests (1.0 = real seconds).
    void setTimeScale( double scale )
    {
        timeScale_ = scale;
    }
    void setDialogs( PluginViewDialogs dialogs );
    void windowClosing();

    // E17d element context menus (docs/plugin-sdk-advanced.md 10.4): adds a plugin's entries of
    // `menuId` to a table's context menu. Set by the window's PluginMenus.
    using ElementMenuFiller = std::function<void( QMenu* menu, const QString& pluginId,
                                                  const QString& menuId,
                                                  const PluginMenuContext& context )>;
    void setElementMenuFiller( ElementMenuFiller filler )
    {
        elementMenuFiller_ = std::move( filler );
    }
    // The menu context of an element's entries: this window, the view and element, and
    // `target` ({row_id, column_id, selected_ids, selected_count}).
    PluginMenuContext elementMenuContext( const QString& viewId, const QString& elementId,
                                          const QString& menuId, const QJsonObject& target ) const;

private:
    struct Open;
    struct Message;
    // E17f (docs/plugin-sdk-advanced.md section 29.3): dialog buttons, Esc and the title bar's
    // close button of a dialog, the "Discard changes?" question and the title mark.
    void dialogButton( const QString& key, const QString& name );
    void dialogRejected( const QString& key );
    // True when the view may close: it is not modified, or the user chose Discard.
    bool confirmDiscard( const QString& key );
    void updateTitle( Open& open );
    void syncButtons( Open& open );
    QJsonObject closedExtra( const Open& open ) const;
    void handleRequest( const QString& pluginId, const QString& method, const QJsonObject& params );
    void queueUpdate( const QString& pluginId, const QJsonObject& params );
    void render( Open& open );
    bool live( const QString& pluginId, const QString& viewId ) const;
    void showMessage( const QString& pluginId, const QJsonObject& params );
    void finishMessage( const QString& key, const QString& button, bool timedOut );
    void tickMessage( const QString& key );
    void dropMessages( const QString& pluginId );
    void setStopped( const QString& pluginId, bool stopped );
    void containerClosed( const QString& key );
    void saveState( const Open& open ) const;
    void place( Open& open, const QJsonObject& definition, const QJsonObject& overrides );
    void invokeAction( const QString& pluginId, const QString& viewId, const QString& action,
                       const QVariantMap& arguments );
    QJsonObject definition( const QString& pluginId, const QString& viewId ) const;
    QVariantMap contextFor( const QString& source, QString* error ) const;
    bool eventFilter( QObject* watched, QEvent* event ) override;
    int scaled( int milliseconds ) const;

    QPointer<QMainWindow> window_;
    QString windowId_;
    ContextProvider context_;
    SourcesProvider sources_;
    SourceContext sourceContext_;
    QPointer<cilogg::plugins::PluginManager> manager_;
    Executor executor_;
    PluginViewDialogs dialogs_;
    ElementMenuFiller elementMenuFiller_;
    std::map<QString, std::unique_ptr<Open>> open_;
    std::map<QString, std::unique_ptr<Message>> messages_;
    QTimer renderTimer_;
    double timeScale_ = 1.0;
    bool closing_ = false;
};
