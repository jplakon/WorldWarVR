namespace WorldAtWarVR.Launcher.Core;

public sealed record LauncherCommand(string FileName, IReadOnlyList<string> Arguments);

public static class LauncherCommandBuilder
{
    public const string NativeLauncherFileName = "wawvr-launcher.exe";
    public const string ModDllFileName = "WorldWarVR.dll";

    public static LauncherCommand Build(
        string applicationDirectory,
        LauncherSettings settings,
        GameInstallationValidation? validation = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(applicationDirectory);
        ArgumentNullException.ThrowIfNull(settings);

        var normalized = settings.Normalize();
        if (string.IsNullOrWhiteSpace(normalized.GameDirectory))
        {
            throw new ArgumentException("A game installation folder is required.", nameof(settings));
        }

        var baseDirectory = Path.GetFullPath(applicationDirectory);
        var helper = Path.Combine(baseDirectory, NativeLauncherFileName);
        var modDll = Path.Combine(baseDirectory, ModDllFileName);
        var resolution = ResolutionChoices.Get(normalized.Resolution);

        var arguments = new List<string>
        {
            "--launch",
            normalized.LaunchTarget == GameLaunchTarget.Multiplayer ? "--multiplayer" : "--menu",
            "--game-dir",
            Path.GetFullPath(normalized.GameDirectory),
            "--resolution",
            resolution.Dimensions,
            "--mod-dll",
            modDll,
            "--bots",
            normalized.AutomaticBots ? "enabled" : "disabled",
        };

        if (validation is not null)
        {
            var validatedDirectory = GameInstallationValidator.NormalizeDirectory(
                validation.Directory);
            if (!string.Equals(
                    validatedDirectory,
                    normalized.GameDirectory,
                    StringComparison.OrdinalIgnoreCase) ||
                !validation.IsReadyFor(normalized.LaunchTarget))
            {
                throw new ArgumentException(
                    "The validated game installation no longer matches the launch settings.",
                    nameof(validation));
            }

            if (normalized.LaunchTarget == GameLaunchTarget.Multiplayer)
            {
                AddExecutableArgument(
                    arguments,
                    "--source-exe",
                    validation.MultiplayerExecutable);
            }
            else
            {
                AddExecutableArgument(
                    arguments,
                    "--source-exe",
                    validation.SinglePlayerExecutable);
                if (validation.MultiplayerBuild != SupportedGameBuild.None)
                {
                    AddExecutableArgument(
                        arguments,
                        "--mp-source-exe",
                        validation.MultiplayerExecutable);
                }
            }
        }

        return new LauncherCommand(helper, arguments);
    }

    private static void AddExecutableArgument(
        ICollection<string> arguments,
        string option,
        string? executable)
    {
        if (string.IsNullOrWhiteSpace(executable))
        {
            throw new ArgumentException(
                $"The validated executable for {option} is unavailable.",
                nameof(executable));
        }

        arguments.Add(option);
        arguments.Add(Path.GetFullPath(executable));
    }
}
