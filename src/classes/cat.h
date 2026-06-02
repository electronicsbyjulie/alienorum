
#ifndef _CatalogReader
#define _CatalogReader

#include <string>
#include <vector>
#include <fstream>
#include "point.h"
#include "galaxy.h"
#include "star.h"
#include "planet.h"
#include "moon.h"

extern std::vector<std::string> known_catalog_names;
extern std::vector<std::string> consline_a, consline_b;
extern std::vector<int> considx, lnpercons;
extern std::vector<Cartesian2D> conscen;
extern int nconsln;
extern int *consaidx, *consbidx;
extern bool have_Gliese, have_BSC, have_HIP, have_CCDM, have_SB9, have_astorb;
extern Star **hdcache, **hipcache;
extern std::map<int,std::map<char,Star* > > hipcomps;

#define auto_match_multiples 0

class CatalogReader
{
    public:
    std::vector<std::string>find_catalogs(std::string path);
    void download_catalogs();

    // Source Catalogs for Stars
    int read_Gliese_catalog(CelestialObject** cels, int max);
    int read_BrightStars_catalog(CelestialObject** cels, int max);
    int read_Hipparcos_catalog(CelestialObject** cels, int max);

    // Binary and Multiple Systems
    int read_CCDM_catalog(CelestialObject** cels, int max);
    int read_SB9_catalog(CelestialObject** cels, int max);

    // Planets, Minor Planets, Comets
    int read_astorb_catalog(CelestialObject** cels, int max);
    int read_exoplanets_catalog(CelestialObject** cels, int max);

    // Internal Catalogs
    int read_starname_dat(CelestialObject** cels);                  // No max because we are not adding stars, only setting names.
    int read_star_orbits_dat(CelestialObject** cels);
    int read_local_planets(CelestialObject** cels, int max);

    protected:
    void read_field_onebased(char* buffer, int start, int end, char* out);
};

#endif
