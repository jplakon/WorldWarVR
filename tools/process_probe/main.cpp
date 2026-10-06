#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

class Handle final {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] HANDLE get() const { return value_; }
    [[nodiscard]] bool valid() const {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE value_{};
};

struct Module {
    std::uintptr_t begin{};
    std::uintptr_t end{};
    std::wstring name;
};

struct UnicodeString32 {
    std::uint16_t length{};
    std::uint16_t maximum_length{};
    std::uint32_t buffer{};
};

template <typename T>
std::optional<T> read_value(HANDLE process, const std::uintptr_t address) {
    T value{};
    SIZE_T bytes = 0;
    if (!ReadProcessMemory(
            process,
            reinterpret_cast<const void*>(address),
            &value,
            sizeof(value),
            &bytes) ||
        bytes != sizeof(value)) {
        return std::nullopt;
    }
    return value;
}

bool is_readable_range(
    HANDLE process,
    const std::uintptr_t address,
    const std::size_t size) {
    if (address == 0 || size == 0 ||
        address > std::numeric_limits<std::uintptr_t>::max() - size) {
        return false;
    }

    const auto end = address + size;
    auto cursor = address;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION information{};
        if (VirtualQueryEx(
                process,
                reinterpret_cast<const void*>(cursor),
                &information,
                sizeof(information)) != sizeof(information) ||
            information.State != MEM_COMMIT ||
            (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }

        const DWORD access = information.Protect & 0xFFU;
        const bool readable =
            access == PAGE_READONLY || access == PAGE_READWRITE ||
            access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
            access == PAGE_EXECUTE_READWRITE ||
            access == PAGE_EXECUTE_WRITECOPY;
        if (!readable) {
            return false;
        }

        const auto region_begin =
            reinterpret_cast<std::uintptr_t>(information.BaseAddress);
        const auto region_size =
            static_cast<std::uintptr_t>(information.RegionSize);
        if (region_size == 0 ||
            region_begin >
                std::numeric_limits<std::uintptr_t>::max() -
                    region_size) {
            return false;
        }
        const auto region_end = region_begin + region_size;
        if (cursor < region_begin || cursor >= region_end) {
            return false;
        }
        cursor = std::min(end, region_end);
    }
    return true;
}

template <typename T>
std::optional<T> read_readable_value(
    HANDLE process, const std::uintptr_t address) {
    if (!is_readable_range(process, address, sizeof(T))) {
        return std::nullopt;
    }
    return read_value<T>(process, address);
}

std::optional<std::string> read_ascii_string(
    HANDLE process, const std::uintptr_t address, const std::size_t limit) {
    if (address == 0 || limit == 0) {
        return std::nullopt;
    }

    std::string value;
    value.reserve(limit);
    for (std::size_t index = 0; index < limit; ++index) {
        const auto character = read_value<char>(process, address + index);
        if (!character) {
            return std::nullopt;
        }
        if (*character == '\0') {
            return value;
        }
        value.push_back(*character);
    }
    return std::nullopt;
}

