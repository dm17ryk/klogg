# F01a exact scalar foundation — local evidence

Base: `fae1374d06e26dd19c56097b5f4119d152a3d965` (merged master).
Branch: `codex/f01-exact-values`. This unused internal prerequisite is
independent of PR84/85. See [the contract](../analysis-datasets.md).

| Check | Observed result |
| --- | --- |
| Test-first build before implementation | Failed because `fieldvalue.h` did not exist |
| Fresh MSVC VS2022 / Qt 6.10.1 Qt-only `ci_build` | Passed |
| Strict Clang C++17 syntax for implementation and test | Passed with conversion, sign-conversion, shortening and dangling-else warnings treated as errors |
| Final `[data][F01]` in SHA-256-verified private deployed runtime | 332 assertions / 6 cases passed |
| Full local CTest, same product implementation | 7/7 groups passed, 661.54 s |
| General and C++ final branch reviews | No confirmed blocking findings |

The full CTest run covered smoke, benchmark harness, decoder proof, unit,
integration/UI and Workbench parity self-tests. The final addition after that
run only extended the existing binary/text test with empty Bytes assertions;
its rebuilt focused run passed. The suspected empty-base64/null-JSON issue did
not reproduce on Qt 6.10.1, so the implementation was not changed to fix a
hypothetical failure. The regression remains for other runtimes.

Validation used a private runtime copied from the build output, checked each
copied executable/DLL/translation with SHA-256 and used private settings. It did
not change installed CILogg settings, active COM sessions or private log data.
One intermediate copy attempted during linking failed with Windows sharing
violation; it was not counted as a test run. After the build completed, copying
and the final focused run succeeded.

Detailed local artifacts are under ignored `build_root`: `f01a-green-build.log`,
`f01a-empty-bytes-red-build.log` (the regression build, whose test passed),
`f01a-strict.log`, `f01a-focused.log`, `f01a-full-ctest.log` and
`f01a-runtime.json`. The latest runtime manifest describes the focused run.

CodeGraph explicitly reports this worktree unindexed. GitNexus refresh exited
3221226505; its registered primary-checkout index is 31 commits behind this base
and has no FTS extension. Impact lookup could not find the new leaf symbol and
detect-changes returned no changes. Those results are tool limitations, not
proof of an empty impact. Direct review found no product callers; only CMake and
the new test consume the contract. Code-review-graph was unavailable.
Standalone cppcheck lacked its standard configuration and standalone clang-tidy
did not have a complete Qt include environment; neither establishes an
analyzer-clean result.

Minor deferred: document the reassignment/encoding policy for moved-from
`FieldValue` instances before adding consumers. Normal factory-created values
and encoded/decoded values are covered by the current tests.

Qt5 runtime parity, hosted platform checks and all broader F01/W0/MD01 gates
remain open. This slice makes no throughput, memory, UI, decoder, dataset API
or hardware acceptance claim.

## PR86 CI repair: precise floating-point compilation

The initial hosted run `41506822893` exposed an optimization-policy mismatch:
Linux tests admitted nonfinite values and mishandled signed-zero inspection
under inherited `-ffast-math`; Apple Clang additionally rejected the infinity
fixture with `-Wnan-infinity-disabled` promoted to an error. Windows passing
and the earlier syntax-only Clang check did not establish optimized Clang
correctness. The downloaded failing Linux and Intel macOS logs showed this
shared cause; ARM macOS cancellations are separate infrastructure outcomes.

`set_exact_floating_point` adds source options after the inherited target flags,
only for `fieldvalue.cpp` and `fieldvalue_test.cpp`. The rest of the project's
optimization policy remains unchanged. `/fp:precise` covers MSVC and clang-cl;
`-fno-fast-math` covers GCC, Clang and Apple Clang. The helper logs its selected
source/option and refuses an unqualified compiler rather than silently guessing.

A local optimized clang-cl/CMake probe compiled the actual codec and all six
existing tests against Qt 6.10.1 with the real `project_options`. It reproduced
five failed assertions out of 332 before the override and passed afterward.
Only the logger enable predicate was replaced in this isolated probe; no scalar
implementation, conversion, wire or JSON behavior was mocked. The native
MSVC/LTO `ci_build` and isolated 332-assertion focused rerun also passed. Both
final reviewers approved the three-file build-policy repair with no findings.
The full isolated regression rerun passed all 7 CTest groups in 157.45 seconds.
The private runtime copy was SHA-256 verified as in the original qualification.
Hosted GCC/Apple Clang reruns and Qt5 qualification remain pending. Detailed logs:
`build_root/pr86-fp-red.log`, `pr86-fp-green.log`, `pr86-native-build.log`,
`pr86-focused.log`, `pr86-full-ctest.log` and `pr86-runtime.json`.
The repair's GitNexus refresh timed out after 90 seconds and stale-index impact
lookups could not resolve the new symbols; detect-changes was attempted but
returned no changes. Generated MSVC source properties, the optimized Clang probe
and direct reviews establish the limited build scope, not the unavailable graph.
