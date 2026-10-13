#include <wawvr_launcher/launcher_core.hpp>

#include <windows.h>
#include <shellapi.h>

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void print_report(
        const wawvr::launcher::LaunchPlan& plan,
        const wawvr::launcher::DiagnosticReport& report)
    {
        std::wcout << L"WaWVR standalone launch diagnostic\n\n"
                   << L"Mode:        "
                   << wawvr::launcher::mode_name(plan.mode, plan.launch_target) << L'\n'
                   << L"Game data:   " << plan.game_dir.wstring() << L'\n'
                   << L"Source EXE:  " << plan.source_exe.wstring() << L'\n'
                   << L"Runtime:     " << plan.runtime_dir.wstring() << L'\n'
                   << L"Home path:   " << plan.home_dir.wstring() << L'\n'
                   << L"Staged EXE:  " << plan.staged_exe.wstring() << L'\n';
        std::wcout << L"VR source:   "
                   << wawvr::launcher::source_resolution_text(plan.source_resolution)
                   << L" packed side-by-side\n"
                   << L"Bots:        "
                   << wawvr::launcher::bot_policy_name(plan.bot_policy) << L'\n';

        if (plan.mod_dll)
        {
            std::wcout << L"Mod DLL:     " << plan.mod_dll->wstring() << L'\n';
        }
        else
        {
            std::wcout << L"Mod DLL:     (none)\n";
        }
        if (plan.launcher_executable)
        {
            std::wcout << L"Launcher:    "
                       << plan.launcher_executable->wstring() << L'\n';
        }
        if (plan.pezbot_archive)
        {
            std::wcout << L"PeZBOT ZIP:  " << plan.pezbot_archive->wstring() << L'\n';
        }

        std::wcout << L'\n';
        for (const auto& check : report.checks)
        {
            const wchar_t* label = !check.required
                ? L"[INFO] "
                : (check.passed ? L"[PASS] " : L"[FAIL] ");
            std::wcout << label
                       << check.name << L": " << check.detail << L'\n';
        }

        std::wcout << L"\nPlanned command line:\n" << report.command_line << L"\n\n"
                   << (report.passed() ? L"Result: ready\n" : L"Result: blocked by failed checks\n");
    }

#if defined(WAWVR_GUI)
    std::wstring widen_error(const char* const text)
    {
        if (text == nullptr || *text == '\0')
        {
            return L"Unknown launcher error";
        }
        const int required = MultiByteToWideChar(
            CP_UTF8, 0, text, -1, nullptr, 0);
        if (required <= 1)
        {
            return L"Unknown launcher error";
        }
        std::wstring result(static_cast<std::size_t>(required), L'\0');
        if (MultiByteToWideChar(
                CP_UTF8, 0, text, -1, result.data(), required) != required)
        {
            return L"Unknown launcher error";
        }
        result.resize(static_cast<std::size_t>(required - 1));
        return result;
    }

    void show_gui_error(
        const std::wstring& detail,
        const bool steam_executable_selected = false)
    {
        std::wstring message;
        std::wstring parent_detail;
        if (steam_executable_selected &&
            (detail.find(L"Target exited before DLL injection") != std::wstring::npos ||
             detail.find(L"Target exited while checking its startup prompts") !=
                 std::wstring::npos))
        {
            parent_detail =
                L"Steam is not running. Start Steam, sign in, and then click Launch in VR again.";
            message =
                L"World War VR could not start.\n\n"
                L"Steam is not running.\n\n"
                L"You must start Steam and sign in before launching this Steam "
                L"installation of Call of Duty: World at War. Then click Launch "
                L"in VR again.";
        }
        else if (detail.find(L"Run as administrator") != std::wstring::npos)
        {
            parent_detail =
                L"Windows blocked the prepared game-data links. Run World War VR as administrator and try again.";
            message =
                L"World War VR could not start.\n\n" + detail;
        }
        else
        {
            parent_detail = detail;
            message =
                L"World War VR could not start.\n\n" + detail +
                L"\n\nOpen World War VR, confirm the selected game installation "
                L"folder and launch settings, then try again.";
        }
        std::wcerr << parent_detail << L'\n';
        std::wcerr.flush();
        MessageBoxW(
            nullptr,
            message.c_str(),
            L"World War VR Launcher",
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }

    void show_failed_checks(const wawvr::launcher::DiagnosticReport& report)
    {
        std::wstring detail = L"Compatibility checks failed:";
        for (const auto& check : report.checks)
        {
            if (check.required && !check.passed)
            {
                detail += L"\n\n- " + check.name + L": " + check.detail;
            }
        }
        show_gui_error(detail);
    }
#endif
}

