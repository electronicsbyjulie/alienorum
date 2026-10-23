
#include <iostream>
#include <algorithm>
#include <math.h>
#include "moon.h"

using namespace alienorum;

static double collision_radius(CelestialObject *object)
{
    double radius = object->get_equatorial_radius();
    if (object->typeclass() == class_moon)
    {
        Moon* moon = static_cast<Moon*>(object);
        radius = std::max(radius, 0.5 * std::max({moon->height, moon->width, moon->depth}));
    }
    return std::max(radius, object->volumetric_mean_radius);
}

double Moon::minimum_safe_pericenter()
{
    if (!orbit || !orbit->center) return 0;
    CelestialObject* parent = orbit->center;
    const double own_radius = collision_radius(this);
    double inner = collision_radius(parent) + own_radius;
    inner = std::max(inner, parent->Roche_limit(this));
    if (parent->typeclass() == class_planet || parent->typeclass() == class_moon)
        inner = std::max(inner, static_cast<Planet*>(parent)->ring_radius + own_radius);
    return 1.1 * inner;
}

bool Moon::enforce_orbit_clearance(bool only_if_touching)
{
    if (!orbit || !orbit->center || !(orbit->semimajor_axis > 0)
        || !std::isfinite(orbit->semimajor_axis) || !std::isfinite(orbit->eccentricity)
        || orbit->eccentricity < 0 || orbit->eccentricity >= 1) return false;
    const double pericenter = orbit->semimajor_axis * (1 - orbit->eccentricity);
    const double contact = collision_radius(orbit->center) + collision_radius(this);
    if (only_if_touching && pericenter > contact + std::max(1.0, contact * 1e-9)) return false;
    const double minimum = minimum_safe_pericenter();
    if (pericenter >= minimum * (1 - 1e-12)) return false;
    const double previous_period = orbit->period;
    orbit->semimajor_axis = minimum / (1 - orbit->eccentricity);
    orbit->compute_period(mass);
    if (previous_period > 0 && fabs(sidereal_rotational_period - previous_period) < 1e-6 * previous_period)
        sidereal_rotational_period = orbit->period;
    Laplace_set = false;
    user_edited = true;
    return true;
}

Rotation Moon::get_Laplace_plane()
{
    if (!orbit || !orbit->center || !orbit->center->orbit || !orbit->center->orbit->center) return location.orbital_plane;
    if (orbit_type == ot_equatorial) return orbit->center->location.equatorial_plane;
    else if (orbit_type == ot_ecliptic) return orbit->center->location.orbital_plane;
    if (Laplace_set && frand(0,1) > 0.25) return Laplace_plane;

    // Fix for minor planets tidally locked to their moons.
    if (fabs(1.0 - (orbit->center->sidereal_rotational_period / orbit->period)) < 0.01) return orbit->center->location.equatorial_plane;

    CelestialObject *myplanet = orbit->center, *mystar = orbit->center->orbit->center;
    // Masses arrive from catalogs and from the object editor, so either can be zero or missing.
    // pmu divides n_ecl below and smu is cubed under a square root, so without this the Laplace
    // plane comes out infinite or NaN and takes the moon's orientation with it. Falling back to
    // the orbital plane is what the guard at the top of this function already does for the other
    // cases where there is not enough information to solve for one.
    if (!(myplanet->mass > 0) || !(mystar->mass > 0)) return location.orbital_plane;

    double pmu = myplanet->mass * G, smu = mystar->mass * G;
    double n = std::sqrt(pmu / std::pow(smu, 3));
    // Was open-coded here as vmr / pow(1-oblateness, 2.0/3) -- the division the right way round
    // but the wrong exponent, and a second copy of a formula that belongs in one place.
    double eqr = myplanet->get_equatorial_radius();
    double n_eq = n * ((Planet*)myplanet)->J2 * std::pow(eqr / orbit->semimajor_axis, 2);
    double n_ecl = n * smu / pmu * std::pow(orbit->semimajor_axis / myplanet->orbit->semimajor_axis, 3);

    double star_influence = n_ecl / (n_ecl+n_eq), planet_influence = 1.0 - star_influence;

    Point ecliptic_pole = yaxis;
    ecliptic_pole = rotate3D(ecliptic_pole, center, myplanet->location.orbital_plane.v, -myplanet->location.orbital_plane.a);

    Point equatorial_pole = yaxis;
    equatorial_pole = rotate3D(equatorial_pole, center, myplanet->location.equatorial_plane.v, -myplanet->location.equatorial_plane.a);
    Point Laplace_pole = ecliptic_pole * star_influence + equatorial_pole * planet_influence;

    Laplace_plane = align_points_3d(Laplace_pole, yaxis, center);
    Laplace_set = true;

    Point oaxis(sin(orbit->ascending_node), 0, cos(orbit->ascending_node));
    Point orbital_pole = rotate3D(Laplace_pole, center, oaxis, orbit->inclination);
    location.orbital_plane = align_points_3d(orbital_pole, yaxis, center);

    Point eqaxis(sin(equinox), 0, cos(equinox));
    Point my_eq_pole = rotate3D(yaxis, center, eqaxis, obliquity);
    my_eq_pole = rotate3D(my_eq_pole, center, Laplace_plane.v, -Laplace_plane.a);
    if (!lock_equatorial_plane) location.equatorial_plane = align_points_3d(my_eq_pole, ecliptic_pole, center);

    return Laplace_plane;
}

void Moon::update_orbit_location(double tmnow)
{
    Rotation plane = get_Laplace_plane();
    CelestialObject::update_orbit_location(tmnow, &plane);
}

Moon::Moon()
{
    _class = class_moon;
}

void Moon::update_location(double tmnow)
{
    if (orbit) update_orbit_location(tmnow);
}

json Moon::to_json()
{
    json towrite = Planet::to_json();

    // These are calculated on the fly; we don't have to write them.
    // towrite["Laplace_plane"] = Laplace_plane.to_json();
    // towrite["Laplace_set"] = Laplace_set;

    if (depth > zero_isnt_really_zero) towrite["depth"] = depth*1e-3;
    if (width > zero_isnt_really_zero) towrite["width"] = width*1e-3;
    if (height > zero_isnt_really_zero) towrite["height"] = height*1e-3;

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
    try { j.at("depth" ).get_to(depth ); depth  *= 1e3; } catch (...) { ; }
    try { j.at("width" ).get_to(width ); width  *= 1e3; } catch (...) { ; }
    try { j.at("height").get_to(height); height *= 1e3; } catch (...) { ; }
    return true;
}
