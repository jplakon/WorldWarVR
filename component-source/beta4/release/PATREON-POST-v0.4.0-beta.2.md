# World War VR v0.4.0-beta.2 — SteamVR Compatibility Update

World War VR **v0.4.0-beta.2** is now available to Patreon members at the
**$3/month tier and above**.

This update focuses on the SteamVR-based Quest Link/Air Link path. The game can
now enter VR with the world upright and facing forward, correct stereo depth,
proper head and controller movement, and normal tracked weapon motion on the
Quest 3 setup used for the final physical-headset test.

## What changed since beta.1

- Fixed the entire VR world appearing upside down through SteamVR.
- Fixed the image becoming backward after compensating for that rotation.
- Fixed controller movement being inverted relative to the real controllers.
- Fixed the in-game hands moving vertically in the opposite direction.
- Fixed weapons losing motion tracking after entering gameplay.
- Unified the SteamVR reference space used by both eyes, the headset, both
  controllers, the game world, and the native-menu panel.
- Added a SteamVR fallback for controllers whose left Menu button is reserved
  by the system dashboard:
  - Tap **B** once to pause or resume.
  - Hold **B** for one second to recenter.
  - Use right-stick down for crouch/prone while this fallback is active.
- Removed the duplicate pause input that previously made the menu immediately
  close again or required holding B for several seconds to resume.
- Added more automated two-eye, controller-space, and pause-control tests to
  protect these fixes from regressions.

The final beta.2 candidate passed all **69 automated checks** and was then
confirmed in a real Quest 3 headset: stereo and depth were normal, the image
was upright, head and controller tracking moved correctly, the weapon tracked
normally, and one B tap reliably paused or resumed the game.

## Download and update

1. Close World at War and any older World War VR launcher.
2. Download `WorldWarVR-v0.4.0-beta.2-Patreon-Release.zip` and the matching
   SHA256 text file attached to this post.
3. Extract the outer release ZIP.
4. Extract `WorldWarVR-v0.4.0-beta.2-Portable.zip` into a **new, empty folder**
   outside the Call of Duty: World at War installation.
5. Connect and wake your headset and controllers before launching.
6. Run `WorldWarVR.exe` and select the folder containing `CoDWaW.exe` if the
   launcher does not detect it automatically.
7. For Quest Link/Air Link through SteamVR, make SteamVR the active OpenXR
   runtime and enable **Quest Air Link compatibility** in the launcher.
8. Start with the **Performance** quality preset and click **Launch in VR**.

You need your own legitimate Call of Duty: World at War 1.7 installation. No
Call of Duty game files are included. The Source ZIP inside the download is the
complete matching GPLv3 source for this exact binary and is not required simply
to play.

## Current status and feedback

This remains beta software. I completed the Campaign in VR during the beta.1
development cycle, but beta.2 received a focused SteamVR/Quest 3 compatibility
test rather than another complete Campaign playthrough. Pimax Crystal Light
compatibility is still unconfirmed, local/offline Multiplayer remains
experimental, and online Multiplayer is unsupported.

If a launch closes unexpectedly, make sure the headset and SteamVR are fully
ready, then launch again. If the problem repeats, please send the new
`WorldWarVR.log` rather than an older combined log.

Please report technical issues in the Patreon comments or DM me and I will
respond as quickly as I can. Logs, screenshots, and recordings can also be sent
through Discord or to **jplakon@gmail.com**. Please include your World War VR
version, mission/map, weapon, headset, connection/OpenXR runtime, GPU, quality
preset, and exact reproduction steps.

Thank you to everyone testing and reporting problems.

More updates: https://www.patreon.com/J_Play/
