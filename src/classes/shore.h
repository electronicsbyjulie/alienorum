#ifndef _Shore
#define _Shore

#include "star.h"
#include "planet.h"

namespace alienorum
{
    class CosmicShore
    {
        public:
        static double calculate_escape_velocity(const Planet &p);
        #if 0
        // Bring these back if ever add classic calculation
        static double get_saturation_time(const Star &s);
        static double get_saturation_fraction(const Star &s);
        #endif
        static double calculate_unified_metric(Star &s, Planet &p);
    };
}

#endif