
#include <iostream>
#include <math.h>
#include "moon.h"

Rotation Moon::get_Laplace_plane()
{
    if (Laplace_set) return Laplace_plane;
    if (!orbit || !orbit->center || !orbit->center->orbit || !orbit->center->orbit->center) return location.orbital_plane;

    CelestialObject *myplanet = orbit->center, *mystar = orbit->center->orbit->center;
    /* double planet_gravitational_pull = myplanet->mass / (orbit->semimajor_axis*orbit->semimajor_axis);
    double star_gravitational_pull = mystar->mass / (myplanet->orbit->semimajor_axis*myplanet->orbit->semimajor_axis);
    double planet_influence = (planet_gravitational_pull/(planet_gravitational_pull+star_gravitational_pull*1e4));      // no idea why this 1e4 is necessary
    double star_influence = 1.0 - planet_influence; */

    double pmu = myplanet->mass * G, smu = mystar->mass * G;
    double n = std::sqrt(pmu / std::pow(smu, 3));
    double eqr = myplanet->volumetric_mean_radius / std::pow(1.0-myplanet->oblateness, 2.0/3);
    double n_eq = n * ((Planet*)myplanet)->J2 * std::pow(eqr / orbit->semimajor_axis, 2);
    double n_ecl = n * smu / pmu * std::pow(orbit->semimajor_axis / myplanet->orbit->semimajor_axis, 3);

    double star_influence = n_ecl / (n_ecl+n_eq), planet_influence = 1.0 - star_influence;

    Point ecliptic_pole = rotate3D(yaxis, center, myplanet->location.local_system_plane.v, -myplanet->location.local_system_plane.a);
    ecliptic_pole = rotate3D(ecliptic_pole, center, myplanet->location.orbital_plane.v, -myplanet->location.orbital_plane.a);
    Point equatorial_pole = rotate3D(yaxis, center, myplanet->location.local_system_plane.v, -myplanet->location.local_system_plane.a);
    equatorial_pole = rotate3D(equatorial_pole, center, myplanet->location.orbital_plane.v, -myplanet->location.orbital_plane.a);
    equatorial_pole = rotate3D(equatorial_pole, center, myplanet->location.equatorial_plane.v, -myplanet->location.equatorial_plane.a);
    Point Laplace_pole = ecliptic_pole * star_influence + equatorial_pole * planet_influence;

    Laplace_plane = align_points_3d(Laplace_pole, yaxis, center);
    Laplace_set = true;

    Point oaxis(sin(orbit->ascending_node), 0, cos(orbit->ascending_node));
    Point orbital_pole = rotate3D(Laplace_pole, center, oaxis, orbit->inclination);
    location.orbital_plane = align_points_3d(orbital_pole, yaxis, center);

    Point eqaxis(sin(equinox), 0, cos(equinox));
    Point my_eq_pole = rotate3D(yaxis, center, eqaxis, inclination);
    my_eq_pole = rotate3D(my_eq_pole, center, Laplace_plane.v, -Laplace_plane.a);
    location.equatorial_plane = align_points_3d(my_eq_pole, ecliptic_pole, center);

    return Laplace_plane;
}

void Moon::update_orbit_location(double tmnow)
{
    // std::cout << "Moon::update_orbit_location() called for " << name << std::endl;
    get_Laplace_plane();
    return CelestialObject::update_orbit_location(tmnow, &Laplace_plane);
}

Moon::Moon()
{
    _class = class_moon;
}

void Moon::update_location(double tmnow)
{
    update_orbit_location(tmnow);
}
