#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

using VersionExport = const wchar_t* (*)();
using BootstrapCompleteExport = BOOL (*)();

int fail(std::string_view message) {
    std::cerr << "WorldAtWarVR DLL smoke test: " << message << '\n';
    return 1;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        return fail("expected a path to WorldWarVR.dll");
    }

    const std::filesystem::path dll_path = std::filesystem::absolute(argv[1]);
    const HMODULE module = LoadLibraryW(dll_path.c_str());
    if (module == nullptr) {
        return fail("LoadLibraryW failed");
    }

    const auto version = reinterpret_cast<VersionExport>(
        GetProcAddress(module, "WorldAtWarVR_GetVersion"));
    if (version == nullptr) {
        return fail("version export was not found");
    }

    const std::wstring_view value(version());
    if (value != L"0.4.0-t4-vr-mp") {
        return fail("version export returned an unexpected value");
    }

    const auto bootstrap_complete = reinterpret_cast<BootstrapCompleteExport>(
        GetProcAddress(module, "WorldAtWarVR_IsBootstrapComplete"));
    if (bootstrap_complete == nullptr) {
        return fail("bootstrap completion export was not found");
    }

    const auto present_hook_installed = reinterpret_cast<BootstrapCompleteExport>(
        GetProcAddress(module, "WorldAtWarVR_IsPresentHookInstalled"));
    const auto frame_boundary_hook_installed =
        reinterpret_cast<BootstrapCompleteExport>(
            GetProcAddress(
                module, "WorldAtWarVR_IsFrameBoundaryHookInstalled"));
    const auto mono_xr_ready = reinterpret_cast<BootstrapCompleteExport>(
        GetProcAddress(module, "WorldAtWarVR_IsMonoXrReady"));
    if (present_hook_installed == nullptr ||
        frame_boundary_hook_installed == nullptr || mono_xr_ready == nullptr) {
        return fail("renderer diagnostic exports were not found");
    }

    constexpr DWORD kTimeoutMs = 5000;
    constexpr DWORD kPollMs = 10;
    DWORD waited = 0;
    while (!bootstrap_complete() && waited < kTimeoutMs) {
        Sleep(kPollMs);
        waited += kPollMs;
    }
    if (!bootstrap_complete()) {
        return fail("bootstrap thread did not finish before timeout");
    }
    if (present_hook_installed() || frame_boundary_hook_installed() ||
        mono_xr_ready()) {
        return fail("renderer activated inside the unsupported smoke-test process");
    }

    // Do not call FreeLibrary while testing a DLL that owns a bootstrap thread.
    // Process shutdown unloads it after the completed thread has returned.
    Sleep(50);
    std::wcout << L"Loaded x86 DLL and found version " << value << L'\n';
    return 0;
}
