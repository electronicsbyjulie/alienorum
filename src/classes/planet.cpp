#include <iostream>
#include <cmath>
#include <iomanip>

#include "planet.h"
#include "star.h"
#include "point.h"
#include "patch.h"
#include "shore.h"

using namespace alienorum;

std::vector<AstorbRow> astorb;

void Planet::set_color_from_type(bool HZ)
{
    if (type == gas_giant) BV_color = 0.98;         // average of Jupiter and Saturn.
    else if (type == rocky)
    {
        if (HZ) BV_color = 0.2;                     // estimate same as Earth.
        else BV_color = 1;
    }
    else if (type == hot_jupiter)
    {
        // https://en.wikipedia.org/wiki/HD_189733_b#/media/File:HD_189733b_blue_planet.png with universal B-V correction added.
        double bluest = -0.1;

        // https://iopscience.iop.org/article/10.3847/1538-4357/aadd9e
        // https://experts.arizona.edu/en/publications/absorption-spectra-of-the-prototype-hot-jupiters-determination-of
        // https://www.cambridge.org/core/journals/proceedings-of-the-international-astronomical-union/article/absorption-spectra-of-the-prototype-hotjupiters-determination-of-atmospheric-constituents-and-structure/654C6A452CD94ABC335C2281F8108FD0
        // https://academic.oup.com/mnras/article/437/1/46/992656
        // https://arxiv.org/html/2603.02409
        // https://www.aanda.org/articles/aa/full_html/2019/07/aa35089-19/aa35089-19.html
        // https://academic.oup.com/mnras/article/426/3/2483/989230
        // https://repository.arizona.edu/handle/10150/628273
        double T = estimate_surface_temperature();
        BV_color = 0.98 + (bluest-0.98) / (1.0 + 0.002 * fabs(T-1200));
    }
    else if (type == ice_giant) BV_color = 0.49;    // average of Uranus and Neptune.
    else if (type == icy) BV_color = 0.6;
    else if (type == lavaworld) BV_color = 1.3;
    else if (type == waterworld) BV_color = -0.3;
}

void Planet::classify()
{
    classify(is_in_con_HZ());
}

void Planet::classify(bool HZ, bool mnrk, bool ck)
{
    Star *s = nullptr;
    double density = mnrk ? (mass / sphere_volume(volumetric_mean_radius) * 1e-6) : 0;

    if (orbit && orbit->center && orbit->center->typeclass() == class_star)
        s = (Star*) orbit->center;

    double T = estimate_surface_temperature();
    if (mass < rocky_mass_cutoff                    // Mass cutoff between rocky planets and ice giants
        || (mass < jupiter_mass && mnrk && density > rocky_density_cutoff)
        )
    {
        if (mnrk && T < water_freezing && density < rocky_density_cutoff) type = icy;
        else if (T > lava_T_cutoff) type = lavaworld;
        else type = rocky;
    }
    else if (mass < giant_mass_cutoff               // Mass cutoff between ice giants and gas giants
        && (!mnrk || mnrk > giant_density_cutoff))
    {
        if (HZ)
        {
            // If system has a hot Jupiter, estimate a waterworld.
            // https://doi.org/10.48550/arXiv.astro-ph/0701048
            if (s && s->has_hot_jupiter) type = waterworld;
            else type = ice_giant;
        }
        else type = ice_giant;
    }
    else if (orbit->period < oneday*10)
    {
        type = hot_jupiter;
        if (s) s->has_hot_jupiter = true;
    }
    else type = gas_giant;

    if (!ck) set_color_from_type(HZ);

    if (!surface_pressure && get_light_center() != cels[0])
    {
        double shoreline = CosmicShore::calculate_unified_metric(*(Star*)(get_light_center()), *this);
        surface_pressure = (shoreline<0) ? 0 : (pow(10, shoreline) * 503);
    }
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

    double tidal_lock_threshold_days = 100 / (1.0 + 4.0 * orbit->eccentricity);         // This is a TOTAL guess. Accounts for Mercury (58d, ecc .205630, 3:2 resonance) and Iapetus (79d, ecc .0284, tidal).

    if (orbit->period < (tidal_lock_threshold_days * oneday)
        || type == hot_jupiter)
    {
        sidereal_rotational_period = orbit->period;
    }
    else if (orbit->period < (250 * oneday) && orbit->eccentricity > 0.15)              // Mercury.
    {
        sidereal_rotational_period = 1.5 * orbit->period;
    }
    else if (type == waterworld ) sidereal_rotational_period = 2.06e+6 / log(mass);              // WAG: average of solid and gas.
    else if (type == ice_giant  ) sidereal_rotational_period = 1.74e+6 / log(mass);
    else if (type == gas_giant  ) sidereal_rotational_period = 1.11e+6 / log(mass);
    else sidereal_rotational_period = 2.38e+6 / log(mass);                              // rocky, icy, or lava world.
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
    if (type == gas_giant       ) est_albedo = 0.5;
    else if (type == hot_jupiter) est_albedo = 0.01;
    else if (type == ice_giant  ) est_albedo = 0.3;
    else if (type == waterworld ) est_albedo = 0.4;
    else if (type == icy        ) est_albedo = 0.8;
    else if (type == rocky
        || type == lavaworld    ) est_albedo = (mass > 0.5 * earth_mass) ? 0.5 : 0.1;
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
    s->m_bol = s->absolute_magnitude + bc_v;

    // Convert to bolometric luminosity relative to the Sun's bolometric 4.74 magnitude.
    s->m_bol = std::pow(magnbase, (4.74 - s->m_bol));

    // Compute planetary illumination
    CelestialObject *myplanet = this;
    while (myplanet->orbit && myplanet->orbit->center != s) myplanet = myplanet->orbit->center;
    double sma_au = myplanet->orbit->semimajor_axis / AU;
    return s->m_bol / (sma_au * sma_au);            // inverse square of distance
}

