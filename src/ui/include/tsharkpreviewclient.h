// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "previewworkerclient.h"

// The TShark/Lua preview backend (MD03a): a persistent TShark session per preview owner, run by
// message_decoder_preview_worker.py on the shared supervised worker transport.
class TsharkPreviewClient : public PreviewWorkerClient {
public:
    // A result's rows (#183).
    static constexpr int FieldLimit = 2048;
    // What the worker reads of TShark's output for one message (#183, #198). A tree past the
    // items or bytes keeps the part read and its result names the limit (`pdmlItems`,
    // `pdmlBytes`); a tree deeper than the depth limit or diagnostics past their limit fail.
    // Mirrors PDML_NODE_LIMIT, PDML_LIMIT, PDML_DEPTH_LIMIT and STDERR_LIMIT in
    // message_decoder_preview_worker.py (test_host_mirrors_the_workers_limits).
    static constexpr int PdmlItemLimit = 8192;
    static constexpr int PdmlByteLimit = 4 * 1024 * 1024;
    static constexpr int PdmlDepthLimit = 32;
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
