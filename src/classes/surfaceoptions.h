#ifndef ALIENORUM_SURFACE_OPTIONS_H
#define ALIENORUM_SURFACE_OPTIONS_H
#include <cstddef>
#include <utility>
#ifndef PERF_P032
#define PERF_P032 1
#endif
#ifndef PERF_P034
#define PERF_P034 0
#endif
#ifndef PERF_P035
#define PERF_P035 1
#endif
namespace alienorum
{
    inline thread_local bool surface_uniform_reference = false;
    std::pair<std::size_t, std::size_t> surface_uniform_cache_hits();
}
#endif