namespace
{
    int run_launcher(const int argc, wchar_t** const argv)
    {
        bool steam_executable_selected = false;
        try
        {
            const auto launcher_path =
                std::filesystem::absolute(argv[0]).lexically_normal();
            wawvr::launcher::Options options;
            if (wawvr::launcher::is_multiplayer_handoff_invocation(launcher_path))
            {
                if (argc != 1)
                {
                    throw std::invalid_argument(
                        "The multiplayer handoff shim does not accept arguments");
                }
                const auto wait =
                    wawvr::launcher::wait_for_multiplayer_handoff_parent(launcher_path);
                if (wait != wawvr::launcher::HandoffParentWaitResult::ready)
                {
                    throw std::runtime_error(
                        std::string("Multiplayer handoff refused to launch: ") +
                        wawvr::launcher::handoff_parent_wait_result_name(wait));
                }
                const auto handoff =
                    wawvr::launcher::read_multiplayer_handoff_config(launcher_path);
                options = wawvr::launcher::make_multiplayer_handoff_options(
                    launcher_path, handoff);
            }
            else
            {
#if defined(WAWVR_DEFAULT_PLAY)
                if (argc == 1)
                {
#if defined(WAWVR_DEFAULT_MULTIPLAYER)
                    options =
                        wawvr::launcher::make_user_facing_multiplayer_default_options(
                            launcher_path);
#else
                    options = wawvr::launcher::make_user_facing_default_options(
                        launcher_path);
#endif
                }
                else
                {
                    options = wawvr::launcher::apply_user_facing_defaults(
                        wawvr::launcher::parse_options(argc, argv),
                        launcher_path);
                }
#else
                options = wawvr::launcher::parse_options(argc, argv);
                options.launcher_executable = launcher_path;
#endif
            }
            if (options.show_help)
            {
                std::wcout << wawvr::launcher::usage_text();
                return 0;
            }

            auto plan = wawvr::launcher::resolve_plan(options);
            steam_executable_selected =
                wawvr::launcher::executable_identity_uses_steam(
                    plan.executable_identity);
            const auto report = wawvr::launcher::diagnose(plan);
            print_report(plan, report);

            if (!report.passed())
            {
#if defined(WAWVR_GUI)
                show_failed_checks(report);
#endif
                return 2;
            }

            if (plan.mode == wawvr::launcher::Mode::diagnose)
            {
                std::wcout << L"Diagnostic mode made no filesystem or process changes.\n";
                return 0;
            }

            wawvr::launcher::prepare_runtime(plan);
            std::wcout << L"Prepared the isolated runtime; the installed game data was not modified.\n";

            if (plan.launch_target ==
                    wawvr::launcher::LaunchTarget::offline_multiplayer ||
                plan.launch_target == wawvr::launcher::LaunchTarget::frontend_menu)
            {
                const auto pezbot =
                    wawvr::launcher::should_import_optional_pezbot(plan)
                    ? wawvr::launcher::prepare_optional_pezbot(plan)
                    : wawvr::launcher::inspect_optional_pezbot(plan);
                plan.pezbot_enabled = pezbot.enabled();
                std::wcout << L"Optional PeZBOT: "
                           << wawvr::launcher::pezbot_state_name(pezbot.state)
                           << L" - " << pezbot.detail << L'\n';
            }

            if (plan.mode == wawvr::launcher::Mode::prepare)
            {
                return 0;
            }

            const auto result = wawvr::launcher::launch_game(plan);
            std::wcout << L"Started World at War with process ID "
                       << result.process_id << L'.';
            if (!result.injection_detail.empty())
            {
                std::wcout << L" " << result.injection_detail;
            }
            if (result.exit_code)
            {
                std::wcout << L" Exit code: " << *result.exit_code << L'.';
            }
            std::wcout << L'\n';
            return result.exit_code ? static_cast<int>(*result.exit_code) : 0;
        }
        catch (const std::exception& error)
        {
#if defined(WAWVR_GUI)
            show_gui_error(
                widen_error(error.what()),
                steam_executable_selected);
#else
            std::cerr << "WaWVR launcher error: " << error.what() << '\n';
#endif
            return 1;
        }
    }
}

#if defined(WAWVR_GUI)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    wchar_t** const argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr || argc <= 0)
    {
        show_gui_error(L"Windows could not parse the launcher command line.");
        return 1;
    }
    const int result = run_launcher(argc, argv);
    LocalFree(argv);
    return result;
}
#else
int wmain(const int argc, wchar_t** const argv)
{
    return run_launcher(argc, argv);
}
#endif