void print_supported_t4_state(HANDLE process) {
    // Exact-build diagnostic addresses for the allowlisted 1.7.1263 SP image.
    constexpr std::uintptr_t kClientConnectionState = 0x0305842C;
    constexpr std::uintptr_t kGfxWorldNamePointer = 0x03DCB4E0;
    constexpr std::uintptr_t kMainRefdef = 0x03520338;
    constexpr std::uintptr_t kCommandRing = 0x030FD700;
    constexpr std::uintptr_t kCommandNumber = 0x030FF300;
    constexpr std::uintptr_t kClientGunPitchDegrees = 0x0352B65C;
    constexpr std::uintptr_t kClientGunYawDegrees = 0x0352B660;
    constexpr std::uintptr_t kLocalServerGclientPointer = 0x0176C870;
    constexpr std::uintptr_t kGclientGunPitchDegreesOffset = 0x2258;
    constexpr std::uintptr_t kGclientGunYawDegreesOffset = 0x225C;
    constexpr std::size_t kCommandRingEntries = 128;

    if (const auto connection =
            read_value<std::int32_t>(process, kClientConnectionState)) {
        std::wcout << L"t4-connection-state=" << *connection
                   << (*connection == 10 ? L" (CA_ACTIVE)" : L"") << L'\n';
    }

    if (const auto name_pointer =
            read_value<std::uint32_t>(process, kGfxWorldNamePointer)) {
        if (const auto name = read_ascii_string(process, *name_pointer, 512)) {
            std::cout << "t4-world-name=" << *name << '\n';
        }
    }

    struct RefdefProbe {
        std::uint32_t x{};
        std::uint32_t y{};
        std::uint32_t width{};
        std::uint32_t height{};
        float tan_half_fov_x{};
        float tan_half_fov_y{};
        float unknown_18{};
        float view_origin[3]{};
        float unknown_28{};
        float view_axis[3][3]{};
        float unknown_50[4]{};
        float near_clip{};
    };
    static_assert(sizeof(RefdefProbe) == 0x64);

    if (const auto refdef = read_value<RefdefProbe>(process, kMainRefdef)) {
        std::wcout << L"t4-refdef=" << refdef->width << L'x' << refdef->height
                   << L" at " << refdef->x << L',' << refdef->y
                   << L" fov-tan=" << refdef->tan_half_fov_x << L','
                   << refdef->tan_half_fov_y << L" origin="
                   << refdef->view_origin[0] << L',' << refdef->view_origin[1]
                   << L',' << refdef->view_origin[2] << L" forward="
                   << refdef->view_axis[0][0] << L','
                   << refdef->view_axis[0][1] << L','
                   << refdef->view_axis[0][2] << L" left="
                   << refdef->view_axis[1][0] << L','
                   << refdef->view_axis[1][1] << L','
                   << refdef->view_axis[1][2] << L" up="
                   << refdef->view_axis[2][0] << L','
                   << refdef->view_axis[2][1] << L','
                   << refdef->view_axis[2][2]
                   << L" near=" << refdef->near_clip << L'\n';
    }

    struct UsercmdProbe {
        std::int32_t server_time{};
        std::uint32_t buttons{};
        std::int32_t view_angles[3]{};
        std::uint8_t weapon{};
        std::uint8_t offhand{};
        std::int8_t forward_move{};
        std::int8_t right_move{};
        std::array<std::uint8_t, 4> opaque_18{};
        std::int16_t gun_pitch{};
        std::int16_t gun_yaw{};
        std::array<std::uint8_t, 0x18> opaque_20{};
    };
    static_assert(sizeof(UsercmdProbe) == 0x38);

    if (const auto command_number =
            read_value<std::uint32_t>(process, kCommandNumber)) {
        const auto command_index = *command_number & (kCommandRingEntries - 1);
        const auto command_address =
            kCommandRing + command_index * sizeof(UsercmdProbe);
        if (const auto command =
                read_value<UsercmdProbe>(process, command_address)) {
            std::wcout << L"t4-usercmd-number=" << *command_number
                       << L" index=" << command_index
                       << L" server-time=" << command->server_time
                       << L" buttons=0x" << std::hex << std::uppercase
                       << command->buttons << std::dec
                       << L" move=" << static_cast<int>(command->forward_move)
                       << L',' << static_cast<int>(command->right_move)
                       << L" gun-short=" << command->gun_pitch << L','
                       << command->gun_yaw << L'\n';
        }
    }

    const auto gun_pitch =
        read_value<float>(process, kClientGunPitchDegrees);
    const auto gun_yaw = read_value<float>(process, kClientGunYawDegrees);
    if (gun_pitch && gun_yaw) {
        std::wcout << L"t4-client-gun-degrees=" << *gun_pitch << L','
                   << *gun_yaw << L'\n';
        std::wcout << L"t4-cgame-visual-gun-degrees=" << *gun_pitch << L','
                   << *gun_yaw << L'\n';
    }

    const auto gclient_pointer = read_readable_value<std::uint32_t>(
        process, kLocalServerGclientPointer);
    if (gclient_pointer && *gclient_pointer >= 0x10000U &&
        *gclient_pointer <=
            std::numeric_limits<std::uint32_t>::max() -
                kGclientGunYawDegreesOffset - sizeof(float) &&
        is_readable_range(
            process,
            static_cast<std::uintptr_t>(*gclient_pointer) +
                kGclientGunPitchDegreesOffset,
            kGclientGunYawDegreesOffset - kGclientGunPitchDegreesOffset +
                sizeof(float))) {
        const auto authoritative_pitch = read_value<float>(
            process,
            static_cast<std::uintptr_t>(*gclient_pointer) +
                kGclientGunPitchDegreesOffset);
        const auto authoritative_yaw = read_value<float>(
            process,
            static_cast<std::uintptr_t>(*gclient_pointer) +
                kGclientGunYawDegreesOffset);
        if (authoritative_pitch && authoritative_yaw) {
            std::wcout << L"t4-authoritative-local-gclient=0x" << std::hex
                       << std::uppercase << *gclient_pointer << std::dec
                       << L" gun-degrees=" << *authoritative_pitch << L','
                       << *authoritative_yaw << L'\n';
        }
    }
}

