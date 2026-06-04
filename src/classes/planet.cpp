#include <iostream>
#include <cmath>
#include <iomanip>

#include "planet.h"
#include "star.h"
#include "point.h"

void Planet::estimate_radius()
{
    volumetric_mean_radius = jupiter_radius * pow(mass/jupiter_mass, 0.67);                 // https://doi.org/10.1051/0004-6361/202348690
}

double Planet::viewer_reflectance_magnitude(CelestialLocation seen_from, double phase, double sourcemagn, double sourcedist)
{
    if (!orbit)
    {
        std::cerr << "Called viewer_reflectance_magnitude on an object without a center of orbit." << std::endl;
        throw 0xbadc0de;
    }

    CelestialObject *light_center = get_light_center();

    double r = seen_from.distance_to(location) * invAU;
    double rcen = sourcedist ? sourcedist : (location.distance_to(light_center->location) * invAU);
    double rsq = r*r*rcen*rcen;

    if (phase<0) phase = find_3D_angle(seen_from.local_position, light_center->location.local_position, location.local_position);
    double phabs = fabs(phase);
    amt_lit = fabs(M_PI - fmin(phabs, M_PI*2-phabs)) / M_PI;

    double reflectivity = pow(magnbase, -absolute_magnitude);
    double apparent = reflectivity * amt_lit / rsq;

    /*std::cout << name << ": " << (phase*fiftyseven) << ", " << amt_lit << ", " << reflectivity << ", " << rsq 
        << " = " << (-log(apparent) * invlogmagnbase) << std::endl;*/

    if (sourcemagn < -1000) sourcemagn = light_center->absolute_magnitude;
    return -log(apparent) * invlogmagnbase + sourcemagn - cels[0]->absolute_magnitude;
}

void Planet::estimate_albedo()
{
    double rearths = volumetric_mean_radius / earth_radius;
    double disc_area = rearths * rearths;
    double brightness = pow(magnbase, earth_absmag-absolute_magnitude);
    double a = fmin(1, brightness / disc_area * earth_albedo);
    if (!isnan(a)) albedo = a;
}

void Planet::update_location(double tmnow)
{
    if (orbit && orbit->period) update_orbit_location(tmnow);
}

bool Planet::is_in_con_HZ()
{
    if (cached_in_cons_hz) return cache_in_cons_hz;

    if (!orbit || !orbit->center) return false;
    Star *s = (Star*)get_light_center();
    assert(s->typeclass() == class_star);

    double t_star = s->estimate_temperature() - sun_temp;

    // Baseline calculations for a 1 earth-mass planet
    // Coefficients for runaway greenhouse
    double rg_SeffSun = 1.107;
    double rg_a = 1.332e-4;
    double rg_b = 1.580e-8;
    double rg_c = -8.308e-12;
    double rg_d = -1.931e-15;

    // Coefficients for maximum greenhouse
    double mg_SeffSun = 0.356;
    double mg_a = 6.171e-5;
    double mg_b = 1.698e-9;
    double mg_c = -3.198e-12;
    double mg_d = -5.575e-16;

    // Evaluate quartic polynomial for base values
    double base_inner = rg_SeffSun + (rg_a * t_star) + (rg_b * std::pow(t_star, 2)) +
                        (rg_c * std::pow(t_star, 3)) + (rg_d * std::pow(t_star, 4));

    double base_outer = mg_SeffSun + (mg_a * t_star) + (mg_b * std::pow(t_star, 2)) +
                        (mg_c * std::pow(t_star, 3)) + (mg_d * std::pow(t_star, 4));

    // Planetary mass corrections
    double inner_correction = 0.0;
    double outer_correction = 0.0;

    double planet_mass = mass / earth_mass;
    double inner_limit, outer_limit;

    if (planet_mass >= 1.0 && planet_mass <= 5.0)
    {
        // Upper mass scaling coefficients
        double rg_a_m = 2.972e-2;
        double rg_b_m = -4.619e-3;
        double rg_c_m = 1.151e-4;

        double mg_a_m = 4.417e-3;
        double mg_b_m = -3.151e-3;
        double mg_c_m = 4.549e-4;

        inner_correction = (rg_a_m * t_star) + (rg_b_m * std::pow(t_star, 2)) + (rg_c_m * std::pow(t_star, 3));
        outer_correction = (mg_a_m * t_star) + (mg_b_m * std::pow(t_star, 2)) + (mg_c_m * std::pow(t_star, 3));

        // Apply scaling factor based on mass
        inner_limit = base_inner + (planet_mass - 1.0) * inner_correction; 
        outer_limit = base_outer + (planet_mass - 1.0) * outer_correction;

    }
    else if (planet_mass >= 0.1 && planet_mass < 1.0)
    {
        // Lower mass scaling coefficients
        double rg_a_m = 1.831e-2;
        double rg_b_m = -3.409e-3;
        double rg_c_m = 5.340e-5;

        double mg_a_m = 3.428e-3;
        double mg_b_m = -2.871e-3;
        double mg_c_m = 3.699e-4;

        inner_correction = (rg_a_m * t_star) + (rg_b_m * std::pow(t_star, 2)) + (rg_c_m * std::pow(t_star, 3));
        outer_correction = (mg_a_m * t_star) + (mg_b_m * std::pow(t_star, 2)) + (mg_c_m * std::pow(t_star, 3));

        // Apply scaling factor down to lower limits
        inner_limit = base_inner - (1.0 - planet_mass) * inner_correction;
        outer_limit = base_outer - (1.0 - planet_mass) * outer_correction;
    }
    else
    {
        // If beyond model boundaries, fall back to baseline
        inner_limit = base_inner;
        outer_limit = base_outer;
    }

    // Compute planetary illumination
    double sun_intrinsic = pow(magnbase, -cels[0]->absolute_magnitude);
    double star_intrinsic = pow(magnbase, -s->absolute_magnitude);
    CelestialObject *myplanet = this;
    while (myplanet->orbit && myplanet->orbit->center != s) myplanet = myplanet->orbit->center;
    double sma_au = myplanet->orbit->semimajor_axis / AU;
    double planet_illumination = star_intrinsic / (sun_intrinsic * sma_au * sma_au);            // inverse square of distance

    // Check habitability bounds
    cache_in_cons_hz = (planet_illumination >= outer_limit && planet_illumination <= inner_limit);
    cached_in_cons_hz = true;
    return cache_in_cons_hz;
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
    towrite["surface_pressure"] = surface_pressure;
    towrite["opposition_surge"] = opposition_surge;
    towrite["J2"] = J2;

    return towrite;
}

bool Planet::from_json(json j)
{
    CelestialObject::from_json(j);
    try { j.at("albedo").get_to(albedo); } catch (...) { ; }
    try { j.at("surface_pressure").get_to(surface_pressure); } catch (...) { ; }
    try { j.at("opposition_surge").get_to(opposition_surge); } catch (...) { ; }
    try { j.at("J2").get_to(J2); } catch (...) { ; }
    return true;
}
