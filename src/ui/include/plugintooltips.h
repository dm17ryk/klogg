// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <functional>

class QMenu;
class QWidget;

// Host queries a plugin view may send (docs/plugin-sdk-advanced.md section 5). PluginViewHost
// fills them in; a view without them shows static tooltips only.
struct PluginViewQueries {
    // Sends `tooltip/resolve` with `params` (element_id, row_id, column_id, node_id,
    // tooltip_key; the host adds the view, window and context). `done` gets `answered` (a valid
    // answer, possibly empty) and the sanitized tooltip. Returns the query id; empty when the query
    // was not sent (the plugin is stopped, busy or paused), and then `done` is never called. A
    // newer `hover` query of the view replaces the pending one; get_plugin_tooltip reads
    // (`hover` false) replace only each other.
    std::function<QString( const QJsonObject& params, int timeoutMs, bool hover,
                           std::function<void( bool answered, const QJsonObject& tooltip )> done )>
        resolveTooltip;
    // The host stopped waiting (`query/cancel`).
    std::function<void( const QString& id )> cancel;
    // An icon reference as an <img> element for the rendered tooltip; empty when unusable.
    std::function<QString( const QJsonValue& reference )> iconHtml;
    // E17c: sends `field/complete` with `params` (element_id, text, cursor; the host adds the
    // view, window and context) and the field's `completion_timeout_ms` (0: the default); `done`
    // gets the items (none when there is nothing). Returns the query id, empty when nothing was
    // sent (done never runs then).
    std::function<QString( const QJsonObject& params, int timeoutMs,
                           std::function<void( const QJsonArray& items )> done )>
        complete;
    // An icon reference as an icon (choices); null when unusable.
    std::function<QIcon( const QJsonValue& reference )> icon;
    // E17d: adds the plugin's entries of `menuId` (a menu with location `element`) to a table's
    // context menu; `target` is {row_id, column_id, selected_ids, selected_count}.
    std::function<void( QMenu* menu, const QString& elementId, const QString& menuId,
                        const QJsonObject& target )>
        elementMenu;
    // E17d: the kept state of an element (table columns), read when its columns are set and
    // written after the user changed them (docs/plugin-sdk-advanced.md section 10.5).
    std::function<QJsonObject( const QString& elementId )> loadState;
    std::function<void( const QString& elementId, const QJsonObject& state )> saveState;
    // E17e: sends `tree/children` with `params` (element_id, node_id; the host adds the view,
    // window and context); `done` gets whether the plugin answered (null and no nodes count as an
    // answer: no children) and the nodes. Returns the query id; empty when nothing was sent (done
    // never runs then). Cancelled with `cancel`.
    std::function<QString( const QJsonObject& params,
                           std::function<void( bool answered, const QJsonArray& nodes )> done )>
        treeChildren;
};

// Static and advanced tooltips of one plugin view (section 9): watches widgets for tooltip
// events, asks the plugin for targets with `tooltip_provider`, keeps answers in a cache and shows
// them in place without ever blocking the GUI thread.
class PluginTooltips final : public QObject {
    Q_OBJECT
public:
    static constexpr int CacheEntries = 256;
    static constexpr int LoadingDelayMs = 250;

    struct Target {
        QString elementId;
        QString rowId;
        QString columnId;
        QString nodeId;
        QString key;        // every tooltip_key that applies (the cache key)
        QString tooltipKey; // the most specific tooltip_key, sent in the query
        QString staticTip;  // plain text; shown while the plugin answers and as the fallback
        QRect rect;        // the target's area in the watched widget
        bool provider = false;
        int timeoutMs = 0;
        bool valid() const
        {
            return !elementId.isEmpty();
        }
        QString cacheKey() const;
    };
    using Locator = std::function<Target( const QPoint& position )>;
    // `source` is static, cache or plugin; `tooltip` the sanitized answer (empty for static).
    using Resolved = std::function<void( const QString& source, const QJsonObject& tooltip,
                                         qint64 latencyMs )>;

    explicit PluginTooltips( QObject* parent = nullptr );
    ~PluginTooltips() override;

    void setQueries( PluginViewQueries queries );
    // Shows the tooltips of `widget`; `locator` maps a position in it to a target.
    void watch( QWidget* widget, Locator locator );
    // Drops cached answers (a patch, tooltip/invalidate, a restart).
    void invalidateElement( const QString& elementId );
    void invalidateRows( const QString& elementId, const QStringList& rowIds );
    void invalidateNodes( const QString& elementId, const QStringList& nodeIds );
    void clear();
    // Cancels the pending query (the plugin stopped or the view closes).
    void cancelPending();
    int cachedCount() const
    {
        return static_cast<int>( cache_.size() );
    }
    // The same path as a hover, for get_plugin_tooltip: a cached answer, else a query, else the
    // static tooltip. `done` runs once, possibly before resolve() returns.
    void resolve( const Target& target, bool useCache, Resolved done );

    // The structured answer as rich text in which every plugin string is escaped.
    static QString render( const QJsonObject& tooltip, const QString& iconHtml = {} );
    // render() with the answer's icon resolved through this view's icons, as the GUI shows it.
    QString renderWithIcon( const QJsonObject& tooltip ) const;

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    struct Entry {
        QJsonObject tooltip;
        qint64 expires = 0;
        quint64 used = 0;
        QString elementId; // for invalidation by element, row or node
        QString rowId;
        QString nodeId;
    };
    struct Hover {
        QPointer<QWidget> widget;
        Target target;
        QPoint globalPosition;
    };
    bool showTooltip( QWidget* widget, const QPoint& position, const Target& target );
    void startQuery( const Target& target );
    // `generation`: the invalidation generation when the query was sent; an answer that arrives
    // after an invalidation is shown but not kept.
    void answered( const Target& target, bool valid, const QJsonObject& tooltip,
                   quint64 generation );
    void showLoading();
    void leave();
    const QJsonObject* cached( const QString& cacheKey );
    void store( const Target& target, const QJsonObject& tooltip, quint64 generation );

    PluginViewQueries queries_;
    QHash<QWidget*, Locator> locators_;
    QHash<QString, Entry> cache_;
    quint64 useCounter_ = 0;
    quint64 generation_ = 0; // increases with every invalidation
    QElapsedTimer clock_;
    Hover hover_;
    QString pendingId_;  // the hover's query
    QString pendingKey_; // its cache key
    bool loadingShown_ = false;
    QTimer loadingTimer_;
};
