# World War VR one-shot implementation reference

Status: archived implementation record. This document preserves the design,
test evidence, and unresolved questions from an earlier development tranche.
It is not current release authority. Current behavior and release status are
documented in `README.md`, `CHANGELOG.md`, `KNOWN-ISSUES.md`, and
`RELEASE-CHECKLIST.md`.

Baseline: pushed commit `b7792df2f4894dc6120509d12c6778d0d88cfbc8`
(`codex/pre-redirection-guard`). The later Windows RedirectionGuard runtime-
junction change is not an ancestor of this worktree and must not be reintroduced.

The baseline and checkpoints below remain useful engineering history, but no
current build, package, or publication depends on the old worktree or its
preflight artifacts.

## Checkpoint status

The recoverable implementation checkpoints, in commit order, are:

1. `3de5947` - document the one-shot VR improvement scope;
2. `646a0e0` - improve OpenXR frame-pacing diagnostics;
3. `22aa67b` - add verified GitHub launcher updates;
4. `026750a` - improve campaign controls and stereo lighting policy;
5. `a4aa631` - clamp stereo LOD culling FOV.

Historical verification completed at `a4aa631`:

- native Win32 Debug build and all 24 tests passed;
- native Win32 Release build and all 24 tests passed;
- all four supported executable profiles passed real-file validation;
- launcher Core tests passed 27/27;
- the x64 Release WinUI build and clean UI-only launch passed;
- the standalone Release package and payload policy checks passed for the
  `0.4.0-alpha.3` preflight payload.

That payload was an internal preflight, not a public release. Its counts,
hashes, and deferred decisions must not be used to describe a current build;
current artifacts are rebuilt and verified from their matching clean source
tag.

## Historical deferred boundaries

The following items were unresolved at that checkpoint. Later source, tests,
and headset acceptance may supersede them; consult the current release
documents before treating any item as an active blocker.

- **One-eye smoke/particles:** no product fix has been guessed. Normal and
  reverse-order diagnostics prove stable draw-surface and code-mesh data, but
  logs cannot observe headset pixels. A physical-headset test was required to
  determine whether reversing the backend eye order moved the Der Riese
  smokestack smoke to the right eye, left it in the left eye, made it binocular,
  or removed it. A left-eye-only result then required the provided
  `r_zFeather 0` A/B result.
- **CPU capture:** the synchronous D3D9 CPU readback/upload path remains active.
  T4 exposes only a pre-existing `IDirect3DDevice9` and swap chain; this project
  does not own a proven `CreateDeviceEx`/`ResetEx` and shared-resource lifecycle.
  Querying for `IDirect3DDevice9Ex` alone is not sufficient to activate the
  existing shared-texture bridge safely.
- **D3D11 flush batching:** the per-eye flush remains because it precedes release
  of each acquired OpenXR swap-chain image. Changing it safely requires a
  two-eye deferred-release transaction and second-eye failure cleanup.
- **Stereo glow:** the stock full-screen glow gate and predicates are mapped,
  but the routines use shared full-frame targets and expose no proven per-eye
  viewport/scissor/UV contract or seam-safe post-second-eye ownership point.
  Forcing the full-screen flag is unsafe and was not done.
- **Right-stick touch:** the current OpenXR action snapshot does not expose that
  touch state. Campaign selection therefore keeps Touch thumbrest and adds the
  held-Y/left-direction fallback rather than extending the runtime blindly.
- **Other visual limits:** dynamic and authored shadow paths remain deliberately
  disabled until their per-eye list/resource ownership is proven. The four-light
  policy and LOD clamp improve known symptoms but do not claim to solve every
  portal, occlusion, shadow, or lighting artifact.

## Ground rules

- At this historical checkpoint, the rollback and one-eye diagnostic builds
  were preserved while integration work proceeded separately.
- Commit verified subsystems incrementally. Do not wait until the entire batch is
  complete to create the first recoverable checkpoint.
- Preserve the exact supported Steam and legacy executable identities and all
  fail-closed hook sentinels. Never weaken identity or byte validation to make a
  hook install.
- Do not regenerate game FX per eye, advance simulation twice, copy proprietary
  game content, or guess unverified native addresses.
- Do not claim that a diagnostic hypothesis is fixed until the discriminating
  test supports it.
- Do not launch the game or installer during automated verification. Physical
  game/headset acceptance remains a separate, explicit human test.

## Existing work in the baseline

The baseline already contains the larger control and recovery batch. This work
must be retained and regression-tested rather than independently reimplemented:

