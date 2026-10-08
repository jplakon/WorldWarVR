using System.Text.Json;
using System.Text.Json.Serialization;

namespace WorldAtWarVR.Launcher.Core;

public enum GameLaunchTarget
{
    MainMenu,
    Multiplayer,
}

public enum VrResolutionPreset
{
    Recommended,
    HighQuality,
    Performance,
}

public sealed record ResolutionChoice(
    VrResolutionPreset Preset,
    string Name,
    string Detail,
    int Width,
    int Height)
{
    public string Dimensions => $"{Width}x{Height}";
}

public static class ResolutionChoices
{
    public static IReadOnlyList<ResolutionChoice> All { get; } =
    [
        // Keep the persisted enum values stable so existing settings migrate without data loss.
        new(VrResolutionPreset.Recommended, "Native + scopes", "Full 2496 x 2688 per eye plus a sharp 1024 x 1024 physical scope camera", 6016, 2688),
        new(VrResolutionPreset.HighQuality, "Performance", "1872 x 2016 per eye with a lower GPU load", 3744, 2016),
        new(VrResolutionPreset.Performance, "Recovery", "1280 x 1440 per eye for compatibility and recovery", 2560, 1440),
    ];

    public static ResolutionChoice Get(VrResolutionPreset preset) =>
        All.FirstOrDefault(choice => choice.Preset == preset) ?? All[0];
}

public sealed record LauncherSettings
{
    public int Version { get; init; } = 5;
    public string GameDirectory { get; init; } = string.Empty;
    public GameLaunchTarget LaunchTarget { get; init; } = GameLaunchTarget.MainMenu;
    public bool AutomaticBots { get; init; } = true;
    public bool QuestAirLinkCompatibility { get; init; }
    public bool AutomaticReload { get; init; }
    public bool ButtonGrenades { get; init; }
    public bool SmoothTurning { get; init; }
    public VrResolutionPreset Resolution { get; init; } = VrResolutionPreset.Recommended;

    public LauncherSettings Normalize()
    {
        var target = Enum.IsDefined(LaunchTarget) ? LaunchTarget : GameLaunchTarget.MainMenu;
        var resolution = Enum.IsDefined(Resolution) ? Resolution : VrResolutionPreset.Recommended;
        var directory = GameInstallationValidator.NormalizeDirectory(GameDirectory);

        return this with
        {
            Version = 5,
            GameDirectory = directory,
            LaunchTarget = target,
            Resolution = resolution,
        };
    }
}

public static class GameplayOptionEnvironment
{
    public const string AutomaticReloadVariable = "WAWVR_AUTOMATIC_RELOAD";
    public const string ButtonGrenadesVariable = "WAWVR_BUTTON_GRENADES";
    public const string TurnModeVariable = "WAWVR_TURN_MODE";
    public const string DeferXrBeginFrameVariable = "WAWVR_DEFER_XR_BEGIN_FRAME";

    public static void ApplyTo(
        IDictionary<string, string?> childEnvironment,
        LauncherSettings settings)
    {
        ArgumentNullException.ThrowIfNull(childEnvironment);
        foreach (var variable in Resolve(settings))
        {
            // Assign explicit off values too: a child may inherit an enabled
            // option from the process that started the launcher.
            childEnvironment[variable.Key] = variable.Value;
        }
    }

    public static IReadOnlyDictionary<string, string> Resolve(LauncherSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            [AutomaticReloadVariable] = settings.AutomaticReload ? "1" : "0",
            [ButtonGrenadesVariable] = settings.ButtonGrenades ? "1" : "0",
            [TurnModeVariable] = settings.SmoothTurning ? "smooth" : "snap",
            // The runtime retains its VirtualDesktopXR allowlist. Explicitly
            // disable this SP timing policy for other launcher targets.
            [DeferXrBeginFrameVariable] = settings.LaunchTarget == GameLaunchTarget.MainMenu ? "1" : "0",
        };
    }
}

public sealed class LauncherSettingsStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter() },
    };
    private readonly SemaphoreSlim _saveGate = new(1, 1);

    public LauncherSettingsStore(string? settingsPath = null)
    {
        SettingsPath = settingsPath ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "WorldAtWarVR",
            "launcher-settings.json");
    }

    public string SettingsPath { get; }

    public async Task<LauncherSettings> LoadAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            if (!File.Exists(SettingsPath))
            {
                return new LauncherSettings();
            }

            await using var stream = File.OpenRead(SettingsPath);
            var settings = await JsonSerializer.DeserializeAsync<LauncherSettings>(
                stream, JsonOptions, cancellationToken);
            return (settings ?? new LauncherSettings()).Normalize();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            return new LauncherSettings();
        }
    }

    public async Task SaveAsync(
        LauncherSettings settings,
        CancellationToken cancellationToken = default)
    {
        await _saveGate.WaitAsync(cancellationToken);
        try
        {
            var normalized = settings.Normalize();
            var directory = Path.GetDirectoryName(SettingsPath)
                ?? throw new InvalidOperationException("The settings path has no parent directory.");
            Directory.CreateDirectory(directory);

            var temporaryPath = SettingsPath + ".tmp";
            try
            {
                await using (var stream = new FileStream(
                    temporaryPath,
                    FileMode.Create,
                    FileAccess.Write,
                    FileShare.None,
                    4096,
                    useAsync: true))
                {
                    await JsonSerializer.SerializeAsync(
                        stream, normalized, JsonOptions, cancellationToken);
                    await stream.FlushAsync(cancellationToken);
                }

                File.Move(temporaryPath, SettingsPath, overwrite: true);
            }
            finally
            {
                if (File.Exists(temporaryPath))
                {
                    File.Delete(temporaryPath);
                }
            }
        }
        finally
        {
            _saveGate.Release();
        }
    }
}
