# World War VR v0.4.0-beta.1 — Campaign Complete, First Beta

World War VR is moving into beta, available to members at the **$3/month tier
and above**!

I've now completed the entire Campaign in VR. Playing through it has helped
me find and fix the mission weapons, vehicles, and support controls needed
along the way. This was a playthrough across the development builds leading
to beta.1; the latest pistol grip and manual reload corrections were tested
separately afterward. It isn't a claim that every weapon variant or headset
configuration has been tested.

## What's new since alpha.5

- **Black Cats aircraft guns:** aim the gunner station with your right
  controller and fire with right trigger. You can look around independently,
  and the gun retains its native movement limits.
- **Satchel charges:** the missing throw control is now connected. Hold Y and
  push the left stick up to select a satchel, hold right grip, then press and
  release left trigger to throw. Right trigger detonates the placed charges.
  This also works with Button Grenades enabled.
- **Shuri Castle airstrikes:** the targeting marker now follows your right
  controller. Hold Y and push the left stick left to select the radio, then
  release Y, hold right grip, aim, and use right trigger. The mission's valid
  strike areas still apply.
- **Campaign performance:** reduced repeated CPU work in the weapon and
  vehicle paths and removed unnecessary diagnostic work during normal play.
  The launcher now carries the tested Virtual Desktop frame-timing setting
  into normal Campaign/Zombies launches.
- **Two-hand pistols:** the support hand now closes beside the firing hand.
  Bringing the controllers close together no longer drops the pistol, and
  aiming remains stable through support-hand release and regripping.
- **Zombies Colt manual reload:** fixed the starting pistol falling back to
  automatic reload. Magazine insertion/release and the empty-reload slide
  action now work with the close two-hand grip.

Earlier improvements are included: stable weapon handling, physical scopes and
reloads, campaign mission unlocking, mounted machine guns, PTRS-41 support,
corrected launcher aiming, dedicated tank controls, firing rumble, and smoke
visible in both eyes.

## Pistol reload reminder

Leave **Automatic Reload** off for physical reloads. Press A to eject the
magazine, release the support grip, take a replacement with left grip at your
left hip, insert it into the pistol, and release it. After an empty reload,
use the free index trigger to grab the slide, pull it back, and release it
before firing. Bring the support hand back beside the firing hand afterward.

## Download and update

1. Close the game and old launcher.
2. Download `WorldWarVR-v0.4.0-beta.1-Patreon-Release.zip` and its matching
   SHA256 text attachment from this post.
3. Extract the release ZIP, then extract the **Portable ZIP inside it** into
   a new, empty folder outside the Call of Duty installation.
4. Connect and wake your headset and both controllers, with the intended
   OpenXR runtime ready.
5. Run `WorldWarVR.exe`, let it detect World at War, or browse to the folder
   containing `CoDWaW.exe`.
6. Choose **Main Menu** for Campaign/Zombies, select **Performance** as the
   recommended starting preset, and click **Launch in VR**.

You need your own legitimate, compatible World at War 1.7 installation.
The included Source ZIP is not needed to play. Controls, requirements,
troubleshooting, and the changelog are beside the launcher.

## Testing and reports

This remains a beta. Quest 3 through Virtual Desktop is my tested setup;
Pimax Crystal Light and reliable Meta Quest Link compatibility remain
unconfirmed. Local/offline Multiplayer is experimental; Online Multiplayer
is unsupported. Smoothness also depends on your system and streaming
connection: the remaining choppiness in my performance test cleared after
a router restart with the mod unchanged.

Please report problems in the Patreon comments or DM me. I'll be working
through your reports and responding as quickly as I can. Send logs,
screenshots, and recordings through Discord or **jplakon@gmail.com**. Include
the version, mission/map, weapon, headset, connection/runtime, GPU, quality
preset, what happened, and `WorldWarVR.log` when available.

Thank you for testing and sending reports. The download includes the portable
mod and complete matching GPLv3 source at no extra source-code fee. Recipients
retain the rights to modify and redistribute the covered software. No Call
of Duty game files are included.

More updates: https://www.patreon.com/J_Play/
