# Unify Timox Audio Repositories Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make TimoxVasio the single source repository for the GUI, audio engine, ASIO driver, PWAR client, shared API contracts, and reproducible preview installer.

**Architecture:** Keep component boundaries and build targets, but bring their source and release orchestration under one Git history and one tagged revision. A release must build its engine, PWAR client, driver, and GUI from that revision, verify API/schema compatibility and artifact hashes, and package only those outputs. Preserve existing user data, API authentication, loopback safety, and API 1.1 compatibility.

**Tech Stack:** C++/CMake, Electron/React/Node.js, Windows MIDI Services, NSIS, Git.

**Spec:** This plan is the migration spec; refine paths and commands during Task 1 inventory before moving source.

## Global Constraints

- Keep API 1.1 as the explicit minimum and preserve authentication.
- Preserve `%APPDATA%/timox-vsounddealer`, its single-instance lock, and user data; do not migrate secrets or preferences from other profiles.
- Keep loopback discovery safety and the per-machine NSIS driver install/registration behavior.
- Do not ship an artifact unless its source revision, build inputs, and SHA-256 are verified.
- Keep TimoxVasio `1.2.7` as the latest stable release. Treat the integrated changes as unreleased `1.3.0` preview work; migration must not tag, publish, or describe a stable `1.3.0` release.
- Preserve component version metadata (GUI, engine, driver, PWAR) as component provenance; the monorepo product version does not silently rewrite their internal version resources.
- Do not start GUI/audio processes or run hardware qualification without a coordinated explicit request.
- Preserve component boundaries and licenses; do not copy generated build trees or machine-local output into source history.

## Review Focus

- A clean checkout can build all release artifacts without sibling repositories or stale local binaries.
- GUI and engine from different commits cannot silently attach as a compatible API 1.1 pair.
- API/schema changes remain aligned across engine, GUI, tests, and OpenAPI documents.
- Existing NSIS install, driver registration, user data, and single-instance behavior survive the move.
- MIDI loopback discovery and API authentication remain intact after path and build changes.

---

### Task 1: Inventory and migration manifest

**Files:**
- Create: `docs/superpowers/specs/2026-11-07-unified-repository-inventory.md`
- Inspect: TimoxVasio, TimoxVSSC, TimoxVAudioEngine, Timox-Pwar source/build/release files and Git histories.

- [ ] Record source roots, licenses, build commands, third-party pins, generated files, release artifacts, and cross-repository references.
- [ ] Record exact source commits currently represented by the alpha.5 lockfile and list any post-lock changes.
- [ ] Define destination paths in this repository and identify colliding paths before any move.
- [ ] Review the manifest for omitted inputs; no files are moved in this task.

### Task 2: Import component histories and sources

**Files:**
- Create/move: `gui/`, `engine/`, `driver/`, `pwar/`, `shared/` (final paths fixed by Task 1 manifest).
- Modify: root build and ignore configuration only where required to expose existing component builds.

- [ ] Import each repository with its Git history into the approved destination path; retain attribution and license files.
- [ ] Exclude build directories, local logs, runtime state, and untracked machine-specific artifacts.
- [ ] Check that component source trees and histories are present and that the original repositories remain untouched.

### Task 3: Unified build orchestration

**Files:**
- Create/modify: root build entry point and component build manifests identified in Task 1.
- Create: clean-checkout build verification script and fixture inputs if needed.

- [ ] Add one documented command that builds engine, PWAR, driver, GUI, and installer in dependency order from the monorepo root.
- [ ] Make artifact assembly fail on missing inputs, source revision mismatch, schema mismatch, or hash mismatch.
- [ ] Verify the build from a clean checkout without consulting sibling repositories or preexisting output directories.

### Task 4: Version and runtime identity contract

**Files:**
- Modify: `shared/` API/version contract, engine version endpoint, GUI discovery/attachment code and tests.
- Modify: unified artifact lock and release metadata.

- [ ] Generate GUI and engine release identity from one tagged revision and expose engine revision/build identity in the API.
- [ ] Keep product metadata at unreleased `1.3.0` preview status while preserving `1.2.7` as the last stable version; do not create a release tag or publish artifacts.
- [ ] Require GUI attachment to match API 1.1 and the expected release identity; report incompatible or unidentified engines clearly.
- [ ] Test same-release attachment, stale engine rejection, foreign engine rejection, and an occupied port without starting real audio.

### Task 5: Packaging and regression qualification

**Files:**
- Modify: installer scripts/configuration and release workflow.
- Modify: component contract/regression tests only as required by the move.

- [ ] Package only artifacts built and verified by the unified pipeline; preserve the per-machine NSIS driver installation and registration.
- [ ] Run API/schema, GUI, engine, PWAR, and packaging checks from a clean checkout; record results and known limitations.
- [ ] Perform runtime MIDI and hardware qualification only in a separately coordinated session.
- [ ] Publish a migration/release note and mark the old repositories read-only only after the unified preview passes all release gates.

### Task 6: Remove obsolete TimoxVasio copy from grrzzzz

**Files:**
- Inspect/remove: `grrzzzz/asio/` legacy TimoxVasio tree, only after source/history comparison and verified backup.
- Modify: `grrzzzz` documentation or ignore rules only if needed to explain the removal.

- [ ] Compare tracked and untracked `grrzzzz/asio/` content with the imported monorepo and classify source, useful documentation, generated build output, and machine-local state.
- [ ] Preserve all unique uncommitted work in TimoxVasio or an explicit archive before deletion; verify that each item is accounted for.
- [ ] Remove only the confirmed obsolete copy and generated debris; keep any unique content until its owner approves disposition.
- [ ] Verify `grrzzzz` no longer presents the duplicate `asio/` project and that its remaining unrelated worktree changes are intact.

## Execution gates

1. Task 1 is read-only and produces the exact import manifest.
2. Review the manifest and destination-path map before importing histories or moving files.
3. Keep the original repositories and their refs unchanged throughout migration; do not archive them until Task 5 passes.
4. Do not install, launch, stop, or qualify audio/driver processes as part of automated migration.
5. `grrzzzz/asio/` currently has extensive tracked modifications and untracked files; never delete it wholesale or before its unique content is accounted for.

### Task 7: Replace the installed preview on this workstation

**Files/systems:**
- Installed per-machine TimoxVSSC preview and its registered ASIO driver.
- `%APPDATA%/timox-vsounddealer` user data and single-instance lock.

- [ ] Verify the built installer is the unreleased TimoxVasio `1.3.0` preview and passes artifact/hash preflight.
- [ ] Uninstall the currently installed preview with its registered uninstaller; preserve the existing `%APPDATA%/timox-vsounddealer` profile.
- [ ] Install the monorepo-built per-machine preview and its pinned driver; do not auto-launch the app from the installer.
- [ ] Verify installed component versions, registration, and retained user data before manually launching or qualifying audio.
