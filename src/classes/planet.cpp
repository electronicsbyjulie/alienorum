#include <iostream>
#include <cmath>
#include <iomanip>
#include <random>
#include <algorithm>

#include "planet.h"
#include "star.h"
#include "point.h"
#include "patch.h"
#include "shore.h"
#include "celestial.h"

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
    if (lock_type) return;
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
        && (!mnrk || density > giant_density_cutoff))
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
    else if (orbit && orbit->period < oneday*10)
    {
        type = hot_jupiter;
        if (s) s->has_hot_jupiter = true;
    }
    else type = gas_giant;

    if (!ck) set_color_from_type(HZ);

    if (!get_surface_pressure() && get_light_center() != cels[0])
    {
        double shoreline = CosmicShore::calculate_unified_metric(*(Star*)(get_light_center()), *this);
        double p_pa = (shoreline<0) ? 0 : (pow(10, shoreline) * 503);
        if (isinf(p_pa)) p_pa = 0;
        if (p_pa > 0) ensure_atmosphere()->surface_pressure = p_pa;
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
        volumetric_mean_radius = pow(volume*3 / (_pi*4), 1.0/3);
    }
    else volumetric_mean_radius = 18.6 * earth_radius;
    // An infinite or NaN radius comes from an infinite or NaN mass, which is catalog data rather
    // than a mistake here -- so it has to be handled in release builds too, where the assert this
    // replaces did not exist. Anything downstream that divides by the radius (density, surface
    // gravity, the impostor's bounding box) would spread the poison rather than stop on it.
    if (!std::isfinite(volumetric_mean_radius))
    {
        std::cerr << "Warning: could not estimate a radius for " << name << " (mass "
            << mass << "); leaving it at Earth's." << std::endl;
        volumetric_mean_radius = earth_radius;
    }
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

// The IAU slope parameter G, which sets how steeply a body with a solid surface fades as it
// leaves opposition. Bright surfaces scatter light around inside the regolith and partly fill
// their own shadows back in, so they fade slowly (large G); dark ones hide their shadows
// completely and fall off fast (small G). Anchored on the Moon, whose albedo works out around
// 0.09 here and which has to lose 2.7 magnitudes between full and quarter -- the eleven-to-one
// ratio that is the whole point of this exercise.
double Planet::phase_slope_parameter()
{
    if (albedo <= 0 && volumetric_mean_radius > 0 && absolute_magnitude) estimate_albedo();
    double a = (albedo > 0) ? fmin(1.0, albedo) : 0.1;      // Middling and dark, for want of anything better.
    return fmin(0.65, fmax(0.15, 0.35 + 0.3 * a));
}

// How much of what we see off this world is cloud rather than ground, 0 to 1. Air thick enough to
// matter starts around a millibar and owns the view by the time it reaches Earth's pressure; a
// giant is all weather and nothing else. Mars lands near a quarter, which is about right for a
// world whose dust does soften its phase curve without hiding the ground.
double Planet::cloud_deck_fraction()
{
    if (type == gas_giant || type == ice_giant || type == hot_jupiter) return 1;

    double p_pa = get_surface_pressure();
    if (p_pa <= 100) return 0;
    return fmin(1.0, (log10(p_pa) - 2) / 3);
}

