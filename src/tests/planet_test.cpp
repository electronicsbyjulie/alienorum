#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "../classes/planet.h"
#include "universe_fixture.h"

using namespace alienorum;
using json = nlohmann::json;

// =====================================================================
// Planet Initialization & Lifecycle Tests
// =====================================================================

TEST(PlanetTest, DefaultInitialization)
{
    Planet p;
    
    // Check initialized variables
    EXPECT_DOUBLE_EQ(p.albedo, 0.0);
    EXPECT_DOUBLE_EQ(p.opposition_surge, 0.0);
    EXPECT_DOUBLE_EQ(p.amt_lit, 0.0);
    EXPECT_DOUBLE_EQ(p.J2, 0.0);
    EXPECT_DOUBLE_EQ(p.ring_radius, 0.0);
    EXPECT_EQ(p.asteroid_no, 0);
    EXPECT_FALSE(p.lock_type);
    
    // Check that pointers start null
    EXPECT_EQ(p.atm, nullptr);
}

// =====================================================================
// Atmosphere Pointer Lifecycle & Accessors
// =====================================================================

TEST(PlanetTest, NullAtmosphereAccessors)
{
    Planet airless_world;
    
    // Ensure atm is null initially
    ASSERT_EQ(airless_world.atm, nullptr);
    
    // Accessors must safely return 0 without segfaulting when atm is null
    EXPECT_DOUBLE_EQ(airless_world.get_surface_pressure(), 0.0);
    EXPECT_DOUBLE_EQ(airless_world.get_atmospheric_tau(), 0.0);
    EXPECT_DOUBLE_EQ(airless_world.get_particulates(), 0.0);
}

TEST(PlanetTest, EnsureAtmosphereInitialization)
{
    Planet world;
    
    Atmosphere* created_atm = world.ensure_atmosphere();
    
    // Must return a valid pointer and assign it to the member
    ASSERT_NE(created_atm, nullptr);
    EXPECT_EQ(world.atm, created_atm);
    
    // Calling it again should return the exact same pointer
    Atmosphere* second_call = world.ensure_atmosphere();
    EXPECT_EQ(created_atm, second_call);
}

TEST(PlanetTest, ValidAtmosphereAccessors)
{
    Planet world;
    Atmosphere* atm = world.ensure_atmosphere();

    // get_atmospheric_tau() derives tau from the real composition rather than trusting a bare
    // hand-set field (a zero tau from a real, mostly-inert composition has to be trusted rather
    // than mistaken for "no data" -- see RealCompositionWithZeroTauIsTrusted), so give it a
    // composition to derive from and check against the same formula it uses internally.
    atm->surface_pressure = 50000.0;
    atm->particulates = 0.8;
    AtmosphereComposition* comp = atm->ensure_composition();
    comp->CO2_portion = 1.0;
    double expected_tau = atmospheric_tau(50000.0 * 0.000009869, 1.0, 0,0,0,0,0,0,0,0,0,0);

    EXPECT_DOUBLE_EQ(world.get_surface_pressure(), 50000.0);
    EXPECT_DOUBLE_EQ(world.get_atmospheric_tau(), expected_tau);
    EXPECT_DOUBLE_EQ(world.get_particulates(), 0.8);
}

// =====================================================================
// Serialization (JSON)
// =====================================================================

TEST(PlanetTest, JsonSerializationRoundTrip)
{
    Planet original;
    original.albedo = 0.35; // Roughly Earth
    original.opposition_surge = 0.5;
    original.asteroid_no = 42;
    original.lock_type = true;
    
    // Instantiate atmosphere to ensure it serializes
    Atmosphere* atm = original.ensure_atmosphere();
    atm->surface_pressure = 100000.0;
    
    json j = original.to_json();
    
    Planet restored;
    bool success = restored.from_json(j);
    
    EXPECT_TRUE(success);
    EXPECT_DOUBLE_EQ(restored.albedo, 0.35);
    EXPECT_DOUBLE_EQ(restored.opposition_surge, 0.5);

    EXPECT_EQ(restored.asteroid_no, 42);
    EXPECT_TRUE(restored.lock_type);

    // Neither is written when it has nothing to say, so an ordinary planet's entry is unchanged.
    Planet major_planet;
    json jmp = major_planet.to_json();
    EXPECT_FALSE(jmp.contains("asteroid_no"));
    EXPECT_FALSE(jmp.contains("lock_type"));
    
    // Ensure the atmosphere was recreated and populated
    ASSERT_NE(restored.atm, nullptr);
    EXPECT_DOUBLE_EQ(restored.get_surface_pressure(), 100000.0);
}

