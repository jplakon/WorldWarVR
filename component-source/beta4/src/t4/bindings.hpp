#pragma once

#include "address.hpp"
#include "profile.hpp"
#include "validation.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace wawvr::t4 {

struct BindingResult;

// A capability object that is produced only after both the on-disk executable
// and its mapped image pass the exact profile. Keeping raw address resolution
// behind this type makes accidental patching before validation less likely.
class ValidatedBindings final {
public:
    [[nodiscard]] const ExecutableProfile& profile() const noexcept { return *profile_; }
    [[nodiscard]] ModuleView module() const noexcept { return module_; }

    [[nodiscard]] const HookSite* site(HookSiteId id) const noexcept;
    [[nodiscard]] std::optional<std::uintptr_t> site_address(HookSiteId id) const noexcept;
    [[nodiscard]] bool site_bytes_still_match(HookSiteId id) const noexcept;

    [[nodiscard]] const DataSymbol* data_symbol(DataSymbolId id) const noexcept;
    [[nodiscard]] std::optional<std::uintptr_t> data_address(
        DataSymbolId id, std::size_t required_size = 0) const noexcept;

private:
    struct ConstructionToken final {};

    ValidatedBindings(ConstructionToken, ModuleView module,
                      const ExecutableProfile& profile) noexcept
        : module_(module), profile_(&profile) {}

    ModuleView module_{};
    const ExecutableProfile* profile_{};

    friend BindingResult validate_and_bind(std::span<const std::uint8_t>, ModuleView,
                                           const ExecutableProfile&);
};

struct BindingResult final {
    std::optional<ValidatedBindings> bindings{};
    ValidationReport validation{};

    [[nodiscard]] bool ok() const noexcept {
        return bindings.has_value() && validation.ok();
    }
};

[[nodiscard]] BindingResult validate_and_bind(
    std::span<const std::uint8_t> executable_file, ModuleView loaded_module,
    const ExecutableProfile& profile);

}  // namespace wawvr::t4
