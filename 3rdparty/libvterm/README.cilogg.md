# libvterm source pin and local bounds patches

Upstream: https://www.leonerd.org.uk/code/libvterm/ , version 0.3.3.
Archive: https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz
SHA256: 09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0 (80024 bytes).
License: MIT, see LICENSE. Copyright 2008 Paul Evans.
Only upstream include/, src/ and LICENSE are copied. SOURCE.json pins every
original and vendored byte hash. No build-time network access is needed.

Local modifications (already applied; cilogg-bounds.patch records the diff):
- state.c: cap fragmented combining state at the existing six-codepoint screen
  cell capacity, matching bulk rendering. Additional combining marks follow
  upstream bulk display behavior; this is not an exact arbitrary-grapheme store.
- parser.c: sticky explicit control-string/parameter/count limits, checked before
  arithmetic/array dispatch; count ignored wire bytes as well as payload bytes.
  Guard absent string-fragment pointers during incomplete command headers.
- state.c/screen.c: no-op REP without a previous positive-width glyph; clip
  wide-glyph continuations/damage at the right screen edge; replace decoded
  negative-width scalars with U+FFFD and guard tabstop bitmap indices.
- vterm_internal.h: minimum one logical column for double-width lines;
  private counters/status. New cilogg_vterm_limits.h and
  cilogg_limits.c expose status without changing the upstream public ABI header.

Controls permit 65536 payload bytes and 66048 total wire bytes (conservative
introducer accounting); numeric components permit values 0..65535 and at most
16 digits, with at most 16 CSI arguments/subarguments. A permanent limit is
sticky until the model is recreated. Previously delivered bounded fragments
remain historical; limit status must be checked before claiming completion.
No terminal bodies are logged by these patches. The Qt owner logs static
termination reasons and numeric counters. OSC52 selection callbacks stay unset.

The pure model and tests are unused by the application. Adoption requires native
MSVC and Unix CI proof plus review; the full terminal milestone remains open.
Upgrades must revalidate the pin, MIT notice, all patches, allocation bounds,
fragmentation parity, resize behavior, and the upstream test suite where feasible.