void print_scope_backend_state(HANDLE process) {
    constexpr std::uintptr_t kRetailDrawSurfsEntry = 0x006F8A30;
    constexpr std::uintptr_t kBackEndDataPointer = 0x03DCB4CC;
    constexpr std::size_t kViewInfoIndexOffset = 0x144DCC;
    constexpr std::size_t kViewInfoCountOffset = 0x144DD0;
    constexpr std::size_t kViewInfoPointerOffset = 0x144DD4;
    constexpr std::size_t kViewInfoStride = 0x6D80;
    constexpr std::size_t kRetailGfxEntitiesOffset = 0x13B160;
    constexpr std::size_t kRetailGfxEntityStride = 24;
    constexpr std::size_t kDrawListViewInfoOffset = 0x0C;
    constexpr std::size_t kDrawListCameraViewOffset = 0x28;
    constexpr std::array<std::size_t, 5> kDrawListOffsets = {
        0x6554, 0x6580, 0x65AC, 0x65D8, 0x6604};

    const auto draw_surfs_entry =
        read_value<std::array<std::uint8_t, 16>>(
            process, kRetailDrawSurfsEntry);
    if (draw_surfs_entry) {
        std::wcout << L"scope-draw-surfs-entry=";
        for (const std::uint8_t byte : *draw_surfs_entry) {
            std::wcout << std::hex << std::setw(2) << std::setfill(L'0')
                       << static_cast<unsigned int>(byte);
        }
        if ((*draw_surfs_entry)[0] == 0xE9) {
            std::int32_t displacement = 0;
            std::memcpy(
                &displacement, draw_surfs_entry->data() + 1,
                sizeof(displacement));
            const auto target = static_cast<std::uint32_t>(
                kRetailDrawSurfsEntry + 5U + displacement);
            std::wcout << L" target=0x" << std::hex << target;
        }
        std::wcout << std::setfill(L' ') << std::dec << L'\n';
    }

    std::optional<std::uint32_t> data_pointer;
    for (std::uint32_t sample = 0; sample < 5000; ++sample) {
        data_pointer = read_value<std::uint32_t>(
            process, kBackEndDataPointer);
        if (data_pointer && *data_pointer != 0) {
            break;
        }
        Sleep(1);
    }
    if (!data_pointer || *data_pointer == 0) {
        std::wcerr << L"scope-backend-data=unavailable\n";
        return;
    }
    const auto data = static_cast<std::uintptr_t>(*data_pointer);
    const auto view_index = read_value<std::uint32_t>(
        process, data + kViewInfoIndexOffset);
    const auto view_count = read_value<std::uint32_t>(
        process, data + kViewInfoCountOffset);
    const auto views_pointer = read_value<std::uint32_t>(
        process, data + kViewInfoPointerOffset);
    const auto gfx_entity_count = read_value<std::uint32_t>(
        process, data + kRetailGfxEntitiesOffset - sizeof(std::uint32_t));
    std::wcout << L"scope-backend-data=0x" << std::hex << std::uppercase
               << data << std::dec << L" view-index="
               << (view_index ? *view_index : 0) << L" view-count="
               << (view_count ? *view_count : 0) << L" views=0x"
               << std::hex << (views_pointer ? *views_pointer : 0)
               << std::dec << L" guessed-gfx-entity-count="
               << (gfx_entity_count ? *gfx_entity_count : 0) << L'\n';

    struct GfxEntityProbe final {
        std::uint32_t render_fx_flags{};
        float material_time{};
    };
    static_assert(sizeof(GfxEntityProbe) == 8);
    for (std::uint32_t index = 0; index < 16; ++index) {
        const auto entity = read_value<GfxEntityProbe>(
            process,
            data + kRetailGfxEntitiesOffset +
                static_cast<std::size_t>(index) * kRetailGfxEntityStride);
        if (!entity) {
            break;
        }
        std::wcout << L"scope-gfx-entity[" << index << L"] flags=0x"
                   << std::hex << entity->render_fx_flags << std::dec
                   << L" material-time=" << entity->material_time << L'\n';
    }

    constexpr std::size_t kTailBegin = 0x130000;
    constexpr std::size_t kTailEnd = 0x144000;
    std::vector<std::byte> tail(kTailEnd - kTailBegin);
    SIZE_T tail_bytes = 0;
    if (ReadProcessMemory(
            process,
            reinterpret_cast<const void*>(data + kTailBegin),
            tail.data(), tail.size(), &tail_bytes) &&
        tail_bytes == tail.size()) {
        const auto read_tail_u32 = [&tail](const std::size_t offset) {
            std::uint32_t value = 0;
            std::memcpy(&value, tail.data() + offset, sizeof(value));
            return value;
        };
        struct Candidate final {
            std::size_t entities_offset{};
            std::uint32_t count{};
            std::uint32_t depth_hacked{};
            std::uint32_t nonzero_flags{};
            std::uint32_t zero_tail{};
        };
        std::vector<Candidate> candidates;
        constexpr std::size_t kEntityBytes = 128U * 8U;
        for (std::size_t relative = 0;
             relative + sizeof(std::uint32_t) + kEntityBytes + 16U <=
                 tail.size();
             relative += sizeof(std::uint32_t)) {
            const std::uint32_t count = read_tail_u32(relative);
            if (count == 0 || count >= 128) {
                continue;
            }
            const std::size_t entities = relative + sizeof(std::uint32_t);
            std::uint32_t depth_hacked = 0;
            std::uint32_t nonzero_flags = 0;
            std::uint32_t zero_tail = 0;
            bool reasonable = true;
            for (std::uint32_t index = 0; index < 128; ++index) {
                const std::uint32_t flags =
                    read_tail_u32(entities + index * 8U);
                float material_time = 0.0F;
                std::memcpy(
                    &material_time,
                    tail.data() + entities + index * 8U + 4U,
                    sizeof(material_time));
                if (flags > 0xFFFFU || !std::isfinite(material_time) ||
                    std::fabs(material_time) > 10000000.0F) {
                    reasonable = false;
                    break;
                }
                nonzero_flags += flags != 0 ? 1U : 0U;
                depth_hacked += (flags & 2U) != 0 ? 1U : 0U;
                zero_tail += index >= count && flags == 0 ? 1U : 0U;
            }
            if (!reasonable || depth_hacked == 0 ||
                read_tail_u32(entities + kEntityBytes) > 256U ||
                read_tail_u32(entities + kEntityBytes + 4U) > 4096U ||
                read_tail_u32(entities + kEntityBytes + 8U) > 4096U ||
                read_tail_u32(entities + kEntityBytes + 12U) > 1536U) {
                continue;
            }
            candidates.push_back({
                kTailBegin + entities, count, depth_hacked,
                nonzero_flags, zero_tail});
        }
        std::sort(
            candidates.begin(), candidates.end(),
            [](const Candidate& left, const Candidate& right) {
                if (left.zero_tail != right.zero_tail) {
                    return left.zero_tail > right.zero_tail;
                }
                return left.depth_hacked > right.depth_hacked;
            });
        for (std::size_t index = 0;
             index < std::min<std::size_t>(candidates.size(), 12U);
             ++index) {
            const auto& candidate = candidates[index];
            std::wcout << L"scope-gfx-candidate entities-offset=0x"
                       << std::hex << candidate.entities_offset << std::dec
                       << L" count=" << candidate.count
                       << L" depth-hacked=" << candidate.depth_hacked
                       << L" nonzero-flags=" << candidate.nonzero_flags
                       << L" zero-tail=" << candidate.zero_tail << L'\n';
        }
    }

    if (!views_pointer || *views_pointer == 0 || !view_count ||
        *view_count == 0 || *view_count > 3) {
        return;
    }
    for (std::uint32_t view = 0; view < *view_count; ++view) {
        const auto view_address =
            static_cast<std::uintptr_t>(*views_pointer) +
            static_cast<std::size_t>(view) * kViewInfoStride;
        for (std::size_t list = 0; list < kDrawListOffsets.size(); ++list) {
            const auto list_address = view_address + kDrawListOffsets[list];
            const auto surfaces_pointer = read_value<std::uint32_t>(
                process, list_address);
            const auto surface_count = read_value<std::uint32_t>(
                process, list_address + sizeof(std::uint32_t));
            const auto owner = read_value<std::uint32_t>(
                process, list_address + kDrawListViewInfoOffset);
            const auto camera_view = read_value<std::uint32_t>(
                process, list_address + kDrawListCameraViewOffset);
            if (!surfaces_pointer || !surface_count ||
                *surfaces_pointer == 0 || *surface_count == 0 ||
                *surface_count > 131072) {
                continue;
            }

            std::array<std::uint32_t, 16> type_counts{};
            std::array<std::uint32_t, 128> model_entity_counts{};
            std::uint32_t depth_hacked_models = 0;
            for (std::uint32_t surface = 0; surface < *surface_count;
                 ++surface) {
                const auto packed = read_value<std::uint64_t>(
                    process,
                    static_cast<std::uintptr_t>(*surfaces_pointer) +
                        static_cast<std::size_t>(surface) *
                            sizeof(std::uint64_t));
                if (!packed) {
                    break;
                }
                const auto type = static_cast<std::uint32_t>(
                    (*packed >> 54U) & 0x0FU);
                ++type_counts[type];
                if (type < 7 || type > 9) {
                    continue;
                }
                const auto object_id = static_cast<std::uint16_t>(
                    *packed & 0xFFFFU);
                const auto entity_index = read_value<std::uint16_t>(
                    process,
                    data + static_cast<std::size_t>(object_id) * 4U + 14U);
                if (!entity_index || *entity_index >= 128) {
                    continue;
                }
                ++model_entity_counts[*entity_index];
                const auto flags = read_value<std::uint32_t>(
                    process,
                    data + kRetailGfxEntitiesOffset +
                        static_cast<std::size_t>(*entity_index) *
                            kRetailGfxEntityStride);
                if (flags && (*flags & 2U) != 0) {
                    ++depth_hacked_models;
                }
            }

            std::wcout << L"scope-view=" << view << L" list=" << list
                       << L" surfaces=" << *surface_count
                       << L" owner=0x" << std::hex
                       << (owner ? *owner : 0) << std::dec
                       << L" camera-view="
                       << (camera_view ? *camera_view : 0)
                       << L" types=";
            for (std::size_t type = 0; type < type_counts.size(); ++type) {
                if (type_counts[type] != 0) {
                    std::wcout << type << L':' << type_counts[type] << L',';
                }
            }
            std::wcout << L" model-entities=";
            for (std::size_t index = 0; index < model_entity_counts.size();
                 ++index) {
                if (model_entity_counts[index] != 0) {
                    std::wcout << index << L':' << model_entity_counts[index]
                               << L',';
                }
            }
            std::wcout << L" depth-hacked=" << depth_hacked_models << L'\n';
        }
    }
}

