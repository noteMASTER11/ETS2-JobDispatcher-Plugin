#pragma once
#include <cstddef>
#include <cstdint>

namespace jd::domain {
// Store owns the market array. This transaction temporarily owns its displaced
// offer and a local reference to the new offer until acceptance is settled.
template<class Store> class OfferTransaction {
    Store& store_;
    size_t slot_;
    uint64_t count_;
    uintptr_t job_,previous_;
    bool active_=true;
public:
    OfferTransaction(Store& store,size_t slot,uintptr_t job):store_(store),slot_(slot),count_(store.Count()),
        job_(job),previous_(slot<count_?store.ReadSlot(slot):0){
        store_.Retain(job_);
        store_.WriteSlot(slot_,job_);
        if(slot_==count_)store_.SetCount(count_+1);
        store_.Refresh();
    }
    OfferTransaction(const OfferTransaction&)=delete;
    OfferTransaction& operator=(const OfferTransaction&)=delete;
    ~OfferTransaction(){Rollback();}
    void Commit(){
        if(!active_)return;
        if(previous_)store_.Dispose(previous_);
        store_.Release(job_);active_=false;
    }
    void Rollback(){
        if(!active_)return;
        const auto current_count=store_.Count();
        if(slot_<current_count&&store_.ReadSlot(slot_)==job_){
            store_.WriteSlot(slot_,previous_);
            if(slot_==count_&&current_count==count_+1)store_.SetCount(count_);
            store_.Dispose(job_);
        }else if(previous_)store_.Dispose(previous_);
        store_.Release(job_);active_=false;
    }
};
}
