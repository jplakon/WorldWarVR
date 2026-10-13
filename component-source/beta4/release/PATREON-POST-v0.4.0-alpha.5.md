# World War VR v0.4.0-alpha.5 — Tanks, Special Weapons & Controller Rumble

World War VR alpha.5 is available to members at the $3/month tier and above!

I'm continuing through the Campaign and fixing the special weapons and vehicle
sections as I reach them. This update also adds the firing rumble from COD4 VR.

## What's new since alpha.4

- **PTRS-41:** a working physical scope and manual five-round clip reload,
  including the loading hatch and either-hand charging. It stays
  semi-automatic—you do not need to cycle it after every shot.
- **Panzerschreck:** corrected launcher alignment and rocket direction so the
  rocket follows the tube instead of shooting upward and to the right.
- **Tank controls:** drive with the left stick and aim the cannon smoothly with
  the right stick. Let go of the right stick to retain the cannon's target
  direction. Head movement stays independent, so you can look around freely.
- **Tank aiming marker:** the crosshair now marks where the cannon is aimed
  instead of staying in the middle of your headset view. It is not a lead
  indicator for moving targets.
- **Firing rumble:** short COD4-style pulses in the right controller when shots
  actually fire. Empty-trigger pulls and enemy fire do not trigger your rumble.

All the earlier updates—including weapon stability, manual weapon actions,
campaign mission unlocking, mounted machine guns, the bazooka correction,
and both-eye smoke—are included too.

## Tank controls

- Left stick: drive and steer using the game's cannon-relative controls.
- Right stick: aim the cannon; up raises it and down lowers it. Native movement
  and elevation limits still apply.
- Right index trigger: fire the cannon. No grip button is required.
- Left index trigger: use the OT-34 flamethrower.
- Headset: look around without steering the cannon.

## Download and update

1. Close the game and the old launcher.
2. Download `WorldWarVR-v0.4.0-alpha.5-Patreon-Release.zip` and its SHA256 text
   attachment from this post.
3. Extract the release ZIP, then extract the **Portable ZIP inside it** into a
   new, empty folder. Do not overwrite an older mod folder or put it inside
   the Call of Duty installation.
4. Connect and wake the headset and both controllers, with your OpenXR runtime
   ready.
5. Run `WorldWarVR.exe` from the new portable folder. Let it detect your game,
   or select the folder containing `CoDWaW.exe`.
6. Select **Main Menu** for Campaign/Zombies and launch in VR. Performance is a
   sensible starting preset; use Recovery if you encounter display problems.

You need your own legitimate, compatible World at War 1.7 installation.
The Source ZIP is included for the matching source code; you do not need to
extract it to play. Full controls, requirements, troubleshooting, and the
changelog are included beside the launcher.

## Testing status and known limitations

This is still an alpha. I have not finished testing the Campaign from beginning
to end. PTRS-41, Panzerschreck, and tank changes have targeted simulator checks,
but not complete physical-headset coverage of every situation. The new firing
rumble has been checked on Quest 3 through Virtual Desktop.

Pimax Crystal Light and Meta Quest Link issues remain under investigation; this
release does not claim to resolve them. Local/offline Multiplayer remains
experimental, and Online Multiplayer is unsupported.

## Please keep sending reports

Post in the Patreon comments or DM me, and I'll respond as quickly as I can.
Logs, screenshots, and recordings can also be sent through Discord or to
**jplakon@gmail.com**.

Please include the version, mission/map, weapon, headset, connection/runtime,
GPU, quality setting, what happened, and `WorldWarVR.log` if available.

Thank you for helping me improve this! Your reports directly guide the fixes.

The download includes the portable mod and its complete matching source under
GPLv3, at no additional source-code fee. Recipients retain the GPL rights to
modify and redistribute the covered software. No Call of Duty game files are
included.

More updates: https://www.patreon.com/J_Play/
