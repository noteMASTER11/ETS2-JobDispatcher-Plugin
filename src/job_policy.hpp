#pragma once
#include "dispatch.hpp"
#include <algorithm>
#include <optional>
#include <variant>

namespace jd::domain {
class ValidatedJob;
template<class Tag> class UnitId {
    uint64_t value_;
    explicit UnitId(uint64_t value):value_(value){}
    friend class ValidatedJob;
public:
    uint64_t Value()const{return value_;}
};
using CargoId=UnitId<struct CargoTag>;
using CompanyId=UnitId<struct CompanyTag>;
using TrailerId=UnitId<struct TrailerTag>;
enum class PlacementMode { Immediate, MarketOffer };
enum class RequestError { Incomplete, SameCompany, OwnedTrailer };
using JobParse=std::variant<ValidatedJob,RequestError>;
JobParse ParseJob(const Request& draft);

// This is a checked domain selection, not a native lifetime lease. Live unit
// identities and the loaded world must still be revalidated before game calls.
class ValidatedJob {
    CargoId cargo_;
    CompanyId source_,destination_;
    std::optional<TrailerId> trailer_;
    PlacementMode placement_;
    explicit ValidatedJob(const Request& draft):cargo_(draft.cargo),source_(draft.source),destination_(draft.destination),
        trailer_(draft.trailer?std::optional<TrailerId>(TrailerId(draft.trailer)):std::nullopt),
        placement_(draft.take_immediately?PlacementMode::Immediate:PlacementMode::MarketOffer){}
    friend JobParse ParseJob(const Request&);
public:
    CargoId Cargo()const{return cargo_;}
    CompanyId Source()const{return source_;}
    CompanyId Destination()const{return destination_;}
    std::optional<TrailerId> Trailer()const{return trailer_;}
    PlacementMode Placement()const{return placement_;}
};
inline JobParse ParseJob(const Request& draft){
    if(!draft.cargo||!draft.source||!draft.destination)return RequestError::Incomplete;
    if(draft.source==draft.destination)return RequestError::SameCompany;
    if(draft.owned_trailer)return RequestError::OwnedTrailer;
    return ValidatedJob(draft);
}

struct ParkingSlot { bool freight,empty; };
inline std::optional<size_t> ExistingParking(const std::vector<ParkingSlot>& slots,size_t count,size_t reserved,bool empty_only){
    for(size_t i=0;i<std::min(count,slots.size());++i)
        if(i!=reserved&&slots[i].freight&&(!empty_only||slots[i].empty))return i;
    return std::nullopt;
}
inline std::optional<size_t> SelectParking(const std::vector<ParkingSlot>& slots,size_t count,size_t reserved){
    if(auto empty=ExistingParking(slots,count,reserved,true))return empty;
    if(auto replace=ExistingParking(slots,count,reserved,false))return replace;
    if(count<slots.size()&&count!=reserved&&slots[count].freight)return count;
    return std::nullopt;
}
}
