// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <functional>

namespace cilogg::preview::detail {
// Client-level explicit and bundled choices retain precedence over this PATH fallback, which
// follows pythonruntime::pathInterpreterCandidates() (python.exe before launcher scripts).
QString findPythonOnPath();
// The interpreter a preview worker runs with: CILOGG_SCRIPT_PYTHON, a bundled interpreter, then
// PATH. Empty when none is found.
QString previewPython();
} // namespace cilogg::preview::detail

// The supervised preview worker transport of MD03a, shared by the TShark/Lua backend and the
// Python decoder backend (MD03b): one persistent Python worker per preview owner (channel), at
// most two warm workers, each in a process group (Unix) or job object (Windows) that ends its
// descendants. Requests and responses are four-byte big-endian length-prefixed JSON frames.
// A deadline, a cancellation, a configuration change or a protocol error retires the worker; the
// next request starts a new one.
class PreviewWorkerClient : public QObject {
public:
    using Completion = std::function<void( QVariantMap )>;
    struct Backend {
        QString name;          // the results' `backend`
        QString operation;     // the requests' `op`
        QString offsetSpace;   // the results' `offsetSpace`
        QString script;        // the resource that runs the worker
        QStringList resources; // every resource the worker needs, the script included
        // Whether a response is valid for request `id` (beyond the frame and JSON checks).
        std::function<bool( const QJsonObject&, const QString& )> validResponse;
        // Whether a failed response ends the worker (TShark) or leaves it running (Python
        // decoder failures, which the worker reports and survives).
        bool failureEndsWorker = true;
    };

    ~PreviewWorkerClient() override;
    void cancel( const QString& requestId, const QString& reason = "cancelled" );
    void reset();
    const Backend& backend() const
    {
        return backend_;
    }

protected:
    PreviewWorkerClient( QObject* parent, Backend backend );
    // Sends `fields` (the backend's request members) with `payload`, the deadline and the ID.
    void submit( QObject* sessionOwner, const QVariantMap& fields, const QByteArray& payload,
                 int timeoutMs, const QString& requestId, Completion done );

private:
    class Session;
    QVariantMap failure( const QString& reason ) const;
    Backend backend_;
    QList<Session*> sessions_;
    quint64 clock_ = 0;
};
