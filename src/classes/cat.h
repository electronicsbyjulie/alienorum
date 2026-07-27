
#ifndef _CatalogReader
#define _CatalogReader

#include <string>
#include <vector>
#include <fstream>
#include <random>
#include "point.h"
#include "galaxy.h"
#include "cons.h"
#include "planet.h"
#include "moon.h"

extern std::vector<std::string> known_catalog_names;
extern Star **hdcache, **hipcache;
extern std::map<int,std::map<char,Star* > > hipcomps;

#define auto_match_multiples 0

namespace alienorum
{
    class CatalogReader
    {
        public:
        std::vector<std::string>find_catalogs(std::string path);
        void download_catalogs();

        // Source Catalogs for Stars
        int read_Gliese_catalog(CelestialObject** cels, int max);
        int read_BrightStars_catalog(CelestialObject** cels, int max);
        int read_Hipparcos_catalog(CelestialObject** cels, int max);
        int read_Uranometria_catalog(CelestialObject** cels, int max);
        int read_WD_catalog(CelestialObject** cels, int max);
        int read_cons_boundaries();

        // Binary and Multiple Systems
        int read_CCDM_catalog(CelestialObject** cels, int max);
        int read_SB9_catalog(CelestialObject** cels, int max);

        // Planets, Minor Planets, Comets
        static bool load_asteroid(AstorbRow *r, char *buffer = nullptr);        // If buffer is null, open astorb.dat and search for the astroid matching the row object.
        int read_astorb_catalog(CelestialObject** cels, int max);
        unsigned int load_exoplanets_from_tap(bool stars_only = false);         // If stars_only, just verify stars/add new stars don't attempt planets.
        int read_exoplanets_catalog(CelestialObject** cels, int max);           // Old method requiring manual download

        // Internal Catalogs
        int read_starname_dat(CelestialObject** cels);                          // No max because we are not adding stars, only setting names.
        int read_star_orbits_dat(CelestialObject** cels);
        int read_local_planets(CelestialObject** cels, int max, CelestialObject* must_orbit = nullptr, CelestialObject* mustnt_orbit = nullptr);

        // Condensed star catalog
        int write_condensed_star_cat(CelestialObject** cels);
        int read_condensed_star_cat(CelestialObject** cels, int max);

        protected:
        static void read_field_onebased(const char* buffer, size_t start, int end, char* out);
        void apply_exoplanet_names(std::map<int, std::vector<int>> planet_celids);
        bool worth_searching(std::string star_name);
    };
}

#endif
