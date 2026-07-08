---
name: SDI-12 project overview
description: State of libsdi12, libsdi12-verifier, and SDI12_PROJECT (Arduino station firmware)
type: project
---

Three related SDI-12 projects in C:\Users\phillip\Documents\projects\:

## libsdi12 (v0.3.0)
- Pure C11, MIT, zero dependencies — sensor + master, full SDI-12 v1.4 spec coverage
- CMake build: builds shared + static lib, pkg-config file
- Debian packaging already exists: `debian/` directory with control, rules, changelog, install, symbols, copyright, watch
  - Packages: libsdi12-0 (shared) + libsdi12-dev (headers, static, pkg-config)
  - debhelper-compat 13, Standards-Version 4.6.2, source format 3.0 (quilt)
  - Symbols file lists all exported symbols at 0.3.0
- Also has library.json and library.properties for PlatformIO/Arduino Library Manager
- 98 unit + metamorphic tests in test/
- Homepage: https://github.com/phillipweinstock/libsdi12
- **Planned: package for Debian and other distros**

## libsdi12-verifier (v0.6.0)
- CLI compliance tester for SDI-12 sensors and recorders
- 47 compliance tests (31 sensor, 16 recorder), timing to microsecond, plain-text + JSON output
- Modes: --test-sensor, --test-recorder, --self-test, --monitor, --transparent
- HAL: POSIX (Linux/macOS) + Win32 + loopback
- libsdi12 is a git submodule at lib/libsdi12
- MONITOR_DECODE_PLAN.md: planned protocol-aware decoded monitor (state machine, 4 phases, ~8-10h estimated)
  - Phase 1: break detection + command/response pairing
  - Phase 2: command type identification
  - Phase 3: response parsing + validation
  - Phase 4: statistics/summary
- Homepage: https://github.com/phillipweinstock/libsdi12-verifier
- **No Debian packaging yet**

## SDI12_PROJECT (Arduino/PlatformIO station firmware)
- Arduino Due (atmelsam) firmware — real sensor station
- Uses libsdi12@^0.2.0 from PlatformIO (pinned to old version, not 0.3.0 yet)
- Dependencies: BME680, BH1750, ST7735/ST7789, RTClib, RTCDue, SdFat, DueFlashStorage, DueTimer, GFX
- This is the "shared lib station" the user mentioned — field deployment code

**Why:** User plans to package libsdi12 for Debian and other distros (e.g. RPM-based).
**How to apply:** Debian packaging in libsdi12/debian/ is the starting point; verifier will need its own packaging separately. SDI12_PROJECT is application code, not for distro packaging.
