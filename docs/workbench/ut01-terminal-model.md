# UT01a — isolated terminal model proof

The next requested workstream is Unified Terminal. Configured TShark-first
Preview is merged through PR90. Its broader MD01–MD06/W0 acceptance remains
open; this preparation does not change those gates or expose terminal typing.

## Delivered internal contracts

The QtCore/C++17 model uses the pinned MIT libvterm0.3.3 VT parser and screen.
It has no QWidget, endpoint, process, clipboard, recorder or Commander consumer.
KLOGG_ENABLE_TERMINAL defaults OFF. Tests compile it independently; the
application does not link it, and a test-free feature-off build excludes it.

Feed accepts at most64KiB, consumes at most256bytes per call, and returns an
explicit consumed prefix. The caller retains/reschedules the suffix on Yielded.
Damage work can yield earlier after1M cell updates between parser bytes; a
single bounded VT primitive completes first. This is not a wall-clock latency
claim. No bounded result is represented as a complete transcript.

The model validates1..1000columns and1..500rows before creating/resizing state.
Each screen page copies at most16384cells; continuation pages must provide the
previous revision to reject intervening mutations/reset. A zero expected revision
starts a new view. Snapshots contain terminal coordinates, never physical log
source provenance or navigation authority. Immutable snapshots survive mutations.
Wide-cell continuations have width0. A glyph wider than the remaining screen
space retains its Unicode text with clipped display width. Decoded negative-width
control scalars render U+FFFD; fresh REP without a prior glyph is a no-op. Cells contain at most6codepoints; the
capacityReached flag identifies possible saturation, including exact-six cells.
Additional combining marks follow upstream bulk rendering capacity; raw-source
capture remains the future shared session owner's responsibility.

Control payloads have64KiB limits, numeric components65535/16digits, and CSI
parameters/subparameters16slots. Wire limits also count ignored control bytes.
Limit reasons are sticky, output remains historical, and reset recreates state.
Replies have a64KiB queue; key/paste bytes are returned separately, never sent.
No local echo is invented. OSC52 selection callbacks are unset. Diagnostics in
cilogg.terminal.model contain static decisions/reasons and counters, no bodies.

All reads/mutations require the construction thread. Cross-thread calls reject the operation without accessing mutable state;
result-bearing methods report WrongThread, while takeReplies returns empty and
logs a static rejection without consuming the queued bytes. This is not a synchronized shared
object. Destruction must occur after callers stop. No external callbacks run
while libvterm mutates its state.

## Evidence and remaining gates

Observed RED against upstream: split combining allocation growth, numeric/count
admission and oversized control payload failures. Narrow attributed patches made
8dependency cases pass. An absent-behavior compiling wrapper then failed11model
cases before implementation. NativeMSVC19.44 private probe:23cases1045assertions
pass; full nativeMSVC19.42 ci_build and9registeredCTest groups pass locally with
Qt6.10.1. Strict WindowsClang C++17 syntax checks pass for model and fixtures.
These are local observations; UnixQt5/Qt6, ARM and packaged platform evidence
remain pending hosted CI. No dependency adoption merge before Unix proof.

Remaining terminal work includes scrollback/renderer/transcript, shared session
RX/TX and capture ownership, D01 arbitration/arming/generations/receipts, native
transport/PTY backends, widget/menu/toolbar integration, Commander/CLI/MCP/Python
parity and full cross-host acceptance. The formal UT01/UT00/MD06 tasks remain open.

Dependency source hash/notice/local diff: [vendor notes](../../3rdparty/libvterm/README.cilogg.md).
Tests: tests/terminal; implementation: src/terminal. No live application commands
or automatic terminal attachment are introduced by this slice.
