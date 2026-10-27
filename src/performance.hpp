#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
namespace jd::perf {
struct Counter {
    std::atomic<std::uint64_t> count{0}, total_us{0}, worst_us{0};
    void Add(std::uint64_t value) {
        count.fetch_add(1,std::memory_order_relaxed);total_us.fetch_add(value,std::memory_order_relaxed);
        auto old=worst_us.load(std::memory_order_relaxed);
        while(old<value&&!worst_us.compare_exchange_weak(old,value,std::memory_order_relaxed)){}
    }
};
inline Counter adapter, overlay;
struct Scope {
    Counter& counter;
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    explicit Scope(Counter& value):counter(value){}
    ~Scope(){counter.Add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count()));}
};
}
