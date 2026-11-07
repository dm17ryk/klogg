# D01b bounded shared writer

Date: 2026-11-07. Branch: `codex/terminal-shared-writer`. Baseline:
`6de6a3b941483fc26cb2e37509c6662199f68d55`, after PRs #96/#97 merged.

This is an unused, test-only communication preparation. It implements the next
shared-writer slice from [D01a](d01a-communication-contracts.md), under the
[D01b plan](../../plans/cilogg-workbench-2026-10-21/slices/D01b-shared-writer.md).
It does not migrate StreamSession, SerialCaptureWorker or Actions, enable manual
terminal input, or accept formal D01/F01/UT01/UT02/MD06/W0 gates. D01a's merged
history remains in PROGRESS.json.previousPreparation.

## Implemented contract

`cilogg::communication::SharedWriter` is a single-owner deterministic state
machine. Its owner supplies monotonic nanoseconds, readiness and backend
acknowledgments. It owns no device, event loop, thread, timer, endpoint lease or
authorization decision. `WriteProducer` is trusted arbitration metadata.

| Event | Result |
| --- | --- |
| Submit | Checked epoch, capabilities, producer, writer limits and exact retained identity; one frozen deadline including queue/backpressure time. |
| Take | One private, bounded FIFO offer, beginning at the acknowledged accepted offset. No repeated unresolved offer. |
| Acceptance > 0 | Records only the accepted prefix; waits for separately acknowledged written progress. |
| Acceptance = 0 | Latches endpoint backpressure until explicit readiness or confirmed reset. Cancellation, expiry, mode switches and retained-history eviction do not clear it. |
| Acceptance = -1 | Explicit rejection resolves this offer's acceptance to zero; faults the writer without replaying its unsent suffix. Earlier prefix counts remain intact. |
| Generic error | Faults admission/dispatch and queued work, while preserving unknown acceptance or drain until acknowledged or the backend is retired. |
| Cancel or deadline | Stops unsent bytes; an issued/accepted prefix still holds the FIFO barrier. First termination cause remains visible. |
| Mode change | Cancels old-mode requests and blocks new admission until old I/O resolves; only then commits ManualArmed or Automatic. |
| Close | Closes core admission and revokes arming, without asserting physical closure or written completion. |
| Confirmed reset | Requires core closure plus a strictly higher generation, after the owner has acknowledged real backend retirement. Keeps acknowledged prefixes, marks unresolved I/O `ioUnconfirmed`, fences old callbacks and never replays bytes. |

`WriteObservation` combines the existing checked receipt with
`awaitingAcceptance`, `awaitingDrain` and `ioUnconfirmed`. Future adapters must
preserve these fields; serializing the receipt alone would lose uncertainty.
`written_to_backend` never means delivery to the peer. No second receipt codec
or fabricated source provenance was introduced.

## Bounds and privacy

The writer freezes D01a limits for its lifetime: defaults are 8 MiB total resident
private request bytes, 64 KiB offers, 1 MiB requests and 256 records. Active and
retained payloads share the same byte/count bounds. Admission first checks whether
active entries leave room, then evicts only oldest finished identities. An issued
or draining request cannot be evicted. Finished identities expire ten minutes
after retirement; retries and queries do not renew them. Deduplication applies
only while an ID remains retained.

One returned private offer can additionally hold at most one chunk. The future
owner must bound its copies and transport buffering; the resident accounting is
not a claim about whole-process RSS or arbitrary external copies.

Clock, deadline-addition, retention-addition and ticket arithmetic are checked.
Stale contexts/tickets and impossible acknowledgment counts do not advance the
clock. Exhausted drain deadlines are excluded from `nextWakeNs` to avoid timer
spinning. A pending terminal I/O barrier exposes `resetRequired`.

Category `cilogg.communication.writer` records static decisions, generations,
tickets, byte counts, deadlines, queue/cancellation/mode/fault/retirement events.
It accepts no backend error text and logs no payload or payload fingerprint.
The native privacy fixture captures real enabled diagnostics and receipt JSON,
and checks plaintext, compact/spaced hex, base64 and SHA-256 forms of a synthetic
secret containing NUL/high-bit bytes. Backend offers retain its exact bytes.

## Local verification

