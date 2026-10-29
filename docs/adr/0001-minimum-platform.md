# ADR 0001: Minimum Ubuntu and Qt version

- **Status:** Accepted
- **Date:** 2026-10-29
- **Task:** 0.1

## Context

The plan relies on two Qt APIs that are newer than what Ubuntu 24.04 ships:

| API | Needed for | Since |
|---|---|---|
| `QStyleHints::colorScheme()` / `colorSchemeChanged` | Following the system light/dark setting (Plan §3.6, task 1.8) | Qt 6.5 |
| Built-in PKCE in `QOAuth2AuthorizationCodeFlow` | Google sign-in (Plan §9.2, task 9.x) | Qt 6.8 |

Versions available, checked on 2026-10-29 (`apt-cache policy` in clean `ubuntu:<version>` containers, and the KDE runtime branches on invent.kde.org / Flathub):

| Platform | Qt | SQLCipher | libsodium | QtKeychain |
|---|---|---|---|---|
| Ubuntu 24.04 LTS | **6.4.2** | 4.5.6 | 1.0.18 | 0.14.2 |
| Ubuntu 26.04 LTS | **6.10.2** | 4.13.0 | 1.0.18 | 0.15.0 |
| KDE Flatpak runtime (Flathub stable) | **6.11** | bundled | bundled | bundled |

Note: the Qt Network Authorization dev package is called `qt6-networkauth-dev` on both releases (the task list's `libqt6networkauth6-dev` does not exist).

## Options

- **A:** The `.deb` targets Ubuntu 26.04+ only. The Flatpak (KDE runtime) covers older Ubuntu and other distros.
- **B:** Support Qt 6.4 and write fallbacks: manual PKCE, D-Bus `org.freedesktop.appearance` theme detection.

## Decision

**Option A.**

- CMake requires **Qt 6.8** (`find_package(Qt6 6.8 REQUIRED ...)`), the lowest version with every API we use. This is a little below what 26.04 ships, so a Flatpak runtime downgrade or a point-release difference won't break the build.
- `scripts/setup-dev.sh` refuses to run on Ubuntu older than 26.04.
- CI builds in an `ubuntu:26.04` container.

## Consequences

- No fallback code paths for old Qt, which means less code to write, test, and audit (this matters most for the OAuth code).
- Ubuntu 24.04 users must use the Flatpak. 24.04 standard support runs until mid-2029, so this is a real audience; the Flatpak must be treated as a first-class package (Phase 11), not an afterthought.
- If a later Qt API is wanted (e.g. something from 6.10), raise the minimum here first and update CMake in the same PR.

## Verification

- `TC-ENV-001`: `scripts/setup-dev.sh` followed by a full build and `ctest` in a clean `ubuntu:26.04` container succeeds.
