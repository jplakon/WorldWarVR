# World War VR controls

## Gameplay

| Control | Action |
| --- | --- |
| Headset movement | 6DOF view and position |
| Right controller pose | Aim the visible weapon |
| Right index trigger | Fire while the right hand owns the firearm; when free, perform contextual right-hand actions such as a bolt or charging handle |
| Left index trigger at a supported weapon action | While the right hand retains the weapon, operate its bolt or charging handle with the free left hand |
| Right grip | Draw and retain the firearm |
| Left grip | Add the support hand and native firing/ADS pose; retain a supported weapon while the right hand performs a manual action |
| Left index trigger at left hip | Grab and hold/cook a frag grenade; release to throw or gently drop |
| Left index trigger at right hip | Grab the secondary tactical grenade; release to throw or drop |
| Left stick | Move relative to the headset's horizontal heading |
| Left stick click | Start sprint; remains latched while moving and clears when the stick returns to neutral |
| Right stick left/right | Turn; one 45-degree snap per deflection by default, or proportional smooth yaw when enabled in the launcher |
| Right stick down | Step standing to crouched, then crouched to prone; center between steps |
| Right stick up | Step prone to crouched, then crouched to standing; jump when already standing |
| A | Begin the native/manual weapon reload |
| B | Crouch normally. If SteamVR cannot expose the left Menu action, tap B to pause/resume or hold B for one second to recenter; use right-stick down for stance on that fallback path |
| X | Use/interact, including doors, wall weapons, mystery box, and revive |
| Y | Switch to the next weapon; while holding the M1 Garand/M7 pair, toggle rifle-grenade mode on or off |
| Right-stick click, fast outward free-right-hand swing, or deliberate forward rifle thrust while both grips are held | Melee; native weapon behavior selects a bayonet thrust or ordinary firearm attack |
| Left Menu, short tap | Open or close the native menu when the runtime exposes this action |
| Left Menu, hold one second | Recenter position, facing, and level horizon when the runtime exposes this action |

The desktop firearm crosshair is intentionally disabled during VR gameplay. Aim with the visible barrel. Firearm traces and predicted impacts originate at the barrel rather than the center of the headset view.

### Smooth Turning option

Snap turning remains the default: each horizontal right-stick deflection turns
45 degrees and returning the stick to center rearms the next turn. Enable
**Smooth Turning** in the launcher for proportional continuous right-stick yaw
at up to 120 degrees per second. Right-stick up/down crouch, prone, rise, and
jump gestures remain unchanged.

## Campaign tank

- Left stick: drive/steer using the native cannon-relative driving controls.
- Right stick: smoothly aim the cannon; up raises it and down lowers it.
  Release the stick to retain its target direction within native limits.
- Right trigger: fire the cannon; no grip is required.
- Left trigger: OT-34 flamethrower.
- Headset: look independently of cannon aim.
- The binocular target marker follows the cannon's native target, not your
  gaze. It is not a moving-target lead predictor.

## Black Cats aircraft guns

- Aim the occupied gunner station with the right controller, within its native
  movement limits. Right trigger fires; no grip is required.
- Headset look remains independent. Ordinary handheld snap/smooth turning,
  stance, and melee gestures are bypassed while in the aircraft.
- X keeps the native use/interact action. Station transitions and temporary
  aiming locks remain controlled by the mission.

## Campaign satchel charges

- Hold Y and deflect the left stick up once to select the satchel.
- Hold right grip, then press and release left index trigger to throw.
  Right index trigger detonates placed charges.
- Release the left trigger after selecting the satchel before pressing it to
  throw. This uses the native throw animation, including when Button Grenades
  is enabled. Switch to a firearm to resume belt/button grenade controls.

## Firing vibration

Confirmed shots produce a short right-controller pulse matching COD4 VR.
The left support hand does not receive an extra firing pulse. Empty-trigger
pulls, reload handling, and enemy fire do not generate firing vibration.

## Two-hand weapons and manual reloads

- Draw and retain a firearm with the right grip.
- Add left grip to support a rifle at its front or a pistol beside the firing
  hand. Pistols retain right-controller aiming with the controllers close together.
- Either hand can retain a supported weapon while the other hand carries out the weapon's physical interaction with that free hand's index trigger.
- Press A to begin a reload. Complete the contextual clip, magazine, bolt, or charging action before the weapon can fire again.
- Bolt-action weapons require physical cycling between shots. Retain the rifle with either hand and use the free hand's index trigger at the bolt.
- Empty magazine-fed weapons can require a manual charging action after a replacement magazine is inserted.
- Bring a mounted scope to the eye and look through it; the scope is not a full-screen overlay.

Interaction geometry varies by weapon. If a specific weapon cannot be cycled or reloaded, report its exact name, upgrade state, hand sequence, and map.

### Colt pistol magazine and slide

With Automatic Reload disabled, press A to eject the magazine. Release the
support grip, collect a replacement with left grip at the left hip, insert it
at the magazine well, and release it to transfer the ammunition. After an
empty reload, hold the free index trigger at the slide, pull it back, then
release it to chamber a round. A tactical reload with a chambered round does
not require that slide action. Re-add left grip beside the firing hand for
closed two-hand support; close or coincident controller positions remain valid.

### Automatic Reload option

Enable **Automatic Reload** under **Zombies & Campaign Controls** in the launcher to make A
perform WaW's complete native reload. Physical clip, magazine, bolt, and
charging interactions are bypassed while the option is enabled. It is disabled
by default; turn it off and relaunch to restore manual reloads.

## Grenades

Use the left index trigger at the appropriate hip to take a grenade. Keep holding while it follows the left hand, then release to throw or gently drop it.

Enable **Button Grenades** under **Zombies & Campaign Controls** in the launcher to use
WaW's conventional frag-grenade button with the left index trigger from any
hand position. This bypasses the belt reach, tracked grenade pose, and motion
throw. It is disabled by default; turn it off and relaunch to restore physical
grenades.

## Native menus and cinematics

World at War's native frontend, loading screens, cinematics, console, and Multiplayer selection panels appear on a gravity-level VR panel.

- Point the right controller at the panel to move its cursor.
- Pull the right trigger once to click the focused item.
- The left stick can move the native menu cursor.
- A confirms/selects.
- B goes back.

Keyboard and mouse input remain available on the desktop as a fallback.

## Campaign support selector

Some Campaign prompts use the original numbered support selections. Hold Y, then deflect the left stick once:

| Left-stick direction | Native Campaign input |
| --- | --- |
| Left | `6` — rocket-barrage designator in Little Resistance or airstrike radio in Shuri Castle |
| Up | `5` — satchel selection when provided by the mission |
| Down | `N` |
| Right | `7` |

Movement remains active while the chord is held. Completing a direction consumes Y's release so it does not also switch weapons. Passive thumbrest contact is ignored, and the chord is disabled in Multiplayer.

Aim the selected designator with the right controller and use right trigger.
Shuri Castle still enforces its authored valid strike areas and bomb runs.