// =====================================================================
// Planet Atmospheric Math Tests
// =====================================================================

TEST(PlanetMathTest, EstimateSurfaceTemperature)
{
    Planet world;
    Atmosphere* atm = world.ensure_atmosphere();
    
    // We mock equilibrium temperature for this test to be isolated.
    // Assuming equilibrium_temperature() returns something based on distance/albedo.
    // For this test, let's assume equilibrium_temperature() evaluates to 255.0 K (Earth-like).
    // (If equilibrium_temperature is derived, you may set distance/albedo here)
    
    // Case 1: No atmospheric tau (greenhouse factor should be 1.0)
    atm->tau = 0.0;
    double t_surf_no_gh = world.estimate_surface_temperature();
    // T_surf should equal T_eq
    EXPECT_DOUBLE_EQ(t_surf_no_gh, world.equilibrium_temperature());

    // Case 2: With atmospheric tau
    atm->tau = 1.0; 
    // greenhouse_factor = 1.0 + (0.75 * 1.0 * 2.4) = 2.8
    // T_surf = T_eq * pow(2.8, 0.25) ≈ T_eq * 1.2936
    double expected_t_surf = world.equilibrium_temperature() * std::pow(2.8, 0.25);
    double t_surf_with_gh = world.estimate_surface_temperature();
    
    EXPECT_NEAR(t_surf_with_gh, expected_t_surf, 0.1);
}

TEST(PlanetMathTest, EstimateScaleHeight)
{
    Planet earth;
    Atmosphere* atm = earth.ensure_atmosphere();
    
    // Case 1: Airless body (Pressure <= 0)
    atm->surface_pressure = 0.0;
    EXPECT_DOUBLE_EQ(earth.estimate_scale_height(), 0.0);

    // Case 2: Earth-like parameters
    atm->surface_pressure = 101325.0; // 1 atm in Pa
    // M will fall back to 0.0289644 since we haven't instantiated atm->comp
    
    // Note: This test's exact expected value depends heavily on what 
    // estimate_surface_temperature() and estimate_surface_gravity() evaluate to.
    double t_surf = earth.estimate_surface_temperature();
    double g = earth.estimate_surface_gravity();
    
    // Only test if the prerequisite functions return valid non-zero values
    if (t_surf > 0 && g > 0)
    {
        double expected_height = 8.314462618 * t_surf / (0.0289644 * g);
        EXPECT_NEAR(earth.estimate_scale_height(), expected_height, 1.0);
    }
}

// =====================================================================
// AstorbRow Struct Tests
// =====================================================================

TEST(AstorbRowTest, InitializationAndAssignment)
{
    AstorbRow asteroid;
    
    // Default initialization checks
    EXPECT_EQ(asteroid.diam, 0.0f);
    EXPECT_EQ(asteroid.sma, 0.0f);
    EXPECT_EQ(asteroid.incl, 0.0f);
    EXPECT_EQ(asteroid.cel, nullptr);
    
    // Assignment checks
    asteroid.number = 1;
    asteroid.name = "Ceres";
    asteroid.diam = 939.4f;
    asteroid.sma = 2.769f;
    asteroid.incl = 10.59f;
    
    Planet ceres_planet;
    asteroid.cel = &ceres_planet;
    
    EXPECT_EQ(asteroid.number, 1);
    EXPECT_EQ(asteroid.name, "Ceres");
    EXPECT_FLOAT_EQ(asteroid.diam, 939.4f);
    EXPECT_FLOAT_EQ(asteroid.sma, 2.769f);
    EXPECT_FLOAT_EQ(asteroid.incl, 10.59f);
    EXPECT_EQ(asteroid.cel, &ceres_planet);
}// =====================================================================
// AtmosphereComposition Math & Integrity Tests
// =====================================================================

