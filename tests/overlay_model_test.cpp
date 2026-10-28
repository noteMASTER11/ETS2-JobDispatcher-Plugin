#include "overlay_model.hpp"
#include "preview_cache.hpp"
#include <cassert>
#include <iostream>

int main() {
    const std::set<std::string> visible={"trailer.flatbed|cargo.excavator","trailer.flatbed|cargo.transformat"};
    assert(jd::preview::CacheGeometryKey("trailer.van|cargo.cheese|53",visible)==
           jd::preview::CacheGeometryKey("trailer.van|cargo.butter|31",visible));
    assert(jd::preview::CacheGeometryKey("trailer.van|cargo.cheese|53",visible)!=
           jd::preview::CacheGeometryKey("trailer.tank|cargo.cheese|53",visible));
    assert(jd::preview::CacheGeometryKey("trailer.flatbed|cargo.excavator|1",visible)!=
           jd::preview::CacheGeometryKey("trailer.flatbed|cargo.transformat|1",visible));
    assert(jd::preview::CacheGeometryKey("trailer.flatbed|cargo.excavator|1",visible)!=
           jd::preview::CacheGeometryKey("trailer.flatbed|cargo.excavator|2",visible));
    assert(jd::preview::CacheGeometryKey("",visible).empty());
    // Preview identity is always the loaded trailer, including cargo and count.
    jd::Snapshot previews;
    previews.cargos = {{1,"Excavator","cargo.excavator",10}, {2,"Transformer","cargo.transformat",10},
                       {3,"Apples","cargo.apples",10}, {4,"Feldbinder","cargo.feldbinder",20,0,"",true}};
    previews.trailers = {{10,"Flatbed","trailer_def.flatbed",0,0,"trailer.flatbed"},
                         {20,"Feldbinder","trailer_def.feldbinder",0,0,"trailer.ffb_tsalm32"}};
    jd::Request preview_request;
    preview_request.cargo=1;
    previews.selection=preview_request;previews.cargo_units=1;
    assert(jd::overlay::model::PreviewToken(previews,preview_request)=="trailer.flatbed|cargo.excavator|1");
    preview_request.cargo=2;
    assert(jd::overlay::model::PreviewToken(previews,preview_request).empty());
    previews.selection=preview_request;
    assert(jd::overlay::model::PreviewToken(previews,preview_request)=="trailer.flatbed|cargo.transformat|1");
    // Dropdown and Surprise replace the same request fields. Both must update
    // the composite key even when the automatic trailer stays unchanged.
    preview_request.trailer=20;
    previews.selection=preview_request;
    assert(jd::overlay::model::PreviewToken(previews,preview_request)=="trailer.ffb_tsalm32|cargo.transformat|1");
    preview_request.destination=99;
    assert(jd::overlay::model::PreviewToken(previews,preview_request)=="trailer.ffb_tsalm32|cargo.transformat|1");
    preview_request.trailer=0;
    preview_request.cargo=3;
    previews.selection=preview_request;previews.cargo_units=20;
    assert(jd::overlay::model::PreviewToken(previews,preview_request)=="trailer.flatbed|cargo.apples|20");
    preview_request.cargo=4;
    previews.selection=preview_request;previews.cargo_units=1;
    assert(jd::overlay::model::PreviewToken(previews,preview_request)=="trailer.ffb_tsalm32|cargo.feldbinder|1");
    std::cout << "Loaded trailer preview follows cargo, trailer and quantity\n";
    jd::Snapshot state;
    state.supported = state.world_ready = state.can_generate = true;
    state.cargos = {{11, "Heavy machinery", "heavy_machine"}, {12, "Apples", "apples"}};
    state.companies = {{21, "Berlin - Depot", "berlin.depot"}, {22, "Paris - Depot", "paris.depot"}};
    state.trailers = {{31, "Flatbed", "flatbed"}};
    state.nearest_company = 22;
    jd::Request request;
    jd::overlay::model::Normalize(state, request);
    assert(request.cargo == 11 && request.trailer == 0);
    assert(request.source == 22 && request.destination == 21);
    assert(jd::overlay::model::CanSubmit(state, request));
    // Auto is not an arbitrary first trailer and survives catalogue refreshes.
    state.trailers.insert(state.trailers.begin(), {99, "Unrelated refrigerated trailer", "reefer"});
    jd::overlay::model::Normalize(state, request);
    assert(request.trailer == 0);
    assert(jd::overlay::model::CanSubmit(state, request));
    // An explicit loaded trailer remains selected, even after changing cargo.
    request.trailer = 31;
    request.cargo = 12;
    jd::overlay::model::Normalize(state, request);
    assert(request.trailer == 31 && request.cargo == 12);
    assert(jd::overlay::model::CanSubmit(state, request));
    // Unknown manual identifiers cannot be submitted; normalization returns to Auto.
    request.trailer = 404;
    assert(!jd::overlay::model::CanSubmit(state, request));
    jd::overlay::model::Normalize(state, request);
    assert(request.trailer == 0 && jd::overlay::model::CanSubmit(state, request));
    // Only the supported company-trailer / immediate-acceptance mode is exposed.
    request.owned_trailer = true;
    assert(!jd::overlay::model::CanSubmit(state, request));
    request.owned_trailer = false;
    request.take_immediately = false;
    assert(jd::overlay::model::CanSubmit(state, request));
    jd::overlay::model::Normalize(state, request);
    assert(!request.owned_trailer && !request.take_immediately);
    assert(jd::overlay::model::Matches(state.cargos[0], "MACHINERY"));
    assert(jd::overlay::model::Matches(state.cargos[0], "heavy_machine"));
    assert(!jd::overlay::model::Matches(state.cargos[0], "apples"));
    const jd::Choice cheese{50, u8"Сыр", "cheese"};
    assert(jd::overlay::model::Matches(cheese, u8"сыр"));
    assert(jd::overlay::model::Matches(cheese, u8"СЫР"));
    assert(jd::overlay::model::Matches(cheese, "CHEESE"));
    assert(!jd::overlay::model::Matches(cheese, u8"суп"));
    assert(jd::overlay::model::Matches({51, u8"Ёлки", "trees"}, u8"ёлКИ"));
    assert(jd::overlay::model::Matches({52, u8"Schwarzmüller", "brand"}, u8"SCHWARZMÜLLER"));
    assert(jd::overlay::model::Matches({53, u8"東京", "tokyo"}, u8"東京"));
    assert(jd::overlay::model::Matches(cheese, nullptr));
    assert(jd::overlay::model::FoldSearch(std::string("\xd0", 1)) == std::string("\xd0", 1));
    state.busy = true;
    assert(!jd::overlay::model::CanSubmit(state, request));
    state.busy = false;
    state.supported = false;
    assert(!jd::overlay::model::CanSubmit(state, request));
    state.supported = true;
    request.destination = request.source;
    assert(!jd::overlay::model::CanSubmit(state, request));
    jd::overlay::model::Normalize(state, request);
    assert(request.destination == 21);
    // A profile/catalogue change must discard pointers from the old session.
    state.cargos = {{101, "New cargo", "new"}};
    state.companies = {{201, "New source", "source"}, {202, "New destination", "dest"}};
    state.trailers = {{301, "New trailer", "trailer"}};
    state.nearest_company = 0;
    request.trailer = 31;
    jd::overlay::model::Normalize(state, request);
    assert(request.cargo == 101 && request.source == 201 && request.destination == 202 && request.trailer == 0);
    assert(jd::overlay::model::CanSubmit(state, request));
    // Auto remains eligible when the adapter can choose a live prototype even if
    // the UI's optional manual list is empty; readiness is the adapter's decision.
    state.trailers.clear();
    assert(jd::overlay::model::CanSubmit(state, request));
    state.world_ready = false;
    jd::overlay::model::Normalize(state, request);
    assert(request.cargo == 0 && request.source == 0 && request.destination == 0 && request.trailer == 0);
    assert(!jd::overlay::model::CanSubmit(state, request));
    std::cout << "overlay model: all checks passed\n";
}
