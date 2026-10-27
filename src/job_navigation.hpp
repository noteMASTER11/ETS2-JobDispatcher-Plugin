#pragma once
#include <cstdint>
namespace jd {
// Observe actual job/attachment transitions; ordinary frames and UI selection
// must never overwrite a route the player has edited manually.
struct JobNavigation {
    std::uintptr_t job = 0;
    bool connected = false;
    bool initialized = false;
    void Reset() { *this = {}; }
    void WaitingForPickup(std::uintptr_t accepted_job) {
        job = accepted_job; connected = false; initialized = true;
    }
    bool Observe(std::uintptr_t current_job, bool trailer_connected) {
        const bool changed = initialized && current_job && current_job != job;
        const bool attached = current_job && trailer_connected && (!initialized || !connected);
        job = current_job; connected = trailer_connected; initialized = true;
        return changed || attached;
    }
};
}
