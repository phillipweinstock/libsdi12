# Debian Packaging Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get `libsdi12` and `libsdi12-verifier` to lintian-clean binary builds ready for Debian NEW queue submission.

**Architecture:** Sequential — fix and lintian-clean `libsdi12` first (it goes through NEW first), then create `debian/` for `libsdi12-verifier` and update its `CMakeLists.txt` to use the installed system library. **File edits** happen via Claude Code tools on the Windows filesystem. **Build/test commands** must be run in a WSL terminal (the current shell is MSYS2/Git Bash — `apt`, `dpkg-buildpackage`, `lintian` do not exist there). Open WSL via Windows Terminal or `wsl.exe`. Source files are visible inside WSL at `/mnt/c/Users/phillip/Documents/projects/`.

**Tech Stack:** C11, CMake 3.14+, debhelper-compat 13, dpkg-buildpackage, lintian, pkg-config, WSL (Debian sid/trixie or Ubuntu)

---

## Chunk 1: WSL setup + libsdi12 fixes

### Task 1: Verify WSL build environment

**Files:** none

- [ ] **Step 1: Open a WSL terminal and install build tools**

All commands in this plan that use `apt`, `dpkg-buildpackage`, `lintian`, etc. must be run in WSL — not in Git Bash/MSYS2.

```bash
# In WSL (wsl.exe, or Windows Terminal WSL tab):
cat /etc/os-release        # note your distro/version
sudo apt update
sudo apt install -y build-essential devscripts debhelper cmake lintian \
    pkg-config git-buildpackage debian-policy
```

- [ ] **Step 2: Determine current Debian policy Standards-Version**

```bash
# Query the installed debian-policy package (no docker needed):
dpkg-query -W -f='${Version}' debian-policy
```

Expected: a version string like `4.7.0.1`. Note it — called `<POLICY_VERSION>` below.

If your WSL is Ubuntu and the `debian-policy` package is outdated, check https://www.debian.org/doc/debian-policy/ for the current version number and use that.

- [ ] **Step 3: Verify WSL can see the Windows filesystem**

```bash
ls /mnt/c/Users/phillip/Documents/projects/libsdi12/debian/
```

Expected: `changelog  control  copyright  libsdi12-0.install  ...`

---

### Task 2: libsdi12 — fix `sdi12.h` copyright comment

**Files:**
- Modify: `C:\Users\phillip\Documents\projects\libsdi12\sdi12.h` (line 12)

- [ ] **Step 1: Verify the current bad state**

```bash
grep -n "All Rights Reserved" /mnt/c/Users/phillip/Documents/projects/libsdi12/sdi12.h
```

Expected: `12: * @copyright 2026 All Rights Reserved`

- [ ] **Step 2: Apply the fix**

Edit `sdi12.h` line 12:
```c
// Before:
 * @copyright 2026 All Rights Reserved
// After:
 * @copyright 2026 Phillip Weinstock
```

- [ ] **Step 3: Verify**

```bash
grep -n "copyright" /mnt/c/Users/phillip/Documents/projects/libsdi12/sdi12.h
```

Expected: `12: * @copyright 2026 Phillip Weinstock`

---

### Task 3: libsdi12 — fix `sdi12.pc.in`

**Files:**
- Modify: `C:\Users\phillip\Documents\projects\libsdi12\sdi12.pc.in`

- [ ] **Step 1: Verify current bad state**

```bash
cat /mnt/c/Users/phillip/Documents/projects/libsdi12/sdi12.pc.in
```

Expected: `prefix=@CMAKE_INSTALL_PREFIX@` present.

- [ ] **Step 2: Apply the fix**

Replace the entire file content:

```
Name: libsdi12
Description: @PROJECT_DESCRIPTION@
URL: @PROJECT_HOMEPAGE_URL@
Version: @PROJECT_VERSION@
Cflags: -I@CMAKE_INSTALL_FULL_INCLUDEDIR@/sdi12
Libs: -L@CMAKE_INSTALL_FULL_LIBDIR@ -lsdi12
```

