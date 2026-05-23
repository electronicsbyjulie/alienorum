
#include <iostream>
#include <math.h>
#include "moon.h"

Rotation Moon::get_Laplace_plane()
{
    if (Laplace_set) return Laplace_plane;
    if (!orbit || !orbit->center || !orbit->center->orbit || !orbit->center->orbit->center) return location.orbital_plane;

    CelestialObject *myplanet = orbit->center, *mystar = orbit->center->orbit->center;

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

json Moon::to_json()
{
    json towrite = Planet::to_json();

    // These are calculated on the fly; we don't have to write them.
    // towrite["Laplace_plane"] = Laplace_plane.to_json();
    // towrite["Laplace_set"] = Laplace_set;

    return towrite;
}

bool Moon::from_json(json j)
{
    Planet::from_json(j);
    try
    {
        json j1 = j.at("Laplace_plane");
        Laplace_plane.from_json(j1);
    } catch (...) { ; }
    try { j.at("Laplace_set").get_to(Laplace_set); } catch (...) { ; }
    return true;
}
