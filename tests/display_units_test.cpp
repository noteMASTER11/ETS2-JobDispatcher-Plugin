#include "display_units.hpp"
#include "game_messages.hpp"
#include <cassert>
#include <cmath>
int main(){
    using namespace jd::units;
    assert(std::abs(Distance(1609.344,true)-1)<1e-9);
    assert(Distance(1000,false)==1);
    assert(std::abs(Mass(0.45359237,"lb")-1)<1e-9);
    assert(Mass(25000,"t")==25);
    assert(std::abs(Mass(907.18474,"st")-1)<1e-9);
    assert(Number(25.45,true,1)=="25,4"||Number(25.45,true,1)=="25,5");
    assert(Number(2205.852,false)=="2206");
    assert(RealMinutes(2052000,19)==72);
    assert(RealMinutes(135000,15)==6);
    assert(RealMinutes(4500,3)==1);
    assert(RealMinutes(1000,0)==-1);
    assert(jd::GameMessage("The game returned a road route outside the supported job field limits.")=="@@jd_route_failed@@");
    assert(jd::GameMessage("There is no unreserved freight parking slot at this source company.")=="@@mp_job_loading_placement_not_found@@");
    assert(jd::GameMessage("Заказ добавлен")=="@@jd_offer@@");
}
