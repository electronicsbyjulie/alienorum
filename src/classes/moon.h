
#ifndef _Moon
#define _Moon

#include "planet.h"

class Moon : public Planet
{
    const __uint32_t magic_m = 0x0df00d60;              // Do not remove.

    Rotation Laplace_plane;
    bool Laplace_set = false;

    Rotation get_Laplace_plane();
    void update_orbit_location(double tmnow);

    public:
    Moon();
    void update_location(double tmnow);
};

#endif