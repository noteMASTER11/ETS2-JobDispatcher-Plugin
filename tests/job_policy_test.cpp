#include "job_policy.hpp"
#include <cassert>
#include <iostream>
#include <type_traits>
int main(){
    using namespace jd::domain;
    static_assert(!std::is_convertible_v<CargoId,CompanyId>);
    static_assert(!std::is_constructible_v<ValidatedJob>);
    jd::Request draft;draft.cargo=1;draft.source=2;draft.destination=3;
    auto parsed=ParseJob(draft);
    assert(std::holds_alternative<ValidatedJob>(parsed));
    const auto& job=std::get<ValidatedJob>(parsed);
    assert(job.Cargo().Value()==1&&job.Source().Value()==2&&!job.Trailer());
    assert(job.Placement()==PlacementMode::Immediate);
    draft.take_immediately=false;draft.trailer=4;
    parsed=ParseJob(draft);
    assert(std::get<ValidatedJob>(parsed).Trailer()->Value()==4);
    assert(std::get<ValidatedJob>(parsed).Placement()==PlacementMode::MarketOffer);
    draft.destination=2;assert(std::get<RequestError>(ParseJob(draft))==RequestError::SameCompany);
    draft.destination=3;draft.cargo=0;assert(std::get<RequestError>(ParseJob(draft))==RequestError::Incomplete);
    draft.cargo=1;draft.owned_trailer=true;assert(std::get<RequestError>(ParseJob(draft))==RequestError::OwnedTrailer);
    std::vector<ParkingSlot> slots{{true,false},{true,true},{false,true},{true,false}};
    assert(SelectParking(slots,4,99)==1); // Empty freight slot first.
    assert(SelectParking(slots,4,1)==0); // Preserve a reserved slot.
    assert(SelectParking(slots,0,99)==0); // Append only into a valid freight slot.
    assert(!SelectParking(slots,0,0)); // A reserved append position is unavailable.
    assert(!SelectParking({{false,true}},1,99));
    assert(!SelectParking({},0,99));
    std::cout<<"Validated jobs, typed identities, placement modes and parking selection passed\n";
}
