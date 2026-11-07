# TimoxVSSC repository guide

This repository owns the Electron/React control interface. Engine, driver and PWAR sources live in their respective repositories. Do not embed paths to the original TimoxVasio checkout.

Before builds, inspect `git rev-parse --show-toplevel` and `git status`. Use the pinned package lock and `artifacts.lock.json`. Packaging must stop on missing or changed engine/PWAR/schema/driver inputs. The preview uses a real per-machine NSIS install and installs/registers its pinned ASIO driver.

Preserve the explicit API 1.1 requirement, loopback discovery safety, API authentication, and existing `%APPDATA%/timox-vsounddealer` user data and single-instance lock. No automatic preference or secret migration from other profiles.

The left navigation contains sections and global state/actions only. Object actions originate in background/process/cable context menus; detail forms are modal. Cable selection alone must not open details. Never add a permanent connection-properties panel beside the graph.

Do not start GUI/audio processes or hardware qualification without a coordinated explicit request. Expected binary inputs: artifacts/backend/TimoxVirtualAsioEngine.exe and artifacts/backend/PwarAsioClient.exe, pinned by revision and SHA-256 in artifacts.lock.json.