- body-yaw alignment and automatic native-body rotation;
- left-grip tactical/secondary-grenade mapping instead of VR ADS;
- physical right-hand melee gesture and increased melee range;
- shared peer-thread quiescence for safe hook patching;
- renderer rebind after legitimate D3D device/swap-chain recreation;
- Steam Build 252004 executable profiles and launcher support;
- the SP-only `rocket_barrage` angle substitution hook;
- the dynamic-light path enabled at a conservative one-light limit;
- launch-failure wording and OpenXR/headset diagnostics already present at
  `b7792df`.

## 1. One-eye smoke and particle effects

### Reported behavior

Der Riese smokestack smoke and some other translucent/particle effects are
visible in the left eye but missing in the right eye.

### Evidence already collected

- Both scene eyes are generated and both backend eye calls return.
- The first/left eye is rendered first in the normal path; the final/right eye
  is rendered second.
- Before drawing, the first eye is rebased to the final eye's five validated
  core draw-list descriptors, including emissive lists and validated point-light
  metadata.
- Opt-in diagnostics found no mutation of the validated backend pages, final
  draw-surface spans, code-mesh records, or proven code-mesh descriptor across
  the first eye draw.
- `needsFloatZ=1` and `isRenderingFullScreen=0` are consistently observed.
- The available engine-family renderer shows that code meshes and particle
  clouds read shared FX data; normal dynamic-buffer cursor advancement is not a
  valid reason to restore or replay backend data.

### Required discriminator

The installed diagnostic can render the eyes in reverse order:

- if the missing effect moves from the right eye to the left eye, the defect
  follows first/second draw order and the fix belongs in shared backend replay;
- if the effect remains missing from the right eye, the defect follows packed
  eye state, making FloatZ/z-feather viewport or sampling the leading cause;
- if needed, `r_zFeather 0` is the follow-up A/B. Becoming binocular with
  z-feather disabled confirms the FloatZ/soft-particle path.

### Fix constraints

- Do not run FX update or vertex generation twice.
- Do not permanently reverse eye order.
- Do not restore normal dynamic upload cursors between eyes.
- Patch only the proven order-dependent state or the exact per-eye FloatZ
  viewport/UV/scissor state identified by the discriminator and guarded runtime
  measurements.

### Acceptance

- Smokestack smoke is visible in both eyes.
- Wunderwaffe bolt travel, power-up effects, grenade smoke, blood/impact FX, and
  soft intersections remain stable in both eyes.
- No eye swap, depth disagreement, backend rejection, or crash is introduced.

## 2. Frame pacing, lag spikes, and steady 72 FPS

### Proven causes and non-causes

- A steady 72 FPS can be correct when the active headset is running at 72 Hz.
  `xrWaitFrame` paces the application and exposes `predictedDisplayPeriod`; the
  number alone is not evidence of poor performance.
- The current playable path still performs a synchronous D3D9
  `GetRenderTargetData`, copies the packed frame through CPU memory, and uploads
  it to D3D11 every frame. At 2560x1440 that is about 14.1 MiB per transfer leg
  and roughly 1 GiB/s at 72 Hz, plus a GPU/CPU synchronization point. The
  repository itself labels this CPU path diagnostic rather than a playable
  target.
- The existing D3D9Ex shared-texture bridge is implemented but is not the active
  Present path.
- Repeated OpenXR layer-rejection messages and backend FX-state telemetry can
  open/append/close the log on the render thread thousands of times, creating
  their own spikes.
- The launcher can inherit the game's common 85 FPS limit unless explicitly
  overridden; desktop vblank is unnecessary because OpenXR owns presentation
  cadence.

### Implemented changes and retained boundaries

1. When `xrWaitFrame` returns `shouldRender=false`, submit no composition layer
   for that frame instead of selecting a cached quad that is rejected later.
2. Rate-limit repeated genuine layer-rejection diagnostics.
3. Put backend FX-state telemetry behind an explicit diagnostic environment
   variable; ordinary release play must not perform that render-thread logging.
4. Add opt-in aggregated frame timing that records predicted display period,
   inferred headset refresh, wait time, application work, end-frame time, and
   missed/non-rendering frames at a low reporting frequency.
5. Launch with the exact T4 unlimited FPS value and desktop VSync disabled, with
   launcher tests proving the old 85 cap is not emitted.
6. The active CPU path is now identified once in the log. D3D9Ex activation is
   deferred for the exact lifecycle/ownership reason recorded above.