(Drop `prefix=`, `exec_prefix=`, `libdir=`, `includedir=` lines — bake absolute paths directly into `Cflags` and `Libs`.)

- [ ] **Step 3: Verify**

```bash
cat /mnt/c/Users/phillip/Documents/projects/libsdi12/sdi12.pc.in
```

Expected: no `${prefix}` references, `@CMAKE_INSTALL_FULL_LIBDIR@` and `@CMAKE_INSTALL_FULL_INCLUDEDIR@` present.

---

### Task 4: libsdi12 — fix `debian/control`

**Files:**
- Modify: `C:\Users\phillip\Documents\projects\libsdi12\debian\control`

- [ ] **Step 1: Update Standards-Version**

Replace `Standards-Version: 4.6.2` with `Standards-Version: <POLICY_VERSION>` (from Task 1 Step 2).

- [ ] **Step 2: Add `pkg-config` to Build-Depends**

Change:
```
Build-Depends:
 debhelper-compat (= 13),
 cmake (>= 3.14),
```
To:
```
Build-Depends:
 debhelper-compat (= 13),
 cmake (>= 3.14),
 pkg-config,
```

- [ ] **Step 3: Fix `libsdi12-dev` Depends for binNMU safety**

Change:
```
Depends: libsdi12-0 (= ${binary:Version}), ${misc:Depends}
```
To:
```
Depends: libsdi12-0 (>= ${source:Version}), libsdi12-0 (<< ${source:Version}.1~), ${misc:Depends}
```

Note: use `${source:Version}`, not `${binary:Version}`. The `binary` version includes the binNMU suffix (e.g. `0.3.0-1+b2`), making the upper bound nonsensical across rebuilds. The `source` version stays at `0.3.0-1` regardless of binNMU, which is the correct Policy 8.6 idiom.

- [ ] **Step 4: Add `Suggests: pkg-config` to `libsdi12-dev`**

Add after the `Depends:` line in the `libsdi12-dev` stanza:
```
Suggests: pkg-config
```

- [ ] **Step 5: Verify the whole file looks correct**

```bash
cat /mnt/c/Users/phillip/Documents/projects/libsdi12/debian/control
```

---

### Task 5: libsdi12 — note on `debian/rules` and pkg-config

**Files:** none (no change needed)

The `pkg-config` tool is in `Build-Depends` so it is available during the build. However, at the point `override_dh_auto_test` runs, the generated `sdi12.pc` file is only in the cmake build directory (`debian/obj-*/`) — not yet staged or installed system-wide. Running `pkg-config --exists sdi12` there would fail.

The `.pc` file is verified in Task 6 Step 4 instead, by extracting the built `.deb` and pointing `PKG_CONFIG_PATH` at it. No change to `debian/rules` is needed for this purpose.

`debian/rules` remains:
```makefile
#!/usr/bin/make -f

export DEB_BUILD_MAINT_OPTIONS = hardening=+all

%:
	dh $@

override_dh_auto_configure:
	dh_auto_configure -- \
		-DSDI12_BUILD_SHARED=ON \
		-DSDI12_BUILD_STATIC=ON \
		-DSDI12_BUILD_TESTS=ON

override_dh_auto_test:
	dh_auto_test --no-parallel
```

---

### Task 6: libsdi12 — binary build + lintian clean

**Files:**
- Possibly modify: `debian/libsdi12-0.symbols` (if `dpkg-gensymbols` finds differences)

- [ ] **Step 1: Build binary package**

```bash
cd /mnt/c/Users/phillip/Documents/projects/libsdi12
dpkg-buildpackage -us -uc -b 2>&1 | tee /tmp/libsdi12-build.log
echo "Exit: $?"
```

Expected: `dpkg-buildpackage: info: binary-only upload (no source included)` and exit 0.
If it fails: check `/tmp/libsdi12-build.log` for the error, fix, and re-run.

