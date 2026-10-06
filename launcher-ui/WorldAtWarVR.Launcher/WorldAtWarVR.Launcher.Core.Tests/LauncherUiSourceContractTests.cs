using System.Text.RegularExpressions;
using System.Xml.Linq;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace WorldAtWarVR.Launcher.Core.Tests;

[TestClass]
public sealed class LauncherUiSourceContractTests
{
    private const string BackgroundAsset = @"Assets\Launcher\LauncherBackground.png";
    private static readonly XNamespace XamlNamespace =
        "http://schemas.microsoft.com/winfx/2006/xaml";

    [TestMethod]
    public void MainPageXaml_PreservesBehaviorControlAndEventContracts()
    {
        var document = LoadXaml("MainPage.xaml");

        AssertElementContract(document, "GamePathTextBox", "TextChanged", "GamePathTextBox_TextChanged");
        AssertElementContract(document, "MainMenuRadio", "Checked", "LaunchTarget_Checked");
        AssertElementContract(document, "MultiplayerRadio", "Checked", "LaunchTarget_Checked");

        var bots = RequiredElementByName(document, "BotsCheckBox");
        AssertAttribute(bots, "Checked", "SettingControl_Changed");
        AssertAttribute(bots, "Unchecked", "SettingControl_Changed");

        var airLink = RequiredElementByName(document, "AirLinkSteamVrCheckBox");
        AssertAttribute(airLink, "Checked", "SettingControl_Changed");
        AssertAttribute(airLink, "Unchecked", "SettingControl_Changed");

        foreach (var name in new[]
                 {
                     "AutomaticReloadCheckBox",
                     "ButtonGrenadesCheckBox",
                     "SmoothTurningCheckBox",
                 })
        {
            var option = RequiredElementByName(document, name);
            AssertAttribute(option, "Checked", "SettingControl_Changed");
            AssertAttribute(option, "Unchecked", "SettingControl_Changed");
        }

        foreach (var name in new[] { "NativeRadio", "PerformanceRadio", "RecoveryRadio" })
        {
            var radio = RequiredElementByName(document, name);
            AssertAttribute(radio, "Checked", "ResolutionRadio_Checked");
            AssertAttribute(radio, "GroupName", "ResolutionPreset");
        }

        foreach (var name in new[]
                 {
                     "InstallationInfoBar",
                     "MultiplayerInfoBar",
                     "ResolutionDetailText",
                     "LaunchSummaryText",
                     "ActivityStatusText",
                     "BusyProgressRing",
                 })
        {
            _ = RequiredElementByName(document, name);
        }

        var launchButton = RequiredElementByName(document, "LaunchButton");
        Assert.AreEqual("Button", launchButton.Name.LocalName);
        AssertAttribute(launchButton, "Click", "LaunchButton_Click");
        AssertAttribute(launchButton, "IsEnabled", "False");

        var browseBindings = document
            .Descendants()
            .Where(element => (string?)element.Attribute("Click") == "BrowseButton_Click")
            .ToArray();
        Assert.AreEqual(1, browseBindings.Length, "BrowseButton_Click must have exactly one XAML binding.");
        Assert.AreEqual("Button", browseBindings[0].Name.LocalName);

        var launchBindings = LoadAllXamlDocuments()
            .SelectMany(xaml => xaml.Descendants().Attributes())
            .Count(attribute => attribute.Value == "LaunchButton_Click");
        Assert.AreEqual(1, launchBindings, "LaunchButton_Click must be bound exactly once across launcher XAML.");

        var codeBehind = ReadLauncherFile("MainPage.xaml.cs");
        var handlerDeclarations = Regex.Matches(
            codeBehind,
            @"\bLaunchButton_Click\s*\(\s*object\s+sender\s*,\s*RoutedEventArgs\s+e\s*\)",
            RegexOptions.CultureInvariant);
        Assert.AreEqual(1, handlerDeclarations.Count, "LaunchButton_Click must have exactly one handler declaration.");
    }

    [TestMethod]
    public void BrowseButton_RecoversWhenTheWindowsFolderPickerFails()
    {
        var source = ReadLauncherFile("MainPage.xaml.cs");

        StringAssert.Contains(source, "picker.PickSingleFolderAsync()");
        StringAssert.Contains(
            source,
            "exception is System.Runtime.InteropServices.COMException");
        StringAssert.Contains(source, "You can still paste");
        StringAssert.Contains(
            source,
            "your Call of Duty World at War folder into the path box above.");

        var browseHandler = source.IndexOf(
            "private async void BrowseButton_Click",
            StringComparison.Ordinal);
        var pickerCall = source.IndexOf(
            "picker.PickSingleFolderAsync()",
            browseHandler,
            StringComparison.Ordinal);
        var pickerCatch = source.IndexOf(
            "System.Runtime.InteropServices.COMException",
            pickerCall,
            StringComparison.Ordinal);
        Assert.IsTrue(
            browseHandler >= 0 && pickerCall > browseHandler && pickerCatch > pickerCall,
            "The folder-picker call must be protected by the Browse handler's recoverable exception path.");
    }

