# Debian Packaging Design — libsdi12 + libsdi12-verifier

**Date:** 2026-03-15
**Target:** Official Debian upload (NEW queue)
**Distros:** Debian/Ubuntu (RPM/Arch deferred)
**Approach:** Sequential — libsdi12 first, then libsdi12-verifier

---

## Scope

Two source packages:

1. **libsdi12** (v0.3.0) — `debian/` already exists, needs fixing and lintian-cleaning
2. **libsdi12-verifier** (v0.6.0) — `debian/` to be created from scratch; upstream `CMakeLists.txt` to be updated to support system library

Both go through the Debian NEW queue. libsdi12 must clear NEW first as libsdi12-verifier build-depends on `libsdi12-dev`.

---

## Phase 1 — libsdi12 packaging fixes

### 1.1 Upstream fix: `sdi12.h` copyright comment

Change `@copyright 2026 All Rights Reserved` → `@copyright 2026 Phillip Weinstock`.
Rationale: the "All Rights Reserved" declaration is inconsistent with the MIT licence stated in `debian/copyright` (DEP-5) and will produce a lintian tag.

### 1.2 Upstream fix: `sdi12.pc.in`

Replace prefix-relative paths with absolute CMake variables:

```
# Before
libdir=${prefix}/@CMAKE_INSTALL_LIBDIR@
includedir=${prefix}/@CMAKE_INSTALL_INCLUDEDIR@/sdi12

# After
libdir=@CMAKE_INSTALL_FULL_LIBDIR@
includedir=@CMAKE_INSTALL_FULL_INCLUDEDIR@/sdi12
```

Rationale: the `${prefix}/...` form depends on `CMAKE_INSTALL_PREFIX` being set correctly at configure time. `dpkg-buildpackage` sets it to `/usr` via `dh_auto_configure`, but using `CMAKE_INSTALL_FULL_*` bakes the absolute path into the `.pc` file at configure time, making it robust regardless of how cmake is invoked. Drop the `prefix=` and `exec_prefix=` lines entirely since they are not referenced by the remaining fields.

### 1.3 `debian/control` changes

- Update `Standards-Version` to current. **Must be verified inside a current Debian `sid`/`trixie` environment** (`apt-cache show debian-policy | grep Version`), not the host WSL install, which may lag.
- Add `pkg-config` to `Build-Depends`. Also add the following to `debian/rules` to verify the installed `.pc` file during `dh_auto_test`:
  ```makefile
  override_dh_auto_test:
      dh_auto_test --no-parallel
      pkg-config --exists --print-errors sdi12
      pkg-config --libs --cflags sdi12
  ```
- Change `libsdi12-dev` dependency from `libsdi12-0 (= ${binary:Version})` to:
  ```
  Depends: libsdi12-0 (>= ${binary:Version}), libsdi12-0 (<< ${binary:Version}.1~), ${misc:Depends}
  ```
  Rationale: `= ${binary:Version}` includes the binNMU suffix (e.g., `0.3.0-1+b1`), which makes `libsdi12-dev` uninstallable after a binary-only rebuild on a new architecture. The `>=`/`<<` pair tracks the correct version range without locking to an exact binNMU suffix.
- Add `Suggests: pkg-config` to `libsdi12-dev` (conventional for `-dev` packages that install a `.pc` file).

### 1.4 Symbols verification

Run after the build. The `-c4` flag makes symbol mismatches hard errors (matching what the Debian build daemon enforces):

```bash
dpkg-gensymbols -plibsdi12-0 -c4
```

Reconcile `debian/libsdi12-0.symbols` against actual exported symbols. Any new symbols get added at the current version; any removed symbols are an ABI break requiring a SOVERSION bump before upload.

### 1.5 ldconfig trigger

With `debhelper-compat (= 13)`, `dh_makeshlibs` auto-generates the `ldconfig` trigger for `libsdi12-0`. Do **not** create a manual `debian/libsdi12-0.triggers` file — that would produce a duplicate trigger error.

### 1.6 WSL build + test workflow

