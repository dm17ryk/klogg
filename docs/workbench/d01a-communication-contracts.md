# D01a checked communication contracts

This independent preparation implements unused checked values for the terminal's shared-session foundations. It introduces no application consumer, transport, settings, device writes, terminal attachment or public Commander operation. Formal D01/UT02 and decoder-core acceptance remains unchanged. The next owner must implement the shared writer/arbitration and endpoint lifecycle before attaching the terminal, with GUI/CLI/native-MCP/Python parity in the first public increment.

## Contract

The QWidget-independent values reuse DataResult/DataError and never fabricate SourceLocation. Private constructors admit positive bounded StreamLimits, explicitly owned WriteRequest bytes/IDs, capability metadata and WriteReceipt snapshots. QString/QByteArray borrowed views are deep-copied only after field validation. Copies of admitted values retain an owned snapshot.

Ceilings are 16 endpoints, 256 retained receipts, 8 MiB RX, 8 MiB pending TX, 64 KiB chunks, 1 MiB writes and 600,000 ms retention. Positive lower limits are supported; chunk<=write<=pendingTX. The value types do not enforce queues, retention, ownership leases, I/O deadlines or state transitions themselves. A nonnegative supplied absolute deadline is metadata, not a freshness check.

IDs are1–128 UTF-16 units, valid Unicode, excluding NUL/CR/LF. Capability limitations are at most 16 strings of 256 units. Receipt error codes/messages are at most 64/512 units. Receipt counts obey written<=accepted<=requested<=write limit; queued has no accepted bytes, accepted has a positive prefix, backend-written has the full requested prefix. Failed/cancelled stages require their corresponding error metadata; successful stages have empty errors. These are snapshot checks, not evidence of actual transmission or peer receipt.

The version 1 metadata object has exactly 11 keys. Generations and counts are canonical unsigned decimal strings, preserving UINT64_MAX without JSON numeric precision loss. The compact UTF-8 encoding ceiling is 4096 bytes. Input is an existing QJsonObject; Qt string extraction may allocate. Raw loaders must bound frames before JSON parsing. This is not canonical JSON, lexical duplicate-key checking, authentication, authorization or a scrubber for externally supplied metadata.

forRequest derives ID, generation, true payload size and redacted/omitted disposition from the checked request. Its failure messages come from a static enum mapping. Neither receipt disposition includes payload bytes. The private cilogg.communication.contracts category logs bounded decisions and valid metadata/counts, never request bodies, encodings or hashes. A caller may put arbitrary content in valid metadata; the codec does not classify that content as safe.

## Verification in progress

Baseline: merged master 43e00df689d3ccdb8fe918fb91aa36333ff18d71; plan head 50c34e5d790647f7ebc17345d4b962e400f28a70 has identical product source. Tests are in a separately deployed cilogg_communication_tests target, outside packaged application output, registered as communication_contract_tests with a 60 s timeout.

Local toolchain: Windows x64, complete VS2022 Community/MSVC 19.44.35207, Qt 6.10.1, standalone CMake 4.4. The former cached Enterprise installation was incomplete and its compiler files unavailable. Private local cache/source-alias corrections were required; no product or CI toolchain change was made.

- Limits RED: 2 failed cases/11 failed assertions against a compiling absent-behavior seam; GREEN: 55 assertions / 2 cases.
- Requests/capabilities RED: 10 failed cases/18 failed assertions. GREEN: 147 assertions / 12 cases for all first-stage values, including the registered CMake-built/deployed executable.
- Receipts RED: 5 failed cases/9 failed assertions. Receipt-core GREEN: 94 assertions / 3 cases.
- Codec RED: 4 failed cases / 5 failed assertions after valid receipt prerequisites. Encoder/receipt GREEN: 131 assertions / 5 cases. Decoder-specific RED: 2 failed cases / 2 failed assertions. Full contract GREEN: 504 assertions / 20 cases, before formatting; the final formatted source will be rebuilt.
- Native first-stage registered target build/deploy passed; full ci_build, full CTest, final reviews, hosted CI and cross-platform qualification remain pending.

Private exact RED/GREEN/build logs use build_root/f01e-d01a-* names and are not distributed. Runtime TShark qualification is a separate workstream and cannot be inferred from these value tests. Qt5, Windows ARM64 and accelerated-search configuration qualification remains unverified locally.

## Code intelligence and review boundary

GitNexus upstream checks for the new classes/codec functions returned target-not-found/riskUNKNOWN in the stale index; its FTS extension is unavailable. Direct source scope has only the new isolated tests as consumers. CodeGraph reported the checkout unindexed. CodeReviewGraph refresh and staged impact inspection will be repeated before publication; no empty graph result is treated as proof of zero risk.
