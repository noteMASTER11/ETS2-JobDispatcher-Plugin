#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "dispatch.hpp"
#include "engine_route.hpp"
#include "labels.hpp"
#include "game_messages.hpp"
#include "display_units.hpp"
#include "job_navigation.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <random>

namespace jd { namespace {
std::mutex state_mutex;
Snapshot state;
std::atomic<bool> refresh{true};
std::atomic<bool> world_loaded{false}, invalidate_world{false};
std::atomic<bool> pending{false};
Request queued;
std::optional<Request> routing_request;
Request selected, route_selection;
JobNavigation job_navigation;
std::atomic<bool> selection_dirty{false};
bool preview_routing = false, cached_route_valid = false;
RouteResult cached_route;
uintptr_t cached_source = 0, cached_destination = 0;
ULONGLONG next_settings = 0;
bool surprise_pending=false;
float surprise_mass=50, surprise_distance=50;
std::vector<uint64_t> surprise_destinations;
size_t surprise_destination_cursor=0;
std::atomic<bool> surprise_search{false};
std::mt19937_64 random_engine{std::random_device{}()};
uintptr_t routing_economy = 0;
uintptr_t base = 0, last_economy = 0;
unsigned frame_counter = 0;
struct CompanyPoint { uint64_t id; double x, z; };
std::vector<CompanyPoint> company_points;
size_t nearest_cursor = 0;
bool nearest_scanning = false;
double nearest_origin_x = 1e30, nearest_origin_z = 1e30, nearest_best = 1e300;
uint64_t nearest_result = 0;
std::string operation_message;
struct TrailerPrototype { uintptr_t variant = 0; std::string truck; bool registry_backed=false; };
std::map<uintptr_t, TrailerPrototype> trailer_prototypes;
std::set<std::string> excluded_countries;
int routing_mode=-1;
bool price_dirty=true;
std::atomic<bool> audit_requested{false};
std::vector<Choice> audit_cargos;
size_t audit_cursor=0,audit_failures=0;

bool Readable(uintptr_t p, size_t bytes) {
    if (p < 0x10000 || bytes > 16 * 1024 * 1024 || p + bytes < p) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(p), &info, sizeof(info))) return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return p + bytes <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
}
template<class T> T Read(uintptr_t p, T fallback = {}) {
    T result{};
    if (!Readable(p, sizeof(T))) return fallback;
    std::memcpy(&result, reinterpret_cast<void*>(p), sizeof(T));
    return result;
}
std::string Setting(uintptr_t rva) {
    auto object = base + rva;
    for (unsigned i=0;i<16;++i) {
        const auto override = Read<uintptr_t>(object+0x120);
        if (!override) break;
        if (!Readable(override,0x140)) return {};
        object=override;
    }
    const auto value = object + (Read<uint8_t>(object+0xa1) ? 0x28 : 0xb1);
    if (!Readable(value,100)) return {};
    const auto* text=reinterpret_cast<const char*>(value);
    const auto length=strnlen(text,100);
    return length<100 ? std::string(text,length) : std::string{};
}
bool SameRoute(const Request& a, const Request& b) { return a.source==b.source && a.destination==b.destination; }
bool SameSelection(const Request& a, const Request& b) {
    return SameRoute(a,b) && a.cargo==b.cargo && a.trailer==b.trailer;
}
std::string String(uintptr_t s) {
    auto data = Read<uintptr_t>(s + 8);
    auto length = Read<uint32_t>(s + 16);
    if (length > 4096 || !Readable(data, length)) return {};
    return std::string(reinterpret_cast<char*>(data), length);
}
std::string PointerLabel(const char* kind, uintptr_t p) {
    std::ostringstream out;
    out << kind << " " << std::hex << p;
    return out.str();
}
std::string DecodeToken(uint64_t token) {
    static const char alphabet[] = "\0" "0123456789abcdefghijklmnopqrstuvwxyz_";
    token &= 0x7fffffffffffffffULL;
    std::string out;
    for (unsigned i = 0; i < 13 && token; ++i) {
        unsigned index = static_cast<unsigned>(token % 38);
        if (!index) break;
        out += alphabet[index];
        token /= 38;
    }
    return out;
}
std::string UnitPath(uintptr_t unit) {
    uint32_t identity = Read<uint32_t>(unit + 0xc);
    if (!identity) return {};
    auto table = Read<uintptr_t>(base + 0x3048200 + ((identity >> 20) & 0x7ff) * 8);
    auto stride = Read<uint8_t>(table + 0x88);
    if (!stride || stride > 31) return {};
    auto row = Read<uintptr_t>(table) + (identity & 0xffff) * stride * 8;
    if (!Readable(row, stride * 8)) return {};
    std::string out;
    for (unsigned i = 0; i < stride; ++i) {
        auto token = DecodeToken(Read<uint64_t>(row + i * 8));
        if (token.empty()) return {};
        if (!out.empty()) out += '.';
        out += token;
    }
    return out;
}
bool ExactExecutable() {
    static const unsigned char expected[32] = {0xb7,0x0c,0x23,0x57,0xcc,0x56,0xeb,0x58,0x34,0xee,0x47,0xfe,0xa3,0xc6,0x0b,0x2f,0x2d,0x45,0x91,0x38,0x3f,0xf2,0xac,0x47,0x9c,0xfd,0x3c,0xd2,0x54,0x30,0x1b,0x8c};
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, 32768)) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char output[32]{};
    bool valid = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (valid) valid = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    unsigned char buffer[65536];
    DWORD bytes = 0;
    while (valid) {
        if (!ReadFile(file, buffer, sizeof(buffer), &bytes, nullptr)) { valid = false; break; }
        if (!bytes) break;
        valid = BCryptHashData(hash, buffer, bytes, 0) >= 0;
    }
    if (valid) valid = BCryptFinishHash(hash, output, sizeof(output), 0) >= 0 && !std::memcmp(output, expected, sizeof(expected));
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return valid;
}
std::string CargoLabel(uintptr_t cargo) {
    auto name = labels::Resolve(String(cargo + 0x10));
    return name.empty() ? UnitPath(cargo) : name;
}
bool Code(uintptr_t rva, const unsigned char* bytes, size_t size) {
    return Readable(base + rva, size) && !std::memcmp(reinterpret_cast<void*>(base + rva), bytes, size);
}
template<class T> bool Write(uintptr_t p, const T& value) {
    MEMORY_BASIC_INFORMATION info{};
    if (!Readable(p, sizeof(T)) || !VirtualQuery(reinterpret_cast<void*>(p), &info, sizeof(info))) return false;
    if (!(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
    std::memcpy(reinterpret_cast<void*>(p), &value, sizeof(T));
    return true;
}
bool HasCargoBody(uintptr_t cargo, uintptr_t definition) {
    auto body = Read<uint64_t>(definition + 0x48);
    auto list = Read<uintptr_t>(cargo + 0xa8);
    auto count = Read<uint64_t>(cargo + 0xb0);
    if (!body || !count || count > 100 || !Readable(list, static_cast<size_t>(count) * 8)) return false;
    for (size_t i = 0; i < count; ++i) if (Read<uint64_t>(list + i * 8) == body) return true;
    return false;
}
struct LivePrototype { uintptr_t definition = 0, variant = 0; std::string truck; };
bool TrailerDelivery(uintptr_t cargo,uintptr_t definition){
    const auto mass=Read<float>(cargo+0x9c);
    // SCS uses a tiny nominal payload and a generated body type for deliveries
    // of the trailer itself. The game's job card displays the trailer tare here.
    return mass>0&&mass<=0.000101f&&Read<float>(cargo+0x98)>=.1f&&
        Read<float>(definition+0x34)+Read<float>(definition+0x38)>0;
}
struct Capacity { uint32_t units=0; float fill=0, mass_kg=0, payload_kg=0; };
Capacity CargoCapacity(uintptr_t economy, uintptr_t cargo, uintptr_t definition) {
    if (!Readable(cargo,0xa0)||!Readable(definition,0xd0)) return {};
    alignas(16) unsigned char buffer[0x68]{};
    auto set=[&](size_t offset,const auto& value){std::memcpy(buffer+offset,&value,sizeof(value));};
    set(0x00,economy);set(0x10,1.0f);set(0x18,economy+0x848);set(0x20,definition);
    set(0x28,uint32_t{2});set(0x2c,10000.0f);set(0x40,cargo);set(0x48,uint16_t{0x100});
    if(!reinterpret_cast<int(*)(void*)>(base+0x84f600)(buffer))return {};
    Capacity result;
    std::memcpy(&result.units,buffer+8,4);std::memcpy(&result.fill,buffer+12,4);
    // Native capacity uses cargo_data.mass (+9c) per unit; units already include fill.
    result.payload_kg=result.units*Read<float>(cargo+0x9c);
    result.mass_kg=result.payload_kg;
    if(TrailerDelivery(cargo,definition))result.mass_kg+=Read<float>(definition+0x34)+Read<float>(definition+0x38);
    if(!result.units||!std::isfinite(result.fill)||result.fill<=0||!std::isfinite(result.mass_kg)||result.mass_kg<=0)return {};
    return result;
}
int64_t Quote(uintptr_t economy,const Request& request,uint32_t units,const RouteResult& route){
    const auto data=Read<uintptr_t>(economy+0x170),skills=Read<uintptr_t>(economy+0x7b8);
    if(!units||!Readable(data,0x400)||!Readable(skills,0x30)||!Readable(request.cargo,0x120))return -1;
    struct RevenueInput{uint32_t market,level;uintptr_t skills,cargo;uint32_t units;float km;uint32_t urgency,pad;uint64_t ferry;};
    static_assert(sizeof(RevenueInput)==0x30);
    RevenueInput input{1,0,skills,request.cargo,units,static_cast<float>(static_cast<uint16_t>(std::max(1.f,route.distance_m*.001f))),0,0,route.ferry_price};
    const auto levels=Read<uintptr_t>(data+0x138);
    if(!Readable(levels,0x20))return -1;
    input.level=reinterpret_cast<uint32_t(*)(uintptr_t,uint32_t)>(base+0x84a230)(levels,Read<uint32_t>(economy+0x77c));
    int64_t components[7]{};
    const float factor=std::clamp(Read<float>(base+0x3645b90,1.f),0.f,1.f);
    // Read-only native freight revenue calculator; same input used by the market
    // and acceptance at 0x5a9592..0x5a9692. No offer insertion or acceptance.
    reinterpret_cast<int64_t*(*)(RevenueInput*,int64_t*,uintptr_t,float)>(base+0x79bf30)(&input,components,data,factor);
    int64_t total=0;for(const auto value:components){if(value<0||value>1000000000000LL)return -1;total+=value;}
    return total;
}
LivePrototype FindLivePrototype(uintptr_t economy, uintptr_t requested_definition, uintptr_t cargo) {
    const auto stable=trailer_prototypes.find(requested_definition);
    if(stable!=trailer_prototypes.end()&&stable->second.registry_backed&&Readable(requested_definition,0xd0)&&
        Readable(stable->second.variant,0x1a8)&&HasCargoBody(cargo,requested_definition))
        return {requested_definition,stable->second.variant,stable->second.truck};
    auto list = Read<uintptr_t>(economy + 0x28);
    auto companies = Read<uint64_t>(economy + 0x30);
    if (companies > 20000 || !Readable(list, static_cast<size_t>(companies) * 8)) return {};
    for (size_t c = 0; c < companies; ++c) {
        auto company = Read<uintptr_t>(list + c * 8);
        auto offers = Read<uintptr_t>(company + 0x80);
        auto count = Read<uint64_t>(company + 0x88);
        if (count > 10000 || (count && !Readable(offers, static_cast<size_t>(count) * 8))) continue;
        for (size_t i = 0; i < count; ++i) {
            auto offer = Read<uintptr_t>(offers + i * 8);
            auto definition = Read<uintptr_t>(offer + 0x90);
            auto variant = Read<uintptr_t>(offer + 0x88);
            if (requested_definition && requested_definition != definition) continue;
            if (!Readable(offer, 0xd8) || !Readable(definition, 0xd0) || !Readable(variant, 16)) continue;
            if (!(Read<uint32_t>(definition + 8) & 0x1ffffff) || !(Read<uint32_t>(variant + 8) & 0x1ffffff)) continue;
            if (!String(offer + 0x10).empty() && HasCargoBody(cargo, definition)) return {definition, variant, String(offer + 0x68)};
        }
    }
    return {};
}
void Retain(uintptr_t unit) { if (unit) InterlockedIncrement(reinterpret_cast<LONG*>(unit + 8)); }
void Release(uintptr_t unit) {
    if (unit && !(InterlockedDecrement(reinterpret_cast<LONG*>(unit + 8)) & 0x1ffffff)) {
        reinterpret_cast<void(*)(uintptr_t)>(base + 0xfbfa0)(unit);
    }
}
void DisposeOwned(uintptr_t unit) {
    if (!unit) return;
    auto dispose = Read<uintptr_t>(Read<uintptr_t>(unit) + 8);
    reinterpret_cast<void(*)(uintptr_t)>(dispose)(unit);
    Release(unit);
}
std::string ValidateRequest(const Request& request, uintptr_t economy) {
    if (GetModuleHandleW(L"core_ets2mp.dll")) return "Job Dispatcher SP is available only in single player.";
    Snapshot snapshot;
    { std::lock_guard<std::mutex> lock(state_mutex); snapshot = state; }
    auto contains = [](const std::vector<Choice>& choices, uint64_t id) {
        return std::any_of(choices.begin(), choices.end(), [id](const Choice& choice) { return choice.id == id && (!choice.token.empty() && UnitPath(id) == choice.token) && (Read<uint32_t>(id + 8) & 0x1ffffff); });
    };
    if (!snapshot.supported || !Readable(economy, 0x1df0)) return "The game economy is not ready.";
    if (!contains(snapshot.cargos, request.cargo) || !contains(snapshot.companies, request.source) || !contains(snapshot.companies, request.destination)) return "The selection no longer belongs to the loaded game catalogue. Refresh it.";
    if (request.trailer && !contains(snapshot.trailers, request.trailer)) return "The selected trailer no longer belongs to the loaded game catalogue. Refresh it.";
    if (request.source == request.destination) return "Select different source and destination companies.";
    if (request.owned_trailer) return "This build currently creates company-trailer freight jobs. Detach the owned trailer and select a company trailer.";
    auto player = Read<uintptr_t>(economy + 0x18);
    if (!Readable(player, 0x30)) return "The player profile is not ready.";
    if (request.take_immediately && Read<uintptr_t>(player + 0x28)) return "Finish or abandon the current job in the game before generating another one.";
    auto telemetry = ReadTelemetry();
    if (!telemetry.driving || !telemetry.placement_valid || !telemetry.speed_valid || std::abs(telemetry.speed) > 0.1f) return "Return to the driving view and stop the truck before generating a job.";
    if (request.take_immediately && telemetry.trailer_connected_valid && telemetry.trailer_connected) return "Detach the connected trailer before generating a company-trailer job.";
    return {};
}
std::string Generate(const Request& request, uintptr_t economy, const RouteResult& route) {
    if (auto invalid = ValidateRequest(request, economy); !invalid.empty()) return invalid;
    auto player = Read<uintptr_t>(economy + 0x18);
    // Resolve again from the live owning arrays. The catalogue stores borrowed IDs only;
    // market regeneration may have released its former trailer variant since refresh.
    auto selected_trailer = request.trailer;
    if (!selected_trailer) {
        std::lock_guard<std::mutex> lock(state_mutex);
        for (const auto& cargo : state.cargos) if (cargo.id == request.cargo) { selected_trailer = cargo.auto_trailer; break; }
    }
    auto prototype = FindLivePrototype(economy, selected_trailer, request.cargo);
    if (!prototype.definition) return "No live compatible company trailer prototype is available. Refresh the catalogue or select another trailer.";
    uintptr_t definition = prototype.definition;
    auto destination_path = UnitPath(request.destination);
    const std::string prefix = "company.volatile.";
    if (destination_path.compare(0, prefix.size(), prefix)) return "The destination company has an unsupported unit identity.";
    std::string target = destination_path.substr(prefix.size());
    if (target.empty() || target.size() > 255) return "The destination identity is invalid.";
    Log("Generate preflight cargo=%p source=%p destination=%p definition=%p variant=%p target=%s", reinterpret_cast<void*>(request.cargo), reinterpret_cast<void*>(request.source), reinterpret_cast<void*>(request.destination), reinterpret_cast<void*>(definition), reinterpret_cast<void*>(prototype.variant), target.c_str());
    const auto capacity = CargoCapacity(economy,request.cargo,definition);
    const auto units=capacity.units;const auto fill=capacity.fill;
    if (!units) return "The game rejected this cargo/trailer capacity combination.";
    auto offer_array = request.source + 0x78;
    auto data = Read<uintptr_t>(offer_array + 8);
    auto count = Read<uint64_t>(offer_array + 16);
    if (count > 10000 || (count && !Readable(data, static_cast<size_t>(count) * 8))) return "The source company's offer list is unavailable.";
    auto source_item = Read<uintptr_t>(request.source + 0x10);
    auto parking = Read<uintptr_t>(source_item + 0x98);
    auto parking_count = Read<uint64_t>(source_item + 0xa0);
    auto offer_limit = Read<uint32_t>(source_item + 0xc0);
    if (!offer_limit || offer_limit > parking_count || parking_count > 1000 || !Readable(parking, static_cast<size_t>(parking_count) * 4)) return "This company has no valid freight trailer parking slots.";
    // Prefer an unused/expired offer; otherwise replace one unreserved market offer.
    // Keep its ownership locally until native acceptance succeeds, so a failed call can restore it.
    size_t slot = static_cast<size_t>(count);
    auto reserved = Read<uint32_t>(request.source + 0xfc, 0xffffffff);
    for (size_t i = 0; i < std::min<uint64_t>(count, offer_limit); ++i) {
        if (i == reserved || !(Read<uint32_t>(parking + i * 4) & 4)) continue;
        auto candidate = Read<uintptr_t>(data + i * 8);
        if (!candidate || !Read<uintptr_t>(candidate + 0x58) || String(candidate + 0x10).empty()) { slot = i; break; }
    }
    if (slot == count && count) {
        for (size_t i = 0; i < std::min<uint64_t>(count, offer_limit); ++i) if (i != reserved && (Read<uint32_t>(parking + i * 4) & 4)) { slot = i; break; }
    }
    if (slot == count) {
        if (count >= offer_limit || !(Read<uint32_t>(parking + count * 4) & 4)) return "There is no unreserved freight parking slot at this source company.";
        auto reserve = Read<uintptr_t>(Read<uintptr_t>(offer_array) + 0x18);
        if (!Readable(reserve, 8) || !reinterpret_cast<bool(*)(uintptr_t, uint64_t)>(reserve)(offer_array, count + 1)) return "The game could not reserve an offer slot.";
        data = Read<uintptr_t>(offer_array + 8);
        if (!Readable(data + slot * 8, 8)) return "The game returned an invalid offer allocation.";
    }
    // Same context lookup as the current game's native generator at RVA 0x7d0486.
    auto context_index = Read<uint32_t>(base + 0x2733298, 0xffffffff);
    if (context_index >= 4) return "The game's unit allocation context is unavailable.";
    auto context = Read<uintptr_t>(base + 0x367f918 + context_index * 8);
    if (!Readable(context, 0x14)) return "The game's unit allocation context is not initialized.";
    auto job = reinterpret_cast<uintptr_t(*)(uintptr_t)>(base + 0xaba700)(context);
    if (!Readable(job, 0xd8)) return "The game could not construct a freight offer.";
    Retain(job);
    InterlockedOr(reinterpret_cast<LONG*>(job + 8), 0x20000000);
    auto format = reinterpret_cast<void(*)(uintptr_t, const char*, ...)>(base + 0xf4960);
    format(job + 0x10, "%s", target.c_str());
    if (!prototype.truck.empty()) format(job + 0x68, "%s", prototype.truck.c_str());
    Retain(request.cargo); Write(job + 0x58, static_cast<uintptr_t>(request.cargo));
    Retain(definition); Write(job + 0x90, definition);
    Retain(prototype.variant); Write(job + 0x88, prototype.variant);
    Write(job + 0x48, Read<uint32_t>(economy + 0x19c) + uint32_t{120});
    Write(job + 0x4c, uint32_t{0});
    // Native routing reports scaled road metres, including map scale and ferries.
    // These conversions match TruckersMP's job-offer fields.
    Write(job + 0x50, static_cast<uint16_t>(std::max(1.0f, route.distance_m * 0.001f)));
    Write(job + 0x52, static_cast<uint16_t>(route.ferry_minutes));
    Write(job + 0x54, static_cast<uint16_t>(route.ferry_price));
    Write(job + 0xcc, units);
    Write(job + 0xd0, fill);
    auto old_offer = slot < count ? Read<uintptr_t>(data + slot * 8) : 0;
    Retain(job); // borrowed local reference survives removal from the source's owning array
    Write(data + slot * 8, job);
    if (slot == count) Write(offer_array + 16, count + 1);
    Log("Refreshing source-company trailer parking item=%p", reinterpret_cast<void*>(source_item));
    reinterpret_cast<void(*)(uintptr_t, uintptr_t)>(base + 0x6fe840)(source_item, economy);
    if (!request.take_immediately) {
        if (old_offer) DisposeOwned(old_offer);
        Release(job);
        Log("Freight offer published source=%p offer=%p parking_slot=%llu; no acceptance requested",
            reinterpret_cast<void*>(request.source), reinterpret_cast<void*>(job), static_cast<unsigned long long>(slot));
        NavigateToPickup(base, request.source, false);
        return "Заказ добавлен в компанию. Подъезжайте к зелёному значку и выберите груз.";
    }
    const char* empty = "";
    Log("Calling freight acceptance offer=%p slot=%llu units=%u fill=%f", reinterpret_cast<void*>(job), static_cast<unsigned long long>(slot), units, fill);
    bool accepted = reinterpret_cast<bool(*)(uintptr_t, uintptr_t, uintptr_t, const char**, uint32_t)>(base + 0x79d8f0)(economy, request.source, job, &empty, 0);
    auto current_job = Read<uintptr_t>(player + 0x28);
    Log("Freight acceptance returned=%d current_job=%p", accepted, reinterpret_cast<void*>(current_job));
    if (accepted && current_job) {
        if (old_offer) DisposeOwned(old_offer);
        Release(job);
        Log("Pickup trailer remains at source company=%p parking_slot=%llu; no truck placement or coupling requested", reinterpret_cast<void*>(request.source), static_cast<unsigned long long>(slot));
        job_navigation.WaitingForPickup(current_job);
        NavigateToPickup(base, request.source, true);
        return "Груз сгенерирован. Можете забирать его в выбранном месте погрузки.";
    }
    auto current_data = Read<uintptr_t>(offer_array + 8);
    auto current_count = Read<uint64_t>(offer_array + 16);
    bool restored = slot < current_count && Read<uintptr_t>(current_data + slot * 8) == job;
    if (restored) {
        Write(current_data + slot * 8, old_offer);
        if (slot == count && current_count == count + 1) Write(offer_array + 16, count);
        DisposeOwned(job);
    } else if (old_offer) DisposeOwned(old_offer);
    Release(job);
    return "The game rejected job acceptance; the previous offer was restored where possible.";
}
void Publish(Snapshot next) { std::lock_guard<std::mutex> lock(state_mutex); state = std::move(next); }

void Catalogue(uintptr_t economy) {
    Snapshot next;
    { std::lock_guard<std::mutex> lock(state_mutex); next.supported = state.supported; next.busy = state.busy; next.version = state.version; next.generation = state.generation + 1; }
    next.world_ready = false;
    next.language=labels::Language();next.miles=Setting(0x2d49240)=="1";next.mass_unit=Setting(0x2d49380);
    if (!Readable(economy, 0x1a0)) { next.message = "Дождитесь загрузки игрового мира."; Publish(std::move(next)); return; }
    auto companies = Read<uintptr_t>(economy + 0x28);
    auto count = Read<uint64_t>(economy + 0x30);
    if (!count || count > 20000 || !Readable(companies, static_cast<size_t>(count) * 8)) {
        next.message = "Площадки погрузки пока недоступны."; Publish(std::move(next)); return;
    }
    std::set<uintptr_t> cargos, trailers;
    std::map<std::string,uintptr_t> loaded_variants;
    std::set<uintptr_t> registered_variants;
    company_points.clear();
    nearest_scanning = false;
    nearest_origin_x = nearest_origin_z = 1e30;
    trailer_prototypes.clear();
    const auto world=Read<uintptr_t>(base+0x36af7c8);
    const auto variant_list=Read<uintptr_t>(world+0x410),variant_count=Read<uint64_t>(world+0x418);
    if(variant_count<20000&&Readable(variant_list,variant_count*8))for(size_t i=0;i<variant_count;++i){
        const auto variant=Read<uintptr_t>(variant_list+i*8);const auto path=UnitPath(variant);
        if(path.rfind("trailer.",0)==0&&Readable(variant,0x1a8)){loaded_variants[path]=variant;registered_variants.insert(variant);}
    }
    const auto definition_list=Read<uintptr_t>(world+0x438),definition_count=Read<uint64_t>(world+0x440);
    if(definition_count<20000&&Readable(definition_list,definition_count*8))for(size_t i=0;i<definition_count;++i){
        const auto definition=Read<uintptr_t>(definition_list+i*8);
        if(!Readable(definition,0xd0)||UnitPath(definition).rfind("trailer_def.",0)!=0)continue;
        const auto legacy=loaded_variants.find(String(definition+0x10));
        uintptr_t variant=legacy==loaded_variants.end()?0:legacy->second;
        if(!variant)for(const auto& entry:loaded_variants)if(Read<uintptr_t>(entry.second+0x1a0)==definition){variant=entry.second;break;}
        if(variant){trailers.insert(definition);trailer_prototypes.emplace(definition,TrailerPrototype{variant,"",true});}
    }
    auto telemetry = ReadTelemetry();
    double nearest_distance = 1e300;
    size_t offer_count = 0;
    for (size_t i = 0; i < count; ++i) {
        auto company = Read<uintptr_t>(companies + i * 8);
        if (!Readable(company, 0x118)) continue;
        auto permanent = Read<uintptr_t>(company + 0x68);
        auto map_item = Read<uintptr_t>(company + 0x10);
        if (!Readable(permanent, 0x148) || !map_item) continue;
        const auto city=Read<uintptr_t>(company+0x100);
        if(!Readable(city,0x160)||excluded_countries.count(DecodeToken(Read<uint64_t>(city+0x158))))continue;
        // No export list means the depot cannot offer company-trailer freight.
        if(!Read<uint64_t>(permanent+0xa8))continue;
        std::string name = String(permanent + 0x10);
        if (name.empty()) name = PointerLabel("Company", company);
        std::string company_path = UnitPath(company);
        const auto city_separator = company_path.find_last_of('.');
        auto city_data = Read<uintptr_t>(company + 0x100);
        std::string city_name = String(city_data + 0x30);
        if (city_name.empty()) city_name = String(city_data + 0x10);
        if (city_name.empty() && city_separator != std::string::npos) city_name = company_path.substr(city_separator + 1);
        name = labels::Company(name, city_name);
        next.companies.push_back({company,name,company_path,0,0,"",false});
        for (const auto direction : {size_t{0xa0}, size_t{0xc8}}) {
            auto list = Read<uintptr_t>(permanent + direction);
            auto size = Read<uint64_t>(permanent + direction + 8);
            if (size > 10000 || !Readable(list, static_cast<size_t>(size) * 8)) continue;
            for (size_t j = 0; j < size; ++j) {
                auto cargo = Read<uintptr_t>(list + j * 8);
                if (Readable(cargo, 0x120)) cargos.insert(cargo);
            }
        }
        {
            auto coordinate = Read<uintptr_t>(map_item + 0x60);
            if (Readable(coordinate, 12)) {
                double x = Read<int32_t>(coordinate) / 256.0;
                double z = Read<int32_t>(coordinate + 8) / 256.0;
                company_points.push_back({company, x, z});
                double distance = std::pow(x - telemetry.x, 2) + std::pow(z - telemetry.z, 2);
                if (distance < nearest_distance) { nearest_distance = distance; next.nearest_company = company; }
            }
        }
        auto offers = Read<uintptr_t>(company + 0x80);
        auto offers_count = Read<uint64_t>(company + 0x88);
        if (offers_count > 10000 || !Readable(offers, static_cast<size_t>(offers_count) * 8)) continue;
        for (size_t j = 0; j < offers_count; ++j) {
            auto offer = Read<uintptr_t>(offers + j * 8);
            if (!Readable(offer, 0xd8)) continue;
            ++offer_count;
            auto cargo = Read<uintptr_t>(offer + 0x58);
            if (Readable(cargo, 0x120)) cargos.insert(cargo);
            auto trailer = Read<uintptr_t>(offer + 0x90);
            auto variant = Read<uintptr_t>(offer + 0x88);
            if (Readable(trailer, 0xd0) && Readable(variant, 16)) {
                trailers.insert(trailer);
                trailer_prototypes.emplace(trailer,TrailerPrototype{variant,String(offer+0x68),registered_variants.count(variant)!=0});
            }
        }
    }
    struct TrailerSize{uint64_t id,body;float volume,payload,tare;};
    std::vector<TrailerSize> trailer_sizes;
    for(auto trailer:trailers) {
        trailer_sizes.push_back({trailer,Read<uint64_t>(trailer+0x48),Read<float>(trailer+0x40),
            Read<float>(trailer+0x30)-Read<float>(trailer+0x34)-Read<float>(trailer+0x38),Read<float>(trailer+0x34)+Read<float>(trailer+0x38)});
    }
    for (auto cargo : cargos) {
        std::string name = CargoLabel(cargo);
        uint64_t compatible = 0;float cargo_mass=0;
        auto bodies=Read<uintptr_t>(cargo+0xa8);
        auto body_count=Read<uint64_t>(cargo+0xb0);
        const auto raw_volume=Read<float>(cargo+0x98),raw_mass=Read<float>(cargo+0x9c);
        const auto unit_volume=raw_volume>0?raw_volume:1.f,unit_mass=raw_mass>0?raw_mass:1.f;
        std::vector<std::pair<float,uint64_t>> candidates;
        if(body_count<1000&&Readable(bodies,body_count*8))for(size_t b=0;b<body_count;++b) {
            const auto body=Read<uint64_t>(bodies+b*8);
            if(unit_volume<=0||unit_mass<=0||!std::isfinite(unit_mass)||!std::isfinite(unit_volume))continue;
            for(const auto& trailer:trailer_sizes)if(trailer.body==body&&trailer.volume>=unit_volume&&trailer.payload>=unit_mass){
                const float units=std::floor(std::min(trailer.volume/unit_volume,trailer.payload/unit_mass));
                if(std::isfinite(units)&&units>=1)candidates.emplace_back(units*unit_mass+(TrailerDelivery(cargo,trailer.id)?trailer.tare:0.f),trailer.id);
            }
        }
        std::sort(candidates.rbegin(),candidates.rend());
        for(const auto& candidate:candidates){const auto capacity=CargoCapacity(economy,cargo,candidate.second);if(capacity.units){compatible=candidate.second;cargo_mass=capacity.mass_kg;break;}}
        next.cargos.push_back({cargo,name,UnitPath(cargo),compatible,cargo_mass,"",compatible&&TrailerDelivery(cargo,compatible)});
    }
    for (auto trailer : trailers) {
        auto unit_path = UnitPath(trailer);
        std::string name = labels::Trailer(unit_path, String(trailer + 0x10), DecodeToken(Read<uint64_t>(trailer + 0x48)));
        if (name.empty()) name = PointerLabel("Trailer", trailer);
        auto preview_token=String(trailer+0x10);
        if(preview_token.rfind("trailer.",0)!=0)preview_token=unit_path;
        next.trailers.push_back({trailer,name,unit_path,0,0,preview_token});
    }
    auto by_label = [](const Choice& a, const Choice& b) { return a.label < b.label; };
    std::sort(next.cargos.begin(), next.cargos.end(), by_label);
    std::sort(next.companies.begin(), next.companies.end(), by_label);
    std::sort(next.trailers.begin(), next.trailers.end(), by_label);
    next.world_ready = !next.companies.empty();
    next.can_generate = next.supported && next.world_ready && !next.cargos.empty() && !trailer_prototypes.empty();
    next.message = operation_message;
    Log("Live catalogue: economy=%p companies=%llu offers=%llu cargos=%llu trailers=%llu nearest=%p", reinterpret_cast<void*>(economy), static_cast<unsigned long long>(next.companies.size()), static_cast<unsigned long long>(offer_count), static_cast<unsigned long long>(next.cargos.size()), static_cast<unsigned long long>(next.trailers.size()), reinterpret_cast<void*>(next.nearest_company));
    Publish(std::move(next));
}
} // namespace

void Initialize() {
    base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    labels::Load(std::filesystem::path(DataDirectory()));
    {std::ifstream input(std::filesystem::path(DataDirectory())/L"excluded-countries.tsv");std::string country;
     while(std::getline(input,country)){if(!country.empty()&&country.back()=='\r')country.pop_back();if(!country.empty()&&country.front()!='#')excluded_countries.insert(country);}}
    Log("Native localization loaded: %llu keys", static_cast<unsigned long long>(labels::Count()));
    Snapshot next;
    next.version = "ETS2 1.61.1.9";
    static const unsigned char accept[] = {0x48,0x8b,0xc4,0x4c,0x89,0x48,0x20,0x48,0x89,0x50,0x10};
    static const unsigned char factory[] = {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9};
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    next.supported = !GetModuleHandleW(L"core_ets2mp.dll") && nt->OptionalHeader.SizeOfImage == 60338176 && Code(0x79d8f0, accept, sizeof(accept)) && ExactExecutable();
    next.message = next.supported ? "Дождитесь загрузки игрового мира." : "Эта версия игры пока не поддерживается.";
    Log("Game adapter initialized: base=%p supported=%d image_size=%u", reinterpret_cast<void*>(base), next.supported, nt->OptionalHeader.SizeOfImage);
    (void)factory;
    Publish(std::move(next));
}
void Frame() {
    bool supported;
    { std::lock_guard<std::mutex> lock(state_mutex); supported = state.supported; }
    if (!supported) return;
    auto economy = Read<uintptr_t>(base + 0x3046780);
    if(GetTickCount64()>=next_settings) {
        price_dirty=true;
        next_settings=GetTickCount64()+1000;
        labels::RequestLanguage(Setting(0x2d54800));
        const auto language=labels::Language();
        std::lock_guard<std::mutex> lock(state_mutex);
        state.miles=Setting(0x2d49240)=="1";state.mass_unit=Setting(0x2d49380);
        const auto economy_data=Read<uintptr_t>(economy+0x170);
        const int currency=reinterpret_cast<int(*)(void*)>(base+0x1cbe10)(reinterpret_cast<void*>(base+0x2d38f20));
        if(currency>=0&&static_cast<uint64_t>(currency)<Read<uint64_t>(economy_data+0x330)){
            const auto index=static_cast<size_t>(currency);
            state.currency_code=String(Read<uintptr_t>(economy_data+0x328)+index*32);
            state.currency_prefix=String(Read<uintptr_t>(economy_data+0x378)+index*32);
            state.currency_suffix=String(Read<uintptr_t>(economy_data+0x3a0)+index*32);
            state.currency_rate=Read<float>(Read<uintptr_t>(economy_data+0x350)+index*4,1.f);
        }
        const auto mode_text=Setting(0x2d382a0);
        const int mode=mode_text=="1"?1:mode_text=="2"?2:0;
        state.routing_mode=mode;
        if(mode!=routing_mode&&!routing_request){routing_mode=mode;CancelRoute();preview_routing=false;cached_route_valid=false;cached_source=cached_destination=0;selection_dirty=true;}
        if(state.language!=language) {
            state.language=language;
            for(auto& cargo:state.cargos)cargo.label=CargoLabel(cargo.id);
            for(auto& company:state.companies){
                const auto permanent=Read<uintptr_t>(company.id+0x68);
                company.label=labels::Company(String(permanent+0x10),company.token.substr(company.token.find_last_of('.')+1));
            }
            for(auto& trailer:state.trailers)trailer.label=labels::Trailer(trailer.token,String(trailer.id+0x10),DecodeToken(Read<uint64_t>(trailer.id+0x48)));
            ++state.generation;
            Log("Locale changed to %s; cached catalogue relabelled",language.c_str());
        }
    }
    auto finish = [](std::string message) {
        Log("Dispatcher result: %s", message.c_str());
        message = GameMessage(message);
        CancelRoute();
        routing_request.reset();
        routing_economy = 0;
        operation_message = message;
        std::lock_guard<std::mutex> lock(state_mutex);
        state.message = std::move(message);
        state.busy = false;
    };
    if (routing_request && economy != routing_economy) finish("The loaded economy changed during route calculation. Refresh the catalogue and try again.");
    const auto live = ReadTelemetry();
    if(invalidate_world.exchange(false)) {
        job_navigation.Reset();
        audit_cargos.clear();audit_cursor=0;
        CancelRoute();routing_request.reset();preview_routing=false;cached_route_valid=false;
        cached_source=cached_destination=0;selection_dirty=true;
        last_economy=0;company_points.clear();nearest_scanning=false;
        Snapshot empty;
        {std::lock_guard<std::mutex> lock(state_mutex);empty.supported=state.supported;empty.version=state.version;empty.generation=state.generation+1;}
        empty.message="Дождитесь загрузки игрового мира.";Publish(std::move(empty));
    }
    if((world_loaded.load()||live.driving)&&Readable(economy,0x38)&&Read<uint64_t>(economy+0x30)>0) {
        const bool requested_refresh=refresh.exchange(false);
        if(economy!=last_economy||requested_refresh){
            if(economy!=last_economy){job_navigation.Reset();CancelRoute();routing_request.reset();preview_routing=false;cached_route_valid=false;cached_source=cached_destination=0;}
            last_economy=economy;Catalogue(economy);selection_dirty=true;
        }
    }
    if (!live.driving || !live.placement_valid) {
        if (routing_request) finish("Расчёт прерван: вернитесь в игровой мир и повторите запрос.");
        if(preview_routing){CancelRoute();preview_routing=false;selection_dirty=true;}
        return;
    }
    Request wanted;bool changed=false;
    if (Readable(economy, 0x20) && live.trailer_connected_valid) {
        const auto player = Read<uintptr_t>(economy + 0x18);
        if (Readable(player, 0x30)) {
            const auto current_job = Read<uintptr_t>(player + 0x28);
            if (job_navigation.Observe(current_job, live.trailer_connected) && Readable(current_job, 0x40) &&
                Readable(Read<uintptr_t>(current_job + 0x30), 0x18))
                NavigateAcceptedDelivery(base, player);
        }
    }
    bool randomize=false;float mass_percent=0,distance_percent=0;
    {std::lock_guard<std::mutex> lock(state_mutex);randomize=surprise_pending;surprise_pending=false;mass_percent=surprise_mass;distance_percent=surprise_distance;}
    if(randomize&&!routing_request&&!company_points.empty()) {
        std::vector<std::pair<double,uint64_t>> nearby;
        for(const auto& point:company_points){const double dx=point.x-live.x,dz=point.z-live.z;nearby.emplace_back(dx*dx+dz*dz,point.id);}
        std::sort(nearby.begin(),nearby.end());
        const double radius=std::max(2000.0,std::sqrt(nearby.front().first)+500.0);
        size_t count=1;
        while(count<nearby.size()&&count<8&&nearby[count].first<=radius*radius)++count;
        const auto source=nearby[std::uniform_int_distribution<size_t>(0,count-1)(random_engine)].second;
        const auto origin=std::find_if(company_points.begin(),company_points.end(),[&](const CompanyPoint& p){return p.id==source;});
        std::vector<std::pair<double,uint64_t>> destinations;
        for(const auto& point:company_points)if(point.id!=source){const double dx=point.x-origin->x,dz=point.z-origin->z;destinations.emplace_back(std::sqrt(dx*dx+dz*dz),point.id);}
        std::vector<std::pair<float,uint64_t>> masses;
        Snapshot choices=ReadSnapshot();
        for(const auto& cargo:choices.cargos)if(cargo.auto_mass_kg>0)masses.emplace_back(cargo.auto_mass_kg,cargo.id);
        if(!destinations.empty()&&!masses.empty()){
            std::sort(destinations.begin(),destinations.end());std::sort(masses.begin(),masses.end());
            const auto target_distance=destinations.front().first+(destinations.back().first-destinations.front().first)*distance_percent/100.;
            const auto target_mass=masses.front().first+(masses.back().first-masses.front().first)*mass_percent/100.;
            std::sort(destinations.begin(),destinations.end(),[&](const auto& a,const auto& b){return std::abs(a.first-target_distance)<std::abs(b.first-target_distance);});
            const auto destination=destinations.front().second;
            const auto cargo=std::min_element(masses.begin(),masses.end(),[&](const auto& a,const auto& b){return std::abs(a.first-target_mass)<std::abs(b.first-target_mass);})->second;
            std::lock_guard<std::mutex> lock(state_mutex);
            selected.cargo=cargo;selected.trailer=0;selected.source=source;selected.destination=destination;
            surprise_destinations.clear();for(size_t i=0;i<std::min<size_t>(32,destinations.size());++i)surprise_destinations.push_back(destinations[i].second);
            surprise_destination_cursor=1;surprise_search=true;
            selection_dirty=true;++state.random_revision;
            Log("Surprise selection cargo=%p source=%p destination=%p mass_percent=%.0f distance_percent=%.0f; preview only",reinterpret_cast<void*>(cargo),reinterpret_cast<void*>(source),reinterpret_cast<void*>(destination),mass_percent,distance_percent);
        }
    }
    {std::lock_guard<std::mutex> lock(state_mutex);wanted=selected;changed=selection_dirty;selection_dirty=false;}
    if(changed) {
        price_dirty=true;
        uint64_t definition=wanted.trailer;bool valid_cargo=false,valid_route=false;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            for(const auto& cargo:state.cargos)if(cargo.id==wanted.cargo){valid_cargo=true;if(!definition)definition=cargo.auto_trailer;break;}
            const auto exists=[&](uint64_t id){return std::any_of(state.companies.begin(),state.companies.end(),[&](const Choice& c){return c.id==id;});};
            valid_route=wanted.source!=wanted.destination&&exists(wanted.source)&&exists(wanted.destination);
        }
        const auto capacity=valid_cargo?CargoCapacity(economy,wanted.cargo,definition):Capacity{};
        if(!routing_request) {
            if(preview_routing&&!SameRoute(wanted,route_selection)){CancelRoute();preview_routing=false;}
            if(valid_route&&(cached_source!=wanted.source||cached_destination!=wanted.destination)&&!preview_routing){
                std::string error;route_selection=wanted;cached_route_valid=false;
                cached_source=wanted.source;cached_destination=wanted.destination;
                preview_routing=BeginRoute(base,wanted.source,wanted.destination,error);
            }
        }
        std::lock_guard<std::mutex> lock(state_mutex);
        state.selection=wanted;state.mass_kg=capacity.mass_kg;
        state.price_valid=false;
        const bool matches=cached_source==wanted.source&&cached_destination==wanted.destination;
        state.distance_m=matches&&cached_route_valid?cached_route.distance_m:0;
        state.route_pending=preview_routing&&SameRoute(wanted,route_selection);
        state.route_failed=valid_route&&matches&&!preview_routing&&!cached_route_valid;
    }
    std::optional<Request> request;
    { std::lock_guard<std::mutex> lock(state_mutex); if (pending.exchange(false)) { request = queued; state.busy = true; } }
    if (request) {
        auto invalid = ValidateRequest(*request, economy);
        if (!invalid.empty()) finish(std::move(invalid));
        else if(cached_route_valid&&cached_source==request->source&&cached_destination==request->destination)finish(Generate(*request,economy,cached_route));
        else {
            std::string error;
            if (!(preview_routing&&SameRoute(*request,route_selection))&&!BeginRoute(base, request->source, request->destination, error)) finish(std::move(error));
            else {
                preview_routing=false;route_selection=*request;
                routing_request = *request;
                routing_economy = economy;
                std::lock_guard<std::mutex> lock(state_mutex);
                state.message = "@@calculating_route@@";
            }
        }
    }
    if (routing_request||preview_routing) {
        RouteResult route;
        std::string error;
        auto result = PollRoute(route, error);
        if(result!=RoutePoll::Pending){
            price_dirty=true;
            cached_source=route_selection.source;cached_destination=route_selection.destination;
            cached_route_valid=result==RoutePoll::Ready;cached_route=route;preview_routing=false;
            {std::lock_guard<std::mutex> lock(state_mutex);
                if(SameRoute(state.selection,route_selection)){state.distance_m=cached_route_valid?route.distance_m:0;state.route_pending=false;state.route_failed=!cached_route_valid;}}
            if(routing_request){if(cached_route_valid)finish(Generate(*routing_request,economy,route));else finish(std::move(error));}
            else if(surprise_search){
                std::lock_guard<std::mutex> lock(state_mutex);
                if(cached_route_valid)surprise_search=false;
                else if(surprise_destination_cursor<surprise_destinations.size()){
                    selected.destination=surprise_destinations[surprise_destination_cursor++];
                    selection_dirty=true;++state.random_revision;
                    state.selection=selected;state.distance_m=0;state.route_pending=true;state.route_failed=false;
                }else surprise_search=false;
            }
        }
    }
    if (!nearest_scanning && (std::abs(live.x - nearest_origin_x) > 50 || std::abs(live.z - nearest_origin_z) > 50)) {
        nearest_origin_x = live.x; nearest_origin_z = live.z;
        nearest_cursor = 0; nearest_best = 1e300; nearest_result = 0; nearest_scanning = true;
    }
    if (nearest_scanning) {
        const auto end = std::min(company_points.size(), nearest_cursor + 96);
        for (; nearest_cursor < end; ++nearest_cursor) {
            const auto& point = company_points[nearest_cursor];
            const double dx = point.x - nearest_origin_x, dz = point.z - nearest_origin_z;
            const double distance = dx * dx + dz * dz;
            if (distance < nearest_best) { nearest_best = distance; nearest_result = point.id; }
        }
        if (nearest_cursor == company_points.size()) {
            nearest_scanning = false;
            std::lock_guard<std::mutex> lock(state_mutex); state.nearest_company = nearest_result;
        }
    }
    if (++frame_counter % 600 == 0 && !economy) refresh = true;
    if(price_dirty){
        price_dirty=false;Request current;uint64_t definition=0;
        {std::lock_guard<std::mutex> lock(state_mutex);current=state.selection;definition=current.trailer;
            if(!definition)for(const auto& cargo:state.cargos)if(cargo.id==current.cargo){definition=cargo.auto_trailer;break;}}
        if(cached_route_valid&&current.source==cached_source&&current.destination==cached_destination){
            const auto capacity=CargoCapacity(economy,current.cargo,definition);
            const auto quote=Quote(economy,current,capacity.units,cached_route);
            const auto source_city=Read<uintptr_t>(current.source+0x100),destination_city=Read<uintptr_t>(current.destination+0x100);
            const bool urban=source_city&&source_city==destination_city;
            const bool uk=DecodeToken(Read<uint64_t>(source_city+0x158))=="uk"&&DecodeToken(Read<uint64_t>(destination_city+0x158))=="uk";
            const auto minutes=units::RealMinutes(cached_route.road_distance_m,urban?3.:uk?15.:19.);
            std::lock_guard<std::mutex> lock(state_mutex);state.price_base=std::max<int64_t>(0,quote);state.price_valid=quote>=0;state.real_minutes=minutes;
        }else{std::lock_guard<std::mutex> lock(state_mutex);state.price_valid=false;state.real_minutes=-1;}
    }
    if(audit_requested.exchange(false)){
        std::lock_guard<std::mutex> lock(state_mutex);audit_cargos=state.cargos;audit_cursor=0;audit_failures=0;
        Log("Catalogue audit started cargos=%llu trailers=%llu",static_cast<unsigned long long>(state.cargos.size()),static_cast<unsigned long long>(state.trailers.size()));
        for(const auto& trailer:state.trailers)Log("Audit trailer token=%s preview=%s",trailer.token.c_str(),trailer.preview_token.c_str());
    }
    if(audit_cursor<audit_cargos.size()){
        const auto& cargo=audit_cargos[audit_cursor++];
        const auto capacity=CargoCapacity(economy,cargo.id,cargo.auto_trailer);
        const bool valid=capacity.units&&std::abs(capacity.mass_kg-cargo.auto_mass_kg)<.1f;
        if(!valid)++audit_failures;
        Log("Audit cargo token=%s trailer=%s units=%u payload_kg=%.5f display_kg=%.3f trailer_delivery=%d valid=%d",cargo.token.c_str(),cargo.auto_trailer?UnitPath(cargo.auto_trailer).c_str():"none",capacity.units,capacity.payload_kg,capacity.mass_kg,cargo.trailer_delivery,valid);
        if(audit_cursor==audit_cargos.size())Log("Catalogue audit complete checked=%llu unavailable_or_invalid=%llu",static_cast<unsigned long long>(audit_cursor),static_cast<unsigned long long>(audit_failures));
    }
}
Snapshot ReadSnapshot() { std::lock_guard<std::mutex> lock(state_mutex); return state; }
void UpdateSnapshot(Snapshot& target) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (target.generation != state.generation || target.supported != state.supported) { target = state; return; }
    target.world_ready = state.world_ready; target.can_generate = state.can_generate;
    target.busy = state.busy; target.message = state.message;
    target.nearest_company = state.nearest_company;
    target.language=state.language;target.mass_unit=state.mass_unit;target.miles=state.miles;
    target.routing_mode=state.routing_mode;
    target.selection=state.selection;target.mass_kg=state.mass_kg;target.distance_m=state.distance_m;
    target.route_pending=state.route_pending;target.route_failed=state.route_failed;
    target.random_revision=state.random_revision;
    target.price_base=state.price_base;target.price_valid=state.price_valid;target.currency_rate=state.currency_rate;
    target.real_minutes=state.real_minutes;
    target.currency_code=state.currency_code;target.currency_prefix=state.currency_prefix;target.currency_suffix=state.currency_suffix;
}
void Submit(const Request& request) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (!state.supported || state.busy || pending.load()) return;
    queued = request; state.busy = true; pending = true;
}
void Select(const Request& request) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if(!SameSelection(selected,request)){selected=request;selection_dirty=true;surprise_search=false;}
}
void Surprise(float mass_percent,float distance_percent){
    std::lock_guard<std::mutex> lock(state_mutex);
    if(state.busy||!state.world_ready)return;
    surprise_mass=std::clamp(mass_percent,0.0f,100.0f);surprise_distance=std::clamp(distance_percent,0.0f,100.0f);surprise_pending=true;
}
void RequestAudit(){audit_requested=true;}
void RequestRefresh() { refresh = true; }
void MarkWorldLoaded(bool loaded) {
    world_loaded=loaded;
    if(!loaded){invalidate_world=true;refresh=true;}
}
void Shutdown() { pending = false; CancelRoute(); routing_request.reset(); routing_economy = 0; Publish({}); }
} // namespace jd
