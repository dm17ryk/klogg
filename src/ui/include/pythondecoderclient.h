// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "previewworkerclient.h"

// The Python decoder backend of the preview (MD03b, docs/plugin-repositories.md 4.4): one
// supervised worker per preview session (message_decoder_python_worker.py) copies and hashes the
// enabled configuration packs' Python decoder modules when it starts, gives each pack an import
// namespace of its own, and calls `decode(payload, context)` of the routed decoder. Results are
// PreviewService result v1 rows (backend `python_decoder`, offsets in `decoder_payload`). A
// decoder failure fails its request and keeps the worker; a deadline, a crash or a protocol
// error ends the worker and its descendants, and the next request starts a new one.
class PythonDecoderClient : public PreviewWorkerClient {
public:
    static PythonDecoderClient& instance();
    // Decodes `payload` with `decoder` (`<pack-id>/<decoder-id>`) of `profile` (`{packs}`, the
    // snapshot's `packPython`). `context` holds `preview`, `protocol`, `inputEncoding` and
    // `direction`.
    void decode( QObject* sessionOwner, const QVariantMap& profile, const QString& decoder,
                 const QVariantMap& context, const QByteArray& payload, int timeoutMs,
                 const QString& requestId, Completion done );

private:
    explicit PythonDecoderClient( QObject* parent );
};