- [ ] **Step 2: Run lintian**

```bash
# dpkg-buildpackage places output in the parent of the source directory:
lintian --pedantic /mnt/c/Users/phillip/Documents/projects/libsdi12_0.3.0-1_amd64.changes \
    2>&1 | tee /tmp/libsdi12-lintian.log
cat /tmp/libsdi12-lintian.log
```

Expected: no output (zero errors, zero warnings). If there are findings, fix them and repeat Step 1.

Common fixable findings:
- `package-contains-empty-directory` → check install files
- `no-symbols-control-file` → already have symbols file, check it
- Any `description-*` tag → tweak the Description fields in `debian/control`

- [ ] **Step 3: Verify symbols**

`dh_makeshlibs` already runs `dpkg-gensymbols` internally during the build. To also check manually, extract the `.deb` first so the `.so` is accessible:

```bash
mkdir -p /tmp/libsdi12-0-test
dpkg -x /mnt/c/Users/phillip/Documents/projects/libsdi12-0_0.3.0-1_amd64.deb \
    /tmp/libsdi12-0-test
dpkg-gensymbols -plibsdi12-0 -c4 \
    -l /tmp/libsdi12-0-test/usr/lib/x86_64-linux-gnu \
    2>&1 | tee /tmp/libsdi12-symbols.log
cat /tmp/libsdi12-symbols.log
```

Expected: exit 0 with no output. If symbols have changed, `dpkg-gensymbols` prints a diff — apply it to `debian/libsdi12-0.symbols` and rebuild.

- [ ] **Step 4: Verify pkg-config output is correct**

```bash
mkdir -p /tmp/sdi12-dev-test
dpkg -x /mnt/c/Users/phillip/Documents/projects/libsdi12-dev_0.3.0-1_amd64.deb \
    /tmp/sdi12-dev-test
PKG_CONFIG_PATH=/tmp/sdi12-dev-test/usr/lib/x86_64-linux-gnu/pkgconfig \
    pkg-config --libs --cflags sdi12
```

Expected: something like `-I/usr/include/sdi12 -L/usr/lib/x86_64-linux-gnu -lsdi12` — no `${prefix}` literals.

- [ ] **Step 5: Verify watch file**

```bash
cd /mnt/c/Users/phillip/Documents/projects/libsdi12
uscan --report --no-download --timeout 30
```

Expected: exits cleanly. If it reports a newer version available, that is informational — note for changelog.

- [ ] **Step 6: Commit**

```bash
cd /mnt/c/Users/phillip/Documents/projects/libsdi12
git add sdi12.h sdi12.pc.in debian/control debian/libsdi12-0.symbols
git commit -m "$(cat <<'EOF'
fix: Debian packaging fixes for NEW queue upload

- sdi12.h: fix copyright comment (All Rights Reserved -> Phillip Weinstock)
- sdi12.pc.in: use CMAKE_INSTALL_FULL_* for absolute pkg-config paths;
  drop prefix=/exec_prefix= lines (Cflags/Libs now use full paths)
- debian/control: update Standards-Version to current policy; add
  pkg-config to Build-Depends; use source:Version in libsdi12-dev
  Depends for binNMU safety; add Suggests: pkg-config to libsdi12-dev
EOF
)"
```

---

## Chunk 2: libsdi12-verifier CMakeLists.txt + full debian/ packaging

### Task 7: libsdi12-verifier — two-path CMakeLists.txt

**Files:**
- Modify: `C:\Users\phillip\Documents\projects\libsdi12-verifier\CMakeLists.txt`

- [ ] **Step 1: Verify the current submodule-only block**

```bash
head -20 /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier/CMakeLists.txt
```

Expected: `set(LIBSDI12_DIR ...)` and `add_library(sdi12 STATIC ...)` hardcoded.

- [ ] **Step 2: Replace the libsdi12 block and update target_link_libraries**

