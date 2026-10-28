#include <iostream>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <random>
#include <algorithm>
#include <set>
#include <array>
#include <cstdint>
#include <cstring>

#include "planet.h"
#include "moon.h"
#include "chemistry.h"
#include "star.h"
#include "point.h"
#include "patch.h"
#include "shore.h"
#include "celestial.h"
#include "../globals.h"

using namespace alienorum;

std::vector<AstorbRow> astorb;

bool alienorum::render_saved_planet(CelestialObject* object,
    const std::function<void(CelestialObject*)>& render)
{
    if (!object || (object->typeclass() != class_planet && object->typeclass() != class_moon)) return false;
    Planet* source = static_cast<Planet*>(object);
    if (!source->preserve_loaded_parameters || source->rendering_saved_parameters) return false;
    std::lock_guard<std::mutex> lock(source->saved_render_mutex);

    // Generate on an independent physical model; publish textures and rendering caches only.
    // Parsing the snapshot gives the copy its own orbit and atmosphere ownership.
    std::unique_ptr<Planet> copy;
    if (object->typeclass() == class_moon)
    {
        Moon* moon = new Moon;
        copy.reset(moon);
        moon->from_json(static_cast<Moon*>(source)->to_json());
        moon->orbit_type = static_cast<Moon*>(source)->orbit_type;
    }
    else
    {
        copy.reset(new Planet);
        copy->from_json(source->to_json());
    }
    copy->rendering_saved_parameters = true;
    copy->cenobj = source->cenobj;
    if (copy->orbit && source->orbit) copy->orbit->center = source->orbit->center;
    copy->fictitious_map_height = source->fictitious_map_height;
    copy->rnd_seed = source->rnd_seed;
    copy->rng = source->rng;
    copy->m_bump_scale = source->m_bump_scale;
    copy->looked_for_maps = source->looked_for_maps;
    copy->ignore_map_files = source->ignore_map_files;
    copy->has_real_maps = source->has_real_maps;
    copy->cloud_map_url = source->cloud_map_url;
    copy->static_clouds = source->static_clouds;

    struct TextureTransfer
    {
        Planet* source;
        Planet* copy;
        void transfer(Planet* from, Planet* to)
        {
            Map* CelestialObject::*const maps[] =
            {
                &CelestialObject::surf_map, &CelestialObject::cloud_map,
                &CelestialObject::night_map, &CelestialObject::ring_map,
                &CelestialObject::ringx_map
            };
            for (auto member : maps)
            {
                to->*member = from->*member;
                from->*member = nullptr;
                if (to->*member) (to->*member)->set_owner(to);
            }
        }
        TextureTransfer(Planet* original, Planet* working) : source(original), copy(working)
        {
            transfer(source, copy);
        }
        ~TextureTransfer()
        {
            transfer(copy, source);
            source->m_bump_scale = copy->m_bump_scale;
            source->looked_for_maps = copy->looked_for_maps;
            source->ignore_map_files = copy->ignore_map_files;
            source->has_real_maps = copy->has_real_maps;
        }
    } textures(source, copy.get());
    render(copy.get());
    return true;
}

std::vector<std::unique_ptr<Moon>> Planet::generate_fictional_moons(std::mt19937_64& random)
{
    std::vector<std::unique_ptr<Moon>> moons;
    Planet* cel = this;
    if (!orbit || !orbit->center || !(mass > 0) || !(orbit->period > 0)) return moons;
    const auto draw = [&](double low, double high)
    {
        return low+(high-low)*double(random() >> 11)*0x1.0p-53;
    };
    double A = sqrt(cel->orbit->period / oneday) / 4;
    double B = sqrt(cel->mass / earth_mass);
    double C = sqrt(A*B);
    double Hill_over_Roche = cel->Hill_sphere_radius() / cel->Roche_limit();
    double D = C * Hill_over_Roche / 500;

    double P = 0;
    int j=1;
    for (int i=0; i<20; i++)
    {
        Moon *m = new Moon();
        m->rnd_seed = int(random() & 0x7fffffff) | 1;
        m->rng.seed(m->rnd_seed);
        std::string mname = std::string(cel->name) + std::string(" ") + Roman(j);
        snprintf(m->name, sizeof(m->name), "%s", mname.c_str());
        m->namelen = strlen(m->name);
        m->user_added = true;
        m->user_edited = true;
        m->mass = pow(draw(0, 1), 4) * cel->mass / 4000;
        m->volumetric_mean_radius = pow(m->mass / earth_mass, 1.0/3.0) * draw(0.9, 1.5) * earth_radius;
        m->albedo = draw(0.1, 0.6);
        m->BV_color = draw(0.8, 1.8);
        m->cenobj = cel->cenobj;
        m->epoch = J2000;
        m->equinox = draw(0, _pi*2);
        m->major_moon = true;
        m->orbit = new Orbit();
        m->orbit->center = cel;
        m->orbit->arg_periapsis = draw(0, _pi*2);
        m->orbit->ascending_node = draw(0, _pi*2);
        m->orbit->eccentricity = pow(draw(0, 0.1), 10);
        m->orbit->epoch = J2000;
        m->orbit->mean_anomaly = draw(0, _pi*2);
        if (!P)
        {
            // First fictional moon of the planet. Base the SMA off the Roche limit and rings.
            double inner_bound = m->minimum_safe_pericenter();
            if (cel->ring_radius > inner_bound)
            {
                inner_bound = cel->ring_radius;
            }
            double A = inner_bound * (1.1 + pow(draw(0, 1), 4) * 20);
            m->orbit->semimajor_axis = A;
            m->orbit->compute_period(m->mass);

        }
        else
        {
            // Base the SMA off the previous moon.
            m->orbit->period = draw(1.8, 2.2) * P;
            m->orbit->compute_semimajor_axis(m->mass);

        }

        double Roche_dist = m->orbit->semimajor_axis / cel->Roche_limit(m);
        double tidal_modifier = fmin(1.0, fmax(0.0, (Roche_dist - 1.0) / 10.0));        // 0 at Roche limit for perfect tidal alignment.
        m->orbit->inclination = pow(draw(0, 1), 4) * 0.2 * half_pi * tidal_modifier;
        if (draw(0,1) < 0.03) m->orbit->inclination = _pi - m->orbit->inclination;
        m->obliquity = pow(draw(0, 1), 10) * half_pi * tidal_modifier;

        if (draw(0,1) > D)
        {
            P = m->orbit->period;
            delete m->orbit;
            m->orbit = nullptr;
            delete m;
            continue;
        }

        if (m->orbit->semimajor_axis > (0.3*cel->Hill_sphere_radius()))
        {
            delete m->orbit;
            m->orbit = nullptr;
            delete m;
            break;
        }
        m->estimate_rotation();
        if (!m->sidereal_rotational_period) m->sidereal_rotational_period = oneday;
        m->classify();
        m->enforce_orbit_clearance();
        if (m->orbit->semimajor_axis * (1 + m->orbit->eccentricity) > 0.3*cel->Hill_sphere_radius())
        {
            delete m;
            break;
        }
        double disc_area = pow(m->volumetric_mean_radius / earth_radius, 2);
        m->absolute_magnitude = earth_absmag - log(disc_area * m->albedo / earth_albedo) / log(magnbase);
        m->apply_cosmic_shoreline();

        m->enforce_orbit_clearance();
        if (m->orbit->semimajor_axis * (1 + m->orbit->eccentricity) > 0.3*cel->Hill_sphere_radius())
        {
            delete m;
            break;
        }

        if (!moons.empty())
        {
            const Moon& previous = *moons.back();
            const double h = 5*std::cbrt((m->mass+previous.mass)/(3*cel->mass));
            const double ratio = (1+previous.orbit->eccentricity+h)/(1-m->orbit->eccentricity-h);
            m->orbit->semimajor_axis = std::max(m->orbit->semimajor_axis, previous.orbit->semimajor_axis*ratio*1.05);
            m->orbit->compute_period(m->mass);
            if (m->orbit->semimajor_axis*(1+m->orbit->eccentricity) > 0.3*cel->Hill_sphere_radius())
            {
                delete m;
                break;
            }
        }
        m->distance_known = true;
        m->update_location(simnow);
        moons.emplace_back(m);
        P = m->orbit->period;
        j++;
    }
    return moons;
}