    [TestMethod]
    public void ResolutionRadios_MapExplicitlyToBackwardCompatibleSettingsPresets()
    {
        var codeBehind = ReadLauncherFile("MainPage.xaml.cs");

        Assert.IsFalse(codeBehind.Contains("ResolutionComboBox", StringComparison.Ordinal));
        Assert.IsFalse(codeBehind.Contains("SelectedIndex", StringComparison.Ordinal));
        StringAssert.Contains(codeBehind, "Resolution = GetSelectedResolution()");

        AssertPresetApplied(codeBehind, "NativeRadio", "Recommended");
        AssertPresetApplied(codeBehind, "PerformanceRadio", "HighQuality");
        AssertPresetApplied(codeBehind, "RecoveryRadio", "Performance");

        AssertRegex(
            codeBehind,
            @"if\s*\(\s*RecoveryRadio\.IsChecked\s*==\s*true\s*\)\s*\{\s*return\s+VrResolutionPreset\.Performance\s*;",
            "The Recovery radio must retain the legacy Performance enum value explicitly.");
        AssertRegex(
            codeBehind,
            @"PerformanceRadio\.IsChecked\s*==\s*true\s*\?\s*VrResolutionPreset\.HighQuality\s*:\s*VrResolutionPreset\.Recommended",
            "The Performance radio must retain the legacy HighQuality enum value, with Native as the fallback.");
    }

    [TestMethod]
    public void GameplayOptions_AreSavedAndAppliedOnlyToTheChildLaunch()
    {
        var source = ReadLauncherFile("MainPage.xaml.cs");
        StringAssert.Contains(source, "AutomaticReload = AutomaticReloadCheckBox.IsChecked == true");
        StringAssert.Contains(source, "ButtonGrenades = ButtonGrenadesCheckBox.IsChecked == true");
        StringAssert.Contains(source, "SmoothTurning = SmoothTurningCheckBox.IsChecked == true");
        StringAssert.Contains(source, "GameplayOptionEnvironment.Resolve(settings)");
        StringAssert.Contains(source, "startInfo.Environment[variable.Key] = variable.Value");
    }

    [TestMethod]
    public void InstalledLanguage_IsRevalidatedBeforeLaunchAndDisplayed()
    {
        var source = ReadLauncherFile("MainPage.xaml.cs");
        StringAssert.Contains(source, "_validation?.InstalledLanguage");
        StringAssert.Contains(source, "Language: {language.DisplayName}");
        StringAssert.Contains(
            source,
            "private async Task<bool> ValidateCurrentDirectoryAsync()");
        StringAssert.Contains(
            source,
            "catch (OperationCanceledException) when (token.IsCancellationRequested)");
        StringAssert.Contains(source, "System.Security.SecurityException");

        var launchHandler = source.IndexOf(
            "private async void LaunchButton_Click",
            StringComparison.Ordinal);
        var timerStop = source.IndexOf(
            "_validationTimer.Stop();",
            launchHandler,
            StringComparison.Ordinal);
        var guardedRevalidation = source.IndexOf(
            "if (!await ValidateCurrentDirectoryAsync())",
            launchHandler,
            StringComparison.Ordinal);
        var commandBuild = source.IndexOf(
            "LauncherCommandBuilder.Build(",
            launchHandler,
            StringComparison.Ordinal);
        Assert.IsTrue(
            launchHandler >= 0 && timerStop > launchHandler &&
            guardedRevalidation > timerStop && commandBuild > guardedRevalidation,
            "Launch must stop debounce work and abort unless Steam language and game-file revalidation completes before command construction.");
    }

    [TestMethod]
    public void ResolutionRadios_ShowNativePerformanceAndRecoveryDimensions()
    {
        var document = LoadXaml("MainPage.xaml");

        AssertResolutionRadio(
            document,
            "NativeRadio",
            "Native plus scopes recommended, 6016 by 2688 packed render source",
            "Native",
            "6016 × 2688");
        AssertResolutionRadio(
            document,
            "PerformanceRadio",
            "Performance, 3744 by 2016 packed stereo",
            "Performance",
            "3744 × 2016");
        AssertResolutionRadio(
            document,
            "RecoveryRadio",
            "Recovery compatibility, 2560 by 1440 packed stereo",
            "Recovery",
            "2560 × 1440");
    }