Windows x64, MSVC Community 17.14 / 19.44, approved Qt 6.10.1. Builds used the
existing configured `build_root`, CL=/MP2 and CMake --parallel 1. Selected targets
were `cilogg_communication_contracts`, `cilogg_shared_writer` and
`cilogg_communication_tests`; unchanged dependency libraries were reused. This is
not a fresh aggregate `ci_build` or a Qt 6.12/platform release qualification.

- Factory RED: two cases failed at missing behavior, then GREEN 39 assertions.
- Admission RED: two cases failed at missing behavior, then full communication
  GREEN 645 assertions / 26 cases. Review's invalid-producer and one-record-limit
  fixture gaps were added and pass in the final suite.
- Progress RED: three cases failed because take was absent, then full
  communication GREEN 832 assertions / 29 cases. Includes 2/0/3 acceptance,
  independently partial drain, FIFO isolation, all 256 binary byte values,
  overcounts, stale tickets and nonmutating invalid clocks.
- Lifecycle RED: eight of nine cases failed at missing lifecycle/eviction rules;
  the existing privacy behavior was already GREEN. A logging-hook compile error
  was fixed before this RED run and is not counted as product evidence.
- Review regression RED: endpoint backpressure disappeared after cancellation;
  the latch fix covers both cancellation and expiry. Final selected build exit 0;
  complete communication suite GREEN **1,590 assertions / 40 cases**.
- Full local registered CTest: **10/10 groups passed in 381.89 seconds**;
  wrapper exit 0 in 382.02 seconds. Includes 1,590 communication assertions,
  1,045 terminal-model assertions, GUI integration and MCP/GUI smoke checks.
  Decoder reference tests retain 48 optional skips; parity self-tests retain
  one skip. Those skipped checks are not accepted by this run.

The full run preceded a final behavior-equivalent explicit empty initializer for
`Record::retiredAt` (GCC/Clang warning compatibility). The selected native targets
were then rebuilt and all 1,590 communication assertions passed again. The full
suite was not unnecessarily repeated for that initializer-only adjustment.

Commands use `cmake --build build_root --config RelWithDebInfo --target
cilogg_shared_writer cilogg_communication_tests --parallel 1` in the prescribed
developer environment; the test executable is
`build_root/communication_tests/RelWithDebInfo/cilogg_communication_tests.exe
[communication]`. Full CTest runs all ten discovered registrations in a private,
deployed runtime, including the newly built communication executable. Existing
application/test binaries have source-equivalent baseline product code; they were
not rebuilt by this unused-library slice. No user capture is stopped.

Ignored local evidence: `build_root/d01b-*-build.log`,
`build_root/d01b-*-test.log` and corresponding JSON; `build_root/d01b-final-runtime.json`,
`build_root/d01b-final-ctest-registration.json` and
`build_root/d01b-final-full-ctest.log`/`.json`. Historical combined baseline
qualification at `1074009a` remains historical, not a new D01b run.

## Review, indexes and remaining gates

Fresh C++ review found one backpressure-latch defect; a genuine regression failure
preceded its fix, and the reviewer confirmed resolution. Fresh general/privacy
review found no actionable code issues. Its canonical-document followup also
approved unchanged formal task records, preserved history and the D01c boundary.
The package validator passes all 119 tasks, 201 requirements and 564 local links.
Staged blob hashes are checked before publication.
The manifest also includes the existing `MD03a-preview-integration.md` slice,
which was already tracked but absent from the prior checksum list.

CodeReviewGraph refreshed the new code successfully. CodeGraph reports this
checkout unindexed; it was not initialized. GitNexus refresh was attempted again
and failed after 307.40 seconds of parser retries with native `0xC0000409`.
Its new-symbol impact is UNKNOWN and FTS is unavailable. Staged detection initially
returned empty; the final attempt saw twelve files but resolved only four document
headings, missing the new C++ symbols. Direct staged diffs and C++/general review therefore
provide the scope check; stale graph output is not verification. The existing
modified `.gitnexus/lbug` is excluded from commits.

Hosted Windows/macOS/Linux and Qt 5/6 acceptance, installed packages, actual
devices, timing/RSS measurements and the formal task gates remain separate.
The next preparation is D01c's existing-owner adapter plan: real readiness,
acceptance/drain/closure, epoch-fenced I/O, atomic endpoint leases and compatible
capture/privacy. First public integration must expose GUI, Commander/CLI,
native MCP and Python together. Public terminal attachment and broader TShark
decoder-core acceptance are not completed by this core.
