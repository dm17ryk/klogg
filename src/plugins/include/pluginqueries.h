// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QVariantList>
#include <deque>
#include <functional>
#include <map>
#include <memory>

class QTimer;

namespace cilogg::plugins {
// Host queries (docs/plugin-sdk-advanced.md section 5): JSON-RPC requests from the host to a
// plugin with ids `q:<n>`, a timeout, `query/cancel`, at most MaxOutstanding per plugin, one
// query per exclusive key, a circuit breaker per plugin and method, and statistics. The GUI thread
// never waits: the answer arrives through a callback.
class PluginQueries final : public QObject {
    Q_OBJECT
public:
    static constexpr int MaxOutstanding = 8;
    static constexpr int BreakerFailures = 5;
    static constexpr int BreakerPauseMs = 60000;

    enum class Status {
        Answered,  // a valid, non-empty result
        Nothing,   // null, an empty result: valid, the target falls back
        Error,     // a JSON-RPC error answer
        Invalid,   // a result the validator refused
        TimedOut,  // no answer in time; `query/cancel` was sent
        Cancelled, // dropped: replaced, the plugin stopped or the scope closed
    };
    struct Outcome {
        Status status = Status::Nothing;
        QJsonValue result; // the validated result for Answered
        QString error;
        qint64 latencyMs = 0;
    };
    using Callback = std::function<void( const Outcome& outcome )>;
    // Validates a result: returns the normalized value; sets `error` for an invalid result. An
    // empty object or null means "nothing".
    using Validator = std::function<QJsonValue( const QJsonValue& result, QString* error )>;
    // Writes one frame to the plugin. `id` is empty for a notification. Returns false when the
    // frame was not sent (the plugin is not ready or not reading its input).
    using Sender = std::function<bool( const QString& pluginId, const QString& id,
                                       const QString& method, const QJsonObject& params )>;

    struct Request {
        QString pluginId;
        QString method;
        QJsonObject params;
        int timeoutMs = 0;
        // A new query with the same key replaces the pending one (one tooltip per view).
        QString exclusiveKey;
        // dropScope() forgets every query whose scope starts with its prefix (a closed view).
        QString scope;
    };

    explicit PluginQueries( Sender sender, QObject* parent = nullptr );
    ~PluginQueries() override;

    // Sends a query. Returns its id, or an empty string with `reason` when it was not sent
    // (paused by the circuit breaker, too many outstanding, the plugin not ready or not reading).
    QString send( const Request& request, Validator validator, Callback callback,
                  QString* reason );
    // The host stopped waiting (the pointer left the target): forgets the query without calling
    // its callback and sends `query/cancel`.
    void cancel( const QString& id );
    // Handles a response whose id is a query id. Returns false for any other id.
    bool handleResponse( const QString& pluginId, const QJsonObject& frame );
    // The plugin stopped, restarted, was disabled or removed: its queries end as Cancelled
    // without `query/cancel`.
    void dropPlugin( const QString& pluginId );
    // A view closed: its queries end as Cancelled and get `query/cancel`.
    void dropScope( const QString& pluginId, const QString& scopePrefix );
    // Statistics per method of one plugin: method, sent, answered, timedOut, failed, medianMs,
    // p95Ms, pausedUntil (ISO date, empty when not paused).
    QVariantList statistics( const QString& pluginId ) const;
    bool paused( const QString& pluginId, const QString& method ) const;
    int outstanding( const QString& pluginId ) const;
    // Tests shorten the pause.
    void setBreakerPauseMs( int milliseconds );

Q_SIGNALS:
    // A method of a plugin was paused or resumed by the circuit breaker.
    void breakerChanged( const QString& pluginId, const QString& method, bool paused );

private:
    struct Pending {
        QString pluginId;
        QString method;
        QString exclusiveKey;
        QString scope;
        Validator validator;
        Callback callback;
        std::unique_ptr<QTimer> timer;
        qint64 started = 0;
    };
    struct MethodStats {
        quint64 sent = 0;
        quint64 answered = 0;
        quint64 timedOut = 0;
        quint64 failed = 0;
        int consecutiveFailures = 0;
        bool invalidLogged = false;
        qint64 pausedUntil = 0; // clock_ milliseconds; 0 when not paused
        std::deque<qint64> latencies;
    };
    // `id` by value: callers may pass the key of the pending_ node that finish() erases.
    void finish( QString id, Outcome outcome, bool sendCancel );
    void record( const QString& pluginId, const QString& method, const Outcome& outcome );
    MethodStats& stats( const QString& pluginId, const QString& method );

    Sender sender_;
    QElapsedTimer clock_;
    quint64 nextId_ = 0;
    int pauseMs_ = BreakerPauseMs;
    std::map<QString, Pending> pending_;
    std::map<QString, std::map<QString, MethodStats>> stats_; // plugin -> method -> statistics
};
} // namespace cilogg::plugins