    [TestMethod]
    public void QuestAirLinkCompatibility_IsSavedAndAppliedOnlyToTheChildLaunch()
    {
        var source = ReadLauncherFile("MainPage.xaml.cs");
        StringAssert.Contains(source, "QuestAirLinkCompatibility = AirLinkSteamVrCheckBox.IsChecked == true");
        StringAssert.Contains(source, "OpenXrRuntimeCompatibility.ResolveLaunchSelection()");
        StringAssert.Contains(source, "SteamVrAirLinkCompatibility.ResolveLaunchEnvironment()");
        StringAssert.Contains(source, "startInfo.Environment[variable.Key] = variable.Value");
        Assert.IsFalse(
            source.Contains("Environment.SetEnvironmentVariable", StringComparison.Ordinal),
            "Air Link compatibility must not change the process or machine-wide OpenXR runtime.");

        var resolver = ReadLauncherFile(Path.Combine(
            "WorldAtWarVR.Launcher.Core",
            "SteamVrAirLinkCompatibility.cs"));
        StringAssert.Contains(resolver, "RegistryView.Registry32");
        StringAssert.Contains(resolver, "steamxr_win32.json");
        StringAssert.Contains(resolver, "EnvironmentVariableTarget.Machine");

        var automaticResolver = ReadLauncherFile(Path.Combine(
            "WorldAtWarVR.Launcher.Core",
            "OpenXrRuntimeCompatibility.cs"));
        StringAssert.Contains(automaticResolver, "RegistryView.Registry32");
        StringAssert.Contains(automaticResolver, "RegistryView.Registry64");
        StringAssert.Contains(automaticResolver, "RegistryHive.CurrentUser");
        StringAssert.Contains(automaticResolver, "PiOpenXR_32.json");
        StringAssert.Contains(automaticResolver, "ImageFileMachineI386");

        var automaticSelection = source.IndexOf(
            "OpenXrRuntimeCompatibility.ResolveLaunchSelection()",
            StringComparison.Ordinal);
        var explicitSteamVrSelection = source.IndexOf(
            "SteamVrAirLinkCompatibility.ResolveLaunchEnvironment()",
            StringComparison.Ordinal);
        Assert.IsTrue(
            automaticSelection >= 0 && explicitSteamVrSelection > automaticSelection,
            "Automatic Pimax x86 repair must run before the explicit SteamVR compatibility override.");
    }

    [TestMethod]
    public void PaidDistributionLauncher_IsOfflineAndDefersUpdatesAndSupportToReleaseProvider()
    {
        var document = LoadXaml("MainPage.xaml");
        var providerNotice = RequiredElementByName(document, "ReleaseProviderNoticeText");
        Assert.AreEqual("TextBlock", providerNotice.Name.LocalName);
        AssertAttribute(
            providerNotice,
            "Text",
            "Get updates, known issues, and support from the service where you received this release.");
        AssertAttribute(providerNotice, "TextWrapping", "Wrap");

        Assert.IsFalse(
            document.Descendants().Any(element => element.Name.LocalName == "HyperlinkButton"),
            "The packaged launcher must not expose release or support web links.");
        Assert.IsFalse(
            document.Descendants().Any(element => element.Attribute("NavigateUri") is not null),
            "The packaged launcher must not contain navigation URLs.");

        var source = ReadLauncherFile("MainPage.xaml.cs");
        foreach (var forbidden in new[]
                 {
                     "GitHubUpdateService",
                     "StartStartupUpdateFlow",
                     "RunStartupUpdateFlowAsync",
                     "FindAvailableUpdateAsync",
                     "DownloadInstallerAsync",
                 })
        {
            Assert.IsFalse(
                source.Contains(forbidden, StringComparison.Ordinal),
                $"The packaged launcher must not retain the updater entry point '{forbidden}'.");
        }

        var coreDirectory = Path.Combine(LauncherDirectory, "WorldAtWarVR.Launcher.Core");
        Assert.IsFalse(File.Exists(Path.Combine(coreDirectory, "GitHubUpdateService.cs")));
        foreach (var productionSource in Directory.EnumerateFiles(
                     coreDirectory,
                     "*.cs",
                     SearchOption.TopDirectoryOnly))
        {
            var contents = File.ReadAllText(productionSource);
            Assert.IsFalse(
                contents.Contains("api.github.com", StringComparison.OrdinalIgnoreCase),
                $"{Path.GetFileName(productionSource)} must not contain an automatic update endpoint.");
            Assert.IsFalse(
                contents.Contains("github.com/", StringComparison.OrdinalIgnoreCase),
                $"{Path.GetFileName(productionSource)} must not contain a release or issue URL.");
        }
    }

