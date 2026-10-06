WORLD WAR VR - VALVE INDEX / STEAMVR DIAGNOSTIC BUILD

This build is for the report where World at War has sound but no image in a
Valve Index. It enables bounded OpenXR frame/orientation diagnostics while
keeping the normal game and controller behavior.

1. Extract the entire ZIP to a new folder. Do not overwrite an older build.
2. Start Steam and SteamVR. Confirm the Index is awake and SteamVR says Ready.
3. Double-click START-DIAGNOSTIC.cmd.
4. In the World War VR launcher, select the correct World at War folder.
5. ENABLE "Quest Air Link compatibility". The old label is misleading: for
   this test it explicitly selects SteamVR's required 32-bit OpenXR runtime.
6. Select Performance quality and launch Main Menu.
7. Wait at least 20 seconds. If the menu is visible, enter gameplay briefly.
8. Close World at War and the World War VR launcher.
9. Double-click COLLECT-DIAGNOSTICS.cmd.
10. Send the newly created WorldWarVR-IndexSteamVR-Diagnostics-*.zip file.

Please report separately whether sound was present, whether either eye showed
an image, and whether SteamVR reported that World War VR was running.

