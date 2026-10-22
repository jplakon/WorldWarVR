using System.Security.Cryptography;
using System.Text;

namespace WorldAtWarVR.Launcher.Core;

public enum SupportedGameBuild
{
    None,
    SteamBuild252004,
    CompatibleBuild,
}

public sealed record InstalledGameLanguage(string Code, string DisplayName);

public sealed record GameInstallationValidation(
    string Directory,
    bool DirectoryExists,
    bool DataFoldersPresent,
    bool RuntimeSupported,
    SupportedGameBuild SinglePlayerBuild,
    SupportedGameBuild MultiplayerBuild,
    string Detail,
    string? SinglePlayerExecutable = null,
    string? MultiplayerExecutable = null,
    InstalledGameLanguage? InstalledLanguage = null,
    bool LanguageAssetsPresent = false,
    bool MainMenuLanguageAssetsPresent = false,
    bool MultiplayerLanguageAssetsPresent = false)
{
    public bool LanguageReady => InstalledLanguage is not null && LanguageAssetsPresent;

    public bool MainMenuReady =>
        DirectoryExists && DataFoldersPresent && LanguageReady &&
        MainMenuLanguageAssetsPresent && RuntimeSupported &&
        SinglePlayerBuild != SupportedGameBuild.None;

    public bool MultiplayerReady =>
        DirectoryExists && DataFoldersPresent && LanguageReady &&
        MultiplayerLanguageAssetsPresent && RuntimeSupported &&
        MultiplayerBuild != SupportedGameBuild.None;

    public bool IsReadyFor(GameLaunchTarget target) =>
        target == GameLaunchTarget.Multiplayer ? MultiplayerReady : MainMenuReady;

    public string ReadySummary => (MainMenuReady, MultiplayerReady) switch
    {
        (true, true) => $"Compatible {InstalledLanguage!.DisplayName} installation found. Zombies, Campaign, and Multiplayer are ready.",
        (true, false) => MultiplayerBuild == SupportedGameBuild.None
            ? $"Compatible {InstalledLanguage!.DisplayName} installation found. Zombies and Campaign are ready; Multiplayer needs a supported CoDWaWmp.exe."
            : $"Zombies and Campaign are ready, but the {InstalledLanguage!.DisplayName} Multiplayer data is incomplete. Verify the game files in Steam.",
        (false, true) => SinglePlayerBuild == SupportedGameBuild.None
            ? $"The {InstalledLanguage!.DisplayName} Multiplayer installation is ready, but Zombies and Campaign need a supported CoDWaW.exe."
            : $"Multiplayer is ready, but the {InstalledLanguage!.DisplayName} Zombies/Campaign data is incomplete. Verify the game files in Steam.",
        (false, false) when LanguageReady && RuntimeSupported &&
            (SinglePlayerBuild != SupportedGameBuild.None ||
             MultiplayerBuild != SupportedGameBuild.None) &&
            (!MainMenuLanguageAssetsPresent || !MultiplayerLanguageAssetsPresent) =>
            $"The {InstalledLanguage!.DisplayName} game data is incomplete. Verify the game files in Steam, then try again.",
        _ => Detail,
    };
}