    [TestMethod]
    public void PublicBranding_UsesWorldWarVrAndPreservesCompatibilityIdentities()
    {
        var mainWindow = LoadXaml("MainWindow.xaml");
        AssertAttribute(mainWindow.Root!, "Title", "World War VR");
        var mainWindowSource = ReadLauncherFile("MainWindow.xaml");
        StringAssert.Contains(mainWindowSource, "Text=\"WORLD WAR \"");
        Assert.IsFalse(mainWindowSource.Contains("WORLD AT WAR", StringComparison.Ordinal));

        var project = XDocument.Load(Path.Combine(LauncherDirectory, "WorldAtWarVR.Launcher.csproj"));
        AssertProjectValue(project, "Product", "World War VR");
        AssertProjectValue(project, "Title", "World War VR");
        AssertProjectValue(project, "Description", "A standalone VR mod for Call of Duty: World at War.");
        AssertProjectValue(project, "AssemblyName", "WorldWarVR.Launcher");

        var coreProject = XDocument.Load(Path.Combine(
            LauncherDirectory,
            "WorldAtWarVR.Launcher.Core",
            "WorldAtWarVR.Launcher.Core.csproj"));
        AssertProjectValue(coreProject, "AssemblyName", "WorldWarVR.Launcher.Core");
        AssertProjectValue(coreProject, "RootNamespace", "WorldAtWarVR.Launcher.Core");

        var publishMoves = project
            .Descendants()
            .Where(element => element.Name.LocalName == "Move")
            .Select(element => (string?)element.Attribute("DestinationFiles"))
            .ToArray();
        CollectionAssert.Contains(publishMoves, "$(PublishDir)WorldWarVR.exe");
        CollectionAssert.Contains(publishMoves, "$(PublishDir)WorldWarVR.pri");
        CollectionAssert.DoesNotContain(publishMoves, "$(PublishDir)WorldAtWarVR.exe");
        CollectionAssert.DoesNotContain(publishMoves, "$(PublishDir)WorldAtWarVR.pri");

        var publishSources = project
            .Descendants()
            .Where(element => element.Name.LocalName == "Move")
            .Select(element => (string?)element.Attribute("SourceFiles"))
            .ToArray();
        CollectionAssert.Contains(publishSources, "$(PublishDir)WorldWarVR.Launcher.exe");
        CollectionAssert.Contains(publishSources, "$(PublishDir)WorldWarVR.Launcher.pri");
        CollectionAssert.DoesNotContain(publishSources, "$(PublishDir)WorldAtWarVR.Launcher.exe");
        CollectionAssert.DoesNotContain(publishSources, "$(PublishDir)WorldAtWarVR.Launcher.pri");

        var mainPage = LoadXaml("MainPage.xaml");
        var providerNotice = RequiredElementByName(mainPage, "ReleaseProviderNoticeText");
        StringAssert.Contains(
            (string?)providerNotice.Attribute("Text") ?? string.Empty,
            "service where you received this release");

        StringAssert.Contains(ReadLauncherFile("App.xaml.cs"), "\"WorldAtWarVR\"");
        StringAssert.Contains(
            ReadLauncherFile(Path.Combine("WorldAtWarVR.Launcher.Core", "LauncherCommandBuilder.cs")),
            "ModDllFileName = \"WorldWarVR.dll\"");
    }

    [TestMethod]
    public void StandalonePackager_AppliesOneValidatedVersionToLauncherAndInstaller()
    {
        var packager = File.ReadAllText(Path.Combine(
            RepositoryDirectory,
            "scripts",
            "package-standalone.ps1"));

        StringAssert.Contains(packager, "[ValidatePattern('^(?:0|[1-9][0-9]*)");
        const string versionPattern = @"\[string\]\$Version\s*=\s*'([^']+)'";
        var standaloneVersion = Regex.Match(packager, versionPattern);
        Assert.IsTrue(standaloneVersion.Success, "The standalone release must declare a default version.");
        var version = standaloneVersion.Groups[1].Value;
        AssertRegex(version,
            @"^(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-(?:alpha|beta|rc)\.(?:0|[1-9][0-9]*))?$",
            "The release default must be a valid supported semantic version.");
        var patreon = File.ReadAllText(Path.Combine(RepositoryDirectory, "scripts", "package-patreon.ps1"));
        var patreonVersion = Regex.Match(patreon, versionPattern);
        Assert.IsTrue(patreonVersion.Success, "The Patreon release must declare a default version.");
        Assert.AreEqual(version, patreonVersion.Groups[1].Value,
            "Both release packagers must default to the same version.");
        var project = XDocument.Load(Path.Combine(LauncherDirectory, "WorldAtWarVR.Launcher.csproj"));
        Assert.AreEqual(version, project.Descendants().Single(element => element.Name.LocalName == "Version").Value,
            "The launcher project and release packagers must agree on the default version.");
        StringAssert.Contains(packager, "CMAKE_COMMAND:INTERNAL=");
        StringAssert.Contains(packager, "& $nativeCmake --version");
        StringAssert.Contains(packager, "[string]$InnoCompiler = ''");
        StringAssert.Contains(packager, "Version components must fit the Windows file-version range");
        StringAssert.Contains(packager, "\"-p:Version=$Version\"");
        StringAssert.Contains(packager, "\"-p:FileVersion=$fileVersion\"");
        StringAssert.Contains(packager, "\"-p:AssemblyVersion=$fileVersion\"");
        StringAssert.Contains(packager, "\"-p:InformationalVersion=$Version\"");
        AssertRegex(
            packager,
            @"dotnet\s+test[\s\S]*?--runtime\s+win-x64",
            "Managed release tests must use the same win-x64 runtime as the published launcher.");
        AssertRegex(
            packager,
            @"&\s+\$installerBuildScript[\s\S]*?-Version\s+\$Version[\s\S]*?-InnoCompiler\s+\$InnoCompiler",
            "The standalone packager must forward its version and compiler to the installer builder.");
    }