TEST(AtmosphereCompositionTest, EnforceIntegrityNormalizesExcess)
{
    CelestialObject dummy;
    AtmosphereComposition comp(&dummy);

    // Set an impossible atmosphere summing to 2.0
    comp.O2_portion = 1.5;
    comp.N2_portion = 0.5;

    comp.enforce_integrity();

    // Should scale down by 1/2.0 (multiplier = 0.5)
    EXPECT_DOUBLE_EQ(comp.O2_portion, 0.75);
    EXPECT_DOUBLE_EQ(comp.N2_portion, 0.25);
    EXPECT_DOUBLE_EQ(comp.H2_portion, 0.0);
}

TEST(AtmosphereCompositionTest, EnforceIntegrityAllowsPartialAtmospheres)
{
    CelestialObject dummy;
    AtmosphereComposition comp(&dummy);

    // Set a partial atmosphere summing to 0.5
    comp.O2_portion = 0.2;
    comp.N2_portion = 0.3;

    comp.enforce_integrity();

    // Should remain entirely untouched since total <= 1.0
    EXPECT_DOUBLE_EQ(comp.O2_portion, 0.2);
    EXPECT_DOUBLE_EQ(comp.N2_portion, 0.3);
}

TEST(AtmosphereCompositionTest, MeanMolarMassCalculation)
{
    CelestialObject dummy;
    AtmosphereComposition comp(&dummy);

    // Case 1: Empty atmosphere falls back to Earth air constant
    EXPECT_DOUBLE_EQ(comp.mean_molar_mass(), 0.0289644);

    // Case 2: Pure Oxygen atmosphere
    comp.O2_portion = 1.0;
    EXPECT_DOUBLE_EQ(comp.mean_molar_mass(), 0.031998);

    // Case 3: Mixed atmosphere (50% H2, 50% He)
    // Note: It normalizes based on the total present in the calculation
    comp.O2_portion = 0.0;
    comp.H2_portion = 0.5;
    comp.He_portion = 0.5;
    
    // Expected: (0.5 * 0.002016 + 0.5 * 0.004003) / 1.0 = 0.0030095
    EXPECT_NEAR(comp.mean_molar_mass(), 0.0030095, 1e-7);
}

// =====================================================================
// AtmosphereComposition Generator Tests
// =====================================================================

TEST(AtmosphereCompositionTest, GeneratorsProduceValidIntegrity)
{
    CelestialObject dummy;
    AtmosphereComposition comp(&dummy);

    // Test Gas Giant Generation
    comp.generate_fictitious_gas_giant();
    
    // Sum all portions to verify leftover calculation and integrity enforcement
    double total_gas = comp.H2_portion + comp.He_portion + comp.N2_portion + 
                       comp.O2_portion + comp.O3_portion + comp.CO2_portion + 
                       comp.CH4_portion + comp.SO2_portion + comp.H2O_portion + 
                       comp.H2S_portion + comp.HCN_portion + comp.NH3_portion + 
                       comp.C2H6_portion + comp.N2O_portion + comp.CO_portion + 
                       comp.Ar_portion;
                       
    // Should be extremely close to 1.0
    EXPECT_NEAR(total_gas, 1.0, 1e-5);
    
    // Gas giants should have Hydrogen as the leftover (dominant) gas
    EXPECT_GT(comp.H2_portion, 0.5);

    // Test Fictitious Routing (Venusian / Rocky)
    AtmosphereComposition comp_rocky(&dummy);
    comp_rocky.generate_fictitious_for_planet(rocky);
    
    // Rocky routing delegates to Venusian, where CO2 is the leftover (dominant)
    EXPECT_GT(comp_rocky.CO2_portion, 0.5);
}

