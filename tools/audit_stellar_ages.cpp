#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include "../src/globals.h"
#include "../src/classes/cat.h"

using namespace alienorum;

int main(int argc, char** argv)
{
    cels = new CelestialObject*[MAX_CELOBJS]{};
    hdcache = new Star*[MAX_HD + 1]{};
    hipcache = new Star*[MAX_HIP + 1]{};
    Star::load_main_seq_dat();
    CatalogReader reader;
    const int loaded = reader.read_condensed_star_cat();
    if (loaded <= 0) return 2;
    std::ofstream output(argc > 1 ? argv[1] : "/tmp/alienorum-stellar-age-audit.tsv");
    output << "name\tHD\tHIP\tspectrum\tmass_solar\tage_gyr\tlower_gyr\tupper_gyr\tsource\n";
    size_t total = 0, invalid = 0, old_ceiling = 0, models = 0;
    std::map<std::string, size_t> sources;
    for (int i = 0; cels[i]; ++i)
    {
        if (cels[i]->typeclass() != class_star) continue;
        const Star* star = static_cast<Star*>(cels[i]);
        const json age = star->age_diagnostics();
        const double value = age.at("value_gyr");
        const std::string source = age.value("source", "unspecified");
        ++sources[source];
        ++total;
        if (!std::isfinite(value) || value < 0) ++invalid;
        if (source == "MIST_1.2_HR_diagram_fit" || source == "Bedard_2020_cooling_and_progenitor_prior") ++models;
        if ((source == "evolved_stellar_lifetime_prior" || source == "main_sequence_lifetime_prior")
            && fabs(value - 13.8) < 1e-6) ++old_ceiling;
        output << star->name << '\t' << star->HD << '\t' << star->HIP << '\t' << star->spectral_type
            << '\t' << star->mass / solar_mass << '\t' << value << '\t'
            << age.value("lower_gyr", value) << '\t' << age.value("upper_gyr", value) << '\t' << source << '\n';
    }
    std::cout << "Catalog stars: " << total << "; model estimates: " << models
        << "; invalid ages: " << invalid << "; exact fallback ceilings: " << old_ceiling << '\n';
    for (const auto& source : sources) std::cout << source.first << ": " << source.second << '\n';
    return invalid || old_ceiling ? 1 : 0;
}
