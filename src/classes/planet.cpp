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

void alienorum::Planet::setup_atm_ring_props()
{
    classify();
    apply_cosmic_shoreline();
    if (estimate_habitability()) ensure_atmosphere()->ensure_composition()->generate_fictitious_habitable();
    else ensure_atmosphere()->ensure_composition()->generate_fictitious_for_planet(type);

    generate_ring_parameters();
}

void alienorum::Planet::apply_cosmic_shoreline()
{
    CelestialObject *plc = get_light_center();
    if (plc
        && plc->seqno                                       // solar system objects are already known to have/not have an atmosphere
        && plc->typeclass() == class_star
    )
    {
        // Dereferenced, so it has to be there and it has to be a star: a rogue planet's light
        // center is a null pointer, and a moon of one leads somewhere that is not a Star.
        double shoreline = CosmicShore::calculate_unified_metric(*(Star*)plc, *this);
        double max_atm_pressure = (shoreline < 0) ? 0 : (pow(10, shoreline) * 503);
        if (isinf(max_atm_pressure)) max_atm_pressure = 0;
        ensure_atmosphere()->surface_pressure = cel_frand(0.1, 1) * max_atm_pressure;
    }
}

bool alienorum::Planet::estimate_habitability()
{
    if (type == waterworld)
    {
        has_water = 1;
    }
    else if (randomize_txgen)
    {
        has_water = 0;
    }

    temperature = 0;
    double T_surf = estimate_surface_temperature();
    const double Tboil = water_freezing+100;                                     // Reference pressure

    // Constants for water b.p.
    const double R = 8.314;                                         // J/(mol*K)
    const double DELTA_H_VAP = 40660.0;                             // J/mol
    const double P1 = 1.0e+5;  

    // Clausius-Clapeyron calculation
    double inv_T1 = 1.0 / Tboil;
    double gas_constant_ratio = R / DELTA_H_VAP;
    double pressure_log = std::log(get_surface_pressure() / P1);

    double inv_T2 = inv_T1 - (gas_constant_ratio * pressure_log);
    double T_boil = 1.0 / inv_T2;
    // std::cout << "At " << (get_surface_pressure() / oneatm) << " atmospheres, water boils at " << T_boil << " K." << std::endl;

    bool life_possible = false;

    // This is a query -- "could this world support life as it stands" -- not a place to give a
    // world an atmosphere it didn't already have. It used to reach for one unconditionally via
    // ensure_atmosphere(), which default-constructs surface_pressure at one bar: called from
    // generate_rocky_map() (so on every airless rock or moon the moment its texture is drawn) and
    // from setup_atm_ring_props(), that put a full Earth atmosphere on bodies that should have
    // stayed airless -- Solar System moons, Mercury, the asteroids -- unless catalogs/planets.json
    // said otherwise. Only read what is already there; only apply_cosmic_shoreline() (exoplanets
    // at load, Shift+A new bodies) and the edit dialog are allowed to create one.
    AtmosphereComposition *ac = atm ? atm->ensure_composition() : nullptr;
    life_possible = (is_in_con_HZ() && mass > 0.02 * earth_mass);       // Based on Titan's mass.
    if (randomize_txgen && ac)
    {
        if (life_possible)
        {
            if (!show_taucalc) ac->generate_fictitious_habitable();
            atm->calculate_tau(get_surface_pressure());
            temperature = 0;
            T_surf = estimate_surface_temperature();
        }
        else if (!show_taucalc) ac->generate_fictitious_for_planet(type);
    }
    life_possible = (life_possible
        && get_surface_pressure() >= 600
        && T_surf > 0.9*water_freezing && T_surf < 320
        && get_surface_pressure() < oneatm*2000);

    if (atm) atm->calculate_tau(get_surface_pressure());

    if (life_possible)
    {
        temperature = 0;
        T_surf = estimate_surface_temperature();
        #ifdef DEBUG
            std::cout << "Surface pressure: " << (get_surface_pressure() / 101325) << " atm." << std::endl << std::flush;
            std::cout << "Surface temperature: " << T_surf << " K." << std::endl << std::flush;
        #endif

        if (randomize_txgen)
        {
            if (T_surf < 0.9 * water_freezing)
            {
                has_water = 1;
            }
            else if (T_surf < T_boil * 1.1)
            {
                double max_water = pow((T_boil*1.1 - T_surf) / (T_boil*1.1 - 0.9*water_freezing), 0.2);
                has_water = cel_frand(0, max_water);
            }
        }

        if (ac) ac->H2O_portion = 0.014 * has_water;
        temperature = 0;
        if (atm) atm->calculate_tau(get_surface_pressure());
        T_surf = estimate_surface_temperature();

        life_possible = (has_water >= 0.05
            && get_surface_pressure() >= 600
            && T_surf > 0.9*water_freezing && T_surf < 320
            && get_surface_pressure() < oneatm*2000);

        if (randomize_txgen && ac)
        {
            if (life_possible)
            {
                if (!show_taucalc) ac->generate_fictitious_habitable();
                atm->calculate_tau(get_surface_pressure());
            }
        }
    }

    temperature = 0;
    if (atm) atm->calculate_tau(get_surface_pressure());
    T_surf = estimate_surface_temperature();

    return life_possible;
}

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

