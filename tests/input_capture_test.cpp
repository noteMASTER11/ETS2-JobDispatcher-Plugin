// No game, DX11, visible window or synthetic input. Exercise the real filters
// with deterministic device/polling responses, then discover native DI vtables.
#include "../src/input_capture.cpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace jd::overlay::input;
namespace {
void Check(bool value, const char* detail) { if (!value) throw std::runtime_error(detail); }
std::array<SHORT, 256> physical{};
POINT actual_cursor{100, 200};
bool own_foreground = true;
int calls = 0;
DWORD last_data_flags = 0;
SHORT WINAPI FakeKey(int key) { return key >= 0 && key < 256 ? physical[key] : 0; }
BOOL WINAPI FakeCursor(LPPOINT point) { if (!point) return FALSE; *point = actual_cursor; return TRUE; }
BOOL WINAPI FakeKeyboard(PBYTE keys) {
    if (!keys) return FALSE;
    for (int key = 0; key < 256; ++key) keys[key] = physical[key] & 0x8000 ? 0x80 : 0;
    return TRUE;
}
struct FakeDevice { void** table; DWORD kind; };
HRESULT STDMETHODCALLTYPE FakeCaps(IDirectInputDevice8W* self, LPDIDEVCAPS caps) {
    caps->dwDevType = reinterpret_cast<FakeDevice*>(self)->kind;
    return DI_OK;
}
HRESULT STDMETHODCALLTYPE FakeState(IDirectInputDevice8W* self, DWORD size, LPVOID data) {
    std::memset(data, 0, size);
    if (reinterpret_cast<FakeDevice*>(self)->kind == DI8DEVTYPE_MOUSE) {
        auto* mouse = static_cast<DIMOUSESTATE2*>(data);
        mouse->lX = 17; mouse->lY = -12; mouse->lZ = 120;
        std::memset(mouse->rgbButtons, 0x80, 8);
    } else {
        std::memset(data, 0x80, size);
    }
    return DI_OK;
}
HRESULT STDMETHODCALLTYPE FakeData(IDirectInputDevice8W*, DWORD stride, LPDIDEVICEOBJECTDATA data,
                                   LPDWORD count, DWORD flags) {
    last_data_flags = flags;
    if (data && *count >= 2) {
        DIDEVICEOBJECTDATA events[2]{};
        events[0].dwOfs = DIK_W; events[0].dwData = 0x80;
        events[1].dwOfs = DIK_A; events[1].dwData = 0x80;
        std::memcpy(data, &events[0], stride);
        std::memcpy(reinterpret_cast<BYTE*>(data) + stride, &events[1], stride);
        *count = 2;
    }
    return DI_OK;
}
bool Foreground() { return own_foreground; }
void Enter() { ++calls; }
void Leave() { --calls; }
unsigned prepared_hooks = 0;
bool ProbeHook(void* target, void*, void** original) {
    Check(target != nullptr, "null hook target"); *original = target; ++prepared_hooks; return true;
}
}
int main() {
    try {
        foreground_fn = Foreground; enter_fn = Enter; leave_fn = Leave;
        real_async = real_key = FakeKey;
        real_position = FakeCursor; real_keyboard = FakeKeyboard;
        state_slots[0].original = FakeState;
        data_slots[0].original = FakeData;
        void* table[4]{}; table[3] = reinterpret_cast<void*>(FakeCaps);
        FakeDevice keyboard{table, DI8DEVTYPE_KEYBOARD};
        FakeDevice mouse{table, DI8DEVTYPE_MOUSE};
        FakeDevice wheel{table, DI8DEVTYPE_JOYSTICK};
        auto* kb = reinterpret_cast<IDirectInputDevice8W*>(&keyboard);
        auto* ms = reinterpret_cast<IDirectInputDevice8W*>(&mouse);
        physical['W'] = physical[VK_ESCAPE] = physical[VK_RBUTTON] = SHORT(0x8000);
        Check(HookAsync('W') != 0, "normal key polling blocked");
        SetEnabled(true);
        actual_cursor = {900, 800};
        POINT point{};
        Check(HookAsync('W') == 0 && HookKey('W') == 0, "game key polling leaked");
        Check(HookPosition(&point) && point.x == 100 && point.y == 200, "game cursor moved");
        BYTE keys[256]{};
        Check(HookKeyboard(keys) && keys['W'] == 0, "keyboard snapshot leaked");
        Check(SUCCEEDED(HookState<0>(kb, sizeof(keys), keys)) && keys[DIK_W] == 0, "DI keyboard leaked");
        DIMOUSESTATE2 state{};
        Check(SUCCEEDED(HookState<0>(ms, sizeof(state), &state)) && state.lX == 0 && !state.rgbButtons[1], "DI mouse leaked");
        {
            UiScope overlay;
            Check(HookAsync('W') != 0 && HookKey('W') != 0, "overlay keys were blocked");
            Check(HookPosition(&point) && point.x == 900, "overlay cursor was frozen");
            Check(SUCCEEDED(HookState<0>(ms, sizeof(state), &state)) && state.lX == 17, "overlay device bypass failed");
        }
        BYTE controller[80]{};
        Check(SUCCEEDED(HookState<0>(reinterpret_cast<IDirectInputDevice8W*>(&wheel), sizeof(controller), controller)) && controller[0] == 0x80, "controller changed");
        DIDEVICEOBJECTDATA events[8]{};
        DWORD count = 8;
        Check(SUCCEEDED(HookData<0>(kb, sizeof(events[0]), events, &count, DIGDD_PEEK)) && count == 0 && !(last_data_flags & DIGDD_PEEK), "buffered capture did not drain");
        own_foreground = false;
        Check(HookAsync('W') != 0 && HookPosition(&point) && point.x == 900, "input blocked outside game foreground");
        SetEnabled(false); // Entering a game menu while the overlay was active.
        Check(SUCCEEDED(HookState<0>(ms, sizeof(state), &state)) && state.lX == 17 && state.rgbButtons[1] == 0x80,
              "game menu mouse drag was filtered after leaving the driving view");
        count = 8; HookData<0>(kb, sizeof(events[0]), events, &count, DIGDD_PEEK);
        Check(count == 2 && (last_data_flags & DIGDD_PEEK), "game menu buffered input was drained");
        own_foreground = true;
        SetEnabled(true);
        SetEnabled(false);
        Check(HookAsync('W') == 0 && HookAsync(VK_ESCAPE) == 0 && HookAsync(VK_RBUTTON) == 0, "held exit keys leaked");
        Check(ConsumeMessage(WM_KEYDOWN, 'W') && ConsumeMessage(WM_KEYDOWN, VK_ESCAPE), "legacy repeat leaked after exit");
        Check(HookPosition(&point) && point.x == 900, "cursor did not resume");
        Check(SUCCEEDED(HookState<0>(ms, sizeof(state), &state)) && state.lX == 0, "first mouse delta was replayed");
        Check(SUCCEEDED(HookState<0>(ms, sizeof(state), &state)) && state.lX == 17 && state.rgbButtons[1] == 0, "mouse did not resume with held button suppressed");
        HookState<0>(kb, sizeof(keys), keys); // Drain the transition once.
        HookState<0>(kb, sizeof(keys), keys);
        Check(keys[DIK_W] == 0 && keys[DIK_A] == 0x80, "DI key release gate failed");
        count = 8; HookData<0>(kb, sizeof(events[0]), events, &count, 0);
        Check(count == 0, "buffered transition was replayed");
        count = 8; HookData<0>(kb, sizeof(events[0]), events, &count, 0);
        Check(count == 1 && events[0].dwOfs == DIK_A, "held key was not removed from buffer");
        physical.fill(0);
        Check(HookAsync('W') == 0 && HookAsync(VK_ESCAPE) == 0 && HookAsync(VK_RBUTTON) == 0, "released keys remain latched");
        Check(!ConsumeMessage(WM_KEYUP, 'W'), "release did not clear message gate");
        physical['W'] = SHORT(0x8000);
        Check(HookAsync('W') != 0, "new key press did not resume");
        Check(!ConsumeMessage(WM_KEYDOWN, 'W'), "new legacy press did not resume");
        Check(calls == 0, "unbalanced unload fence");
        Reset();
        // No hooks enabled: load the native factory and verify mouse/keyboard A/W
        // method discovery without acquiring devices or changing system input.
        Check(Prepare(ProbeHook, Foreground, Enter, Leave), "native DirectInput discovery failed");
        Check(prepared_hooks >= 6, "missing polling/device hooks");
        Reset();
        std::cout << "input capture: game polling blocked; overlay bypass, release, focus and DirectInput discovery passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
