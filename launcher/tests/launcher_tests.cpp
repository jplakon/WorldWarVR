#include <wawvr_launcher/launcher_core.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace wawvr::launcher;

namespace
{
    void require(const bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    std::size_t count_occurrences(
        const std::wstring_view text,
        const std::wstring_view needle)
    {
        if (needle.empty())
        {
            return 0;
        }
        std::size_t count = 0;
        std::size_t offset = 0;
        while ((offset = text.find(needle, offset)) != std::wstring_view::npos)
        {
            ++count;
            offset += needle.size();
        }
        return count;
    }

    void require_exact_openxr_pacing_policy(const std::wstring_view command)
    {
        require(
            count_occurrences(command, L"+set com_maxfps ") == 1 &&
                count_occurrences(command, L"+set r_vsync ") == 1 &&
                count_occurrences(
                    command,
                    L"+set com_maxfps 0 +set r_vsync 0") == 1,
            "launch command did not contain the exact uncapped-FPS/disabled-vsync policy");
    }

    void require_exact_view_bob_policy(const std::wstring_view command)
    {
        const auto movement_bob = command.find(L"+set bg_bobMax 0");
        const auto weapon_bob =
            command.find(L"+set cg_bobWeaponAmplitude 0");
        require(
            count_occurrences(command, L"+set bg_bobMax ") == 1 &&
                count_occurrences(command, L"+set bg_bobMax 0") == 1 &&
                count_occurrences(
                    command, L"+set cg_bobWeaponAmplitude ") == 1 &&
                count_occurrences(
                    command, L"+set cg_bobWeaponAmplitude 0") == 1,
            "launch command did not contain exactly one movement/weapon bob override");
        require(command.find(L"+seta bg_bobMax") == std::wstring_view::npos &&
                    command.find(L"+seta cg_bobWeaponAmplitude") ==
                        std::wstring_view::npos,
                "view-bob policy unexpectedly became persistent profile state");
        const auto direct_map = command.find(L"+devmap");
        require(direct_map == std::wstring_view::npos ||
                    (movement_bob < direct_map && weapon_bob < direct_map),
                "view-bob policy was applied after the direct-map command");
    }

    void require_exact_dynamic_light_policy(const std::wstring_view command)
    {
        require(
            count_occurrences(command, L"+set r_dlightLimit ") == 1 &&
                count_occurrences(
                    command,
                    L"+set r_dlightLimit 4 +set r_spotLightShadows 0") == 1,
            "launch command did not contain exactly one four-partition dynamic-light limit");
    }

    void require_exact_campaign_unlock_policy(
        const std::wstring_view command,
        const bool expected)
    {
        const auto exact_count =
            count_occurrences(command, L"+seta mis_01 50");
        const auto any_count =
            count_occurrences(command, L"+seta mis_01 ");
        if (!expected)
        {
            require(exact_count == 0 && any_count == 0 &&
                        command.find(L"mis_01") == std::wstring_view::npos &&
                        command.find(L"mis_cheat") == std::wstring_view::npos &&
                        command.find(L"mis_difficulty") == std::wstring_view::npos &&
                        command.find(L"ui_sp_unlock") == std::wstring_view::npos,
                    "non-SP launch command gained campaign unlock state");
            return;
        }

        require(exact_count == 1 && any_count == 1,
                "SP launch command did not contain exactly one campaign unlock");
        require(command.find(L"mis_cheat") == std::wstring_view::npos &&
                    command.find(L"mis_difficulty") == std::wstring_view::npos &&
                    command.find(L"mis_01_unlock") == std::wstring_view::npos &&
                    command.find(L"ui_sp_unlock") == std::wstring_view::npos,
                "campaign unlock changed unrelated campaign state");
        const auto unlock = command.find(L"+seta mis_01 50");
        const auto direct_map = command.find(L"+devmap");
        require(direct_map == std::wstring_view::npos || unlock < direct_map,
                "campaign unlock was applied after the direct-map command");
    }

    const DiagnosticCheck* find_check(
        const DiagnosticReport& report,
        const std::wstring_view name)
    {
        const auto check = std::find_if(
            report.checks.begin(),
            report.checks.end(),
            [&](const DiagnosticCheck& candidate) { return candidate.name == name; });
        return check == report.checks.end() ? nullptr : &*check;
    }

    void put_u16(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint16_t value)
    {
        bytes.at(offset) = static_cast<std::uint8_t>(value);
        bytes.at(offset + 1) = static_cast<std::uint8_t>(value >> 8);
    }

    void put_u32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value)
    {
        for (int index = 0; index < 4; ++index)
        {
            bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (index * 8));
        }
    }

    void append_u16(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    }

    void append_u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
    {
        for (int index = 0; index < 4; ++index)
        {
            bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
        }
    }

    std::uint32_t crc32(const std::string_view data)
    {
        std::uint32_t crc = 0xFFFFFFFFU;
        for (const unsigned char byte : data)
        {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit)
            {
                const auto mask = static_cast<std::uint32_t>(
                    -static_cast<std::int32_t>(crc & 1U));
                crc = (crc >> 1) ^ (0xEDB88320U & mask);
            }
        }
        return ~crc;
    }

    struct StoredZipEntry
    {
        std::string name;
        std::string data;
        std::uint32_t external_attributes = 0;
    };

    void write_stored_zip(
        const fs::path& path,
        const std::vector<StoredZipEntry>& entries)
    {
        struct CentralRecord
        {
            StoredZipEntry entry;
            std::uint32_t crc = 0;
            std::uint32_t local_offset = 0;
        };

        std::vector<std::uint8_t> bytes;
        std::vector<CentralRecord> central;
        for (const auto& entry : entries)
        {
            require(!entry.name.empty() && entry.name.size() <= 0xFFFF,
                    "ZIP fixture entry name is invalid");
            require(entry.data.size() <= 0xFFFFFFFFULL,
                    "ZIP fixture entry is too large");
            const auto checksum = crc32(entry.data);
            const auto offset = static_cast<std::uint32_t>(bytes.size());
            append_u32(bytes, 0x04034B50U);
            append_u16(bytes, 20);
            append_u16(bytes, 0);
            append_u16(bytes, 0);
            append_u16(bytes, 0);
            append_u16(bytes, 0);
            append_u32(bytes, checksum);
            append_u32(bytes, static_cast<std::uint32_t>(entry.data.size()));
            append_u32(bytes, static_cast<std::uint32_t>(entry.data.size()));
            append_u16(bytes, static_cast<std::uint16_t>(entry.name.size()));
            append_u16(bytes, 0);
            bytes.insert(bytes.end(), entry.name.begin(), entry.name.end());
            bytes.insert(bytes.end(), entry.data.begin(), entry.data.end());
            central.push_back({entry, checksum, offset});
        }

        const auto central_offset = static_cast<std::uint32_t>(bytes.size());
        for (const auto& record : central)
        {
            append_u32(bytes, 0x02014B50U);
            append_u16(bytes, 20);
            append_u16(bytes, 20);
            append_u16(bytes, 0);
            append_u16(bytes, 0);
            append_u16(bytes, 0);
            append_u16(bytes, 0);
            append_u32(bytes, record.crc);
            append_u32(bytes, static_cast<std::uint32_t>(record.entry.data.size()));
            append_u32(bytes, static_cast<std::uint32_t>(record.entry.data.size()));
            append_u16(bytes, static_cast<std::uint16_t>(record.entry.name.size()));
            append_u16(bytes, 0);
            append_u16(bytes, 0);
            append_u16(bytes, 0);
            append_u16(bytes, 0);
            append_u32(bytes, record.entry.external_attributes);
            append_u32(bytes, record.local_offset);
            bytes.insert(
                bytes.end(), record.entry.name.begin(), record.entry.name.end());
        }
        const auto central_size =
            static_cast<std::uint32_t>(bytes.size()) - central_offset;
        append_u32(bytes, 0x06054B50U);
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u16(bytes, static_cast<std::uint16_t>(central.size()));
        append_u16(bytes, static_cast<std::uint16_t>(central.size()));
        append_u32(bytes, central_size);
        append_u32(bytes, central_offset);
        append_u16(bytes, 0);

        std::ofstream output(path, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        output.close();
        require(static_cast<bool>(output), "could not write ZIP fixture");
    }

    fs::path current_test_executable()
    {
        std::vector<wchar_t> buffer(32'768);
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        require(length != 0 && length < buffer.size(),
                "could not locate the launcher test executable");
        buffer.resize(length);
        return fs::path(buffer.data()).lexically_normal();
    }

    fs::path unique_test_directory()
    {
        return fs::temp_directory_path() /
            (L"wawvr-launcher-tests-" + std::to_wstring(GetCurrentProcessId()));
    }

    void write_binary_file(const fs::path& path, const std::string_view contents)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(
            contents.data(), static_cast<std::streamsize>(contents.size()));
        output.close();
        require(static_cast<bool>(output), "could not write binary test fixture");
    }

    std::string read_binary_file(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        require(static_cast<bool>(input), "could not read binary test fixture");
        return {
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>(),
        };
    }

    void test_sha256(const fs::path& root)
    {
        const auto path = root / L"abc.txt";
        std::ofstream(path, std::ios::binary) << "abc";
        require(
            sha256_file(path) ==
                "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
            "SHA-256 implementation returned the wrong digest");
        require(
            md5_file(path) == "900150983CD24FB0D6963F7D28E17F72",
            "MD5 implementation returned the wrong digest");
    }

    void test_pe_parser(const fs::path& root)
    {
        std::vector<std::uint8_t> bytes(512, 0);
        put_u16(bytes, 0, 0x5A4D);
        put_u32(bytes, 0x3c, 0x80);
        put_u32(bytes, 0x80, 0x00004550);
        const auto file_header = 0x84u;
        put_u16(bytes, file_header, 0x014c);
        put_u32(bytes, file_header + 4, 0x4AEA1F46);
        put_u16(bytes, file_header + 16, 0x00e0);
        put_u16(bytes, file_header + 18, 0x0102);
        const auto optional_header = file_header + 20;
        put_u16(bytes, optional_header, 0x010b);
        put_u32(bytes, optional_header + 16, 0x003AF316);
        put_u32(bytes, optional_header + 28, 0x00400000);

        const auto path = root / L"fixture.exe";
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        output.close();

        const auto pe = inspect_pe(path);
        require(pe.is_x86_pe32(), "PE parser did not identify x86 PE32");
        require(!pe.is_dll(), "PE parser incorrectly identified fixture as a DLL");
        require(pe.timestamp == 0x4AEA1F46, "PE timestamp mismatch");
        require(pe.entrypoint_rva == 0x003AF316, "PE entrypoint mismatch");
        require(pe.image_base == 0x00400000, "PE image base mismatch");
    }

    void test_windows_quoting()
    {
        require(quote_windows_argument(L"simple") == L"simple", "simple argument changed");
        require(
            quote_windows_argument(L"C:\\Games Folder\\WaW") == L"\"C:\\Games Folder\\WaW\"",
            "path with spaces was not quoted");
        require(quote_windows_argument(L"") == L"\"\"", "empty argument was not quoted");
        require(
            quote_windows_argument(L"a\\\"b") == L"\"a\\\\\\\"b\"",
            "quote/backslash escaping mismatch");
    }

    void test_path_boundaries()
    {
        require(
            is_same_or_descendant(L"C:\\Games\\WaW\\main", L"c:\\games\\waw"),
            "case-insensitive descendant was not detected");
        require(
            !is_same_or_descendant(L"C:\\Games\\WaW-VR", L"C:\\Games\\WaW"),
            "sibling path was incorrectly treated as a descendant");
        require(is_same_path(
                    L"C:\\LOCAL DATA\\WaWVR\\runtime\\.\\CoDWaW.exe",
                    L"c:\\local data\\wawvr\\runtime\\CoDWaW.exe"),
                "case/separator-normalized exact path was not detected");
        require(!is_same_path(
                    L"C:\\Local Data\\WaWVR\\runtime\\other.exe",
                    L"C:\\Local Data\\WaWVR\\runtime\\CoDWaW.exe"),
                "different runtime source was treated as the staged executable");
        require(!is_same_path({}, L"C:\\CoDWaW.exe"),
                "empty path was treated as an exact staged source");

        require(is_safe_runtime_link_name(L"zone"), "safe link name was rejected");
        require(is_safe_runtime_link_name(L"main"), "safe link name was rejected");
        require(!is_safe_runtime_link_name(L"..\\zone"), "parent path escape was accepted");
        require(!is_safe_runtime_link_name(L"main\\video"), "nested link name was accepted");
        require(!is_safe_runtime_link_name(L"C:\\zone"), "absolute link name was accepted");
        require(!is_safe_runtime_link_name(L"."), "dot link name was accepted");
    }

    void test_multiplayer_handoff_policy(const fs::path& root)
    {
        require(is_multiplayer_handoff_invocation(
                    L"C:\\WaWVR Runtime\\CoDWaWmp.exe"),
                "exact runtime handoff basename was not recognized");
        require(is_multiplayer_handoff_invocation(
                    L"C:\\WaWVR Runtime\\codwawMP.EXE"),
                "runtime handoff basename classification is not case-insensitive");
        require(!is_multiplayer_handoff_invocation(
                    L"C:\\WaWVR Runtime\\WorldWarVR.exe") &&
                    !is_multiplayer_handoff_invocation(
                        L"C:\\WaWVR Runtime\\CoDWaWmp.exe.bak") &&
                    !is_multiplayer_handoff_invocation(
                        L"C:\\WaWVR Runtime\\evil-CoDWaWmp.exe"),
                "a non-exact launcher basename activated the MP handoff");

        const fs::path shim = L"C:\\WaWVR Runtime\\CoDWaWmp.exe";
        require(is_expected_handoff_parent_path(
                    L"C:\\WaWVR Runtime\\CoDWaW.exe", shim),
                "exact sibling stock SP parent path was rejected");
        require(!is_expected_handoff_parent_path(
                    L"C:\\Other Runtime\\CoDWaW.exe", shim) &&
                    !is_expected_handoff_parent_path(
                        L"C:\\WaWVR Runtime\\t4sp.exe", shim),
                "non-sibling or non-staged SP parent path was accepted");

        require(classify_handoff_parent_wait(true, WAIT_OBJECT_0) ==
                    HandoffParentWaitResult::ready,
                "signaled exact SP parent did not release the handoff");
        require(classify_handoff_parent_wait(true, WAIT_TIMEOUT) ==
                    HandoffParentWaitResult::timed_out,
                "SP parent wait timeout was not fail-closed");
        require(classify_handoff_parent_wait(true, WAIT_FAILED) ==
                    HandoffParentWaitResult::wait_failed,
                "SP parent wait failure was not fail-closed");
        require(classify_handoff_parent_wait(false, WAIT_OBJECT_0) ==
                    HandoffParentWaitResult::rejected_parent,
                "a signaled but unverified parent released the handoff");

        LaunchPlan sp_plan;
        sp_plan.executable_kind = ExecutableKind::sp;
        sp_plan.executable_identity =
            ExecutableIdentityId::plutonium_sp_1_7_1263;
        sp_plan.game_dir = (root / L"handoff-game").lexically_normal();
        sp_plan.runtime_dir =
            (root / L"handoff-stage" / L"runtime" / L"waw-1.7.1263")
                .lexically_normal();
        sp_plan.home_dir =
            (root / L"handoff-stage" / L"home").lexically_normal();
        sp_plan.handoff_mp_source_exe =
            (root / L"owned-source" / L"t4mp.exe").lexically_normal();
        sp_plan.handoff_mp_executable_identity =
            ExecutableIdentityId::plutonium_mp_1_7_1263;
        sp_plan.source_resolution = kPerformanceSourceResolution;
        const auto config = derive_multiplayer_handoff_config(sp_plan);
        const auto runtime_shim = sp_plan.runtime_dir / L"CoDWaWmp.exe";
        require(config.game_dir == sp_plan.game_dir &&
                    config.sp_runtime_dir == sp_plan.runtime_dir &&
                    config.sp_home_dir == sp_plan.home_dir &&
                    config.mp_source_exe == *sp_plan.handoff_mp_source_exe &&
                    config.mp_runtime_dir ==
                        sp_plan.runtime_dir.parent_path() / L"waw-mp-1.7.1263" &&
                    config.mp_home_dir ==
                        sp_plan.home_dir.parent_path() / L"home-mp" &&
                    config.sp_executable_identity ==
                        ExecutableIdentityId::plutonium_sp_1_7_1263 &&
                    config.mp_executable_identity ==
                        ExecutableIdentityId::plutonium_mp_1_7_1263 &&
                    config.source_resolution == kDefaultSourceResolution &&
                    config.bot_policy == BotPolicy::automatic,
                "SP plan did not derive the exact isolated MP handoff paths or legacy resolution cap");
        require(derive_multiplayer_runtime_dir(sp_plan.runtime_dir) ==
                    config.mp_runtime_dir &&
                    derive_multiplayer_home_dir(sp_plan.home_dir) ==
                        config.mp_home_dir,
                "standalone MP runtime/home derivation changed");

        const auto serialized = serialize_multiplayer_handoff_config(config);
        require(serialized.starts_with("WAWVR_MULTIPLAYER_HANDOFF_V3\n") &&
                    serialized.ends_with("bot_policy=automatic\n") &&
                    serialized.find("C:\\") == std::string::npos &&
                    parse_multiplayer_handoff_config(serialized, runtime_shim) ==
                        config,
                "canonical MP handoff serialization did not round-trip");
        auto legacy_v2_serialized = serialized;
        legacy_v2_serialized.replace(
            0,
            std::string("WAWVR_MULTIPLAYER_HANDOFF_V3").size(),
            "WAWVR_MULTIPLAYER_HANDOFF_V2");
        const auto erase_field = [](std::string& fixture,
                                    const std::string_view key) {
            const auto begin = fixture.find(
                "\n" + std::string(key));
            require(begin != std::string::npos,
                    "could not construct legacy handoff fixture");
            const auto end = fixture.find('\n', begin + 1U);
            require(end != std::string::npos,
                    "legacy handoff fixture field was not terminated");
            fixture.erase(begin + 1U, end - begin);
        };
        erase_field(legacy_v2_serialized, "bot_policy=");
        require(
            parse_multiplayer_handoff_config(
                legacy_v2_serialized, runtime_shim) == config,
            "legacy V2 handoff did not migrate to automatic bot policy");

        auto legacy_serialized = legacy_v2_serialized;
        legacy_serialized.replace(
            0,
            std::string("WAWVR_MULTIPLAYER_HANDOFF_V2").size(),
            "WAWVR_MULTIPLAYER_HANDOFF_V1");
        erase_field(legacy_serialized, "sp_executable_identity=");
        erase_field(legacy_serialized, "mp_executable_identity=");
        require(
            parse_multiplayer_handoff_config(
                legacy_serialized, runtime_shim) == config,
            "legacy V1 handoff did not migrate to exact legacy identities");

        const auto options = make_multiplayer_handoff_options(
            runtime_shim, config);
        require(options.mode == Mode::launch &&
                    options.launch_target == LaunchTarget::offline_multiplayer &&
                    options.game_dir == config.game_dir &&
                    options.source_exe == config.mp_source_exe &&
                    options.runtime_dir == config.mp_runtime_dir &&
                    options.home_dir == config.mp_home_dir &&
                    options.source_resolution == config.source_resolution &&
                    options.bot_policy == BotPolicy::automatic &&
                    options.required_source_identity ==
                        config.mp_executable_identity &&
                    options.launcher_executable == fs::absolute(runtime_shim) &&
                    options.mod_dll ==
                        fs::absolute(runtime_shim).parent_path() /
                            L"WorldWarVR.dll" &&
                    options.stock_multiplayer_handoff,
                "runtime shim options did not come exclusively from the config");

        auto disabled_plan = sp_plan;
        disabled_plan.bot_policy = BotPolicy::disabled;
        const auto disabled_config =
            derive_multiplayer_handoff_config(disabled_plan);
        require(
            disabled_config.bot_policy == BotPolicy::disabled &&
                serialize_multiplayer_handoff_config(disabled_config).ends_with(
                    "bot_policy=disabled\n") &&
                make_multiplayer_handoff_options(
                    runtime_shim, disabled_config).bot_policy ==
                    BotPolicy::disabled,
            "disabled bot policy did not survive the SP -> MP handoff");

        auto recovery_plan = sp_plan;
        recovery_plan.source_resolution = kFallbackSourceResolution;
        require(
            derive_multiplayer_handoff_config(recovery_plan).source_resolution ==
                kFallbackSourceResolution,
            "SP -> MP handoff raised an explicit recovery resolution");

        auto steam_plan = sp_plan;
        steam_plan.executable_identity =
            ExecutableIdentityId::steam_sp_build_252004;
        steam_plan.runtime_dir =
            (root / L"steam-handoff-stage" / L"runtime" /
             L"waw-steam-252004-1.7").lexically_normal();
        steam_plan.home_dir =
            (root / L"steam-handoff-stage" / L"home-steam-252004")
                .lexically_normal();
        steam_plan.handoff_mp_source_exe =
            (root / L"steam-owned-source" / L"CoDWaWmp.exe")
                .lexically_normal();
        steam_plan.handoff_mp_executable_identity =
            ExecutableIdentityId::steam_mp_build_252004;
        const auto steam_config = derive_multiplayer_handoff_config(steam_plan);
        require(
            steam_config.sp_executable_identity ==
                    ExecutableIdentityId::steam_sp_build_252004 &&
                steam_config.mp_executable_identity ==
                    ExecutableIdentityId::steam_mp_build_252004 &&
                steam_config.mp_runtime_dir.filename() ==
                    L"waw-mp-steam-252004-1.7" &&
                steam_config.mp_home_dir.filename() ==
                    L"home-mp-steam-252004" &&
                parse_multiplayer_handoff_config(
                    serialize_multiplayer_handoff_config(steam_config),
                    steam_plan.runtime_dir / L"CoDWaWmp.exe") == steam_config,
            "Steam SP identity did not bind an exact Steam MP V2 handoff");

        fs::create_directories(sp_plan.runtime_dir);
        std::ofstream(
            sp_plan.runtime_dir / kPezBotArchiveFilename,
            std::ios::binary) << "must not be discovered by the managed shim";
        const auto resolved_handoff = resolve_plan(options);
        require(resolved_handoff.stock_multiplayer_handoff &&
                    resolved_handoff.home_dir_is_launcher_managed &&
                    !resolved_handoff.pezbot_archive &&
                    !should_import_optional_pezbot(resolved_handoff) &&
                    pezbot_install_directory(resolved_handoff) ==
                        config.mp_home_dir / L"mods" / kPezBotModFolderName &&
                    !fs::exists(config.mp_home_dir),
                "managed shim discovered/imported PeZBOT instead of using the prepared MP home");

        auto direct_mp = resolved_handoff;
        direct_mp.stock_multiplayer_handoff = false;
        require(should_import_optional_pezbot(direct_mp),
                "ordinary direct MP plan unexpectedly disabled optional PeZBOT import");
        direct_mp.bot_policy = BotPolicy::enabled;
        require(should_import_optional_pezbot(direct_mp),
                "explicitly enabled direct MP plan disabled optional PeZBOT import");
        direct_mp.bot_policy = BotPolicy::disabled;
        direct_mp.pezbot_enabled = true;
        require(!should_import_optional_pezbot(direct_mp) &&
                    !inspect_optional_pezbot(direct_mp).enabled() &&
                    build_game_command_line(direct_mp).find(
                        L"+set wawvr_pezbot_autofill 0") !=
                        std::wstring::npos &&
                    build_game_command_line(direct_mp).find(
                        L"+set wawvr_pezbot_autofill 9") ==
                        std::wstring::npos,
                "disabled bot policy discovered, enabled, or autofilled PeZBOT");

        require(multiplayer_handoff_config_path(runtime_shim) ==
                    sp_plan.runtime_dir / kMultiplayerHandoffConfigFilename,
                "handoff config was not confined to the isolated SP runtime");

        auto expect_rejected = [](const auto& action, const char* message) {
            bool rejected = false;
            try
            {
                action();
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            require(rejected, message);
        };
        expect_rejected(
            [&]() {
                static_cast<void>(parse_multiplayer_handoff_config(
                    "WAWVR_MULTIPLAYER_HANDOFF_V1\n", runtime_shim));
            },
            "truncated MP handoff config did not fail closed");

        auto invalid_bot_policy = serialized;
        const auto automatic_policy = invalid_bot_policy.find(
            "bot_policy=automatic\n");
        require(automatic_policy != std::string::npos,
                "could not construct invalid bot-policy handoff fixture");
        invalid_bot_policy.replace(
            automatic_policy,
            std::string("bot_policy=automatic").size(),
            "bot_policy=always");
        expect_rejected(
            [&]() {
                static_cast<void>(parse_multiplayer_handoff_config(
                    invalid_bot_policy, runtime_shim));
            },
            "unknown MP handoff bot policy did not fail closed");

        auto other = config;
        other.sp_runtime_dir =
            (root / L"other-stage" / L"waw-1.7.1263").lexically_normal();
        other.mp_runtime_dir =
            derive_multiplayer_runtime_dir(other.sp_runtime_dir);
        const auto other_serialized =
            serialize_multiplayer_handoff_config(other);
        expect_rejected(
            [&]() {
                static_cast<void>(parse_multiplayer_handoff_config(
                    other_serialized, runtime_shim));
            },
            "config generated for another SP stage was accepted");

        auto unsafe = config;
        unsafe.mp_source_exe =
            config.game_dir / L"main" / L".." / L"CoDWaWmp.exe";
        expect_rejected(
            [&]() {
                static_cast<void>(serialize_multiplayer_handoff_config(unsafe));
            },
            "non-canonical MP source path was serialized");

        auto unc = config;
        unc.mp_source_exe = L"\\\\server\\share\\t4mp.exe";
        expect_rejected(
            [&]() {
                static_cast<void>(serialize_multiplayer_handoff_config(unc));
            },
            "UNC MP source path was accepted by the local-stage handoff");

        expect_rejected(
            [&]() {
                static_cast<void>(read_multiplayer_handoff_config(runtime_shim));
            },
            "absent runtime handoff config did not fail closed");
        const auto config_path = multiplayer_handoff_config_path(runtime_shim);
        std::ofstream(config_path, std::ios::binary) << "malformed\n";
        expect_rejected(
            [&]() {
                static_cast<void>(read_multiplayer_handoff_config(runtime_shim));
            },
            "malformed runtime handoff config did not fail closed");
        std::ofstream(config_path, std::ios::binary | std::ios::trunc)
            << serialized;
        require(read_multiplayer_handoff_config(runtime_shim) == config,
                "runtime-local handoff config was not read exactly");
    }

    void test_automatic_source_selection()
    {
        require(
            std::string_view(kExpectedSteamSpExeSha256) ==
                "732900D158982C33E3121F0B86D22230BE79839BBCBFE3BDFC1238F408A7D64D" &&
            std::string_view(kExpectedSteamMpExeSha256) ==
                "7D0B518A4BD267FFDB6D0203AD8F3721603B172AC13BA2ABCDB32584F759D36C",
            "pinned Steam Build 252004 executable hashes changed");
        require(
            std::string_view(kExpectedMpExeSha256) ==
                "943BB93001AD2ED465B6652C27FB649B5F0C5B24097E18A27A588AC35B3457A0",
            "pinned MP executable SHA-256 changed");
        require(
            std::string_view(kExpectedPezBotArchiveSha256) ==
                "B7958B96CBE3A8C316290DF7148C63CA601D1DE2F96F6166D2C67FE069500FDF",
            "pinned PeZBOT archive SHA-256 changed");
        const fs::path stage = L"C:\\managed\\CoDWaW.exe";
        const fs::path game = L"E:\\game\\CoDWaW.exe";
        const fs::path legacy = L"C:\\legacy\\t4sp.exe";
        require(select_automatic_source(stage, true, game, true, legacy) == game,
                "genuine retail filename did not win automatic discovery");
        require(select_automatic_source(stage, false, game, true, legacy) == game,
                "owned game executable did not replace an unverified stage");
        require(select_automatic_source(stage, false, game, false, legacy) == legacy,
                "legacy first-run source fallback was not retained");

        const fs::path custom_stage = L"D:\\custom-runtime\\CoDWaW.exe";
        require(select_automatic_source(
                    custom_stage, true, game, true, legacy) == game,
                "retail source did not outrank a custom managed stage");

        const fs::path mp_stage = L"C:\\managed-mp\\CoDWaWmp.exe";
        const fs::path mp_game = L"E:\\game\\CoDWaWmp.exe";
        const fs::path mp_legacy = L"C:\\legacy\\t4mp.exe";
        require(select_automatic_source(
                    mp_stage, true, mp_game, true, mp_legacy) == mp_game,
                "genuine retail MP filename did not win automatic discovery");
        require(select_automatic_source(
                    mp_stage, false, mp_game, true, mp_legacy) == mp_game,
                "owned game MP executable did not replace an unverified stage");
        require(select_automatic_source(
                    mp_stage, false, mp_game, false, mp_legacy) == mp_legacy,
                "legacy first-run MP source fallback was not retained");
    }

    void test_executable_identities_and_steam_environment()
    {
        require(
            executable_identity_from_sha256(
                ExecutableKind::sp, kExpectedSpExeSha256) ==
                    ExecutableIdentityId::plutonium_sp_1_7_1263 &&
            executable_identity_from_sha256(
                ExecutableKind::mp, kExpectedMpExeSha256) ==
                    ExecutableIdentityId::plutonium_mp_1_7_1263 &&
            executable_identity_from_sha256(
                ExecutableKind::sp, kExpectedSteamSpExeSha256) ==
                    ExecutableIdentityId::steam_sp_build_252004 &&
            executable_identity_from_sha256(
                ExecutableKind::mp, kExpectedSteamMpExeSha256) ==
                    ExecutableIdentityId::steam_mp_build_252004,
            "one of the four exact executable hashes did not resolve");
        require(
            !executable_identity_from_sha256(
                ExecutableKind::mp, kExpectedSteamSpExeSha256) &&
            !executable_identity_from_sha256(
                ExecutableKind::sp, kExpectedSteamMpExeSha256) &&
            !executable_identity_from_sha256(
                ExecutableKind::sp,
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"),
            "wrong-kind or unknown executable hash did not fail closed");
        require(
            executable_kind_for_identity(
                ExecutableIdentityId::steam_sp_build_252004) ==
                    ExecutableKind::sp &&
            executable_kind_for_identity(
                ExecutableIdentityId::steam_mp_build_252004) ==
                    ExecutableKind::mp &&
            executable_identity_uses_steam(
                ExecutableIdentityId::steam_sp_build_252004) &&
            !executable_identity_uses_steam(
                ExecutableIdentityId::plutonium_sp_1_7_1263),
            "stable identity metadata is inconsistent");

        bool unknown_identity_rejected = false;
        try
        {
            static_cast<void>(executable_identity_stable_id(
                static_cast<ExecutableIdentityId>(0xFF)));
        }
        catch (const std::invalid_argument&)
        {
            unknown_identity_rejected = true;
        }
        require(unknown_identity_rejected,
                "unknown executable identity silently fell back to a known profile");

        const std::vector<std::wstring> inherited = {
            L"Path=C:\\Windows",
            L"steamappid=old",
            L"STEAMGAMEID=old",
            L"WAWVR_TURN_MODE=smooth",
            L"WAWVR_DEFER_XR_BEGIN_FRAME=1",
            L"XR_RUNTIME_JSON=C:\\Pimax\\Runtime\\PiOpenXR_32.json",
            L"WAWVR_OPENXR_RUNTIME_SELECTION=pimax-x86",
            L"=C:=C:\\fixture",
        };
        require(
            build_child_environment_block(
                ExecutableIdentityId::plutonium_sp_1_7_1263,
                inherited).empty(),
            "legacy executable unexpectedly received a custom child environment");
        const auto block = build_child_environment_block(
            ExecutableIdentityId::steam_sp_build_252004,
            inherited);
        require(block.size() >= 2U && block.back() == L'\0' &&
                    block[block.size() - 2U] == L'\0',
                "Steam child environment is not double-NUL terminated");
        std::vector<std::wstring> entries;
        for (std::size_t index = 0; index + 1U < block.size();)
        {
            if (block[index] == L'\0')
            {
                break;
            }
            const std::wstring entry(block.data() + index);
            entries.push_back(entry);
            index += entry.size() + 1U;
        }
        const auto count_case_insensitive = [&](const std::wstring_view expected) {
            return std::count_if(entries.begin(), entries.end(), [&](const auto& entry) {
                return CompareStringOrdinal(
                           entry.c_str(), -1,
                           expected.data(), static_cast<int>(expected.size()), TRUE) ==
                    CSTR_EQUAL;
            });
        };
        require(count_case_insensitive(L"SteamAppId=10090") == 1 &&
                    count_case_insensitive(L"SteamGameId=10090") == 1 &&
                    count_case_insensitive(L"steamappid=old") == 0 &&
                     count_case_insensitive(L"STEAMGAMEID=old") == 0 &&
                     count_case_insensitive(L"Path=C:\\Windows") == 1 &&
                     count_case_insensitive(L"WAWVR_TURN_MODE=smooth") == 1 &&
                     count_case_insensitive(L"WAWVR_DEFER_XR_BEGIN_FRAME=1") == 1 &&
                     count_case_insensitive(
                         L"XR_RUNTIME_JSON=C:\\Pimax\\Runtime\\PiOpenXR_32.json") == 1 &&
                     count_case_insensitive(
                         L"WAWVR_OPENXR_RUNTIME_SELECTION=pimax-x86") == 1 &&
                     count_case_insensitive(L"=C:=C:\\fixture") == 1,
                "Steam child environment did not preserve gameplay and OpenXR runtime markers while replacing only its two Steam variables");
    }

    void test_multiplayer_child_disables_deferred_frame_begin()
    {
        const std::vector<std::wstring> base_environment = {
            L"Path=C:\\Windows",
            L"SteamAppId=old",
            L"SteamGameId=old",
            L"WAWVR_TURN_MODE=smooth",
            L"XR_RUNTIME_JSON=C:\\Runtime\\openxr.json",
            L"=C:=C:\\fixture",
        };
        const auto manual_sp = build_child_environment_block(
            ExecutableIdentityId::steam_sp_build_252004, base_environment);
        require(std::wstring(manual_sp.data(), manual_sp.size()).find(
                    L"WAWVR_DEFER_XR_BEGIN_FRAME") == std::wstring::npos,
                "manual SP launch unexpectedly opted into deferred frame begin");

        for (const auto identity : {ExecutableIdentityId::plutonium_mp_1_7_1263,
                                    ExecutableIdentityId::steam_mp_build_252004})
        {
            for (const bool from_sp_handoff : {false, true})
            {
                auto inherited = base_environment;
                if (from_sp_handoff)
                {
                    inherited.emplace_back(L"WAWVR_DEFER_XR_BEGIN_FRAME=1");
                    inherited.emplace_back(L"wawvr_defer_xr_begin_frame=1");
                }
                const auto block = build_child_environment_block(identity, inherited);
                require(block.size() >= 2U && block.back() == L'\0' &&
                            block[block.size() - 2U] == L'\0',
                        "MP child environment is not double-NUL terminated");
                std::vector<std::wstring> entries;
                for (std::size_t index = 0; index + 1U < block.size() && block[index];)
                {
                    entries.emplace_back(block.data() + index);
                    index += entries.back().size() + 1U;
                }
                auto expected = base_environment;
                if (executable_identity_uses_steam(identity))
                {
                    expected[1] = L"SteamAppId=10090";
                    expected[2] = L"SteamGameId=10090";
                }
                expected.emplace_back(L"WAWVR_DEFER_XR_BEGIN_FRAME=0");
                std::sort(entries.begin(), entries.end());
                std::sort(expected.begin(), expected.end());
                require(entries == expected,
                        "MP child did not disable inherited SP frame timing while preserving unrelated environment");
            }
        }
    }

    void test_target_specific_plan_defaults(const fs::path& root)
    {
        const wchar_t* const game_directory_variable = L"WAWVR_GAME_DIR";
        const DWORD game_directory_needed =
            GetEnvironmentVariableW(game_directory_variable, nullptr, 0);
        std::optional<std::wstring> previous_game_directory;
        if (game_directory_needed != 0)
        {
            std::wstring value(game_directory_needed, L'\0');
            const DWORD written = GetEnvironmentVariableW(
                game_directory_variable, value.data(), game_directory_needed);
            require(written != 0 && written < game_directory_needed,
                    "could not preserve game-directory environment setting");
            value.resize(written);
            previous_game_directory = std::move(value);
        }
        require(SetEnvironmentVariableW(game_directory_variable, nullptr) != FALSE,
                "could not clear game-directory environment fixture");
        bool missing_game_directory_rejected = false;
        try
        {
            Options missing_game_directory;
            missing_game_directory.source_exe = L"C:\\fixture-source\\t4sp.exe";
            missing_game_directory.runtime_dir = root / L"unprepared-runtime";
            static_cast<void>(resolve_plan(missing_game_directory));
        }
        catch (const std::invalid_argument& error)
        {
            missing_game_directory_rejected =
                std::string(error.what()).find("Game directory is required") !=
                std::string::npos;
        }
        require(SetEnvironmentVariableW(
                    game_directory_variable,
                    previous_game_directory
                        ? previous_game_directory->c_str()
                        : nullptr) != FALSE,
                "could not restore game-directory environment setting");
        require(missing_game_directory_rejected,
                "plan resolution retained a machine-specific game-directory fallback");

        Options sp_options;
        sp_options.game_dir = L"C:\\fixture-game";
        sp_options.source_exe = L"C:\\fixture-source\\t4sp.exe";
        sp_options.handoff_mp_source_exe =
            L"D:\\trusted-launch-plan\\CoDWaWmp.exe";

        const wchar_t* const mp_source_variable = L"WAWVR_MP_SOURCE_EXE";
        const DWORD needed =
            GetEnvironmentVariableW(mp_source_variable, nullptr, 0);
        std::optional<std::wstring> previous_mp_source;
        if (needed != 0)
        {
            std::wstring value(needed, L'\0');
            const DWORD written = GetEnvironmentVariableW(
                mp_source_variable, value.data(), needed);
            require(written != 0 && written < needed,
                    "could not preserve MP source environment setting");
            value.resize(written);
            previous_mp_source = std::move(value);
        }
        const auto restore_mp_source = [&]() {
            SetEnvironmentVariableW(
                mp_source_variable,
                previous_mp_source ? previous_mp_source->c_str() : nullptr);
        };
        require(SetEnvironmentVariableW(
                    mp_source_variable,
                    L"C:\\ambient-fallback\\t4mp.exe") != FALSE,
                "could not set MP source environment fixture");
        LaunchPlan sp;
        try
        {
            sp = resolve_plan(sp_options);
        }
        catch (...)
        {
            restore_mp_source();
            throw;
        }
        restore_mp_source();
        require(sp.executable_kind == ExecutableKind::sp &&
                    sp.executable_identity ==
                        ExecutableIdentityId::plutonium_sp_1_7_1263 &&
                    sp.staged_exe.filename() == L"CoDWaW.exe",
                "SP plan lost its executable identity");
        require(sp.runtime_dir.filename() == L"waw-1.7.1263" &&
                    sp.home_dir.filename() == L"home" &&
                    sp.home_dir_is_launcher_managed,
                "SP runtime/home defaults changed");
        require(sp.handoff_mp_source_exe ==
                    fs::absolute(*sp_options.handoff_mp_source_exe)
                        .lexically_normal(),
                "explicit SP --mp-source-exe did not override ambient discovery");

        Options mp_options;
        mp_options.launch_target = LaunchTarget::offline_multiplayer;
        mp_options.game_dir = L"C:\\fixture-game";
        mp_options.source_exe = L"C:\\fixture-source\\t4mp.exe";
        mp_options.handoff_mp_source_exe =
            L"D:\\invalid-mp-handoff-source\\t4mp.exe";
        bool rejected_mp_handoff_source = false;
        try
        {
            static_cast<void>(resolve_plan(mp_options));
        }
        catch (const std::invalid_argument&)
        {
            rejected_mp_handoff_source = true;
        }
        require(rejected_mp_handoff_source,
                "programmatic MP plan accepted an SP handoff source");
        mp_options.handoff_mp_source_exe.reset();
        const auto mp = resolve_plan(mp_options);
        require(mp.executable_kind == ExecutableKind::mp &&
                    mp.executable_identity ==
                        ExecutableIdentityId::plutonium_mp_1_7_1263 &&
                    mp.staged_exe.filename() == L"CoDWaWmp.exe",
                "offline multiplayer plan did not select the MP executable identity");
        require(mp.runtime_dir.filename() == L"waw-mp-1.7.1263" &&
                    mp.home_dir.filename() == L"home-mp" &&
                    mp.home_dir_is_launcher_managed &&
                    mp.runtime_dir != sp.runtime_dir && mp.home_dir != sp.home_dir,
                "offline multiplayer runtime/home defaults are not isolated from SP");

        mp_options.runtime_dir = L"D:\\custom-mp-runtime";
        mp_options.home_dir = L"D:\\custom-mp-home";
        const auto overridden = resolve_plan(mp_options);
        require(overridden.runtime_dir == fs::absolute(*mp_options.runtime_dir) &&
                    overridden.home_dir == fs::absolute(*mp_options.home_dir) &&
                    !overridden.home_dir_is_launcher_managed &&
                    overridden.staged_exe ==
                        fs::absolute(*mp_options.runtime_dir) / L"CoDWaWmp.exe",
                "explicit offline multiplayer isolation paths were not preserved");
    }

    void remove_runtime_junctions(const fs::path& runtime)
    {
        RemoveDirectoryW((runtime / L"zone").c_str());
        RemoveDirectoryW((runtime / L"main").c_str());
    }

    void create_fake_game_data(const fs::path& game, const std::string& marker)
    {
        fs::create_directories(game / L"zone" / L"english");
        fs::create_directories(game / L"main" / L"video");
        std::ofstream(game / L"localization.txt", std::ios::binary) <<
            "english\r\n\r\nWIN_ERROR\r\n\"Fatal Error\"\r\n";
        std::ofstream(game / L"zone" / L"english" / L"code_post_gfx.ff") << marker;
        std::ofstream(game / L"main" / L"iw_00.iwd") << marker;
        std::ofstream(game / L"main" / L"video" / L"intro.bik") << marker;
    }

    void test_game_language_detection_and_cache_refresh(const fs::path& root)
    {
        struct LanguageCase
        {
            std::string_view token;
            GameLanguage language;
            std::wstring_view code;
            std::wstring_view display_name;
        };
        constexpr std::array<LanguageCase, 5> cases = {{
            {"english", GameLanguage::english, L"english", L"English"},
            {"french", GameLanguage::french, L"french", L"French"},
            {"italian", GameLanguage::italian, L"italian", L"Italian"},
            {"german", GameLanguage::german, L"german", L"German"},
            {"spanish", GameLanguage::spanish, L"spanish", L"Spanish (Spain)"},
        }};
        for (const auto& language_case : cases)
        {
            const auto selector = std::string("\r\n\t\r\n") +
                std::string(language_case.token) + "\r\nlocalized contents";
            require(parse_game_language_selector(selector) == language_case.language,
                    "supported installed language selector was rejected");
            require(std::wstring_view(game_language_code(language_case.language)) ==
                        language_case.code &&
                    std::wstring_view(
                        game_language_display_name(language_case.language)) ==
                        language_case.display_name,
                    "installed language code/display mapping changed");
        }

        for (const auto invalid : {
                 std::string_view(),
                 std::string_view("\r\n\t\r\n"),
                 std::string_view("English\r\n"),
                 std::string_view(" french\r\n"),
                 std::string_view("french \r\n"),
                 std::string_view("../english\r\n"),
                 std::string_view("french/../english\r\n"),
                 std::string_view("polish\r\n"),
                 std::string_view("\xEF\xBB\xBF" "french\r\n")})
        {
            bool rejected = false;
            try
            {
                static_cast<void>(parse_game_language_selector(invalid));
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            require(rejected, "unsafe or unsupported installed language was accepted");
        }
        const std::string embedded_nul("french\0english", 14);
        bool rejected_nul = false;
        try
        {
            static_cast<void>(parse_game_language_selector(embedded_nul));
        }
        catch (const std::invalid_argument&)
        {
            rejected_nul = true;
        }
        require(rejected_nul, "NUL-containing installed language selector was accepted");

        const auto game = root / L"language-game";
        const auto runtime = root / L"language-runtime";
        constexpr std::string_view english_contents =
            "english\r\n\r\nWIN_ERROR\r\n\"Fatal Error\"\r\n";
        constexpr std::string_view french_contents =
            "french\r\n\r\nWIN_ERROR\r\n\"Erreur fatale\"\r\n";
        fs::create_directories(game / L"zone" / L"english");
        fs::create_directories(game / L"zone" / L"french");
        fs::create_directories(runtime);
        const auto selector_path = game / L"localization.txt";
        const auto cached_selector_path = runtime / L"localization.txt";
        write_binary_file(selector_path, english_contents);
        write_binary_file(cached_selector_path, "stale cached selector");

        const auto english = detect_installed_game_language(game);
        require(english.language == GameLanguage::english &&
                    english.selector_sha256 == sha256_file(selector_path),
                "English installation language was not detected exactly");

        LaunchPlan plan;
        plan.game_dir = game;
        plan.runtime_dir = runtime;
        const auto english_support = runtime_support_files(plan);
        const auto find_selector = [](const auto& files) {
            return std::find_if(files.begin(), files.end(), [](const auto& file) {
                return file.destination.filename() == L"localization.txt";
            });
        };
        const auto english_selector = find_selector(english_support);
        require(english_selector != english_support.end() &&
                    english_selector->expected_sha256 == english.selector_sha256,
                "English runtime selector did not use the installed source digest");

        const auto has_selector_temporary = [&runtime] {
            for (const auto& entry : fs::directory_iterator(runtime))
            {
                const auto filename = entry.path().filename().wstring();
                if (filename.rfind(L"localization.txt.tmp-", 0) == 0)
                {
                    return true;
                }
            }
            return false;
        };
        stage_runtime_language_selector(plan);
        require(read_binary_file(cached_selector_path) == english_contents &&
                    sha256_file(cached_selector_path) == english.selector_sha256,
                "stale cached English selector was not physically refreshed");
        require(!has_selector_temporary(),
                "successful English selector staging left a temporary file");

        write_binary_file(selector_path, french_contents);
        const auto french = detect_installed_game_language(game);
        const auto french_support = runtime_support_files(plan);
        const auto french_selector = find_selector(french_support);
        require(french.language == GameLanguage::french &&
                    french_selector != french_support.end() &&
                    french_selector->expected_sha256 == french.selector_sha256 &&
                    french_selector->expected_sha256 !=
                        english_selector->expected_sha256 &&
                    french_selector->destination == english_selector->destination,
                "same-runtime English-to-French selector refresh was not planned");

        stage_runtime_language_selector(plan);
        require(read_binary_file(cached_selector_path) == french_contents &&
                    sha256_file(cached_selector_path) == french.selector_sha256,
                "same-runtime English-to-French selector was not physically refreshed");
        require(!has_selector_temporary(),
                "successful French selector staging left a temporary file");

        write_binary_file(selector_path, english_contents);
        stage_runtime_language_selector(plan);
        const auto preserved_cached_selector = read_binary_file(cached_selector_path);
        require(preserved_cached_selector == english_contents,
                "English selector could not be restored before failure testing");

        write_binary_file(selector_path, french_contents);
        fs::remove(game / L"zone" / L"french");
        bool rejected_missing_zone = false;
        try
        {
            stage_runtime_language_selector(plan);
        }
        catch (const std::invalid_argument&)
        {
            rejected_missing_zone = true;
        }
        require(rejected_missing_zone,
                "installed language without zone assets was accepted");
        require(read_binary_file(cached_selector_path) == preserved_cached_selector,
                "missing French zone assets modified the cached selector");
        require(!has_selector_temporary(),
                "rejected French selector staging left a temporary file");

        const auto diagnostic_game = root / L"french-diagnostic-game";
        const auto diagnostic_source = root / L"french-diagnostic-source.exe";
        fs::create_directories(diagnostic_game / L"zone" / L"english");
        fs::create_directories(diagnostic_game / L"zone" / L"french");
        fs::create_directories(diagnostic_game / L"main");
        std::ofstream(diagnostic_game / L"localization.txt", std::ios::binary) <<
            "french\r\n";
        std::ofstream(diagnostic_game / L"main" / L"iw_00.iwd") << "main";
        std::ofstream(diagnostic_game / L"zone" / L"english" /
                      L"nazi_zombie_prototype.ff") << "wrong-language-decoy";
        std::ofstream(diagnostic_game / L"zone" / L"french" /
                      L"nazi_zombie_prototype.ff") << "french-marker";
        std::ofstream(diagnostic_source) << "not-a-real-pe";
        LaunchPlan diagnostic_plan;
        diagnostic_plan.game_dir = diagnostic_game;
        diagnostic_plan.source_exe = diagnostic_source;
        diagnostic_plan.runtime_dir = root / L"french-diagnostic-runtime";
        diagnostic_plan.home_dir = root / L"french-diagnostic-home";
        diagnostic_plan.staged_exe =
            diagnostic_plan.runtime_dir / L"CoDWaW.exe";
        const auto report = diagnose(diagnostic_plan);
        const auto* language_check = find_check(report, L"installed game language");
        const auto* zone_check = find_check(
            report, L"selected language zone directory");
        const auto* nacht_marker = find_check(report, L"Nacht data marker");
        require(language_check != nullptr && language_check->passed &&
                    language_check->detail.find(L"French (french)") !=
                        std::wstring::npos &&
                    zone_check != nullptr && zone_check->passed &&
                    nacht_marker != nullptr && nacht_marker->passed,
                "French diagnostics did not select and display zone\\french");
        require(find_check(report, L"Nacht fastfile SHA-256") == nullptr,
                "French assets were incorrectly compared with the English fastfile hash");
    }

    void test_runtime_data_junctions(const fs::path& root)
    {
        const auto game_a = root / L"game-a";
        const auto game_b = root / L"game-b";
        const auto runtime = root / L"waw-1.7.1263";
        const auto home = root / L"home";
        const auto source_dir = root / L"source";
        create_fake_game_data(game_a, "a");
        create_fake_game_data(game_b, "b");
        fs::create_directories(runtime);
        fs::create_directories(home);
        fs::create_directories(source_dir);

        LaunchPlan plan;
        plan.game_dir = game_a;
        plan.runtime_dir = runtime;
        plan.home_dir = home;
        plan.source_exe = source_dir / L"t4sp.exe";

        const auto support_files = runtime_support_files(plan);
        require(support_files.size() == 4,
                "runtime root support plan has the wrong file count");
        constexpr std::array<std::wstring_view, 4> support_names = {
            L"binkw32.dll", L"localization.txt", L"cod.bmp", L"codlogo.bmp"};
        for (std::size_t index = 0; index < support_files.size(); ++index)
        {
            const auto& file = support_files[index];
            const fs::path support_name(support_names[index]);
            require(file.source == game_a / support_name &&
                        file.destination == runtime / support_name &&
                        file.source.parent_path() == game_a &&
                        file.destination.parent_path() == runtime &&
                        file.expected_sha256.size() == 64,
                    "runtime root support path/hash plan is incorrect");
            require(!is_same_or_descendant(file.destination, game_a),
                    "runtime root support plan writes into installed game data");
        }

        auto handoff_plan = plan;
        handoff_plan.launcher_executable = root / L"package" / L"WorldWarVR.exe";
        handoff_plan.mod_dll = root / L"package" / L"WorldWarVR.dll";
        const auto handoff_files = runtime_handoff_files(handoff_plan);
        require(handoff_files.size() == 2 &&
                    handoff_files[0].source == *handoff_plan.launcher_executable &&
                    handoff_files[0].destination == runtime / L"CoDWaWmp.exe" &&
                    handoff_files[1].source == *handoff_plan.mod_dll &&
                    handoff_files[1].destination == runtime / L"WorldWarVR.dll",
                "runtime-only stock multiplayer handoff plan is incorrect");
        for (const auto& file : handoff_files)
        {
            require(file.destination.parent_path() == runtime &&
                        !is_same_or_descendant(file.destination, game_a),
                    "runtime handoff plan writes into installed game data");
        }
        handoff_plan.executable_kind = ExecutableKind::mp;
        handoff_plan.executable_identity =
            ExecutableIdentityId::plutonium_mp_1_7_1263;
        require(runtime_handoff_files(handoff_plan).empty(),
                "proprietary MP runtime was confused with the launcher shim");

        const auto links = runtime_data_links(plan);
        require(links.size() == 2, "runtime link plan has the wrong directory count");
        require(links[0].relative_name == L"zone" && links[1].relative_name == L"main",
                "runtime link plan has the wrong directory names");

        prepare_runtime_data_links(plan);
        const auto zone_attributes = GetFileAttributesW((runtime / L"zone").c_str());
        const auto main_attributes = GetFileAttributesW((runtime / L"main").c_str());
        require(zone_attributes != INVALID_FILE_ATTRIBUTES &&
                    (zone_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0,
                "zone junction was not created");
        require(main_attributes != INVALID_FILE_ATTRIBUTES &&
                    (main_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0,
                "main junction was not created");
        require(fs::is_regular_file(runtime / L"zone" / L"english" / L"code_post_gfx.ff"),
                "zone marker is not visible through the runtime junction");
        require(fs::is_regular_file(runtime / L"main" / L"video" / L"intro.bik"),
                "nested main/video data is not visible through the runtime junction");

        const wchar_t* const game_directory_variable = L"WAWVR_GAME_DIR";
        const DWORD game_directory_needed =
            GetEnvironmentVariableW(game_directory_variable, nullptr, 0);
        std::optional<std::wstring> previous_game_directory;
        if (game_directory_needed != 0)
        {
            std::wstring value(game_directory_needed, L'\0');
            const DWORD written = GetEnvironmentVariableW(
                game_directory_variable, value.data(), game_directory_needed);
            require(written != 0 && written < game_directory_needed,
                    "could not preserve runtime-discovery environment fixture");
            value.resize(written);
            previous_game_directory = std::move(value);
        }
        const auto restore_game_directory = [&]() {
            SetEnvironmentVariableW(
                game_directory_variable,
                previous_game_directory ? previous_game_directory->c_str() : nullptr);
        };
        require(SetEnvironmentVariableW(game_directory_variable, nullptr) != FALSE,
                "could not clear runtime-discovery environment fixture");
        try
        {
            Options discovered_options;
            discovered_options.runtime_dir = runtime;
            discovered_options.home_dir = root / L"discovered-home";
            discovered_options.source_exe = source_dir / L"t4sp.exe";
            const auto discovered = resolve_plan(discovered_options);
            require(is_same_path(discovered.game_dir, game_a),
                    "paired managed runtime junctions did not recover the game root");

            Options multiplayer_from_sp;
            multiplayer_from_sp.launch_target = LaunchTarget::offline_multiplayer;
            multiplayer_from_sp.runtime_dir = root / L"waw-mp-1.7.1263";
            multiplayer_from_sp.home_dir = root / L"discovered-home-mp";
            multiplayer_from_sp.source_exe = source_dir / L"t4sp.exe";
            const auto multiplayer_discovered = resolve_plan(multiplayer_from_sp);
            require(is_same_path(multiplayer_discovered.game_dir, game_a),
                    "MP launcher did not recover the game root from SP-only preparation");

            discovered_options.game_dir = game_b;
            const auto explicit_plan = resolve_plan(discovered_options);
            require(is_same_path(explicit_plan.game_dir, game_b),
                    "explicit game root did not override prepared-runtime discovery");

            discovered_options.game_dir.reset();
            require(SetEnvironmentVariableW(
                        game_directory_variable, game_b.c_str()) != FALSE,
                    "could not set runtime-discovery precedence fixture");
            const auto environment_plan = resolve_plan(discovered_options);
            require(is_same_path(environment_plan.game_dir, game_b),
                    "environment game root did not override prepared-runtime discovery");
        }
        catch (...)
        {
            restore_game_directory();
            throw;
        }
        restore_game_directory();

        // A second preparation must validate and reuse the exact junctions.
        prepare_runtime_data_links(plan);

        auto mismatched = plan;
        mismatched.game_dir = game_b;
        bool rejected_mismatch = false;
        try
        {
            prepare_runtime_data_links(mismatched);
        }
        catch (const std::exception&)
        {
            rejected_mismatch = true;
        }
        require(rejected_mismatch, "mismatched pre-existing junction target was accepted");

        remove_runtime_junctions(runtime);

        // The launcher may reuse its exact managed staged executable as its
        // read-only source. This is the sole permitted source/runtime overlap.
        const auto managed_runtime = root / L"managed-runtime";
        const auto managed_home = root / L"managed-home";
        fs::create_directories(managed_runtime);
        fs::create_directories(managed_home);
        std::ofstream(managed_runtime / L"CoDWaW.exe") << "fixture";
        LaunchPlan managed = plan;
        managed.runtime_dir = managed_runtime;
        managed.home_dir = managed_home;
        managed.staged_exe = managed_runtime / L"CoDWaW.exe";
        managed.source_exe = managed.staged_exe;
        prepare_runtime_data_links(managed);
        require(
            (GetFileAttributesW((managed_runtime / L"zone").c_str()) &
             FILE_ATTRIBUTE_REPARSE_POINT) != 0,
            "exact managed staged self-source was rejected by write boundaries");
        remove_runtime_junctions(managed_runtime);

        managed.source_exe = managed_runtime / L"other.exe";
        bool rejected_other_runtime_source = false;
        try
        {
            prepare_runtime_data_links(managed);
        }
        catch (const std::exception&)
        {
            rejected_other_runtime_source = true;
        }
        require(rejected_other_runtime_source,
                "arbitrary source inside the managed runtime was accepted");
    }

    void test_command_line()
    {
        LaunchPlan plan;
        plan.staged_exe = L"C:\\Local Data\\WaWVR\\CoDWaW.exe";
        plan.game_dir = L"E:\\fixture-game";
        plan.home_dir = L"C:\\Local Data\\WaWVR\\home";
        const auto command = build_game_command_line(plan);
        require_exact_openxr_pacing_policy(command);
        require_exact_view_bob_policy(command);
        require_exact_dynamic_light_policy(command);
        require_exact_campaign_unlock_policy(command, true);
        require(command.find(L"+devmap nazi_zombie_prototype") != std::wstring::npos,
                "Nacht map command missing");
        require(command.find(L"+devmap nazi_zombie_factory") == std::wstring::npos,
                "Nacht command unexpectedly includes Der Riese");
        require(command.find(L"+set fs_basepath") != std::wstring::npos,
                "fs_basepath command missing");
        require(command.find(L"+set fs_homepath") != std::wstring::npos,
                "fs_homepath command missing");
        require(command.find(L"+set fs_localAppData") == std::wstring::npos,
                "SP command unexpectedly gained the MP-only local data root");
        require(command.find(L"+set r_customMode 2560x1440") != std::wstring::npos,
                "default high-clarity VR packed source resolution missing");
        require(command.find(L"+set r_fullscreen 0") != std::wstring::npos,
                "windowed mode required by T4 r_customMode path is missing");
        require(command.find(L"+set r_mode ") == std::wstring::npos,
                "launcher added an invalid IW3-style r_mode override for T4");
        require(command.find(L"+set r_aaSamples 1") != std::wstring::npos,
                "VR CPU-capture antialiasing override missing");
        require(command.find(L"+set r_dof_enable 0") != std::wstring::npos,
                "ADS depth-of-field blur disable missing");
        require(command.find(L"+set cg_drawCrosshair 0") != std::wstring::npos &&
                    command.find(L"+set cg_drawCrosshairNames 0") != std::wstring::npos &&
                    command.find(L"+set cg_drawTurretCrosshair 0") != std::wstring::npos &&
                    command.find(L"+set cg_crosshairAlpha 0") != std::wstring::npos &&
                    command.find(L"+set cg_crosshairAlphaMin 0") != std::wstring::npos,
                "VR firearm crosshair suppression is missing");
        require(command.find(
                    L"+set cg_firstPersonTracerChance 1") !=
                    std::wstring::npos,
                "VR local-player always-visible tracer override is missing");
        require(command.find(L"+set fx_cull_elem_spawn 1") !=
                    std::wstring::npos &&
                    command.find(L"+set fx_cull_elem_spawn 0") ==
                        std::wstring::npos &&
                    command.find(L"+set fx_cull_elem_draw 0") !=
                        std::wstring::npos,
                "bounded-spawn/headset-draw FX culling policy is missing");
        require(command.find(L"+set aim_automelee_enabled 0") != std::wstring::npos,
                "VR auto-melee camera suppression is missing");
        require(command.find(L"+set player_meleeRange 96") != std::wstring::npos,
                "VR-comfort melee range extension is missing");
        require(command.find(L"+set r_smp_backend 0") != std::wstring::npos,
                "single-threaded D3D9 backend safety override missing");
        require(command.find(L"+set r_multiGpu 0") != std::wstring::npos,
                "single-GPU D3D9 safety override missing");
        require(command.find(L"+set sm_enable 0") != std::wstring::npos,
                "same-frame stereo shadow-map safety override missing");
        require(command.find(L"+set sc_enable 0") != std::wstring::npos,
                "same-frame stereo shadow-cookie safety override missing");
        require(command.find(L"+set sm_sunEnable 0") != std::wstring::npos,
                "same-frame stereo sun-shadow safety override missing");
        require(command.find(L"+set sm_spotEnable 0") != std::wstring::npos,
                "same-frame stereo spot-shadow safety override missing");
        require(command.find(L"+set r_spotLightShadows 0") != std::wstring::npos,
                "same-frame stereo spotlight safety override missing");

        plan.launch_target = LaunchTarget::frontend_menu;
        const auto menu_command = build_game_command_line(plan);
        require_exact_openxr_pacing_policy(menu_command);
        require_exact_view_bob_policy(menu_command);
        require_exact_dynamic_light_policy(menu_command);
        require_exact_campaign_unlock_policy(menu_command, true);
        require(menu_command.find(L"+devmap") == std::wstring::npos,
                "frontend/menu command unexpectedly forces a development map");
        require(menu_command.find(L"+set fs_basepath") != std::wstring::npos &&
                    menu_command.find(L"+set fs_homepath") != std::wstring::npos,
                "frontend/menu command lost isolated data or profile paths");
        require(menu_command.find(L"+set r_smp_backend 0") != std::wstring::npos &&
                    menu_command.find(L"+set sm_enable 0") != std::wstring::npos &&
                    menu_command.find(L"+set r_dof_enable 0") != std::wstring::npos,
                "frontend/menu command lost VR renderer safety overrides");

        auto multiplayer_plan = plan;
        multiplayer_plan.launch_target = LaunchTarget::offline_multiplayer;
        multiplayer_plan.executable_kind = ExecutableKind::mp;
        multiplayer_plan.executable_identity =
            ExecutableIdentityId::plutonium_mp_1_7_1263;
        multiplayer_plan.staged_exe =
            L"C:\\Local Data\\WaWVR\\MP Runtime\\CoDWaWmp.exe";
        const auto multiplayer_command = build_game_command_line(multiplayer_plan);
        require_exact_openxr_pacing_policy(multiplayer_command);
        require_exact_view_bob_policy(multiplayer_command);
        require_exact_dynamic_light_policy(multiplayer_command);
        require_exact_campaign_unlock_policy(multiplayer_command, false);
        require(multiplayer_command.find(
                    L"\"C:\\Local Data\\WaWVR\\MP Runtime\\CoDWaWmp.exe\"") == 0,
                "offline multiplayer command did not select the staged MP executable");
        require(multiplayer_command.find(L"+devmap") == std::wstring::npos,
                "offline multiplayer frontend unexpectedly forces a development map");
        require(multiplayer_command.find(L"+set onlinegame 0") != std::wstring::npos &&
                    multiplayer_command.find(L"+set ui_netSource 0") != std::wstring::npos &&
                    multiplayer_command.find(L"+set dedicated 0") != std::wstring::npos &&
                    multiplayer_command.find(L"+set ui_dedicated 0") != std::wstring::npos &&
                    multiplayer_command.find(L"+set cl_punkbuster 0") != std::wstring::npos &&
                    multiplayer_command.find(L"+set sv_punkbuster 0") != std::wstring::npos,
                "offline multiplayer command lost its local-only frontend defaults");
        require(multiplayer_command.find(L"+set fs_basepath") != std::wstring::npos &&
                    multiplayer_command.find(L"+set fs_homepath") != std::wstring::npos &&
                    multiplayer_command.find(
                        L"+set fs_localAppData \"C:\\Local Data\\WaWVR\\home\"") !=
                        std::wstring::npos &&
                    multiplayer_command.find(L"+set r_smp_backend 0") != std::wstring::npos,
                "offline multiplayer command lost isolated paths or VR safety overrides");
        require(multiplayer_command.find(L"+set fs_game mods/mp_PeZBOTWAW") ==
                        std::wstring::npos &&
                    multiplayer_command.find(L"+set fs_game \"\"") !=
                        std::wstring::npos &&
                    multiplayer_command.find(L"+set svr_pezbots 0") !=
                        std::wstring::npos &&
                    multiplayer_command.find(
                        L"+set wawvr_pezbot_autofill 0") !=
                        std::wstring::npos,
                "unverified PeZBOT mod was added to the MP command line");

        multiplayer_plan.pezbot_enabled = true;
        const auto pezbot_command = build_game_command_line(multiplayer_plan);
        require(pezbot_command.find(L"+set fs_game mods/mp_PeZBOTWAW") !=
                    std::wstring::npos &&
                    pezbot_command.find(L"+exec pezbot.cfg") != std::wstring::npos &&
                    pezbot_command.find(L"+set svr_pezbots 9") != std::wstring::npos &&
                    pezbot_command.find(L"+set wawvr_pezbot_autofill 9") !=
                        std::wstring::npos &&
                    pezbot_command.find(L"+set svr_pezbots_weapons authentic") !=
                        std::wstring::npos &&
                    pezbot_command.find(L"+set svr_pezbots_useperks 0") !=
                        std::wstring::npos &&
                    pezbot_command.find(L"+set svr_pezbots_skill 0.7") !=
                        std::wstring::npos,
                "verified PeZBOT command line lost its conservative local defaults");
        require(pezbot_command.find(L"+devmap") == std::wstring::npos &&
                    pezbot_command.find(L"+set onlinegame 0") != std::wstring::npos &&
                    pezbot_command.find(L"+set dedicated 0") != std::wstring::npos,
                "PeZBOT activation changed frontend map selection or local-only policy");
        require(pezbot_command.find(L"+set fs_localAppData") <
                    pezbot_command.find(L"+set fs_game"),
                "MP local data root was not applied before fs_game");
        require(pezbot_command.ends_with(L"+set ui_dedicated 0") &&
                    pezbot_command.rfind(L"+set ui_dedicated 0") >
                        pezbot_command.find(L"+exec pezbot.cfg"),
                "listen-server UI safeguard is not the final startup command");
        multiplayer_plan.bot_policy = BotPolicy::disabled;
        const auto disabled_bots_command =
            build_game_command_line(multiplayer_plan);
        require(disabled_bots_command.find(
                    L"+set fs_game mods/mp_PeZBOTWAW") ==
                        std::wstring::npos &&
                    disabled_bots_command.find(L"+set svr_pezbots 9") ==
                        std::wstring::npos &&
                    disabled_bots_command.find(
                        L"+set wawvr_pezbot_autofill 9") ==
                        std::wstring::npos &&
                    disabled_bots_command.find(L"+set fs_game \"\"") !=
                        std::wstring::npos &&
                    disabled_bots_command.find(L"+set svr_pezbots 0") !=
                        std::wstring::npos &&
                    disabled_bots_command.find(
                        L"+set wawvr_pezbot_autofill 0") !=
                        std::wstring::npos,
                "disabled bot policy did not suppress all PeZBOT activation");
        require(command.find(L"+set onlinegame 0") == std::wstring::npos &&
                    menu_command.find(L"+set onlinegame 0") == std::wstring::npos,
                "SP launch commands unexpectedly gained MP-only offline dvars");

        plan.launch_target = LaunchTarget::der_riese;
        const auto der_riese_command = build_game_command_line(plan);
        require_exact_openxr_pacing_policy(der_riese_command);
        require_exact_view_bob_policy(der_riese_command);
        require_exact_dynamic_light_policy(der_riese_command);
        require_exact_campaign_unlock_policy(der_riese_command, true);
        require(der_riese_command.find(L"+devmap nazi_zombie_factory") !=
                    std::wstring::npos &&
                    der_riese_command.find(L"+devmap nazi_zombie_prototype") ==
                    std::wstring::npos,
                "Der Riese target did not select only nazi_zombie_factory");
        require(der_riese_command.find(L"+set fs_basepath") != std::wstring::npos &&
                    der_riese_command.find(L"+set fs_homepath") != std::wstring::npos &&
                    der_riese_command.find(L"+set r_smp_backend 0") != std::wstring::npos,
                "Der Riese command lost isolated paths or VR safety overrides");
        auto expected_der_riese_command = command;
        const auto nacht_map = expected_der_riese_command.find(
            L"+devmap nazi_zombie_prototype");
        require(nacht_map != std::wstring::npos,
                "could not derive the exact Der Riese command invariant");
        expected_der_riese_command.replace(
            nacht_map,
            std::wstring(L"+devmap nazi_zombie_prototype").size(),
            L"+devmap nazi_zombie_factory");
        require(der_riese_command == expected_der_riese_command,
                "Der Riese command differs from Nacht by more than the fixed map name");

        plan.launch_target = LaunchTarget::campaign_ber1;
        const auto campaign_ber1_command = build_game_command_line(plan);
        require_exact_openxr_pacing_policy(campaign_ber1_command);
        require_exact_view_bob_policy(campaign_ber1_command);
        require_exact_dynamic_light_policy(campaign_ber1_command);
        require_exact_campaign_unlock_policy(campaign_ber1_command, true);
        require(campaign_ber1_command.find(L"+devmap ber1") !=
                    std::wstring::npos &&
                    campaign_ber1_command.find(
                        L"+devmap nazi_zombie_prototype") ==
                        std::wstring::npos &&
                    campaign_ber1_command.find(
                        L"+devmap nazi_zombie_factory") ==
                        std::wstring::npos,
                "Ber1 campaign target did not select only the fixed campaign map");
        auto expected_campaign_ber1_command = command;
        const auto campaign_nacht_map = expected_campaign_ber1_command.find(
            L"+devmap nazi_zombie_prototype");
        require(campaign_nacht_map != std::wstring::npos,
                "could not derive the exact Ber1 campaign command invariant");
        expected_campaign_ber1_command.replace(
            campaign_nacht_map,
            std::wstring(L"+devmap nazi_zombie_prototype").size(),
            L"+devmap ber1");
        require(campaign_ber1_command == expected_campaign_ber1_command,
                "Ber1 campaign command differs from Nacht by more than the fixed map name");

        plan.launch_target = LaunchTarget::campaign_pel2_airfield;
        const auto campaign_pel2_airfield_command =
            build_game_command_line(plan);
        require_exact_openxr_pacing_policy(campaign_pel2_airfield_command);
        require_exact_view_bob_policy(campaign_pel2_airfield_command);
        require_exact_dynamic_light_policy(campaign_pel2_airfield_command);
        require_exact_campaign_unlock_policy(
            campaign_pel2_airfield_command, true);
        require(
            count_occurrences(
                campaign_pel2_airfield_command, L"+set monkeytoy 0") == 1 &&
                count_occurrences(
                    campaign_pel2_airfield_command, L"+set start airfield") == 1 &&
                count_occurrences(
                    campaign_pel2_airfield_command, L"+spdevmap pel2") == 1 &&
                campaign_pel2_airfield_command.find(L"+devmap") ==
                    std::wstring::npos,
            "Pel2 airfield target did not select exactly one bounded campaign jump");
        const auto pel2_monkeytoy = campaign_pel2_airfield_command.find(
            L"+set monkeytoy 0");
        const auto pel2_start = campaign_pel2_airfield_command.find(
            L"+set start airfield");
        const auto pel2_map = campaign_pel2_airfield_command.find(
            L"+spdevmap pel2");
        require(
            campaign_pel2_airfield_command.find(L"+seta mis_01 50") <
                    pel2_monkeytoy &&
                pel2_monkeytoy < pel2_start && pel2_start < pel2_map &&
                campaign_pel2_airfield_command.ends_with(L"+spdevmap pel2"),
            "Pel2 airfield target did not apply unlock, developer, start, and map commands in order");

        plan.launch_target = LaunchTarget::nacht;
        plan.source_resolution = kPerformanceSourceResolution;
        const auto performance_nacht = build_game_command_line(plan);
        require_exact_openxr_pacing_policy(performance_nacht);
        require_exact_view_bob_policy(performance_nacht);
        require_exact_dynamic_light_policy(performance_nacht);
        require(performance_nacht.find(L"+set r_customMode 3744x2016") !=
                    std::wstring::npos,
                "direct Nacht did not honor the explicit performance resolution");

        plan.source_resolution = kFallbackSourceResolution;
        const auto fallback_nacht = build_game_command_line(plan);
        require_exact_openxr_pacing_policy(fallback_nacht);
        require_exact_view_bob_policy(fallback_nacht);
        require_exact_dynamic_light_policy(fallback_nacht);
        require(fallback_nacht.find(L"+set r_customMode 1024x768") !=
                    std::wstring::npos,
                "direct Nacht did not honor the explicit recovery resolution");
        plan.launch_target = LaunchTarget::frontend_menu;
        const auto fallback_menu = build_game_command_line(plan);
        require_exact_openxr_pacing_policy(fallback_menu);
        require_exact_view_bob_policy(fallback_menu);
        require_exact_dynamic_light_policy(fallback_menu);
        require(fallback_menu.find(L"+set r_customMode 1024x768") !=
                    std::wstring::npos &&
                    fallback_menu.find(L"+devmap") == std::wstring::npos &&
                    fallback_menu.find(L"+set r_mode ") == std::wstring::npos,
                "frontend/menu did not honor the explicit recovery resolution");
    }

    void test_multiplayer_profile_normalization(const fs::path& root)
    {
        const auto test_root = root / L"multiplayer-profile-policy";
        const auto home = test_root / L"home-mp";
        const auto profiles = home / L"players" / L"profiles";
        const auto profile = profiles / L"$$$";
        fs::create_directories(profile);

        LaunchPlan plan;
        plan.launch_target = LaunchTarget::offline_multiplayer;
        plan.executable_kind = ExecutableKind::mp;
        plan.executable_identity =
            ExecutableIdentityId::plutonium_mp_1_7_1263;
        plan.game_dir = test_root / L"installed-game";
        plan.source_exe = test_root / L"source" / L"CoDWaWmp.exe";
        plan.runtime_dir = test_root / L"runtime";
        plan.home_dir = home;
        plan.home_dir_is_launcher_managed = true;

        write_binary_file(profiles / L"active.txt", "$$$");
        const auto config = profile / L"config_mp.cfg";
        const std::string live_form =
            "seta ui_browserShowDedicated \"1\"\n"
            "seta ui_dedicated \"1\"\n"
            "seta ui_netSource \"0\"\n";
        write_binary_file(config, live_form);
        require(enforce_offline_multiplayer_listen_profile(plan),
                "live-form dedicated profile was not normalized");
        const std::string expected_live_form =
            "seta ui_browserShowDedicated \"1\"\n"
            "seta ui_dedicated \"0\"\n"
            "seta ui_netSource \"0\"\n";
        require(read_binary_file(config) == expected_live_form,
                "profile normalization changed unrelated LF config bytes");
        require(!enforce_offline_multiplayer_listen_profile(plan),
                "already-safe multiplayer profile was rewritten");

        const std::string duplicate_crlf =
            "  SeTa\tUI_DEDICATED\t\"2\"\r\n"
            "seta unrelated \"value\"\r\n"
            "set ui_dedicated \"1\"\r\n";
        write_binary_file(config, duplicate_crlf);
        require(enforce_offline_multiplayer_listen_profile(plan),
                "duplicate/case-insensitive dedicated assignments were not normalized");
        const std::string expected_duplicate_crlf =
            "seta ui_dedicated \"0\"\r\n"
            "seta unrelated \"value\"\r\n"
            "seta ui_dedicated \"0\"\r\n";
        require(read_binary_file(config) == expected_duplicate_crlf,
                "duplicate CRLF dedicated assignments were not all disabled");

        write_binary_file(config, "seta ui_netSource \"0\"");
        require(enforce_offline_multiplayer_listen_profile(plan),
                "missing dedicated assignment was not appended");
        require(
            read_binary_file(config) ==
                "seta ui_netSource \"0\"\nseta ui_dedicated \"0\"\n",
            "appended dedicated assignment did not preserve LF convention");

        auto sp_plan = plan;
        sp_plan.launch_target = LaunchTarget::frontend_menu;
        sp_plan.executable_kind = ExecutableKind::sp;
        const auto before_sp_noop = read_binary_file(config);
        require(!enforce_offline_multiplayer_listen_profile(sp_plan) &&
                    read_binary_file(config) == before_sp_noop,
                "non-MP launch mutated the multiplayer profile");

        const auto absent_home = test_root / L"absent-home-mp";
        fs::create_directories(absent_home);
        auto absent = plan;
        absent.home_dir = absent_home;
        absent.runtime_dir = test_root / L"absent-runtime";
        require(!enforce_offline_multiplayer_listen_profile(absent),
                "missing active profile was not treated as a first-launch no-op");
        require(!fs::exists(absent_home / L"players"),
                "first-launch no-op created profile directories");

        write_binary_file(profiles / L"active.txt", "..\\outside");
        const auto outside = test_root / L"outside-sentinel.txt";
        write_binary_file(outside, "unchanged");
        bool rejected_traversal = false;
        try
        {
            (void)enforce_offline_multiplayer_listen_profile(plan);
        }
        catch (const std::exception&)
        {
            rejected_traversal = true;
        }
        require(rejected_traversal &&
                    read_binary_file(outside) == "unchanged",
                "active-profile traversal was not rejected safely");

        write_binary_file(profiles / L"active.txt", "$$$\r\n");
        std::string binary_config = "seta ui_dedicated \"1\"";
        binary_config.push_back('\0');
        binary_config += "seta unrelated \"1\"";
        write_binary_file(config, binary_config);
        bool rejected_binary = false;
        try
        {
            (void)enforce_offline_multiplayer_listen_profile(plan);
        }
        catch (const std::exception&)
        {
            rejected_binary = true;
        }
        require(rejected_binary && read_binary_file(config) == binary_config,
                "binary profile config was changed instead of rejected");

        const std::string maximum_sized_config(2 * 1'024 * 1'024, 'x');
        write_binary_file(config, maximum_sized_config);
        bool rejected_growth = false;
        try
        {
            (void)enforce_offline_multiplayer_listen_profile(plan);
        }
        catch (const std::exception&)
        {
            rejected_growth = true;
        }
        require(
            rejected_growth &&
                read_binary_file(config) == maximum_sized_config,
            "maximum-sized profile was replaced before growth was rejected");

        const auto hard_link_source = test_root / L"hard-link-sentinel.cfg";
        const std::string hard_link_original =
            "seta ui_dedicated \"1\"\nseta unrelated \"preserve\"\n";
        write_binary_file(hard_link_source, hard_link_original);
        fs::remove(config);
        fs::create_hard_link(hard_link_source, config);
        require(enforce_offline_multiplayer_listen_profile(plan),
                "hard-linked profile config was not safely normalized");
        require(
            read_binary_file(config) ==
                "seta ui_dedicated \"0\"\nseta unrelated \"preserve\"\n" &&
                read_binary_file(hard_link_source) == hard_link_original,
            "atomic replacement mutated another hard link target");

        const auto caller_home = test_root / L"caller-owned-home";
        const auto caller_profile =
            caller_home / L"players" / L"profiles" / L"$$$";
        fs::create_directories(caller_profile);
        write_binary_file(
            caller_home / L"players" / L"profiles" / L"active.txt", "$$$");
        const auto caller_config = caller_profile / L"config_mp.cfg";
        write_binary_file(caller_config, hard_link_original);
        auto caller_owned = plan;
        caller_owned.home_dir = caller_home;
        caller_owned.runtime_dir = test_root / L"caller-runtime";
        caller_owned.home_dir_is_launcher_managed = false;
        require(
            !enforce_offline_multiplayer_listen_profile(caller_owned) &&
                read_binary_file(caller_config) == hard_link_original,
            "explicit caller-owned home was rewritten as launcher-managed data");
    }

    std::vector<StoredZipEntry> pezbot_fixture_entries()
    {
        return {
            {"mp_PeZBOTWAW/mod.ff", "fixture mod fastfile"},
            {"mp_PeZBOTWAW/PeZBOTWaW.iwd", "fixture iwd"},
            {"mp_PeZBOTWAW/pezbot.cfg", "set svr_pezbots 0\n"},
            {"mp_PeZBOTWAW/pezbot_dev.cfg", "set svr_pezbots_mode dev\n"},
            {"mp_PeZBOTWAW/ReadMe/Installation.txt", "fixture documentation\n"},
        };
    }

    PezBotArchiveIdentity fixture_archive_identity(const fs::path& archive)
    {
        return {
            fs::file_size(archive),
            md5_file(archive),
            sha256_file(archive),
        };
    }

    LaunchPlan pezbot_fixture_plan(
        const fs::path& root,
        const fs::path& archive)
    {
        LaunchPlan plan;
        plan.executable_kind = ExecutableKind::mp;
        plan.launch_target = LaunchTarget::offline_multiplayer;
        plan.executable_identity =
            ExecutableIdentityId::plutonium_mp_1_7_1263;
        plan.home_dir = root / L"home-mp";
        plan.launcher_executable = current_test_executable();
        plan.pezbot_archive = archive;
        return plan;
    }

    void test_pezbot_import(const fs::path& root)
    {
        const auto test_root = root / L"pezbot";
        fs::create_directories(test_root);

        LaunchPlan explicit_sp_home;
        explicit_sp_home.executable_kind = ExecutableKind::sp;
        explicit_sp_home.home_dir =
            test_root / L"managed-stage" / L"h" / L"home";
        require(pezbot_install_directory(explicit_sp_home) ==
                    test_root / L"managed-stage" / L"h" / L"home-mp" /
                        L"mods" / kPezBotModFolderName,
                "explicit SP home escaped to the legacy LocalAppData MP home");

        Options legacy_sp_options;
        legacy_sp_options.game_dir = L"C:\\fixture-game";
        legacy_sp_options.source_exe = L"C:\\fixture-source\\t4sp.exe";
        const auto legacy_sp = resolve_plan(legacy_sp_options);
        require(pezbot_install_directory(legacy_sp) ==
                    derive_multiplayer_home_dir(legacy_sp.home_dir) /
                        L"mods" / kPezBotModFolderName,
                "default SP plan no longer preserves the sibling legacy home-mp location");

        const auto launcher_directory = test_root / L"launcher";
        const auto downloads_directory = test_root / L"Downloads";
        fs::create_directories(launcher_directory);
        fs::create_directories(downloads_directory);
        const auto launcher = launcher_directory / L"WorldWarVR.exe";
        const auto beside_launcher =
            launcher_directory / kPezBotArchiveFilename;
        const auto in_downloads =
            downloads_directory / kPezBotArchiveFilename;
        std::ofstream(beside_launcher, std::ios::binary) << "beside";
        std::ofstream(in_downloads, std::ios::binary) << "downloads";

        const auto discovered_beside = discover_pezbot_archive(
            std::nullopt, launcher, downloads_directory);
        require(discovered_beside && is_same_path(*discovered_beside, beside_launcher),
                "PeZBOT discovery did not prefer the archive beside the launcher");

        const auto explicit_archive = test_root / L"renamed-official.zip";
        const auto discovered_explicit = discover_pezbot_archive(
            explicit_archive, launcher, downloads_directory);
        require(discovered_explicit &&
                    is_same_path(*discovered_explicit, explicit_archive),
                "explicit PeZBOT archive did not take discovery priority");

        fs::remove(beside_launcher);
        const auto discovered_download = discover_pezbot_archive(
            std::nullopt, launcher, downloads_directory);
        require(discovered_download && is_same_path(*discovered_download, in_downloads),
                "PeZBOT discovery did not fall back to Downloads");

        const auto valid_archive = test_root / L"valid-fixture.zip";
        write_stored_zip(valid_archive, pezbot_fixture_entries());
        const auto valid_identity = fixture_archive_identity(valid_archive);
        require(validate_pezbot_archive(valid_archive, valid_identity).valid,
                "generated PeZBOT fixture failed exact identity validation");
        auto wrong_size = valid_identity;
        ++wrong_size.size;
        require(!validate_pezbot_archive(valid_archive, wrong_size).valid,
                "PeZBOT archive with the wrong size was accepted");
        auto wrong_md5 = valid_identity;
        wrong_md5.md5 = "00000000000000000000000000000000";
        require(!validate_pezbot_archive(valid_archive, wrong_md5).valid,
                "PeZBOT archive with the wrong MD5 was accepted");
        auto wrong_sha256 = valid_identity;
        wrong_sha256.sha256 =
            "0000000000000000000000000000000000000000000000000000000000000000";
        require(!validate_pezbot_archive(valid_archive, wrong_sha256).valid,
                "PeZBOT archive with the wrong SHA-256 was accepted");
        require(!validate_pezbot_archive(
                    valid_archive,
                    {kExpectedPezBotArchiveSize,
                     kExpectedPezBotArchiveMd5,
                     kExpectedPezBotArchiveSha256}).valid,
                "test fixture was mistaken for the supported PeZBOT archive");

        const auto helper = current_test_executable().parent_path() /
            kPezBotImportHelperFilename;
        require(fs::is_regular_file(helper),
                "configured PeZBOT helper was not staged beside launcher tests");

        auto valid_plan = pezbot_fixture_plan(
            test_root / L"valid-case", valid_archive);
        auto disabled_plan = pezbot_fixture_plan(
            test_root / L"disabled-case", valid_archive);
        disabled_plan.bot_policy = BotPolicy::disabled;
        const auto disabled = prepare_optional_pezbot(
            disabled_plan, valid_identity);
        require(disabled.state == PezBotState::unavailable &&
                    !disabled.archive &&
                    !fs::exists(pezbot_install_directory(disabled_plan)) &&
                    !should_import_optional_pezbot(disabled_plan),
                "disabled bot policy imported or retained a discovered archive");

        const auto ready = inspect_optional_pezbot(valid_plan, valid_identity);
        require(ready.state == PezBotState::archive_ready,
                "verified PeZBOT fixture was not reported importable");
        const auto imported = prepare_optional_pezbot(valid_plan, valid_identity);
        require(imported.enabled(), "valid PeZBOT fixture was not imported");
        const auto install = pezbot_install_directory(valid_plan);
        require(fs::is_regular_file(install / L"mod.ff") &&
                    fs::is_regular_file(install / L"PeZBOTWaW.iwd") &&
                    fs::is_regular_file(install / L"pezbot.cfg") &&
                    fs::is_regular_file(install / L"pezbot_dev.cfg") &&
                    fs::is_regular_file(install / L"ReadMe" / L"Installation.txt") &&
                    fs::is_regular_file(
                        install / L".wawvr-pezbot-005p.receipt"),
                "safe PeZBOT import did not create the exact expected layout");
        const auto mod_write_time = fs::last_write_time(install / L"mod.ff");
        const auto reused = prepare_optional_pezbot(valid_plan, valid_identity);
        require(reused.enabled() &&
                    fs::last_write_time(install / L"mod.ff") == mod_write_time,
                "verified PeZBOT receipt was not reused idempotently");

        std::ofstream(install / L"games_mp.log") << "runtime log\n";
        std::ofstream(install / L"console_mp.log") << "console log\n";
        const auto logged_install = prepare_optional_pezbot(
            valid_plan, valid_identity);
        require(logged_install.enabled() &&
                    fs::last_write_time(install / L"mod.ff") == mod_write_time,
                "known mutable MP logs invalidated or rewrote the verified PeZBOT install");
        std::ofstream(install / L"unexpected-runtime-file.txt") << "reject me\n";
        require(inspect_optional_pezbot(valid_plan, valid_identity).state ==
                    PezBotState::custom_folder_conflict,
                "an arbitrary unreceipted file was accepted in the PeZBOT install");

        const auto custom_root = test_root / L"custom-case";
        auto custom_plan = pezbot_fixture_plan(custom_root, valid_archive);
        const auto custom_install = pezbot_install_directory(custom_plan);
        fs::create_directories(custom_install);
        const auto custom_marker = custom_install / L"custom-marker.iwd";
        std::ofstream(custom_marker, std::ios::binary) << "do not touch";
        const auto custom = prepare_optional_pezbot(custom_plan, valid_identity);
        require(custom.state == PezBotState::custom_folder_conflict &&
                    fs::is_regular_file(custom_marker) &&
                    !fs::exists(custom_install / L"mod.ff"),
                "existing custom PeZBOT folder was overwritten or modified");

        auto traversal_entries = pezbot_fixture_entries();
        traversal_entries.push_back({
            "mp_PeZBOTWAW/../escaped.txt", "must never escape"});
        const auto traversal_archive = test_root / L"traversal-fixture.zip";
        write_stored_zip(traversal_archive, traversal_entries);
        auto traversal_plan = pezbot_fixture_plan(
            test_root / L"traversal-case", traversal_archive);
        const auto traversal = prepare_optional_pezbot(
            traversal_plan, fixture_archive_identity(traversal_archive));
        require(traversal.state == PezBotState::import_failed &&
                    !fs::exists(pezbot_install_directory(traversal_plan)) &&
                    !fs::exists(test_root / L"traversal-case" / L"escaped.txt") &&
                    !fs::exists(test_root / L"traversal-case" / L"home-mp" /
                                L"mods" / L"escaped.txt"),
                "archive traversal was not rejected without an escaped write");

        auto symlink_entries = pezbot_fixture_entries();
        symlink_entries[4].external_attributes = 0xA0000000U;
        const auto symlink_archive = test_root / L"symlink-fixture.zip";
        write_stored_zip(symlink_archive, symlink_entries);
        auto symlink_plan = pezbot_fixture_plan(
            test_root / L"symlink-case", symlink_archive);
        const auto symlink = prepare_optional_pezbot(
            symlink_plan, fixture_archive_identity(symlink_archive));
        require(symlink.state == PezBotState::import_failed &&
                    !fs::exists(pezbot_install_directory(symlink_plan)),
                "ZIP symbolic-link entry was not rejected");

        const auto malformed_archive = test_root / L"malformed-fixture.zip";
        std::ofstream(malformed_archive, std::ios::binary) << "not a ZIP archive";
        auto malformed_plan = pezbot_fixture_plan(
            test_root / L"malformed-case", malformed_archive);
        const auto malformed = prepare_optional_pezbot(
            malformed_plan, fixture_archive_identity(malformed_archive));
        require(malformed.state == PezBotState::import_failed &&
                    !fs::exists(pezbot_install_directory(malformed_plan)),
                "malformed identity-matched ZIP fixture was not rejected safely");

        LaunchPlan absent_plan;
        absent_plan.executable_kind = ExecutableKind::mp;
        absent_plan.launch_target = LaunchTarget::offline_multiplayer;
        absent_plan.executable_identity =
            ExecutableIdentityId::plutonium_mp_1_7_1263;
        absent_plan.home_dir = test_root / L"absent-case" / L"home-mp";
        absent_plan.launcher_executable = current_test_executable();
        absent_plan.bot_policy = BotPolicy::enabled;
        const auto absent = inspect_optional_pezbot(absent_plan);
        require(absent.state == PezBotState::unavailable,
                "missing optional PeZBOT archive did not remain non-fatal");
        DiagnosticReport optional_only;
        optional_only.checks.push_back(
            {L"optional PeZBOT", false, L"unavailable", false});
        require(optional_only.passed(),
                "unavailable optional PeZBOT incorrectly blocked base MP diagnostics");
    }

    void test_source_resolution()
    {
        require(parse_source_resolution(L"2560x1440") == kDefaultSourceResolution,
                "legacy source resolution did not parse");
        require(parse_source_resolution(L"6016x2688") ==
                    kNativeSpSourceResolution,
                "native SP source resolution did not parse");
        require(parse_source_resolution(L"3744x2016") ==
                    kPerformanceSourceResolution,
                "performance source resolution did not parse");
        require(parse_source_resolution(L"1024X768") == kFallbackSourceResolution,
                "case-insensitive resolution separator did not parse");
        require(parse_source_resolution(L"640x480") == SourceResolution{640, 480},
                "minimum source resolution was rejected");
        require(parse_source_resolution(L"3840x2160") ==
                    SourceResolution{kMaximumLegacySourceWidth,
                                     kMaximumLegacySourceHeight},
                "legacy maximum source resolution was rejected");
        require(parse_source_resolution(L"6016x2688") ==
                    SourceResolution{kMaximumSourceWidth, kMaximumSourceHeight},
                "native maximum source resolution was rejected");
        require(source_resolution_text(kDefaultSourceResolution) == L"2560x1440",
                "source resolution formatting changed");
        require(source_resolution_text(kNativeSpSourceResolution) == L"6016x2688",
                "native SP source resolution formatting changed");
        require(source_resolution_fits_desktop(
                    kDefaultSourceResolution, SourceResolution{2560, 1440}),
                "desktop-sized source resolution was rejected");
        require(!source_resolution_fits_desktop(
                    SourceResolution{3200, 1800}, SourceResolution{2560, 1440}),
                "oversized source resolution passed the desktop-fit gate");
        require(!source_resolution_fits_desktop(
                    SourceResolution{2560, 1440}, SourceResolution{0, 1440}),
                "unavailable desktop metrics passed the desktop-fit gate");

        const auto require_rejected = [](const std::wstring_view value) {
            bool rejected = false;
            try
            {
                static_cast<void>(parse_source_resolution(value));
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            require(rejected, "invalid source resolution was accepted");
        };
        require_rejected(L"1600");
        require_rejected(L"1600x");
        require_rejected(L"x900");
        require_rejected(L"1600x900x1");
        require_rejected(L"639x480");
        require_rejected(L"6018x2688");
        require_rejected(L"6016x2689");
        require_rejected(L"1601x900");
        require_rejected(L"1600x479");
        require_rejected(L"999999999999x900");

        wchar_t program[] = L"wawvr-launcher";
        wchar_t launch[] = L"--launch";
        wchar_t resolution[] = L"--resolution";
        wchar_t fallback[] = L"1024x768";
        wchar_t* argv[] = {program, launch, resolution, fallback};
        const auto parsed = parse_options(4, argv);
        require(parsed.source_resolution == kFallbackSourceResolution,
                "--resolution did not select the explicit recovery setting");

        const wchar_t* const variable = L"WAWVR_SOURCE_RESOLUTION";
        const DWORD needed = GetEnvironmentVariableW(variable, nullptr, 0);
        std::optional<std::wstring> previous;
        if (needed != 0)
        {
            std::wstring value(needed, L'\0');
            const DWORD written = GetEnvironmentVariableW(
                variable, value.data(), needed);
            require(written != 0 && written < needed,
                    "could not preserve source-resolution environment setting");
            value.resize(written);
            previous = std::move(value);
        }
        const auto restore_environment = [&]() {
            SetEnvironmentVariableW(
                variable, previous ? previous->c_str() : nullptr);
        };

        require(SetEnvironmentVariableW(variable, nullptr) != FALSE,
                "could not clear source-resolution environment fixture");
        try
        {
            Options default_sp_options;
            default_sp_options.game_dir = L"C:\\fixture-game";
            default_sp_options.source_exe = L"C:\\fixture-game\\t4sp.exe";
            const auto default_sp_plan = resolve_plan(default_sp_options);
            require(default_sp_plan.source_resolution ==
                        kNativeSpSourceResolution,
                    "exact SP plan did not default to native packed resolution");

            Options default_mp_options = default_sp_options;
            default_mp_options.launch_target = LaunchTarget::offline_multiplayer;
            default_mp_options.source_exe = L"C:\\fixture-game\\t4mp.exe";
            const auto default_mp_plan = resolve_plan(default_mp_options);
            require(default_mp_plan.source_resolution == kDefaultSourceResolution,
                    "MP plan did not retain the legacy packed resolution");

            default_mp_options.source_resolution = kNativeSpSourceResolution;
            bool rejected_sp_only_mp_resolution = false;
            try
            {
                static_cast<void>(resolve_plan(default_mp_options));
            }
            catch (const std::invalid_argument&)
            {
                rejected_sp_only_mp_resolution = true;
            }
            require(rejected_sp_only_mp_resolution,
                    "MP plan accepted the SP-only native packed resolution");
        }
        catch (...)
        {
            restore_environment();
            throw;
        }

        require(SetEnvironmentVariableW(variable, L"1024x768") != FALSE,
                "could not set source-resolution environment fixture");
        try
        {
            Options environment_options;
            environment_options.game_dir = L"C:\\fixture-game";
            environment_options.source_exe = L"C:\\fixture-game\\t4sp.exe";
            const auto environment_plan = resolve_plan(environment_options);
            require(environment_plan.source_resolution == kFallbackSourceResolution,
                    "WAWVR_SOURCE_RESOLUTION was not applied");

            environment_options.source_resolution = kDefaultSourceResolution;
            const auto explicit_plan = resolve_plan(environment_options);
            require(explicit_plan.source_resolution == kDefaultSourceResolution,
                    "explicit resolution did not override the environment");
        }
        catch (...)
        {
            restore_environment();
            throw;
        }
        restore_environment();
    }

    void test_desktop_resolution_compatibility_detail()
    {
        const auto mismatch = desktop_source_resolution_compatibility_detail(
            kDefaultSourceResolution, SourceResolution{1920, 1080});
        require(
            mismatch.find(
                L"Current Windows desktop resolution: 1920x1080.") !=
                std::wstring::npos,
            "desktop-resolution error omitted the current desktop size");
        require(
            mismatch.find(
                L"Requested VR source resolution: 2560x1440.") !=
                std::wstring::npos,
            "desktop-resolution error omitted the requested VR source size");
        require(
            mismatch.find(
                L"Call of Duty: World at War cannot create this window when it "
                L"is larger than the active primary desktop.") !=
                std::wstring::npos,
            "desktop-resolution error omitted the engine limitation");
        require(
            mismatch.find(L"Select a VR quality preset that fits") !=
                    std::wstring::npos &&
                mismatch.find(
                    L"set the Windows desktop resolution to at least 2560x1440") !=
                    std::wstring::npos,
            "desktop-resolution error omitted both supported recovery choices");
        require(
            mismatch.find(L"NVIDIA Dynamic Super Resolution (DSR)") !=
                    std::wstring::npos &&
                mismatch.find(
                    L"Deep Learning Dynamic Super Resolution (DLDSR)") !=
                    std::wstring::npos &&
                mismatch.find(L"On supported NVIDIA GPUs and drivers") !=
                    std::wstring::npos,
            "desktop-resolution error misstated the optional NVIDIA method");

        const auto compatible = desktop_source_resolution_compatibility_detail(
            kPerformanceSourceResolution, SourceResolution{3840, 2160});
        require(
            compatible.find(
                L"The requested resolution fits within the current desktop.") !=
                    std::wstring::npos &&
                compatible.find(L"Select a VR quality preset") ==
                    std::wstring::npos,
            "compatible desktop resolution incorrectly included failure guidance");

        for (const auto sp_identity : {
                 ExecutableIdentityId::plutonium_sp_1_7_1263,
                 ExecutableIdentityId::steam_sp_build_252004})
        {
            LaunchPlan exact_sp;
            exact_sp.executable_kind = ExecutableKind::sp;
            exact_sp.executable_identity = sp_identity;
            exact_sp.source_resolution = kNativeSpSourceResolution;
            require(plan_source_resolution_is_desktop_compatible(
                        exact_sp, SourceResolution{1920, 1080}) &&
                    plan_source_resolution_is_desktop_compatible(
                        exact_sp, SourceResolution{0, 0}),
                    "exact SP profile did not bypass the legacy desktop-size restriction");
            const auto patched_detail =
                plan_source_resolution_compatibility_detail(
                    exact_sp, SourceResolution{1920, 1080});
            require(
                patched_detail.find(L"exact validated SP renderer bootstrap") !=
                        std::wstring::npos &&
                    patched_detail.find(L"off-screen D3D9Ex back buffer") !=
                        std::wstring::npos &&
                    patched_detail.find(L"not used for multiplayer") !=
                        std::wstring::npos,
                "SP desktop bypass did not explain its exact patched scope");
        }

        LaunchPlan multiplayer;
        multiplayer.executable_kind = ExecutableKind::mp;
        multiplayer.executable_identity =
            ExecutableIdentityId::steam_mp_build_252004;
        multiplayer.source_resolution = kDefaultSourceResolution;
        require(!plan_source_resolution_is_desktop_compatible(
                    multiplayer, SourceResolution{1920, 1080}) &&
                    plan_source_resolution_is_desktop_compatible(
                        multiplayer, SourceResolution{2560, 1440}),
                "MP plan did not retain the legacy desktop-fit requirement");
        require(
            plan_source_resolution_compatibility_detail(
                multiplayer, SourceResolution{1920, 1080})
                    .find(L"cannot create this window") != std::wstring::npos,
            "MP desktop mismatch incorrectly used the SP renderer exception");

        auto inconsistent_sp = multiplayer;
        inconsistent_sp.executable_kind = ExecutableKind::sp;
        inconsistent_sp.source_resolution = kNativeSpSourceResolution;
        require(!plan_source_resolution_is_desktop_compatible(
                    inconsistent_sp, SourceResolution{1920, 1080}),
                "mismatched SP/MP identity activated the SP renderer exception");

        const auto unavailable = desktop_source_resolution_compatibility_detail(
            kDefaultSourceResolution, SourceResolution{0, 0});
        require(
            unavailable.find(
                L"Current Windows desktop resolution: unavailable.") !=
                    std::wstring::npos &&
                unavailable.find(
                    L"could not determine the physical current primary-display mode") !=
                    std::wstring::npos &&
                unavailable.find(L"cannot create this window when it is larger") ==
                    std::wstring::npos,
            "unavailable desktop metrics were formatted as a fake resolution");
    }

    void test_launch_target_options()
    {
        wchar_t program[] = L"wawvr-launcher";
        wchar_t launch[] = L"--launch";
        wchar_t prepare[] = L"--prepare";
        wchar_t menu[] = L"--menu";
        wchar_t der_riese_option[] = L"--der-riese";
        wchar_t campaign_ber1_option[] = L"--campaign-ber1";
        wchar_t campaign_pel2_airfield_option[] =
            L"--campaign-pel2-airfield";
        wchar_t multiplayer_option[] = L"--multiplayer";
        wchar_t mp_source_option[] = L"--mp-source-exe";
        wchar_t mp_source_path[] = L"D:\\managed-input\\CoDWaWmp.exe";
        wchar_t pezbot_archive_option[] = L"--pezbot-archive";
        wchar_t pezbot_archive_path[] = L"C:\\Downloads\\PeZBOTWAW_005p.zip";
        wchar_t bots_option[] = L"--bots";
        wchar_t bots_enabled[] = L"enabled";
        wchar_t bots_disabled[] = L"disabled";

        wchar_t* diagnose_argv[] = {program};
        const auto diagnose = parse_options(1, diagnose_argv);
        require(diagnose.mode == Mode::diagnose &&
                    diagnose.launch_target == LaunchTarget::nacht,
                "raw launcher no-argument diagnostic default changed");

        wchar_t* nacht_argv[] = {program, launch};
        const auto nacht = parse_options(2, nacht_argv);
        require(nacht.mode == Mode::launch &&
                    nacht.launch_target == LaunchTarget::nacht,
                "--launch no longer defaults to direct Nacht");

        wchar_t* menu_argv[] = {program, launch, menu};
        const auto frontend = parse_options(3, menu_argv);
        require(frontend.mode == Mode::launch &&
                    frontend.launch_target == LaunchTarget::frontend_menu,
                "--launch --menu did not select the stock frontend");

        wchar_t* frontend_mp_source_argv[] = {
            program, launch, menu, mp_source_option, mp_source_path};
        const auto frontend_mp_source =
            parse_options(5, frontend_mp_source_argv);
        require(frontend_mp_source.handoff_mp_source_exe ==
                    fs::path(mp_source_path),
                "--mp-source-exe did not preserve the trusted SP handoff source");

        wchar_t* der_riese_argv[] = {program, launch, der_riese_option};
        const auto der_riese = parse_options(3, der_riese_argv);
        require(der_riese.mode == Mode::launch &&
                    der_riese.launch_target == LaunchTarget::der_riese,
                "--launch --der-riese did not select direct Der Riese");

        wchar_t* campaign_ber1_argv[] = {
            program, launch, campaign_ber1_option};
        const auto campaign_ber1 = parse_options(3, campaign_ber1_argv);
        require(campaign_ber1.mode == Mode::launch &&
                    campaign_ber1.launch_target ==
                        LaunchTarget::campaign_ber1,
                "--launch --campaign-ber1 did not select direct Ber1 campaign");

        wchar_t* campaign_pel2_airfield_argv[] = {
            program, launch, campaign_pel2_airfield_option};
        const auto campaign_pel2_airfield =
            parse_options(3, campaign_pel2_airfield_argv);
        require(
            campaign_pel2_airfield.mode == Mode::launch &&
                campaign_pel2_airfield.launch_target ==
                    LaunchTarget::campaign_pel2_airfield,
            "--launch --campaign-pel2-airfield did not select the Pel2 airfield campaign checkpoint");

        wchar_t* multiplayer_diagnose_argv[] = {program, multiplayer_option};
        const auto multiplayer_diagnose = parse_options(2, multiplayer_diagnose_argv);
        require(multiplayer_diagnose.mode == Mode::diagnose &&
                    multiplayer_diagnose.launch_target ==
                        LaunchTarget::offline_multiplayer,
                "--multiplayer did not select a read-only MP diagnostic");

        wchar_t* multiplayer_prepare_argv[] = {program, prepare, multiplayer_option};
        const auto multiplayer_prepare = parse_options(3, multiplayer_prepare_argv);
        require(multiplayer_prepare.mode == Mode::prepare &&
                    multiplayer_prepare.launch_target ==
                        LaunchTarget::offline_multiplayer,
                "--prepare --multiplayer did not select isolated MP staging");

        wchar_t* multiplayer_launch_argv[] = {program, launch, multiplayer_option};
        const auto multiplayer_launch = parse_options(3, multiplayer_launch_argv);
        require(multiplayer_launch.mode == Mode::launch &&
                    multiplayer_launch.launch_target ==
                        LaunchTarget::offline_multiplayer,
                "--launch --multiplayer did not select the offline MP frontend");

        wchar_t* multiplayer_pezbot_argv[] = {
            program,
            launch,
            multiplayer_option,
            pezbot_archive_option,
            pezbot_archive_path,
        };
        const auto multiplayer_pezbot = parse_options(5, multiplayer_pezbot_argv);
        require(multiplayer_pezbot.pezbot_archive ==
                    fs::path(pezbot_archive_path),
                "--pezbot-archive did not preserve the explicit user archive");

        wchar_t* bots_enabled_argv[] = {
            program, launch, multiplayer_option, bots_option, bots_enabled};
        const auto parsed_bots_enabled = parse_options(5, bots_enabled_argv);
        require(parsed_bots_enabled.bot_policy == BotPolicy::enabled,
                "--bots enabled did not select the enabled bot policy");
        wchar_t* bots_disabled_argv[] = {
            program, launch, menu, bots_option, bots_disabled};
        const auto parsed_bots_disabled = parse_options(5, bots_disabled_argv);
        require(parsed_bots_disabled.bot_policy == BotPolicy::disabled,
                "--bots disabled did not select the disabled SP handoff policy");
        require(diagnose.bot_policy == BotPolicy::automatic,
                "omitted --bots did not retain backward-compatible automatic policy");

        const auto expect_invalid_bots = [&](wchar_t** invalid_argv,
                                             const int invalid_argc,
                                             const char* message) {
            bool rejected = false;
            try
            {
                static_cast<void>(parse_options(invalid_argc, invalid_argv));
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            require(rejected, message);
        };
        wchar_t bots_invalid[] = L"yes";
        wchar_t* invalid_bots_value_argv[] = {
            program, launch, bots_option, bots_invalid};
        expect_invalid_bots(
            invalid_bots_value_argv, 4,
            "unknown --bots value did not fail closed");
        wchar_t* missing_bots_value_argv[] = {
            program, launch, bots_option};
        expect_invalid_bots(
            missing_bots_value_argv, 3,
            "missing --bots value did not fail closed");
        wchar_t* duplicate_bots_argv[] = {
            program,
            launch,
            bots_option,
            bots_enabled,
            bots_option,
            bots_disabled,
        };
        expect_invalid_bots(
            duplicate_bots_argv, 6,
            "duplicate --bots policy did not fail closed");

        bool rejected_menu_without_launch = false;
        try
        {
            wchar_t* invalid_argv[] = {program, menu};
            static_cast<void>(parse_options(2, invalid_argv));
        }
        catch (const std::invalid_argument&)
        {
            rejected_menu_without_launch = true;
        }
        require(rejected_menu_without_launch,
                "--menu was accepted without explicit --launch mode");

        bool rejected_der_riese_without_launch = false;
        try
        {
            wchar_t* invalid_argv[] = {program, der_riese_option};
            static_cast<void>(parse_options(2, invalid_argv));
        }
        catch (const std::invalid_argument&)
        {
            rejected_der_riese_without_launch = true;
        }
        require(rejected_der_riese_without_launch,
                "--der-riese was accepted without explicit --launch mode");

        bool rejected_campaign_ber1_without_launch = false;
        try
        {
            wchar_t* invalid_argv[] = {program, campaign_ber1_option};
            static_cast<void>(parse_options(2, invalid_argv));
        }
        catch (const std::invalid_argument&)
        {
            rejected_campaign_ber1_without_launch = true;
        }
        require(rejected_campaign_ber1_without_launch,
                "--campaign-ber1 was accepted without explicit --launch mode");

        bool rejected_campaign_pel2_airfield_without_launch = false;
        try
        {
            wchar_t* invalid_argv[] = {
                program, campaign_pel2_airfield_option};
            static_cast<void>(parse_options(2, invalid_argv));
        }
        catch (const std::invalid_argument&)
        {
            rejected_campaign_pel2_airfield_without_launch = true;
        }
        require(
            rejected_campaign_pel2_airfield_without_launch,
            "--campaign-pel2-airfield was accepted without explicit --launch mode");

        bool rejected_multiple_targets = false;
        try
        {
            wchar_t* invalid_argv[] = {program, launch, menu, der_riese_option};
            static_cast<void>(parse_options(4, invalid_argv));
        }
        catch (const std::invalid_argument&)
        {
            rejected_multiple_targets = true;
        }
        require(rejected_multiple_targets,
                "--menu and --der-riese were accepted together");

        bool rejected_reverse_multiple_targets = false;
        try
        {
            wchar_t* invalid_argv[] = {program, launch, der_riese_option, menu};
            static_cast<void>(parse_options(4, invalid_argv));
        }
        catch (const std::invalid_argument&)
        {
            rejected_reverse_multiple_targets = true;
        }
        require(rejected_reverse_multiple_targets,
                "--der-riese and --menu were accepted together in reverse order");

        bool rejected_campaign_with_other_target = false;
        try
        {
            wchar_t* invalid_argv[] = {
                program, launch, campaign_ber1_option, menu};
            static_cast<void>(parse_options(4, invalid_argv));
        }
        catch (const std::invalid_argument&)
        {
            rejected_campaign_with_other_target = true;
        }
        require(rejected_campaign_with_other_target,
                "--campaign-ber1 and --menu were accepted together");

        bool rejected_multiplayer_with_sp_target = false;
        try
        {
            wchar_t* invalid_argv[] = {
                program, launch, multiplayer_option, menu};
            static_cast<void>(parse_options(4, invalid_argv));
        }
        catch (const std::invalid_argument&)
        {
            rejected_multiplayer_with_sp_target = true;
        }
        require(rejected_multiplayer_with_sp_target,
                "--multiplayer and --menu were accepted together");

        bool rejected_mp_source_for_multiplayer = false;
        try
        {
            wchar_t* invalid_argv[] = {
                program,
                launch,
                multiplayer_option,
                mp_source_option,
                mp_source_path,
            };
            static_cast<void>(parse_options(5, invalid_argv));
        }
        catch (const std::invalid_argument&)
        {
            rejected_mp_source_for_multiplayer = true;
        }
        require(rejected_mp_source_for_multiplayer,
                "--multiplayer accepted SP-only --mp-source-exe");

        const auto branded = make_user_facing_default_options(
            L"C:\\WaWVR Package\\WorldWarVR.exe");
        require(branded.mode == Mode::launch &&
                    branded.launch_target == LaunchTarget::frontend_menu,
                "user-facing no-argument launcher does not default to the frontend");
        require(branded.mod_dll.has_value() &&
                    branded.mod_dll->filename() == L"WorldWarVR.dll",
                "user-facing launcher no longer injects its adjacent mod DLL");
        require(branded.launcher_executable ==
                    fs::absolute(L"C:\\WaWVR Package\\WorldWarVR.exe"),
                "user-facing defaults did not preserve the launcher source artifact");

        const auto branded_multiplayer =
            make_user_facing_multiplayer_default_options(
                L"C:\\WaWVR Package\\WorldWarVR-Multiplayer.exe");
        require(branded_multiplayer.mode == Mode::launch &&
                    branded_multiplayer.launch_target ==
                        LaunchTarget::offline_multiplayer,
                "dedicated multiplayer launcher does not default to offline MP");
        require(branded_multiplayer.mod_dll.has_value() &&
                    branded_multiplayer.mod_dll->filename() == L"WorldWarVR.dll",
                "dedicated multiplayer launcher lost the adjacent VR DLL default");
        require(branded_multiplayer.launcher_executable == fs::absolute(
                    L"C:\\WaWVR Package\\WorldWarVR-Multiplayer.exe"),
                "dedicated multiplayer launcher did not preserve its executable path");

        const auto branded_der_riese = apply_user_facing_defaults(
            der_riese,
            L"C:\\WaWVR Package\\WorldWarVR.exe");
        require(branded_der_riese.mode == Mode::launch &&
                    branded_der_riese.launch_target == LaunchTarget::der_riese,
                "user-facing defaults changed the explicit Der Riese target");
        require(branded_der_riese.mod_dll.has_value() &&
                    *branded_der_riese.mod_dll ==
                        fs::path(L"C:\\WaWVR Package\\WorldWarVR.dll"),
                "user-facing launch arguments lost the adjacent VR DLL default");

        const auto branded_campaign_ber1 = apply_user_facing_defaults(
            campaign_ber1,
            L"C:\\WaWVR Package\\WorldWarVR.exe");
        require(branded_campaign_ber1.mode == Mode::launch &&
                    branded_campaign_ber1.launch_target ==
                        LaunchTarget::campaign_ber1 &&
                    branded_campaign_ber1.mod_dll.has_value(),
                "user-facing defaults changed the explicit Ber1 campaign target");

        Options explicit_dll;
        explicit_dll.mode = Mode::launch;
        explicit_dll.mod_dll = L"D:\\Custom VR\\CustomWaWVR.dll";
        const auto preserved_dll = apply_user_facing_defaults(
            explicit_dll,
            L"C:\\WaWVR Package\\WorldWarVR.exe");
        require(preserved_dll.mod_dll == explicit_dll.mod_dll,
                "user-facing adjacent DLL default replaced an explicit --mod-dll");

        require(mode_name(Mode::launch, LaunchTarget::nacht) ==
                    L"launch direct Nacht" &&
                    mode_name(Mode::launch, LaunchTarget::der_riese) ==
                    L"launch direct Der Riese" &&
                    mode_name(Mode::launch, LaunchTarget::campaign_ber1) ==
                        L"launch direct Ber1 campaign (developer)" &&
                    mode_name(
                        Mode::launch,
                        LaunchTarget::campaign_pel2_airfield) ==
                        L"launch Pel2 airfield campaign checkpoint (developer)" &&
                    mode_name(Mode::launch, LaunchTarget::frontend_menu) ==
                    L"launch stock frontend/menu" &&
                    mode_name(Mode::diagnose, LaunchTarget::offline_multiplayer) ==
                    L"diagnose offline multiplayer (read-only)" &&
                    mode_name(Mode::prepare, LaunchTarget::offline_multiplayer) ==
                    L"prepare offline multiplayer (stage only)" &&
                    mode_name(Mode::launch, LaunchTarget::offline_multiplayer) ==
                    L"launch offline multiplayer frontend",
                "launch-mode diagnostics do not distinguish the target");
        require(usage_text().find(L"--menu") != std::wstring::npos,
                "launcher help does not document frontend/menu mode");
        require(usage_text().find(L"--der-riese") != std::wstring::npos,
                "launcher help does not document direct Der Riese mode");
        require(usage_text().find(L"--campaign-ber1") != std::wstring::npos,
                "launcher help does not document direct Ber1 campaign mode");
        require(
            usage_text().find(L"--campaign-pel2-airfield") ==
                std::wstring::npos,
            "hidden Pel2 airfield test target leaked into public launcher help");
        require(usage_text().find(L"--multiplayer") != std::wstring::npos &&
                    usage_text().find(L"--mp-source-exe") != std::wstring::npos &&
                    usage_text().find(L"WAWVR_MP_SOURCE_EXE") != std::wstring::npos,
                "launcher help does not document offline multiplayer discovery");
        require(usage_text().find(L"--bots enabled|disabled") !=
                        std::wstring::npos &&
                    std::wstring_view(bot_policy_name(BotPolicy::enabled)) ==
                        L"enabled" &&
                    std::wstring_view(bot_policy_name(BotPolicy::disabled)) ==
                        L"disabled",
                "launcher help or diagnostics do not document bot policy");
        require(usage_text().find(L"Required for") != std::wstring::npos,
                "launcher help does not explain the raw-launch DLL requirement");
    }

    void test_der_riese_diagnostic_scope(const fs::path& root)
    {
        const auto game = root / L"diagnostic-game";
        const auto source = root / L"diagnostic-source.exe";
        create_fake_game_data(game, "diagnostic");
        std::ofstream(game / L"zone" / L"english" /
                      L"nazi_zombie_prototype.ff") << "nacht";
        std::ofstream(game / L"binkw32.dll") << "bink";
        std::ofstream(source) << "not-a-real-pe";

        LaunchPlan plan;
        plan.game_dir = game;
        plan.source_exe = source;
        plan.runtime_dir = root / L"diagnostic-runtime";
        plan.home_dir = root / L"diagnostic-home";
        plan.staged_exe = plan.runtime_dir / L"CoDWaW.exe";

        plan.mode = Mode::launch;
        plan.launch_target = LaunchTarget::nacht;
        const auto unmodded_launch_report = diagnose(plan);
        const auto* dll_selection = find_check(
            unmodded_launch_report,
            L"VR launch DLL selection");
        require(dll_selection != nullptr && !dll_selection->passed,
                "raw launch without a VR DLL did not fail closed");

        plan.mode = Mode::diagnose;
        const auto read_only_report = diagnose(plan);
        require(find_check(read_only_report, L"VR launch DLL selection") == nullptr,
                "read-only diagnosis unexpectedly requires a VR DLL");

        constexpr std::array<std::wstring_view, 5> marker_checks = {
            L"Der Riese base data marker",
            L"Der Riese load data marker",
            L"Der Riese patch data marker",
            L"Der Riese localized data marker",
            L"Der Riese intro video data marker",
        };
        constexpr std::array<std::wstring_view, 5> hash_checks = {
            L"Der Riese base fastfile SHA-256",
            L"Der Riese load fastfile SHA-256",
            L"Der Riese patch fastfile SHA-256",
            L"Der Riese localized fastfile SHA-256",
            L"Der Riese intro video SHA-256",
        };

        for (const auto target : {
                 LaunchTarget::nacht,
                 LaunchTarget::campaign_ber1,
                 LaunchTarget::campaign_pel2_airfield,
                 LaunchTarget::frontend_menu})
        {
            plan.launch_target = target;
            const auto report = diagnose(plan);
            for (const auto name : marker_checks)
            {
                require(find_check(report, name) == nullptr,
                        "non-Der-Riese target gained a Der Riese marker gate");
            }
            for (const auto name : hash_checks)
            {
                require(find_check(report, name) == nullptr,
                        "non-Der-Riese target gained a Der Riese hash gate");
            }
        }

        plan.launch_target = LaunchTarget::campaign_ber1;
        const auto missing_ber1_report = diagnose(plan);
        const auto* missing_ber1 = find_check(
            missing_ber1_report, L"Ber1 campaign data marker");
        require(missing_ber1 != nullptr && !missing_ber1->passed,
                "missing Ber1 campaign data did not fail its marker gate");
        std::ofstream(game / L"zone" / L"english" / L"ber1.ff") <<
            "fixture";
        const auto present_ber1_report = diagnose(plan);
        const auto* present_ber1 = find_check(
            present_ber1_report, L"Ber1 campaign data marker");
        require(present_ber1 != nullptr && present_ber1->passed,
                "present Ber1 campaign data did not pass its marker gate");

        plan.launch_target = LaunchTarget::campaign_pel2_airfield;
        const auto missing_pel2_report = diagnose(plan);
        const auto* missing_pel2 = find_check(
            missing_pel2_report, L"Pel2 campaign data marker");
        require(missing_pel2 != nullptr && !missing_pel2->passed,
                "missing Pel2 campaign data did not fail its marker gate");
        std::ofstream(game / L"zone" / L"english" / L"pel2.ff") <<
            "fixture";
        const auto present_pel2_report = diagnose(plan);
        const auto* present_pel2 = find_check(
            present_pel2_report, L"Pel2 campaign data marker");
        require(present_pel2 != nullptr && present_pel2->passed,
                "present Pel2 campaign data did not pass its marker gate");

        plan.launch_target = LaunchTarget::der_riese;
        const auto missing_report = diagnose(plan);
        for (const auto name : marker_checks)
        {
            const auto* check = find_check(missing_report, name);
            require(check != nullptr && !check->passed,
                    "missing Der Riese component did not fail its marker gate");
        }
        for (const auto name : hash_checks)
        {
            const auto* check = find_check(missing_report, name);
            require(check != nullptr && !check->passed,
                    "missing Der Riese component did not fail its hash gate");
        }

        for (const auto filename : {
                 L"nazi_zombie_factory.ff",
                 L"nazi_zombie_factory_load.ff",
                 L"nazi_zombie_factory_patch.ff",
                 L"localized_nazi_zombie_factory.ff"})
        {
            std::ofstream(game / L"zone" / L"english" / filename) << "fixture";
        }
        std::ofstream(game / L"main" / L"video" /
                      L"nazi_zombie_factory_load.bik") << "fixture";
        const auto present_report = diagnose(plan);
        for (const auto name : marker_checks)
        {
            const auto* check = find_check(present_report, name);
            require(check != nullptr && check->passed,
                    "present Der Riese component did not pass its marker gate");
        }
        for (const auto name : hash_checks)
        {
            const auto* check = find_check(present_report, name);
            require(check != nullptr && !check->passed,
                    "unrecognized Der Riese component unexpectedly passed its hash gate");
        }
    }

    void test_multiplayer_diagnostic_scope(const fs::path& root)
    {
        const auto game = root / L"mp-diagnostic-game";
        const auto source = root / L"mp-diagnostic-source.exe";
        create_fake_game_data(game, "mp-diagnostic");
        std::ofstream(game / L"binkw32.dll") << "bink";
        std::ofstream(source) << "not-a-real-mp-pe";

        constexpr std::array<std::wstring_view, 6> labels = {
            L"code post-gfx",
            L"patch",
            L"UI",
            L"common",
            L"localized code post-gfx",
            L"localized common",
        };
        constexpr std::array<std::wstring_view, 6> filenames = {
            L"code_post_gfx_mp.ff",
            L"patch_mp.ff",
            L"ui_mp.ff",
            L"common_mp.ff",
            L"localized_code_post_gfx_mp.ff",
            L"localized_common_mp.ff",
        };
        for (const auto filename : filenames)
        {
            std::ofstream(game / L"zone" / L"english" / filename) << "fixture";
        }

        LaunchPlan plan;
        plan.mode = Mode::diagnose;
        plan.launch_target = LaunchTarget::offline_multiplayer;
        plan.executable_kind = ExecutableKind::mp;
        plan.executable_identity =
            ExecutableIdentityId::plutonium_mp_1_7_1263;
        plan.game_dir = game;
        plan.source_exe = source;
        plan.runtime_dir = root / L"mp-diagnostic-runtime";
        plan.home_dir = root / L"mp-diagnostic-home";
        plan.staged_exe = plan.runtime_dir / L"CoDWaWmp.exe";

        const auto report = diagnose(plan);
        const auto* identity = find_check(report, L"executable target identity");
        require(identity != nullptr && identity->passed,
                "offline multiplayer plan identity did not pass diagnostics");
        const auto* source_hash = find_check(report, L"source SHA-256");
        require(source_hash != nullptr && !source_hash->passed &&
                    source_hash->detail.find(
                        L"943BB93001AD2ED465B6652C27FB649B5F0C5B24097E18A27A588AC35B3457A0") !=
                        std::wstring::npos,
                "offline multiplayer diagnostic did not require the pinned MP hash");

        for (const auto label : labels)
        {
            const auto marker_name = L"MP " + std::wstring(label) + L" data marker";
            const auto hash_name =
                L"MP " + std::wstring(label) + L" fastfile SHA-256";
            const auto* marker = find_check(report, marker_name);
            const auto* hash = find_check(report, hash_name);
            require(marker != nullptr && marker->passed,
                    "present offline multiplayer fastfile did not pass its marker gate");
            require(hash != nullptr && !hash->passed,
                    "unrecognized offline multiplayer fastfile passed its hash gate");
        }
        require(find_check(report, L"Nacht data marker") == nullptr &&
                    find_check(report, L"Nacht fastfile SHA-256") == nullptr &&
                    find_check(report, L"Der Riese base data marker") == nullptr,
                "offline multiplayer diagnostics retained SP/Zombies asset gates");
        require(report.command_line.find(L"+set onlinegame 0") !=
                    std::wstring::npos &&
                    report.command_line.find(L"+devmap") == std::wstring::npos,
                "offline multiplayer diagnostic command is not local/frontend-only");

        auto inconsistent = plan;
        inconsistent.executable_kind = ExecutableKind::sp;
        const auto inconsistent_report = diagnose(inconsistent);
        const auto* inconsistent_identity =
            find_check(inconsistent_report, L"executable target identity");
        require(inconsistent_identity != nullptr && !inconsistent_identity->passed,
                "mismatched SP identity was accepted for offline multiplayer");
    }

    void test_safe_mode_dialog_identity()
    {
        SafeModeDialogIdentity identity{
            4242,
            L"#32770",
            L"Run In Safe Mode?",
            L"It appears that Call of Duty: World at War did not quit properly the last "
                L"time it ran.\nDo you want to run the game in safe mode?\n"
                L"This is recommended for most people.",
            L"Button",
            IDNO,
        };
        require(is_exact_waw_safe_mode_dialog(identity, 4242),
                "exact WaW safe-mode dialog was not recognized");

        auto changed = identity;
        changed.owner_process_id = 99;
        require(!is_exact_waw_safe_mode_dialog(changed, 4242),
                "dialog from an unrelated process was accepted");
        changed = identity;
        changed.title = L"Run In Safe Mode? - Other Game";
        require(!is_exact_waw_safe_mode_dialog(changed, 4242),
                "inexact dialog title was accepted");
        changed = identity;
        changed.window_class = L"OtherClass";
        require(!is_exact_waw_safe_mode_dialog(changed, 4242),
                "inexact dialog class was accepted");
        changed = identity;
        changed.body = L"Do you want to run the game in safe mode?";
        require(!is_exact_waw_safe_mode_dialog(changed, 4242),
                "dialog without the WaW crash text was accepted");
        changed = identity;
        changed.no_button_id = IDYES;
        require(!is_exact_waw_safe_mode_dialog(changed, 4242),
                "wrong button ID was accepted");
    }

    void test_optimal_settings_dialog_identity()
    {
        SafeModeDialogIdentity identity{
            4242,
            L"#32770",
            L"Set Optimal Settings?",
            L"Your computer appears to have changed since the last time you ran Call of "
                L"Duty: World at War.\nWould you like the game to configure itself optimally "
                L"for your new hardware?\nThis is recommended for most people.\nIt will "
                L"change your system settings but not your controls.",
            L"Button",
            IDNO,
        };
        require(is_exact_waw_optimal_settings_dialog(identity, 4242),
                "exact WaW optimal-settings dialog was not recognized");

        auto changed = identity;
        changed.owner_process_id = 99;
        require(!is_exact_waw_optimal_settings_dialog(changed, 4242),
                "optimal-settings dialog from an unrelated process was accepted");
        changed = identity;
        changed.title = L"Set Optimal Settings? - Other Game";
        require(!is_exact_waw_optimal_settings_dialog(changed, 4242),
                "inexact optimal-settings title was accepted");
        changed = identity;
        changed.body = L"Would you like the game to configure itself optimally?";
        require(!is_exact_waw_optimal_settings_dialog(changed, 4242),
                "optimal-settings dialog without exact WaW text was accepted");
        changed = identity;
        changed.no_button_id = IDYES;
        require(!is_exact_waw_optimal_settings_dialog(changed, 4242),
                "optimal-settings dialog with wrong button ID was accepted");
    }

    void test_sp_renderer_thread_snapshot_classification()
    {
        constexpr std::uint32_t image_base = 0x00400000;
        constexpr std::uint32_t unrelated_eip = image_base + 0x00100000;
        const std::array<std::uint32_t, 4> harmless_stack{
            image_base + 0x002D62BF,
            image_base + 0x002D62C1,
            image_base + 0x002D605B,
            image_base + 0x002D605D,
        };
        require(
            classify_sp_renderer_thread_snapshot(
                image_base, unrelated_eip, harmless_stack) ==
                SpRendererThreadSnapshotStatus::safe,
            "harmless suspended SP thread snapshot was rejected");

        constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 3>
            critical_ranges = {{
                {0x002D62A0, 0x002D6307},
                {0x002D5FF0, 0x002D6108},
                {0x002D6310, 0x002D6375},
            }};
        for (const auto& [begin, end] : critical_ranges)
        {
            require(
                classify_sp_renderer_thread_snapshot(
                    image_base,
                    image_base + begin - 1,
                    std::span<const std::uint32_t>{}) ==
                    SpRendererThreadSnapshotStatus::safe,
                "byte before SP renderer routine start was rejected");
            require(
                classify_sp_renderer_thread_snapshot(
                    image_base,
                    image_base + begin,
                    std::span<const std::uint32_t>{}) ==
                    SpRendererThreadSnapshotStatus::
                        instruction_pointer_in_critical_routine,
                "critical SP renderer routine start was not rejected");
            require(
                classify_sp_renderer_thread_snapshot(
                    image_base,
                    image_base + end - 1,
                    std::span<const std::uint32_t>{}) ==
                    SpRendererThreadSnapshotStatus::
                        instruction_pointer_in_critical_routine,
                "critical SP renderer routine final byte was not rejected");
            require(
                classify_sp_renderer_thread_snapshot(
                    image_base,
                    image_base + end,
                    std::span<const std::uint32_t>{}) ==
                    SpRendererThreadSnapshotStatus::safe,
                "exclusive SP renderer routine end was rejected");
        }

        for (const auto return_rva : {0x002D62C0U, 0x002D605CU})
        {
            const std::array<std::uint32_t, 3> active_stack{
                0x77123456,
                image_base + return_rva,
                0x77234567,
            };
            require(
                classify_sp_renderer_thread_snapshot(
                    image_base, unrelated_eip, active_stack) ==
                    SpRendererThreadSnapshotStatus::
                        critical_return_address_on_stack,
                "active SP renderer return address was not rejected");
        }

        const std::array<std::uint32_t, 2> relocated_stack{
            0x10000000U + 0x002D62C0U,
            0x12345678,
        };
        require(
            classify_sp_renderer_thread_snapshot(
                0x10000000U, 0x10100000U, relocated_stack) ==
                SpRendererThreadSnapshotStatus::
                    critical_return_address_on_stack,
            "relocated SP renderer return address was not rejected");
    }

    void test_module_polling()
    {
        {
            const std::vector<std::optional<std::uintptr_t>> observations = {
                std::nullopt,
                0x1000u,
                0x1000u,
                0x1000u,
            };
            std::size_t next = 0;
            std::uint32_t delayed_ms = 0;
            const auto result = poll_for_stable_module(
                [&]() { return observations.at(next++); },
                []() { return true; },
                [&](const std::uint32_t milliseconds) { delayed_ms += milliseconds; },
                100,
                5,
                3);

            require(result.status == ModulePollStatus::found,
                    "delayed stable module was not found");
            require(result.module_base == 0x1000u,
                    "stable module base was not returned");
            require(result.attempts == 4 && result.elapsed_ms == 15 && delayed_ms == 15,
                    "delayed stable module poll accounting mismatch");
        }

        {
            const std::vector<std::optional<std::uintptr_t>> observations = {
                0x1000u,
                0x2000u,
                0x2000u,
                0x2000u,
            };
            std::size_t next = 0;
            const auto result = poll_for_stable_module(
                [&]() { return observations.at(next++); },
                []() { return true; },
                [](const std::uint32_t) {},
                100,
                1,
                3);

            require(result.status == ModulePollStatus::found &&
                        result.module_base == 0x2000u && result.attempts == 4,
                    "module base change did not reset the stability window");
        }

        {
            std::uint32_t delayed_ms = 0;
            const auto result = poll_for_stable_module(
                []() -> std::optional<std::uintptr_t> { return std::nullopt; },
                []() { return true; },
                [&](const std::uint32_t milliseconds) { delayed_ms += milliseconds; },
                25,
                10,
                2);

            require(result.status == ModulePollStatus::timed_out,
                    "missing module did not time out");
            require(result.attempts == 4 && result.elapsed_ms == 25 && delayed_ms == 25,
                    "module timeout accounting mismatch");
        }

        {
            std::uint32_t alive_checks = 0;
            const auto result = poll_for_stable_module(
                []() -> std::optional<std::uintptr_t> { return std::nullopt; },
                [&]() { return ++alive_checks < 3; },
                [](const std::uint32_t) {},
                100,
                5,
                2);

            require(result.status == ModulePollStatus::process_exited,
                    "target exit was not reported during module polling");
            require(result.attempts == 2 && result.elapsed_ms == 10,
                    "target-exit poll accounting mismatch");
        }

        {
            bool rejected = false;
            try
            {
                poll_for_stable_module(
                    []() -> std::optional<std::uintptr_t> { return std::nullopt; },
                    []() { return true; },
                    [](const std::uint32_t) {},
                    100,
                    0,
                    2);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            require(rejected, "zero polling interval was not rejected");
        }
    }
}

int wmain()
{
    const auto root = unique_test_directory();
    try
    {
        fs::remove_all(root);
        fs::create_directories(root);
        test_sha256(root);
        test_pe_parser(root);
        test_windows_quoting();
        test_path_boundaries();
        test_multiplayer_handoff_policy(root);
        test_automatic_source_selection();
        test_executable_identities_and_steam_environment();
        test_multiplayer_child_disables_deferred_frame_begin();
        test_target_specific_plan_defaults(root);
        test_game_language_detection_and_cache_refresh(root);
        test_runtime_data_junctions(root);
        test_command_line();
        test_multiplayer_profile_normalization(root);
        test_pezbot_import(root);
        test_source_resolution();
        test_desktop_resolution_compatibility_detail();
        test_launch_target_options();
        test_der_riese_diagnostic_scope(root);
        test_multiplayer_diagnostic_scope(root);
        test_safe_mode_dialog_identity();
        test_optimal_settings_dialog_identity();
        test_sp_renderer_thread_snapshot_classification();
        test_module_polling();
        fs::remove_all(root);
        std::wcout << L"All WaWVR launcher tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        remove_runtime_junctions(root / L"waw-1.7.1263");
        fs::remove_all(root);
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