std::wstring window_text(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }

    std::wstring text(static_cast<std::size_t>(length) + 1U, L'\0');
    const int copied = GetWindowTextW(window, text.data(), length + 1);
    if (copied <= 0) {
        return {};
    }
    text.resize(static_cast<std::size_t>(copied));
    return text;
}

std::wstring window_class(HWND window) {
    std::array<wchar_t, 256> name{};
    const int copied = GetClassNameW(
        window, name.data(), static_cast<int>(name.size()));
    return copied > 0
        ? std::wstring(name.data(), static_cast<std::size_t>(copied))
        : std::wstring{};
}

void print_window(HWND window, const wchar_t* prefix) {
    RECT rectangle{};
    GetWindowRect(window, &rectangle);
    std::wcout << prefix << L" hwnd=0x" << std::hex
               << reinterpret_cast<std::uintptr_t>(window) << std::dec
               << L" class=\"" << window_class(window) << L"\""
               << L" title=\"" << window_text(window) << L"\""
               << L" visible=" << (IsWindowVisible(window) ? 1 : 0)
               << L" enabled=" << (IsWindowEnabled(window) ? 1 : 0)
               << L" rect=" << rectangle.left << L',' << rectangle.top << L','
               << rectangle.right << L',' << rectangle.bottom
               << L" control-id=" << GetDlgCtrlID(window) << L'\n';
}

