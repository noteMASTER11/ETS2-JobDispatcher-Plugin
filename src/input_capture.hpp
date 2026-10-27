#pragma once
#include <cstdint>

namespace jd::overlay::input {
using AddHook = bool (*)(void*, void*, void**);
// Prepare while hooks are disabled. Enter/leave share the overlay's unload fence.
bool Prepare(AddHook add, bool (*foreground)(), void (*enter)(), void (*leave)());
void SetEnabled(bool enabled);
bool ConsumeMessage(unsigned message, std::uintptr_t wparam);
void Reset(); // Only after disabling hooks and draining active callbacks.
struct UiScope {
    UiScope();
    ~UiScope();
    UiScope(const UiScope&) = delete;
    UiScope& operator=(const UiScope&) = delete;
};
}
