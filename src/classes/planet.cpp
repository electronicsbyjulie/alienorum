#include <iostream>
#include <cmath>
#include <iomanip>

#include "planet.h"
#include "star.h"
#include "point.h"

using namespace alienorum;

std::vector<AstorbRow> astorb;

void Planet::classify()
{
    classify(is_in_con_HZ());
}

void Planet::set_color_from_type(bool HZ)
{
    if (type == gas_giant) BV_color = 0.98;         // average of Jupiter and Saturn.
    else if (type == rocky)
    {
        if (HZ) BV_color = 0.2;                     // estimate same as Earth.
        else BV_color = 1;
    }
    else if (type == hot_jupiter) BV_color = -1;    // https://en.wikipedia.org/wiki/HD_189733_b#/media/File:HD_189733b_blue_planet.png with universal B-V correction added.
    else if (type == ice_giant) BV_color = 0.49;    // average of Uranus and Neptune.
    else if (type == icy) BV_color = 0.6;
    else if (type == steam_giant) BV_color = 0.4;
    else if (type == lavaworld) BV_color = 1.3;
    else if (type == waterworld) BV_color = -0.3;
}

void Planet::classify(bool HZ)
{
    double T = estimate_surface_temperature();
    if (mass < rocky_mass_cutoff)
    {
        if (T > lava_T_cutoff) type = lavaworld;
        else type = rocky;
    }
    else if (mass < neptune_mass_cutoff)
    {
        if (T < icy_T_cutoff) type = icy;
        else if (HZ) type = waterworld;
        else type = steam_giant;
    }
    else if (orbit->period < oneday*10) type = hot_jupiter;
    else if (T < icy_T_cutoff) type = ice_giant;
    else if (HZ) type = steam_giant;
    else type = gas_giant;

    set_color_from_type(HZ);
}

void Planet::estimate_radius()
{
    // https://doi.org/10.1051/0004-6361/202348690
    if ((mass < rocky_mass_cutoff)
        || type == rocky || type == waterworld || type == icy)
        volumetric_mean_radius = 1.02 * earth_radius * pow(mass/earth_mass, 0.27);
    else if (mass < giant_mass_cutoff) volumetric_mean_radius = 0.56 * earth_radius * pow(mass/earth_mass, 0.67);
    else if (type == hot_jupiter)
    {
        double volume = mass / hot_jupiter_density;
        volumetric_mean_radius = pow(volume*3 / _pi*4, 1.0/3);
    }
    else volumetric_mean_radius = 18.6 * earth_radius;
    assert(!isinf(volumetric_mean_radius));
}

void Planet::estimate_rotation()
{
    if (!orbit || !orbit->period) return;

    double tidal_lock_threshold_days = 100 / (1.0 + 4.0 * orbit->eccentricity);      // This is a TOTAL guess. Accounts for Mercury (58d, ecc .205630, 3:2 resonance) and Iapetus (79d, ecc .0284, tidal).

    if (orbit->period < (tidal_lock_threshold_days * oneday)
        || type == hot_jupiter)
    {
        sidereal_rotational_period = orbit->period;
    }
    else if (orbit->period < (250 * oneday))          // Another guess.
    {
        sidereal_rotational_period = 1.5 * orbit->period;
    }
    else if (type == rocky || type == icy
        || type == lavaworld                            ) sidereal_rotational_period = 2.38e+6 / log(mass);
    else if (type == waterworld                         ) sidereal_rotational_period = 2.06e+6 / log(mass);              // WAG: average of solid and gas.
    else if (type == ice_giant || type == steam_giant   ) sidereal_rotational_period = 1.74e+6 / log(mass);
    else if (type == gas_giant                          ) sidereal_rotational_period = 1.11e+6 / log(mass);
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
    amt_lit = fabs(_pi - fmin(phabs, _pi*2-phabs)) / _pi;

    double reflectivity = pow(magnbase, -absolute_magnitude);
    double apparent = reflectivity * amt_lit / rsq;

    /*std::cout << name << ": " << (phase*fiftyseven) << ", " << amt_lit << ", " << reflectivity << ", " << rsq 
        << " = " << (-log(apparent) * invlogmagnbase) << std::endl;*/

    if (sourcemagn < -1000) sourcemagn = light_center->absolute_magnitude;
    return -log(apparent) * invlogmagnbase + sourcemagn - cels[0]->absolute_magnitude;
}