BOOL CALLBACK print_child_window(HWND window, LPARAM) {
    print_window(window, L"  child-window");
    return TRUE;
}

struct WindowEnumeration {
    DWORD process_id{};
    std::size_t count{};
};

BOOL CALLBACK print_process_window(HWND window, LPARAM parameter) {
    auto* enumeration = reinterpret_cast<WindowEnumeration*>(parameter);
    DWORD owner_process_id = 0;
    GetWindowThreadProcessId(window, &owner_process_id);
    if (owner_process_id != enumeration->process_id) {
        return TRUE;
    }

    ++enumeration->count;
    print_window(window, L"top-level-window");
    EnumChildWindows(window, print_child_window, 0);
    return TRUE;
}

void print_process_windows(const DWORD process_id) {
    WindowEnumeration enumeration{process_id, 0};
    EnumWindows(
        print_process_window,
        reinterpret_cast<LPARAM>(&enumeration));
    std::wcout << L"top-level-windows=" << enumeration.count << L'\n';
}

std::optional<std::wstring> read_unicode_string(
    HANDLE process, const std::uintptr_t descriptor_address) {
    UnicodeString32 descriptor{};
    SIZE_T bytes = 0;
    if (!ReadProcessMemory(
            process,
            reinterpret_cast<const void*>(descriptor_address),
            &descriptor,
            sizeof(descriptor),
            &bytes) ||
        bytes != sizeof(descriptor) || descriptor.length == 0 ||
        descriptor.buffer == 0 || (descriptor.length % sizeof(wchar_t)) != 0) {
        return std::nullopt;
    }

    std::wstring value(descriptor.length / sizeof(wchar_t), L'\0');
    if (!ReadProcessMemory(
            process,
            reinterpret_cast<const void*>(descriptor.buffer),
            value.data(),
            descriptor.length,
            &bytes) ||
        bytes != descriptor.length) {
        return std::nullopt;
    }
    return value;
}

