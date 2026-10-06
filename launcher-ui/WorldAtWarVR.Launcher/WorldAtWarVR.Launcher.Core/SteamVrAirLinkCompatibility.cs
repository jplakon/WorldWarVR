using Microsoft.Win32;
using System.Security;

namespace WorldAtWarVR.Launcher.Core;

public static class SteamVrAirLinkCompatibility
{
    public const string RuntimeManifestEnvironmentVariable =
        OpenXrRuntimeCompatibility.RuntimeManifestEnvironmentVariable;
    public const string OculusBaseEnvironmentVariable = "OculusBase";
    public const string SteamVrX86ManifestFileName = "steamxr_win32.json";

    public static IReadOnlyDictionary<string, string> ResolveLaunchEnvironment()
    {
        var manifest = FindRegisteredSteamVrX86Manifest();
        if (manifest is null)
        {
            throw new InvalidOperationException(
                "SteamVR's 32-bit OpenXR runtime was not found. Install or update SteamVR, then try again.");
        }

        var environment = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            [RuntimeManifestEnvironmentVariable] = manifest,
            [OpenXrRuntimeCompatibility.RuntimeSelectionEnvironmentVariable] =
                "steamvr-x86",
        };

        var oculusBase = FindCurrentOculusBase();
        if (oculusBase is not null)
        {
            environment[OculusBaseEnvironmentVariable] = oculusBase;
        }

        return environment;
    }

    public static string? FindRegisteredSteamVrX86Manifest()
    {
        var candidates = new List<string>();
        try
        {
            using var root = RegistryKey.OpenBaseKey(
                RegistryHive.LocalMachine,
                RegistryView.Registry32);
            using var openXr = root.OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1");
            if (openXr?.GetValue("ActiveRuntime") is string activeRuntime)
            {
                candidates.Add(activeRuntime);
            }

            using var available = openXr?.OpenSubKey("AvailableRuntimes");
            if (available is not null)
            {
                candidates.AddRange(available.GetValueNames());
            }
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or SecurityException)
        {
            return null;
        }

        return SelectSteamVrX86Manifest(candidates);
    }

    public static string? SelectSteamVrX86Manifest(
        IEnumerable<string> candidates,
        Func<string, bool>? fileExists = null)
    {
        ArgumentNullException.ThrowIfNull(candidates);
        fileExists ??= File.Exists;

        foreach (var candidate in candidates)
        {
            if (string.IsNullOrWhiteSpace(candidate))
            {
                continue;
            }

            var expanded = Environment
                .ExpandEnvironmentVariables(candidate.Trim().Trim('"'));
            if (!string.Equals(
                    Path.GetFileName(expanded),
                    SteamVrX86ManifestFileName,
                    StringComparison.OrdinalIgnoreCase) ||
                !fileExists(expanded))
            {
                continue;
            }

            return Path.GetFullPath(expanded);
        }

        return null;
    }

    public static string? SelectCurrentOculusBase(
        string? machineValue,
        string? processValue,
        Func<string, bool>? directoryExists = null)
    {
        directoryExists ??= Directory.Exists;
        foreach (var candidate in new[] { machineValue, processValue })
        {
            if (string.IsNullOrWhiteSpace(candidate))
            {
                continue;
            }

            var expanded = Environment
                .ExpandEnvironmentVariables(candidate.Trim().Trim('"'));
            if (directoryExists(expanded))
            {
                return Path.GetFullPath(expanded);
            }
        }

        return null;
    }

    private static string? FindCurrentOculusBase()
    {
        try
        {
            return SelectCurrentOculusBase(
                Environment.GetEnvironmentVariable(
                    OculusBaseEnvironmentVariable,
                    EnvironmentVariableTarget.Machine),
                Environment.GetEnvironmentVariable(OculusBaseEnvironmentVariable));
        }
        catch (Exception exception) when (
            exception is SecurityException or UnauthorizedAccessException)
        {
            return SelectCurrentOculusBase(
                null,
                Environment.GetEnvironmentVariable(OculusBaseEnvironmentVariable));
        }
    }
}
