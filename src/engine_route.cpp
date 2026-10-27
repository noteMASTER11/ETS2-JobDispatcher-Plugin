#include "engine_route.hpp"
#include "dispatch.hpp"
#include <windows.h>
#include <array>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace jd {
namespace {
// ETS2 1.61.1.9 native independent routing object, same ownership as the map
// screen's embedded object. Static ABI evidence: work/engine-mapping/route/
// 1410ad630.cpp, 1408b0{b70,c50,d30}.cpp; vtable 0x1422cd8b0.
alignas(16) std::array<unsigned char, 0x188> route_storage{};
std::uintptr_t image_base = 0;
bool constructed = false;
ULONGLONG started_at = 0;
int last_phase = -1;

template<typename Function> Function Native(std::uintptr_t rva) {
    return reinterpret_cast<Function>(image_base + rva);
}
void* Object() { return route_storage.data(); }
}

void CancelRoute() {
    if (!constructed) return;
    // Non-deleting destructor joins native tasks and releases their allocations.
    Native<void (*)(void*)>(0x8b0c50)(Object());
    constructed = false;
    image_base = 0;
    started_at = 0;
    last_phase = -1;
}

bool BeginRoute(std::uintptr_t executable_base, std::uintptr_t source_company,
                std::uintptr_t destination_company, std::string& error) {
    CancelRoute();
    error.clear();
    if (!executable_base || !source_company || !destination_company || source_company == destination_company) {
        error = "Choose two different loaded companies for the road route.";
        return false;
    }
    image_base = executable_base;
    route_storage.fill(0);
    Native<void* (*)(void*)>(0x8b0b70)(Object());
    constructed = true;
    void* task = nullptr;
    // RDX is unused; R8/R9 hold company_volatile unit pointers.
    Native<void** (*)(void**, std::uintptr_t, std::uintptr_t, std::uintptr_t)>(0x7a3bf0)
        (&task, 0, source_company, destination_company);
    if (!task) {
        error = "The game could not create a road route between these companies.";
        CancelRoute();
        return false;
    }
    // Same preference mapping as the game's GPS builder at 0x4fcb1b..0x4fcba3:
    // UI 0 -> optimal 0, UI 1 -> shortest 3, UI 2 -> small roads 4.
    const int preference=std::clamp(Native<int(*)(void*)>(0x1cbe10)(reinterpret_cast<void*>(image_base+0x2d382a0)),0,2);
    const uint32_t native_mode=preference==1?3:preference==2?4:0;
    std::memcpy(static_cast<unsigned char*>(task)+0xf0,&native_mode,4);
    Native<void (*)(void*, void**)>(0x8b0d30)(Object(), &task);
    started_at = GetTickCount64();
    Log("Road route started source=%p destination=%p gps_preference=%d native_mode=%u", reinterpret_cast<void*>(source_company),
        reinterpret_cast<void*>(destination_company),preference,native_mode);
    return true;
}

