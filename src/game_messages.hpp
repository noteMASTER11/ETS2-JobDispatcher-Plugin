#pragma once
#include <string>
namespace jd {
inline std::string GameMessage(const std::string& detail) {
    const auto has = [&](const char* text) { return detail.find(text) != std::string::npos; };
    if (has("Заказ добавлен")) return "@@jd_offer@@";
    if (has("Груз сгенерирован") || has("Задание создано")) return "@@cargo_waiting1@@";
    if (has("current job")) return "@@mp_warning_on_job@@";
    if (has("stop the truck") || has("driving view")) return "@@jd_stop@@";
    if (has("Detach") || has("owned_trailer")) return "@@jd_detach@@";
    if (has("prototype") || has("capacity")) return "@@jd_incompatible@@";
    if (has("parking") || has("offer slot")) return "@@mp_job_loading_placement_not_found@@";
    if (has("route") || has("different") || has("destination identity")) return "@@jd_route_failed@@";
    if (has("not ready") || has("loaded economy") || has("каталог")) return "@@loading@@";
    return "@@mp_job_unknown_error@@";
}
}
