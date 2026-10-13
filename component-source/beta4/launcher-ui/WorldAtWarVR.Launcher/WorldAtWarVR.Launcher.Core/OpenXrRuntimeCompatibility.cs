using Microsoft.Win32;
using System.Buffers.Binary;
using System.Security;
using System.Text.Json;

namespace WorldAtWarVR.Launcher.Core;

public sealed record OpenXrRuntimeLaunchSelection(
    string Kind,
    string Detail,
    string? OverrideManifest)
{
    public IReadOnlyDictionary<string, string> EnvironmentVariables
    {
        get
        {
            var environment = new Dictionary<string, string>(
                StringComparer.OrdinalIgnoreCase)
            {
                [OpenXrRuntimeCompatibility.RuntimeSelectionEnvironmentVariable] = Kind,
            };
            if (!string.IsNullOrWhiteSpace(OverrideManifest))
            {
                environment[OpenXrRuntimeCompatibility.RuntimeManifestEnvironmentVariable] =
                    OverrideManifest;
            }

            return environment;
        }
    }
}

public sealed record OpenXrRuntimeManifestInspection(
    bool Valid,
    string ManifestPath,
    string? LibraryPath,
    string Detail);

public static class OpenXrRuntimeCompatibility
{
    public const string RuntimeManifestEnvironmentVariable = "XR_RUNTIME_JSON";
    public const string RuntimeSelectionEnvironmentVariable =
        "WAWVR_OPENXR_RUNTIME_SELECTION";
    public const string PimaxX86ManifestFileName = "PiOpenXR_32.json";

    private const ushort ImageFileMachineI386 = 0x014c;

    public static OpenXrRuntimeLaunchSelection ResolveLaunchSelection()
    {
        var explicitRuntime = Environment.GetEnvironmentVariable(
            RuntimeManifestEnvironmentVariable);
        return SelectLaunchRuntime(
            explicitRuntime,
            ReadActiveRuntime(RegistryHive.LocalMachine, RegistryView.Registry32),
            ReadActiveRuntime(RegistryHive.CurrentUser, RegistryView.Registry32),
            ReadActiveRuntime(RegistryHive.LocalMachine, RegistryView.Registry64),
            ReadActiveRuntime(RegistryHive.CurrentUser, RegistryView.Registry64),
            PimaxInstallationCandidates());
    }

    public static OpenXrRuntimeLaunchSelection SelectLaunchRuntime(
        string? explicitRuntime,
        string? machineRuntime32,
        string? userRuntime32,
        string? machineRuntime64,
        string? userRuntime64,
        IEnumerable<string>? pimaxInstallationCandidates = null)
    {
        if (!string.IsNullOrWhiteSpace(explicitRuntime))
        {
            return new OpenXrRuntimeLaunchSelection(
                "explicit",
                "The existing XR_RUNTIME_JSON selection was preserved for this launch.",
                null);
        }

        var active32 = FirstRegisteredRuntime(machineRuntime32, userRuntime32);
        var active64 = FirstRegisteredRuntime(machineRuntime64, userRuntime64);
        var activePimax = IsPimaxRuntimeManifest(active32)
            ? active32
            : string.IsNullOrWhiteSpace(active32) &&
                IsPimaxRuntimeManifest(active64)
                ? active64
                : null;

        if (string.IsNullOrWhiteSpace(activePimax))
        {
            return new OpenXrRuntimeLaunchSelection(
                "system-default",
                "The registered 32-bit OpenXR ActiveRuntime remains authoritative.",
                null);
        }

        var candidates = new List<string>();
        var expandedActive = ExpandPath(activePimax);
        var activeDirectory = Path.GetDirectoryName(expandedActive);
        if (!string.IsNullOrWhiteSpace(activeDirectory))
        {
            candidates.Add(Path.Combine(activeDirectory, PimaxX86ManifestFileName));
        }

        if (pimaxInstallationCandidates is not null)
        {
            candidates.AddRange(pimaxInstallationCandidates);
        }

        var failures = new List<string>();
        foreach (var candidate in candidates
                     .Where(path => !string.IsNullOrWhiteSpace(path))
                     .Select(ExpandPath)
                     .Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var inspection = InspectRuntimeManifest(candidate);
            if (inspection.Valid)
            {
                return new OpenXrRuntimeLaunchSelection(
                    "pimax-x86",
                    $"Selected Pimax's verified 32-bit OpenXR runtime: {inspection.ManifestPath}",
                    inspection.ManifestPath);
            }

            failures.Add(inspection.Detail);
        }

        var failureDetail = failures.Count == 0
            ? "No Pimax 32-bit manifest candidate could be constructed."
            : string.Join(" ", failures.Distinct(StringComparer.Ordinal));
        throw new InvalidOperationException(
            "Pimax is the active OpenXR runtime, but its usable 32-bit " +
            $"{PimaxX86ManifestFileName} could not be found. {failureDetail} " +
            "Update or repair Pimax EVO/Pimax Play, then try again.");
    }

