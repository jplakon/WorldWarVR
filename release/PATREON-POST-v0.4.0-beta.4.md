World War VR v0.4.0-beta.4 — Chest Holstering & Reload Reliability

Beta.4 is ready! This update is available free on GitHub, and I'm sharing the release here too. Thank you to everyone supporting the work and testing the mod.

What's new

Weapons now rest close to your chest when you release both grips. They follow you as you turn, lean and crouch instead of floating ahead in the game's old body direction. Looking down or tilting your head keeps the weapon upright. This uses the headset to estimate your chest position—it doesn't require a body tracker. I tested the new placement in my headset and it now feels right.

I've also hardened launcher startup and reload settings. The launcher waits until your saved options finish loading before accepting changes, so early clicks aren't overwritten. Gameplay receives explicit reload/control settings, and delayed manual-reload magazine assets can recover more reliably.

Manual reload, snap/smooth switching, automatic reload and switching back to manual all passed our focused Garand tests. The original customer's intermittent first-launch auto-reload problem didn't reproduce here, so I still need their confirmation rather than calling every version of that report solved.

Download

https://github.com/jplakon/WorldWarVR/releases/tag/v0.4.0-beta.4

Choose WorldWarVR-v0.4.0-beta.4-Portable.zip to play. The attachment here contains that same Portable ZIP, complete matching source and checksums. You don't need a Patreon subscription to download the GitHub release.

Installation / updating

1. Close World War VR and its launcher.
2. Extract the Portable ZIP into a new, empty folder—not your game folder, and not over an older mod package.
3. Keep its files together and run WorldWarVR.exe after connecting and waking your headset/controllers.
4. Select your own legitimate compatible World at War 1.7 installation. Choose Main Menu for Campaign/Zombies and start with Performance quality.
5. Leave Automatic Reload OFF for physical reloads, or turn it ON if you want A to complete the normal game reload. Turning mode is a separate option.

This is a portable release, with no installer or automatic updater. Normal saved preferences/profile locations are retained. Full installation instructions, controls and known issues are included in the download and repository.

Testing and ongoing support

The gameplay DLL passed 72 native checks and targeted OpenXR simulator tests, followed by my headset confirmation of the chest fix. Reload settings also received five simulator and five physical Garand test cases. This isn't another full Campaign replay on the final package, a performance update, or a promise that every headset/weapon is fixed.

I'm still investigating the affected Index/Pimax startup cases, Rift S aiming, Custom Zombies DirectX errors, regional executable compatibility and the remaining visual/interaction reports. Please keep sending reproducible problems in the comments, Discord DMs or GitHub Issues. Include your exact build, headset/runtime, mission/map, weapon and what you did. Send logs privately if they contain personal information; don't send game executables, keys or saves.

World War VR is GPLv3 software. Complete matching source is included and available free alongside the binaries. Credits to Ryan Craighead for the original World War VR work, John Plakon and contributors for modifications, and KisakCOD/John-deep COD4 VR as an implementation/interaction reference. No Call of Duty game assets or optional PeZBOT content are included. Original game required; not affiliated with Activision.