Replace the current `# ── libsdi12 (git submodule) ──` block and the unconditional `target_link_libraries(sdi12-verifier PRIVATE sdi12)` call.

The new `CMakeLists.txt` libsdi12 section (replaces lines 7-14 in the current file):

```cmake
# ── libsdi12: system library (preferred) or git submodule fallback ────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBSDI12 QUIET IMPORTED_TARGET sdi12)
endif()

if(TARGET PkgConfig::LIBSDI12)
    message(STATUS "libsdi12: using system library (${LIBSDI12_VERSION})")
else()
    message(STATUS "libsdi12: system library not found, using submodule")
    set(LIBSDI12_DIR ${CMAKE_SOURCE_DIR}/lib/libsdi12)
    if(NOT EXISTS ${LIBSDI12_DIR}/sdi12.h)
        message(FATAL_ERROR
            "libsdi12 submodule not initialised.\n"
            "Run: git submodule update --init")
    endif()
    add_library(sdi12 STATIC
        ${LIBSDI12_DIR}/sdi12_crc.c
        ${LIBSDI12_DIR}/sdi12_sensor.c
        ${LIBSDI12_DIR}/sdi12_master.c
    )
    target_include_directories(sdi12 PUBLIC ${LIBSDI12_DIR})
endif()
```

Update `target_include_directories(sdi12-verifier ...)` — remove `${LIBSDI12_DIR}`:

```cmake
target_include_directories(sdi12-verifier PRIVATE
    include
)
```

Replace the unconditional `target_link_libraries(sdi12-verifier PRIVATE sdi12)` with a conditional form placed immediately after the `add_executable` block:

```cmake
if(TARGET PkgConfig::LIBSDI12)
    target_link_libraries(sdi12-verifier PRIVATE PkgConfig::LIBSDI12)
else()
    target_link_libraries(sdi12-verifier PRIVATE sdi12)
endif()
```

- [ ] **Step 3: Test submodule path (normal developer build)**

```bash
cd /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier
mkdir -p build-test-submodule && cd build-test-submodule
cmake .. -DCMAKE_BUILD_TYPE=Debug 2>&1 | grep "libsdi12:"
cmake --build . 2>&1 | tail -5
```

