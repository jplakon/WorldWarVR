# Rifle-grenade Garand reload and mode-switch acceptance

## Exact variant

- Rifle: `m1garand_gl`.
- Rifle viewmodel: `viewmodel_usa_m1garand_rifle_grenade_mount`.
- Alternate mode: `m7_launcher`.
- Manual-reload profile: `M1GarandGrenadeLauncher` (ID 45), using the Garand's
  eight-round en-bloc contract with exact model topology validation.
- Focused development build: `out/garand-gl-auto`.

The test equipped the loaded rifle/M7 pair in an isolated Campaign test
environment. This does not grant the pair during normal Campaign play.

## Simulator evidence

The exact rifle identity and model were confirmed before testing. The rifle
fired from eight rounds to empty and locked open. A began the physical reload
without adding ammunition. A waist grab produced the clip, insertion/release
committed once (reserve 64 to 56, clip zero to eight), the action closed, and
the next shot reduced the clip from eight to seven.

The native weapon selector now preserves the old-to-new selection transition
instead of prewriting the new weapon ID before the native selector runs.
Plain Y was separately checked to select `m7_launcher` from `m1garand_gl` and
return to the rifle on a second tap. Ordinary weapons keep next-weapon
switching. Focused profile, reload, charging, input, and handoff regression
checks passed before the final headset handoff.

## Physical-headset acceptance

The user first accepted the rifle's reload but reported that plain Y selected
another weapon instead of the launcher. The subsequent mode-switch fix was
tested in the connected headset using Virtual Desktop after a fresh `ready`.
The requested checklist was:

1. Grip the Garand and tap Y to enter rifle-grenade mode.
2. Fire with the right trigger.
3. Tap Y again to return to rifle mode.
4. Confirm the manual rifle reload still works.

The user replied, `everything still works. Let's do another patreon release.`
This accepts the focused rifle reload and rifle/M7 mode-switch behavior on
the user's setup. It is not a claim of a manual M7 grenade reload: grenade
mode retains its native reload behavior. Customer hardware acceptance,
other rifle-grenade variants, and a complete replay of the final release
remain separate checks.
