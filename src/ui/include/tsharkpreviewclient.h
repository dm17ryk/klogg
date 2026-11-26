// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "previewworkerclient.h"

// The TShark/Lua preview backend (MD03a): a persistent TShark session per preview owner, run by
// message_decoder_preview_worker.py on the shared supervised worker transport.
class TsharkPreviewClient : public PreviewWorkerClient {
public:
    // A result's rows (#183).
    static constexpr int FieldLimit = 2048;
    // What the worker reads of TShark's output (#183): past these the whole preview fails
    // (`field_limit`, `output_limit`). Mirrors PDML_NODE_LIMIT, PDML_LIMIT and STDERR_LIMIT in
    // message_decoder_preview_worker.py.
    static constexpr int PdmlItemLimit = 8192;
    static constexpr int PdmlByteLimit = 4 * 1024 * 1024;
    static constexpr int DiagnosticByteLimit = 64 * 1024;
    // The result body limits a request may choose (#183), in bytes; 1 MiB unless it chooses.
    static constexpr int ResultLimitMin = 128 * 1024;
    static constexpr int ResultLimitDefault = 1024 * 1024;
    static constexpr int ResultLimitMax = 4 * 1024 * 1024;

    static TsharkPreviewClient& instance();
    // `resultLimit`: the largest result body, in bytes, within [ResultLimitMin, ResultLimitMax].
    void evaluate( QObject* sessionOwner, const QVariantMap& profile, const QByteArray& payload,
                   int timeoutMs, const QString& requestId, Completion done,
                   int resultLimit = ResultLimitDefault );

private:
    explicit TsharkPreviewClient( QObject* parent );
};