RoutePoll PollRoute(RouteResult& result, std::string& error) {
    error.clear();
    if (!constructed) {
        error = "There is no pending road route.";
        return RoutePoll::Failed;
    }
    Native<void (*)(void*)>(0x8b1130)(Object());
    int phase = 0;
    std::memcpy(&phase, route_storage.data() + 0x0c, sizeof(phase));
    if (phase != last_phase) {
        Log("Road route phase=%d elapsed_ms=%llu", phase,
            static_cast<unsigned long long>(GetTickCount64() - started_at));
        last_phase = phase;
    }
    if (Native<bool (*)(void*)>(0x8b1460)(Object())) {
        if (GetTickCount64() - started_at <= 30000) return RoutePoll::Pending;
        error = "The game's road route calculation timed out. Try another destination.";
        CancelRoute();
        return RoutePoll::Failed;
    }
    if (!Native<bool (*)(void*)>(0x8b1470)(Object())) {
        error = "The game found no road route between these companies.";
        CancelRoute();
        return RoutePoll::Failed;
    }
    result.distance_m = Native<float (*)(void*)>(0x8b1570)(Object());
    const auto secondary_distance=Native<float(*)(void*)>(0x8b15c0)(Object());
    const auto first_metric=Native<float(*)(void*)>(0x8b1610)(Object());
    const auto second_metric=Native<float(*)(void*)>(0x8b1660)(Object());
    Log("Route metrics primary=%.3f secondary=%.3f start_a=%.3f start_b=%.3f",result.distance_m,secondary_distance,first_metric,second_metric);
    // Current game: slot 70 is ferry LENGTH (task+a4), slot 78 is TIME (task+a8).
    // TruckersMP's older wrapper slot 70 cannot be copied to this game version.
    const auto ferry_metres = Native<float (*)(void*)>(0x8b16b0)(Object());
    result.road_distance_m=std::max(0.f,result.distance_m-ferry_metres);
    result.ferry_minutes = Native<float (*)(void*)>(0x8b1700)(Object())/60.0f;
    result.ferry_price = Native<std::uint64_t (*)(void*)>(0x8b1750)(Object());
    const bool valid = std::isfinite(result.distance_m) && result.distance_m > 0 &&
        result.distance_m < 65535000.0f && std::isfinite(result.ferry_minutes) &&
        result.ferry_minutes >= 0 && result.ferry_minutes <= 65535.0f &&
        result.ferry_price <= 65535;
    Log("Road route result metres=%.3f ferry_metres=%.3f ferry_minutes=%.3f ferry_price=%llu valid=%d",
        result.distance_m, ferry_metres, result.ferry_minutes,
        static_cast<unsigned long long>(result.ferry_price), valid);
    CancelRoute();
    if (!valid) {
        error = "The game returned a road route outside the supported job field limits.";
        return RoutePoll::Failed;
    }
    return RoutePoll::Ready;
}
bool NavigateToPickup(std::uintptr_t executable_base, std::uintptr_t source_company, bool accepted) {
    // Same native 24-byte navigation waypoint used by company/map actions.
    // 7b4bd0 resolves company geometry to the road network; 4fade0 owns/copies
    // the waypoint and schedules the truck's GPS, including its current origin.
    const auto world = *reinterpret_cast<const std::uintptr_t*>(executable_base + 0x36af7c8);
    const auto item = *reinterpret_cast<const std::uintptr_t*>(source_company + 0x10);
    if (!world || !item) return false;
    alignas(8) std::array<std::uint32_t, 6> waypoint{2, 0, 0, 0, 2, 0};
    reinterpret_cast<void* (*)(std::uintptr_t, void*, std::uintptr_t, bool, bool)>(executable_base + 0x7b4bd0)
        (world, waypoint.data(), item, false, false);
    if (waypoint[0] == 2) {
        Log("Pickup GPS: source company has no navigable waypoint source=%p", reinterpret_cast<void*>(source_company));
        return false;
    }
    // Accepted jobs use the native job purpose, so attachment can advance them
    // to delivery. An unaccepted market offer needs the user-selected purpose.
    reinterpret_cast<void (*)(std::uintptr_t, std::uint32_t, const void*)>(executable_base + 0x4fade0)
        (world + 0x4128, accepted ? 1u : 5u, waypoint.data());
    Log("Pickup GPS assigned source=%p waypoint_kind=%u route_revision=%u",
        reinterpret_cast<void*>(source_company), waypoint[0],
        *reinterpret_cast<const std::uint32_t*>(world + 0x4398));
    return true;
}
void NavigateAcceptedDelivery(std::uintptr_t executable_base, std::uintptr_t player) {
    // Native player operation also updates waypoint history, route purpose,
    // routing worker, map and Route Advisor notifications. true selects the
    // accepted job's destination; the actual truck remains the route origin.
    reinterpret_cast<void (*)(std::uintptr_t, bool, std::uintptr_t)>(executable_base + 0x6008c0)
        (player, true, 0);
    Log("Delivery GPS assigned through native job navigation player=%p", reinterpret_cast<void*>(player));
}
} // namespace jd
