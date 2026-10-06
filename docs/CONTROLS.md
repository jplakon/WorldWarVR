# World War VR controls and recovery notes

## Gameplay

- Headset: 6DOF view and position.
- Right controller pose: weapon aim.
- Right trigger: fire while the right hand owns the firearm. When the opposite
  hand retains a supported weapon, the free right index trigger can instead
  operate its bolt or charging handle.
- Mounted machine gun: move the live right-controller aim to traverse the gun
  within its native mechanical limits, and use the right trigger to fire. The
  headset remains independent for looking around. Fixed scoped turrets retain
  their native controls.
- Left trigger: when the right hand retains a supported weapon, the free left
  index trigger can operate its bolt or charging handle. Hip reach continues
  to select the contextual grenade interaction described below.
- Hip-fired local VR bullets use the current weapon's fixed ADS-level accuracy;
  the native expanding desktop hip-fire cone is not applied.
- Left index trigger at the left hip: grab and hold/cook a frag grenade;
  release to throw or gently drop it. Use the same physical left index trigger
  at the right hip for the secondary tactical grenade. The firearm remains in
  the other hand while the grenade follows the tracked left hand.
- Left stick: move relative to the headset's horizontal heading. Physical HMD
  turning automatically brings the hidden native player body into the same
  heading, so sprint and interaction traces do not retain an invisible old
  facing direction.
- Left stick click: start sprint. One normal click stays latched while the
  movement stick remains outside its deadzone; returning the stick to neutral
  clears sprint.
- Right stick left/right: horizontal turning. Snap turning is the default and
  turns 45 degrees once per deflection; the launcher's optional Smooth Turning
  mode provides proportional continuous yaw at up to 120 degrees per second.
- Right stick down: one stance step per deflection—standing to crouched, then
  crouched to prone. Return the stick to centre between steps.
- Right stick up: one stance step per deflection—prone to crouched, then
  crouched to standing. When already standing, the same upward gesture jumps.
  Holding either vertical direction never repeats an action.
- X: use/interact, including doors, wall weapons, the mystery box, and revive.
- A: begin the native/manual weapon reload.
- B: crouch. On SteamVR controller paths where the left Menu action is
  unavailable because the runtime reserves it for the system dashboard, tap B
  to pause/resume instead, or hold B for one second to recenter. Right-stick
  down remains the stance control on that fallback path.
- Y/left-secondary: a plain tap switches to the next weapon when released.
  While the M1 Garand with the M7 rifle-grenade attachment is active, the same
  tap toggles directly between the rifle and its grenade launcher; tap Y again
  to return to the rifle.
- Campaign support selector: hold Y/left-secondary, then push the left stick
  left once to send WaW's native `6` key. This equips the rocket-barrage
  designator in Little Resistance or the airstrike radio in Shuri Castle.
  Point with the right controller and use the normal right trigger. Shuri
  Castle still restricts valid strike areas through its mission logic.
  Locomotion remains active while the chord is held;
  completing it consumes Y's release so it cannot also switch weapons. The
  same hidden mission D-pad provides up=`5`, down=`N`, and right=`7` for
  matching native campaign prompts; the path cannot run in multiplayer.
  Passive Touch thumbrest contact is never treated as a modifier.
- Right stick click, a fast outward free-right-hand swing, a forward pistol
  jab while holding only the right grip, or a deliberate forward rifle thrust
  while both grips are held: melee. A held-pistol jab follows the right
  controller's pointing direction; release the firing trigger first. It is
  suppressed during reload/weapon-switch interactions. The native weapon
  behavior chooses a bayonet thrust for a bayonet-equipped rifle or the
  firearm's ordinary melee attack otherwise. The action and damage remain
  native, its comfortable trace range is extended to 96 game units, and
  target-assisted camera rotation/lunge is suppressed for VR comfort. Normal
  two-hand aiming, firing, and grip handoffs are not melee gestures.
- Right grip: draw and hold the firearm. Left grip adds the support hand and
  the native firing/ADS pose; either hand can retain a supported weapon while
  the other hand uses that free hand's index trigger for its manual weapon
  interaction.
- Left Menu short tap: Escape/open or close the native menu.
- Left Menu held for one second: recenter position and facing direction while
  always restoring a gravity-level horizon.

### Pistols and manual magazine reloads

- Hold the pistol with right grip, bring the left hand beside the firing hand,
  and hold left grip for close two-hand support. The support fingers close
  around the grip; the right controller continues to aim even when the
  controllers are touching or their tracked origins overlap.
