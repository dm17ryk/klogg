# TShark preview: local validation record

This record covers the MD03a preview integration on Windows 11 with local
Qt 6.10.1, MSVC and C++17, TShark 4.6.9 and Lua 5.4.6. It records local
execution separately from pending hosted, platform, release and performance
qualification. User-approved external Lua was exercised with public synthetic
EHCP/EHCI messages. Private Lua source and private log bodies are not shipped.

## Integration baseline

The following results apply to source
`5c640f15da9ee607347df752bfee87bc1dbddafe`, before the final review corrections
for the pane budget and overdue delivery. They must not be read as results for
a later binary. Final review corrections have focused regression evidence;
final combined build and acceptance results are recorded separately.

| Check | Recorded result |
| --- | --- |
| Persistent worker, actual TShark | 21 tests passed on Python 3.9.25, 3.12.14 and 3.14.10 |
| Full Python 3.9 plugin suite | 161 tests, including 5 skips |
| Python 3.9 benchmark/process suite | 113 tests, including 5 skips |
| Focused native actual-engine cohort | 401 assertions in 24 cases passed |
| Standard full native unit cohort | 12202 assertions in 246 cases passed; 8 actual-engine opt-ins explicitly warned and returned |
| Strict real CLI/native-MCP comparison | 42 result pairs passed |
| Portable comparison oracle | 3 tests passed on Python 3.9 and 3.14 |
| Portable GUI cohort | 80 assertions in 7 cases passed |
| Actual public-Lua GUI cohort | 102 assertions in 2 cases passed |
| External EHCI pane/hover, clean rerun | 27 assertions passed |
| Complete `ci_build` target | Exit 0 |
| Registered normal-Info CTest | 7 of 7 groups passed in 296.94 seconds |

The standard unit cohort's eight actual-engine opt-ins are skips expressed by
test warnings and early returns, not actual-engine passes. Their execution
evidence is the separate focused cohort. That focused run exercised cancellation
after selector exit, authoritative parent-stop and timeout envelopes, a warmed
four-second Lua stall, configuration restoration and two exact successful
public-probe decodes. Its stalled integration case does not identify which of
the two timers produced the timeout.

The 42 strict comparisons retain exact repeated and empty fields, large integer
strings, transformed-buffer provenance, backend/fallback and assembly/source
metadata. They include three external public fixtures, malformed hex/base64,
absent selected protocol, Lua exception, timeout and two actual public-probe
successes after the intentional stall. Each transport reused its new engine PID
for those two successes. The external fixtures precede the stall: this does not
establish post-stall recovery for the external protocol pack.

Baseline private-runtime SHA-256 identifiers:

| Binary | SHA-256 |
| --- | --- |
| `cilogg.exe` | `005c063cd97573122ad98fa12d34f9de9d0e021d8fa51091f285c2e4717a6151` |
| `cilogg_tests.exe` | `bb38c82588a6dda9e07c443b57193c667360c7b38695e65b8655ff6382cfdcf9` |
| `cilogg_itests.exe` | `eb889c998a5b0f48945c434bf8ca87c0fbbce1c76b8d91621467bc3022430958` |

Strict Clang conversion, sign-conversion, narrowing and dangling-else checks
passed eight native/test translation units at the final baseline. Fourteen
previously checked GUI/Commander/MCP translation units had no later changes at
that baseline. Earlier repo clang-tidy checks passed four native units without
project-source diagnostics; suppressed vendor-header warnings were retained.
Cppcheck could not start because its installation lacked `std.cfg`.

## Preserved failed observations

- The first post-fix focused run failed two existing actual-engine cases. A
  bounded replay of the same binary passed 20 assertions in those two cases.
  A fresh decode took 7920 ms, a warm decode 521 ms, and fresh recovery requests
  4071/4159 ms. The original failures had no stage trace; their cause is unknown.
- A combined full-unit cohort with actual-engine opt-ins enabled failed five
  decoder cases and six assertions: 246 cases/241 passed/5 failed, 12194
  assertions/12188 passed/6 failed, exit 6. A fake-worker rejection/recovery case
  also failed. TShark cold startup cannot explain every failure by itself.
  Neither the separately passing focused cohort nor the standard full cohort
  resolves this observation.
- The first external EHCI GUI attempt failed pane availability near three
  seconds (21 of 22 assertions passed); an unchanged clean rerun passed all 27
  assertions. No root cause is proved.
- An earlier real parity run timed out on its first external EHCP request after
  the deliberate Lua stall. The later successful post-stall public probes do
  not erase that failed external-pack recovery observation.

The pane default remains 3000 ms, with a 10000 ms maximum. The final review fix
makes the displayed budget available in Automatic, TShark and JSON modes and
rejects successful results delivered after the original total deadline. Tests
that select 10000 ms exercise an existing user setting; they do not increase a
production default or establish cold-start performance acceptance. Hover keeps
its fixed default and may visibly time out on a cold channel.

## Code intelligence and acceptance limits

CodeReviewGraph was refreshed incrementally at the baseline: eight files,
62 nodes and 2109 edges, with structural signatures and FTS, without errors.
This was not a full flow/community rebuild. Unresolved Catch calls and absent
flows are not evidence that tests are missing. GitNexus's prior index was
41 commits stale; an earlier parse hit a circuit breaker, and the final owned
90-second refresh terminated with native exit 3221226505. CodeGraph was
unindexed and was not initialized. Current source diffs and scoped general,
Python and security reviews supplement these tool limits.

Decoded offsets remain engine-buffer positions with unresolved original-log
mapping. Successful processing does not assert protocol integrity or message
completeness. Hosted Qt 6.12, Qt 5, Unix/macOS runtime containment, installers,
release packages and broad performance acceptance need separate execution.
The wider MD01 partial/NUL qualification, generic MD03 dataset/plugin contract,
and W0/F01 gates remain open.