7. Per-eye D3D11 flushing is retained until acquire/draw/release can be changed
   as one validated two-eye transaction.

### Acceptance

- Automated tests cover `shouldRender=false`, rate limiting, and timing math.
- Logs state the predicted headset refresh so 72 Hz can be distinguished from a
  missed-frame condition.
- No ordinary per-frame backend telemetry or rejection storm remains.
- The runtime reports which capture path is active exactly once.
- Headset play is smooth at Performance and Recommended resolution, with no
  regression in either eye or during D3D recreation.

## 3. Missing glow, lighting, and visual popping

### Proven renderer behavior

- The packed stereo eyes are classified as non-fullscreen views. T4's late
  fullscreen post-effects path therefore skips glow/bloom during ordinary eye
  rendering. This explains power-ups and emissive materials looking flat even
  though their textures/assets are present.
- Forcing `isRenderingFullScreen=1` is unsafe because the post-effect targets and
  UV assumptions cover the full packed render target and can mix or overwrite
  the two eyes.
- The launcher currently caps authored dynamic lights at one even though the
  stereo backend validates and rebases four exact point-light partitions.
- Dynamic/sun/spot shadows are intentionally disabled. Their absence explains
  some flat lighting and light leaks; simply removing those safety overrides is
  not a valid fix.
- T4 currently receives the wide stereo projection for ordinary LOD decisions.
  The related engine's VR implementation clamps only the LOD FOV term to avoid
  premature distant-model culling while retaining the real eye projection.
- Shared final-eye visibility lists can still produce portal/occlusion popping
  at eye-specific boundaries. A conservative head-center culling list is the
  eventual architectural solution, not two independent scene simulations.

### Implemented changes and retained boundaries

1. Raise `r_dlightLimit` from one to four because four exact light partitions
   are already validated/rebased. Retain disabled spotlight shadows.
2. A narrow, reversible LOD-only clamp is installed at the exact validated SP/MP
   instruction sites and scoped only around each stereo scene call. It does not
   change the real projection matrix or scene FOV.
3. Glow remains deferred because no stereo-safe per-eye viewport/scissor/UV or
   after-both-eyes contract was proven. The general fullscreen branch was not
   forced.
4. Keep shadow overrides in place for this batch unless final-view shadow lists
   and resources are independently validated for binocular replay.
5. Keep the remaining portal/occlusion and sun-query work scoped as measured
   follow-up if the concrete reported popping survives the LOD and pacing fixes.

### Acceptance

- Power-up and Wunderwaffe illumination can coexist up to four authored point
  lights without a list-rebase rejection or one-eye mismatch.
- Distant models no longer pop solely because of stereo FOV, while true distance
  LOD transitions remain.
- Any glow implementation is binocular, respects the packed seam, and does not
  overwrite the opposite eye.
- Shadows remain deliberately disabled unless their independent safety gate is
  passed.

## 4. Little Resistance rocket-strike aiming and selection

### Proven game behavior

- Mission script `pel1` grants `rocket_barrage` in action slot 4.
- The targeting script gets player angles, turns them into a forward vector,
  traces 4000 units from the eye, and then traces down to the ground. Firing is a
  separate native attack-button press.
- Normal firearm muzzle/gun-angle hooks cannot steer this marker because the
  script reads player angles directly.
- The baseline contains a narrow SP hook at the sole validated script-vector
  return. It substitutes the right-controller pitch/yaw only for local SP player
  zero while the exact weapon is `rocket_barrage`. The hook installation is
  already proven in the runtime log.
- Current selection uses Touch right-thumbrest plus left-stick-left to emit the
  native action-slot-4 key. That is obscure and unavailable on controller
  profiles without thumbrest touch.
- The menu-input path incorrectly inferred SP from connection state 10, which is
  also used by MP. Exact profile identity must gate mission-only controls.

### Implemented changes and retained boundary

1. Preserve the exact right-controller aim substitution and native trigger fire.
2. Gate mission controls using the validated SP/MP executable profile, not a
   shared connection-state number.
3. Preserve the Touch thumbrest chord and add a controller-profile-compatible
   fallback. A held left-secondary/Y plus direction can act as the mission
   modifier, while a plain tap still changes weapon only on release. A completed
   mission chord must suppress the queued weapon switch.
4. Right-stick touch was not added because that touch state is absent from the
   current action snapshot. The held-Y fallback covers those profiles without a
   conflicting gameplay-button extension.
5. Add one-shot diagnostics for the emitted action-slot key and the first
   accepted/rejected rocket-angle substitution. Do not log every frame.

