#ifndef ALIENORUM_PERFORMANCE_OPTIONS_H
#define ALIENORUM_PERFORMANCE_OPTIONS_H

#include <array>

// Production switches; benchmark builds can compare each original path independently.
#ifndef PERF_P001
#define PERF_P001 1
#endif
#ifndef PERF_P002
#define PERF_P002 1
#endif
#ifndef PERF_P003
#define PERF_P003 1
#endif
#ifndef PERF_P004
#define PERF_P004 1
#endif
#ifndef PERF_P005
#define PERF_P005 1
#endif
#ifndef PERF_P020
#define PERF_P020 1
#endif
#ifndef PERF_P021
#define PERF_P021 1
#endif
#ifndef PERF_P022
#define PERF_P022 1
#endif
#ifndef PERF_P023
#define PERF_P023 1
#endif
#ifndef PERF_P024
#define PERF_P024 1
#endif
#ifndef PERF_P031
#define PERF_P031 1
#endif
#ifndef PERF_P039
#define PERF_P039 1
#endif
#ifndef PERF_P040
#define PERF_P040 1
#endif
#ifndef PERF_P041
#define PERF_P041 1
#endif
#ifndef PERF_P042
#define PERF_P042 0
#endif
#ifndef PERF_P050
#define PERF_P050 1
#endif
#ifndef PERF_P051
#define PERF_P051 1
#endif
#ifndef PERF_P052
#define PERF_P052 1
#endif

namespace alienorum
{
    inline std::array<bool, 118> performance_reference = {};
    inline bool performance_enabled(unsigned int id)
    {
        bool compiled = true;
        switch (id)
        {
            case 1: compiled = PERF_P001; break;
            case 2: compiled = PERF_P002; break;
            case 3: compiled = PERF_P003; break;
            case 4: compiled = PERF_P004; break;
            case 5: compiled = PERF_P005; break;
            case 20: compiled = PERF_P020; break;
            case 21: compiled = PERF_P021; break;
            case 22: compiled = PERF_P022; break;
            case 23: compiled = PERF_P023; break;
            case 24: compiled = PERF_P024; break;
            case 31: compiled = PERF_P031; break;
            case 39: compiled = PERF_P039; break;
            case 40: compiled = PERF_P040; break;
            case 41: compiled = PERF_P041; break;
            case 42: compiled = PERF_P042; break;
            case 50: compiled = PERF_P050; break;
            case 51: compiled = PERF_P051; break;
            case 52: compiled = PERF_P052; break;
            default: break;
        }
#ifdef ALIENORUM_PERFORMANCE_BENCHMARK
        return compiled && id < performance_reference.size() && !performance_reference[id];
#else
        return compiled;
#endif
    }
}
#endif