double Planet::estimate_bond_albedo()
{
    #if 1
    LocalPatchPredictor model;
    model.load_data(dataset_bond_albedines);
    return fmax(0, fmin(1, model.predict(BV_color, albedo)));
    #else
    if (albedo > 0.65) return 1.0 - (1.0 - albedo) * 0.77;              // Venus
    return albedo / 1.46;                                               // typical value
    #endif
}

double Planet::estimate_surface_temperature()
{
    if (temperature) return temperature;
    double Bond = estimate_bond_albedo();
    double absorbed_flux = (est_bolometric_flux() * (1.0 - Bond)) / 4.0;
    double t_eq = std::pow(absorbed_flux / STEFAN_BOLTZMANN_NORM, 0.25);

    // 4. Apply the Eddington Gray-Atmosphere Approximation
    double empirical_tau_scale = 2.4; 
    double greenhouse_factor = 1.0 + (0.75 * atmospheric_tau * empirical_tau_scale);

    // 5. Calculate final surface temperature
    double t_surface = t_eq * std::pow(greenhouse_factor, 0.25);

    return t_surface;
}

// Convert true altitude to observed.
double alienorum::Planet::atmospheric_refraction(double alt_rad)
{
    temperature = estimate_surface_temperature();       // Kelvins
    double P_hpa = surface_pressure * 0.01;             // Pascals to hPa (millibars)
    double T_c = temperature - 273.15;

    double h_true_deg = alt_rad * fiftyseven;

    // 1. Calculate pressure modifier first
    double pressure_ratio = P_hpa / 1013.25;
    if (pressure_ratio > 5.0)
    {
        pressure_ratio = 5.0 + std::log10(pressure_ratio - 4.0);
    }

    double tempfactor = 283.0 / (273.0 + T_c);

    // Saemundsson's formula on its own (see step 2/3 below for the altitude it actually gets
    // called with). Factored out because step 4 requires it evaluated at a second altitude too.
    auto saemundsson_deg = [&](double x_deg) -> double
    {
        double correction = 10.3 / (x_deg + 5.11);
        double arg_deg = x_deg + correction;
        if (arg_deg >= 90.0) return 0.0; // Zenith: no refraction.
        double cot_val = 1.0 / std::tan(arg_deg * fiftyseventh);
        return fmax(0.0, 1.02 * cot_val * pressure_ratio * tempfactor / 60.0);
    };

    // 2. Smooth clamp near the horizon, widened for dense atmospheres.
    //
    // R_arcmin above is linear in pressure_ratio, so on a dense-atmosphere world the whole
    // refraction curve scales up -- and with a fixed-width smoothing curve, its slope scales up
    // by that same factor right where objects cross the horizon. Measured: at Earth pressure,
    // the worst-case d(apparent altitude)/d(true altitude) near the horizon is a reasonable
    // 0.88; at 50x Earth pressure with a fixed k it collapsed to 0.18 -- objects visually
    // "stuck" near the horizon over several degrees of true altitude change. An earlier attempt
    // at this fix (moving min_calc_alt instead, see git history) doesn't actually help: the
    // softplus curve's own peak slope at its center is a fixed 0.5 regardless of where
    // min_calc_alt sits, so shifting it leaves the real problem untouched.
    //
    // Widening k in proportion to pressure_ratio spreads that same total rise in refraction over
    // a proportionally wider altitude range instead, keeping the slope roughly pressure-
    // independent: verified numerically flat around 0.81-0.83 from 1x to 500x Earth pressure.
    // Exponent 1.3 was chosen empirically for that flatness -- lower exponents undercorrect at
    // extreme pressures, higher ones overcorrect at moderate ones.
    double min_calc_alt = -1.0;
    double k = 0.5 * std::pow(fmax(1.0, pressure_ratio), 1.3);

    // Steps 2 and 3 together: clamp the altitude, then run Saemundsson on it. A lambda because
    // step 4 must evaluate this same base curve at a second altitude (the true horizon).
    auto base_refraction_deg = [&](double h_deg) -> double
    {
        double delta = h_deg - min_calc_alt;
        double calc_h_deg;

        // Same shortcut as before (skip the smoothing once it's converged to y=x), scaled by k so
        // a widened curve still gets the same number of half-widths of runway before the cutoff.
        if (delta > 40.0 * k)
        {
            // Prevent std::exp overflow for stars high in the sky.
            // At this altitude, the smoothing function is effectively y = x anyway.
            calc_h_deg = h_deg;
        }
        else
        {
            // As delta goes negative (dropping below the limit), the exp() term
            // approaches 0, log1p approaches 0, and calc_h_deg smoothly approaches min_calc_alt.
            calc_h_deg = min_calc_alt + k * std::log1p(std::exp(delta / k));
        }

        if (calc_h_deg < min_calc_alt) calc_h_deg = min_calc_alt;
        return saemundsson_deg(calc_h_deg);
    };

    double R_deg = base_refraction_deg(h_true_deg);

    // 4. Horizon-bowl consistency (see find_horizon() in visuals.cpp, "Horizon bowl" -- it draws
    // the ground/sky boundary lifted by atmospheric_horizon_lift() on dense-atmosphere worlds).
    // Star refraction and that ground lift used to be computed by two unrelated formulas that
    // disagreed by several degrees (verified: ~7.7 deg gap on a Venus-like world, since the flat
    // 5 deg ceiling below was far short of an 8.2 deg ground lift) -- any star between the true
    // and visually-lifted horizon rendered as if behind solid ground.
    //
    // The binding constraint is at the true horizon itself, not below it: the ground polygon
    // (draw_horizon()) fills everything below the lifted rim, so a star at true altitude 0 that
    // lands even slightly under the rim is painted over and vanishes. An earlier version of this
    // step aimed only to converge on the rim asymptotically, several degrees *below* the horizon,
    // which left a star at true altitude 0 sitting 2.5 deg under the rim at 10 atm -- verified
    // against the 1atm/10atm screenshot pair, where an Ursa Major star that should have risen with
    // the horizon disappeared behind it instead, and only cleared the rim at 3.35 deg true.
    //
    // So the extra lift is sized to exactly close the gap AT h=0 -- making apparent altitude 0 map
    // onto the rim, which is precisely what the visible horizon means -- and then decays away above
    // it, leaving high-altitude stars on Saemundsson's own curve. Below the horizon it simply holds
    // (the base curve's own softplus floor keeps things monotonic down there). Verified across
    // Earth/Titan/Venus/300 atm/1000 atm: apparent altitude stays strictly increasing everywhere,
    // with a worst-case slope of 0.21, a star at true altitude 0 landing exactly on the rim, and
    // one at 90 landing exactly on the zenith (see the renormalization inside the block below --
    // both endpoints are pinned, which is what keeps the two ends of the sky from folding over).
    double horizon_lift_deg = atmospheric_horizon_lift() * fiftyseven;
    double extra_at_horizon_deg = fmax(0.0, horizon_lift_deg - base_refraction_deg(0.0));
    if (extra_at_horizon_deg > 0.0)
    {
        double k_extra = fmax(0.5, extra_at_horizon_deg * 1.5);
        auto shape = [&](double h_deg) -> double
        {
            return 2.0 / (1.0 + std::exp(fmax(0.0, h_deg) / k_extra));
        };

        // shape() on its own only approaches 0 asymptotically, and k_extra scales with the bowl
        // itself -- so the deeper the bowl, the slower the decay, which is backwards. At 1000 atm
        // (k_extra 57 deg) shape(90) was still 0.343, leaving ~13 deg of lift at the zenith: a
        // star directly overhead was pushed 13 deg *past* the zenith and folded over to the
        // opposite azimuth, which is what scrunched everything up there. It also made
        // refract_true_point()'s degenerate rotation axis near the zenith (compute_normal() of two
        // parallel vectors) visible, since it was applying a 13 deg rotation about it.
        //
        // Renormalizing onto shape(90) pins both ends instead: 1 at the true horizon, so h=0 still
        // lands exactly on the rim as step 4 requires, and exactly 0 at the zenith, so the zenith
        // stays the zenith and the axis degeneracy no longer matters (the angle goes to 0 with it).
        //
        // Pinning both ends fixes the mean slope at (90 - lift)/90, so the near-horizon slope
        // necessarily drops -- the old curve only read steeper there by overshooting the far end.
        // Verified across Earth/Titan/Venus/300 atm/1000 atm: apparent altitude stays strictly
        // increasing, h=0 lands on the rim and h=90 on the zenith to the last digit, worst-case
        // slope 0.21 (Titan, and set by the base curve, not this term).
        double shape90 = shape(90.0);
        double decay = fmax(0.0, (shape(h_true_deg) - shape90) / (1.0 - shape90));
        R_deg += extra_at_horizon_deg * decay;
    }

    // Final safety clamp -- never exceed the larger of the old flat ceiling or what the bowl
    // itself requires.
    double ceiling_deg = fmax(5.0, horizon_lift_deg);
    if (R_deg < 0.0) R_deg = 0.0;
    if (R_deg > ceiling_deg) R_deg = ceiling_deg;

    return R_deg * fiftyseventh;  // Return just the refractive shift in radians
}

// Visual horizon lift from atmospheric density (see find_horizon() in visuals.cpp, which draws
// the ground/sky boundary at this same elevation, and atmospheric_refraction() above, which
// calibrates star refraction near the horizon to reach it too). Modeled as the critical angle of
// a thin shell of uniform refractive index n_0 wrapping the planet, from Snell's law. Zero below
// density_ratio 4 -- mild atmospheres (Earth included) don't show a visible "bowl".
double alienorum::Planet::atmospheric_horizon_lift()
{
    double density_ratio = (surface_pressure / 101325.0) * (288.15 / estimate_surface_temperature());
    if (density_ratio <= 4.0) return 0.0;
    double n_0 = 1.0 + (0.000293 * density_ratio);
    return std::acos(1.0 / n_0);
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

Point refract_true_point(Point pt)
{
    if (view_mode != vm_horizon) return pt;
    return refract_true_point(pt, half_pi - find_3D_angle(pt, yaxis, center));
}

Point refract_true_point(Point pt, double alt_rad)
{
    Point axis = compute_normal(pt, yaxis, center);
    return rotate3D(pt, center, axis, ((Planet*)cels[whereami])->atmospheric_refraction(alt_rad));
}
