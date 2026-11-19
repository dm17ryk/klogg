// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginqueries.h"

#include "log.h"
#include <QDateTime>
#include <QTimer>
#include <QVariantMap>
#include <algorithm>
#include <utility>
#include <vector>

namespace cilogg::plugins {
namespace {
constexpr size_t MaxLatencySamples = 128;

qint64 percentile( std::vector<qint64> samples, double fraction )
{
    if ( samples.empty() ) {
        return 0;
    }
    std::sort( samples.begin(), samples.end() );
    const auto index = static_cast<size_t>( fraction * static_cast<double>( samples.size() - 1 ) );
    return samples.at( index );
}
} // namespace

PluginQueries::PluginQueries( Sender sender, QObject* parent )
    : QObject( parent )
    , sender_( std::move( sender ) )
{
    clock_.start();
}

PluginQueries::~PluginQueries() = default;

PluginQueries::MethodStats& PluginQueries::stats( const QString& pluginId, const QString& method )
{
    return stats_[ pluginId ][ method ];
}

bool PluginQueries::paused( const QString& pluginId, const QString& method ) const
{
    const auto plugin = stats_.find( pluginId );
    if ( plugin == stats_.end() ) {
        return false;
    }
    const auto found = plugin->second.find( method );
    return found != plugin->second.end() && found->second.pausedUntil > clock_.elapsed();
}

int PluginQueries::outstanding( const QString& pluginId ) const
{
    return static_cast<int>( std::count_if( pending_.begin(), pending_.end(), [ & ]( const auto& item ) {
        return item.second.pluginId == pluginId;
    } ) );
}

void PluginQueries::setBreakerPauseMs( int milliseconds )
{
    pauseMs_ = std::max( 1, milliseconds );
}

QString PluginQueries::send( const Request& request, Validator validator, Callback callback,
                             QString* reason )
{
    const auto setReason = [ reason ]( const QString& text ) {
        if ( reason ) {
            *reason = text;
        }
    };
    auto& methodStats = stats( request.pluginId, request.method );
    if ( methodStats.pausedUntil > 0 && methodStats.pausedUntil <= clock_.elapsed() ) {
        methodStats.pausedUntil = 0;
        LOG_INFO << "Plugin queries resumed plugin=" << request.pluginId.toStdString()
                 << " method=" << request.method.toStdString();
        Q_EMIT breakerChanged( request.pluginId, request.method, false );
    }
    if ( methodStats.pausedUntil > 0 ) {
        setReason( QStringLiteral( "paused" ) );
        return {};
    }
    if ( !request.exclusiveKey.isEmpty() ) {
        const auto previous
            = std::find_if( pending_.begin(), pending_.end(), [ & ]( const auto& item ) {
                  return item.second.pluginId == request.pluginId
                         && item.second.exclusiveKey == request.exclusiveKey;
              } );
        if ( previous != pending_.end() ) {
            finish( previous->first, { Status::Cancelled, {}, {}, 0 }, true );
        }
    }
    if ( outstanding( request.pluginId ) >= MaxOutstanding ) {
        setReason( QStringLiteral( "busy" ) );
        return {};
    }
    const auto id = QStringLiteral( "q:%1" ).arg( ++nextId_ );
    if ( !sender_( request.pluginId, id, request.method, request.params ) ) {
        setReason( QStringLiteral( "not ready" ) );
        return {};
    }
    ++methodStats.sent;
    Pending entry;
    entry.pluginId = request.pluginId;
    entry.method = request.method;
    entry.exclusiveKey = request.exclusiveKey;
    entry.scope = request.scope;
    entry.validator = std::move( validator );
    entry.callback = std::move( callback );
    entry.started = clock_.elapsed();
    entry.timer = std::make_unique<QTimer>();
    entry.timer->setSingleShot( true );
    connect( entry.timer.get(), &QTimer::timeout, this, [ this, id ] {
        finish( id, { Status::TimedOut, {}, QStringLiteral( "timed out" ), 0 }, true );
    } );
    entry.timer->start( std::max( 1, request.timeoutMs ) );
    pending_.emplace( id, std::move( entry ) );
    LOG_DEBUG << "Plugin query sent plugin=" << request.pluginId.toStdString()
              << " method=" << request.method.toStdString() << " id=" << id.toStdString();
    return id;
}

void PluginQueries::cancel( const QString& id )
{
    const auto found = pending_.find( id );
    if ( found == pending_.end() ) {
        return;
    }
    found->second.callback = nullptr; // the requester stopped waiting
    finish( id, { Status::Cancelled, {}, {}, 0 }, true );
}

bool PluginQueries::handleResponse( const QString& pluginId, const QJsonObject& frame )
{
    const auto id = frame.value( "id" ).toString();
    if ( !id.startsWith( QLatin1String( "q:" ) ) ) {
        return false;
    }
    const auto found = pending_.find( id );
    if ( found == pending_.end() || found->second.pluginId != pluginId ) {
        // A late answer, or an id this plugin was never asked: dropped, as for jobs.
        LOG_DEBUG << "Plugin query answer dropped plugin=" << pluginId.toStdString()
                  << " id=" << id.left( 32 ).toStdString();
        return true;
    }
    Outcome outcome;
    if ( frame.contains( "error" ) ) {
        outcome.status = Status::Error;
        outcome.error = frame.value( "error" ).toObject().value( "message" ).toString().left( 256 );
    }
    else {
        QString error;
        const auto value = found->second.validator
                               ? found->second.validator( frame.value( "result" ), &error )
                               : frame.value( "result" );
        if ( !error.isEmpty() ) {
            outcome.status = Status::Invalid;
            outcome.error = error;
        }
        else if ( value.isNull() || value.isUndefined()
                  || ( value.isObject() && value.toObject().isEmpty() ) ) {
            outcome.status = Status::Nothing;
        }
        else {
            outcome.status = Status::Answered;
            outcome.result = value;
        }
    }
    finish( id, outcome, false );
    return true;
}

void PluginQueries::dropPlugin( const QString& pluginId )
{
    QStringList ids;
    for ( const auto& [ id, entry ] : pending_ ) {
        if ( entry.pluginId == pluginId ) {
            ids.append( id );
        }
    }
    for ( const auto& id : ids ) {
        finish( id, { Status::Cancelled, {}, {}, 0 }, false );
    }
}

void PluginQueries::dropScope( const QString& pluginId, const QString& scopePrefix )
{
    QStringList ids;
    for ( const auto& [ id, entry ] : pending_ ) {
        if ( entry.pluginId == pluginId && !entry.scope.isEmpty()
             && entry.scope.startsWith( scopePrefix ) ) {
            ids.append( id );
        }
    }
    for ( const auto& id : ids ) {
        finish( id, { Status::Cancelled, {}, {}, 0 }, true );
    }
}

void PluginQueries::finish( QString id, Outcome outcome, bool sendCancel )
{
    const auto found = pending_.find( id );
    if ( found == pending_.end() ) {
        return;
    }
    auto entry = std::move( found->second );
    pending_.erase( found );
    entry.timer->stop();
    // finish() can run inside the timer's own timeout signal.
    entry.timer.release()->deleteLater();
    outcome.latencyMs = clock_.elapsed() - entry.started;
    LOG_DEBUG << "Plugin query finished plugin=" << entry.pluginId.toStdString()
              << " method=" << entry.method.toStdString() << " id=" << id.toStdString()
              << " status=" << static_cast<int>( outcome.status )
              << " latency_ms=" << outcome.latencyMs;
    if ( outcome.status != Status::Cancelled ) {
        record( entry.pluginId, entry.method, outcome );
    }
    if ( sendCancel ) {
        sender_( entry.pluginId, {}, QStringLiteral( "query/cancel" ), { { "id", id } } );
    }
    if ( entry.callback ) {
        entry.callback( outcome );
    }
}

void PluginQueries::record( const QString& pluginId, const QString& method,
                            const Outcome& outcome )
{
    auto& methodStats = stats( pluginId, method );
    if ( outcome.status == Status::Answered || outcome.status == Status::Nothing ) {
        ++methodStats.answered;
        methodStats.consecutiveFailures = 0;
        methodStats.latencies.push_back( outcome.latencyMs );
        if ( methodStats.latencies.size() > MaxLatencySamples ) {
            methodStats.latencies.pop_front();
        }
        return;
    }
    if ( outcome.status == Status::TimedOut ) {
        ++methodStats.timedOut;
    }
    else {
        ++methodStats.failed;
        // An invalid answer is a plugin bug: logged once per plugin and method (design 5.2).
        if ( outcome.status == Status::Invalid && !methodStats.invalidLogged ) {
            methodStats.invalidLogged = true;
            LOG_WARNING << "Plugin query answer invalid plugin=" << pluginId.toStdString()
                        << " method=" << method.toStdString()
                        << " reason=" << outcome.error.left( 256 ).toStdString();
        }
        else {
            LOG_DEBUG << "Plugin query failed plugin=" << pluginId.toStdString()
                      << " method=" << method.toStdString()
                      << " reason=" << outcome.error.left( 256 ).toStdString();
        }
    }
    if ( ++methodStats.consecutiveFailures >= BreakerFailures && methodStats.pausedUntil == 0 ) {
        methodStats.consecutiveFailures = 0;
        methodStats.pausedUntil = clock_.elapsed() + pauseMs_;
        LOG_WARNING << "Plugin queries paused after " << BreakerFailures
                    << " failures plugin=" << pluginId.toStdString()
                    << " method=" << method.toStdString() << " pause_ms=" << pauseMs_;
        Q_EMIT breakerChanged( pluginId, method, true );
    }
}

QVariantList PluginQueries::statistics( const QString& pluginId ) const
{
    QVariantList result;
    const auto plugin = stats_.find( pluginId );
    if ( plugin == stats_.end() ) {
        return result;
    }
    const auto now = clock_.elapsed();
    for ( const auto& [ method, item ] : plugin->second ) {
        const std::vector<qint64> samples( item.latencies.begin(), item.latencies.end() );
        result.append( QVariantMap{
            { "method", method },
            { "sent", static_cast<qulonglong>( item.sent ) },
            { "answered", static_cast<qulonglong>( item.answered ) },
            { "timedOut", static_cast<qulonglong>( item.timedOut ) },
            { "failed", static_cast<qulonglong>( item.failed ) },
            { "medianMs", percentile( samples, 0.5 ) },
            { "p95Ms", percentile( samples, 0.95 ) },
            { "pausedUntil",
              item.pausedUntil > now
                  ? QDateTime::currentDateTimeUtc()
                        .addMSecs( item.pausedUntil - now )
                        .toString( Qt::ISODateWithMs )
                  : QString{} },
        } );
    }
    return result;
}
} // namespace cilogg::plugins