    [TestMethod]
    public void LauncherArtwork_IsRelativeDeclaredAndNotShownAsTheOldTitleBarIcon()
    {
        var mainWindow = LoadXaml("MainWindow.xaml");
        var windowRoot = RequiredElementByName(mainWindow, "WindowRoot");
        var background = RequiredElementByName(mainWindow, "BackgroundTexture");
        Assert.AreEqual("Image", background.Name.LocalName);
        AssertAttribute(background, "Source", BackgroundAsset.Replace('\\', '/'));
        AssertAttribute(background, "HorizontalAlignment", "Stretch");
        AssertAttribute(background, "VerticalAlignment", "Stretch");
        AssertAttribute(background, "Stretch", "Fill");
        AssertAttribute(background, "IsHitTestVisible", "False");
        AssertAttribute(background, "AutomationProperties.AccessibilityView", "Raw");
        Assert.AreSame(windowRoot, background.Parent, "The physical-window background must be a WindowRoot child.");

        var backgroundTint = RequiredElementByName(mainWindow, "BackgroundTint");
        Assert.AreSame(windowRoot, backgroundTint.Parent);
        AssertAttribute(backgroundTint, "IsHitTestVisible", "False");

        var rootChildren = windowRoot.Elements().ToList();
        Assert.AreSame(background, rootChildren[0], "BackgroundTexture must be the first WindowRoot child.");
        Assert.AreSame(backgroundTint, rootChildren[1], "BackgroundTint must immediately follow the texture.");
        Assert.IsTrue(
            rootChildren.IndexOf(backgroundTint) < rootChildren.IndexOf(RequiredElementByName(mainWindow, "ShellScaler")),
            "The full-window background and tint must render behind the scaled interactive shell.");

        var mainPage = LoadXaml("MainPage.xaml");
        Assert.AreEqual(
            0,
            mainPage.Descendants().Count(element =>
                (string?)element.Attribute(XamlNamespace + "Name") == "BackgroundTexture"),
            "MainPage must not retain a duplicate fixed-design background texture.");
        AssertAttribute(mainPage.Root!, "Background", "Transparent");
        AssertAttribute(RequiredElementByName(mainPage, "PageShell"), "Background", "Transparent");

        var projectPath = Path.Combine(LauncherDirectory, "WorldAtWarVR.Launcher.csproj");
        var project = XDocument.Load(projectPath);
        var backgroundItems = project
            .Descendants()
            .Where(element => element.Name.LocalName == "Content")
            .Where(element => NormalizeAssetPath((string?)element.Attribute("Include")) == BackgroundAsset)
            .ToArray();
        Assert.AreEqual(1, backgroundItems.Length, "The launcher background must have one explicit Content item.");
        AssertChildValue(backgroundItems[0], "CopyToOutputDirectory", "PreserveNewest");
        AssertChildValue(backgroundItems[0], "CopyToPublishDirectory", "PreserveNewest");
        Assert.IsTrue(
            File.Exists(Path.Combine(LauncherDirectory, BackgroundAsset)),
            "The declared launcher background asset is missing.");

        var contentIncludes = project
            .Descendants()
            .Where(element => element.Name.LocalName == "Content")
            .Select(element => NormalizeAssetPath((string?)element.Attribute("Include")))
            .ToArray();
        CollectionAssert.DoesNotContain(contentIncludes, @"Assets\AppIcon.ico");
        CollectionAssert.DoesNotContain(contentIncludes, @"Assets\WorldAtWarVR-Icon.png");

        var iconItems = project
            .Descendants()
            .Where(element => element.Name.LocalName == "Content")
            .Where(element => NormalizeAssetPath((string?)element.Attribute("Include")) == @"Assets\WorldAtWarVR-Icon.ico")
            .ToArray();
        Assert.AreEqual(1, iconItems.Length, "The authored ICO must have one explicit Content item.");
        AssertChildValue(iconItems[0], "TargetPath", @"Assets\WorldWarVR-Icon.ico");
        AssertChildValue(iconItems[0], "CopyToOutputDirectory", "PreserveNewest");
        AssertChildValue(iconItems[0], "CopyToPublishDirectory", "PreserveNewest");

        Assert.AreEqual(
            0,
            mainWindow.Descendants().Count(element => element.Name.LocalName == "ImageIconSource"),
            "The old icon must not be rendered in the custom title bar.");
        Assert.IsFalse(
            ReadLauncherFile("MainWindow.xaml").Contains("WorldWarVR-Icon", StringComparison.OrdinalIgnoreCase),
            "MainWindow.xaml must remain a text-only title bar.");

        var mainWindowCode = ReadLauncherFile("MainWindow.xaml.cs");
        StringAssert.Contains(mainWindowCode, "AppWindow.SetIcon");
        AssertRegex(
            mainWindowCode,
            "\"Assets\"\\s*,\\s*\"WorldWarVR-Icon\\.ico\"",
            "The replacement ICO must remain the native window/taskbar icon.");
    }

