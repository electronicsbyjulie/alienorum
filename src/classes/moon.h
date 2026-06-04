
#ifndef _Moon
#define _Moon

#include "planet.h"

class Moon : public Planet
{
    Rotation Laplace_plane;
    bool Laplace_set = false;

    Rotation get_Laplace_plane();
    void update_orbit_location(double tmnow);

    public:
    double height, width, depth;        // Height = pole-pole distance; width = diameter along direction of orbit; depth = dia. toward/away from planet.

    Moon();
    void update_location(double tmnow);
    json to_json();
    bool from_json(json j);
};

#endif