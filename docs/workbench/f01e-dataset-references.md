# F01e — Revision-bound dataset references

Scope: [contract](../analysis-dataset-references.md) and
[implementation slice](../../plans/cilogg-workbench-2026-10-21/slices/F01e-dataset-references.md).
Base: `080b504c184796ad3942383149d0385503dd9643` (merged master).
This slice is independent of open PRs #90, #91 and #92.

## Local verification

The private QtCore runner first compiled an empty API and failed nine cases for
missing behavior. The existing scalar baseline passed 332 assertions in six
cases. Implementing the real contract passed 133 assertions in nine cases.
Fixtures cover exact 64-bit JSON round trips, stale tuple components, forged
current metadata with an old issued token, kind separation, expiry/release,
owner error/exception propagation, contradictory owner status, malformed wire
types/versions and bounded Unicode.

Fresh MSVC/Qt6.10.1 `ci_build` passed in 583.88 seconds. Strict Clang checks
passed all five changed translation units. Related native fixtures passed 408
assertions in 34 cases; the controlled 1200 ms cold-start regression passed 76
assertions. All seven registered CTest groups passed in 489.98 seconds in a
fresh deployed, isolated runtime. The eight frozen source/registration hashes
remained unchanged through validation. Real-engine fixtures skip when explicit
TShark/text2pcap paths are absent; this is not additional Lua qualification.

Three earlier full runs each passed six of seven groups: existing harness
subprocess deadlines, then preview semantic fixture deadlines. Negative evidence
is retained in [the observations](f01e-observations.json). A controlled 1200 ms
fixture startup delay reproduced the original protocol/recovery failures under
the 1000 ms budget. Test-only semantic fixtures now allow 5000 ms and use safe
callback state with a request-ID completion guard. Dedicated stall/cancellation
and production deadlines are unchanged. Fresh C++ and general whole-branch reviews approved implementation `1ff3af07`
with no actionable findings. Both were source reviews; the C++ reviewer also
checked all eight frozen input hashes. Their analyzer attempts were incomplete
(cppcheck configuration unavailable; clang-tidy lacked Qt include context).

## Decisions and gates

1. Provide a generic tagged reference contract; actual page/row/lineage adapters
   and lifecycle integration stay with their owners. Cost: consumers must check
   row association and use the owner lookup before exposing results.
2. Treat schema IDs as opaque equality identities, independent of the unmerged
   schema descriptor implementation. Cost: the owner must bind the actual retained
   schema identity and issued tuple.

3. Preserve unsuccessful full-suite observations and require a fresh isolated
   pass. Cost: existing timing instability remains a release acceptance concern.
4. Give semantic preview fixtures cold-start headroom and a safe completion
   guard, supported by the controlled failure/pass pair. This supersedes the
   initial choice to leave those test budgets unchanged. Cost: semantic failures
   can take longer to report; production deadlines and P00 gates stay unchanged.

Qt5, hosted platforms/backends, packaging and release/performance acceptance
remain separate gates. Full F01, W0 and production decoder acceptance remain
open. Code-review-graph refreshed structure and FTS; its heuristic test gaps are not
coverage evidence. CodeGraph reports this worktree unindexed. GitNexus refresh
timed out after parser worker failures; stale output is not structural verification. No user source text, private Lua, capture bodies or
configuration is included in these observations.

Implementation: `1ff3af07`; [PR #93](https://github.com/dm17ryk/klogg/pull/93).
Hosted checks are pending.