```bash
# Use a sid/trixie schroot or Docker image for accurate results.
# In WSL with a current Debian environment:
sudo apt install build-essential devscripts debhelper cmake lintian pkg-config

cd /path/to/libsdi12

# Local binary build for iterative testing:
dpkg-buildpackage -us -uc -b
lintian --pedantic ../libsdi12_0.3.0-1_amd64.changes
dpkg-gensymbols -plibsdi12-0 -c4

# Verify pkg-config output:
dpkg -x ../libsdi12-dev_0.3.0-1_amd64.deb /tmp/sdi12-dev-test
PKG_CONFIG_PATH=/tmp/sdi12-dev-test/usr/lib/x86_64-linux-gnu/pkgconfig \
    pkg-config --libs --cflags sdi12

# Verify watch file:
uscan --report --no-download
```

### 1.7 Source package for upload

The NEW queue requires a **signed source upload**, not a binary-only build:

```bash
# Generate .orig.tar.gz from the upstream tag (or uscan):
uscan --download-current-version   # downloads and creates .orig.tar.gz

# Build source + binary:
dpkg-buildpackage -sa              # includes orig tarball on first upload
debsign ../libsdi12_0.3.0-1_source.changes   # sign with GPG key registered on keyring.debian.org

# Or sign inline if key is available:
dpkg-buildpackage -k<KEYID>
```

The `.orig.tar.gz` must match the upstream tag byte-for-byte (verified by `uscan`). Reproducibility matters for the NEW queue.

Target: zero lintian errors, zero warnings (pedantic), clean `uscan` output.

---

## Phase 2 — libsdi12-verifier CMakeLists.txt (upstream)

Replace the hardcoded submodule block with a two-path approach. The system-library branch uses `IMPORTED_TARGET` mode (CMake ≥ 3.6) so that all flags, include dirs, and library paths are encoded in a single imported target — avoiding the fragile bare-name + `target_link_directories` pattern that fails on Debian multi-arch layouts.

The existing `target_include_directories(sdi12-verifier PRIVATE include ${LIBSDI12_DIR})` line **must also be updated**: remove `${LIBSDI12_DIR}` from it unconditionally and move that reference inside the `else()` block only (submodule path). In the system-library branch, include dirs are propagated by `PkgConfig::LIBSDI12`.

```cmake
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBSDI12 QUIET IMPORTED_TARGET sdi12)
endif()

if(TARGET PkgConfig::LIBSDI12)
    # System library path (Debian build, etc.)
    message(STATUS "Using system libsdi12 ${LIBSDI12_VERSION}")
    # Link directly to the imported target — do NOT create an alias,
    # as CMake may error if an alias target of that name already exists.
    target_link_libraries(sdi12-verifier PRIVATE PkgConfig::LIBSDI12)
else()
    # Submodule fallback
    message(STATUS "System libsdi12 not found — using submodule")
    set(LIBSDI12_DIR ${CMAKE_SOURCE_DIR}/lib/libsdi12)
    if(NOT EXISTS ${LIBSDI12_DIR}/sdi12.h)
        message(FATAL_ERROR "libsdi12 submodule not found. Run: git submodule update --init")
    endif()
    add_library(sdi12 STATIC
        ${LIBSDI12_DIR}/sdi12_crc.c
        ${LIBSDI12_DIR}/sdi12_sensor.c
        ${LIBSDI12_DIR}/sdi12_master.c
    )
    target_include_directories(sdi12 PUBLIC ${LIBSDI12_DIR})
    target_link_libraries(sdi12-verifier PRIVATE sdi12)
endif()
```

Because `target_link_libraries(sdi12-verifier ...)` now appears inside each branch, remove the unconditional `target_link_libraries(sdi12-verifier PRIVATE sdi12)` line that currently sits outside the if/else block.

Update the verifier's `target_include_directories` to remove `${LIBSDI12_DIR}` — it is now propagated transitively by each branch:

```cmake
# Before:
target_include_directories(sdi12-verifier PRIVATE
    include
    ${LIBSDI12_DIR}
)

# After:
target_include_directories(sdi12-verifier PRIVATE
    include
)
```

---

## Phase 3 — libsdi12-verifier `debian/` packaging

### Files

| File | Purpose |
|---|---|
| `debian/control` | Source + binary package definitions |
| `debian/changelog` | Initial 0.6.0-1 entry |
| `debian/copyright` | DEP-5, MIT (see note on submodule below) |
| `debian/rules` | Standard dh rules with hardening=+all |
| `debian/sdi12-verifier.install` | Installs `usr/bin/sdi12-verifier` |
| `debian/source/format` | `3.0 (quilt)` |
| `debian/watch` | uscan v4, GitHub tags |
| `debian/sdi12-verifier.1` | Man page (required for binary in Debian main) |

### Upstream tarball and submodule