public static class GameInstallationValidator
{
    private const int MaximumLocalizationSelectorBytes = 64 * 1024;
    private static readonly IReadOnlyDictionary<string, string> SupportedLanguages =
        new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["english"] = "English",
            ["french"] = "French",
            ["italian"] = "Italian",
            ["german"] = "German",
            ["spanish"] = "Spanish (Spain)",
        };
    private static readonly string[] MultiplayerLanguageMarkers =
    [
        "code_post_gfx_mp.ff",
        "patch_mp.ff",
        "ui_mp.ff",
        "common_mp.ff",
        "localized_code_post_gfx_mp.ff",
        "localized_common_mp.ff",
    ];

    public const string SteamSinglePlayerSha256 =
        "732900D158982C33E3121F0B86D22230BE79839BBCBFE3BDFC1238F408A7D64D";
    public const string SteamMultiplayerSha256 =
        "7D0B518A4BD267FFDB6D0203AD8F3721603B172AC13BA2ABCDB32584F759D36C";
    public const string CompatibleSinglePlayerSha256 =
        "F26D45524BFFF7E44C8EBAB4D758CA524EDFB0FB7D52352B6C95E1E908799361";
    public const string CompatibleMultiplayerSha256 =
        "943BB93001AD2ED465B6652C27FB649B5F0C5B24097E18A27A588AC35B3457A0";
    public const string SupportedBinkSha256 =
        "1BA84B170EFBA8FB99B48E2AC0DED52D14B9F6CE657B65748E9A8385C3AB51BB";

    public static async Task<GameInstallationValidation> ValidateAsync(
        string? directory,
        CancellationToken cancellationToken = default,
        string? localApplicationDataDirectory = null,
        Func<string, CancellationToken, Task<string?>>? fileHasher = null)
    {
        fileHasher ??= HashIfPresentAsync;
        var normalized = NormalizeDirectory(directory);
        if (string.IsNullOrWhiteSpace(normalized))
        {
            return Invalid(normalized, "Select your Call of Duty: World at War installation folder.");
        }

        if (!System.IO.Directory.Exists(normalized))
        {
            return Invalid(normalized, "That folder does not exist.");
        }

        var mainDirectory = Path.Combine(normalized, "main");
        var zoneDirectory = Path.Combine(normalized, "zone");
        var dataFoldersPresent = System.IO.Directory.Exists(mainDirectory) &&
            System.IO.Directory.Exists(zoneDirectory);
        if (!dataFoldersPresent)
        {
            return new GameInstallationValidation(
                normalized, true, false, false, SupportedGameBuild.None,
                SupportedGameBuild.None,
                "This does not look like the game folder. Select the folder containing main, zone, and CoDWaW.exe.");
        }

        try
        {
            var installedLanguage = await DetectInstalledLanguageAsync(
                normalized,
                cancellationToken);
            var languageZoneDirectory = Path.Combine(
                zoneDirectory,
                installedLanguage.Code);
            if (!IsNormalDirectory(languageZoneDirectory))
            {
                return new GameInstallationValidation(
                    normalized, true, true, false, SupportedGameBuild.None,
                    SupportedGameBuild.None,
                    $"World at War is set to {installedLanguage.DisplayName}, but zone\\{installedLanguage.Code} is missing. Let Steam finish downloading that language, then try again.",
                    InstalledLanguage: installedLanguage,
                    LanguageAssetsPresent: false);
            }

            var mainMenuLanguageAssetsPresent = IsNormalFile(Path.Combine(
                languageZoneDirectory,
                "nazi_zombie_prototype.ff"));
            var multiplayerLanguageAssetsPresent = MultiplayerLanguageMarkers.All(
                marker => IsNormalFile(Path.Combine(languageZoneDirectory, marker)));

            var binkPath = Path.Combine(normalized, "binkw32.dll");
            var spPath = Path.Combine(normalized, "CoDWaW.exe");
            var mpPath = Path.Combine(normalized, "CoDWaWmp.exe");
            var localApplicationData = string.IsNullOrWhiteSpace(localApplicationDataDirectory)
                ? Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData)
                : NormalizeDirectory(localApplicationDataDirectory);

            var binkHashTask = fileHasher(binkPath, cancellationToken);
            var spResolutionTask = ResolveExecutableAsync(
                spPath,
                GetManagedFallbackExecutablePaths(
                    localApplicationData,
                    GameLaunchTarget.MainMenu),
                SteamSinglePlayerSha256,
                CompatibleSinglePlayerSha256,
                fileHasher,
                cancellationToken);
            var mpResolutionTask = ResolveExecutableAsync(
                mpPath,
                GetManagedFallbackExecutablePaths(
                    localApplicationData,
                    GameLaunchTarget.Multiplayer),
                SteamMultiplayerSha256,
                CompatibleMultiplayerSha256,
                fileHasher,
                cancellationToken);
            await Task.WhenAll(binkHashTask, spResolutionTask, mpResolutionTask);

            var runtimeSupported = string.Equals(
                binkHashTask.Result, SupportedBinkSha256, StringComparison.OrdinalIgnoreCase);
            var spResolution = spResolutionTask.Result;
            var mpResolution = mpResolutionTask.Result;

            var detail = BuildDetail(
                binkHashTask.Result,
                spResolution.Hash,
                mpResolution.Hash,
                runtimeSupported,
                spResolution.Build,
                mpResolution.Build);
            return new GameInstallationValidation(
                normalized,
                true,
                true,
                runtimeSupported,
                spResolution.Build,
                mpResolution.Build,
                detail,
                spResolution.Path,
                mpResolution.Path,
                installedLanguage,
                true,
                mainMenuLanguageAssetsPresent,
                multiplayerLanguageAssetsPresent);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or InvalidDataException or
            System.Security.SecurityException or NotSupportedException)
        {
            return new GameInstallationValidation(
                normalized, true, true, false, SupportedGameBuild.None,
                SupportedGameBuild.None,
                exception is InvalidDataException
                    ? exception.Message
                    : "The game files could not be read. Check folder permissions and try again.");
        }
    }

    public static InstalledGameLanguage ParseInstalledLanguageSelector(string contents)
    {
        ArgumentNullException.ThrowIfNull(contents);
        if (contents.Length == 0 || contents.Length > MaximumLocalizationSelectorBytes ||
            contents.IndexOf('\0') >= 0)
        {
            throw new InvalidDataException(
                "World at War localization.txt is empty, oversized, or not text.");
        }

        foreach (var rawLine in contents.Split('\n'))
        {
            var line = rawLine.EndsWith('\r') ? rawLine[..^1] : rawLine;
            if (line.All(character => character is ' ' or '\t'))
            {
                continue;
            }

            if (SupportedLanguages.TryGetValue(line, out var displayName))
            {
                return new InstalledGameLanguage(line, displayName);
            }

            throw new InvalidDataException(
                $"World at War reports an unsupported installed language: {line}");
        }

        throw new InvalidDataException(
            "World at War localization.txt has no installed language selector.");
    }

    public static GameInstallationValidation? ChooseBestCandidate(
        IEnumerable<GameInstallationValidation> candidates)
    {
        ArgumentNullException.ThrowIfNull(candidates);
        var materialized = candidates.ToArray();
        return materialized.FirstOrDefault(candidate =>
                   candidate.MainMenuReady && candidate.MultiplayerReady)
               ?? materialized.FirstOrDefault(candidate => candidate.MainMenuReady)
               ?? materialized.FirstOrDefault(candidate => candidate.MultiplayerReady);
    }

    public static IReadOnlyList<string> GetManagedFallbackExecutablePaths(
        string? localApplicationDataDirectory,
        GameLaunchTarget target)
    {
        var root = NormalizeDirectory(localApplicationDataDirectory);
        if (string.IsNullOrWhiteSpace(root))
        {
            return Array.Empty<string>();
        }

        if (target == GameLaunchTarget.Multiplayer)
        {
            return new[]
            {
                Path.Combine(root, "Plutonium", "games", "t4mp.exe"),
                Path.Combine(
                    root, "WaWVR", "runtime", "waw-mp-1.7.1263", "CoDWaWmp.exe"),
            };
        }

        return new[]
        {
            Path.Combine(root, "Plutonium", "games", "t4sp.exe"),
            Path.Combine(root, "WaWVR", "runtime", "waw-1.7.1263", "CoDWaW.exe"),
        };
    }

    public static string NormalizeDirectory(string? directory)
    {
        if (string.IsNullOrWhiteSpace(directory))
        {
            return string.Empty;
        }

        var value = Environment.ExpandEnvironmentVariables(directory.Trim().Trim('"'));
        try
        {
            var fullPath = Path.GetFullPath(value);
            var root = Path.GetPathRoot(fullPath);
            return string.Equals(fullPath, root, StringComparison.OrdinalIgnoreCase)
                ? fullPath
                : fullPath.TrimEnd(
                    Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        }
        catch (Exception exception) when (exception is ArgumentException or NotSupportedException)
        {
            return value;
        }
    }

    private static async Task<InstalledGameLanguage> DetectInstalledLanguageAsync(
        string gameDirectory,
        CancellationToken cancellationToken)
    {
        var path = Path.Combine(gameDirectory, "localization.txt");
        var file = new FileInfo(path);
        if (!file.Exists || file.Length == 0 ||
            file.Length > MaximumLocalizationSelectorBytes ||
            (file.Attributes & (FileAttributes.Directory | FileAttributes.ReparsePoint)) != 0)
        {
            throw new InvalidDataException(
                "World at War localization.txt is missing or invalid. Verify the game files in Steam and try again.");
        }

        var bytes = await File.ReadAllBytesAsync(path, cancellationToken);
        return ParseInstalledLanguageSelector(Encoding.Latin1.GetString(bytes));
    }

    private static bool IsNormalDirectory(string path)
    {
        if (!System.IO.Directory.Exists(path))
        {
            return false;
        }

        var attributes = File.GetAttributes(path);
        return (attributes & FileAttributes.Directory) != 0 &&
            (attributes & FileAttributes.ReparsePoint) == 0;
    }

    private static bool IsNormalFile(string path)
    {
        if (!File.Exists(path))
        {
            return false;
        }

        var attributes = File.GetAttributes(path);
        return (attributes & (FileAttributes.Directory | FileAttributes.ReparsePoint)) == 0;
    }

    private static async Task<string?> HashIfPresentAsync(
        string path,
        CancellationToken cancellationToken)
    {
        if (!File.Exists(path))
        {
            return null;
        }

        await using var stream = new FileStream(
            path, FileMode.Open, FileAccess.Read, FileShare.Read,
            1024 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan);
        var digest = await SHA256.HashDataAsync(stream, cancellationToken);
        return Convert.ToHexString(digest);
    }

    private static async Task<ExecutableResolution> ResolveExecutableAsync(
        string installationExecutable,
        IReadOnlyList<string> fallbackExecutables,
        string steamHash,
        string compatibleHash,
        Func<string, CancellationToken, Task<string?>> fileHasher,
        CancellationToken cancellationToken)
    {
        var directHash = await fileHasher(
            installationExecutable,
            cancellationToken);
        if (directHash is not null)
        {
            return new ExecutableResolution(
                IdentifyBuild(directHash, steamHash, compatibleHash),
                installationExecutable,
                directHash);
        }

        foreach (var fallback in fallbackExecutables)
        {
            var fallbackHash = await fileHasher(fallback, cancellationToken);
            if (fallbackHash is null)
            {
                continue;
            }

            return new ExecutableResolution(
                IdentifyBuild(fallbackHash, steamHash, compatibleHash),
                fallback,
                fallbackHash);
        }

        return new ExecutableResolution(SupportedGameBuild.None, null, null);
    }

    private static SupportedGameBuild IdentifyBuild(
        string? hash,
        string steamHash,
        string compatibleHash)
    {
        if (string.Equals(hash, steamHash, StringComparison.OrdinalIgnoreCase))
        {
            return SupportedGameBuild.SteamBuild252004;
        }

        return string.Equals(hash, compatibleHash, StringComparison.OrdinalIgnoreCase)
            ? SupportedGameBuild.CompatibleBuild
            : SupportedGameBuild.None;
    }

    private static string BuildDetail(
        string? binkHash,
        string? spHash,
        string? mpHash,
        bool runtimeSupported,
        SupportedGameBuild spBuild,
        SupportedGameBuild mpBuild)
    {
        if (binkHash is null)
        {
            return "binkw32.dll is missing. Verify the game files in Steam and try again.";
        }

        if (!runtimeSupported)
        {
            return "binkw32.dll is from an unsupported game build. Verify the game files in Steam and try again.";
        }

        if (spHash is null && mpHash is null)
        {
            return "CoDWaW.exe and CoDWaWmp.exe are missing from this folder.";
        }

        if (spHash is not null && spBuild == SupportedGameBuild.None &&
            mpHash is not null && mpBuild == SupportedGameBuild.None)
        {
            return "The game executables are from an unsupported build. Update or verify the game in Steam and try again.";
        }

        if (spBuild == SupportedGameBuild.None)
        {
            return spHash is null
                ? "CoDWaW.exe is missing. It is required for Zombies and Campaign."
                : "CoDWaW.exe is from an unsupported build. Verify the game files in Steam and try again.";
        }

        if (mpBuild == SupportedGameBuild.None)
        {
            return mpHash is null
                ? "CoDWaWmp.exe is missing. It is required for Multiplayer."
                : "CoDWaWmp.exe is from an unsupported build. Verify the game files in Steam and try again.";
        }

        return "Compatible installation found.";
    }

    private static GameInstallationValidation Invalid(string directory, string detail) =>
        new(directory, false, false, false, SupportedGameBuild.None,
            SupportedGameBuild.None, detail);

    private sealed record ExecutableResolution(
        SupportedGameBuild Build,
        string? Path,
        string? Hash);
}