TEST(AtmosphereCompositionTest, HabitableGenerationLogic)
{
    CelestialObject dummy;
    AtmosphereComposition comp(&dummy);
    
    comp.generate_fictitious_habitable();
    
    // Nitrogen is the leftover gas for habitable worlds
    EXPECT_GT(comp.N2_portion, 0.0);
    
    // Enforce integrity should have capped the total at 1.0
    double total_habitable = comp.H2_portion + comp.He_portion + comp.N2_portion + 
                             comp.O2_portion + comp.O3_portion + comp.CO2_portion + 
                             comp.CH4_portion + comp.SO2_portion + comp.H2O_portion + 
                             comp.H2S_portion + comp.HCN_portion + comp.NH3_portion + 
                             comp.C2H6_portion + comp.N2O_portion + comp.CO_portion + 
                             comp.Ar_portion;
                             
    EXPECT_NEAR(total_habitable, 1.0, 1e-5);
}
// =====================================================================
// Photometry
// =====================================================================

// Every planet in the sky is drawn at a brightness these produce, and the numbers themselves are
// empirical fits that nobody can check by eye. What can be checked is their shape: brightest at
// opposition, falling away either side of it, symmetric between waxing and waning, and never
// negative or greater than one.

TEST(PlanetPhotometryTest, PhaseBrightnessPeaksAtOpposition)
{
    Planet p;
    p.type = rocky;
    p.albedo = 0.3;

    double full = p.phase_brightness(0);
    EXPECT_NEAR(full, 1.0, 1e-9) << "at opposition the whole lit disc faces us";

    // Falling away from opposition, all the way to new.
    double previous = full;
    for (double alpha = 0.1; alpha <= _pi; alpha += 0.1)
    {
        double here = p.phase_brightness(alpha);
        EXPECT_LE(here, previous + 1e-12) << " at phase angle " << alpha;
        EXPECT_GE(here, 0) << " at phase angle " << alpha;
        EXPECT_LE(here, 1.0 + 1e-12) << " at phase angle " << alpha;
        previous = here;
    }

    // A new planet shows us nothing at all, or as near as makes no difference.
    EXPECT_LT(p.phase_brightness(_pi), 0.01);
}

TEST(PlanetPhotometryTest, PhaseBrightnessIsSymmetricAndWraps)
{
    Planet p;
    p.type = rocky;
    p.albedo = 0.3;

    // Waxing and waning at the same width are the same brightness.
    for (double alpha = 0.2; alpha < _pi; alpha += 0.4)
        EXPECT_NEAR(p.phase_brightness(alpha), p.phase_brightness(-alpha), 1e-12)
            << " at phase angle " << alpha;

    // And a phase angle that has gone round the whole way is the same as one that has not.
    EXPECT_NEAR(p.phase_brightness(0.7), p.phase_brightness(0.7 + _pi*2), 1e-12);
    EXPECT_NEAR(p.phase_brightness(0.7), p.phase_brightness(0.7 - _pi*2), 1e-12);
}

TEST(PlanetPhotometryTest, CloudierWorldsFadeMoreGently)
{
    // A bare regolith world dims sharply off opposition -- the shadows between its grains open
    // up -- while a cloud deck scatters much more like a smooth ball and holds its brightness.
    Planet bare;
    bare.type = rocky;
    bare.albedo = 0.15;

    Planet shrouded;
    shrouded.type = gas_giant;
    shrouded.albedo = 0.5;

    EXPECT_LE(bare.cloud_deck_fraction(), shrouded.cloud_deck_fraction());

    double half_phase = _pi/2;
    EXPECT_LE(bare.phase_brightness(half_phase), shrouded.phase_brightness(half_phase));

    // The slope parameter is the IAU G, which lives between 0 and 1.
    EXPECT_GE(bare.phase_slope_parameter(), 0.0);
    EXPECT_LE(bare.phase_slope_parameter(), 1.0);
    EXPECT_GE(shrouded.cloud_deck_fraction(), 0.0);
    EXPECT_LE(shrouded.cloud_deck_fraction(), 1.0);
}

