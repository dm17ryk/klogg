// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "previewworkerclient.h"

// E16b (docs/plugin-repositories.md 4.4): finds the decoders among the enabled configuration
// packs' Python modules. A supervised worker (message_decoder_python_entries.py) gives each module
// to Python's own parser and compiler, so nothing in it runs and nothing is imported, and reports
// whether `decode` may be bound to something callable after the module's top level. It runs with
// the preview's interpreter (CILOGG_SCRIPT_PYTHON, a bundled one, then PATH), answers once and
// exits.
class PythonEntriesClient : public PreviewWorkerClient {
public:
    // How long a check may take: Python's start and up to 64 modules of 1 MiB each.
    static constexpr int TimeoutMs = 10000;

    static PythonEntriesClient& instance();
    // Checks the modules of `packs` (`{id, root, decoders: [{id, module}]}`, as
    // PreviewDecoderPacks::python lists them). `done` gets the worker's answer, `modules` with one
    // `{decoder, entry, reason, line, detail}` per listed module in order, or a failure: `state`
    // "failed" and the `reason` ("python_unavailable" when no interpreter was found, "timeout",
    // "configuration_changed" when the preview settings changed during the check, ...).
    void check( QObject* owner, const QVariantList& packs, const QString& requestId,
                Completion done );

private:
    explicit PythonEntriesClient( QObject* parent );
};
