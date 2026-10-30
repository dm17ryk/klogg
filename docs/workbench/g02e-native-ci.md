# G02e - Native Windows benchmark budgets in CI

G02b-G02d provide local timing and memory evidence. The Windows x64 CI job now
opts into those existing `ui-service` gates after its public parity/SDK checks.
The composite action exposes `native-benchmarks`, disabled by default. Other
platforms and Windows ARM64 keep their existing selections; explicitly requesting
this step on a non-Windows runner fails.

The step deploys the integration-test runtime, verifies the executable/cache
against the expected backend using the existing `verify_build` helper, records
source/product/test revisions and hashes, then generates 100,000 rows with seed 42.
It runs three native repetitions, each with 100 event/paint samples in both idle
and active-analysis contexts, plus direct cancellation and owned-process memory.

No budget is relaxed: event/CPU-scroll p95 below 100 ms, service cancellation
below 1000 ms, committed-tree and sampled-working-set totals each below 512 MiB.
The native-suite deadline remains 120 seconds per repetition. The existing
benchmark's nonzero exit propagates through Bash `set -euo pipefail` to fail CI.

Build provenance is under `reports/workbench-benchmark-inputs/build.json`;
measurements, gates and failure diagnostics are under
`reports/workbench-benchmarks/`. The existing unconditional Windows diagnostic
upload collects them when an earlier step fails. Copied `native-payload` binaries
and Qt runtime are excluded; the separate benchmark `settings/` diagnostics
remain eligible for upload. Generated corpora stay outside Git
and can be reproduced from the recorded seed/hash.

## Local validation and boundaries

The exact proposed Bash step passed locally using a private CI-shaped copy of
the approved Qt 6.10.1 Qt-only merged-PR71 deployment. All three repetitions pass.
The maximum event p95 was 0.1377 ms, CPU-scroll p95 was 10.3527 ms and service
cancellation was 38.1556 ms. The largest sampled-tree total was 95,776,768 bytes.
These are sequential observations on an uncontrolled Windows machine, not
compositor measurements or calibrated hosted-runner capacity.

A second execution using a one-byte sampled-RSS budget exits 1: only that gate
fails, and its observations remain in the failed result. The native host and
workers are owned/reaped by the existing supervisor. Bash syntax, inline Python
compilation and Ruff E9/F with Python 3.9 syntax checks pass.

The reused source files were freshly built and validated for PR71's integration:
6/6 CTest in 293.43 seconds, 85 parity self-tests with one platform skip, and the
native MainWindow journey all pass. Its product source tree equals this branch's
master base; its test tree also contains G03e. The binary was built before the
merge commit, so its embedded version stamp is 8c06a9e5 while the validated source
epoch is 1bdcfb85. No fresh G02e application build or hosted Windows Qt 6.12 accelerated
pass is claimed. Exact hashes and per-repetition values are in
[the observations](g02e-windows-observations.json).

This is test/CI wiring. It introduces no product operation needing GUI/CLI/MCP
registration and does not complete G02/G03 or W0. Sampled RSS remains sequential
and may miss short-lived workers and peaks. Exact worker peak RSS, sustained
memory slope, controlled cold-cache runs, compositor latency, rich-renderer
overhead and native Linux/macOS performance evidence remain open.

Rollback disables the Windows `native-benchmarks` opt-in and removes this action
step; existing native UI/parity jobs and the benchmark CLI remain intact.
