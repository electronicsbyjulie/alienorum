#include <iostream>
#include <cmath>
#include <iomanip>

#include "planet.h"
#include "point.h"

double Planet::viewer_reflectance_magnitude(CelestialLocation seen_from)
{
    if (!orbit)
    {
        std::cerr << "Called viewer_reflectance_magnitude on an object without a center of orbit." << std::endl;
        throw 0xbadc0de;
    }

    CelestialObject *light_center = orbit->center;
    int i;
    for (i=0; i<5; i++)
    {
        if (light_center->type == rocky || light_center->type == ice_giant || light_center->type == gas_giant)
        {
            light_center = light_center->orbit->center;
            if (!light_center)
            {
                std::cerr << "Cannot find light source for " << name << std::endl;
                throw 0xbadda7a;
            }
            // std::cout << "Light source for " << name << " is " << light_center->name << std::endl;
        }
        else break;
    }

    double r = seen_from.distance_to(location) * invAU;
    double rcen = location.distance_to(light_center->location) * invAU;
    double rsq = r*r*rcen*rcen;

    double phase = find_3D_angle(seen_from.local_position, light_center->location.local_position, location.local_position);
    double phabs = fabs(phase);
    amt_lit = fabs(M_PI - fmin(phabs, M_PI*2-phabs)) / M_PI;

    double reflectivity = pow(magnbase, -absolute_magnitude);
    double apparent = reflectivity * amt_lit / rsq;

    /*std::cout << name << ": " << (phase*fiftyseven) << ", " << amt_lit << ", " << reflectivity << ", " << rsq 
        << " = " << (-log(apparent) * invlogmagnbase) << std::endl;*/

    return -log(apparent) * invlogmagnbase;
}

void Planet::update_location(double tmnow)
{
    update_orbit_location(tmnow);
}

Planet::Planet()
{
    _class = class_planet;
    BV_color = 1;
    UB_color = 0.5;
}

json Planet::to_json()
{
    json towrite = CelestialObject::to_json();

    if (albedo) towrite["albedo"] = albedo;
    towrite["period"] = period/86400;
    towrite["surface_pressure"] = surface_pressure;
    towrite["opposition_surge"] = opposition_surge;
    towrite["J2"] = J2;

    return towrite;
}

bool Planet::from_json(json j)
{
    CelestialObject::from_json(j);
    try { j.at("albedo").get_to(albedo); } catch (...) { ; }
    try { j.at("period").get_to(period); period *= 86400; } catch (...) { ; }
    try { j.at("surface_pressure").get_to(surface_pressure); } catch (...) { ; }
    try { j.at("opposition_surge").get_to(opposition_surge); } catch (...) { ; }
    try { j.at("J2").get_to(J2); } catch (...) { ; }
    return true;
}
