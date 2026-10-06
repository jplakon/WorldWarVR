#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wawvr::launcher
{
    inline constexpr auto kExpectedSpExeSha256 =
        "F26D45524BFFF7E44C8EBAB4D758CA524EDFB0FB7D52352B6C95E1E908799361";
    // Backward-compatible name for callers that predate the explicit SP/MP
    // launch split.
    inline constexpr auto kExpectedExeSha256 = kExpectedSpExeSha256;
    inline constexpr auto kExpectedMpExeSha256 =
        "943BB93001AD2ED465B6652C27FB649B5F0C5B24097E18A27A588AC35B3457A0";
    inline constexpr auto kExpectedSteamSpExeSha256 =
        "732900D158982C33E3121F0B86D22230BE79839BBCBFE3BDFC1238F408A7D64D";
    inline constexpr auto kExpectedSteamMpExeSha256 =
        "7D0B518A4BD267FFDB6D0203AD8F3721603B172AC13BA2ABCDB32584F759D36C";
    inline constexpr auto kPezBotArchiveFilename = L"PeZBOTWAW_005p.zip";
    inline constexpr std::uintmax_t kExpectedPezBotArchiveSize = 1'138'246;
    inline constexpr auto kExpectedPezBotArchiveMd5 =
        "4DEFEAB88624BAF05D28BBEBF6C86C01";
    inline constexpr auto kExpectedPezBotArchiveSha256 =
        "B7958B96CBE3A8C316290DF7148C63CA601D1DE2F96F6166D2C67FE069500FDF";
    inline constexpr auto kPezBotModFolderName = L"mp_PeZBOTWAW";
    inline constexpr auto kPezBotImportHelperFilename =
        L"WaWVR-PeZBOT-Import.ps1";
    inline constexpr auto kExpectedBinkSha256 =
        "1BA84B170EFBA8FB99B48E2AC0DED52D14B9F6CE657B65748E9A8385C3AB51BB";
    inline constexpr auto kExpectedNachtSha256 =
        "B9CEDBAB57F9793CA50F119C9A17B10A39A9953C78E5385C0E0D18AC49F4FE84";
    inline constexpr auto kExpectedDerRieseSha256 =
        "7C6286743ADBF7431A7945798559BE3622ADB03C99E36F64FE557F6163AFE902";
    inline constexpr auto kExpectedDerRieseLoadSha256 =
        "8C1C2CA633DD72ED3A139605375B646BA50D9A94811C408061BA6CE431E62559";
    inline constexpr auto kExpectedDerRiesePatchSha256 =
        "A97CE02875EB97ED04F1A3DF1B40C59065DDD2A567F1CF01C8B4413C6CD5E1F7";
    inline constexpr auto kExpectedLocalizedDerRieseSha256 =
        "7C5B6517785589E0C66A0535CDDAC5AFE4D3093910B23CD0A3A0257F75F37034";
    inline constexpr auto kExpectedDerRieseLoadVideoSha256 =
        "422837B1D59D554E3A34A4362B92E43A471D0DF146304C654A13A578341808A6";

    enum class Mode
    {
        diagnose,
        prepare,
        launch,
    };

    enum class LaunchTarget
    {
        nacht,
        der_riese,
        campaign_ber1,
        campaign_pel2_airfield,
        frontend_menu,
        offline_multiplayer,
    };

    enum class ExecutableKind
    {
        sp,
        mp,
    };

    enum class GameLanguage
    {
        english,
        french,
        italian,
        german,
        spanish,
    };

    struct InstalledGameLanguage
    {
        GameLanguage language = GameLanguage::english;
        std::string selector_sha256;

        friend bool operator==(
            const InstalledGameLanguage&,
            const InstalledGameLanguage&) = default;
    };

    // `automatic` preserves the historical command-line behavior when the
    // option is omitted. The release UI always sends enabled or disabled so
    // the user's choice is explicit and survives the later SP -> MP handoff.
    enum class BotPolicy
    {
        automatic,
        enabled,
        disabled,
    };

    // Stable identifiers distinguish executable byte identities that share a
    // gameplay kind. Never infer one profile from the other: every source and
    // staged executable is selected by its complete SHA-256.
    enum class ExecutableIdentityId
    {
        plutonium_sp_1_7_1263,
        plutonium_mp_1_7_1263,
        steam_sp_build_252004,
        steam_mp_build_252004,
    };

    struct SourceResolution
    {
        std::uint32_t width = 2560;
        std::uint32_t height = 1440;

        friend constexpr bool operator==(
            const SourceResolution&,
            const SourceResolution&) = default;
    };

    // Exact validated SP builds use the Quest 3 / Virtual Desktop native
    // packed source by default. The legacy default remains the safe MP mode;
    // MP does not receive the SP-only oversized-resolution renderer patch.
    // 4992 pixels remain the two native 2496-wide eye views. The additional
    // 1024 pixels hold the COD4-style dedicated physical-scope camera.
    inline constexpr SourceResolution kNativeSpSourceResolution{6016, 2688};
    inline constexpr SourceResolution kDefaultSourceResolution{2560, 1440};
    inline constexpr SourceResolution kPerformanceSourceResolution{3744, 2016};
    inline constexpr SourceResolution kFallbackSourceResolution{1024, 768};
    inline constexpr std::uint32_t kMinimumSourceWidth = 640;
    inline constexpr std::uint32_t kMinimumSourceHeight = 480;
    inline constexpr std::uint32_t kMaximumSourceWidth = 6016;
    inline constexpr std::uint32_t kMaximumSourceHeight = 2688;
    inline constexpr std::uint32_t kMaximumLegacySourceWidth = 3840;
    inline constexpr std::uint32_t kMaximumLegacySourceHeight = 2160;
    inline constexpr auto kMultiplayerHandoffConfigFilename =
        L"WaWVR-Multiplayer-Handoff.v1";

    struct MultiplayerHandoffConfig
    {
        std::filesystem::path game_dir;
        std::filesystem::path sp_runtime_dir;
        std::filesystem::path sp_home_dir;
        std::filesystem::path mp_source_exe;
        std::filesystem::path mp_runtime_dir;
        std::filesystem::path mp_home_dir;
        ExecutableIdentityId sp_executable_identity =
            ExecutableIdentityId::plutonium_sp_1_7_1263;
        ExecutableIdentityId mp_executable_identity =
            ExecutableIdentityId::plutonium_mp_1_7_1263;
        SourceResolution source_resolution = kDefaultSourceResolution;
        BotPolicy bot_policy = BotPolicy::automatic;

        friend bool operator==(
            const MultiplayerHandoffConfig&,
            const MultiplayerHandoffConfig&) = default;
    };

    struct Options
    {
        Mode mode = Mode::diagnose;
        std::optional<std::filesystem::path> game_dir;
        std::optional<std::filesystem::path> source_exe;
        // SP-only explicit source for the later stock-menu MP handoff. This
        // becomes part of the trusted SP LaunchPlan and is never parsed by the
        // runtime CoDWaWmp.exe shim.
        std::optional<std::filesystem::path> handoff_mp_source_exe;
        std::optional<std::filesystem::path> runtime_dir;
        std::optional<std::filesystem::path> home_dir;
        std::optional<std::filesystem::path> mod_dll;
        std::optional<std::filesystem::path> launcher_executable;
        std::optional<std::filesystem::path> pezbot_archive;
        std::optional<SourceResolution> source_resolution;
        BotPolicy bot_policy = BotPolicy::automatic;
        LaunchTarget launch_target = LaunchTarget::nacht;
        // Internal-only provenance set by make_multiplayer_handoff_options.
        // It is never command-line controllable.
        bool stock_multiplayer_handoff = false;
        // Internal-only exact identity requirement populated from the
        // authenticated/canonical runtime handoff record. Command-line users
        // cannot select an identity without also supplying matching bytes.
        std::optional<ExecutableIdentityId> required_source_identity;
        bool wait_for_exit = false;
        bool show_help = false;
    };

    struct LaunchPlan
    {
        Mode mode = Mode::diagnose;
        std::filesystem::path game_dir;
        std::filesystem::path source_exe;
        std::filesystem::path runtime_dir;
        std::filesystem::path home_dir;
        std::filesystem::path staged_exe;
        std::optional<std::filesystem::path> mod_dll;
        std::optional<std::filesystem::path> launcher_executable;
        std::optional<std::filesystem::path> pezbot_archive;
        // Exact MP source selected while resolving the trusted SP plan. The
        // stock-menu shim must consume the generated handoff configuration;
        // it never repeats executable or LocalAppData discovery itself.
        std::optional<std::filesystem::path> handoff_mp_source_exe;
        SourceResolution source_resolution = kDefaultSourceResolution;
        BotPolicy bot_policy = BotPolicy::automatic;
        LaunchTarget launch_target = LaunchTarget::nacht;
        ExecutableKind executable_kind = ExecutableKind::sp;
        ExecutableIdentityId executable_identity =
            ExecutableIdentityId::plutonium_sp_1_7_1263;
        std::optional<ExecutableIdentityId> handoff_mp_executable_identity;
        bool stock_multiplayer_handoff = false;
        // True only for the default WaWVR home or an internally validated
        // stock handoff. Explicit --homepath values remain caller-owned.
        bool home_dir_is_launcher_managed = false;
        bool pezbot_enabled = false;
        bool wait_for_exit = false;
    };

    struct PeInfo
    {
        std::uint16_t machine = 0;
        std::uint16_t characteristics = 0;
        std::uint16_t optional_magic = 0;
        std::uint32_t timestamp = 0;
        std::uint32_t entrypoint_rva = 0;
        std::uint64_t image_base = 0;

        [[nodiscard]] bool is_x86_pe32() const noexcept;
        [[nodiscard]] bool is_dll() const noexcept;
    };

    struct FileVersion
    {
        std::uint16_t major = 0;
        std::uint16_t minor = 0;
        std::uint16_t build = 0;
        std::uint16_t revision = 0;
    };

    struct DiagnosticCheck
    {
        std::wstring name;
        bool passed = false;
        std::wstring detail;
        bool required = true;
    };

    struct PezBotArchiveIdentity
    {
        std::uintmax_t size = kExpectedPezBotArchiveSize;
        std::string md5 = kExpectedPezBotArchiveMd5;
        std::string sha256 = kExpectedPezBotArchiveSha256;
    };

    enum class PezBotState
    {
        unavailable,
        archive_ready,
        installed,
        custom_folder_conflict,
        invalid_archive,
        helper_unavailable,
        import_failed,
    };

    struct PezBotStatus
    {
        PezBotState state = PezBotState::unavailable;
        std::optional<std::filesystem::path> archive;
        std::filesystem::path install_directory;
        std::wstring detail;

        [[nodiscard]] bool enabled() const noexcept
        {
            return state == PezBotState::installed;
        }
    };

    struct PezBotArchiveValidation
    {
        bool valid = false;
        std::wstring detail;
    };

    struct DiagnosticReport
    {
        std::vector<DiagnosticCheck> checks;
        std::wstring command_line;

        [[nodiscard]] bool passed() const noexcept;
    };

    struct LaunchResult
    {
        std::uint32_t process_id = 0;
        std::optional<std::uint32_t> exit_code;
        std::wstring injection_detail;
    };

    enum class ModulePollStatus
    {
        found,
        timed_out,
        process_exited,
    };

    struct ModulePollResult
    {
        ModulePollStatus status = ModulePollStatus::timed_out;
        std::optional<std::uintptr_t> module_base;
        std::uint32_t attempts = 0;
        std::uint32_t elapsed_ms = 0;
    };

    enum class SpRendererThreadSnapshotStatus
    {
        safe,
        instruction_pointer_in_critical_routine,
        critical_return_address_on_stack,
    };

    using ModuleProbe = std::function<std::optional<std::uintptr_t>()>;
    using ProcessAliveProbe = std::function<bool()>;
    using PollDelay = std::function<void(std::uint32_t)>;

    struct RuntimeDataLink
    {
        std::filesystem::path relative_name;
        std::filesystem::path source;
        std::filesystem::path destination;
    };

    struct RuntimeSupportFile
    {
        std::filesystem::path source;
        std::filesystem::path destination;
        std::string expected_sha256;
    };

    struct RuntimeHandoffFile
    {
        std::filesystem::path source;
        std::filesystem::path destination;
    };

    enum class HandoffParentWaitResult
    {
        ready,
        rejected_parent,
        timed_out,
        wait_failed,
    };

    struct SafeModeDialogIdentity
    {
        std::uint32_t owner_process_id = 0;
        std::wstring window_class;
        std::wstring title;
        std::wstring body;
        std::wstring no_button_class;
        int no_button_id = 0;
    };

    ModulePollResult poll_for_stable_module(
        const ModuleProbe& probe,
        const ProcessAliveProbe& process_alive,
        const PollDelay& delay,
        std::uint32_t timeout_ms,
        std::uint32_t interval_ms,
        std::uint32_t required_stable_observations = 2);
    SpRendererThreadSnapshotStatus classify_sp_renderer_thread_snapshot(
        std::uint32_t image_base,
        std::uint32_t instruction_pointer,
        std::span<const std::uint32_t> stack_words) noexcept;

    Options parse_options(int argc, wchar_t** argv);
    Options apply_user_facing_defaults(
        Options options,
        const std::filesystem::path& launcher_executable);
    Options make_user_facing_default_options(
        const std::filesystem::path& launcher_executable);
    Options make_user_facing_multiplayer_default_options(
        const std::filesystem::path& launcher_executable);
    Options make_multiplayer_handoff_options(
        const std::filesystem::path& launcher_executable,
        const MultiplayerHandoffConfig& config);
    LaunchPlan resolve_plan(const Options& options);
    DiagnosticReport diagnose(const LaunchPlan& plan);
    void prepare_runtime(const LaunchPlan& plan);
    void prepare_runtime_data_links(const LaunchPlan& plan);
    bool enforce_offline_multiplayer_listen_profile(const LaunchPlan& plan);
    LaunchResult launch_game(const LaunchPlan& plan);

    PeInfo inspect_pe(const std::filesystem::path& path);
    FileVersion inspect_file_version(const std::filesystem::path& path);
    std::string sha256_file(const std::filesystem::path& path);
    std::string md5_file(const std::filesystem::path& path);
    GameLanguage parse_game_language_selector(std::string_view contents);
    InstalledGameLanguage detect_installed_game_language(
        const std::filesystem::path& game_dir);
    const wchar_t* game_language_code(GameLanguage language) noexcept;
    const wchar_t* game_language_display_name(GameLanguage language) noexcept;
    SourceResolution parse_source_resolution(std::wstring_view text);
    std::wstring source_resolution_text(SourceResolution resolution);
    const wchar_t* bot_policy_name(BotPolicy policy) noexcept;
    bool source_resolution_fits_desktop(
        SourceResolution source,
        SourceResolution desktop) noexcept;
    bool plan_source_resolution_is_desktop_compatible(
        const LaunchPlan& plan,
        SourceResolution desktop) noexcept;
    std::wstring desktop_source_resolution_compatibility_detail(
        SourceResolution source,
        SourceResolution desktop);
    std::wstring plan_source_resolution_compatibility_detail(
        const LaunchPlan& plan,
        SourceResolution desktop);
    std::wstring quote_windows_argument(const std::wstring& argument);
    std::wstring build_game_command_line(const LaunchPlan& plan);
    std::optional<ExecutableIdentityId> executable_identity_from_sha256(
        ExecutableKind kind,
        std::string_view sha256) noexcept;
    ExecutableKind executable_kind_for_identity(
        ExecutableIdentityId identity);
    std::string_view executable_identity_stable_id(
        ExecutableIdentityId identity);
    bool executable_identity_uses_steam(
        ExecutableIdentityId identity);
    std::vector<wchar_t> build_child_environment_block(
        ExecutableIdentityId identity,
        const std::vector<std::wstring>& inherited_entries);
    std::optional<std::filesystem::path> discover_pezbot_archive(
        const std::optional<std::filesystem::path>& explicit_archive,
        const std::optional<std::filesystem::path>& launcher_executable,
        const std::filesystem::path& downloads_directory);
    PezBotArchiveValidation validate_pezbot_archive(
        const std::filesystem::path& archive,
        const PezBotArchiveIdentity& identity = {});
    std::filesystem::path pezbot_install_directory(const LaunchPlan& plan);
    PezBotStatus inspect_optional_pezbot(
        const LaunchPlan& plan,
        const PezBotArchiveIdentity& identity = {});
    PezBotStatus prepare_optional_pezbot(
        const LaunchPlan& plan,
        const PezBotArchiveIdentity& identity = {},
        const std::optional<std::filesystem::path>& helper_override = std::nullopt);
    bool should_import_optional_pezbot(const LaunchPlan& plan) noexcept;
    const wchar_t* pezbot_state_name(PezBotState state) noexcept;
    std::filesystem::path select_automatic_source(
        const std::filesystem::path& staged_exe,
        bool staged_exe_verified,
        const std::filesystem::path& game_exe,
        bool game_exe_present,
        const std::filesystem::path& legacy_exe);
    bool is_same_path(
        const std::filesystem::path& left,
        const std::filesystem::path& right);
    bool is_same_or_descendant(
        const std::filesystem::path& candidate,
        const std::filesystem::path& parent);
    bool is_safe_runtime_link_name(const std::filesystem::path& relative_name);
    bool is_multiplayer_handoff_invocation(
        const std::filesystem::path& executable_path) noexcept;
    std::filesystem::path derive_multiplayer_runtime_dir(
        const std::filesystem::path& sp_runtime_dir,
        ExecutableIdentityId mp_identity =
            ExecutableIdentityId::plutonium_mp_1_7_1263);
    std::filesystem::path derive_multiplayer_home_dir(
        const std::filesystem::path& sp_home_dir,
        ExecutableIdentityId mp_identity =
            ExecutableIdentityId::plutonium_mp_1_7_1263);
    MultiplayerHandoffConfig derive_multiplayer_handoff_config(
        const LaunchPlan& sp_plan);
    std::filesystem::path multiplayer_handoff_config_path(
        const std::filesystem::path& handoff_executable);
    std::string serialize_multiplayer_handoff_config(
        const MultiplayerHandoffConfig& config);
    MultiplayerHandoffConfig parse_multiplayer_handoff_config(
        std::string_view text,
        const std::filesystem::path& handoff_executable);
    MultiplayerHandoffConfig read_multiplayer_handoff_config(
        const std::filesystem::path& handoff_executable);
    bool is_expected_handoff_parent_path(
        const std::filesystem::path& parent_executable,
        const std::filesystem::path& handoff_executable);
    HandoffParentWaitResult classify_handoff_parent_wait(
        bool exact_parent,
        std::uint32_t wait_result) noexcept;
    HandoffParentWaitResult wait_for_multiplayer_handoff_parent(
        const std::filesystem::path& handoff_executable) noexcept;
    const char* handoff_parent_wait_result_name(
        HandoffParentWaitResult result) noexcept;
    bool is_exact_waw_safe_mode_dialog(
        const SafeModeDialogIdentity& identity,
        std::uint32_t expected_process_id);
    bool is_exact_waw_optimal_settings_dialog(
        const SafeModeDialogIdentity& identity,
        std::uint32_t expected_process_id);
    std::vector<RuntimeDataLink> runtime_data_links(const LaunchPlan& plan);
    std::vector<RuntimeSupportFile> runtime_support_files(const LaunchPlan& plan);
    void stage_runtime_language_selector(const LaunchPlan& plan);
    std::vector<RuntimeHandoffFile> runtime_handoff_files(const LaunchPlan& plan);
    std::wstring mode_name(Mode mode, LaunchTarget launch_target);
    std::wstring usage_text();
}
