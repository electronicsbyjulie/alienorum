#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "../classes/planet.h"

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
    
    // Manually set test values
    // Assuming oneatm is defined globally as ~101325 or similar
    // We'll set arbitrary numbers to verify the routing works
    atm->surface_pressure = 50000.0; 
    atm->tau = 1.5;
    atm->particulates = 0.8;
    
    EXPECT_DOUBLE_EQ(world.get_surface_pressure(), 50000.0);
    EXPECT_DOUBLE_EQ(world.get_atmospheric_tau(), 1.5);
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