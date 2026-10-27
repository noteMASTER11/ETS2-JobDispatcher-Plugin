#include <windows.h>
#include <scssdk_telemetry.h>
#include <array>
#include <cstring>
#include <cstdio>

namespace {
std::array<scs_telemetry_event_callback_t, 8> callbacks{};
void SCSAPIFUNC Log(scs_log_type_t, scs_string_t) {}
scs_result_t SCSAPIFUNC Register(scs_event_t event, scs_telemetry_event_callback_t callback, scs_context_t) {
    if (event >= callbacks.size()) return SCS_RESULT_invalid_parameter;
    callbacks[event] = callback;
    return SCS_RESULT_ok;
}
int Fail(const char* text) { fprintf(stderr, "FAIL: %s (Windows error %lu)\n", text, GetLastError()); return 1; }
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return Fail("DLL path required");
    SetEnvironmentVariableW(L"JD_SP_NO_OVERLAY", L"1");
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) return Fail("LoadLibrary");
    using Init = scs_result_t (*)(scs_u32_t, const scs_telemetry_init_params_t*);
    const auto init = reinterpret_cast<Init>(GetProcAddress(dll, "scs_telemetry_init"));
    const auto stop = reinterpret_cast<void(*)()>(GetProcAddress(dll, "scs_telemetry_shutdown"));
    const auto snapshot = reinterpret_cast<const char*(*)()>(GetProcAddress(dll, "jd_snapshot_json"));
    if (!init || !stop || !snapshot) return Fail("Required exports");
    scs_telemetry_init_params_v101_t params{};
    params.common.game_name = "Loader ABI test";
    params.common.game_id = "amtrucks";
    params.common.log = Log;
    params.register_for_event = Register;
    if (init(0, &params) != SCS_RESULT_unsupported) return Fail("Reject telemetry version");
    if (init(SCS_TELEMETRY_VERSION_1_01, &params) != SCS_RESULT_unsupported)
        return Fail("Reject other game id");
    params.common.game_id = "eut2";
    if (init(SCS_TELEMETRY_VERSION_1_01, &params) != SCS_RESULT_ok) return Fail("SDK init");
    if (!callbacks[SCS_TELEMETRY_EVENT_frame_end]) return Fail("Frame callback registration");
    callbacks[SCS_TELEMETRY_EVENT_frame_end](SCS_TELEMETRY_EVENT_frame_end, nullptr, nullptr);
    const char* json = snapshot();
    if (!json || !strstr(json, "\"can_generate\":false") || !strstr(json, "\"supported\":false"))
        return Fail("Wrong executable must disable native operations");
    puts(json);
    stop();
    // Shutdown is idempotent, including SDK teardown after an unsuccessful reload.
    stop();
    FreeLibrary(dll);
    puts("PASS: loader ABI, exports, callbacks, version guard, shutdown");
    return 0;
}