// =====================================================================
// The habitable zone
// =====================================================================

class PlanetHabitabilityTest : public UniverseFixture {};

TEST_F(PlanetHabitabilityTest, EarthIsInTheZoneAndItsNeighboursAreNot)
{
    Star* sun = make_star("Sol");

    Planet* earth = make_planet(sun, "Earth", AU);
    EXPECT_TRUE(earth->is_in_con_HZ()) << "one AU from a G2V star is the definition of the zone";

    Planet* mercury = make_planet(sun, "Too Close", 0.387 * AU);
    EXPECT_FALSE(mercury->is_in_con_HZ());

    Planet* jupiter = make_planet(sun, "Too Far", 5.2 * AU);
    EXPECT_FALSE(jupiter->is_in_con_HZ());

    // A planet in orbit around nothing has no zone to be in, rather than an answer by accident.
    Planet rogue;
    rogue.mass = earth_mass;
    EXPECT_FALSE(rogue.is_in_con_HZ());

    delete_the_universe();
}

TEST_F(PlanetHabitabilityTest, TheZoneFollowsTheStar)
{
    // A cooler star's zone is closer in. Rather than assert where it is -- which is a claim about
    // the Kopparapu coefficients, not about this code -- walk a planet outwards from each star
    // and find the band, then compare the two bands.
    auto find_zone = [this](Star* s, double& inner, double& outer)
    {
        Planet* wanderer = make_planet(s, "Wanderer", AU);
        inner = outer = 0;
        for (double au = 0.02; au < 10.0; au *= 1.05)
        {
            wanderer->orbit->semimajor_axis = au * AU;
            if (wanderer->is_in_con_HZ())
            {
                if (!inner) inner = au;
                outer = au;
            }
        }
    };

    Star* sun = make_star("Sol");
    double sun_inner = 0, sun_outer = 0;
    find_zone(sun, sun_inner, sun_outer);

    // The conservative zone's inner edge sits almost exactly at the Earth's orbit in this model --
    // the runaway greenhouse limit is about 0.99 AU -- so the band is checked to a step of the
    // scan rather than to a sharp figure. That the Earth itself is inside it is asserted above,
    // at exactly one AU, where it can be checked without any sampling at all.
    ASSERT_GT(sun_inner, 0) << "a G2V star has a habitable zone";
    EXPECT_LT(sun_inner, 1.1);
    EXPECT_GT(sun_outer, 1.1) << "and it reaches out past the Earth";

    Star* cool = make_star("Red Dwarf");
    cool->temperature = 3200;
    cool->absolute_magnitude = 11.0;
    strcpy(cool->spectral_type, "M2V");
    double cool_inner = 0, cool_outer = 0;
    find_zone(cool, cool_inner, cool_outer);

    ASSERT_GT(cool_inner, 0) << "a red dwarf has one too, much closer in";
    EXPECT_LT(cool_outer, sun_inner) << "the whole of it is inside the Sun's inner edge";

    delete_the_universe();
}

// =====================================================================
// Deriving what a catalog did not state
// =====================================================================

