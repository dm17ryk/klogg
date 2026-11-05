# MD01e — Qualify EHCI partial, NUL and structural framing capabilities

Base: merged master 080b504c. Branch: codex/md01-ehci-capabilities.
This is an independently reviewable preparation slice of MD01, retaining its
G02/W0 hard gates and the parent task status. Generic MD03 and subsequent decoder
core tasks remain open. Unified terminal work follows accepted decoder core.

- [x] Reproduce original external-pack failures and native NUL/can_desegment APIs.
- [x] Repair a private copy with bounded structural framing, exact raw bytes,
  observed partial fields and preserved native TCP reassembly.
- [x] Correct review-discovered CRCA/CRCX ambiguity with a failing then passing
  public synthetic fixture; rerun selected genuine-capture equivalence.
- [x] Complete fresh native build, isolated full CTest and GUI/CLI/MCP checks.
- [x] Finish reviews, install a versioned local pack and prepare the PR.

Public changes are proof raw-byte assertions, synthetic fixtures and opt-in
GUI/CLI/native-MCP qualification using the existing preview backend/commands.
Private Lua and capture bodies are excluded from Git pending distribution rights.
See [qualification](../../../docs/workbench/md01e-ehci-capabilities.md) and
[observations](../../../docs/workbench/md01e-windows-observations.json).

Local validation: seven CTest groups passed; 1278 GUI assertions; 51 CLI/MCP
comparisons; 15 capability-suite cases on both Python 3.14/3.9 (11 engine tests, 4 portable); portable 122 each
with 47 explicit external-input skips. Hosted/release and parent gates remain open.

PR90 review corrections and exact validation epochs are recorded in the
[follow-up observations](../../../docs/workbench/md01e-sourcery-observations.json).
Parent acceptance and terminal entry gates are preserved.