void print_process_parameters(HANDLE process) {
    using NtQueryInformationProcessFn = NTSTATUS(NTAPI*)(
        HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto query = reinterpret_cast<NtQueryInformationProcessFn>(
        ntdll != nullptr ? GetProcAddress(ntdll, "NtQueryInformationProcess")
                         : nullptr);
    if (query == nullptr) {
        return;
    }

    PROCESS_BASIC_INFORMATION information{};
    if (query(
            process,
            ProcessBasicInformation,
            &information,
            sizeof(information),
            nullptr) < 0 ||
        information.PebBaseAddress == nullptr) {
        return;
    }

    std::uint32_t process_parameters = 0;
    SIZE_T bytes = 0;
    const auto peb = reinterpret_cast<std::uintptr_t>(information.PebBaseAddress);
    if (!ReadProcessMemory(
            process,
            reinterpret_cast<const void*>(peb + 0x10),
            &process_parameters,
            sizeof(process_parameters),
            &bytes) ||
        bytes != sizeof(process_parameters) || process_parameters == 0) {
        return;
    }

    // Stable 32-bit RTL_USER_PROCESS_PARAMETERS offsets used by this 32-bit
    // diagnostic: CURDIR.DosPath at +0x24 and CommandLine at +0x40.
    if (const auto current = read_unicode_string(process, process_parameters + 0x24)) {
        std::wcout << L"current-directory=" << *current << L'\n';
    }
    if (const auto command = read_unicode_string(process, process_parameters + 0x40)) {
        std::wcout << L"command-line=" << *command << L'\n';
    }
}

std::vector<Module> modules_for(DWORD process_id) {
    std::vector<Module> modules;
    Handle snapshot(CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id));
    if (!snapshot.valid()) {
        return modules;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot.get(), &entry)) {
        return modules;
    }
    do {
        const auto begin = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
        modules.push_back({begin, begin + entry.modBaseSize, entry.szModule});
    } while (Module32NextW(snapshot.get(), &entry));
    return modules;
}

