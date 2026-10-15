# Post-1.1.0 Work Program for Codex Agents

> **ARCHIVE** — Document historique conservé pour référence. Il ne décrit pas l'état courant de TimoxVasio (1.2.1). Voir le [README](../../../../README.md) et la documentation courante qu'il référence.

**Purpose:** Coordinate all currently documented follow-up work in this repository, while keeping the formal roadmap distinct from validation gaps and improvement candidates.

**Evidence reviewed:** `docs/ROADMAP.md`, `docs/superpowers/plans/IMPLEMENTATION_LEDGER.md`, `docs/UX_UI_REVIEW.md`, `docs/mixxx-256-channel-compatibility.md`, and `docs/RELEASE_VALIDATION_1.1.0.md`. Older four-driver plans and `CORRECTION_PLAN.md` describe superseded designs and must not be executed as a backlog.

**Scope limit:** The only feature explicitly committed in `docs/ROADMAP.md` is physical ASIO buffer diagnostics. The other streams below are documented gaps or candidate improvements; they need a product decision before being called committed roadmap items. The public GitHub issue/project queue was not verified because `gh issue list` returned HTTP 401 in this session.

## Assignable objectives

| ID | Status | Objective and deliverable | Depends on | File ownership / coordination |
|---|---|---|---|---|
| P1 | Validation gap | Reproduce and record the remaining real-host transitions: reconfiguration, stop/start, disconnect/reconnect, incompatible settings, and visible error recovery. Produce an English evidence matrix that separates software probes from hardware observations. | Stable 1.1.0 installation and user-controlled hardware session | One hardware agent owns devices and applications for the duration; do not run concurrent hardware sessions. |
| P2 | Candidate compatibility work | Decide the Mixxx path. Verify the existing 255-channel application profile in TimoxVasio; separately test the 256-channel Mixxx patch through discovery, stream open, channel allocation, and audio. Document exact Mixxx version and result. | P1 hardware session may share a scheduled test slot, but not concurrent device access | One compatibility agent owns tests and `patches/mixxx/`; do not alter the installed Mixxx or upstream project without a separate decision. |
| P3 | Candidate UX work | Audit the current 1.1.0 build, including Logs and Swagger, keyboard focus, resize/zoom, and matrix reading. If confirmed, implement readable route endpoint names while preserving technical IDs in details. Produce an English audit with screenshots and focused UI tests. | None | One UI agent owns `gui/src/` and current UI tests. Coordinate with P4 before editing the same diagnostics view. |
| P4 | Formal roadmap feature | Implement physical ASIO callback timing, evidence aggregation, API contract, Control diagnostics, and multi-device validation. Follow the dedicated [ASIO buffer diagnostics agent plan](2026-10-06-asio-buffer-diagnostics-agent-plan.md). | P1 evidence can inform thresholds | Split into design, native measurement, policy, API, integration, UI, and hardware objectives as specified in the linked plan. |
| P5 | Integration gate | Reconcile the selected streams, rerun the relevant native/API/UI tests, and check the binary provenance rules before any package or release decision. Publish an English verification report. | All streams selected for the next release | One integration/release agent owns build and package artifacts; no other agent packages simultaneously. |

## Efficient schedule

1. **Now:** Start P1's read-only test design and P2's source/patch review in parallel. Start P4's measurement-contract design if buffer diagnostics are the next release target.
2. **After the contract decisions:** Run P4 native measurement, policy, and API tasks concurrently in isolated worktrees with disjoint file ownership. P3 may audit the current interface concurrently. The P4 UI implementation and any P3 UI fix must be serialized or assigned to one UI owner.
3. **Hardware window:** Run P1, P2's real Mixxx test, and P4's driver comparison sequentially under one hardware coordinator. Reuse setup and record distinct results; do not assume a 1.1.0 result validates a later engine binary.
4. **Integration:** Merge each reviewed deliverable one at a time, then run P5 once for the selected release scope. Keep compatibility and UX candidates out of a release unless their acceptance criteria and priority are approved.

## Assignment rules

- Give each Codex agent one objective, explicit source files, a deliverable, and an exit test. Agents may write narrower design and implementation plans under `docs/superpowers/` before changing code.
- All newly written or changed documentation must be in English. The separate translation work is outside this plan; avoid competing edits to its `*.en.md` files.
- Before any build or installation, check `git rev-parse --show-toplevel` and `git status`. Use only this public repository. Verify canonical binary paths, timestamps, and SHA-256 before packaging, as required by `AGENTS.md`.
- Do not treat older 1.0.0 snapshots in the implementation ledger as current defects. Check current code or a current 1.1.0 run before assigning a fix.
- A real hardware result must name the exact engine, driver, application, device, sample rate, buffer size, and route. An API state or a probe alone is not audible-signal evidence.

## Skill readiness

Local skill files were verified for `superpowers:writing-plans`, `superpowers:executing-plans`, `superpowers:using-git-worktrees`, `superpowers:test-driven-development`, `superpowers:verification-before-completion`, `audio-dsp-review`, `audio-numerics-review`, `audio-performance-debug`, `api-builder`, `api-documentation-generator`, and `frontend-design:frontend-design`. No known skill installation is needed. Hardware, Windows build tools, and host applications are separate prerequisites for the relevant objectives.

## Decisions needed before launching all streams

- Confirm whether the source of truth is the repository's documented follow-ups or a separate roadmap/project board.
- Choose whether P2 and P3 are in the next release scope or remain candidate work.
- Schedule one controlled hardware window for P1, P2, and P4, subject to the chosen scope.