The upstream tarball for the Debian source package **must not include the submodule** — the build must succeed using the installed `libsdi12-dev`. Exclude `lib/libsdi12/` via `debian/source/options` (preferred — keeps the upstream tarball unmodified, exclusion lives entirely in the packaging layer):

```
extend-diff-ignore = lib/libsdi12
```

This means `debian/copyright` for the verifier does not need to cover libsdi12 source files, simplifying the copyright file.

Verify the build succeeds without the submodule present before upload:
```bash
rm -rf lib/libsdi12/
dpkg-buildpackage -us -uc -b   # must succeed with only libsdi12-dev installed
```

### `debian/control`

```
Source: libsdi12-verifier
Section: electronics
Priority: optional
Maintainer: Phillip Weinstock <phillipweinstock@gmail.com>
Build-Depends: debhelper-compat (= 13), cmake (>= 3.14), libsdi12-dev, pkg-config
Standards-Version: <current — verify in sid environment>
Homepage: https://github.com/phillipweinstock/libsdi12-verifier
Vcs-Git: https://github.com/phillipweinstock/libsdi12-verifier.git
Vcs-Browser: https://github.com/phillipweinstock/libsdi12-verifier
Rules-Requires-Root: no

Package: sdi12-verifier
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}
Description: SDI-12 v1.4 compliance tester
 Open-source compliance tester for SDI-12 v1.4 sensors and data recorders.
 Runs 47 protocol tests covering every command type, measures response timing
 to the microsecond, and produces plain-text or JSON compliance reports with
 references to the relevant spec sections.
 .
 Supports sensor testing (verifier acts as recorder), recorder testing
 (verifier simulates a sensor), passive bus monitoring, and an interactive
 transparent mode for manual command entry.
```

Note: `Section: electronics` is the best fit but may generate a NEW committee query — `science` or `utils` are alternatives. Not a blocking issue.

### Man page

`debian/sdi12-verifier.1` — static troff covering:
- **Synopsis:** `sdi12-verifier --port <port> [options]` and `sdi12-verifier --self-test`
- **Modes (mutually exclusive):** `--test-sensor`, `--test-recorder`, `--self-test`, `--monitor`, `--transparent`
- **Options:** `--port`, `--addr`, `--format`, `-o`, `--rts`
- **Exit codes:** 0 = all tests passed, 1 = one or more tests failed, 2 = internal error

`--self-test` appears in the SYNOPSIS as its own form (no `--port` required) — not duplicated across Modes and Options.

### WSL build + test workflow

```bash
# Install libsdi12 from Phase 1 build output:
sudo dpkg -i ../libsdi12-0_0.3.0-1_amd64.deb ../libsdi12-dev_0.3.0-1_amd64.deb

# Confirm system libsdi12 is discoverable:
pkg-config --libs --cflags sdi12

# Verify build without submodule:
rm -rf lib/libsdi12/
dpkg-buildpackage -us -uc -b
lintian --pedantic ../libsdi12-verifier_0.6.0-1_amd64.changes

# Verify watch file:
uscan --report --no-download

# Run installed binary self-test:
sudo dpkg -i ../sdi12-verifier_0.6.0-1_amd64.deb
sdi12-verifier --self-test
```

### Autopkgtest note

`debian/tests/` is deferred as out-of-scope for this design, but a sponsoring DD will likely require at least a minimal autopkgtest before accepting the upload. Plan to add a compile-and-link test (build a trivial program against `-lsdi12`) for libsdi12, and a `sdi12-verifier --self-test` invocation for the verifier, before approaching a sponsor.

---

## Success Criteria

- `dpkg-buildpackage -us -uc` succeeds for both packages
- `lintian --pedantic` reports zero errors and zero warnings for both
- `sdi12-verifier --self-test` passes when run against the installed binary
- `pkg-config --libs sdi12` resolves correctly on the test system
- `uscan --report --no-download` exits cleanly for both watch files
- Build succeeds for libsdi12-verifier with submodule directory absent (system library path exercised)
- Both packages ready for sponsorship submission (mentors.debian.net or direct sponsor)

---

## Out of Scope

- RPM / Arch / Homebrew packaging (deferred)
- Autopkgtest (`debian/tests/`) — likely required before sponsorship; add as follow-up
- Uploading to Launchpad PPA (deferred, same packaging artifacts work)
- Bumping SDI12_PROJECT from libsdi12@0.2.0 to 0.3.0 (separate task)