    [TestMethod]
    public void WindowResize_UniformlyScalesOneFixedDesignSurface()
    {
        var mainWindow = LoadXaml("MainWindow.xaml");
        var windowRoot = RequiredElementByName(mainWindow, "WindowRoot");
        AssertAttribute(windowRoot, "SizeChanged", "WindowRoot_SizeChanged");

        var scaler = RequiredElementByName(mainWindow, "ShellScaler");
        Assert.AreEqual("Viewbox", scaler.Name.LocalName);
        AssertAttribute(scaler, "Stretch", "Uniform");
        AssertAttribute(scaler, "StretchDirection", "DownOnly");
        AssertAttribute(scaler, "HorizontalAlignment", "Center");
        AssertAttribute(scaler, "VerticalAlignment", "Top");

        var designSurface = RequiredElementByName(mainWindow, "DesignSurface");
        Assert.AreSame(scaler, designSurface.Parent, "DesignSurface must be the single child scaled by ShellScaler.");
        AssertAttribute(designSurface, "Width", "1440");
        AssertAttribute(designSurface, "Height", "970");
        AssertAttribute(designSurface, "Background", "Transparent");
        var fixedRows = designSurface
            .Elements()
            .Where(element => element.Name.LocalName == "Grid.RowDefinitions")
            .SelectMany(element => element.Elements())
            .Select(element => (string?)element.Attribute("Height"))
            .ToArray();
        CollectionAssert.AreEqual(new[] { "116", "854" }, fixedRows);

        var rootFrame = RequiredElementByName(mainWindow, "RootFrame");
        Assert.IsTrue(
            rootFrame.Ancestors().Contains(designSurface),
            "RootFrame must remain inside the uniformly scaled design surface.");
        AssertAttribute(rootFrame, "Grid.Row", "1");
        AssertAttribute(rootFrame, "Background", "Transparent");

        var dragRegion = RequiredElementByName(mainWindow, "TitleBarDragRegion");
        Assert.AreSame(windowRoot, dragRegion.Parent, "The title-bar drag region must remain outside ShellScaler.");
        Assert.IsFalse(
            dragRegion.Ancestors().Contains(scaler),
            "Native title-bar input must not be down-scaled with the visual shell.");
        AssertAttribute(dragRegion, "Background", "Transparent");
        AssertAttribute(dragRegion, "HorizontalAlignment", "Center");
        AssertAttribute(dragRegion, "VerticalAlignment", "Top");
        Assert.IsNull(
            dragRegion.Attribute("IsHitTestVisible"),
            "Do not disable hit testing on the title-bar drag region.");
        Assert.IsTrue(
            windowRoot.Elements().ToList().IndexOf(dragRegion) > windowRoot.Elements().ToList().IndexOf(scaler),
            "The unscaled drag region must render after the scaled surface as its input overlay.");

        var mainPage = LoadXaml("MainPage.xaml");
        Assert.AreEqual(
            0,
            mainPage.Descendants().Count(element => element.Name.LocalName == "AdaptiveTrigger"),
            "Breakpoint-driven rearrangement conflicts with proportional scaling.");
        Assert.AreEqual(
            0,
            mainPage.Descendants().Count(element =>
                (string?)element.Attribute(XamlNamespace + "Name") == "LayoutStates"),
            "The old responsive LayoutStates group must remain removed.");
        _ = RequiredElementByName(mainPage, "ContrastStates");

        var codeBehind = ReadLauncherFile("MainWindow.xaml.cs");
        StringAssert.Contains(codeBehind, "SetTitleBar(TitleBarDragRegion)");
        StringAssert.Contains(codeBehind, "AppWindow.ResizeClient");
        Assert.IsFalse(
            Regex.IsMatch(codeBehind, @"\bAppWindow\.Resize\s*\(", RegexOptions.CultureInvariant),
            "Initial size must describe the 1440x970 client design surface, not the outer window.");
        AssertRegex(codeBehind, @"DesignWidth\s*=\s*1440(?:\.0)?\s*;", "DesignWidth must stay 1440.");
        AssertRegex(codeBehind, @"DesignHeight\s*=\s*970(?:\.0)?\s*;", "DesignHeight must stay 970.");
        AssertRegex(codeBehind, @"DesignTitleBarHeight\s*=\s*116(?:\.0)?\s*;", "Title-bar design height must stay 116.");
        AssertRegex(codeBehind, @"MinimumScale\s*=\s*0\.70\s*;", "Minimum window scale must remain 70 percent.");
        AssertRegex(
            codeBehind,
            @"PreferredMinimumWidth\s*=\s*\(int\)Math\.Round\(DesignWidth\s*\*\s*MinimumScale\)",
            "Minimum width must be derived from the fixed design width and scale.");
        AssertRegex(
            codeBehind,
            @"PreferredMinimumHeight\s*=\s*\(int\)Math\.Round\(DesignHeight\s*\*\s*MinimumScale\)",
            "Minimum height must be derived from the fixed design height and scale.");
        AssertRegex(
            codeBehind,
            @"Math\.Min\(\s*1\.0\s*,\s*Math\.Min\(\s*e\.NewSize\.Width\s*/\s*DesignWidth\s*,\s*e\.NewSize\.Height\s*/\s*DesignHeight\s*\)\s*\)",
            "The drag-region scale must use the same uniform, down-only fit as ShellScaler.");
        AssertRegex(
            codeBehind,
            @"TitleBarDragRegion\.Width\s*=\s*e\.NewSize\.Width\s*;",
            "The unscaled drag region must cover the full client width.");
        AssertRegex(
            codeBehind,
            @"TitleBarDragRegion\.Height\s*=\s*Math\.Max\(\s*1\.0\s*,\s*DesignTitleBarHeight\s*\*\s*scale\s*\)\s*;",
            "The unscaled drag region height must follow the scaled visual title bar.");

        AssertRegex(
            codeBehind,
            @"new\s+AccessibilitySettings\s*\(\s*\)\.HighContrast",
            "MainWindow must check high contrast before exposing decorative background artwork.");
        AssertRegex(
            codeBehind,
            @"BackgroundTexture\.Visibility\s*=\s*Visibility\.Collapsed\s*;",
            "High contrast must hide the decorative physical-window texture.");
        AssertRegex(
            codeBehind,
            @"BackgroundTint\.Visibility\s*=\s*Visibility\.Collapsed\s*;",
            "High contrast must hide the decorative tint as well as the texture.");
        AssertRegex(
            codeBehind,
            "WindowRoot\\.Background\\s*=\\s*\\(Microsoft\\.UI\\.Xaml\\.Media\\.Brush\\)\\s*Application\\.Current\\.Resources\\s*\\[\\s*\"LauncherBaseBrush\"\\s*\\]",
            "High contrast must restore the accessible launcher base brush behind the transparent shell.");

        var staleBackgroundTargets = mainPage
            .Descendants()
            .Attributes("Target")
            .Where(attribute => attribute.Value.StartsWith("BackgroundTexture.", StringComparison.Ordinal))
            .ToArray();
        Assert.AreEqual(0, staleBackgroundTargets.Length, "MainPage must not target a background in another XAML namescope.");
    }

