#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cwctype>
#include <cmath>
#include <stdexcept>
#include <io.h>
#include <deque>
#include <scssdk_telemetry.h>
#include <common/scssdk_telemetry_truck_common_channels.h>
#include <common/scssdk_telemetry_trailer_common_channels.h>
#include "dispatch.hpp"
#include "overlay.hpp"
#include "preview.hpp"
#include "performance.hpp"
#include "labels.hpp"
#include "offline_assets.hpp"

namespace {
std::mutex log_mutex;
std::atomic<bool> initialized{false};
std::atomic<bool> stopping{false};
HANDLE worker = nullptr;
HANDLE log_event = nullptr;
std::deque<std::string> log_queue;
size_t log_bytes = 0;
DWORD frame_thread = 0;
scs_log_t game_log = nullptr;
std::mutex telemetry_mutex;
jd::TelemetrySample telemetry;

std::string Escape(const std::string& value) {
    std::string out;
    for (unsigned char c : value) {
        if (c == '\\' || c == '"') { out += '\\'; out += char(c); }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c >= 32) out += char(c);
    }
    return out;
}

std::string SnapshotJson() {
    const auto s = jd::ReadSnapshot();
    const auto overlay = jd::overlay::Inspect();
    const auto t = jd::ReadTelemetry();
    std::ostringstream out;
    out << "{\"supported\":" << (s.supported ? "true" : "false")
        << ",\"world_ready\":" << (s.world_ready ? "true" : "false")
        << ",\"can_generate\":" << (s.can_generate ? "true" : "false")
        << ",\"busy\":" << (s.busy ? "true" : "false")
        << ",\"version\":\"" << Escape(s.version)
        << "\",\"message\":\"" << Escape(s.message)
        << "\",\"nearest_company\":" << s.nearest_company
        << ",\"generation\":" << s.generation;
    out << ",\"language\":\"" << Escape(s.language) << "\",\"mass_unit\":\"" << Escape(s.mass_unit)
        << "\",\"miles\":" << (s.miles?"true":"false")
        << ",\"selection\":{\"cargo\":" << s.selection.cargo << ",\"source\":" << s.selection.source
        << ",\"destination\":" << s.selection.destination << ",\"trailer\":" << s.selection.trailer
        << ",\"mass_kg\":" << s.mass_kg << ",\"distance_m\":" << s.distance_m
        << ",\"price_base\":" << s.price_base << ",\"price_valid\":" << (s.price_valid?"true":"false")
        << ",\"real_minutes\":" << s.real_minutes
        << ",\"currency_rate\":" << s.currency_rate << ",\"currency_code\":\"" << Escape(s.currency_code) << "\""
        << ",\"route_pending\":" << (s.route_pending?"true":"false")
        << ",\"route_failed\":" << (s.route_failed?"true":"false") << ",\"random_revision\":" << s.random_revision << '}';
    out << ",\"overlay\":{\"running\":" << (overlay.running ? "true" : "false")
        << ",\"attached\":" << (overlay.attached ? "true" : "false")
        << ",\"interactive\":" << (overlay.interactive ? "true" : "false")
        << ",\"panel\":" << (overlay.panel?"true":"false")
        << ",\"icon\":" << (overlay.icon?"true":"false")
        << ",\"translations_ready\":" << (jd::AssetsReady()?"true":"false") << '}';
    out << ",\"telemetry\":{\"driving\":" << (t.driving ? "true" : "false")
        << ",\"placement_valid\":" << (t.placement_valid ? "true" : "false")
        << ",\"speed_valid\":" << (t.speed_valid ? "true" : "false")
        << ",\"speed\":" << t.speed << ",\"x\":" << t.x << ",\"y\":" << t.y << ",\"z\":" << t.z
        << ",\"trailer_connected_valid\":" << (t.trailer_connected_valid ? "true" : "false")
        << ",\"trailer_connected\":" << (t.trailer_connected ? "true" : "false")
        << ",\"trailer_placement_valid\":" << (t.trailer_placement_valid ? "true" : "false")
        << ",\"trailer_x\":" << t.trailer_x << ",\"trailer_y\":" << t.trailer_y
        << ",\"trailer_z\":" << t.trailer_z << '}';
    const auto choices = [&out](const char* key, const std::vector<jd::Choice>& values) {
        out << ",\"" << key << "\":[";
        bool first = true;
        for (const auto& c : values) {
            if (!first) out << ',';
            first = false;
            out << "{\"id\":" << c.id << ",\"label\":\"" << Escape(c.label)
                << "\",\"token\":\"" << Escape(c.token) << "\"}";
        }
        out << ']';
    };
    choices("cargos", s.cargos);
    choices("companies", s.companies);
    choices("trailers", s.trailers);
    out << '}';
    return out.str();
}