double Planet::estimate_bump_scale()
{
    return 0.001 * volumetric_mean_radius * (surface_pressure ? log(surface_pressure) : 1) / log(20);
}

void alienorum::Planet::incline_exo_orbit(double sys_solincl, double sys_solnode)
{
    // Subtract the solar inclination of the local system plane from the planetary inclination.
    if (orbit && orbit->inclination) orbit->inclination -= sys_solincl;
    else orbit->inclination = 0;             // If unknown, assume system plane.

    // The planetary node will be 90 degrees west of the Sun.
    orbit->ascending_node = sys_solnode - half_pi;

    // If the resulting local inclination is negative, reverse its sign and move the node 180 degrees.
    if (orbit->inclination < 0)
    {
        orbit->inclination = -orbit->inclination;
        orbit->ascending_node += _pi;
    }
}

void Planet::estimate_albedo()
{
    double rearths = volumetric_mean_radius / earth_radius;
    double disc_area = rearths * rearths;
    double brightness = pow(magnbase, earth_absmag-absolute_magnitude);
    double a = fmin(1, brightness / disc_area * earth_albedo);
    if (!isnan(a)) albedo = a;
}

void Planet::estimate_albedo_and_absmagn()
{
    double p_rad_e = fmax(0.01, volumetric_mean_radius / earth_radius);
    double est_albedo = 0.3;
    if (type == gas_giant) est_albedo = 0.5;
    else if (type == hot_jupiter) est_albedo = 0.01;
    else if (type == ice_giant || type == steam_giant) est_albedo = 0.3;
    else if (type == waterworld) est_albedo = 0.4;
    else if (type == icy) est_albedo = 0.8;
    else if (type == rocky || type == lavaworld) est_albedo = (mass > 0.5 * earth_mass) ? 0.5 : 0.1;
    absolute_magnitude = fmax(-10, earth_absmag - log(p_rad_e*p_rad_e*est_albedo/earth_albedo) / log(magnbase));
    albedo = est_albedo;
}

void Planet::update_location(double tmnow)
{
    if (orbit && orbit->period) update_orbit_location(tmnow);
}

double Planet::est_bolometric_flux(double t_eff)
{
    Star *s = (Star*)get_light_center();
    assert(s->typeclass() == class_star);
    if (!t_eff) t_eff = s->estimate_temperature();
    double t_star = t_eff - sun_temp;

    double bc_v;
    if (t_eff < 3500.0)
    {
        // Linear interpolation for M-dwarfs based on the 
        // BT-Settl stellar atmospheric models used by Kopparapu.
        // At 3500K, BC_V is ~ -1.75. At 2500K, BC_V drops to ~ -4.50.
        double t_fraction = (t_eff - 2500.0) / (3500.0 - 2500.0);
        // bc_v = -4.02 + t_fraction * (-1.75 - (-4.50));
        bc_v = -4.33 + t_fraction * (-1.75 - (-4.33)); 
    }
    else
    {
        // Standard calculation for most main sequence stars.
        bc_v = -0.192 - (1.41e-4 * t_star) - (1.25e-7 * std::pow(t_star, 2));
    }

    // Calculate absolute bolometric magnitude.
    double m_bol = s->absolute_magnitude + bc_v;

    // Convert to bolometric luminosity relative to the Sun's bolometric 4.74 magnitude.
    double star_intrinsic = std::pow(magnbase, (4.74 - m_bol));

    // Compute planetary illumination
    CelestialObject *myplanet = this;
    while (myplanet->orbit && myplanet->orbit->center != s) myplanet = myplanet->orbit->center;
    double sma_au = myplanet->orbit->semimajor_axis / AU;
    return star_intrinsic / (sma_au * sma_au);            // inverse square of distance
}

