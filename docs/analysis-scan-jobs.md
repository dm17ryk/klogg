# Bounded analysis jobs (P03b)

`AnalysisJobService` in `src/logdata` is independent of widgets. This internal
slice supplies snapshot scanning and retained, paged output. Capture evaluation,
public Commander/MCP commands and the Regex Lab are the dependent P03c, P03d and
P04 slices; this slice does not expose an unfinished user feature.

## Snapshot and text contract

A request freezes `SourceSnapshot`, inclusive zero-based physical-line ranges,
scope, decoding generation and request revision. The reader checks published
metadata and file identity before bounded reads. Index publication can request a
retry; generation changes, replacement, truncation and decoding changes reject
the job. Appends may extend the live source but cannot extend the original byte
watermark or provisional final record. Decoders survive batches and skipped rows.

Subjects are decoded raw physical lines before display transformations. LF is
removed; CR in a CRLF record remains, matching `RawLines::decodeLines()`. Display
line reads remove CR separately. Empty files have no subjects; a trailing LF does
not create an additional empty subject. Inline text follows the same splitting
rule. Public result rows use one-based line numbers.

Source owners must call `cancelSource()` when closing a source. Cancellation is
asynchronous. Owner-thread leases survive all worker reads and processor
destruction; final source destruction occurs outside registry iteration and
locks. Finished jobs retain a weak source reference for validated navigation.

## Bounds and lifecycle

The service permits two running jobs and eight additional admitted jobs. A job
reads at most 256 lines and 1 MiB raw bytes per block. It rejects a physical record
over 1 MiB before allocating that record. Pending output is bounded to eight
batches and 4 MiB per job; producers wait for delivery credit, cancellation or
their deadline. Sample scope stops after 1,000 rows or 4 MiB decoded text.

Results retain at most 10,000 records or 16 MiB per job and 64 MiB globally.
Individual serialized rows are limited to 4 MiB. Pages default to 200 records,
allow at most 1,000, and stop at 8 MiB. Opaque continuation tokens are reused per
offset. Metadata is also bounded: at most 26 retained jobs, including empty jobs.
Oldest finished jobs are evicted first; active jobs are never silently evicted.
Finished jobs expire after ten minutes or explicit release.

States are queued, running, completed, cancelled and failed. `complete` is
separate: sample, scope and result limits produce visibly partial completion.
Cancellation is idempotent for retained jobs. Release during work requests
cancellation and removes the job after readers finish. Status reports original
provenance, progress, limits, historical state and newer-data availability.

The default deadline is 60 seconds (maximum ten minutes), including queue and
credit waits. The processor receives a per-batch budget of two seconds (maximum
30 seconds), capped by the remaining job deadline. The P03b raw processor is
cooperative; P03c supplies enforced child-process execution. Only compile,
matching and startup duration counters are accepted from processors.

Diagnostics include IDs, counters, snapshot watermarks and termination reasons;
source bodies are not logged. Remove dependent GUI and public automation slices
before removing the supporting service.

## Verification

`cilogg_itests '[analysis]' -platform offscreen` covers bounded reads, UTF-16/BOM,
sparse decoding, CRLF, empty rows, frozen provisional tails, append, rewrite,
closure, owner-thread destruction and reentrant destruction callbacks. It also
covers admission, output/schema limits, paging/cursor reuse, metadata eviction,
backpressure and cancellation while delivery is stalled. The complete CTest suite
and strict GCC/Clang compilation supplement these fixtures.

Throughput, peak-memory and UI latency measurements remain release gates for the
complete milestone. Passing allocation bounds is not a measured performance
budget. Local validation uses the available Qt 6.10.1 SDK; hosted Windows CI
validates the selected Qt 6.12 toolchain.