    public static bool IsPimaxRuntimeManifest(string? path)
    {
        return !string.IsNullOrWhiteSpace(path) &&
            (path.Contains("pimax", StringComparison.OrdinalIgnoreCase) ||
             path.Contains("piopenxr", StringComparison.OrdinalIgnoreCase));
    }

    public static OpenXrRuntimeManifestInspection InspectRuntimeManifest(
        string manifestPath)
    {
        string fullManifestPath;
        try
        {
            fullManifestPath = Path.GetFullPath(ExpandPath(manifestPath));
            if (!File.Exists(fullManifestPath))
            {
                return InvalidInspection(
                    fullManifestPath,
                    $"Pimax x86 manifest not found at {fullManifestPath}.");
            }

            using var document = JsonDocument.Parse(File.ReadAllText(fullManifestPath));
            if (!document.RootElement.TryGetProperty("runtime", out var runtime) ||
                !runtime.TryGetProperty("library_path", out var libraryProperty) ||
                libraryProperty.ValueKind != JsonValueKind.String ||
                string.IsNullOrWhiteSpace(libraryProperty.GetString()))
            {
                return InvalidInspection(
                    fullManifestPath,
                    $"Pimax x86 manifest has no runtime.library_path: {fullManifestPath}.");
            }

            var libraryPath = ExpandPath(libraryProperty.GetString()!);
            if (!Path.IsPathRooted(libraryPath))
            {
                libraryPath = Path.Combine(
                    Path.GetDirectoryName(fullManifestPath)!,
                    libraryPath);
            }
            libraryPath = Path.GetFullPath(libraryPath);
            if (!File.Exists(libraryPath))
            {
                return new OpenXrRuntimeManifestInspection(
                    false,
                    fullManifestPath,
                    libraryPath,
                    $"Pimax x86 runtime DLL not found at {libraryPath}.");
            }

            if (!IsX86PeImage(libraryPath))
            {
                return new OpenXrRuntimeManifestInspection(
                    false,
                    fullManifestPath,
                    libraryPath,
                    $"Pimax runtime DLL is not a 32-bit x86 image: {libraryPath}.");
            }

            return new OpenXrRuntimeManifestInspection(
                true,
                fullManifestPath,
                libraryPath,
                "Pimax 32-bit OpenXR manifest and runtime DLL are valid.");
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or
            SecurityException or JsonException or ArgumentException or
            NotSupportedException)
        {
            return InvalidInspection(
                manifestPath,
                $"Pimax x86 manifest could not be validated: {exception.Message}");
        }
    }

    private static OpenXrRuntimeManifestInspection InvalidInspection(
        string manifestPath,
        string detail)
    {
        return new OpenXrRuntimeManifestInspection(
            false,
            manifestPath,
            null,
            detail);
    }

    private static bool IsX86PeImage(string path)
    {
        using var stream = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read | FileShare.Delete);
        Span<byte> dosHeader = stackalloc byte[64];
        if (stream.Read(dosHeader) != dosHeader.Length ||
            dosHeader[0] != (byte)'M' || dosHeader[1] != (byte)'Z')
        {
            return false;
        }

        var peOffset = BinaryPrimitives.ReadInt32LittleEndian(dosHeader[0x3c..]);
        if (peOffset < dosHeader.Length || peOffset > stream.Length - 6)
        {
            return false;
        }

        stream.Position = peOffset;
        Span<byte> peHeader = stackalloc byte[6];
        return stream.Read(peHeader) == peHeader.Length &&
            peHeader[0] == (byte)'P' && peHeader[1] == (byte)'E' &&
            peHeader[2] == 0 && peHeader[3] == 0 &&
            BinaryPrimitives.ReadUInt16LittleEndian(peHeader[4..]) ==
                ImageFileMachineI386;
    }

    private static string? ReadActiveRuntime(
        RegistryHive hive,
        RegistryView view)
    {
        try
        {
            using var root = RegistryKey.OpenBaseKey(hive, view);
            using var openXr = root.OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1");
            return openXr?.GetValue("ActiveRuntime") as string;
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or SecurityException)
        {
            return null;
        }
    }

    private static string? FirstRegisteredRuntime(
        string? machineRuntime,
        string? userRuntime)
    {
        return !string.IsNullOrWhiteSpace(machineRuntime)
            ? machineRuntime
            : !string.IsNullOrWhiteSpace(userRuntime)
                ? userRuntime
                : null;
    }

    private static IEnumerable<string> PimaxInstallationCandidates()
    {
        foreach (var variable in new[]
                 {
                     "ProgramW6432",
                     "ProgramFiles",
                     "ProgramFiles(x86)",
                 })
        {
            var root = Environment.GetEnvironmentVariable(variable);
            if (!string.IsNullOrWhiteSpace(root))
            {
                yield return Path.Combine(
                    root,
                    "Pimax",
                    "Runtime",
                    PimaxX86ManifestFileName);
            }
        }
    }

    private static string ExpandPath(string path)
    {
        return Environment.ExpandEnvironmentVariables(path.Trim().Trim('"'));
    }
}