Expected: `libsdi12: using submodule` (since system libsdi12 isn't installed yet) and successful build.

- [ ] **Step 4: Install libsdi12-dev from Phase 1 build output and test system library path**

```bash
# Install the packages built in Chunk 1 (dpkg-buildpackage puts output in the
# parent of the source directory, i.e. /mnt/c/Users/phillip/Documents/projects/):
sudo dpkg -i /mnt/c/Users/phillip/Documents/projects/libsdi12-0_0.3.0-1_amd64.deb \
             /mnt/c/Users/phillip/Documents/projects/libsdi12-dev_0.3.0-1_amd64.deb

# Verify pkg-config finds it:
pkg-config --libs --cflags sdi12

# Now build without submodule to exercise system library path:
cd /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier
mkdir -p build-test-system && cd build-test-system
cmake .. -DCMAKE_BUILD_TYPE=Debug 2>&1 | grep "libsdi12:"
cmake --build . 2>&1 | tail -5
./sdi12-verifier --self-test
```

Expected: `libsdi12: using system library (0.3.0)`, successful build, and `--self-test` passes.

- [ ] **Step 5: Commit**

```bash
cd /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier
git add CMakeLists.txt
git commit -m "build: support system libsdi12 via pkg-config with submodule fallback

When libsdi12 is installed as a system library (e.g. from libsdi12-dev),
pkg_check_modules finds it and links via PkgConfig::LIBSDI12. Falls back
to the git submodule for standalone developer builds.

Required for Debian packaging: the Debian build daemon uses libsdi12-dev
as a build-dep and must not require the submodule."
```

---

### Task 8: libsdi12-verifier — `debian/control`

**Files:**
- Create: `C:\Users\phillip\Documents\projects\libsdi12-verifier\debian\control`

- [ ] **Step 1: Create `debian/` directory**

```bash
mkdir -p /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier/debian
```

- [ ] **Step 2: Write `debian/control`**

```
Source: libsdi12-verifier
Section: electronics
Priority: optional
Maintainer: Phillip Weinstock <phillipweinstock@gmail.com>
Build-Depends:
 debhelper-compat (= 13),
 cmake (>= 3.14),
 libsdi12-dev,
 pkg-config,
Standards-Version: <POLICY_VERSION>
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

(Fill in `<POLICY_VERSION>` from Task 1 Step 2.)

---

### Task 9: libsdi12-verifier — `debian/changelog`, `debian/copyright`, `debian/rules`

**Files:**
- Create: `debian/changelog`
- Create: `debian/copyright`
- Create: `debian/rules`

- [ ] **Step 1: Write `debian/changelog`**

```
libsdi12-verifier (0.6.0-1) unstable; urgency=medium

  * Initial Debian packaging.
  * Upstream release 0.6.0:
    - 47 SDI-12 v1.4 compliance tests (31 sensor, 16 recorder).
    - Timing measurement to microsecond precision.
    - Plain-text and JSON output formats.
    - Passive bus monitor and interactive transparent modes.
    - Self-test via loopback HAL (no hardware required).
    - Builds against system libsdi12-dev; falls back to git submodule
      for standalone developer builds.

 -- Phillip Weinstock <phillipweinstock@gmail.com>  Sun, 15 Mar 2026 12:00:00 +0000
```

- [ ] **Step 2: Write `debian/copyright`**

```
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: libsdi12-verifier
Upstream-Contact: Phillip Weinstock <phillipweinstock@gmail.com>
Source: https://github.com/phillipweinstock/libsdi12-verifier

Files: *
Copyright: 2026 Phillip Weinstock
License: MIT

Files: debian/*
Copyright: 2026 Phillip Weinstock
License: MIT

License: MIT
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 .
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 .
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
```

- [ ] **Step 3: Write `debian/rules`**

```makefile
#!/usr/bin/make -f

export DEB_BUILD_MAINT_OPTIONS = hardening=+all

%:
	dh $@

override_dh_auto_configure:
	dh_auto_configure -- \
		-DCMAKE_BUILD_TYPE=Release
```

Make it executable:
```bash
chmod +x /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier/debian/rules
```

---

### Task 10: libsdi12-verifier — install file, source format/options, watch file

**Files:**
- Create: `debian/sdi12-verifier.install`
- Create: `debian/source/format`
- Create: `debian/source/options`
- Create: `debian/watch`

- [ ] **Step 1: Write `debian/sdi12-verifier.install`**

```
usr/bin/sdi12-verifier
```

- [ ] **Step 2: Write `debian/source/format`**

```
3.0 (quilt)
```

- [ ] **Step 3: Write `debian/source/options`**

```
extend-diff-ignore = lib/libsdi12
```

This prevents dpkg-source from flagging the (empty) submodule directory.

- [ ] **Step 4: Write `debian/watch`**

```
version=4
opts=filenamemangle=s/.+\/v?(\d\S+)\.tar\.gz/libsdi12-verifier-$1\.tar\.gz/ \
  https://github.com/phillipweinstock/libsdi12-verifier/tags .*/v?(\d\S+)\.tar\.gz
```

- [ ] **Step 5: Verify watch file**

```bash
cd /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier
uscan --report --no-download --timeout 30
```

Expected: clean exit.

---

### Task 11: libsdi12-verifier — man page `debian/sdi12-verifier.1`

**Files:**
- Create: `C:\Users\phillip\Documents\projects\libsdi12-verifier\debian\sdi12-verifier.1`

- [ ] **Step 1: Write the man page**

```troff
.TH SDI12-VERIFIER 1 "March 2026" "sdi12-verifier 0.6.0" "User Commands"
.SH NAME
sdi12\-verifier \- SDI\-12 v1.4 compliance tester
.SH SYNOPSIS
.B sdi12\-verifier
\fB\-\-port\fR \fIport\fR
[\fB\-\-test\-sensor\fR|\fB\-\-test\-recorder\fR|\fB\-\-monitor\fR|\fB\-\-transparent\fR]
[\fIoptions\fR]
.br
.B sdi12\-verifier
\fB\-\-self\-test\fR
.SH DESCRIPTION
.B sdi12\-verifier
is an open\-source SDI\-12 v1.4 compliance tester for sensors and data
recorders.
It connects to a device via a serial port and runs up to 47 protocol tests
covering every command type defined in the SDI\-12 v1.4 specification.
Response timing is measured to microsecond precision.
Results are reported as plain text or JSON with references to the relevant
specification sections.
.SH MODES
.TP
.B \-\-test\-sensor
Test a connected SDI\-12 sensor.
The verifier acts as the data recorder, sends commands, and validates the
sensor's responses.
Runs 31 compliance tests.
.TP
.B \-\-test\-recorder
Test a connected data recorder.
The verifier simulates a sensor and validates the recorder's command
behaviour.
Runs 16 compliance tests.
.TP
.B \-\-monitor
Passive bus monitor.
Captures and displays raw bus traffic without participating in
communication.
.TP
.B \-\-transparent
Interactive transparent mode.
Allows manual entry of SDI\-12 commands and displays raw responses.
.TP
.B \-\-self\-test
Run a loopback self\-test.
No hardware is required.
Verifies the verifier's own logic against a simulated sensor/recorder pair.
.SH OPTIONS
.TP
\fB\-\-port\fR \fIport\fR
Serial port to use (e.g.\&
.IR /dev/ttyUSB0
or
.IR COM3 ).
Required for all modes except
.BR \-\-self\-test .
.TP
\fB\-\-addr\fR \fIaddr\fR
SDI\-12 sensor address to test (default:
.BR 0 ).
Valid addresses: 0\(en9, A\(enZ, a\(enz.
.TP
\fB\-\-format\fR \fBtext\fR|\fBjson\fR
Output format for the compliance report (default:
.BR text ).
.TP
\fB\-o\fR \fIfile\fR
Write the compliance report to
.I file
instead of standard output.
.TP
.B \-\-rts
Enable RTS direction control for half\-duplex RS\-485 adapters.
.SH EXIT STATUS
.TP
.B 0
All tests passed.
.TP
.B 1
One or more tests failed.
.TP
.B 2
Internal error (invalid arguments, port error, etc.).
.SH EXAMPLES
Test a sensor on /dev/ttyUSB0 at address 0:
.PP
.nf
.RS
sdi12\-verifier \-\-port /dev/ttyUSB0 \-\-test\-sensor \-\-addr 0
.RE
.fi
.PP
Run a self\-test (no hardware required):
.PP
.nf
.RS
sdi12\-verifier \-\-self\-test
.RE
.fi
.PP
Generate a JSON report to a file:
.PP
.nf
.RS
sdi12\-verifier \-\-port /dev/ttyUSB0 \-\-test\-sensor \-\-format json \-o report.json
.RE
.fi
.SH SEE ALSO
.BR sdi12 (3)
.SH AUTHOR
Phillip Weinstock <phillipweinstock@gmail.com>
.SH COPYRIGHT
Copyright \(co 2026 Phillip Weinstock.
License MIT.
```

- [ ] **Step 2: Verify man page renders correctly**

```bash
man /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier/debian/sdi12-verifier.1
```

Check: NAME, SYNOPSIS, MODES, OPTIONS, EXIT STATUS, EXAMPLES all render cleanly. No troff errors.

- [ ] **Step 3: Add man page install entry**

Append to `debian/sdi12-verifier.install`:
```
usr/share/man/man1/sdi12-verifier.1*
```

And add a `debian/sdi12-verifier.manpages` file with:
```
debian/sdi12-verifier.1
```

This tells `dh_installman` where to find it.

---

### Task 12: libsdi12-verifier — binary build + lintian clean

**Files:**
- None expected; fix any lintian findings as they arise.

- [ ] **Step 1: Confirm libsdi12-dev is installed**

```bash
pkg-config --libs --cflags sdi12
dpkg -l libsdi12-dev
```

Expected: `pkg-config` returns flags; `dpkg -l` shows `ii  libsdi12-dev  0.3.0-1`.

- [ ] **Step 2: Verify build succeeds without the submodule**

```bash
# Simulate the Debian build daemon's view (no submodule):
ls /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier/lib/libsdi12/
# If non-empty, the dpkg-buildpackage will still work — extend-diff-ignore
# handles dpkg-source; cmake will prefer the system library anyway.
```

- [ ] **Step 3: Build binary package**

```bash
cd /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier
dpkg-buildpackage -us -uc -b 2>&1 | tee /tmp/verifier-build.log
echo "Exit: $?"
```

Expected: exit 0. If it fails, check the log and fix the issue.

- [ ] **Step 4: Run lintian**

```bash
lintian --pedantic \
    /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier_0.6.0-1_amd64.changes \
    2>&1 | tee /tmp/verifier-lintian.log
cat /tmp/verifier-lintian.log
```

Expected: no output. Fix any findings and repeat Step 3.

- [ ] **Step 5: Run installed binary self-test**

```bash
sudo dpkg -i /mnt/c/Users/phillip/Documents/projects/sdi12-verifier_0.6.0-1_amd64.deb
sdi12-verifier --self-test
```

Expected: all self-tests pass, exit 0.

- [ ] **Step 6: Verify watch file**

```bash
cd /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier
uscan --report --no-download --timeout 30
```

Expected: clean exit.

- [ ] **Step 7: Commit**

```bash
cd /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier
git add debian/
git commit -m "packaging: initial Debian packaging for libsdi12-verifier 0.6.0-1

Targets official Debian upload (NEW queue). Packages sdi12-verifier binary
with man page. Uses system libsdi12-dev as build-dep; submodule excluded
from Debian source package via debian/source/options."
```

---

## Source Package Preparation (both packages)

This step is done manually by the maintainer when ready to upload — requires a GPG key registered on keyring.debian.org.

### Task 13: Prepare signed source uploads

- [ ] **Step 1: libsdi12 — generate orig tarball and sign**

```bash
cd /mnt/c/Users/phillip/Documents/projects/libsdi12
uscan --download-current-version    # creates ../libsdi12_0.3.0.orig.tar.gz
dpkg-buildpackage -sa -k<YOUR_GPG_KEY_ID>
# Output: ../libsdi12_0.3.0-1_source.changes (signed)
```

- [ ] **Step 2: libsdi12-verifier — generate orig tarball and sign**

```bash
cd /mnt/c/Users/phillip/Documents/projects/libsdi12-verifier
uscan --download-current-version
dpkg-buildpackage -sa -k<YOUR_GPG_KEY_ID>
# Output: ../libsdi12-verifier_0.6.0-1_source.changes (signed)
```

- [ ] **Step 3: Final lintian check on source packages**

```bash
lintian --pedantic ../libsdi12_0.3.0-1_source.changes
lintian --pedantic ../libsdi12-verifier_0.6.0-1_source.changes
```

Expected: zero output for both.

- [ ] **Step 4: Submit to Debian via sponsor**

Upload to mentors.debian.org (requires free account) or send directly to a sponsoring DD. Do **not** dput to the Debian archive directly unless you have DM/DD upload rights.

```bash
dput mentors ../libsdi12_0.3.0-1_source.changes
dput mentors ../libsdi12-verifier_0.6.0-1_source.changes
```
