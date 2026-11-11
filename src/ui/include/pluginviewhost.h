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
class QMessageBox;
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

    PluginViewHost( QMainWindow* window, QString windowId, ContextProvider context,
                    cilogg::plugins::PluginManager* manager, Executor executor,
                    PluginViewDialogs dialogs = PluginViewDialogs::native() );
    ~PluginViewHost() override;

    // Opens (or raises) a declared view; `overrides` are view/show keys (area, floating,
    // title, focus). Returns false and an error message when the plugin cannot open it.
    bool openView( const QString& pluginId, const QString& viewId,
                   const QJsonObject& overrides = {}, QString* error = nullptr );
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

private:
    struct Open;
    struct Message;
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
    bool eventFilter( QObject* watched, QEvent* event ) override;
    int scaled( int milliseconds ) const;

    QPointer<QMainWindow> window_;
    QString windowId_;
    ContextProvider context_;
    QPointer<cilogg::plugins::PluginManager> manager_;
    Executor executor_;
    PluginViewDialogs dialogs_;
    std::map<QString, std::unique_ptr<Open>> open_;
    std::map<QString, std::unique_ptr<Message>> messages_;
    QTimer renderTimer_;
    double timeScale_ = 1.0;
    bool closing_ = false;
};