TEST(PlanetEstimationTest, RadiusFromMass)
{
    // An exoplanet is often massed and not measured. One Earth mass should come back at about one
    // Earth radius, and a heavier world should be larger -- but not proportionally so, since rock
    // compresses under its own weight.
    Planet earth;
    earth.mass = earth_mass;
    earth.type = rocky;
    earth.estimate_radius();
    EXPECT_NEAR(earth.volumetric_mean_radius, earth_radius, earth_radius * 0.25);

    Planet heavy;
    heavy.mass = 5 * earth_mass;
    heavy.type = rocky;
    heavy.estimate_radius();
    EXPECT_GT(heavy.volumetric_mean_radius, earth.volumetric_mean_radius);
    EXPECT_LT(heavy.volumetric_mean_radius, 5 * earth.volumetric_mean_radius);

    // It is a derivation and not a fallback: it overwrites whatever radius the body had, which is
    // why the callers check first whether they have a measured one. Worth pinning, since the name
    // reads like it might defer to a value already there.
    Planet measured;
    measured.mass = earth_mass;
    measured.type = rocky;
    measured.volumetric_mean_radius = 1234567;
    measured.estimate_radius();
    EXPECT_NE(measured.volumetric_mean_radius, 1234567);
    EXPECT_NEAR(measured.volumetric_mean_radius, earth.volumetric_mean_radius, 1);

    // A mass that is not a number cannot give a radius that is; rather than pass the poison on to
    // everything that divides by a radius, it settles for Earth's.
    Planet nonsense;
    nonsense.mass = INFINITY;
    nonsense.type = gas_giant;
    nonsense.estimate_radius();
    EXPECT_TRUE(std::isfinite(nonsense.volumetric_mean_radius));
}

// Given a star to stand in front of, rather than a temperature written straight into the field.
// Setting temperature by hand used to work because equilibrium_temperature() started by returning
// it -- which is exactly what must not happen: the surface temperature is stored back into that
// same field, so returning it here would feed the calculation its own last answer and multiply it
// by the greenhouse factor again on every pass, climbing until it is infinite. See the warning at
// the top of Planet::equilibrium_temperature(). So the temperature has to be arrived at the way
// the program arrives at it, from a real star at a real distance.
class PlanetAtmosphereTest : public UniverseFixture {};

TEST_F(PlanetAtmosphereTest, ScaleHeightRequiresAnAtmosphere)         // Claude is NOT PTSD-friendly.
{
    Star* sun = make_star("Sol");

    // No atmosphere at all: nothing to have a height, whatever the star is doing.
    Planet* airless = make_planet(sun, "Airless", AU);
    EXPECT_DOUBLE_EQ(airless->estimate_scale_height(), 0);

    // An atmosphere of no pressure is the same answer by a different route -- the object exists,
    // but there is nothing in it.
    Planet* vacuum = make_planet(sun, "Vacuum", AU);
    vacuum->ensure_atmosphere()->surface_pressure = 0;
    EXPECT_DOUBLE_EQ(vacuum->estimate_scale_height(), 0);

    // An Earth-sized world an AU out, with an atmosphere: a few kilometres, as ours is 8.5.
    Planet* warm = make_planet(sun, "Warm", AU);
    warm->ensure_atmosphere()->surface_pressure = oneatm;
    double earthlike = warm->estimate_scale_height();
    EXPECT_GT(earthlike, 0);
    EXPECT_GT(earthlike, 1000) << "a scale height of less than a kilometre is not an atmosphere";
    EXPECT_LT(earthlike, 100000);

    // Moved in towards the star, the same world is hotter, and a hotter atmosphere is puffier.
    warm->orbit->semimajor_axis = 0.5 * AU;
    double closer_in = warm->estimate_scale_height();
    EXPECT_GT(closer_in, earthlike) << "a hotter atmosphere is puffier";

    // Back out, and made heavier at the same radius: stronger gravity holds it down.
    warm->orbit->semimajor_axis = AU;
    EXPECT_NEAR(warm->estimate_scale_height(), earthlike, earthlike * 1e-9)
        << "and it comes back to where it was when the distance does";
    warm->mass = 5 * earth_mass;
    EXPECT_LT(warm->estimate_scale_height(), earthlike) << "stronger gravity holds it down";

    delete_the_universe();
}

