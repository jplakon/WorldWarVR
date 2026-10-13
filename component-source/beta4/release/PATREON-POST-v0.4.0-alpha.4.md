# World War VR v0.4.0-alpha.4 — Campaign & Stereo Effects Update

World War VR v0.4.0-alpha.4 is now available to Patreon members at the $3/month tier and above.

This update focuses on problems found while playing through the Campaign, along with an important stereo-rendering fix.

## Improvements since v0.4.0-alpha.3

- Campaign Mission Select now actually unlocks every standard level. World War VR uses World at War's real mission-progression setting rather than the ineffective COD4-derived setting used previously.
- Fixed ordinary mounted machine guns in the Campaign. The gun now follows the right controller within its normal mechanical limits, the right trigger fires it, bullets follow the weapon's aim, and headset movement remains independent.
- Fixed bazooka aiming so the rocket, visible trail, impact, and explosion follow the direction of the barrel instead of traveling upward and off-angle.
- Added physical manual reloading to the scoped Mosin-Nagant used in the Campaign, matching the other supported bolt-action rifles.
- Added physical manual reloading to the bayonetted M1 Garand.
- Fixed smoke and other soft-particle effects appearing in only the left eye. These effects now render correctly in both eyes.
- Expanded the 32-bit OpenXR runtime selection and logging used to investigate Pimax compatibility. Pimax Crystal Light support has not yet been confirmed, so I am not claiming that issue is fixed in this build.

## Campaign status

I am actively playing through the Campaign and fixing problems as I encounter them, but I still have not completed a full beginning-to-end test.

This release should not be treated as confirmation that every mission, scripted sequence, cutscene, special weapon, or progression trigger works perfectly yet. Please continue reporting anything that blocks or disrupts your playthrough.

## Download and update instructions

Download both attachments from this post:

- `WorldWarVR-v0.4.0-alpha.4-Patreon-Release.zip`
- `WorldWarVR-v0.4.0-alpha.4-Patreon-Release-SHA256.txt`

Then:

1. Extract the Patreon Release ZIP.
2. Inside it, extract `WorldWarVR-v0.4.0-alpha.4-Portable.zip` into a new, empty folder.
3. Do not place it inside the Call of Duty: World at War installation folder.
4. Do not copy it over an older World War VR build.
5. Connect and wake your headset and both controllers.
6. Make sure the correct OpenXR runtime is active.
7. Run `WorldWarVR.exe` from the new portable folder.
8. Let the launcher detect World at War, or browse to the folder containing `CoDWaW.exe`.
9. Select Main Menu for Campaign or Zombies.
10. Start with Performance quality. Try Recovery if you encounter a black screen or performance problems.
11. Select Launch in VR.

You must provide your own legitimate Call of Duty: World at War installation updated to version 1.7. The release does not contain Call of Duty game files.

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
- Open or close the native menu: tap the left Menu button
- Recenter: hold the left Menu button for one second

The Campaign support selector remains available: hold Y and deflect the left stick once. Left sends native `6` for the Little Resistance rocket-barrage designator; up sends `5`, down sends `N`, and right sends `7` for matching scripted prompts.

## Optional launcher controls

These remain disabled by default:

- **Automatic Reload:** A performs the game's complete native reload and bypasses physical reload interactions.
- **Button Grenades:** the left index trigger uses the conventional frag-grenade button from any hand position.
- **Smooth Turning:** proportional continuous turning replaces the default 45-degree snap turn.

Change an option and relaunch the game for it to take effect.

## Current known limitations

- The Campaign has not yet been completion-tested from beginning to end.
- Some scripted sequences, cutscenes, special weapons, or mission interactions may still need VR-specific fixes.
- Pimax Crystal Light and 32-bit OpenXR runtime compatibility remain unconfirmed.
- Quest 3 through Meta Quest Link remains under investigation; Quest 3 through Virtual Desktop is the currently verified Quest configuration.
- Rare, upgraded, and mission-specific weapons may still require individual adjustment.
- Local/offline Multiplayer remains experimental.
- Online Multiplayer is unsupported and untested.
- Haptic feedback is not currently part of the tested feature set.

## Reporting problems

Please report technical issues in the Patreon comments or send me a direct message. I will respond as quickly as possible.

Logs, screenshots, and recordings can also be sent through Discord or by email to `jplakon@gmail.com`.

Please include:

- World War VR version
- Campaign mission or Zombies map
- Weapon involved
- Headset and connection method
- Active OpenXR runtime
- GPU and driver version
- Launcher quality setting
- Steps that reproduce the issue
- `WorldWarVR.log`, if available
- A screenshot or short recording for visual problems

Thank you to everyone testing the mod and sending detailed reports. These reports are directly guiding each new build.

The download contains the portable build and its complete corresponding source and license information under GPLv3. It contains no Call of Duty game content.

More releases and project updates: https://www.patreon.com/J_Play/
