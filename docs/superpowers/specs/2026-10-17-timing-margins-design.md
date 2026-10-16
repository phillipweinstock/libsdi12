# Master timing margins + verifier wake probe — design

Date: 2026-10-17
Repos: `libsdi12` (this repo) and `libsdi12-verifier` (`../libsdi12-verifier`), shipped in lockstep.

## Why

Real buses don't always meet SDI-12 at the spec minimums. Slow sensors and cheap
transceivers miss the wakeup or answer late when the recorder uses exactly
12 ms break / 8.33 ms marking / 15 ms reply window. Field evidence: the
microSDI12 maintainer stretched BREAK to 2.0x and MARK to 1.5x a character
time (commit `bf85449`, 2026-07-22) to get real sensors answering.

libsdi12 today gives a recorder no way to add slack: post-break marking is a
fixed `SDI12_MARKING_MS` (9) at `sdi12_master.c:129`, and every library-internal
reply wait is a fixed `SDI12_RESPONSE_TIMEOUT_MS` (15). Overriding those macros is
not an option: they are spec constants shared with the sensor side (sensor
examples use `SDI12_MARKING_MS` for pre-reply marking, and a sensor must start
replying within 15 ms), so bumping them silently pushes sensor builds out of spec.

The verifier should be able to say *how far* out of spec a sensor's wake-up is,
rather than just "no response".

> **Amendment (2026-10-17, spec oracle review):** both first-byte deadlines
> carry +1 ms so ports timing with a 1 ms tick (`millis() - t < wait`, AVR's
> 1.024 ms tick) don't cut a compliant first byte at 23.73 ms: reply deadline
> **25 ms**, multi-line **160 ms**; reply margin limit therefore **62**.
> Compile-time margin defaults are checked against 87 ms with `#error`.
> Example break callbacks no longer add their own marking.

## Spec facts this design rests on (SDI-12 v1.4, 2023-02-20)

Checked against the PDF (`~/Downloads/SDI-12 Specification 1.4 February 20 2023.pdf`):

- §7.0: all timing tolerance ±0.40 ms (except the 1.66 ms inter-character
  maximum, no tolerance).
- §7.0: recorder break = spacing ≥ 12 ms; a sensor never treats < 6.5 ms as a
  break and always recognises > 12 ms. No upper bound on break length.
- §7.0: after a break, a sensor must see 8.33 ms of marking before it looks for
  an address.
- §7.0: a sensor must wake and be able to detect a command start bit **within
  100 ms after detecting a break**. §7.2: sensors "will not respond to any
  commands until they have awakened"; the recorder handles this with retries,
  at least one retry more than 100 ms after the falling edge of the break.
  → A sensor that misses a command sent at minimum marking is **not**
  non-compliant if it wakes within 100 ms.
- §7.0: the sensor marks 8.33 ms (−0.40) then replies; the **start bit** of the
  first response byte must begin within 15 ms (+0.40) of the command's last
  stop bit. A UART reports a byte only after its full 10-bit frame (8.33 ms at
  1200 baud 7E1), so the first byte is observable up to
  15.40 + 8.33 = 23.73 ms after the command.
- §7.0/§7.1: more than 87 ms of marking → the next command needs a break.
  §7.2: a retry goes out no sooner than 16.67 ms and no later than 87 ms after
  the command's last stop bit.

## Goals

- Fix the existing first-byte deadline so libsdi12 recorders can read
  compliant sensors on real UART hardware (Part 1).
- A recorder built on libsdi12 can add extra time on top of the spec values for
  (a) post-break marking and (b) the wait for a reply's first byte (Part 2).
- Per bus (per master context), changeable at runtime, so the caller's own
  retry loop can escalate the margin on later attempts.
- Zero margin = exactly the (fixed) default behaviour.
- The verifier automatically finds the smallest margin a sensor needs to
  answer, judges it against the spec (wake ≤ 100 ms is compliant; a late reply
  start is not), and still runs the rest of the suite.

## Non-goals

- Lengthening the break inside the library. Break duration belongs to the
  port's `send_break` callback (it is not told a duration, and changing its
  signature breaks every port). Documented instead (see README note).
