#pragma once
#include <cstdint>
#include <string>

namespace jd {
struct RouteResult {
    float distance_m = 0;
    float road_distance_m = 0;
    float ferry_minutes = 0;
    std::uint64_t ferry_price = 0;
};
enum class RoutePoll { Pending, Ready, Failed };

// Game frame thread only; requires dispatch.cpp's exact executable guard.
bool BeginRoute(std::uintptr_t executable_base, std::uintptr_t source_company,
                std::uintptr_t destination_company, std::string& error);
RoutePoll PollRoute(RouteResult& result, std::string& error);
void CancelRoute();
// Assigns the truck's live navigator to the source company after creation.
// Selection previews and Surprise never call this mutating operation.
bool NavigateToPickup(std::uintptr_t executable_base, std::uintptr_t source_company, bool accepted);
void NavigateAcceptedDelivery(std::uintptr_t executable_base, std::uintptr_t player);
} // namespace jd
