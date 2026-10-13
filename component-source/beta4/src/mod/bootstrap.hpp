#pragma once

#include <windows.h>

namespace wawvr::mod {

DWORD WINAPI bootstrap_thread(void* module_handle) noexcept;
bool bootstrap_complete() noexcept;
bool game_build_validated() noexcept;

} // namespace wawvr::mod
