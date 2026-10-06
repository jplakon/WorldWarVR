using System.Diagnostics;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.UI.ViewManagement;
using Windows.Storage.Pickers;
using WorldAtWarVR.Launcher.Core;

namespace WorldAtWarVR_Launcher;

public sealed partial class MainPage : Page
{
    private readonly LauncherSettingsStore _settingsStore = new();
    private readonly DispatcherQueueTimer _validationTimer;
    private LauncherSettings _settings = new();
    private GameInstallationValidation? _validation;
    private CancellationTokenSource? _validationCancellation;
    private bool _initializing = true;
    private bool _launching;

    public bool IsHighContrast { get; } = new AccessibilitySettings().HighContrast;

    public MainPage()
    {
        InitializeComponent();
        _validationTimer = DispatcherQueue.CreateTimer();
        _validationTimer.Interval = TimeSpan.FromMilliseconds(550);
        _validationTimer.IsRepeating = false;
        _validationTimer.Tick += ValidationTimer_Tick;
        Loaded += MainPage_Loaded;
        Unloaded += MainPage_Unloaded;
    }

    private async void MainPage_Loaded(object sender, RoutedEventArgs e)
    {
        _settings = await _settingsStore.LoadAsync();
        if (string.IsNullOrWhiteSpace(_settings.GameDirectory))
        {
            SetActivity("Looking for your Steam installation…", busy: true);
            var detected = await Task.Run(SteamInstallDetector.FindCandidateGameDirectories);
            var validated = await Task.WhenAll(
                detected.Select(path => GameInstallationValidator.ValidateAsync(path)));
            var best = GameInstallationValidator.ChooseBestCandidate(validated);
            _settings = _settings with { GameDirectory = best?.Directory ?? string.Empty };
        }

        ApplySettingsToControls();
        _initializing = false;
        await ValidateCurrentDirectoryAsync();
        await SaveSettingsQuietlyAsync();
    }

    private void MainPage_Unloaded(object sender, RoutedEventArgs e)
    {
        _validationTimer.Stop();
        _validationCancellation?.Cancel();
        _validationCancellation?.Dispose();
    }

    private void ApplySettingsToControls()
    {
        GamePathTextBox.Text = _settings.GameDirectory;
        MainMenuRadio.IsChecked = _settings.LaunchTarget == GameLaunchTarget.MainMenu;
        MultiplayerRadio.IsChecked = _settings.LaunchTarget == GameLaunchTarget.Multiplayer;
        BotsCheckBox.IsChecked = _settings.AutomaticBots;
        AirLinkSteamVrCheckBox.IsChecked = _settings.QuestAirLinkCompatibility;
        AutomaticReloadCheckBox.IsChecked = _settings.AutomaticReload;
        ButtonGrenadesCheckBox.IsChecked = _settings.ButtonGrenades;
        SmoothTurningCheckBox.IsChecked = _settings.SmoothTurning;
        NativeRadio.IsChecked = _settings.Resolution == VrResolutionPreset.Recommended;
        PerformanceRadio.IsChecked = _settings.Resolution == VrResolutionPreset.HighQuality;
        RecoveryRadio.IsChecked = _settings.Resolution == VrResolutionPreset.Performance;
        UpdateTargetPresentation();
        UpdateResolutionPresentation();
    }

    private LauncherSettings ReadSettingsFromControls()
    {
        return (_settings with
        {
            GameDirectory = GamePathTextBox.Text,
            LaunchTarget = MultiplayerRadio.IsChecked == true
                ? GameLaunchTarget.Multiplayer
                : GameLaunchTarget.MainMenu,
            AutomaticBots = BotsCheckBox.IsChecked == true,
            QuestAirLinkCompatibility = AirLinkSteamVrCheckBox.IsChecked == true,
            AutomaticReload = AutomaticReloadCheckBox.IsChecked == true,
            ButtonGrenades = ButtonGrenadesCheckBox.IsChecked == true,
            SmoothTurning = SmoothTurningCheckBox.IsChecked == true,
            Resolution = GetSelectedResolution(),
        }).Normalize();
    }

    private void GamePathTextBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        if (_initializing)
        {
            return;
        }

