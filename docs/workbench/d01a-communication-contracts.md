# D01a checked communication contracts

This independent preparation implements unused checked values for the terminal's shared-session foundations. It introduces no application consumer, transport, settings, device writes, terminal attachment or public Commander operation. Formal D01/UT02 and decoder-core acceptance remains unchanged. The next owner must implement the shared writer/arbitration and endpoint lifecycle before attaching the terminal, with GUI/CLI/native-MCP/Python parity in the first public increment.

## Contract

The QWidget-independent values reuse DataResult/DataError and never fabricate SourceLocation. Private constructors admit positive bounded StreamLimits, explicitly owned WriteRequest bytes/IDs, capability metadata and WriteReceipt snapshots. QString/QByteArray borrowed views are deep-copied only after field validation. Copies of admitted values retain an owned snapshot.

Ceilings are 16 endpoints, 256 retained receipts, 8 MiB RX, 8 MiB pending TX, 64 KiB chunks, 1 MiB writes and 600,000 ms retention. Positive lower limits are supported; chunk<=RX and chunk<=write<=pendingTX. The value types do not enforce queues, retention, ownership leases, I/O deadlines or state transitions themselves. A nonnegative supplied absolute deadline is metadata, not a freshness check.

IDs are1–128 UTF-16 units, valid Unicode, excluding NUL/CR/LF. Capability limitations are at most 16 strings of 256 units. Receipt error codes/messages are at most 64/512 units. Receipt counts obey written<=accepted<=requested<=write limit; queued has no accepted bytes, accepted has a positive prefix, backend-written has the full requested prefix. Failed/cancelled stages require their corresponding error metadata; the reserved cancelled code is valid only for the Cancelled stage; successful stages have empty errors. These are snapshot checks, not evidence of actual transmission or peer receipt.

The version 1 metadata object has exactly 11 keys. Generations and counts are canonical unsigned decimal strings, preserving UINT64_MAX without JSON numeric precision loss. The compact UTF-8 encoding ceiling is 4096 bytes. Input is an existing QJsonObject; Qt string extraction may allocate. Raw loaders must bound frames before JSON parsing. This is not canonical JSON, lexical duplicate-key checking, authentication, authorization or a scrubber for externally supplied metadata.

forRequest derives ID, generation, true payload size and redacted/omitted disposition from the checked request. Its failure messages come from a static enum mapping. Neither receipt disposition includes payload bytes. The private cilogg.communication.contracts category logs bounded decisions and valid metadata/counts, never request bodies, encodings or hashes. A caller may put arbitrary content in valid metadata; the codec does not classify that content as safe.

## Local verification and remaining qualification

Baseline: merged master 43e00df689d3ccdb8fe918fb91aa36333ff18d71; plan head 50c34e5d790647f7ebc17345d4b962e400f28a70 has identical product source. Tests are in a separately deployed cilogg_communication_tests target, outside packaged application output, registered as communication_contract_tests with a 60 s timeout.

Local toolchain: Windows x64, complete VS2022 Community/MSVC 19.44.35207, Qt 6.10.1, standalone CMake 4.4. The former cached Enterprise installation was incomplete and its compiler files unavailable. Private local cache/source-alias corrections were required; no product or CI toolchain change was made.

- Limits RED: 2 failed cases/11 failed assertions against a compiling absent-behavior seam; GREEN: 55 assertions / 2 cases.
- Requests/capabilities RED: 10 failed cases/18 failed assertions. GREEN: 147 assertions / 12 cases for all first-stage values, including the registered CMake-built/deployed executable.
- Receipts RED: 5 failed cases/9 failed assertions. Receipt-core GREEN: 94 assertions / 3 cases.
- Codec RED: 4 failed cases / 5 failed assertions after valid receipt prerequisites. Encoder/receipt GREEN: 131 assertions / 5 cases. Decoder-specific RED: 2 failed cases / 2 failed assertions. Initial full contract GREEN: 504 assertions / 20 cases before formatting; the formatted native build also passed 504 assertions / 20 cases.
- PR #96 review regressions: the two new cases failed with six assertions before the cancellation-stage and RX/chunk policy guards were added. A strict manual native MSVC build passed 519 assertions / 22 cases after both fixes. The registered communication_contract_tests then passed the same 519 assertions / 22 cases (1/1 registered test, 5.35 s total CTest time). The subsequent full-suite result is recorded below.
- Combined local qualification of the PR #96 review fixes with the PR #97 test compile fix: native ci_build passed (exit 0, 1013.01 s), then the isolated full registered CTest suite passed all 10 groups (655.80 s CTest time; 656.19 s wrapper time; exit 0). Existing application/unit, GUI, CLI and native-MCP self-tests passed. The terminal-model group passed 1045 assertions / 23 cases, and communication_contract_tests passed 519 assertions / 22 cases (0.33 s in the full run).
- Fresh focused C++ and general/privacy reviews approved the review-fix delta with zero findings.