### Acceptance

- The strike can be selected without a keyboard on Touch and non-Touch profiles.
- A normal weapon-next tap still works.
- Mission chords never leak into MP.
- The marker follows the right controller and the right trigger confirms the
  strike.

## 5. GitHub automatic updater

### Product behavior

- Check the public `jplakon/CallOfDutyWorldAtWar_VR` GitHub releases endpoint
  asynchronously after the launcher is usable. Network failure must never block
  startup or launching the game.
- Compare the installed informational version against valid release tags,
  including the project's alpha/prerelease versions.
- When a newer release exists, show a startup `ContentDialog` with the release
  title and plain-text release notes from GitHub.
- Offer `Update now`, `Later`, and `Don't show this popup for 30 days`.
- Snoozing suppresses only the automatic dialog. A persistent, clickable
  `Update available` notice remains in the main launcher UI.
- The notice/dialog opens the same release details and update action.
- Download only the exact `WorldWarVR-v<version>-Setup.exe` release asset to a fresh local
  temporary path. Require one matching asset, verify size and the GitHub SHA-256
  digest, then launch the installer and close the launcher.
- Never execute an unverified partial download. Never replace installed files
  directly from the running launcher.
- Persist the 30-day snooze timestamp in the existing unpackaged settings store
  without breaking version-1 settings migration.

### Security and UX constraints

- Keep the fixed owner/repository and fixed asset name in code; do not accept a
  release URL or executable name from untrusted UI input.
- Parse release notes as text, not HTML/Markdown capable of executing content.
- Use cancellation/timeouts and keep all network/hash work off the UI thread.
- The launch button and existing launcher validation remain independent of the
  updater.
- Preserve the fixed responsive shell, high-contrast behavior, keyboard access,
  and disabled-by-default launch safety contracts.

### Acceptance

- Unit tests cover semantic-version ordering, stable/prerelease comparisons,
  malformed tags, snooze expiry, exact-asset selection, digest mismatch, and
  settings round-trip/migration.
- Offline, rate-limited, malformed, missing-asset, wrong-size, and wrong-digest
  cases leave the launcher fully usable and execute nothing.
- A clean unpackaged publish launches into a responsive `World War VR` window;
  startup alone cannot launch the game or installer.

## 6. Commit and verification sequence

The historical checkpoint sequence is recorded with hashes in **Checkpoint
status** above. At that time, binocular FX remained evidence-gated, stereo LOD
was proven and committed independently, and stereo-safe glow was not.

Each checkpoint was required to pass its focused tests before commit. The
historical validation recipe included:

- the full native Win32 Debug test suite;
- the full native Win32 Release test suite where practical;
- the WinUI Core tests;
- the x64 Release WinUI build with zero errors;
- a clean standalone package build without `-SkipTests`;
- installer payload/manifest/forbidden-file validation;
- clean WinUI-only launch verification with no native helper or game available;
- final source and payload scans for local paths, game assets, bot archives,
  debug files, stale public names, or unexpected binaries.

## 7. Historical consolidated headset acceptance pass

The planned checkpoint acceptance pass covered:

1. Launcher startup online and offline; update dialog, notes, Later, 30-day
   snooze, persistent update notice, and no unexpected installer execution.
2. Main Menu/Zombies launch and SteamVR/Meta OpenXR launch as available.
3. Headset refresh and active capture path recorded once in the log.
4. Recommended and Performance resolution frame pacing through map load,
   teleport/menu transitions, combat, and a D3D recreation path.
5. Der Riese smokestacks, power-ups, Wunderwaffe travel/self-zap, grenade smoke,
   and other translucent effects in both eyes.
6. Multiple simultaneous authored dynamic lights without rejection or popping.
7. Distant-model LOD behavior and the previously reported visual-artifact areas.
8. Little Resistance action-slot selection, right-controller marker aiming, and
   trigger confirmation.
9. Regression controls: body-aligned sprint/use, secondary grenade, physical
   melee, menus, weapon switching, Zombies HUD, and MP HUD.
10. Logs copied after the run so any remaining issue is tied to the exact
    version, runtime, refresh period, capture path, and renderer diagnostic gate.

## Historical definition of done

This tranche required every safely implementable item to have a checkpoint,
all automated gates to pass, a local installer to be produced, and every
deferred item to have a precise evidence blocker and next discriminator. That
tranche is superseded by the current community implementation. Public release
completion is now defined by `RELEASE-CHECKLIST.md` and the exact tagged-source
workflow, not by this archived record.
