#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <gtest/gtest.h>
#include "../classes/stellarevolution.h"
#include "../classes/star.h"
#include "../classes/cat.h"
#include "../classes/savemodels.h"
#include "universe_fixture.h"

using namespace alienorum;

namespace
{
    json benchmarks()
    {
        std::ifstream input("src/tests/data/stellar_age_benchmarks.json");
        return json::parse(input);
    }

    void observe(Star& star, const json& row, bool constrain_mass)
    {
        strcpy(star.spectral_type, row.at("spectral_type").get<std::string>().c_str());
        star.temperature = row.at("temperature_k");
        star.metallicity = row.at("metallicity");
        const double luminosity = row.at("luminosity_solar");
        star.absolute_magnitude = 4.74 - 2.5 * std::log10(luminosity) - Star::bolometric_correction(star.temperature);
        star.volumetric_mean_radius = row.value("radius_solar", std::sqrt(luminosity)
            / std::pow(star.temperature / 5772, 2)) * solar_radius;
        json observed = row;
        observed["log_luminosity_error_dex"] = row.value("log_luminosity_error_dex",
            row.value("luminosity_error_solar", .2 * luminosity) / (luminosity * std::log(10.0)));
        // Reference labels are never copied into stellar_age or used as constraints.
        for (const char* field : {"reference_age_gyr", "lower_gyr", "upper_gyr", "partition"}) observed.erase(field);
        if (!constrain_mass)
        {
            observed.erase("mass_solar");
            observed.erase("mass_error_solar");
        }
        else star.mass = row.value("mass_solar", 0.0) * solar_mass;
        star.stellar_parameters = {{"evolution", observed}};
    }
}

TEST(StellarAgeCalibrationTest, ReferencesSpanPopulationsWithoutSplittingSystems)
{
    const json catalog = benchmarks();
    ASSERT_GE(catalog.at("rows").size(), 100u);
    std::set<std::string> names, categories;
    std::map<std::string, std::string> partitions;
    int calibration = 0, validation = 0;
    for (const auto& row : catalog.at("rows"))
    {
        ASSERT_TRUE(names.insert(row.at("name").get<std::string>()).second);
        ASSERT_TRUE(catalog.at("sources").contains(row.at("source").get<std::string>()));
        const std::string system = row.at("system"), partition = row.at("partition");
        auto entry = partitions.emplace(system, partition);
        EXPECT_EQ(entry.first->second, partition);
        categories.insert(row.at("category"));
        calibration += partition == "calibration";
        validation += partition == "validation";
        EXPECT_GT(row.at("reference_age_gyr").get<double>(), 0);
        EXPECT_TRUE(row.contains("method"));
        EXPECT_TRUE(row.contains("model_dependence"));
    }
    EXPECT_GE(categories.size(), 8u);
    EXPECT_GE(calibration, 30);
    EXPECT_GE(validation, 100);
}

TEST(StellarAgeCalibrationTest, FullEstimatorMatchesAgeLabelsWithDocumentedScatter)
{
    Star::load_main_seq_dat();
    const json catalog = benchmarks();
    std::map<std::string, double> errors;
    std::map<std::string, int> counts, coverage;
    for (const auto& row : catalog.at("rows"))
    {
        SCOPED_TRACE(row.at("name").get<std::string>());
        Star star;
        observe(star, row, true);
        ASSERT_TRUE(star.stellar_age.is_null());
        const json result = star.age_diagnostics();
        const double age = result.at("value_gyr");
        ASSERT_TRUE(std::isfinite(age));
        EXPECT_GT(age, 0);
        EXPECT_LE(result.at("lower_gyr").get<double>(), age);
        EXPECT_GE(result.at("upper_gyr").get<double>(), age);
        const std::string partition = row.at("partition");
        errors[partition] += std::abs(std::log10(age / row.at("reference_age_gyr").get<double>()));
        ++counts[partition];
        const double lower = row.at("lower_gyr").is_number() ? row.at("lower_gyr").get<double>() : 0;
        const double upper = row.at("upper_gyr").is_number() ? row.at("upper_gyr").get<double>() : 100;
        coverage[partition] += result.at("upper_gyr").get<double>() >= lower
            && result.at("lower_gyr").get<double>() <= upper;
        EXPECT_EQ(result, star.age_diagnostics());
    }
    EXPECT_LT(errors["calibration"] / counts["calibration"], .13);
    EXPECT_LT(errors["validation"] / counts["validation"], .25);
    EXPECT_GT(double(coverage["validation"]) / counts["validation"], .85);
}

