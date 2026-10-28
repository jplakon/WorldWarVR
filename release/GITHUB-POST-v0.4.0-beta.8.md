# Ammo Pouch & Aiming Update

World War VR v0.4.0-beta.8 is available free for everyone.

## What's new

- Small near-hip AMMO/GRIP marker and gentle vibration to help locate reload ammunition.
- Added raised front-lap grab area for seated play; looking down no longer tilts the pouch.
- Expanded model-specific aiming corrections across audited Campaign/Zombies weapons and variants, preserving native recoil and spread.
- Optional right weapon-hand pitch/yaw/roll controls for personal controller fit.
- Refined exact-model scope camera/lens handling, including PTRS variants.

The retained gameplay build passed 114 native tests and focused OpenXR Tester
checks, with 203 correlated native traces across 76 ordinary weapon/model pairs
(68 iron-sight, 8 barrel-axis). It also received positive local headset feedback.
This is not certification of every weapon, scope impact, distance or headset.
Personal hand angles rotate the visible gun and shot together; leave them at
zero unless the whole weapon's controller fit needs adjustment.

## Download and update

Download **WorldWarVR-v0.4.0-beta.8-Portable.zip** to play. Close the game and old
launcher, extract into a **new empty folder**, keep all files together, then run
**WorldWarVR.exe** with your headset/controllers connected. Do not merge builds
or extract over the game installation. Select your legitimate compatible World
at War 1.7 installation. Start with **Performance**; use **Recovery** if needed.

Complete corresponding source, including pinned dependencies, is provided free
in **Source.zip**. Source is optional to play. Checksums and the GPL source notice
accompany the downloads. GitHub's automatic source ZIP is not the player package.
Accepted binaries are retained unchanged, so internal launcher labels can still
say beta.7; identify beta.8 by its download filename/checksums and component map.

## Remaining limitations

Low FPS/stutter is not claimed fixed. Practical enemy-grenade throwback remains
disabled. Scope optical-policy checks are separate from impact/zero qualification;
rare/custom and special projectile variants can still need individual work. This
does not provide an automatic Quest 2 offset preset or certify every headset.
Regional Compatibility Test1's Campaign validation correction is not included;
keep a working Test1 folder intact. Local/offline Multiplayer is experimental;
online/public/protected servers are unsupported.

Report the exact ZIP, headset/runtime, map, weapon and reproducible steps through
[GitHub Issues](https://github.com/jplakon/WorldWarVR/issues) or your existing
support conversation. Review logs for personal information; do not upload game
executables, keys or saves.

GPL-3.0-only. Original work by Ryan Craighead; modifications by John Plakon and
contributors. Credits and third-party notices are preserved; KisakCOD/John-deep
COD4 VR is an implementation/interaction reference. No Call of Duty game content
or optional PeZBOT files are included. Original game required. Independent fan
mod, not affiliated with Activision.