- Built-in retries. §7.2 retry stays the caller's job (`README.md:64`).
- Sensor-side leniency (e.g. accepting short breaks).
- A fixed `--margin` CLI flag in the verifier (considered; auto-probe chosen).
- Changing any spec constant in `sdi12.h`.

## Anti-goals (failure even if it "works")

- Any configuration that makes a libsdi12 recorder go *below* spec timing.
- Margin leaking into a timeout the caller passes explicitly to
  `sdi12_master_transact()`. An explicit value is the caller's decision and is
  used exactly; callers who want the margin there add it themselves (the
  verifier does, see below).
- Margin leaking into the verifier's pass/fail limits (`spec_limit_us`).
- `sizeof(sdi12_master_ctx_t)` or any existing field offset changing on 32- or
  64-bit targets (shared library `libsdi12-0` ships via `debian/`).
- Any behaviour change at default settings in Part 2. (Part 1 changes the
  default first-byte wait deliberately: it is a bug fix.)

## Part 1: bug fixes

Same release as Part 2 (0.6.0), but as their own commits ahead of the margin
commits, so a 0.5.5 can still be cut from that point if Part 2 stalls.

### 1a. First-byte deadline (libsdi12)

Today every library-internal reply wait passes `SDI12_RESPONSE_TIMEOUT_MS`
(15) as the recv first-byte deadline. Per the spec facts above, a compliant
sensor's first byte is observable only 16.26–23.73 ms after the command, so on
real UART hardware every compliant reply times out. (The loopback self-test
hides this: its delays don't wait.)

- New constant in `sdi12.h`, alongside the other timing constants:
  first-byte deadline = 24 ms (15 ms + 0.40 tolerance + one 8.33 ms character,
  rounded up). Name left to the builder; comment cites §7.0.
- The **11 first-byte waits** in `sdi12_master.c` use the new constant: the 10
  `sdi12_master_transact(..., SDI12_RESPONSE_TIMEOUT_MS)` sites (including the
  change-address wait `SDI12_ADDRESS_CHANGE_DELAY_MS + ...`; §4.4.4: normal
  reply, then up to 1 s busy) and the high-volume binary header read
  (`sdi12_master.c:876`, the first read after `send_command`).
- The other 3 high-volume binary reads (type byte, payload+CRC, oversize
  drain) are continuation reads with bytes already flowing: the next byte is
  due within one character + the 1.66 ms gap (≈ 10 ms, §7.0), so they keep
  `SDI12_RESPONSE_TIMEOUT_MS` (15) and get no margin.
- `SDI12_RESPONSE_TIMEOUT_MS` itself stays 15: it is the spec's start-bit
  window, used by sensor-side docs and by the verifier's compliance limits.
- Update the recv contract comment at `sdi12_master.h:48` ("The library passes
  the spec's 15 ms response window here") to describe the new value.
- Explicit `sdi12_master_transact(ctx, cmd, T)` still uses `T` exactly.
- Tests: internal transactions pass 24; explicit timeouts unchanged.

### 1b. Example master recv callbacks (libsdi12)

`EasyMaster.ino:23`, `BareMaster.ino:64`, `InterruptMaster.ino:69`,
`interrupt_master.c:83` treat `timeout_ms` as a whole-read deadline, breaking
the recv contract (`sdi12_master.h:48-66`): with any spec-sized timeout they
can't read even `0\r\n`. Rewrite each to the contract: wait up to `timeout_ms`
for the first byte, then keep reading until LF or a full buffer with an
inter-character idle timeout. Keep them short and in each file's existing
style. The stub ports (`easy_master.c`, `example_master.c`) get a TODO comment
stating the same contract.

### 1c. Verifier

- `cb_break` (`src/timing.c:99-113`): keep the 15 ms break (compliant, headroom
  for USB-serial break jitter); **remove its extra 9 ms marking** —
  `sdi12_master_send_break()` already provides post-break marking, so today the
  wire carries ~18 ms.
- Explicit waits at `sensor_tests.c:1187, 1273, 1296, 1592` that pass
  `SDI12_RESPONSE_TIMEOUT_MS` as a recv deadline switch to the new first-byte
  constant. Compliance limits (`spec_limit_us`) keep using
  `SDI12_RESPONSE_TIMEOUT_MS`.
- Ships with Part 2b against the `v0.6.0` pin.

## Part 2a: margins — libsdi12 0.6.0

### Data

Two `uint8_t` fields in `sdi12_master_ctx_t` (`sdi12_master.h:103`), placed
**immediately after `resp_buf`, before `resp_len`**:

```c
uint8_t  marking_margin_ms;   /* added to SDI12_MARKING_MS */
uint8_t  response_margin_ms;  /* added to the Part 1 first-byte deadline */
```

Measured on current defaults: `resp_buf` ends at offset 158 (64-bit) / 134
(32-bit) and `resp_len` starts at 160 / 136, so the two bytes fill existing
padding. `sizeof` stays 168 / 140. AVR (no alignment padding) grows by 2 bytes;
acceptable since AVR is always built from source.

### Compile-time defaults

In `sdi12_master.h`:

```c
#ifndef SDI12_MASTER_MARKING_MARGIN_MS
#define SDI12_MASTER_MARKING_MARGIN_MS   0
#endif
#ifndef SDI12_MASTER_RESPONSE_MARGIN_MS
#define SDI12_MASTER_RESPONSE_MARGIN_MS  0
#endif
```

`sdi12_master_init()` loads both fields from these. Note: they are read when
`sdi12_master.c` is compiled, so they only take effect for source builds
(PlatformIO `build_flags`, CMake). They do nothing for apps linking the Debian
shared library, and the Arduino IDE has no project-wide defines. The runtime
setter is therefore the primary interface; the macros are a convenience.

### Runtime setter

```c
sdi12_err_t sdi12_master_set_timing_margin(sdi12_master_ctx_t *ctx,
                                           uint8_t marking_ms,
                                           uint8_t response_ms);
```

- NULL `ctx` → error, consistent with the other master functions.
- Rejects the call (returns an error, **leaves both previous values unchanged**)
  if `SDI12_MARKING_MS + marking_ms > SDI12_MARKING_TIMEOUT_MS` (87, so
  marking margin ≤ 78) or `<first-byte deadline> + response_ms > 87` (so reply
  margin ≤ 63).
  Rationale: past 87 ms of marking, sensors need a fresh break (§7.0/§7.2).
  The verifier's simulated sensor documents the same rule
  (`recorder_tests.c:290`, a comment; the simulator does not enforce it).
- Use an existing error code; do not add a new one.

Easy API (`sdi12_easy.h`): `SDI12_MASTER_MARGIN(name, mark, resp)` →
`sdi12_master_set_timing_margin(&name##_ctx, (mark), (resp))`.

### Where margins apply

- `sdi12_master_send_break()`: post-break delay becomes
  `SDI12_MARKING_MS + ctx->marking_margin_ms`.
- Every library-internal first-byte wait in `sdi12_master.c` (the 11 sites
  Part 1 moved to the new first-byte constant) adds `ctx->response_margin_ms`.
  Continuation reads get no margin.
- **Not** applied to the `timeout_ms` argument a caller passes to the public
  `sdi12_master_transact()`; that value is used exactly as given.
- `recv` callback semantics are unchanged: `timeout_ms` is the first-byte
  deadline (`sdi12_master.h:48`), so the response margin is exactly "more time
  before the sensor starts answering".

### Untouched

`sdi12.h` spec constants, the sensor side, the `send_break` callback signature,
retries.

### Docs

- README: a short "slow sensors / cheap transceivers" note:
  - the setter is the primary interface; macros only for source builds (say
    which build systems, as above);
  - break length is the port's: in `send_break`, switch the direction pin
    *first*, then hold `SDI12_BREAK_MS` plus your own margin — a late direction
    switch truncates the break the sensor sees;
  - escalating the margin from the caller's retry loop is the intended pattern;
  - the marking margin is a shortcut, not compliance: §7.0 lets a sensor take
    up to 100 ms to wake, and the spec's answer is §7.2 retries (at least one
    retry more than 100 ms after the break). A margin just lets the first
    attempt land. The reply margin, by contrast, accommodates sensors that
    start replying later than §7.0 allows.
- `examples/example_master.c:48`: replace the bare `12` with `SDI12_BREAK_MS`.
- README / TESTING.md / `library.json` / `library.properties` test counts.
- `debian/libsdi12-0.symbols`: add `sdi12_master_set_timing_margin` at 0.6.0.
  No soname bump (symbol addition only, layout unchanged).

### Tests (`test/test_master.c`)

- Defaults: marking delay recorded by the mock is 9; internal transactions use
  the first-byte deadline (24).
- With margins set: 9 + m and 24 + r at the internal sites (cover at least
  acknowledge, a measurement, change-address, and HV binary).
- Explicit `sdi12_master_transact(ctx, cmd, T)` uses exactly `T` regardless of
  margins.
- Setter rejects each over-limit value, accepts the exact limit, and leaves the
  previous values in place on rejection; NULL ctx.
- `SDI12_MASTER_MARGIN` easy macro sets both fields.
- Struct size/offsets unchanged on the host build.

### Version

One release for Parts 1 and 2a: 0.5.4 → **0.6.0**, following the normal release process (version in
`CMakeLists.txt`, `sdi12.h`, `library.json`, `library.properties`, debian
changelog, annotated tag `v0.6.0`).

## Part 2b: wake probe — verifier, pinned to 0.6.0

### Prerequisite

The verifier has uncommitted work in progress (`src/sensor_tests.c`
`response_too_early()` stub with a TODO, plus README timing wording). Phillip
finishes and commits that separately before this work starts.

### New sensor test: wake-up probe

- Runs as a **pre-suite step**, before the sensor-identity pre-scan
  (`main.c:251-258`), and always runs, even when `--test <name>` filters the
  suite (`test_suite.c:64`), because the pre-scan and every selected test must
  run with the margin it finds. Its result is still reported as a sensor test
  (counted, so 31 → 32). Spec reference §7.0.
- Procedure: break + `a!` at margins (0, 0). If the sensor answers → PASS
  ("wakes at spec timing").
- Otherwise probe: step the marking margin up (response margin 0); if nothing
  works, step the response margin up (marking margin 0); if still nothing,
  step both together. Stop at the setter's limits.
- Flakiness guard: a margin counts only when it succeeds **3 times in a row**,
  and spec timing counts as failed only after **3 misses** — so one dropped reply
  never produces a false FAIL on a compliant sensor.
- Verdicts follow the spec, not "any margin = fail":
  - Needed only a **marking** margin → **PASS** with a note, e.g. "missed the
    command at minimum marking; answered with +6 ms (wakes within §7.0's
    100 ms)". Slow-but-compliant wake.
  - Needed a **reply** margin → **FAIL**, e.g. "reply starts ~N ms late;
    §7.0 allows 15 ms (+0.40)". Non-compliant.
  - Nothing works up to the setter's limits → before failing, apply the §7.2
    check the margins can't reach: break, then a retry more than 100 ms after
    the break's falling edge. Answers → PASS with a "wakes in 87–100 ms" note.
    Such a sensor will miss every later break-then-command at the maximum
    margin too, so later tests need that same late-retry pattern; how the
    verifier carries it through the suite is the builder's call. Still
    nothing → FAIL "no answer
    within §7.0's 100 ms wake window, or no sensor at this address"; the rest
    of the suite runs at (0, 0) and fails on its own.
- Whatever margin was found is applied to `timing_master(ctx)` via
  `sdi12_master_set_timing_margin()` for the rest of the run.

### Report

- When the run used non-zero margins, the text report header and the JSON
  output both state them (e.g. "lenient run: +6 ms marking, +0 ms reply"), so
  every other result is read knowing it was obtained with leniency.
- Timing tests keep judging against the spec constants; the margin never
  changes a pass/fail limit.
- The verifier's own explicit waits (`sdi12_master_transact(...,
  SDI12_RESPONSE_TIMEOUT_MS)` at `sensor_tests.c:1187, 1273, 1296, 1592`) are
  plain waits, not compliance limits: the verifier adds the found response
  margin there itself, so a slow-replying sensor isn't reported as "no
  response" by tests that check something else.

### Self-test

- Plain loopback: libsdi12's own sensor wakes at spec, so the probe PASSes at
  (0, 0). Self-test baseline pass count goes up by one; re-baseline and update
  the documented baseline.
- New loopback fault (`lb_fault_t`, alongside the existing ones in
  `include/hal_loopback.h`): a simulated slow-waking sensor. The loopback's
  `delay_ms` is a ~10 µs no-op (`hal_loopback.c:442`), so wall-clock wake
  latency can't be modelled; instead the loopback records the *requested*
  post-break delay and the simulated sensor ignores the next command when it
  was below N ms. A fault self-test asserts the probe PASSes with a note and
  a marking margin that covers N (slow but compliant wake). If that can't be
  done cleanly, drop this one test rather than fake it.
- Second new loopback fault: a late-replying sensor whose response is held
  back by wall-clock time past the first-byte deadline (like the existing
  `LB_FAULT_PACED_UART`). A fault self-test asserts the probe FAILs and
  reports a reply margin that covers the hold-back.

### Docs and versioning

- README: sensor test table and totals (31 → 32 sensor tests, 47 → 48 total),
  plus a sentence on what a lenient run means in the report.
- Pin `lib/libsdi12` to the **`v0.6.0` release tag** (not a branch SHA —
  squash merges drop branch commits). Verifier minor version bump.

## Lockstep sequence

0. Phillip commits the verifier's in-progress `response_too_early()` work
   (blocks verifier steps only).
1. libsdi12: Part 1a/1b fix commits, then Part 2a margin commits; release
   `v0.6.0`, push (tag must be fetchable).
2. Verifier: pin submodule to `v0.6.0`; Part 1c, then Part 2b probe + report +
   self-test; re-baseline, release.

During development the verifier may point its submodule at the libsdi12 work
branch, but the committed pin is always the release tag.

## Decisions and who made them

Phillip:
- A leniency mode exists, for slow sensors and cheap transceivers.
- Margins are `uint8_t` (nothing needs more than 255 ms).
- Retries stay the caller's job.
- Margin-on-top-of-spec model with a runtime setter and macro defaults
  (proposed by Claude, approved).
- The verifier changes in lockstep, using an **auto-probe** (not a fixed flag).
- First-byte deadline fixed **in the library** (not by changing the recv
  contract).
- One release, **0.6.0**, for fixes and margins (few users; fixes stay as
  separate, earlier commits).
- No superfluous comments; trim verbose ones in code being touched.
- Verifier break: **keep 15 ms, drop the extra 9 ms marking** — confirmed after
  reviewing the spec.

Claude's calls (open to change):
- Probe verdicts per spec: marking margin = PASS with note (§7.0 100 ms wake),
  reply margin = FAIL, plus a §7.2 >100 ms retry before declaring no answer.
  (Changed from "any margin = FAIL" after the spec review.)
- First-byte deadline value 24 ms (15 + 0.40 + 8.33, rounded up).
- Continuation reads in the binary path keep 15 ms and get no margin.
- Setter limit covers the library's own delays only; port overhead (direction
  switch, flush) lands on top, so at the exact limit a port can exceed 87 ms
  by its overhead. Documented in one line, not guarded.
- Two separate margins (marking, reply wait) rather than one.
- 87 ms limit, inclusive, applied to the default value + margin.
- Field placement in existing padding.
- Names: `SDI12_MASTER_MARKING_MARGIN_MS`, `SDI12_MASTER_RESPONSE_MARGIN_MS`,
  `sdi12_master_set_timing_margin`, `SDI12_MASTER_MARGIN`.
- Probe order (marking, then reply, then both) and the 3-in-a-row rule.
- Probe registered first; lenient-run note in report header and JSON.
- Slow-wake loopback fault for self-testing the probe, keyed on the requested
  delay rather than wall-clock time.
- Probe as an unfilterable pre-suite step that is still reported as a test.
- Verifier adds the found margin at its own explicit transact waits.

## Left to the builder

- Which existing error codes the setter returns.
- Probe step size and maximum probe duration.
- How the struct-size/offset check is expressed (static assert vs runtime test).
- Exact report wording and JSON field names.
- Test names and how the mocks record delays/timeouts.
- If the loopback HAL cannot model wake latency for the slow-wake fault, say
  so and drop that one self-test rather than faking it.