This is combined local qualification, not an isolated hosted run of PR #96. The private build_root/qualification-integration.json report records testedSourceEqualsPublished=true for the PR #96 publication commit 6859691b6764899fb84ed721906a0af104ac7cc9, including the changed communication implementation and fixture hashes. The build/run started from qualification head e903e455565bb0b490162cb4d4632c357e661d97 with the then-uncommitted P2 fixes; the exact published blobs were subsequently integrated as qualification commit ed41f3f473841c08c7be88c72869f5f0e9b13577. This evidence does not accept formal D01/UT02, decoder-core or terminal milestones.

Local qualification used Qt Core 6.10.1 with KLOGG_USE_HYPERSCAN=OFF and KLOGG_USE_VECTORSCAN=OFF. The isolated runtime was C:/ESSENC~1/temp/cilogg-merge-validation-859dpozm; private full-suite records are build_root/preview-terminal-qualified-full-ctest.log and its JSON result, with runtime binary hashes in build_root/preview-terminal-qualified-runtime.json. Other private RED/GREEN/build logs use build_root/f01e-d01a-* names and are not distributed. Runtime TShark qualification is a separate workstream and cannot be inferred from these value tests. Hosted CI, Qt5, Windows ARM64, accelerated-search configurations and cross-platform qualification remain open. No static-analyzer or hardware/device-I/O pass is claimed. Keep PR #96 draft until its pending hosted checks are reviewed.

## macOS C++17 logging follow-up

The four macOS jobs at PR head 73adf063 failed compiling the same 13 stream-style
category logging calls. Apple Clang treats omission of the variadic macro
argument as a C++20 extension under the existing C++17 warnings-as-errors policy.
The calls now supply an explicitly empty argument, retaining the exact Qt
category guard, severity, stream operands and privacy behavior. Compiler warning
policy and language level are unchanged.

A focused Clang/actual-Qt-6.10.1-header fixture reproduced two omitted-argument
errors with the original qCDebug/qCWarning forms (exit 1), then compiled the
corrected forms (exit 0) under the same strict warning. Standalone attempts to
compile the complete translation unit encountered dependency-include setup gaps
and are not acceptance evidence. C++ and general/privacy reviews approved the
13-call delta without findings. The corrected implementation's frozen Git blob
is 6f88e93f38a1e4c99800dc48c86be1e91486642b.

Combined native qualification of this follow-up and the PR97 decoder-watch
repair used qualification commit 1074009a4b9a45fc0d9ab23d0c90ced793034a97.
The selected-target native build passed with exit 0 in 706.56 s; this was not
an aggregate ci_build invocation. A fresh isolated runtime then passed all ten
registered CTest groups in 247.93 s (wrapper exit 0, 248.106 s), including
519 communication-contract assertions in 22 cases and 1045 terminal-model
assertions in 23 cases. The corrected implementation blob above matches the
tested source. The same Windows MSVC 19.44 / Qt 6.10.1, Qt-only environment was
used; these results do not replace hosted macOS or other-platform qualification.

The native evidence below lives in the primary qualification checkout,
C:/Essence_SC/home/.codex/worktrees/linked-regex-charts-p06/klogg, rather than
the secondary PR96 publication checkout. Private evidence is in
build_root/ci96-97-green-full-ctest.log and its JSON result, with binary/source
identities in build_root/ci96-97-green-runtime.json.
The isolated runtime was C:/ESSENC~1/temp/cilogg-merge-validation-xydxg_xp.

Dedicated public TShark qualification on that same frozen runtime then passed
all 57 strict normal CLI/native-MCP comparisons. Both overflow responses retained
291 exact whole rows with their explicit limited-result metadata. The hover
test exited normally with status 0; its independent observer recorded zero
active jobs, zero surviving observed process handles and no missing required
images before outer cleanup, observing 25 processes. These checks used no
interpreter override, bundled Python candidate or private input. Parity success
alone is not independent leak proof. Their summary lives in the secondary
validation/publication checkout at
build_root/preview-final-qualification/post-ci-1074009-normal/post-ci-summary.json.
These combined local checks do not establish formal D01/UT02 or interactive
terminal acceptance.

## Code intelligence and review boundary

GitNexus upstream checks for the new classes/codec functions returned target-not-found/riskUNKNOWN in the stale index; its FTS extension is unavailable. Direct source scope has only the new isolated tests as consumers. CodeGraph reported the checkout unindexed. No empty graph result is treated as proof of zero risk.

For PR #96 review-fix publication, exact-checkout GitNexus upstream checks for checkedReceipt and StreamLimits::fromDefinition and staged detect-changes reported the secondary checkout unregistered; no sibling index was substituted. CodeReviewGraph refreshed that exact checkout successfully (9 files reparsed, 200 nodes and 3497 edges updated, zero errors).

Post-publication CodeReviewGraph refreshes succeeded on both the qualification and publication checkouts with matching commit hashes, zero changed files and zero errors. The explicitly requested, bounded primary GitNexus analyze attempt failed with native exit 3221226505 (0xC0000409) after 237.83 s, following parser-worker idle/split handling and an exhausted 150 s file budget on mainwindow.cpp. The owned runner closed remaining descendants; this is not a successful analysis or normal-exit claim, and the attempt was not repeated. GitNexus remains stale and unqualified; unavailable FTS and the unregistered secondary checkout remain limitations.
