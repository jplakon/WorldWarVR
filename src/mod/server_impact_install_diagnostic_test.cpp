// SPDX-License-Identifier: GPL-3.0-only
#include "server_impact_install_diagnostic.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

int main() {
    using namespace wawvr::mod;
    unsigned checks{}, failures{};
    const auto check = [&](const bool pass, const char* const message) {
        ++checks;
        if (!pass) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
    };
    const std::array<const char*, 7> names{
        "installed", "already-installed", "disabled", "unsupported",
        "fingerprint-mismatch", "thread-suspend-failed", "patch-failed"};
    for (std::uint32_t status = 0; status < names.size(); ++status) {
        DeferredServerImpactInstallDiagnostic deferred;
        ServerImpactInstallDiagnosticSnapshot snapshot{
            .status_known = true, .status = status,
            .installed_at_record = status <= 1,
            .enabled_at_record = status <= 1,
            .post_gameplay = {1, true}, .post_server_impact = {1, true},
            .post_disable_xr = {0, false}};
        ServerImpactInstallDiagnosticSnapshot output{};
        check(!deferred.claim(true, &output), "no fabricated result before record");
        check(deferred.record(snapshot), "first actual status records");
        check(!deferred.claim(false, &output), "unready logger does not claim");
        check(!deferred.claim(true, nullptr), "null output does not claim");
        auto later = snapshot;
        later.status = 999; later.post_gameplay = {8, false};
        check(!deferred.record(later), "later record cannot replace first result");
        check(deferred.claim(true, &output), "ready logger claims exactly once");
        check(output.status_known && output.status == status &&
            output.installed_at_record == (status <= 1) &&
            output.enabled_at_record == (status <= 1),
            "actual result and hook-state bits preserved");
        check(output.post_gameplay.length_result == 1 &&
            output.post_gameplay.exact_one &&
            output.post_server_impact.length_result == 1 &&
            output.post_server_impact.exact_one &&
            output.post_disable_xr.length_result == 0 &&
            !output.post_disable_xr.exact_one,
            "post-install request observation copied without interpretation");
        snapshot.status = 888;
        check(output.status == status, "owned output is independent of caller snapshot");
        check(!deferred.claim(true, &output) && !deferred.record(later),
            "emission and record remain spent for process lifetime");
        check(std::strcmp(server_impact_install_status_name(status), names[status]) == 0,
            "numeric native result has truthful descriptive label");
    }
    DeferredServerImpactInstallDiagnostic unknown;
    ServerImpactInstallDiagnosticSnapshot absent{}, observed{};
    check(unknown.record(absent) && unknown.claim(true, &observed) &&
        !observed.status_known, "explicit unknown remains unknown, never installed proof");
    check(std::strcmp(server_impact_install_status_name(7), "unknown-status") == 0 &&
        std::strcmp(server_impact_install_status_name(
            (std::numeric_limits<std::uint32_t>::max)()), "unknown-status") == 0,
        "unrecognized values have bounded unknown labels");
    std::printf("%u checks, %u failures; pure deferred observer status only\n", checks, failures);
    return failures ? 1 : 0;
}
