#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace jd {
struct Choice {
    std::uint64_t id = 0;
    std::string label;
    std::string token;
    std::uint64_t auto_trailer = 0;
    float auto_mass_kg = 0;
    std::string preview_token;
    bool trailer_delivery = false;
};
struct Request {
    std::uint64_t cargo = 0;
    std::uint64_t source = 0;
    std::uint64_t destination = 0;
    std::uint64_t trailer = 0;
    bool take_immediately = true;
    bool owned_trailer = false;
};
struct Snapshot {
    bool supported = false;
    bool world_ready = false;
    bool can_generate = false;
    bool busy = false;
    std::string version;
    std::string message;
    std::vector<Choice> cargos;
    std::vector<Choice> companies;
    std::vector<Choice> trailers;
    std::uint64_t nearest_company = 0;
    std::uint64_t generation = 0;
    std::string language = "en_gb", mass_unit = "kg";
    bool miles = false;
    int routing_mode = 0;
    Request selection;
    float distance_m = 0, mass_kg = 0;
    unsigned cargo_units = 0;
    bool route_pending = false, route_failed = false;
    std::int64_t price_base = 0;
    bool price_valid = false;
    int real_minutes = -1;
    double currency_rate = 1;
    std::string currency_code = "EUR", currency_prefix, currency_suffix = "€";
    std::uint64_t random_revision = 0;
};
struct TelemetrySample {
    bool driving = false;
    bool placement_valid = false;
    bool speed_valid = false;
    bool trailer_connected_valid = false;
    bool trailer_connected = false;
    bool trailer_placement_valid = false;
    double x = 0, y = 0, z = 0;
    double trailer_x = 0, trailer_y = 0, trailer_z = 0;
    float speed = 0;
};
TelemetrySample ReadTelemetry();
// Called once, after plugin loading. Does not mutate game state.
void Initialize();
// Called on the game's telemetry frame-end callback; all game mutations happen here.
void Frame();
// Thread-safe copies / queueing for the render thread.
Snapshot ReadSnapshot();
// Reuses catalogue vectors when the generation has not changed.
void UpdateSnapshot(Snapshot& target);
void Submit(const Request& request);
void Select(const Request& request);
void Surprise(float mass_percent, float distance_percent);
void RequestAudit();
void RequestRefresh();
void MarkWorldLoaded(bool loaded);
bool AssetsReady();
bool WorldLoaded();
unsigned AssetsWorldRevision();
void AssetsPrepared(unsigned revision);
// Frame-thread-only preview clone. Never inserts an offer, spawns an actor,
// accepts a job or changes navigation. Caller releases with DisposePreviewVehicle.
struct PreviewVehicle { std::uintptr_t unit=0; std::uint64_t paint=0; };
PreviewVehicle BuildPreviewVehicle(const Request& request);
void DisposePreviewVehicle(std::uintptr_t unit);
void Shutdown();
// Infrastructure provided by the host.
void Log(const char* format, ...);
std::wstring DataDirectory();
} // namespace jd
