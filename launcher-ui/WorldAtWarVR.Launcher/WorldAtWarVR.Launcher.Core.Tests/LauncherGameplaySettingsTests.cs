using System.Diagnostics;
using System.Text.Json;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using WorldAtWarVR.Launcher.Core;

namespace WorldAtWarVR.Launcher.Core.Tests;

[TestClass]
public sealed class LauncherGameplaySettingsTests
{
    [TestMethod]
    public async Task FreshStore_SmoothSnapSmoothPreservesManualReloadAcrossRestarts()
    {
        var root = Directory.CreateTempSubdirectory("wawvr-first-run-settings-");
        var path = Path.Combine(root.FullName, "launcher-settings.json");
        try
        {
            var settings = await new LauncherSettingsStore(path).LoadAsync();
            Assert.IsFalse(File.Exists(path), "Reading a fresh store must not create settings.");
            Assert.IsFalse(settings.AutomaticReload);
            Assert.IsFalse(settings.SmoothTurning);

            foreach (var smoothTurning in new[] { true, false, true })
            {
                await new LauncherSettingsStore(path).SaveAsync(
                    settings with { SmoothTurning = smoothTurning });
                settings = await new LauncherSettingsStore(path).LoadAsync();

                Assert.AreEqual(smoothTurning, settings.SmoothTurning);
                Assert.IsFalse(settings.AutomaticReload,
                    "Changing only the turn mode must preserve manual reload.");
                Assert.IsFalse(settings.ButtonGrenades);

                var child = new ProcessStartInfo();
                child.Environment[GameplayOptionEnvironment.AutomaticReloadVariable] = "1";
                GameplayOptionEnvironment.ApplyTo(child.Environment, settings);
                Assert.AreEqual("0", child.Environment[GameplayOptionEnvironment.AutomaticReloadVariable]);
                Assert.AreEqual(smoothTurning ? "smooth" : "snap",
                    child.Environment[GameplayOptionEnvironment.TurnModeVariable]);
            }
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }

    [TestMethod]
    public async Task ExplicitAutomaticReload_TrueThenFalsePersistsWithoutChangingSmoothTurning()
    {
        var root = Directory.CreateTempSubdirectory("wawvr-reload-selection-");
        var path = Path.Combine(root.FullName, "launcher-settings.json");
        try
        {
            var settings = new LauncherSettings { SmoothTurning = true };
            foreach (var automaticReload in new[] { true, false })
            {
                await new LauncherSettingsStore(path).SaveAsync(
                    settings with { AutomaticReload = automaticReload });
                settings = await new LauncherSettingsStore(path).LoadAsync();

                Assert.AreEqual(automaticReload, settings.AutomaticReload);
                Assert.IsTrue(settings.SmoothTurning);
                var child = new ProcessStartInfo();
                GameplayOptionEnvironment.ApplyTo(child.Environment, settings);
                Assert.AreEqual(automaticReload ? "1" : "0",
                    child.Environment[GameplayOptionEnvironment.AutomaticReloadVariable]);
                Assert.AreEqual("smooth", child.Environment[GameplayOptionEnvironment.TurnModeVariable]);
            }
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }

    [TestMethod]
    public async Task LegacySettings_MissingReloadFieldKeepsManualReloadWithSmoothTurning()
    {
        var root = Directory.CreateTempSubdirectory("wawvr-legacy-gameplay-settings-");
        var path = Path.Combine(root.FullName, "launcher-settings.json");
        try
        {
            await File.WriteAllTextAsync(path, """
                { "Version": 4, "SmoothTurning": true }
                """);
            var store = new LauncherSettingsStore(path);
            var settings = await store.LoadAsync();
            Assert.IsTrue(settings.SmoothTurning);
            Assert.IsFalse(settings.AutomaticReload);

            await store.SaveAsync(settings);
            using var saved = JsonDocument.Parse(await File.ReadAllTextAsync(path));
            Assert.IsFalse(saved.RootElement.GetProperty("AutomaticReload").GetBoolean());
            var reloaded = await new LauncherSettingsStore(path).LoadAsync();
            Assert.IsTrue(reloaded.SmoothTurning);
            Assert.IsFalse(reloaded.AutomaticReload);
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }

    [DataTestMethod]
    [DataRow("{ not json")]
    [DataRow("{\"AutomaticReload\":\"true\",\"SmoothTurning\":true}")]
    [DataRow("null")]
    public async Task InvalidSettings_ThenSelectingSmoothTurningKeepsManualReload(string contents)
    {
        var root = Directory.CreateTempSubdirectory("wawvr-invalid-gameplay-settings-");
        var path = Path.Combine(root.FullName, "launcher-settings.json");
        try
        {
            await File.WriteAllTextAsync(path, contents);
            var store = new LauncherSettingsStore(path);
            var settings = await store.LoadAsync();
            Assert.IsFalse(settings.AutomaticReload);
            await store.SaveAsync(settings with { SmoothTurning = true });

            var reloaded = await new LauncherSettingsStore(path).LoadAsync();
            Assert.IsTrue(reloaded.SmoothTurning);
            Assert.IsFalse(reloaded.AutomaticReload);
            var child = new ProcessStartInfo();
            child.Environment[GameplayOptionEnvironment.AutomaticReloadVariable] = "1";
            GameplayOptionEnvironment.ApplyTo(child.Environment, reloaded);
            Assert.AreEqual("0", child.Environment[GameplayOptionEnvironment.AutomaticReloadVariable]);
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }

    [DataTestMethod]
    [DataRow(false, false)]
    [DataRow(false, true)]
    [DataRow(true, false)]
    [DataRow(true, true)]
    public void GameplayOptions_OverrideInheritedReloadWithoutChangingParentEnvironment(
        bool automaticReload, bool smoothTurning)
    {
        var parentReload = Environment.GetEnvironmentVariable(
            GameplayOptionEnvironment.AutomaticReloadVariable);
        var parentTurning = Environment.GetEnvironmentVariable(
            GameplayOptionEnvironment.TurnModeVariable);
        var child = new ProcessStartInfo();
        child.Environment[GameplayOptionEnvironment.AutomaticReloadVariable] = "1";
        child.Environment[GameplayOptionEnvironment.TurnModeVariable] = "smooth";
        child.Environment["WAWVR_TEST_UNRELATED"] = "preserved";

        GameplayOptionEnvironment.ApplyTo(child.Environment, new LauncherSettings
        {
            AutomaticReload = automaticReload,
            SmoothTurning = smoothTurning,
        });

        Assert.AreEqual(automaticReload ? "1" : "0",
            child.Environment[GameplayOptionEnvironment.AutomaticReloadVariable]);
        Assert.AreEqual(smoothTurning ? "smooth" : "snap",
            child.Environment[GameplayOptionEnvironment.TurnModeVariable]);
        Assert.AreEqual("preserved", child.Environment["WAWVR_TEST_UNRELATED"]);
        Assert.AreEqual(parentReload, Environment.GetEnvironmentVariable(
            GameplayOptionEnvironment.AutomaticReloadVariable));
        Assert.AreEqual(parentTurning, Environment.GetEnvironmentVariable(
            GameplayOptionEnvironment.TurnModeVariable));
    }
}
