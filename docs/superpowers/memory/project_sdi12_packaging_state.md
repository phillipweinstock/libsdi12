---
name: sdi-12-packaging-distribution-current-state-and-next-steps
description: PlatformIO + Arduino live at 0.3.0; Debian stuck on sponsor (RFS list mail ignored); v0.4.0/v0.7.0 tagged locally awaiting push + re-upload
metadata: 
  node_type: memory
  type: project
  originSessionId: 7916c978-6c4b-47fc-b04b-4d06ceec19f5
---

## Distribution state (verified 2026-07-06)

- **PlatformIO**: LIVE — registry.platformio.org/libraries/phillipweinstock/libsdi12, latest 0.3.0 (Feb 2026). New version: `pio pkg publish` from repo root after bump.
- **Arduino Library Manager**: LIVE — indexed 0.1.0–0.3.0, registry PR #7785 merged. New versions auto-indexed from GitHub tags (library.properties version must match tag).
- **Debian**: stuck. 0.3.0-1/0.6.0-1 accepted on mentors.debian.net 2026-03-16 (ITPs #1130920/#1130921). RFS emailed to debian-mentors list 2026-03-25 — **zero replies**. Old mentors uploads contain the pagination data-loss bug — must be superseded before any sponsor touches them.
- **⏰ DEADLINE: mentors uploads expire ~2026-08-03** (20-week no-upload removal policy; uploaded 2026-03-16; both confirmed still live 2026-07-06). Any new dput resets the clock — the 0.4.0-1/0.7.0-1 re-upload must happen **before early August**. ITP bugs are on a slower clock (wnpp nag ~6–12 months), no risk in 2026.

## Releases staged locally 2026-07-06 (NOT pushed)

- libsdi12 **v0.4.0** tagged (commit b8b87e1): all bug fixes, versions aligned across CMakeLists/header/library.json/library.properties, debian/changelog collapsed to single 0.4.0-1 entry (Closes: #1130920).
- libsdi12-verifier **v0.7.0** tagged: VERIFIER_VERSION bumped, submodule pinned to v0.4.0, changelog single 0.7.0-1 entry (Closes: #1130921).
- All history rewritten to strip Co-Authored-By per [[feedback-no-coauthor]] — **push will need --force-with-lease if remotes have the March packaging commits** (check remote state before pushing).

## Sponsor plan

1. Gmail draft ready (r-5814580071575275444): direct sponsorship ask to Yifei Zhan (yifei@zhan.science / yifei@debian.org) — warm contact, signed Phillip's key Feb 2026, thread "Toy packaging test". Phillip never directly asked him to sponsor before.
2. Parallel: file RFS bug against `sponsorship-requests` pseudo-package (the tracked queue DDs triage) — plain list emails demonstrably evaporate.
3. mentors uploads may expire after ~20 weeks inactivity — re-upload 0.4.0-1/0.7.0-1 refreshes them.

## Remaining manual steps (need Phillip's credentials)

1. Push both repos + tags (`git push --follow-tags`, likely `--force-with-lease` due to history rewrite).
2. WSL: rebuild source packages from v0.4.0/v0.7.0 tags, sign (`~/sign-packages.sh`, key 48AD7C53246F05A2A7BB62C49E5982D551F703C3, USB D:), `dput mentors`.
3. Send the Yifei draft; file RFS bug.
4. `pio pkg publish` (needs `pio account login`).
5. Arduino: nothing — tag push triggers indexing (verify library.properties 0.4.0 == tag v0.4.0 ✓).
