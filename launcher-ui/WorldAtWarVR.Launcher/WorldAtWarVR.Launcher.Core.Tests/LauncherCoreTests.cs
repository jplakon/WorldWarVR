using System.Text.Json;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using WorldAtWarVR.Launcher.Core;

namespace WorldAtWarVR.Launcher.Core.Tests;

[TestClass]
public sealed class LauncherCoreTests
{
    private const string MainMenuLanguageMarker = "nazi_zombie_prototype.ff";
    private static readonly string[] MultiplayerLanguageMarkers =
    [
        "code_post_gfx_mp.ff",
        "patch_mp.ff",
        "ui_mp.ff",
        "common_mp.ff",
        "localized_code_post_gfx_mp.ff",
        "localized_common_mp.ff",
    ];

    [TestMethod]
    public void NormalizeDirectory_PreservesDriveRoot()
    {
        Assert.AreEqual(
            @"E:\",
            GameInstallationValidator.NormalizeDirectory(@"E:\"));
        Assert.AreEqual(
            @"E:\",
            new LauncherSettings { GameDirectory = @"E:\" }.Normalize().GameDirectory);
    }

    [TestMethod]
    public void CommandBuilder_UsesOnlyAdjacentSupportFilesAndChosenSettings()
    {
        var appDirectory = Path.Combine(Path.GetTempPath(), "World War VR");
        var gameDirectory = Path.Combine(Path.GetTempPath(), "Call of Duty World at War");
        var settings = new LauncherSettings
        {
            GameDirectory = gameDirectory,
            LaunchTarget = GameLaunchTarget.Multiplayer,
            AutomaticBots = false,
            Resolution = VrResolutionPreset.HighQuality,
        };

        var command = LauncherCommandBuilder.Build(appDirectory, settings);

        Assert.AreEqual(
            Path.Combine(Path.GetFullPath(appDirectory), "wawvr-launcher.exe"),
            command.FileName);
        CollectionAssert.AreEqual(
            new[]
            {
                "--launch", "--multiplayer", "--game-dir",
                Path.GetFullPath(gameDirectory), "--resolution", "3744x2016",
                "--mod-dll", Path.Combine(Path.GetFullPath(appDirectory), "WorldWarVR.dll"),
                "--bots", "disabled",
            },
            command.Arguments.ToArray());
    }

    [TestMethod]
    public void CommandBuilder_UsesMenuAndNativeDefaults()
    {
        var settings = new LauncherSettings { GameDirectory = @"C:\Games\WaW" };
        var command = LauncherCommandBuilder.Build(@"C:\WorldWarVR", settings);

        CollectionAssert.Contains(command.Arguments.ToArray(), "--menu");
        CollectionAssert.Contains(command.Arguments.ToArray(), "6016x2688");
        CollectionAssert.Contains(command.Arguments.ToArray(), "enabled");
    }

    [TestMethod]
    public void CommandBuilder_PinsValidatedSplitInstallationExecutables()
    {
        var gameDirectory = Path.Combine(Path.GetTempPath(), "WaW game data");
        var spExecutable = Path.Combine(Path.GetTempPath(), "verified", "t4sp.exe");
        var mpExecutable = Path.Combine(Path.GetTempPath(), "verified", "t4mp.exe");
        var settings = new LauncherSettings
        {
            GameDirectory = gameDirectory,
            LaunchTarget = GameLaunchTarget.MainMenu,
        };
        var validation = new GameInstallationValidation(
            Path.GetFullPath(gameDirectory),
            true,
            true,
            true,
            SupportedGameBuild.CompatibleBuild,
            SupportedGameBuild.CompatibleBuild,
            "ready",
            spExecutable,
            mpExecutable,
            new InstalledGameLanguage("english", "English"),
            true,
            true,
            true);

        var command = LauncherCommandBuilder.Build(
            Path.GetTempPath(),
            settings,
            validation);

        CollectionAssert.AreEqual(
            new[]
            {
                "--source-exe", Path.GetFullPath(spExecutable),
                "--mp-source-exe", Path.GetFullPath(mpExecutable),
            },
            command.Arguments.TakeLast(4).ToArray());

        var multiplayerCommand = LauncherCommandBuilder.Build(
            Path.GetTempPath(),
            settings with { LaunchTarget = GameLaunchTarget.Multiplayer },
            validation);
        CollectionAssert.AreEqual(
            new[] { "--source-exe", Path.GetFullPath(mpExecutable) },
            multiplayerCommand.Arguments.TakeLast(2).ToArray());
        CollectionAssert.DoesNotContain(
            multiplayerCommand.Arguments.ToArray(),
            "--mp-source-exe");
    }

    [TestMethod]
    public async Task SettingsStore_RoundTripsEnumsAndNormalizesPath()
    {
        var root = Path.Combine(Path.GetTempPath(), $"wawvr-settings-{Guid.NewGuid():N}");
        var path = Path.Combine(root, "launcher-settings.json");
        try
        {
            var store = new LauncherSettingsStore(path);
            await store.SaveAsync(new LauncherSettings
            {
                GameDirectory = @"C:\Games\WaW\",
                LaunchTarget = GameLaunchTarget.Multiplayer,
                AutomaticBots = false,
                QuestAirLinkCompatibility = true,
                AutomaticReload = true,
                ButtonGrenades = true,
                SmoothTurning = true,
                Resolution = VrResolutionPreset.Performance,
            });

            var loaded = await store.LoadAsync();
            Assert.AreEqual(@"C:\Games\WaW", loaded.GameDirectory);
            Assert.AreEqual(GameLaunchTarget.Multiplayer, loaded.LaunchTarget);
            Assert.IsFalse(loaded.AutomaticBots);
            Assert.IsTrue(loaded.QuestAirLinkCompatibility);
            Assert.IsTrue(loaded.AutomaticReload);
            Assert.IsTrue(loaded.ButtonGrenades);
            Assert.IsTrue(loaded.SmoothTurning);
            Assert.AreEqual(VrResolutionPreset.Performance, loaded.Resolution);

            var json = await File.ReadAllTextAsync(path);
            StringAssert.Contains(json, "Multiplayer");
            StringAssert.Contains(json, "Performance");
        }
        finally
        {
            if (Directory.Exists(root))
            {
                Directory.Delete(root, recursive: true);
            }
        }
    }

    [TestMethod]
    public async Task SettingsStore_MigratesVersionOneAndDiscardsLegacyUpdaterState()
    {
        var root = Path.Combine(Path.GetTempPath(), $"wawvr-settings-migration-{Guid.NewGuid():N}");
        var path = Path.Combine(root, "launcher-settings.json");
        try
        {
            Directory.CreateDirectory(root);
            await File.WriteAllTextAsync(path, """
                {
                  "Version": 1,
                  "GameDirectory": "C:\\\\Games\\\\WaW\\\\",
                  "LaunchTarget": "MainMenu",
                  "AutomaticBots": true,
                  "Resolution": "Recommended",
                  "LastUpdateCheckUtc": "2026-08-17T12:00:00+00:00",
                  "UpdatePromptSnoozedUntilUtc": "2026-09-16T12:00:00+00:00",
                  "CachedUpdate": {
                    "TagName": "v0.5.0-alpha.2",
                    "Version": "0.5.0-alpha.2",
                    "Title": "Legacy update",
                    "ReleaseNotes": "Legacy release notes",
                    "ReleasePageUrl": "https://updates.example.invalid/v0.5.0-alpha.2"
                  }
                }
                """);
            var store = new LauncherSettingsStore(path);
            var migrated = await store.LoadAsync();

            Assert.AreEqual(5, migrated.Version);
            Assert.AreEqual(@"C:\Games\WaW", migrated.GameDirectory);
            Assert.IsFalse(migrated.QuestAirLinkCompatibility);
            Assert.IsFalse(migrated.AutomaticReload);
            Assert.IsFalse(migrated.ButtonGrenades);
            Assert.IsFalse(migrated.SmoothTurning);

            await store.SaveAsync(migrated);
            var saved = await File.ReadAllTextAsync(path);
            Assert.IsFalse(saved.Contains("LastUpdateCheckUtc", StringComparison.Ordinal));
            Assert.IsFalse(saved.Contains("UpdatePromptSnoozedUntilUtc", StringComparison.Ordinal));
            Assert.IsFalse(saved.Contains("CachedUpdate", StringComparison.Ordinal));
        }
        finally
        {
            if (Directory.Exists(root))
            {
                Directory.Delete(root, recursive: true);
            }
        }
    }

    [TestMethod]
    public void SteamVrAirLinkCompatibility_SelectsRegisteredX86RuntimeAndCurrentMetaRoot()
    {
        var root = Path.Combine(Path.GetTempPath(), $"wawvr-air-link-{Guid.NewGuid():N}");
        var steamVrDirectory = Path.Combine(root, "moved-steam", "SteamVR");
        var metaDirectory = Path.Combine(root, "moved-meta", "Meta Horizon");
        var manifest = Path.Combine(steamVrDirectory, "steamxr_win32.json");
        try
        {
            Directory.CreateDirectory(steamVrDirectory);
            Directory.CreateDirectory(metaDirectory);
            File.WriteAllText(manifest, "{}");

            Assert.AreEqual(
                Path.GetFullPath(manifest),
                SteamVrAirLinkCompatibility.SelectSteamVrX86Manifest(
                    new[]
                    {
                        Path.Combine(root, "steamxr_win64.json"),
                        manifest,
                    }));
            Assert.AreEqual(
                Path.GetFullPath(metaDirectory),
                SteamVrAirLinkCompatibility.SelectCurrentOculusBase(
                    metaDirectory,
                    Path.Combine(root, "stale-meta")));
        }
        finally
        {
            if (Directory.Exists(root))
            {
                Directory.Delete(root, recursive: true);
            }
        }
    }

    [TestMethod]
    public async Task SettingsStore_ReturnsDefaultsForMalformedJson()
    {
        var root = Path.Combine(Path.GetTempPath(), $"wawvr-settings-{Guid.NewGuid():N}");
        var path = Path.Combine(root, "launcher-settings.json");
        try
        {
            Directory.CreateDirectory(root);
            await File.WriteAllTextAsync(path, "{ not json");
            var loaded = await new LauncherSettingsStore(path).LoadAsync();
            Assert.AreEqual(new LauncherSettings(), loaded);
        }
        finally
        {
            if (Directory.Exists(root))
            {
                Directory.Delete(root, recursive: true);
            }
        }
    }

    [TestMethod]
    public async Task Validator_RejectsFolderWithoutGameLayoutBeforeHashing()
    {
        var root = Path.Combine(Path.GetTempPath(), $"wawvr-invalid-{Guid.NewGuid():N}");
        try
        {
            Directory.CreateDirectory(root);
            var result = await GameInstallationValidator.ValidateAsync(root);
            Assert.IsFalse(result.MainMenuReady);
            Assert.IsFalse(result.MultiplayerReady);
            StringAssert.Contains(result.Detail, "main, zone");
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [TestMethod]
    public void Validator_UsesOnlyExactLegacyCompatibleFallbackLocations()
    {
        var localApplicationData = Path.Combine(Path.GetTempPath(), "LocalAppData");

        CollectionAssert.AreEqual(
            new[]
            {
                Path.Combine(localApplicationData, "Plutonium", "games", "t4sp.exe"),
                Path.Combine(
                    localApplicationData,
                    "WaWVR",
                    "runtime",
                    "waw-1.7.1263",
                    "CoDWaW.exe"),
            },
            GameInstallationValidator.GetManagedFallbackExecutablePaths(
                localApplicationData,
                GameLaunchTarget.MainMenu).ToArray());
        CollectionAssert.AreEqual(
            new[]
            {
                Path.Combine(localApplicationData, "Plutonium", "games", "t4mp.exe"),
                Path.Combine(
                    localApplicationData,
                    "WaWVR",
                    "runtime",
                    "waw-mp-1.7.1263",
                    "CoDWaWmp.exe"),
            },
            GameInstallationValidator.GetManagedFallbackExecutablePaths(
                localApplicationData,
                GameLaunchTarget.Multiplayer).ToArray());
    }

    [TestMethod]
    public async Task Validator_RestoresSplitInstallWithoutWeakeningDirectExecutableChecks()
    {
        var root = Path.Combine(Path.GetTempPath(), $"wawvr-split-{Guid.NewGuid():N}");
        var gameDirectory = Path.Combine(root, "game-data");
        var localApplicationData = Path.Combine(root, "local-app-data");
        try
        {
            Directory.CreateDirectory(Path.Combine(gameDirectory, "main"));
            CreateCompleteLanguageZone(gameDirectory, "english");
            await File.WriteAllTextAsync(
                Path.Combine(gameDirectory, "localization.txt"),
                "english\r\n\r\nWIN_ERROR\r\n\"Fatal Error\"\r\n");

            var directSp = Path.Combine(gameDirectory, "CoDWaW.exe");
            var directMp = Path.Combine(gameDirectory, "CoDWaWmp.exe");
            var fallbackSp = GameInstallationValidator
                .GetManagedFallbackExecutablePaths(
                    localApplicationData,
                    GameLaunchTarget.MainMenu)[0];
            var fallbackMp = GameInstallationValidator
                .GetManagedFallbackExecutablePaths(
                    localApplicationData,
                    GameLaunchTarget.Multiplayer)[0];
            var hashes = new Dictionary<string, string>(
                StringComparer.OrdinalIgnoreCase)
            {
                [Path.Combine(gameDirectory, "binkw32.dll")] =
                    GameInstallationValidator.SupportedBinkSha256,
                [fallbackSp] = GameInstallationValidator.CompatibleSinglePlayerSha256,
                [fallbackMp] = GameInstallationValidator.CompatibleMultiplayerSha256,
            };

            Task<string?> HashFile(string path, CancellationToken cancellationToken)
            {
                cancellationToken.ThrowIfCancellationRequested();
                return Task.FromResult(
                    hashes.TryGetValue(Path.GetFullPath(path), out var hash)
                        ? hash
                        : null);
            }

            var split = await GameInstallationValidator.ValidateAsync(
                gameDirectory,
                CancellationToken.None,
                localApplicationData,
                HashFile);
            Assert.IsTrue(split.MainMenuReady);
            Assert.IsTrue(split.MultiplayerReady);
            Assert.AreEqual(Path.GetFullPath(fallbackSp), split.SinglePlayerExecutable);
            Assert.AreEqual(Path.GetFullPath(fallbackMp), split.MultiplayerExecutable);

            hashes[directSp] = GameInstallationValidator.SteamSinglePlayerSha256;
            hashes[directMp] = GameInstallationValidator.SteamMultiplayerSha256;
            var completeSteam = await GameInstallationValidator.ValidateAsync(
                gameDirectory,
                CancellationToken.None,
                localApplicationData,
                HashFile);
            Assert.AreEqual(
                SupportedGameBuild.SteamBuild252004,
                completeSteam.SinglePlayerBuild);
            Assert.AreEqual(
                SupportedGameBuild.SteamBuild252004,
                completeSteam.MultiplayerBuild);
            Assert.AreEqual(Path.GetFullPath(directSp), completeSteam.SinglePlayerExecutable);
            Assert.AreEqual(Path.GetFullPath(directMp), completeSteam.MultiplayerExecutable);

            hashes[directSp] = "UNSUPPORTED-SP";
            hashes[directMp] = "UNSUPPORTED-MP";
            var unsupportedDirect = await GameInstallationValidator.ValidateAsync(
                gameDirectory,
                CancellationToken.None,
                localApplicationData,
                HashFile);
            Assert.IsFalse(unsupportedDirect.MainMenuReady);
            Assert.IsFalse(unsupportedDirect.MultiplayerReady);
            Assert.AreEqual(Path.GetFullPath(directSp), unsupportedDirect.SinglePlayerExecutable);
            Assert.AreEqual(Path.GetFullPath(directMp), unsupportedDirect.MultiplayerExecutable);
        }
        finally
        {
            if (Directory.Exists(root))
            {
                Directory.Delete(root, recursive: true);
            }
        }
    }

    [TestMethod]
    public void ResolutionChoices_HaveStableProductPresets()
    {
        CollectionAssert.AreEqual(
            new[] { "6016x2688", "3744x2016", "2560x1440" },
            ResolutionChoices.All.Select(choice => choice.Dimensions).ToArray());
        CollectionAssert.AreEqual(
            new[] { "Native + scopes", "Performance", "Recovery" },
            ResolutionChoices.All.Select(choice => choice.Name).ToArray());

        Assert.AreEqual(VrResolutionPreset.Recommended, ResolutionChoices.All[0].Preset);
        Assert.AreEqual(VrResolutionPreset.HighQuality, ResolutionChoices.All[1].Preset);
        Assert.AreEqual(VrResolutionPreset.Performance, ResolutionChoices.All[2].Preset);
        StringAssert.Contains(ResolutionChoices.All[0].Detail, "2496 x 2688");
        StringAssert.Contains(ResolutionChoices.All[1].Detail, "1872 x 2016");
        StringAssert.Contains(ResolutionChoices.All[2].Detail, "compatibility");
    }

    [TestMethod]
    public void LanguageSelector_RecognizesOnlyOfficialSteamLanguages()
    {
        var cases = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["english"] = "English",
            ["french"] = "French",
            ["italian"] = "Italian",
            ["german"] = "German",
            ["spanish"] = "Spanish (Spain)",
        };
        foreach (var languageCase in cases)
        {
            var parsed = GameInstallationValidator.ParseInstalledLanguageSelector(
                $"\r\n\t\r\n{languageCase.Key}\r\nlocalized contents");
            Assert.AreEqual(languageCase.Key, parsed.Code);
            Assert.AreEqual(languageCase.Value, parsed.DisplayName);
        }

        foreach (var invalid in new[]
                 {
                     string.Empty,
                     "\r\n\t\r\n",
                     "English\r\n",
                     " french\r\n",
                     "french \r\n",
                     "../english\r\n",
                     "french/../english\r\n",
                     "polish\r\n",
                     "\uFEFFfrench\r\n",
                     "french\0english",
                 })
        {
            Assert.ThrowsException<InvalidDataException>(() =>
                GameInstallationValidator.ParseInstalledLanguageSelector(invalid));
        }
    }

    [TestMethod]
    public void OpenXrRuntimeCompatibility_RepairsActivePimaxToVerifiedX86Sibling()
    {
        var root = Path.Combine(Path.GetTempPath(), $"wawvr-pimax-{Guid.NewGuid():N}");
        var activePimax64 = Path.Combine(root, "PiOpenXR_64.json");
        var pimaxX86 = Path.Combine(root, "PiOpenXR_32.json");
        var runtimeDll = Path.Combine(root, "PimaxOpenXR.dll");
        try
        {
            Directory.CreateDirectory(root);
            File.WriteAllText(activePimax64, "{}");
            File.WriteAllText(pimaxX86, """
                {
                  "file_format_version": "1.0.0",
                  "runtime": { "library_path": "PimaxOpenXR.dll" }
                }
                """);
            WritePeImage(runtimeDll, 0x014c);

            var selected = OpenXrRuntimeCompatibility.SelectLaunchRuntime(
                null,
                activePimax64,
                null,
                activePimax64,
                null);

            Assert.AreEqual("pimax-x86", selected.Kind);
            Assert.AreEqual(Path.GetFullPath(pimaxX86), selected.OverrideManifest);
            Assert.AreEqual(
                Path.GetFullPath(pimaxX86),
                selected.EnvironmentVariables[
                    OpenXrRuntimeCompatibility.RuntimeManifestEnvironmentVariable]);
            Assert.AreEqual(
                "pimax-x86",
                selected.EnvironmentVariables[
                    OpenXrRuntimeCompatibility.RuntimeSelectionEnvironmentVariable]);
        }
        finally
        {
            if (Directory.Exists(root))
            {
                Directory.Delete(root, recursive: true);
            }
        }
    }

    [TestMethod]
    public void OpenXrRuntimeCompatibility_PreservesExplicitAndNonPimaxChoices()
    {
        var explicitSelection = OpenXrRuntimeCompatibility.SelectLaunchRuntime(
            @"C:\Runtime\explicit.json",
            @"C:\Program Files\Pimax\Runtime\PiOpenXR_64.json",
            null,
            null,
            null);
        Assert.AreEqual("explicit", explicitSelection.Kind);
        Assert.IsNull(explicitSelection.OverrideManifest);
        Assert.IsFalse(
            explicitSelection.EnvironmentVariables.ContainsKey(
                OpenXrRuntimeCompatibility.RuntimeManifestEnvironmentVariable));

        var nonPimaxSelection = OpenXrRuntimeCompatibility.SelectLaunchRuntime(
            null,
            @"C:\OtherRuntime\active_runtime.json",
            @"C:\Program Files\Pimax\Runtime\PiOpenXR_64.json",
            @"C:\Program Files\Pimax\Runtime\PiOpenXR_64.json",
            null);
        Assert.AreEqual("system-default", nonPimaxSelection.Kind);
        Assert.IsNull(nonPimaxSelection.OverrideManifest);
    }

    [TestMethod]
    public void OpenXrRuntimeCompatibility_RejectsPimaxManifestWithNonX86Library()
    {
        var root = Path.Combine(Path.GetTempPath(), $"wawvr-pimax-x64-{Guid.NewGuid():N}");
        var activePimax64 = Path.Combine(root, "PiOpenXR_64.json");
        var pimaxX86 = Path.Combine(root, "PiOpenXR_32.json");
        var runtimeDll = Path.Combine(root, "PimaxOpenXR.dll");
        try
        {
            Directory.CreateDirectory(root);
            File.WriteAllText(activePimax64, "{}");
            File.WriteAllText(pimaxX86, """
                {
                  "file_format_version": "1.0.0",
                  "runtime": { "library_path": "PimaxOpenXR.dll" }
                }
                """);
            WritePeImage(runtimeDll, 0x8664);

            var exception = Assert.ThrowsException<InvalidOperationException>(() =>
                OpenXrRuntimeCompatibility.SelectLaunchRuntime(
                    null,
                    activePimax64,
                    null,
                    null,
                    null));
            StringAssert.Contains(exception.Message, "not a 32-bit x86 image");
        }
        finally
        {
            if (Directory.Exists(root))
            {
                Directory.Delete(root, recursive: true);
            }
        }
    }

    [TestMethod]
    public async Task Validator_ReReadsSteamLanguageAndRequiresItsZoneDirectory()
    {
        var root = Path.Combine(
            Path.GetTempPath(), $"wawvr-language-{Guid.NewGuid():N}");
        try
        {
            Directory.CreateDirectory(Path.Combine(root, "main"));
            CreateCompleteLanguageZone(root, "english");
            CreateCompleteLanguageZone(root, "french");
            var localization = Path.Combine(root, "localization.txt");
            await File.WriteAllTextAsync(localization, "english\r\n");

            var hashes = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                [Path.Combine(root, "binkw32.dll")] =
                    GameInstallationValidator.SupportedBinkSha256,
                [Path.Combine(root, "CoDWaW.exe")] =
                    GameInstallationValidator.SteamSinglePlayerSha256,
                [Path.Combine(root, "CoDWaWmp.exe")] =
                    GameInstallationValidator.SteamMultiplayerSha256,
            };
            Task<string?> HashFile(string path, CancellationToken cancellationToken)
            {
                cancellationToken.ThrowIfCancellationRequested();
                return Task.FromResult(
                    hashes.TryGetValue(Path.GetFullPath(path), out var hash)
                        ? hash
                        : null);
            }

            var english = await GameInstallationValidator.ValidateAsync(
                root,
                CancellationToken.None,
                root,
                HashFile);
            Assert.IsTrue(english.MainMenuReady);
            Assert.IsTrue(english.MultiplayerReady);
            Assert.AreEqual("english", english.InstalledLanguage?.Code);
            Assert.AreEqual("English", english.InstalledLanguage?.DisplayName);

            await File.WriteAllTextAsync(localization, "french\r\n");
            var french = await GameInstallationValidator.ValidateAsync(
                root,
                CancellationToken.None,
                root,
                HashFile);
            Assert.IsTrue(french.MainMenuReady);
            Assert.IsTrue(french.MultiplayerReady);
            Assert.AreEqual("french", french.InstalledLanguage?.Code);
            Assert.AreEqual("French", french.InstalledLanguage?.DisplayName);
            StringAssert.Contains(french.ReadySummary, "French");

            var frenchZone = Path.Combine(root, "zone", "french");
            File.Delete(Path.Combine(frenchZone, MainMenuLanguageMarker));
            var missingMainMenuMarker = await GameInstallationValidator.ValidateAsync(
                root,
                CancellationToken.None,
                root,
                HashFile);
            Assert.IsFalse(missingMainMenuMarker.MainMenuReady);
            Assert.IsTrue(missingMainMenuMarker.MultiplayerReady);
            Assert.IsFalse(missingMainMenuMarker.MainMenuLanguageAssetsPresent);
            Assert.IsTrue(missingMainMenuMarker.MultiplayerLanguageAssetsPresent);
            File.WriteAllText(
                Path.Combine(frenchZone, MainMenuLanguageMarker),
                "fixture");

            foreach (var multiplayerMarker in MultiplayerLanguageMarkers)
            {
                var markerPath = Path.Combine(frenchZone, multiplayerMarker);
                File.Delete(markerPath);
                var missingMultiplayerMarker = await
                    GameInstallationValidator.ValidateAsync(
                        root,
                        CancellationToken.None,
                        root,
                        HashFile);
                Assert.IsTrue(
                    missingMultiplayerMarker.MainMenuReady,
                    $"Removing {multiplayerMarker} incorrectly disabled the main menu.");
                Assert.IsFalse(
                    missingMultiplayerMarker.MultiplayerReady,
                    $"Multiplayer remained ready without {multiplayerMarker}.");
                Assert.IsTrue(
                    missingMultiplayerMarker.MainMenuLanguageAssetsPresent);
                Assert.IsFalse(
                    missingMultiplayerMarker.MultiplayerLanguageAssetsPresent);
                File.WriteAllText(markerPath, "fixture");
            }

            Directory.CreateDirectory(Path.Combine(root, "zone", "german"));
            await File.WriteAllTextAsync(localization, "german\r\n");
            var emptyGermanZone = await GameInstallationValidator.ValidateAsync(
                root,
                CancellationToken.None,
                root,
                HashFile);
            Assert.IsTrue(emptyGermanZone.LanguageAssetsPresent);
            Assert.IsFalse(emptyGermanZone.MainMenuLanguageAssetsPresent);
            Assert.IsFalse(emptyGermanZone.MultiplayerLanguageAssetsPresent);
            Assert.IsFalse(emptyGermanZone.MainMenuReady);
            Assert.IsFalse(emptyGermanZone.MultiplayerReady);
            StringAssert.Contains(emptyGermanZone.ReadySummary, "German game data is incomplete");

            await File.WriteAllTextAsync(localization, "french\r\n");

            Directory.Delete(Path.Combine(root, "zone", "french"), recursive: true);
            var missingFrenchAssets = await GameInstallationValidator.ValidateAsync(
                root,
                CancellationToken.None,
                root,
                HashFile);
            Assert.IsFalse(missingFrenchAssets.MainMenuReady);
            Assert.AreEqual("french", missingFrenchAssets.InstalledLanguage?.Code);
            Assert.IsFalse(missingFrenchAssets.LanguageAssetsPresent);
            StringAssert.Contains(missingFrenchAssets.Detail, "zone\\french");

            await File.WriteAllTextAsync(localization, "../english\r\n");
            var unsafeSelector = await GameInstallationValidator.ValidateAsync(
                root,
                CancellationToken.None,
                root,
                HashFile);
            Assert.IsFalse(unsafeSelector.MainMenuReady);
            Assert.IsNull(unsafeSelector.InstalledLanguage);
            StringAssert.Contains(unsafeSelector.Detail, "unsupported installed language");
        }
        finally
        {
            if (Directory.Exists(root))
            {
                Directory.Delete(root, recursive: true);
            }
        }
    }

    [TestMethod]
    public void GameplayOptionEnvironment_IsExplicitAndPreservesManualGameplayDefaults()
    {
        Assert.AreEqual("WAWVR_TURN_MODE", GameplayOptionEnvironment.TurnModeVariable);
        var defaults = GameplayOptionEnvironment.Resolve(new LauncherSettings());
        Assert.AreEqual("0", defaults[GameplayOptionEnvironment.AutomaticReloadVariable]);
        Assert.AreEqual("0", defaults[GameplayOptionEnvironment.ButtonGrenadesVariable]);
        Assert.AreEqual("snap", defaults[GameplayOptionEnvironment.TurnModeVariable]);
        Assert.AreEqual("1", defaults[GameplayOptionEnvironment.DeferXrBeginFrameVariable]);
        Assert.AreEqual(4, defaults.Count);

        var enabled = GameplayOptionEnvironment.Resolve(new LauncherSettings
        {
            AutomaticReload = true,
            ButtonGrenades = true,
            SmoothTurning = true,
        });
        Assert.AreEqual("1", enabled[GameplayOptionEnvironment.AutomaticReloadVariable]);
        Assert.AreEqual("1", enabled[GameplayOptionEnvironment.ButtonGrenadesVariable]);
        Assert.AreEqual("smooth", enabled[GameplayOptionEnvironment.TurnModeVariable]);
        Assert.AreEqual("1", enabled[GameplayOptionEnvironment.DeferXrBeginFrameVariable]);
        Assert.AreEqual(4, enabled.Count);
    }

    [DataTestMethod]
    [DataRow(GameLaunchTarget.MainMenu, "1")]
    [DataRow(GameLaunchTarget.Multiplayer, "0")]
    [DataRow((GameLaunchTarget)999, "0")]
    public void GameplayOptionEnvironment_RestrictsDeferredFrameBeginToSinglePlayer(
        GameLaunchTarget target,
        string expected)
    {
        Assert.AreEqual("WAWVR_DEFER_XR_BEGIN_FRAME", GameplayOptionEnvironment.DeferXrBeginFrameVariable);
        var environment = GameplayOptionEnvironment.Resolve(new LauncherSettings
        {
            LaunchTarget = target,
        });

        Assert.AreEqual(expected, environment[GameplayOptionEnvironment.DeferXrBeginFrameVariable]);
        Assert.IsFalse(environment.ContainsKey("WAWVR_DEFER_XR_BEGIN_FRAME_SIMULATOR_TEST"));
    }

    [TestMethod]
    public void CandidateSelection_SkipsStalePathsAndPrefersCompleteInstallation()
    {
        var stale = new GameInstallationValidation(
            @"C:\Stale", false, false, false,
            SupportedGameBuild.None, SupportedGameBuild.None, "missing");
        var mainOnly = new GameInstallationValidation(
            @"D:\MainOnly", true, true, true,
            SupportedGameBuild.SteamBuild252004, SupportedGameBuild.None, "main only",
            InstalledLanguage: new InstalledGameLanguage("english", "English"),
            LanguageAssetsPresent: true,
            MainMenuLanguageAssetsPresent: true);
        var complete = new GameInstallationValidation(
            @"E:\Steam\steamapps\common\Call of Duty World at War",
            true, true, true,
            SupportedGameBuild.SteamBuild252004,
            SupportedGameBuild.SteamBuild252004,
            "complete",
            InstalledLanguage: new InstalledGameLanguage("french", "French"),
            LanguageAssetsPresent: true,
            MainMenuLanguageAssetsPresent: true,
            MultiplayerLanguageAssetsPresent: true);

        var selected = GameInstallationValidator.ChooseBestCandidate(
            [stale, mainOnly, complete]);

        Assert.AreSame(complete, selected);
    }

    private static void CreateCompleteLanguageZone(
        string gameDirectory,
        string language)
    {
        var languageZone = Path.Combine(gameDirectory, "zone", language);
        Directory.CreateDirectory(languageZone);
        File.WriteAllText(
            Path.Combine(languageZone, MainMenuLanguageMarker),
            "fixture");
        foreach (var marker in MultiplayerLanguageMarkers)
        {
            File.WriteAllText(Path.Combine(languageZone, marker), "fixture");
        }
    }

    private static void WritePeImage(string path, ushort machine)
    {
        var bytes = new byte[256];
        bytes[0] = (byte)'M';
        bytes[1] = (byte)'Z';
        BitConverter.GetBytes(128).CopyTo(bytes, 0x3c);
        bytes[128] = (byte)'P';
        bytes[129] = (byte)'E';
        BitConverter.GetBytes(machine).CopyTo(bytes, 132);
        File.WriteAllBytes(path, bytes);
    }
}
