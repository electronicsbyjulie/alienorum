
#ifndef _Galaxy
#define _Galaxy

#include "celestial.h"

class Galaxy : public CelestialObject
{
    public:
    Galaxy();
    json to_json();
};

#endif