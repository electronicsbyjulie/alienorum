
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
    Moon();
    void update_location(double tmnow);
    json to_json();
    bool from_json(json j);
};

#endif