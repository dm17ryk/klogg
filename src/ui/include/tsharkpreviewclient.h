// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "previewworkerclient.h"

// The TShark/Lua preview backend (MD03a): a persistent TShark session per preview owner, run by
// message_decoder_preview_worker.py on the shared supervised worker transport.
class TsharkPreviewClient : public PreviewWorkerClient {
public:
    static TsharkPreviewClient& instance();
    void evaluate( QObject* sessionOwner, const QVariantMap& profile, const QByteArray& payload,
                   int timeoutMs, const QString& requestId, Completion done );

private:
    explicit TsharkPreviewClient( QObject* parent );
};
