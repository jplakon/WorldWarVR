# World War VR v0.4.0-beta.3 — Aiming, Scopes & Weapon Controls

Beta.3 is ready for Patreon members at the **$3/month tier and above**!

This update focuses on the weapon and scope problems you've been reporting. I've been testing the fixes in the OpenXR simulator and in my Quest 3 through Virtual Desktop, including aiming in different directions, one- and two-handed grips, mounted guns, the scoped Mosin, and the grenade-launcher Garand.

## What's changed since beta.2

- **Rifle trajectories:** corrected the firing and impact paths so shots follow the visible barrel even when the weapon points away from where your head is looking. The M1 Garand, unscoped Mosin-Nagant and SVT-40 received focused testing. The PPSh-41 and SVT-40 also received sight-alignment corrections.
- **Mounted machine guns:** the right controller controls the aim and the right trigger fires. Moving your head or the turning stick no longer redirects an ordinary mounted gun. The native mechanical limits still apply.
- **Scoped Mosin in Vendetta:** fixed enemies and vehicles disappearing when holding the rifle with both hands and activating the scope. Also corrected the scoped rifle's shot alignment with the reticle.
- **Scope visibility and HUD:** the optic and surrounding world remain visible, and the compass/map and ammunition display no longer disappear just because the scope is active.
- **Grenade-launcher M1 Garand:** the campaign's attachment variant now uses the Garand's eight-round manual en-bloc clip reload instead of falling back to an automatic rifle reload. While that attached Garand is equipped, tap **Y** to switch between the rifle and its **M7 grenade launcher**, and use **right trigger** to fire. Tap Y again to return to the rifle. The grenade-launcher mode retains its native reload behavior; this is not a new physical grenade-loading interaction.
- **Motion melee:** added stronger safeguards against unintended attacks during tracking jumps, head movement and grip transitions. You can also perform a deliberate forward jab while retaining a supported pistol in your right hand. Release the firing trigger first. The existing free-hand swing, two-handed rifle/bayonet thrust and right-stick-click melee remain available.
- **Launcher recovery:** a failed game-folder Browse dialog now produces a recoverable message rather than preventing you from continuing; you can enter the game path manually.

This is a complete download. The previous campaign, performance, manual-reload, grenade, language, mission-unlock, smooth-turn, haptic and SteamVR fixes remain included; you don't need to install earlier versions first.

## Install or update

1. Close World at War and any older World War VR launcher.
2. Download **WorldWarVR-v0.4.0-beta.3-Patreon-Release.zip** and the matching **SHA256.txt** attachment.
3. Extract the release ZIP, then extract **WorldWarVR-v0.4.0-beta.3-Portable.zip** inside it into a **new, empty folder**. Keep the extracted files together. Do not put it in the game's installation folder or merge it with an old mod package.
4. Connect and wake the headset and both controllers. Start the OpenXR runtime used by your connection method.
5. Run **WorldWarVR.exe** from the new folder. Select the same legitimate World at War installation you normally use, containing **CoDWaW.exe**. Do not delete your existing VR profile or saves.
6. Select **Main Menu** for Campaign or Zombies. Start with **Performance** quality, choose your control options, then **Launch in VR**.

Virtual Desktop users should normally leave **Quest Air Link compatibility** off. For the SteamVR-based Link/Air Link path, start SteamVR and use that compatibility option; a usable **32-bit OpenXR runtime** is required. If the launcher reports that it cannot find one, send the exact error instead of repeatedly forcing a launch.

Leave **Automatic Reload** off for physical reloads. The launcher also offers **Button Grenades** and **Smooth Turning**. Relaunch after changing these options. Full controls and troubleshooting are included in **CONTROLS.txt**, **INSTALL.txt** and **KNOWN-ISSUES.txt**.

## Testing and remaining issues

The aiming, mounted-gun, scoped-Mosin, scope-HUD, held-pistol melee and attached-Garand changes received focused physical-headset acceptance on my setup. This does not mean every weapon, map, runtime or headset has been checked. I completed the campaign during earlier beta development; I have **not replayed the entire campaign on this exact release**.

I'm still working through customer reports. Pimax/Index and other headset-specific problems, custom-Zombies DirectX crashes, regional game compatibility, pause-menu clipping and the remaining Hard Landing visual report are not being declared solved by this release. Reports of random melee or blank scopes on other setups still need the original reporters to retest. Local/offline Multiplayer remains experimental; online Multiplayer is unsupported.

If an issue persists, please comment here or DM me on Patreon/Discord. Include **beta.3**, the mission/map and weapon, headset and OpenXR runtime, selected control/quality options, and the steps that reproduce it. A short video is especially helpful for aiming or visual problems. Logs and recordings can also be sent to **jplakon@gmail.com**. I'll respond as quickly as I can.

Thank you for the reports, testing and patience—this release is directly shaped by that feedback.

You need your own legitimate compatible **Call of Duty: World at War 1.7** installation. No game executable or game assets are included. The download includes the complete matching source and license information under **GPLv3**; recipients retain the rights granted by that license, including redistribution. You do not need to extract the Source ZIP just to play.

More updates: https://www.patreon.com/J_Play/
