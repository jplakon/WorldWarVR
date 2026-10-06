using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace WorldAtWarVR.Launcher.Core;

public static partial class SteamInstallDetector
{
    private const string SteamAppId = "10090";

    public static IReadOnlyList<string> FindCandidateGameDirectories()
    {
        var candidates = new List<string>();
        AddUninstallCandidates(candidates);

        foreach (var steamRoot in FindSteamRoots())
        {
            foreach (var library in FindLibraryRoots(steamRoot))
            {
                AddLibraryGameCandidates(library, candidates);
            }
        }

        return candidates
            .Select(GameInstallationValidator.NormalizeDirectory)
            .Where(System.IO.Directory.Exists)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    private static IEnumerable<string> FindSteamRoots()
    {
        var roots = new List<string>();
        ReadRegistryValue(Registry.CurrentUser, @"Software\Valve\Steam", "SteamPath", roots);
        ReadRegistryValue(Registry.LocalMachine, @"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath", roots);
        ReadRegistryValue(Registry.LocalMachine, @"SOFTWARE\Valve\Steam", "InstallPath", roots);

        var programFilesX86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
        var programFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
        roots.Add(Path.Combine(programFilesX86, "Steam"));
        roots.Add(Path.Combine(programFiles, "Steam"));

        return roots
            .Where(path => !string.IsNullOrWhiteSpace(path))
            .Select(GameInstallationValidator.NormalizeDirectory)
            .Where(System.IO.Directory.Exists)
            .Distinct(StringComparer.OrdinalIgnoreCase);
    }

    private static IEnumerable<string> FindLibraryRoots(string steamRoot)
    {
        var libraries = new List<string> { steamRoot };
        var vdfPath = Path.Combine(steamRoot, "steamapps", "libraryfolders.vdf");
        try
        {
            if (File.Exists(vdfPath))
            {
                var content = File.ReadAllText(vdfPath);
                foreach (Match match in LibraryPathRegex().Matches(content))
                {
                    var path = match.Groups[1].Value.Replace(@"\\", @"\");
                    if (!string.IsNullOrWhiteSpace(path))
                    {
                        libraries.Add(path);
                    }
                }
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            // The default Steam root remains useful if a library file is temporarily locked.
        }

        return libraries
            .Select(GameInstallationValidator.NormalizeDirectory)
            .Where(System.IO.Directory.Exists)
            .Distinct(StringComparer.OrdinalIgnoreCase);
    }

    private static void AddLibraryGameCandidates(string libraryRoot, ICollection<string> candidates)
    {
        var steamApps = Path.Combine(libraryRoot, "steamapps");
        var common = Path.Combine(steamApps, "common");
        var manifest = Path.Combine(steamApps, $"appmanifest_{SteamAppId}.acf");

        try
        {
            if (File.Exists(manifest))
            {
                var content = File.ReadAllText(manifest);
                var installDir = InstallDirectoryRegex().Match(content);
                if (installDir.Success)
                {
                    candidates.Add(Path.Combine(common, installDir.Groups[1].Value));
                }
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            // Fall back to Steam's known directory names below.
        }

        candidates.Add(Path.Combine(common, "Call of Duty World at War"));
        candidates.Add(Path.Combine(common, "Call of Duty - World at War"));
    }

    private static void AddUninstallCandidates(ICollection<string> candidates)
    {
        const string keyPath = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 10090";
        ReadRegistryValue(Registry.LocalMachine, keyPath, "InstallLocation", candidates);
        ReadRegistryValue(Registry.LocalMachine, @"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 10090", "InstallLocation", candidates);
        ReadRegistryValue(Registry.CurrentUser, keyPath, "InstallLocation", candidates);
    }

    private static void ReadRegistryValue(
        RegistryKey root,
        string keyPath,
        string valueName,
        ICollection<string> values)
    {
        try
        {
            using var key = root.OpenSubKey(keyPath);
            if (key?.GetValue(valueName) is string value && !string.IsNullOrWhiteSpace(value))
            {
                values.Add(value.Replace('/', Path.DirectorySeparatorChar));
            }
        }
        catch (Exception exception) when (exception is UnauthorizedAccessException or System.Security.SecurityException)
        {
            // Registry discovery is best-effort. The Browse button remains available.
        }
    }

    [GeneratedRegex("\\\"path\\\"\\s+\\\"([^\\\"]+)\\\"", RegexOptions.IgnoreCase)]
    private static partial Regex LibraryPathRegex();

    [GeneratedRegex("\\\"installdir\\\"\\s+\\\"([^\\\"]+)\\\"", RegexOptions.IgnoreCase)]
    private static partial Regex InstallDirectoryRegex();
}