- For the supported Colt, leave **Automatic Reload** disabled. Press A to
  eject the magazine, release the support grip, and collect a replacement
  magazine with left grip at the left hip. Insert it at the magazine well and
  release it to complete the transfer; proximity alone does not refill ammo.
- After an empty reload, use the free hand's index trigger at the slide,
  pull it back, then release it to chamber a round. Re-add the support grip
  when ready. Tactical reloads with a round chambered do not require this step.
- Other weapon variants can have different manual actions. Report the exact
  weapon and map if its interaction differs from these instructions.

### Black Cats aircraft guns

- Point the right controller to aim the occupied gunner station within its
  native movement limits. Use the right index trigger to fire; no grip is
  required for the mounted gun.
- Move your head to look around independently of gun aim. Ordinary handheld
  snap/smooth turning, stance, and melee gestures are bypassed in the aircraft.
- X retains the mission's use/interact action, including authored prompts.
  Station changes and temporary scripted aiming locks remain mission-controlled.

### Campaign satchel charges

- Select the satchel using the mission support selector (hold Y and deflect
  the left stick up once, the native `5` / action-slot-3 control).
- Hold the satchel with the right grip. Press and release the **left index
  trigger to throw** a charge; the **right index trigger detonates** placed
  charges. These use WaW's native throw animation, timing and mission logic,
  not the physical belt-grenade throw gesture.
- While a satchel is selected, the left trigger is reserved for its throw,
  including when the launcher's Button Grenades option is enabled. Switch back
  to a firearm to use normal belt/button grenades. Release the left trigger
  after selecting the satchel before pressing it to throw.

### Campaign tank

- Left stick: keep WaW's native view-relative driving/steering, using the
  cannon target direction rather than your headset's look direction.
- Right stick: smoothly traverse/elevate the cannon. Small deflections give
  fine aim; release the stick to retain its target direction. Native tank
  traverse speed and elevation/depression limits still apply.
- Right stick up raises the cannon; down lowers it. The world-space crosshair
  marks the cannon's native target, not the middle of the headset view; it disappears offscreen
  rather than following your gaze. It is not a moving-target lead predictor.
- Right index trigger: fire the cannon. No grip button is required.
- Left index trigger: use the OT-34's native flamethrower.
- Headset: look around freely relative to the turret view; head and hand poses
  do not steer the cannon.
- Ordinary snap/smooth player turning, stance gestures, and handheld weapon
  interactions are bypassed only while driving a verified tracked tank.

### Optional launcher controls

For Zombies and Campaign, the launcher offers opt-in accessibility/control
alternatives. All are disabled by default:

- **Automatic Reload:** A performs WaW's complete native reload. Physical clip,
  magazine, bolt, and charging interactions are bypassed while this is enabled.
- **Button Grenades:** the left index trigger uses WaW's conventional frag
  grenade button from any hand position. The belt reach, tracked grenade pose,
  and motion throw are bypassed while this is enabled.
- **Smooth Turning:** horizontal right-stick movement produces proportional
  continuous yaw at up to 120 degrees per second instead of one 45-degree snap
  per deflection. Right-stick up/down crouch, prone, rise, and jump gestures
  remain unchanged.

Turn an option off and relaunch the game to restore its default control.

The desktop firearm crosshair is disabled during VR gameplay. Aim with the
visible barrel; use prompts, grenade warnings, ammo, points, and round HUD
remain available. Gameplay HUD elements are reduced and pulled into the
central binocular field so ammo, points, round, and minimap elements can be
read without turning the eyes toward the uncomfortable edges of the lenses.

Local firearm tracers and predicted impacts originate at the visible barrel
and use the same fixed ADS-level cone as the authoritative shot. Blood,
projectile trails, and explosions remain native game effects; the launcher
disables the two stock camera-dependent element discard checks that are unsafe
for the late HMD camera.

Previous-weapon is intentionally unbound in this MVP. Native keyboard
input—including desktop ADS—remains available on the desktop.

## Native menus and cinematics

- Point the right controller at the finite menu panel to move WaW's native
  cursor. An orange ring/dot is drawn into the menu itself at the hit point;
  pull the right trigger once to click the pointed item.
