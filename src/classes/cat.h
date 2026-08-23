
#ifndef _CatalogReader
#define _CatalogReader

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <random>
#include <cmath>
#include "point.h"
#include "galaxy.h"
#include "cons.h"
#include "planet.h"
#include "moon.h"
#include "comet.h"

extern std::vector<std::string> known_catalog_names;
extern Star **hdcache, **hipcache;
extern std::map<std::string, Star*> dmcache;

#define auto_match_multiples 0

namespace alienorum
{
    // Flattened row of the NASA Exoplanet Archive TAP "pscomppars" table, or of a cache line
    // derived from it. Absent fields are NAN / empty, matching how the archive reports them.
    struct ExoRow
    {
        std::string pl_name, hostname, hd_name, hip_name, st_spectype;
        double sy_dist=NAN, ra=NAN, dec=NAN, st_lum=NAN, sy_vmag=NAN, st_teff=NAN;
        double st_mass=NAN, st_rad=NAN, st_rotp=NAN;
        double pl_orbincl=NAN, pl_msinij=NAN, pl_msinie=NAN, pl_bmassj=NAN, pl_bmasse=NAN;
        double pl_radj=NAN, pl_rade=NAN, pl_trueobliq=NAN;
        double pl_orbtper=NAN, pl_orblper=NAN, pl_tranmid=NAN, pl_orbper=NAN, pl_orbsmax=NAN, pl_orbeccen=NAN;
    };
}

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
        int read_Tycho_catalog(CelestialObject** cels, int max);
        int read_Uranometria_catalog(CelestialObject** cels, int max);
        int read_WD_catalog(CelestialObject** cels, int max);
        int read_GCVS_catalog(CelestialObject** cels);          // Variability only; adds no stars, so no max.

        // Galaxies. UNGC first, its distances being individually measured; RC3 has only radial
        // velocities, useless inside the Local Volume, and skips whatever the UNGC already placed.
        int read_UNGC_catalog(CelestialObject** cels, int max);
        int read_RC3_catalog(CelestialObject** cels, int max);
        int read_cons_boundaries();

        // Binary and Multiple Systems
        int read_CCDM_catalog(CelestialObject** cels, int max);
        int read_SB9_catalog(CelestialObject** cels, int max);

        // Planets, Minor Planets, Comets
        static bool load_asteroid(AstorbRow *r, char *buffer = nullptr);        // If buffer is null, open astorb.dat and search for the astroid matching the row object.
        int read_astorb_catalog(CelestialObject** cels, int max);
        static bool load_comet(CometRow *r, char *buffer = nullptr);            // Same bargain as load_asteroid: pass the line if you already have it, or let it go find the line itself.
        int read_comets_catalog(CelestialObject** cels, int max);
        unsigned int load_exoplanets_from_tap(bool stars_only = false);         // If stars_only, just verify stars/add new stars don't attempt planets.
        int read_exoplanets_catalog(CelestialObject** cels, int max);           // Old method requiring manual download

        // Internal Catalogs
        int read_starname_dat(CelestialObject** cels);                          // No max because we are not adding stars, only setting names.
        int read_star_orbits_dat(CelestialObject** cels);
        int read_local_planets(CelestialObject** cels, int max, CelestialObject* must_orbit = nullptr, CelestialObject* mustnt_orbit = nullptr);

        // Condensed star catalog
        std::string get_condensed_starcat_name();
        int write_condensed_star_cat(ConsBins cb);
        int read_condensed_star_cat();

    protected:
        static void read_field_onebased(const char* buffer, size_t start, int end, char* out);
        void apply_exoplanet_names(std::map<int, std::vector<int>> planet_celids);
        bool worth_searching(std::string star_name);
        void write_condensed_star_cat_line(FILE *fp, Star *s);

        // Helpers for load_exoplanets_from_tap: one TAP row (or cache line) into an ExoRow, then
        // resolve/create the host star and the planet. Separate so that the same derivation runs
        // whether the data came off the wire or out of the derived caches.
        static ExoRow exorow_from_json(const json& row, bool* ok);
        static void exorow_write_line(FILE* fp, const ExoRow& row);
        static bool exorow_read_line(FILE* fp, ExoRow& row);
        Star* resolve_or_create_exostar(const ExoRow& row, bool loaded_starsonly, bool* was_new);
        void add_exoplanet_from_row(const ExoRow& row, Star* host_star, std::map<int, std::vector<int>>& planet_celids, unsigned int& result);
    };
}

#endif
