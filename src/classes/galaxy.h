
#ifndef _Galaxy
#define _Galaxy

#include "celestial.h"

class Galaxy : public CelestialObject
{
    public:
    Galaxy();
    json to_json();
    bool from_json(json j);
};

#endif