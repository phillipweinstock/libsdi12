---
name: project-sdi12-review-findings
description: "Code review of libsdi12 + libsdi12-verifier — all findings FIXED 2026-07-06 (commits ebdd4c4/a36fd92 lib, 61de762 verifier)"
metadata: 
  node_type: memory
  type: project
  originSessionId: 7916c978-6c4b-47fc-b04b-4d06ceec19f5
---

Review + fixes 2026-07-06 for [[project-sdi12]] repos. All fixed and committed locally (not yet pushed):

- libsdi12 `ebdd4c4`: D-page pagination data loss (worst bug — any >1-page measurement returned empty), master CRC never actually verified (`crc_valid` now set, `SDI12_ERR_CRC_MISMATCH` on corrupt), wrong-address responses rejected, aDBn! binary packet overflow clamped, overlong value OOB read clamped, break no longer aborts concurrent (§4.4.7), aIMC!/aICC! CRC variants, measurement_done NULL guard. Tests 98 → 114, TDD red/green.
- libsdi12 `a36fd92`: test/CMakeLists.txt committed (was untracked, broke fresh clones with tests ON), Debian build artifacts gitignored.
- libsdi12-verifier `61de762`: submodule bumped to a36fd92, loopback sim now 9 values (self-test exercises pagination — caught the bug as RED before bump), snprintf-accumulate OOB fixed via `append_str` helper, JSON version single-sourced (`VERIFIER_VERSION` in verifier.h), hal_posix honors serial cfg, micros() overflow divide-first, transparent appends missing '!', --self-test mode combos rejected.

Spec notes (SDI-12 v1.4 Feb 2023, PDF in user's Downloads): §4.4.7.1 — ANY addressed command (incl. D) aborts an in-progress concurrent measurement; that is the intended abort mechanism, do NOT "fix" it. §4.4.5.1 — break aborts standard M. Breaks do NOT abort concurrent. All locked in unit tests.

Verified: 114/114 unit tests (Windows MinGW + Linux/WSL), verifier self-test 27 pass/0 fail both platforms, JSON report parses. Follow-up `b04943f`: SDI12_MAX_RESPONSE_LEN now #ifndef-overridable (-D at build; 1006 = spec-max binary packet; #error below 82; payload clamped to 1000 spec cap; changes ctx struct size so lib+app must match). Spec Table 14/18: small per-packet payloads are compliant — sensor's choice, recorder paginates until empty packet. Remaining known-but-unfixed (deliberate): parse_meas_response CONTINUOUS case (useful alias for aIRn! identify), ~2KB stack in master get_hv_binary_data.
