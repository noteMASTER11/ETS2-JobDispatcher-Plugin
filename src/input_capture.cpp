#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include "input_capture.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace jd::overlay::input {
namespace {
using KeyFn = SHORT (WINAPI*)(int);
using CursorFn = BOOL (WINAPI*)(LPPOINT);
using KeyboardFn = BOOL (WINAPI*)(PBYTE);
using StateFn = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPVOID);
using DataFn = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
KeyFn real_async = GetAsyncKeyState, real_key = GetKeyState;
CursorFn real_position = GetCursorPos;
KeyboardFn real_keyboard = GetKeyboardState;
struct StateSlot { void* target = nullptr; StateFn original = nullptr; };
struct DataSlot { void* target = nullptr; DataFn original = nullptr; };
std::array<StateSlot, 8> state_slots{};
std::array<DataSlot, 8> data_slots{};
HMODULE direct_input = nullptr;
bool (*foreground_fn)() = nullptr;
void (*enter_fn)() = nullptr;
void (*leave_fn)() = nullptr;
std::atomic<bool> enabled{false};
std::atomic<unsigned> resume_epoch{0};
thread_local unsigned ui_depth = 0;
std::mutex gate_mutex;
std::array<bool, 256> held_on_exit{};
std::atomic<unsigned> held_count{0};
std::unordered_map<void*, std::array<unsigned, 2>> drained_epochs;
POINT frozen_cursor{};
bool frozen_valid = false;