- Left stick moves the native menu cursor.
- A confirms/selects.
- B goes back.
- The frontend, loading screens, console, and cinematics are presented as a
  gravity-level panel fixed two metres into OpenXR Local space. Moving your
  head left/right/up/down produces real parallax; gameplay switches to stereo
  VR. Controller pointing is enabled only while WaW's native UI owns input.

Launch the configured `WorldWarVR.exe` for the stock Zombies frontend. The
managed launcher supplies the game directory; standalone use requires
`--game-dir`, `WAWVR_GAME_DIR`, or an already prepared launcher-managed runtime.
That same
executable boots Nacht with `--launch`, Der Riese with
`--launch --der-riese`, or the frontend with `--launch --menu`; every form
automatically validates and injects the adjacent `WorldWarVR.dll`.

## Offline multiplayer and bots

Choose **Launch Multiplayer** from the single-player frontend or double-click:

```text
WorldWarVR-Multiplayer.exe
```

The command-line equivalent is `WorldWarVR.exe --launch --multiplayer`.

The single-player process exits before the isolated multiplayer runtime starts,
so OpenXR is handed off rather than shared by two game processes. From the MP
frontend, create a local game and choose a stock map/mode. Gameplay uses the
same headset, controller, weapon, and menu bindings above.

World War VR does not bundle bots. To enable PeZBOT, place the exact
user-owned `PeZBOTWAW_005p.zip` beside the launcher or in Downloads before
starting MP. A missing or rejected archive leaves ordinary offline multiplayer
available. See `docs\OFFLINE_MULTIPLAYER.md` for the accepted archive identity,
safe import behavior, and stage-only commands.

## Firing vibration

Confirmed local shots produce a short pulse in the right firing controller,
matching COD4 VR's default recoil feedback (78% amplitude, 50 ms). This covers
ordinary firearms, successful launcher/tank-cannon projectile spawns, and
locally operated mounted machine guns. Two-hand support does not add a second
left-hand firing pulse. Dry-trigger pulls, weapon handling, AI shots, and
menus do not generate firing vibration. Stale feedback is discarded through
focus/session loss rather than replayed when the headset returns.

## Resolution and recovery

Exact validated single-player launches default to a `6016x2688` packed source:
two `2496x2688` eyes plus a dedicated `1024x1024` physical-scope camera. The renderer
creates this off-screen through D3D9Ex, independently of the desktop size. If
performance regresses, use the `3744x2016` packed performance preset:

```text
WorldWarVR.exe --launch --resolution 3744x2016
```

Use `2560x1440` for the compatibility preset. Offline multiplayer keeps that
desktop-bound resolution as its default because the oversized renderer patch is
single-player-only. For minimum-cost device-loss recovery, use:

```text
wawvr-launcher.exe --launch --resolution 1024x768 --mod-dll WorldWarVR.dll
```

`WAWVR_SOURCE_RESOLUTION` provides the same override without changing a
shortcut; an explicit `--resolution` value takes precedence.

On this exact T4 executable's windowed path, `r_customMode` is parsed before
the stock `r_mode` resolution enum. The launcher therefore sets
`r_customMode` directly and intentionally does not use the older IW3-style
`r_mode -1` override.

The launcher disables stock depth-of-field with `r_dof_enable 0` for all
launch targets. This removes the legacy full-screen focus blur when aiming
down sights; it does not disable ADS itself.

For fail-closed diagnosis, set one of these environment variables to `1`
before starting the launcher:

- `WAWVR_DISABLE_XR`: leave WaW on its stock desktop renderer.
- `WAWVR_DISABLE_INPUT`: leave native game input untouched.
- `WAWVR_DISABLE_WEAPON`: disable tracked weapon placement and ballistics.
- `WAWVR_DISABLE_WEAPON_CAMERA_PATCH`: restore stock `tag_camera` movement.
- `WAWVR_DISABLE_MELEE_CAMERA_PATCH`: restore stock auto-melee target aiming.
- `WAWVR_DISABLE_DIRECT_BOOT`: restore the stock startup cinematic command.

These are recovery controls, not normal play settings. Remove the variable or
set it to `0` to restore the corresponding VR feature on the next launch.

For a targeted renderer investigation only,
`WAWVR_FX_STEREO_DIAGNOSTICS=1` enables changing backend emissive/light-state
telemetry. Leave it unset during ordinary play; the opt-in path writes from
the render thread and is intentionally disabled by default.

World War VR does not include the game. The launcher validates and stages a
user-owned compatible World at War 1.7 executable and links to its existing
data without modifying the installation.
