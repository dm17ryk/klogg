# UT01a — Bounded terminal emulator model proof

Spec: [UT01](../tasks/UT01.md), [terminal integration](../11-unified-terminal.md), [UT00a audit](../../../docs/workbench/ut00-terminal-baseline.md).
Base: reviewed UT00a7429f991c70b8937b15058802e8a342a6a4e9ddf; parent PR94. This is unused internal model preparation, not terminal milestone acceptance.

## Global constraints

C++17/Qt5/Qt6; no endpoint/PTY ownership, source recorder, scheduler or alternative authorization. Keep current log view, Actions/Responses and public operations unchanged. No live keyboard/device input until UT02/D01 arbitration/generation/receipts exist. KLOGG_ENABLE_TERMINAL defaults OFF; tests can compile the isolated model while the application remains feature-off. All later exposed UI/API capabilities require shared Commander/CLI/native-MCP/Python parity in that increment. Preserve formal MD06/UT00/F01/D01/W0 gates; local model proof does not satisfy them. No private Lua, captures or user log bodies.

## Task 1: Pinned dependency and enforceable resource behavior

1. Pin the official libvterm0.3.3 archiveSHA25609156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0 and MIT notice. Use a private MSVC/Clang QtCore-independent probe first; no target is adopted before platform/resource evidence.
   Expected: upstream builds natively or concrete portability failures are reported, with the exact source pin and no silent replacement engine.
2. Write real dependency tests for split combining-glyph parity with bounded allocator observations, oversized numeric parameters, and control strings exceeding64KiB. Observe failures before patching the dependency.
   Expected: unmodified source demonstrates the missing bounds, not fixture/build mistakes. Current source has a growing split-combining buffer, while the bulk path already caps rendered cells at six codepoints.
3. Apply the smallest attributed bounds/portability patches: keep split combining state consistent with VTERM_MAX_CHARS_PER_CELL; cap numeric/control-string parser resources using existing libvterm parsing. Keep patches reviewable against the pristine archive. Expose private bridge status for explicit limit reporting; never replace VT parsing.
   Expected: adversarial tests pass, fragmented and bulk rendering agree, no body diagnostics or OS/clipboard/command side effects. MSVC and available Unix/CI proof required before merging dependency adoption; missing platform proof remains an explicit gate.

## Task 2: Pure bounded QtCore model, tests first

1. Write the UT01 fixtures against a compiling absent-behavior wrapper: Split UTF8 is decoded once; CSI split at every boundary is invariant; Prompt without LF is visible; CR and backspace update live cells; Alternate screen restores primary; Resize preserves valid cursor; Huge OSC is bounded; OSC52 cannot touch clipboard. Add exact six-codepoint, saturated combining, paging, queued-reply and invalid-request cases.
   Expected: missing behavior fails real assertions before implementation, not a missing dependency or typo.
2. Implement checked dimensions, owner-thread mutation, libvterm callbacks, safe immutable value snapshots and bounded encoded reply/input queues. Inspect at most VTERM_MAX_CHARS_PER_CELL slots without relying on NUL. Do not invoke external callbacks while the C library mutates state. Snapshot rows/cells are terminal coordinates, never physical log lines or navigation authority. Capture remains owned by the existing session.
   Expected: tests pass for all fragmentation boundaries, optional/alternate modes and invalid ranges; accepted data is never silently dropped. No widget, transport, file, clipboard or process operation is linked into the model. Show explicit renderer saturation/resource reasons.
3. Validate dimensions before allocation (1000cols/500rows), input chunks64KiB, control strings64KiB, reply bytes64KiB and snapshot pages16384cells. Keep scrollback/renderer/transcript and live attachment as explicit remaining work if not delivered here. Use detailed static-reason/counter diagnostics without text bodies.
   Expected: boundary/one-over and stale/reset/owner-thread cases fail safely, no unlimited recursive/string/combining growth, and no claim of full UT01/terminal acceptance.

## Task 3: Optional integration and full verification

Register the isolated C/QtCore targets and a focused terminal test executable under KLOGG_BUILD_TESTS. The app feature remains OFF and unlinked until a later consumer. Preserve normal app/regression targets and deployment choices. Add dependency attribution and exact patch/source manifest.

Run fresh focused tests, warning checks, native ci_build and every registered CTest group in an isolated deployed runtime. Compare frozen inputs/artifacts. Exercise the existing Qt5/Qt6 Windows/macOS/Linux CI and report pending evidence honestly; do not adopt/merge an unsupported platform. Refresh relevant code databases, run staged impact detection, validate the canonical plan and preserve history/hard edges. Obtain independent C++, general and security review before publication. New live terminal/public operations remain future steps.

## Review focus

C99/ABI/Qt compatibility; all allocator and parser growth paths; chunking invariance including combining marks; fixed-array bounds; malformed UTF8/OSC/DCS/numeric fields; callback lifetime/reentrancy and thread ownership; limited output honesty; source/license/patch fidelity; no side effects/duplicate owners; no public input authority or milestone acceptance inferred from model tests.
