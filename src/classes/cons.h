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
    };

    Constellation* identify_cons_of_star(Star* s);
}

using namespace alienorum;

extern std::vector<Constellation> constellations;

#endif