// The phase function: what fraction of its opposition brightness the body still shows at phase
// angle alpha, the Sun-body-viewer angle. Returns 1 at opposition and 0 at conjunction, so an
// absolute magnitude quoted the usual way -- V(1,0), the body at 1 AU from both its star and its
// viewer, fully face-on -- stays exactly what it was.
//
// The old linear ramp made brightness proportional to elongation, which is far too generous away
// from full: it said a half Moon was half a full Moon, when it is nearer a tenth. Two laws replace
// it, blended by how much air the world has:
//
//   * Bare ground gets the IAU H-G function (Bowell et al. 1989, Asteroids II, 524-556), the
//     standard two-term fit used for every numbered minor planet. Its first term is the
//     opposition surge itself -- a cusp with infinite slope at zero phase, the signature of
//     shadow-hiding, where every pit in the regolith conceals its own shadow behind the grain
//     that cast it just as the light comes back over the viewer's shoulder. That cusp is worth
//     about 0.4 magnitudes in the last five degrees, which is why the full Moon so outruns any
//     smooth extrapolation from the phases either side of it.
//
//   * Cloud gets the Lambert sphere instead, [sin a + (pi-a) cos a] / pi. Multiple scattering in
//     a deck of cloud fills the shadows in as fast as they form, so Venus and the giants have
//     essentially no surge, only the geometry of a diffusing ball.
double Planet::phase_brightness(double alpha)
{
    double a = fabs(fmod(alpha, _pi*2));
    if (a > _pi) a = _pi*2 - a;                             // Waxing and waning at equal width are equally bright.

    double lambert = (sin(a) + (_pi - a) * cos(a)) / _pi;

    double t = tan(a * 0.5);
    double g = phase_slope_parameter();
    double hg = (1-g) * exp(-3.33 * pow(t, 0.63)) + g * exp(-1.87 * pow(t, 1.22));

    double w = cloud_deck_fraction();
    double phi = (1-w) * hg + w * lambert;

    // A world may be given a sharper surge than its surface type would imply. The extra dies off
    // over a couple of degrees, so it is a brightening at opposition rather than a dimming
    // everywhere else: the value at zero phase is left alone and the rest of the curve drops.
    if (opposition_surge > 0)
    {
        const double surge_width = 0.035;                   // radians, about two degrees.
        phi *= pow(magnbase, -opposition_surge * (1.0 - surge_width / (surge_width + a)));
    }

    return isnan(phi) ? 0 : fmax(0.0, phi);
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
    double alpha = fmin(phabs, _pi*2-phabs);                // Phase angle proper: 0 at full, pi at new.
    amt_lit = (1 + cos(alpha)) * 0.5;                       // Geometry only -- the lit part of the disc, not its brightness.

    double reflectivity = pow(magnbase, -absolute_magnitude);
    double apparent = reflectivity * phase_brightness(alpha) / rsq;

    /*std::cout << name << ": " << (phase*fiftyseven) << ", " << amt_lit << ", " << reflectivity << ", " << rsq 
        << " = " << (-log(apparent) * invlogmagnbase) << std::endl;*/

    if (sourcemagn < -1000) sourcemagn = light_center->absolute_magnitude;
    return -log(apparent) * invlogmagnbase + sourcemagn - cels[0]->absolute_magnitude;
}

double Planet::estimate_bump_scale()
{
    double p_pa = get_surface_pressure();
    return 0.001 * volumetric_mean_radius * (p_pa ? log(p_pa) : 1) / log(20);
}

// Pressure scale height, meters -- the altitude over which the air thins by a factor of e,
// H = RT/(Mg) from the barometric formula. This is the natural thickness of the bright band of
// atmosphere seen on a planet's limb from space (the impostor shader draws the glow out to a
// few H, see SphereImpostorInput::atmosphere_height), and it varies enormously between worlds:
// roughly 8 km on Earth, 11 on Mars for all its thin air (cold, light gravity), 15 on Titan,
// and 27 on Jupiter, whose hydrogen is barely a fifteenth the molar mass of Venus's CO2.
//
// Returns 0 for an airless body, which reads as "no glow at all" downstream.

// TODO: Looking at estimate_scale_height(), I did spot one potential units mismatch you might
// want to double-check. The barometric formula $H = \frac{RT}{Mg}$ expects $g$ to be standard
// acceleration (m/s²). If your estimate_surface_gravity() function returns Earth Gs (where
// Earth = 1.0) as we saw in celestial.cpp, this scale height calculation will return a value
// roughly 9.8 times too large (around 82 km for Earth instead of the 8.5 km mentioned in your
// comments). You may want to multiply $g$ by 9.80665 in this specific function if that's the case!
double Planet::estimate_scale_height()
{
    if (get_surface_pressure() <= 0) return 0;

    double T = estimate_surface_temperature();
    double g = estimate_surface_gravity();
    if (T <= 0 || g <= 0) return 0;

    // Falls back to Earth air when the body has a pressure but no composition listed at all,
    // which is the common case for a world sketched in without that much detail.
    double M = (atm && atm->comp) ? atm->comp->mean_molar_mass() : 0.0289644;
    if (M <= 0) return 0;

    return 8.314462618 * T / (M * g);
}

void alienorum::Planet::incline_exo_orbit(double sys_solincl, double sys_solnode)
{
    // Subtract the solar inclination of the local system plane from the planetary inclination.
    if (!orbit) return;
    orbit->inclination = orbit->inclination ? (orbit->inclination - sys_solincl) : 0;            // If unknown, assume system plane.

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
    if (orbit) update_orbit_location(tmnow);
}