// Explicit test mode is enabled only by the isolated test launcher. Production
// sessions cannot consume command files left by an earlier integration test.
void ReadTestCommand() {
    static const bool enabled = GetEnvironmentVariableW(L"JD_SP_TEST_CONTROL", nullptr, 0) != 0;
    if (!enabled) return;
    const auto path = std::filesystem::path(jd::DataDirectory()) / L"test-command.txt";
    std::ifstream input(path);
    if (!input) return;
    std::string command;
    input >> command;
    jd::Request request;
    if ((command == "generate"||command=="select") && input >> request.cargo >> request.source >> request.destination >> request.trailer) {
        unsigned immediate = 1;
        if (input >> immediate) request.take_immediately = immediate != 0;
        jd::Log("Integration test queues job request");
        if(command=="select")jd::Select(request);else jd::Submit(request);
    } else if(command=="surprise") {
        float mass=50,distance=50;if(input>>mass>>distance)jd::Surprise(mass,distance);
    } else if(command=="audit") {
        jd::RequestAudit();
    } else if(command=="open"||command=="close") {
        jd::overlay::QueueTestPanel(command=="open");
    } else if (command == "refresh") {
        jd::RequestRefresh();
    } else {
        jd::Log("Ignored invalid integration command");
    }
    input.close();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void FlushLogs() {
    std::deque<std::string> batch;
    { std::lock_guard<std::mutex> guard(log_mutex); batch.swap(log_queue); log_bytes = 0; }
    if (batch.empty()) return;
    const auto path = std::filesystem::path(jd::DataDirectory()) / L"dispatcher.log";
    FILE* file = _wfopen(path.c_str(), L"ab");
    if (!file) return;
    for (const auto& line : batch) fwrite(line.data(), 1, line.size(), file);
    fclose(file);
}

DWORD WINAPI OverlayWorker(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    jd::Log("Overlay worker started, thread=%lu", GetCurrentThreadId());
    if (!stopping && !GetEnvironmentVariableW(L"JD_SP_NO_OVERLAY", nullptr, 0)) {
        try { jd::overlay::Start(); }
        catch (const std::exception& e) { jd::Log("Overlay initialization exception: %s", e.what()); }
        catch (...) { jd::Log("Overlay initialization exception"); }
    }
    const bool diagnostics = GetEnvironmentVariableW(L"JD_SP_DIAGNOSTICS", nullptr, 0) ||
                             GetEnvironmentVariableW(L"JD_SP_TEST_CONTROL", nullptr, 0);
    ULONGLONG last_dump = 0;
    ULONGLONG last_performance = GetTickCount64();
    unsigned prepared_revision=0;
    ULONGLONG next_preparation=0;
    while (!stopping.load()) {
        WaitForSingleObject(log_event, 100);
        const auto revision=jd::AssetsWorldRevision();
        if(jd::WorldLoaded()&&revision&&revision!=prepared_revision&&GetTickCount64()>=next_preparation){
            next_preparation=GetTickCount64()+5000;
            try{
            const bool ready=jd::offline::Run(jd::offline::PackageRoot(),jd::offline::GameDirectory(),
                std::filesystem::path(jd::DataDirectory()),jd::offline::GameHome(),[&]{return stopping.load()||!jd::WorldLoaded()||jd::AssetsWorldRevision()!=revision;});
            if(ready&&!stopping&&jd::WorldLoaded()&&jd::AssetsWorldRevision()==revision){
                jd::AssetsPrepared(revision);prepared_revision=revision;
            }
            }catch(const std::exception& error){jd::Log("Offline preparation error: %s",error.what());}
            catch(...){jd::Log("Offline preparation error");}
        }
        jd::labels::PumpLanguage(std::filesystem::path(jd::DataDirectory()));
        FlushLogs();
        const auto now = GetTickCount64();
        if(now-last_performance>30000) {
            last_performance=now;
            for(const auto& sample : {std::pair<const char*,jd::perf::Counter*>("adapter",&jd::perf::adapter),{"overlay",&jd::perf::overlay}}) {
                const auto count=sample.second->count.exchange(0);
                const auto total=sample.second->total_us.exchange(0);
                const auto worst=sample.second->worst_us.exchange(0);
                if(count)jd::Log("Performance %s samples=%llu average_ms=%.4f worst_ms=%.3f",sample.first,
                    static_cast<unsigned long long>(count),double(total)/count/1000.,double(worst)/1000.);
            }
        }
        if (diagnostics && now - last_dump > 10000) {
            last_dump = now;
            auto path = std::filesystem::path(jd::DataDirectory()) / L"status.json";
            auto temporary = path; temporary += L".tmp";
            { std::ofstream output(temporary, std::ios::binary | std::ios::trunc); output << SnapshotJson(); }
            MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
    FlushLogs();
    return 0;
}

void SCSAPIFUNC OnFrame(scs_event_t, const void*, scs_context_t) {
    if (stopping || !initialized) return;
    if (!frame_thread) {
        frame_thread = GetCurrentThreadId();
        jd::Log("First telemetry frame-end, thread=%lu", frame_thread);
    }
    if (frame_thread != GetCurrentThreadId()) {
        jd::Log("Ignoring callback on changed telemetry thread");
        return;
    }
    try {
        jd::perf::Scope frame_performance(jd::perf::adapter);
        ReadTestCommand();
        jd::Frame();
        jd::preview::Frame();
    } catch (const std::exception& e) { jd::Log("Frame exception: %s", e.what()); }
    catch (...) { jd::Log("Frame exception"); }
}

void SCSAPIFUNC OnState(scs_event_t event, const void*, scs_context_t) {
    {
        std::lock_guard<std::mutex> guard(telemetry_mutex);
        telemetry.driving = event == SCS_TELEMETRY_EVENT_started;
        if (!telemetry.driving) telemetry.placement_valid = false;
    }
    jd::Log("Telemetry %s", event == SCS_TELEMETRY_EVENT_started ? "started" : "paused");
    // Pause precedes world unloading. Release the preview's world-bound scene
    // while that world and its renderer are still alive.
    if(event==SCS_TELEMETRY_EVENT_paused&&frame_thread==GetCurrentThreadId())jd::preview::Shutdown();
}

void SCSAPIFUNC OnPosition(scs_string_t, scs_u32_t, const scs_value_t* value, scs_context_t) {
    std::lock_guard<std::mutex> guard(telemetry_mutex);
    telemetry.placement_valid = value && value->type == SCS_VALUE_TYPE_dplacement;
    if (telemetry.placement_valid)
        telemetry.placement_valid = std::isfinite(value->value_dplacement.position.x) &&
            std::isfinite(value->value_dplacement.position.y) && std::isfinite(value->value_dplacement.position.z);
    if (telemetry.placement_valid) {
        telemetry.x = value->value_dplacement.position.x;
        telemetry.y = value->value_dplacement.position.y;
        telemetry.z = value->value_dplacement.position.z;
    }
}

void SCSAPIFUNC OnSpeed(scs_string_t, scs_u32_t, const scs_value_t* value, scs_context_t) {
    std::lock_guard<std::mutex> guard(telemetry_mutex);
    telemetry.speed_valid = value && value->type == SCS_VALUE_TYPE_float && std::isfinite(value->value_float.value);
    if (telemetry.speed_valid) telemetry.speed = value->value_float.value;
}

void SCSAPIFUNC OnNavigationDistance(scs_string_t, scs_u32_t, const scs_value_t* value, scs_context_t) {
    static float last_logged = -1;
    if (!value || value->type != SCS_VALUE_TYPE_float || !std::isfinite(value->value_float.value)) {
        last_logged = -1;
        return;
    }
    const float meters = value->value_float.value;
    if (last_logged < 0 || std::fabs(meters - last_logged) > 1000) {
        jd::Log("SDK navigation distance=%.1f meters", meters);
        last_logged = meters;
    }
}

void SCSAPIFUNC OnTrailerConnected(scs_string_t, scs_u32_t, const scs_value_t* value, scs_context_t) {
    bool changed = false, connected = false;
    {
        std::lock_guard<std::mutex> guard(telemetry_mutex);
        const bool valid = value && value->type == SCS_VALUE_TYPE_bool;
        connected = valid && value->value_bool.value != 0;
        changed = valid && (!telemetry.trailer_connected_valid || connected != telemetry.trailer_connected);
        telemetry.trailer_connected_valid = valid;
        telemetry.trailer_connected = connected;
    }
    if (changed) jd::Log("SDK trailer.connected=%d", connected);
}

void SCSAPIFUNC OnTrailerPosition(scs_string_t, scs_u32_t, const scs_value_t* value, scs_context_t) {
    std::lock_guard<std::mutex> guard(telemetry_mutex);
    telemetry.trailer_placement_valid = value && value->type == SCS_VALUE_TYPE_dplacement;
    if (telemetry.trailer_placement_valid)
        telemetry.trailer_placement_valid = std::isfinite(value->value_dplacement.position.x) &&
            std::isfinite(value->value_dplacement.position.y) && std::isfinite(value->value_dplacement.position.z);
    if (telemetry.trailer_placement_valid) {
        telemetry.trailer_x = value->value_dplacement.position.x;
        telemetry.trailer_y = value->value_dplacement.position.y;
        telemetry.trailer_z = value->value_dplacement.position.z;
    }
}

void LogAttributes(const scs_named_value_t* attributes) {
    if (!attributes) return;
    for (unsigned i = 0; i < 100 && attributes[i].name; ++i) {
        const auto& a = attributes[i];
        switch (a.value.type) {
        case SCS_VALUE_TYPE_string:
            jd::Log("  %s=%s", a.name, a.value.value_string.value ? a.value.value_string.value : ""); break;
        case SCS_VALUE_TYPE_bool:
            jd::Log("  %s=%d", a.name, int(a.value.value_bool.value)); break;
        case SCS_VALUE_TYPE_u32:
            jd::Log("  %s=%u", a.name, unsigned(a.value.value_u32.value)); break;
        case SCS_VALUE_TYPE_u64:
            jd::Log("  %s=%llu", a.name, static_cast<unsigned long long>(a.value.value_u64.value)); break;
        case SCS_VALUE_TYPE_float:
            jd::Log("  %s=%g", a.name, double(a.value.value_float.value)); break;
        default: break;
        }
    }
}

void SCSAPIFUNC OnConfiguration(scs_event_t, const void* info, scs_context_t) {
    const auto* config = static_cast<const scs_telemetry_configuration_t*>(info);
    if (!config || !config->id) return;
    if(strcmp(config->id,"truck")==0) {
        jd::MarkWorldLoaded(config->attributes&&config->attributes[0].name);
        return;
    }
    if(strcmp(config->id,"job")!=0)return;
    jd::Log("SDK job configuration changed");
    LogAttributes(config->attributes);
}

void SCSAPIFUNC OnGameplay(scs_event_t, const void* info, scs_context_t) {
    const auto* event = static_cast<const scs_telemetry_gameplay_event_t*>(info);
    if (!event || !event->id) return;
    jd::Log("SDK gameplay event: %s", event->id);
    LogAttributes(event->attributes);
}
} // namespace

namespace jd {
TelemetrySample ReadTelemetry() {
    std::lock_guard<std::mutex> guard(telemetry_mutex);
    return telemetry;
}
std::wstring DataDirectory() {
    static const auto path = [] {
        wchar_t override_path[32768]{};
        const DWORD len = GetEnvironmentVariableW(L"JD_SP_DATA", override_path, 32768);
        HMODULE module=nullptr;wchar_t module_path[32768]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ReadTelemetry),&module);
        GetModuleFileNameW(module,module_path,32768);
        const auto parent=std::filesystem::path(module_path).parent_path();
        wchar_t configured[32768]{};
        GetPrivateProfileStringW(L"JobDispatcherSP",L"DataDirectory",L"",configured,32768,(parent/L"JobDispatcherSP.ini").c_str());
        std::filesystem::path result=(len&&len<32768)?std::filesystem::path(override_path):
            configured[0]&&_wcsicmp(configured,L"auto")?std::filesystem::path(configured):parent/L"JobDispatcherSP"/L"data";
        if(result.is_relative())result=parent/result;
        std::error_code error;
        std::filesystem::create_directories(result, error);
        const bool automatic=!(len&&len<32768)&&(!configured[0]||!_wcsicmp(configured,L"auto"));
        if(automatic){
            const auto probe=result/(L".write-probe-"+std::to_wstring(GetCurrentProcessId()));
            HANDLE file=CreateFileW(probe.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
            if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
            else{
                PWSTR local=nullptr;
                if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))){
                    uint64_t identity=14695981039346656037ull;
                    for(auto c:parent.wstring()){identity^=static_cast<uint64_t>(std::towlower(c));identity*=1099511628211ull;}
                    result=std::filesystem::path(local)/L"JobDispatcherSP"/std::to_wstring(identity)/L"data";CoTaskMemFree(local);
                    std::filesystem::create_directories(result,error);
                    const auto source=parent/L"JobDispatcherSP"/L"data";
                    if(std::filesystem::is_directory(source,error))for(const auto& entry:std::filesystem::directory_iterator(source,error)){
                        const auto name=entry.path().filename().wstring();
                        if(name.rfind(L"ui-labels.",0)==0&&entry.path().extension()==L".tsv")
                            std::filesystem::copy_file(entry.path(),result/entry.path().filename(),std::filesystem::copy_options::overwrite_existing,error);
                    }
                }
            }
        }
        return std::filesystem::absolute(result,error).wstring();
    }();
    return path;
}

