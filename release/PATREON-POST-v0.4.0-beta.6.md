Beta.6 is ready! This update focuses on controller interactions and weapon aiming. It's available free on GitHub, and the attachment here bundles the same player download with complete corresponding source and checksums. Thank you to everyone supporting the work and helping test the mod.

WHAT'S NEW

Right-stick-click melee is now disabled by default, so turning and incidental clicks should no longer activate that binding. Deliberate physical melee remains available, with an explicit optional stick-click binding retained.

Sprint activation is more reliable: a short left-stick click starts sprint when the game allows it. Stamina, stance and scripted restrictions still apply.

I've refined manual reloads and regripping across the tested weapon families, improved weapon-handling transitions and recovery after released-hand tracking gaps, and corrected specific sight-alignment profiles. That includes the starting Colt in Nacht der Untoten, which now passed actual-shot OpenXR Tester checks and my focused headset test. These are targeted corrections, not one global offset applied to all guns.

The Little Resistance radio can be selected, aimed and fired using controllers; its objective progression was verified in OpenXR Tester without keyboard gameplay input. There are clearer controller-aware reload/radio hints, lower held-controls help, and a correction for Colt weapon parts disappearing during magazine removal.

TESTING

The gameplay DLL passed 102 automated native tests and is the same tested binary in this download. Focused headset feedback covered sprinting, manual reload, handling, weapon-part visibility, help readability and the starting Colt. Other cases have bounded Tester coverage. This isn't another complete Campaign replay or a promise that every weapon, controller and runtime is now perfect. Earlier foliage, chest-holstering, scope and reload-feedback improvements are retained.

DOWNLOAD

https://github.com/jplakon/WorldWarVR/releases/tag/v0.4.0-beta.6

Choose WorldWarVR-v0.4.0-beta.6-Portable.zip to play. The Patreon attachment contains that identical ZIP, complete corresponding source, checksums and license information. No subscription is required for the GitHub download.

INSTALLATION / UPDATING

1. Close World War VR and its launcher.
2. Extract the Portable ZIP into a new, empty folder, not the game folder or an older mod package.
3. Keep all files together and run WorldWarVR.exe after connecting and waking your headset/controllers.
4. Select your legitimate compatible World at War 1.7 installation, choose Main Menu for Campaign/Zombies, and start with Performance quality.
5. Leave Automatic Reload OFF for physical reloads, or enable it if you prefer A to complete the native reload.

There is no installer or automatic updater. Full controls, installation instructions and known issues are included.

ONGOING WORK

The experimental contextual enemy-grenade throwback shortcut is still unresolved and disabled; I am not calling it fixed. Normal grenade controls remain. Rare keyboard-only tutorial paths, additional weapon variants, affected startup/runtime configurations, regional executable compatibility, Rift S aiming reports, Custom Zombies DirectX crashes and smoke flicker still need targeted follow-up. Some foliage pop-in remains. Local/offline Multiplayer is experimental; online/public/protected servers are unsupported.

Please report the exact ZIP/build, headset/connection/runtime, map, weapon and steps to reproduce through Discord DMs, comments or GitHub Issues. Keep personal information out of public logs; don't upload game executables, keys or saves.

World War VR is GPLv3 software. Complete corresponding source and retained component sources are freely available alongside the binaries. Credits to Ryan Craighead for the original work, John Plakon and contributors for modifications, and KisakCOD/John-deep COD4 VR as an implementation/interaction reference. No Call of Duty game assets or optional PeZBOT content are included. Original game required; not affiliated with Activision.
