// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QVariantMap>
#include <functional>

namespace cilogg::preview::detail {
// Client-level explicit and bundled choices retain precedence over this PATH fallback.
QString findPythonOnPath();
} // namespace cilogg::preview::detail

class TsharkPreviewClient : public QObject {
public:
    using Completion = std::function<void( QVariantMap )>;
    static TsharkPreviewClient& instance();
    ~TsharkPreviewClient() override;
    void evaluate( QObject* sessionOwner, const QVariantMap& profile, const QByteArray& payload,
                   int timeoutMs, const QString& requestId, Completion done );
    void cancel( const QString& requestId, const QString& reason = "cancelled" );
    void reset();

private:
    explicit TsharkPreviewClient( QObject* parent );
    class Session;
    QList<Session*> sessions_;
    quint64 clock_ = 0;
};