// get_atmospheric_tau() used to treat "computed tau of zero" as a proxy for "no composition data,"
// and would invent a random fictitious atmosphere to replace it. But a real, fully-specified
// atmosphere that's mostly inert gas -- Earth's N2/O2/Ar, none of which are greenhouse gases this
// model tracks -- legitimately computes to zero tau too. That's a real answer, not a missing one,
// and it must not get overwritten.
TEST_F(PlanetAtmosphereTest, RealCompositionWithZeroTauIsTrusted)
{
    Star* sun = make_star("Sol");

    Planet* earth = make_planet(sun, "Earth", AU);
    Atmosphere* eatm = earth->ensure_atmosphere();
    eatm->surface_pressure = oneatm;
    AtmosphereComposition* ecomp = eatm->ensure_composition();
    ecomp->N2_portion = 0.78;
    ecomp->O2_portion = 0.21;
    ecomp->Ar_portion = 0.01;

    EXPECT_DOUBLE_EQ(earth->get_atmospheric_tau(), 0.0);

    // The composition object itself, and the real gas fractions on it, must survive untouched --
    // not be silently replaced by ensure_composition()->generate_fictitious_for_planet().
    EXPECT_EQ(eatm->comp, ecomp);
    EXPECT_DOUBLE_EQ(ecomp->N2_portion, 0.78);
    EXPECT_DOUBLE_EQ(ecomp->O2_portion, 0.21);
    EXPECT_DOUBLE_EQ(ecomp->Ar_portion, 0.01);
    EXPECT_DOUBLE_EQ(ecomp->CO2_portion, 0.0) << "no fictitious greenhouse gas should have been invented";

    // A planet with pressure but genuinely no composition at all is the case this fallback exists
    // for, and should still get one invented for it.
    Planet* sketched = make_planet(sun, "Sketched", AU);
    sketched->ensure_atmosphere()->surface_pressure = oneatm;
    EXPECT_EQ(sketched->atm->comp, nullptr);
    sketched->get_atmospheric_tau();
    EXPECT_NE(sketched->atm->comp, nullptr) << "a planet with no composition data at all should still get one made up";

    delete_the_universe();
}

// Real bodies, real mass/radius/orbit/pressure/composition, checked against the estimate_
// scale_height() formula (H = RT/(Mg), g converted from the Earth-Gs estimate_surface_gravity()
// returns to m/s2 -- see the fix in Planet::estimate_scale_height()). Bounds are deliberately
// loose: equilibrium_temperature() runs through a fitted Bond-albedo model and an Eddington-gray
// greenhouse approximation, neither of which promises to land on the textbook surface temperature
// (real scale heights: Earth ~8.5 km, Venus ~15.9 km, Mars ~11.1 km, Jupiter ~27 km). What these
// bounds do pin down is the order of magnitude: a units bug like the one this guards against (the
// pre-fix code was off by the 9.80665 m/s2 -> Earth-Gs conversion) inflates every one of these by
// roughly 9.8x, which blows straight through the upper bound on all four.
TEST_F(PlanetAtmosphereTest, ScaleHeightIsSaneForRealPlanets)
{
    Star* sun = make_star("Sol");

    Planet* earth = make_planet(sun, "Earth", AU);
    earth->albedo = 0.306;
    Atmosphere* eatm = earth->ensure_atmosphere();
    eatm->surface_pressure = oneatm;
    AtmosphereComposition* ecomp = eatm->ensure_composition();
    ecomp->N2_portion = 0.78;
    ecomp->O2_portion = 0.21;
    ecomp->Ar_portion = 0.01;

    Planet* venus = make_planet(sun, "Venus", 0.723 * AU);
    venus->mass = 0.815 * earth_mass;
    venus->volumetric_mean_radius = 0.9499 * earth_radius;
    venus->albedo = 0.75;
    Atmosphere* vatm = venus->ensure_atmosphere();
    vatm->surface_pressure = 92 * oneatm;
    AtmosphereComposition* vcomp = vatm->ensure_composition();
    vcomp->CO2_portion = 0.965;
    vcomp->N2_portion = 0.035;

    Planet* mars = make_planet(sun, "Mars", 1.524 * AU);
    mars->mass = 0.1074 * earth_mass;
    mars->volumetric_mean_radius = 0.532 * earth_radius;
    mars->albedo = 0.25;
    Atmosphere* matm = mars->ensure_atmosphere();
    matm->surface_pressure = 610;
    AtmosphereComposition* mcomp = matm->ensure_composition();
    mcomp->CO2_portion = 0.95;
    mcomp->N2_portion = 0.027;
    mcomp->Ar_portion = 0.019;
    mcomp->O2_portion = 0.0013;

    Planet* jupiter = make_planet(sun, "Jupiter", 5.2 * AU);
    jupiter->mass = jupiter_mass;
    jupiter->volumetric_mean_radius = jupiter_radius;
    jupiter->albedo = 0.343;
    Atmosphere* jatm = jupiter->ensure_atmosphere();
    jatm->surface_pressure = oneatm;
    AtmosphereComposition* jcomp = jatm->ensure_composition();
    jcomp->H2_portion = 0.864;
    jcomp->He_portion = 0.136;

    struct Expectation { Planet* p; double min_m; double max_m; };
    Expectation expectations[] =
    {
        { earth,   2000, 30000 },
        { venus,   2000, 40000 },
        { mars,    2000, 30000 },
        { jupiter, 2000, 60000 },
    };

    for (auto& e : expectations)
    {
        double H = e.p->estimate_scale_height();
        EXPECT_GT(H, e.min_m) << e.p->name << ": scale height implausibly thin";
        EXPECT_LT(H, e.max_m) << e.p->name << ": scale height implausibly puffy -- check for a units regression";

        // The cap in estimate_scale_height() should never be the thing holding a normal planet
        // down: a real world's scale height is a small fraction of its own radius.
        EXPECT_LT(H, e.p->volumetric_mean_radius * 0.05)
            << e.p->name << ": within the cap, but by coincidence rather than because it's tiny by nature";
    }

    delete_the_universe();
}

