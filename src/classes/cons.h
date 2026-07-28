#ifndef _Constellation
#define _Constellation

#include "star.h"

namespace alienorum
{
    class ConsBoundary
    {
        public:
        double RA = 0;                  // RADIANS
        double decl = 0;                // RADIANS
    };

    class ConsLine
    {
        public:
        Star *a = nullptr, *b = nullptr;
        std::string starnamea, starnameb;
    };

    class Constellation
    {
        public:
        std::string name, genitive, abbrev;
        std::vector<ConsLine> lines;
        std::vector<ConsBoundary> bounds;
        double RA_center;               // RADIANS
        double decl_center;
        void build_constellation_perimeter();           // Run this after load to translate the catalog's RA ordering into a perimeter order.
        // RADIANS
    };
}

using namespace alienorum;

extern std::vector<Constellation> constellations;
Constellation* identify_cons_of_star(Star* s);
void fill_alienorum_ids();

#endif

