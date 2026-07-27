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

    class Constellation
    {
        //
    };
}

extern std::vector<ConsBoundary> consbounds;

#endif

