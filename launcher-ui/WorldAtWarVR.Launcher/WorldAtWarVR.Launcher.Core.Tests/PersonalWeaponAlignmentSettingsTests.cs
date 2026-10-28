using System.Diagnostics;
using System.Globalization;
using System.Xml.Linq;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using WorldAtWarVR.Launcher.Core;

namespace WorldAtWarVR.Launcher.Core.Tests;

[TestClass]
public sealed class PersonalWeaponAlignmentSettingsTests
{
    [TestMethod]
    public void NeutralDefault_ExplicitlyClearsInheritedPersonalFit()
    {
        var child = new ProcessStartInfo();
        foreach (var variable in new[]
                 {
                     GameplayOptionEnvironment.WeaponPitchVariable,
                     GameplayOptionEnvironment.WeaponYawVariable,
                     GameplayOptionEnvironment.WeaponRollVariable,
                 })
        {
            child.Environment[variable] = "25";
        }

        GameplayOptionEnvironment.ApplyTo(child.Environment, new LauncherSettings());
        Assert.AreEqual("0", child.Environment[GameplayOptionEnvironment.WeaponPitchVariable]);
        Assert.AreEqual("0", child.Environment[GameplayOptionEnvironment.WeaponYawVariable]);
        Assert.AreEqual("0", child.Environment[GameplayOptionEnvironment.WeaponRollVariable]);
    }

    [TestMethod]
    public void FiniteBounds_InvalidSavedAnglesResetToNeutralNotMaximum()
    {
        foreach (var invalid in new[] { double.NaN, double.PositiveInfinity, double.NegativeInfinity, -35.01, 35.01 })
        {
            var normalized = new LauncherSettings
            {
                WeaponPitchDegrees = invalid, WeaponYawDegrees = invalid, WeaponRollDegrees = invalid,
                AutomaticReload = true, SmoothTurning = true,
            }.Normalize();
            Assert.AreEqual(0.0, normalized.WeaponPitchDegrees);
            Assert.AreEqual(0.0, normalized.WeaponYawDegrees);
            Assert.AreEqual(0.0, normalized.WeaponRollDegrees);
            Assert.IsTrue(normalized.AutomaticReload);
            Assert.IsTrue(normalized.SmoothTurning);
        }
        Assert.AreEqual(-35.0, PersonalWeaponAlignment.NormalizeAngle(-35));
        Assert.AreEqual(35.0, PersonalWeaponAlignment.NormalizeAngle(35));
    }

    [TestMethod]
    public void ChildEnvironment_IsInvariantAndRejectsInvalidUnnormalizedCaller()
    {
        var originalCulture = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("de-DE");
            var values = GameplayOptionEnvironment.Resolve(new LauncherSettings
            {
                WeaponPitchDegrees = -12.5, WeaponYawDegrees = 3.25, WeaponRollDegrees = double.NaN,
            });
            Assert.AreEqual("-12.5", values[GameplayOptionEnvironment.WeaponPitchVariable]);
            Assert.AreEqual("3.25", values[GameplayOptionEnvironment.WeaponYawVariable]);
            Assert.AreEqual("0", values[GameplayOptionEnvironment.WeaponRollVariable]);
        }
        finally
        {
            CultureInfo.CurrentCulture = originalCulture;
        }
    }

    [TestMethod]
    public async Task SavedFit_RoundTripsAndZeroResetPreservesOtherGameplayChoices()
    {
        var folder = Directory.CreateTempSubdirectory("wawvr-weapon-fit-");
        try
        {
            var store = new LauncherSettingsStore(Path.Combine(folder.FullName, "launcher-settings.json"));
            var settings = new LauncherSettings
            {
                WeaponPitchDegrees = -12.5, WeaponYawDegrees = 3.25, WeaponRollDegrees = -4,
                AutomaticReload = true, ButtonGrenades = true, SmoothTurning = true,
            };
            await store.SaveAsync(settings);
            var loaded = await store.LoadAsync();
            Assert.AreEqual(-12.5, loaded.WeaponPitchDegrees);
            Assert.AreEqual(3.25, loaded.WeaponYawDegrees);
            Assert.AreEqual(-4.0, loaded.WeaponRollDegrees);
            await store.SaveAsync(loaded with
            {
                WeaponPitchDegrees = 0, WeaponYawDegrees = 0, WeaponRollDegrees = 0,
            });
            var reset = await store.LoadAsync();
            Assert.AreEqual(0.0, reset.WeaponPitchDegrees);
            Assert.AreEqual(0.0, reset.WeaponYawDegrees);
            Assert.AreEqual(0.0, reset.WeaponRollDegrees);
            Assert.IsTrue(reset.AutomaticReload);
            Assert.IsTrue(reset.ButtonGrenades);
            Assert.IsTrue(reset.SmoothTurning);
        }
        finally
        {
            folder.Delete(recursive: true);
        }
    }

    [TestMethod]
    public void UiSourceContract_BoundedControlsAndExplicitResetApplyNextLaunch()
    {
        var projectFolder = FindLauncherFolder();
        var document = XDocument.Load(Path.Combine(projectFolder, "MainPage.xaml"));
        XNamespace x = "http://schemas.microsoft.com/winfx/2006/xaml";
        foreach (var name in new[] { "WeaponPitchNumberBox", "WeaponYawNumberBox", "WeaponRollNumberBox" })
        {
            var control = document.Descendants().Single(element => (string?)element.Attribute(x + "Name") == name);
            Assert.AreEqual("NumberBox", control.Name.LocalName);
            Assert.AreEqual("-35", (string?)control.Attribute("Minimum"));
            Assert.AreEqual("35", (string?)control.Attribute("Maximum"));
            Assert.AreEqual("0", (string?)control.Attribute("Value"));
            Assert.AreEqual("WeaponAlignment_ValueChanged", (string?)control.Attribute("ValueChanged"));
        }
        var source = File.ReadAllText(Path.Combine(projectFolder, "MainPage.xaml.cs"));
        StringAssert.Contains(source, "WeaponPitchNumberBox.Value = 0;");
        StringAssert.Contains(source, "WeaponYawNumberBox.Value = 0;");
        StringAssert.Contains(source, "WeaponRollNumberBox.Value = 0;");
        var xaml = File.ReadAllText(Path.Combine(projectFolder, "MainPage.xaml"));
        StringAssert.Contains(xaml, "next game launch");
        StringAssert.Contains(xaml, "ResetWeaponAlignment_Click");
    }

    private static string FindLauncherFolder()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "MainPage.xaml"))) return directory.FullName;
            var candidate = Path.Combine(directory.FullName, "launcher-ui", "WorldAtWarVR.Launcher");
            if (File.Exists(Path.Combine(candidate, "MainPage.xaml"))) return candidate;
            directory = directory.Parent;
        }
        throw new DirectoryNotFoundException("Could not find authored launcher UI for its source-contract check.");
    }
}
