#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include "overlay.hpp"
#include "overlay_model.hpp"
#include "input_capture.hpp"
#include "preview.hpp"
#include "performance.hpp"
#include "labels.hpp"
#include "display_units.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace jd::overlay {
namespace {
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ClipFn = BOOL(WINAPI*)(const RECT*);
using WarpFn = BOOL(WINAPI*)(int, int);
using SetCursorFn = HCURSOR(WINAPI*)(HCURSOR);
PresentFn real_present = nullptr;
ResizeFn real_resize = nullptr;
ClipFn real_clip = nullptr;
WarpFn real_warp = nullptr;
SetCursorFn real_cursor = nullptr;
std::vector<void*> hook_targets;
std::atomic<bool> running{false};
std::atomic<bool> stopping{false};
std::atomic<bool> interactive{false};
std::atomic<unsigned> active_hooks{0};
std::recursive_mutex ui_mutex;
HWND game_window = nullptr;
std::atomic<HWND> input_window{nullptr};
std::atomic<bool> subclass_attached{false};
bool renderer_thread_warning = false;
UINT detach_message = 0;
constexpr UINT_PTR subclass_id = 0x4a445350;
IDXGISwapChain* game_chain = nullptr;
ID3D11Device* device = nullptr;
ID3D11DeviceContext* context = nullptr;
ID3D11RenderTargetView* target_view = nullptr;
ImGuiContext* imgui_context = nullptr;
bool panel_open = false;
std::atomic<int> test_panel{-1};
bool right_button_consumed = false;
bool clip_saved = false;
bool win32_initialized = false;
bool dx11_initialized = false;
RECT previous_clip{};
Snapshot snapshot;
Request request;
std::chrono::steady_clock::time_point next_snapshot{};
std::string settings_path;
float ui_scale = 1.0f;
std::string T(const char* key){return labels::Ui(key);}
void LocaleFonts(){
    auto& io=ImGui::GetIO();
    const auto language=labels::Language();
    const char* family=language=="ja_jp"?"YuGothM.ttc":language=="ko_kr"?"malgun.ttf":
        language=="zh_cn"||language=="zh_tw"?"msyh.ttc":language=="ka_ge"?"sylfaen.ttf":nullptr;
    if(!family)return;
    for(const auto& source:io.Fonts->Sources)if(std::string(source.Name).find(family)!=std::string::npos)return;
    wchar_t directory[MAX_PATH]{};GetWindowsDirectoryW(directory,MAX_PATH);
    const auto path=std::filesystem::path(directory)/L"Fonts"/family;
    if(!std::filesystem::exists(path))return;
    ImFontConfig config{};config.MergeMode=true;config.DstFont=io.FontDefault;
    std::snprintf(config.Name,sizeof(config.Name),"%s",family);
    // ImGui 1.93 loads glyphs on demand, without a full CJK atlas.
    io.Fonts->AddFontFromFileTTF(path.string().c_str(),15.5f*ui_scale,&config);
}

struct ActiveCall {
    ActiveCall() { active_hooks.fetch_add(1, std::memory_order_acq_rel); }
    ~ActiveCall() { active_hooks.fetch_sub(1, std::memory_order_acq_rel); }
};
template <typename T> void Release(T*& object) {
    if (object) { object->Release(); object = nullptr; }
}
bool OwnsForeground() {
    const HWND hwnd = input_window.load(std::memory_order_acquire);
    return hwnd && GetForegroundWindow() == hwnd;
}
bool CanInteract() {
    if (!OwnsForeground()) return false;
    const auto world = jd::ReadTelemetry();
    return world.driving && world.placement_valid;
}
void SetInteractive(bool enabled, bool restore_clip = true) {
    // ui_mutex is held by every caller, including WndProc and Stop.
    if (enabled == interactive.load()) return;
    jd::Log("overlay: mouse capture %s",enabled?"enabled":"disabled");
    interactive.store(enabled);
    input::SetEnabled(enabled);
    if (enabled) {
        previous_clip = {};
        clip_saved = GetClipCursor(&previous_clip) != FALSE;
        if (real_clip) real_clip(nullptr); else ClipCursor(nullptr);
        if (real_cursor) real_cursor(nullptr);
    } else {
        if (real_clip) {
            if (restore_clip && clip_saved && OwnsForeground()) real_clip(&previous_clip);
            else real_clip(nullptr);
        }
        clip_saved = false;
        if (real_cursor) real_cursor(LoadCursorW(nullptr, IDC_ARROW));
    }
    if (imgui_context) {
        ImGui::SetCurrentContext(imgui_context);
        auto& io = ImGui::GetIO();
        io.MouseDrawCursor = enabled;
        io.ClearInputMouse();
        io.ClearInputKeys();
    }
}
bool IsCapturedMessage(UINT message) {
    switch (message) {
    case WM_INPUT: case WM_INPUT_DEVICE_CHANGE:
    case WM_MOUSEMOVE: case WM_MOUSELEAVE: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
    case WM_CHAR: case WM_SYSCHAR: case WM_UNICHAR:
        return true;
    default: return false;
    }
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                            UINT_PTR, DWORD_PTR) {
    ActiveCall active;
    // Marshal removal onto the owning HWND thread without touching ImGui or its lock.
    if (detach_message && message == detach_message && wparam == subclass_id)
        return RemoveWindowSubclass(hwnd, WindowProc, subclass_id) ? 1 : 0;
    std::lock_guard<std::recursive_mutex> lock(ui_mutex);
    if (!stopping.load() && hwnd == game_window) {
        // Menus and the world map own all their mouse input, even before the
        // next Present has had a chance to release an active overlay capture.
        if (!CanInteract()) {
            SetInteractive(false, false);
            right_button_consumed = false;
            return DefSubclassProc(hwnd, message, wparam, lparam);
        }
        if (message == WM_RBUTTONDOWN) {
            right_button_consumed = true;
            SetInteractive(!interactive.load());
            return 0;
        }
        if (message == WM_RBUTTONUP && right_button_consumed) {
            right_button_consumed = false;
            return 0;
        }
        if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wparam)) {
            SetInteractive(false, false);
            right_button_consumed = false;
        }
        if (interactive.load() && message == WM_KEYDOWN && wparam == VK_ESCAPE) {
            panel_open = false;
            SetInteractive(false);
            return 0;
        }
        if (!interactive.load() && input::ConsumeMessage(message, wparam)) return 0;
        if (imgui_context && interactive.load()) {
            input::UiScope ui_input;
            ImGui::SetCurrentContext(imgui_context);
            ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);
            if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
                if (real_cursor) real_cursor(nullptr);
                return TRUE;
            }
            if (IsCapturedMessage(message)) {
                // WM_INPUT needs DefWindowProc for documented raw-input cleanup.
                return message == WM_INPUT ? DefWindowProcW(hwnd, message, wparam, lparam) : 0;
            }
        }
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, WindowProc, subclass_id);
        if (hwnd == game_window) {
            subclass_attached = false;
            input_window.store(nullptr, std::memory_order_release);
        }
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}
BOOL WINAPI HookClip(const RECT* rect) {
    ActiveCall active;
    if (interactive.load() && CanInteract() && !stopping.load()) return TRUE;
    return real_clip(rect);
}
BOOL WINAPI HookWarp(int x, int y) {
    ActiveCall active;
    if (interactive.load() && CanInteract() && !stopping.load()) return TRUE;
    return real_warp(x, y);
}
HCURSOR WINAPI HookCursor(HCURSOR cursor) {
    ActiveCall active;
    if (interactive.load() && CanInteract() && !stopping.load()) return real_cursor(nullptr);
    return real_cursor(cursor);
}
std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                        nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), count, nullptr, nullptr);
    return result;
}
void ConfigureStyle() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.WindowPadding = ImVec2(12, 10);
    style.FramePadding = ImVec2(7, 4);
    style.ItemSpacing = ImVec2(8, 5);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.073f, 0.10f, 0.97f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.12f, 0.22f, 0.32f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.34f, 0.47f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.19f, 0.43f, 0.58f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.33f, 0.76f, 0.88f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.14f, 0.30f, 0.39f, 1.0f);
    style.ScaleAllSizes(ui_scale);
}
bool CreateTarget(IDXGISwapChain* chain) {
    ID3D11Texture2D* buffer = nullptr;
    const HRESULT hr = chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer));
    if (FAILED(hr)) return false;
    const HRESULT target_hr = device->CreateRenderTargetView(buffer, nullptr, &target_view);
    buffer->Release();
    return SUCCEEDED(target_hr);
}
void PinSelfForResidualSubclass() {
    HMODULE module = nullptr;
    const BOOL pinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                           reinterpret_cast<LPCWSTR>(WindowProc), &module);
    jd::Log("overlay: subclass removal incomplete; retain module until process exit: %d", int(pinned));
}
void RemoveSubclass(HWND hwnd) {
    if (!hwnd || !subclass_attached.exchange(false)) return;
    bool removed = !IsWindow(hwnd);
    if (!removed && GetWindowThreadProcessId(hwnd, nullptr) == GetCurrentThreadId()) {
        removed = RemoveWindowSubclass(hwnd, WindowProc, subclass_id) != FALSE;
    } else if (!removed && detach_message) {
        DWORD_PTR result = 0;
        removed = SendMessageTimeoutW(hwnd, detach_message, subclass_id, 0,
                                      SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result) && result;
    }
    if (!removed) PinSelfForResidualSubclass();
    // An unsuccessful removal has a pinned, pass-through callback until HWND destruction.
}
void DestroyUi() {
    SetInteractive(false, false);
    jd::preview::ReleaseGpu();
    RemoveSubclass(game_window);
    if (imgui_context) {
        ImGui::SetCurrentContext(imgui_context);
        if (dx11_initialized) ImGui_ImplDX11_Shutdown();
        if (win32_initialized) ImGui_ImplWin32_Shutdown();
        dx11_initialized = win32_initialized = false;
        ImGui::DestroyContext(imgui_context);
        imgui_context = nullptr;
    }
    Release(target_view);
    Release(context);
    Release(device);
    Release(game_chain);
    game_window = nullptr;
    input_window.store(nullptr, std::memory_order_release);
    panel_open = false;
    right_button_consumed = false;
}
bool AttachUi(IDXGISwapChain* chain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(chain->GetDesc(&desc)) || !desc.OutputWindow || !IsWindowVisible(desc.OutputWindow)) return false;
    DWORD process = 0;
    const DWORD window_thread = GetWindowThreadProcessId(desc.OutputWindow, &process);
    if (process != GetCurrentProcessId() || GetAncestor(desc.OutputWindow, GA_ROOT) != desc.OutputWindow) return false;
    if (window_thread != GetCurrentThreadId()) {
        if (!renderer_thread_warning) {
            jd::Log("overlay: HWND thread=%lu differs from Present thread=%lu; comctl subclass attachment deferred",
                     window_thread, GetCurrentThreadId());
            renderer_thread_warning = true;
        }
        return false;
    }
    if (FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) return false;
    device->GetImmediateContext(&context);
    game_window = desc.OutputWindow;
    input_window.store(game_window, std::memory_order_release);
    game_chain = chain;
    game_chain->AddRef();
    if (!CreateTarget(chain)) { DestroyUi(); return false; }
    IMGUI_CHECKVERSION();
    imgui_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_context);
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    settings_path = Utf8(jd::DataDirectory() + L"\\overlay.ini");
    io.IniFilename = settings_path.c_str();
    io.LogFilename = nullptr;
    ui_scale = std::clamp(ImGui_ImplWin32_GetDpiScaleForHwnd(game_window), 1.0f, 2.0f);
    wchar_t windows_path[MAX_PATH]{};
    GetWindowsDirectoryW(windows_path, MAX_PATH);
    const std::string font_path = Utf8(std::wstring(windows_path) + L"\\Fonts\\segoeui.ttf");
    ImFontConfig font_config{};
    font_config.OversampleH = 2;
    ImFont* ui_font = nullptr;
    if (GetFileAttributesA(font_path.c_str()) != INVALID_FILE_ATTRIBUTES)
        ui_font = io.Fonts->AddFontFromFileTTF(font_path.c_str(), 15.5f * ui_scale, &font_config);
    if (!ui_font) ui_font = io.Fonts->AddFontDefault();
    io.FontDefault=ui_font;
    jd::Log("overlay: font=%s, dynamic glyphs",ui_font?ui_font->GetDebugName():"none");
    ConfigureStyle();
    if (!ImGui_ImplWin32_Init(game_window)) { DestroyUi(); return false; }
    win32_initialized = true;
    if (!ImGui_ImplDX11_Init(device, context)) { DestroyUi(); return false; }
    dx11_initialized = true;
    if (!SetWindowSubclass(game_window, WindowProc, subclass_id, 0)) {
        jd::Log("overlay: SetWindowSubclass failed (%lu)", GetLastError()); DestroyUi(); return false;
    }
    subclass_attached = true;
    jd::Log("overlay: attached DX11 swapchain, HWND=%p, scale=%.2f", game_window, ui_scale);
    return true;
}
const char* SelectedLabel(const std::vector<Choice>& choices, std::uint64_t id, const char* auto_label) {
    if (!id && auto_label) return auto_label;
    for (const auto& item : choices) if (item.id == id) return item.label.c_str();
    return "—";
}
void ChoiceCombo(const char* id, const char* label, const std::vector<Choice>& choices,
                 std::uint64_t& selected, char* search, std::size_t search_size,
                 const char* auto_label = nullptr) {
    ImGui::PushID(id);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(102.0f*ui_scale);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##choice", SelectedLabel(choices, selected, auto_label), ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##filter",T("search").c_str(),search,search_size);
        ImGui::Separator();
        if (auto_label) {
            if (ImGui::Selectable(auto_label, selected == 0)) selected = 0;
            ImGui::Separator();
        }
        unsigned shown = 0;
        for (const auto& item : choices) {
            if (!model::Matches(item, search)) continue;
            ++shown;
            ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(item.id)));
            if (ImGui::Selectable(item.label.c_str(), selected == item.id)) {
                selected = item.id;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        if (!shown) ImGui::TextDisabled("—");
        ImGui::EndCombo();
    }
    ImGui::PopID();
}
void DrawPanel() {
    static char cargo_search[128]{}, source_search[128]{}, destination_search[128]{}, trailer_search[128]{};
    static float random_mass=50,random_distance=50;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(760.0f * ui_scale, display.x - 40.0f),
                                   std::min(405.0f * ui_scale, display.y - 40.0f)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    const ImGuiWindowFlags flags = interactive.load() ? 0 : ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin((T("jd_title")+"###JobDispatcherSP_v4").c_str(), &panel_open, flags)) {
        if(!snapshot.message.empty()){
            ImGui::PushTextWrapPos();ImGui::TextWrapped("%s",labels::Resolve(snapshot.message).c_str());ImGui::PopTextWrapPos();
            ImGui::Separator();
        }
        ImGui::BeginTable("content", 2, ImGuiTableFlags_SizingStretchProp);
        ImGui::TableSetupColumn("selection", ImGuiTableColumnFlags_WidthStretch, .57f);
        ImGui::TableSetupColumn("preview", ImGuiTableColumnFlags_WidthStretch, .43f);
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!snapshot.world_ready || snapshot.busy);
        ChoiceCombo("cargo",T("cb_cargo").c_str(),snapshot.cargos,request.cargo,cargo_search,sizeof(cargo_search));
        const Choice* chosen_cargo=nullptr;
        for(const auto& cargo:snapshot.cargos)if(cargo.id==request.cargo){chosen_cargo=&cargo;break;}
        const bool trailer_delivery=chosen_cargo&&chosen_cargo->trailer_delivery;
        if(trailer_delivery)request.trailer=0;
        ImGui::BeginDisabled(trailer_delivery);
        ChoiceCombo("trailer",T("cb_trailer").c_str(),snapshot.trailers,request.trailer,trailer_search,sizeof(trailer_search),T("jd_auto").c_str());
        ImGui::EndDisabled();
            ChoiceCombo("source",T("source_company").c_str(),snapshot.companies,request.source,source_search,sizeof(source_search));
            if (model::Contains(snapshot.companies, snapshot.nearest_company)) {
                if(ImGui::SmallButton(T("jd_nearby").c_str()))request.source=snapshot.nearest_company;
            }
            ChoiceCombo("destination",T("target_company").c_str(),snapshot.companies,request.destination,
                        destination_search, sizeof(destination_search));
        request.owned_trailer = false;
        ImGui::Checkbox(T("jd_immediate").c_str(),&request.take_immediately);
        ImGui::Spacing();
        ImGui::TextUnformatted(T("cargo_mass").c_str());ImGui::SameLine(102*ui_scale);ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##random-mass",&random_mass,0,100,"%.0f%%");
        ImGui::TextUnformatted(T("route_length").c_str());ImGui::SameLine(102*ui_scale);ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##random-distance",&random_distance,0,100,"%.0f%%");
        if(ImGui::Button(T("jd_surprise").c_str(),ImVec2(-1,0))){jd::Surprise(random_mass,random_distance);next_snapshot={};}
        ImGui::EndDisabled();
        ImGui::TableNextColumn();
        const auto preview_width = ImGui::GetContentRegionAvail().x;
        jd::preview::DrawNative(device, request, preview_width, preview_width*.625f, interactive.load());
        const bool matching_cargo=snapshot.selection.cargo==request.cargo&&snapshot.selection.trailer==request.trailer;
        const bool matching_route=snapshot.selection.source==request.source&&snapshot.selection.destination==request.destination;
        const bool comma=units::DecimalComma(snapshot.language);
        std::string mass="—",distance="—";
        if(matching_cargo&&snapshot.mass_kg>0){
            const auto unit=snapshot.mass_unit.empty()?"kg":snapshot.mass_unit;
            const auto label=T(unit=="t"?"unit_t":unit.c_str());
            mass=units::Number(units::Mass(snapshot.mass_kg,unit),comma,unit=="kg"||unit=="lb"?0:1)+" "+(label.find("@@")==std::string::npos?label:unit);
        }
        if(matching_route&&snapshot.distance_m>0)distance=units::Number(units::Distance(snapshot.distance_m,snapshot.miles),comma)+" "+T(snapshot.miles?"mi":"km");
        else if(matching_route&&snapshot.route_pending)distance=T("calculating_route");
        ImGui::Text("%s: %s",T("cargo_mass").c_str(),mass.c_str());
        ImGui::TextWrapped("%s: %s",T("route_length").c_str(),distance.c_str());
        std::string price="—";
        if(matching_cargo&&matching_route&&snapshot.price_valid){
            const auto value=units::Number(snapshot.price_base*snapshot.currency_rate,comma);
            const auto suffix=snapshot.currency_suffix.empty()?snapshot.currency_code:snapshot.currency_suffix;
            price=snapshot.currency_prefix+value+" "+suffix;
        }
        ImGui::TextWrapped("%s: %s",T("job_income").c_str(),price.c_str());
        if(matching_route&&snapshot.real_minutes>=0&&snapshot.distance_m>0){
            ImGui::TextWrapped("%s ~ %d %s %d %s",T("jd_real_time").c_str(),snapshot.real_minutes/60,T("hours_short").c_str(),snapshot.real_minutes%60,T("minutes_short").c_str());
        }
        ImGui::EndTable();
        ImGui::Spacing();
        ImGui::BeginDisabled(!model::CanSubmit(snapshot,request)||!matching_route||snapshot.distance_m<=0||snapshot.route_pending||snapshot.route_failed);
        if(ImGui::Button(T(snapshot.busy?"loading":"jd_create").c_str(),ImVec2(180.0f*ui_scale,0))) {
            jd::Submit(request);
            next_snapshot = {};
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(snapshot.busy);
        if(ImGui::Button(T("refresh_jobs").c_str())){jd::RequestRefresh();next_snapshot={};}
        ImGui::EndDisabled();
    }
    ImGui::End();
    static Request sent;
    if(sent.cargo!=request.cargo||sent.trailer!=request.trailer||sent.source!=request.source||sent.destination!=request.destination){sent=request;jd::Select(request);}
    if (!panel_open) SetInteractive(false);
}
void DrawIcon() {
    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(18.0f * ui_scale, io.DisplaySize.y - 18.0f * ui_scale),
                            ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowBgAlpha(0.86f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;
    if (!interactive.load()) flags |= ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3, 3));
    if (ImGui::Begin("##JobDispatcherIcon", nullptr, flags)) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float s = ui_scale;
        if (ImGui::InvisibleButton("##dispatcher-icon", ImVec2(36*s,36*s))) {
            panel_open = !panel_open;
            if (!panel_open) SetInteractive(false);
        }
        auto* draw = ImGui::GetWindowDrawList();
        const auto point = [&](float x,float y) { return ImVec2(origin.x+x*s,origin.y+y*s); };
        const ImU32 ink = IM_COL32(220,234,245,255);
        draw->AddRectFilled(origin,point(36,36),ImGui::IsItemHovered()?IM_COL32(38,88,120,255):IM_COL32(24,48,68,255),7*s);
        draw->AddRect(point(7,8),point(27,23),ink,2*s,0,1.6f*s);
        draw->AddLine(point(14,8),point(14,23),ink,1.3f*s);
        draw->AddCircleFilled(point(11,25),2*s,ink);
        draw->AddCircleFilled(point(24,25),2*s,ink);
        draw->AddLine(point(8,31),point(28,31),ink,1.6f*s);
        draw->AddLine(point(24,28),point(28,31),ink,1.6f*s);
        draw->AddLine(point(24,34),point(28,31),ink,1.6f*s);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
