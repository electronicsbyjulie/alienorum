
#include <iostream>
#include "moon.h"

Rotation Moon::get_Laplace_plane()
{
    if (Laplace_set) return Laplace_plane;
    if (!orbit || !orbit->center || !orbit->center->orbit || !orbit->center->orbit->center) return location.orbital_plane;

    CelestialObject *myplanet = orbit->center, *mystar = orbit->center->orbit->center;
    double planet_gravitational_pull = myplanet->mass / (orbit->semimajor_axis*orbit->semimajor_axis);
    double star_gravitational_pull = mystar->mass / (myplanet->orbit->semimajor_axis*myplanet->orbit->semimajor_axis);
    double planet_influence = (planet_gravitational_pull/(planet_gravitational_pull+star_gravitational_pull*1e4));      // no idea why this 1e4 is necessary
    double star_influence = 1.0 - planet_influence;

    Point ecliptic_pole = rotate3D(yaxis, center, myplanet->location.orbital_plane.v, myplanet->location.orbital_plane.a);
    Point equatorial_pole = rotate3D(yaxis, center, myplanet->location.equatorial_plane.v, myplanet->location.equatorial_plane.a);
    Point Laplace_pole = ecliptic_pole * star_influence + equatorial_pole * planet_influence;

    Laplace_plane = align_points_3d(Laplace_pole, yaxis, center);
    Laplace_set = true;

    return Laplace_plane;
}

void Moon::update_orbit_location(double tmnow)
{
    std::cout << "Moon::update_orbit_location() called for " << name << std::endl;
    get_Laplace_plane();
    return CelestialObject::update_orbit_location(tmnow, &Laplace_plane);
}

void Moon::update_location(double tmnow)
{
    update_orbit_location(tmnow);
}
