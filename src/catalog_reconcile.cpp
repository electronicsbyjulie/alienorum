#include "classes/cat.h"
#include "classes/cons.h"
#include "classes/stellar_sources.h"
#include "globals.h"
#include "loaders.h"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace alienorum;

int main(int argc, char **argv)
{
    try
    {
        if (argc < 2)
        {
            std::cerr << "Usage: catalog_reconcile OUTPUT_DIRECTORY [--offline] [--download-only]" << std::endl;
            return 2;
        }
        bool offline = false, download_only = false;
        for (int i = 2; i < argc; i++)
        {
            const std::string option = argv[i];
            if (option == "--offline")
            {
                offline = true;
            }
            else if (option == "--download-only")
            {
                download_only = true;
            }
            else
            {
                throw std::runtime_error("Unknown option: " + option);
            }
        }
        const auto original = std::filesystem::current_path();
        const auto output = std::filesystem::absolute(argv[1]).lexically_normal();
        if (output == original || std::filesystem::weakly_canonical(output / "catalogs")
            == std::filesystem::weakly_canonical(original / "catalogs"))
        {
            throw std::runtime_error("The preview output must be separate from the input catalogs");
        }
        std::filesystem::create_directories(output / "catalogs");
        cels = new CelestialObject*[MAX_CELOBJS]();
        CatalogReader reader;
        if (!download_only)
        {
            Star::load_main_seq_dat();
            read_cons_lines();
            if (!reader.read_cons_boundaries())
            {
                throw std::runtime_error("No constellation boundaries were loaded");
            }
            if (!reader.read_condensed_star_cat())
            {
                throw std::runtime_error("No generated input catalog was loaded");
            }
        }
        const json report = reconcile_stellar_sources(cels, (original / "catalogs/reference").string(), !offline);
        std::ofstream provenance(output / "reference_report.json");
        provenance << report.dump(2) << '\n';
        provenance.close();
        if (!download_only)
        {
            for (int i = 1; cels[i]; i++)
            {
                if (!cels[i]->deleted && cels[i]->typeclass() == class_star)
                {
                    ((Star*)cels[i])->reconcile_visual_magnitudes();
                }
            }
            ConsBins bins = fill_alienorum_ids();
            std::filesystem::current_path(output);
            const int rows = reader.write_condensed_star_cat(bins);
            std::cout << "Generated isolated preview: " << rows << " rows in " << output << std::endl;
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Catalog reconciliation failed: " << error.what() << std::endl;
        return 1;
    }
}