// A world with no measured composition or temperature -- exactly the shape of a directly-imaged,
// wide-orbit exoplanet like COCONUTS-2 b, whose orbital archive gives a period and semimajor axis
// but nothing about its atmosphere -- can still send T or g to extremes through the estimation
// pipeline (a bad greenhouse tau, an underestimated gravity). Whatever the upstream cause, the
// rendered glow must never be allowed to swallow the planet: confirm the absolute cap holds even
// when the raw RT/(Mg) value would clearly blow past it.
TEST_F(PlanetAtmosphereTest, ScaleHeightCapHoldsForAPathologicalPlanet)
{
    Star* sun = make_star("Sol");

    // Cranked far hotter than any real greenhouse effect would produce, on a low-gravity world:
    // T high and g low both push H up, so this is a stress case, not a realistic one.
    Planet* monster = make_planet(sun, "Monster", 0.05 * AU);
    monster->mass = 0.01 * earth_mass;
    monster->volumetric_mean_radius = 5 * earth_radius;
    monster->albedo = 0.0;
    Atmosphere* matm = monster->ensure_atmosphere();
    matm->surface_pressure = oneatm;
    AtmosphereComposition* mcomp = matm->ensure_composition();
    mcomp->H2_portion = 1.0;

    double H = monster->estimate_scale_height();
    EXPECT_GT(H, 0);
    EXPECT_LE(H, monster->volumetric_mean_radius * 0.05 * 1.0000001)
        << "the cap must hold regardless of how extreme the upstream temperature/gravity estimate gets";

    delete_the_universe();
}

TEST(PlanetEstimationTest, SurfaceGravityAndDensityAgree)
{
    // Two derivations of the same body from the same two numbers; they have to be consistent
    // with each other, and with the Earth, which is where both are calibrated.
    Planet earth;
    earth.mass = earth_mass;
    earth.volumetric_mean_radius = earth_radius;

    EXPECT_NEAR(earth.estimate_surface_gravity(), 1.0, 1e-9);
    EXPECT_NEAR(earth.density(), 5.51, 0.05);

    // Same density, bigger world: gravity goes up with the radius.
    Planet bigger;
    bigger.volumetric_mean_radius = 2 * earth_radius;
    bigger.mass = earth_mass * 8;                       // eight times the volume, same density
    EXPECT_NEAR(bigger.density(), earth.density(), 1e-9);
    EXPECT_NEAR(bigger.estimate_surface_gravity(), 2.0, 1e-9);
}