void Planet::refresh_host_display_designation()
{
    if (!orbit || !orbit->center || orbit->center->typeclass() != class_star) return;
    const std::string source = display_designation.empty() ? catalog_designation : display_designation;
    if (source.size() < 3 || source.back() < 'b' || source.back() > 'z' ||
        std::islower(static_cast<unsigned char>(source[source.size() - 2]))) return;
    const bool use_designation = name == display_designation || name == catalog_designation;
    display_designation = std::string(orbit->center->name) + " " + source.back();
    if (use_designation)
    {
        snprintf(name, sizeof(name), "%s", display_designation.c_str());
        namelen = 0;
    }
}

void alienorum::Planet::setup_atm_ring_props()
{
    classify();
    apply_cosmic_shoreline();
    if (estimate_habitability() && type != hycean) ensure_atmosphere()->ensure_composition()->generate_fictitious_habitable();
    else ensure_atmosphere()->ensure_composition()->generate_fictitious_for_planet(type);

    temperature = 0;
    estimate_surface_temperature();

    if (typeclass() == class_planet)
    {
        generate_ring_parameters();
    }
}

void alienorum::Planet::apply_cosmic_shoreline()
{
    if (atm && atm->pressure_supplied) return;
    CelestialObject *plc = get_light_center();
    if (plc
        && (user_added || plc->seqno)                                          // Real solar system objects are already known to have/not have an atmosphere.
        && plc->typeclass() == class_star
    )
    {
        // Dereferenced, so it has to be there and it has to be a star: a rogue planet's light
        // center is a null pointer, and a moon of one leads somewhere that is not a Star.
        double shoreline = CosmicShore::calculate_unified_metric(*(Star*)plc, *this);
        double max_atm_pressure = CosmicShore::estimate_surface_pressure(shoreline);
        ensure_atmosphere()->surface_pressure = max_atm_pressure;
        temperature = 0;
        estimate_surface_temperature();
    }
}

