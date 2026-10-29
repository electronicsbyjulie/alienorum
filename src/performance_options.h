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
#ifndef PERF_P006
#define PERF_P006 1
#endif
#ifndef PERF_P007
#define PERF_P007 1
#endif
#ifndef PERF_P008
#define PERF_P008 0
#endif
#ifndef PERF_P012
#define PERF_P012 0
#endif
#ifndef PERF_P013
#define PERF_P013 0
#endif
#ifndef PERF_P018
#define PERF_P018 0
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
#ifndef PERF_P025
#define PERF_P025 0
#endif
#ifndef PERF_P026
#define PERF_P026 0
#endif
#ifndef PERF_P028
#define PERF_P028 0
#endif
#ifndef PERF_P029
#define PERF_P029 1
#endif
#ifndef PERF_P030
#define PERF_P030 0
#endif
#ifndef PERF_P031
#define PERF_P031 1
#endif
#ifndef PERF_P036
#define PERF_P036 0
#endif
#ifndef SURFACE_PREFETCH_BUDGET_MS
#define SURFACE_PREFETCH_BUDGET_MS 0.25
#endif
#ifndef PERF_P037
#define PERF_P037 0
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
#ifndef PERF_P043
#define PERF_P043 0
#endif
#ifndef PERF_P049
#define PERF_P049 1
#endif
#ifndef PERF_P068
#define PERF_P068 1
#endif
#ifndef PERF_P067
#define PERF_P067 1
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
            case 6: compiled = PERF_P006; break;
            case 7: compiled = PERF_P007; break;
            case 8: compiled = PERF_P008; break;
            case 12: compiled = PERF_P012; break;
            case 13: compiled = PERF_P013; break;
            case 18: compiled = PERF_P018; break;
            case 20: compiled = PERF_P020; break;
            case 21: compiled = PERF_P021; break;
            case 22: compiled = PERF_P022; break;
            case 23: compiled = PERF_P023; break;
            case 24: compiled = PERF_P024; break;
            case 25: compiled = PERF_P025; break;
            case 26: compiled = PERF_P026; break;
            case 28: compiled = PERF_P028; break;
            case 29: compiled = PERF_P029; break;
            case 30: compiled = PERF_P030; break;
            case 31: compiled = PERF_P031; break;
            case 36: compiled = PERF_P036; break;
            case 37: compiled = PERF_P037; break;
            case 39: compiled = PERF_P039; break;
            case 40: compiled = PERF_P040; break;
            case 41: compiled = PERF_P041; break;
            case 42: compiled = PERF_P042; break;
            case 43: compiled = PERF_P043; break;
            case 49: compiled = PERF_P049; break;
            case 50: compiled = PERF_P050; break;
            case 51: compiled = PERF_P051; break;
            case 52: compiled = PERF_P052; break;
            case 68: compiled = PERF_P068; break;
            case 67: compiled = PERF_P067; break;
            default: break;
        }
#if defined(ALIENORUM_PERFORMANCE_BENCHMARK) && !defined(ALIENORUM_PERFORMANCE_FIXED)
        return compiled && id < performance_reference.size() && !performance_reference[id];
#else
        return compiled;
#endif
    }
}
#endif
