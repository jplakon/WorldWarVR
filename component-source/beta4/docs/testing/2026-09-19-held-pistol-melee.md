# Held-pistol melee candidate, 2026-09-19

## Status

Built, simulator-validated, and confirmed working by the user in the physical
headset on 2026-09-19 ("it works. done"). The original failure was that free-hand
stabbing worked but stabbing while retaining the pistol's right grip did not.
This change adds that exact pistol-only gesture without changing weapon aiming,
grip transforms, reload mechanics, or rendering.

The simulator was closed before the fresh-ready physical test. CoDWaW PID 14868
finished exiting after the user's completion reply without intervention; the
follow-up check found no game or launcher process. Require a fresh ready reply
before any subsequent physical launch.

## Candidate

- Launcher: `C:\Users\jplak\Downloads\test\wawvr-beta2-release-build\native\vs-release\launcher\Release\WorldWarVR.exe`
- Adjacent staged DLL SHA256: `6DEBE076E2865E2EFC58875C02EA7D6D1862016FD8FDED3F0C98F2EF8BAA8E2E`.
- Existing source edits and campaign profiles were preserved. No release ZIP
  was rebuilt or published.

## Behavior

An exact known pistol held by the right grip with the left grip released can
issue native melee with a fast outward jab along the right-controller aim ray.
The right trigger must be released. Native idle, identity, reload/grenade hand
ownership, tracking validity, travel, speed, direction, and cooldown guards
remain active. MP has no validated pistol identity reader and fails closed.

The new pistol path uses bounded recent motion samples. This avoids missing
jabs across an arbitrary 150 ms idle-window boundary. Distinct VR frames can
share a GetTickCount64 timestamp; equal timestamps preserve history, while
backwards time and stale input clear it. Existing free-hand and two-hand rifle
gesture logic is unchanged.

## Validation

- Release build succeeded; all 69 CTest checks passed.
- Added 750 positive idle-phase/rate cases, 300 raising/sweeping negatives,
  90/120 Hz quantized-clock cases, rotated aiming, slow motion, duplicate
  suppression, history wrap, and recovery checks.
- OpenXR Simulator runtime and loaded candidate module were verified.
- `OpenXR-Simulator-wawvr/out/wawvr-melee-stress-artifacts/20260919-155624`:
  seven held-pistol strikes plus one free-hand swing; all phases passed.
- `.../20260919-155705`: 1,800 jitter samples, eight forward pistol jabs and
  one rotated jab. No negative-phase attacks. The final buffered receipt was
  recovered by its input timestamp; see that directory's
  `log-reconciliation.md`. The initial collection failure was preserved.
- `.../20260919-160007`: repeat using corrected timestamp/flush collection;
  all phases passed (three held-pistol strikes and one free-hand swing).
- `.../20260919-160127`: rifle regression passed (one free-hand swing and one
  two-hand thrust; no held-pistol or unexpected native melee).

Simulator command consumption is not a constant-duration frame step. The test
records command timings, uses sampled forward motion, and attributes buffered
log receipts to their monotonic `sampleMs`, not file-arrival time.

Right-stick click is not exposed by this simulator profile and is not claimed
as validated. No physical headset visual, comfort, or damage acceptance is
claimed by these automated tests.

## Physical headset acceptance

The physical test launched the recorded candidate with `--launch --der-riese`
using a process-scoped VirtualDesktopXR 32-bit runtime override. The loaded
candidate module was verified and no simulator module was loaded. The log
reported VirtualDesktopXR 1.0.10, gameplay stereo, and a held-pistol strike at
16:21:14 (action sequence 318, sampleMs 88504906).

The user confirmed "it works. done" after being asked to test the held-right-
grip stab and regripping. This is acceptance of the reported pistol-stab fix,
not a separate detailed sign-off for every weapon, runtime, damage case, or
the original customer random-melee report. Customer retesting remains needed
before closing that broader report. Right-stick-click headset coverage was
not explicitly reported in this exchange.