void Render() {
    input::UiScope ui_input;
    if (!imgui_context || !target_view) return;
    ImGui::SetCurrentContext(imgui_context);
    if (!OwnsForeground() && interactive.load()) SetInteractive(false, false);
    const auto now = std::chrono::steady_clock::now();
    const auto test_open=test_panel.exchange(-1);
    if(test_open>=0){panel_open=test_open!=0;next_snapshot={};SetInteractive(panel_open&&OwnsForeground());}
    if (panel_open && now >= next_snapshot) {
        const auto previous_random=snapshot.random_revision;
        jd::UpdateSnapshot(snapshot);
        if(snapshot.random_revision!=previous_random){request.cargo=snapshot.selection.cargo;request.trailer=snapshot.selection.trailer;request.source=snapshot.selection.source;request.destination=snapshot.selection.destination;}
        model::Normalize(snapshot, request);
        next_snapshot = now + std::chrono::milliseconds(400);
    }
    auto& io = ImGui::GetIO();
    LocaleFonts();
    io.MouseDrawCursor = interactive.load() && OwnsForeground();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawIcon();
    if (panel_open) DrawPanel();
    ImGui::Render();
    // Backend saves its render state, but OM targets must be saved around our own binding.
    ID3D11RenderTargetView* old_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* old_depth = nullptr;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old_targets, &old_depth);
    context->OMSetRenderTargets(1, &target_view, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old_targets, old_depth);
    for (auto*& item : old_targets) Release(item);
    Release(old_depth);
}
HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* chain, UINT interval, UINT flags) {
    ActiveCall active;
    if (!stopping.load() && !(flags & DXGI_PRESENT_TEST)) {
        jd::perf::Scope frame_performance(jd::perf::overlay);
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        if (!stopping.load()) {
            if (game_chain && chain != game_chain) {
                DXGI_SWAP_CHAIN_DESC candidate{};
                if (!IsWindow(game_window) ||
                    (SUCCEEDED(chain->GetDesc(&candidate)) && candidate.OutputWindow == game_window))
                    DestroyUi();
            }
            const auto world = jd::ReadTelemetry();
            if (!world.driving || !world.placement_valid) {
                jd::preview::Suspend();
                if (interactive.load()) SetInteractive(false, false);
            } else {
            if(!panel_open)jd::preview::Suspend();
            if (!game_chain) AttachUi(chain);
            if (chain == game_chain) {
                if (!target_view) CreateTarget(chain);
                Render();
            }
            }
        }
    }
    const HRESULT hr = real_present(chain, interval, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        if (chain == game_chain) DestroyUi();
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE HookResize(IDXGISwapChain* chain, UINT count, UINT width, UINT height,
                                      DXGI_FORMAT format, UINT flags) {
    ActiveCall active;
    std::lock_guard<std::recursive_mutex> lock(ui_mutex);
    if (chain == game_chain) {
        if (imgui_context) { ImGui::SetCurrentContext(imgui_context); ImGui_ImplDX11_InvalidateDeviceObjects(); }
        Release(target_view);
    }
    const HRESULT hr = real_resize(chain, count, width, height, format, flags);
    if (SUCCEEDED(hr) && chain == game_chain && !stopping.load()) {
        CreateTarget(chain);
        if (imgui_context) ImGui_ImplDX11_CreateDeviceObjects();
    }
    return hr;
}
bool AddHook(void* target, void* callback, void** original) {
    const auto status = MH_CreateHook(target, callback, original);
    if (status != MH_OK) { jd::Log("overlay: MH_CreateHook failed: %s", MH_StatusToString(status)); return false; }
    hook_targets.push_back(target);
    return true;
}
void RemoveHooks() {
    for (void* target : hook_targets) MH_RemoveHook(target);
    hook_targets.clear();
}
} // namespace

bool Start() {
    if (running.exchange(true)) return true;
    stopping.store(false);
    detach_message = RegisterWindowMessageW(L"JobDispatcherSP.DetachSubclass.8e58fc6d-4f34-40df-8d36-33033d223eae");
    const auto status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        jd::Log("overlay: MinHook init failed: %s", MH_StatusToString(status)); running = false; return false;
    }
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* class_name = L"JobDispatcherSP_DX11_Probe";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = class_name;
    const ATOM registered = RegisterClassExW(&wc);
    HWND probe = CreateWindowExW(0, class_name, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                                 nullptr, nullptr, instance, nullptr);
    if (!probe) {
        jd::Log("overlay: DX11 probe window failed: %lu", GetLastError());
        if (registered) UnregisterClassW(class_name, instance);
        running = false;
        return false;
    }
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = probe;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* chain = nullptr;
    ID3D11Device* probe_device = nullptr;
    ID3D11DeviceContext* probe_context = nullptr;
    D3D_FEATURE_LEVEL level{};
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                                    nullptr, 0, D3D11_SDK_VERSION, &desc, &chain,
                                                    &probe_device, &level, &probe_context);
    bool result = false;
    if (SUCCEEDED(hr)) {
        void** table = *reinterpret_cast<void***>(chain);
        result = AddHook(table[8], reinterpret_cast<void*>(HookPresent), reinterpret_cast<void**>(&real_present)) &&
                 AddHook(table[13], reinterpret_cast<void*>(HookResize), reinterpret_cast<void**>(&real_resize)) &&
                 AddHook(reinterpret_cast<void*>(ClipCursor), reinterpret_cast<void*>(HookClip), reinterpret_cast<void**>(&real_clip)) &&
                 AddHook(reinterpret_cast<void*>(SetCursorPos), reinterpret_cast<void*>(HookWarp), reinterpret_cast<void**>(&real_warp)) &&
                 AddHook(reinterpret_cast<void*>(SetCursor), reinterpret_cast<void*>(HookCursor), reinterpret_cast<void**>(&real_cursor)) &&
                 input::Prepare(AddHook, CanInteract,
                    [] { active_hooks.fetch_add(1, std::memory_order_acq_rel); },
                    [] { active_hooks.fetch_sub(1, std::memory_order_acq_rel); });
    } else jd::Log("overlay: DX11 hardware probe failed: 0x%08lx", static_cast<unsigned long>(hr));
    Release(probe_context);
    Release(probe_device);
    Release(chain);
    DestroyWindow(probe);
    if (registered) UnregisterClassW(class_name, instance);
    if (result) {
        for (void* target : hook_targets) {
            const auto enable_status = MH_EnableHook(target);
            if (enable_status != MH_OK) {
                jd::Log("overlay: enabling hook failed: %s", MH_StatusToString(enable_status)); result = false; break;
            }
        }
    }
    if (!result) {
        Stop();
        return false;
    }
    jd::Log("overlay: DX11 hooks ready; waiting for game Present");
    return true;
}
void Stop() {
    if (!running.exchange(false)) return;
    stopping.store(true);
    for (void* target : hook_targets) MH_DisableHook(target);
    // Restore the subclass before waiting, so new game messages bypass this module.
    HWND window_to_detach = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        SetInteractive(false, false);
        window_to_detach = game_window;
    }
    RemoveSubclass(window_to_detach);
    while (active_hooks.load(std::memory_order_acquire)) Sleep(1);
    {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        DestroyUi();
    }
    RemoveHooks();
    input::Reset();
    // Do not MH_Uninitialize: another plugin may share this instance's hook registry.
    jd::Log("overlay: stopped");
}
Diagnostics Inspect() {
    std::lock_guard<std::recursive_mutex> lock(ui_mutex);
    const bool attached=imgui_context!=nullptr&&game_chain!=nullptr;
    const auto world=jd::ReadTelemetry();
    return {running.load(),attached,interactive.load(),panel_open,attached&&world.driving&&world.placement_valid};
}
void QueueTestPanel(bool open){test_panel.store(open?1:0);}
} // namespace jd::overlay
