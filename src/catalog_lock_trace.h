#ifndef ALIENORUM_CATALOG_LOCK_TRACE_H
#define ALIENORUM_CATALOG_LOCK_TRACE_H

#include "performance_options.h"
#include <atomic>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <ostream>

namespace alienorum
{
    enum class CatalogLockSite { frame, publication, input, count };
    struct CatalogLockSample
    {
        uint64_t acquisitions, wait_ns, hold_ns, maximum_wait_ns, maximum_hold_ns;
    };
    struct CatalogLockCounter
    {
        std::atomic<uint64_t> acquisitions{0}, wait_ns{0}, hold_ns{0};
        std::atomic<uint64_t> maximum_wait_ns{0}, maximum_hold_ns{0};
    };
    inline std::array<CatalogLockCounter, static_cast<size_t>(CatalogLockSite::count)> catalog_lock_counters;

    inline CatalogLockSample catalog_lock_sample(CatalogLockSite site)
    {
        const auto& counter = catalog_lock_counters[static_cast<size_t>(site)];
        return {counter.acquisitions.load(), counter.wait_ns.load(), counter.hold_ns.load(),
            counter.maximum_wait_ns.load(), counter.maximum_hold_ns.load()};
    }

    template<bool Trace> class CatalogScopeLock : public std::unique_lock<std::recursive_mutex>
    {
        using Base = std::unique_lock<std::recursive_mutex>;
        using Clock = std::chrono::steady_clock;
        CatalogLockSite site;
        Clock::time_point held;
        bool traced = false;
        static void maximum(std::atomic<uint64_t>& target, uint64_t value)
        {
            auto previous = target.load(std::memory_order_relaxed);
            while (previous < value && !target.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {}
        }
        static uint64_t nanos(Clock::duration duration)
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        }
        public:
        explicit CatalogScopeLock(std::recursive_mutex& mutex, CatalogLockSite source = CatalogLockSite::publication)
            : Base(mutex, std::defer_lock), site(source)
        {
            lock();
        }
        ~CatalogScopeLock()
        {
            if (Base::owns_lock()) unlock();
        }
        void lock()
        {
            traced = Trace && performance_enabled(18);
            const auto began = traced ? Clock::now() : Clock::time_point{};
            Base::lock();
            if (traced)
            {
                held = Clock::now();
                auto& counter = catalog_lock_counters[static_cast<size_t>(site)];
                const auto wait = nanos(held - began);
                counter.acquisitions.fetch_add(1, std::memory_order_relaxed);
                counter.wait_ns.fetch_add(wait, std::memory_order_relaxed);
                maximum(counter.maximum_wait_ns, wait);
            }
        }
        void unlock()
        {
            if (traced)
            {
                const auto duration = nanos(Clock::now() - held);
                auto& counter = catalog_lock_counters[static_cast<size_t>(site)];
                counter.hold_ns.fetch_add(duration, std::memory_order_relaxed);
                maximum(counter.maximum_hold_ns, duration);
            }
            Base::unlock();
            traced = false;
        }
    };

    template<> class CatalogScopeLock<false> : public std::unique_lock<std::recursive_mutex>
    {
        public:
        explicit CatalogScopeLock(std::recursive_mutex& mutex, CatalogLockSite = CatalogLockSite::publication)
            : std::unique_lock<std::recursive_mutex>(mutex) {}
    };
    using CatalogLock = CatalogScopeLock<(PERF_P018 != 0)>;

    inline void print_catalog_lock_trace(std::ostream& stream)
    {
        if (!performance_enabled(18)) return;
        for (size_t index = 0; index < static_cast<size_t>(CatalogLockSite::count); ++index)
        {
            const auto sample = catalog_lock_sample(static_cast<CatalogLockSite>(index));
            stream << "Catalog lock site=" << index << " acquisitions=" << sample.acquisitions
                << " wait_ns=" << sample.wait_ns << " hold_ns=" << sample.hold_ns
                << " max_wait_ns=" << sample.maximum_wait_ns << " max_hold_ns=" << sample.maximum_hold_ns << '\n';
        }
    }
}
#endif
