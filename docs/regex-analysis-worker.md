# Capture evaluation and supervised execution (P03c)

This internal slice builds on the bounded scanner. Commander/MCP operations and
the Regex Lab dock follow in P03d and P04. The existing executable accepts the
internal `--internal-analysis-worker` mode before settings, logging configuration
or GUI initialization. No additional application executable is deployed.

## Evaluation contract

`CaptureEvaluator` uses Qt `QRegularExpression` with Unicode properties and normal
numbered/named capture groups. Existing search conversion still uses
`DontCaptureOption`. The shared `parseBooleanExpressions` utility is extracted
from existing search code; both paths use the existing Boolean evaluator and
match-mode wrappers. Boolean plus whole-line mode remains unsupported.

Each physical subject is independent. Qt global-match iteration defines repeated
and zero-length matching. A result contains its physical line, decoded raw text,
combined Boolean outcome and a separate outcome/match list for every subpattern.
Group zero is the whole match. Groups carry number, name, value, participation
flag, zero-based UTF-16 start and length. An unmatched optional group has a null
value and negative start; a participating empty group has an empty string and
nonnegative start. Captures from different Boolean subpatterns never share a
namespace. `includeUnmatched` includes subjects whose combined outcome is false.

Pattern input is limited to 64 KiB UTF-8, 64 Boolean leaves and 128 groups per
leaf. A batch stops at 10,000 match occurrences or 4 MiB serialized output. It
returns a visible limit reason and only fully evaluated source rows. Compilation
errors carry their leaf index and an explicitly labeled UTF-16 offset in the
generated Qt subpattern (including match-mode wrappers).

## Supervision and protocol

`makeRegexProcessor` creates a QProcess on the analysis thread. An initial empty
batch compiles the pattern, including jobs with no source subjects. Subsequent
requests match bounded source batches. Requests and replies are JSON objects
preceded by a four-byte big-endian length, with protocol version 1 and an 8 MiB
frame limit. Compilation and matching replies have operation-specific fields;
wrong types, unknown errors, missing backend/timing fields and oversized frames
fail the job rather than becoming empty successful output.

The child first sends a validated ready frame. Startup has a separate ceiling of
30 seconds, capped by the remaining job deadline. The compilation budget starts
after readiness, so loading the application cannot consume its two-second
execution allowance. All startup time still counts toward the overall deadline.
Compilation is sent only after readiness is validated and its execution deadline
starts. Monotonic request IDs correlate every compilation/matching reply;
unsolicited or stale frames fail the job. Unbuffered stdin and exact single-frame
reads avoid pipe fill waits. Buffered replies are drained before reporting child
exit, including when a child writes its last reply and exits immediately.

The supervisor checks cancellation/deadlines while flushing and reading, kills
and reaps a stalled child, and destroys the child before publishing job
completion. Standard error remains separate from protocol output; a cumulative
64 KiB diagnostic quota prevents a flooding worker from accumulating unbounded
parent buffers. Diagnostic bodies are discarded by default.

Windows applies a 512 MiB private-memory job-object limit with kill-on-close.
Linux limits child address space to 1 GiB. macOS permits 512 MiB additional address
space above mappings already present before main(), accounting for Mach-O page
zero and shared-cache reservations. OS quota setup failure rejects the worker.
The parent enforces the configured compilation/batch and overall job deadlines;
backpressure and queue waits count toward the overall deadline.

Successful worker replies report compile and match microseconds independently.
The service separately reports process startup, source reading/decoding, result
delivery and delivery-credit wait costs. The backend is explicitly
`Qt QRegularExpression`, including accelerated-search builds. An interrupted
operation cannot report an exact successful worker timing.

## Verification and attribution

`[captures]` fixtures cover Unicode offsets, named/ordinary/optional/empty groups,
Qt global iteration, Boolean parity, supported match modes and explicit engine
and output limits. `[regexworker]` covers the packaged application worker,
empty-source compilation, timeout/cancellation, PID exit checks, worker crashes,
protocol/schema faults, diagnostic flooding and subsequent recovery. A real
adversarial expression supplements the controlled stalled-child fixture.

The behavior reference is [LogSquirl Regex Lab at a88c4a4d](https://github.com/64x-lunicorn/LogSquirl/blob/a88c4a4d/src/regex/include/regexlab.h).
The implementation uses CILogg services and existing parser/evaluator code; no
LogSquirl source was copied. Public commands and the dock must be removed before
rolling back the worker/service layers.
