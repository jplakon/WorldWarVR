#pragma once

#include "bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace wawvr::t4 {

enum class HookPrepareError : std::uint8_t {
    none,
    null_detour,
    site_not_in_profile,
    site_is_validation_only,
    target_unmapped,
    target_bytes_changed,
};

struct PreparedInlineHook final {
    HookSiteId id{};
    std::uintptr_t target{};
    std::uintptr_t detour{};
    std::uint8_t minimum_patch_bytes{};
    std::uint8_t expected_size{};
    std::array<std::uint8_t, HookSite::kMaximumExpectedBytes> expected{};

    [[nodiscard]] constexpr std::span<const std::uint8_t> expected_bytes() const noexcept {
        return {expected.data(), expected_size};
    }
};

struct HookPreparationResult final {
    std::optional<PreparedInlineHook> hook{};
    HookPrepareError error{HookPrepareError::none};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept { return hook.has_value(); }
};

// Rechecks the site's original bytes at the last possible point before handing
// it to a patching backend. The backend must check `expected` once more while it
// owns whatever thread-suspension/write-protection protocol it uses.
[[nodiscard]] HookPreparationResult prepare_inline_hook(
    const ValidatedBindings& bindings, HookSiteId site, std::uintptr_t detour);

enum class HookInstallError : std::uint8_t {
    none,
    unsupported,
    expected_bytes_changed,
    memory_protection_failed,
    trampoline_allocation_failed,
    patch_failed,
};

struct HookInstallResult final {
    HookInstallError error{HookInstallError::none};
    std::uintptr_t trampoline{};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept { return error == HookInstallError::none; }
};

// T4 bindings deliberately do not contain a patch writer. The application may
// use MinHook, Detours, or a small audited backend behind this interface.
class IHookBackend {
public:
    virtual ~IHookBackend() = default;
    [[nodiscard]] virtual HookInstallResult install(const PreparedInlineHook& hook) = 0;
    [[nodiscard]] virtual HookInstallResult remove(HookSiteId site) = 0;
};

}  // namespace wawvr::t4