    [TestMethod]
    public void AuthoredLauncherSources_DoNotContainAbsoluteDeveloperPaths()
    {
        var drivePath = new Regex(
            @"(?<![A-Za-z0-9_])[A-Za-z]:[\\/]",
            RegexOptions.CultureInvariant);
        var uncPath = new Regex(
            @"(?<![:/\\])\\{2,4}[A-Za-z0-9_.-]+\\",
            RegexOptions.CultureInvariant);
        var unixHomePath = new Regex(
            @"(?<![:A-Za-z0-9_])/(?:Users|home|tmp)/",
            RegexOptions.CultureInvariant | RegexOptions.IgnoreCase);

        var offendingFiles = AuthoredLauncherSourceFiles()
            .Where(path =>
            {
                var source = File.ReadAllText(path);
                return drivePath.IsMatch(source) || uncPath.IsMatch(source) || unixHomePath.IsMatch(source);
            })
            .Select(path => Path.GetRelativePath(LauncherDirectory, path))
            .ToArray();

        Assert.AreEqual(
            0,
            offendingFiles.Length,
            $"Authored launcher source must not contain absolute developer paths: {string.Join(", ", offendingFiles)}");
    }

    private static void AssertElementContract(
        XDocument document,
        string name,
        string attributeName,
        string attributeValue)
    {
        AssertAttribute(RequiredElementByName(document, name), attributeName, attributeValue);
    }

