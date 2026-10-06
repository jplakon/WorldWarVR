#include <wawvr_launcher/launcher_core.hpp>

#include <pezbot_helper_hash.hpp>

#if defined(WAWVR_HAS_T4_PROFILE)
#include "t4/profile.hpp"
#endif

#include <Windows.h>
#include <bcrypt.h>
#include <ShlObj.h>
#include <TlHelp32.h>
#include <winioctl.h>
#include <winver.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace wawvr::launcher
{
    namespace
    {
        constexpr std::uint16_t kImageFileMachineI386 = 0x014c;
        constexpr std::uint16_t kPe32Magic = 0x010b;
        constexpr std::uint16_t kImageFileDll = 0x2000;
        constexpr std::uint32_t kExpectedSpTimestamp = 0x4AEA1F46;
        constexpr std::uint32_t kExpectedSpEntrypointRva = 0x003AF316;
        constexpr std::uint32_t kExpectedMpTimestamp = 0x4AEA1F42;
        constexpr std::uint32_t kExpectedMpEntrypointRva = 0x003A8256;
        constexpr std::uint32_t kExpectedSteamSpTimestamp = 0x4AEA1F46;
        constexpr std::uint32_t kExpectedSteamSpEntrypointRva = 0x04ABB2ED;
        constexpr std::uint32_t kExpectedSteamMpTimestamp = 0x4AEA1F42;
        constexpr std::uint32_t kExpectedSteamMpEntrypointRva = 0x111762ED;
        constexpr std::uint64_t kExpectedImageBase = 0x00400000;
        constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 3>
            kSpRendererCriticalRoutineRvas = {{
                {0x002D62A0, 0x002D6307},
                {0x002D5FF0, 0x002D6108},
                {0x002D6310, 0x002D6375},
            }};
        constexpr std::array<std::uint32_t, 2>
            kSpRendererCriticalReturnRvas = {
                0x002D62C0,
                0x002D605C,
            };
        constexpr std::size_t kMaximumSpRendererStackProofBytes =
            1024U * 1024U;
        constexpr std::array<const wchar_t*, 2> kRuntimeDataDirectories = {
            L"zone",
            L"main",
        };
        constexpr std::uint32_t kHandoffParentWaitMilliseconds = 60'000;
        constexpr std::uint32_t kPezBotImportTimeoutMilliseconds = 30'000;
        constexpr std::uintmax_t kMaximumActiveProfileBytes = 1'024;
        constexpr std::uintmax_t kMaximumMpProfileConfigBytes = 2 * 1'024 * 1'024;
        constexpr std::uintmax_t kMaximumLocalizationSelectorBytes = 64 * 1'024;
        constexpr std::string_view kUiDedicatedDisabledLine =
            "seta ui_dedicated \"0\"";
        constexpr auto kPezBotReceiptFilename =
            L".wawvr-pezbot-005p.receipt";
        constexpr auto kExpectedPezBotImportHelperSha256 =
            WAWVR_PEZBOT_IMPORT_HELPER_SHA256;
        constexpr std::array<const wchar_t*, 4> kPezBotRequiredRootFiles = {
            L"mod.ff",
            L"PeZBOTWaW.iwd",
            L"pezbot.cfg",
            L"pezbot_dev.cfg",
        };

        [[nodiscard]] bool is_verified_pezbot_helper(
            const fs::path& helper) noexcept;
        [[nodiscard]] bool is_normal_file(const fs::path& path) noexcept;
        [[nodiscard]] bool is_normal_directory(const fs::path& path) noexcept;

        struct RootSupportSpec
        {
            const wchar_t* label;
            const wchar_t* filename;
            const char* expected_sha256;
        };

        constexpr std::array<RootSupportSpec, 4> kRootSupportFiles = {{
            {L"Bink runtime", L"binkw32.dll", kExpectedBinkSha256},
            // The selector contains localized startup text as well as the
            // installed language token. Its exact digest is therefore derived
            // from the current Steam-owned source on every launch.
            {L"localization root support", L"localization.txt", ""},
            {L"console bitmap root support", L"cod.bmp",
             "A49A0F67AFEDA9313D242C5B496FD04D809D9A530D0196E7840B44763CF075D6"},
            {L"console-logo bitmap root support", L"codlogo.bmp",
             "56F57E6AF1D4294A6C5C44E3D124F7280C5CC98C5F66EBA0D009E43086000133"},
        }};

        struct DerRieseFastfile
        {
            const wchar_t* label;
            const wchar_t* filename;
            const char* expected_sha256;
        };

        constexpr std::array<DerRieseFastfile, 4> kDerRieseFastfiles = {{
            {L"base", L"nazi_zombie_factory.ff", kExpectedDerRieseSha256},
            {L"load", L"nazi_zombie_factory_load.ff", kExpectedDerRieseLoadSha256},
            {L"patch", L"nazi_zombie_factory_patch.ff", kExpectedDerRiesePatchSha256},
            {L"localized", L"localized_nazi_zombie_factory.ff",
             kExpectedLocalizedDerRieseSha256},
        }};

        struct MultiplayerFastfile
        {
            const wchar_t* label;
            const wchar_t* filename;
            const char* expected_sha256;
        };

        constexpr std::array<MultiplayerFastfile, 6> kMultiplayerFastfiles = {{
            {L"code post-gfx", L"code_post_gfx_mp.ff",
             "D5CB1A61466763181879C96E4C5028A333AF3CDB06813FFB97C4382672555186"},
            {L"patch", L"patch_mp.ff",
             "C282EFA039044774567DA841EAE89142C33E9189BA840639C22077B5B55E0870"},
            {L"UI", L"ui_mp.ff",
             "6039BAA4B631AFF24D11F224F95F702E182A98334B8E4C618F452146D95A3642"},
            {L"common", L"common_mp.ff",
             "165609E74216F3A1656B97F49A80B8D9148E518E63A721C1351EFAAFC0B1940A"},
            {L"localized code post-gfx", L"localized_code_post_gfx_mp.ff",
             "997A9491439B1E6E70252AE9B664F264D046AB16B3E9FD3B89926A1775EBBBE8"},
            {L"localized common", L"localized_common_mp.ff",
             "26FD3F85E954263831525DDE0946A36DFD2ED998DC71F3FFA743884B08155F5F"},
        }};

        struct ExecutableIdentity
        {
            ExecutableIdentityId id;
            ExecutableKind kind;
            const char* stable_id;
            const wchar_t* display_name;
            const wchar_t* staged_filename;
            const wchar_t* legacy_filename;
            const wchar_t* runtime_leaf;
            const wchar_t* home_leaf;
            const wchar_t* source_environment_variable;
            const char* expected_sha256;
            std::uint32_t expected_timestamp;
            std::uint32_t expected_entrypoint_rva;
            bool uses_steam;
        };

        constexpr ExecutableIdentity kSpExecutableIdentity{
            ExecutableIdentityId::plutonium_sp_1_7_1263,
            ExecutableKind::sp,
            "plutonium-sp-1.7.1263",
            L"SP/Zombies",
            L"CoDWaW.exe",
            L"t4sp.exe",
            L"waw-1.7.1263",
            L"home",
            L"WAWVR_SOURCE_EXE",
            kExpectedSpExeSha256,
            kExpectedSpTimestamp,
            kExpectedSpEntrypointRva,
            false,
        };

        constexpr ExecutableIdentity kMpExecutableIdentity{
            ExecutableIdentityId::plutonium_mp_1_7_1263,
            ExecutableKind::mp,
            "plutonium-mp-1.7.1263",
            L"offline multiplayer",
            L"CoDWaWmp.exe",
            L"t4mp.exe",
            L"waw-mp-1.7.1263",
            L"home-mp",
            L"WAWVR_MP_SOURCE_EXE",
            kExpectedMpExeSha256,
            kExpectedMpTimestamp,
            kExpectedMpEntrypointRva,
            false,
        };

        constexpr ExecutableIdentity kSteamSpExecutableIdentity{
            ExecutableIdentityId::steam_sp_build_252004,
            ExecutableKind::sp,
            "steam-sp-build-252004",
            L"Steam SP/Zombies Build 252004",
            L"CoDWaW.exe",
            L"t4sp.exe",
            L"waw-steam-252004-1.7",
            L"home-steam-252004",
            L"WAWVR_SOURCE_EXE",
            kExpectedSteamSpExeSha256,
            kExpectedSteamSpTimestamp,
            kExpectedSteamSpEntrypointRva,
            true,
        };

        constexpr ExecutableIdentity kSteamMpExecutableIdentity{
            ExecutableIdentityId::steam_mp_build_252004,
            ExecutableKind::mp,
            "steam-mp-build-252004",
            L"Steam offline multiplayer Build 252004",
            L"CoDWaWmp.exe",
            L"t4mp.exe",
            L"waw-mp-steam-252004-1.7",
            L"home-mp-steam-252004",
            L"WAWVR_MP_SOURCE_EXE",
            kExpectedSteamMpExeSha256,
            kExpectedSteamMpTimestamp,
            kExpectedSteamMpEntrypointRva,
            true,
        };

        constexpr std::array<const ExecutableIdentity*, 4>
            kExecutableIdentities = {{
                &kSteamSpExecutableIdentity,
                &kSteamMpExecutableIdentity,
                &kSpExecutableIdentity,
                &kMpExecutableIdentity,
            }};

        [[nodiscard]] constexpr ExecutableKind executable_kind_for_target(
            const LaunchTarget target) noexcept
        {
            return target == LaunchTarget::offline_multiplayer
                ? ExecutableKind::mp
                : ExecutableKind::sp;
        }

        [[nodiscard]] constexpr const ExecutableIdentity& default_executable_identity(
            const ExecutableKind kind) noexcept
        {
            return kind == ExecutableKind::mp
                ? kMpExecutableIdentity
                : kSpExecutableIdentity;
        }

        [[nodiscard]] const ExecutableIdentity& executable_identity(
            const ExecutableIdentityId id)
        {
            switch (id)
            {
            case ExecutableIdentityId::plutonium_sp_1_7_1263:
                return kSpExecutableIdentity;
            case ExecutableIdentityId::plutonium_mp_1_7_1263:
                return kMpExecutableIdentity;
            case ExecutableIdentityId::steam_sp_build_252004:
                return kSteamSpExecutableIdentity;
            case ExecutableIdentityId::steam_mp_build_252004:
                return kSteamMpExecutableIdentity;
            }
            throw std::invalid_argument("Unknown executable identity");
        }

        [[nodiscard]] constexpr ExecutableIdentityId paired_mp_identity(
            const ExecutableIdentityId sp_identity) noexcept
        {
            return sp_identity == ExecutableIdentityId::steam_sp_build_252004
                ? ExecutableIdentityId::steam_mp_build_252004
                : ExecutableIdentityId::plutonium_mp_1_7_1263;
        }

        class UniqueHandle
        {
        public:
            UniqueHandle() = default;
            explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
            ~UniqueHandle()
            {
                if (valid())
                {
                    CloseHandle(handle_);
                }
            }

            UniqueHandle(const UniqueHandle&) = delete;
            UniqueHandle& operator=(const UniqueHandle&) = delete;

            UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release()) {}
            UniqueHandle& operator=(UniqueHandle&& other) noexcept
            {
                if (this != &other)
                {
                    if (valid())
                    {
                        CloseHandle(handle_);
                    }
                    handle_ = other.release();
                }
                return *this;
            }

            [[nodiscard]] HANDLE get() const noexcept { return handle_; }
            [[nodiscard]] bool valid() const noexcept
            {
                return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
            }

            HANDLE release() noexcept
            {
                const auto result = handle_;
                handle_ = nullptr;
                return result;
            }

        private:
            HANDLE handle_ = nullptr;
        };

        class RemoteAllocation
        {
        public:
            RemoteAllocation() = default;
            RemoteAllocation(HANDLE process, void* address) noexcept
                : process_(process), address_(address)
            {
            }

            ~RemoteAllocation()
            {
                reset();
            }

            RemoteAllocation(const RemoteAllocation&) = delete;
            RemoteAllocation& operator=(const RemoteAllocation&) = delete;

            RemoteAllocation(RemoteAllocation&& other) noexcept
                : process_(other.process_), address_(other.address_)
            {
                other.process_ = nullptr;
                other.address_ = nullptr;
            }

            RemoteAllocation& operator=(RemoteAllocation&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    process_ = other.process_;
                    address_ = other.address_;
                    other.process_ = nullptr;
                    other.address_ = nullptr;
                }
                return *this;
            }

            [[nodiscard]] void* get() const noexcept { return address_; }

            void reset() noexcept
            {
                if (process_ && address_)
                {
                    VirtualFreeEx(process_, address_, 0, MEM_RELEASE);
                }
                process_ = nullptr;
                address_ = nullptr;
            }

        private:
            HANDLE process_ = nullptr;
            void* address_ = nullptr;
        };

        [[noreturn]] void throw_last_error(const char* operation)
        {
            throw std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                operation);
        }

        std::wstring widen_ascii(const std::string& text)
        {
            return {text.begin(), text.end()};
        }

        std::wstring hex_value(const std::uint64_t value, const int width)
        {
            std::wostringstream stream;
            stream << L"0x" << std::uppercase << std::hex << std::setw(width)
                   << std::setfill(L'0') << value;
            return stream.str();
        }

        std::wstring lower_path_string(const fs::path& path)
        {
            auto value = fs::absolute(path).lexically_normal().wstring();
            std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t ch) {
                return static_cast<wchar_t>(std::towlower(ch));
            });
            return value;
        }

        fs::path known_folder(const KNOWNFOLDERID& id)
        {
            PWSTR raw = nullptr;
            const auto result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw);
            if (FAILED(result))
            {
                throw std::runtime_error("SHGetKnownFolderPath failed");
            }

            const fs::path path(raw);
            CoTaskMemFree(raw);
            return path;
        }

        std::optional<std::wstring> environment_value(const wchar_t* name)
        {
            const auto needed = GetEnvironmentVariableW(name, nullptr, 0);
            if (needed == 0)
            {
                return std::nullopt;
            }

            std::wstring value(needed, L'\0');
            const auto written = GetEnvironmentVariableW(name, value.data(), needed);
            if (written == 0 || written >= needed)
            {
                throw_last_error("GetEnvironmentVariableW");
            }
            value.resize(written);
            return value;
        }

        std::vector<std::wstring> inherited_environment_entries()
        {
            wchar_t* const raw = GetEnvironmentStringsW();
            if (raw == nullptr)
            {
                throw_last_error("GetEnvironmentStringsW");
            }
            std::vector<std::wstring> entries;
            try
            {
                for (const wchar_t* cursor = raw; *cursor != L'\0';)
                {
                    const std::wstring entry(cursor);
                    entries.push_back(entry);
                    cursor += entry.size() + 1U;
                }
            }
            catch (...)
            {
                FreeEnvironmentStringsW(raw);
                throw;
            }
            FreeEnvironmentStringsW(raw);
            return entries;
        }

        std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint16_t))
            {
                throw std::runtime_error("Truncated PE file");
            }
            return static_cast<std::uint16_t>(bytes[offset]) |
                   (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
        }

        std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t))
            {
                throw std::runtime_error("Truncated PE file");
            }
            return static_cast<std::uint32_t>(bytes[offset]) |
                   (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
                   (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
                   (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
        }

        std::vector<std::uint8_t> read_prefix(const fs::path& path, const std::size_t limit)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error("Could not open file: " + path.string());
            }

            std::vector<std::uint8_t> bytes(limit);
            input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(limit));
            bytes.resize(static_cast<std::size_t>(input.gcount()));
            return bytes;
        }

        void add_check(
            DiagnosticReport& report,
            std::wstring name,
            const bool passed,
            std::wstring detail)
        {
            report.checks.push_back({std::move(name), passed, std::move(detail)});
        }

        void add_optional_check(
            DiagnosticReport& report,
            std::wstring name,
            const bool passed,
            std::wstring detail)
        {
            report.checks.push_back(
                {std::move(name), passed, std::move(detail), false});
        }

        template <typename Function>
        void checked(
            DiagnosticReport& report,
            std::wstring name,
            Function&& function)
        {
            try
            {
                auto [passed, detail] = function();
                add_check(report, std::move(name), passed, std::move(detail));
            }
            catch (const std::exception& error)
            {
                add_check(report, std::move(name), false, widen_ascii(error.what()));
            }
        }

        template <typename Function>
        void checked_optional(
            DiagnosticReport& report,
            std::wstring name,
            Function&& function)
        {
            try
            {
                auto [passed, detail] = function();
                add_optional_check(
                    report, std::move(name), passed, std::move(detail));
            }
            catch (const std::exception& error)
            {
                add_optional_check(
                    report, std::move(name), false, widen_ascii(error.what()));
            }
        }

        std::pair<bool, std::wstring> file_exists_check(const fs::path& path)
        {
            const auto exists = fs::is_regular_file(path);
            return {exists, exists ? path.wstring() : L"Missing: " + path.wstring()};
        }

        [[nodiscard]] bool is_verified_staged_executable_candidate(
            const fs::path& path,
            const char* expected_sha256) noexcept
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                return false;
            }
            try
            {
                return sha256_file(path) == expected_sha256;
            }
            catch (...)
            {
                return false;
            }
        }

        void validate_write_boundaries(const LaunchPlan& plan)
        {
            if (is_same_or_descendant(plan.runtime_dir, plan.game_dir) ||
                is_same_or_descendant(plan.home_dir, plan.game_dir))
            {
                throw std::runtime_error(
                    "Runtime and home paths must be outside the installed game directory");
            }

            const auto source_parent = plan.source_exe.parent_path();
            const bool source_is_managed_staged_executable =
                is_same_path(plan.source_exe, plan.staged_exe);
            if ((!source_is_managed_staged_executable &&
                 is_same_or_descendant(plan.runtime_dir, source_parent)) ||
                is_same_or_descendant(plan.home_dir, source_parent))
            {
                throw std::runtime_error(
                    "Runtime and home paths must not modify the source executable directory");
            }

            if (is_same_or_descendant(plan.runtime_dir, plan.home_dir) ||
                is_same_or_descendant(plan.home_dir, plan.runtime_dir))
            {
                throw std::runtime_error("Runtime and home paths must be separate directories");
            }

            // A lexical check alone can be bypassed by making an existing
            // runtime/home directory a junction into the protected game tree.
            // Launcher-owned roots are expected to be ordinary directories.
            for (const auto& [label, path] : std::array{
                     std::pair{"Runtime", plan.runtime_dir},
                     std::pair{"Home", plan.home_dir},
                 })
            {
                const auto attributes = GetFileAttributesW(path.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES &&
                    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                {
                    throw std::runtime_error(
                        std::string(label) + " directory must not itself be a reparse point");
                }
            }
        }

        std::string narrow_for_error(const fs::path& path)
        {
            const auto value = path.wstring();
            if (value.empty())
            {
                return {};
            }
            const auto required = WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (required <= 0)
            {
                throw_last_error("WideCharToMultiByte(path)");
            }
            std::string result(static_cast<std::size_t>(required), '\0');
            if (WideCharToMultiByte(
                    CP_UTF8,
                    WC_ERR_INVALID_CHARS,
                    value.data(),
                    static_cast<int>(value.size()),
                    result.data(),
                    required,
                    nullptr,
                    nullptr) != required)
            {
                throw_last_error("WideCharToMultiByte(path)");
            }
            return result;
        }

        bool paths_resolve_to_same_directory(const fs::path& left, const fs::path& right)
        {
            std::error_code error;
            const auto equivalent = fs::equivalent(left, right, error);
            if (error)
            {
                if (error == std::error_code(
                        ERROR_UNTRUSTED_MOUNT_POINT,
                        std::system_category()))
                {
                    throw std::runtime_error(
                        "Windows blocked access to the prepared game-data links "
                        "because they contain an untrusted mount point. Close "
                        "World War VR, right-click World War VR, select Run as "
                        "administrator, and try again.");
                }
                throw std::system_error(
                    error,
                    "Could not resolve runtime data link target");
            }
            return equivalent;
        }

        enum class RuntimeLinkPresence
        {
            absent,
            matching_junction,
        };

        RuntimeLinkPresence validate_existing_runtime_link(const RuntimeDataLink& link)
        {
            WIN32_FILE_ATTRIBUTE_DATA attributes{};
            if (!GetFileAttributesExW(
                    link.destination.c_str(),
                    GetFileExInfoStandard,
                    &attributes))
            {
                const auto error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                {
                    return RuntimeLinkPresence::absent;
                }
                SetLastError(error);
                throw_last_error("GetFileAttributesExW(runtime data link)");
            }

            if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                (attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
            {
                throw std::runtime_error(
                    "Refusing to replace unmanaged runtime path: " +
                    narrow_for_error(link.destination));
            }

            UniqueHandle reparse_handle(CreateFileW(
                link.destination.c_str(),
                FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr));
            if (!reparse_handle.valid())
            {
                throw_last_error("CreateFileW(runtime data reparse point)");
            }

            FILE_ATTRIBUTE_TAG_INFO tag{};
            if (!GetFileInformationByHandleEx(
                    reparse_handle.get(),
                    FileAttributeTagInfo,
                    &tag,
                    sizeof(tag)))
            {
                throw_last_error("GetFileInformationByHandleEx(FileAttributeTagInfo)");
            }
            if (tag.ReparseTag != IO_REPARSE_TAG_MOUNT_POINT)
            {
                throw std::runtime_error(
                    "Runtime data path is a reparse point, but not a launcher-managed "
                    "directory junction: " + narrow_for_error(link.destination));
            }

            if (!paths_resolve_to_same_directory(link.destination, link.source))
            {
                throw std::runtime_error(
                    "Runtime data junction target does not match the validated game data: " +
                    narrow_for_error(link.destination));
            }
            return RuntimeLinkPresence::matching_junction;
        }

        std::optional<fs::path> resolve_directory_junction_target(
            const fs::path& junction) noexcept
        {
            try
            {
                const DWORD attributes = GetFileAttributesW(junction.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES ||
                    (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                {
                    return std::nullopt;
                }

                UniqueHandle reparse_handle(CreateFileW(
                    junction.c_str(),
                    FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                    nullptr));
                if (!reparse_handle.valid())
                {
                    return std::nullopt;
                }
                FILE_ATTRIBUTE_TAG_INFO tag{};
                if (!GetFileInformationByHandleEx(
                        reparse_handle.get(),
                        FileAttributeTagInfo,
                        &tag,
                        sizeof(tag)) ||
                    tag.ReparseTag != IO_REPARSE_TAG_MOUNT_POINT)
                {
                    return std::nullopt;
                }

                UniqueHandle target_handle(CreateFileW(
                    junction.c_str(),
                    FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS,
                    nullptr));
                if (!target_handle.valid())
                {
                    return std::nullopt;
                }
                constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
                const DWORD required = GetFinalPathNameByHandleW(
                    target_handle.get(), nullptr, 0, flags);
                if (required == 0)
                {
                    return std::nullopt;
                }
                std::wstring resolved(static_cast<std::size_t>(required), L'\0');
                const DWORD written = GetFinalPathNameByHandleW(
                    target_handle.get(), resolved.data(), required, flags);
                if (written == 0 || written >= required)
                {
                    return std::nullopt;
                }
                resolved.resize(written);
                if (resolved.rfind(L"\\\\?\\UNC\\", 0) == 0)
                {
                    resolved = L"\\\\" + resolved.substr(8);
                }
                else if (resolved.rfind(L"\\\\?\\", 0) == 0)
                {
                    resolved.erase(0, 4);
                }
                return fs::path(std::move(resolved)).lexically_normal();
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        std::optional<fs::path> discover_game_directory_from_runtime(
            const fs::path& runtime_dir) noexcept
        {
            const auto main_target =
                resolve_directory_junction_target(runtime_dir / L"main");
            const auto zone_target =
                resolve_directory_junction_target(runtime_dir / L"zone");
            if (!main_target || !zone_target ||
                _wcsicmp(main_target->filename().c_str(), L"main") != 0 ||
                _wcsicmp(zone_target->filename().c_str(), L"zone") != 0 ||
                !is_same_path(
                    main_target->parent_path(), zone_target->parent_path()))
            {
                return std::nullopt;
            }
            return main_target->parent_path();
        }

        std::wstring junction_substitute_name(const fs::path& target)
        {
            auto absolute = fs::absolute(target).lexically_normal().wstring();
            while (absolute.size() > 3 &&
                   (absolute.back() == L'\\' || absolute.back() == L'/'))
            {
                absolute.pop_back();
            }

            if (absolute.rfind(L"\\\\?\\UNC\\", 0) == 0)
            {
                return L"\\??\\UNC\\" + absolute.substr(8);
            }
            if (absolute.rfind(L"\\\\?\\", 0) == 0)
            {
                return L"\\??\\" + absolute.substr(4);
            }
            if (absolute.size() >= 3 && std::iswalpha(absolute[0]) &&
                absolute[1] == L':' &&
                (absolute[2] == L'\\' || absolute[2] == L'/'))
            {
                return L"\\??\\" + absolute;
            }
            if (absolute.rfind(L"\\\\", 0) == 0)
            {
                return L"\\??\\UNC\\" + absolute.substr(2);
            }
            throw std::runtime_error("Junction target must be an absolute DOS or UNC path");
        }

        void create_directory_junction(const RuntimeDataLink& link)
        {
            if (!CreateDirectoryW(link.destination.c_str(), nullptr))
            {
                const auto error = GetLastError();
                if (error == ERROR_ALREADY_EXISTS)
                {
                    static_cast<void>(validate_existing_runtime_link(link));
                    return;
                }
                SetLastError(error);
                throw_last_error("CreateDirectoryW(runtime data junction)");
            }

            bool committed = false;
            try
            {
                UniqueHandle handle(CreateFileW(
                    link.destination.c_str(),
                    GENERIC_WRITE,
                    0,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                    nullptr));
                if (!handle.valid())
                {
                    throw_last_error("CreateFileW(new runtime data junction)");
                }

                const auto substitute = junction_substitute_name(link.source);
                const auto print_name = fs::absolute(link.source).lexically_normal().wstring();
                const auto substitute_bytes = substitute.size() * sizeof(wchar_t);
                const auto print_bytes = print_name.size() * sizeof(wchar_t);
                constexpr std::size_t kHeaderBytes = 16;
                const auto total_bytes = kHeaderBytes + substitute_bytes + sizeof(wchar_t) +
                    print_bytes + sizeof(wchar_t);
                if (total_bytes > MAXIMUM_REPARSE_DATA_BUFFER_SIZE ||
                    substitute_bytes > std::numeric_limits<std::uint16_t>::max() ||
                    print_bytes > std::numeric_limits<std::uint16_t>::max())
                {
                    throw std::runtime_error("Runtime data junction target is too long");
                }

                std::vector<std::byte> buffer(total_bytes);
                const auto put_u16 = [&](const std::size_t offset, const std::uint16_t value) {
                    std::memcpy(buffer.data() + offset, &value, sizeof(value));
                };
                const auto put_u32 = [&](const std::size_t offset, const std::uint32_t value) {
                    std::memcpy(buffer.data() + offset, &value, sizeof(value));
                };
                put_u32(0, IO_REPARSE_TAG_MOUNT_POINT);
                put_u16(4, static_cast<std::uint16_t>(total_bytes - 8));
                put_u16(6, 0);
                put_u16(8, 0);
                put_u16(10, static_cast<std::uint16_t>(substitute_bytes));
                put_u16(12, static_cast<std::uint16_t>(substitute_bytes + sizeof(wchar_t)));
                put_u16(14, static_cast<std::uint16_t>(print_bytes));
                std::memcpy(buffer.data() + kHeaderBytes, substitute.data(), substitute_bytes);
                std::memcpy(
                    buffer.data() + kHeaderBytes + substitute_bytes + sizeof(wchar_t),
                    print_name.data(),
                    print_bytes);

                DWORD returned = 0;
                if (!DeviceIoControl(
                        handle.get(),
                        FSCTL_SET_REPARSE_POINT,
                        buffer.data(),
                        static_cast<DWORD>(buffer.size()),
                        nullptr,
                        0,
                        &returned,
                        nullptr))
                {
                    throw_last_error("FSCTL_SET_REPARSE_POINT(runtime data junction)");
                }
                committed = true;
            }
            catch (...)
            {
                if (!committed)
                {
                    RemoveDirectoryW(link.destination.c_str());
                }
                throw;
            }

            if (validate_existing_runtime_link(link) !=
                RuntimeLinkPresence::matching_junction)
            {
                throw std::runtime_error("New runtime data junction failed validation");
            }
        }

        std::wstring runtime_link_diagnostic_detail(const LaunchPlan& plan)
        {
            std::wstring detail;
            for (const auto& link : runtime_data_links(plan))
            {
                if (!detail.empty())
                {
                    detail += L"; ";
                }
                const auto state = validate_existing_runtime_link(link);
                detail += link.relative_name.wstring();
                detail += state == RuntimeLinkPresence::matching_junction
                    ? L" junction verified"
                    : L" junction will be created";
            }
            return detail;
        }

        void copy_verified_file_to_runtime(
            const LaunchPlan& plan,
            const fs::path& source,
            const fs::path& destination,
            const std::string& expected_sha256,
            const char* description)
        {
            const auto normalized_runtime = plan.runtime_dir.lexically_normal();
            const auto normalized_destination = destination.lexically_normal();
            if (!is_same_or_descendant(normalized_destination, normalized_runtime) ||
                normalized_destination.parent_path() != normalized_runtime)
            {
                throw std::logic_error(
                    std::string(description) + " destination escaped the runtime root");
            }

            const DWORD source_attributes = GetFileAttributesW(source.c_str());
            if (source_attributes == INVALID_FILE_ATTRIBUTES ||
                (source_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
                (source_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                throw std::runtime_error(
                    std::string(description) +
                    " source is missing, not a file, or a reparse point");
            }

            const DWORD destination_attributes =
                GetFileAttributesW(normalized_destination.c_str());
            if (destination_attributes != INVALID_FILE_ATTRIBUTES &&
                (destination_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                throw std::runtime_error(
                    std::string(description) + " destination must not be a reparse point");
            }
            if (destination_attributes != INVALID_FILE_ATTRIBUTES &&
                (destination_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                sha256_file(normalized_destination) == expected_sha256)
            {
                return;
            }

            // A managed staged self-source cannot repair itself after its hash
            // changes. Never turn an in-place source into the temporary copy.
            if (is_same_path(source, normalized_destination))
            {
                throw std::runtime_error(
                    std::string(description) + " managed self-source failed verification");
            }

            const auto temporary = normalized_runtime /
                (normalized_destination.filename().wstring() + L".tmp-" +
                 std::to_wstring(GetCurrentProcessId()));
            const DWORD temporary_attributes = GetFileAttributesW(temporary.c_str());
            if (temporary_attributes != INVALID_FILE_ATTRIBUTES &&
                (temporary_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                throw std::runtime_error(
                    std::string(description) + " temporary path is a reparse point");
            }
            DeleteFileW(temporary.c_str());

            if (!CopyFileW(source.c_str(), temporary.c_str(), TRUE))
            {
                throw_last_error("CopyFileW");
            }

            try
            {
                if (sha256_file(temporary) != expected_sha256)
                {
                    throw std::runtime_error(
                        std::string(description) +
                        " failed its post-copy SHA-256 check");
                }

                if (!MoveFileExW(
                        temporary.c_str(),
                        normalized_destination.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    throw_last_error("MoveFileExW");
                }
            }
            catch (...)
            {
                DeleteFileW(temporary.c_str());
                throw;
            }

            const DWORD final_attributes =
                GetFileAttributesW(normalized_destination.c_str());
            if (final_attributes == INVALID_FILE_ATTRIBUTES ||
                (final_attributes & (FILE_ATTRIBUTE_DIRECTORY |
                                     FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
                sha256_file(normalized_destination) != expected_sha256)
            {
                throw std::runtime_error(
                    std::string(description) + " failed final runtime validation");
            }
        }

        void copy_verified_executable(const LaunchPlan& plan)
        {
            const auto& identity = executable_identity(plan.executable_identity);
            copy_verified_file_to_runtime(
                plan,
                plan.source_exe,
                plan.staged_exe,
                identity.expected_sha256,
                "Staged executable");
        }

        void copy_runtime_support_files(const LaunchPlan& plan)
        {
            stage_runtime_language_selector(plan);
            for (const auto& file : runtime_support_files(plan))
            {
                if (file.destination.filename() == L"localization.txt")
                {
                    continue;
                }
                copy_verified_file_to_runtime(
                    plan,
                    file.source,
                    file.destination,
                    file.expected_sha256,
                    "Runtime root support file");
            }
        }

        void copy_runtime_handoff_files(const LaunchPlan& plan)
        {
            for (const auto& file : runtime_handoff_files(plan))
            {
                const auto source_hash = sha256_file(file.source);
                copy_verified_file_to_runtime(
                    plan,
                    file.source,
                    file.destination,
                    source_hash,
                    "Runtime multiplayer handoff file");
            }
        }

        void write_runtime_multiplayer_handoff_config(const LaunchPlan& plan)
        {
            const auto handoff_files = runtime_handoff_files(plan);
            if (handoff_files.empty())
            {
                return;
            }
            if (plan.executable_kind != ExecutableKind::sp ||
                handoff_files.size() != 2U ||
                handoff_files[0].destination.filename() != L"CoDWaWmp.exe")
            {
                throw std::logic_error(
                    "Multiplayer handoff configuration requires the exact SP stage");
            }

            validate_write_boundaries(plan);
            if (!is_normal_directory(plan.runtime_dir))
            {
                throw std::runtime_error(
                    "SP runtime must be an ordinary directory before writing handoff configuration");
            }
            const auto config = derive_multiplayer_handoff_config(plan);
            const auto contents = serialize_multiplayer_handoff_config(config);
            const auto destination = multiplayer_handoff_config_path(
                handoff_files[0].destination);
            if (destination.parent_path() != plan.runtime_dir.lexically_normal())
            {
                throw std::logic_error(
                    "Multiplayer handoff configuration escaped the validated SP runtime");
            }

            const DWORD existing_attributes =
                GetFileAttributesW(destination.c_str());
            if (existing_attributes != INVALID_FILE_ATTRIBUTES &&
                (existing_attributes & (FILE_ATTRIBUTE_DIRECTORY |
                                        FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
            {
                throw std::runtime_error(
                    "Existing multiplayer handoff configuration is unsafe");
            }

            fs::path temporary;
            UniqueHandle output;
            for (std::uint32_t attempt = 0; attempt < 100U; ++attempt)
            {
                temporary = destination;
                temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) +
                    L"-" + std::to_wstring(GetTickCount64()) + L"-" +
                    std::to_wstring(attempt);
                output = UniqueHandle(CreateFileW(
                    temporary.c_str(),
                    GENERIC_WRITE,
                    0,
                    nullptr,
                    CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr));
                if (output.valid())
                {
                    break;
                }
                if (GetLastError() != ERROR_FILE_EXISTS &&
                    GetLastError() != ERROR_ALREADY_EXISTS)
                {
                    throw_last_error(
                        "CreateFileW(multiplayer handoff temporary config)");
                }
            }
            if (!output.valid())
            {
                throw std::runtime_error(
                    "Could not allocate a unique multiplayer handoff temporary config");
            }

            bool moved = false;
            try
            {
                std::size_t written_total = 0;
                while (written_total < contents.size())
                {
                    DWORD written = 0;
                    const auto remaining = contents.size() - written_total;
                    const auto chunk = static_cast<DWORD>((std::min)(
                        remaining,
                        static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
                    if (!WriteFile(
                            output.get(),
                            contents.data() + written_total,
                            chunk,
                            &written,
                            nullptr) ||
                        written == 0)
                    {
                        throw_last_error(
                            "WriteFile(multiplayer handoff config)");
                    }
                    written_total += written;
                }
                if (!FlushFileBuffers(output.get()))
                {
                    throw_last_error(
                        "FlushFileBuffers(multiplayer handoff config)");
                }
                output = UniqueHandle{};
                if (!MoveFileExW(
                        temporary.c_str(),
                        destination.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    throw_last_error(
                        "MoveFileExW(multiplayer handoff config)");
                }
                moved = true;
            }
            catch (...)
            {
                output = UniqueHandle{};
                if (!moved)
                {
                    DeleteFileW(temporary.c_str());
                }
                throw;
            }

            if (!is_normal_file(destination) ||
                read_multiplayer_handoff_config(
                    handoff_files[0].destination) != config)
            {
                throw std::runtime_error(
                    "Multiplayer handoff configuration failed final validation");
            }
        }

        void copy_optional_pezbot_helper_to_runtime(
            const LaunchPlan& plan) noexcept
        {
            if (plan.executable_kind != ExecutableKind::sp ||
                !plan.launcher_executable ||
                plan.bot_policy == BotPolicy::disabled)
            {
                return;
            }

            try
            {
                const auto source =
                    plan.launcher_executable->parent_path() /
                    kPezBotImportHelperFilename;
                if (!is_verified_pezbot_helper(source))
                {
                    return;
                }
                copy_verified_file_to_runtime(
                    plan,
                    source,
                    plan.runtime_dir / kPezBotImportHelperFilename,
                    kExpectedPezBotImportHelperSha256,
                    "Optional PeZBOT import helper");
            }
            catch (...)
            {
                // PeZBOT is optional. A helper staging failure must not make
                // Zombies or the stock base multiplayer handoff unavailable.
            }
        }

        struct ProcedureLocation
        {
            std::wstring owner_module;
            std::uintptr_t rva = 0;
        };

        struct InjectionResult
        {
            ProcedureLocation procedure;
            ModulePollResult poll;
            std::uint32_t loaded_module = 0;
            bool prebootstrap_safe_mode_dismissed = false;
            bool prebootstrap_optimal_settings_dismissed = false;
            std::uint32_t prebootstrap_prompt_probes = 0;
        };

        enum class RemoteSpRendererState
        {
            waiting_for_unwrap,
            ready,
            already_initialized,
            read_failed,
        };

        struct RemoteSpRendererProbe
        {
            RemoteSpRendererState state =
                RemoteSpRendererState::waiting_for_unwrap;
            DWORD system_error = ERROR_SUCCESS;
            std::uint32_t factory = 0;
            std::uint32_t device = 0;
        };

        RemoteSpRendererProbe probe_remote_sp_renderer_bootstrap(
            const HANDLE process,
            const std::uintptr_t image_base)
        {
            constexpr std::uintptr_t kFactoryCallRva = 0x002D62BB;
            constexpr std::uintptr_t kCreateDeviceDispatchRva = 0x002D6056;
            constexpr std::uintptr_t kFactoryPointerRva = 0x037F3B04;
            constexpr std::uintptr_t kDevicePointerRva = 0x037F3B08;
            constexpr std::array<std::uint8_t, 5> kFactoryCall{
                0xE8, 0xE8, 0x46, 0x08, 0x00};
            constexpr std::array<std::uint8_t, 6> kCreateDeviceDispatch{
                0x50, 0x8B, 0x41, 0x40, 0xFF, 0xD0};

            RemoteSpRendererProbe probe;
            std::array<std::uint8_t, kFactoryCall.size()> factory_call{};
            std::array<std::uint8_t, kCreateDeviceDispatch.size()>
                create_device_dispatch{};
            SIZE_T bytes_read = 0;
            const auto read_exact =
                [&](const std::uintptr_t address,
                    void* const destination,
                    const std::size_t size) {
                    bytes_read = 0;
                    return ReadProcessMemory(
                               process,
                               reinterpret_cast<const void*>(address),
                               destination,
                               size,
                               &bytes_read) != FALSE &&
                           bytes_read == size;
                };
            if (!read_exact(
                    image_base + kFactoryCallRva,
                    factory_call.data(), factory_call.size()) ||
                !read_exact(
                    image_base + kCreateDeviceDispatchRva,
                    create_device_dispatch.data(),
                    create_device_dispatch.size()))
            {
                probe.state = RemoteSpRendererState::read_failed;
                probe.system_error = GetLastError();
                return probe;
            }

            if (factory_call != kFactoryCall ||
                create_device_dispatch != kCreateDeviceDispatch)
            {
                probe.state = RemoteSpRendererState::waiting_for_unwrap;
                return probe;
            }

#if defined(WAWVR_HAS_T4_PROFILE)
            // SteamStub restores the executable in regions. Renderer entry
            // bytes can be ready while input/weapon/frame-boundary code is
            // still wrapped. Require every centrally profiled mapped-code
            // sentinel before suspending the primary thread, then re-run this
            // same proof after suspension immediately before injection.
            const auto& profile =
                wawvr::t4::t4_steam_sp_1_7_1263_profile();
            std::array<
                std::uint8_t,
                wawvr::t4::HookSite::kMaximumExpectedBytes> mapped_bytes{};
            for (const wawvr::t4::HookSite& site : profile.sites)
            {
                if (site.expected_size == 0)
                {
                    continue;
                }
                if (!read_exact(
                        image_base + site.rva,
                        mapped_bytes.data(), site.expected_size))
                {
                    probe.state = RemoteSpRendererState::read_failed;
                    probe.system_error = GetLastError();
                    return probe;
                }
                if (!std::equal(
                        site.expected.begin(),
                        site.expected.begin() + site.expected_size,
                        mapped_bytes.begin()))
                {
                    probe.state =
                        RemoteSpRendererState::waiting_for_unwrap;
                    return probe;
                }
            }
#endif

            if (!read_exact(
                    image_base + kFactoryPointerRva,
                    &probe.factory, sizeof(probe.factory)) ||
                !read_exact(
                    image_base + kDevicePointerRva,
                    &probe.device, sizeof(probe.device)))
            {
                probe.state = RemoteSpRendererState::read_failed;
                probe.system_error = GetLastError();
                return probe;
            }
            probe.state = probe.factory == 0 && probe.device == 0
                ? RemoteSpRendererState::ready
                : RemoteSpRendererState::already_initialized;
            return probe;
        }

        [[nodiscard]] bool is_readable_remote_page(
            const DWORD protection) noexcept
        {
            if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            {
                return false;
            }
            switch (protection & 0xFFU)
            {
            case PAGE_READONLY:
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE_READ:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                return true;
            default:
                return false;
            }
        }

        void read_remote_exact(
            const HANDLE process,
            const std::uintptr_t address,
            void* const destination,
            const std::size_t size,
            const char* const operation)
        {
            SIZE_T bytes_read = 0;
            const BOOL read = ReadProcessMemory(
                process,
                reinterpret_cast<const void*>(address),
                destination,
                size,
                &bytes_read);
            if (!read || bytes_read != size)
            {
                const DWORD error = read ? ERROR_PARTIAL_COPY : GetLastError();
                throw std::system_error(
                    static_cast<int>(error), std::system_category(), operation);
            }
        }

        void prove_remote_sp_renderer_thread_is_quiescent(
            const HANDLE process,
            const HANDLE primary_thread,
            const std::uintptr_t image_base)
        {
#if defined(_M_IX86) || defined(__i386__)
            if (image_base >
                static_cast<std::uintptr_t>(
                    (std::numeric_limits<std::uint32_t>::max)() -
                    kSpRendererCriticalRoutineRvas.back().second))
            {
                throw std::runtime_error(
                    "The SP image base cannot be represented by the x86 renderer proof");
            }

            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(primary_thread, &context))
            {
                throw_last_error(
                    "GetThreadContext(SP renderer bootstrap proof)");
            }

            const auto base32 = static_cast<std::uint32_t>(image_base);
            const auto instruction_status =
                classify_sp_renderer_thread_snapshot(
                    base32,
                    context.Eip,
                    std::span<const std::uint32_t>{});
            if (instruction_status != SpRendererThreadSnapshotStatus::safe)
            {
                throw std::runtime_error(
                    "The suspended SP primary thread is executing inside a renderer bootstrap critical routine");
            }

            struct RemoteThreadBasicInformation32
            {
                LONG exit_status = 0;
                std::uint32_t teb_base_address = 0;
                std::uint32_t unique_process = 0;
                std::uint32_t unique_thread = 0;
                std::uint32_t affinity_mask = 0;
                LONG priority = 0;
                LONG base_priority = 0;
            };
            static_assert(sizeof(RemoteThreadBasicInformation32) == 28);

            struct RemoteNtTibPrefix32
            {
                std::uint32_t exception_list = 0;
                std::uint32_t stack_base = 0;
                std::uint32_t stack_limit = 0;
            };
            static_assert(sizeof(RemoteNtTibPrefix32) == 12);

            using NtQueryInformationThreadFn = LONG(NTAPI*)(
                HANDLE, LONG, void*, ULONG, ULONG*);
            const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            if (!ntdll)
            {
                throw_last_error(
                    "GetModuleHandleW(ntdll.dll) for SP stack proof");
            }
            const auto query_thread =
                reinterpret_cast<NtQueryInformationThreadFn>(
                    GetProcAddress(ntdll, "NtQueryInformationThread"));
            if (!query_thread)
            {
                throw_last_error(
                    "GetProcAddress(NtQueryInformationThread) for SP stack proof");
            }

            RemoteThreadBasicInformation32 thread_information{};
            ULONG returned_bytes = 0;
            constexpr LONG kThreadBasicInformation = 0;
            const LONG query_status = query_thread(
                primary_thread,
                kThreadBasicInformation,
                &thread_information,
                static_cast<ULONG>(sizeof(thread_information)),
                &returned_bytes);
            if (query_status < 0 ||
                returned_bytes < sizeof(thread_information) ||
                thread_information.teb_base_address == 0)
            {
                std::ostringstream message;
                message << "NtQueryInformationThread could not prove the x86 SP stack bounds (status 0x"
                        << std::hex
                        << static_cast<std::uint32_t>(query_status)
                        << ", bytes " << std::dec << returned_bytes << ')';
                throw std::runtime_error(message.str());
            }

            RemoteNtTibPrefix32 tib{};
            read_remote_exact(
                process,
                thread_information.teb_base_address,
                &tib,
                sizeof(tib),
                "ReadProcessMemory(SP NT_TIB stack bounds)");

            const std::uint32_t stack_pointer = context.Esp;
            if (tib.stack_limit == 0 || tib.stack_base <= tib.stack_limit ||
                stack_pointer < tib.stack_limit ||
                stack_pointer > tib.stack_base ||
                (stack_pointer & 3U) != 0 || (tib.stack_base & 3U) != 0)
            {
                throw std::runtime_error(
                    "The suspended SP primary thread reported invalid or unaligned x86 stack bounds");
            }

            const std::size_t active_stack_bytes =
                static_cast<std::size_t>(tib.stack_base - stack_pointer);
            if (active_stack_bytes > kMaximumSpRendererStackProofBytes)
            {
                throw std::runtime_error(
                    "The suspended SP primary thread stack exceeds the bounded renderer proof limit");
            }
            if ((active_stack_bytes & 3U) != 0)
            {
                throw std::runtime_error(
                    "The suspended SP primary thread stack cannot be scanned as exact x86 words");
            }

            std::array<std::uint32_t, 4096> stack_words{};
            std::uintptr_t cursor = stack_pointer;
            const std::uintptr_t stack_base = tib.stack_base;
            while (cursor < stack_base)
            {
                MEMORY_BASIC_INFORMATION memory{};
                if (VirtualQueryEx(
                        process,
                        reinterpret_cast<const void*>(cursor),
                        &memory,
                        sizeof(memory)) != sizeof(memory))
                {
                    throw_last_error(
                        "VirtualQueryEx(SP renderer stack proof)");
                }

                const auto region_begin =
                    reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
                if (memory.RegionSize == 0 ||
                    region_begin >
                        (std::numeric_limits<std::uintptr_t>::max)() -
                            static_cast<std::uintptr_t>(memory.RegionSize))
                {
                    throw std::runtime_error(
                        "The SP renderer stack proof encountered invalid virtual-memory bounds");
                }
                const std::uintptr_t region_end =
                    region_begin +
                    static_cast<std::uintptr_t>(memory.RegionSize);
                if (cursor < region_begin || cursor >= region_end ||
                    memory.State != MEM_COMMIT ||
                    !is_readable_remote_page(memory.Protect))
                {
                    throw std::runtime_error(
                        "The SP renderer stack proof encountered an uncommitted or unreadable page");
                }

                const auto readable_end = (std::min)(region_end, stack_base);
                while (cursor < readable_end)
                {
                    const auto remaining = readable_end - cursor;
                    const auto chunk_bytes = static_cast<std::size_t>((std::min)(
                        remaining,
                        static_cast<std::uintptr_t>(sizeof(stack_words))));
                    if (chunk_bytes == 0 || (chunk_bytes & 3U) != 0)
                    {
                        throw std::runtime_error(
                            "The SP renderer stack proof encountered a partial x86 stack word");
                    }
                    read_remote_exact(
                        process,
                        cursor,
                        stack_words.data(),
                        chunk_bytes,
                        "ReadProcessMemory(SP renderer stack proof)");
                    const auto stack_status =
                        classify_sp_renderer_thread_snapshot(
                            base32,
                            context.Eip,
                            std::span<const std::uint32_t>(
                                stack_words.data(), chunk_bytes / sizeof(std::uint32_t)));
                    if (stack_status == SpRendererThreadSnapshotStatus::
                            critical_return_address_on_stack)
                    {
                        throw std::runtime_error(
                            "The suspended SP primary thread stack contains an active renderer bootstrap return address");
                    }
                    if (stack_status != SpRendererThreadSnapshotStatus::safe)
                    {
                        throw std::runtime_error(
                            "The suspended SP primary thread failed the renderer bootstrap instruction-pointer proof");
                    }
                    cursor += chunk_bytes;
                }
            }
#else
            static_cast<void>(process);
            static_cast<void>(primary_thread);
            static_cast<void>(image_base);
            throw std::runtime_error(
                "The deterministic SP renderer proof requires a Win32/x86 launcher");
#endif
        }

        ProcedureLocation resolve_local_load_library()
        {
            const auto kernel32 = GetModuleHandleW(L"kernel32.dll");
            if (!kernel32)
            {
                throw_last_error("GetModuleHandleW(kernel32.dll)");
            }

            const auto load_library = GetProcAddress(kernel32, "LoadLibraryW");
            if (!load_library)
            {
                throw_last_error("GetProcAddress(LoadLibraryW)");
            }

            // On current Windows versions the kernel32 export is commonly
            // forwarded into KernelBase. Resolve the module that actually owns
            // the returned address instead of assuming a kernel32-relative RVA.
            HMODULE owner = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(load_library),
                    &owner))
            {
                throw_last_error("GetModuleHandleExW(LoadLibraryW owner)");
            }

            std::array<wchar_t, MAX_PATH> owner_path{};
            const auto length = GetModuleFileNameW(
                owner,
                owner_path.data(),
                static_cast<DWORD>(owner_path.size()));
            if (length == 0 || length >= owner_path.size())
            {
                throw_last_error("GetModuleFileNameW(LoadLibraryW owner)");
            }

            const auto procedure_address = reinterpret_cast<std::uintptr_t>(load_library);
            const auto module_address = reinterpret_cast<std::uintptr_t>(owner);
            if (procedure_address < module_address)
            {
                throw std::runtime_error("LoadLibraryW address is outside its owner module");
            }

            return {
                fs::path(std::wstring(owner_path.data(), length)).filename().wstring(),
                procedure_address - module_address,
            };
        }

        std::optional<std::uintptr_t> probe_remote_module_once(
            const std::uint32_t process_id,
            const std::wstring& module_name,
            DWORD& last_error,
            std::uint32_t* const module_size = nullptr)
        {
            UniqueHandle snapshot(CreateToolhelp32Snapshot(
                TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                process_id));
            if (!snapshot.valid())
            {
                last_error = GetLastError();
                return std::nullopt;
            }

            MODULEENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (!Module32FirstW(snapshot.get(), &entry))
            {
                last_error = GetLastError();
                return std::nullopt;
            }

            do
            {
                if (_wcsicmp(entry.szModule, module_name.c_str()) == 0)
                {
                    last_error = ERROR_SUCCESS;
                    if (module_size != nullptr)
                    {
                        *module_size = entry.modBaseSize;
                    }
                    return reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
                }
            } while (Module32NextW(snapshot.get(), &entry));

            last_error = ERROR_MOD_NOT_FOUND;
            return std::nullopt;
        }

        RemoteAllocation write_remote_dll_path(
            const HANDLE process,
            const fs::path& dll_path)
        {
            const auto absolute_dll = fs::absolute(dll_path).lexically_normal().wstring();
            const auto byte_count = (absolute_dll.size() + 1) * sizeof(wchar_t);
            auto* remote_path = VirtualAllocEx(
                process,
                nullptr,
                byte_count,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE);
            if (!remote_path)
            {
                throw_last_error("VirtualAllocEx");
            }
            RemoteAllocation allocation(process, remote_path);

            SIZE_T written = 0;
            if (!WriteProcessMemory(
                    process,
                    remote_path,
                    absolute_dll.c_str(),
                    byte_count,
                    &written) ||
                written != byte_count)
            {
                throw_last_error("WriteProcessMemory");
            }
            return allocation;
        }

        [[nodiscard]] std::wstring bootstrap_event_name(
            const std::uint32_t process_id,
            const bool ready)
        {
            return std::wstring(
                       ready ? L"Local\\WaWVR-BootstrapReady-"
                             : L"Local\\WaWVR-BootstrapFailed-") +
                   std::to_wstring(process_id);
        }

        bool dismiss_spawned_startup_prompt_once(
            std::uint32_t process_id,
            bool* safe_mode_dismissed,
            bool* optimal_settings_dismissed);

        InjectionResult inject_prepared_dll_after_resume(
            const HANDLE process,
            const std::uint32_t process_id,
            const HANDLE primary_thread,
            const HANDLE bootstrap_ready_event,
            const HANDLE bootstrap_failed_event,
            void* remote_path,
            const std::wstring& target_module_name,
            const bool require_sp_renderer_bootstrap)
        {
            if constexpr (sizeof(void*) != 4)
            {
                throw std::runtime_error(
                    "DLL injection requires the launcher to be built as Win32/x86");
            }

            const auto procedure = resolve_local_load_library();
            DWORD last_module_error = ERROR_SUCCESS;
            DWORD process_exit_code = STILL_ACTIVE;
            DWORD process_probe_error = ERROR_SUCCESS;
            DWORD last_renderer_error = ERROR_SUCCESS;
            std::optional<std::uintptr_t> observed_main_module;
            std::uint32_t observed_main_module_size = 0;
            RemoteSpRendererProbe renderer_probe;

            const auto poll = poll_for_stable_module(
                [&]() {
                    const auto owner_module = probe_remote_module_once(
                        process_id,
                        procedure.owner_module,
                        last_module_error);
                    if (!owner_module || !require_sp_renderer_bootstrap)
                    {
                        if (!owner_module)
                        {
                            return owner_module;
                        }
                    }
                    observed_main_module = probe_remote_module_once(
                        process_id,
                        target_module_name,
                        last_module_error,
                        &observed_main_module_size);
                    if (!observed_main_module)
                    {
                        return std::optional<std::uintptr_t>{};
                    }
                    if (!require_sp_renderer_bootstrap)
                    {
                        return owner_module;
                    }
                    renderer_probe = probe_remote_sp_renderer_bootstrap(
                        process, *observed_main_module);
                    if (renderer_probe.state ==
                        RemoteSpRendererState::read_failed)
                    {
                        last_renderer_error = renderer_probe.system_error;
                        return std::optional<std::uintptr_t>{};
                    }
                    if (renderer_probe.state == RemoteSpRendererState::ready ||
                        renderer_probe.state ==
                            RemoteSpRendererState::already_initialized)
                    {
                        return owner_module;
                    }
                    return std::optional<std::uintptr_t>{};
                },
                [&]() {
                    if (!GetExitCodeProcess(process, &process_exit_code))
                    {
                        process_probe_error = GetLastError();
                        return false;
                    }
                    return process_exit_code == STILL_ACTIVE;
                },
                [](const std::uint32_t milliseconds) { Sleep(milliseconds); },
                10'000,
                10,
                // Require a stable full SP mapped-code manifest, rather than
                // accepting the first renderer-only observation while
                // SteamStub may still be restoring another code region.
                3u);

            if (poll.status == ModulePollStatus::process_exited)
            {
                if (process_probe_error != ERROR_SUCCESS)
                {
                    throw std::system_error(
                        static_cast<int>(process_probe_error),
                        std::system_category(),
                        "GetExitCodeProcess while waiting for target loader");
                }
                throw std::runtime_error(
                    "Target exited before DLL injection; exit code " +
                    std::to_string(process_exit_code));
            }
            if (poll.status != ModulePollStatus::found || !poll.module_base)
            {
                std::ostringstream message;
                message << "Timed out after " << poll.elapsed_ms << " ms and "
                        << poll.attempts << " probes waiting for "
                        << fs::path(procedure.owner_module).string()
                        << " in the resumed target process; last Toolhelp error "
                        << last_module_error;
                if (require_sp_renderer_bootstrap)
                {
                    message << "; the exact SP renderer image did not become "
                               "patchable before its 10000 ms deadline";
                    if (last_renderer_error != ERROR_SUCCESS)
                    {
                        message << " (last ReadProcessMemory error "
                                << last_renderer_error << ')';
                    }
                }
                throw std::runtime_error(message.str());
            }

            const auto target_load_library = *poll.module_base + procedure.rva;

            bool primary_suspended = false;
            const auto resume_primary = [&]() {
                if (!primary_suspended)
                {
                    return;
                }
                if (ResumeThread(primary_thread) ==
                    std::numeric_limits<DWORD>::max())
                {
                    throw_last_error("ResumeThread after deterministic DLL bootstrap");
                }
                primary_suspended = false;
            };

            // A restored executable image does not prove that the process
            // loader has finished calling startup DLLs. Suspending the primary
            // thread while it owns the loader lock deadlocks the subsequent
            // remote LoadLibraryW. Sample only while suspended and proceed once
            // the primary instruction pointer has reached the target image;
            // Windows has released process-initialization serialization before
            // transferring control to the executable entry path.
            if (!observed_main_module || observed_main_module_size == 0)
            {
                throw std::runtime_error(
                    "The target main module was not fully observed before DLL bootstrap");
            }
            constexpr ULONGLONG kLoaderQuiesceTimeoutMilliseconds = 10'000;
            const ULONGLONG loader_quiesce_deadline =
                GetTickCount64() + kLoaderQuiesceTimeoutMilliseconds;
            ULONGLONG next_startup_prompt_probe = GetTickCount64();
            std::uint32_t last_primary_instruction = 0;
            bool prebootstrap_safe_mode_dismissed = false;
            bool prebootstrap_optimal_settings_dismissed = false;
            std::uint32_t prebootstrap_prompt_probes = 0;
            for (;;)
            {
                const ULONGLONG now = GetTickCount64();
                if (now >= next_startup_prompt_probe)
                {
                    ++prebootstrap_prompt_probes;
                    dismiss_spawned_startup_prompt_once(
                        process_id,
                        &prebootstrap_safe_mode_dismissed,
                        &prebootstrap_optimal_settings_dismissed);
                    next_startup_prompt_probe = now + 25;
                }

                const DWORD previous_suspend_count = SuspendThread(primary_thread);
                if (previous_suspend_count == std::numeric_limits<DWORD>::max())
                {
                    throw_last_error(
                        "SuspendThread before deterministic DLL bootstrap");
                }
                if (previous_suspend_count != 0)
                {
                    static_cast<void>(ResumeThread(primary_thread));
                    throw std::runtime_error(
                        "Target primary thread had an unexpected suspend count before DLL bootstrap");
                }
                primary_suspended = true;

#if defined(_M_IX86) || defined(__i386__)
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;
                if (!GetThreadContext(primary_thread, &context))
                {
                    resume_primary();
                    throw_last_error(
                        "GetThreadContext before deterministic DLL bootstrap");
                }
                last_primary_instruction = context.Eip;
                const auto instruction =
                    static_cast<std::uintptr_t>(context.Eip);
                const bool inside_target_image =
                    instruction >= *observed_main_module &&
                    instruction - *observed_main_module <
                        observed_main_module_size;
                if (inside_target_image)
                {
                    break;
                }
#else
                resume_primary();
                throw std::runtime_error(
                    "The deterministic loader proof requires a Win32/x86 launcher");
#endif

                resume_primary();
                if (GetTickCount64() >= loader_quiesce_deadline)
                {
                    std::ostringstream message;
                    message << "Timed out after "
                            << kLoaderQuiesceTimeoutMilliseconds
                            << " ms waiting for the target primary thread to leave process-loader initialization; last EIP 0x"
                            << std::hex << std::uppercase
                            << last_primary_instruction;
                    throw std::runtime_error(message.str());
                }
                Sleep(1);
            }

            if (require_sp_renderer_bootstrap)
            {
                if (!observed_main_module)
                {
                    throw std::runtime_error(
                        "The exact SP main module disappeared before renderer bootstrap suspension");
                }

                // Zero globals alone cannot distinguish a genuinely early
                // thread from one currently executing the factory/device
                // path. Prove both EIP and every readable active x86 stack
                // word while the primary is suspended, then re-read the
                // sentinels immediately before injecting the bootstrap DLL.
                prove_remote_sp_renderer_thread_is_quiescent(
                    process, primary_thread, *observed_main_module);
                renderer_probe = probe_remote_sp_renderer_bootstrap(
                    process, *observed_main_module);
                if (renderer_probe.state != RemoteSpRendererState::ready)
                {
                    if (renderer_probe.state ==
                        RemoteSpRendererState::already_initialized)
                    {
                        throw std::runtime_error(
                            "The SP renderer initialized before the deterministic VR bootstrap gate");
                    }
                    throw std::runtime_error(
                        "The exact SP renderer image changed before deterministic VR bootstrap suspension");
                }
            }

            UniqueHandle thread(CreateRemoteThread(
                process,
                nullptr,
                0,
                reinterpret_cast<LPTHREAD_START_ROUTINE>(target_load_library),
                remote_path,
                0,
                nullptr));
            if (!thread.valid())
            {
                throw_last_error("CreateRemoteThread");
            }

            const auto wait = WaitForSingleObject(thread.get(), 30'000);
            if (wait == WAIT_TIMEOUT)
            {
                throw std::runtime_error(
                    "Timed out after 30000 ms waiting for remote LoadLibraryW");
            }
            if (wait != WAIT_OBJECT_0)
            {
                throw_last_error("WaitForSingleObject(remote LoadLibraryW)");
            }

            DWORD module_handle = 0;
            if (!GetExitCodeThread(thread.get(), &module_handle))
            {
                throw_last_error("GetExitCodeThread(remote LoadLibraryW)");
            }
            if (module_handle == 0)
            {
                throw std::runtime_error(
                    "LoadLibraryW rejected the mod DLL in the target process");
            }

            const std::array<HANDLE, 2> bootstrap_events{
                bootstrap_ready_event,
                bootstrap_failed_event,
            };
            const DWORD bootstrap_wait = WaitForMultipleObjects(
                static_cast<DWORD>(bootstrap_events.size()),
                bootstrap_events.data(),
                FALSE,
                30'000);
            if (bootstrap_wait == WAIT_TIMEOUT)
            {
                throw std::runtime_error(
                    "Timed out after 30000 ms waiting for the injected VR bootstrap handshake");
            }
            if (bootstrap_wait == WAIT_OBJECT_0 + 1)
            {
                throw std::runtime_error(
                    "The injected VR bootstrap rejected the executable or early renderer patch");
            }
            if (bootstrap_wait != WAIT_OBJECT_0)
            {
                throw_last_error(
                    "WaitForMultipleObjects(VR bootstrap handshake)");
            }

            // This is the only success path that makes the primary runnable.
            // Any exception leaves it suspended for launch_game's owned-process
            // failure path to terminate without a rejected/partial bootstrap
            // executing.
            resume_primary();
            return {
                procedure,
                poll,
                module_handle,
                prebootstrap_safe_mode_dismissed,
                prebootstrap_optimal_settings_dismissed,
                prebootstrap_prompt_probes,
            };
        }

        std::wstring window_text(const HWND window)
        {
            const auto length = GetWindowTextLengthW(window);
            if (length <= 0)
            {
                return {};
            }
            std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
            const auto copied = GetWindowTextW(
                window,
                result.data(),
                static_cast<int>(result.size()));
            if (copied <= 0)
            {
                return {};
            }
            result.resize(static_cast<std::size_t>(copied));
            return result;
        }

        std::wstring window_class_name(const HWND window)
        {
            std::array<wchar_t, 256> name{};
            const auto copied = GetClassNameW(
                window,
                name.data(),
                static_cast<int>(name.size()));
            return copied > 0 ? std::wstring(name.data(), static_cast<std::size_t>(copied))
                              : std::wstring{};
        }

        BOOL CALLBACK collect_static_text(const HWND child, const LPARAM parameter)
        {
            if (window_class_name(child) == L"Static")
            {
                auto& body = *reinterpret_cast<std::wstring*>(parameter);
                const auto text = window_text(child);
                if (!text.empty())
                {
                    if (!body.empty())
                    {
                        body += L'\n';
                    }
                    body += text;
                }
            }
            return TRUE;
        }

        enum class StartupPromptKind
        {
            none,
            safe_mode,
            optimal_settings,
        };

        struct StartupPromptWindowSearch
        {
            std::uint32_t process_id = 0;
            HWND dialog = nullptr;
            HWND no_button = nullptr;
            StartupPromptKind kind = StartupPromptKind::none;
        };

        BOOL CALLBACK find_startup_prompt_window(const HWND window, const LPARAM parameter)
        {
            auto& search = *reinterpret_cast<StartupPromptWindowSearch*>(parameter);
            DWORD owner_process_id = 0;
            GetWindowThreadProcessId(window, &owner_process_id);
            if (owner_process_id != search.process_id || !IsWindowVisible(window))
            {
                return TRUE;
            }

            const auto no_button = GetDlgItem(window, IDNO);
            if (!no_button)
            {
                return TRUE;
            }

            std::wstring body;
            EnumChildWindows(window, collect_static_text, reinterpret_cast<LPARAM>(&body));
            const SafeModeDialogIdentity identity{
                owner_process_id,
                window_class_name(window),
                window_text(window),
                std::move(body),
                window_class_name(no_button),
                GetDlgCtrlID(no_button),
            };
            if (is_exact_waw_safe_mode_dialog(identity, search.process_id))
            {
                search.dialog = window;
                search.no_button = no_button;
                search.kind = StartupPromptKind::safe_mode;
                return FALSE;
            }
            if (is_exact_waw_optimal_settings_dialog(identity, search.process_id))
            {
                search.dialog = window;
                search.no_button = no_button;
                search.kind = StartupPromptKind::optimal_settings;
                return FALSE;
            }
            return TRUE;
        }

        bool dismiss_spawned_startup_prompt_once(
            const std::uint32_t process_id,
            bool* const safe_mode_dismissed,
            bool* const optimal_settings_dismissed)
        {
            StartupPromptWindowSearch search{process_id};
            SetLastError(ERROR_SUCCESS);
            if (!EnumWindows(
                    find_startup_prompt_window,
                    reinterpret_cast<LPARAM>(&search)))
            {
                const auto enum_error = GetLastError();
                // The callback deliberately returns FALSE once it finds the
                // exact dialog; EnumWindows does not set last-error then.
                if (!search.dialog && enum_error != ERROR_SUCCESS)
                {
                    SetLastError(enum_error);
                    throw_last_error("EnumWindows(startup prompts)");
                }
            }
            if (!search.dialog)
            {
                return false;
            }

            DWORD_PTR ignored = 0;
            if (!SendMessageTimeoutW(
                    search.no_button,
                    BM_CLICK,
                    0,
                    0,
                    SMTO_ABORTIFHUNG | SMTO_BLOCK,
                    1'000,
                    &ignored))
            {
                throw_last_error("SendMessageTimeoutW(startup prompt No)");
            }
            if (search.kind == StartupPromptKind::safe_mode &&
                safe_mode_dismissed != nullptr)
            {
                *safe_mode_dismissed = true;
            }
            else if (search.kind == StartupPromptKind::optimal_settings &&
                     optimal_settings_dismissed != nullptr)
            {
                *optimal_settings_dismissed = true;
            }
            return true;
        }

        struct StartupPromptDismissResult
        {
            bool safe_mode_dismissed = false;
            bool optimal_settings_dismissed = false;
            bool process_exited = false;
            std::uint32_t probes = 0;
            std::uint32_t elapsed_ms = 0;
        };

        StartupPromptDismissResult dismiss_spawned_startup_prompts(
            const HANDLE process,
            const std::uint32_t process_id,
            const std::uint32_t timeout_ms = 2'500,
            const std::uint32_t interval_ms = 25,
            const std::uint32_t quiet_after_dismiss_ms = 750)
        {
            StartupPromptDismissResult result;
            std::optional<std::uint32_t> last_dismissed_at;
            for (;;)
            {
                DWORD exit_code = 0;
                if (!GetExitCodeProcess(process, &exit_code))
                {
                    throw_last_error("GetExitCodeProcess(startup prompts)");
                }
                if (exit_code != STILL_ACTIVE)
                {
                    result.process_exited = true;
                    return result;
                }

                ++result.probes;
                bool safe_mode_dismissed = false;
                bool optimal_settings_dismissed = false;
                if (dismiss_spawned_startup_prompt_once(
                        process_id,
                        &safe_mode_dismissed,
                        &optimal_settings_dismissed))
                {
                    result.safe_mode_dismissed =
                        result.safe_mode_dismissed || safe_mode_dismissed;
                    result.optimal_settings_dismissed =
                        result.optimal_settings_dismissed ||
                        optimal_settings_dismissed;
                    last_dismissed_at = result.elapsed_ms;
                    if (result.safe_mode_dismissed && result.optimal_settings_dismissed)
                    {
                        return result;
                    }
                }

                const bool quiet_after_dismiss =
                    last_dismissed_at.has_value() &&
                    result.elapsed_ms - *last_dismissed_at >= quiet_after_dismiss_ms;
                if (result.elapsed_ms >= timeout_ms || quiet_after_dismiss)
                {
                    return result;
                }
                const auto delay = (std::min)(interval_ms, timeout_ms - result.elapsed_ms);
                Sleep(delay);
                result.elapsed_ms += delay;
            }
        }
    }

    bool PeInfo::is_x86_pe32() const noexcept
    {
        return machine == kImageFileMachineI386 && optional_magic == kPe32Magic;
    }

    bool PeInfo::is_dll() const noexcept
    {
        return (characteristics & kImageFileDll) != 0;
    }

    bool DiagnosticReport::passed() const noexcept
    {
        return std::all_of(checks.begin(), checks.end(), [](const DiagnosticCheck& check) {
            return !check.required || check.passed;
        });
    }

    SpRendererThreadSnapshotStatus classify_sp_renderer_thread_snapshot(
        const std::uint32_t image_base,
        const std::uint32_t instruction_pointer,
        const std::span<const std::uint32_t> stack_words) noexcept
    {
        const auto absolute = [image_base](const std::uint32_t rva) {
            return static_cast<std::uint64_t>(image_base) + rva;
        };
        const auto current = static_cast<std::uint64_t>(instruction_pointer);
        for (const auto& [begin_rva, end_rva] :
             kSpRendererCriticalRoutineRvas)
        {
            if (current >= absolute(begin_rva) && current < absolute(end_rva))
            {
                return SpRendererThreadSnapshotStatus::
                    instruction_pointer_in_critical_routine;
            }
        }

        for (const auto word : stack_words)
        {
            for (const auto return_rva : kSpRendererCriticalReturnRvas)
            {
                if (static_cast<std::uint64_t>(word) == absolute(return_rva))
                {
                    return SpRendererThreadSnapshotStatus::
                        critical_return_address_on_stack;
                }
            }
        }
        return SpRendererThreadSnapshotStatus::safe;
    }

    ModulePollResult poll_for_stable_module(
        const ModuleProbe& probe,
        const ProcessAliveProbe& process_alive,
        const PollDelay& delay,
        const std::uint32_t timeout_ms,
        const std::uint32_t interval_ms,
        const std::uint32_t required_stable_observations)
    {
        if (!probe || !process_alive || !delay)
        {
            throw std::invalid_argument("Module poll callbacks must be provided");
        }
        if (interval_ms == 0 || required_stable_observations == 0)
        {
            throw std::invalid_argument(
                "Module poll interval and stable-observation count must be non-zero");
        }

        ModulePollResult result;
        std::optional<std::uintptr_t> last_candidate;
        std::uint32_t stable_observations = 0;

        for (;;)
        {
            if (!process_alive())
            {
                result.status = ModulePollStatus::process_exited;
                return result;
            }

            ++result.attempts;
            const auto candidate = probe();
            if (candidate)
            {
                if (candidate == last_candidate)
                {
                    ++stable_observations;
                }
                else
                {
                    last_candidate = candidate;
                    stable_observations = 1;
                }

                if (stable_observations >= required_stable_observations)
                {
                    result.status = ModulePollStatus::found;
                    result.module_base = candidate;
                    return result;
                }
            }
            else
            {
                last_candidate.reset();
                stable_observations = 0;
            }

            if (result.elapsed_ms >= timeout_ms)
            {
                result.status = ModulePollStatus::timed_out;
                return result;
            }

            const auto remaining = timeout_ms - result.elapsed_ms;
            const auto next_delay = (std::min)(interval_ms, remaining);
            delay(next_delay);
            result.elapsed_ms += next_delay;
        }
    }

    Options parse_options(const int argc, wchar_t** argv)
    {
        Options options;
        bool selected_mode = false;
        bool selected_launch_target = false;
        bool selected_bot_policy = false;

        const auto require_value = [&](const int index, const wchar_t* option) -> fs::path {
            if (index + 1 >= argc)
            {
                static_cast<void>(option);
                throw std::invalid_argument("Missing value for command-line path option");
            }
            return argv[index + 1];
        };
        const auto require_text = [&](const int index) -> std::wstring_view {
            if (index + 1 >= argc)
            {
                throw std::invalid_argument("Missing value for command-line option");
            }
            return argv[index + 1];
        };

        const auto set_mode = [&](const Mode mode) {
            if (selected_mode)
            {
                throw std::invalid_argument(
                    "Choose only one of --diagnose, --prepare, or --launch");
            }
            options.mode = mode;
            selected_mode = true;
        };
        const auto set_launch_target = [&](const LaunchTarget launch_target) {
            if (selected_launch_target)
            {
                throw std::invalid_argument(
                    "Choose only one launch target");
            }
            options.launch_target = launch_target;
            selected_launch_target = true;
        };

        for (int index = 1; index < argc; ++index)
        {
            const std::wstring argument = argv[index];
            if (argument == L"--help" || argument == L"-h")
            {
                options.show_help = true;
            }
            else if (argument == L"--diagnose")
            {
                set_mode(Mode::diagnose);
            }
            else if (argument == L"--prepare")
            {
                set_mode(Mode::prepare);
            }
            else if (argument == L"--launch")
            {
                set_mode(Mode::launch);
            }
            else if (argument == L"--menu")
            {
                set_launch_target(LaunchTarget::frontend_menu);
            }
            else if (argument == L"--der-riese")
            {
                set_launch_target(LaunchTarget::der_riese);
            }
            else if (argument == L"--campaign-ber1")
            {
                set_launch_target(LaunchTarget::campaign_ber1);
            }
            else if (argument == L"--campaign-pel2-airfield")
            {
                set_launch_target(LaunchTarget::campaign_pel2_airfield);
            }
            else if (argument == L"--multiplayer")
            {
                set_launch_target(LaunchTarget::offline_multiplayer);
            }
            else if (argument == L"--wait")
            {
                options.wait_for_exit = true;
            }
            else if (argument == L"--game-dir")
            {
                options.game_dir = require_value(index, L"--game-dir");
                ++index;
            }
            else if (argument == L"--source-exe")
            {
                options.source_exe = require_value(index, L"--source-exe");
                ++index;
            }
            else if (argument == L"--mp-source-exe")
            {
                options.handoff_mp_source_exe =
                    require_value(index, L"--mp-source-exe");
                ++index;
            }
            else if (argument == L"--runtime-dir")
            {
                options.runtime_dir = require_value(index, L"--runtime-dir");
                ++index;
            }
            else if (argument == L"--homepath")
            {
                options.home_dir = require_value(index, L"--homepath");
                ++index;
            }
            else if (argument == L"--mod-dll")
            {
                options.mod_dll = require_value(index, L"--mod-dll");
                ++index;
            }
            else if (argument == L"--pezbot-archive")
            {
                options.pezbot_archive =
                    require_value(index, L"--pezbot-archive");
                ++index;
            }
            else if (argument == L"--resolution")
            {
                options.source_resolution = parse_source_resolution(require_text(index));
                ++index;
            }
            else if (argument == L"--bots")
            {
                if (selected_bot_policy)
                {
                    throw std::invalid_argument(
                        "Specify --bots only once");
                }
                const auto value = require_text(index);
                if (value == L"enabled")
                {
                    options.bot_policy = BotPolicy::enabled;
                }
                else if (value == L"disabled")
                {
                    options.bot_policy = BotPolicy::disabled;
                }
                else
                {
                    throw std::invalid_argument(
                        "--bots must be exactly enabled or disabled");
                }
                selected_bot_policy = true;
                ++index;
            }
            else
            {
                throw std::invalid_argument("Unknown command-line option");
            }
        }

        if (options.wait_for_exit && options.mode != Mode::launch)
        {
            throw std::invalid_argument("--wait is valid only with --launch");
        }
        if (options.launch_target == LaunchTarget::frontend_menu &&
            options.mode != Mode::launch)
        {
            throw std::invalid_argument("--menu is valid only with --launch");
        }
        if (options.launch_target == LaunchTarget::der_riese &&
            options.mode != Mode::launch)
        {
            throw std::invalid_argument("--der-riese is valid only with --launch");
        }
        if (options.launch_target == LaunchTarget::campaign_ber1 &&
            options.mode != Mode::launch)
        {
            throw std::invalid_argument(
                "--campaign-ber1 is valid only with --launch");
        }
        if (options.launch_target == LaunchTarget::campaign_pel2_airfield &&
            options.mode != Mode::launch)
        {
            throw std::invalid_argument(
                "--campaign-pel2-airfield is valid only with --launch");
        }
        if (options.launch_target == LaunchTarget::offline_multiplayer &&
            options.handoff_mp_source_exe)
        {
            throw std::invalid_argument(
                "--mp-source-exe configures the later SP frontend handoff; "
                "use --source-exe with --multiplayer");
        }
        return options;
    }

    Options apply_user_facing_defaults(
        Options options,
        const fs::path& launcher_executable)
    {
        const auto executable = fs::absolute(launcher_executable).lexically_normal();
        options.launcher_executable = executable;
        if (!options.mod_dll)
        {
            options.mod_dll = executable.parent_path() / L"WorldWarVR.dll";
        }
        return options;
    }

    Options make_user_facing_default_options(
        const fs::path& launcher_executable)
    {
        Options options;
        options.mode = Mode::launch;
        options.launch_target = LaunchTarget::frontend_menu;
        return apply_user_facing_defaults(std::move(options), launcher_executable);
    }

    Options make_user_facing_multiplayer_default_options(
        const fs::path& launcher_executable)
    {
        Options options;
        options.mode = Mode::launch;
        options.launch_target = LaunchTarget::offline_multiplayer;
        return apply_user_facing_defaults(std::move(options), launcher_executable);
    }

    Options make_multiplayer_handoff_options(
        const fs::path& launcher_executable,
        const MultiplayerHandoffConfig& config)
    {
        // Re-parse the canonical form against this exact shim path so callers
        // cannot construct an Options object from an unvalidated in-memory
        // record. Every discovery-sensitive field is then explicit, leaving
        // resolve_plan no opportunity to consult ambient path fallbacks.
        const auto validated = parse_multiplayer_handoff_config(
            serialize_multiplayer_handoff_config(config),
            fs::absolute(launcher_executable).lexically_normal());
        Options options;
        options.mode = Mode::launch;
        options.launch_target = LaunchTarget::offline_multiplayer;
        options.game_dir = validated.game_dir;
        options.source_exe = validated.mp_source_exe;
        options.runtime_dir = validated.mp_runtime_dir;
        options.home_dir = validated.mp_home_dir;
        options.source_resolution = validated.source_resolution;
        options.bot_policy = validated.bot_policy;
        options.stock_multiplayer_handoff = true;
        options.required_source_identity =
            validated.mp_executable_identity;
        return apply_user_facing_defaults(std::move(options), launcher_executable);
    }

    fs::path select_automatic_source(
        const fs::path& staged_exe,
        const bool staged_exe_verified,
        const fs::path& game_exe,
        const bool game_exe_present,
        const fs::path& legacy_exe)
    {
        if (game_exe_present)
        {
            return game_exe;
        }
        if (staged_exe_verified)
        {
            return staged_exe;
        }
        return legacy_exe;
    }

    std::optional<ExecutableIdentityId> executable_identity_from_sha256(
        const ExecutableKind kind,
        const std::string_view sha256) noexcept
    {
        const auto equal_ascii_case_insensitive = [](
            const std::string_view left,
            const std::string_view right) noexcept {
            return left.size() == right.size() &&
                std::equal(
                    left.begin(), left.end(), right.begin(),
                    [](const char a, const char b) {
                        return std::toupper(static_cast<unsigned char>(a)) ==
                            std::toupper(static_cast<unsigned char>(b));
                    });
        };
        for (const auto* identity : kExecutableIdentities)
        {
            if (identity->kind == kind &&
                equal_ascii_case_insensitive(sha256, identity->expected_sha256))
            {
                return identity->id;
            }
        }
        return std::nullopt;
    }

    ExecutableKind executable_kind_for_identity(
        const ExecutableIdentityId identity)
    {
        return executable_identity(identity).kind;
    }

    std::string_view executable_identity_stable_id(
        const ExecutableIdentityId identity)
    {
        return executable_identity(identity).stable_id;
    }

    bool executable_identity_uses_steam(
        const ExecutableIdentityId identity)
    {
        return executable_identity(identity).uses_steam;
    }

    std::vector<wchar_t> build_child_environment_block(
        const ExecutableIdentityId identity,
        const std::vector<std::wstring>& inherited_entries)
    {
        const bool uses_steam = executable_identity_uses_steam(identity);
        const bool multiplayer = executable_kind_for_identity(identity) == ExecutableKind::mp;
        if (!uses_steam && !multiplayer)
        {
            return {};
        }

        const auto environment_name = [](const std::wstring_view entry) {
            const auto search_begin = entry.starts_with(L'=') ? 1U : 0U;
            const auto delimiter = entry.find(L'=', search_begin);
            return delimiter == entry.npos
                ? std::wstring_view{}
                : entry.substr(0, delimiter);
        };
        const auto equal_name = [](const std::wstring_view left,
                                   const std::wstring_view right) {
            return CompareStringOrdinal(
                       left.data(), static_cast<int>(left.size()),
                       right.data(), static_cast<int>(right.size()), TRUE) ==
                CSTR_EQUAL;
        };

        std::vector<std::wstring> entries;
        entries.reserve(inherited_entries.size() + 3U);
        for (const auto& entry : inherited_entries)
        {
            if (entry.empty() || entry.find(L'\0') != std::wstring::npos)
            {
                throw std::invalid_argument(
                    "Inherited environment entry is empty or malformed");
            }
            const auto name = environment_name(entry);
            if (name.empty())
            {
                throw std::invalid_argument(
                    "Inherited environment entry has no variable name");
            }
            if ((uses_steam && (equal_name(name, L"SteamAppId") ||
                                equal_name(name, L"SteamGameId"))) ||
                (multiplayer && equal_name(name, L"WAWVR_DEFER_XR_BEGIN_FRAME")))
            {
                continue;
            }
            entries.push_back(entry);
        }
        if (uses_steam)
        {
            entries.emplace_back(L"SteamAppId=10090");
            entries.emplace_back(L"SteamGameId=10090");
        }
        if (multiplayer)
        {
            // The stock SP frontend's MP handoff inherits the SP environment.
            // Keep the accepted deferred-begin policy out of every MP child.
            entries.emplace_back(L"WAWVR_DEFER_XR_BEGIN_FRAME=0");
        }
        std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
            return CompareStringOrdinal(
                       left.c_str(), -1, right.c_str(), -1, TRUE) ==
                CSTR_LESS_THAN;
        });

        std::size_t character_count = 1U;
        for (const auto& entry : entries)
        {
            character_count += entry.size() + 1U;
        }
        std::vector<wchar_t> block;
        block.reserve(character_count);
        for (const auto& entry : entries)
        {
            block.insert(block.end(), entry.begin(), entry.end());
            block.push_back(L'\0');
        }
        block.push_back(L'\0');
        return block;
    }

    GameLanguage parse_game_language_selector(const std::string_view contents)
    {
        if (contents.empty() ||
            contents.size() > kMaximumLocalizationSelectorBytes ||
            contents.find('\0') != std::string_view::npos)
        {
            throw std::invalid_argument(
                "World at War localization.txt is empty, oversized, or not text");
        }

        std::size_t offset = 0;
        while (offset < contents.size())
        {
            const auto line_end = contents.find('\n', offset);
            auto line = contents.substr(
                offset,
                line_end == std::string_view::npos
                    ? contents.size() - offset
                    : line_end - offset);
            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }

            const bool blank = std::all_of(
                line.begin(), line.end(), [](const char character) {
                    return character == ' ' || character == '\t';
                });
            if (!blank)
            {
                if (line == "english")
                {
                    return GameLanguage::english;
                }
                if (line == "french")
                {
                    return GameLanguage::french;
                }
                if (line == "italian")
                {
                    return GameLanguage::italian;
                }
                if (line == "german")
                {
                    return GameLanguage::german;
                }
                if (line == "spanish")
                {
                    return GameLanguage::spanish;
                }
                throw std::invalid_argument(
                    "Unsupported World at War language selector: " +
                    std::string(line));
            }

            if (line_end == std::string_view::npos)
            {
                break;
            }
            offset = line_end + 1U;
        }

        throw std::invalid_argument(
            "World at War localization.txt has no installed language selector");
    }

    const wchar_t* game_language_code(const GameLanguage language) noexcept
    {
        switch (language)
        {
        case GameLanguage::english:
            return L"english";
        case GameLanguage::french:
            return L"french";
        case GameLanguage::italian:
            return L"italian";
        case GameLanguage::german:
            return L"german";
        case GameLanguage::spanish:
            return L"spanish";
        }
        return L"unknown";
    }

    const wchar_t* game_language_display_name(const GameLanguage language) noexcept
    {
        switch (language)
        {
        case GameLanguage::english:
            return L"English";
        case GameLanguage::french:
            return L"French";
        case GameLanguage::italian:
            return L"Italian";
        case GameLanguage::german:
            return L"German";
        case GameLanguage::spanish:
            return L"Spanish (Spain)";
        }
        return L"Unknown";
    }

    InstalledGameLanguage detect_installed_game_language(
        const fs::path& game_dir)
    {
        const auto normalized_game = fs::absolute(game_dir).lexically_normal();
        const auto selector = normalized_game / L"localization.txt";
        if (!is_normal_file(selector))
        {
            throw std::invalid_argument(
                "World at War localization.txt is missing or unsafe");
        }

        std::error_code size_error;
        const auto size = fs::file_size(selector, size_error);
        if (size_error || size == 0 || size > kMaximumLocalizationSelectorBytes)
        {
            throw std::invalid_argument(
                "World at War localization.txt has an invalid size");
        }

        const auto digest_before = sha256_file(selector);
        std::ifstream input(selector, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error(
                "World at War localization.txt could not be opened");
        }
        std::string contents(static_cast<std::size_t>(size), '\0');
        input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (input.gcount() != static_cast<std::streamsize>(contents.size()) ||
            input.peek() != std::char_traits<char>::eof())
        {
            throw std::runtime_error(
                "World at War localization.txt could not be read exactly");
        }
        const auto digest_after = sha256_file(selector);
        if (digest_before != digest_after)
        {
            throw std::runtime_error(
                "World at War localization.txt changed while it was being checked");
        }

        const auto language = parse_game_language_selector(contents);
        const auto zone_directory = normalized_game / L"zone" /
            game_language_code(language);
        if (!is_normal_directory(zone_directory))
        {
            throw std::invalid_argument(
                "The installed World at War language is missing its zone directory: " +
                zone_directory.string());
        }
        return {language, digest_after};
    }

    LaunchPlan resolve_plan(const Options& options)
    {
        const auto local_app_data = known_folder(FOLDERID_LocalAppData);
        LaunchPlan plan;
        plan.mode = options.mode;
        plan.launch_target = options.launch_target;
        plan.executable_kind = executable_kind_for_target(options.launch_target);
        plan.stock_multiplayer_handoff = options.stock_multiplayer_handoff;
        plan.wait_for_exit = options.wait_for_exit;
        if (plan.stock_multiplayer_handoff &&
            (plan.executable_kind != ExecutableKind::mp ||
             !options.game_dir || !options.source_exe || !options.runtime_dir ||
             !options.home_dir || !options.source_resolution ||
             !options.required_source_identity))
        {
            throw std::invalid_argument(
                "Stock MP handoff requires exact config-derived paths, identity, and resolution");
        }
        if (plan.executable_kind == ExecutableKind::mp &&
            options.handoff_mp_source_exe)
        {
            throw std::invalid_argument(
                "An MP handoff source can be attached only to an SP plan");
        }

        struct SelectedExecutable
        {
            fs::path path;
            ExecutableIdentityId identity;
        };

        const auto identify_existing = [&](const fs::path& candidate)
            -> std::optional<SelectedExecutable> {
            if (!is_normal_file(candidate))
            {
                return std::nullopt;
            }
            const auto digest = sha256_file(candidate);
            const auto identity = executable_identity_from_sha256(
                plan.executable_kind, digest);
            if (!identity)
            {
                throw std::invalid_argument(
                    "Unsupported or wrong-kind World at War executable SHA-256 " +
                    digest + ": " + candidate.string());
            }
            return SelectedExecutable{
                fs::absolute(candidate).lexically_normal(), *identity};
        };

        const auto missing_identity = [&]() {
            if (options.required_source_identity)
            {
                if (executable_kind_for_identity(
                        *options.required_source_identity) != plan.executable_kind)
                {
                    throw std::invalid_argument(
                        "Required executable identity has the wrong SP/MP kind");
                }
                return *options.required_source_identity;
            }
            return default_executable_identity(plan.executable_kind).id;
        };

        std::optional<SelectedExecutable> selected;
        if (options.source_exe)
        {
            const auto source = fs::absolute(*options.source_exe).lexically_normal();
            selected = identify_existing(source);
            if (!selected)
            {
                selected = SelectedExecutable{source, missing_identity()};
            }
        }
        else
        {
            const auto& default_identity =
                default_executable_identity(plan.executable_kind);
            if (const auto from_environment =
                    environment_value(default_identity.source_environment_variable))
            {
                const auto source = fs::absolute(*from_environment).lexically_normal();
                selected = identify_existing(source);
                if (!selected)
                {
                    selected = SelectedExecutable{source, missing_identity()};
                }
            }
        }

        const auto provisional_identity = selected
            ? selected->identity
            : missing_identity();

        if (options.game_dir)
        {
            plan.game_dir = *options.game_dir;
        }
        else if (const auto from_environment = environment_value(L"WAWVR_GAME_DIR"))
        {
            plan.game_dir = *from_environment;
        }
        else
        {
            std::vector<fs::path> discovery_runtimes;
            if (options.runtime_dir)
            {
                discovery_runtimes.push_back(
                    fs::absolute(*options.runtime_dir).lexically_normal());
            }
            const auto add_managed_runtime = [&](const ExecutableIdentity& identity) {
                const auto candidate = options.runtime_dir
                    ? fs::absolute(*options.runtime_dir).lexically_normal()
                        .parent_path() / identity.runtime_leaf
                    : local_app_data / L"WaWVR" / L"runtime" /
                        identity.runtime_leaf;
                if (std::none_of(
                        discovery_runtimes.begin(), discovery_runtimes.end(),
                        [&](const fs::path& existing) {
                            return is_same_path(existing, candidate);
                        }))
                {
                    discovery_runtimes.push_back(candidate);
                }
            };
            add_managed_runtime(executable_identity(provisional_identity));
            for (const auto* identity : kExecutableIdentities)
            {
                if (identity->kind == plan.executable_kind)
                {
                    add_managed_runtime(*identity);
                }
            }
            for (const auto* identity : kExecutableIdentities)
            {
                if (identity->kind != plan.executable_kind)
                {
                    add_managed_runtime(*identity);
                }
            }

            std::optional<fs::path> prepared_game_directory;
            for (const auto& runtime : discovery_runtimes)
            {
                prepared_game_directory =
                    discover_game_directory_from_runtime(runtime);
                if (prepared_game_directory)
                {
                    break;
                }
            }
            if (!prepared_game_directory)
            {
                throw std::invalid_argument(
                    "Game directory is required; pass --game-dir, set "
                    "WAWVR_GAME_DIR, or prepare the standalone runtime once");
            }
            plan.game_dir = *prepared_game_directory;
        }
        plan.game_dir = fs::absolute(plan.game_dir).lexically_normal();

        if (!selected)
        {
            const auto& default_identity =
                default_executable_identity(plan.executable_kind);
            const auto retail =
                plan.game_dir / default_identity.staged_filename;
            selected = identify_existing(retail);

            if (!selected)
            {
                std::vector<const ExecutableIdentity*> staged_identities;
                const auto add_staged_identity = [&](const ExecutableIdentity& identity) {
                    if (identity.kind == plan.executable_kind &&
                        std::find(
                            staged_identities.begin(), staged_identities.end(),
                            &identity) == staged_identities.end())
                    {
                        staged_identities.push_back(&identity);
                    }
                };
                add_staged_identity(executable_identity(provisional_identity));
                for (const auto* identity : kExecutableIdentities)
                {
                    add_staged_identity(*identity);
                }

                if (options.runtime_dir)
                {
                    const auto candidate =
                        fs::absolute(*options.runtime_dir).lexically_normal() /
                        default_identity.staged_filename;
                    selected = identify_existing(candidate);
                }
                for (const auto* identity : staged_identities)
                {
                    if (selected)
                    {
                        break;
                    }
                    const auto candidate = local_app_data / L"WaWVR" /
                        L"runtime" / identity->runtime_leaf /
                        identity->staged_filename;
                    const auto verified = identify_existing(candidate);
                    if (verified && verified->identity == identity->id)
                    {
                        selected = verified;
                    }
                }
            }

            const auto legacy = local_app_data / L"Plutonium" / L"games" /
                default_identity.legacy_filename;
            if (!selected)
            {
                selected = identify_existing(legacy);
            }
            if (!selected)
            {
                // Preserve the diagnostic path for a genuinely absent source,
                // while any present-but-unknown candidate above has already
                // failed closed during full-hash identification.
                selected = SelectedExecutable{legacy, missing_identity()};
            }
        }

        if (options.required_source_identity &&
            selected->identity != *options.required_source_identity)
        {
            throw std::invalid_argument(
                "Executable bytes do not match the exact handoff identity");
        }

        plan.executable_identity = selected->identity;
        const auto& identity = executable_identity(plan.executable_identity);
        if (identity.kind != plan.executable_kind)
        {
            throw std::invalid_argument(
                "Resolved executable identity has the wrong SP/MP kind");
        }
        plan.source_exe = selected->path;
        plan.runtime_dir = fs::absolute(options.runtime_dir.value_or(
            local_app_data / L"WaWVR" / L"runtime" / identity.runtime_leaf))
            .lexically_normal();
        plan.home_dir = fs::absolute(options.home_dir.value_or(
            local_app_data / L"WaWVR" / identity.home_leaf))
            .lexically_normal();
        plan.home_dir_is_launcher_managed =
            !options.home_dir.has_value() || options.stock_multiplayer_handoff;
        plan.staged_exe = plan.runtime_dir / identity.staged_filename;

        plan.mod_dll = options.mod_dll;
        plan.launcher_executable = options.launcher_executable;
        plan.bot_policy = options.bot_policy;
        if (options.source_resolution)
        {
            plan.source_resolution = *options.source_resolution;
        }
        else if (const auto from_environment =
                     environment_value(L"WAWVR_SOURCE_RESOLUTION"))
        {
            plan.source_resolution = parse_source_resolution(*from_environment);
        }
        else
        {
            plan.source_resolution = plan.executable_kind == ExecutableKind::sp
                ? kNativeSpSourceResolution
                : kDefaultSourceResolution;
        }
        if (plan.executable_kind == ExecutableKind::mp &&
            (plan.source_resolution.width > kMaximumLegacySourceWidth ||
             plan.source_resolution.height > kMaximumLegacySourceHeight))
        {
            throw std::invalid_argument(
                "Source resolutions above 3840x2160 require the exact validated SP renderer patch");
        }

        if (plan.mod_dll)
        {
            plan.mod_dll = fs::absolute(*plan.mod_dll).lexically_normal();
        }
        if (plan.launcher_executable)
        {
            plan.launcher_executable =
                fs::absolute(*plan.launcher_executable).lexically_normal();
        }
        if (!plan.stock_multiplayer_handoff &&
            plan.bot_policy != BotPolicy::disabled)
        {
            fs::path downloads_directory;
            try
            {
                downloads_directory = known_folder(FOLDERID_Downloads);
            }
            catch (...)
            {
                // Downloads discovery is optional. Explicit/beside-launcher
                // paths and base SP/MP launch remain available if the shell
                // folder cannot be resolved in this Windows account.
            }
            plan.pezbot_archive = discover_pezbot_archive(
                options.pezbot_archive,
                plan.launcher_executable,
                downloads_directory);
        }
        if (plan.executable_kind == ExecutableKind::sp)
        {
            const auto default_mp_identity =
                paired_mp_identity(plan.executable_identity);
            std::optional<SelectedExecutable> selected_mp;
            if (options.handoff_mp_source_exe)
            {
                const auto source =
                    fs::absolute(*options.handoff_mp_source_exe).lexically_normal();
                const auto digest = is_normal_file(source)
                    ? std::optional<std::string>(sha256_file(source))
                    : std::nullopt;
                const auto mp_identity = digest
                    ? executable_identity_from_sha256(ExecutableKind::mp, *digest)
                    : std::optional<ExecutableIdentityId>(default_mp_identity);
                if (!mp_identity)
                {
                    throw std::invalid_argument(
                        "Unsupported or wrong-kind MP handoff executable SHA-256 " +
                        *digest + ": " + source.string());
                }
                selected_mp = SelectedExecutable{source, *mp_identity};
            }
            else if (const auto from_environment = environment_value(
                    kMpExecutableIdentity.source_environment_variable))
            {
                const auto source =
                    fs::absolute(*from_environment).lexically_normal();
                if (is_normal_file(source))
                {
                    const auto digest = sha256_file(source);
                    const auto mp_identity = executable_identity_from_sha256(
                        ExecutableKind::mp, digest);
                    if (!mp_identity)
                    {
                        throw std::invalid_argument(
                            "Unsupported or wrong-kind MP handoff executable SHA-256 " +
                            digest + ": " + source.string());
                    }
                    selected_mp = SelectedExecutable{source, *mp_identity};
                }
                else
                {
                    selected_mp = SelectedExecutable{source, default_mp_identity};
                }
            }
            else
            {
                const auto identify_mp = [&](const fs::path& source)
                    -> std::optional<SelectedExecutable> {
                    if (!is_normal_file(source))
                    {
                        return std::nullopt;
                    }
                    const auto digest = sha256_file(source);
                    const auto mp_identity = executable_identity_from_sha256(
                        ExecutableKind::mp, digest);
                    if (!mp_identity)
                    {
                        throw std::invalid_argument(
                            "Unsupported or wrong-kind MP handoff executable SHA-256 " +
                            digest + ": " + source.string());
                    }
                    return SelectedExecutable{
                        fs::absolute(source).lexically_normal(), *mp_identity};
                };

                selected_mp = identify_mp(
                    plan.game_dir / kMpExecutableIdentity.staged_filename);
                const auto add_mp_stage = [&](const ExecutableIdentityId id) {
                    if (selected_mp)
                    {
                        return;
                    }
                    const auto& mp_identity = executable_identity(id);
                    const auto candidate = local_app_data / L"WaWVR" / L"runtime" /
                        mp_identity.runtime_leaf / mp_identity.staged_filename;
                    const auto verified = identify_mp(candidate);
                    if (verified && verified->identity == id)
                    {
                        selected_mp = verified;
                    }
                };
                add_mp_stage(default_mp_identity);
                add_mp_stage(ExecutableIdentityId::steam_mp_build_252004);
                add_mp_stage(ExecutableIdentityId::plutonium_mp_1_7_1263);
                const auto legacy = local_app_data / L"Plutonium" / L"games" /
                    kMpExecutableIdentity.legacy_filename;
                if (!selected_mp)
                {
                    selected_mp = identify_mp(legacy);
                }
                if (!selected_mp)
                {
                    selected_mp = SelectedExecutable{legacy, default_mp_identity};
                }
            }
            plan.handoff_mp_source_exe = selected_mp->path;
            plan.handoff_mp_executable_identity = selected_mp->identity;
        }
        if (plan.executable_kind == ExecutableKind::mp &&
            plan.bot_policy != BotPolicy::disabled)
        {
            plan.pezbot_enabled = inspect_optional_pezbot(plan).enabled();
        }
        return plan;
    }

    PeInfo inspect_pe(const fs::path& path)
    {
        const auto bytes = read_prefix(path, 4096);
        if (bytes.size() < 0x40 || read_u16(bytes, 0) != 0x5A4D)
        {
            throw std::runtime_error("Missing DOS MZ header: " + path.string());
        }

        const auto pe_offset = static_cast<std::size_t>(read_u32(bytes, 0x3c));
        if (pe_offset > bytes.size() || bytes.size() - pe_offset < 24 ||
            read_u32(bytes, pe_offset) != 0x00004550)
        {
            throw std::runtime_error("Missing PE signature: " + path.string());
        }

        const auto file_header = pe_offset + 4;
        const auto optional_header = file_header + 20;
        PeInfo info;
        info.machine = read_u16(bytes, file_header);
        info.timestamp = read_u32(bytes, file_header + 4);
        info.characteristics = read_u16(bytes, file_header + 18);
        info.optional_magic = read_u16(bytes, optional_header);
        info.entrypoint_rva = read_u32(bytes, optional_header + 16);
        if (info.optional_magic == kPe32Magic)
        {
            info.image_base = read_u32(bytes, optional_header + 28);
        }
        else
        {
            throw std::runtime_error("Only PE32 files are supported: " + path.string());
        }
        return info;
    }

    FileVersion inspect_file_version(const fs::path& path)
    {
        DWORD ignored = 0;
        const auto size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        if (size == 0)
        {
            throw_last_error("GetFileVersionInfoSizeW");
        }

        std::vector<std::uint8_t> data(size);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
        {
            throw_last_error("GetFileVersionInfoW");
        }

        VS_FIXEDFILEINFO* fixed = nullptr;
        UINT fixed_size = 0;
        if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixed_size) ||
            !fixed || fixed_size < sizeof(VS_FIXEDFILEINFO) || fixed->dwSignature != 0xFEEF04BD)
        {
            throw std::runtime_error("Missing valid VS_FIXEDFILEINFO");
        }

        return {
            HIWORD(fixed->dwFileVersionMS),
            LOWORD(fixed->dwFileVersionMS),
            HIWORD(fixed->dwFileVersionLS),
            LOWORD(fixed->dwFileVersionLS),
        };
    }

    namespace
    {
        std::string bcrypt_hash_file(
            const fs::path& path,
            const wchar_t* algorithm_identifier,
            const std::size_t digest_size,
            const char* algorithm_name)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            std::vector<std::uint8_t> hash_object;

            const auto close_handles = [&]() {
                if (hash)
                {
                    BCryptDestroyHash(hash);
                }
                if (algorithm)
                {
                    BCryptCloseAlgorithmProvider(algorithm, 0);
                }
            };

            if (BCryptOpenAlgorithmProvider(
                    &algorithm, algorithm_identifier, nullptr, 0) < 0)
            {
                throw std::runtime_error(
                    std::string("BCryptOpenAlgorithmProvider(") +
                    algorithm_name + ") failed");
            }

            try
            {
                DWORD object_size = 0;
                DWORD result_size = 0;
                if (BCryptGetProperty(
                        algorithm,
                        BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&object_size),
                        sizeof(object_size),
                        &result_size,
                        0) < 0)
                {
                    throw std::runtime_error("BCryptGetProperty failed");
                }

                hash_object.resize(object_size);
                if (BCryptCreateHash(
                        algorithm,
                        &hash,
                        hash_object.data(),
                        static_cast<ULONG>(hash_object.size()),
                        nullptr,
                        0,
                        0) < 0)
                {
                    throw std::runtime_error("BCryptCreateHash failed");
                }

                std::ifstream input(path, std::ios::binary);
                if (!input)
                {
                    throw std::runtime_error(
                        "Could not open file for hashing: " + path.string());
                }

                // Keep the 1 MiB streaming buffer off the comparatively small
                // x86 thread stack.
                std::vector<std::uint8_t> chunk(1024 * 1024);
                while (input)
                {
                    input.read(
                        reinterpret_cast<char*>(chunk.data()),
                        static_cast<std::streamsize>(chunk.size()));
                    const auto count = input.gcount();
                    if (count > 0 && BCryptHashData(
                            hash,
                            chunk.data(),
                            static_cast<ULONG>(count),
                            0) < 0)
                    {
                        throw std::runtime_error("BCryptHashData failed");
                    }
                }

                if (!input.eof())
                {
                    throw std::runtime_error(
                        "Read failure while hashing: " + path.string());
                }

                std::vector<std::uint8_t> digest(digest_size);
                if (BCryptFinishHash(
                        hash,
                        digest.data(),
                        static_cast<ULONG>(digest.size()),
                        0) < 0)
                {
                    throw std::runtime_error("BCryptFinishHash failed");
                }

                std::ostringstream output;
                output << std::uppercase << std::hex << std::setfill('0');
                for (const auto byte : digest)
                {
                    output << std::setw(2) << static_cast<unsigned int>(byte);
                }
                close_handles();
                return output.str();
            }
            catch (...)
            {
                close_handles();
                throw;
            }
        }
    }

    std::string sha256_file(const fs::path& path)
    {
        return bcrypt_hash_file(
            path, BCRYPT_SHA256_ALGORITHM, 32, "SHA-256");
    }

    std::string md5_file(const fs::path& path)
    {
        return bcrypt_hash_file(path, BCRYPT_MD5_ALGORITHM, 16, "MD5");
    }

    namespace
    {
        struct PezBotReceiptRecord
        {
            fs::path relative_path;
            std::uintmax_t size = 0;
            std::string sha256;
        };

        struct PezBotImportProcessResult
        {
            bool started = false;
            bool timed_out = false;
            std::uint32_t exit_code = ERROR_GEN_FAILURE;
            std::wstring detail;
        };

        [[nodiscard]] std::string upper_ascii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](const char ch) {
                return static_cast<char>(
                    std::toupper(static_cast<unsigned char>(ch)));
            });
            return value;
        }

        [[nodiscard]] std::wstring lower_text(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t ch) {
                return static_cast<wchar_t>(std::towlower(ch));
            });
            return value;
        }

        [[nodiscard]] bool is_normal_file(const fs::path& path) noexcept
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & (FILE_ATTRIBUTE_DIRECTORY |
                               FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
        }

        [[nodiscard]] bool is_normal_directory(const fs::path& path) noexcept
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
        }

        [[nodiscard]] bool path_exists_at_all(const fs::path& path) noexcept
        {
            return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
        }

        [[nodiscard]] bool normal_path_exists_or_throw(
            const fs::path& path,
            const bool directory,
            const char* description)
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                const DWORD error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                {
                    return false;
                }
                throw std::system_error(
                    static_cast<int>(error),
                    std::system_category(),
                    std::string("GetFileAttributesW(") + description + ')');
            }

            const bool is_directory =
                (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                is_directory != directory)
            {
                throw std::runtime_error(
                    std::string(description) +
                    " must be an ordinary non-reparse " +
                    (directory ? "directory" : "file"));
            }
            return true;
        }

        [[nodiscard]] std::string read_bounded_normal_file(
            const fs::path& path,
            const std::uintmax_t maximum_size,
            const char* description)
        {
            UniqueHandle input(CreateFileW(
                path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr));
            if (!input.valid())
            {
                throw_last_error(description);
            }

            BY_HANDLE_FILE_INFORMATION information{};
            if (!GetFileInformationByHandle(input.get(), &information))
            {
                throw_last_error("GetFileInformationByHandle(profile file)");
            }
            if ((information.dwFileAttributes &
                 (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
            {
                throw std::runtime_error(
                    std::string(description) +
                    " changed into a directory or reparse point");
            }

            LARGE_INTEGER file_size{};
            if (!GetFileSizeEx(input.get(), &file_size))
            {
                throw_last_error("GetFileSizeEx(profile file)");
            }
            if (file_size.QuadPart < 0 ||
                static_cast<std::uint64_t>(file_size.QuadPart) > maximum_size)
            {
                throw std::runtime_error(
                    std::string(description) + " exceeds its safe size limit");
            }

            std::string contents(
                static_cast<std::size_t>(file_size.QuadPart), '\0');
            std::size_t read_total = 0;
            while (read_total < contents.size())
            {
                DWORD read = 0;
                const auto remaining = contents.size() - read_total;
                const auto chunk = static_cast<DWORD>((std::min)(
                    remaining,
                    static_cast<std::size_t>(
                        (std::numeric_limits<DWORD>::max)())));
                if (!ReadFile(
                        input.get(),
                        contents.data() + read_total,
                        chunk,
                        &read,
                        nullptr) ||
                    read == 0)
                {
                    throw_last_error("ReadFile(profile file)");
                }
                read_total += read;
            }
            return contents;
        }

        [[nodiscard]] std::wstring decode_active_profile_name(
            std::string contents)
        {
            if (contents.starts_with("\xEF\xBB\xBF"))
            {
                contents.erase(0, 3);
            }
            while (!contents.empty() &&
                   (contents.back() == '\r' || contents.back() == '\n'))
            {
                contents.pop_back();
            }
            if (contents.empty() || contents.size() > 512 ||
                contents.find_first_of("\r\n\0", 0, 3) != std::string::npos)
            {
                throw std::runtime_error(
                    "Isolated multiplayer active profile metadata is malformed");
            }

            const auto decode = [&](const UINT code_page, const DWORD flags) {
                const int required = MultiByteToWideChar(
                    code_page,
                    flags,
                    contents.data(),
                    static_cast<int>(contents.size()),
                    nullptr,
                    0);
                if (required <= 0)
                {
                    return std::wstring{};
                }
                std::wstring decoded(static_cast<std::size_t>(required), L'\0');
                if (MultiByteToWideChar(
                        code_page,
                        flags,
                        contents.data(),
                        static_cast<int>(contents.size()),
                        decoded.data(),
                        required) != required)
                {
                    return std::wstring{};
                }
                return decoded;
            };

            auto profile = decode(CP_UTF8, MB_ERR_INVALID_CHARS);
            if (profile.empty())
            {
                profile = decode(CP_ACP, 0);
            }
            if (profile.empty() || profile.size() > 128 ||
                profile == L"." || profile == L".." ||
                profile.back() == L'.' || profile.back() == L' ')
            {
                throw std::runtime_error(
                    "Isolated multiplayer active profile name is unsafe");
            }
            for (const wchar_t character : profile)
            {
                if (character < 0x20 ||
                    std::wstring_view(L"<>:\"/\\|?*").find(character) !=
                        std::wstring_view::npos)
                {
                    throw std::runtime_error(
                        "Isolated multiplayer active profile name is unsafe");
                }
            }

            const fs::path component(profile);
            if (component.is_absolute() || component.has_root_name() ||
                component.has_root_directory() || component.has_parent_path() ||
                component.filename().wstring() != profile)
            {
                throw std::runtime_error(
                    "Isolated multiplayer active profile escaped its profile root");
            }
            return profile;
        }

        [[nodiscard]] bool equals_ascii_case_insensitive(
            const std::string_view left,
            const std::string_view right) noexcept
        {
            if (left.size() != right.size())
            {
                return false;
            }
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                if (std::tolower(static_cast<unsigned char>(left[index])) !=
                    std::tolower(static_cast<unsigned char>(right[index])))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool is_ui_dedicated_assignment(
            const std::string_view line) noexcept
        {
            const auto horizontal_space = [](const char character) {
                return character == ' ' || character == '\t';
            };
            std::size_t cursor = 0;
            while (cursor < line.size() && horizontal_space(line[cursor]))
            {
                ++cursor;
            }
            const auto command_begin = cursor;
            while (cursor < line.size() && !horizontal_space(line[cursor]))
            {
                ++cursor;
            }
            const auto command = line.substr(
                command_begin, cursor - command_begin);
            if (!equals_ascii_case_insensitive(command, "seta") &&
                !equals_ascii_case_insensitive(command, "set"))
            {
                return false;
            }
            if (cursor == line.size())
            {
                return false;
            }
            while (cursor < line.size() && horizontal_space(line[cursor]))
            {
                ++cursor;
            }
            const auto name_begin = cursor;
            while (cursor < line.size() && !horizontal_space(line[cursor]))
            {
                ++cursor;
            }
            return equals_ascii_case_insensitive(
                line.substr(name_begin, cursor - name_begin),
                "ui_dedicated");
        }

        struct MpProfileConfigNormalization
        {
            std::string contents;
            std::size_t assignment_count = 0;
        };

        [[nodiscard]] MpProfileConfigNormalization normalize_mp_profile_config(
            const std::string& original)
        {
            if (original.find('\0') != std::string::npos)
            {
                throw std::runtime_error(
                    "Isolated multiplayer profile config contains binary data");
            }

            MpProfileConfigNormalization result;
            result.contents.reserve(
                original.size() + kUiDedicatedDisabledLine.size() + 2);
            std::size_t cursor = 0;
            while (cursor < original.size())
            {
                const auto newline = original.find('\n', cursor);
                const auto segment_end =
                    newline == std::string::npos ? original.size() : newline;
                auto line_end = segment_end;
                if (line_end > cursor && original[line_end - 1] == '\r')
                {
                    --line_end;
                }
                const std::string_view line(
                    original.data() + cursor, line_end - cursor);
                if (is_ui_dedicated_assignment(line))
                {
                    ++result.assignment_count;
                    result.contents.append(kUiDedicatedDisabledLine);
                    result.contents.append(
                        original.data() + line_end,
                        (newline == std::string::npos
                             ? segment_end
                             : newline + 1) -
                            line_end);
                }
                else
                {
                    result.contents.append(
                        original.data() + cursor,
                        (newline == std::string::npos
                             ? segment_end
                             : newline + 1) -
                            cursor);
                }
                cursor = newline == std::string::npos
                    ? original.size()
                    : newline + 1;
            }

            if (result.assignment_count == 0)
            {
                const bool use_crlf =
                    original.find("\r\n") != std::string::npos;
                const std::string_view line_break =
                    use_crlf ? std::string_view("\r\n") : std::string_view("\n");
                if (!result.contents.empty() &&
                    result.contents.back() != '\n')
                {
                    result.contents.append(line_break);
                }
                result.contents.append(kUiDedicatedDisabledLine);
                result.contents.append(line_break);
                result.assignment_count = 1;
            }
            return result;
        }

        void replace_profile_config_atomically(
            const fs::path& destination,
            const std::string& contents)
        {
            const auto parent = destination.parent_path().lexically_normal();
            if (!is_normal_directory(parent) || !is_normal_file(destination) ||
                destination.lexically_normal().parent_path() != parent)
            {
                throw std::runtime_error(
                    "Isolated multiplayer profile changed before normalization");
            }

            fs::path temporary;
            UniqueHandle output;
            for (std::uint32_t attempt = 0; attempt < 100U; ++attempt)
            {
                temporary = parent /
                    (destination.filename().wstring() + L".wawvr-tmp-" +
                     std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(GetTickCount64()) + L"-" +
                     std::to_wstring(attempt));
                if (temporary.parent_path() != parent)
                {
                    throw std::logic_error(
                        "Multiplayer profile temporary path escaped its parent");
                }
                output = UniqueHandle(CreateFileW(
                    temporary.c_str(),
                    GENERIC_WRITE,
                    0,
                    nullptr,
                    CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr));
                if (output.valid())
                {
                    break;
                }
                if (GetLastError() != ERROR_FILE_EXISTS &&
                    GetLastError() != ERROR_ALREADY_EXISTS)
                {
                    throw_last_error(
                        "CreateFileW(multiplayer profile temporary config)");
                }
            }
            if (!output.valid())
            {
                throw std::runtime_error(
                    "Could not allocate a multiplayer profile temporary config");
            }

            bool moved = false;
            try
            {
                std::size_t written_total = 0;
                while (written_total < contents.size())
                {
                    DWORD written = 0;
                    const auto remaining = contents.size() - written_total;
                    const auto chunk = static_cast<DWORD>((std::min)(
                        remaining,
                        static_cast<std::size_t>(
                            (std::numeric_limits<DWORD>::max)())));
                    if (!WriteFile(
                            output.get(),
                            contents.data() + written_total,
                            chunk,
                            &written,
                            nullptr) ||
                        written == 0)
                    {
                        throw_last_error(
                            "WriteFile(multiplayer profile config)");
                    }
                    written_total += written;
                }
                if (!FlushFileBuffers(output.get()))
                {
                    throw_last_error(
                        "FlushFileBuffers(multiplayer profile config)");
                }
                output = UniqueHandle{};

                if (!is_normal_directory(parent) ||
                    !is_normal_file(destination))
                {
                    throw std::runtime_error(
                        "Isolated multiplayer profile changed during normalization");
                }
                if (!MoveFileExW(
                        temporary.c_str(),
                        destination.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    throw_last_error(
                        "MoveFileExW(multiplayer profile config)");
                }
                moved = true;
            }
            catch (...)
            {
                output = UniqueHandle{};
                if (!moved)
                {
                    DeleteFileW(temporary.c_str());
                }
                throw;
            }

            if (!is_normal_file(destination) ||
                read_bounded_normal_file(
                    destination,
                    kMaximumMpProfileConfigBytes,
                    "ReadFile(final multiplayer profile config)") != contents)
            {
                throw std::runtime_error(
                    "Multiplayer profile normalization failed final validation");
            }
        }

        [[nodiscard]] bool is_hex_digest(
            const std::string_view value,
            const std::size_t expected_length) noexcept
        {
            return value.size() == expected_length &&
                std::all_of(value.begin(), value.end(), [](const char ch) {
                    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
                });
        }

        [[nodiscard]] std::optional<std::uintmax_t> parse_uintmax(
            const std::string_view text) noexcept
        {
            std::uintmax_t value = 0;
            const auto [end, error] = std::from_chars(
                text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size())
            {
                return std::nullopt;
            }
            return value;
        }

        [[nodiscard]] std::vector<std::string_view> split_receipt_line(
            const std::string& line)
        {
            std::vector<std::string_view> fields;
            std::size_t begin = 0;
            for (;;)
            {
                const auto separator = line.find('|', begin);
                fields.emplace_back(
                    line.data() + begin,
                    (separator == std::string::npos ? line.size() : separator) - begin);
                if (separator == std::string::npos)
                {
                    return fields;
                }
                begin = separator + 1;
            }
        }

        [[nodiscard]] bool is_safe_readme_filename(
            const std::wstring_view filename) noexcept
        {
            if (filename.empty() || filename.size() > 125)
            {
                return false;
            }
            for (const wchar_t ch : filename)
            {
                const bool allowed =
                    (ch >= L'a' && ch <= L'z') ||
                    (ch >= L'A' && ch <= L'Z') ||
                    (ch >= L'0' && ch <= L'9') ||
                    ch == L' ' || ch == L'_' || ch == L'(' || ch == L')' ||
                    ch == L'.' || ch == L',' || ch == L'\'' || ch == L'-';
                if (!allowed)
                {
                    return false;
                }
            }

            const auto extension = lower_text(
                fs::path(std::wstring(filename)).extension().wstring());
            return extension == L".txt" || extension == L".rtf" ||
                extension == L".htm" || extension == L".html" ||
                extension == L".pdf";
        }

        [[nodiscard]] bool is_safe_receipt_relative_path(
            const std::wstring& text) noexcept
        {
            if (text.empty() || text.front() == L'/' || text.find(L'\\') != text.npos ||
                text.find(L':') != text.npos || text.find(L'\0') != text.npos)
            {
                return false;
            }

            const auto slash = text.find(L'/');
            if (slash == text.npos)
            {
                return std::any_of(
                    kPezBotRequiredRootFiles.begin(),
                    kPezBotRequiredRootFiles.end(),
                    [&](const wchar_t* required) {
                        return lower_text(text) == lower_text(required);
                    });
            }

            if (text.find(L'/', slash + 1) != text.npos ||
                lower_text(text.substr(0, slash)) != L"readme")
            {
                return false;
            }
            return is_safe_readme_filename(text.substr(slash + 1));
        }

        [[nodiscard]] bool verify_pezbot_receipt(
            const fs::path& install_directory,
            const PezBotArchiveIdentity& identity,
            std::wstring& detail)
        {
            if (!is_normal_directory(install_directory))
            {
                detail = L"Install folder is missing, not a directory, or a reparse point";
                return false;
            }

            const auto receipt_path = install_directory / kPezBotReceiptFilename;
            if (!is_normal_file(receipt_path))
            {
                detail = L"No trusted WaWVR PeZBOT receipt is present";
                return false;
            }

            std::error_code file_error;
            const auto receipt_size = fs::file_size(receipt_path, file_error);
            if (file_error || receipt_size == 0 || receipt_size > 64 * 1024)
            {
                detail = L"PeZBOT receipt size is invalid";
                return false;
            }

            std::ifstream input(receipt_path, std::ios::binary);
            if (!input)
            {
                detail = L"PeZBOT receipt could not be opened";
                return false;
            }
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (line.find('\0') != std::string::npos)
                {
                    detail = L"PeZBOT receipt contains invalid text";
                    return false;
                }
                if (!line.empty())
                {
                    lines.push_back(line);
                }
            }
            if (!input.eof() || lines.size() < 8 || lines.size() > 70 ||
                lines[0] != "WAWVR_PEZBOT_RECEIPT_V1")
            {
                detail = L"PeZBOT receipt format is invalid";
                return false;
            }

            const auto size_fields = split_receipt_line(lines[1]);
            const auto md5_fields = split_receipt_line(lines[2]);
            const auto parsed_size = size_fields.size() == 2
                ? parse_uintmax(size_fields[1])
                : std::nullopt;
            if (size_fields.size() != 2 || size_fields[0] != "archive-size" ||
                !parsed_size || *parsed_size != identity.size ||
                md5_fields.size() != 2 || md5_fields[0] != "archive-md5" ||
                upper_ascii(std::string(md5_fields[1])) != upper_ascii(identity.md5))
            {
                detail = L"PeZBOT receipt archive identity does not match";
                return false;
            }

            std::vector<PezBotReceiptRecord> records;
            std::set<std::wstring> seen;
            bool has_readme = false;
            for (std::size_t index = 3; index < lines.size(); ++index)
            {
                const auto fields = split_receipt_line(lines[index]);
                const auto parsed_file_size = fields.size() == 4
                    ? parse_uintmax(fields[2])
                    : std::nullopt;
                if (fields.size() != 4 || fields[0] != "file" ||
                    !parsed_file_size || *parsed_file_size == 0 ||
                    !is_hex_digest(fields[3], 64))
                {
                    detail = L"PeZBOT receipt file record is invalid";
                    return false;
                }

                const std::string relative_ascii(fields[1]);
                if (!std::all_of(
                        relative_ascii.begin(), relative_ascii.end(),
                        [](const char ch) {
                            return static_cast<unsigned char>(ch) < 0x80;
                        }))
                {
                    detail = L"PeZBOT receipt filename is not ASCII";
                    return false;
                }
                const std::wstring relative(
                    relative_ascii.begin(), relative_ascii.end());
                if (!is_safe_receipt_relative_path(relative) ||
                    !seen.insert(lower_text(relative)).second)
                {
                    detail = L"PeZBOT receipt contains an unsafe or duplicate path";
                    return false;
                }
                has_readme = has_readme ||
                    lower_text(relative).starts_with(L"readme/");
                records.push_back({
                    fs::path(relative),
                    *parsed_file_size,
                    upper_ascii(std::string(fields[3])),
                });
            }

            for (const wchar_t* required : kPezBotRequiredRootFiles)
            {
                if (!seen.contains(lower_text(required)))
                {
                    detail = L"PeZBOT receipt is missing a required mod file";
                    return false;
                }
            }
            if (!has_readme)
            {
                detail = L"PeZBOT receipt is missing its ReadMe documents";
                return false;
            }

            for (const auto& record : records)
            {
                auto relative = record.relative_path.generic_wstring();
                std::replace(relative.begin(), relative.end(), L'/', fs::path::preferred_separator);
                const auto path =
                    (install_directory / fs::path(relative)).lexically_normal();
                if (!is_same_or_descendant(path, install_directory) ||
                    !is_normal_file(path))
                {
                    detail = L"A receipted PeZBOT file is missing or unsafe: " +
                        record.relative_path.generic_wstring();
                    return false;
                }
                std::error_code size_error;
                if (fs::file_size(path, size_error) != record.size || size_error ||
                    sha256_file(path) != record.sha256)
                {
                    detail = L"A receipted PeZBOT file has changed: " +
                        record.relative_path.generic_wstring();
                    return false;
                }
            }

            for (const auto& entry : fs::recursive_directory_iterator(
                     install_directory, fs::directory_options::none))
            {
                const DWORD attributes = GetFileAttributesW(entry.path().c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES ||
                    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                {
                    detail = L"PeZBOT install contains an unsafe filesystem entry";
                    return false;
                }
                auto relative = entry.path().lexically_relative(install_directory)
                    .generic_wstring();
                const auto lowered = lower_text(relative);
                if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    if (lowered != L"readme")
                    {
                        detail = L"PeZBOT install contains an unexpected directory";
                        return false;
                    }
                    continue;
                }

                const bool receipted = seen.contains(lowered);
                const bool receipt = lowered == lower_text(kPezBotReceiptFilename);
                const bool mutable_runtime_log =
                    lowered == L"console_mp.log" || lowered == L"games_mp.log";
                if (!receipted && !receipt && !mutable_runtime_log)
                {
                    detail = L"PeZBOT install contains an unexpected file: " + relative;
                    return false;
                }
            }

            detail = L"Verified receipt and " + std::to_wstring(records.size()) +
                L" imported files in " + install_directory.wstring();
            return true;
        }

        [[nodiscard]] std::optional<fs::path> pezbot_helper_path(
            const LaunchPlan& plan,
            const std::optional<fs::path>& override_path)
        {
            if (override_path)
            {
                return fs::absolute(*override_path).lexically_normal();
            }
            if (!plan.launcher_executable)
            {
                return std::nullopt;
            }
            return (plan.launcher_executable->parent_path() /
                    kPezBotImportHelperFilename)
                .lexically_normal();
        }

        [[nodiscard]] bool is_verified_pezbot_helper(
            const fs::path& helper) noexcept
        {
            if (!is_normal_file(helper))
            {
                return false;
            }
            try
            {
                return sha256_file(helper) == kExpectedPezBotImportHelperSha256;
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] fs::path system_windows_powershell()
        {
            std::array<wchar_t, MAX_PATH> windows_directory{};
            const UINT length = GetWindowsDirectoryW(
                windows_directory.data(),
                static_cast<UINT>(windows_directory.size()));
            if (length == 0 || length >= windows_directory.size())
            {
                throw_last_error("GetWindowsDirectoryW");
            }
            return fs::path(windows_directory.data()) / L"System32" /
                L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
        }

        PezBotImportProcessResult run_pezbot_import_helper(
            const fs::path& helper,
            const fs::path& archive,
            const fs::path& destination,
            const PezBotArchiveIdentity& identity)
        {
            PezBotImportProcessResult result;
            const auto powershell = system_windows_powershell();
            if (!is_normal_file(powershell))
            {
                result.detail = L"Fixed system Windows PowerShell executable is unavailable";
                return result;
            }

            std::wstring command = quote_windows_argument(powershell.wstring()) +
                L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
                quote_windows_argument(helper.wstring()) +
                L" -ArchivePath " + quote_windows_argument(archive.wstring()) +
                L" -DestinationRoot " + quote_windows_argument(destination.wstring()) +
                L" -ExpectedSize " + std::to_wstring(identity.size) +
                L" -ExpectedMd5 " + quote_windows_argument(
                    widen_ascii(upper_ascii(identity.md5)));
            std::vector<wchar_t> mutable_command(command.begin(), command.end());
            mutable_command.push_back(L'\0');

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process_info{};
            if (!CreateProcessW(
                    powershell.c_str(),
                    mutable_command.data(),
                    nullptr,
                    nullptr,
                    FALSE,
                    CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                    nullptr,
                    destination.parent_path().c_str(),
                    &startup,
                    &process_info))
            {
                result.detail = L"Could not start the fixed PeZBOT import helper";
                return result;
            }

            result.started = true;
            UniqueHandle process(process_info.hProcess);
            UniqueHandle thread(process_info.hThread);
            const DWORD wait = WaitForSingleObject(
                process.get(), kPezBotImportTimeoutMilliseconds);
            if (wait == WAIT_TIMEOUT)
            {
                result.timed_out = true;
                TerminateProcess(process.get(), WAIT_TIMEOUT);
                WaitForSingleObject(process.get(), 5'000);
                result.detail = L"PeZBOT import helper timed out";
                return result;
            }
            if (wait != WAIT_OBJECT_0)
            {
                result.detail = L"Waiting for the PeZBOT import helper failed";
                return result;
            }

            DWORD exit_code = ERROR_GEN_FAILURE;
            if (!GetExitCodeProcess(process.get(), &exit_code))
            {
                result.detail = L"Could not read the PeZBOT import helper exit code";
                return result;
            }
            result.exit_code = exit_code;
            result.detail = exit_code == 0
                ? L"PeZBOT import helper completed"
                : L"PeZBOT import helper rejected the archive (exit " +
                    std::to_wstring(exit_code) + L")";
            return result;
        }

        void ensure_normal_directory(const fs::path& path)
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES)
            {
                if (!is_normal_directory(path))
                {
                    throw std::runtime_error(
                        "Managed PeZBOT directory is not an ordinary directory");
                }
                return;
            }
            fs::create_directories(path);
            if (!is_normal_directory(path))
            {
                throw std::runtime_error(
                    "Managed PeZBOT directory failed post-creation validation");
            }
        }

        [[nodiscard]] fs::path create_pezbot_import_directory(
            const fs::path& mods_directory)
        {
            for (std::uint32_t attempt = 0; attempt < 100; ++attempt)
            {
                const auto leaf = L"mp_PeZBOTWAW.import-" +
                    std::to_wstring(GetCurrentProcessId()) + L"-" +
                    std::to_wstring(GetTickCount64()) + L"-" +
                    std::to_wstring(attempt);
                const auto candidate = (mods_directory / leaf).lexically_normal();
                if (candidate.parent_path() != mods_directory.lexically_normal())
                {
                    throw std::logic_error("PeZBOT import path escaped its mods directory");
                }
                if (CreateDirectoryW(candidate.c_str(), nullptr))
                {
                    if (!is_normal_directory(candidate))
                    {
                        throw std::runtime_error(
                            "PeZBOT import directory failed validation");
                    }
                    return candidate;
                }
                if (GetLastError() != ERROR_ALREADY_EXISTS)
                {
                    throw_last_error("CreateDirectoryW(PeZBOT import)");
                }
            }
            throw std::runtime_error("Could not allocate a unique PeZBOT import directory");
        }

        void remove_safe_pezbot_import_directory(
            const fs::path& candidate,
            const fs::path& mods_directory) noexcept
        {
            try
            {
                const auto normalized = fs::absolute(candidate).lexically_normal();
                const auto normalized_mods = fs::absolute(mods_directory).lexically_normal();
                const auto leaf = normalized.filename().wstring();
                if (normalized.parent_path() != normalized_mods ||
                    !leaf.starts_with(L"mp_PeZBOTWAW.import-") ||
                    !is_normal_directory(normalized))
                {
                    return;
                }
                for (const auto& entry : fs::recursive_directory_iterator(
                         normalized, fs::directory_options::none))
                {
                    const DWORD attributes = GetFileAttributesW(entry.path().c_str());
                    if (attributes == INVALID_FILE_ATTRIBUTES ||
                        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                        !is_same_or_descendant(entry.path(), normalized))
                    {
                        return;
                    }
                }
                std::error_code ignored;
                fs::remove_all(normalized, ignored);
            }
            catch (...)
            {
                // A failed safety proof intentionally leaves the unique temp
                // folder in place instead of risking removal outside it.
            }
        }
    }

    std::optional<fs::path> discover_pezbot_archive(
        const std::optional<fs::path>& explicit_archive,
        const std::optional<fs::path>& launcher_executable,
        const fs::path& downloads_directory)
    {
        if (explicit_archive)
        {
            return fs::absolute(*explicit_archive).lexically_normal();
        }

        if (launcher_executable)
        {
            const auto beside_launcher =
                (fs::absolute(*launcher_executable).lexically_normal().parent_path() /
                 kPezBotArchiveFilename)
                    .lexically_normal();
            if (path_exists_at_all(beside_launcher))
            {
                return beside_launcher;
            }
        }

        if (!downloads_directory.empty())
        {
            const auto in_downloads =
                (fs::absolute(downloads_directory).lexically_normal() /
                 kPezBotArchiveFilename)
                    .lexically_normal();
            if (path_exists_at_all(in_downloads))
            {
                return in_downloads;
            }
        }
        return std::nullopt;
    }

    PezBotArchiveValidation validate_pezbot_archive(
        const fs::path& archive,
        const PezBotArchiveIdentity& identity)
    {
        PezBotArchiveValidation validation;
        try
        {
            const auto normalized = fs::absolute(archive).lexically_normal();
            if (!is_normal_file(normalized))
            {
                validation.detail =
                    L"Archive is missing, not a regular file, or a reparse point: " +
                    normalized.wstring();
                return validation;
            }
            const auto actual_size = fs::file_size(normalized);
            if (actual_size != identity.size)
            {
                validation.detail = L"Archive size is " +
                    std::to_wstring(actual_size) + L" bytes; required " +
                    std::to_wstring(identity.size);
                return validation;
            }
            const auto actual_md5 = md5_file(normalized);
            if (actual_md5 != upper_ascii(identity.md5))
            {
                validation.detail = L"Archive MD5 is " + widen_ascii(actual_md5) +
                    L"; required " + widen_ascii(upper_ascii(identity.md5));
                return validation;
            }
            if (!is_hex_digest(identity.sha256, 64U))
            {
                validation.detail =
                    L"Required PeZBOT SHA-256 identity is malformed";
                return validation;
            }
            const auto actual_sha256 = sha256_file(normalized);
            if (actual_sha256 != upper_ascii(identity.sha256))
            {
                validation.detail = L"Archive SHA-256 is " +
                    widen_ascii(actual_sha256) + L"; required " +
                    widen_ascii(upper_ascii(identity.sha256));
                return validation;
            }
            validation.valid = true;
            validation.detail = L"Exact PeZBOT archive identity verified (" +
                std::to_wstring(actual_size) + L" bytes, MD5 " +
                widen_ascii(actual_md5) + L", SHA-256 " +
                widen_ascii(actual_sha256) + L")";
            return validation;
        }
        catch (const std::exception& error)
        {
            validation.detail = widen_ascii(error.what());
            return validation;
        }
    }

    fs::path pezbot_install_directory(const LaunchPlan& plan)
    {
        fs::path multiplayer_home;
        if (plan.executable_kind == ExecutableKind::mp)
        {
            multiplayer_home = plan.home_dir;
        }
        else
        {
            // A resolved default SP home is %LOCALAPPDATA%\WaWVR\home, so
            // sibling derivation preserves the legacy home-mp location. An
            // explicit --homepath instead stays inside that caller-owned
            // stage and never escapes back to LocalAppData.
            multiplayer_home = derive_multiplayer_home_dir(
                plan.home_dir,
                plan.handoff_mp_executable_identity.value_or(
                    paired_mp_identity(plan.executable_identity)));
        }
        return (fs::absolute(multiplayer_home).lexically_normal() /
                L"mods" / kPezBotModFolderName)
            .lexically_normal();
    }

    PezBotStatus inspect_optional_pezbot(
        const LaunchPlan& plan,
        const PezBotArchiveIdentity& identity)
    {
        PezBotStatus status;
        status.archive = plan.pezbot_archive;
        try
        {
            status.install_directory = pezbot_install_directory(plan);
            if (plan.bot_policy == BotPolicy::disabled)
            {
                status.state = PezBotState::unavailable;
                status.archive.reset();
                status.detail = L"Bots disabled in launcher settings";
                return status;
            }
            if (path_exists_at_all(status.install_directory))
            {
                std::wstring receipt_detail;
                if (verify_pezbot_receipt(
                        status.install_directory, identity, receipt_detail))
                {
                    status.state = PezBotState::installed;
                    status.detail = std::move(receipt_detail);
                    return status;
                }
                status.state = PezBotState::custom_folder_conflict;
                status.detail =
                    L"Existing mp_PeZBOTWAW folder was not changed: " +
                    receipt_detail;
                return status;
            }

            if (!plan.pezbot_archive)
            {
                status.state = PezBotState::unavailable;
                status.detail =
                    L"Bots unavailable: place PeZBOTWAW_005p.zip beside the "
                    L"WaWVR launcher or in Downloads, or pass --pezbot-archive";
                return status;
            }

            const auto archive_validation =
                validate_pezbot_archive(*plan.pezbot_archive, identity);
            if (!archive_validation.valid)
            {
                status.state = PezBotState::invalid_archive;
                status.detail = L"Bots unavailable: " + archive_validation.detail;
                return status;
            }

            const auto helper = pezbot_helper_path(plan, std::nullopt);
            if (!helper || !is_verified_pezbot_helper(*helper))
            {
                status.state = PezBotState::helper_unavailable;
                status.detail =
                    L"Bots unavailable: the exact WaWVR PeZBOT import helper is missing "
                    L"or changed";
                return status;
            }

            status.state = PezBotState::archive_ready;
            status.detail = archive_validation.detail +
                L"; safe import will run during preparation";
            return status;
        }
        catch (const std::exception& error)
        {
            status.state = PezBotState::import_failed;
            status.detail = L"Bots unavailable: " + widen_ascii(error.what());
            return status;
        }
    }

    PezBotStatus prepare_optional_pezbot(
        const LaunchPlan& plan,
        const PezBotArchiveIdentity& identity,
        const std::optional<fs::path>& helper_override)
    {
        auto status = inspect_optional_pezbot(plan, identity);
        if (status.state == PezBotState::installed ||
            status.state == PezBotState::custom_folder_conflict ||
            status.state == PezBotState::unavailable ||
            status.state == PezBotState::invalid_archive ||
            status.state == PezBotState::import_failed)
        {
            return status;
        }

        const auto helper = pezbot_helper_path(plan, helper_override);
        if (!helper || !is_verified_pezbot_helper(*helper))
        {
            status.state = PezBotState::helper_unavailable;
            status.detail =
                L"Bots unavailable: the exact WaWVR PeZBOT import helper is missing "
                L"or changed";
            return status;
        }
        if (!status.archive)
        {
            status.state = PezBotState::unavailable;
            status.detail = L"Bots unavailable: no archive was discovered";
            return status;
        }

        fs::path temporary;
        const auto mods_directory = status.install_directory.parent_path();
        try
        {
            const auto multiplayer_home = mods_directory.parent_path();
            if (multiplayer_home.empty() || mods_directory.filename() != L"mods" ||
                status.install_directory.filename() != kPezBotModFolderName)
            {
                throw std::logic_error("PeZBOT install path is not isolated under home-mp/mods");
            }
            ensure_normal_directory(multiplayer_home);
            ensure_normal_directory(mods_directory);
            if (path_exists_at_all(status.install_directory))
            {
                std::wstring receipt_detail;
                if (verify_pezbot_receipt(
                        status.install_directory, identity, receipt_detail))
                {
                    status.state = PezBotState::installed;
                    status.detail = std::move(receipt_detail);
                }
                else
                {
                    status.state = PezBotState::custom_folder_conflict;
                    status.detail =
                        L"Existing mp_PeZBOTWAW folder was not changed: " +
                        receipt_detail;
                }
                return status;
            }

            temporary = create_pezbot_import_directory(mods_directory);
            const auto process = run_pezbot_import_helper(
                *helper, *status.archive, temporary, identity);
            if (!process.started || process.timed_out || process.exit_code != 0)
            {
                status.state = PezBotState::import_failed;
                status.detail = L"Bots unavailable: " + process.detail;
                remove_safe_pezbot_import_directory(temporary, mods_directory);
                return status;
            }

            std::wstring receipt_detail;
            if (!verify_pezbot_receipt(temporary, identity, receipt_detail))
            {
                status.state = PezBotState::import_failed;
                status.detail =
                    L"Bots unavailable: extracted PeZBOT files failed validation: " +
                    receipt_detail;
                remove_safe_pezbot_import_directory(temporary, mods_directory);
                return status;
            }

            if (!MoveFileExW(
                    temporary.c_str(),
                    status.install_directory.c_str(),
                    MOVEFILE_WRITE_THROUGH))
            {
                remove_safe_pezbot_import_directory(temporary, mods_directory);
                if (verify_pezbot_receipt(
                        status.install_directory, identity, receipt_detail))
                {
                    status.state = PezBotState::installed;
                    status.detail = std::move(receipt_detail);
                    return status;
                }
                status.state = path_exists_at_all(status.install_directory)
                    ? PezBotState::custom_folder_conflict
                    : PezBotState::import_failed;
                status.detail =
                    L"PeZBOT import was not committed and no existing folder was changed";
                return status;
            }
            temporary.clear();

            if (!verify_pezbot_receipt(
                    status.install_directory, identity, receipt_detail))
            {
                status.state = PezBotState::import_failed;
                status.detail =
                    L"Bots unavailable: committed PeZBOT install failed final validation: " +
                    receipt_detail;
                return status;
            }
            status.state = PezBotState::installed;
            status.detail = std::move(receipt_detail);
            return status;
        }
        catch (const std::exception& error)
        {
            if (!temporary.empty())
            {
                remove_safe_pezbot_import_directory(temporary, mods_directory);
            }
            status.state = PezBotState::import_failed;
            status.detail = L"Bots unavailable: " + widen_ascii(error.what());
            return status;
        }
    }

    bool enforce_offline_multiplayer_listen_profile(const LaunchPlan& plan)
    {
        if (plan.launch_target != LaunchTarget::offline_multiplayer)
        {
            return false;
        }
        if (plan.executable_kind != ExecutableKind::mp)
        {
            throw std::logic_error(
                "Offline multiplayer profile policy requires the MP executable");
        }
        if (!plan.home_dir_is_launcher_managed)
        {
            // An explicit --homepath is caller-owned and may intentionally be
            // an existing profile tree. Command-line listen-server defaults
            // still apply, but the launcher must not rewrite arbitrary data.
            return false;
        }

        validate_write_boundaries(plan);
        const auto home = fs::absolute(plan.home_dir).lexically_normal();
        if (!is_normal_directory(home))
        {
            throw std::runtime_error(
                "Isolated multiplayer home must be an ordinary directory");
        }

        const auto players = (home / L"players").lexically_normal();
        const auto profiles = (players / L"profiles").lexically_normal();
        if (!is_same_or_descendant(players, home) ||
            !is_same_or_descendant(profiles, home) ||
            players.parent_path() != home ||
            profiles.parent_path() != players)
        {
            throw std::logic_error(
                "Multiplayer profile paths escaped the isolated home");
        }
        if (!normal_path_exists_or_throw(
                players, true, "Isolated multiplayer players directory") ||
            !normal_path_exists_or_throw(
                profiles, true, "Isolated multiplayer profiles directory"))
        {
            return false;
        }

        const auto active = (profiles / L"active.txt").lexically_normal();
        if (active.parent_path() != profiles ||
            !normal_path_exists_or_throw(
                active, false, "Isolated multiplayer active profile file"))
        {
            return false;
        }
        const auto profile_name = decode_active_profile_name(
            read_bounded_normal_file(
                active,
                kMaximumActiveProfileBytes,
                "ReadFile(isolated multiplayer active profile)"));
        const auto profile_directory =
            (profiles / fs::path(profile_name)).lexically_normal();
        if (profile_directory.parent_path() != profiles ||
            !is_same_or_descendant(profile_directory, profiles))
        {
            throw std::runtime_error(
                "Isolated multiplayer active profile escaped its root");
        }
        if (!normal_path_exists_or_throw(
                profile_directory,
                true,
                "Isolated multiplayer active profile directory"))
        {
            return false;
        }

        const auto config =
            (profile_directory / L"config_mp.cfg").lexically_normal();
        if (config.parent_path() != profile_directory ||
            !is_same_or_descendant(config, home) ||
            !normal_path_exists_or_throw(
                config, false, "Isolated multiplayer profile config"))
        {
            return false;
        }
        const auto original = read_bounded_normal_file(
            config,
            kMaximumMpProfileConfigBytes,
            "ReadFile(isolated multiplayer profile config)");
        const auto normalized = normalize_mp_profile_config(original);
        if (normalized.contents == original)
        {
            return false;
        }
        if (normalized.contents.size() > kMaximumMpProfileConfigBytes)
        {
            throw std::runtime_error(
                "Normalized multiplayer profile config exceeds its safe size limit");
        }

        replace_profile_config_atomically(config, normalized.contents);
        return true;
    }

    bool should_import_optional_pezbot(const LaunchPlan& plan) noexcept
    {
        // The no-argument stock shim consumes only its prepared config/home.
        // It may verify an existing receipt, but it must never discover or
        // import an archive after the exact SP parent has exited.
        return plan.bot_policy != BotPolicy::disabled &&
            !plan.stock_multiplayer_handoff;
    }

    const wchar_t* pezbot_state_name(const PezBotState state) noexcept
    {
        switch (state)
        {
        case PezBotState::unavailable:
            return L"unavailable";
        case PezBotState::archive_ready:
            return L"archive_ready";
        case PezBotState::installed:
            return L"installed";
        case PezBotState::custom_folder_conflict:
            return L"custom_folder_conflict";
        case PezBotState::invalid_archive:
            return L"invalid_archive";
        case PezBotState::helper_unavailable:
            return L"helper_unavailable";
        case PezBotState::import_failed:
            return L"import_failed";
        }
        return L"unknown";
    }

    SourceResolution parse_source_resolution(const std::wstring_view text)
    {
        const auto separator = text.find_first_of(L"xX");
        if (separator == std::wstring_view::npos || separator == 0 ||
            separator + 1 >= text.size() ||
            text.find_first_of(L"xX", separator + 1) != std::wstring_view::npos)
        {
            throw std::invalid_argument(
                "Resolution must use the exact WIDTHxHEIGHT format");
        }

        const auto parse_dimension = [](const std::wstring_view value) {
            std::uint64_t parsed = 0;
            for (const wchar_t character : value)
            {
                if (character < L'0' || character > L'9')
                {
                    throw std::invalid_argument(
                        "Resolution dimensions must contain decimal digits only");
                }
                const auto digit =
                    static_cast<std::uint64_t>(character - L'0');
                constexpr auto maximum =
                    static_cast<std::uint64_t>(
                        std::numeric_limits<std::uint32_t>::max());
                if (parsed > (maximum - digit) / 10U)
                {
                    throw std::invalid_argument("Resolution dimension is too large");
                }
                parsed = parsed * 10U + digit;
            }
            return static_cast<std::uint32_t>(parsed);
        };

        const SourceResolution resolution{
            parse_dimension(text.substr(0, separator)),
            parse_dimension(text.substr(separator + 1)),
        };
        if (resolution.width < kMinimumSourceWidth ||
            resolution.width > kMaximumSourceWidth ||
            resolution.height < kMinimumSourceHeight ||
            resolution.height > kMaximumSourceHeight)
        {
            throw std::invalid_argument(
                "Resolution must be within 640x480 and 6016x2688");
        }
        if ((resolution.width & 1U) != 0U)
        {
            throw std::invalid_argument(
                "Packed side-by-side source width must be even");
        }
        return resolution;
    }

    std::wstring source_resolution_text(const SourceResolution resolution)
    {
        return std::to_wstring(resolution.width) + L'x' +
            std::to_wstring(resolution.height);
    }

    const wchar_t* bot_policy_name(const BotPolicy policy) noexcept
    {
        switch (policy)
        {
        case BotPolicy::automatic:
            return L"automatic";
        case BotPolicy::enabled:
            return L"enabled";
        case BotPolicy::disabled:
            return L"disabled";
        }
        return L"invalid";
    }

    bool source_resolution_fits_desktop(
        const SourceResolution source,
        const SourceResolution desktop) noexcept
    {
        return source.width != 0 && source.height != 0 &&
            desktop.width != 0 && desktop.height != 0 &&
            source.width <= desktop.width && source.height <= desktop.height;
    }

    bool plan_source_resolution_is_desktop_compatible(
        const LaunchPlan& plan,
        const SourceResolution desktop) noexcept
    {
        const auto source = plan.source_resolution;
        const bool source_is_valid =
            source.width >= kMinimumSourceWidth &&
            source.width <= kMaximumSourceWidth &&
            source.height >= kMinimumSourceHeight &&
            source.height <= kMaximumSourceHeight &&
            (source.width & 1U) == 0U;
        const bool exact_sp_profile =
            plan.executable_kind == ExecutableKind::sp &&
            (plan.executable_identity ==
                 ExecutableIdentityId::plutonium_sp_1_7_1263 ||
             plan.executable_identity ==
                 ExecutableIdentityId::steam_sp_build_252004);
        return source_is_valid &&
            (exact_sp_profile || source_resolution_fits_desktop(source, desktop));
    }

    std::wstring desktop_source_resolution_compatibility_detail(
        const SourceResolution source,
        const SourceResolution desktop)
    {
        const auto requested = source_resolution_text(source);
        const auto current =
            desktop.width != 0 && desktop.height != 0
                ? source_resolution_text(desktop)
                : std::wstring(L"unavailable");
        std::wstring detail =
            L"Current Windows desktop resolution: " + current +
            L". Requested VR source resolution: " + requested + L".";

        if (desktop.width == 0 || desktop.height == 0)
        {
            return detail +
                L" World War VR could not determine the physical current "
                L"primary-display mode, so it cannot verify that the requested "
                L"window will fit. Check the Windows display resolution and try "
                L"again.";
        }

        if (source_resolution_fits_desktop(source, desktop))
        {
            return detail +
                L" The requested resolution fits within the current desktop.";
        }

        return detail +
            L" Call of Duty: World at War cannot create this window when it is "
            L"larger than the active primary desktop. Select a VR quality preset "
            L"that fits within the current desktop, "
            L"or set the Windows desktop resolution to at least " + requested +
            L" and try again. On supported NVIDIA GPUs and drivers, NVIDIA "
            L"Dynamic Super Resolution (DSR) or Deep Learning Dynamic Super "
            L"Resolution (DLDSR) is an optional way to make that higher desktop "
            L"resolution available.";
    }

    std::wstring plan_source_resolution_compatibility_detail(
        const LaunchPlan& plan,
        const SourceResolution desktop)
    {
        const bool exact_sp_profile =
            plan.executable_kind == ExecutableKind::sp &&
            (plan.executable_identity ==
                 ExecutableIdentityId::plutonium_sp_1_7_1263 ||
             plan.executable_identity ==
                 ExecutableIdentityId::steam_sp_build_252004);
        if (!exact_sp_profile)
        {
            return desktop_source_resolution_compatibility_detail(
                plan.source_resolution, desktop);
        }

        const auto current = desktop.width != 0 && desktop.height != 0
            ? source_resolution_text(desktop)
            : std::wstring(L"unavailable");
        return L"Current Windows desktop resolution: " + current +
            L". Requested VR source resolution: " +
            source_resolution_text(plan.source_resolution) +
            L". The exact validated SP renderer bootstrap removes the stock "
            L"desktop-size restriction and creates this packed source as an "
            L"off-screen D3D9Ex back buffer. This exception is not used for "
            L"multiplayer.";
    }

    std::wstring quote_windows_argument(const std::wstring& argument)
    {
        if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        {
            return argument;
        }

        std::wstring result = L"\"";
        std::size_t backslashes = 0;
        for (const auto character : argument)
        {
            if (character == L'\\')
            {
                ++backslashes;
                continue;
            }

            if (character == L'\"')
            {
                result.append(backslashes * 2 + 1, L'\\');
                result.push_back(L'\"');
            }
            else
            {
                result.append(backslashes, L'\\');
                result.push_back(character);
            }
            backslashes = 0;
        }
        result.append(backslashes * 2, L'\\');
        result.push_back(L'\"');
        return result;
    }

    std::wstring build_game_command_line(const LaunchPlan& plan)
    {
        std::wstring command_line = quote_windows_argument(plan.staged_exe.wstring()) +
            L" +set fs_basepath " + quote_windows_argument(plan.game_dir.wstring()) +
            L" +set fs_homepath " + quote_windows_argument(plan.home_dir.wstring());

        if (plan.executable_kind == ExecutableKind::mp)
        {
            // T4 MP resolves mod fastfiles through fs_localAppData even when
            // fs_homepath already finds the mod's IWD. Point both roots at the
            // same isolated MP home so mod.ff and PeZBOTWaW.iwd load together.
            command_line += L" +set fs_localAppData " +
                quote_windows_argument(plan.home_dir.wstring());
        }

        command_line +=
            std::wstring(L" +set com_introPlayed 1 +set r_fullscreen 0") +
            L" +set r_customMode " + source_resolution_text(plan.source_resolution) +
            // OpenXR owns presentation cadence. Do not inherit T4's common
            // 85-FPS profile cap or wait for the desktop window's vblank;
            // com_maxfps 0 is T4's registered uncapped value.
            L" +set com_maxfps 0 +set r_vsync 0"
            L" +set r_aaSamples 1 +set r_dof_enable 0"
            // Keep stock locomotion and viewmodel bob out of the headset.
            // Plain `set` makes this a process-local launch policy rather than
            // persisting zeroed comfort dvars into the isolated profile.
            L" +set bg_bobMax 0 +set cg_bobWeaponAmplitude 0"
            // The tracked barrel is the firearm reticle. Keep the stock
            // head-centred crosshair and name target out of gameplay while
            // preserving use prompts, grenade warnings, and the rest of HUD.
            L" +set cg_drawCrosshair 0 +set cg_drawCrosshairNames 0"
            L" +set cg_drawTurretCrosshair 0"
            L" +set cg_crosshairAlpha 0 +set cg_crosshairAlphaMin 0"
            // Flat WaW deliberately randomizes first-person tracers. In VR
            // each shot needs a visible controller-to-impact trajectory, so
            // request the exact registered local-player dvar at full chance.
            L" +set cg_firstPersonTracerChance 1"
            // T4 updates its FX camera before the late same-frame stereo eye
            // transforms. Retain stock element-spawn culling so distant map
            // effects cannot repeat unchecked into the fixed 1024-effect
            // pool, but disable
            // element draw culling so already-spawned HMD-visible effects are
            // not discarded against the stale stock/body camera.
            L" +set fx_cull_elem_spawn 1 +set fx_cull_elem_draw 0"
            // Auto-melee writes a target yaw/distance into the command and
            // visibly drags the legacy camera. The VR input hook also clears
            // those serialized fields while the knife button is held.
            L" +set aim_automelee_enabled 0"
            // Stock melee expects desktop timing and forces the player much
            // closer than a comfortable physical VR swing. Preserve the
            // native knife trace/damage path but extend its registered range
            // from the stock 64 units to a conservative 96 units.
            L" +set player_meleeRange 96"
            // Stock depth-of-field focus blur is tied to the legacy camera
            // and becomes an uncomfortable full-headset blur during ADS.
            // Disable it for both gameplay and frontend launch targets.
            // VR frame submission is serviced on WinMain immediately after
            // Com_Frame. Keep T4's legacy D3D9 backend on that same thread so
            // Present/readback cannot overlap an SMP render worker or touch a
            // non-multithreaded device from two threads.
            L" +set r_smp_backend 0 +set r_multiGpu 0"
            // Same-frame stereo must force SHADOW_NONE. T4's
            // earlier-eye shadow lists are invalidated while the later eye
            // is generated, so keep both shadow-map and shadow-cookie paths
            // disabled until their complete list-rebase policy is ported.
            L" +set sm_enable 0 +set sc_enable 0"
            L" +set sm_sunEnable 0 +set sm_spotEnable 0"
            // The exact stereo backend validates and rebases T4's auxiliary
            // point-light lists. The backend validates and rebases four exact
            // point-light partitions, so retain up to four authored lights
            // for power-ups and Wunderwaffe effects while keeping the
            // independently unsafe spotlight-shadow path disabled.
            L" +set r_dlightLimit 4 +set r_spotLightShadows 0";

        // Launch-only discriminator for stereo particles. Do not disable the
        // FloatZ allocation itself: this selects T4's non-z-feather material
        // techniques so a physical headset test can isolate whether the
        // packed right-eye FloatZ lookup is at fault.
        if (const auto disable_z_feather =
                environment_value(L"WAWVR_DISABLE_ZFEATHER");
            disable_z_feather && *disable_z_feather == L"1")
        {
            command_line += L" +set r_zFeather 0";
        }

        if (plan.executable_kind == ExecutableKind::sp)
        {
            // WaW gates Mission Select on the archived mis_01 high-water mark;
            // COD4's mis_cheat flag does not unlock this menu. Apply WaW's
            // stock unlock-all value on every SP launch. The injected runtime
            // repeats it after profile initialization, while leaving saves and
            // the separate mission-difficulty record untouched.
            command_line += L" +seta mis_01 50";
        }

        if (plan.launch_target == LaunchTarget::offline_multiplayer)
        {
            // Keep the standalone MP frontend on a local listen-server path.
            // `ui_netSource 0` selects LAN/local browsing and `onlinegame 0`
            // keeps stats/matchmaking disabled. These defaults do not create a
            // firewall rule and do not attempt any public-server connection.
            command_line +=
                L" +set onlinegame 0 +set ui_netSource 0"
                L" +set dedicated 0"
                L" +set cl_punkbuster 0 +set sv_punkbuster 0";
            if (plan.bot_policy != BotPolicy::disabled &&
                plan.pezbot_enabled)
            {
                // PeZBOT is activated only after its exact archive import and
                // receipt have been revalidated. Keep map and game-mode choice
                // in the stock frontend; these are conservative local defaults.
                // PeZBOT consumes and clears svr_pezbots after one map. The
                // WaWVR marker authorizes one reassignment per later server:
                // either in a disconnected frontend or on the first loading
                // frame after an active server exits without visiting state 0.
                command_line +=
                    L" +set fs_game mods/mp_PeZBOTWAW"
                    L" +exec pezbot.cfg"
                    L" +set svr_pezbots 9"
                    L" +set wawvr_pezbot_autofill 9"
                    L" +set svr_pezbots_weapons authentic"
                    L" +set svr_pezbots_useperks 0"
                    L" +set svr_pezbots_skill 0.7";
            }
            else
            {
                // Clear archived activation from an earlier bot-enabled run.
                // This also guarantees that enabled-without-a-verified-install
                // degrades to base offline multiplayer rather than loading a
                // stale or modified bot folder.
                command_line +=
                    L" +set fs_game \"\""
                    L" +set svr_pezbots 0"
                    L" +set wawvr_pezbot_autofill 0";
            }
            // Keep the UI selector last as command-line defense in depth.
            // launch_game also normalizes the archived value in only the
            // isolated active profile immediately before process creation.
            command_line += L" +set ui_dedicated 0";
        }

        switch (plan.launch_target)
        {
        case LaunchTarget::nacht:
            command_line += L" +devmap nazi_zombie_prototype";
            break;
        case LaunchTarget::der_riese:
            command_line += L" +devmap nazi_zombie_factory";
            break;
        case LaunchTarget::campaign_ber1:
            command_line += L" +devmap ber1";
            break;
        case LaunchTarget::campaign_pel2_airfield:
            // Hidden, bounded campaign test target. The stock pel2 script
            // consumes `start` during spdevmap and jumps to its authored
            // airfield section, avoiding a replay from the mission opening.
            // monkeytoy 0 enables the stock developer map command without
            // persisting developer state in the isolated profile.
            command_line +=
                L" +set monkeytoy 0 +set start airfield +spdevmap pel2";
            break;
        case LaunchTarget::frontend_menu:
            break;
        case LaunchTarget::offline_multiplayer:
            break;
        }
        return command_line;
    }

    bool is_same_or_descendant(const fs::path& candidate, const fs::path& parent)
    {
        auto candidate_text = lower_path_string(candidate);
        auto parent_text = lower_path_string(parent);
        while (parent_text.size() > 3 &&
               (parent_text.back() == L'\\' || parent_text.back() == L'/'))
        {
            parent_text.pop_back();
        }

        if (candidate_text == parent_text)
        {
            return true;
        }
        if (candidate_text.size() <= parent_text.size() ||
            candidate_text.compare(0, parent_text.size(), parent_text) != 0)
        {
            return false;
        }
        const auto separator = candidate_text[parent_text.size()];
        return separator == L'\\' || separator == L'/';
    }

    bool is_same_path(const fs::path& left, const fs::path& right)
    {
        return !left.empty() && !right.empty() &&
            is_same_or_descendant(left, right) &&
            is_same_or_descendant(right, left);
    }

    bool is_safe_runtime_link_name(const fs::path& relative_name)
    {
        if (relative_name.empty() || relative_name.is_absolute() ||
            relative_name.has_root_name() || relative_name.has_root_directory() ||
            relative_name == L"." || relative_name == L"..")
        {
            return false;
        }

        auto component_count = std::size_t{0};
        for (const auto& component : relative_name)
        {
            if (component.empty() || component == L"." || component == L"..")
            {
                return false;
            }
            ++component_count;
        }
        return component_count == 1 && relative_name.filename() == relative_name;
    }

    namespace
    {
        constexpr std::string_view kMultiplayerHandoffMagic =
            "WAWVR_MULTIPLAYER_HANDOFF_V3";
        constexpr std::string_view kLegacyV2MultiplayerHandoffMagic =
            "WAWVR_MULTIPLAYER_HANDOFF_V2";
        constexpr std::string_view kLegacyV1MultiplayerHandoffMagic =
            "WAWVR_MULTIPLAYER_HANDOFF_V1";
        constexpr std::uintmax_t kMaximumMultiplayerHandoffConfigBytes =
            32U * 1024U;

        [[nodiscard]] bool is_safe_absolute_handoff_path(
            const fs::path& path) noexcept
        {
            try
            {
                if (path.empty() || !path.is_absolute() ||
                    !path.has_root_name() || !path.has_root_directory() ||
                    path != path.lexically_normal())
                {
                    return false;
                }

                const auto text = path.wstring();
                const auto root_name = path.root_name().wstring();
                if (root_name.size() != 2U ||
                    std::iswalpha(root_name[0]) == 0 ||
                    root_name[1] != L':')
                {
                    // Managed runtimes are local-drive stages. Reject ordinary
                    // UNC roots as well as the device/extended prefixes below.
                    return false;
                }
                if (text.empty() || text.starts_with(L"\\\\?\\") ||
                    text.starts_with(L"\\\\.\\"))
                {
                    return false;
                }
                for (std::size_t index = 0; index < text.size(); ++index)
                {
                    const auto ch = text[index];
                    if (ch < 0x20 || ch == L'"' || ch == L'*' || ch == L'?' ||
                        ch == L'<' || ch == L'>' || ch == L'|')
                    {
                        return false;
                    }
                    if (ch == L':' &&
                        !(index == 1 && std::iswalpha(text[0]) != 0))
                    {
                        return false;
                    }
                }
                for (const auto& component : path)
                {
                    if (component == L"." || component == L"..")
                    {
                        return false;
                    }
                }
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        void require_safe_absolute_handoff_path(
            const fs::path& path,
            const std::string_view label)
        {
            if (!is_safe_absolute_handoff_path(path))
            {
                throw std::invalid_argument(
                    std::string(label) +
                    " must be a canonical absolute Windows path");
            }
        }

        [[nodiscard]] std::string path_utf8(const fs::path& path)
        {
            const auto value = path.wstring();
            if (value.empty())
            {
                throw std::invalid_argument("Handoff path cannot be empty");
            }
            const auto required = WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (required <= 0)
            {
                throw std::invalid_argument("Handoff path is not valid Unicode");
            }
            std::string result(static_cast<std::size_t>(required), '\0');
            if (WideCharToMultiByte(
                    CP_UTF8,
                    WC_ERR_INVALID_CHARS,
                    value.data(),
                    static_cast<int>(value.size()),
                    result.data(),
                    required,
                    nullptr,
                    nullptr) != required)
            {
                throw std::invalid_argument("Handoff path UTF-8 conversion failed");
            }
            return result;
        }

        [[nodiscard]] std::wstring wide_from_utf8(const std::string_view value)
        {
            if (value.empty() || value.size() >
                    static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            {
                throw std::invalid_argument("Handoff path encoding is empty or too large");
            }
            const auto required = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            if (required <= 0)
            {
                throw std::invalid_argument("Handoff path is not valid UTF-8");
            }
            std::wstring result(static_cast<std::size_t>(required), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    value.data(),
                    static_cast<int>(value.size()),
                    result.data(),
                    required) != required)
            {
                throw std::invalid_argument("Handoff path UTF-8 decoding failed");
            }
            return result;
        }

        [[nodiscard]] std::string hex_encode_path(const fs::path& path)
        {
            constexpr char digits[] = "0123456789ABCDEF";
            const auto bytes = path_utf8(path);
            std::string encoded;
            encoded.reserve(bytes.size() * 2U);
            for (const auto ch : bytes)
            {
                const auto byte = static_cast<unsigned char>(ch);
                encoded.push_back(digits[byte >> 4U]);
                encoded.push_back(digits[byte & 0x0FU]);
            }
            return encoded;
        }

        [[nodiscard]] fs::path hex_decode_path(const std::string_view encoded)
        {
            const auto nibble = [](const char ch) -> int {
                if (ch >= '0' && ch <= '9')
                {
                    return ch - '0';
                }
                if (ch >= 'A' && ch <= 'F')
                {
                    return ch - 'A' + 10;
                }
                return -1;
            };
            if (encoded.empty() || encoded.size() % 2U != 0U)
            {
                throw std::invalid_argument("Handoff path hex encoding is invalid");
            }
            std::string bytes;
            bytes.reserve(encoded.size() / 2U);
            for (std::size_t index = 0; index < encoded.size(); index += 2U)
            {
                const auto high = nibble(encoded[index]);
                const auto low = nibble(encoded[index + 1U]);
                if (high < 0 || low < 0)
                {
                    throw std::invalid_argument(
                        "Handoff path hex encoding must be uppercase hexadecimal");
                }
                bytes.push_back(static_cast<char>((high << 4) | low));
            }
            return fs::path(wide_from_utf8(bytes));
        }

        [[nodiscard]] std::string_view require_handoff_field(
            const std::string_view line,
            const std::string_view key)
        {
            if (!line.starts_with(key) || line.size() == key.size())
            {
                throw std::invalid_argument("Multiplayer handoff field is missing or malformed");
            }
            return line.substr(key.size());
        }

        [[nodiscard]] ExecutableIdentityId parse_executable_identity_id(
            const std::string_view text)
        {
            for (const auto* identity : kExecutableIdentities)
            {
                if (text == identity->stable_id)
                {
                    return identity->id;
                }
            }
            throw std::invalid_argument(
                "Multiplayer handoff executable identity is unknown");
        }

        [[nodiscard]] std::string_view bot_policy_stable_id(
            const BotPolicy policy)
        {
            switch (policy)
            {
            case BotPolicy::automatic:
                return "automatic";
            case BotPolicy::enabled:
                return "enabled";
            case BotPolicy::disabled:
                return "disabled";
            }
            throw std::invalid_argument("Bot policy is invalid");
        }

        [[nodiscard]] BotPolicy parse_bot_policy_id(
            const std::string_view text)
        {
            if (text == "automatic")
            {
                return BotPolicy::automatic;
            }
            if (text == "enabled")
            {
                return BotPolicy::enabled;
            }
            if (text == "disabled")
            {
                return BotPolicy::disabled;
            }
            throw std::invalid_argument(
                "Multiplayer handoff bot policy is unknown");
        }

        void validate_multiplayer_handoff_config_record(
            const MultiplayerHandoffConfig& config)
        {
            static_cast<void>(bot_policy_stable_id(config.bot_policy));
            require_safe_absolute_handoff_path(config.game_dir, "game_dir");
            require_safe_absolute_handoff_path(
                config.sp_runtime_dir, "sp_runtime_dir");
            require_safe_absolute_handoff_path(config.sp_home_dir, "sp_home_dir");
            require_safe_absolute_handoff_path(
                config.mp_source_exe, "mp_source_exe");
            require_safe_absolute_handoff_path(
                config.mp_runtime_dir, "mp_runtime_dir");
            require_safe_absolute_handoff_path(config.mp_home_dir, "mp_home_dir");

            if (executable_kind_for_identity(config.sp_executable_identity) !=
                    ExecutableKind::sp ||
                executable_kind_for_identity(config.mp_executable_identity) !=
                    ExecutableKind::mp)
            {
                throw std::invalid_argument(
                    "Multiplayer handoff executable identities have the wrong kind");
            }

            if (!is_same_path(
                    config.mp_runtime_dir,
                    derive_multiplayer_runtime_dir(
                        config.sp_runtime_dir,
                        config.mp_executable_identity)) ||
                !is_same_path(
                    config.mp_home_dir,
                    derive_multiplayer_home_dir(
                        config.sp_home_dir,
                        config.mp_executable_identity)))
            {
                throw std::invalid_argument(
                    "MP runtime/home paths are not derived from the SP plan");
            }
            const std::array managed_directories = {
                config.sp_runtime_dir,
                config.sp_home_dir,
                config.mp_runtime_dir,
                config.mp_home_dir,
            };
            if (std::any_of(
                    managed_directories.begin(),
                    managed_directories.end(),
                    [&](const fs::path& path) {
                        return is_same_or_descendant(path, config.game_dir);
                    }))
            {
                throw std::invalid_argument(
                    "Multiplayer handoff paths violate runtime isolation");
            }
            for (std::size_t left = 0; left < managed_directories.size(); ++left)
            {
                for (std::size_t right = left + 1U;
                     right < managed_directories.size();
                     ++right)
                {
                    if (is_same_or_descendant(
                            managed_directories[left], managed_directories[right]) ||
                        is_same_or_descendant(
                            managed_directories[right], managed_directories[left]))
                    {
                        throw std::invalid_argument(
                            "SP/MP runtime and home directories must not overlap");
                    }
                }
            }

            LaunchPlan mp_plan;
            mp_plan.executable_kind = ExecutableKind::mp;
            mp_plan.executable_identity = config.mp_executable_identity;
            mp_plan.game_dir = config.game_dir;
            mp_plan.source_exe = config.mp_source_exe;
            mp_plan.runtime_dir = config.mp_runtime_dir;
            mp_plan.home_dir = config.mp_home_dir;
            mp_plan.staged_exe =
                config.mp_runtime_dir /
                executable_identity(config.mp_executable_identity).staged_filename;
            validate_write_boundaries(mp_plan);

            const auto resolution_text =
                source_resolution_text(config.source_resolution);
            if (parse_source_resolution(resolution_text) != config.source_resolution ||
                config.source_resolution.width > kMaximumLegacySourceWidth ||
                config.source_resolution.height > kMaximumLegacySourceHeight)
            {
                throw std::invalid_argument(
                    "Multiplayer handoff source resolution is invalid or SP-only");
            }
        }
    }

    bool is_multiplayer_handoff_invocation(
        const fs::path& executable_path) noexcept
    {
        try
        {
            const auto filename = executable_path.filename().wstring();
            return CompareStringOrdinal(
                       filename.c_str(),
                       static_cast<int>(filename.size()),
                       L"CoDWaWmp.exe",
                       -1,
                       TRUE) == CSTR_EQUAL;
        }
        catch (...)
        {
            return false;
        }
    }

    fs::path derive_multiplayer_runtime_dir(
        const fs::path& sp_runtime_dir,
        const ExecutableIdentityId mp_identity)
    {
        if (executable_kind_for_identity(mp_identity) != ExecutableKind::mp)
        {
            throw std::invalid_argument(
                "MP runtime derivation requires an MP executable identity");
        }
        require_safe_absolute_handoff_path(sp_runtime_dir, "sp_runtime_dir");
        const auto parent = sp_runtime_dir.parent_path();
        if (parent.empty() || is_same_path(parent, sp_runtime_dir))
        {
            throw std::invalid_argument(
                "SP runtime must have a parent for MP sibling derivation");
        }
        const auto result =
            (parent / executable_identity(mp_identity).runtime_leaf)
                .lexically_normal();
        require_safe_absolute_handoff_path(result, "mp_runtime_dir");
        if (is_same_path(result, sp_runtime_dir))
        {
            throw std::invalid_argument("SP and MP runtime paths must differ");
        }
        return result;
    }

    fs::path derive_multiplayer_home_dir(
        const fs::path& sp_home_dir,
        const ExecutableIdentityId mp_identity)
    {
        if (executable_kind_for_identity(mp_identity) != ExecutableKind::mp)
        {
            throw std::invalid_argument(
                "MP home derivation requires an MP executable identity");
        }
        require_safe_absolute_handoff_path(sp_home_dir, "sp_home_dir");
        const auto parent = sp_home_dir.parent_path();
        if (parent.empty() || is_same_path(parent, sp_home_dir))
        {
            throw std::invalid_argument(
                "SP home must have a parent for MP sibling derivation");
        }
        const auto result =
            (parent / executable_identity(mp_identity).home_leaf)
                .lexically_normal();
        require_safe_absolute_handoff_path(result, "mp_home_dir");
        if (is_same_path(result, sp_home_dir))
        {
            throw std::invalid_argument("SP and MP home paths must differ");
        }
        return result;
    }

    MultiplayerHandoffConfig derive_multiplayer_handoff_config(
        const LaunchPlan& sp_plan)
    {
        if (sp_plan.executable_kind != ExecutableKind::sp ||
            executable_kind_for_identity(sp_plan.executable_identity) !=
                ExecutableKind::sp ||
            !sp_plan.handoff_mp_source_exe ||
            !sp_plan.handoff_mp_executable_identity)
        {
            throw std::invalid_argument(
                "A resolved SP plan with an exact MP source is required");
        }
        MultiplayerHandoffConfig config;
        config.game_dir = sp_plan.game_dir;
        config.sp_runtime_dir = sp_plan.runtime_dir;
        config.sp_home_dir = sp_plan.home_dir;
        config.sp_executable_identity = sp_plan.executable_identity;
        config.mp_source_exe = *sp_plan.handoff_mp_source_exe;
        config.mp_executable_identity =
            *sp_plan.handoff_mp_executable_identity;
        config.mp_runtime_dir =
            derive_multiplayer_runtime_dir(
                sp_plan.runtime_dir,
                config.mp_executable_identity);
        config.mp_home_dir = derive_multiplayer_home_dir(
            sp_plan.home_dir,
            config.mp_executable_identity);
        config.source_resolution =
            sp_plan.source_resolution.width > kDefaultSourceResolution.width ||
                sp_plan.source_resolution.height > kDefaultSourceResolution.height
            ? kDefaultSourceResolution
            : sp_plan.source_resolution;
        config.bot_policy = sp_plan.bot_policy;
        validate_multiplayer_handoff_config_record(config);
        return config;
    }

    fs::path multiplayer_handoff_config_path(
        const fs::path& handoff_executable)
    {
        require_safe_absolute_handoff_path(
            handoff_executable, "handoff_executable");
        if (!is_multiplayer_handoff_invocation(handoff_executable))
        {
            throw std::invalid_argument(
                "Multiplayer handoff configuration requires the exact shim basename");
        }
        const auto result = (handoff_executable.parent_path() /
            kMultiplayerHandoffConfigFilename).lexically_normal();
        if (result.parent_path() != handoff_executable.parent_path() ||
            !is_same_or_descendant(result, handoff_executable.parent_path()))
        {
            throw std::invalid_argument(
                "Multiplayer handoff configuration path escaped the SP runtime");
        }
        return result;
    }

    std::string serialize_multiplayer_handoff_config(
        const MultiplayerHandoffConfig& config)
    {
        validate_multiplayer_handoff_config_record(config);
        std::ostringstream output;
        output << kMultiplayerHandoffMagic << '\n'
               << "game_dir=" << hex_encode_path(config.game_dir) << '\n'
               << "sp_runtime_dir=" << hex_encode_path(config.sp_runtime_dir) << '\n'
               << "sp_home_dir=" << hex_encode_path(config.sp_home_dir) << '\n'
               << "sp_executable_identity="
               << executable_identity_stable_id(config.sp_executable_identity)
               << '\n'
               << "mp_source_exe=" << hex_encode_path(config.mp_source_exe) << '\n'
               << "mp_runtime_dir=" << hex_encode_path(config.mp_runtime_dir) << '\n'
               << "mp_home_dir=" << hex_encode_path(config.mp_home_dir) << '\n'
               << "mp_executable_identity="
               << executable_identity_stable_id(config.mp_executable_identity)
               << '\n'
               << "source_resolution="
               << config.source_resolution.width << 'x'
               << config.source_resolution.height << '\n'
               << "bot_policy=" << bot_policy_stable_id(config.bot_policy)
               << '\n';
        const auto result = output.str();
        if (result.size() > kMaximumMultiplayerHandoffConfigBytes)
        {
            throw std::invalid_argument(
                "Multiplayer handoff configuration is too large");
        }
        return result;
    }

    MultiplayerHandoffConfig parse_multiplayer_handoff_config(
        const std::string_view text,
        const fs::path& handoff_executable)
    {
        if (text.empty() || text.size() > kMaximumMultiplayerHandoffConfigBytes ||
            text.back() != '\n' || text.find('\r') != text.npos ||
            text.find('\0') != text.npos)
        {
            throw std::invalid_argument(
                "Multiplayer handoff configuration framing is invalid");
        }

        std::vector<std::string_view> lines;
        std::size_t begin = 0;
        while (begin < text.size())
        {
            const auto end = text.find('\n', begin);
            if (end == text.npos)
            {
                throw std::invalid_argument(
                    "Multiplayer handoff configuration is not line terminated");
            }
            lines.push_back(text.substr(begin, end - begin));
            begin = end + 1U;
        }
        const bool legacy_v1 =
            lines.size() == 8U && lines[0] == kLegacyV1MultiplayerHandoffMagic;
        const bool legacy_v2 =
            lines.size() == 10U && lines[0] == kLegacyV2MultiplayerHandoffMagic;
        const bool exact_v3 =
            lines.size() == 11U && lines[0] == kMultiplayerHandoffMagic;
        if (!legacy_v1 && !legacy_v2 && !exact_v3)
        {
            throw std::invalid_argument(
                "Multiplayer handoff configuration version or field count is invalid");
        }

        MultiplayerHandoffConfig config;
        config.game_dir = hex_decode_path(
            require_handoff_field(lines[1], "game_dir="));
        config.sp_runtime_dir = hex_decode_path(
            require_handoff_field(lines[2], "sp_runtime_dir="));
        config.sp_home_dir = hex_decode_path(
            require_handoff_field(lines[3], "sp_home_dir="));
        std::string_view resolution;
        if (legacy_v1)
        {
            config.sp_executable_identity =
                ExecutableIdentityId::plutonium_sp_1_7_1263;
            config.mp_source_exe = hex_decode_path(
                require_handoff_field(lines[4], "mp_source_exe="));
            config.mp_runtime_dir = hex_decode_path(
                require_handoff_field(lines[5], "mp_runtime_dir="));
            config.mp_home_dir = hex_decode_path(
                require_handoff_field(lines[6], "mp_home_dir="));
            config.mp_executable_identity =
                ExecutableIdentityId::plutonium_mp_1_7_1263;
            resolution = require_handoff_field(
                lines[7], "source_resolution=");
        }
        else
        {
            config.sp_executable_identity = parse_executable_identity_id(
                require_handoff_field(lines[4], "sp_executable_identity="));
            config.mp_source_exe = hex_decode_path(
                require_handoff_field(lines[5], "mp_source_exe="));
            config.mp_runtime_dir = hex_decode_path(
                require_handoff_field(lines[6], "mp_runtime_dir="));
            config.mp_home_dir = hex_decode_path(
                require_handoff_field(lines[7], "mp_home_dir="));
            config.mp_executable_identity = parse_executable_identity_id(
                require_handoff_field(lines[8], "mp_executable_identity="));
            resolution = require_handoff_field(
                lines[9], "source_resolution=");
        }
        config.source_resolution = parse_source_resolution(
            std::wstring(resolution.begin(), resolution.end()));
        config.bot_policy = exact_v3
            ? parse_bot_policy_id(
                  require_handoff_field(lines[10], "bot_policy="))
            : BotPolicy::automatic;
        validate_multiplayer_handoff_config_record(config);

        const auto normalized_handoff =
            fs::absolute(handoff_executable).lexically_normal();
        multiplayer_handoff_config_path(normalized_handoff);
        const auto expected_handoff =
            (config.sp_runtime_dir / L"CoDWaWmp.exe").lexically_normal();
        if (!is_same_path(normalized_handoff, expected_handoff))
        {
            throw std::invalid_argument(
                "Multiplayer handoff configuration belongs to a different SP runtime");
        }
        if (exact_v3 && serialize_multiplayer_handoff_config(config) != text)
        {
            throw std::invalid_argument(
                "Multiplayer handoff configuration is not canonical");
        }
        return config;
    }

    MultiplayerHandoffConfig read_multiplayer_handoff_config(
        const fs::path& handoff_executable)
    {
        const auto normalized_handoff =
            fs::absolute(handoff_executable).lexically_normal();
        const auto path = multiplayer_handoff_config_path(normalized_handoff);
        if (!is_normal_directory(normalized_handoff.parent_path()) ||
            !is_normal_file(path))
        {
            throw std::runtime_error(
                "Multiplayer handoff configuration is absent or unsafe");
        }
        std::error_code size_error;
        const auto size = fs::file_size(path, size_error);
        if (size_error || size == 0 ||
            size > kMaximumMultiplayerHandoffConfigBytes)
        {
            throw std::runtime_error(
                "Multiplayer handoff configuration size is invalid");
        }
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error(
                "Multiplayer handoff configuration could not be opened");
        }
        std::string contents(static_cast<std::size_t>(size), '\0');
        input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (input.gcount() != static_cast<std::streamsize>(contents.size()) ||
            input.peek() != std::char_traits<char>::eof())
        {
            throw std::runtime_error(
                "Multiplayer handoff configuration could not be read exactly");
        }
        return parse_multiplayer_handoff_config(contents, normalized_handoff);
    }

    bool is_expected_handoff_parent_path(
        const fs::path& parent_executable,
        const fs::path& handoff_executable)
    {
        if (parent_executable.empty() || handoff_executable.empty())
        {
            return false;
        }
        const auto expected =
            handoff_executable.parent_path() / kSpExecutableIdentity.staged_filename;
        std::error_code error;
        if (fs::exists(parent_executable, error) && !error &&
            fs::exists(expected, error) && !error)
        {
            return fs::equivalent(parent_executable, expected, error) && !error;
        }
        return is_same_path(parent_executable, expected);
    }

    HandoffParentWaitResult classify_handoff_parent_wait(
        const bool exact_parent,
        const std::uint32_t wait_result) noexcept
    {
        if (!exact_parent)
        {
            return HandoffParentWaitResult::rejected_parent;
        }
        if (wait_result == WAIT_OBJECT_0)
        {
            return HandoffParentWaitResult::ready;
        }
        if (wait_result == WAIT_TIMEOUT)
        {
            return HandoffParentWaitResult::timed_out;
        }
        return HandoffParentWaitResult::wait_failed;
    }

    HandoffParentWaitResult wait_for_multiplayer_handoff_parent(
        const fs::path& handoff_executable) noexcept
    {
        try
        {
            UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
            if (!snapshot.valid())
            {
                return HandoffParentWaitResult::wait_failed;
            }

            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            DWORD parent_process_id = 0;
            if (Process32FirstW(snapshot.get(), &entry))
            {
                do
                {
                    if (entry.th32ProcessID == GetCurrentProcessId())
                    {
                        parent_process_id = entry.th32ParentProcessID;
                        break;
                    }
                } while (Process32NextW(snapshot.get(), &entry));
            }
            if (parent_process_id == 0)
            {
                return HandoffParentWaitResult::rejected_parent;
            }

            UniqueHandle parent(OpenProcess(
                SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                parent_process_id));
            if (!parent.valid())
            {
                return HandoffParentWaitResult::rejected_parent;
            }

            std::wstring parent_image(32'768, L'\0');
            DWORD parent_image_size = static_cast<DWORD>(parent_image.size());
            if (!QueryFullProcessImageNameW(
                    parent.get(), 0, parent_image.data(), &parent_image_size) ||
                parent_image_size == 0)
            {
                return HandoffParentWaitResult::rejected_parent;
            }
            parent_image.resize(parent_image_size);
            const fs::path parent_path(parent_image);
            const auto config =
                read_multiplayer_handoff_config(handoff_executable);
            const DWORD parent_attributes = GetFileAttributesW(parent_path.c_str());
            std::optional<ExecutableIdentityId> parent_identity;
            if (parent_attributes != INVALID_FILE_ATTRIBUTES &&
                (parent_attributes & (FILE_ATTRIBUTE_DIRECTORY |
                                      FILE_ATTRIBUTE_REPARSE_POINT)) == 0)
            {
                parent_identity = executable_identity_from_sha256(
                    ExecutableKind::sp,
                    sha256_file(parent_path));
            }
            const bool exact_parent =
                is_expected_handoff_parent_path(parent_path, handoff_executable) &&
                parent_identity &&
                *parent_identity == config.sp_executable_identity;
            if (!exact_parent)
            {
                return HandoffParentWaitResult::rejected_parent;
            }

            return classify_handoff_parent_wait(
                true,
                WaitForSingleObject(parent.get(), kHandoffParentWaitMilliseconds));
        }
        catch (...)
        {
            return HandoffParentWaitResult::wait_failed;
        }
    }

    const char* handoff_parent_wait_result_name(
        const HandoffParentWaitResult result) noexcept
    {
        switch (result)
        {
        case HandoffParentWaitResult::ready: return "ready";
        case HandoffParentWaitResult::rejected_parent: return "rejected-parent";
        case HandoffParentWaitResult::timed_out: return "timed-out";
        case HandoffParentWaitResult::wait_failed: return "wait-failed";
        }
        return "unknown";
    }

    bool is_exact_waw_safe_mode_dialog(
        const SafeModeDialogIdentity& identity,
        const std::uint32_t expected_process_id)
    {
        return expected_process_id != 0 &&
            identity.owner_process_id == expected_process_id &&
            identity.window_class == L"#32770" &&
            identity.title == L"Run In Safe Mode?" &&
            identity.no_button_id == IDNO &&
            identity.no_button_class == L"Button" &&
            identity.body.find(
                L"It appears that Call of Duty: World at War did not quit properly") !=
                std::wstring::npos &&
            identity.body.find(L"Do you want to run the game in safe mode?") !=
                std::wstring::npos;
    }

    bool is_exact_waw_optimal_settings_dialog(
        const SafeModeDialogIdentity& identity,
        const std::uint32_t expected_process_id)
    {
        return expected_process_id != 0 &&
            identity.owner_process_id == expected_process_id &&
            identity.window_class == L"#32770" &&
            identity.title == L"Set Optimal Settings?" &&
            identity.no_button_id == IDNO &&
            identity.no_button_class == L"Button" &&
            identity.body.find(
                L"Your computer appears to have changed since the last time you ran "
                L"Call of Duty: World at War") != std::wstring::npos &&
            identity.body.find(
                L"Would you like the game to configure itself optimally for your new hardware?") !=
                std::wstring::npos &&
            identity.body.find(
                L"It will change your system settings but not your controls.") !=
                std::wstring::npos;
    }

    std::vector<RuntimeDataLink> runtime_data_links(const LaunchPlan& plan)
    {
        if (!fs::is_directory(plan.game_dir))
        {
            throw std::runtime_error("Game data root is missing or not a directory");
        }

        const auto canonical_game = fs::canonical(plan.game_dir);
        std::vector<RuntimeDataLink> links;
        links.reserve(kRuntimeDataDirectories.size());
        for (const auto* name : kRuntimeDataDirectories)
        {
            const fs::path relative_name(name);
            if (!is_safe_runtime_link_name(relative_name))
            {
                throw std::logic_error("Unsafe built-in runtime data link name");
            }

            const auto requested_source = plan.game_dir / relative_name;
            if (!fs::is_directory(requested_source))
            {
                throw std::runtime_error(
                    "Required game data directory is missing: " +
                    narrow_for_error(requested_source));
            }
            const auto source = fs::canonical(requested_source);
            if (!is_same_or_descendant(source, canonical_game))
            {
                throw std::runtime_error(
                    "Required game data directory resolves outside the validated game root: " +
                    narrow_for_error(requested_source));
            }

            const auto destination =
                (plan.runtime_dir / relative_name).lexically_normal();
            if (!is_same_or_descendant(destination, plan.runtime_dir) ||
                destination.parent_path() != plan.runtime_dir.lexically_normal())
            {
                throw std::logic_error("Runtime data link destination escaped the runtime root");
            }
            links.push_back({relative_name, source, destination});
        }
        return links;
    }

    std::vector<RuntimeSupportFile> runtime_support_files(const LaunchPlan& plan)
    {
        if (plan.game_dir.empty() || plan.runtime_dir.empty())
        {
            throw std::invalid_argument(
                "Game and runtime roots are required for support-file staging");
        }

        const auto installed_language =
            detect_installed_game_language(plan.game_dir);

        std::vector<RuntimeSupportFile> files;
        files.reserve(kRootSupportFiles.size());
        for (const auto& spec : kRootSupportFiles)
        {
            const fs::path relative_name(spec.filename);
            if (!is_safe_runtime_link_name(relative_name))
            {
                throw std::logic_error("Unsafe built-in runtime support filename");
            }
            const auto source = (plan.game_dir / relative_name).lexically_normal();
            const auto destination =
                (plan.runtime_dir / relative_name).lexically_normal();
            if (source.parent_path() != plan.game_dir.lexically_normal() ||
                destination.parent_path() != plan.runtime_dir.lexically_normal() ||
                !is_same_or_descendant(source, plan.game_dir) ||
                !is_same_or_descendant(destination, plan.runtime_dir))
            {
                throw std::logic_error("Runtime support-file path escaped its root");
            }
            const auto expected_sha256 = spec.expected_sha256[0] == '\0'
                ? installed_language.selector_sha256
                : std::string(spec.expected_sha256);
            files.push_back({source, destination, expected_sha256});
        }
        return files;
    }

    void stage_runtime_language_selector(const LaunchPlan& plan)
    {
        const auto files = runtime_support_files(plan);
        const auto selector = std::find_if(
            files.begin(), files.end(), [](const RuntimeSupportFile& file) {
                return file.destination.filename() == L"localization.txt";
            });
        if (selector == files.end() ||
            std::find_if(std::next(selector), files.end(), [](const RuntimeSupportFile& file) {
                return file.destination.filename() == L"localization.txt";
            }) != files.end())
        {
            throw std::logic_error(
                "Runtime support plan must contain exactly one language selector");
        }

        copy_verified_file_to_runtime(
            plan,
            selector->source,
            selector->destination,
            selector->expected_sha256,
            "Runtime language selector");
    }

    std::vector<RuntimeHandoffFile> runtime_handoff_files(const LaunchPlan& plan)
    {
        if (plan.executable_kind != ExecutableKind::sp ||
            !plan.launcher_executable || !plan.mod_dll)
        {
            return {};
        }

        const auto launcher_destination =
            (plan.runtime_dir / L"CoDWaWmp.exe").lexically_normal();
        const auto dll_destination =
            (plan.runtime_dir / L"WorldWarVR.dll").lexically_normal();
        if (launcher_destination.parent_path() != plan.runtime_dir.lexically_normal() ||
            dll_destination.parent_path() != plan.runtime_dir.lexically_normal() ||
            !is_same_or_descendant(launcher_destination, plan.runtime_dir) ||
            !is_same_or_descendant(dll_destination, plan.runtime_dir))
        {
            throw std::logic_error("Runtime multiplayer handoff path escaped its root");
        }
        return {
            {*plan.launcher_executable, launcher_destination},
            {*plan.mod_dll, dll_destination},
        };
    }

    DiagnosticReport diagnose(const LaunchPlan& plan)
    {
        DiagnosticReport report;
        report.command_line = build_game_command_line(plan);
        const auto& identity = executable_identity(plan.executable_identity);

        checked(report, L"executable target identity", [&]() {
            const bool passed =
                plan.executable_kind == executable_kind_for_target(plan.launch_target) &&
                identity.kind == plan.executable_kind;
            return std::pair(
                passed,
                std::wstring(identity.display_name) + L" / " +
                    identity.staged_filename + L" / " +
                    widen_ascii(identity.stable_id));
        });

        checked(report, L"packed VR source resolution", [&]() {
            const auto text = source_resolution_text(plan.source_resolution);
            const auto validated = parse_source_resolution(text);
            const bool passed = validated == plan.source_resolution;
            const auto detail = text + L" packed (" +
                std::to_wstring(plan.source_resolution.width / 2U) + L"x" +
                std::to_wstring(plan.source_resolution.height) + L" per eye)";
            return std::pair(passed, detail);
        });

        checked(report, L"source-resolution renderer compatibility", [&]() {
            // Query the physical display mode rather than DPI-virtualized
            // logical metrics. Otherwise a scaled 4K primary display can be
            // misreported as 1920x1080 and reject a source mode that fits.
            DEVMODEW display_mode{};
            display_mode.dmSize = sizeof(display_mode);
            const bool display_mode_available = EnumDisplaySettingsW(
                nullptr, ENUM_CURRENT_SETTINGS, &display_mode) != FALSE;
            const SourceResolution desktop{
                display_mode_available ? display_mode.dmPelsWidth : 0U,
                display_mode_available ? display_mode.dmPelsHeight : 0U,
            };
            const bool passed = plan_source_resolution_is_desktop_compatible(
                plan, desktop);
            const auto detail = plan_source_resolution_compatibility_detail(
                plan, desktop);
            return std::pair(passed, detail);
        });

        checked(report, L"game directory", [&]() {
            const auto exists = fs::is_directory(plan.game_dir);
            return std::pair(exists, exists ? plan.game_dir.wstring() : L"Missing directory");
        });
        std::optional<InstalledGameLanguage> installed_language;
        checked(report, L"installed game language", [&]() {
            installed_language = detect_installed_game_language(plan.game_dir);
            return std::pair(
                true,
                std::wstring(game_language_display_name(
                    installed_language->language)) +
                    L" (" + game_language_code(installed_language->language) +
                    L"); selector SHA-256 " +
                    widen_ascii(installed_language->selector_sha256));
        });
        const auto selected_zone = plan.game_dir / L"zone" /
            (installed_language
                 ? game_language_code(installed_language->language)
                 : L"__invalid_language__");
        checked(report, L"selected language zone directory", [&]() {
            const auto valid = is_normal_directory(selected_zone);
            return std::pair(
                valid,
                valid ? selected_zone.wstring()
                      : L"Missing or unsafe: " + selected_zone.wstring());
        });
        checked(report, L"source executable", [&]() { return file_exists_check(plan.source_exe); });
        checked(report, L"main data marker", [&]() {
            return file_exists_check(plan.game_dir / L"main" / L"iw_00.iwd");
        });
        if (plan.launch_target == LaunchTarget::offline_multiplayer)
        {
            for (const auto& fastfile : kMultiplayerFastfiles)
            {
                checked(
                    report,
                    L"MP " + std::wstring(fastfile.label) + L" data marker",
                    [&]() {
                        return file_exists_check(
                            selected_zone / fastfile.filename);
                    });
            }
            checked_optional(report, L"optional PeZBOT offline bots", [&]() {
                const auto status = inspect_optional_pezbot(plan);
                const bool available = status.state == PezBotState::installed ||
                    status.state == PezBotState::archive_ready;
                return std::pair(
                    available,
                    L"state=" + std::wstring(pezbot_state_name(status.state)) +
                        L"; " + status.detail);
            });
        }
        else
        {
            checked(report, L"Nacht data marker", [&]() {
                return file_exists_check(
                    selected_zone / L"nazi_zombie_prototype.ff");
            });
        }
        if (plan.launch_target == LaunchTarget::der_riese)
        {
            for (const auto& fastfile : kDerRieseFastfiles)
            {
                checked(
                    report,
                    L"Der Riese " + std::wstring(fastfile.label) + L" data marker",
                    [&]() {
                        return file_exists_check(
                            selected_zone / fastfile.filename);
                    });
            }
            checked(report, L"Der Riese intro video data marker", [&]() {
                return file_exists_check(
                    plan.game_dir / L"main" / L"video" /
                    L"nazi_zombie_factory_load.bik");
            });
        }
        if (plan.launch_target == LaunchTarget::campaign_ber1)
        {
            checked(report, L"Ber1 campaign data marker", [&]() {
                return file_exists_check(selected_zone / L"ber1.ff");
            });
        }
        if (plan.launch_target == LaunchTarget::campaign_pel2_airfield)
        {
            checked(report, L"Pel2 campaign data marker", [&]() {
                return file_exists_check(selected_zone / L"pel2.ff");
            });
        }

        checked(report, L"source SHA-256", [&]() {
            const auto actual = sha256_file(plan.source_exe);
            return std::pair(
                actual == identity.expected_sha256,
                widen_ascii(actual) + L" (required " +
                    widen_ascii(identity.expected_sha256) + L")");
        });
        checked(report, L"source PE build", [&]() {
            const auto pe = inspect_pe(plan.source_exe);
            const auto passed = pe.is_x86_pe32() &&
                pe.timestamp == identity.expected_timestamp &&
                pe.entrypoint_rva == identity.expected_entrypoint_rva &&
                pe.image_base == kExpectedImageBase &&
                !pe.is_dll();
            const auto detail = L"machine=" + hex_value(pe.machine, 4) +
                L", timestamp=" + hex_value(pe.timestamp, 8) +
                L", entrypoint VA=" + hex_value(pe.image_base + pe.entrypoint_rva, 8) +
                L", image base=" + hex_value(pe.image_base, 8);
            return std::pair(passed, detail);
        });
        checked(report, L"source file version", [&]() {
            const auto version = inspect_file_version(plan.source_exe);
            const auto passed = version.major == 1 && version.minor == 7;
            const auto detail = std::to_wstring(version.major) + L'.' +
                std::to_wstring(version.minor) + L'.' +
                std::to_wstring(version.build) + L'.' +
                std::to_wstring(version.revision);
            return std::pair(passed, detail);
        });
        for (const auto& support : kRootSupportFiles)
        {
            checked(
                report,
                std::wstring(support.label) + L" marker",
                [&]() {
                    return file_exists_check(plan.game_dir / support.filename);
                });
            checked(
                report,
                std::wstring(support.label) + L" SHA-256",
                [&]() {
                    const auto path = plan.game_dir / support.filename;
                    const auto actual = sha256_file(path);
                    const auto expected = support.expected_sha256[0] == '\0'
                        ? (installed_language
                               ? installed_language->selector_sha256
                               : std::string())
                        : std::string(support.expected_sha256);
                    return std::pair(
                        !expected.empty() && actual == expected,
                        widen_ascii(actual) + L" (required " +
                            widen_ascii(expected) + L")");
                });
        }
        const bool has_pinned_language_fastfiles =
            installed_language &&
            installed_language->language == GameLanguage::english;
        if (has_pinned_language_fastfiles &&
            plan.launch_target == LaunchTarget::offline_multiplayer)
        {
            for (const auto& fastfile : kMultiplayerFastfiles)
            {
                checked(
                    report,
                    L"MP " + std::wstring(fastfile.label) + L" fastfile SHA-256",
                    [&]() {
                        const auto path = selected_zone / fastfile.filename;
                        const auto actual = sha256_file(path);
                        return std::pair(
                            actual == fastfile.expected_sha256,
                            widen_ascii(actual) + L" (required " +
                                widen_ascii(fastfile.expected_sha256) + L")");
                    });
            }
        }
        else if (has_pinned_language_fastfiles)
        {
            checked(report, L"Nacht fastfile SHA-256", [&]() {
                const auto path = selected_zone / L"nazi_zombie_prototype.ff";
                const auto actual = sha256_file(path);
                return std::pair(
                    actual == kExpectedNachtSha256,
                    widen_ascii(actual) + L" (required " +
                        widen_ascii(kExpectedNachtSha256) + L")");
            });
        }
        if (has_pinned_language_fastfiles &&
            plan.launch_target == LaunchTarget::der_riese)
        {
            for (const auto& fastfile : kDerRieseFastfiles)
            {
                checked(
                    report,
                    L"Der Riese " + std::wstring(fastfile.label) +
                        L" fastfile SHA-256",
                    [&]() {
                        const auto path = selected_zone / fastfile.filename;
                        const auto actual = sha256_file(path);
                        return std::pair(
                            actual == fastfile.expected_sha256,
                            widen_ascii(actual) + L" (required " +
                                widen_ascii(fastfile.expected_sha256) + L")");
                    });
            }
        }
        if (plan.launch_target == LaunchTarget::der_riese)
        {
            checked(report, L"Der Riese intro video SHA-256", [&]() {
                const auto path = plan.game_dir / L"main" / L"video" /
                    L"nazi_zombie_factory_load.bik";
                const auto actual = sha256_file(path);
                return std::pair(
                    actual == kExpectedDerRieseLoadVideoSha256,
                    widen_ascii(actual) + L" (required " +
                        widen_ascii(kExpectedDerRieseLoadVideoSha256) + L")");
            });
        }
        checked(report, L"write isolation", [&]() {
            validate_write_boundaries(plan);
            return std::pair(true, L"runtime/home are outside game and source directories");
        });
        checked(report, L"runtime data junctions", [&]() {
            return std::pair(true, runtime_link_diagnostic_detail(plan));
        });
        checked(report, L"runtime root support staging boundaries", [&]() {
            const auto files = runtime_support_files(plan);
            return std::pair(
                files.size() == kRootSupportFiles.size(),
                std::to_wstring(files.size()) +
                    L" verified game-root files will be copied only into " +
                    plan.runtime_dir.wstring());
        });

        if (plan.mode == Mode::launch)
        {
            checked(report, L"VR launch DLL selection", [&]() {
                const bool selected = plan.mod_dll.has_value();
                return std::pair(
                    selected,
                    selected
                        ? plan.mod_dll->wstring()
                        : L"Raw --launch requires --mod-dll; packaged play uses "
                          L"WorldWarVR.exe, which selects its adjacent DLL");
            });
        }

        if (plan.executable_kind == ExecutableKind::sp &&
            plan.mode == Mode::launch)
        {
            checked(report, L"multiplayer handoff launcher selection", [&]() {
                const bool selected = plan.launcher_executable.has_value();
                return std::pair(
                    selected,
                    selected
                        ? plan.launcher_executable->wstring()
                        : L"The current WaWVR launcher path is required for the stock MP handoff");
            });
        }

        if (plan.launcher_executable &&
            plan.executable_kind == ExecutableKind::sp)
        {
            checked(report, L"multiplayer handoff launcher", [&]() {
                return file_exists_check(*plan.launcher_executable);
            });
            checked(report, L"multiplayer handoff launcher PE architecture", [&]() {
                const auto pe = inspect_pe(*plan.launcher_executable);
                const auto passed = pe.is_x86_pe32() && !pe.is_dll();
                return std::pair(
                    passed,
                    L"machine=" + hex_value(pe.machine, 4) +
                        L", PE32=" +
                        (pe.optional_magic == kPe32Magic ? L"yes" : L"no") +
                        L", DLL=" + (pe.is_dll() ? L"yes" : L"no"));
            });
        }

        if (plan.executable_kind == ExecutableKind::sp &&
            plan.launcher_executable && plan.mod_dll)
        {
            checked(report, L"runtime multiplayer handoff staging boundaries", [&]() {
                const auto files = runtime_handoff_files(plan);
                const bool passed = files.size() == 2 &&
                    files[0].destination.filename() == L"CoDWaWmp.exe" &&
                    files[1].destination.filename() == L"WorldWarVR.dll";
                return std::pair(
                    passed,
                    L"runtime-only launcher alias and adjacent VR DLL in " +
                        plan.runtime_dir.wstring());
            });
            checked(report, L"runtime multiplayer handoff configuration", [&]() {
                const auto files = runtime_handoff_files(plan);
                if (files.size() != 2U)
                {
                    return std::pair(
                        false,
                        std::wstring(L"Exact runtime shim path is unavailable"));
                }
                const auto config = derive_multiplayer_handoff_config(plan);
                const auto canonical =
                    serialize_multiplayer_handoff_config(config);
                const auto reparsed = parse_multiplayer_handoff_config(
                    canonical, files[0].destination);
                return std::pair(
                    reparsed == config,
                    L"canonical runtime-local config selects " +
                        config.mp_source_exe.wstring() + L" -> " +
                        config.mp_runtime_dir.wstring());
            });
        }

        if (plan.mod_dll)
        {
            checked(report, L"mod DLL", [&]() { return file_exists_check(*plan.mod_dll); });
            checked(report, L"mod DLL PE architecture", [&]() {
                const auto pe = inspect_pe(*plan.mod_dll);
                const auto passed = pe.is_x86_pe32() && pe.is_dll();
                return std::pair(
                    passed,
                    L"machine=" + hex_value(pe.machine, 4) +
                        L", PE32=" + (pe.optional_magic == kPe32Magic ? L"yes" : L"no") +
                        L", DLL=" + (pe.is_dll() ? L"yes" : L"no"));
            });
            checked(report, L"injector architecture", [&]() {
                const auto passed = sizeof(void*) == 4;
                return std::pair(
                    passed,
                    passed ? L"Win32 launcher can inject the x86 DLL" :
                             L"Rebuild the launcher with CMake -A Win32");
            });
        }

        return report;
    }

    void prepare_runtime(const LaunchPlan& plan)
    {
        const auto report = diagnose(plan);
        if (!report.passed())
        {
            throw std::runtime_error("Refusing to prepare because diagnostics failed");
        }

        validate_write_boundaries(plan);
        fs::create_directories(plan.runtime_dir);
        fs::create_directories(plan.home_dir);
        // Re-run after creation so a reparse-point race or a pre-existing
        // redirected root cannot turn our managed writes into game-data writes.
        validate_write_boundaries(plan);
        copy_verified_executable(plan);
        copy_runtime_support_files(plan);
        prepare_runtime_data_links(plan);
        copy_runtime_handoff_files(plan);
        write_runtime_multiplayer_handoff_config(plan);
        copy_optional_pezbot_helper_to_runtime(plan);
    }

    void prepare_runtime_data_links(const LaunchPlan& plan)
    {
        validate_write_boundaries(plan);
        if (!fs::is_directory(plan.runtime_dir))
        {
            throw std::runtime_error("Runtime directory must exist before creating data links");
        }

        for (const auto& link : runtime_data_links(plan))
        {
            const auto state = validate_existing_runtime_link(link);
            if (state == RuntimeLinkPresence::absent)
            {
                create_directory_junction(link);
            }
        }
    }

    LaunchResult launch_game(const LaunchPlan& plan)
    {
        if (plan.mode != Mode::launch)
        {
            throw std::logic_error("launch_game requires --launch mode");
        }

        if (plan.executable_kind != executable_kind_for_target(plan.launch_target) ||
            executable_kind_for_identity(plan.executable_identity) !=
                plan.executable_kind)
        {
            throw std::logic_error("Launch target and executable identity do not match");
        }
        const auto& identity = executable_identity(plan.executable_identity);
        if (!fs::is_regular_file(plan.staged_exe) ||
            sha256_file(plan.staged_exe) != identity.expected_sha256)
        {
            throw std::runtime_error("The staged executable is missing or no longer verified");
        }
        for (const auto& support : runtime_support_files(plan))
        {
            const DWORD attributes = GetFileAttributesW(support.destination.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & (FILE_ATTRIBUTE_DIRECTORY |
                               FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
                sha256_file(support.destination) != support.expected_sha256)
            {
                throw std::runtime_error(
                    "A staged runtime root support file is missing or no longer verified");
            }
        }
        if (plan.executable_kind == ExecutableKind::sp)
        {
            const auto handoff = runtime_handoff_files(plan);
            if (handoff.size() != 2)
            {
                throw std::runtime_error(
                    "The runtime-only multiplayer handoff shim is not configured");
            }
            for (const auto& file : handoff)
            {
                const DWORD attributes = GetFileAttributesW(file.destination.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES ||
                    (attributes & (FILE_ATTRIBUTE_DIRECTORY |
                                   FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
                    sha256_file(file.destination) != sha256_file(file.source))
                {
                    throw std::runtime_error(
                        "The runtime-only multiplayer handoff shim is missing or changed");
                }
            }
            if (read_multiplayer_handoff_config(handoff[0].destination) !=
                derive_multiplayer_handoff_config(plan))
            {
                throw std::runtime_error(
                    "The runtime-only multiplayer handoff configuration changed");
            }
        }

        auto effective_plan = plan;
        if (effective_plan.executable_kind == ExecutableKind::mp)
        {
            // Revalidate immediately before process creation so a changed or
            // custom mod directory can only disable bots, never be auto-loaded.
            effective_plan.pezbot_enabled =
                inspect_optional_pezbot(effective_plan).enabled();
            // The stock Start New Server menu persists ui_dedicated. A stale
            // value of 1 converts the only VR process into a server console:
            // bots connect, but the headset player never does. Rewrite only
            // the launcher's isolated active MP profile and leave the normal
            // Activision profile and installed game untouched.
            enforce_offline_multiplayer_listen_profile(effective_plan);
        }
        auto command_line = build_game_command_line(effective_plan);
        std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
        mutable_command.push_back(L'\0');
        std::vector<wchar_t> child_environment;
        if (executable_identity_uses_steam(effective_plan.executable_identity) ||
            effective_plan.executable_kind == ExecutableKind::mp)
        {
            child_environment = build_child_environment_block(
                effective_plan.executable_identity,
                inherited_environment_entries());
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process_info{};
        if (!CreateProcessW(
                plan.staged_exe.c_str(),
                mutable_command.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                child_environment.empty()
                    ? nullptr
                    : static_cast<void*>(child_environment.data()),
                plan.runtime_dir.c_str(),
                &startup,
                &process_info))
        {
            throw_last_error("CreateProcessW");
        }

        UniqueHandle process(process_info.hProcess);
        UniqueHandle primary_thread(process_info.hThread);
        UniqueHandle bootstrap_ready_event;
        UniqueHandle bootstrap_failed_event;
        std::optional<RemoteAllocation> remote_dll_path;
        bool launch_committed = false;
        try
        {
            if (plan.mod_dll)
            {
                const auto ready_name = bootstrap_event_name(
                    process_info.dwProcessId, true);
                const auto failed_name = bootstrap_event_name(
                    process_info.dwProcessId, false);
                bootstrap_ready_event = UniqueHandle(CreateEventW(
                    nullptr, TRUE, FALSE, ready_name.c_str()));
                bootstrap_failed_event = UniqueHandle(CreateEventW(
                    nullptr, TRUE, FALSE, failed_name.c_str()));
                if (!bootstrap_ready_event.valid() ||
                    !bootstrap_failed_event.valid())
                {
                    throw_last_error("CreateEventW(VR bootstrap handshake)");
                }
                // Allocate and write the absolute DLL path while the target is
                // still suspended. Module enumeration is intentionally delayed
                // until after ResumeThread lets the Windows loader initialize.
                remote_dll_path.emplace(write_remote_dll_path(process.get(), *plan.mod_dll));
            }

            if (ResumeThread(primary_thread.get()) == std::numeric_limits<DWORD>::max())
            {
                throw_last_error("ResumeThread");
            }

            LaunchResult result;
            result.process_id = process_info.dwProcessId;
            if (plan.mod_dll)
            {
                const auto injection = inject_prepared_dll_after_resume(
                    process.get(),
                    process_info.dwProcessId,
                    primary_thread.get(),
                    bootstrap_ready_event.get(),
                    bootstrap_failed_event.get(),
                    remote_dll_path->get(),
                    plan.staged_exe.filename().wstring(),
                    plan.executable_kind == ExecutableKind::sp);
                result.injection_detail =
                    L"Loaded the mod through " + injection.procedure.owner_module +
                    L" after " + std::to_wstring(injection.poll.attempts) +
                    L" loader probes (" + std::to_wstring(injection.poll.elapsed_ms) +
                    L" ms), with the primary thread held until the validated "
                    L"early-renderer bootstrap completed.";
                remote_dll_path.reset();
            }

            const auto startup_prompts = dismiss_spawned_startup_prompts(
                process.get(),
                process_info.dwProcessId);
            if (startup_prompts.process_exited)
            {
                DWORD exit_code = 0;
                GetExitCodeProcess(process.get(), &exit_code);
                throw std::runtime_error(
                    "Target exited while checking its startup prompts; exit code " +
                    std::to_string(exit_code));
            }
            if (!result.injection_detail.empty())
            {
                result.injection_detail += L' ';
            }
            if (startup_prompts.safe_mode_dismissed)
            {
                result.injection_detail +=
                    L"Selected No on the spawned process's exact World at War safe-mode "
                    L"dialog. ";
            }
            if (startup_prompts.optimal_settings_dismissed)
            {
                result.injection_detail +=
                    L"Selected No on the spawned process's exact World at War optimal-settings "
                    L"dialog. ";
            }
            result.injection_detail +=
                L"Startup prompt polling used " +
                std::to_wstring(startup_prompts.probes) + L" spawned-PID-only probes (" +
                std::to_wstring(startup_prompts.elapsed_ms) + L" ms).";

            // From here onward the explicitly requested game is running and,
            // when supplied, the mod DLL has been confirmed loaded.
            launch_committed = true;
            if (plan.wait_for_exit)
            {
                if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0)
                {
                    throw_last_error("WaitForSingleObject");
                }
                DWORD exit_code = 0;
                if (!GetExitCodeProcess(process.get(), &exit_code))
                {
                    throw_last_error("GetExitCodeProcess");
                }
                result.exit_code = exit_code;
            }
            return result;
        }
        catch (...)
        {
            if (!launch_committed)
            {
                TerminateProcess(process.get(), ERROR_DLL_INIT_FAILED);
                WaitForSingleObject(process.get(), 5'000);
                remote_dll_path.reset();
            }
            throw;
        }
    }

    std::wstring mode_name(
        const Mode mode,
        const LaunchTarget launch_target)
    {
        switch (mode)
        {
        case Mode::diagnose:
            return launch_target == LaunchTarget::offline_multiplayer
                ? L"diagnose offline multiplayer (read-only)"
                : L"diagnose (read-only)";
        case Mode::prepare:
            return launch_target == LaunchTarget::offline_multiplayer
                ? L"prepare offline multiplayer (stage only)"
                : L"prepare (stage only)";
        case Mode::launch:
            switch (launch_target)
            {
            case LaunchTarget::nacht:
                return L"launch direct Nacht";
            case LaunchTarget::der_riese:
                return L"launch direct Der Riese";
            case LaunchTarget::campaign_ber1:
                return L"launch direct Ber1 campaign (developer)";
            case LaunchTarget::campaign_pel2_airfield:
                return L"launch Pel2 airfield campaign checkpoint (developer)";
            case LaunchTarget::frontend_menu:
                return L"launch stock frontend/menu";
            case LaunchTarget::offline_multiplayer:
                return L"launch offline multiplayer frontend";
            }
            return L"launch unknown target";
        }
        return L"unknown";
    }

    std::wstring usage_text()
    {
        return LR"USAGE(WaWVR standalone launcher

Usage:
  wawvr-launcher [--diagnose] [--multiplayer] [path options]
  wawvr-launcher --prepare [--multiplayer] [path options]
  wawvr-launcher --launch [--menu | --der-riese | --campaign-ber1 | --multiplayer]
                         [--mod-dll FILE] [--pezbot-archive ZIP] [--wait]
                         [--bots enabled|disabled]
                         [path options]

Modes:
  --diagnose     Validate and print the exact plan without writing or launching.
                 This is the default.
  --prepare      Validate, create isolated runtime/home directories, and stage
                 the selected locally owned executable as CoDWaW.exe or
                 CoDWaWmp.exe, with validated zone/main data junctions. Does
                 not launch.
  --launch       Validate, prepare, and launch the selected game target in VR.

Launch target:
  --menu               Omit +devmap and enter WaW's stock frontend so the
                       Zombies map-selection menus can be used. Valid only
                       with --launch; direct Nacht remains the default.
  --der-riese          Launch the installed final DLC Zombies map directly
                       with +devmap nazi_zombie_factory. Valid only with
                       --launch and mutually exclusive with other targets.
  --campaign-ber1      Developer-only physical Mosin validation target. Launch
                       the fixed Ber1 campaign map with +devmap ber1. Valid
                       only with --launch and mutually exclusive with other
                       targets.
  --multiplayer        Select the exact pinned World at War MP executable and
                       its separately isolated runtime/home directories. The
                       frontend defaults to offline/local browsing and disables
                       public stats/matchmaking. A verified optional PeZBOT
                       archive enables local bots without changing map choice.

Path options:
  --game-dir DIR       WaW data root containing main/, zone/, and binkw32.dll.
  --source-exe FILE    User-owned WaW 1.7 executable for the selected SP/MP target.
  --mp-source-exe FILE SP-only explicit MP executable for the stock frontend's
                       later Launch Multiplayer handoff. Stored in the strict
                       runtime-local config during SP preparation. With
                       --multiplayer, use --source-exe instead.
  --runtime-dir DIR    Isolated staging directory outside the game/source dirs.
  --homepath DIR       Isolated persistent profile directory.
  --mod-dll FILE       Absolute or relative path to the x86 VR DLL. Required for
                       raw wawvr-launcher --launch. Packaged WorldWarVR.exe
                       selects its adjacent WorldWarVR.dll automatically,
                       including when launch-target arguments are supplied.
  --pezbot-archive ZIP Import only the exact PeZBOTWAW_005p.zip release into the
                       isolated MP home. The launcher also checks beside itself
                       and in Downloads. Missing/invalid archives never block
                       base offline multiplayer and existing custom mod folders
                       are never overwritten.
  --bots POLICY        Set optional local/offline multiplayer bots to exactly
                       enabled or disabled. Disabled prevents archive discovery,
                       import and autofill, including after Launch Multiplayer
                       is selected from the main menu. If omitted, the historical
                       automatic behavior is retained for compatibility.
  --resolution WxH     Packed side-by-side source resolution. Width must be
                       even; accepted range is 640x480 through 6016x2688.
                       Exact validated SP builds default to 6016x2688: two
                       native 2496x2688 eyes plus a 1024x1024 scope camera,
                       through the patched off-screen D3D9Ex
                       renderer, even when larger than the Windows desktop.
                       Performance: 3744x2016 (1872x2016 per eye). MP retains
                       the desktop-bound 2560x1440 legacy default and cannot use
                       SP-only modes above 3840x2160. Recovery: 1024x768.
  --wait               Wait for the launched game and return its exit code.
  -h, --help           Show this help.

The first preparation needs --game-dir or WAWVR_GAME_DIR. Either prepared
standalone runtime can subsequently recover it only from matching
launcher-managed main/ and zone/ junctions.
WAWVR_SOURCE_EXE (SP), WAWVR_MP_SOURCE_EXE (MP), and
WAWVR_SOURCE_RESOLUTION may override source discovery/settings.
Explicit command-line values, including SP-only --mp-source-exe, take
precedence. Launch and preparation remain
blocked unless every pinned executable/data hash and safety check passes.
)USAGE";
    }
}
