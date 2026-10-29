#ifndef ALIENORUM_SURFACE_PREFETCH_H
#define ALIENORUM_SURFACE_PREFETCH_H

#include <chrono>
#include <cstddef>
#include <utility>
#include <cmath>
#include <functional>

namespace alienorum
{
    // Main-thread idle work. The caller protects catalog/cache lifetimes.
    class SurfacePrefetchQueue
    {
        std::function<bool()> valid, step;
        public:
        bool queued() const { return static_cast<bool>(step); }
        void clear()
        {
            valid = {};
            step = {};
        }
        void schedule(std::function<bool()> validation, std::function<bool()> next)
        {
            valid = std::move(validation);
            step = std::move(next);
        }
        std::size_t run_for(double milliseconds)
        {
            if (!(milliseconds > 0) || !std::isfinite(milliseconds) || !step) return 0;
            if (!valid || !valid())
            {
                clear();
                return 0;
            }
            using Clock = std::chrono::steady_clock;
            const auto deadline = Clock::now() + std::chrono::duration<double, std::milli>(milliseconds);
            std::size_t count = 0;
            while (step && Clock::now() < deadline)
            {
                const bool more = step();
                ++count;
                if (!more) clear();
            }
            return count;
        }
    };
}
#endif
