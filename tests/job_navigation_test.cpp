#include "job_navigation.hpp"
#include <cassert>
int main() {
    jd::JobNavigation state;
    assert(!state.Observe(0, false));
    state.WaitingForPickup(100);
    assert(!state.Observe(100, false));
    assert(state.Observe(100, true)); // Couple the prepared trailer: go to delivery.
    assert(!state.Observe(100, true)); // Do not reset manually edited GPS every frame.
    assert(!state.Observe(100, false));
    assert(state.Observe(100, true)); // Reattach after dropping the trailer.
    assert(!state.Observe(0, false));
    assert(state.Observe(200, false)); // Accept a published offer using the green marker.
    assert(!state.Observe(200, false));
    assert(state.Observe(200, true));
    state.Reset();
    assert(state.Observe(200, true)); // Resume an accepted delivery after loading a world.
    assert(!state.Observe(200, true));
}