bool alienorum::Planet::estimate_habitability()
{
    if (preserve_loaded_parameters)
    {
        const double press = get_surface_pressure();
        const double surface = estimate_surface_temperature();
        return has_water >= 0.05 && is_in_con_HZ() && mass > 0.02 * earth_mass
            && press >= 600 && press < oneatm * 2000
            && surface > 0.9 * water_freezing && surface < 320;
    }
    if (type == waterworld || type == hycean)
    {
        has_water = 1;
    }
    else if (randomize_txgen)
    {
        has_water = 0;
    }

    double press = get_surface_pressure();
    if (press < 150) return false;

    temperature = 0;            // force refresh
    double T_surf = estimate_surface_temperature();
    const double Tboil = water_freezing+100;                                     // at reference pressure

    // Constants for water b.p.
    const double R = 8.314;                                         // J/(mol*K)
    const double DELTA_H_VAP = 40660.0;                             // J/mol
    const double P1 = 1.0e+5;  

    // Clausius-Clapeyron calculation
    double inv_T1 = 1.0 / Tboil;
    double gas_constant_ratio = R / DELTA_H_VAP;
    double pressure_log = std::log(press / P1);

    double inv_T2 = inv_T1 - (gas_constant_ratio * pressure_log);
    double T_boil = 1.0 / inv_T2;
    // std::cout << "At " << (press / oneatm) << " atmospheres, water boils at " << T_boil << " K." << std::endl;

    bool life_possible = false;

    AtmosphereComposition *ac = atm ? atm->ensure_composition() : nullptr;
    life_possible = (is_in_con_HZ() && mass > 0.02 * earth_mass);       // Based on Titan's mass.
    if (randomize_txgen && ac)
    {
        if (life_possible)
        {
            if (!show_taucalc) ac->generate_fictitious_habitable();
            press = get_surface_pressure();
            atm->calculate_tau(press);
            temperature = 0;            // force refresh
            T_surf = estimate_surface_temperature();
        }
        else if (!show_taucalc) ac->generate_fictitious_for_planet(type);
        press = get_surface_pressure();
    }
    life_possible = (life_possible
        && press >= 600
        && T_surf > 0.9*water_freezing && T_surf < 320
        && press < oneatm*2000);

    if (atm) atm->calculate_tau(press);

    if (life_possible)
    {
        temperature = 0;        // force refresh
        T_surf = estimate_surface_temperature();
        #ifdef DEBUG
            /*std::cout << "Surface pressure: " << (press / 101325) << " atm." << std::endl << std::flush;
            std::cout << "Surface temperature: " << T_surf << " K." << std::endl << std::flush;*/
        #endif

        if (!has_water && randomize_txgen)
        {
            if (type == waterworld || type == hycean) has_water = 1;
            else if (T_surf < 0.9 * water_freezing)
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
        temperature = 0;        // force refresh
        if (atm) atm->calculate_tau(press);
        T_surf = estimate_surface_temperature();

        life_possible = (has_water >= 0.05
            && press >= 600
            && T_surf > 0.9*water_freezing && T_surf < 320
            && press < oneatm*2000);

        if (randomize_txgen && ac)
        {
            if (life_possible)
            {
                if (!show_taucalc) ac->generate_fictitious_habitable();
                press = get_surface_pressure();
                atm->calculate_tau(press);
            }
        }
    }

    temperature = 0;                    // force refresh
    press = get_surface_pressure();     // just in case
    if (atm) atm->calculate_tau(press);
    T_surf = estimate_surface_temperature();

    return life_possible;
}

void Planet::set_color_from_type(bool HZ)
{
    if (type == gas_giant)
    {
        BV_color = 0.98;         // average of Jupiter and Saturn.
        #if debug_planet_class_color
        std::cout << name << " B-V color defined as " << BV_color << " based on average of Jupiter and Saturn." << std::endl;
        #endif
    }
    else if (type == rocky)
    {
        if (HZ)
        {
            BV_color = 0.2;                     // estimate same as Earth.
            #if debug_planet_class_color
            std::cout << name << " B-V color defined as " << BV_color << " similar to Earth." << std::endl;
            #endif
        }
        else
        {
            BV_color = 1;
            #if debug_planet_class_color
            std::cout << name << " B-V color defined as " << BV_color << " for a lifeless rocky planet." << std::endl;
            #endif
        }
    }
    else if (type == clearskies)
    {
        BV_color = 0;                               // required for equilibrium_temperature.
        double T = equilibrium_temperature();
        
        // Clamp the temperature within the Class III bounds for the calculation
        double bounded_T = fmin(800.0, fmax(350.0, T));
        
        // Normalize the temperature to a 0.0 to 1.0 scale across the Class III window
        double t_ratio = (bounded_T - 350.0) / (800.0 - 350.0);
        
        // At 350 K (t_ratio = 0), B-V is 0.4 (Neptune-like).
        // At 800 K (t_ratio = 1), B-V drops to -0.15 (deep alkali azure).
        BV_color = 0.4 - (0.55 * t_ratio);
        #if debug_planet_class_color
        std::cout << name << " B-V color defined as " << BV_color << " based on eq temp " << T
            << "." << std::endl;
        #endif
    }
    else if (type == hot_jupiter)
    {
        // https://en.wikipedia.org/wiki/HD_189733_b#/media/File:HD_189733b_blue_planet.png with universal B-V correction added.
        double bluest = -0.1;
        BV_color = bluest;                          // required for equilibrium_temperature.

        // https://iopscience.iop.org/article/10.3847/1538-4357/aadd9e
        // https://experts.arizona.edu/en/publications/absorption-spectra-of-the-prototype-hot-jupiters-determination-of
        // https://www.cambridge.org/core/journals/proceedings-of-the-international-astronomical-union/article/absorption-spectra-of-the-prototype-hotjupiters-determination-of-atmospheric-constituents-and-structure/654C6A452CD94ABC335C2281F8108FD0
        // https://academic.oup.com/mnras/article/437/1/46/992656
        // https://arxiv.org/html/2603.02409
        // https://www.aanda.org/articles/aa/full_html/2019/07/aa35089-19/aa35089-19.html
        // https://academic.oup.com/mnras/article/426/3/2483/989230
        // https://repository.arizona.edu/handle/10150/628273
        double T = equilibrium_temperature();
        BV_color = 0.98 + (bluest-0.98) / (1.0 + 0.002 * fabs(T-1200));
        #if debug_planet_class_color
        std::cout << name << " B-V color defined as " << BV_color << " based on eq temp " << T
            << "." << std::endl;
        #endif
    }
    else if (type == ice_giant)
    {
        BV_color = 0.49;    // average of Uranus and Neptune.
        #if debug_planet_class_color
        std::cout << name << " B-V color defined as " << BV_color << " average of Uranus and Neptune." << std::endl;
        #endif
    }
    else if (type == icy)
    {
        BV_color = 0.6;
        #if debug_planet_class_color
        std::cout << name << " B-V color defined as " << BV_color << " for an icy world." << std::endl;
        #endif
    }
    else if (type == lavaworld)
    {
        BV_color = 1.3;
        #if debug_planet_class_color
        std::cout << name << " B-V color defined as " << BV_color << " for a lava world." << std::endl;
        #endif
    }
    else if (type == waterworld || type == hycean)
    {
        BV_color = -0.3;
        #if debug_planet_class_color
        std::cout << name << " B-V color defined as " << BV_color << " for an ocean world." << std::endl;
        #endif
    }
}

double alienorum::Planet::get_atmospheric_tau()
{
    if (!atm) return 0;
    if (rendering_saved_parameters && !atm->comp) return atm->tau;

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
    if (lock_type && (uses_rocky_map(type) || uses_gaseous_map(type))) return;
    classify(is_in_con_HZ(), false, preserve_loaded_parameters);
}

void Planet::classify(bool HZ, bool mnrk, bool ck)
{
    if (lock_type && (uses_rocky_map(type) || uses_gaseous_map(type)))
    {
        classification_evidence = {{"basis", "explicit_type_lock"}, {"class_code", static_cast<int>(type)}};
        return;
    }
    const cel_obj_type previous_type = type;
    if (mnrk)
    {
        if (mass_evidence == PlanetMeasurement::unknown && std::isfinite(mass) && mass > 0)
            mass_evidence = msini > 0 && mass <= msini * 1.000001 ? PlanetMeasurement::minimum : PlanetMeasurement::observed;
        if (radius_evidence == PlanetMeasurement::unknown && std::isfinite(volumetric_mean_radius) && volumetric_mean_radius > 0)
            radius_evidence = PlanetMeasurement::observed;
    }
    const bool valid_mass = std::isfinite(mass) && mass > 0;
    const bool valid_radius = std::isfinite(volumetric_mean_radius) && volumetric_mean_radius > 0;
    const bool measured_radius = valid_radius && radius_evidence == PlanetMeasurement::observed;
    const bool measured_density = valid_mass && measured_radius && mass_evidence == PlanetMeasurement::observed;
    const bool lower_density_bound = valid_mass && measured_radius && mass_evidence == PlanetMeasurement::minimum;
    const double density = valid_mass && valid_radius ? mass / sphere_volume(volumetric_mean_radius) * 1e-6 : NAN;
    // Preserve the RV lower bound separately from the density of the adopted physical model.
    const double minimum_mass = lower_density_bound ? mass
        : measured_radius && valid_mass && !measured_density && std::isfinite(msini) && msini > 0 && msini <= mass
            ? msini : NAN;
    const double minimum_density = std::isfinite(minimum_mass)
        ? minimum_mass / sphere_volume(volumetric_mean_radius) * 1e-6 : NAN;
    const double density_constraint = measured_radius && valid_mass ? density : NAN;
    classification_evidence =
    {
        {"mass_evidence", static_cast<int>(mass_evidence)}, {"radius_evidence", static_cast<int>(radius_evidence)},
        {"density_g_cm3", std::isfinite(density) ? json(density) : json(nullptr)},
        {"density_observed", measured_density}, {"density_is_lower_bound", lower_density_bound},
        {"density_estimated", std::isfinite(density) && !measured_density && !lower_density_bound},
        {"minimum_density_g_cm3", std::isfinite(minimum_density) ? json(minimum_density) : json(nullptr)},
        {"density_constraint_g_cm3", std::isfinite(density_constraint) ? json(density_constraint) : json(nullptr)},
        {"density_constraint_basis", !std::isfinite(density_constraint) ? "radius_unmeasured_or_mass_absent"
            : measured_density ? "observed_mass_and_radius"
            : lower_density_bound ? "minimum_mass_and_observed_radius" : "adopted_mass_and_observed_radius"},
        {"metallicity_applied", false}
    };

    Star* host = nullptr;
    Orbit* stellar_orbit = nullptr;
    std::set<const CelestialObject*> visited;
    CelestialObject* body = this;
    while (body && body->orbit && body->orbit->center && visited.insert(body).second)
    {
        CelestialObject* parent = body->orbit->center;
        if (parent->typeclass() == class_star)
        {
            host = static_cast<Star*>(parent);
            stellar_orbit = body->orbit;
            break;
        }
        body = parent;
    }
    double irradiation = NAN, equilibrium = NAN;
    if (host && stellar_orbit && std::isfinite(stellar_orbit->semimajor_axis) && stellar_orbit->semimajor_axis > 0)
    {
        const double teff = host->temperature > 0 ? host->temperature : host->estimate_temperature();
        const double bolometric = host->absolute_magnitude + Star::bolometric_correction(teff);
        const double luminosity = pow(magnbase, 4.74 - bolometric);
        irradiation = luminosity / pow(stellar_orbit->semimajor_axis / AU, 2);
        // A fixed neutral albedo for an unmeasured surface avoids a class/color feedback loop.
        const double bond = std::isfinite(albedo) && albedo > 0 ? std::clamp(albedo, 0.0, 1.0) : 0.3;
        equilibrium = pow(irradiation * (1.0 - bond) / (4.0 * STEFAN_BOLTZMANN_NORM), 0.25);
        classification_evidence["host"] = host->name;
        classification_evidence["stellar_chemistry"] = host->chemistry_diagnostics();
        classification_evidence["bond_albedo"] = bond;
    }
    const double surface_temperature = std::isfinite(temperature) && temperature > 0 ? temperature : equilibrium;
    classification_evidence["irradiation_earth"] = std::isfinite(irradiation) ? json(irradiation) : json(nullptr);
    classification_evidence["equilibrium_temperature_k"] = std::isfinite(equilibrium) ? json(equilibrium) : json(nullptr);
    classification_evidence["surface_temperature_k"] = std::isfinite(surface_temperature) ? json(surface_temperature) : json(nullptr);
    if (!valid_mass && !measured_radius)
    {
        if (!uses_rocky_map(type) && !uses_gaseous_map(type)) type = rocky;
        classification_evidence["basis"] = "insufficient_planetary_properties";
        classification_evidence["class_code"] = static_cast<int>(type);
        return;
    }

    const double radius_earth = volumetric_mean_radius / earth_radius;
    const bool dense_solid = std::isfinite(density_constraint) && density_constraint >= 3.2 && mass < jupiter_mass;
    bool solid = dense_solid || (valid_mass && mass < rocky_mass_cutoff && (!measured_radius || radius_earth <= 1.8)) ||
        (!valid_mass && measured_radius && radius_earth <= 1.8);
    classification_evidence["basis"] = measured_radius && valid_mass
        ? classification_evidence["density_constraint_basis"].get<std::string>()
        : measured_radius ? "observed_radius" : "mass_with_radius_unavailable_or_estimated";

    const bool inclination_mass = mass_evidence == PlanetMeasurement::estimated
        && std::isfinite(msini) && msini > 0 && msini <= mass;
    classification_evidence["mass_from_rv_inclination"] = inclination_mass;
    if (host && valid_mass && (mass_evidence == PlanetMeasurement::observed || inclination_mass)
        && !measured_radius && typeclass() == class_planet)
    {
        const double error_mass = inclination_mass ? msini : mass;
        const double relative_error = std::isfinite(mass_error_upper) && std::isfinite(mass_error_lower)
            ? fmax(fabs(mass_error_upper), fabs(mass_error_lower)) / error_mass : NAN;
        const MetallicityPrior prior = planet_envelope_prior(*host, mass / earth_mass, irradiation, relative_error);
        classification_evidence["metallicity_model"] = prior.reason;
        if (prior.applicable)
        {
            classification_evidence[prior.class_balanced ? "baseline_large_radius_score" : "baseline_large_radius_probability"] = prior.baseline;
            classification_evidence[prior.class_balanced ? "large_radius_score" : "large_radius_probability"] = prior.probability;
            // The balanced fit supplies the decision; the mass-only fit is a diagnostic.
            if (prior.class_balanced ||
                (solid == (prior.baseline < 0.5) && (prior.baseline < 0.5) != (prior.probability < 0.5)))
            {
                solid = prior.probability < 0.5;
                classification_evidence["metallicity_applied"] = true;
            }
        }
    }
    else classification_evidence["metallicity_model"] = "direct_observations_or_no_eligible_host";

    if (solid)
    {
        if (std::isfinite(surface_temperature) && surface_temperature > lava_T_cutoff) type = lavaworld;
        else
        {
            // Dense silicate bodies remain rocky in the cold; porous warm bodies can be rocky too.
            const bool cold = std::isfinite(equilibrium) ? equilibrium < 170.0
                : std::isfinite(surface_temperature) && surface_temperature < 170.0;
            type = cold && !dense_solid ? icy : rocky;
        }
    }
    else
    {
        const bool giant = (valid_mass && mass >= giant_mass_cutoff) || (measured_radius && radius_earth >= 6.0);
        if (!giant) type = ice_giant;
        else if (stellar_orbit && stellar_orbit->period > 0 && stellar_orbit->period < 10 * oneday) type = hot_jupiter;
        else if (std::isfinite(equilibrium) && equilibrium >= 350 && equilibrium < 800) type = clearskies;
        else type = gas_giant;
    }
    classification_evidence["class_code"] = static_cast<int>(type);
    if (valid_mass && (!valid_radius || (radius_evidence == PlanetMeasurement::estimated && previous_type != type)))
    {
        estimate_radius();
        classification_evidence["radius_evidence"] = static_cast<int>(radius_evidence);
        if (std::isfinite(volumetric_mean_radius) && volumetric_mean_radius > 0)
        {
            classification_evidence["density_g_cm3"] = mass / sphere_volume(volumetric_mean_radius) * 1e-6;
            classification_evidence["density_estimated"] = true;
        }
    }
    if (!ck) set_color_from_type(HZ);
}

LocalClimate Planet::local_climate()
{
    LocalClimate climate;
    climate.mean_k = estimate_surface_temperature();
    if (!std::isfinite(climate.mean_k) || climate.mean_k < 2.7) climate.mean_k = 2.7;
    const double pressure = std::max(0.0, get_surface_pressure()) / oneatm;
    climate.contrast_k = climate.mean_k * (0.04 + 0.25 / (1.0 + pressure));
    climate.redistribution = pressure / (pressure + 0.1);
    const double gravity = estimate_surface_gravity();
    climate.lapse_k_m = std::isfinite(gravity)
        ? 0.0065 * std::clamp(gravity, 0.0, 3.0) * pressure / (pressure + 0.01) : 0;
    climate.sea_level_m = m_bump_scale > 0 ? m_bump_scale * (has_water - 0.5) : 0;
    climate.dry_belt_k = climate.mean_k * 0.04 * pressure / (pressure + 0.01);
    climate.regional_k = climate.mean_k * 0.012 / (1.0 + pressure / 10.0);
    climate.stellar_locked = orbit && orbit->center && orbit->center->typeclass() == class_star
        && is_tidal_locked();
    if (climate.stellar_locked)
    {
        CelestialObject* source = get_light_center();
        const double angle = local_solar_time(0);
        if (source && angle >= 0)
        {
            const double longitude = _pi - angle;
            const double latitude = source->Decl_as_radians(location);
            climate.sun_x = -cos(latitude) * cos(longitude);
            climate.sun_y = -cos(latitude) * sin(longitude);
            climate.sun_z = sin(latitude);
        }
    }
    return climate;
}

double alienorum::Planet::est_local_temp(double lat, double lon)
{
    const LocalClimate climate = local_climate();
    if (!std::isfinite(lat) || !std::isfinite(lon)) return climate.mean_k;
    lat = std::clamp(lat, -half_pi, half_pi);
    const double cos_lat = cos(lat);
    const double elevation = surf_map ? surf_map->elevation_at_bilinear(lat, lon) : climate.sea_level_m;
    return climate.temperature(-cos_lat * cos(lon), -cos_lat * sin(lon), sin(lat), elevation);
}

void Planet::set_explicit_mass(double value)
{
    mass = value;
    mass_evidence = std::isfinite(value) && value > 0 ? PlanetMeasurement::observed : PlanetMeasurement::unknown;
    msini = 0;
    mass_error_upper = mass_error_lower = NAN;
    sync_host_statistics();
}

void Planet::set_explicit_radius(double value)
{
    volumetric_mean_radius = value;
    radius_evidence = std::isfinite(value) && value > 0 ? PlanetMeasurement::observed : PlanetMeasurement::unknown;
    radius_error_upper = radius_error_lower = NAN;
}

void Planet::estimate_radius()
{
    radius_evidence = PlanetMeasurement::estimated;
    // https://doi.org/10.1051/0004-6361/202348690
    if ((mass < rocky_mass_cutoff && !uses_gaseous_map(type))
        || uses_rocky_map(type))
        volumetric_mean_radius = 1.02 * earth_radius * pow(mass/earth_mass, 0.27);
    else if (mass < giant_mass_cutoff) volumetric_mean_radius = fmax(2.0, 0.56 * pow(mass/earth_mass, 0.67)) * earth_radius;
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
    else if (type == waterworld || type == hycean ) sidereal_rotational_period = 2.06e+6 / log(mass);              // WAG: average of solid and gas.
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
    if (uses_gaseous_map(type)) return 1;

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
    CelestialObject *light_center = get_light_center();
    if (!light_center)
    {
        amt_lit = 0;
        return INFINITY;
    }

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
    const double P_HALF = 100.0;   // Pa
    double p_pa = fmax(get_surface_pressure(), 0.0);
    double relief = 1.0 / (1.0 + log1p(p_pa / P_HALF));

    return 0.001 * volumetric_mean_radius * relief / log(20);
}

// Pressure scale height, meters -- the altitude over which the air thins by a factor of e,
// H = RT/(Mg) from the barometric formula. This is the natural thickness of the bright band of
// atmosphere seen on a planet's limb from space.
//
// Returns 0 for an airless body, which reads as "no glow at all" downstream.
double Planet::estimate_scale_height()
{
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
    else if (type == clearskies ) est_albedo = 0.1;
    else if (type == ice_giant  ) est_albedo = 0.3;
    else if (type == waterworld || type == hycean ) est_albedo = 0.4;
    else if (type == icy        ) est_albedo = 0.8;
    else if (type == rocky
        || type == lavaworld    ) est_albedo = (mass > 0.5 * earth_mass) ? 0.5 : 0.1;
    absolute_magnitude = fmax(-10, earth_absmag - log(p_rad_e*p_rad_e*est_albedo/earth_albedo) / log(magnbase));
    albedo = est_albedo;
}

void Planet::update_location(double tmnow)
{
    if (orbit) update_orbit_location(tmnow);
    sync_host_statistics();
}

bool Planet::counts_for_host_statistics() const
{
    // An absent mass is not evidence that an identified planet falls below the cutoff.
    return typeclass() == class_planet && !asteroid_no
        && (!std::isfinite(mass) || mass <= 0 || mass >= planet_count_mass_cutoff);
}

void Planet::refresh_host_statistics_if_changed()
{
    if (typeclass() != class_planet || !cels || seqno < 0 || seqno >= ncelobjs || cels[seqno] != this) return;
    if (deleted || !counts_for_host_statistics())
    {
        if (statistics_host) sync_host_statistics();
        return;
    }
    if (statistics_host && statistics_host_index >= 0 && statistics_host_index < ncelobjs
        && cels[statistics_host_index] == statistics_host && !statistics_host->deleted
        && orbit && orbit->center == statistics_host
        && cached_hz_host == statistics_host && cached_in_cons_hz == orbit->semimajor_axis
        && cached_hz_stellar_sma == orbit->semimajor_axis && cached_hz_mass == mass
        && cached_hz_temperature == statistics_host->temperature
        && cached_hz_magnitude == statistics_host->absolute_magnitude
        && statistics_hz == cache_in_cons_hz && !statistics_host->pl_indices.empty()
        && statistics_host->has_planets == static_cast<int>(statistics_host->pl_indices.size())) return;
    sync_host_statistics();
}

void Planet::sync_host_statistics()
{
    if (typeclass() != class_planet || !cels || seqno < 0 || seqno >= ncelobjs || cels[seqno] != this) return;
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    CelestialObject* light = !deleted && counts_for_host_statistics() ? get_light_center() : nullptr;
    Star* host = light && !light->deleted && light->typeclass() == class_star ? static_cast<Star*>(light) : nullptr;
    if (statistics_host != host && statistics_host_index >= 0 && statistics_host_index < ncelobjs
        && cels[statistics_host_index] == statistics_host)
    {
        auto& indices = statistics_host->pl_indices;
        indices.erase(std::remove(indices.begin(), indices.end(), seqno), indices.end());
        statistics_host->refresh_planet_statistics();
    }
    const bool hz = host && is_in_con_HZ();
    const bool changed = statistics_host != host || statistics_hz != hz;
    statistics_host = host;
    statistics_host_index = host ? host->seqno : -1;
    statistics_hz = hz;
    if (!host) return;
    const bool missing = std::find(host->pl_indices.begin(), host->pl_indices.end(), seqno) == host->pl_indices.end();
    if (missing) host->pl_indices.push_back(seqno);
    if (missing || changed || host->has_planets != static_cast<int>(host->pl_indices.size())) host->refresh_planet_statistics();
}

void Planet::rebuild_host_statistics()
{
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    if (!cels) return;
    int count = 0;
    while (count < MAX_CELOBJS && cels[count]) ++count;
    ncelobjs = count;
    for (int i = 0; i < ncelobjs; ++i)
    {
        if (!cels[i] || cels[i]->typeclass() != class_star) continue;
        Star* host = static_cast<Star*>(cels[i]);
        host->pl_indices.clear();
        host->has_planets = host->has_hz_planets = 0;
    }
    for (int i = 0; i < ncelobjs; ++i)
    {
        if (cels[i] && cels[i]->typeclass() == class_planet) static_cast<Planet*>(cels[i])->sync_host_statistics();
    }
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
    struct Entry
    {
        uint64_t color = 0, geometric = 0, revision = 0;
        double result = 0;
    };
    struct Cache
    {
        std::vector<LocalPatchPoint> source;
        LocalPatchPredictor prototype;
        std::array<Entry, 256> entries{};
        uint64_t revision = 0;
        bool source_valid = false;
    };
    static thread_local Cache cache;
    bool source_matches = cache.source_valid && cache.source.size() == dataset_bond_albedines.size();
    for (size_t index = 0; source_matches && index < cache.source.size(); ++index)
    {
        const auto& a = cache.source[index];
        const auto& b = dataset_bond_albedines[index];
        source_matches = a.a == b.a && a.b == b.b && a.c == b.c;
    }
    if (!source_matches)
    {
        cache.source_valid = false;
        cache.source = dataset_bond_albedines;
        cache.prototype.load_data(dataset_bond_albedines);
        if (!++cache.revision)
        {
            for (auto& entry : cache.entries) entry.revision = 0;
            cache.revision = 1;
        }
        cache.source_valid = true;
    }
    Entry* entry = nullptr;
    uint64_t color = 0, geometric = 0;
    static_assert(sizeof(double) == sizeof(uint64_t), "Bond cache requires 64-bit doubles.");
    if (std::isfinite(BV_color) && std::isfinite(albedo))
    {
        std::memcpy(&color, &BV_color, sizeof(color));
        std::memcpy(&geometric, &albedo, sizeof(geometric));
        uint64_t hash = color ^ (geometric + 0x9e3779b97f4a7c15ULL + (color << 6) + (color >> 2));
        hash = (hash ^ (hash >> 30))*0xbf58476d1ce4e5b9ULL;
        hash = (hash ^ (hash >> 27))*0x94d049bb133111ebULL;
        hash ^= hash >> 31;
        entry = &cache.entries[hash%cache.entries.size()];
        if (entry->revision == cache.revision && entry->color == color && entry->geometric == geometric)
            return entry->result;
    }
    // Copy the unsorted prototype so every miss retains the original interpolation order.
    LocalPatchPredictor model = cache.prototype;
    const double result = fmax(0, fmin(1, model.predict(BV_color, albedo)));
    if (entry) *entry = {color, geometric, cache.revision, result};
    return result;
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
    if (rendering_saved_parameters)
        return temperature ? temperature : temperature_at_pressure(get_surface_pressure());
    if (temperature)
    {
        // Catalog and editor temperatures still constrain which gases can remain aloft.
        if (atm && atm->comp && !uses_gaseous_map(type))
            temperature = condense_atmosphere(NAN, temperature);
        return temperature;
    }
    if (uses_gaseous_map(type)) return temperature = equilibrium_temperature();         // For gas giants, just give equilibrium temp.
    const double surface_k = temperature_at_pressure(get_surface_pressure());
    return temperature = condense_atmosphere(equilibrium_temperature(), surface_k);
}

double Planet::condense_atmosphere(double equilibrium_k, double surface_k)
{
    return surface_k;

    // Leave the rest of this. If we ever revisit this feature later, we're going to want the original code.
    #if 0
    if (!atm || !atm->comp || uses_gaseous_map(type) || !(atm->surface_pressure > 0)
        || !std::isfinite(atm->surface_pressure) || !std::isfinite(surface_k) || surface_k <= 0)
        return surface_k;

    // Clausius-Clapeyron screening with rounded phase constants in K and J/mol.
    // Below the triple point, include fusion in the sublimation enthalpy.
    // Phase data: https://webbook.nist.gov/chemistry/ (Phase change data).
    struct Phase
    {
        double AtmosphereComposition::*portion;
        double reference_k, reference_pa, triple_k, critical_k, vapor_j, fusion_j;
    };
    static const Phase phases[] =
    {
        {&AtmosphereComposition::H2_portion, 20.37, oneatm, 13.8, 33.15, 904, 117},
        {&AtmosphereComposition::He_portion, 4.22, oneatm, 0, 5.20, 83, 0},
        {&AtmosphereComposition::N2_portion, 77.36, oneatm, 63.15, 126.19, 5560, 720},
        {&AtmosphereComposition::O2_portion, 90.19, oneatm, 54.36, 154.58, 6820, 444},
        {&AtmosphereComposition::O3_portion, 161.3, oneatm, 80.7, 261, 15200, 2100},
        {&AtmosphereComposition::CO2_portion, 216.59, 518500, 216.59, 304.13, 16000, 9000},
        {&AtmosphereComposition::CH4_portion, 111.66, oneatm, 90.69, 190.56, 8200, 940},
        {&AtmosphereComposition::SO2_portion, 263.05, oneatm, 197.7, 430.64, 24900, 7400},
        {&AtmosphereComposition::H2O_portion, 273.16, 611.657, 273.16, 647.10, 45050, 6010},
        {&AtmosphereComposition::H2S_portion, 212.85, oneatm, 187.6, 373.1, 18700, 2400},
        {&AtmosphereComposition::HCN_portion, 298.85, oneatm, 259.9, 456.7, 25200, 8400},
        {&AtmosphereComposition::NH3_portion, 239.82, oneatm, 195.4, 405.4, 23350, 5650},
        {&AtmosphereComposition::C2H6_portion, 184.55, oneatm, 90.35, 305.32, 14700, 2720},
        {&AtmosphereComposition::N2O_portion, 184.67, oneatm, 182.3, 309.52, 16500, 6540},
        {&AtmosphereComposition::CO_portion, 81.64, oneatm, 68.15, 132.86, 6040, 840},
        {&AtmosphereComposition::Ar_portion, 87.30, oneatm, 83.81, 150.69, 6430, 1180}
    };
    AtmosphereComposition& composition = *atm->comp;
    composition.enforce_integrity();
    double partial[16], total_fraction = 0;
    for (int i = 0; i < 16; ++i)
    {
        const double fraction = composition.*(phases[i].portion);
        total_fraction += fraction;
        partial[i] = atm->surface_pressure * fraction;
    }
    if (!(total_fraction > 0))
    {
        atm->surface_pressure = atm->tau = 0;
        return std::isfinite(equilibrium_k) ? equilibrium_k : surface_k;
    }
    // Fractions describe relative abundances, as in mean_molar_mass(). Missing catalog
    // constituents must not become an anonymous gas that survives every cold limit.
    for (double& value : partial) value /= total_fraction;
    for (int iteration = 0; iteration < 256; ++iteration)
    {
        const double previous_pressure = atm->surface_pressure;
        double pressure = 0;
        bool changed = false;
        for (int i = 0; i < 16; ++i)
        {
            const Phase& phase = phases[i];
            if (surface_k < phase.critical_k && partial[i] > 0)
            {
                double exponent = phase.vapor_j / 8.314462618
                    * (1.0 / phase.reference_k - 1.0 / surface_k);
                if (surface_k < phase.triple_k)
                    exponent += phase.fusion_j / 8.314462618
                        * (1.0 / phase.triple_k - 1.0 / surface_k);
                const double saturation = phase.reference_pa * std::exp(std::min(700.0, exponent));
                const double previous = partial[i];
                partial[i] = std::min(partial[i], saturation);
                if (partial[i] < 1e-8) partial[i] = 0;
                changed = changed || partial[i] != previous;
            }
            pressure += partial[i];
        }
        if (!changed) return surface_k;
        if (!std::isfinite(equilibrium_k))
        {
            // Preserve a supplied surface temperature unless condensation changes its
            // greenhouse contribution; do not replace observed warm temperatures.
            atm->calculate_tau(previous_pressure);
            equilibrium_k = surface_k / std::pow(1.0 + 1.8 * atm->tau, 0.25);
        }
        if (pressure < 1e-6) pressure = 0;
        atm->surface_pressure = pressure;
        for (int i = 0; i < 16; ++i)
            composition.*(phases[i].portion) = pressure > 0 ? partial[i] / pressure : 0;
        atm->calculate_tau(pressure);
        const double next_k = equilibrium_k * std::pow(1.0 + 1.8 * atm->tau, 0.25);
        const bool converged = std::abs(previous_pressure - pressure) <= 1e-9 * std::max(1.0, previous_pressure)
            && std::abs(next_k - surface_k) < 1e-7;
        surface_k = next_k;
        if (converged || pressure == 0) break;
    }
    return surface_k;
    #endif
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

double alienorum::Planet::mean_instellation()
{
    CelestialObject* lumcen = get_light_center();
    if (!lumcen) return 0;
    CelestialObject* distancer = this;
    while (distancer->orbit && distancer->orbit->center != lumcen)
        distancer = distancer->orbit->center;
    if (!distancer->orbit || !distancer->orbit->center) return 0;
    double mean_dist;

    if (!distancer->orbit->semimajor_axis) distancer->orbit->compute_semimajor_axis(distancer->mass);
    if (!distancer->orbit->semimajor_axis) mean_dist = location.distance_to(distancer->orbit->center->location);
    else mean_dist = distancer->orbit->semimajor_axis;

    double luminosity = pow(magnbase, 4.85 - lumcen->absolute_magnitude);                   // scaled so sun=1
    double mean_dist_AU = mean_dist * invAU;

    return luminosity / (mean_dist_AU*mean_dist_AU);
}

bool Planet::set_habitable_orbit()
{
    if (!orbit || !orbit->center || orbit->center->typeclass() != class_star) return false;
    Star* host = static_cast<Star*>(orbit->center);
    const double temp = host->estimate_temperature();
    const double luminosity = std::pow(magnbase,
        4.74 - host->absolute_magnitude - Star::bolometric_correction(temp));
    if (!std::isfinite(luminosity) || luminosity <= 0) return false;

    // Search in luminosity-scaled distance using the same zone model as the editor.
    const double scale = AU * std::sqrt(luminosity);
    const double original = orbit->semimajor_axis;
    double inner = 0, outer = 0;
    for (double radius = 0.2; radius <= 5.0; radius *= 1.01)
    {
        orbit->semimajor_axis = scale * radius;
        cached_in_cons_hz = -1;
        if (is_in_con_HZ())
        {
            if (!inner) inner = orbit->semimajor_axis;
            outer = orbit->semimajor_axis;
        }
    }
    orbit->semimajor_axis = inner ? std::sqrt(inner * outer) : original;
    cached_in_cons_hz = -1;
    if (!inner) return false;
    orbit->compute_period(mass);
    sync_host_statistics();
    return true;
}

bool Planet::is_in_con_HZ()
{
    if (!orbit || !orbit->center) return false;
    // See est_bolometric_flux(): the light center is not guaranteed to be a star, and nothing
    // without one has a habitable zone to be in.
    CelestialObject *lc = get_light_center();
    if (!lc || lc->typeclass() != class_star) return false;
    Star *s = (Star*)lc;
    const CelestialObject* outer = this;
    while (outer->orbit && outer->orbit->center && outer->orbit->center != s) outer = outer->orbit->center;
    const double stellar_sma = outer->orbit ? outer->orbit->semimajor_axis : 0;
    if (cached_in_cons_hz == orbit->semimajor_axis && cached_hz_host == s && cached_hz_mass == mass
        && cached_hz_temperature == s->temperature && cached_hz_magnitude == s->absolute_magnitude
        && cached_hz_stellar_sma == stellar_sma) return cache_in_cons_hz;

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
    cached_hz_host = s;
    cached_hz_mass = mass;
    cached_hz_temperature = s->temperature;
    cached_hz_magnitude = s->absolute_magnitude;
    cached_hz_stellar_sma = stellar_sma;

    return cache_in_cons_hz;
}

Planet::Planet()
{
    _class = class_planet;
    type = rocky;
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
    towrite["pressure_supplied"] = pressure_supplied;
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
    for (const char* key : {"surface_pressure", "SurfacePressure"})
    {
        if (j.contains(key) && j[key].is_number())
        {
            const double pressure = j[key].get<double>();
            if (std::isfinite(pressure) && pressure >= 0)
            {
                surface_pressure = pressure;
                pressure_supplied = j.value("pressure_supplied", true);
            }
        }
    }
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
    towrite["lock_system_plane"] = lock_system_plane;
    towrite["mass_evidence"] = static_cast<int>(mass_evidence);
    towrite["radius_evidence"] = static_cast<int>(radius_evidence);
    if (std::isfinite(mass_error_upper)) towrite["mass_error_upper"] = mass_error_upper;
    if (std::isfinite(mass_error_lower)) towrite["mass_error_lower"] = mass_error_lower;
    if (std::isfinite(radius_error_upper)) towrite["radius_error_upper"] = radius_error_upper;
    if (std::isfinite(radius_error_lower)) towrite["radius_error_lower"] = radius_error_lower;
    if (classification_evidence.is_object()) towrite["classification_evidence"] = classification_evidence;

    if (albedo) towrite["albedo"] = albedo;
    towrite["opposition_surge"] = opposition_surge;
    if (atm) towrite["atmosphere"] = atm->to_json();
    if (J2) towrite["J2"] = J2;
    towrite["overcast"] = overcast;
    towrite["has_water"] = has_water;
    towrite["ring_status_known"] = ring_status_known;
    towrite["cloud_map_url"] = cloud_map_url;
    towrite["static_clouds"] = static_clouds;

    // Both of these come from a catalog originally -- the astorb row number, and the flag that
    // says the catalog stated a type we are not to second-guess in classify(). They used not to
    // be written, which left a saved asteroid without its number after a reload, and so out of
    // the one place that number is read: the pass in visuals.cpp that keeps the asteroids out of
    // the ordinary planet drawing. Zero and false are the "no such thing" values, so as with the
    // fields above, they are written only when they say something.
    if (asteroid_no) towrite["asteroid_no"] = asteroid_no;
    if (lock_type) towrite["lock_type"] = lock_type;
    if (msini)
    {
        towrite["msini"] = msini;
    }
    towrite["transiting"] = transiting;
    if (!catalog_designation.empty()) towrite["catalog_designation"] = catalog_designation;
    if (!display_designation.empty()) towrite["display_designation"] = display_designation;
    if (std::isfinite(inclination_error_upper)) towrite["inclination_error_upper"] = inclination_error_upper * fiftyseven;
    if (std::isfinite(inclination_error_lower)) towrite["inclination_error_lower"] = inclination_error_lower * fiftyseven;

    // A ring system's geometry used not to be written at all, on the understanding that
    // generate_ring_parameters() would invent one again on the next load. That is fine for a body
    // whose rings we made up in the first place, and wrong for one whose rings are a stated fact:
    // a hand-edited radius, or the Inner/Outer a Celestia add-on gave -- both were silently
    // replaced by a fresh guess. Written in kilometres, as every other distance in these files is,
    // and only when there are rings to describe.
    if (ring_radius)
    {
        towrite["ring_radius"] = ring_radius * 1e-3;
        towrite["ring_inner_radius"] = ring_inner_radius * 1e-3;
        towrite["ring_mean_opacity"] = ring_mean_opacity;
    }
    towrite["transparent_clouds"] = transparent_clouds;
    towrite["vegetation_r"] = vegetation_r;
    towrite["vegetation_g"] = vegetation_g;
    towrite["vegetation_b"] = vegetation_b;
    towrite["plant_trunk_height"] = plant_trunk_height;
    towrite["plant_trunk_width"] = plant_trunk_width;
    towrite["plant_crown_size"] = plant_crown_size;
    towrite["plant_crown_oblateness"] = plant_crown_oblateness;

    return towrite;
}

bool Planet::from_json(json j)
{
    temperature = 0;
    CelestialObject::from_json(j);
    lock_system_plane = j.value("lock_system_plane", false);
    preserve_loaded_parameters = true;
    // Optional zero-valued fields are omitted by older saves; a reload must clear stale values.
    albedo = msini = J2 = ring_radius = ring_inner_radius = ring_mean_opacity = 0;
    asteroid_no = 0;
    lock_type = false;
    inclination_error_upper = inclination_error_lower = NAN;
    ring_status_known = j.value("ring_status_known", false);
    cloud_map_url = j.value("cloud_map_url", std::string());
    static_clouds = j.value("static_clouds", true);
    catalog_designation.clear();
    display_designation.clear();
    delete atm;
    atm = nullptr;
    refr_cache.valid = false;
    // Older saves used these retired codes for solid surfaces. Translate before map dispatch.
    switch (static_cast<int>(type))
    {
        case 0x402: type = icy; break;
        case 0x403: type = rocky; break;
        case 0x404: type = lavaworld; break;
        default: break;
    }
    const auto evidence = [&](const char* key)
    {
        auto it = j.find(key);
        if (it == j.end() || !it->is_number_integer()) return PlanetMeasurement::unknown;
        const int value = it->get<int>();
        return value >= 0 && value <= static_cast<int>(PlanetMeasurement::estimated)
            ? static_cast<PlanetMeasurement>(value) : PlanetMeasurement::unknown;
    };
    mass_evidence = evidence("mass_evidence");
    radius_evidence = evidence("radius_evidence");
    const auto optional_number = [&](const char* key) -> double
    {
        auto it = j.find(key);
        return it != j.end() && it->is_number() && std::isfinite(it->get<double>()) ? it->get<double>() : NAN;
    };
    mass_error_upper = optional_number("mass_error_upper");
    mass_error_lower = optional_number("mass_error_lower");
    radius_error_upper = optional_number("radius_error_upper");
    radius_error_lower = optional_number("radius_error_lower");
    classification_evidence = j.contains("classification_evidence") && j["classification_evidence"].is_object()
        ? j["classification_evidence"] : json(nullptr);
    try { j.at("albedo").get_to(albedo); } catch (...) { ; }
    try
    {
        j.at("msini").get_to(msini);
    }
    catch (...)
    {
        ;
    }
    transiting = j.value("transiting", false);
    try { inclination_error_upper = j.at("inclination_error_upper").get<double>() * fiftyseventh; } catch (...) { ; }
    try { inclination_error_lower = j.at("inclination_error_lower").get<double>() * fiftyseventh; } catch (...) { ; }
    try { j.at("opposition_surge").get_to(opposition_surge); } catch (...) { ; }
    try { j.at("overcast").get_to(overcast); } catch (...) { ; }
    // Fetch the node into a local FIRST. Written as ensure_atmosphere()->from_json(j.at(...)),
    // C++17 sequences the postfix-expression before the argument, so ensure_atmosphere() runs and
    // builds an Atmosphere before j.at() ever gets the chance to throw -- which handed an airless
    // body a default one-atmosphere sky just for being loaded, and suppressed the legacy branch
    // below by leaving atm non-null.
    try
    {
        json ja = j.at("atmosphere");
        if (ja.is_object()) ensure_atmosphere()->from_json(ja);
    }
    catch (...) { ; }

    // Universe files written before the atmosphere became its own object carry these two at the
    // top level instead. Read them only when there was no "atmosphere" block, so a current file
    // is never second-guessed by a stale key that happens to sit alongside it.
    if (!atm)
    {
        double legacy_pressure = 0, legacy_tau = 0;
        try { j.at("surface_pressure").get_to(legacy_pressure); } catch (...) { ; }
        try { j.at("atmospheric_tau").get_to(legacy_tau); } catch (...) { ; }
        if (j.contains("surface_pressure") || legacy_tau > 0)
        {
            if (std::isfinite(legacy_pressure) && legacy_pressure >= 0)
            {
                ensure_atmosphere()->surface_pressure = legacy_pressure;
                atm->pressure_supplied = j.contains("surface_pressure");
            }
            if (!atm) ensure_atmosphere();
            atm->tau = legacy_tau;
        }
    }

    try { j.at("J2").get_to(J2); } catch (...) { ; }
    try { j.at("asteroid_no").get_to(asteroid_no); } catch (...) { ; }
    try { j.at("lock_type").get_to(lock_type); } catch (...) { ; }
    try { j.at("ring_radius").get_to(ring_radius); ring_radius *= 1e3; } catch (...) { ; }
    try { j.at("ring_inner_radius").get_to(ring_inner_radius); ring_inner_radius *= 1e3; } catch (...) { ; }
    try { j.at("ring_mean_opacity").get_to(ring_mean_opacity); } catch (...) { ; }
    try { j.at("transparent_clouds").get_to(transparent_clouds); } catch (...) { ; }
    try { j.at("has_water").get_to(has_water); } catch (...) { ; }
    try { j.at("catalog_designation").get_to(catalog_designation); } catch (...) { ; }
    try { j.at("display_designation").get_to(display_designation); } catch (...) { ; }
    try { j.at("vegetation_r").get_to(vegetation_r); } catch (...) { ; }
    try { j.at("vegetation_g").get_to(vegetation_g); } catch (...) { ; }
    try { j.at("vegetation_b").get_to(vegetation_b); } catch (...) { ; }
    try { j.at("plant_trunk_height").get_to(plant_trunk_height); } catch (...) { ; }
    try { j.at("plant_trunk_width").get_to(plant_trunk_width); } catch (...) { ; }
    try { j.at("plant_crown_size").get_to(plant_crown_size); } catch (...) { ; }
    try { j.at("plant_crown_oblateness").get_to(plant_crown_oblateness); } catch (...) { ; }

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
    if (ring_status_known) return ring_radius > 0;
    if (get_light_center() == cels[0])                  // No rings on solar system objects unless explicitly indicated.
    {
        ring_status_known = true;
        return ring_radius > 0;
    }

    double probability = 0.0;

    // Anything tidally locked or in a resonance probably wouldn't have a ring.
    if (orbit && orbit->period && sidereal_rotational_period > (orbit->period/4.4)) return false;

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
    else if (t_eq > 600.0)
    {
        // Too hot, dust is dragged and ice is gone
        return false;
    }
    else if (t_eq > 300.0)
    {
        // Warm, mostly sparse rock/dust if any
        probability -= 0.60;
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
    if (rendering_saved_parameters) return;
    if (!gr && !guess_has_rings())
    {
        ring_radius = 0;
        return;
    }

    // Rings consist of small, non-cohesive aggregate particles whose tidal disruption
    // boundary is defined by the fluid Roche limit.
    CelestialObject ring_particle;
    ring_particle.type = waterworld;
    double t_eq = equilibrium_temperature();
    double particle_density = (t_eq < 200.0) ? 1.0 : 2.5;
    ring_particle.volumetric_mean_radius = 1.0;
    ring_particle.mass = particle_density * sphere_volume(1.0) * 1e6;
    double roche_limit_zero = this->Roche_limit(&ring_particle);

    // Safety constraint: If Roche limit is somehow smaller than the planet 
    // (e.g., highly dense planet, very low density moon proxy), rings cannot form.
    double minInnerRadius = volumetric_mean_radius * 1.1; // 10% gap from the surface/atmosphere
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
    ring_radius = outerDist(rng);

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
