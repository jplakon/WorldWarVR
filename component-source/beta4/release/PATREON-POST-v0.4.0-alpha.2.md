# World War VR v0.4.0-alpha.2 — Weapon Stability Update

World War VR v0.4.0-alpha.2 is now available to Patreon members at the $3/month tier and above.

The headline change is weapon stability. I spent the last four days almost entirely on one extremely stubborn problem: rifles floating and wobbling when held with two hands or moved between grip states. World at War now has two independently usable hands, which made this much more complicated than COD4 VR's fixed right-hand weapon setup.

That issue is finally fixed on my test setup. One-handed and two-handed aiming now stay aligned through normal movement, manual reloads, bolt operation, hand changes, and regripping. The rifle should feel much more solid when you bring the sights to your eye.

## Improvements since v0.4.0-alpha.1

- Fixed the major one-handed and two-handed weapon float and wobble.
- Fixed weapons hovering around the controller's aiming line during two-handed use.
- Fixed grip transitions, reload handoffs, and regripping so the rifle keeps its position and balance.
- Fixed the weapon jumping forward when releasing the right hand to work a bolt or charging handle.
- Bolts and charging handles can now be operated with either hand: retain the weapon in one hand and use the free hand's index trigger at the action.
- Completed the M1 Garand's physical en-bloc clip reload, including its open action and automatic close after insertion.
- Added a deliberate two-handed rifle-thrust melee gesture. Bayonet-equipped rifles use their native bayonet attack; other firearms use their ordinary melee attack.
- Removed normal locomotion and weapon view bob for steadier, more comfortable VR movement and aiming.
- Added optional Smooth Turning at up to 120 degrees per second. The existing 45-degree snap turn remains the default.
- Added optional Automatic Reload. When enabled, A performs World at War's complete native reload instead of requiring the physical magazine, clip, bolt, or charging interaction.
- Added optional Button Grenades. When enabled, the left index trigger uses the conventional frag-grenade control without requiring a belt reach and motion throw.
- Included an initial Campaign Mission Select unlock attempt. That
  COD4-derived startup flag did not unlock WaW's mission list and is corrected
  in the next release.
- Added safer controller tracking-loss and relocalization recovery to prevent a temporarily lost controller from sending a held weapon floating or violently jumping when tracking returns.
- Improved controller-pose/frame matching and the VR frame handoff used by held weapons and stereo presentation.
- Added Steam-installed language handling for English, French, Italian, German, and Spanish (Spain). The launcher now displays the detected language, checks the matching language data, and refreshes its cached language selector when Steam's installed language changes.
- Added stronger last-second launcher validation so incomplete or mismatched language files cannot silently launch from stale cached data.
- Added the reported Quest 3 Meta Quest Link crash and Pimax desktop-only output paths to the known-issues list for investigation.

## An honest Campaign status update

I still have not personally played through the entire Campaign.

Most of my available development time over the last four days went into diagnosing and finally fixing the weapon-wobble and grip-handoff problem. This build should not be treated as confirmation that every mission, scripted sequence, cutscene, support prompt, and mission-specific weapon works perfectly from beginning to end. Zombies remains the most thoroughly tested mode.

Now that the wobble problem is out of the way, I am moving directly onto the other issues people have reported, including Campaign blockers, headset/runtime compatibility, crashes, and weapon-specific problems. Please keep sending reports; they will guide the next updates.

## Download and update instructions

Download both version-matched attachments from this post:

- `WorldWarVR-v0.4.0-alpha.2-Patreon-Release.zip`
- `WorldWarVR-v0.4.0-alpha.2-Patreon-Release-SHA256.txt`

Then:

1. Extract the Patreon Release ZIP.
2. Inside it, extract `WorldWarVR-v0.4.0-alpha.2-Portable.zip` into a new, empty folder.
3. Do not extract the mod into the Call of Duty: World at War installation folder, and do not copy it over an older World War VR build.
4. Connect and wake your headset and both controllers before starting.
5. Make sure the OpenXR runtime for your connection method is active.
6. Run `WorldWarVR.exe` from the new portable folder.
7. Let the launcher detect World at War, or browse to the folder containing `CoDWaW.exe`.
8. Select Main Menu for Zombies or Campaign.
9. Start with the Performance preset. Use Recovery if you encounter a black screen, stutter, or limited GPU headroom.
10. Press Launch in VR.

The launcher now shows the World at War language it detected. You must provide your own legitimate Steam copy of Call of Duty: World at War updated to version 1.7. No Call of Duty game files are included.

## Main controls

- Move: left stick
- Sprint: click the left stick; sprint remains active until the stick returns to neutral
- Turn: right stick left/right; snap by default or smooth when enabled in the launcher
- Crouch/prone: right stick down, one stance step per deflection
- Rise/jump: right stick up; rise one stance step or jump while standing
- Draw and retain firearm: right grip
- Add support hand/firing pose: left grip
- Fire: right index trigger
- Begin manual reload: A
- Operate a bolt or charging handle: retain the weapon with either hand and use the free hand's index trigger at the action
- Use/interact: X
- Switch weapon: Y
- Melee: right-stick click, a fast free-right-hand swing, or a forward two-handed rifle thrust
- Grab frag grenade: left index trigger at the left hip
- Grab tactical grenade: left index trigger at the right hip
- Throw or drop grenade: release the left index trigger
- Open/close native menu: tap the left Menu button
- Recenter: hold the left Menu button for one second

The existing Campaign support selector remains available: hold Y and deflect the left stick once. Left sends native `6` for the Little Resistance rocket-barrage designator; up sends `5`, down sends `N`, and right sends `7` for matching scripted prompts.

## Optional launcher controls

These remain disabled by default:

- **Automatic Reload:** A performs the game's complete native reload and bypasses physical reload interactions.
- **Button Grenades:** the left index trigger uses the conventional frag-grenade button from any hand position.
- **Smooth Turning:** proportional continuous turning replaces the default 45-degree snap turn.

Change an option and relaunch the game for it to take effect.

## Current known limitations

- The Campaign has not been completion-tested from beginning to end.
- Scripted Campaign sequences, support prompts, cutscenes, or mission-specific interactions may still need fixes.
- Quest 3 through Meta Quest Link and Pimax desktop-only headset output still need further compatibility investigation.
- Rare, upgraded, and mission-specific weapons may still need individual adjustment.
- Local/offline Multiplayer remains experimental.
- Online Multiplayer is unsupported and untested.
- Haptic feedback is not currently part of the tested feature set.

## Reporting problems

Please report technical issues in the comments or send me a Patreon direct message. I will respond as quickly as I can.

If you need to send a log, screenshot, or video, you can also use Discord or email `jplakon@gmail.com`.

Please include:

- The exact World War VR version
- Campaign mission or Zombies map
- Weapon being used
- Headset and controllers
- Connection method and active OpenXR runtime
- GPU and driver version
- Launcher quality preset
- Exact steps that reproduce the problem
- `WorldWarVR.log`, if generated
- A screenshot or short recording when the issue is visual or difficult to describe

World War VR is pre-release software and will continue receiving updates. Thank you to everyone testing it, recording problems, sending logs, and being patient while I work through the difficult issues.

The download contains the portable build and its complete corresponding source and license information under GPLv3. It contains no Call of Duty game content.

More releases and project updates: https://www.patreon.com/J_Play/