const Module* containing(
    const std::vector<Module>& modules, const std::uintptr_t address) {
    const auto found = std::find_if(
        modules.begin(), modules.end(), [address](const Module& module) {
            return address >= module.begin && address < module.end;
        });
    return found != modules.end() ? &*found : nullptr;
}

void print_address(
    const std::vector<Module>& modules, const std::uintptr_t address) {
    std::wcout << L"0x" << std::hex << std::uppercase << std::setw(8)
               << std::setfill(L'0') << address << std::dec;
    if (const Module* module = containing(modules, address)) {
        std::wcout << L" " << module->name << L"+0x" << std::hex
                   << (address - module->begin) << std::dec;
    }
}

bool inspect_thread(
    HANDLE process,
    const std::vector<Module>& modules,
    const THREADENTRY32& entry) {
    Handle thread(OpenThread(
        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
        FALSE,
        entry.th32ThreadID));
    if (!thread.valid()) {
        std::wcerr << L"thread " << entry.th32ThreadID
                   << L": OpenThread failed: " << GetLastError() << L'\n';
        return false;
    }

    const DWORD previous_suspend_count = SuspendThread(thread.get());
    if (previous_suspend_count == std::numeric_limits<DWORD>::max()) {
        std::wcerr << L"thread " << entry.th32ThreadID
                   << L": SuspendThread failed: " << GetLastError() << L'\n';
        return false;
    }

    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    const BOOL context_ok = GetThreadContext(thread.get(), &context);

    std::array<std::uint32_t, 96> stack{};
    SIZE_T stack_bytes = 0;
    const BOOL stack_ok = context_ok && ReadProcessMemory(
        process,
        reinterpret_cast<const void*>(context.Esp),
        stack.data(),
        sizeof(stack),
        &stack_bytes);

    ResumeThread(thread.get());

    if (!context_ok) {
        std::wcerr << L"thread " << entry.th32ThreadID
                   << L": GetThreadContext failed: " << GetLastError() << L'\n';
        return false;
    }

    std::wcout << L"thread " << entry.th32ThreadID << L" EIP=";
    print_address(modules, context.Eip);
    std::wcout << L" ESP=0x" << std::hex << context.Esp
               << L" EBP=0x" << context.Ebp << std::dec << L'\n';

    if (!stack_ok) {
        std::wcout << L"  stack read failed: " << GetLastError() << L'\n';
        return true;
    }

    std::size_t shown = 0;
    const std::size_t word_count = stack_bytes / sizeof(stack[0]);
    for (std::size_t index = 0; index < word_count && shown < 16; ++index) {
        const auto value = static_cast<std::uintptr_t>(stack[index]);
        if (containing(modules, value) == nullptr) {
            continue;
        }
        std::wcout << L"  stack[" << index << L"]=";
        print_address(modules, value);
        std::wcout << L'\n';
        ++shown;
    }
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const bool state_only =
        argc == 3 && std::wstring_view(argv[1]) == L"--state-only";
    const bool scope_backend =
        argc == 3 && std::wstring_view(argv[1]) == L"--scope-backend";
    const bool camera_samples =
        argc == 5 && std::wstring_view(argv[1]) == L"--camera-samples";
    if ((!state_only && !scope_backend && !camera_samples && argc != 2) ||
        (state_only && argc != 3) || (camera_samples && argc != 5)) {
        std::wcerr
            << L"usage: wawvr-process-probe [--state-only|--scope-backend] <process-id>\n"
            << L"       wawvr-process-probe --camera-samples <process-id> <count> <interval-ms>\n";
        return 2;
    }

    const DWORD process_id =
        std::stoul(argv[state_only || scope_backend || camera_samples ? 2 : 1]);
    Handle process(OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id));
    if (!process.valid()) {
        std::wcerr << L"OpenProcess failed: " << GetLastError() << L'\n';
        return 1;
    }

    if (camera_samples) {
        constexpr std::uintptr_t kMainRefdefOrigin = 0x03520354;
        struct CameraProbe final {
            float origin[3]{};
            float unknown_28{};
            float axis[3][3]{};
        };
        static_assert(sizeof(CameraProbe) == 0x34);
        const unsigned long count = std::stoul(argv[3]);
        const unsigned long interval = std::stoul(argv[4]);
        for (unsigned long sample = 0; sample < count; ++sample) {
            const auto camera = read_value<CameraProbe>(
                process.get(), kMainRefdefOrigin);
            if (!camera) {
                std::wcerr << L"camera sample failed: " << GetLastError()
                           << L'\n';
                return 1;
            }
            std::wcout
                << sample << L" origin="
                << camera->origin[0] << L',' << camera->origin[1] << L','
                << camera->origin[2] << L" forward="
                << camera->axis[0][0] << L',' << camera->axis[0][1] << L','
                << camera->axis[0][2] << L" left="
                << camera->axis[1][0] << L',' << camera->axis[1][1] << L','
                << camera->axis[1][2] << L" up="
                << camera->axis[2][0] << L',' << camera->axis[2][1] << L','
                << camera->axis[2][2] << L'\n';
            if (sample + 1 < count && interval != 0) {
                Sleep(interval);
            }
        }
        return 0;
    }

    if (scope_backend) {
        print_scope_backend_state(process.get());
        return 0;
    }

    const auto modules = modules_for(process_id);
    std::wcout << L"modules=" << modules.size() << L'\n';
    print_process_parameters(process.get());
    print_supported_t4_state(process.get());
    print_process_windows(process_id);
    if (state_only) {
        return 0;
    }

    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    if (!snapshot.valid()) {
        std::wcerr << L"thread snapshot failed: " << GetLastError() << L'\n';
        return 1;
    }

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (!Thread32First(snapshot.get(), &entry)) {
        std::wcerr << L"Thread32First failed: " << GetLastError() << L'\n';
        return 1;
    }

    bool found = false;
    do {
        if (entry.th32OwnerProcessID == process_id) {
            found = true;
            inspect_thread(process.get(), modules, entry);
        }
    } while (Thread32Next(snapshot.get(), &entry));

    return found ? 0 : 1;
}
