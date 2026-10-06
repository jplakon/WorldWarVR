#include "bindings.hpp"

#include <algorithm>

namespace wawvr::t4 {

const HookSite* ValidatedBindings::site(const HookSiteId id) const noexcept {
    return find_hook_site(*profile_, id);
}

std::optional<std::uintptr_t> ValidatedBindings::site_address(
    const HookSiteId id) const noexcept {
    const auto* descriptor = site(id);
    if (descriptor == nullptr) {
        return std::nullopt;
    }
    return module_.address(descriptor->rva, descriptor->expected_size);
}

bool ValidatedBindings::site_bytes_still_match(const HookSiteId id) const noexcept {
    const auto* descriptor = site(id);
    if (descriptor == nullptr) {
        return false;
    }
    const auto actual = module_.bytes(descriptor->rva, descriptor->expected_size);
    return actual.size() == descriptor->expected_size &&
           std::ranges::equal(actual, descriptor->expected_bytes());
}

const DataSymbol* ValidatedBindings::data_symbol(const DataSymbolId id) const noexcept {
    return find_data_symbol(*profile_, id);
}

std::optional<std::uintptr_t> ValidatedBindings::data_address(
    const DataSymbolId id, const std::size_t required_size) const noexcept {
    const auto* descriptor = data_symbol(id);
    if (descriptor == nullptr) {
        return std::nullopt;
    }

    const auto width = required_size == 0 ? static_cast<std::size_t>(descriptor->size)
                                          : required_size;
    if (width > descriptor->size) {
        return std::nullopt;
    }
    return module_.address(descriptor->rva, width);
}

BindingResult validate_and_bind(const std::span<const std::uint8_t> executable_file,
                                const ModuleView loaded_module,
                                const ExecutableProfile& profile) {
    BindingResult result{};
    result.validation = validate_executable_file(executable_file, profile);
    result.validation.append(validate_loaded_module(loaded_module, profile));
    if (result.validation.ok()) {
        result.bindings =
            ValidatedBindings(ValidatedBindings::ConstructionToken{}, loaded_module, profile);
    }
    return result;
}

}  // namespace wawvr::t4