double Planet::estimate_surface_temperature()
{
    if (temperature) return temperature;
    double absorbed_flux = (est_bolometric_flux() * (1.0 - albedo)) / 4.0;
    double t_eq = std::pow(absorbed_flux / STEFAN_BOLTZMANN_NORM, 0.25);

    // 4. Apply the Eddington Gray-Atmosphere Approximation
    double empirical_tau_scale = 2.4; 
    double greenhouse_factor = 1.0 + (0.75 * atmospheric_tau * empirical_tau_scale);

    // 5. Calculate final surface temperature
    double t_surface = t_eq * std::pow(greenhouse_factor, 0.25);

    return temperature = t_surface;
}

bool Planet::is_in_con_HZ()
{
    if (orbit && fabs(cached_in_cons_hz - orbit->semimajor_axis) < 0.001) return cache_in_cons_hz;

    if (!orbit || !orbit->center) return false;
    Star *s = (Star*)get_light_center();
    assert(s->typeclass() == class_star);

    // Mathematical model to approximate this chart: https://personal.ems.psu.edu/~jfk4/ruk15/planets/T_Seff_HZ_plusTRAPPIST_ALL__MM_10202020v2.jpg
    double t_eff = s->estimate_temperature();
    double t_star = t_eff - sun_temp;

    // Calculate the baseline flux for the given mass
    // Coefficients from Kopparapu et al. (2014)
    double rg_SeffSun = 0.0;
    double mg_SeffSun = 0.0;
    double planet_mass = mass/earth_mass;

    if (planet_mass >= 1.0 && planet_mass <= 5.0)
    {
        rg_SeffSun = 1.107 - 0.0214 * (planet_mass - 1.0);
        mg_SeffSun = 0.356 - 0.0038 * (planet_mass - 1.0);
    }
    else if (planet_mass >= 0.1 && planet_mass < 1.0)
    {
        rg_SeffSun = 1.107 - 0.0242 * (1.0 - planet_mass);
        mg_SeffSun = 0.356 - 0.0053 * (1.0 - planet_mass);
    }
    else
    {
        // Fallback to 1 Earth mass baseline if out of bounds.
        rg_SeffSun = 1.107;
        mg_SeffSun = 0.356;
    }

    // Stellar temperature coefficients
    double rg_a = 1.332e-4;  double rg_b = 1.580e-8;  double rg_c = -8.308e-12; double rg_d = -1.931e-15;
    double mg_a = 6.171e-5;  double mg_b = 1.698e-9;  double mg_c = -3.198e-12; double mg_d = -5.575e-16;

    // Apply the temperature polynomial to the mass-adjusted baseline
    double inner_limit = rg_SeffSun + (rg_a * t_star) + (rg_b * std::pow(t_star, 2)) + 
                        (rg_c * std::pow(t_star, 3)) + (rg_d * std::pow(t_star, 4));

    double outer_limit = mg_SeffSun + (mg_a * t_star) + (mg_b * std::pow(t_star, 2)) + 
                        (mg_c * std::pow(t_star, 3)) + (mg_d * std::pow(t_star, 4));

    double planet_illumination = est_bolometric_flux(t_eff);

    // Check habitability bounds
    cache_in_cons_hz = (planet_illumination >= outer_limit && planet_illumination <= inner_limit);
    cached_in_cons_hz = orbit->semimajor_axis;

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
    towrite["atmospheric_tau"] = atmospheric_tau;
    if (J2) towrite["J2"] = J2;

    return towrite;
}

bool Planet::from_json(json j)
{
    CelestialObject::from_json(j);
    try { j.at("albedo").get_to(albedo); } catch (...) { ; }
    try { j.at("surface_pressure").get_to(surface_pressure); } catch (...) { ; }
    try { j.at("opposition_surge").get_to(opposition_surge); } catch (...) { ; }
    try { j.at("atmospheric_tau").get_to(atmospheric_tau); } catch (...) { ; }
    try { j.at("J2").get_to(J2); } catch (...) { ; }
    return true;
}