void Log(const char* format, ...) {
    char message[4096]{};
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char line[4352]{};
    snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u [%lu] %s\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
        GetCurrentThreadId(), message);
    {
        std::lock_guard<std::mutex> guard(log_mutex);
        while (!log_queue.empty() && log_bytes + strlen(line) > 65536) {
            log_bytes -= log_queue.front().size(); log_queue.pop_front();
        }
        log_queue.emplace_back(line); log_bytes += log_queue.back().size();
    }
    if (log_event) SetEvent(log_event);
}
} // namespace jd

extern "C" SCSAPI_RESULT scs_telemetry_init(
    const scs_u32_t version, const scs_telemetry_init_params_t* const parameters) {
    if (version != SCS_TELEMETRY_VERSION_1_01 || !parameters) return SCS_RESULT_unsupported;
    const auto* params = static_cast<const scs_telemetry_init_params_v101_t*>(parameters);
    if (!params->common.game_id || strcmp(params->common.game_id, "eut2") != 0)
        return SCS_RESULT_unsupported;
    if (initialized.exchange(true)) return SCS_RESULT_already_registered;
    stopping = false;
    game_log = params->common.log;
    jd::Log("Job Dispatcher SP initialization, telemetry API=%u.%u, game=%s, thread=%lu",
        SCS_GET_MAJOR_VERSION(version), SCS_GET_MINOR_VERSION(version),
        params->common.game_id, GetCurrentThreadId());
    jd::Initialize();
    if (!params->register_for_event ||
        params->register_for_event(SCS_TELEMETRY_EVENT_frame_end, OnFrame, nullptr) != SCS_RESULT_ok) {
        initialized = false;
        jd::Shutdown();
        return SCS_RESULT_generic_error;
    }
    params->register_for_event(SCS_TELEMETRY_EVENT_started, OnState, nullptr);
    params->register_for_event(SCS_TELEMETRY_EVENT_paused, OnState, nullptr);
    params->register_for_event(SCS_TELEMETRY_EVENT_configuration, OnConfiguration, nullptr);
    params->register_for_event(SCS_TELEMETRY_EVENT_gameplay, OnGameplay, nullptr);
    if (params->register_for_channel) {
        params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_U32_NIL,
            SCS_VALUE_TYPE_dplacement, SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnPosition, nullptr);
        params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_U32_NIL,
            SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnSpeed, nullptr);
        params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_navigation_distance, SCS_U32_NIL,
            SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnNavigationDistance, nullptr);
        const auto connected_result = params->register_for_channel(SCS_TELEMETRY_TRAILER_CHANNEL_connected, SCS_U32_NIL,
            SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnTrailerConnected, nullptr);
        const auto trailer_position_result = params->register_for_channel(SCS_TELEMETRY_TRAILER_CHANNEL_world_placement, SCS_U32_NIL,
            SCS_VALUE_TYPE_dplacement, SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnTrailerPosition, nullptr);
        jd::Log("Trailer telemetry registration: connected=%d placement=%d", connected_result, trailer_position_result);
    }
    log_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    worker = CreateThread(nullptr, 0, OverlayWorker, nullptr, 0, nullptr);
    if (!worker || !log_event) {
        stopping = true;
        if (worker) { WaitForSingleObject(worker, INFINITE); CloseHandle(worker); worker = nullptr; }
        if (log_event) { CloseHandle(log_event); log_event = nullptr; }
        initialized = false; jd::Shutdown(); FlushLogs();
        return SCS_RESULT_generic_error;
    }
    if (game_log) game_log(SCS_LOG_TYPE_message, "[JobDispatcherSP] Native dispatcher plugin loaded.");
    return SCS_RESULT_ok;
}

extern "C" SCSAPI_VOID scs_telemetry_shutdown() {
    if (!initialized.exchange(false)) return;
    stopping = true;
    if (log_event) SetEvent(log_event);
    if (worker) {
        // Do not unload code while the driver initialization worker still runs.
        // Start is normally a single sub-second DX11 device creation attempt.
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
        worker = nullptr;
    }
    jd::overlay::Stop();
    jd::preview::Shutdown();
    jd::Shutdown();
    jd::Log("Plugin shutdown");
    FlushLogs();
    if (log_event) { CloseHandle(log_event); log_event = nullptr; }
    frame_thread = 0;
    game_log = nullptr;
}

// Read-only diagnostic export, used by the loader test harness.
extern "C" __declspec(dllexport) const char* jd_snapshot_json() {
    static thread_local std::string data;
    data = SnapshotJson();
    return data.c_str();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
