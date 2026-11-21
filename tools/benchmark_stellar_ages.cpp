#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#ifndef STELLAR_GRID_HEADER
#define STELLAR_GRID_HEADER "../src/classes/stellarevolution.h"
#endif
#include STELLAR_GRID_HEADER

using json = nlohmann::json;
using namespace alienorum;

int main(int argc, char** argv)
{
    const auto started = std::chrono::steady_clock::now();
    std::ifstream input(argc > 1 ? argv[1] : "src/tests/data/stellar_age_benchmarks.json");
    if (!input) return 2;
    const json catalog = json::parse(input);
    json report = {{"schema", 1}, {"rows", json::array()}, {"sources", catalog.at("sources")}};
    struct Summary
    {
        int count = 0, covered = 0;
        double residual = 0, absolute = 0;
    };
    std::map<std::string, Summary> summaries;
    for (const auto& row : catalog.at("rows"))
    {
        const std::string spectrum = row.at("spectral_type");
        int group = spectrum.empty() ? 0 : spectrum[0] == 'D' ? (spectrum[1] == 'A' ? 3 : 4)
            : spectrum.find("III") != std::string::npos ? 2 : spectrum.find("IV") != std::string::npos ? 6
            : spectrum.find("I") != std::string::npos ? 7 : spectrum.find("V") != std::string::npos ? 1 : 0;
        const double temperature = row.at("temperature_k"), luminosity = row.at("luminosity_solar");
        for (bool constrained : {false, true})
        {
            const double mass = constrained ? row.value("mass_solar", 0.0) : 0;
            const double reference = row.at("reference_age_gyr");
#ifdef STELLAR_BENCHMARK_BASELINE
            const auto fit = StellarEvolutionGrid::instance().fit(std::log10(temperature), std::log10(luminosity),
                row.value("metallicity", 0.0), group > 5 ? 2 : group, mass);
#else
            const double temperature_sigma = std::hypot(row.value("temperature_error_k", 100.0) / (temperature * std::log(10.0)), .0125 / std::sqrt(12.0));
            const double luminosity_sigma = std::hypot(row.value("log_luminosity_error_dex",
                row.value("luminosity_error_solar", .2 * luminosity) / (luminosity * std::log(10.0))), .075 / std::sqrt(12.0));
            const double mass_sigma = mass > 0 ? std::max(.03, row.value("mass_error_solar", .1 * mass) / (mass * std::log(10.0))) : .30;
            const auto fit = StellarEvolutionGrid::instance().fit(std::log10(temperature), std::log10(luminosity),
                row.value("metallicity", 0.0), group, mass, -1, temperature_sigma, luminosity_sigma,
                std::max(.08, row.value("metallicity_error", .40)), mass_sigma);
#endif
            if (!fit.valid || !(fit.age > 0)) return 3;
            const double residual = std::log10(fit.age / reference);
            const double reference_lower = row.at("lower_gyr").is_number() ? row.at("lower_gyr").get<double>() : 0;
            const double reference_upper = row.at("upper_gyr").is_number() ? row.at("upper_gyr").get<double>() : 100;
            const bool overlap = fit.upper >= reference_lower && fit.lower <= reference_upper;
            const std::string mode = constrained ? "mass_constrained" : "hr_only";
            for (const auto& category : {row.at("category").get<std::string>(), std::string("all")})
            {
                auto& summary = summaries[row.at("partition").get<std::string>() + "/" + category + "/" + mode];
                ++summary.count;
                summary.covered += overlap;
                summary.residual += residual;
                summary.absolute += std::abs(residual);
            }
            report["rows"].push_back({{"name", row.at("name")}, {"system", row.at("system")},
                {"partition", row.at("partition")}, {"category", row.at("category")}, {"source", row.at("source")},
                {"mode", mode}, {"reference_age_gyr", reference}, {"value_gyr", fit.age},
                {"lower_gyr", fit.lower}, {"upper_gyr", fit.upper}, {"phase", fit.phase},
                {"mass_solar", fit.mass}, {"initial_mass_solar", fit.initial_mass},
                {"fit_distance", fit.distance},
                {"log10_age_ratio", residual}, {"reference_interval_overlap", overlap}});
        }
    }
    for (const auto& item : summaries)
        report["summary"][item.first] = {{"stars", item.second.count},
            {"mean_log10_age_ratio", item.second.residual / item.second.count},
            {"mean_absolute_log10_age_ratio", item.second.absolute / item.second.count},
            {"reference_interval_overlap_fraction", double(item.second.covered) / item.second.count}};
    report["elapsed_seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::ofstream output(argc > 2 ? argv[2] : "/tmp/stellar-age-benchmark.json");
    output << report.dump(2) << '\n';
    std::cout << report.at("summary").dump(2) << '\n';
}