TEST(StellarAgeCalibrationTest, MassConstraintParticipatesInCandidateSearch)
{
    const auto path = std::filesystem::temp_directory_path() / "alienorum-stellar-mass-query.dat";
    {
        std::ofstream output(path);
        for (int index = 0; index < 600; ++index)
            output << "0 2 4.2 4.0 1 1 9.0 8.9 9.1 0\n";
        output << "0 2 4.21 4.01 25 30 6.8 6.7 6.9 0\n";
    }
    const StellarEvolutionGrid grid(path.string());
    const auto result = grid.fit(4.2, 4.0, 0, 2, 25, -1, .035, .2, .4, .03);
    ASSERT_TRUE(result.valid);
    EXPECT_GT(result.mass, 20);
    EXPECT_LT(result.age, .02);
    EXPECT_GT(result.initial_mass, result.mass);
    std::filesystem::remove(path);
}

TEST(StellarAgeCalibrationTest, PopulationWeightsAreAppliedOnceAndIgnoreRowMultiplicity)
{
    const auto path = std::filesystem::temp_directory_path() / "alienorum-stellar-quadrature.dat";
    for (int copies : {1, 9})
    {
        {
            std::ofstream output(path);
            for (int index = 0; index < copies; ++index)
                output << "0 0 3.76 0 1 1 9 9 9 " << std::log(.9 / copies) << '\n';
            output << "0 0 3.76 0 1 1 10 10 10 " << std::log(.1) << '\n';
        }
        const StellarEvolutionGrid grid(path.string());
        const auto result = grid.fit(3.76, 0, 0, 1);
        ASSERT_TRUE(result.valid);
        EXPECT_DOUBLE_EQ(result.age, 1);
        EXPECT_GE(result.effective_samples, 1);
    }
    std::filesystem::remove(path);
}

TEST(StellarAgeCalibrationTest, ObservationalErrorsInvalidateCacheAndSurviveReload)
{
    Star::load_main_seq_dat();
    Star star;
    observe(star, benchmarks().at("rows").at(0), true);
    const json original = star.age_diagnostics();
    const auto key = star.evolution_inputs;
    star.stellar_parameters["evolution"]["temperature_error_k"] = "250?";
    star.age_diagnostics();
    EXPECT_NE(key, star.evolution_inputs);
    star.stellar_parameters["evolution"]["temperature_error_k"] = 250;
    const json annotated = star.age_diagnostics();
    Star restored;
    ASSERT_TRUE(restored.from_json(star.to_json()));
    EXPECT_DOUBLE_EQ(restored.estimate_age_gyr(), annotated.at("value_gyr").get<double>());
    EXPECT_EQ(original.at("model_revision"), 2);
    double probability = 0;
    for (const auto& phase : annotated.at("phase_probabilities")) probability += phase.at("probability").get<double>();
    EXPECT_NEAR(probability, 1, 1e-12);
    star.stellar_age = {{"value_gyr", "0?"}};
    EXPECT_DOUBLE_EQ(star.estimate_age_gyr(), 0);
}

TEST(StellarAgeCalibrationTest, MassiveStarLifetimeComesFromInitialMassTracks)
{
    const auto& grid = StellarEvolutionGrid::instance();
    const double solar = grid.main_sequence_lifetime_gyr(1);
    const double massive = grid.main_sequence_lifetime_gyr(30);
    EXPECT_GT(solar, 5);
    EXPECT_LT(solar, 13.9);
    EXPECT_GT(massive, .003);
    EXPECT_LT(massive, .02);
    EXPECT_GT(grid.main_sequence_lifetime_gyr(15), massive);
}

TEST_F(UniverseFixture, BetelgeuseUsesObservedHrPositionThroughCatalogueLoading)
{
    Star::load_main_seq_dat();
    CatalogReader reader;
    ASSERT_GT(reader.read_condensed_star_cat(), 0);
    Star* target = nullptr;
    for (int index = 0; cels[index]; ++index)
        if (cels[index]->typeclass() == class_star && static_cast<Star*>(cels[index])->HIP == 27989)
            target = static_cast<Star*>(cels[index]);
    ASSERT_NE(target, nullptr);
    EXPECT_FALSE(target->stellar_age.is_object() && target->stellar_age.contains("value_gyr"));
    const json age = target->age_diagnostics();
    EXPECT_EQ(age.at("source"), "MIST_1.2_HR_diagram_fit");
    EXPECT_GT(age.at("value_gyr").get<double>(), .005);
    EXPECT_LT(age.at("value_gyr").get<double>(), .025);
    EXPECT_EQ(age.at("luminosity_basis"), "supplied_evolution_observation");
    EXPECT_NEAR(age.at("log_luminosity_solar").get<double>(), 5.10, .001);
    delete_the_universe();
}