double alienorum::Planet::get_atmospheric_tau()
{
    if (!atm) return 0;

    // A real, explicitly-set composition can legitimately compute to zero tau -- Earth's N2/O2/Ar
    // air isn't in the tracked greenhouse-gas list at all -- so a zero result is not itself a sign
    // that composition data is missing. Only invent a fictitious composition when there is truly
    // none to work with; once one exists (real or invented), recompute tau from it every time
    // rather than trusting a stale cached value that a falsy tau can't be told apart from.
    if (!atm->comp)
    {
        double press = atm->surface_pressure;
        atm->ensure_composition()->generate_fictitious_for_planet(type);
        if (press) atm->surface_pressure = press;
    }
    atm->calculate_tau(atm->surface_pressure);
    return atm->tau;
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

    // classify() used to also reach for an atmosphere here, on its own separate copy of the same
    // cosmic-shoreline math apply_cosmic_shoreline() already does properly (randomized within its
    // range, one shared implementation). classify() runs from far more places than just exoplanet
    // creation -- catalog loads, the edit dialog, procedural moon generation -- so that gave any
    // of them a side-effect atmosphere assignment classify()'s name gives no reason to expect. Only
    // apply_cosmic_shoreline() assigns one now, called explicitly by the load/creation paths that
    // are supposed to (setup_atm_ring_props() for exoplanets, the Shift+A dialog for new bodies).
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
double Planet::estimate_scale_height()
{
    // get_surface_pressure() is not one consistent reference level: for a bare-ish rock we see
    // the ground of (Mars, Earth) it is that ground, which is exactly where the visible haze
    // starts. For a world we only ever see the top of the weather of -- Venus's permanent
    // sulfuric-acid overcast sitting on 92 atmospheres we never render, any gas or ice giant --
    // it can be the true deep pressure (Venus) or left at zero because there is no ground to give
    // a value to (Uranus, Neptune, if nobody typed one in). Either way it is the wrong altitude
    // for a glow meant to sit above the cloud deck we actually draw, so cloud_deck_fraction() (1
    // for a world that is nothing but weather) picks the reference level instead: the
    // conventional 1-bar cloud-top pressure astronomers already use to define a gas giant's own
    // radius, rather than whatever get_surface_pressure() happens to hold underneath it.
    bool at_cloud_deck = cloud_deck_fraction() >= 1.0;
    double p_ref = at_cloud_deck ? oneatm : get_surface_pressure();
    if (p_ref <= 0) return 0;

    double T = at_cloud_deck ? temperature_at_pressure(p_ref) : estimate_surface_temperature();
    double g = estimate_surface_gravity();     // Earth Gs (Earth = 1.0); H = RT/(Mg) requires m/s2.
    if (T <= 0 || g <= 0) return 0;
    g *= 9.80665;

    // Falls back to Earth air when the body has a pressure but no composition listed at all,
    // which is the common case for a world sketched in without that much detail.
    double M = (atm && atm->comp) ? atm->comp->mean_molar_mass() : 0.0289644;
    if (M <= 0) return 0;

    double H = 8.314462618 * T / (M * g);

    // Even the puffiest real atmosphere we know of is a small fraction of its planet's radius
    // (Titan's ~15 km against a 2575 km radius is under 0.6%). A poorly-constrained world -- a
    // directly-imaged wide-orbit giant with no measured composition or temperature, say -- can
    // still send T or g to an extreme that makes H balloon past the planet itself. Cap it well
    // above anything physical so the glow stays a limb, not a shroud, however bad the inputs are.
    const double max_scale_height_fraction_of_radius = 0.05;
    if (volumetric_mean_radius > 0)
        H = std::min(H, volumetric_mean_radius * max_scale_height_fraction_of_radius);

    return H;
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
    // DO NOT return temperature here, or the surface temp will go infinite in horizon view!
    double Bond = estimate_bond_albedo();
    double absorbed_flux = (est_bolometric_flux() * (1.0 - Bond)) / 4.0;
    double t_eq = std::pow(absorbed_flux / STEFAN_BOLTZMANN_NORM, 0.25);

    return t_eq;
}

double Planet::estimate_surface_temperature()
{
    return temperature_at_pressure(get_surface_pressure());
}

double Planet::temperature_at_pressure(double pressure_pa)
{
    double t_eq = equilibrium_temperature();

    get_atmospheric_tau();
    double tau = (atm && atm->comp) ? atmospheric_tau(pressure_pa * 0.000009869,
        atm->comp->CO2_portion, atm->comp->CH4_portion, atm->comp->H2O_portion, atm->comp->N2O_portion,
        atm->comp->O3_portion,  atm->comp->SO2_portion, atm->comp->H2S_portion, atm->comp->CO_portion,
        atm->comp->HCN_portion, atm->comp->H2_portion,  atm->comp->NH3_portion, atm->comp->C2H6_portion) : 0;

    // Apply the Eddington Gray-Atmosphere Approximation
    double empirical_tau_scale = 2.4;
    double greenhouse_factor = 1.0 + (0.75 * tau * empirical_tau_scale);

    return t_eq * std::pow(greenhouse_factor, 0.25);
}

// The altitude-independent half of atmospheric_refraction(), computed once per epoch per planet
// rather than once per object per frame. See RefractionConstants in planet.h for why.
const alienorum::Planet::RefractionConstants& alienorum::Planet::refraction_constants()
{
    double pressure_now = get_surface_pressure();
    if (refr_cache.valid && refr_cache.key_jd == JDnow && refr_cache.key_pressure == pressure_now)
        return refr_cache;

    temperature = estimate_surface_temperature();       // Kelvins
    double P_hpa = pressure_now * 0.01;                 // Pascals to hPa (millibars)
    double T_c = temperature - 273.15;

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
    double k_extra = 0, shape90 = 0;
    if (extra_at_horizon_deg > 0.0)
    {
        k_extra = fmax(0.5, extra_at_horizon_deg * 1.5);
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
        shape90 = shape(90.0);
    }

    refr_cache.pressure_ratio = pressure_ratio;
    refr_cache.tempfactor = tempfactor;
    refr_cache.min_calc_alt = min_calc_alt;
    refr_cache.k = k;
    refr_cache.extra_at_horizon_deg = extra_at_horizon_deg;
    refr_cache.k_extra = k_extra;
    refr_cache.shape90 = shape90;
    // Final safety clamp -- never exceed the larger of the old flat ceiling or what the bowl
    // itself requires.
    refr_cache.ceiling_deg = fmax(5.0, horizon_lift_deg);
    refr_cache.temperature_k = temperature;
    refr_cache.key_jd = JDnow;
    refr_cache.key_pressure = pressure_now;
    refr_cache.valid = true;
    return refr_cache;
}

// Convert true altitude to observed. Everything here bar the curve evaluation itself is the same
// for every object in the sky, and lives in refraction_constants() above.
double alienorum::Planet::atmospheric_refraction(double alt_rad)
{
    const RefractionConstants &c = refraction_constants();

    temperature = c.temperature_k;
    double h_true_deg = alt_rad * fiftyseven;

    // Saemundsson's formula, then steps 2 and 3 together: clamp the altitude, then run it. Same
    // curve refraction_constants() evaluated at the true horizon to size the bowl term below.
    double delta = h_true_deg - c.min_calc_alt;
    double calc_h_deg;
    if (delta > 40.0 * c.k) calc_h_deg = h_true_deg;
    else calc_h_deg = c.min_calc_alt + c.k * std::log1p(std::exp(delta / c.k));
    if (calc_h_deg < c.min_calc_alt) calc_h_deg = c.min_calc_alt;

    // Written exactly as the saemundsson_deg() lambda in refraction_constants() still writes it --
    // the reciprocal taken first and then multiplied, not folded into a single division. The two
    // round differently in the last bit, and the horizon term below is sized by that lambda's own
    // value at h=0, so the two halves have to agree to the bit or the curve steps at the horizon.
    double R_deg = 0.0;
    double arg_deg = calc_h_deg + 10.3 / (calc_h_deg + 5.11);
    if (arg_deg < 90.0)
    {
        double cot_val = 1.0 / std::tan(arg_deg * fiftyseventh);
        R_deg = fmax(0.0, 1.02 * cot_val * c.pressure_ratio * c.tempfactor / 60.0);
    }

    if (c.extra_at_horizon_deg > 0.0)
    {
        double shape_h = 2.0 / (1.0 + std::exp(fmax(0.0, h_true_deg) / c.k_extra));
        double decay = fmax(0.0, (shape_h - c.shape90) / (1.0 - c.shape90));
        R_deg += c.extra_at_horizon_deg * decay;
    }

    if (R_deg < 0.0) R_deg = 0.0;
    if (R_deg > c.ceiling_deg) R_deg = c.ceiling_deg;

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

    // compute_normal() reduces to cross(pt, yaxis) here, which vanishes as pt lines up with the
    // zenith axis -- and rotate3D() rejects only an exactly-zero axis before normalizing, so a
    // cross product that has decayed into pure rounding noise still comes back out as a full unit
    // vector, just pointing in an arbitrary direction. The rotation then swings pt by the whole
    // refraction angle about that arbitrary direction rather than by the vanishing amount it
    // should.
    //
    // yaxis is (0,1e37,0), so the axis works out to 1e37*(-pt.z, 0, pt.x): with pt at nadir, pt.x
    // and pt.z are the last couple of bits of the magnitude and nothing else. Bug: the ring arch
    // seen from a ringed planet's own surface -- draw_ring_gpu() is the one caller that hands this
    // function a nadir vector, the planet's own centre straight down underfoot -- landing somewhere
    // different on screen every frame while the star field behind it sat perfectly still. Measured
    // on Perennia (14.72 bar, which clamps refraction to ~11.4 degrees at that altitude):
    // consecutive frames swung the "refracted" centre 4 to 19 degrees apart, at random.
    //
    // Returning pt unrotated is the physically right answer at both poles of this geometry in any
    // case: refraction is exactly zero at the zenith, and undefined straight down, there being no
    // direction to bend toward.
    double pm = pt.magnitude(), ym = yaxis.magnitude();
    if (pm <= 0 || ym <= 0 || axis.magnitude() < pm * ym * 1e-9) return pt;

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
    ring_inner_radius = innerDist(rng);

    // Generate Outer Radius
    // Must be larger than inner, and capped tightly by the Roche limit
    std::uniform_real_distribution<double> outerDist(ring_inner_radius * 1.15, roche_limit_zero * 0.98);
    ring_radius = outerDist(rng) + volumetric_mean_radius;

    // Generate Density/Opacity
    // Wider rings or rings around more massive planets tend to be more substantial.
    // Here we just generate a random float, slightly weighted by the size of the ring disk.
    double ringWidthRatio = (ring_radius - ring_inner_radius) / roche_limit_zero;
    
    // Base opacity between 0.1 (faint dust) and 0.8 (highly reflective ice)
    std::uniform_real_distribution<double> opacityDist(0.1, 0.8);
    ring_mean_opacity = opacityDist(rng) + (ringWidthRatio * 0.2); 
    
    // Clamp opacity to 1.0 max
    ring_mean_opacity = std::min(1.0, ring_mean_opacity);
}