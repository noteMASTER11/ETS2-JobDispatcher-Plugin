#include "offer_transaction.hpp"
#include <cassert>
#include <map>
#include <vector>
#include <iostream>
struct Market {
    std::vector<uintptr_t> slots{10};
    uint64_t count=1;
    std::map<uintptr_t,int> refs{{10,1},{20,1}};
    unsigned refreshes=0;
    uintptr_t ReadSlot(size_t slot){return slots.at(slot);}
    void WriteSlot(size_t slot,uintptr_t value){if(slot>=slots.size())slots.resize(slot+1);slots[slot]=value;}
    uint64_t Count(){return count;}
    void SetCount(uint64_t value){count=value;}
    void Retain(uintptr_t unit){++refs[unit];}
    void Release(uintptr_t unit){assert(refs[unit]>0);--refs[unit];}
    void Dispose(uintptr_t unit){Release(unit);}
    void Refresh(){++refreshes;}
};
int main(){
    using jd::domain::OfferTransaction;
    {Market market;{OfferTransaction transaction(market,0,20);assert(market.slots[0]==20&&market.refs[20]==2);}
     assert(market.slots[0]==10&&market.refs[10]==1&&market.refs[20]==0&&market.count==1);}
    {Market market;{OfferTransaction transaction(market,0,20);transaction.Commit();transaction.Commit();}
     assert(market.slots[0]==20&&market.refs[10]==0&&market.refs[20]==1&&market.refreshes==1);}
    {Market market;{OfferTransaction transaction(market,1,20);assert(market.count==2);market.slots.reserve(100);}
     assert(market.count==1&&market.slots[0]==10&&market.slots[1]==0&&market.refs[20]==0);}
    {Market market;{OfferTransaction transaction(market,0,20);market.Release(20);market.slots[0]=30;market.refs[30]=1;}
     assert(market.slots[0]==30&&market.refs[30]==1&&market.refs[10]==0&&market.refs[20]==0);}
    std::cout<<"Offer publication, commit, rollback, reallocation and reference ownership passed\n";
}