    private static XElement RequiredElementByName(XDocument document, string name)
    {
        var matches = document
            .Descendants()
            .Where(element => (string?)element.Attribute(XamlNamespace + "Name") == name)
            .ToArray();
        Assert.AreEqual(1, matches.Length, $"Expected exactly one x:Name=\"{name}\" element.");
        return matches[0];
    }

    private static void AssertAttribute(XElement element, string name, string expected)
    {
        Assert.AreEqual(expected, (string?)element.Attribute(name), $"Unexpected {name} on {element.Name.LocalName}.");
    }

    private static void AssertChildValue(XElement element, string name, string expected)
    {
        var children = element.Elements().Where(child => child.Name.LocalName == name).ToArray();
        Assert.AreEqual(1, children.Length, $"Expected exactly one {name} child.");
        Assert.AreEqual(expected, children[0].Value.Trim(), $"Unexpected {name} value.");
    }

    private static void AssertProjectValue(XDocument project, string name, string expected)
    {
        var values = project
            .Descendants()
            .Where(element => element.Name.LocalName == name)
            .Select(element => element.Value.Trim())
            .ToArray();
        Assert.AreEqual(1, values.Length, $"Expected exactly one {name} project property.");
        Assert.AreEqual(expected, values[0], $"Unexpected {name} project property.");
    }

    private static void AssertPresetApplied(string source, string radioName, string presetName)
    {
        AssertRegex(
            source,
            $@"\b{Regex.Escape(radioName)}\.IsChecked\s*=\s*_settings\.Resolution\s*==\s*VrResolutionPreset\.{Regex.Escape(presetName)}\s*;",
            $"{radioName} must be initialized from VrResolutionPreset.{presetName} explicitly.");
    }

    private static void AssertResolutionRadio(
        XDocument document,
        string radioName,
        string automationName,
        string label,
        string dimensions)
    {
        var radio = RequiredElementByName(document, radioName);
        AssertAttribute(radio, "AutomationProperties.Name", automationName);
        var text = radio
            .Descendants()
            .Select(element => (string?)element.Attribute("Text"))
            .Where(value => value is not null)
            .ToArray();
        CollectionAssert.Contains(text, label);
        CollectionAssert.Contains(text, dimensions);
    }

    private static void AssertRegex(string source, string pattern, string message)
    {
        Assert.IsTrue(
            Regex.IsMatch(source, pattern, RegexOptions.CultureInvariant | RegexOptions.Singleline),
            message);
    }

    private static IEnumerable<XDocument> LoadAllXamlDocuments()
    {
        return Directory
            .EnumerateFiles(LauncherDirectory, "*.xaml", SearchOption.TopDirectoryOnly)
            .Select(XDocument.Load);
    }

    private static XDocument LoadXaml(string fileName)
    {
        return XDocument.Load(Path.Combine(LauncherDirectory, fileName));
    }

    private static string ReadLauncherFile(string fileName)
    {
        return File.ReadAllText(Path.Combine(LauncherDirectory, fileName));
    }

    private static string NormalizeAssetPath(string? path)
    {
        return (path ?? string.Empty).Replace('/', '\\');
    }

    private static IEnumerable<string> AuthoredLauncherSourceFiles()
    {
        var extensions = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            ".cs",
            ".csproj",
            ".manifest",
            ".xaml",
        };

        return Directory
            .EnumerateFiles(LauncherDirectory, "*", SearchOption.AllDirectories)
            .Where(path => extensions.Contains(Path.GetExtension(path)))
            .Where(path => !HasPathSegment(path, "WorldAtWarVR.Launcher.Core.Tests"))
            .Where(path => !HasPathSegment(path, "bin"))
            .Where(path => !HasPathSegment(path, "obj"));
    }

    private static bool HasPathSegment(string path, string segment)
    {
        return path
            .Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
            .Contains(segment, StringComparer.OrdinalIgnoreCase);
    }

    private static string LauncherDirectory
    {
        get
        {
            var directory = new DirectoryInfo(AppContext.BaseDirectory);
            while (directory is not null)
            {
                if (File.Exists(Path.Combine(directory.FullName, "WorldAtWarVR.Launcher.csproj")))
                {
                    return directory.FullName;
                }

                directory = directory.Parent;
            }

            throw new AssertFailedException(
                $"Could not locate WorldAtWarVR.Launcher.csproj above {AppContext.BaseDirectory}.");
        }
    }

    private static string RepositoryDirectory => Path.GetFullPath(Path.Combine(
        LauncherDirectory,
        "..",
        ".."));
}