        _validationTimer.Stop();
        _validationTimer.Start();
        ActivityStatusText.Text = "Waiting to check the selected folder…";
    }

    private async void ValidationTimer_Tick(DispatcherQueueTimer sender, object args)
    {
        await ValidateCurrentDirectoryAsync();
        await SaveSettingsQuietlyAsync();
    }

    private async void BrowseButton_Click(object sender, RoutedEventArgs e)
    {
        var window = App.MainWindowInstance;
        if (window is null)
        {
            ShowActivityError("The folder picker could not be opened. Restart the launcher and try again.");
            return;
        }

        try
        {
            var picker = new FolderPicker
            {
                SuggestedStartLocation = PickerLocationId.ComputerFolder,
            };
            picker.FileTypeFilter.Add("*");
            WinRT.Interop.InitializeWithWindow.Initialize(
                picker, WinRT.Interop.WindowNative.GetWindowHandle(window));

            var folder = await picker.PickSingleFolderAsync();
            if (folder is null)
            {
                return;
            }

            _validationTimer.Stop();
            GamePathTextBox.Text = folder.Path;
            await ValidateCurrentDirectoryAsync();
            await SaveSettingsQuietlyAsync();
        }
        catch (Exception exception) when (
            exception is System.Runtime.InteropServices.COMException or
            IOException or UnauthorizedAccessException or InvalidOperationException or
            ArgumentException or NotSupportedException or
            System.Security.SecurityException)
        {
            Debug.WriteLine($"World War VR folder picker failed: {exception}");
            ShowActivityError(
                "Windows could not open the folder picker. You can still paste " +
                "your Call of Duty World at War folder into the path box above.");
        }
    }

    private async void LaunchTarget_Checked(object sender, RoutedEventArgs e)
    {
        if (_initializing)
        {
            return;
        }

        UpdateTargetPresentation();
        UpdateValidationPresentation();
        await SaveSettingsQuietlyAsync();
    }

    private async void SettingControl_Changed(object sender, RoutedEventArgs e)
    {
        UpdateLaunchSummary();
        if (!_initializing)
        {
            await SaveSettingsQuietlyAsync();
        }
    }

    private async void ResolutionRadio_Checked(object sender, RoutedEventArgs e)
    {
        UpdateResolutionPresentation();
        if (!_initializing)
        {
            await SaveSettingsQuietlyAsync();
        }
    }

    private void UpdateTargetPresentation()
    {
        var multiplayer = MultiplayerRadio.IsChecked == true;
        MultiplayerInfoBar.IsOpen = multiplayer;
        UpdateLaunchSummary();
    }

    private void UpdateResolutionPresentation()
    {
        var choice = ResolutionChoices.Get(GetSelectedResolution());
        ResolutionDetailText.Text = $"{choice.Detail} · {choice.Width} × {choice.Height}";
        UpdateLaunchSummary();
    }

    private VrResolutionPreset GetSelectedResolution()
    {
        if (RecoveryRadio.IsChecked == true)
        {
            return VrResolutionPreset.Performance;
        }

        return PerformanceRadio.IsChecked == true
            ? VrResolutionPreset.HighQuality
            : VrResolutionPreset.Recommended;
    }

    private async Task<bool> ValidateCurrentDirectoryAsync()
    {
        _validationCancellation?.Cancel();
        _validationCancellation?.Dispose();
        _validationCancellation = new CancellationTokenSource();
        var token = _validationCancellation.Token;

        SetActivity("Checking the game files…", busy: true);
        try
        {
            var validation = await GameInstallationValidator.ValidateAsync(
                GamePathTextBox.Text,
                token);
            if (token.IsCancellationRequested)
            {
                return false;
            }

            _validation = validation;
            UpdateValidationPresentation();
            SetActivity("Settings are saved automatically.", busy: false);
            return true;
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
            // A newer folder selection superseded this validation.
            return false;
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or
            InvalidOperationException or ArgumentException or NotSupportedException or
            System.Security.SecurityException)
        {
            _validation = null;
            UpdateValidationPresentation();
            ShowActivityError(
                $"The selected game files could not be checked: {exception.Message}");
            return false;
        }
    }

    private void UpdateValidationPresentation()
    {
        if (_validation is null)
        {
            InstallationInfoBar.Severity = InfoBarSeverity.Informational;
            InstallationInfoBar.Title = "Select your game";
            InstallationInfoBar.Message = string.Empty;
            LaunchButton.IsEnabled = false;
            InstallationStatusText.Text = "Select your game installation";
            InstallationStatusText.Foreground = ResourceBrush("LauncherSecondaryTextBrush");
            InstallationStatusRing.Stroke = ResourceBrush("LauncherBorderBrush");
            InstallationStatusIcon.Foreground = ResourceBrush("LauncherSecondaryTextBrush");
            UpdateLaunchSummary();
            return;
        }

        var target = MultiplayerRadio.IsChecked == true
            ? GameLaunchTarget.Multiplayer
            : GameLaunchTarget.MainMenu;
        var ready = _validation.IsReadyFor(target);
        var anyReady = _validation.MainMenuReady || _validation.MultiplayerReady;

        InstallationInfoBar.Severity = ready
            ? InfoBarSeverity.Success
            : anyReady ? InfoBarSeverity.Warning : InfoBarSeverity.Error;
        InstallationInfoBar.Title = ready
            ? "Installation ready"
            : anyReady ? "Selected target needs attention" : "Installation not supported";
        InstallationInfoBar.Message = _validation.ReadySummary;
        LaunchButton.IsEnabled = ready && !_launching;
        UpdateInstallationStatus(ready, anyReady);
        UpdateLaunchSummary();
    }

    private void UpdateInstallationStatus(bool ready, bool anyReady)
    {
        var steamBuild = MultiplayerRadio.IsChecked == true
            ? _validation?.MultiplayerBuild
            : _validation?.SinglePlayerBuild;
        var status = ready
            ? steamBuild == SupportedGameBuild.SteamBuild252004
                ? "Steam installation verified"
                : "Compatible installation verified"
            : anyReady
                ? "Selected target needs attention"
                : _validation?.Detail ?? "Installation not supported";
        var language = _validation?.InstalledLanguage;
        InstallationStatusText.Text = language is null
            ? status
            : $"{status} · Language: {language.DisplayName}";

        var brush = ready
            ? ResourceBrush("LauncherAccentBrush")
            : anyReady
                ? new SolidColorBrush(Microsoft.UI.Colors.Goldenrod)
                : new SolidColorBrush(Microsoft.UI.Colors.OrangeRed);
        InstallationStatusText.Foreground = brush;
        InstallationStatusRing.Stroke = brush;
        InstallationStatusIcon.Foreground = brush;
    }

    private void UpdateLaunchSummary()
    {
        var target = MultiplayerRadio.IsChecked == true
            ? GameLaunchTarget.Multiplayer
            : GameLaunchTarget.MainMenu;
        if (_validation?.IsReadyFor(target) != true)
        {
            LaunchSummaryText.Text = target == GameLaunchTarget.Multiplayer
                ? "Select an installation that is ready for Multiplayer."
                : "Select an installation that is ready for Zombies and Campaign.";
            return;
        }

        LaunchSummaryText.Text = AirLinkSteamVrCheckBox.IsChecked == true
            ? "Ready to launch in VR through SteamVR"
            : "Ready to launch in VR";
    }

    private async void LaunchButton_Click(object sender, RoutedEventArgs e)
    {
        if (_launching)
        {
            return;
        }

        _validationTimer.Stop();
        if (!await ValidateCurrentDirectoryAsync())
        {
            return;
        }

        var settings = ReadSettingsFromControls();
        if (_validation?.IsReadyFor(settings.LaunchTarget) != true)
        {
            ShowActivityError("The selected installation is not ready for this launch target.");
            return;
        }

        LauncherCommand command;
        Dictionary<string, string> launchEnvironment;
        try
        {
            command = LauncherCommandBuilder.Build(
                AppContext.BaseDirectory,
                settings,
                _validation);
            launchEnvironment = new Dictionary<string, string>(
                GameplayOptionEnvironment.Resolve(settings),
                StringComparer.OrdinalIgnoreCase);
            var openXrSelection =
                OpenXrRuntimeCompatibility.ResolveLaunchSelection();
            foreach (var variable in openXrSelection.EnvironmentVariables)
            {
                launchEnvironment[variable.Key] = variable.Value;
            }
            if (settings.QuestAirLinkCompatibility)
            {
                // This is an explicit SteamVR choice and intentionally wins
                // over automatic Pimax x86 runtime repair for this child only.
                foreach (var variable in SteamVrAirLinkCompatibility.ResolveLaunchEnvironment())
                {
                    launchEnvironment[variable.Key] = variable.Value;
                }
            }
            var modDll = Path.Combine(AppContext.BaseDirectory, LauncherCommandBuilder.ModDllFileName);
            if (!File.Exists(command.FileName) || !File.Exists(modDll))
            {
                ShowActivityError("The VR support files are missing. Reinstall World War VR and try again.");
                return;
            }
        }
        catch (Exception exception) when (
            exception is ArgumentException or IOException or InvalidOperationException)
        {
            ShowActivityError(exception.Message);
            return;
        }

        _launching = true;
        LaunchButton.IsEnabled = false;
        SetActivity(
            settings.QuestAirLinkCompatibility
                ? "Starting Call of Duty: World at War through SteamVR…"
                : "Starting Call of Duty: World at War in VR…",
            busy: true);
        await SaveSettingsQuietlyAsync();

        try
        {
            var startInfo = new ProcessStartInfo
            {
                FileName = command.FileName,
                WorkingDirectory = AppContext.BaseDirectory,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };
            foreach (var argument in command.Arguments)
            {
                startInfo.ArgumentList.Add(argument);
            }
            foreach (var variable in launchEnvironment)
            {
                startInfo.Environment[variable.Key] = variable.Value;
            }

            using var process = Process.Start(startInfo)
                ?? throw new InvalidOperationException("The VR launcher process could not be started.");
            var outputTask = process.StandardOutput.ReadToEndAsync();
            var errorTask = process.StandardError.ReadToEndAsync();
            await process.WaitForExitAsync();
            var output = await outputTask;
            var error = await errorTask;

            if (process.ExitCode != 0)
            {
                var detail = LastUsefulLine(error, output);
                ShowActivityError(string.IsNullOrWhiteSpace(detail)
                    ? $"World War VR could not start (error {process.ExitCode})."
                    : detail);
            }
            else
            {
                ActivityStatusText.Foreground = new SolidColorBrush(Microsoft.UI.Colors.LightGreen);
                SetActivity("World War VR started successfully.", busy: false);
            }
        }
        catch (Exception exception) when (exception is IOException or InvalidOperationException or UnauthorizedAccessException)
        {
            ShowActivityError($"World War VR could not start: {exception.Message}");
        }
        finally
        {
            _launching = false;
            UpdateValidationPresentation();
        }
    }

    private async Task SaveSettingsQuietlyAsync()
    {
        if (_initializing)
        {
            return;
        }

        _settings = ReadSettingsFromControls();
        await SaveSettingsSnapshotQuietlyAsync(reportFailure: true);
    }

    private async Task SaveSettingsSnapshotQuietlyAsync(bool reportFailure)
    {
        try
        {
            await _settingsStore.SaveAsync(_settings);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            if (reportFailure)
            {
                ShowActivityError("Settings could not be saved, but you can still launch the game.");
            }
        }
    }

    private void SetActivity(string message, bool busy)
    {
        BusyProgressRing.IsActive = busy;
        BusyProgressRing.Visibility = busy ? Visibility.Visible : Visibility.Collapsed;
        ReadyStatusRing.Visibility = busy ? Visibility.Collapsed : Visibility.Visible;
        ReadyStatusIcon.Visibility = busy ? Visibility.Collapsed : Visibility.Visible;
        InstallationProgressRing.IsActive = busy;
        InstallationProgressRing.Visibility = busy ? Visibility.Visible : Visibility.Collapsed;
        InstallationStatusRing.Visibility = busy ? Visibility.Collapsed : Visibility.Visible;
        InstallationStatusIcon.Visibility = busy ? Visibility.Collapsed : Visibility.Visible;
        ActivityStatusText.Text = message;
        if (busy)
        {
            ActivityStatusText.ClearValue(TextBlock.ForegroundProperty);
        }
    }

    private void ShowActivityError(string message)
    {
        SetActivity(message, busy: false);
        ActivityStatusText.Foreground = new SolidColorBrush(Microsoft.UI.Colors.OrangeRed);
    }

    private static SolidColorBrush ResourceBrush(string key) =>
        (SolidColorBrush)Application.Current.Resources[key];

    private static string LastUsefulLine(params string[] messages)
    {
        foreach (var message in messages)
        {
            var line = message
                .Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
                .LastOrDefault();
            if (!string.IsNullOrWhiteSpace(line))
            {
                return line;
            }
        }

        return string.Empty;
    }
}