double Planet::est_bolometric_flux(double t_eff)
{
    // get_light_center() answers with whatever the orbital hierarchy leads to, which is not
    // always a star -- a moon of a rogue planet, an object whose center was never resolved. The
    // assert this replaces vanished under NDEBUG and left the Star members below being read off
    // an object of another class.
    CelestialObject *lc = get_light_center();
    if (!lc || lc->typeclass() != class_star) return 0;
    Star *s = (Star*)lc;
    if (!t_eff) t_eff = s->estimate_temperature();

    // See Star::bolometric_correction(), where this calculation now resides: the exostar loader
    // must apply exactly the same correction in reverse (cat.cpp,
    // resolve_or_create_exostar), and two copies would eventually have diverged.
    double bc_v = Star::bolometric_correction(t_eff);

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

double Planet::equilibrium_temperature()
{
    if (temperature) return temperature;
    double Bond = estimate_bond_albedo();
    double absorbed_flux = (est_bolometric_flux() * (1.0 - Bond)) / 4.0;
    double t_eq = std::pow(absorbed_flux / STEFAN_BOLTZMANN_NORM, 0.25);

    return t_eq;
}

double Planet::estimate_surface_temperature()
{
    double t_eq = equilibrium_temperature();

    // Apply the Eddington Gray-Atmosphere Approximation
    double empirical_tau_scale = 2.4; 
    double greenhouse_factor = 1.0 + (0.75 * get_atmospheric_tau() * empirical_tau_scale);

    double t_surface = t_eq * std::pow(greenhouse_factor, 0.25);

    return t_surface;
}

// Convert true altitude to observed.
double alienorum::Planet::atmospheric_refraction(double alt_rad)
{
    temperature = estimate_surface_temperature();       // Kelvins
    double P_hpa = get_surface_pressure() * 0.01;       // Pascals to hPa (millibars)
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
    double density_ratio = (get_surface_pressure() / 101325.0) * (288.15 / estimate_surface_temperature());
    if (density_ratio <= 4.0) return 0.0;
    double n_0 = 1.0 + (0.000293 * density_ratio);
    return std::acos(1.0 / n_0);
}

bool Planet::is_in_con_HZ()
{
    if (orbit && fabs(cached_in_cons_hz - orbit->semimajor_axis) < 0.001) return cache_in_cons_hz;

    if (!orbit || !orbit->center) return false;
    // See est_bolometric_flux(): the light center is not guaranteed to be a star, and nothing
    // without one has a habitable zone to be in.
    CelestialObject *lc = get_light_center();
    if (!lc || lc->typeclass() != class_star) return false;
    Star *s = (Star*)lc;

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

// Only the constituents actually present are written out, so an atmosphere whose composition is
// unknown or trivial does not carry a dozen zeros around with it.
json AtmosphereComposition::to_json()
{
    json towrite;
    if (H2_portion  ) towrite["H2"]   = H2_portion;
    if (He_portion  ) towrite["He"]   = He_portion;
    if (N2_portion  ) towrite["N2"]   = N2_portion;
    if (O2_portion  ) towrite["O2"]   = O2_portion;
    if (O3_portion  ) towrite["O3"]   = O3_portion;
    if (CO2_portion ) towrite["CO2"]  = CO2_portion;
    if (CH4_portion ) towrite["CH4"]  = CH4_portion;
    if (SO2_portion ) towrite["SO2"]  = SO2_portion;
    if (H2O_portion ) towrite["H2O"]  = H2O_portion;
    if (H2S_portion ) towrite["H2S"]  = H2S_portion;
    if (HCN_portion ) towrite["HCN"]  = HCN_portion;
    if (NH3_portion ) towrite["NH3"]  = NH3_portion;
    if (C2H6_portion) towrite["C2H6"] = C2H6_portion;
    if (N2O_portion ) towrite["N2O"]  = N2O_portion;
    if (CO_portion  ) towrite["CO"]   = CO_portion;
    if (Ar_portion  ) towrite["Ar"]   = Ar_portion;
    return towrite;
}

bool AtmosphereComposition::from_json(json j)
{
    try { j.at("H2"  ).get_to(H2_portion  ); } catch (...) { ; }
    try { j.at("He"  ).get_to(He_portion  ); } catch (...) { ; }
    try { j.at("N2"  ).get_to(N2_portion  ); } catch (...) { ; }
    try { j.at("O2"  ).get_to(O2_portion  ); } catch (...) { ; }
    try { j.at("O3"  ).get_to(O3_portion  ); } catch (...) { ; }
    try { j.at("CO2" ).get_to(CO2_portion ); } catch (...) { ; }
    try { j.at("CH4" ).get_to(CH4_portion ); } catch (...) { ; }
    try { j.at("SO2" ).get_to(SO2_portion ); } catch (...) { ; }
    try { j.at("H2O" ).get_to(H2O_portion ); } catch (...) { ; }
    try { j.at("H2S" ).get_to(H2S_portion ); } catch (...) { ; }
    try { j.at("HCN" ).get_to(HCN_portion ); } catch (...) { ; }
    try { j.at("NH3" ).get_to(NH3_portion ); } catch (...) { ; }
    try { j.at("C2H6").get_to(C2H6_portion); } catch (...) { ; }
    try { j.at("N2O" ).get_to(N2O_portion ); } catch (...) { ; }
    try { j.at("CO"  ).get_to(CO_portion  ); } catch (...) { ; }
    try { j.at("Ar"  ).get_to(Ar_portion  ); } catch (...) { ; }
    return true;
}

json Atmosphere::to_json()
{
    json towrite;
    towrite["surface_pressure"] = surface_pressure;
    if (tau) towrite["tau"] = tau;
    if (particulates) towrite["particulates"] = particulates;
    if (comp) towrite["composition"] = comp->to_json();
    return towrite;
}

// Accepts either spelling of every key, because the two files that carry atmospheres do not agree
// on a convention: universe files are snake_case throughout ("surface_pressure"), while
// catalogs/planets.json is PascalCase ("SurfacePressure"). One reader for both beats two readers
// that have to be kept in step.
bool Atmosphere::from_json(json j)
{
    try { j.at("surface_pressure").get_to(surface_pressure); } catch (...) { ; }
    try { j.at("SurfacePressure").get_to(surface_pressure); } catch (...) { ; }
    try { j.at("tau").get_to(tau); } catch (...) { ; }
    try { j.at("AtmosphericTau").get_to(tau); } catch (...) { ; }
    try { j.at("particulates").get_to(particulates); } catch (...) { ; }
    try { j.at("Particulates").get_to(particulates); } catch (...) { ; }
    try { json jc = j.at("composition"); ensure_composition()->from_json(jc); } catch (...) { ; }
    try { json jc = j.at("Composition"); ensure_composition()->from_json(jc); } catch (...) { ; }
    return true;
}

json Planet::to_json()
{
    json towrite = CelestialObject::to_json();

    if (albedo) towrite["albedo"] = albedo;
    towrite["opposition_surge"] = opposition_surge;
    if (atm) towrite["atmosphere"] = atm->to_json();
    if (J2) towrite["J2"] = J2;

    // Both of these come from a catalog originally -- the astorb row number, and the flag that
    // says the catalog stated a type we are not to second-guess in classify(). They used not to
    // be written, which left a saved asteroid without its number after a reload, and so out of
    // the one place that number is read: the pass in visuals.cpp that keeps the asteroids out of
    // the ordinary planet drawing. Zero and false are the "no such thing" values, so as with the
    // fields above, they are written only when they say something.
    if (asteroid_no) towrite["asteroid_no"] = asteroid_no;
    if (lock_type) towrite["lock_type"] = lock_type;

    return towrite;
}

bool Planet::from_json(json j)
{
    CelestialObject::from_json(j);
    try { j.at("albedo").get_to(albedo); } catch (...) { ; }
    try { j.at("opposition_surge").get_to(opposition_surge); } catch (...) { ; }
    // Fetch the node into a local FIRST. Written as ensure_atmosphere()->from_json(j.at(...)),
    // C++17 sequences the postfix-expression before the argument, so ensure_atmosphere() runs and
    // builds an Atmosphere before j.at() ever gets the chance to throw -- which handed an airless
    // body a default one-atmosphere sky just for being loaded, and suppressed the legacy branch
    // below by leaving atm non-null.
    try { json ja = j.at("atmosphere"); ensure_atmosphere()->from_json(ja); } catch (...) { ; }

    // Universe files written before the atmosphere became its own object carry these two at the
    // top level instead. Read them only when there was no "atmosphere" block, so a current file
    // is never second-guessed by a stale key that happens to sit alongside it.
    if (!atm)
    {
        double legacy_pressure = 0, legacy_tau = 0;
        try { j.at("surface_pressure").get_to(legacy_pressure); } catch (...) { ; }
        try { j.at("atmospheric_tau").get_to(legacy_tau); } catch (...) { ; }
        if (legacy_pressure > 0 || legacy_tau > 0)
        {
            if (!isinf(legacy_pressure)) ensure_atmosphere()->surface_pressure = legacy_pressure;
            atm->tau = legacy_tau;
        }
    }

    try { j.at("J2").get_to(J2); } catch (...) { ; }
    try { j.at("asteroid_no").get_to(asteroid_no); } catch (...) { ; }
    try { j.at("lock_type").get_to(lock_type); } catch (...) { ; }
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

// Evaluates the probability of a ring system existing based on mass and temperature.
bool Planet::guess_has_rings()
{
    double probability = 0.0;

    // 1. Mass factor: Jovians have a much higher capture/retention rate
    if (mass > giant_mass_cutoff)
    {
        // Gas/Ice giant threshold (approx Neptune mass)
        probability += 0.65;
    }
    else if (mass > rocky_mass_cutoff)
    {
        // Super-Earth / Sub-Neptune
        probability += 0.15;
    }
    else
    {
        // Terrestrial
        probability += 0.02;
    }

    // 2. Temperature factor (The Frost Line proxy)
    // Most magnificent rings are ice. If it's a "Hot Jupiter", rings sublimate.
    double t_eq = equilibrium_temperature();
    if (t_eq < 170.0)
    {
        // Deep freeze (like Saturn/Uranus)
        probability += 0.25;
    }
    else if (t_eq > 800.0)
    {
        // Too hot, dust is dragged and ice is gone
        probability -= 0.60;
    }
    else if (t_eq > 300.0)
    {
        // Warm, mostly sparse rock/dust if any
        probability -= 0.20;
    }

    // Clamp the final probability between 1% and 95%
    probability = std::max(0.01, std::min(0.95, probability));

    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng) <= probability;
}

/*
 * Generates the physical parameters of the ring system bounded by the Roche limit.
 * Assumes access to class members: radius, rocheLimit
 */
void Planet::generate_ring_parameters(bool gr)
{
    if (!gr && !guess_has_rings())
    {
        ring_radius = 0;
        return;
    }

    // Safety constraint: If Roche limit is somehow smaller than the planet 
    // (e.g., highly dense planet, very low density moon proxy), rings cannot form.
    double minInnerRadius = volumetric_mean_radius * 1.1; // 10% gap from the surface/atmosphere
    double roche_limit_zero = this->Roche_limit();
    if (roche_limit_zero <= minInnerRadius)
    {
        ring_radius = 0;
        return;
    }

    // Generate Inner Radius
    // Favor starting relatively close to the planet
    std::uniform_real_distribution<double> innerDist(minInnerRadius, roche_limit_zero * 0.6);
    double innerRadius = innerDist(rng);

    // Generate Outer Radius
    // Must be larger than inner, and capped tightly by the Roche limit
    std::uniform_real_distribution<double> outerDist(innerRadius * 1.15, roche_limit_zero * 0.98);
    ring_radius = outerDist(rng) + volumetric_mean_radius;

    // Generate Density/Opacity
    // Wider rings or rings around more massive planets tend to be more substantial.
    // Here we just generate a random float, slightly weighted by the size of the ring disk.
    double ringWidthRatio = (ring_radius - innerRadius) / roche_limit_zero;
    
    // Base opacity between 0.1 (faint dust) and 0.8 (highly reflective ice)
    std::uniform_real_distribution<double> opacityDist(0.1, 0.8);
    double meanOpacity = opacityDist(rng) + (ringWidthRatio * 0.2); 
    
    // Clamp opacity to 1.0 max
    meanOpacity = std::min(1.0, meanOpacity);

    // Generate a ring texture and a ring transparency map using innerRadius/ring_radius and meanOpacity.
    if (ring_map) delete ring_map;
    if (ringx_map) delete ringx_map;

    ring_map = new Map(this);
    ringx_map = new Map(this);

    int ringres = ring_radius / volumetric_mean_radius * 1024;
    ring_map->generate_ring_map(this, ringres, innerRadius/ring_radius, meanOpacity, ringx_map);
}