struct Call {
    Call() { if (enter_fn) enter_fn(); }
    ~Call() { if (leave_fn) leave_fn(); }
};
bool GameForeground() { return foreground_fn && foreground_fn(); }
bool Capture() { return !ui_depth && enabled.load(std::memory_order_acquire) && GameForeground(); }
bool Suppressed(int key) {
    if (ui_depth || !held_count.load(std::memory_order_relaxed) || key < 0 || key >= 256 || !GameForeground()) return false;
    std::lock_guard<std::mutex> lock(gate_mutex);
    if (!held_on_exit[key]) return false;
    if (!(real_async(key) & 0x8000)) { held_on_exit[key] = false; held_count.fetch_sub(1, std::memory_order_relaxed); }
    return held_on_exit[key];
}
bool DrainOnce(void* device, unsigned channel) {
    const auto epoch = resume_epoch.load(std::memory_order_acquire);
    if (!epoch) return false;
    std::lock_guard<std::mutex> lock(gate_mutex);
    if (drained_epochs.size() > 256) drained_epochs.clear();
    auto& previous = drained_epochs[device][channel];
    if (previous == epoch) return false;
    previous = epoch;
    return true;
}
SHORT WINAPI HookAsync(int key) {
    Call call;
    if (Capture() || Suppressed(key)) return 0;
    return real_async(key);
}
SHORT WINAPI HookKey(int key) {
    Call call;
    if (Capture() || Suppressed(key)) return 0;
    return real_key(key);
}
BOOL WINAPI HookKeyboard(PBYTE keys) {
    Call call;
    const BOOL result = real_keyboard(keys);
    if (!result || !keys || ui_depth) return result;
    if (Capture()) std::memset(keys, 0, 256);
    else if (held_count.load(std::memory_order_relaxed))
        for (int key = 0; key < 256; ++key) if (Suppressed(key)) keys[key] = 0;
    return result;
}
BOOL WINAPI HookPosition(LPPOINT point) {
    Call call;
    if (point && Capture()) {
        std::lock_guard<std::mutex> lock(gate_mutex);
        if (frozen_valid) { *point = frozen_cursor; return TRUE; }
    }
    return real_position(point);
}
unsigned Kind(IDirectInputDevice8W* device) {
    DIDEVCAPS caps{};
    caps.dwSize = sizeof(caps);
    if (FAILED(device->GetCapabilities(&caps))) return 0;
    const auto type = caps.dwDevType & 0xff;
    // Windows SDK hides the old DIDEVTYPE_* names when building for DI8.
    // Their ABI values still occur in legacy device implementations.
    constexpr DWORD legacy_mouse = 2, legacy_keyboard = 3;
    if (type == DI8DEVTYPE_MOUSE || type == legacy_mouse) return 1;
    if (type == DI8DEVTYPE_KEYBOARD || type == legacy_keyboard) return 2;
    return 0;
}
int ScanKey(unsigned scan) {
    if (scan & 0x80) scan = (scan & 0x7f) | 0xe000;
    return static_cast<int>(MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX));
}
template<unsigned Slot> HRESULT STDMETHODCALLTYPE HookState(IDirectInputDevice8W* device, DWORD size, LPVOID data) {
    Call call;
    const auto result = state_slots[Slot].original(device, size, data);
    if (FAILED(result) || !data || ui_depth || !GameForeground()) return result;
    const bool captured = Capture();
    const bool drain = !captured && DrainOnce(device, 0);
    if (!captured && !drain && !held_count.load(std::memory_order_relaxed)) return result;
    const auto kind = Kind(device);
    if (!kind) return result; // Steering wheels/controllers keep their own state.
    if (captured || drain) {
        std::memset(data, 0, size);
        return result;
    }
    if (kind == 2 && size == 256) {
        auto* keys = static_cast<BYTE*>(data);
        for (unsigned scan = 0; scan < 256; ++scan) if (Suppressed(ScanKey(scan))) keys[scan] = 0;
    } else if (kind == 1 && size >= sizeof(DIMOUSESTATE)) {
        auto* mouse_buttons = static_cast<BYTE*>(data) + offsetof(DIMOUSESTATE, rgbButtons);
        const int buttons[]{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
        const unsigned count = size >= sizeof(DIMOUSESTATE2) ? 5 : 4;
        for (unsigned index = 0; index < count; ++index) if (Suppressed(buttons[index])) mouse_buttons[index] = 0;
    }
    return result;
}
template<unsigned Slot> HRESULT STDMETHODCALLTYPE HookData(IDirectInputDevice8W* device, DWORD stride,
        LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags) {
    Call call;
    if (ui_depth || !GameForeground())
        return data_slots[Slot].original(device, stride, data, count, flags);
    const bool captured = !ui_depth && Capture();
    const bool drain = !ui_depth && !captured && DrainOnce(device, 1);
    if (!captured && !drain && !held_count.load(std::memory_order_relaxed))
        return data_slots[Slot].original(device, stride, data, count, flags);
    const auto kind = Kind(device);
    // Consume queued events while captured even if the game requested PEEK.
    const auto result = data_slots[Slot].original(device, stride, data, count,
                                                 captured || drain ? flags & ~DIGDD_PEEK : flags);
    if (FAILED(result) || !count || !kind) return result;
    if (captured || drain) { *count = 0; return result; }
    if (!data || stride < 16 || *count == INFINITE || *count > 65536) return result;
    auto* bytes = reinterpret_cast<BYTE*>(data);
    DWORD kept = 0;
    for (DWORD index = 0; index < *count; ++index) {
        DWORD offset = 0;
        std::memcpy(&offset, bytes + index * stride, sizeof(offset));
        int key = 0;
        if (kind == 2 && offset < 256) key = ScanKey(offset);
        else if (kind == 1 && offset >= DIMOFS_BUTTON0 && offset <= DIMOFS_BUTTON4) {
            const int buttons[]{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
            key = buttons[offset - DIMOFS_BUTTON0];
        }
        if (key && Suppressed(key)) continue;
        if (kept != index) std::memmove(bytes + kept * stride, bytes + index * stride, stride);
        ++kept;
    }
    *count = kept;
    return result;
}
const StateFn state_callbacks[]{HookState<0>, HookState<1>, HookState<2>, HookState<3>, HookState<4>, HookState<5>, HookState<6>, HookState<7>};
const DataFn data_callbacks[]{HookData<0>, HookData<1>, HookData<2>, HookData<3>, HookData<4>, HookData<5>, HookData<6>, HookData<7>};
bool AddDevice(AddHook add, void* device) {
    auto** table = *reinterpret_cast<void***>(device);
    bool state_found = false, data_found = false;
    for (const auto& slot : state_slots) state_found |= slot.target == table[9];
    for (const auto& slot : data_slots) data_found |= slot.target == table[10];
    if (!state_found) {
        for (unsigned index = 0; index < state_slots.size(); ++index) if (!state_slots[index].target) {
            auto& slot = state_slots[index];
            slot.target = table[9];
            state_found = add(slot.target, reinterpret_cast<void*>(state_callbacks[index]), reinterpret_cast<void**>(&slot.original));
            break;
        }
    }
    if (!data_found) {
        for (unsigned index = 0; index < data_slots.size(); ++index) if (!data_slots[index].target) {
            auto& slot = data_slots[index];
            slot.target = table[10];
            data_found = add(slot.target, reinterpret_cast<void*>(data_callbacks[index]), reinterpret_cast<void**>(&slot.original));
            break;
        }
    }
    return state_found && data_found;
}
}
UiScope::UiScope() { ++ui_depth; }
UiScope::~UiScope() { --ui_depth; }
void SetEnabled(bool value) {
    if (enabled.load() == value) return;
    if (value) {
        POINT cursor{};
        const bool valid = real_position(&cursor) != FALSE;
        std::lock_guard<std::mutex> lock(gate_mutex);
        frozen_cursor = cursor;
        frozen_valid = valid;
        held_on_exit.fill(false);
        held_count = 0;
    } else {
        std::array<bool, 256> held{};
        for (int key = 0; key < 256; ++key) held[key] = (real_async(key) & 0x8000) != 0;
        std::lock_guard<std::mutex> lock(gate_mutex);
        held_on_exit = held;
        unsigned count = 0; for (bool down : held) count += down;
        held_count.store(count, std::memory_order_relaxed);
        resume_epoch.fetch_add(1, std::memory_order_release);
    }
    enabled.store(value, std::memory_order_release);
}
bool ConsumeMessage(unsigned message, std::uintptr_t wparam) {
    switch (message) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        return Suppressed(static_cast<int>(wparam));
    case WM_CHAR: case WM_SYSCHAR: {
        const auto key = VkKeyScanW(static_cast<WCHAR>(wparam));
        return key != -1 && Suppressed(key & 0xff);
    }
    default: return false;
    }
}
bool Prepare(AddHook add, bool (*foreground)(), void (*enter)(), void (*leave)()) {
    foreground_fn = foreground; enter_fn = enter; leave_fn = leave;
    if (!add(reinterpret_cast<void*>(GetAsyncKeyState), reinterpret_cast<void*>(HookAsync), reinterpret_cast<void**>(&real_async)) ||
        !add(reinterpret_cast<void*>(GetKeyState), reinterpret_cast<void*>(HookKey), reinterpret_cast<void**>(&real_key)) ||
        !add(reinterpret_cast<void*>(GetKeyboardState), reinterpret_cast<void*>(HookKeyboard), reinterpret_cast<void**>(&real_keyboard)) ||
        !add(reinterpret_cast<void*>(GetCursorPos), reinterpret_cast<void*>(HookPosition), reinterpret_cast<void**>(&real_position))) return false;
    direct_input = LoadLibraryW(L"dinput8.dll");
    if (!direct_input) return false;
    using Factory = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    const auto create = reinterpret_cast<Factory>(GetProcAddress(direct_input, "DirectInput8Create"));
    if (!create) return false;
    // A and W interfaces may expose different implementations; prepare both.
    for (bool wide : {false, true}) {
        void* raw = nullptr;
        if (FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                          wide ? IID_IDirectInput8W : IID_IDirectInput8A, &raw, nullptr))) return false;
        bool ok = true;
        if (wide) {
            auto* api = static_cast<IDirectInput8W*>(raw);
            for (const auto* guid : {&GUID_SysMouse, &GUID_SysKeyboard}) {
                IDirectInputDevice8W* device = nullptr;
                if (FAILED(api->CreateDevice(*guid, &device, nullptr))) { ok = false; break; }
                ok = AddDevice(add, device); device->Release(); if (!ok) break;
            }
            api->Release();
        } else {
            auto* api = static_cast<IDirectInput8A*>(raw);
            for (const auto* guid : {&GUID_SysMouse, &GUID_SysKeyboard}) {
                IDirectInputDevice8A* device = nullptr;
                if (FAILED(api->CreateDevice(*guid, &device, nullptr))) { ok = false; break; }
                ok = AddDevice(add, device); device->Release(); if (!ok) break;
            }
            api->Release();
        }
        if (!ok) return false;
    }
    return true;
}
void Reset() {
    enabled = false; resume_epoch = 0; held_count = 0;
    { std::lock_guard<std::mutex> lock(gate_mutex); held_on_exit.fill(false); drained_epochs.clear(); frozen_valid = false; }
    state_slots = {}; data_slots = {};
    real_async = GetAsyncKeyState; real_key = GetKeyState;
    real_position = GetCursorPos; real_keyboard = GetKeyboardState;
    foreground_fn = nullptr; enter_fn = leave_fn = nullptr;
    if (direct_input) { FreeLibrary(direct_input); direct_input = nullptr; }
}
}
