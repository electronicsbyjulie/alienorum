
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <ctime>
#include <map>
#include <unordered_map>
#include <memory>
#include <string_view>
#include <string.h>
#include "cat.h"
#include "serial.h"
#include "cons.h"
#include "shore.h"
#include "../globals.h"

// Zeta 1,2 Reticuli is the test case for binary detection: well separated in distance and angle,
// near edge-on, close enough to calibrate parallax tolerances, and NOT flagged as an A/B pair in
// the catalogs -- so the detection cannot lazily rely on that flag.
#define _debug_sbinaries_zetret 0

namespace fs = std::filesystem;

// True for the GCVS eclipsing-binary codes E, EA, EB, EW and their subtypes, in any position of a
// compound type. ELL is ellipsoidal and EP a planetary transit, so neither answers true.
static bool is_eclipsing_type(const char *gcvs_vartype)
{
    std::string t = trim(gcvs_vartype);
    for (size_t start = 0; start <= t.size(); )
    {
        size_t end = t.find_first_of("/+", start);
        std::string tok = t.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
        size_t flag = tok.find_first_of(":?");
        if (flag != std::string::npos) tok.resize(flag);
        if (tok == "E" || tok == "EA" || tok == "EB" || tok == "EW") return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

// Important: Catalogs not listed in this array will not be seen by the app and will not be loaded!
// Its function is to prevent miscellaneous files and folders in the catalogs/ dir from being mistaken for real catalogs.
std::vector<std::string> known_catalog_names =
{
    "Gliese", "GJ", "Gliese-Jahreiss",
    "HD", "HenryDraper",
    "Hipparcos",
    "Uranometria",
    "USNO", "SAO",
    "BSC", "BrightStarCatalog", "BrightStarCatalogue",
    "WD",
#if _USE_CCDM
    "CCDM",
#endif
    "SB9",
    "GCVS",
    "2MASS",
    "REGALADE",
    "GALEX",
    "UNGC",
    "RC3",
    "astorb",
    "comets"
    // TODO: Add hundreds more...
};

#if _debug_sbinaries_zetret
Star *zet1ret = nullptr, *zet2ret = nullptr;
#endif

std::vector<std::string> CatalogReader::find_catalogs(std::string path)
{
    std::vector<std::string> results;
    try
    {
        for (const auto& entry : fs::directory_iterator(path))
        {
            std::string entry_name = entry.path().filename().string();
            if (fs::is_directory(entry.path())
                &&
                std::find(known_catalog_names.begin(), known_catalog_names.end(), entry_name) != known_catalog_names.end()
               )
            {
                results.push_back(path + _FSSTR + entry_name);
            }
        }
    }
    catch (const fs::filesystem_error& e)
    {
        std::cerr << "Error: " << e.what() << '\n';
    }

    return results;
}

void CatalogReader::download_catalogs(bool hih)
{
    bool separator_yet = false;
    std::string path = "catalogs" _FILESLASH "urls.dat", cmd;
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp)
    {
        std::cerr << "File not found " << path << std::endl;
        throw 0xbadf12e;
    }

    char buffer[1024], *catname = nullptr, *url = nullptr;
    std::string destfname = "";
    int i, j, l;
    bool frist = true;
    while (fgets(buffer, 1020, fp))
    {
        if (buffer[0] == '#')
        {
            if (buffer[1] == '#' && buffer[2] == '#' && buffer[3] == '#') separator_yet = true;
            continue;
        }

        for (i=0; buffer[i] && buffer[i] <= ' '; i++);
        catname = &buffer[i];
        if (!*catname) continue;
        if (catname[0] == '#') continue;

        if (hih && !separator_yet) continue;

        for (j=i; buffer[j] && buffer[j] > ' '; j++);
        buffer[j] = 0;

        std::string destdir = (std::string)"catalogs" + _FSSTR + (std::string)catname;

        for (j++; buffer[j] && buffer[j] <= ' '; j++);

        for (l=j; buffer[l] && buffer[l] > ' '; l++);
        buffer[l] = 0;
        destfname = destdir + _FSSTR + std::string(&buffer[j]);
        if (!destfname.size()) continue;

        for (l++; buffer[l] && buffer[l] <= ' '; l++);
        url = &buffer[l];
        if (!*url) continue;

        for (; buffer[l] && buffer[l] > ' '; l++);
        buffer[l] = 0;

        // If the destination folder exists, assume we already have the catalog.
        fs::path p = destdir.c_str();
        if (!fs::exists(p))
        {
            // Create the dest folder.
            fs::create_directories(destdir);
        }

        l = destfname.size();
        std::string dest_unzipped = destfname;
        if (!strcmp(dest_unzipped.substr(l-3).c_str(), ".gz")) dest_unzipped = dest_unzipped.substr(0, l-3);
        if (!fs::exists(destfname) && !fs::exists(dest_unzipped.c_str()))
        {
            if (frist)
            {
                mtx.lock();
                loading_msg = "Downloading catalogs...";
                mtx.unlock();
                std::cout << loading_msg << std::endl;
            }

            std::cout << "Download " << catname << " as " << destfname << " from " << url << " and unzip to " << dest_unzipped << std::endl;
            // throw 0xbadc0de;

            // Download the (possibly gzipped) file.
            download_file(url, destfname);
        }

        // Any .gz files in the destination folder, unzip them.
        for (const auto& entry : fs::directory_iterator(destdir))
        {
            std::string entry_name = entry.path().filename().string();
            // std::cout << entry_name << std::endl;

            i = entry_name.size();
            j = i - 3;
            if (j>4 && !strcmp(".tar.gz", &entry_name.c_str()[j-4]))
            {
                std::string entry_path = destdir + _FSSTR + entry_name;
                extract_archive(entry_path.c_str());
            }
            else if (!strcmp(".gz", &entry_name.c_str()[j]))
            {
                std::string decompressed_name = entry_name.substr(0, entry_name.size()-3);
                std::string entry_path = destdir + _FSSTR + entry_name;
                std::string decompressed_path = destdir + _FSSTR + decompressed_name;
                if (!fs::exists((decompressed_path).c_str()))
                {
                    extract_archive(entry_path.c_str());
                }
            }
        }
    }
}

// Assumes no other star catalogs have been loaded before Gliese,
// since Gliese contains the Sun.
int CatalogReader::read_Gliese_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs" _FILESLASH "Gliese" _FILESLASH "catalog.dat";
    char buffer[300];
    char field[32];
    int num_read = 0;
    int offset, j;
    double deg, mnt, sec, ra, dec, ep_y, ra2k, dec2k, pm, pmtheta, absmagn;
    std::string build_name;
    Star *s, *A = nullptr;

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);
    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    if (!hdcache)
    {
        hdcache = new Star*[MAX_HD+1];
        memset(hdcache, 0, sizeof(Star*)*(MAX_HD+1));
    }
    if (!hipcache)
    {
        hipcache = new Star*[MAX_HIP+1];
        memset(hipcache, 0, sizeof(Star*)*(MAX_HIP+1));
    }

    FILE* fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    while (fgets(buffer, 300, fp))
    {
        s = new Star();
        s->type = star;

        //    1-  8  A8     ---     Name    *Identifier ; see remarks.
        // Note on Name: the following acronyms are used:
        //      Gl   Gliese: CNS2,                                 =1969VeARI..22....1G
        //      GJ   Gliese & Jahreiss, A&AS, 38, 423 (1979)
        //      Wo   Woolley et al.,   Roy. Obs. Ann. No. 5 (1970)
        //      NN   newly added stars (number added at CDS)
        read_field_onebased(buffer, 1, 8, field);
        if (field[0] == 'G' && field[1] == 'l')
            build_name = "GJ ";
        else if (field[0] == 'G' && field[1] == 'J')
            build_name = "GJ ";
        else if (field[0] == 'W' && field[1] == 'o')
            build_name = "GJ ";
        else if (field[0] == 'N' && field[1] == 'N')
            build_name = "GJ ";
        else build_name = trim(field);

        float f = atof(&field[2]);
        j = floor(f);
        if (j)
        {
            build_name += std::to_string(j);
            if (field[6] == '.')
                build_name += std::string(&field[6]);
        }

        //   9- 10  A2     ---     Comp     Components (A,B,C,... )
        read_field_onebased(buffer, 9, 10, field);
        if (field[0] == '1') field[0] = ' ';                // SMH!!!
        std::string comp = trim(field);
        if (comp.size())
        {
            if (comp.c_str()[0] <= 'A')
            {
                A = nullptr;
            }

            build_name += (std::string)" " + comp;
            if (field[0] == '-') field[0] = 'D';            // GJ 1255 fix
            if (field[0] == 'A') A = s;
            s->set_component(field[0], A);

            // Special case for Proxima since Gliese et al couldn't be bothered to group it with Alp Cen AB.
            if ((fabs(f-559) < 0.05) && A && s->get_component() > 'A')
            {
                Star *C = (Star*) cels[find_object("GJ 551", true)];
                A->multisys->add_member(C, 'C');

                /*// Also prevent duplicate loading from Hipparcos.
                s->HIP = 70890;
                hipcache[70890] = s;*/
            }

            // Special case for Zeta Reticuli
            if ((fabs(f-138) < 0.05) && A && s->get_component() > 'A')
            {
                Star *B = (Star*) cels[find_object("GJ 136", true)];
                A->multisys->add_member(B, 'B');
            }
        }

        strcpy(s->Gliese, trim(build_name).c_str());
        strcpy(s->name, s->Gliese);
        if (!strcmp(s->Gliese, "GJ 324 B"))
        {
            strcpy(s->Flamsteed, "55 Cnc B");                   // For exoplanets
            strcpy(s->name, "55 Cnc B");
            s->namelen = strlen(s->name);
            s->has_custom_name = true;
        }
        if (!strcmp(s->Gliese, "GJ 22 AC"))
        {
            s->HIP = 2552;
            hipcache[2552] = s;
        }

        double ra, dec, ra2000, dec2000;

        //  13- 14  I2     h       RAh      ? Right Ascension B1950 (hours)
        read_field_onebased(buffer, 13, 14, field);
        deg = atof(field) * 15;

        //  16- 17  I2     min     RAm      ? Right Ascension B1950 (minutes)
        read_field_onebased(buffer, 16, 17, field);
        mnt = atof(field) * 15;

        //  19- 20  I2     s       RAs      ? Right Ascension B1950 (seconds)
        read_field_onebased(buffer, 19, 20, field);
        sec = atof(field) * 15;

        ra = (deg + mnt/60 + sec/3600) * fiftyseventh;

        //      22  A1     ---     DE-      Declination B1950 (sign)
        read_field_onebased(buffer, 22, 22, field);
        int sgndecl = (field[0] == '-') ? -1 : 1;

        //  23- 24  I2     deg     DEd      ? Declination B1950 (degrees)
        read_field_onebased(buffer, 23, 24, field);
        deg = atof(field);

        //  26- 29  F4.1   arcmin  DEm      ? Declination B1950 (minutes)
        read_field_onebased(buffer, 26, 29, field);
        mnt = atof(field);
        sec = 0;

        dec = (deg + mnt/60 + sec/3600) * fiftyseventh * sgndecl;
        if (ra || dec)
        {
            convert_to_J2000(ra, dec, 1950, ra2000, dec2000, true);
            s->right_ascension = ra2000;
            s->declination = dec2000;
            s->epoch = J2000; // 2433282.42345905;
        }
        else
        {
            s->mass = solar_mass;
            s->epoch = J2000;
        }

        //  31- 36  F6.3 arcsec/yr pm       ? Total proper motion
        read_field_onebased(buffer, 31, 36, field);
        pm = atof(field) / 3600 * fiftyseventh / oneyear;

        //  38- 42  F5.1   deg     pmPA     ? Direction angle of proper motion
        read_field_onebased(buffer, 38, 42, field);
        pmtheta = atof(field) * fiftyseventh;

        s->proper_motion_RA = pm * sin(pmtheta);
        s->proper_motion_decl = pm * cos(pmtheta);

        //  44- 49  F6.1   km/s    RV       ? Radial velocity
        read_field_onebased(buffer, 44, 49, field);
        s->radial_velocity = atof(field) * 1000;

        //  55- 66  A12    ---     Sp       Spectral type or color class
        read_field_onebased(buffer, 55, 66, field);
        strcpy(s->spectral_type, trim(field).c_str());

        //  68- 73  F6.2   mag     Vmag     Apparent magnitude
        read_field_onebased(buffer, 68, 73, field);
        s->apparent_magnitude = atof(field);

        //  76- 80  F5.2   mag     B-V      ? color
        read_field_onebased(buffer, 76, 80, field);
        s->BV_color = atof(field);

        //  83- 87  F5.2   mag     U-B      ? color
        read_field_onebased(buffer, 83, 87, field);
        s->UB_color = atof(field);

        //  90- 94  F5.2   mag     R-I      ? color
        read_field_onebased(buffer, 90, 94, field);
        s->RI_color = atof(field);

        // 109-114  F6.1   mas     plx      ? Resulting parallax
        read_field_onebased(buffer, 109, 114, field);
        s->parallax = atof(field) / 1000 / 3600 * fiftyseventh;

        // 122-126  F5.2   mag     Mv       Absolute visual magnitude
        read_field_onebased(buffer, 122, 126, field);
        if (trim(field).size())
        {
            absmagn = atof(field);
            if (absmagn) s->absolute_magnitude = absmagn;
            s->distance = CelestialObject::distance_from_magnitudes(s->apparent_magnitude, absmagn);
            s->distance_known = true;
        }

        // Sun is distance zero.
        if (!s->right_ascension && !s->declination)
        {
            if (!num_read)
            {
                s->distance = 0;
                s->distance_known = true;
                s->mass = solar_mass;
                s->volumetric_mean_radius = solar_radius;
                assert(!isinf(s->volumetric_mean_radius));
            }
            else
            {
                if (s->multisys)
                {
                    s->multisys->unlink();
                    delete s->multisys;
                }
                delete s;
                continue;
            }
        }

        // 147-152  I6     ---     HD       [15/352860]? designation
        read_field_onebased(buffer, 147, 152, field);
        s->HD = atoi(field);
        if (!hdcache[s->HD]) hdcache[s->HD] = s;

        // 154-166  A13    ---     DM       Durchmusterung Identification (BD/CD/CP survey+zone+sequential)
        // Many Gliese/Woolley entries (e.g. old "Wo" numbers, like GJ 9827) carry no HD or HIP at
        // all, so this is often the only identifier this catalog shares with Hipparcos/BSC -- it's
        // what lets those later readers recognize the star instead of creating a duplicate.
        read_field_onebased(buffer, 154, 155, field);
        if ((field[0] == 'B' || field[0] == 'C') && field[1] != ' ')
        {
            s->Bonn_survey[0] = field[0];
            s->Bonn_survey[1] = field[1];
            read_field_onebased(buffer, 156, 158, field);
            s->Bonn_survey_sign = field[0];
            s->Bonn_survey_declination = atoi(field);
            read_field_onebased(buffer, 159, 166, field);
            s->Bonn_survey_sequential = atoi(field);

            std::string dmkey = bonn_survey_key(s->Bonn_survey, s->Bonn_survey_declination, s->Bonn_survey_sequential);
            if (dmkey.size() && !dmcache.count(dmkey)) dmcache[dmkey] = s;
        }

        s->update_location(J2000_TIME_T);

        if (!num_read)
        {
            Rotation rot = align_points_3d(solar_north, ecliptic_north, center);
            s->obliquity = rot.a;
            s->equinox = find_angle_along_vector(rot.v, zaxis, center, yaxis);
            if (s->equinox < 0) s->equinox += (_pi*2);

            s->location.local_system_plane = s->location.equatorial_plane = rot;
            s->known_poles = true;
        }
        else
        {
            s->update_location(J2000_TIME_T);
            // Assumed 90 degree inclination for all extrasolar systems unless inclination known.
            s->location.equatorial_plane = s->location.local_system_plane =
                align_points_3d(cels[0]->location.system_center, Point(0,0,light_year*1e9), s->location.system_center);
        }

#if _debug_sbinaries_zetret
        if (s->HD == 20766) zet1ret = s;
        else if (s->HD == 20807)
        {
            zet2ret = s;
            std::cout << "Gliese Zeta Reticuli separation:"
                      << " RA " << fabs(zet1ret->right_ascension - zet2ret->right_ascension)
                      << " Decl " << fabs(zet1ret->declination - zet2ret->declination)
                      << " plx " << fabs(zet1ret->parallax - zet2ret->parallax)
                      << " pmRA " << fabs(zet1ret->proper_motion_RA - zet2ret->proper_motion_RA)
                      << " pmDE " << fabs(zet1ret->proper_motion_decl - zet2ret->proper_motion_decl)
                      << std::endl;
        }
#endif

        if (num_read)
        {
            s->estimate_radius();
            s->estimate_mass();
        }

        append_cel(s);

        num_read++;
        if ((offset+num_read) >= (max-1)) break;

        mtx.lock();
        if (!(num_read % 123)) loading_msg = std::string("Loaded ") + std::to_string(num_read) + std::string(" objects from Gliese's Third Catalogue of Nearby Stars...");
        mtx.unlock();
    }

    fclose(fp);

    ncelobjs = num_read;
    path = "catalogs" _FILESLASH "CNS5" _FILESLASH "cns5.dat";
    fp = fopen(path.c_str(), "rb");
    if (!fp)
    {
        std::cerr << "ERROR: Missing CNS5 catalog." << std::endl;
        return num_read;
    }
    while (fgets(buffer, 300, fp))
    {
        //   6- 11  A6    ---       GJ      Gliese-Jahreiss number (gj_id)
        read_field_onebased(buffer, 6, 11, field);
        float f = atof(field);
        if (f > 9848.05) continue;              // 9848 is the highest number in the CNS3
        std::string Gliese = trim(field);
        if (!strcmp(Gliese.c_str(), "Sun")) continue;
        Gliese = std::string("GJ ") + Gliese;

        Star *s = nullptr;
        for (j=0; !s && j<ncelobjs; j++) if (!strcmp(Gliese.c_str(), ((Star*)cels[j])->Gliese)) s = (Star*)cels[j];

        // CNS5 drops spectral type and B-V, so its new stars are unusable here: it serves only
        // to update a few fields (radial velocity) on stars CNS3 already supplied.
        if (!s) continue;

        //  48- 53  I6    ---       HIP     ?=- Hipparcos identifier (hip_id)
        read_field_onebased(buffer, 48, 53, field);
        s->HIP = atoi(field);
        if (!hipcache[s->HIP]) hipcache[s->HIP] = s;

        //  55- 74 F20.16 deg       RAdeg   ?=- Right ascension (J2000) at Ep=Epoch (ra)
        read_field_onebased(buffer, 55, 74, field);
        ra = atof(field) * fiftyseventh;
        //  76- 98 F23.19 deg       DEdeg   ?=- Declination (J2000) at Ep=Epoch (dec)
        read_field_onebased(buffer, 76, 98, field);
        dec = atof(field) * fiftyseventh;
        // 100-108  F9.4  yr        Epoch   [1991.25/2017.97]?=- Reference epoch for coordinates (epoch)
        read_field_onebased(buffer, 100, 108, field);
        ep_y = atof(field);
        convert_to_J2000(ra, dec, ep_y, ra2k, dec2k, false);

        s->right_ascension = ra2k;
        s->declination = dec2k;
        s->epoch = J2000;

        // 130-148 F19.15 mas       plx     ?=- Absolute trigonometric parallax (parallax)
        read_field_onebased(buffer, 130, 148, field);
        s->parallax = atof(field) * 1e-3;
        s->distance = parsec / s->parallax;
        s->parallax *= 2.777777777777e-4 * fiftyseventh;
        s->distance_known = true;
        double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
        s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;

        // 184-206 F23.17 mas/yr    pmRA    ?=- Proper motion in right ascension (d(RAcosDE)/dt) (pmra)
        read_field_onebased(buffer, 184, 206, field);
        ra = atof(field) * 2.777777777777e-4 * fiftyseventh / oneyear;
        s->proper_motion_RA = ra * cos(s->declination);

        // 230-252 F23.17 mas/yr    pmDE    ?=- Proper motion in declination (pmdec)
        read_field_onebased(buffer, 230, 252, field);
        dec = atof(field) * 2.777777777777e-4 * fiftyseventh / oneyear;
        s->proper_motion_decl = dec;

        // 297-319 F23.18 km/s      RV      ?=- Spectroscopic radial velocity (rv)
        read_field_onebased(buffer, 297, 319, field);
        s->radial_velocity = atof(field) * 1000;
    }

    return num_read;
}

int CatalogReader::read_BrightStars_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs" _FILESLASH "BSC" _FILESLASH "catalog";
    char buffer[65536];
    char field[32];
    int num_read = 0;
    int offset, j;
    uint32_t HD, HR;
    double deg, mnt, sec;
    bool HDfound;
    double f;
    // StarMulti *current_multi = nullptr;
    double current_multi_ra = -1e9, current_multi_decl = -1e9;
#define ra_dec_multi_limit (fiftyseventh / 60)

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    if (!hdcache)
    {
        hdcache = new Star*[MAX_HD+1];
        memset(hdcache, 0, sizeof(Star*)*(MAX_HD+1));
    }
    FILE* fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    Star *s, *A = nullptr;
    while (fgets(buffer, 65520, fp))
    {
        bool is_new = false;

        //   1-  4  I4     ---     HR       [1/9110]+ Harvard Revised Number = Bright Star Number
        read_field_onebased(buffer, 1, 4, field);
        HR = atoi(field);

        read_field_onebased(buffer, 26, 31, field);
        HD = atoi(field);

        //  15- 25  A11    ---     DM       Durchmusterung Identification (zone in bytes 17-19)
        // Parsed ahead of the existence check (rather than where the rest of the fields are read,
        // further below) so it can double as a cross-catalog match key: some BSC stars carry a DM
        // but no HD, and may already exist under that DM from a prior Gliese load.
        char dm_survey[3] = {0,0,0};
        char dm_sign = 0;
        int dm_decl = 0;
        unsigned int dm_seq = 0;
        read_field_onebased(buffer, 15, 16, field);
        dm_survey[0] = field[0];
        dm_survey[1] = field[1];
        read_field_onebased(buffer, 17, 19, field);
        dm_sign = field[0];
        dm_decl = atoi(field);
        read_field_onebased(buffer, 20, 25, field);
        dm_seq = atoi(field);
        std::string dmkey = bonn_survey_key(dm_survey, dm_decl, dm_seq);

        HDfound = false;
        if (HD)
        {
            if (hdcache[HD])
            {
                s = hdcache[HD];
                HDfound = true;
            }
            for (j=0; j<offset; j++)
            {
                if (cels[j]->type == star && ((Star*)cels[j])->HD == HD)
                {
                    HDfound = true;
                    s = (Star*)cels[j];
                    break;
                }
            }
        }

        // A shared Durchmusterung entry does not always mean the same star. Only trust the DM match when it doesn't contradict an HD# already known to be distinct.
        if (!HDfound && dmkey.size() && dmcache.count(dmkey))
        {
            Star *dmcand = dmcache[dmkey];
            if (!dmcand->HD || !HD || dmcand->HD == HD)
            {
                s = dmcand;
                HDfound = true;
            }
        }

        if (!HDfound)
        {
            s = new Star();
            s->type = star;
            is_new = true;
        }

        read_field_onebased(buffer, 1, 4, field);
        s->HR = HR;

        //   5- 14  A10    ---     Name     Name, generally Bayer and/or Flamsteed name
        read_field_onebased(buffer, 5, 14, field);
        if (strlen(trim(field).c_str())) strcpy(s->name, trim(field).c_str());
        s->namelen = strlen(s->name);

        s->Bonn_survey[0] = dm_survey[0];
        s->Bonn_survey[1] = dm_survey[1];
        s->Bonn_survey_sign = dm_sign;
        s->Bonn_survey_declination = dm_decl;
        s->Bonn_survey_sequential = dm_seq;
        if (dmkey.size() && !dmcache.count(dmkey)) dmcache[dmkey] = s;

        read_field_onebased(buffer, 5, 7, field);
        s->FlamsteedNo = atoi(field);
        read_field_onebased(buffer, 8, 11, field);
        std::string bayer = trim(field);
        if (!bayer.empty() && field[3] < 'A')
        {
            s->BayerGrkno = grkno_from_abbrev(field);
        }
        read_field_onebased(buffer, 12, 14, field);
        std::string cons = trim(field);

        if (cons.size())
        {
            if (bayer.size())
            {
                strcpy(s->Bayer, (bayer
                                  + std::string(bayer.size() < 3 ? " " : "")
                                  + std::string(bayer.size() < 4 ? " " : "")
                                  + cons).c_str());

                strcpy(s->constellation, cons.c_str());
            }

            if (s->FlamsteedNo)
            {
                strcpy(s->Flamsteed, (std::to_string(s->FlamsteedNo)
                                      + std::string((s->FlamsteedNo < 10) ? " " : "")
                                      + std::string((s->FlamsteedNo < 100) ? " " : "")
                                      + std::string((s->FlamsteedNo < 1000) ? " " : "")
                                      + cons).c_str());

                strcpy(s->constellation, cons.c_str());
            }
        }

        //   26- 31  I6     ---     HD       [1/225300]? Henry Draper Catalog Number
        read_field_onebased(buffer, 26, 31, field);
        if (strlen(trim(field).c_str())) s->HD = atoi(field);

        //   32- 37  I6     ---     SAO      [1/258997]? SAO Catalog Number
        read_field_onebased(buffer, 32, 37, field);
        s->SAO = atoi(field);

        s->gotta_be_named_something();

        //   76- 77  I2     h       RAh      ?Hours RA, equinox J2000, epoch 2000.0 (1)
        read_field_onebased(buffer, 76, 77, field);
        deg = atof(field) * 15;

        //   78- 79  I2     min     RAm      ?Minutes RA, equinox J2000, epoch 2000.0 (1)
        read_field_onebased(buffer, 78, 79, field);
        mnt = atof(field) * 15;

        //   80- 83  F4.1   s       RAs      ?Seconds RA, equinox J2000, epoch 2000.0 (1)
        read_field_onebased(buffer, 80, 83, field);
        sec = atof(field) * 15;

        s->right_ascension = (deg + mnt/60 + sec/3600) * fiftyseventh;

        //       84  A1     ---     DE-      ?Sign Dec, equinox J2000, epoch 2000.0 (1)
        read_field_onebased(buffer, 84, 84, field);
        int sgndecl = (field[0] == '-') ? -1 : 1;

        //   85- 86  I2     deg     DEd      ?Degrees Dec, equinox J2000, epoch 2000.0 (1)
        read_field_onebased(buffer, 85, 86, field);
        deg = atof(field);

        //   87- 88  I2     arcmin  DEm      ?Minutes Dec, equinox J2000, epoch 2000.0 (1)
        read_field_onebased(buffer, 87, 88, field);
        mnt = atof(field);

        //   89- 90  I2     arcsec  DEs      ?Seconds Dec, equinox J2000, epoch 2000.0 (1)
        read_field_onebased(buffer, 89, 90, field);
        sec = atof(field);

        s->declination = (deg + mnt/60 + sec/3600) * fiftyseventh * sgndecl;
        if (!s->right_ascension && !s->declination)
        {
            if (is_new) delete s;
            continue;
        }
        s->epoch = J2000;

        if (!s->get_component() && buffer[49] > ' ' && strcmp(s->name, "41The1Ori"))
        {
            if (fabs(current_multi_ra - s->right_ascension) > ra_dec_multi_limit
                    && fabs(current_multi_decl - s->declination) > ra_dec_multi_limit
               )
            {
                // current_multi = nullptr;
                current_multi_ra = s->right_ascension;
                current_multi_decl = s->declination;
                A = nullptr;
            }
            s->set_component(buffer[49], A);
            // current_multi = s->multisys;
            if (buffer[49] == 'A') A = s;
        }

        //  103-107  F5.2   mag     Vmag     ?Visual magnitude (1)
        read_field_onebased(buffer, 103, 107, field);
        s->apparent_magnitude = atof(field);

        //  110-114  F5.2   mag     B-V      ? B-V color in the UBV system
        read_field_onebased(buffer, 110, 114, field);
        s->BV_color = atof(field);

        //  116-120  F5.2   mag     U-B      ? U-B color in the UBV system
        read_field_onebased(buffer, 116, 120, field);
        s->UB_color = atof(field);

        //  122-126  F5.2   mag     R-I      ? R-I   in system specified by n_R-I
        read_field_onebased(buffer, 122, 126, field);
        s->RI_color = atof(field);

        //  128-147  A20    ---     SpType   Spectral type
        read_field_onebased(buffer, 128, 147, field);
        strcpy(s->spectral_type, trim(field).c_str());

        //  149-154  F6.3 arcsec/yr pmRA    *?Annual proper motion in RA J2000, FK5 system
        read_field_onebased(buffer, 149, 154, field);
        s->proper_motion_RA = atof(field) * fiftyseventh / 3600 / oneyear;

        //  155-160  F6.3 arcsec/yr pmDE     ?Annual proper motion in Dec J2000, FK5 system
        read_field_onebased(buffer, 155, 160, field);
        s->proper_motion_decl = atof(field) * fiftyseventh / 3600 / oneyear;

        //  162-166  F5.3   arcsec  Parallax ? Trigonometric parallax (unless n_Parallax)
        read_field_onebased(buffer, 162, 166, field);
        f = atof(field);
        if (f > 0)
        {
            s->parallax = f;
            s->distance = parsec / s->parallax;
            s->distance_known = true;
            s->parallax /= fiftyseven * 3600;
        }
        else if (!s->distance_known) s->distance = light_year*1e4;

        //  167-170  I4     km/s    RadVel   ? Heliocentric Radial Velocity
        read_field_onebased(buffer, 167, 170, field);
        s->radial_velocity = atof(field) * 1000;

        // Estimate some more parameters based on the data.
        if (!strlen(s->name))
        {
            if (s->HD) strcpy(s->name, ((std::string)"HD" + std::to_string(s->HD)).c_str());
            s->namelen = strlen(s->name);
        }

        s->VR_color = (s->RI_color + s->BV_color*2) / 3;      // VERY rough estimate
        double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
        s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;

        s->estimate_radius();
        s->estimate_mass();

        s->update_location(J2000_TIME_T);
        // Assumed 90 degree inclination for all extrasolar systems unless inclination known.
        if (!s->Gliese[0])
            s->location.equatorial_plane = s->location.local_system_plane =
                                               align_points_3d(cels[0]->location.system_center, Point(0,0,light_year*1e9), s->location.system_center);

#if _debug_sbinaries_zetret
        if (s->HD == 20766) zet1ret = s;
        else if (s->HD == 20807)
        {
            zet2ret = s;
            std::cout << "BSC Zeta Reticuli separation:"
                      << " RA " << fabs(zet1ret->right_ascension - zet2ret->right_ascension)
                      << " Decl " << fabs(zet1ret->declination - zet2ret->declination)
                      << " plx " << fabs(zet1ret->parallax - zet2ret->parallax)
                      << " pmRA " << fabs(zet1ret->proper_motion_RA - zet2ret->proper_motion_RA)
                      << " pmDE " << fabs(zet1ret->proper_motion_decl - zet2ret->proper_motion_decl)
                      << std::endl;
        }
#endif

        // Register every component's own HD, not just the 'A'/primary one: each ADS component here
        // carries its own distinct HD number (e.g. Gam1Vel/HD68243 vs Gam2Vel/HD68273, comp 'B'/'A'
        // respectively), so gating this on comp<='A' left 'B'/'C' components unfindable by HD --
        // Hipparcos would then create a second, Bayer-less duplicate for that HD, and Uranometria
        // would attach that component's Gould number to the duplicate instead of to the real star.
        if (HD) hdcache[HD] = s;

        if (is_new)
        {
            append_cel(s);
            num_read++;
            if ((offset+num_read) >= (max-1)) break;
        }

        mtx.lock();
        if (!(num_read % 123)) loading_msg = std::string("Loaded ") + std::to_string(num_read) + std::string(" objects from Bright Star Catalogue...");
        mtx.unlock();
    }
    fclose(fp);
    return num_read;
}

bool CatalogReader::load_Hipparcos_orbit_line(const char* buffer, CelestialObject** cels, int max, int& offset)
{
    char field[32];
    uint32_t HIP;
    Star *s = nullptr;

    //   1-  6  I6    ---      HIP      Identifier (HIP)                         (D01)
    read_field_onebased(buffer, 1, 6, field);
    HIP = atoi(field);

    Star* A = nullptr;
    if (hipcache && HIP <= MAX_HIP)
    {
        A = hipcache[HIP];
        if (A && (A->seqno < 0 || A->seqno >= ncelobjs || cels[A->seqno] != A))
        {
            A = hipcache[HIP] = nullptr;
        }
    }
    if (!A)
    {
        for (int i = 0; cels[i]; i++)
        {
            if (cels[i]->typeclass() == class_star && ((Star*)cels[i])->HIP == HIP)
            {
                A = (Star*)cels[i];
                if (hipcache && HIP <= MAX_HIP)
                {
                    hipcache[HIP] = A;
                }
                break;
            }
        }
    }
    if (!A)
    {
        return false;
    }

    A->set_component('A', A);

    s = A->multisys->get_member('B');

    if (!s)
    {
        s = new Star();
        append_cel(s);
        offset++;
        s->absolute_magnitude = 1e29;
    }
    s->make_companion_of(A, 'B');
    s->epoch = J2000 + (1991.25 - 2000);

    //   8- 17  F10.4 d        P        Orbital period                           (DO2)
    read_field_onebased(buffer, 8, 17, field);
    s->orbit->period = atof(field) * oneday;

    //  19- 29  F11.4 d        T       *Time of periastron passage (JD-2440000)  (DO3)
    read_field_onebased(buffer, 19, 29, field);
    s->orbit->epoch = 2440000 + atof(field);

    //  31- 38  F8.2  mas      a0       Semi-major axis of photocentric orbit    (DO4)
    read_field_onebased(buffer, 31, 38, field);
    s->orbit->semimajor_axis = (atof(field)/206264806) * s->distance;

    //  40- 45  F6.4  ---      ecc      [0,1] Eccentricity                       (DO5)
    read_field_onebased(buffer, 40, 45, field);
    s->orbit->eccentricity = atof(field);

    //  47- 52  F6.2  deg      w       *[0,360] Argument of periastron           (DO6)
    read_field_onebased(buffer, 47, 52, field);
    s->orbit->arg_periapsis = atof(field) * fiftyseventh;

    //  54- 59  F6.2  deg      i       *[0,180] Inclination                      (DO7)
    read_field_onebased(buffer, 54, 59, field);
    double inclination = atof(field) * fiftyseventh;
    s->orbit->inclination = A->obliquity = 0;
    s->orbit->heliocentric_inclination = inclination;

    //  61- 66  F6.2  deg      Omega   *[0,360] Position angle of the node       (DO8)
    read_field_onebased(buffer, 61, 66, field);
    double node = atof(field) * fiftyseventh;
    s->orbit->ascending_node = A->obliquity = 0;
    s->orbit->heliocentric_node = node;

    A->known_poles = true;
    A->obliquity = 0;
    A->equinox = 0;
    if (!A->location.system_center.magnitude())
    {
        A->location.system_center = Point::from_ra_dec(A->right_ascension, A->declination, A->distance);
    }
    A->location.local_system_plane = system_plane_from_incl_and_node(inclination, node, A->location.system_center);
    A->location.orbital_plane = A->location.local_system_plane;
    A->location.equatorial_plane = A->location.local_system_plane;
    A->lock_system_plane = true;
    A->lock_equatorial_plane = true;

    s->location = A->location;
    s->location.local_system_plane = A->location.local_system_plane;
    s->location.orbital_plane = A->location.local_system_plane;
    s->location.equatorial_plane = A->location.equatorial_plane;
    s->lock_system_plane = true;
    s->lock_equatorial_plane = true;
    s->known_poles = true;
    s->obliquity = 0;
    s->equinox = 0;

    s->distance_known = true;
    s->apparent_magnitude = 11;         // placeholder
    if (s->absolute_magnitude > 1e28)
    {
        s->absolute_magnitude = A->absolute_magnitude + 1;      // garbage number
    }

    return true;
}

int CatalogReader::read_Hipparcos_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs" _FILESLASH "Hipparcos" _FILESLASH "hip_main.dat";
    char buffer[1024];
    char field[32];
    char Bonn[32], Cordoba[32], Cape[32];
    int num_read = 0;
    int offset, j;
    uint32_t HD, HIP;
    double deg, mnt, sec, RA, Decl, f;
    Star *s, *A = nullptr;

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);
    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    if (!hdcache)
    {
        hdcache = new Star*[MAX_HD+1];
        memset(hdcache, 0, sizeof(Star*)*(MAX_HD+1));
    }
    if (!hipcache)
    {
        hipcache = new Star*[MAX_HIP+1];
        memset(hipcache, 0, sizeof(Star*)*(MAX_HIP+1));
    }

    for (j=0; j<offset; j++)
    {
        if (cels[j]->typeclass() != class_star) continue;
        if (((Star*)cels[j])->HD) hdcache[((Star*)cels[j])->HD] = (Star*)cels[j];
        if (((Star*)cels[j])->HIP) hipcache[((Star*)cels[j])->HIP] = (Star*)cels[j];
        Star *dms = (Star*)cels[j];
        if (dms->Bonn_survey[0])
        {
            std::string k = bonn_survey_key(dms->Bonn_survey, dms->Bonn_survey_declination, dms->Bonn_survey_sequential);
            if (k.size() && !dmcache.count(k)) dmcache[k] = dms;
        }
    }

    FILE* fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    while (fgets(buffer, 1020, fp))
    {
        //   9- 14  I6    ---     HIP       Identifier (HIP number)
        read_field_onebased(buffer, 9, 14, field);
        HIP = atoi(field);

        // 391-396  I6    ---     HD        [1/359083]? HD number <III/135>
        read_field_onebased(buffer, 391, 396, field);
        HD = atoi(field);

        // 398-429  Bonner/Cordoba/Cape Durchmusterung, parsed ahead of the existence check below so
        // it can serve as a fallback match key: a star loaded from Gliese under an old "Wo"/"NN"
        // designation (e.g. GJ 9827 / BD-02 5958) often carries neither HD nor HIP, so without this
        // it silently gets recreated as a second, planet-less duplicate under this record's HIP name.
        char dm_survey[3] = {0,0,0};
        char dm_sign = 0;
        int dm_decl = 0;
        unsigned int dm_seq = 0;
        read_field_onebased(buffer, 398, 407, Bonn);
        read_field_onebased(buffer, 409, 418, Cordoba);
        read_field_onebased(buffer, 420, 429, Cape);
        if (Cape[0] != ' ')
        {
            dm_survey[0] = 'C';
            dm_survey[1] = 'P';
            read_field_onebased(buffer, 421, 423, field);
            dm_sign = field[0];
            dm_decl = atoi(field);
            read_field_onebased(buffer, 424, 429, field);
            dm_seq = atoi(field);
        }
        else if (Cordoba[0] != ' ')
        {
            dm_survey[0] = 'C';
            dm_survey[1] = 'D';
            read_field_onebased(buffer, 410, 412, field);
            dm_sign = field[0];
            dm_decl = atoi(field);
            read_field_onebased(buffer, 413, 418, field);
            dm_seq = atoi(field);
        }
        else if (Bonn[0] != ' ')
        {
            dm_survey[0] = 'B';
            dm_survey[1] = 'D';
            read_field_onebased(buffer, 399, 401, field);
            dm_sign = field[0];
            dm_decl = atoi(field);
            read_field_onebased(buffer, 402, 407, field);
            dm_seq = atoi(field);
        }
        std::string dmkey = bonn_survey_key(dm_survey, dm_decl, dm_seq);

        s = nullptr;
        bool is_new = false;

        if (HD && hdcache && hdcache[HD]) s = (Star*)hdcache[HD];
        else if (HIP && hipcache && hipcache[HIP]) s = (Star*)hipcache[HIP];
        else if (dmkey.size() && dmcache.count(dmkey))
        {
            // A shared Durchmusterung entry can legitimately belong to two distinct stars; only accept the match if it doesn't contradict the HD/HIP number.
            Star *dmcand = dmcache[dmkey];
            if ((!dmcand->HD || !HD || dmcand->HD == HD) && (!dmcand->HIP || !HIP || dmcand->HIP == HIP))
                s = dmcand;
        }
        if (!s)
        {
            // There are only a handful with no V magnitude; omit them.
            read_field_onebased(buffer, 42, 46, field);
            if (!trim(field).size()) continue;
#if _filter_Hipparcos_stars_absmag
            double appmag = atof(field);
#endif

#if _filter_Hipparcos_stars_appmag
            f = atof(field);
            if (f > 9) continue;
#endif
#if _filter_Hipparcos_stars_absmag
            read_field_onebased(buffer, 80, 86, field);
            double parallax = atof(field);
            if (parallax <= 0) continue;
            double distance = (parallax > 0) ? (parsec / parallax * 1000) : light_year*1e4;
            double intrinsic_brightness = pow(magnbase, -appmag) * pow(fmax(AU, distance) / parsec / 10, 2);
            double absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
            if (absolute_magnitude > 8.5) continue;
#endif
            s = new Star();
            is_new = true;
        }

        s->HD = HD;
        s->HIP = HIP;
        if (HD) hdcache[HD] = s;
        if (HIP) hipcache[HIP] = s;

        // 328-337  A10   ---     CCDM      CCDM identifier                          (H55)
        read_field_onebased(buffer, 328, 337, field);
        s->CCDM = trim(field);

        // BD/CoD/CPD already parsed above (dm_survey/dm_sign/dm_decl/dm_seq) for the existence check;
        // apply them to the resolved star and register/refresh the cross-catalog lookup entry.
        if (dm_survey[0])
        {
            s->Bonn_survey[0] = dm_survey[0];
            s->Bonn_survey[1] = dm_survey[1];
            s->Bonn_survey_sign = dm_sign;
            s->Bonn_survey_declination = dm_decl;
            s->Bonn_survey_sequential = dm_seq;
            if (dmkey.size() && !dmcache.count(dmkey)) dmcache[dmkey] = s;
        }

        //  18- 28  A11   ---     RAhms     Right ascension in h m s, ICRS (J1991.25) (H3)
        read_field_onebased(buffer, 18, 19, field);
        deg = atof(field) * 15;

        read_field_onebased(buffer, 21, 22, field);
        mnt = atof(field) * 15;

        read_field_onebased(buffer, 24, 28, field);
        sec = atof(field) * 15;

        RA = (deg + mnt/60 + sec/3600) * fiftyseventh;

        //  30- 40  A11   ---     DEdms     Declination in deg ' ", ICRS (J1991.25)   (H4)
        read_field_onebased(buffer, 30, 30, field);
        int sgndecl = (field[0] == '-') ? -1 : 1;

        read_field_onebased(buffer, 31, 32, field);
        deg = atof(field);

        read_field_onebased(buffer, 34, 35, field);
        mnt = atof(field);

        read_field_onebased(buffer, 37, 40, field);
        sec = atof(field);

        Decl = (deg + mnt/60 + sec/3600) * fiftyseventh * sgndecl;

        if (RA && Decl)
        {
            double ra2000, dec2000;
            convert_to_J2000(RA, Decl, 1991.25, ra2000, dec2000, false);
            s->right_ascension = ra2000;
            s->declination = dec2000;
            s->epoch = J2000;
        }
        else
        {
            std::cout << "ERROR: HIP" << HIP << " has no RA/Decl" << std::endl;
            throw 0xbadda7a;
            continue;
        }

        //  80- 86  F7.2  mas     Plx       ? Trigonometric parallax
        read_field_onebased(buffer, 80, 86, field);
        f = atof(field) / 1000 / 3600 * fiftyseventh;
        if (f > 0)
        {
            // Fix for e.g. NN3254.
            if (!*(s->Gliese) || fabs(f - s->parallax) < 0.25 * fmin(f, s->parallax))
            {
                s->parallax = f;
                s->distance = (s->parallax > 0) ? (parsec / atof(field) * 1000) : light_year*1e4;
                s->distance_known = true;
            }
        }
        else if (!s->distance_known) s->distance = light_year*1e4;

        //  88- 95  F8.2 mas/yr   pmRA     *? Proper motion mu_alpha.cos(delta), ICRS
        read_field_onebased(buffer, 88, 95, field);
        f = atof(field) / 1000 / 3600 / oneyear * fiftyseventh;
        if (f) s->proper_motion_RA = f;

        //  97-104  F8.2 mas/yr   pmDE     *? Proper motion mu_delta, ICRS
        read_field_onebased(buffer, 97, 104, field);
        f = atof(field) / 1000 / 3600 / oneyear * fiftyseventh;
        if (f) s->proper_motion_decl = f;

        //  42- 46  F5.2  mag     Vmag      ? Magnitude in Johnson V                  (H5)
        read_field_onebased(buffer, 42, 46, field);
        f = atof(field);
        if (trim(field).size())
        {
            char combined_flag[2];
            read_field_onebased(buffer, 273, 273, combined_flag);
            const bool combined_magnitude = combined_flag[0] == '*';
            const bool resolved_magnitude = !is_new && s->HR > 0 && s->get_component() &&
                std::isfinite(s->apparent_magnitude) && s->apparent_magnitude >= -40 && s->apparent_magnitude < 99;
            // H43 marks combined photometry. Combined AB light must not replace
            // a component magnitude already supplied by the Bright Star Catalogue.
            if (!combined_magnitude || !resolved_magnitude)
            {
                s->apparent_magnitude = f;
            }
            double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
            s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
        }

        // 246-251  F6.3  mag     B-V       ? Johnson B-V colour
        read_field_onebased(buffer, 246, 251, field);
        f = atof(field);
        if (f || trim(field).size()) s->BV_color = f;

        // 436-447  A12   ---     SpType    Spectral type
        read_field_onebased(buffer, 436, 447, field);
        if (trim(field).size()) strcpy(s->spectral_type, trim(field).c_str());

#if _debug_sbinaries_zetret
        if (s->HD == 20766) zet1ret = s;
        else if (s->HD == 20807)
        {
            zet2ret = s;
            std::cout << "Hipparcos Zeta Reticuli separation:"
                      << " RA " << fabs(zet1ret->right_ascension - zet2ret->right_ascension)
                      << " Decl " << fabs(zet1ret->declination - zet2ret->declination)
                      << " plx " << fabs(zet1ret->parallax - zet2ret->parallax)
                      << " pmRA " << fabs(zet1ret->proper_motion_RA - zet2ret->proper_motion_RA)
                      << " pmDE " << fabs(zet1ret->proper_motion_decl - zet2ret->proper_motion_decl)
                      << std::endl;
        }
#endif

        s->gotta_be_named_something();
        s->estimate_radius();
        s->estimate_mass();
        s->update_location(J2000_TIME_T);
        if (is_new)
        {
            append_cel(s);
            offset++;
            mtx.lock();
            loading_msg = std::string("Loading Hipparcos Catalog... Added HIP") + std::to_string(s->HIP);
            mtx.unlock();
        }
        else
        {
            mtx.lock();
            loading_msg = std::string("Loading Hipparcos Catalog... Updated HIP") + std::to_string(s->HIP);
            mtx.unlock();
        }

        num_read++;
        if (num_read >= max-4) return num_read;

        mtx.lock();
        if (!(num_read % 123)) loading_msg = std::string("Loaded ") + std::to_string(num_read) + std::string(" objects from The Hipparcos Catalogue...");
        mtx.unlock();
    }

    fclose(fp);
    mtx.lock();
    loading_msg = "Building Hipparcos-CCDM Cross Reference...";
    mtx.unlock();
    path = "catalogs" _FILESLASH "Hipparcos" _FILESLASH "h_dm_com.dat";
    fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return num_read;

    while (fgets(buffer, 1020, fp))
    {
        //  43- 48  I6     ---     HIP      HIP number                               (DC8)
        read_field_onebased(buffer, 43, 48, field);
        HIP = atoi(field);
        if (!hipcache[HIP]) continue;
        s = hipcache[HIP];

        //   1- 10  A10    ---     CCDM     CCDM number                              (DC1)
        read_field_onebased(buffer, 1, 10, field);
        s->CCDM = trim(field);

        if (s->ccdm_compseq)
        {
            A = hipcache[HIP];
            if (A->seqno < 0 || cels[A->seqno] != A) A = hipcache[HIP] = nullptr;
            if (!A) continue;

            if (!A->multisys) A->set_component('A', A);
            s = A->multisys->get_member(buffer[40]);
            if (!s)
            {
                s = new Star();
                append_cel(s);
                offset++;
                if (offset >= max-2) return num_read++;
            }
            s->make_companion_of(A, buffer[40]);
            s->epoch = J2000 + (1991.25 - 2000);
        }

        //  38- 39  I2     ---     seq      Sequential component number             (DCM6)
        read_field_onebased(buffer, 38, 39, field);
        s->ccdm_compseq = atoi(field);

        if (s != hipcache[HIP])
        {
            //  50- 55  F6.3   mag     Hp       Magnitude of component                   (DC9)
            read_field_onebased(buffer, 50, 55, field);
            if (trim(field).size())
            {
                s->apparent_magnitude = atof(field);
                double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
                s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
                if (A && (s->absolute_magnitude < A->absolute_magnitude)) s->absolute_magnitude = A->absolute_magnitude + 1;
            }
        }
        num_read++;
    }

    fclose(fp);
    mtx.lock();
    loading_msg = "Loading Hipparcos Binary Star Orbits...";
    mtx.unlock();
    path = "catalogs" _FILESLASH "Hipparcos" _FILESLASH "hip_dm_o.dat";
    fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return num_read;

    while (fgets(buffer, 1020, fp))
    {
        if (load_Hipparcos_orbit_line(buffer, cels, max, offset))
        {
            num_read++;
            if (num_read >= max-4)
            {
                return num_read;
            }
        }
    }

    fclose(fp);
    mtx.lock();
    loading_msg = "Loading Hipparcos Variable Stars...";
    mtx.unlock();
    path = "catalogs" _FILESLASH "Hipparcos" _FILESLASH "hip_va_1.dat";
    fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return num_read;

    while (fgets(buffer, 1020, fp))
    {
        //   1-  6  I6    ---   HIP         Identifier (HIP)
        read_field_onebased(buffer, 1, 6, field);
        HIP = atoi(field);
        if (!HIP) continue;
        Star *s = hipcache[HIP];

        //  25- 30  A6    ---   VarType    *Variability type as in GCVS/NSV
        read_field_onebased(buffer, 25, 30, field);
        s->is_eclipsing_binary = is_eclipsing_type(field);

        //  34- 39  F6.3  mag   maxMag      Magnitude at max from curve fitting
        read_field_onebased(buffer, 34, 39, field);
        s->minmag = atof(field);

        //  43- 48  F6.3  mag   minMag      Magnitude at min from curve fitting
        read_field_onebased(buffer, 43, 48, field);
        s->maxmag = atof(field);

        //  57- 68  F12.7 d     Period      ? Mean period in days
        read_field_onebased(buffer, 57, 68, field);
        s->variability_period = atof(field) * oneday;

        //  77- 85  F9.4  d     Ep-2440000  ? Epoch (JD-2440000) of zero phase
        read_field_onebased(buffer, 77, 85, field);
        s->epoch_max_brightness = atof(field) + 2440000;

        //  93-104  A12   ---   VarName     Variable star name
        read_field_onebased(buffer, 93, 104, field);
        std::string sname = trim(field);
        std::replace(sname.begin(), sname.end(), '_', ' ');
        strcpy(s->name, sname.c_str());
        s->namelen = strlen(s->name);
    }

    fclose(fp);
    return num_read;
}

// GCVS 5.1, for the variables Hipparcos never fitted. It carries no HIP or HD column of its own,
// so stars are matched through the cross-identifications in crossid.dat.
int alienorum::CatalogReader::read_GCVS_catalog(CelestialObject **cels)
{
    if (!hipcache || !hdcache) return 0;

    char buffer[1024], field[256];
    std::string path = "catalogs" _FILESLASH "GCVS" _FILESLASH "crossid.dat";
    std::string path1 = "catalogs" _FILESLASH "GCVS" _FILESLASH "data";             // This catalog has to be a pain with the filenames.
    FILE *fp = fopen(path1.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + std::string(".gz");
        if (file_exists(gzpath.c_str()))
        {
            std::cout << "Extract " << gzpath << std::endl;
            extract_archive(gzpath.c_str());
            fp = fopen(path1.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    std::map<std::string, int> desig_HIP, desig_HD;
    while (fgets(buffer, 1020, fp))
    {
        //   1- 32  A32   ---   Name      Alternative name
        //      33  A1    ---   ---       [=]
        //  34- 59  A26   ---   VarName   Designation in the GCVS
        if (strlen(buffer) < 34 || buffer[32] != '=') continue;

        read_field_onebased(buffer, 34, 59, field);
        std::string var = squeeze_spaces(field);
        if (!var.size()) continue;

        read_field_onebased(buffer, 1, 32, field);
        std::string alt = squeeze_spaces(field);
        if (!alt.compare(0, 4, "Hip ")) desig_HIP[var] = atoi(alt.c_str() + 4);
        else if (!alt.compare(0, 3, "HD ")) desig_HD[var] = atoi(alt.c_str() + 3);
    }
    fclose(fp);

    path = "catalogs" _FILESLASH "GCVS" _FILESLASH "gcvs_cat.dat";
    fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + std::string(".gz");
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    int num_read = 0;
    while (fgets(buffer, 1020, fp))
    {
        if (strlen(buffer) < 127) continue;

        //  89- 90  A2    ---   flt       The photometric system for magnitudes
        read_field_onebased(buffer, 89, 90, field);
        // if (trim(field) != "V") continue;                   // minmag/maxmag are visual everywhere else

        //     111  A1    ---   l_Period  [<>(] Code for upper or lower limits
        // 112-127  F16.10 d    Period    ? Period of the variable star
        if (strchr("<>(", buffer[110])) continue;           // a limit, or a mean cycle time
        read_field_onebased(buffer, 112, 127, field);
        double period = atof(field);
        if (period <= 0) continue;

        //  92-102  F11.5 d     Epoch     ? Epoch for maximum light, Julian days
        read_field_onebased(buffer, 92, 102, field);
        double epoch = atof(field);
        if (epoch <= 0) continue;                           // no phase to be had without one
        epoch += 2400000;                                   // the GCVS omits the leading 24

        //  42- 51  A10   ---   VarType   Type of variability
        // The epoch is of MINIMUM light for eclipsing, ellipsoidal, RV Tau, and RS CVn types, and
        // of maximum for everything else; half a period turns the one into the other.
        read_field_onebased(buffer, 42, 51, field);
        std::string vartype = trim(field);
        bool eclipsing = is_eclipsing_type(vartype.c_str());
        std::string primary = vartype;
        size_t vtlen = primary.find_first_of("/+:");
        if (vtlen != std::string::npos) primary.resize(vtlen);
        if (primary[0] == 'E' || primary == "RS" || !primary.compare(0, 2, "RV")) epoch += period / 2;

        //      53  A1    ---   l_magMax  [<>(] Limit or amplitude symbol on magMax
        //  54- 59  F6.3  mag   magMax    ? Magnitude at maximum brightness
        if (strchr("<>(", buffer[52])) continue;
        read_field_onebased(buffer, 54, 59, field);
        if (!trim(field).size()) continue;
        double magmax = atof(field);

        //  63- 64  A2    ---   l_Min1    [< (]  Limit or amplitude symbol on Min1
        //  65- 70  F6.3  mag   Min1      ? Minimum I magnitude or amplitude
        //      74  A1    ---   ---       [)] ) if Min1 is an amplitude
        read_field_onebased(buffer, 65, 70, field);
        if (!trim(field).size()) continue;
        double min1 = atof(field);
        read_field_onebased(buffer, 63, 64, field);
        if (strchr(field, '<')) continue;                   // a faintest-seen limit, not a minimum
        if (strchr(field, '(') || buffer[73] == ')') min1 += magmax;    // Min1 is the amplitude

        //   9- 18  A10   ---   GCVS      Variable star designation
        read_field_onebased(buffer, 9, 18, field);
        std::string desig = squeeze_spaces(field);

        Star *s = nullptr;
        std::map<std::string, int>::iterator it = desig_HIP.find(desig);
        if (it != desig_HIP.end() && it->second > 0 && it->second <= MAX_HIP) s = hipcache[it->second];
        if (!s)
        {
            it = desig_HD.find(desig);
            if (it != desig_HD.end() && it->second > 0 && it->second <= MAX_HD) s = hdcache[it->second];
        }
        if (!s || s->variability_period) continue;          // Hipparcos fitted this one already

        s->minmag = magmax;
        s->maxmag = min1;
        s->variability_period = period * oneday;
        s->epoch_max_brightness = epoch;
        s->is_eclipsing_binary = eclipsing;
        num_read++;
    }

    fclose(fp);
    return num_read;
}

int alienorum::CatalogReader::read_Tycho_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs" _FILESLASH "Hipparcos" _FILESLASH "tyc_main.dat";
    char buffer[1024];
    char field[32];
    int num_read = 0;
    uint32_t HD, HIP;
    std::string TYC;

    FILE* fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    while (fgets(buffer, 1020, fp))
    {
        //   3- 14  A12   ---     TYC      *TYC1-3 (TYC number)
        read_field_onebased(buffer, 3, 6, field);
        TYC = std::to_string(atoi(field));

        read_field_onebased(buffer, 8, 12, field);
        TYC += std::string("-") + std::to_string(atoi(field));

        read_field_onebased(buffer, 14, 14, field);
        TYC += std::string("-") + std::to_string(atoi(field));

        // 211-216  I6    ---     HIP       ? Hipparcos HIP number
        read_field_onebased(buffer, 211, 216, field);
        HIP = atoi(field);

        // 310-315  I6    ---     HD        [1/359083]? HD cat. <III/135>
        read_field_onebased(buffer, 310, 315, field);
        HD = atoi(field);

        if (HIP && hipcache[HIP]) continue;
        if (HD && hdcache[HD]) continue;
        // std::cout << buffer << std::endl;

        Star *s = new Star();
        std::string starname;

        if (HD) starname = std::string("HD") + std::to_string(HD);
        else if (HIP) starname = std::string("HIP") + std::to_string(HIP);
        else starname = std::string("TYC") + TYC;

        //  42- 46  F5.2  mag     Vmag      ? Magnitude in Johnson V
        read_field_onebased(buffer, 42, 46, field);
        s->apparent_magnitude = atof(field);

        //  52- 63  F12.8 deg     RAdeg    *alpha, degrees (ICRS, Epoch=J1991.25)
        read_field_onebased(buffer, 52, 63, field);
        s->right_ascension = atof(field) * fiftyseventh;

        //  65- 76  F12.8 deg     DEdeg    *delta, degrees (ICRS, Epoch=J1991.25)
        read_field_onebased(buffer, 65, 76, field);
        s->declination = atof(field) * fiftyseventh;

        if (s->right_ascension && s->declination)
        {
            double ra2000, dec2000;
            convert_to_J2000(s->right_ascension, s->declination, 1991.25, ra2000, dec2000, false);
            s->right_ascension = ra2000;
            s->declination = dec2000;
            s->epoch = J2000;
        }
        else
        {
            std::cout << "ERROR: TYC" << TYC << " has no RA/Decl" << std::endl;
            throw 0xbadda7a;
            continue;
        }

        //  80- 86  F7.2  mas     Plx      *? Trigonometric parallax
        read_field_onebased(buffer, 80, 86, field);
        double f = atof(field) / 1000 / 3600 * fiftyseventh;
        if (f > 0)
        {
            s->parallax = f;
            s->distance = (s->parallax > 0) ? (parsec / atof(field) * 1000) : light_year*1e4;
            s->distance_known = true;
        }
        else s->distance = light_year*1e4;

        double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
        s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;

        //  88- 95  F8.2 mas/yr   pmRA     *? Proper motion mu_alpha.cos(delta), ICRS
        read_field_onebased(buffer, 88, 95, field);
        f = atof(field) / 1000 / 3600 / oneyear * fiftyseventh;
        if (f) s->proper_motion_RA = f;

        //  97-104  F8.2 mas/yr   pmDE     *? Proper motion mu_delta, ICRS
        read_field_onebased(buffer, 97, 104, field);
        f = atof(field) / 1000 / 3600 / oneyear * fiftyseventh;
        if (f) s->proper_motion_decl = f;

        // 246-251  F6.3  mag     B-V       ? Johnson B-V colour
        read_field_onebased(buffer, 246, 251, field);
        s->BV_color = atof(field);

        s->update_location(simnow);
        s->mass = s->estimate_mass();
        s->volumetric_mean_radius = s->estimate_radius();
        s->temperature = s->estimate_temperature();
        s->origname = s->name;

        append_cel(s);
        num_read++;
    }

    return num_read;
}

int alienorum::CatalogReader::read_Uranometria_catalog(CelestialObject **cels, int max)
{
    std::string catpath = "catalogs" _FILESLASH "Uranometria" _FILESLASH "catalog.dat";
    char buffer[1024];
    char field[32];
    int HD, Gould, num_read = 0;
    Star *s;

    FILE* fp = fopen(catpath.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = catpath + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(catpath.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    while (fgets(buffer, 1020, fp))
    {
        //  66- 71  I6   ---     HD      ? Star number in the Henry Draper (HD) catalogue
        read_field_onebased(buffer, 66, 71, field);
        HD = atoi(field);

        if (!hdcache[HD]) continue;
        s = hdcache[HD];
        if (s == cels[0]) continue;

        //   3-  5  I3   ---     G       [1/393]? Number in printed Uranometria Argentina (blank if not in Gould Uranometria)
        read_field_onebased(buffer, 3, 5, field);
        Gould = atoi(field);
        if (!Gould) continue;

        s->GouldNo = Gould;

        //   7-  9  A3   ---     cst     Three letter abbreviation of the constellation name under which the star appears in the
        read_field_onebased(buffer, 7, 9, field);
        if (strlen(trim(field).c_str()))
        {
            strcpy(s->Gouldcons, field);
            if (!strlen(s->constellation)) strcpy(s->constellation, field);
        }
    }

    return num_read;
}

namespace
{
    std::string wd_key(std::string text)
    {
        const std::string wd = wd_identifier_key(text);
        if (!wd.empty()) return wd;
        std::string key;
        for (unsigned char c : text)
        {
            if (c > ' ')
            {
                key += static_cast<char>(std::toupper(c));
            }
        }
        if (key.compare(0, 6, "GLIESE") == 0)
        {
            key.replace(0, 6, "GJ");
        }
        else if (key.compare(0, 2, "GL") == 0)
        {
            key.replace(0, 2, "GJ");
        }
        if (key.compare(0, 2, "GJ") == 0 || key.compare(0, 2, "HD") == 0 ||
            key.compare(0, 3, "HIP") == 0)
        {
            const size_t first = key.compare(0, 3, "HIP") == 0 ? 3 : 2;
            while (first + 1 < key.size() && key[first] == '0' &&
                key[first + 1] >= '0' && key[first + 1] <= '9')
            {
                key.erase(first, 1);
            }
        }
        return key;
    }

    std::string wd_field(const std::string &line, size_t first, size_t last)
    {
        return first <= line.size() ? trim(line.substr(first - 1, last - first + 1)) : "";
    }

    double wd_number(const std::string &line, size_t first, size_t last)
    {
        const std::string text = wd_field(line, first, last);
        char *end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        return end != text.c_str() && *end == '\0' && std::isfinite(value) ? value : NAN;
    }

    struct WdMeasurement
    {
        double value = NAN;
        double quality = -1;

        void offer(double candidate, double rank)
        {
            // Equal-quality measurements use a stable numeric tie-break, independent of row order.
            if (std::isfinite(candidate) && (rank > quality ||
                (rank == quality && (!std::isfinite(value) || candidate < value))))
            {
                value = candidate;
                quality = rank;
            }
        }
    };

    struct WdRecord
    {
        std::string id, spectral;
        std::vector<std::string> aliases;
        WdMeasurement visual, absolute, bv, ub, temperature, parallax, velocity, motion, angle;
        double ra = NAN, dec = NAN;
    };

    struct WdGaiaRow
    {
        std::string name;
        uint64_t source = 0;
        double ra = NAN, dec = NAN, parallax = NAN, error = NAN;
        double pmra = NAN, pmdec = NAN, g = NAN, bp = NAN, rp = NAN;
    };

    bool wd_j2000_coordinates(std::string name, double &ra, double &dec)
    {
        if (name.compare(0, 3, "WDJ") == 0)
        {
            name.erase(0, 3);
        }
        else if (!name.empty() && name[0] == 'J')
        {
            name.erase(0, 1);
        }
        const size_t sign = name.find_first_of("+-", 4);
        if (sign == std::string::npos || sign < 6 || name.size() - sign < 7 ||
            name.find_first_not_of("0123456789.+-") != std::string::npos)
        {
            return false;
        }
        try
        {
            const auto seconds = [](const std::string &text)
            {
                const double value = std::stod(text);
                return text.find('.') == std::string::npos ? value / pow(10, static_cast<int>(text.size()) - 2) : value;
            };
            const int h = std::stoi(name.substr(0, 2)), m = std::stoi(name.substr(2, 2));
            const int d = std::stoi(name.substr(sign + 1, 2)), dm = std::stoi(name.substr(sign + 3, 2));
            const double sec = seconds(name.substr(4, sign - 4)), ds = seconds(name.substr(sign + 5));
            if (h >= 24 || m >= 60 || sec >= 60 || d > 90 || dm >= 60 || ds >= 60)
            {
                return false;
            }
            ra = (h + m / 60.0 + sec / 3600) * 15 * fiftyseventh;
            dec = (d + dm / 60.0 + ds / 3600) * fiftyseventh * (name[sign] == '-' ? -1 : 1);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    using WdGaiaIndex = std::unordered_map<int, std::vector<WdGaiaRow>>;

    int wd_gaia_cell(double ra, double dec)
    {
        const int x = static_cast<int>(floor(ra * fiftyseven)) % 360;
        const int y = std::clamp(static_cast<int>(floor(dec * fiftyseven + 90)), 0, 179);
        return y * 360 + (x + 360) % 360;
    }

    WdGaiaIndex wd_read_gaia(const std::string &path)
    {
        std::ifstream input(path);
        std::string line;
        WdGaiaIndex index;
        bool header = false;
        while (std::getline(input, line))
        {
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            std::vector<std::string> fields;
            std::istringstream row(line);
            std::string field;
            while (std::getline(row, field, '\t'))
            {
                fields.push_back(trim(field));
            }
            if (fields.size() != 9)
            {
                continue;
            }
            if (fields[0] == "WDJname")
            {
                header = fields[1] == "GaiaEDR3" && fields[2] == "Plx" && fields[3] == "e_Plx" &&
                    fields[4] == "pmRA" && fields[5] == "pmDE" && fields[6] == "Gmag" &&
                    fields[7] == "BPmag" && fields[8] == "RPmag";
                continue;
            }
            WdGaiaRow r;
            if (!header || fields[0].compare(0, 3, "WDJ") != 0 ||
                !wd_j2000_coordinates(fields[0], r.ra, r.dec))
            {
                continue;
            }
            const auto number = [&](size_t column)
            {
                return wd_number(fields[column], 1, fields[column].size());
            };
            char *end = nullptr;
            r.source = std::strtoull(fields[1].c_str(), &end, 10);
            r.parallax = number(2);
            r.error = number(3);
            if (!r.source || !end || *end || !(r.parallax > 0))
            {
                continue;
            }
            r.name = fields[0];
            r.pmra = number(4);
            r.pmdec = number(5);
            r.g = number(6);
            r.bp = number(7);
            r.rp = number(8);
            index[wd_gaia_cell(r.ra, r.dec)].push_back(std::move(r));
        }
        return index;
    }

    WdGaiaIndex wd_load_gaia(const std::string &root, bool fetch)
    {
        const std::string path = root + "gaia_edr3.tsv";
        if (file_exists(path.c_str()))
        {
            return wd_read_gaia(path);
        }
        if (!fetch || radio_silence || abort_load)
        {
            return WdGaiaIndex();
        }
        // Compact projection of the complete Gaia WD candidate catalog; match only WD source
        // records, rather than adding the candidate population itself. Positions in WDJname
        // are already at epoch J2000. See CDS J/MNRAS/508/3877, note G1.
        const char *url = "https://vizier.cds.unistra.fr/viz-bin/asu-tsv?"
            "-source=J%2FMNRAS%2F508%2F3877%2Fmaincat&"
            "-out=WDJname,GaiaEDR3,Plx,e_Plx,pmRA,pmDE,Gmag,BPmag,RPmag&"
            "-out.max=unlimited&Plx=%3E0";
        const std::string temporary = path + ".tmp";
        FILE *output = fopen(temporary.c_str(), "wb");
        CURL *curl = curl_easy_init();
        if (!output || !curl)
        {
            if (output)
            {
                fclose(output);
            }
            if (curl)
            {
                curl_easy_cleanup(curl);
            }
            return WdGaiaIndex();
        }
        std::cout << "Downloading Gaia white dwarf astrometry..." << std::endl;
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, output);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char *data, size_t size, size_t count, void *context) -> size_t
        {
            return fwrite(data, size, count, static_cast<FILE*>(context)) * size;
        });
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, +[](void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int
        {
            return abort_load ? 1 : 0;
        });
        const CURLcode result = curl_easy_perform(curl);
        const bool written = fclose(output) == 0;
        curl_easy_cleanup(curl);
        WdGaiaIndex index;
        if (result == CURLE_OK && written)
        {
            index = wd_read_gaia(temporary);
        }
        std::error_code error;
        if (index.empty())
        {
            std::filesystem::remove(temporary, error);
            std::cerr << "Gaia WD download unavailable; retaining the original WD measurements." << std::endl;
        }
        else
        {
            std::filesystem::rename(temporary, path, error);
        }
        return index;
    }

    const WdGaiaRow *wd_gaia_match(const WdGaiaIndex &index, const WdRecord &record, double ra, double dec)
    {
        double radius = 30.0 / 3600 * fiftyseventh;
        for (const std::string &alias : record.aliases)
        {
            if (wd_j2000_coordinates(alias, ra, dec))
            {
                radius = 3.0 / 3600 * fiftyseventh;
                break;
            }
        }
        if (!std::isfinite(ra) || !std::isfinite(dec))
        {
            return nullptr;
        }
        const int cell = wd_gaia_cell(ra, dec), y = cell / 360, x = cell % 360;
        const int width = std::min(180, static_cast<int>(ceil(1 / fmax(0.0001, cos(dec)))));
        const WdGaiaRow *match = nullptr;
        for (int dy = -1; dy <= 1; dy++)
        {
            if (y + dy < 0 || y + dy > 179)
            {
                continue;
            }
            for (int dx = -width; dx <= width; dx++)
            {
                const auto found = index.find((y + dy) * 360 + (x + dx + 360) % 360);
                if (found == index.end())
                {
                    continue;
                }
                for (const WdGaiaRow &candidate : found->second)
                {
                    const double a = sin((candidate.dec - dec) / 2);
                    const double b = sin((candidate.ra - ra) / 2);
                    const double separation = 2 * asin(sqrt(std::clamp(a * a + cos(dec) * cos(candidate.dec) * b * b, 0.0, 1.0)));
                    if (separation <= radius)
                    {
                        if (match && match->source != candidate.source)
                        {
                            return nullptr;
                        }
                        match = &candidate;
                    }
                }
            }
        }
        return match;
    }

    void wd_source_coordinates(const WdRecord &r, double &ra, double &dec, double &pmra, double &pmdec)
    {
        if (std::isfinite(r.ra))
        {
            const double pm = std::isfinite(r.motion.value) ? r.motion.value * fiftyseventh / 3600 : 0;
            const double angle = r.angle.value * fiftyseventh;
            const double dra = pm ? pm * sin(angle) / fmax(1e-6, cos(r.dec)) : 0;
            const double ddec = pm ? pm * cos(angle) : 0;
            // Source positions are at B1950; propagate motion before precessing the frame.
            const double years = (J2000 - 2433282.42345905) * oneday / oneyear;
            convert_to_J2000(r.ra + dra * years, r.dec + ddec * years, 1950, ra, dec, true);
            double next_ra, next_dec;
            convert_to_J2000(r.ra + dra * (years + 1), r.dec + ddec * (years + 1), 1950, next_ra, next_dec, true);
            pmra = std::remainder(next_ra - ra, 2 * _pi) / oneyear;
            pmdec = (next_dec - dec) / oneyear;
        }
    }

    bool wd_observed_magnitude(double value)
    {
        return std::isfinite(value) && value >= -40 && value < 99;
    }

    bool wd_candidate(const Star *s)
    {
        return s->is_white_dwarf() ||
            (!s->spectral_type[0] && s->volumetric_mean_radius > 0 &&
             s->volumetric_mean_radius < 0.05 * solar_radius);
    }
}

int alienorum::CatalogReader::read_WD_catalog(CelestialObject **objects, int max, bool fetch_gaia)
{
    const std::string root = "catalogs" _FILESLASH "WD" _FILESLASH;
    const char *files[] =
    {
        "catalog.dat", "names.dat"
    };
    for (const char *name : files)
    {
        const std::string path = root + name;
        if (!file_exists(path.c_str()) && file_exists((path + ".gz").c_str()))
        {
            extract_archive((path + ".gz").c_str());
        }
    }
    std::ifstream catalog(root + "catalog.dat");
    std::ifstream names(root + "names.dat");
    if (!catalog || !names || !objects || max < 2)
    {
        return 0;
    }
    std::map<std::string, WdRecord> records;
    std::string line;
    while (std::getline(catalog, line))
    {
        const std::string number = wd_field(line, 2, 11);
        const std::string record_key = wd_identifier_key("WD" + number);
        if (record_key.empty())
        {
            continue;
        }
        const std::string id = "WD" + number;
        WdRecord &r = records[record_key];
        r.id = id;
        const std::string spectral = wd_field(line, 31, 40);
        if (!spectral.empty() && (r.spectral.empty() || spectral < r.spectral))
        {
            r.spectral = spectral;
        }
        const double h = wd_number(line, 13, 14), m = wd_number(line, 16, 17);
        const double sec = wd_number(line, 19, 20), d = wd_number(line, 23, 24);
        const double dm = wd_number(line, 26, 29);
        if (h >= 0 && h < 24 && m >= 0 && m < 60 && sec >= 0 && sec <= 60 &&
            d >= 0 && d <= 90 && dm >= 0 && dm < 60)
        {
            const double ra = (h + m / 60 + sec / 3600) * 15 * fiftyseventh;
            const double dec = (d + dm / 60) * fiftyseventh * (wd_field(line, 22, 22) == "-" ? -1 : 1);
            if (!std::isfinite(r.ra) || ra < r.ra || (ra == r.ra && dec < r.dec))
            {
                r.ra = ra;
                r.dec = dec;
            }
        }
        const std::string band = wd_field(line, 80, 81);
        const double quality = wd_field(line, 79, 79).empty() ? 2 : 1;
        if (band.empty() || band == "v")
        {
            const double v = wd_number(line, 73, 78);
            if (wd_observed_magnitude(v))
            {
                r.visual.offer(v, quality);
            }
        }
        r.visual.offer(wd_number(line, 104, 108), 0);
        const double bv = wd_number(line, 82, 87), ub = wd_number(line, 92, 97);
        if (bv >= -0.7 && bv <= 2)
        {
            r.bv.offer(bv, wd_field(line, 88, 88).empty() ? 2 : 1);
        }
        if (ub >= -2.5 && ub <= 1.4)
        {
            r.ub.offer(ub, wd_field(line, 98, 98).empty() ? 2 : 1);
        }
        const double absolute = wd_number(line, 146, 150);
        if (spectral.compare(0, 2, "DO") == 0)
        {
            r.temperature.offer(absolute * 1000, 0);
        }
        else if (wd_field(line, 151, 151) != "B" && absolute >= -5 && absolute <= 30)
        {
            r.absolute.offer(absolute, wd_field(line, 151, 151).empty() ? 2 : 1);
        }
        const double temperature = wd_number(line, 153, 155) * 1000;
        if (temperature > 0 && temperature <= 200000)
        {
            r.temperature.offer(temperature, 2);
        }
        const double plx = wd_number(line, 200, 206), error = wd_number(line, 207, 210);
        if (plx > 0)
        {
            r.parallax.offer(plx, error > 0 ? 1 + plx * 1000 / error : 0);
        }
        r.velocity.offer(wd_number(line, 180, 186) * 1000, 1);
        const double motion = wd_number(line, 162, 167), angle = wd_number(line, 170, 174);
        if (motion >= 0 && (std::isfinite(angle) || motion == 0) && wd_field(line, 161, 161).empty())
        {
            const double rank = wd_field(line, 168, 168).empty() ? 2 : 1;
            // Keep the vector components from the same observation.
            if (rank > r.motion.quality || (rank == r.motion.quality && motion < r.motion.value))
            {
                r.motion.value = motion;
                r.motion.quality = rank;
                r.angle.value = std::isfinite(angle) ? angle : 0;
            }
        }
    }
    while (std::getline(names, line))
    {
        const auto found = records.find(wd_identifier_key("WD" + wd_field(line, 17, 26)));
        if (found != records.end())
        {
            found->second.aliases.push_back(wd_field(line, 1, 14));
        }
    }
    const WdGaiaIndex gaia_index = wd_load_gaia(root, fetch_gaia);
    std::unordered_map<uint64_t, std::vector<std::string>> gaia_records;
    for (const auto &entry : records)
    {
        double ra = NAN, dec = NAN, pmra = 0, pmdec = 0;
        wd_source_coordinates(entry.second, ra, dec, pmra, pmdec);
        const WdGaiaRow *match = wd_gaia_match(gaia_index, entry.second, ra, dec);
        if (match) gaia_records[match->source].push_back(entry.first);
    }
    // Published color targets can be absent from McCook-Sion. Their pinned Gaia
    // measurements also allow catalog generation without a Gaia download.
    std::map<std::string, WdGaiaRow> color_astrometry;
    std::ifstream colors("catalogs/WD_colors.dat");
    while (std::getline(colors, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string label, name, hex, method, token;
        if (!(row >> label >> name >> hex >> method)) continue;
        WdGaiaRow gaia;
        gaia.name = name;
        double distance_pc = NAN, visual = NAN, temperature = NAN;
        std::string spectral;
        std::vector<std::string> aliases;
        bool alternative = false, component = false;
        while (row >> token)
        {
            const size_t equal = token.find('=');
            if (equal == std::string::npos)
            {
                if (token == "alternative") alternative = true;
                else aliases.push_back(token);
                continue;
            }
            const std::string key = token.substr(0, equal), value = token.substr(equal + 1);
            const double number = wd_number(value, 1, value.size());
            if (key == "gaia_dr3")
            {
                char *end = nullptr;
                gaia.source = std::strtoull(value.c_str(), &end, 10);
                if (!end || *end) gaia.source = 0;
            }
            else if (key == "distance_pc") distance_pc = number;
            else if (key == "gaia_g") gaia.g = number;
            else if (key == "gaia_bp") gaia.bp = number;
            else if (key == "gaia_rp") gaia.rp = number;
            else if (key == "pmra_masyr") gaia.pmra = number;
            else if (key == "pmdec_masyr") gaia.pmdec = number;
            else if (key == "visual_mag") visual = number;
            else if (key == "temperature_k") temperature = number;
            else if (key == "spectral") spectral = value;
            else if (key == "unresolved_component") component = true;
        }
        if (alternative || component || !gaia.source || !(distance_pc > 0) ||
            !std::isfinite(distance_pc) || !wd_j2000_coordinates(name, gaia.ra, gaia.dec)) continue;
        gaia.parallax = 1000 / distance_pc;
        std::string key = wd_key(name);
        for (const auto &alias : aliases)
        {
            const auto found = records.find(wd_key(alias));
            if (found != records.end())
            {
                key = found->first;
                break;
            }
        }
        // A published target can already have a legacy record without a shared
        // printed alias. Reuse a unique Gaia identity before adding a new record.
        const auto original = gaia_records.find(gaia.source);
        if (records.find(key) == records.end() && original != gaia_records.end() && original->second.size() == 1)
        {
            key = original->second.front();
        }
        WdRecord &record = records[key];
        if (record.id.empty()) record.id = name;
        record.aliases.insert(record.aliases.end(), aliases.begin(), aliases.end());
        record.aliases.push_back(label);
        record.aliases.push_back(name);
        if (record.spectral.empty()) record.spectral = spectral;
        if (wd_observed_magnitude(visual)) record.visual.offer(visual, 1);
        if (temperature > 0) record.temperature.offer(temperature, 1);
        color_astrometry[record.id] = gaia;
    }
    std::map<std::string, const WdGaiaRow*> gaia_matches;
    std::unordered_map<uint64_t, int> gaia_uses;
    std::unordered_map<uint64_t, std::string> color_owners;
    for (const auto &entry : color_astrometry) color_owners[entry.second.source] = entry.first;
    for (const auto &entry : records)
    {
        const WdRecord &r = entry.second;
        double ra = NAN, dec = NAN, pmra = 0, pmdec = 0;
        wd_source_coordinates(r, ra, dec, pmra, pmdec);
        const auto pinned = color_astrometry.find(r.id);
        const WdGaiaRow *match = pinned != color_astrometry.end() ?
            &pinned->second : wd_gaia_match(gaia_index, r, ra, dec);
        if (match)
        {
            const auto owner = color_owners.find(match->source);
            // An explicit published identity takes priority over ambiguous
            // positional associations with other legacy records.
            if (owner != color_owners.end() && owner->second != r.id) match = nullptr;
        }
        if (match)
        {
            gaia_matches[r.id] = match;
            gaia_uses[match->source]++;
        }
    }
    int unique_gaia = 0;
    for (const auto &entry : gaia_uses)
    {
        if (entry.second == 1)
        {
            unique_gaia++;
        }
    }
    std::cout << "Gaia WD cross-match: " << unique_gaia << " unique matches." << std::endl;

    std::unordered_map<std::string, std::vector<Star*>> identities;
    std::vector<Star*> existing_wd;
    int count = 0;
    const auto index_star = [&](Star *s)
    {
        const auto add = [&](const std::string &alias)
        {
            if (!alias.empty())
            {
                auto &matches = identities[wd_key(alias)];
                if (std::find(matches.begin(), matches.end(), s) == matches.end())
                {
                    matches.push_back(s);
                }
            }
        };
        add(s->name);
        add(s->WD);
        add(s->Gliese);
        add(s->gaia_wd_name);
        if (s->HD)
        {
            add("HD" + std::to_string(s->HD));
        }
        if (s->HIP)
        {
            add("HIP" + std::to_string(s->HIP));
        }
        add(bonn_survey_key(s->Bonn_survey, s->Bonn_survey_declination, s->Bonn_survey_sequential));
        if (s->Bayer[0] && s->constellation[0])
        {
            add(std::string(s->Bayer) + s->constellation);
        }
        if (s->multisys && s->get_component() > 'A')
        {
            Star *a = s->multisys->get_member('A');
            if (a && a->HD)
            {
                add("HD" + std::to_string(a->HD) + s->get_component());
            }
            if (a && a->Bayer[0] && a->constellation[0])
            {
                add(std::string(a->Bayer) + a->constellation + s->get_component());
            }
            if (a && a->FlamsteedNo && a->constellation[0])
            {
                add(std::to_string(a->FlamsteedNo) + a->constellation + s->get_component());
            }
        }
    };
    for (; count < max && objects[count]; count++)
    {
        if (objects[count]->typeclass() == class_star)
        {
            Star *s = static_cast<Star*>(objects[count]);
            index_star(s);
            if (wd_candidate(s))
            {
                existing_wd.push_back(s);
            }
        }
    }
    if (count == max)
    {
        return 0;
    }
    int added = 0, updated = 0, deferred = 0, conflicts = 0;
    for (auto &entry : records)
    {
        WdRecord &r = entry.second;
        double ra = NAN, dec = NAN, pmra = 0, pmdec = 0;
        wd_source_coordinates(r, ra, dec, pmra, pmdec);
        const auto gaia_entry = gaia_matches.find(r.id);
        const WdGaiaRow *gaia = gaia_entry == gaia_matches.end() ? nullptr : gaia_entry->second;
        if (gaia && gaia_uses[gaia->source] != 1)
        {
            gaia = nullptr;
        }
        bool visual_estimated = false;
        if (gaia)
        {
            ra = gaia->ra;
            dec = gaia->dec;
            if (std::isfinite(gaia->pmra) && std::isfinite(gaia->pmdec))
            {
                pmra = gaia->pmra / (1000 * 3600) * fiftyseventh / oneyear / fmax(1e-6, cos(dec));
                pmdec = gaia->pmdec / (1000 * 3600) * fiftyseventh / oneyear;
            }
            const double quality = gaia->error > 0 ? gaia->parallax / gaia->error : 0;
            r.parallax.offer(gaia->parallax / 1000, 1 + quality);
            const double color = gaia->bp - gaia->rp;
            if (!wd_observed_magnitude(r.visual.value) && wd_observed_magnitude(gaia->g) && color > -0.5 && color < 5)
            {
                // ESA EDR3 documentation, table 5.7: G-V as a function of BP-RP.
                // This is an estimated V magnitude, never a measured V or an unlabelled G.
                const double g_minus_v = -0.02704 + 0.01424 * color - 0.2156 * color * color + 0.01426 * color * color * color;
                r.visual.offer(gaia->g - g_minus_v, 0);
                visual_estimated = true;
            }
            if (!wd_observed_magnitude(r.visual.value) && wd_observed_magnitude(gaia->g))
            {
                r.visual.offer(gaia->g, 0);
                visual_estimated = true;
            }
        }
        const auto nearby = [&](Star *s)
        {
            if (!std::isfinite(ra))
            {
                return true;
            }
            const double delta_ra = std::remainder(s->right_ascension - ra, 2 * _pi) * cos(dec);
            return hypot(delta_ra, s->declination - dec) < 10.0 / 60 * fiftyseventh;
        };
        std::vector<Star*> matches;
        r.aliases.push_back(r.id);
        for (const std::string &alias : r.aliases)
        {
            const auto found = identities.find(wd_key(alias));
            if (found == identities.end())
            {
                continue;
            }
            for (Star *s : found->second)
            {
                // A system identifier can denote the luminous primary rather than the WD.
                const bool same_wd = wd_key(s->WD) == wd_key(r.id) || wd_key(s->name) == wd_key(r.id);
                if (wd_candidate(s) && (same_wd || nearby(s)) &&
                    (!gaia || !s->gaia_source_id || s->gaia_source_id == gaia->source) &&
                    (s->WD.empty() || wd_key(s->WD) == wd_key(r.id)) &&
                    std::find(matches.begin(), matches.end(), s) == matches.end())
                {
                    matches.push_back(s);
                }
            }
        }
        if (matches.size() > 1)
        {
            conflicts++;
            std::cout << "WD identity conflict: " << r.id << std::endl;
            continue;
        }
        Star *s = matches.empty() ? nullptr : matches[0];
        if (!s && std::isfinite(ra))
        {
            // Require both positional and visual-photometric agreement for a coordinate match.
            // Keep ambiguous neighbors out of the catalog rather than creating duplicates.
            bool close = false;
            const bool color_target = color_astrometry.find(r.id) != color_astrometry.end();
            for (Star *other : existing_wd)
            {
                const bool coordinate_match = color_target ?
                    hypot(std::remainder(other->right_ascension - ra, 2 * _pi) * cos(dec),
                        other->declination - dec) < 3.0 / 3600 * fiftyseventh : nearby(other);
                if (other->WD.empty() && coordinate_match &&
                    (!gaia || !other->gaia_source_id || other->gaia_source_id == gaia->source))
                {
                    close = true;
                    if (wd_observed_magnitude(other->apparent_magnitude) &&
                        wd_observed_magnitude(r.visual.value) &&
                        fabs(other->apparent_magnitude - r.visual.value) <= 0.75)
                    {
                        matches.push_back(other);
                    }
                }
            }
            if (matches.size() == 1)
            {
                s = matches[0];
            }
            else if (close)
            {
                conflicts++;
                std::cout << "WD unresolved neighbor: " << r.id << std::endl;
                continue;
            }
        }
        double distance = s && s->distance > 0 ? s->distance : NAN;
        if (!std::isfinite(distance) && r.parallax.value > 0)
        {
            distance = parsec / r.parallax.value;
        }
        if (!std::isfinite(distance) && wd_observed_magnitude(r.visual.value) && wd_observed_magnitude(r.absolute.value))
        {
            distance = parsec * 10 * pow(10, (r.visual.value - r.absolute.value) / 5);
        }
        if (!s && (!std::isfinite(ra) || !(distance > 0) ||
            (!wd_observed_magnitude(r.visual.value) && !wd_observed_magnitude(r.absolute.value)) || count >= max - 1))
        {
            deferred++;
            continue;
        }
        const bool fresh = !s;
        if (fresh)
        {
            s = new Star();
            snprintf(s->name, sizeof(s->name), "%s", r.id.c_str());
            s->has_custom_name = true;
            s->right_ascension = ra;
            s->declination = dec;
            s->proper_motion_RA = pmra;
            s->proper_motion_decl = pmdec;
            s->epoch = J2000;
            s->mass = 0.6 * solar_mass; // Explicit WD fallback, rather than a main-sequence estimate.
            added++;
        }
        else
        {
            updated++;
        }
        // The fixed-width WD field is reserved for legacy B1950 identifiers.
        // Full J2000 identities travel in the Gaia metadata instead.
        s->WD = r.id.compare(0, 3, "WDJ") == 0 ? "" : r.id;
        for (const auto &alias : r.aliases)
        {
            const std::string key = stellar_alias_key(alias);
            if (key.empty() || key.find('=') != std::string::npos ||
                key.rfind("GAIA", 0) == 0 || key.rfind("SDSS", 0) == 0 || key.rfind("2MASS", 0) == 0) continue;
            const auto canonical = records.find(wd_identifier_key(alias));
            if (canonical != records.end() && canonical->first != wd_identifier_key(r.id)) continue;
            s->add_catalog_alias(alias);
        }
        if (gaia)
        {
            s->gaia_wd_name = gaia->name;
            s->gaia_source_id = gaia->source;
            s->gaia_g_magnitude = gaia->g;
        }
        if (fresh || !s->spectral_type[0])
        {
            snprintf(s->spectral_type, sizeof(s->spectral_type), "%s", r.spectral.empty() ? "D" : r.spectral.c_str());
        }
        if ((fresh || !(s->distance > 0)) && std::isfinite(distance))
        {
            s->distance = distance;
            s->distance_known = r.parallax.value > 0;
            s->parallax = r.parallax.value > 0 ? r.parallax.value / 3600 * fiftyseventh : 0;
        }
        if (!wd_observed_magnitude(s->apparent_magnitude) && wd_observed_magnitude(r.visual.value))
        {
            s->apparent_magnitude = r.visual.value;
            s->apparent_magnitude_estimated = visual_estimated;
        }
        if ((fresh || !std::isfinite(s->absolute_magnitude) || s->absolute_magnitude >= 99) &&
            (wd_observed_magnitude(s->apparent_magnitude) || wd_observed_magnitude(r.absolute.value)))
        {
            s->absolute_magnitude = wd_observed_magnitude(s->apparent_magnitude) ?
                s->apparent_magnitude - 5 * log10(s->distance / (10 * parsec)) : r.absolute.value;
        }
        if (fresh && !wd_observed_magnitude(s->apparent_magnitude))
        {
            s->apparent_magnitude = s->absolute_magnitude + 5 * log10(s->distance / (10 * parsec));
            s->apparent_magnitude_estimated = true;
        }
        if (fresh)
        {
            if (std::isfinite(r.bv.value))
            {
                s->BV_color = r.bv.value;
            }
            if (std::isfinite(r.ub.value))
            {
                s->UB_color = r.ub.value;
            }
            s->radial_velocity = std::isfinite(r.velocity.value) ? r.velocity.value : 0;
        }
        if (!(s->temperature > 0))
        {
            s->temperature = r.temperature.value > 0 ? r.temperature.value :
                (std::isfinite(r.bv.value) ? Star::temperature_from_BV(r.bv.value) : 10000);
            s->wd_temperature_source = r.temperature.value > 0 ? "catalog" :
                (std::isfinite(r.bv.value) ? "BV" : "assumed");
        }
        if (fresh && !std::isfinite(r.bv.value))
        {
            s->estimate_BV(s->temperature);
        }
        if (fresh && !std::isfinite(r.ub.value))
        {
            s->estimate_UB(s->temperature);
        }
        if (fresh || !(s->volumetric_mean_radius > 0))
        {
            const double bolometric = s->absolute_magnitude + Star::bolometric_correction(s->temperature);
            const double luminosity = pow(magnbase, 4.74 - bolometric);
            const double radius = sqrt(luminosity) * pow(sun_temp / s->temperature, 2) * solar_radius;
            s->volumetric_mean_radius = std::isfinite(radius) && radius > 0.001 * solar_radius && radius < 0.1 * solar_radius ?
                radius : 0.012 * solar_radius;
        }
        if (fresh)
        {
            append_cel(s);
            count++;
            existing_wd.push_back(s);
        }
        s->update_location(simnow);
        index_star(s);
    }
    std::cout << "WD import: " << records.size() << " objects, " << added << " added, " << updated
              << " matched, " << deferred << " deferred, " << conflicts << " conflicts." << std::endl;
    return added + updated;
}

int alienorum::CatalogReader::read_cons_boundaries()
{
    std::string path = "catalogs" _FILESLASH "CBD" _FILESLASH "bound_20.dat";
    char buffer[1024];
    char field[32];
    int num_read = 0;
    int i, n = constellations.size();

    FILE* fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    while (fgets(buffer, 1020, fp))
    {
        if (abort_load) { fclose(fp); return 0; }
        ConsBoundary cb;

        //  1- 11   F11.7   deg     RAdeg   [0/360] Right ascension in degrees (J2000)
        read_field_onebased(buffer, 1, 11, field);
        cb.RA = atof(field) * fiftyseventh;

        // 13- 23   F11.7   deg     DEdeg   [-86/89] Declination in degrees (J2000)
        read_field_onebased(buffer, 13, 23, field);
        cb.decl= atof(field) * fiftyseventh;

        // 25- 28   A4      ---     cst     Constellation abbreviation
        read_field_onebased(buffer, 25, 28, field);
        for (i=0; i<n; i++)
        {
            if (!strcasecmp(constellations[i].abbrev.c_str(), trim(field).c_str()))
            {
                constellations[i].bounds.push_back(cb);
                num_read++;
                break;
            }
        }
    }
    fclose(fp);

    for (i=0; i<n; i++)
    {
        if (abort_load) return 0;
        constellations[i].build_constellation_perimeter();
    }

    return num_read;
}

int CatalogReader::read_CCDM_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs" _FILESLASH "CCDM" _FILESLASH "ccdm.dat";
    char buffer[1024];
    char field[32];
    int num_read = 0;
    int offset;
    uint32_t HD, HIP;
    Star *s, *A = nullptr;
    std::string CCDM, CCDM_A;

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    FILE* fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    while (fgets(buffer, 1020, fp))
    {
        //   2- 11  A10    ---     CCDM     (Catalogue of the Components of the Double and Multiple stars) identifier (1)
        read_field_onebased(buffer, 2, 11, field);
        CCDM = trim(field);
        if (CCDM != CCDM_A) A = nullptr;

        //  99-104  A6     ---     HD       HD identifier
        read_field_onebased(buffer, 99, 104, field);
        HD = atoi(field);

        // 127-132  I6     ---     HIC      ? Hipparcos Input Catalogue (Turon et al., Cat. <I/196>) identifier (also HIP <I/239>)
        read_field_onebased(buffer, 127, 132, field);
        HIP = atoi(field);

        if (HIP==15371 || HIP==15330) continue;

        char refcomp = buffer[11], conccomp = buffer[12];
        if (refcomp == ' ') refcomp = 'A';

        s = nullptr;

        if (conccomp == 'A')
        {
            CCDM_A = CCDM;
            if (HIP && hipcache && hipcache[HIP]) A = hipcache[HIP];
            else if (HD && hdcache && hdcache[HD]) A = hdcache[HD];
        }
        else
        {
            if (HIP && hipcache && hipcache[HIP]) s = hipcache[HIP];
            else if (HD && hdcache && hdcache[HD]) s = hdcache[HD];
        }

        if (!A) continue;
        if (!A->multisys) A->set_component('A', A);
        A = A->multisys->get_member(refcomp);
        if (!A) continue;
        s = A->multisys->get_member(conccomp);

        if (!s)
        {
#if _ALLOW_CCDM_ADDITIONS
            s = new Star();
            s->epoch = J2000 + (1991.25 - 2000);

            append_cel(s);
            offset++;
#else
            continue;
#endif
        }
        if (s != A) s->make_companion_of(A, conccomp);

        if (A->HD == 20766)                            // Zeta 1 Reticuli orbits Zeta 2, not the other way around.
        {
            A->orbit = s->orbit;
            s->orbit = nullptr;
            A->orbit->center = s;
            Star *swap = A;
            A = s;
            s = swap;
        }

        // HD225034 fix
        if (!s->orbit && A->orbit && A->orbit->center == s)
        {
            Star *swap = A;
            A = s;
            s = swap;

            if (!A->has_custom_name)
            {
                strcpy(A->name, lop_component(A->name).c_str());
                A->namelen = strlen(A->name);
            }
            if (!s->has_custom_name)
            {
                s->assign_identifier_name();
                if (!trim(s->name).size() || !strcmp(trim(s->name).c_str(), A->name))
                    strcpy(s->name, (std::string(A->name) + std::string(" B")).c_str() );
                s->namelen = strlen(s->name);
            }
        }

        if (buffer[12] != s->name[strlen(s->name)-1]) continue;

        //  47- 49  A3     deg     theta    Position angle (degrees) (4)
        read_field_onebased(buffer, 47, 49, field);
        double theta = atof(field);
        if (!theta && *field >= 'A')
        {
            if (!strcmp(field, "N  ")) theta = 0;
            if (!strcmp(field, "NF ")) theta = 45;
            if (!strcmp(field, "F  ")) theta = 90;
            if (!strcmp(field, "SF ")) theta = 135;
            if (!strcmp(field, "S  ")) theta = 180;
            if (!strcmp(field, "SP ")) theta = 225;
            if (!strcmp(field, "P  ")) theta = 270;
            if (!strcmp(field, "NP ")) theta = 315;
        }
        theta *= fiftyseventh;

        //  50- 55  F6.1   arcsec  rho      ? angular separation of Comp along theta
        read_field_onebased(buffer, 50, 55, field);
        double asep = atof(field);
        double rho = asep;
        if (!rho) continue;
        rho /= (3600 * fiftyseven);

        // Fill in positional parameters based on angular separation
        s->right_ascension = A->right_ascension - rho * sin(theta) / cos(A->declination);
        s->declination = A->declination + rho * cos(theta);

        s->location = A->location;                      // Copies local system reference frame
        s->epoch = J2000;
        s->update_location(J2000_TIME_T);

        // Estimate the semimajor axis
        double sma = asep * A->distance / 206264.806;
        s->orbit->semimajor_axis = sma;

        // Figure the absolute magnitude
        if (s->distance_known)
        {
            double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
            s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
        }
        if (A && (s->absolute_magnitude < A->absolute_magnitude)) s->absolute_magnitude = A->absolute_magnitude + 1;

        // TODO: For systems where both members are not already loaded,
        // can load additional members.

        //  57- 58  I2     ---     Obs      ? for component A: number of components; other component: number of measurements
        //  60- 63  F4.1   mag     Vmag     ? magnitude
        //  65- 66  A2     ---     Sp       Spectral type
        //  68- 72  I5    mas/yr   pmRA     ? annual proper motion in 0"001
        //  73- 77  I5    mas/yr   pmDE     ? annual proper motion in 0"001

        s->gotta_be_named_something();
        num_read++;

        if (A) A = A->multisys->get_member('A');

        mtx.lock();
        loading_msg = std::string("Loaded ") + std::to_string(num_read)
                      + std::string(" objects from Catalogue of the Components of Double and Multiple Stars...");
        mtx.unlock();
    }
    return num_read;
}

int CatalogReader::read_SB9_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs" _FILESLASH "SB9" _FILESLASH "main.dat";
    char buffer[1024];
    char field[32], Bonn, Bonn_sign, cen[5], comp[5];
    int num_read = 0;
    int Bonn_decl, i, n, found, offset;
    uint32_t HD, HIP, SB9, Bonn_seq;
    Star *s, *A, *B;
    double f;

    FILE* fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);
    for (offset=0; cels[offset]; offset++);
    if (offset >= (max-1)) return num_read;

    while (fgets(buffer, 1020, fp))
    {
        //  52- 57  F6.3  mag     mag2    ? Magnitude of component 2
        read_field_onebased(buffer, 52, 57, field);
        if (!trim(field).size()) continue;

        HD = HIP = Bonn = 0;
        read_field_onebased(buffer, 104, 132, field);
        if (field[0] == 'H' && field[1] == 'I' && field[2] == 'P' && field[3] == ' ')
        {
            HIP = atoi(&field[4]);
        }
        else if (field[0] == 'H' && field[1] == 'D' && field[2] == ' ')
        {
            HD = atoi(&field[3]);
        }
        else if (field[0] == 'B' && field[1] == 'D')
        {
            Bonn = 'B';
            Bonn_sign = field[2];
            Bonn_decl = atoi(&field[2]);
            Bonn_seq = atoi(&field[6]);
        }
        else if (field[0] == 'C' && field[1] == 'D')
        {
            Bonn = 'C';
            Bonn_sign = field[2];
            Bonn_decl = atoi(&field[2]);
            Bonn_seq = atoi(&field[6]);
        }
        else if (field[0] == 'C' && field[1] == 'P')
        {
            Bonn = 'P';
            Bonn_sign = field[2];
            Bonn_decl = atoi(&field[2]);
            Bonn_seq = atoi(&field[6]);
        }

        if (!HD && !HIP && !Bonn) continue;

        //  36- 42  A7    ---     Comp    ? Component
        read_field_onebased(buffer, 36, 42, field);
        strcpy(field, trim(field).c_str());
        n = strlen(field);
        if (n < 2)
        {
            strcpy(cen, "A");
            strcpy(comp, "B");
        }
        else
        {
            cen[0] = field[0];
            cen[1] = 0;
            comp[0] = field[n-1];
            comp[1] = 0;
            if (comp[0] > 'a') comp[0] = comp[0] & 0x5f;
            if (!strcmp(field, "Aab")) strcpy(comp, "B");
        }

        found = -1;
        if (HIP && hipcache && hipcache[HIP])
        {
            A = hipcache[HIP];
        }
        else if (HD && hdcache && hdcache[HD])
        {
            A = hdcache[HD];
        }
        else
        {
            for (i=0; cels[i]; i++)
            {
                if (cels[i]->type != star) continue;
                s = (Star*)cels[i];

                n = strlen(s->name);
                if (n > 3 && s->name[n-2] == ' ' && s->name[n-1] != cen[0]) continue;

                if (HIP && (s->HIP == HIP)) found = i;
                else if (HD && (s->HD == HD)) found = i;
                else if (Bonn == 'B' && s->Bonn_survey[0] == 'B' && s->Bonn_survey[1] == 'D'
                         && s->Bonn_survey_sign == Bonn_sign
                         && s->Bonn_survey_declination == Bonn_decl
                         && s->Bonn_survey_sequential == Bonn_seq
                        ) found = i;
                else if (Bonn == 'C' && s->Bonn_survey[0] == 'C' && s->Bonn_survey[1] == 'D'
                         && s->Bonn_survey_sign == Bonn_sign
                         && s->Bonn_survey_declination == Bonn_decl
                         && s->Bonn_survey_sequential == Bonn_seq
                        ) found = i;
                else if (Bonn == 'P' && s->Bonn_survey[0] == 'C' && s->Bonn_survey[1] == 'P'
                         && s->Bonn_survey_sign == Bonn_sign
                         && s->Bonn_survey_declination == Bonn_decl
                         && s->Bonn_survey_sequential == Bonn_seq
                        ) found = i;
                if (found >= 0) break;
            }
            if (found < 0)
            {
                continue;
            }

            A = (Star*)cels[found];
            A->is_orbit_multiple = true;
            A->gotta_be_named_something();
            if (!A->distance_known) continue;
        }

        if (!A) continue;
        if (!A->multisys) A->set_component('A', A);
        A = A->multisys->get_member(cen[0]);
        if (!A) continue;
        if (A && A->BayerGrkno == 7 && !strcmp(A->constellation, "Ori")) B = nullptr;
        else B = A->multisys->get_member(comp[0]);

        if (!B)
        {
            B = new Star();
            B->type = star;
            if (A->HD == 20766)
            {
                std::cerr << "BAD! 1366" << std::endl;
                throw 0xbadc0de;
            }
            B->make_companion_of(A, comp[0]);
            B->absolute_magnitude = B->apparent_magnitude = 1e29;
            B->epoch = A->epoch;
            append_cel(B);
            offset++;
        }

        //   1-  4  I4    ---     Seq     System Number (SB8 number when Seq<=1469)
        read_field_onebased(buffer, 1, 4, field);
        SB9 = atoi(field);

        B->SB9 = SB9;

        //  52- 57  F6.3  mag     mag2    ? Magnitude of component 2
        read_field_onebased(buffer, 52, 57, field);
        if (atof(field)) B->apparent_magnitude = atof(field);
        double intrinsic_brightness = pow(magnbase, -B->apparent_magnitude) * pow(fmax(AU, B->distance) / parsec / 10, 2);
        if (B->absolute_magnitude > 1e28) B->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
        if (A && (B->absolute_magnitude < A->absolute_magnitude)) B->absolute_magnitude = A->absolute_magnitude + 1;

        //  93-102  A10   ---     Sp2     MK Spectral type component 2
        read_field_onebased(buffer, 93, 102, field);
        strcpy(B->spectral_type, trim(field).c_str());
        if (found < 0) B->estimate_BV();

        B->gotta_be_named_something();
        B->estimate_radius();
        B->estimate_mass();
    }
    fclose(fp);

    path = "catalogs" _FILESLASH "SB9" _FILESLASH "orbits.dat";
    fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    while (fgets(buffer, 1020, fp))
    {
        //   1-  4  I4    ---     Seq     System Number, as in "main.dat"
        read_field_onebased(buffer, 1, 4, field);
        SB9 = atoi(field);

        found = -1;
        for (i=0; cels[i]; i++)
        {
            if (cels[i]->type != star) continue;
            if (!cels[i]->orbit) continue;
            s = (Star*)cels[i];
            if (s->SB9 == SB9) found = i;
            if (found >= 0) break;
        }

        if (found < 0) continue;
        B = (Star*)cels[found];

        //   8- 23  F16.9 d       Per     [0.05,116675] Period
        read_field_onebased(buffer, 8, 23, field);
        B->orbit->period = atof(field)*oneday;

        //  42- 57  F16.8 d       T0      ? Periastron time (JD)
        read_field_onebased(buffer, 42, 57, field);
        B->orbit->epoch = atof(field);
        B->orbit->ascending_node = B->orbit->mean_anomaly = 0;

        //  79- 89  F11.9 ---     e       Orbital eccentricity
        read_field_onebased(buffer, 79, 89, field);
        B->orbit->eccentricity = atof(field);

        // 104-112  F9.4  deg     omega   [-359,360]? Argument of periastron {omega}
        read_field_onebased(buffer, 104, 112, field);
        B->orbit->arg_periapsis = atof(field)*fiftyseventh;
        B->orbit->mean_anomaly -= B->orbit->arg_periapsis;

        // a(*)sin i (expressed in km) can be computed from the Fortran formula
        // 13751 * sqrt(1-e*e) * K(*) * P
        // 124-133  F10.5 km/s    K1      ? Velocity amplitude of primary
        read_field_onebased(buffer, 124, 133, field);
        f = atof(field);
        B->orbit->semimajor_axis = (13751000 / oneday)              // convert to m/s
                                   * sqrt(1.0 - B->orbit->eccentricity*B->orbit->eccentricity)
                                   * f
                                   * B->orbit->period;

        // Star A is held stationary with B orbiting it, so the two semimajor axes are summed to
        // get the distance between the stars.
        // 148-157  E10.5 km/s    K2      ? Velocity amplitude of secondary
        read_field_onebased(buffer, 148, 157, field);
        f = atof(field);
        B->orbit->semimajor_axis += (13751000 / oneday)             // convert to m/s
                                    * sqrt(1.0 - B->orbit->eccentricity*B->orbit->eccentricity)
                                    * f
                                    * B->orbit->period;

        num_read++;
        if (offset >= (max-1))
        {
            fclose(fp);
            return num_read;
        }

        mtx.lock();
        if (!(num_read % 123)) loading_msg = std::string("Loaded ") + std::to_string(num_read)
                                                 + std::string(" objects from Stellar Binaries Catalogue...");
        mtx.unlock();
    }
    fclose(fp);

    return num_read;
}

bool CatalogReader::load_asteroid(AstorbRow *r, char *buffer)
{
    std::string path = "catalogs" _FILESLASH "astorb" _FILESLASH "astorb.dat";
    uint32_t asno;
    int _year, _month, _day;
    float absmagn;
    char field[32];
    bool delete_buffer = false;

    if (!buffer)
    {
        FILE* fp = fopen(path.c_str(), "rb");
        if (!fp) return false;
        buffer = new char[1024];
        delete_buffer = true;

        bool found = false;
        while (fgets(buffer, 1020, fp))
        {
            //   1-  6  I6    ---     Planet    [1,]?+ Asteroid number (blank if unnumbered)
            read_field_onebased(buffer, 1, 6, field);
            asno = atoi(field);
            if (r->number == asno) found = true;
            if (atoi(field) == 5747 && !strcmp(r->name.c_str(), "Williamina")) found = true;

            //   8- 25  A18   ---     Name      Name or preliminary designation.
            read_field_onebased(buffer, 8, 25, field);
            if (!strcmp(r->name.c_str(), trim(field).c_str())) found = true;

            //  60- 64  F5.1  km      Diam      ? IRAS diameter (see E.F.Tedesco, pp.1151-1161; catalog <II/190>)
            read_field_onebased(buffer, 60, 64, field);
            r->diam = atof(field);

            // 148-157  F10.6 deg     i         Inclination (3)
            read_field_onebased(buffer, 148, 157, field);
            r->incl = atof(field);

            // 169-181  F13.8 AU      a         ? Semimajor axis (3)
            read_field_onebased(buffer, 169, 181, field);
            r->sma = atof(field);

            if (found) break;
        }
        fclose(fp);
        if (!found)
        {
            delete[] buffer;
            return false;
        }
    }

    //  43- 47  F5.2  mag     H         Absolute magnitude H parameter (1)
    read_field_onebased(buffer, 43, 47, field);
    absmagn = atof(field);

    Planet *p = new Planet();
    r->cel = p;
    p->type = rocky;
    p->asteroid_no = r->number;
    p->location = cels[0]->location;
    p->cenobj = cels[0];
    p->orbit = new Orbit();
    p->orbit->center = cels[0];
    strcpy(p->name, r->name.c_str());
    p->namelen = strlen(p->name);
    p->absolute_magnitude = absmagn;

    //  55- 58  F4.2  mag     B-V       ? Color index (see E.F.Tedesco, pp.1090-1138)
    read_field_onebased(buffer, 55, 58, field);
    if (trim(field).size())
        p->BV_color = atof(field);
    else p->BV_color = 0.71;                            // typical value for asteroids

    p->volumetric_mean_radius = r->diam * 500;
    if (!p->volumetric_mean_radius)
    {
        if (!strcmp(r->name.c_str(), "Pluto")) p->volumetric_mean_radius = 1188300;
        else if (!strcmp(r->name.c_str(), "Eris")) p->volumetric_mean_radius = 1163000;
        else if (!strcmp(r->name.c_str(), "Quaoar")) p->volumetric_mean_radius = 1097600 / 2;
        else if (!strcmp(r->name.c_str(), "Sedna")) p->volumetric_mean_radius = 906000 / 2;
        else if (!strcmp(r->name.c_str(), "Orcus")) p->volumetric_mean_radius = 913000 / 2;
        else if (!strcmp(r->name.c_str(), "Haumea")) p->volumetric_mean_radius = 1544000 / 2;
        else if (!strcmp(r->name.c_str(), "Makemake")) p->volumetric_mean_radius = 715000;
    }
    assert(!isinf(p->volumetric_mean_radius));
    p->mass = p->volumetric_mean_radius * p->volumetric_mean_radius * p->volumetric_mean_radius * 4.0/3 * _pi * 1853;  // Pluto density.

    // 107-114  A8 "YYYYMMDD" Epoch     Epoch of osculation, yyyymmdd (TDT) (2)
    read_field_onebased(buffer, 107, 110, field);
    _year = atoi(field);
    read_field_onebased(buffer, 111, 112, field);
    _month = atoi(field);
    read_field_onebased(buffer, 113, 114, field);
    _day = atoi(field);

    std::tm epoch = {};
    epoch.tm_year = _year - 1900;
    epoch.tm_mon = _month - 1;
    epoch.tm_mday = _day;
    time_t t = mktime(&epoch);
    p->epoch = p->orbit->epoch = (((double)t - J2000_TIME_T)/oneday) + 2451544.5;

    // 116-125  F10.6 deg     M         Mean anomaly (3)
    read_field_onebased(buffer, 116, 125, field);
    p->orbit->mean_anomaly = atof(field) * fiftyseventh;

    // 127-136  F10.6 deg     omega     Argument of perihelion (3)
    read_field_onebased(buffer, 127, 136, field);
    p->orbit->arg_periapsis = atof(field) * fiftyseventh;

    // 138-147  F10.6 deg     Omega     Longitude of ascending node (3)
    read_field_onebased(buffer, 138, 147, field);
    p->orbit->ascending_node = atof(field) * fiftyseventh;

    p->orbit->inclination = r->incl * fiftyseventh;

    // 159-168  F10.8 ---     e         Eccentricity (3)
    read_field_onebased(buffer, 159, 168, field);
    p->orbit->eccentricity = atof(field);

    p->orbit->semimajor_axis = r->sma * AU;
    p->orbit->period = sqrt(r->sma*r->sma*r->sma) * oneyear;

    // Issue #58: Add default parameters for asteroids.
    p->estimate_albedo();
    if (!p->volumetric_mean_radius)
    {
        // Based on 163693 Atira. Ideally, this equation should use the estimated albedo and
        // #defined constants instead of hard coding it.
        p->volumetric_mean_radius = 8.74e+6 * sqrt(pow(magnbase, -p->absolute_magnitude));
        assert(!isinf(p->volumetric_mean_radius));
    }
    if (!p->mass) p->mass = 2.0e+6 * sphere_volume(p->volumetric_mean_radius);
    p->estimate_rotation();
    if (!p->albedo) p->estimate_albedo_and_absmagn();

    append_cel(p);
    if (delete_buffer) delete[] buffer;
    return true;
}

int CatalogReader::read_astorb_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs" _FILESLASH "astorb" _FILESLASH "astorb.dat";
    char buffer[1024];
    char field[32];
    int asno, num_read = 0, offset;
    AstorbRow row;
    float absmagn;

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);

    FILE* fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    while (fgets(buffer, 1020, fp))
    {
        row.cel = nullptr;

        //   8- 25  A18   ---     Name      Name or preliminary designation.
        read_field_onebased(buffer, 8, 25, field);
        row.name = trim(field);

        //   1-  6  I6    ---     Planet    [1,]?+ Asteroid number (blank if unnumbered)
        read_field_onebased(buffer, 1, 6, field);
        row.number = asno = atoi(field);
        if (asno == 5747) row.name = "Williamina";              // She invented the OBAFGKM system and astorb can't even honor her namesake????

        //  43- 47  F5.2  mag     H         Absolute magnitude H parameter (1)
        read_field_onebased(buffer, 43, 47, field);
        absmagn = atof(field);

        //  60- 64  F5.1  km      Diam      ? IRAS diameter (see E.F.Tedesco, pp.1151-1161; catalog <II/190>)
        read_field_onebased(buffer, 60, 64, field);
        row.diam = atof(field);

        // 148-157  F10.6 deg     i         Inclination (3)
        read_field_onebased(buffer, 148, 157, field);
        row.incl = atof(field);

        // 169-181  F13.8 AU      a         ? Semimajor axis (3)
        read_field_onebased(buffer, 169, 181, field);
        row.sma = atof(field);

        if (!asno)
        {
            astorb.push_back(row);
            continue;
        }

        if ((asno > 4 || absmagn >= 8)
                && asno != 55 && asno != 89 && asno != 105 && asno != 116 && asno != 490 && asno != 742 && asno != 896
                && asno != 1001 && asno != 1006 && asno != 1134 && asno != 1143 && asno != 1221 && asno != 1388 && asno != 1404 && asno != 1421
                && asno != 1566 && asno != 1604 && asno != 1691 && asno != 1693 && asno != 1709 && asno != 1741 && asno != 1772 && asno != 1776 && asno != 1789
                && asno != 1790 && asno != 1791 && asno != 1814 && asno != 1815 && asno != 1823 && asno != 1862 && asno != 1964 && asno != 1991
                && asno != 2000 && asno != 2001 && asno != 2002 && asno != 2060 && asno != 2062 && asno != 2069 && asno != 2101 && asno != 2161
                && asno != 2244 && asno != 2247 && asno != 2309 && asno != 2322 && asno != 2362 && asno != 2476 && asno != 2675 && asno != 2688
                && asno != 2709 && asno != 2769 && asno != 2801 && asno != 2807 && asno != 2810 && asno != 2830 && asno != 2937 && asno != 2985 && asno != 2999
                && asno != 3130 && asno != 3142 && asno != 3153 && asno != 3163 && asno != 3313 && asno != 3350 && asno != 3351 && asno != 3352
                && asno != 3353 && asno != 3354 && asno != 3355 && asno != 3356 && asno != 3366 && asno != 3412 && asno != 3524 && asno != 3534
                && asno != 3600 && asno != 3768 && asno != 3838 && asno != 3895 && asno != 3905 && asno != 3948
                && asno != 4062 && asno != 4147 && asno != 4169 && asno != 4179 && asno != 4180 && asno != 4221 && asno != 4330 && asno != 4337
                && asno != 4444 && asno != 4457 && asno != 4500 && asno != 4513 && asno != 4628 && asno != 4659 && asno != 4716 && asno != 4804 && asno != 4987
                && asno != 5000 && asno != 5020 && asno != 5370 && asno != 5471 && asno != 5535 && asno != 5668 && asno != 5747 && asno != 5773
                && asno != 5790 && asno != 5803 && asno != 5811
                && asno != 6006 && asno != 6032 && asno != 6123 && asno != 6143 && asno != 6186 && asno != 6433 && asno != 6469 && asno != 6470
                && asno != 6471 && asno != 6486 && asno != 6493 && asno != 6701 && asno != 6714 && asno != 6826 && asno != 6875 && asno != 6914 && asno != 6999
                && asno != 7000 && asno != 8000 && asno != 50000 && asno != 90377 && asno != 90482 && asno != 134340 && asno != 136108 && asno != 136199
                && asno != 136472 && asno != 163693 && asno != 486958 && asno != 541132
           )
        {
            astorb.push_back(row);
            continue;
        }

        load_asteroid(&row, buffer);

        astorb.push_back(row);
        num_read++;
        offset++;
        if (offset >= (max-1))
        {
            fclose(fp);
            return num_read;
        }
    }

    fclose(fp);
    return num_read;
}

// The IMCCE catalog names a comet twice: an IAU code ("1P", "C/1995 O1") and a discoverer's name
// that repeats the code's own letter ("P/Halley", "McNaught"). Put the two together the way the
// IAU writes them, so that a numbered periodic comet reads 1P/Halley and a one-visit comet reads
// C/1995 O1 (Hale-Bopp). Necessary and not merely tidy: there are sixty-odd comets in this file
// whose discoverer's name is nothing but "McNaught", and a picker full of them would be useless.
static std::string comet_display_name(std::string code, std::string name)
{
    // Drop the leading "P/", "C/", "D/" or "A/" the name field repeats from the code.
    size_t slash = name.find('/');
    if (slash != std::string::npos && slash <= 2)
    {
        bool letters = true;
        for (size_t i=0; i<slash; i++) if (!isalpha((unsigned char)name[i])) letters = false;
        if (letters) name = name.substr(slash+1);
    }

    if (!code.size()) return name;
    if (!name.size()) return code;

    // A bare number-and-letter code is the front half of the comet's name; a full provisional
    // designation is the whole of it, and the discoverer goes in brackets after.
    if (code.find('/') == std::string::npos) return code + "/" + name;
    return code + " (" + name + ")";
}

bool CatalogReader::load_comet(CometRow *r, char *buffer)
{
    std::string path = "catalogs" _FILESLASH "comets" _FILESLASH "comets.dat";
    char field[64];
    bool delete_buffer = false;

    if (!buffer)
    {
        FILE* fp = fopen(path.c_str(), "rb");
        if (!fp) return false;
        buffer = new char[1024];
        delete_buffer = true;

        bool found = false;
        while (fgets(buffer, 1020, fp))
        {
            //  18- 29  A12   ---        Code    IAU code for the comet
            read_field_onebased(buffer, 18, 29, field);
            if (r->code.size() && r->code == trim(field)) found = true;
            if (found) break;
        }
        fclose(fp);
        if (!found)
        {
            delete[] buffer;
            return false;
        }
    }

    Comet *c = new Comet();
    r->cel = c;
    c->location = cels[0]->location;
    c->cenobj = cels[0];
    c->orbit = new Orbit();
    c->orbit->center = cels[0];

    read_field_onebased(buffer, 18, 29, field);
    c->designation = trim(field);
    read_field_onebased(buffer, 39, 66, field);
    std::string dispname = comet_display_name(c->designation, trim(field));
    memset(c->name, 0, name_max_len);
    strncpy(c->name, dispname.c_str(), name_max_len-1);

    // 343-365 E23.15 d          T0      Date of perihelion (3)
    read_field_onebased(buffer, 343, 365, field);
    double T0 = atof(field);

    // 367-389 E23.15 AU         q       Perihelion distance (3)
    read_field_onebased(buffer, 367, 389, field);
    double q_au = atof(field);

    // 391-413 E23.15 ---        e       Orbit eccentricity (3)
    read_field_onebased(buffer, 391, 413, field);
    double e = atof(field);

    // 415-437 E23.15 deg        omega   Argument of perihelion (3)
    read_field_onebased(buffer, 415, 437, field);
    c->orbit->arg_periapsis = atof(field) * fiftyseventh;

    // 439-461 E23.15 deg        Omega   Longitude of orbital node (3)
    read_field_onebased(buffer, 439, 461, field);
    c->orbit->ascending_node = atof(field) * fiftyseventh;

    // 463-485 E23.15 deg        i       Inclination of the orbit (3)
    read_field_onebased(buffer, 463, 485, field);
    c->orbit->inclination = atof(field) * fiftyseventh;

    // 487-521: the two light curves: total and nucleus.
    read_field_onebased(buffer, 487, 491, field);    c->H1 = atof(field);
    read_field_onebased(buffer, 493, 497, field);    c->R1 = atof(field);
    read_field_onebased(buffer, 499, 503, field);    c->D1 = atof(field);
    read_field_onebased(buffer, 505, 509, field);    c->H2 = atof(field);
    read_field_onebased(buffer, 511, 515, field);    c->R2 = atof(field);
    read_field_onebased(buffer, 517, 521, field);    c->D2 = atof(field);

    if (q_au <= 0) q_au = 1;
    c->orbit->eccentricity = e;
    c->orbit->periapsis_distance = q_au * AU;
    c->orbit->T_periapsis = T0;

    // Anchoring the epoch on the perihelion passage lets the mean anomaly be exactly zero, which
    // is the one value it is known to take, and spares the closed-orbit path any conversion at
    // all: it counts from the epoch, and the epoch is now the moment the comet rounded the Sun.
    c->epoch = c->orbit->epoch = T0;
    c->orbit->mean_anomaly = 0;

    if (e < 1)
    {
        double a_au = q_au / (1.0 - e);
        c->orbit->semimajor_axis = a_au * AU;
        c->orbit->period = sqrt(a_au*a_au*a_au) * oneyear;
    }
    else
    {
        // No period, and no semimajor axis either -- but the culling code measures objects against
        // orbit->semimajor_axis to decide whether they are worth drawing, so give it the size of
        // the hyperbola rather than a zero that would read as "sitting on top of its star". A
        // parabola has no such size at all, hence the ceiling, which is far enough out that
        // nothing is ever culled by it.
        c->orbit->period = 0;
        double scale = (e > 1) ? (q_au / (e - 1.0)) : 0;
        if (!(scale > 0) || scale > 1e+7) scale = 1e+7;
        c->orbit->semimajor_axis = scale * AU;
    }

    // Nothing in the catalog says how big the nucleus is, so read it off the nucleus magnitude by
    // the usual diameter-albedo relation, at the 4% albedo of cometary ice and soot -- among the
    // darkest surfaces in the solar system. Halley comes out near 12 km against a measured 5.5,
    // which is the right order for a body the relation assumes is a sphere and which is in fact a
    // 15-by-8-kilometer peanut.
    double h_nucleus = c->H2 ? c->H2 : 14.0;
    c->volumetric_mean_radius = 0.5 * 1329000.0 / sqrt(0.04) * pow(10, -0.2 * h_nucleus);
    if (c->volumetric_mean_radius < 100) c->volumetric_mean_radius = 100;
    assert(!isinf(c->volumetric_mean_radius));

    c->mass = sphere_volume(c->volumetric_mean_radius) * 600;       // Nuclei are porous ice: about six tenths the density of water.
    c->absolute_magnitude = c->H1 ? c->H1 : h_nucleus;
    c->BV_color = 0.65;                                             // Sunlight, near enough: a coma is dust and gas scattering it back at us.

    append_cel(c);
    if (delete_buffer) delete[] buffer;
    return true;
}

int CatalogReader::read_comets_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs" _FILESLASH "comets" _FILESLASH "comets.dat";
    char buffer[1024];
    char field[64];
    int num_read = 0, offset;
    CometRow row;

    // The four that get a cels[] slot without being asked for: the one everybody can name, and
    // the three great comets of living memory. Keyed on the IAU code because the discoverers'
    // names are not unique -- "McNaught" alone would match sixty-odd comets, nearly all of them
    // faint short-period ones, and not the Great Comet of 2007 that is meant here.
    static const char *default_comets[] = { "1P", "C/1995 O1", "C/1996 B2", "C/2006 P1", nullptr };

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);

    FILE* fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1))
    {
        fclose(fp);
        return 0;
    }

    while (fgets(buffer, 1020, fp))
    {
        row.cel = nullptr;

        read_field_onebased(buffer, 18, 29, field);
        row.code = trim(field);
        read_field_onebased(buffer, 39, 66, field);
        row.name = comet_display_name(row.code, trim(field));

        read_field_onebased(buffer, 343, 365, field);
        row.T_peri = atof(field);
        read_field_onebased(buffer, 367, 389, field);
        row.q = atof(field);
        read_field_onebased(buffer, 391, 413, field);
        row.e = atof(field);
        read_field_onebased(buffer, 463, 485, field);
        row.incl = atof(field);

        if (!row.code.size() && !row.name.size()) continue;

        bool wanted = false;
        for (int k=0; default_comets[k]; k++) if (row.code == default_comets[k]) wanted = true;

        if (wanted)
        {
            load_comet(&row, buffer);
            num_read++;
            offset++;
            if (offset >= (max-1))
            {
                comets.push_back(row);
                fclose(fp);
                return num_read;
            }
        }

        comets.push_back(row);
    }

    fclose(fp);
    return num_read;
}

static double accepted_exoplanet_inclination(double inclination)
{
    return std::isfinite(inclination) && inclination >= 2 * fiftyseventh ? inclination : 0;
}

static bool hot_jupiter_inclination_exemption(const Planet* planet)
{
    if (!planet || !planet->orbit) return false;
    // The general classifier also labels short-period sub-Neptunes as hot
    // Jupiters. Reserve this exemption for giant masses or giant radii.
    const bool giant = planet->mass >= 0.2 * jupiter_mass ||
        planet->volumetric_mean_radius >= 0.6 * jupiter_radius;
    return giant && (planet->type == hot_jupiter ||
        (planet->orbit->period > 0 && planet->orbit->period < 10 * oneday));
}

static bool broad_exoplanet_inclination(const Planet* planet)
{
    return planet && std::isfinite(planet->inclination_error_upper) &&
        std::isfinite(planet->inclination_error_lower) &&
        fabs(planet->inclination_error_upper) >= 20 * fiftyseventh &&
        fabs(planet->inclination_error_lower) >= 20 * fiftyseventh;
}

void CatalogReader::reconcile_exoplanet_inclinations(
    Star* host_star,
    const std::vector<Planet*>& planets,
    std::vector<double>& pincls,
    std::vector<double>& pnodes)
{
    const size_t n = planets.size();
    if (n == 0 || pincls.size() != n || pnodes.size() != n) return;
    for (size_t i = 0; i < n; ++i)
    {
        pincls[i] = accepted_exoplanet_inclination(pincls[i]);
        // An exact edge-on catalog value is a placeholder without transit evidence.
        if (pincls[i] == 90 * fiftyseventh && (!planets[i] || !planets[i]->transiting)) pincls[i] = 0;
    }

    std::vector<double> precise, available;
    for (size_t i = 0; i < n; ++i)
    {
        if (pincls[i] == 0) continue;
        available.push_back(pincls[i]);
        const Planet* planet = planets[i];
        if (!planet || broad_exoplanet_inclination(planet) ||
            hot_jupiter_inclination_exemption(planet)) continue;
        const bool published_errors = std::isfinite(planet->inclination_error_upper) &&
            std::isfinite(planet->inclination_error_lower);
        // Non-placeholder near-edge-on measurements remain useful reference candidates.
        // This does not classify them as transiting; that status comes from source metadata.
        const bool edge_on_reference = pincls[i] >= 80 * fiftyseventh && pincls[i] <= 100 * fiftyseventh;
        if (published_errors || edge_on_reference) precise.push_back(pincls[i]);
    }
    const auto reference = [](std::vector<double> values)
    {
        double transit_sum = 0;
        size_t transit_count = 0;
        for (double inclination : values)
        {
            if (inclination >= 80 * fiftyseventh && inclination <= 100 * fiftyseventh)
            {
                transit_sum += inclination;
                ++transit_count;
            }
        }
        if (transit_count) return transit_sum / transit_count;
        // Preserve the dominant four-degree cluster, including supplementary
        // representations of the same orbital plane.
        double cluster_reference = 0;
        size_t anchor_votes = 0;
        for (double candidate : values)
        {
            size_t votes = 0, direct_votes = 0;
            double sum = 0;
            for (double inclination : values)
            {
                if (fabs(candidate - inclination) < 4 * fiftyseventh)
                {
                    ++votes;
                    ++direct_votes;
                    sum += inclination;
                }
                else if (fabs((_pi - candidate) - inclination) < 4 * fiftyseventh)
                {
                    ++votes;
                    sum += _pi - inclination;
                }
            }
            if (votes > anchor_votes || (votes == anchor_votes && direct_votes > anchor_votes / 2))
            {
                anchor_votes = votes;
                cluster_reference = sum / votes;
            }
        }
        if (cluster_reference) return cluster_reference;
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    };
    const double precise_reference = reference(precise);
    double anchor = host_star && host_star->has_disk ? host_star->disk_heliocen_inclination : 0;
    if (!anchor) anchor = precise_reference;
    if (!anchor) anchor = reference(available);
    if (!anchor) return;
    if (host_star) host_star->planets_heliocen_inclination = anchor;

    for (size_t i = 0; i < n; ++i)
    {
        Planet* planet = planets[i];
        bool replace = pincls[i] == 0;
        if (!replace && precise_reference && broad_exoplanet_inclination(planet) &&
            !hot_jupiter_inclination_exemption(planet))
        {
            const double lower = pincls[i] - fabs(planet->inclination_error_lower);
            const double upper = pincls[i] + fabs(planet->inclination_error_upper);
            // Broad estimates remain useful until a more precise planet-plane
            // estimate falls outside their published uncertainty interval.
            replace = precise_reference < lower || precise_reference > upper;
        }
        if (!replace) continue;
        const double replacement = pincls[i] == 0 ? anchor : precise_reference;
        const double new_sin = sin(replacement);
        if (planet && new_sin > 0.05 && planet->msini > 0) planet->mass = planet->msini / new_sin;
        pincls[i] = replacement;
        if (planet && planet->orbit) planet->orbit->inclination = replacement;
        if (!pnodes[i] && host_star)
        {
            pnodes[i] = host_star->planets_heliocen_node ? host_star->planets_heliocen_node : host_star->disk_heliocen_node;
            if (planet && planet->orbit && pnodes[i]) planet->orbit->ascending_node = pnodes[i];
        }
    }
}

#define _debug_exoplanet_inclinations 0
void CatalogReader::apply_exoplanet_names(const std::map<int, std::vector<int>>& planet_celids,
    const ExactStarSearchIndex *lookup)
{
    std::lock_guard<std::recursive_mutex> catalog_lock(catalog_mutex);
    std::unique_ptr<ExactStarSearchIndex> local_lookup;
    if (!lookup)
    {
        local_lookup = std::make_unique<ExactStarSearchIndex>();
        lookup = local_lookup.get();
    }
    std::map<std::string, std::string> planet_names;
    std::map<std::string, std::string> planet_types;
    std::map<std::string, double> planet_temps;
    std::map<std::string, double> planet_incls;
    std::map<std::string, double> planet_nodes;
    std::map<std::string, double> planet_istars;
    std::map<std::string, double> planet_bvcols;
    std::map<std::string, double> planet_albedines;
    std::map<std::string, bool> planet_name_used;
    bool any_planet_name_used = false;

    FILE *fp;
    char buffer[2048];
    fp = fopen("exoname.dat", "r");
    if (fp)
    {
        char field[256];
        while (fgets(buffer, 2046, fp))
        {
            if (buffer[0] == '#') continue;
            if (strlen(buffer) < 40) continue;

            if (buffer[0] == 'H' && buffer[1] == 'D')
            {
                // A bare "HD nnnnnn" or "HDnnnnnn" designation names the star itself; anything trailing the
                // number ("HD nnnnnn b") is one of its planets, and belongs to the pass below.
                read_field_onebased(buffer, 1, 39, field);
                std::string desig = trim(field);
                int HD = 0;
                if (desig.find_first_not_of("0123456789 ", 2) == std::string::npos)
                {
                    HD = atoi(&desig[2]);
                }

                if (HD > 0)
                {
                    read_field_onebased(buffer, 41, 63, field);
                    std::string friendly = trim(field);
                    Star* target_star = nullptr;
                    if (HD <= MAX_HD && hdcache && hdcache[HD])
                    {
                        target_star = hdcache[HD];
                    }
                    else
                    {
                        std::string s_hd1 = "HD " + std::to_string(HD);
                        std::string s_hd2 = "HD" + std::to_string(HD);
                        int obj_idx = find_object(s_hd1.c_str(), true, 9e29, 1, lookup);
                        if (obj_idx < 0)
                        {
                            obj_idx = find_object(s_hd2.c_str(), true, 9e29, 1, lookup);
                        }
                        if (obj_idx >= 0 && cels[obj_idx]->typeclass() == class_star)
                        {
                            target_star = (Star*)cels[obj_idx];
                            if (HD <= MAX_HD && hdcache)
                            {
                                hdcache[HD] = target_star;
                            }
                        }
                    }
                    if (target_star)
                    {
                        target_star->local_name = friendly;
                    }
                    continue;
                }
            }

            read_field_onebased(buffer, 1, 39, field);
            std::string designation = trim(field);

            read_field_onebased(buffer, 41, 63, field);
            std::string friendly = trim(field);

            planet_names[designation] = friendly;
            planet_name_used[designation] = false;

            if (strlen(buffer) < 65) continue;
            read_field_onebased(buffer, 65, 87, field);
            std::string ptype = trim(field);
            if (ptype.size()) planet_types[designation] = ptype;

            if (strlen(buffer) < 89) continue;
            read_field_onebased(buffer, 89, 103, field);
            double T = atof(field);
            if (T) planet_temps[designation] = T;

            if (strlen(buffer) < 105) continue;
            read_field_onebased(buffer, 105, 115, field);
            double i = atof(field);
            if (i) planet_incls[designation] = i;

            if (strlen(buffer) < 117) continue;
            read_field_onebased(buffer, 117, 128, field);
            double n = atof(field);
            if (n) planet_nodes[designation] = n;

            if (strlen(buffer) < 129) continue;
            read_field_onebased(buffer, 129, 143, field);
            double l = atof(field);
            if (l) planet_istars[designation] = l;

            if (strlen(buffer) < 145) continue;
            read_field_onebased(buffer, 145, 155, field);
            std::string bvstr = trim(field);
            double bv = atof(field);
            if      (!strcmp(bvstr.c_str(), "red"       )) bv =  1.5;
            else if (!strcmp(bvstr.c_str(), "reddish"   )) bv =  1.0;
            else if (!strcmp(bvstr.c_str(), "gray"      )) bv =  0.6;
            else if (!strcmp(bvstr.c_str(), "bluish"    )) bv =  0.3;
            else if (!strcmp(bvstr.c_str(), "blue"      )) bv = -0.1;
            if (bv) planet_bvcols[designation] = bv;

            if (strlen(buffer) < 157) continue;
            read_field_onebased(buffer, 157, 177, field);
            double alb = atof(field);
            if (alb) planet_albedines[designation] = alb;
        }

        fclose(fp);
    }

    const auto designation_key = [](const std::string& designation)
    {
        std::string key;
        for (unsigned char c : designation)
        {
            if (!std::isspace(c)) key += c;
        }
        // An omitted primary component and an explicit A describe the same host.
        // Preserve B and later components so planets around different stars stay distinct.
        if (key.size() > 2 && key[key.size() - 2] == 'A'
            && key.back() >= 'b' && key.back() <= 'z')
        {
            key.erase(key.size() - 2, 1);
        }
        for (char& c : key) c = std::tolower(static_cast<unsigned char>(c));
        return key;
    };

    for (auto const& [idx, row] : planet_celids)
    {
        Star *s = (Star*)cels[idx];

        double sysincl = 0, sysnode = 0;
        double stincl = s->obliquity, stnode = s->equinox;
        if (s->has_disk)
        {
            sysincl = s->disk_heliocen_inclination;
            sysnode = s->disk_heliocen_node;
        }
        std::vector<double> pincls, pnodes;
        std::vector<double> cincls, cnodes;

        int i, n = row.size();
        for (i = 0; i < n; i++)
        {
            Planet *p = (Planet*)cels[row[i]];
            std::string designation = p->name;
            p->origname = designation;

            if (planet_names.find(designation) == planet_names.end())
            {
                std::vector<std::string> candidate_desigs;
                candidate_desigs.push_back(designation);

                if (designation.size() > 2)
                {
                    char p_let = designation.back();
                    if (p_let >= 'b' && p_let <= 'z')
                    {
                        size_t dlen = designation.size();
                        if (dlen >= 3 && designation[dlen - 2] == 'A')
                        {
                            candidate_desigs.push_back(designation.substr(0, dlen - 2) + p_let);
                        }
                        if (dlen >= 4 && designation[dlen - 2] == 'A' && designation[dlen - 3] == ' ')
                        {
                            candidate_desigs.push_back(designation.substr(0, dlen - 3) + " " + p_let);
                        }
                    }
                }
                if (designation.rfind("gamma Cephei", 0) == 0)
                {
                    candidate_desigs.push_back("gam Cep " + std::string(1, designation.back()));
                }
                if (designation.rfind("eps Eridani", 0) == 0)
                {
                    candidate_desigs.push_back("eps Eri " + std::string(1, designation.back()));
                }
                if (s)
                {
                    char p_let = designation.empty() ? 'b' : designation.back();
                    if (p_let >= 'b' && p_let <= 'z')
                    {
                        if (s->Gliese[0])
                        {
                            candidate_desigs.push_back(std::string(s->Gliese) + " " + p_let);
                            candidate_desigs.push_back(std::string(s->Gliese) + "  " + p_let);
                        }
                        if (!strcmp(s->name, "Pollux"))
                        {
                            candidate_desigs.push_back(std::string("GJ 286 ") + p_let);
                            candidate_desigs.push_back(std::string("GJ 286  ") + p_let);
                        }
                        if (s->HD)
                        {
                            candidate_desigs.push_back("HD " + std::to_string(s->HD) + " " + p_let);
                        }
                        if (s->HIP)
                        {
                            candidate_desigs.push_back("HIP " + std::to_string(s->HIP) + " " + p_let);
                        }
                    }
                }

                bool found = false;
                for (const auto& cdesig : candidate_desigs)
                {
                    if (planet_names.find(cdesig) != planet_names.end())
                    {
                        designation = cdesig;
                        found = true;
                        break;
                    }
                    for (auto const& [dkey, fname] : planet_names)
                    {
                        if (designation_key(cdesig) == designation_key(dkey))
                        {
                            designation = dkey;
                            found = true;
                            break;
                        }
                    }
                    if (found)
                    {
                        break;
                    }
                }
            }

            if (planet_incls.find(designation) != planet_incls.end())
                pincls.push_back(planet_incls[designation] * fiftyseventh);
            else if (p->orbit)
            {
                pincls.push_back((p->orbit->heliocentric_inclination || p->orbit->heliocentric_node)
                    ? p->orbit->heliocentric_inclination : p->orbit->inclination);
            }

            double p_node = p->orbit ? p->orbit->heliocentric_node : 0;
            if (planet_nodes.find(designation) != planet_nodes.end())
                p_node = planet_nodes[designation] * fiftyseventh;
            pnodes.push_back(p_node);

            if (planet_names.find(designation) != planet_names.end())
            {
                strcpy(p->name, planet_names[designation].c_str());
                planet_name_used[designation] = true;
                any_planet_name_used = true;
            }

            if (planet_types.find(designation) != planet_types.end())
            {
                const char* ihavetomove = planet_types[designation].c_str();
                if (!strcmp(ihavetomove, "gas_giant")) p->type = gas_giant;
                else if (!strcmp(ihavetomove, "hot_jupiter")) p->type = hot_jupiter;
                else if (!strcmp(ihavetomove, "rocky")) p->type = rocky;
                else if (!strcmp(ihavetomove, "super_venus"))
                {
                    p->type = rocky;
                    if (!p->volumetric_mean_radius) p->estimate_radius();
                    double pressure = 100.0 * oneatm * p->estimate_surface_gravity();
                    if (!isinf(pressure)) p->ensure_atmosphere()->surface_pressure = pressure;
                }
                else if (!strcmp(ihavetomove, "lavaworld")) p->type = lavaworld;
                else if (!strcmp(ihavetomove, "ice_giant")) p->type = ice_giant;
                else if (!strcmp(ihavetomove, "icy")) p->type = icy;
                else if (!strcmp(ihavetomove, "hycean")) p->type = hycean;
                else if (!strcmp(ihavetomove, "waterworld")) p->type = waterworld;
                p->lock_type = true;
                p->set_color_from_type(p->is_in_con_HZ());
            }

            if (planet_temps.find(designation) != planet_temps.end()) p->temperature = planet_temps[designation];
            if (planet_bvcols.find(designation) != planet_bvcols.end()) p->BV_color = planet_bvcols[designation];
            if (planet_albedines.find(designation) != planet_albedines.end())
            {
                p->albedo = planet_albedines[designation];
                if (!p->volumetric_mean_radius) p->estimate_radius();
                p->estimate_albedo_and_absmagn();
                p->set_color_from_type(p->is_in_con_HZ());
            }
        }

        bool have_companion_plane = false;
        double companion_inclination = 0, companion_node = 0;
        std::vector<bool> unknown_inclinations;
        for (double& inclination : pincls)
        {
            inclination = accepted_exoplanet_inclination(inclination);
            unknown_inclinations.push_back(inclination == 0);
        }
        if (s->multisys)
        {
            for (char c = 'A'; c <= 'Z'; c++)
            {
                Star *comp = s->multisys->get_member(c);
                if (comp && comp->orbit && comp->orbit->center == s)
                {
                    cincls.push_back(comp->orbit->heliocentric_inclination);
                    cnodes.push_back(comp->orbit->heliocentric_node);
                    if (!have_companion_plane && (cincls.back() || cnodes.back()))
                    {
                        companion_inclination = cincls.back();
                        companion_node = cnodes.back();
                        have_companion_plane = true;
                    }
                }
            }
        }

#if _debug_exoplanet_inclinations
        std::cout << s->name;
        if (s->HD) std::cout << " HD " << s->HD;
        std::cout << std::endl;

        std::cout << "System: " << (sysincl*fiftyseven) << "," << (sysnode*fiftyseven) << std::endl;
        std::cout << "Star:   " << (stincl*fiftyseven) << "," << (stnode*fiftyseven) << std::endl;
#endif

        std::vector<Planet*> sys_planets;
        for (i = 0; i < (int)row.size(); i++)
        {
            sys_planets.push_back((Planet*)cels[row[i]]);
        }
        reconcile_exoplanet_inclinations(s, sys_planets, pincls, pnodes);

        // A host that is itself a stellar companion can use its own binary orbit.
        if (!have_companion_plane && s->orbit && s->orbit->center &&
            s->orbit->center->typeclass() == class_star &&
            (s->orbit->heliocentric_inclination || s->orbit->heliocentric_node))
        {
            companion_inclination = s->orbit->heliocentric_inclination;
            companion_node = s->orbit->heliocentric_node;
            have_companion_plane = true;
        }
        if (have_companion_plane)
        {
            bool all_unknown = true;
            for (size_t k = 0; k < pincls.size(); ++k)
            {
                if (unknown_inclinations[k])
                {
                    pincls[k] = companion_inclination;
                    pnodes[k] = companion_node;
                }
                else all_unknown = false;
            }
            if (all_unknown)
            {
                sysincl = companion_inclination;
                sysnode = companion_node;
            }
        }

        n = pincls.size();
        int l=0;
        double m=0;
        double pmeanincl=0, pmeannode=0;
        for (i=0; i<n; i++)
        {
            double sini = 1;
            if (pincls[i])
            {
                sini = sin(pincls[i]);
                pmeanincl += pincls[i];
                l++;
            }
            if (pnodes[i])
            {
                // TODO: Account for retrograde nodes e.g. 30deg is coplanar with 210deg.
                double node = pnodes[i];
                double mnew = m+sini;
                if (m)
                {
                    // Account for nodes near 0 and 360 deg.
                    if (node > _pi && pmeannode < _pi) node -= _pi*2;
                    else if (node < _pi && pmeannode > _pi) node += _pi*2;
                }
                pmeannode = (m ? (pmeannode * (double)m/mnew) : pmeannode) + node * sini / mnew;
                m = mnew;
            }
#if _debug_exoplanet_inclinations
            std::cout << "Planet: " << (pincls[i]*fiftyseven) << "," << (pnodes[i]*fiftyseven) << std::endl;
#endif
        }

        if (l) pmeanincl /= l;
        if (pmeannode < 0) pmeannode += _pi*2;

        n = cincls.size();
        double cmeanincl=0, cmeannode=0;
        l=m=0;
        for (i=0; i<n; i++)
        {
            double sini = 1;
            if (cincls[i])
            {
                sini = sin(cincls[i]);
                cmeanincl += cincls[i];
                l++;
            }
            if (cnodes[i])
            {
                double node = cnodes[i];
                double mnew = m+sini;
                if (m)
                {
                    if (node > _pi && cmeannode < _pi) node -= _pi*2;
                    else if (node < _pi && cmeannode > _pi) node += _pi*2;
                }
                cmeannode = (m ? (cmeannode * (double)m/mnew) : cmeannode) + node * sini / mnew;
                m = mnew;
            }
#if _debug_exoplanet_inclinations
            std::cout << "Comp:   " << (cincls[i]*fiftyseven) << "," << (cnodes[i]*fiftyseven) << std::endl;
#endif
        }

        if (l) cmeanincl /= l;

#if _debug_exoplanet_inclinations
        std::cout << std::endl;
#endif

        // planets > system > star > comps
        if (s->planets_heliocen_inclination && !sysincl)
        {
            sysincl = s->planets_heliocen_inclination;
        }
        if (pmeanincl && !sysincl)
        {
            sysincl = pmeanincl;
        }
        if (!sysincl && stincl)
        {
            sysincl = stincl;
        }
        if (cmeanincl && !sysincl)
        {
            sysincl = cmeanincl;
        }
        if (!sysincl && s->orbit && s->orbit->heliocentric_inclination)
        {
            sysincl = s->orbit->heliocentric_inclination;
        }
        if (sysincl)
        {
            if (!stincl) stincl = sysincl;
            n = pincls.size();
            for (i=0; i<n; i++) if (!pincls[i]) pincls[i] = sysincl;
            n = cincls.size();
            for (i=0; i<n; i++) if (!cincls[i]) cincls[i] = sysincl;
        }

        if (pmeannode && !sysnode) sysnode = pmeannode;
        if (!sysnode && stnode) sysnode = stnode;
        if (cmeannode && !sysnode) sysnode = cmeannode;
        if (!sysnode && s->orbit && s->orbit->heliocentric_node) sysnode = s->orbit->heliocentric_node;
        if (!stnode && sysnode)
        {
            stnode = sysnode;
        }
        n = pnodes.size();
        for (i = 0; i < n; i++)
        {
            if (!pnodes[i] && !(have_companion_plane && unknown_inclinations[i]))
            {
                double direct_delta = fabs(pincls[i] - sysincl);
                double fold_delta = fabs((_pi - pincls[i]) - sysincl);
                if (fold_delta < direct_delta)
                {
                    pnodes[i] = fmod(sysnode + _pi, 2.0 * _pi);
                }
                else
                {
                    pnodes[i] = sysnode;
                }
            }
        }
        n = cnodes.size();
        for (i = 0; i < n; i++)
        {
            if (!cnodes[i])
            {
                cnodes[i] = sysnode;
            }
        }

#if _debug_exoplanet_inclinations
        std::cout << "Filled in:" << std::endl;
        std::cout << "System: " << (sysincl*fiftyseven) << "," << (sysnode*fiftyseven) << std::endl;
        std::cout << "Star:   " << (stincl*fiftyseven ) << "," << (stnode*fiftyseven ) << std::endl;

        n = pincls.size();
        for (i=0; i<n; i++) std::cout << "Planet: " << (pincls[i]*fiftyseven) << "," << (pnodes[i]*fiftyseven) << std::endl;
        n = cincls.size();
        for (i=0; i<n; i++) std::cout << "Comp:   " << (cincls[i]*fiftyseven) << "," << (cnodes[i]*fiftyseven) << std::endl;

        std::cout << std::endl;
#endif

        if (sysincl) s->planets_heliocen_inclination = sysincl;
        if (sysnode) s->planets_heliocen_node = sysnode;

        s->location.local_system_plane = system_plane_from_incl_and_node((fabs(sysincl) >= 1e-6) ? sysincl : half_pi,
                                         sysnode, s->location.system_center);
        s->lock_system_plane = true;

#if _debug_exoplanet_inclinations
        double czincl, cznode;
        incl_and_node_from_system_plane(s->location.local_system_plane, czincl, cznode, s->location.system_center);
        std::cout << "Double check: " << (czincl*fiftyseven) << "," << (cznode*fiftyseven) << std::endl;
#endif

        n = row.size();
        for (i=0; i<n; i++)
        {
            Planet *p = (Planet*)cels[row[i]];
            if (planet_istars.find(p->origname) != planet_istars.end())
            {
                stnode = pnodes[i] - planet_istars[p->origname]*fiftyseventh;
#if _debug_exoplanet_inclinations
                std::cout << "Star:   " << (stincl*fiftyseven ) << "," << (stnode*fiftyseven ) << std::endl;
#endif
            }

            // Preserve the source frame for subsequent catalog passes and serialization.
            p->orbit->heliocentric_inclination = (fabs(sysincl) >= 1e-6) ? pincls[i] : half_pi;
            p->orbit->heliocentric_node = pnodes[i];
            elements_in_new_reference_plane(system_plane_from_incl_and_node((fabs(sysincl) >= 1e-6) ? pincls[i] : half_pi,
                                            pnodes[i], s->location.system_center),
                                            s->location.local_system_plane,
                                            p->orbit->inclination, p->orbit->ascending_node);

            p->location.local_system_plane = s->location.local_system_plane;
            p->lock_equatorial_plane = false;

#if _debug_exoplanet_inclinations
            p->update_location(simnow);
            double czincl, cznode;
            incl_and_node_from_system_plane(p->location.orbital_plane, czincl, cznode, s->location.system_center);
            std::cout << "Double check " << p->name << ": " << (czincl*fiftyseven) << "," << (cznode*fiftyseven) << std::endl;
#endif

            p->origname = p->name;
        }

        elements_in_new_reference_plane(system_plane_from_incl_and_node((fabs(sysincl) >= 1e-6) ? stincl : half_pi,
                                        stnode,
                                        s->location.system_center),
                                        s->location.local_system_plane,
                                        s->obliquity, s->equinox);

        if (s->multisys)
        {
            i = 0;
            n = cincls.size();
            for (char c = 'A'; c <= 'Z'; c++)
            {
                Star *comp = s->multisys->get_member(c);
                if (comp && comp->orbit && comp->orbit->center == s)
                {
                    assert(i<n);
                    elements_in_new_reference_plane(system_plane_from_incl_and_node((fabs(sysincl) >= 1e-6) ? cincls[i] : half_pi,
                                                    cnodes[i],
                                                    s->location.system_center),
                                                    s->location.local_system_plane,
                                                    comp->orbit->inclination, comp->orbit->ascending_node);

                    comp->location.local_system_plane = s->location.local_system_plane;

#if _debug_exoplanet_inclinations
                    comp->update_location(simnow);
                    double czincl, cznode;
                    incl_and_node_from_system_plane(comp->location.orbital_plane, czincl, cznode, comp->location.system_center);
                    std::cout << "Double check " << comp->name << ": " << (czincl*fiftyseven) << "," << (cznode*fiftyseven) << std::endl;
#endif
                    i++;
                }
            }
        }

#if _debug_exoplanet_inclinations
        std::cout << std::endl << std::endl;
#endif
    }

    if (any_planet_name_used) for (auto& [designation, name] : planet_names)            // This line gets hit twice, once with no matches and once with hopably all matched.
    {
        if (!planet_name_used[designation]) std::cerr << "WARNING - exoname not matched: " << designation << " / " << name << std::endl;
    }
}

bool CatalogReader::worth_searching(std::string star_name)
{
    if (!strcmp(star_name.substr(0, 5).c_str(), "2MASS")) return false;
    if (!strcmp(star_name.substr(0, 6).c_str(), "Kepler"))
        return false;
    if (!strcmp(star_name.substr(0, 5).c_str(), "CoRoT")) return false;
    if (!strcmp(star_name.substr(0, 5).c_str(), "Qatar")) return false;
    if (!strcmp(star_name.substr(0, 4).c_str(), "Gaia")) return false;
    if (!strcmp(star_name.substr(0, 4).c_str(), "Wolf")) return false;
    if (!strcmp(star_name.c_str(), "Teegarden's Star")) return false;
    if (!strcmp(star_name.substr(0, 6).c_str(), "82 Eri")) return true;

    if (!strcmp(star_name.substr(0, 3).c_str(), "GJ ")) return true;
    if (((star_name.c_str()[0] >= 'A' && star_name.c_str()[0] <= 'Z')
            || (star_name.c_str()[0] >= 'a' && star_name.c_str()[0] <= 'z')
        )
            && star_name.c_str()[1] >= 'a' && star_name.c_str()[1] <= 'z'
            && star_name.c_str()[2] >= 'a' && star_name.c_str()[2] <= 'z'
       )
        return true;
    int l = star_name.size();
    if ((star_name.c_str()[0] >= '1' && star_name.c_str()[0] <= '9')
            && (star_name.c_str()[l-1] >= 'A' && star_name.c_str()[l-1] <= 'z')
       )
        return true;

    return false;
}

int CatalogReader::read_exoplanets_catalog(CelestialObject **cels, int max)
{
    FILE *fp;
    char buffer[2048], wasfirst = 0;
    int offset, num_added = 0;

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);
    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    std::string path = "catalogs" + _FSSTR;
    std::string startswith = "PSCompPars_";
    std::string candidate = "";
    std::vector<std::string> results;
    std::map<int, std::vector<int>> planet_celids;

    try
    {
        for (const auto& entry : fs::directory_iterator(path))
        {
            std::string entry_name = entry.path().filename().string();
            if (!fs::is_directory(entry.path())
                    &&
                    !strcmp(entry_name.substr(0, startswith.size()).c_str(), startswith.c_str())
               )
            {
                if (strcmp(entry_name.c_str(), candidate.c_str()) > 0) candidate = entry_name;
            }
        }
        if (!candidate.size()) return 0;
        std::cout << "Found " << candidate << std::endl;
        have_exo = true;
    }
    catch (const fs::filesystem_error& e)
    {
        std::cerr << "Error: " << e.what() << '\n';
        return 0;
    }

    path += candidate;
    std::stringstream lmss;
    lmss << "Loading exoplanets from " << path << "...";
    mtx.lock();
    loading_msg = lmss.str();
    mtx.unlock();

    fp = fopen(path.c_str(), "r");
    int i=0;
    int col_plnm=-1, col_stnm=-1, col_hd=-1, col_orbper=-1, col_sma=-1, col_rade=-1, col_radj=-1,
        col_mass_e=-1, col_mass_j=-1, col_eccn=-1, col_incl=-1, col_periepo=-1, col_argperi=-1,
        col_oblt=-1, col_sptp=-1, col_srad=-1, col_smass=-1, col_stemp=-1,
        col_ra=-1, col_decl=-1, col_dist=-1, col_vmag=-1;
    while (fgets(buffer, 2046, fp))
    {
        if (buffer[0] == '#' && buffer[2] == 'C' && buffer[3] == 'O'
                && buffer[4] == 'L' && buffer[5] == 'U' && buffer[6] == 'M'
                && buffer[7] == 'N'
           )
        {
            char *colon = strchr(buffer, ':');
            if (!colon) continue;
            *colon = 0;
            if (!strcmp(buffer, "# COLUMN pl_name")) col_plnm = i;
            if (!strcmp(buffer, "# COLUMN hostname")) col_stnm = i;
            if (!strcmp(buffer, "# COLUMN hd_name")) col_hd = i;
            if (!strcmp(buffer, "# COLUMN pl_orbper")) col_orbper = i;
            if (!strcmp(buffer, "# COLUMN pl_orbsmax")) col_sma = i;
            if (!strcmp(buffer, "# COLUMN pl_rade")) col_rade = i;
            if (!strcmp(buffer, "# COLUMN pl_radj")) col_radj = i;
            if (!strcmp(buffer, "# COLUMN pl_bmasse")) col_mass_e = i;
            if (!strcmp(buffer, "# COLUMN pl_bmassj")) col_mass_j = i;
            if (!strcmp(buffer, "# COLUMN pl_orbeccen")) col_eccn = i;
            if (!strcmp(buffer, "# COLUMN pl_orbincl")) col_incl = i;
            if (!strcmp(buffer, "# COLUMN pl_orbtper")) col_periepo = i;
            if (!strcmp(buffer, "# COLUMN pl_orblper")) col_argperi = i;
            if (!strcmp(buffer, "# COLUMN pl_trueobliq")) col_oblt = i;
            if (!strcmp(buffer, "# COLUMN st_spectype")) col_sptp = i;
            if (!strcmp(buffer, "# COLUMN st_rad")) col_srad = i;
            if (!strcmp(buffer, "# COLUMN st_mass")) col_smass = i;
            if (!strcmp(buffer, "# COLUMN st_teff")) col_stemp = i;
            if (!strcmp(buffer, "# COLUMN ra")) col_ra = i;
            if (!strcmp(buffer, "# COLUMN dec")) col_decl = i;
            if (!strcmp(buffer, "# COLUMN sy_dist")) col_dist = i;
            if (!strcmp(buffer, "# COLUMN sy_vmag")) col_vmag = i;

            i++;
        }
        else if (wasfirst == '#' && buffer[0] != '#')
        {
            if (col_plnm<0 || col_stnm<0 || col_hd<0 || col_orbper<0 || col_sma<0
                    || (col_rade<0 && col_radj<0) || (col_mass_e<0 && col_mass_j<0)
                    || col_eccn<0 || col_incl<0 || col_periepo<0 || col_argperi<0
                    || col_oblt<0 || col_srad<0 || col_smass<0
                    || col_ra<0 || col_decl<0 || col_vmag<0
               )
            {
                std::stringstream oss;
                oss << "ERROR: Exoplanets file " << candidate << " missing one or more required columns!";
                mtx.lock();
                loading_msg = oss.str();
                mtx.unlock();
                std::cerr << loading_msg << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(60000));
                return 0;
            }
        }
        else if (buffer[0] != '#')
        {
            int j=-1, HD, HIP;
            Star *s = nullptr;
            bool s_is_new = false;
            searched = false;
            Planet *p = nullptr;
            char *comma, *field = buffer;
            std::string planet_name = "", star_name = "", spectral_type = "";
            double p_incl=0, star_radius=0, star_mass=0, star_ra=0, star_decl=0, star_dist=0, star_vmag = 1e29, star_temp=sun_temp;
            for (i=0; strlen(field); i++)
            {
                comma = strchr(field, ',');
                if (comma) *comma = 0;
                if (i == col_plnm)
                {
                    planet_name = field;
                    if (p)
                    {
                        strcpy(p->name, planet_name.c_str());
                    }
                }
                else if (i == col_stnm)
                {
                    star_name = field;
                }
                else if (i == col_hd)
                {
                    if (field[0] == 'H' && field[1] == 'D') field += 2;
                    HD = atoi(field);
                    HIP=0;
                    if (!HD && !strcmp(star_name.substr(0,2).c_str(), "HD")) HD = atoi(star_name.substr(2).c_str());
                    if (!strcmp(star_name.substr(0,3).c_str(), "HIP")) HIP = atoi(star_name.substr(3).c_str());

                    if (HD && hdcache[HD]) s = hdcache[HD];
                    else if (HIP && hipcache && hipcache[HIP]) s = hipcache[HIP];
                    else if (!HD && !HIP)
                    {
                        // In case of multi-planet system, scan the last several objects for an EXACT name match.
                        for (j=0; j<10; j++)
                        {
                            CelestialObject *cel = cels[ncelobjs-1-j];
                            // if (cel->typeclass() != class_star) continue;
                            if (cel->cenobj && !strcmp(cel->cenobj->name, star_name.c_str()))
                            {
                                s = (Star*)cel->cenobj;
                                break;
                            }
                        }

                        if (!strcmp(star_name.c_str(), "55 Cnc B")) star_name = "GJ 324 B";
                        bool do_search = worth_searching(star_name);

                        if (!s && do_search)
                        {
                            searched = true;
                            j = find_object(star_name.c_str(), true);
                            if (j < 0)
                            {
                                std::cout << "Warning: failed to identify star " << star_name << "; adding from exoplanet catalog." << std::endl;
                                break;
                            }
                            if (cels[j]->typeclass() != class_star)
                            {
                                std::cerr << "ERROR: " << star_name << " matches " << cels[j]->name << " not a star." << std::endl;
                                break;
                            }
                            s = (Star*)cels[j];
                        }
                    }

                    if (!s)
                    {
                        s = new Star();
                        if (!s->has_custom_name) strcpy(s->name, star_name.c_str());
                        s->namelen = strlen(s->name);
                        s->cenobj = s;
                        s_is_new = true;
                    }

                    if (!p)
                    {
                        p = new Planet();
                        if (planet_name.size()) strcpy(p->name, planet_name.c_str());
                        p->namelen = strlen(p->name);
                    }
                    if (!p->orbit) p->orbit = new Orbit();
                    p->orbit->center = p->cenobj = s;
                    p->orbit->ascending_node = 0;           // unknown for exoplanets :(
                }
                else
                {
                    if (!s || !p)
                    {
                        std::cerr << "Columns out of sequence." << std::endl;
                        throw 0xbadda7a;
                    }
                    else if (trim(field).size())
                    {
                        if (i == col_orbper)
                        {
                            p->orbit->period = atof(field) * oneday;
                            if (!p->orbit->period)
                            {
                                delete p->orbit;
                                delete p;
                                p = nullptr;
                                break;
                            }
                        }
                        else if (i == col_sma) p->orbit->semimajor_axis = atof(field) * AU;
                        else if (i == col_rade) p->volumetric_mean_radius = atof(field) * earth_radius;
                        else if (i == col_radj) p->volumetric_mean_radius = atof(field) * jupiter_radius;
                        else if (i == col_mass_e) p->mass = atof(field) * earth_mass;
                        else if (i == col_mass_j) p->mass = atof(field) * jupiter_mass;
                        else if (i == col_eccn) p->orbit->eccentricity = atof(field);
                        else if (i == col_incl)
                        {
                            p_incl = atof(field) * fiftyseventh;
                        }
                        else if (i == col_periepo)
                        {
                            p->orbit->epoch = atof(field);
                            p->orbit->mean_anomaly = 0;
                        }
                        else if (i == col_argperi) p->orbit->arg_periapsis = atof(field) * fiftyseventh;
                        else if (i == col_oblt) p->obliquity = atof(field) * fiftyseventh;
                        else if (i == col_sptp) spectral_type = field;
                        else if (i == col_srad) star_radius = atof(field) * solar_radius;
                        else if (i == col_smass) star_mass = atof(field) * solar_mass;
                        else if (i == col_stemp) star_temp = atof(field);
                        else if (i == col_ra) star_ra = atof(field) * fiftyseventh;
                        else if (i == col_decl) star_decl = atof(field) * fiftyseventh;
                        else if (i == col_dist) star_dist = atof(field) * parsec;
                        else if (i == col_vmag) star_vmag = atof(field);
                    }
                }

                if (!comma) break;
                field = comma+1;
            }

            if (s)
            {
                if (!star_dist || (star_vmag > 1e28))
                {
                    if (s_is_new)
                    {
                        if (s->multisys)
                        {
                            s->multisys->unlink();
                            delete s->multisys;
                        }
                        delete s;
                    }
                    if (p) delete p;
                    continue;
                }

                s->type = star;
                if (!s->has_custom_name) strcpy(s->name, star_name.c_str());
                s->namelen = strlen(s->name);
                p->orbit->center = p->cenobj = s;

                s->right_ascension = star_ra;
                s->declination = star_decl;
                s->distance = star_dist;
                s->distance_known = true;
                s->mass = star_mass;
                s->estimate_BV(star_temp);
                s->estimate_UB(star_temp);
                s->volumetric_mean_radius = star_radius;
                assert(!isinf(s->volumetric_mean_radius));
                s->apparent_magnitude = star_vmag;
                double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
                s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
                strcpy(s->spectral_type, spectral_type.c_str());

                if (!s->mass) s->estimate_mass();
                if (!s->volumetric_mean_radius) s->estimate_radius();

                s->is_really_truly_in_visible_box(cels[0]->location);

                if (s_is_new)
                {
                    append_cel(s);
                    s->estimate_BV(star_temp);
                    s->estimate_UB(star_temp);
                    offset++;
                    if (offset >= max-1)
                    {
                        fclose(fp);
                        return num_added;
                    }
                }

                p->orbit->inclination = p_incl * fiftyseventh;             // For now.
            }

            if (s && p && p->orbit->period)
            {
                p->cenobj = s;
                bool HZ = p->is_in_con_HZ();

                // Star's planet-hosting metrics.
                s->has_planets++;
                if (HZ) s->has_hz_planets++;
                s->pl_indices.push_back(p->seqno);

                // Show distance to planet on mouse hover.
                p->distance_known = true;

                // Estimate semimajor axis if unknown.
                if (!p->orbit->semimajor_axis) p->orbit->compute_semimajor_axis(p->mass);

                // Estimate planet type.
                p->classify(HZ, p->mass && p->volumetric_mean_radius);

                // Estimate radius if unknown.
                if (!p->volumetric_mean_radius) p->estimate_radius();

                // Estimate albedo and absolute magnitude.
                p->estimate_albedo_and_absmagn();
                p->set_color_from_type(HZ);

                // Estimate planet rotation.
                p->estimate_rotation();

                // Estimate atmosphere and rings.
                p->setup_atm_ring_props();

                // Add planet to celestial bodies array.
                append_cel(p);
                offset++;
                num_added++;

                if (planet_celids.find(s->seqno) == planet_celids.end())
                    planet_celids[s->seqno] = std::vector<int>();
                planet_celids[s->seqno].push_back(p->seqno);

                std::stringstream lmss1;
                lmss1 << "Loaded " << num_added << " exoplanets from " << path << "...";
                mtx.lock();
                loading_msg = lmss1.str();
                mtx.unlock();
                if (offset >= max-1)
                {
                    fclose(fp);
                    return num_added;
                }
            }
            else delete p;
        }

        wasfirst = buffer[0];
    }

    fclose(fp);

    apply_exoplanet_names(planet_celids);

    return num_added;
}

int CatalogReader::read_starname_dat(CelestialObject **cels)
{
    std::string path = "starname.dat";
    char buffer[1024];
    char field[32];
    int num_read = 0;
    int i;
    uint32_t HD, HIP;
    std::string Gliese;

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;

    while (fgets(buffer, 1020, fp))
    {
        if (buffer[0] == '#') continue;

        read_field_onebased(buffer, 26, 32, field);
        HD = atoi(field);

        read_field_onebased(buffer, 34, 39, field);
        HIP = atoi(field);

        read_field_onebased(buffer, 41, 48, field);
        i = atoi(field);
        Gliese = trim(field);
        if (i >= 9000) Gliese = std::string("GJ ") + Gliese;
        else if (i >= 3000) Gliese = std::string("GJ ") + Gliese;
        else if (i >= 1000) Gliese = std::string("GJ ") + Gliese;
        else Gliese = std::string("GJ ") + Gliese;

        read_field_onebased(buffer, 1, 25, field);

        for (i=0; cels[i]; i++)
        {
            if (cels[i]->type != star) continue;
            Star* s = (Star*)cels[i];
            if ((HD && s->HD == HD) || (HIP && s->HIP == HIP) || (Gliese.size() && !strcmp(s->Gliese, Gliese.c_str())))
            {
                // A star can already carry a hardcoded custom name (e.g. 55 Cnc B) set upstream of
                // this pass; that outranks starname.dat's own entries, so leave it alone.
                if (s->has_custom_name) break;

                strcpy(s->name, trim(field).c_str());
                s->namelen = strlen(s->name);
                s->has_custom_name = true;
                num_read++;

                if (s->multisys && s->multisys->get_member('A') == s)
                {
                    Star* companion;
                    for (char c = 'B'; (companion = s->multisys->get_member(c)); c++)
                    {
                        if (companion->has_custom_name) continue;

                        // Only borrow "<primary> <letter>" when the companion has no identity of
                        // its own, or it's identical to the primary's -- never when it already
                        // reads differently.
                        companion->assign_identifier_name();
                        std::string base = lop_component(s->name);
                        if (!trim(companion->name).size() || !strcmp(trim(companion->name).c_str(), base.c_str()))
                            strcpy(companion->name, (base + std::string(" ") + std::string(1, c)).c_str() );
                        companion->namelen = strlen(companion->name);
                    }
                }

                break;
            }
        }
    }

    return num_read;
}

int CatalogReader::read_star_orbits_dat(CelestialObject **cels, int first_host)
{
    std::lock_guard<std::recursive_mutex> catalog_lock(catalog_mutex);
    std::string path = "catalogs" _FILESLASH "star_orbits.dat";
    char buffer[1024];
    char field[32];
    int num_read = 0;
    double f;

    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;

    Star *A, *s;
    std::string str;
    int l;
    char last = 0, nxtlast = 0;
    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);
    numfails = 0;

    ExactStarSearchIndex orbit_lookup(first_host);

    while (fgets(buffer, 1020, fp))
    {
        if (*buffer == '#') continue;
        if (!trim(buffer).size()) continue;

        read_field_onebased(buffer, 1, 23, field);
        std::string cenname = trim(field);
        const char* censtr = cenname.c_str();
        A = nullptr;
        // Catalog rows can only attach to exact identifiers or aliases, never fuzzy guesses.
        int sioxt = find_object(censtr, true, 9e29, 1, &orbit_lookup);
        if (sioxt >= 0)
        {
            A = (Star*)cels[sioxt];
        }

        if (!A)
        {
            if (showfails) std::cerr << "Warning: " << censtr << " not found in star catalog." << std::endl;
            continue;
        }

        if (A->seqno < first_host) continue;

        read_field_onebased(buffer, 65, 75, field);
        double ascending_node = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 77, 87, field);
        double inclination = atof(field) * fiftyseventh;
        if (!inclination) inclination = _pi/2;

        read_field_onebased(buffer, 25, 47, field);
        std::string bdyname = trim(field);
        const char* bdystr = bdyname.c_str();

        Rotation new_orbital_plane = system_plane_from_incl_and_node(inclination, ascending_node,
                                     A->location.system_center - cels[0]->location.system_center);
        if (inclination || ascending_node)
        {
            if (!strcmp(bdystr, "(stellar rotation)"))
            {
                A->location.equatorial_plane = new_orbital_plane;
                A->lock_equatorial_plane = true;
                A->obliquity = inclination;
                A->equinox = ascending_node;
            }
            else
            {
                if (!A->lock_system_plane)
                {
                    A->location.local_system_plane = new_orbital_plane;
                    A->location.orbital_plane = A->location.local_system_plane;
                    A->lock_system_plane = true;
                }
                if (!A->lock_equatorial_plane && !A->rot_axis_known)
                {
                    A->location.equatorial_plane = A->location.local_system_plane;
                    A->obliquity = 0;
                }
            }
            A->known_poles = true;
        }

        s = nullptr;
        if (!strcmp(bdystr, "(system inclination)")
                || strstr(bdystr, " disk") || strstr(bdystr, " disc")
                || strstr(bdystr, " belt")
                || !strcmp(bdystr, "(companion orbit)")
           )
        {
            A->has_disk = A->known_poles;
            s = A;
        }
        if (s == A && A->has_disk)
        {
            A->disk_heliocen_inclination = inclination;
            A->disk_heliocen_node = ascending_node;

            read_field_onebased(buffer, 101, 111, field);
            str = trim(field);
            nxtlast = last = '\0';
            l = str.size();
            if (l) last = str.c_str()[l-1];
            if (l>1) nxtlast = str.c_str()[l-2];
            double disk_sma;
            if (nxtlast == 'A' && last == 'U') disk_sma = atof(field) * AU;
            else if (last == 's') disk_sma = atof(field) * (AU / parsec) * A->distance;
            else disk_sma = atof(field);
            if (disk_sma) A->disk_inner_edge_sma = disk_sma;
            s = A;
        }

        if (!strcmp(bdystr, "(stellar rotation)"))
        {
            read_field_onebased(buffer, 49, 63, field);
            str = trim(field);
            last = '\0';
            l = str.size();
            if (l) last = str.c_str()[l-1];
            if (last == 'y') A->sidereal_rotational_period = atof(field) * oneyear;
            else if (last == 'd') A->sidereal_rotational_period = atof(field) * oneday;
            else A->sidereal_rotational_period = atof(field);
            A->rot_heliocen_incl = inclination;
            A->rot_heliocen_node = ascending_node;
            s = A;
        }

        if (bdystr[0] == '(') continue;

        if (!A)
        {
            if (showfails) std::cerr << "FAILED to orbit " << bdyname << " around " << cenname << ": center not found." << std::endl;
            numfails++;
            continue;
        }

        if (!s && A->multisys)
        {
            char comp = bdystr[strlen(bdystr)-1];
            if (comp > 'A' && comp <= 'Z') s = A->multisys->get_member(comp);
        }
        if (!s)
        {
            sioxt = find_object(bdystr, true, 9e29, 1, first_host ? nullptr : &orbit_lookup);
            if (sioxt >= 0)
            {
                s = (Star*)cels[sioxt];
            }
        }

        if (s && s != A && s->distance > 0 && A->distance > 0)
        {
            Point primary_direction = Point::from_ra_dec(A->right_ascension, A->declination, 1);
            Point member_direction = Point::from_ra_dec(s->right_ascension, s->declination, 1);
            const double cosine = primary_direction.x * member_direction.x +
                primary_direction.y * member_direction.y + primary_direction.z * member_direction.z;
            const Point separation = primary_direction * A->distance - member_direction * s->distance;
            // Preserve wide nearby systems, but reject stars far apart both on the sky and in space.
            if (cosine < cos(fiftyseventh) && separation.magnitude() > parsec)
            {
                if (showfails) std::cerr << "Rejected orbit for " << bdyname << " around " << cenname
                    << ": catalog positions describe different systems." << std::endl;
                numfails++;
                continue;
            }
        }

        bool s_is_new = false;
        int bs = trim(buffer).size();
        if (bs >= 180)
        {
            if (!s)
            {
                s = new Star();
                append_cel(s);
                s_is_new = true;
            }

            // Orbit identifiers locate objects; existing display names retain their priority.
            if (s_is_new)
            {
                strcpy(s->name, bdyname.c_str());
                s->has_custom_name = bdyname.rfind("HD", 0) != 0;
            }
            s->namelen = strlen(s->name);
            if (s_is_new && bdyname.rfind("HD", 0) == 0)
            {
                char *number_end = nullptr;
                const unsigned long hd = strtoul(bdystr + 2, &number_end, 10);
                if (hd > 0 && hd <= MAX_HD && number_end && *number_end == '\0')
                {
                    s->HD = hd;
                    if (hdcache)
                    {
                        hdcache[hd] = s;
                    }
                }
            }
            s->distance_known = true;
            read_field_onebased(buffer, 161, 175, field);
            strcpy(s->spectral_type, trim(field).c_str());
            read_field_onebased(buffer, 177, 191, field);
            f = atof(field);
            const bool has_visual_magnitude = !trim(field).empty();
            if (has_visual_magnitude) s->absolute_magnitude = f;
            double msun=0, rsun=0, lum=0, tempK=0;
            bool has_bolometric_luminosity = false;
            if (bs >= 193)
            {
                read_field_onebased(buffer, 193, 203, field);
                msun = atof(field);
                s->mass = msun * solar_mass;
            }
            if (bs >= 205)
            {
                read_field_onebased(buffer, 205, 215, field);
                rsun = atof(field);
                s->volumetric_mean_radius = rsun * solar_radius;
                assert(!isinf(s->volumetric_mean_radius));
            }
            if (bs >= 217)
            {
                read_field_onebased(buffer, 217, 227, field);
                lum = atof(field);
                has_bolometric_luminosity = lum > 0;
            }
            if (bs >= 229)
            {
                read_field_onebased(buffer, 229, 238, field);
                tempK = atof(field);
                // if (A->HD == 47152) std::cout << s->name << " tempK=" << tempK << std::endl;
                if (tempK)
                {
                    s->temperature = tempK;
                    s->estimate_BV(tempK);
                    s->estimate_UB(tempK);
                }
            }

            const bool missing_visual_magnitude = !has_visual_magnitude &&
                (!std::isfinite(s->absolute_magnitude) || fabs(s->absolute_magnitude) >= 99 ||
                    (s->absolute_magnitude == 0 && s->apparent_magnitude >= 99));
            if (s_is_new || missing_visual_magnitude)
            {
                double mseqi = 0;
                int mseqn = 0;

                double mseqim=0, mseqir=0, mseqil=0, mseqit=0;
                if (msun)
                {
                    mseqi += (mseqim = Star::get_mseqidx_from_mass(msun));
                    mseqn++;
                }
                if (rsun)
                {
                    mseqi += (mseqir = Star::get_mseqidx_from_rad(rsun));
                    mseqn++;
                }
                if (lum)
                {
                    mseqi += (mseqil = Star::get_mseqidx_from_lum(lum));
                    mseqn++;
                }
                if (tempK)
                {
                    mseqi += (mseqit = Star::get_mseqidx_from_temp(tempK));
                    mseqn++;
                }

                if (mseqn)
                {
                    mseqi /= mseqn;
                    // Filter out white dwarfs and brown dwarfs.
                    if (
                        (!msun  || fabs(mseqim - mseqi) < 7 )
                        && (!rsun  || fabs(mseqir - mseqi) < 7 )
                        && (!lum   || fabs(mseqil - mseqi) < 7 )
                        && (!tempK || fabs(mseqit - mseqi) < 7 )
                    )
                    {
                        if (!msun ) s->mass = Star::interpolate_mseq_mass(mseqi) * solar_mass;
                        if (!rsun ) s->volumetric_mean_radius = Star::interpolate_mseq_rad(mseqi) * solar_radius;
                        if (!lum) lum = Star::interpolate_mseq_lum(mseqi);
                        if (!tempK)
                        {
                            tempK = Star::interpolate_mseq_temp(mseqi);
                            s->temperature = tempK;
                            s->estimate_BV(tempK);
                            s->estimate_UB(tempK);
                        }
                    }
                }
            }
            // Published radius and temperature override a main-sequence guess.
            if (!has_bolometric_luminosity && rsun > 0 && tempK > 0)
            {
                lum = rsun * rsun * pow(tempK / sun_temp, 4);
            }
            // Luminosities are bolometric; the cache stores absolute visual magnitudes.
            // Explicit visual measurements, including zero, always take priority.
            if (!has_visual_magnitude && lum > 0)
            {
                const double effective_temperature = tempK > 0 ? tempK : s->estimate_temperature();
                s->absolute_magnitude = 4.74 - log(lum) * invlogmagnbase
                    - Star::bolometric_correction(effective_temperature);
            }
        }
        else if (!s || s == A)
        {
            if (showfails) std::cerr << "FAILED to orbit " << bdyname << " around " << cenname << ": member not found and insufficient data to construct new." << std::endl;
            numfails++;
            continue;
        }

        if (A->HD == 20766)
        {
            std::cerr << "BAD! 2585" << std::endl;
            throw 0xbadc0de;
        }
        if (!s->orbit) s->orbit = new Orbit();
        s->orbit->center = A;
        s->orbit->heliocentric_inclination = inclination;
        s->orbit->heliocentric_node = ascending_node;

        char comp = 0;
        if (!A->multisys)
        {
            A->set_component('A', A);
        }
        else
        {
            comp = A->multisys->is_member(s);
        }

        if (!comp)
        {
            comp = 'B';
            while (comp <= 'Z' && A->multisys->get_member(comp))
            {
                comp++;
            }
        }
        if (comp <= 'Z')
        {
            s->make_companion_of(A, comp);
            const std::string primary_id = std::string("HD") + std::to_string(A->HD);
            if (!s->has_custom_name && A->HD && bdyname == primary_id + " " + comp
                && (!s->HD || s->HD == A->HD) && s->BayerGrkno < 0
                && s->FlamsteedNo <= 0 && s->GouldNo <= 0 && !strlen(s->Gliese))
            {
                const std::string display = lop_component(A->name) + " " + comp;
                strcpy(s->name, display.c_str());
                s->namelen = display.size();
                s->origname = display;
            }
        }

        read_field_onebased(buffer, 49, 63, field);
        str = trim(field);
        last = '\0';
        l = str.size();
        if (l) last = str.c_str()[l-1];
        if (last == 'y') f = atof(field) * oneyear;
        else if (last == 'd') f = atof(field) * oneday;
        else f = atof(field);
        if (f && s->orbit) s->orbit->period = f;

        read_field_onebased(buffer, 89, 99, field);
        f = atof(field) * fiftyseventh;
        if (f && s->orbit) s->orbit->arg_periapsis = f;

        read_field_onebased(buffer, 101, 111, field);
        str = trim(field);
        nxtlast = last = '\0';
        l = str.size();
        if (l) last = str.c_str()[l-1];
        if (l>1) nxtlast = str.c_str()[l-2];
        if (nxtlast == 'A' && last == 'U') f = atof(field) * AU;
        else if (last == 's') f = atof(field) * (AU / parsec) * A->distance;
        else f = atof(field);
        if (f && s->orbit) s->orbit->semimajor_axis = f;

        read_field_onebased(buffer, 113, 123, field);
        f = atof(field);
        if (f && s->orbit) s->orbit->eccentricity = f;

        read_field_onebased(buffer, 125, 143, field);
        f = atof(field) * fiftyseventh;
        if (f && s->orbit) s->orbit->mean_anomaly = f;

        read_field_onebased(buffer, 145, 160, field);
        str = trim(field);
        l = str.size();
        if (l)
        {
            if (str.c_str()[0] == 'J' && str.c_str()[1] == 'D') f = atof(&field[2]);
            else f = (atof(field)-2000) * (oneyear / oneday) + J2000;
            if (f && s->orbit) s->orbit->epoch = f;
        }

        if (inclination || ascending_node)
        {
            s->location.equatorial_plane = s->location.orbital_plane = s->location.local_system_plane = new_orbital_plane;
            // std::cout << "Set " << s->name << " incl=" << (inclination * fiftyseven) << " node=" << (ascending_node * fiftyseven) << std::endl;

            if (!A->multisys || s == A->multisys->get_member('B'))
            {
                A->location.equatorial_plane = A->location.orbital_plane = A->location.local_system_plane = new_orbital_plane;
                // std::cout << "Also set " << A->name << " incl=" << (inclination * fiftyseven) << " node=" << (ascending_node * fiftyseven) << std::endl;
            }

            s->lock_system_plane = true;
            s->lock_equatorial_plane = true;
            s->obliquity = 0;
            s->equinox = 0;
            A->known_poles = s->known_poles = true;
        }

        // Later rows can refer to a companion just created or renamed above.
        orbit_lookup.add(A->seqno, *A);
        orbit_lookup.add(s->seqno, *s);
        num_read++;
    }
    fclose(fp);

    if (numfails && !showfails) std::cout << "There were " << numfails
        << " star orbits that could not be applied. To see them, re-run Alienorum with the `orbfails` command like argument."
        << std::endl;

    return num_read;
}

static void apply_body_metadata(CelestialObject* body, const json& record)
{
    body->home_world = record.value("home_world", body->home_world);
    body->map_inverted = record.value("map_inverted", body->map_inverted);
    body->orbit_center_codes = record.value("orbit_center_codes", body->orbit_center_codes);
    body->known_poles = record.value("known_poles", body->known_poles);
    if (body->typeclass() == class_star && record.contains("catalog_aliases"))
    {
        for (const auto& alias : record.at("catalog_aliases")) static_cast<Star*>(body)->add_catalog_alias(alias.get<std::string>());
    }
}

void CatalogReader::apply_local_catalog_metadata(CelestialObject** objects)
{
    std::ifstream file("catalogs/planets.json");
    if (!file || !objects) return;
    json records;
    file >> records;
    for (const auto& record : records)
    {
        const std::string name = record.value("BODYNAME", "");
        for (int i = 0; objects[i]; ++i)
        {
            if (name == objects[i]->name) apply_body_metadata(objects[i], record);
        }
    }
}

int CatalogReader::read_local_planets(CelestialObject **cels, int max, CelestialObject* must_orbit, CelestialObject* mustnt_orbit)
{
    std::fstream fs(std::string("catalogs" _FILESLASH "planets.json"), std::ios::in);
    if (!fs) throw 0xbadf12e;
    int result = 0, offset;
    json planets;
    fs >> planets;
    int i, j, k, n = planets.size();
    bool createnew;
    Planet *p;
    Moon *m = nullptr;

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);
    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    std::unordered_map<std::string, int> center_indices;

    for (i=0; i<n; i++)
    {
        json pl = planets[i];
        std::string bodyname, cenname, mapurl;
        m = nullptr;
        try
        {
            pl.at("BODYNAME").get_to(bodyname);
            cenname = "";
            try
            {
                pl.at("CENTER_OF_ORBIT").get_to(cenname);
            }
            catch (...) { ; }
            j = cenname.size() ? -1 : find_object(bodyname.c_str(), false, 9e+29, 0);
            k = -1;
            if (cenname.size())
            {
                const auto cached = center_indices.find(cenname);
                k = cached != center_indices.end() ? cached->second : find_object(cenname.c_str(), false, 9e+29, 0);
                // Successful matches remain first in catalog order as bodies are
                // appended. Retry absent centers after subsequent additions.
                if (k >= 0) center_indices.emplace(cenname, k);
            }
            if (must_orbit && (k<0 || must_orbit != cels[k])) continue;
            if (k>=0 && mustnt_orbit == cels[k]) continue;

            if (j < 0 || k >= 0)                // Name not taken or center of orbit,
            {
                // create new.
                if (k < 0) throw 0xbadda7a;     // Future expansion.
                if (cels[k]->type == galaxy)
                    throw 0xbadda7a;            // Future expansion.
                if (cels[k]->type == star)
                {
                    p = new Planet();
                    p->type = rocky;
                }
                else
                {
                    m = new Moon();
                    p = m;
                    p->type = rocky;
                }
                memset(p->name, 0, 32);
                strcpy(p->name, bodyname.c_str());
                p->namelen = strlen(p->name);
                if (k >= 0)
                {
                    p->orbit = new Orbit;
                    p->orbit->center = cels[k];
                }

                append_cel(p);
                offset++;
                result++;
                createnew = true;
            }
            else                                // Name taken and no center specified,
            {
                // update existing.
                p = (Planet*)cels[j];
                m = (p->typeclass() == class_moon) ? ((Moon*)p) : nullptr;
                createnew = false;
            }

            apply_body_metadata(p, pl);

            try
            {
                double ra, decl;
                pl.at("NorthPoleRA").get_to(ra);
                pl.at("NorthPoleDecl").get_to(decl);

                ra *= fiftyseventh;
                decl *= fiftyseventh;

                Point pole = Point::from_ra_dec(ra, decl, light_year*1e29, 0);
                p->location.equatorial_plane = align_points_3d(pole, yaxis, center);
                p->lock_equatorial_plane = true;
                p->known_poles = true;
            }
            catch (...) { ; }
            try { pl.at("ABSMG").get_to(p->absolute_magnitude); } catch (...) { ; }
            try { pl.at("ArgPeri").get_to(p->orbit->arg_periapsis); p->orbit->arg_periapsis *= fiftyseventh; } catch (...) { ; }
            try { pl.at("AscNode").get_to(p->orbit->ascending_node); p->orbit->ascending_node *= fiftyseventh; } catch (...) { ; }

            try { /* TODO: pl.at("BondAlbedo").get_to(p->BV_color); */ } catch (...) { ; }
            try { pl.at("BVmag").get_to(p->BV_color); } catch (...) { if (createnew) p->BV_color = p->orbit->center->BV_color; }
            try { pl.at("UBmag").get_to(p->UB_color); } catch (...) { if (createnew) p->UB_color = p->orbit->center->UB_color; }
            try { pl.at("CloudsStatic").get_to(p->static_clouds); } catch (...) { ; }
            try { pl.at("Eccentricity").get_to(p->orbit->eccentricity); } catch (...) { ; }
            try { pl.at("Epoch").get_to(p->epoch); p->epoch = J2000 + (p->epoch - 2000)*(oneyear/oneday); p->orbit->epoch = p->epoch; } catch (...) { ; }
            try { double pre; pl.at("EqPrecession").get_to(pre); p->precession = pre ? (_pi * 2 / pre / oneyear) : 0; } catch (...) { ; }
            try { double pre; pl.at("NodePrecession").get_to(pre); p->orbit->prec_node = pre ? (_pi * 2 / pre / oneyear) : 0; } catch (...) { ; }
            try { double pro; pl.at("ArgPeriProcession").get_to(pro); p->orbit->proc_argperi = pro ? (_pi * 2 / pro / oneyear) : 0; } catch (...) { ; }
            try { pl.at("Equinox").get_to(p->equinox); p->equinox *= fiftyseventh; } catch (...) { ; }
            try { pl.at("HasWater").get_to(p->has_water); } catch (...) { ; }
            try { pl.at("Incl").get_to(p->orbit->inclination); p->orbit->inclination *= fiftyseventh; } catch (...) { ; }
            try { pl.at("J2").get_to(p->J2); } catch (...) { ; }
            try { pl.at("Lon_J2000_offset").get_to(p->lon_J2000_offset); p->lon_J2000_offset *= fiftyseventh; } catch (...) { ; }
            try
            {
                pl.at("Mass").get_to(p->mass);
                p->mass *= 1000;
            } catch (...) { ; }
            try { pl.at("MeanAnom").get_to(p->orbit->mean_anomaly); p->orbit->mean_anomaly *= fiftyseventh; } catch (...) { ; }
            try { pl.at("Oblateness").get_to(p->oblateness); } catch (...) { ; }
            try { pl.at("Obliquity").get_to(p->obliquity); p->obliquity *= fiftyseventh; } catch (...) { ; }
            try { pl.at("OrbitPeriod").get_to(p->orbit->period); } catch (...) { ; }
            try { pl.at("Overcast").get_to(p->overcast); } catch (...) { ; }
            try { pl.at("RotationPeriod").get_to(p->sidereal_rotational_period); } catch (...) { ; }
            try { pl.at("SEMIMAJOR_AXIS").get_to(p->orbit->semimajor_axis); } catch (...) { ; }
            try { pl.at("SurfaceTemperature").get_to(p->temperature); } catch (...) { ; }
            // Atmosphere. Read into locals first and only build the object if the record really
            // carries something: most bodies in planets.json have no atmospheric keys at all and
            // must come out with atm == nullptr rather than the struct's one-atmosphere default.
            {
                bool has_atm = false;
                double a_pressure = 0, a_tau = 0, a_partic = 0;
                try
                {
                    pl.at("SurfacePressure").get_to(a_pressure);
                    has_atm = true;
                }
                catch (...) { ; }
                try
                {
                    pl.at("AtmosphericTau").get_to(a_tau);
                    has_atm = true;
                }
                catch (...) { ; }
                try
                {
                    pl.at("Particulates").get_to(a_partic);
                    has_atm = true;
                }
                catch (...) { ; }
                if (has_atm)
                {
                    Atmosphere *a = p->ensure_atmosphere();
                    if (!isinf(a_pressure)) a->surface_pressure = a_pressure;
                    a->tau = a_tau;
                    a->particulates = a_partic;
                }

                // A composition may sit at the top level of the record, alongside the flat keys
                // above, which is how planets.json spells it.
                try
                {
                    json jc = pl.at("AtmosphereComposition");
                    p->ensure_atmosphere()->ensure_composition()->from_json(jc);
                }
                catch (...) { ; }

                // Or the whole thing as one "Atmosphere" object, optionally holding a
                // "Composition". Read last so it wins over the flatter spellings above.
                try
                {
                    json ja = pl.at("Atmosphere");
                    p->ensure_atmosphere()->from_json(ja);
                }
                catch (...) { ; }
            }

            try { pl.at("PlantTrunkHeight").get_to(p->plant_trunk_height); } catch (...) { ; }
            try { pl.at("PlantTrunkWidth").get_to(p->plant_trunk_width); } catch (...) { ; }
            try { pl.at("PlantCrownSize").get_to(p->plant_crown_size); } catch (...) { ; }
            try { pl.at("PlantCrownOblateness").get_to(p->plant_crown_oblateness); } catch (...) { ; }
            
            try { pl.at("VolMeanRad").get_to(p->volumetric_mean_radius); } catch (...) { ; }
            try { pl.at("RingRadius").get_to(p->ring_radius); p->ring_radius *= 1000; p->ring_status_known = true; } catch (...) { ; }
            try { pl.at("TransparentClouds").get_to(p->transparent_clouds); } catch (...) { ; }
            try { pl.at("transparent_clouds").get_to(p->transparent_clouds); } catch (...) { ; }
            try { pl.at("vegetation_r").get_to(p->vegetation_r); } catch (...) { ; }
            try { pl.at("vegetation_g").get_to(p->vegetation_g); } catch (...) { ; }
            try { pl.at("vegetation_b").get_to(p->vegetation_b); } catch (...) { ; }
            // try { pl.at("").get_to(p->); } catch (...) { ; }

            if (m)
            {
                try { pl.at("Depth" ).get_to(m->depth ); m->depth  *= 1000; } catch (...) { ; }
                try { pl.at("Width" ).get_to(m->width ); m->width  *= 1000; } catch (...) { ; }
                try { pl.at("Height").get_to(m->height); m->height *= 1000; } catch (...) { ; }
                try { pl.at("Major" ).get_to(m->major_moon); } catch (...) { ; }
                if (!m->orbit) std::cout << "WARNING: " << m->name << " has no orbit." << std::endl << std::flush;
                if (m->orbit && !m->sidereal_rotational_period) m->sidereal_rotational_period = m->orbit->period;
                if (m->depth > zero_isnt_really_zero && m->width > zero_isnt_really_zero && m->height > zero_isnt_really_zero)
                    m->volumetric_mean_radius = pow(m->depth * m->width * m->height, 1.0/3.0) * 0.5;
                assert(!isinf(m->volumetric_mean_radius));
            }

            const char *mapkeys[6] = {"SurfMap", "CloudMap", "BumpMap", "NightMap", "RingColorMap", "RingTranspMap"};
            const char *mapsuffs[6] = {"_surf", "_clouds", "_bump", "_night", "_ring", "_ringx"};

            for (j=0; j<6; j++)
            {
                try
                {
                    pl.at(mapkeys[j]).get_to(mapurl);
                    if (mapurl.c_str())
                    {
                        std::string destdir = (std::string)"maps/";
                        std::string destfname;
                        if (!strcasecmp(mapurl.substr(mapurl.size()-4).c_str(), ".png"))
                            destfname = destdir + std::string(p->name) + std::string(mapsuffs[j]) + std::string(".png");
                        else destfname = destdir + std::string(p->name) + std::string(mapsuffs[j]) + std::string(".jpg");
                        if (!strcmp(mapkeys[j], "CloudMap"))
                        {
                            p->cloud_map_url = mapurl;
                            check_and_download_clouds(mapurl, destfname, p->static_clouds);
                        }
                        else if (!file_exists(destfname.c_str()))
                        {
                            download_file(mapurl, destfname);
                        }
                    }
                }
                catch (...) { ; }
            }

            if (p->orbit && p->orbit->center && createnew)
            {
                p->known_poles = p->known_poles || (p->obliquity && p->equinox);
                p->location = p->orbit->center->location;           // Copy the system center and local plane. The local position will auto-fill later.
                p->location.equatorial_plane.a = p->obliquity;
                p->location.equatorial_plane.v = Point(std::sin(p->equinox), 0, -std::cos(p->equinox));
                p->classify(p->is_in_con_HZ(), true, true);

                // Checked before the cast: has_planets and has_hz_planets live past the end of
                // any class smaller than Star, and get_light_center() does not promise a star.
                CelestialObject* lc = p->get_light_center();
                Star* s = (lc && lc->typeclass() == class_star) ? (Star*)lc : nullptr;
                // std::cout << p->name << " cosmic shoreline = " << CosmicShore::calculate_unified_metric(*s, *p) << std::endl;
                if (s && p->orbit->center == s)
                {
                    s->has_planets++;
                    if (p->is_in_con_HZ()) s->has_hz_planets++;
                    s->pl_indices.push_back(p->seqno);
                }
            }

            if (p->orbit && p->orbit->center)
            {
                std::string oscname = std::string(p->orbit->center->name) + std::string(".") + std::string(p->name);
                if (p->orbit && p->orbit->center) p->orbit->read_osc_elements(oscname);
            }
        }
        catch (...)
        {
            continue;
        }
    }

    return result;
}

static std::string star_orbits_signature()
{
    std::ifstream source("catalogs/star_orbits.dat", std::ios::binary);
    if (!source) return {};
    uint64_t hash = 14695981039346656037ULL;
    char c;
    while (source.get(c))
    {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return std::to_string(hash);
}

static std::string baked_star_link_key(const json& key)
{
    if (key.is_string()) return key.get<std::string>();
    const int index = key.get<int>();
    return index < 0 ? std::string() : "N:" + std::to_string(index);
}

void alienorum::CatalogReader::write_condensed_star_cat_line(FILE *fp, Star *s)
{
    // These one-based ranges match read_condensed_star_cat(); SC2 fixes the metadata tail too.
    std::string line(655, ' ');
    const auto put = [&](size_t first, size_t last, const std::string &value)
    {
        if (value.size() > last - first + 1)
            throw std::runtime_error("Star catalog field overflow at column " + std::to_string(first) + ": " + value);
        line.replace(first - 1, value.size(), value);
    };
    const auto number = [](double value, int precision = 4, bool scientific = true)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        if (scientific) out << std::scientific;
        else out << std::fixed;
        out << std::setprecision(precision) << value;
        return out.str();
    };
    const auto magnitude = [&](double value)
    {
        if (!std::isfinite(value) || fabs(value) >= 99) return std::string(value < 0 ? "-" : "+");
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::showpos << std::internal << std::setfill('0') << std::setw(6)
            << std::fixed << std::setprecision(2) << value;
        return out.str();
    };
    put(1, 14, s->alienorumid);
    put(16, 54, s->name);
    put(56, 65, s->RA_as_hms(0));
    put(67, 75, s->Decl_as_degms());
    put(77, 82, magnitude(s->apparent_magnitude));
    put(84, 98, s->spectral_type);
    if (!s->BV_color && s->WD.empty()) s->estimate_BV(s->estimate_temperature());
    if (!s->UB_color && s->WD.empty()) s->estimate_UB(s->estimate_temperature());
    put(100, 105, magnitude(s->BV_color));
    put(107, 112, magnitude(s->UB_color));
    if (s->HD) put(114, 119, std::to_string(s->HD));
    if (s->HIP) put(121, 126, std::to_string(s->HIP));
    if (s->HR) put(128, 133, std::to_string(s->HR));
    if (s->SB9) put(135, 140, std::to_string(s->SB9));
    if (s->FlamsteedNo > 0) put(142, 144, std::to_string(s->FlamsteedNo));
    put(146, 152, s->Bayer);
    put(154, 168, s->Gliese);
    if (s->GouldNo > 0) put(170, 172, std::to_string(s->GouldNo));
    put(174, 176, s->Gouldcons);
    put(178, 188, number(s->proper_motion_RA));
    put(190, 200, number(s->proper_motion_decl));
    put(202, 212, number(s->radial_velocity));
    put(214, 224, number(s->parallax));
    put(226, 236, number(s->distance / light_year));
    if (!s->absolute_magnitude)
    {
        const double brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
        s->absolute_magnitude = -log(brightness) * invlogmagnbase;
    }
    put(238, 243, magnitude(s->absolute_magnitude));
    if (s->mass) put(245, 255, number(s->mass / solar_mass));
    if (s->volumetric_mean_radius) put(257, 267, number(s->volumetric_mean_radius / solar_radius));
    if (s->temperature) put(269, 274, std::to_string(static_cast<int>(s->temperature)));
    if (s->sidereal_rotational_period) put(276, 285, number(s->sidereal_rotational_period / oneday, 3, false));
    if (s->orbit)
    {
        const Orbit &orbit = *s->orbit;
        if (orbit.center) put(287, 300, static_cast<Star*>(orbit.center)->alienorumid);
        if (orbit.period) put(302, 313, number(orbit.period / oneday, 5));
        if (orbit.semimajor_axis) put(315, 326, number(orbit.semimajor_axis / AU, 5));
        if (orbit.eccentricity) put(328, 339, number(orbit.eccentricity, 5, false));
        const auto angle = [&](size_t first, size_t last, double radians)
        {
            if (radians) put(first, last, number(fmod(fmod(radians * fiftyseven, 360) + 360, 360), 4, false));
        };
        angle(341, 348, orbit.arg_periapsis);
        angle(350, 357, orbit.mean_anomaly);
        if (orbit.epoch && (orbit.mean_anomaly || fabs(orbit.epoch - J2000) > 0.001)) put(359, 372, number(orbit.epoch, orbit.epoch < 0 ? 7 : 8));
        angle(374, 381, orbit.heliocentric_inclination);
        angle(383, 390, orbit.heliocentric_node);
    }
    if (s->variability_period) put(392, 403, number(s->variability_period / oneday, 5));
    if (s->minmag) put(405, 410, magnitude(s->minmag));
    if (s->maxmag) put(412, 417, magnitude(s->maxmag));
    if (s->variability_period && s->epoch_max_brightness) put(419, 435, number(s->epoch_max_brightness, 4, false));
    if (s->variability_period && s->is_eclipsing_binary) put(437, 437, "E");
    if (s->Bonn_survey[0])
    {
        put(439, 440, std::string(s->Bonn_survey, 2));
        put(442, 445, (s->Bonn_survey_declination < 0 ? "-" : "+") + std::to_string(abs(s->Bonn_survey_declination)));
        put(447, 452, std::to_string(s->Bonn_survey_sequential));
    }
    if (s->has_custom_name) put(454, 454, "Y");
    put(456, 467, s->WD);
    put(469, 471, "SC2");
    put(473, 512, s->gaia_wd_name);
    put(514, 533, std::to_string(s->gaia_source_id));
    put(535, 552, number(s->gaia_g_magnitude, 10));
    put(554, 554, s->apparent_magnitude_estimated ? "1" : "0");
    put(556, 556, s->wd_color_valid ? "1" : "0");
    put(558, 560, std::to_string(s->wd_color_red));
    put(562, 564, std::to_string(s->wd_color_green));
    put(566, 568, std::to_string(s->wd_color_blue));
    put(570, 601, s->wd_color_source);
    put(603, 634, s->wd_temperature_source);
    put(636, 655, std::to_string(s->wd_color_revision));
    const auto link_key = [](const Star* object)
    {
        if (cels && object->seqno >= 0 && object->seqno < MAX_CELOBJS && cels[object->seqno] == object)
            return "N:" + std::to_string(object->seqno);
        return object->alienorumid.empty() ? "D:" + std::string(object->name) : "I:" + object->alienorumid;
    };
    std::ostringstream identity;
    identity << " SID1 " << std::quoted(link_key(s));
    line += identity.str();
    if (s->stellar_abundances.is_object() && !s->stellar_abundances.empty())
    {
        std::ostringstream extra;
        extra << " CHEM1 " << std::quoted(s->stellar_abundances.dump());
        line += extra.str();
    }
    if (std::isfinite(s->metallicity))
    {
        const json measurement = json::array(
        {
            s->metallicity, s->metallicity_ratio,
            std::isfinite(s->metallicity_error_upper) ? json(s->metallicity_error_upper) : json(nullptr),
            std::isfinite(s->metallicity_error_lower) ? json(s->metallicity_error_lower) : json(nullptr),
            s->metallicity_source
        });
        std::ostringstream extra;
        extra << " MET1 " << std::quoted(measurement.dump());
        line += extra.str();
    }
    if (s->orbit || s->multisys || s->known_poles || s->has_disk || s->rot_axis_known ||
        s->lock_system_plane || s->lock_equatorial_plane)
    {
        const auto rotation = [](const Rotation& plane)
        {
            return json::array({plane.v.x, plane.v.y, plane.v.z, plane.a});
        };
        const int flags = (s->known_poles ? 1 : 0) | (s->lock_system_plane ? 2 : 0) |
            (s->lock_equatorial_plane ? 4 : 0) | (s->has_disk ? 8 : 0) |
            (s->rot_axis_known ? 16 : 0) | (s->distance_known ? 32 : 0);
        // Positional extension: identity, center, component, flags, three rotations,
        // spin angles, disk geometry, observer-frame spin, period, and full elements.
        json state = json::array({link_key(s), s->orbit && s->orbit->center ? link_key(static_cast<Star*>(s->orbit->center)) : std::string(),
            static_cast<int>(s->get_component()), flags, rotation(s->location.local_system_plane),
            rotation(s->location.orbital_plane), rotation(s->location.equatorial_plane),
            s->obliquity, s->equinox, s->disk_heliocen_inclination, s->disk_heliocen_node,
            s->disk_inner_edge_sma, s->rot_heliocen_incl, s->rot_heliocen_node,
            s->sidereal_rotational_period, nullptr, s->orbit ? s->orbit->center_name : std::string()});
        if (s->orbit)
        {
            state[15] = json::array({s->orbit->period, s->orbit->semimajor_axis,
                s->orbit->eccentricity, s->orbit->arg_periapsis, s->orbit->mean_anomaly,
                s->orbit->epoch, s->orbit->heliocentric_inclination, s->orbit->heliocentric_node,
                s->orbit->inclination, s->orbit->ascending_node});
        }
        std::ostringstream extra;
        extra << " ORB1 " << std::quoted(state.dump());
        line += extra.str();
    }
    if (!s->catalog_aliases.empty())
    {
        std::ostringstream aliases;
        aliases << " ALS1";
        for (const auto &alias : s->catalog_aliases) aliases << ' ' << std::quoted(alias);
        line += aliases.str();
    }
    line += '\n';
    fputs(line.c_str(), fp);
}

int alienorum::CatalogReader::write_condensed_star_cat(ConsBins cb)
{
    int i, n, written = 0;
    char c;
    std::string path = get_condensed_starcat_name();
    FILE *fp = fopen(path.c_str(), "w");
    if (fp)
    {
        fprintf(fp, "# ORBITS1 %s\n", star_orbits_signature().c_str());
        write_condensed_star_cat_line(fp, (Star*)cels[0]);

        for (const auto& [cs, val1] : cb)
        {
            for (const auto& [m, val2] : val1)
            {
                for (const auto& [cc, val3] : val2)
                {
                    n = val3.size();
                    for (i=0; i<n; i++)
                    {
                        if (val3[i]->multisys)
                        {
                            for (c = 'A'; c <= 'Z'; c++)
                            {
                                Star *B = val3[i]->multisys->get_member(c);
                                if (B && B->get_component() == c) write_condensed_star_cat_line(fp, B);
                            }
                        }
                        else write_condensed_star_cat_line(fp, val3[i]);

                        if (frand(0,1) < 0.1)
                        {
                            std::stringstream lmss;
                            lmss << "Saving " << val3[i]->alienorumid << " to condensed star catalog...";
                            mtx.lock();
                            loading_msg = lmss.str();
                            mtx.unlock();
                        }
                    }
                }
            }
        }

        fclose(fp);

        #ifndef _WIN32
        // TODO: This command will only execute on Linux - maybe not a problem if only the author ever issues new internal catalogs.
        system("gzip -k catalogs/soles_alienorum.dat");
        #endif
    }

    return written;
}

bool alienorum::CatalogReader::bake_condensed_star_orbits()
{
    const std::string signature = star_orbits_signature();
    if (signature.empty() || abort_load) return false;
    const std::string path = get_condensed_starcat_name();
    const std::string temporary = path + ".tmp";
    FILE* fp = fopen(temporary.c_str(), "wb");
    if (!fp) return false;
    try
    {
        fprintf(fp, "# ORBITS1 %s\n", signature.c_str());
        for (int i = 0; cels[i]; ++i)
        {
            if (cels[i]->typeclass() == class_star)
                write_condensed_star_cat_line(fp, static_cast<Star*>(cels[i]));
        }
    }
    catch (...)
    {
        fclose(fp);
        fs::remove(temporary);
        throw;
    }
    const bool failed = ferror(fp);
    const int closed = fclose(fp);
    if (failed || closed != 0 || abort_load)
    {
        fs::remove(temporary);
        return false;
    }
    std::error_code error;
    fs::rename(temporary, path, error);
#ifdef _WIN32
    if (error)
    {
        fs::copy_file(temporary, path, fs::copy_options::overwrite_existing, error);
        if (!error) fs::remove(temporary);
    }
#endif
    if (error) return false;
    condensed_star_orbits_loaded = true;
    return true;
}

int alienorum::CatalogReader::apply_gaia_astrometry(CelestialObject** cels)
{
    if (!cels)
    {
        return 0;
    }

    std::string path = "catalogs" _FILESLASH "GAIA" _FILESLASH "catalog.dat";
    std::string gzpath = "catalogs" _FILESLASH "GAIA" _FILESLASH "catalog.dat.gz";

    if (!file_exists(path.c_str()))
    {
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
        }
        else if (file_exists(("catalogs" _FILESLASH "GAIA" _FILESLASH "hgca_edr3.dat.gz")))
        {
            extract_archive("catalogs" _FILESLASH "GAIA" _FILESLASH "hgca_edr3.dat.gz");
        }
    }

    if (!file_exists(path.c_str()))
    {
        path = "catalogs" _FILESLASH "GAIA" _FILESLASH "hgca_edr3.dat";
    }

    FILE* fp = fopen(path.c_str(), "r");
    if (!fp)
    {
        std::cerr << "Notice: Gaia astrometry catalog not available (" << path << ")." << std::endl;
        return 0;
    }

    struct GaiaAstRecord
    {
        uint64_t source_id = 0;
        float plx_mas = 0.0f;
        float pm_ra = 0.0f;
        float pm_dec = 0.0f;
        float rv_kms = 0.0f;
        bool has_rv = false;
        bool valid = false;
    };

    std::vector<GaiaAstRecord> gaia_table(MAX_HIP + 1);

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp))
    {
        if (buffer[0] == '#' || buffer[0] == '\n' || buffer[0] == '\r')
        {
            continue;
        }

        char* token = buffer;
        char* next = nullptr;
        int field_idx = 0;
        int hip = 0;
        uint64_t src_id = 0;
        float plx = 0.0f;
        float pmra = 0.0f;
        float pmde = 0.0f;
        float rv = 0.0f;
        bool has_rv_val = false;

        while (token && *token)
        {
            next = strchr(token, '|');
            if (next)
            {
                *next = '\0';
                next++;
            }

            while (*token == ' ' || *token == '\t')
            {
                token++;
            }
            int len = strlen(token);
            while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\t' || token[len - 1] == '\n' || token[len - 1] == '\r'))
            {
                token[--len] = '\0';
            }

            if (field_idx == 0)
            {
                hip = atoi(token);
            }
            else if (field_idx == 1)
            {
                if (len > 0)
                {
                    src_id = strtoull(token, nullptr, 10);
                }
            }
            else if (field_idx == 4)
            {
                if (len > 0)
                {
                    rv = (float)atof(token);
                    has_rv_val = true;
                }
            }
            else if (field_idx == 7)
            {
                if (len > 0)
                {
                    plx = (float)atof(token);
                }
            }
            else if (field_idx == 9)
            {
                if (len > 0)
                {
                    pmra = (float)atof(token);
                }
            }
            else if (field_idx == 10)
            {
                if (len > 0)
                {
                    pmde = (float)atof(token);
                }
                break;
            }

            field_idx++;
            token = next;
        }

        if (hip > 0 && hip <= MAX_HIP && plx > 0.0f)
        {
            gaia_table[hip].source_id = src_id;
            gaia_table[hip].plx_mas = plx;
            gaia_table[hip].pm_ra = pmra;
            gaia_table[hip].pm_dec = pmde;
            gaia_table[hip].rv_kms = rv;
            gaia_table[hip].has_rv = has_rv_val;
            gaia_table[hip].valid = true;
        }
    }
    fclose(fp);

    int updated_count = 0;
    int reclassified_count = 0;

    for (int i = 0; cels[i]; i++)
    {
        if (cels[i]->typeclass() != class_star)
        {
            continue;
        }

        Star* s = (Star*)cels[i];
        if (s->HIP <= 0 || s->HIP > MAX_HIP || !gaia_table[s->HIP].valid)
        {
            continue;
        }

        const auto& rec = gaia_table[s->HIP];
        double plx_mas = rec.plx_mas;
        if (plx_mas <= 0.0)
        {
            continue;
        }

        double new_dist_pc = 1000.0 / plx_mas;
        s->parallax = (plx_mas / 1000.0) / 3600.0 * fiftyseventh;
        s->distance = new_dist_pc * parsec;
        s->distance_known = true;

        if (rec.pm_ra != 0.0f || rec.pm_dec != 0.0f)
        {
            s->proper_motion_RA = (rec.pm_ra / 1000.0) / 3600.0 / oneyear * fiftyseventh;
            s->proper_motion_decl = (rec.pm_dec / 1000.0) / 3600.0 / oneyear * fiftyseventh;
        }

        if (rec.has_rv && s->radial_velocity == 0)
        {
            s->radial_velocity = rec.rv_kms * 1000.0;
        }

        s->update_location(simnow);

        double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10.0, 2);
        s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;

        if (s->is_main_sequence() && !s->is_hot_ob_star())
        {
            const double exp_mag = s->expected_main_sequence_absmag();

            bool is_verified_dwarf = (s->volumetric_mean_radius > 0 && s->volumetric_mean_radius < 1.2 * solar_radius)
                                  || (s->mass > 0 && s->mass < 0.6 * solar_mass)
                                  || (s->spectral_type[0] == 'M')
                                  || (s->spectral_type[0] == 'd' && s->spectral_type[1] == 'M');

            if (!is_verified_dwarf && std::isfinite(exp_mag) && exp_mag < 1e28
                && (exp_mag - s->absolute_magnitude >= 1.5))
            {
                const char* new_class = "IV";
                if (s->absolute_magnitude <= -5.0)
                {
                    new_class = "I";
                }
                else if (s->absolute_magnitude <= 0.0)
                {
                    new_class = "II";
                }
                else if (s->absolute_magnitude <= 2.5)
                {
                    new_class = "III";
                }

                for (int k = 0; s->spectral_type[k]; k++)
                {
                    if (s->spectral_type[k] == 'V')
                    {
                        bool prev_I = (k > 0 && s->spectral_type[k - 1] == 'I');
                        bool next_I = (s->spectral_type[k + 1] == 'I');
                        if (!prev_I && !next_I)
                        {
                            char remainder[32] = {0};
                            strncpy(remainder, &s->spectral_type[k + 1], sizeof(remainder) - 1);
                            s->spectral_type[k] = '\0';
                            std::string new_sp = std::string(s->spectral_type) + new_class + remainder;
                            strncpy(s->spectral_type, new_sp.c_str(), sizeof(s->spectral_type) - 1);
                            s->spectral_type[sizeof(s->spectral_type) - 1] = '\0';
                            reclassified_count++;
                            break;
                        }
                    }
                }

                double T = s->estimate_temperature();
                if (T > 0)
                {
                    double lum = pow(magnbase, -(s->absolute_magnitude - 4.83));
                    s->volumetric_mean_radius = sqrt(lum) * pow(sun_temp / T, 2.0) * solar_radius;
                }
            }
        }

        if (s->multisys)
        {
            for (char c = 'B'; c <= 'Z'; c++)
            {
                Star* B = s->multisys->get_member(c);
                if (B && B != s)
                {
                    B->distance = s->distance;
                    B->distance_known = true;
                    B->parallax = s->parallax;
                    B->update_location(simnow);
                    if (std::isfinite(B->apparent_magnitude)
                        && B->apparent_magnitude >= -40 && B->apparent_magnitude < 99)
                    {
                        double b_intrinsic = pow(magnbase, -B->apparent_magnitude) * pow(fmax(AU, B->distance) / parsec / 10.0, 2);
                        B->absolute_magnitude = -log(b_intrinsic) * invlogmagnbase;
                    }
                }
            }
        }

        updated_count++;
    }

    std::cout << "Applied Gaia DR3 astrometry: updated " << updated_count
              << " star distances, corrected " << reclassified_count
              << " spectral luminosity classes." << std::endl;

    return updated_count;
}

std::string alienorum::CatalogReader::get_condensed_starcat_name()
{
    return "catalogs" _FILESLASH "soles_alienorum.dat";
}

int alienorum::CatalogReader::read_condensed_star_cat()
{
    condensed_star_orbits_loaded = false;
    std::map<Star*, json> orbit_states;
    std::unordered_map<std::string, Star*> saved_stars;
    std::vector<Star*> catalog_stars;
    int num_read = 0, i, j;
    char buffer[16384];
    char field[64];
    double f, deg, mnt, sec;

    if (!hdcache)
    {
        hdcache = new Star*[MAX_HD+1];
        memset(hdcache, 0, sizeof(Star*)*(MAX_HD+1));
    }
    if (!hipcache)
    {
        hipcache = new Star*[MAX_HIP+1];
        memset(hipcache, 0, sizeof(Star*)*(MAX_HIP+1));
    }

    std::string str, path = get_condensed_starcat_name();
    FILE *fp = fopen(path.c_str(), "r");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    while (fgets(buffer, sizeof(buffer), fp))
    {
        if (abort_load) { fclose(fp); return num_read; }
        if (buffer[0] == '#')
        {
            std::istringstream header(buffer);
            std::string comment, version, signature;
            header >> comment >> version >> signature;
            if (version == "ORBITS1")
            {
                const std::string source_signature = star_orbits_signature();
                condensed_star_orbits_loaded = source_signature.empty() || signature == source_signature;
            }
            continue;
        }
        Star *s = new Star();
        read_field_onebased(buffer, 1, 14, field);
        s->alienorumid = trim(field);

        read_field_onebased(buffer, 16, 54, field);

        str = trim(field);
        strcpy(s->name, str.c_str());
        s->namelen = strlen(s->name);
        s->origname = s->name;

        read_field_onebased(buffer, 56, 57, field);
        deg = atof(field) * 15;

        read_field_onebased(buffer, 59, 60, field);
        mnt = atof(field) * 15;

        read_field_onebased(buffer, 62, 65, field);
        sec = atof(field) * 15;

        s->right_ascension = (deg + mnt/60 + sec/3600) * fiftyseventh;

        read_field_onebased(buffer, 67, 67, field);
        int sgndecl = (field[0] == '-') ? -1 : 1;

        read_field_onebased(buffer, 68, 69, field);
        deg = atof(field);

        read_field_onebased(buffer, 71, 72, field);
        mnt = atof(field);

        read_field_onebased(buffer, 74, 75, field);
        sec = atof(field);

        s->declination = (deg + mnt/60 + sec/3600) * fiftyseventh * sgndecl;


        read_field_onebased(buffer, 77, 82, field);
        str = trim(field);
        char *magnitude_end = nullptr;
        const double apparent = std::strtod(str.c_str(), &magnitude_end);
        // The writer represents an unknown magnitude with a sign and otherwise blank digits.
        // Keep Star's unknown-value default unless the field contains a complete finite number.
        if (magnitude_end != str.c_str() && *magnitude_end == '\0' &&
            std::isfinite(apparent) && apparent >= -40 && apparent < 99)
        {
            s->apparent_magnitude = apparent;
        }

        read_field_onebased(buffer, 84, 98, field);
        str = trim(field);
        strcpy(s->spectral_type, str.c_str());

        read_field_onebased(buffer, 456, 467, field);
        s->WD = trim(field);
        if (strlen(buffer) > 655)
        {
            std::istringstream extras(buffer + 655);
            std::string version, value;
            while (extras >> version)
            {
                if (version == "CHEM1" && extras >> std::quoted(value))
                {
                    json chemistry = json::parse(value, nullptr, false);
                    if (chemistry.is_object()) s->stellar_abundances = std::move(chemistry);
                }
                else if (version == "MET1" && extras >> std::quoted(value))
                {
                    const json measurement = json::parse(value, nullptr, false);
                    if (measurement.is_array() && measurement.size() == 5 && measurement[0].is_number() &&
                        measurement[1].is_string() && measurement[4].is_string())
                    {
                        const double metallicity = measurement[0].get<double>();
                        if (std::isfinite(metallicity))
                        {
                            s->metallicity = metallicity;
                            s->metallicity_ratio = measurement[1].get<std::string>();
                            s->metallicity_source = measurement[4].get<std::string>();
                            if (measurement[2].is_number()) s->metallicity_error_upper = measurement[2].get<double>();
                            if (measurement[3].is_number()) s->metallicity_error_lower = measurement[3].get<double>();
                        }
                    }
                }
                else if (version == "ORB1" && extras >> std::quoted(value))
                {
                    json state = json::parse(value);
                    if (state.is_object())
                    {
                        const auto rotation = [](const json& value)
                        {
                            Rotation plane;
                            plane.from_json(value);
                            return json::array({plane.v.x, plane.v.y, plane.v.z, plane.a});
                        };
                        const int flags = (state.at("known_poles").get<bool>() ? 1 : 0) |
                            (state.at("lock_system").get<bool>() ? 2 : 0) |
                            (state.at("lock_equator").get<bool>() ? 4 : 0) |
                            (state.at("has_disk").get<bool>() ? 8 : 0) |
                            (state.at("rot_axis_known").get<bool>() ? 16 : 0) |
                            (state.at("distance_known").get<bool>() ? 32 : 0);
                        json elements = state.value("elements", json());
                        if (elements.is_array() && elements.size() == 8)
                        {
                            elements.push_back(0);
                            elements.push_back(0);
                        }
                        state = json::array({state.at("id"), state.value("center", -1), state.at("component"),
                            flags, rotation(state.at("system_plane")), rotation(state.at("orbital_plane")),
                            rotation(state.at("equatorial_plane")), state.at("obliquity"), state.at("equinox"),
                            state.at("disk_inclination"), state.at("disk_node"), state.at("disk_inner_edge"),
                            state.at("rot_inclination"), state.at("rot_node"), state.at("rotation_period"), elements});
                    }
                    saved_stars.emplace(baked_star_link_key(state.at(0)), s);
                    orbit_states.emplace(s, std::move(state));
                }
                else if (version == "SID1" && extras >> std::quoted(value))
                {
                    saved_stars.emplace(value, s);
                }
                else if (version == "ALS1")
                {
                    while (extras >> std::quoted(value)) s->add_catalog_alias(value);
                }
            }
        }
        read_field_onebased(buffer, 469, 471, field);
        if (!strcmp(field, "SC2"))
        {
            read_field_onebased(buffer, 473, 512, field);
            s->gaia_wd_name = trim(field);
            read_field_onebased(buffer, 514, 533, field);
            s->gaia_source_id = strtoull(field, nullptr, 10);
            read_field_onebased(buffer, 535, 552, field);
            s->gaia_g_magnitude = atof(field);
            read_field_onebased(buffer, 554, 554, field);
            s->apparent_magnitude_estimated = field[0] == '1';
            read_field_onebased(buffer, 556, 556, field);
            s->wd_color_valid = field[0] == '1';
            read_field_onebased(buffer, 558, 560, field);
            s->wd_color_red = std::clamp(atoi(field), 0, 255);
            read_field_onebased(buffer, 562, 564, field);
            s->wd_color_green = std::clamp(atoi(field), 0, 255);
            read_field_onebased(buffer, 566, 568, field);
            s->wd_color_blue = std::clamp(atoi(field), 0, 255);
            read_field_onebased(buffer, 570, 601, field);
            s->wd_color_source = trim(field);
            read_field_onebased(buffer, 603, 634, field);
            s->wd_temperature_source = trim(field);
            read_field_onebased(buffer, 636, 655, field);
            s->wd_color_revision = strtoull(field, nullptr, 10);
        }
        else if (strlen(buffer) > 468)
        {
            std::istringstream metadata(buffer + 468);
            metadata >> std::quoted(s->gaia_wd_name) >> s->gaia_source_id
                     >> s->gaia_g_magnitude >> s->apparent_magnitude_estimated;
            metadata >> s->wd_color_valid;
            unsigned int red, green, blue;
            if (metadata >> red >> green >> blue >> std::quoted(s->wd_color_source))
            {
                s->wd_color_red = std::min(255u, red);
                s->wd_color_green = std::min(255u, green);
                s->wd_color_blue = std::min(255u, blue);
                std::string version;
                if (metadata >> version && version == "WDC1")
                {
                    metadata >> std::quoted(s->wd_temperature_source) >> s->wd_color_revision;
                }
            }
            else
            {
                s->wd_color_valid = false;
            }
        }

        read_field_onebased(buffer, 100, 105, field);
        s->BV_color = atof(field);

        read_field_onebased(buffer, 107, 112, field);
        s->UB_color = atof(field);


        read_field_onebased(buffer, 114, 119, field);
        s->HD = atoi(field);
        hdcache[s->HD] = s;

        read_field_onebased(buffer, 121, 126, field);
        s->HIP = atoi(field);
        hipcache[s->HIP] = s;

        read_field_onebased(buffer, 128, 133, field);
        s->HR = atoi(field);

        read_field_onebased(buffer, 135, 140, field);
        s->SB9 = atoi(field);

        read_field_onebased(buffer, 142, 144, field);
        s->FlamsteedNo = atoi(field);

        read_field_onebased(buffer, 146, 152, field);
        str = trim(field);
        if (str.size())
        {
            strcpy(s->Bayer, str.c_str());
            s->BayerGrkno = grkno_from_abbrev(s->Bayer);
        }

        read_field_onebased(buffer, 154, 168, field);
        str = trim(field);
        strcpy(s->Gliese, str.c_str());

        read_field_onebased(buffer, 170, 172, field);
        s->GouldNo = atoi(field);

        read_field_onebased(buffer, 174, 176, field);
        strcpy(s->Gouldcons, field);

        Constellation *mycons = nullptr;
        if (strlen(s->Bayer) || (s->FlamsteedNo > 0) || (s->GouldNo > 0))
        {
            strcpy(s->constellation, cons_from_alienorumid(s->alienorumid).c_str());
            j = constellations.size();
            for (i = 0; i < j && !mycons; i++)
            {
                if (constellations[i].abbrev == s->constellation)
                {
                    mycons = &constellations[i];
                }
            }
        }

        if (s->FlamsteedNo > 0)
        {
            str = std::to_string(s->FlamsteedNo) + std::string(" ");
            if (s->FlamsteedNo < 10) str += std::string(" ");
            str += std::string(s->constellation);
            strcpy(s->Flamsteed, str.c_str());

            if (mycons)
            {
                mycons->Flamsteed_stars.emplace(s->FlamsteedNo, s);
            }
        }

        if (mycons && s->BayerGrkno >= 0)
        {
            mycons->Bayer_stars.emplace(s->BayerGrkno, s);
        }
        if (mycons && s->GouldNo > 0)
        {
            mycons->Gould_stars.emplace(s->GouldNo, s);
        }

        read_field_onebased(buffer, 178, 188, field);
        s->proper_motion_RA = atof(field);

        read_field_onebased(buffer, 190, 200, field);
        s->proper_motion_decl = atof(field);

        read_field_onebased(buffer, 202, 212, field);
        s->radial_velocity = atof(field);

        read_field_onebased(buffer, 214, 224, field);
        s->parallax = atof(field);

        read_field_onebased(buffer, 226, 236, field);
        s->distance = atof(field) * light_year;
        s->update_location(simnow);
        if (s->parallax > 0) s->distance_known = true;

        read_field_onebased(buffer, 238, 243, field);
        s->absolute_magnitude = atof(field);

        read_field_onebased(buffer, 245, 255, field);
        s->mass = atof(field) * solar_mass;
        if (s->mass < jupiter_mass) s->mass = s->estimate_mass();

        read_field_onebased(buffer, 257, 267, field);
        s->volumetric_mean_radius = atof(field) * solar_radius;
        if (!(s->volumetric_mean_radius > 0) || !std::isfinite(s->volumetric_mean_radius) ||
            (!s->is_white_dwarf() && s->volumetric_mean_radius < 0.5 * jupiter_radius))
        {
            s->volumetric_mean_radius = s->estimate_radius();
        }

        read_field_onebased(buffer, 269, 274, field);
        s->temperature = atof(field);

        read_field_onebased(buffer, 276, 285, field);
        s->sidereal_rotational_period = atof(field) * oneday;
        if (!s->sidereal_rotational_period) s->sidereal_rotational_period = oneday*25;

        read_field_onebased(buffer, 287, 300, field);
        str = trim(field);
        if (str.size())
        {
            s->orbit = new Orbit();
            s->orbit->center_name = str;
        }

        read_field_onebased(buffer, 302, 313, field);
        if (s->orbit) s->orbit->period = atof(field) * oneday;

        read_field_onebased(buffer, 315, 326, field);
        if (s->orbit) s->orbit->semimajor_axis = atof(field) * AU;

        read_field_onebased(buffer, 328, 339, field);
        if (s->orbit) s->orbit->eccentricity = atof(field);

        read_field_onebased(buffer, 341, 348, field);
        if (s->orbit) s->orbit->arg_periapsis = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 350, 357, field);
        if (s->orbit) s->orbit->mean_anomaly = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 359, 372, field);
        if (s->orbit) s->orbit->epoch = atof(field);

        read_field_onebased(buffer, 374, 381, field);
        f = atof(field) * fiftyseventh;
        if (s->orbit) s->orbit->heliocentric_inclination = f;

        read_field_onebased(buffer, 383, 390, field);
        f = atof(field) * fiftyseventh;
        if (s->orbit) s->orbit->heliocentric_node = f;

        read_field_onebased(buffer, 392, 403, field);
        s->variability_period = atof(field) * oneday;

        read_field_onebased(buffer, 405, 410, field);
        s->minmag = atof(field);

        read_field_onebased(buffer, 412, 417, field);
        s->maxmag = atof(field);

        read_field_onebased(buffer, 419, 435, field);
        s->epoch_max_brightness = atof(field);

        read_field_onebased(buffer, 437, 437, field);
        if (field[0] == 'E') s->is_eclipsing_binary = true;

        read_field_onebased(buffer, 439, 440, field);
        str = trim(field);
        if (str.size() >= 2)
        {
            s->Bonn_survey[0] = str[0];
            s->Bonn_survey[1] = str[1];

            read_field_onebased(buffer, 442, 445, field);
            s->Bonn_survey_sign = field[0];
            s->Bonn_survey_declination = atoi(field);

            read_field_onebased(buffer, 447, 452, field);
            s->Bonn_survey_sequential = atoi(field);

            std::string dmkey = bonn_survey_key(s->Bonn_survey, s->Bonn_survey_declination, s->Bonn_survey_sequential);
            if (dmkey.size() && !dmcache.count(dmkey)) dmcache[dmkey] = s;
        }

        read_field_onebased(buffer, 454, 454, field);
        if (field[0] == 'Y') s->has_custom_name = true;

        catalog_stars.push_back(s);
        append_cel(s);
        num_read++;

        if (!(num_read & 0x1fff)) loading_msg = std::string("Loading star catalog (") + std::string(s->alienorumid) + std::string(")...");
    }

    fclose(fp);
    ((Star*)cels[0])->distance_known = true;
    for (Star* object : catalog_stars) apply_stellar_abundances(object);

    const auto find_saved_star = [&](const json& key) -> Star*
    {
        auto found = saved_stars.find(baked_star_link_key(key));
        if (found != saved_stars.end()) return found->second;
        // Early snapshots used row numbers without a separate identity record.
        if (key.is_number_integer())
        {
            const int index = key.get<int>();
            if (index >= 0 && static_cast<size_t>(index) < catalog_stars.size()) return catalog_stars[index];
        }
        return nullptr;
    };

    loading_msg = std::string("Verifying star orbits...");

    std::unordered_map<std::string_view, Star*> id_to_star;
    id_to_star.reserve(num_read + 100);

    std::vector<Star*> orbiting_stars;
    orbiting_stars.reserve(16384);

    for (i = 0; cels[i]; i++)
    {
        if (cels[i]->type == star)
        {
            Star *s = (Star*)cels[i];
            if (!s->alienorumid.empty())
            {
                id_to_star.emplace(s->alienorumid, s);
            }
            if (s->orbit && !s->orbit->center_name.empty())
            {
                orbiting_stars.push_back(s);
            }
        }
    }

    for (size_t k = 0; k < orbiting_stars.size(); k++)
    {
        Star *s = orbiting_stars[k];
        auto saved = orbit_states.find(s);
        if (saved != orbit_states.end() && find_saved_star(saved->second.at(1))) continue;
        auto it = id_to_star.find(s->orbit->center_name);
        if (it != id_to_star.end() && it->second != s)
        {
            Star *A = it->second;
            s->orbit->center = A;
            s->origcenname = A->name;
            if (!s->multisys || s->multisys != A->multisys)
            {
                if (!A->multisys)
                {
                    A->multisys = new StarMulti();
                    A->multisys->add_member(A, 'A');
                }
                char component = 0;
                const std::string identity = s->alienorumid.empty() ? s->name : s->alienorumid;
                if (identity.size() >= 2 && identity[identity.size() - 2] == ' ' &&
                    identity.back() >= 'B' && identity.back() <= 'Z') component = identity.back();
                if (!component || (A->multisys->get_member(component) && A->multisys->get_member(component) != s))
                    component = A->multisys->next_available();
                if (component >= 'B' && component <= 'Z') s->set_component(component, A);
            }

            if (s->orbit->heliocentric_inclination || s->orbit->heliocentric_node)
            {
                A->known_poles = true;
                A->obliquity = 0;
                A->equinox = 0;
                if (!A->location.system_center.magnitude())
                {
                    A->location.system_center = Point::from_ra_dec(A->right_ascension, A->declination, A->distance);
                }
                A->location.local_system_plane = system_plane_from_incl_and_node(
                    s->orbit->heliocentric_inclination,
                    s->orbit->heliocentric_node,
                    A->location.system_center);
                A->location.orbital_plane = A->location.local_system_plane;
                A->location.equatorial_plane = A->location.local_system_plane;
                A->lock_system_plane = true;
                A->lock_equatorial_plane = true;

                s->location = A->location;
                s->location.local_system_plane = A->location.local_system_plane;
                s->location.orbital_plane = A->location.local_system_plane;
                s->location.equatorial_plane = A->location.equatorial_plane;
                s->lock_system_plane = true;
                s->lock_equatorial_plane = true;
                s->known_poles = true;
                s->obliquity = 0;
                s->equinox = 0;
            }
            else
            {
                s->location = A->location;
            }
        }

        if (!(k & 0xff))
        {
            loading_msg = std::string("Verifying ") + s->alienorumid + std::string("...");
        }
    }

    // Restore baked states last: the legacy reconstruction above infers planes and
    // would otherwise overwrite measured rotation axes and disk orientations.
    for (auto& [s, state] : orbit_states)
    {
        if (!baked_star_link_key(state.at(1)).empty())
        {
            Star* saved_center = find_saved_star(state.at(1));
            if (saved_center && saved_center != s)
            {
                if (!s->orbit) s->orbit = new Orbit();
                s->orbit->center = saved_center;
                s->orbit->center_name = saved_center->alienorumid;
                s->origcenname = saved_center->name;
                char component = state.at(2).get<int>();
                if (!component)
                {
                    const std::string identity = s->alienorumid.empty() ? s->name : s->alienorumid;
                    if (identity.size() >= 2 && identity[identity.size() - 2] == ' ' &&
                        identity.back() >= 'B' && identity.back() <= 'Z') component = identity.back();
                    else component = saved_center->multisys ? saved_center->multisys->next_available() : 'B';
                }
                if (component >= 'B' && component <= 'Z')
                {
                    if (!saved_center->multisys)
                    {
                        saved_center->multisys = new StarMulti();
                        saved_center->multisys->add_member(saved_center, 'A');
                    }
                    s->set_component(component, saved_center);
                }
            }
        }
        if (!state.at(15).is_null())
        {
            if (!s->orbit) s->orbit = new Orbit();
            if (!s->orbit->center && state.size() > 16) state.at(16).get_to(s->orbit->center_name);
            const auto& elements = state.at(15);
            s->orbit->period = elements.at(0);
            s->orbit->semimajor_axis = elements.at(1);
            s->orbit->eccentricity = elements.at(2);
            s->orbit->arg_periapsis = elements.at(3);
            s->orbit->mean_anomaly = elements.at(4);
            s->orbit->epoch = elements.at(5);
            s->orbit->heliocentric_inclination = elements.at(6);
            s->orbit->heliocentric_node = elements.at(7);
            s->orbit->inclination = elements.at(8);
            s->orbit->ascending_node = elements.at(9);
        }
        const int flags = state.at(3);
        s->known_poles = flags & 1;
        s->lock_system_plane = flags & 2;
        s->lock_equatorial_plane = flags & 4;
        s->has_disk = flags & 8;
        s->rot_axis_known = flags & 16;
        s->distance_known = flags & 32;
        const auto restore_rotation = [](Rotation& plane, const json& values)
        {
            plane.v = Point(values.at(0), values.at(1), values.at(2));
            plane.a = values.at(3);
        };
        restore_rotation(s->location.local_system_plane, state.at(4));
        restore_rotation(s->location.orbital_plane, state.at(5));
        restore_rotation(s->location.equatorial_plane, state.at(6));
        state.at(7).get_to(s->obliquity);
        state.at(8).get_to(s->equinox);
        state.at(9).get_to(s->disk_heliocen_inclination);
        state.at(10).get_to(s->disk_heliocen_node);
        state.at(11).get_to(s->disk_inner_edge_sma);
        state.at(12).get_to(s->rot_heliocen_incl);
        state.at(13).get_to(s->rot_heliocen_node);
        state.at(14).get_to(s->sidereal_rotational_period);
    }

    return num_read;
}

void CatalogReader::read_field_onebased(const char *buffer, size_t start, int end, char *out)
{
    if (start > strlen(buffer))
    {
        out[0] = 0;
        return;
    }
    start--;
    int len = end - start;
    int i;
    for (i=0; i<len; i++) if (!(out[i] = buffer[i+start])) break;
    out[i] = 0;
}

// libcurl write callback to append chunk data to an allocated std::string buffer
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// Minimal pipe-delimited record I/O for the exoplanet derived caches. NAN/empty
// fields are written as nothing between pipes and read back the same way.
static void write_exo_double(FILE* fp, double v)
{
    if (isnan(v)) fputc('|', fp);
    else fprintf(fp, "%.10g|", v);
}

static void write_exo_string(FILE* fp, const std::string& s)
{
    for (char c : s) fputc((c == '|' || c == '\n' || c == '\r') ? ' ' : c, fp);
    fputc('|', fp);
}

static double read_exo_double(char*& cursor)
{
    char* pipe = strchr(cursor, '|');
    if (!pipe) return NAN;
    *pipe = 0;
    double v = cursor[0] ? atof(cursor) : NAN;
    cursor = pipe + 1;
    return v;
}

static std::string read_exo_string(char*& cursor)
{
    char* pipe = strchr(cursor, '|');
    if (!pipe) return "";
    *pipe = 0;
    std::string v = cursor;
    cursor = pipe + 1;
    return v;
}

ExoRow CatalogReader::exorow_from_json(const json& row, bool* ok)
{
    ExoRow r;
    *ok = false;

    // Ensure baseline primary keys exist
    auto it_pl = row.find("pl_name");
    auto it_host = row.find("hostname");
    auto it_smax = row.find("pl_orbsmax");
    auto it_per = row.find("pl_orbper");

    bool has_smax = (it_smax != row.end() && !it_smax->is_null() && it_smax->is_number());
    bool has_per = (it_per != row.end() && !it_per->is_null() && it_per->is_number());

    if (it_pl == row.end() || it_pl->is_null() || !it_pl->is_string() || it_pl->get<std::string>().empty()
            || it_host == row.end() || it_host->is_null() || !it_host->is_string() || it_host->get<std::string>().empty()
            || (!has_smax && !has_per)
       )
    {
        return r;
    }

    std::string pl_name_check = it_pl->get<std::string>();
    if (!has_smax && !has_per)
    {
        bool is_special_imaging = (pl_name_check == "PDS 70 e" || pl_name_check == "HIP 65426 b" || pl_name_check == "WD 0806-661 b");
        if (!is_special_imaging)
        {
            return r;
        }
    }

    r.pl_name = it_pl->get<std::string>();
    r.hostname = it_host->get<std::string>();

    auto gets = [&row](const char* key) -> std::string
    {
        auto it = row.find(key);
        return (it != row.end() && !it->is_null() && it->is_string()) ? it->get<std::string>() : "";
    };

    r.hd_name  = gets("hd_name");
    r.hip_name = gets("hip_name");

    auto getd = [&row](const char* key) -> double
    {
        auto it = row.find(key);
        return (it != row.end() && !it->is_null() && it->is_number()) ? it->get<double>() : NAN;
    };

    r.sy_dist = getd("sy_dist");
    r.ra = getd("ra");
    r.dec = getd("dec");
    r.st_lum = getd("st_lum");
    r.sy_vmag = getd("sy_vmag");
    r.st_teff = getd("st_teff");
    r.st_mass = getd("st_mass");
    r.st_rad = getd("st_rad");
    r.st_rotp = getd("st_rotp");
    r.pl_bmassprov = gets("pl_bmassprov");
    r.pl_bmasselim = getd("pl_bmasselim");
    r.pl_radelim = getd("pl_radelim");
    r.pl_bmasseerr1 = getd("pl_bmasseerr1");
    r.pl_bmasseerr2 = getd("pl_bmasseerr2");
    r.pl_bmassjerr1 = getd("pl_bmassjerr1");
    r.pl_bmassjerr2 = getd("pl_bmassjerr2");
    r.pl_radeerr1 = getd("pl_radeerr1");
    r.pl_radeerr2 = getd("pl_radeerr2");
    r.pl_radjerr1 = getd("pl_radjerr1");
    r.pl_radjerr2 = getd("pl_radjerr2");
    const double metallicity_limit = getd("st_metlim");
    if (!std::isfinite(metallicity_limit) || metallicity_limit == 0)
    {
        r.st_met = getd("st_met");
        r.st_meterr1 = getd("st_meterr1");
        r.st_meterr2 = getd("st_meterr2");
        r.st_metratio = gets("st_metratio");
        r.st_met_source = gets("st_met_source");
    }

    auto it_spec = row.find("st_spectype");
    if (it_spec != row.end() && !it_spec->is_null() && it_spec->is_string())
    {
        r.st_spectype = it_spec->get<std::string>();
        size_t pos = r.st_spectype.find("&plusmn;");
        if (pos != std::string::npos)
        {
            r.st_spectype.replace(pos, 8, "\xf1");
        }
    }

    const double transit_flag = getd("tran_flag");
    std::string method = gets("discoverymethod");
    if (method.empty()) method = gets("detection_type");
    std::transform(method.begin(), method.end(), method.begin(), [](unsigned char c) { return std::tolower(c); });
    bool transit_method = false;
    std::istringstream methods(method);
    std::string token;
    while (std::getline(methods, token, ','))
    {
        token = trim(token);
        transit_method = transit_method || token == "transit" || token == "primary transit";
    }
    r.transiting = std::isfinite(transit_flag) ? transit_flag == 1 : transit_method;
    r.pl_orbincl = getd("pl_orbincl");
    r.pl_orbinclerr1 = getd("pl_orbinclerr1");
    r.pl_orbinclerr2 = getd("pl_orbinclerr2");
    r.pl_msinij = getd("pl_msinij");
    r.pl_msinie = getd("pl_msinie");
    r.pl_bmassj = getd("pl_bmassj");
    r.pl_bmasse = getd("pl_bmasse");
    r.pl_radj = getd("pl_radj");
    r.pl_rade = getd("pl_rade");
    r.pl_trueobliq = getd("pl_trueobliq");
    r.pl_orbtper = getd("pl_orbtper");
    r.pl_orblper = getd("pl_orblper");
    r.pl_tranmid = getd("pl_tranmid");
    r.pl_orbper = getd("pl_orbper");
    r.pl_orbsmax = getd("pl_orbsmax");
    r.pl_orbeccen = getd("pl_orbeccen");

    if (isnan(r.pl_orbper) && !isnan(r.pl_orbsmax) && !isnan(r.st_mass) && r.st_mass > 0)
    {
        r.pl_orbper = 365.25 * sqrt(r.pl_orbsmax * r.pl_orbsmax * r.pl_orbsmax / r.st_mass);
    }
    else if (isnan(r.pl_orbsmax) && !isnan(r.pl_orbper) && !isnan(r.st_mass) && r.st_mass > 0)
    {
        double per_yr = r.pl_orbper / 365.25;
        r.pl_orbsmax = cbrt(r.st_mass * per_yr * per_yr);
    }

    *ok = true;
    return r;
}

void CatalogReader::exorow_write_line(FILE* fp, const ExoRow& row)
{
    write_exo_string(fp, row.pl_name);
    write_exo_string(fp, row.hostname);
    write_exo_string(fp, row.hd_name);
    write_exo_string(fp, row.hip_name);
    write_exo_string(fp, row.st_spectype);

    write_exo_double(fp, row.sy_dist);
    write_exo_double(fp, row.ra);
    write_exo_double(fp, row.dec);
    write_exo_double(fp, row.st_lum);
    write_exo_double(fp, row.sy_vmag);
    write_exo_double(fp, row.st_teff);
    write_exo_double(fp, row.st_mass);
    write_exo_double(fp, row.st_rad);
    write_exo_double(fp, row.st_rotp);
    write_exo_double(fp, row.pl_orbincl);
    write_exo_double(fp, row.pl_msinij);
    write_exo_double(fp, row.pl_msinie);
    write_exo_double(fp, row.pl_bmassj);
    write_exo_double(fp, row.pl_bmasse);
    write_exo_double(fp, row.pl_radj);
    write_exo_double(fp, row.pl_rade);
    write_exo_double(fp, row.pl_trueobliq);
    write_exo_double(fp, row.pl_orbtper);
    write_exo_double(fp, row.pl_orblper);
    write_exo_double(fp, row.pl_tranmid);
    write_exo_double(fp, row.pl_orbper);
    write_exo_double(fp, row.pl_orbsmax);
    write_exo_double(fp, row.pl_orbeccen);
    write_exo_double(fp, row.pl_orbinclerr1);
    write_exo_double(fp, row.pl_orbinclerr2);
    write_exo_double(fp, row.st_met);
    write_exo_double(fp, row.st_meterr1);
    write_exo_double(fp, row.st_meterr2);
    write_exo_string(fp, row.st_metratio);
    write_exo_string(fp, row.st_met_source);
    write_exo_string(fp, row.pl_bmassprov);
    const double errors[] =
    {
        row.pl_bmasseerr1, row.pl_bmasseerr2, row.pl_bmassjerr1, row.pl_bmassjerr2,
        row.pl_radeerr1, row.pl_radeerr2, row.pl_radjerr1, row.pl_radjerr2, row.pl_bmasselim, row.pl_radelim
    };
    for (double value : errors) write_exo_double(fp, value);
    write_exo_double(fp, row.transiting ? 1 : 0);

    fputc('\n', fp);
}

bool CatalogReader::exorow_read_line(FILE* fp, ExoRow& row)
{
    char buffer[2048];
    do
    {
        if (!fgets(buffer, sizeof(buffer), fp)) return false;
    }
    while (buffer[0] == '#');

    char* eol = strchr(buffer, '\n');
    if (eol) *eol = 0;
    eol = strchr(buffer, '\r');
    if (eol) *eol = 0;

    char* cursor = buffer;
    row.pl_name     = read_exo_string(cursor);
    row.hostname    = read_exo_string(cursor);
    row.hd_name     = read_exo_string(cursor);
    row.hip_name    = read_exo_string(cursor);
    row.st_spectype = read_exo_string(cursor);

    row.sy_dist      = read_exo_double(cursor);
    row.ra           = read_exo_double(cursor);
    row.dec          = read_exo_double(cursor);
    row.st_lum       = read_exo_double(cursor);
    row.sy_vmag      = read_exo_double(cursor);
    row.st_teff      = read_exo_double(cursor);
    row.st_mass      = read_exo_double(cursor);
    row.st_rad       = read_exo_double(cursor);
    row.st_rotp      = read_exo_double(cursor);
    row.pl_orbincl   = read_exo_double(cursor);
    row.pl_msinij    = read_exo_double(cursor);
    row.pl_msinie    = read_exo_double(cursor);
    row.pl_bmassj    = read_exo_double(cursor);
    row.pl_bmasse    = read_exo_double(cursor);
    row.pl_radj      = read_exo_double(cursor);
    row.pl_rade      = read_exo_double(cursor);
    row.pl_trueobliq = read_exo_double(cursor);
    row.pl_orbtper   = read_exo_double(cursor);
    row.pl_orblper   = read_exo_double(cursor);
    row.pl_tranmid   = read_exo_double(cursor);
    row.pl_orbper    = read_exo_double(cursor);
    row.pl_orbsmax   = read_exo_double(cursor);
    row.pl_orbeccen  = read_exo_double(cursor);
    row.pl_orbinclerr1 = read_exo_double(cursor);
    row.pl_orbinclerr2 = read_exo_double(cursor);
    row.st_met = read_exo_double(cursor);
    row.st_meterr1 = read_exo_double(cursor);
    row.st_meterr2 = read_exo_double(cursor);
    row.st_metratio = read_exo_string(cursor);
    row.st_met_source = read_exo_string(cursor);
    row.pl_bmassprov = read_exo_string(cursor);
    row.pl_bmasseerr1 = read_exo_double(cursor);
    row.pl_bmasseerr2 = read_exo_double(cursor);
    row.pl_bmassjerr1 = read_exo_double(cursor);
    row.pl_bmassjerr2 = read_exo_double(cursor);
    row.pl_radeerr1 = read_exo_double(cursor);
    row.pl_radeerr2 = read_exo_double(cursor);
    row.pl_radjerr1 = read_exo_double(cursor);
    row.pl_radjerr2 = read_exo_double(cursor);
    row.pl_bmasselim = read_exo_double(cursor);
    row.pl_radelim = read_exo_double(cursor);
    row.transiting = read_exo_double(cursor) == 1;

    return true;
}

bool CatalogReader::exorow_has_position(const ExoRow& row)
{
    return std::isfinite(row.ra) && row.ra >= 0 && row.ra < 360 &&
           std::isfinite(row.dec) && row.dec >= -90 && row.dec <= 90 &&
           std::isfinite(row.sy_dist) && row.sy_dist > 0;
}

std::map<std::string, ExoRow> CatalogReader::read_exoplanet_host_positions(const char* path)
{
    std::map<std::string, ExoRow> positions;
    std::ifstream input(path);
    if (!input)
    {
        std::cerr << "WARNING: Cannot open exoplanet host positions: " << path << std::endl;
        return positions;
    }

    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        std::istringstream fields(line);
        ExoRow position;
        std::getline(fields, position.hostname, '|');
        auto read_number = [&fields]() -> double
        {
            std::string value;
            std::getline(fields, value, '|');
            std::istringstream number(value);
            double result = NAN;
            if (!(number >> result))
            {
                return NAN;
            }
            number >> std::ws;
            return number.eof() ? result : NAN;
        };
        position.ra = read_number();
        position.dec = read_number();
        position.sy_dist = read_number();
        // Optional stellar fields follow the distance interval and Gaia identifier.
        std::string unused;
        for (int i = 0; i < 3; ++i)
        {
            std::getline(fields, unused, '|');
        }
        position.sy_vmag = read_number();
        std::getline(fields, position.st_spectype, '|');
        position.st_teff = read_number();
        position.st_rad = read_number();
        if (position.hostname.empty() || !exorow_has_position(position))
        {
            std::cerr << "WARNING: Invalid exoplanet host position: " << position.hostname << std::endl;
            continue;
        }
        positions.emplace(position.hostname, position);
    }
    return positions;
}

bool CatalogReader::apply_exoplanet_host_position(ExoRow& row, const std::map<std::string, ExoRow>& positions)
{
    if (exorow_has_position(row))
    {
        return false;
    }
    auto match = positions.find(row.hostname);
    if (match == positions.end())
    {
        return false;
    }
    // Keep coordinates and distance together from the same identified source.
    row.ra = match->second.ra;
    row.dec = match->second.dec;
    row.sy_dist = match->second.sy_dist;
    return true;
}

bool CatalogReader::accept_exoplanet_source(json& row, bool from_nasa)
{
    std::string host = row.value("hostname", "");
    std::string normalized;
    for (unsigned char c : host)
    {
        if (c != ' ' && c != '-')
        {
            normalized += std::tolower(c);
        }
    }
    if (from_nasa && normalized == "hd160691")
    {
        const std::string name = row.value("pl_name", "");
        if (name.size() == 11 && name.compare(0, 10, "HD 160691 ") == 0)
        {
            // NASA orders the outer companion as c; exoname.dat follows the
            // mu Ara convention: b=Quijote, c=Dulcinea, d=Rocinante, e=Sancho.
            char letter = name.back();
            if (letter == 'c') letter = 'e';
            else if (letter == 'd') letter = 'c';
            else if (letter == 'e') letter = 'd';
            if (letter >= 'b' && letter <= 'e')
            {
                row["pl_name"] = std::string("mu Ara ") + letter;
                row["hostname"] = "mu Ara";
            }
        }
    }
    if (normalized == "koi5" || normalized == "koi5a" ||
        normalized == "toi1241" || normalized == "toi1241a" || normalized == "kic8554498")
    {
        // EU's seven-day KOI-5 b is NASA's false-positive KOI-5.02.
        // Accept planets in this system only from NASA's confirmed-planet feed.
        if (!from_nasa || row.value("koi_disposition", "") == "FALSE POSITIVE")
        {
            return false;
        }
        row["hostname"] = "KOI-5";
        row["source_catalog"] = "NASA";
    }
    return true;
}

static int metallicity_source_priority(const std::string& source)
{
    if (source == "NASA pscomppars") return 0;
    if (source == "exoplanet.eu") return 1;
    if (source.rfind("XHIP V/137D", 0) == 0) return 2;
    return 3;
}

void CatalogReader::merge_exoplanet_metallicity(json& destination, const json& source)
{
    const auto valid = [](const json& row)
    {
        return row.contains("st_met") && row["st_met"].is_number() &&
            std::isfinite(row["st_met"].get<double>()) &&
            (!row.contains("st_metlim") || row["st_metlim"].is_null() || row["st_metlim"] == 0);
    };
    const auto origin = [](const json& row)
    {
        auto it = row.find("st_met_source");
        return it != row.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    if (!valid(source)) return;
    if (valid(destination) && metallicity_source_priority(origin(destination)) <=
        metallicity_source_priority(origin(source))) return;
    for (const char* key : {"st_met", "st_meterr1", "st_meterr2", "st_metlim", "st_metratio", "st_met_source"})
    {
        destination[key] = source.value(key, json());
    }
}

void CatalogReader::apply_exoplanet_metallicity(const ExoRow& row, Star* host)
{
    if (!host || !std::isfinite(row.st_met)) return;
    if (std::isfinite(host->metallicity) && metallicity_source_priority(host->metallicity_source) <=
        metallicity_source_priority(row.st_met_source)) return;
    host->metallicity = row.st_met;
    host->metallicity_error_upper = std::isfinite(row.st_meterr1) ? fabs(row.st_meterr1) : NAN;
    host->metallicity_error_lower = std::isfinite(row.st_meterr2) ? -fabs(row.st_meterr2) : NAN;
    host->metallicity_ratio = row.st_metratio;
    host->metallicity_source = row.st_met_source;
}

Star* CatalogReader::resolve_or_create_exostar(const ExoRow& row, bool loaded_starsonly, bool* was_new,
    ExactStarSearchIndex *lookup)
{
    std::lock_guard<std::recursive_mutex> catalog_lock(catalog_mutex);
    *was_new = false;
    std::string hostname = row.hostname;
    if (hostname == "QZ Ser (AB)")
    {
        hostname = "QZ Ser";
    }
    if (hostname == "eps Eridani")
    {
        hostname = "eps Eri";
    }
    char cinit = hostname.c_str()[0];

    if (cinit == 'P' && hostname.substr(0, 8) == "Proxima ")
    {
        hostname = "Proxima Cen";
    }
    else if (cinit == 'T' && hostname.substr(0, 11) == "Teegarden's")
    {
        hostname = "Teegarden's Star";
    }

    // 1. Resolve host star context: check if it already exists in global array
    Star* host_star = nullptr;
    if (!host_star && hostname.substr(0, 6) == "82 Eri" && hdcache && hdcache[20794])
    {
        host_star = hdcache[20794];
    }
    if (!host_star && (hostname == "eps Eri" || hostname == "eps Eridani") && hdcache && hdcache[22049])
    {
        host_star = hdcache[22049];
    }
    if (!host_star && hostname.substr(0, 6) == "mu Ara" && hdcache && hdcache[160691])
    {
        host_star = hdcache[160691];
    }

    int HD = 0;
    if (row.hd_name.size() > 2)
    {
        HD = atoi(&(row.hd_name.c_str()[2]));
    }
    if (!HD && hostname.size() >= 3 && hostname[0] == 'H' && hostname[1] == 'D')
    {
        HD = atoi(hostname.c_str() + ((hostname[2] == ' ') ? 3 : 2));
    }
    if (!host_star && HD > 0 && HD <= MAX_HD && hdcache && hdcache[HD])
    {
        host_star = hdcache[HD];
    }

    int HIP = 0;
    if (row.hip_name.size() > 3)
    {
        HIP = atoi(&(row.hip_name.c_str()[3]));
    }
    if (!HIP && hostname.size() >= 4 && hostname[0] == 'H' && hostname[1] == 'I' && hostname[2] == 'P')
    {
        HIP = atoi(hostname.c_str() + ((hostname[3] == ' ') ? 4 : 3));
    }
    if (!host_star && HIP > 0 && HIP <= MAX_HIP && hipcache && hipcache[HIP])
    {
        host_star = hipcache[HIP];
    }

    if (!host_star)
    {
        if (loaded_starsonly || worth_searching(hostname))
        {
            if (!strcmp(hostname.c_str(), "55 Cnc B"))
            {
                hostname = "GJ 324 B";
            }
            int i = find_object(hostname.c_str(), true, 9e29, lookup ? 1 : 3, lookup);
            if (i > 0)
            {
                host_star = (Star*)cels[i];
            }
            else
            {
                // Check if trailing component letter was present, e.g. "HD 100655 A" -> "HD 100655"
                size_t hlen = hostname.size();
                if (hlen > 2 && hostname[hlen - 2] == ' ' && hostname[hlen - 1] >= 'A' && hostname[hlen - 1] <= 'Z')
                {
                    std::string base_host = hostname.substr(0, hlen - 2);
                    int bi = find_object(base_host.c_str(), true, 9e29, lookup ? 1 : 3, lookup);
                    if (bi > 0 && cels[bi]->typeclass() == class_star)
                    {
                        host_star = (Star*)cels[bi];
                    }
                }
            }
        }
        else
        {
            for (int i = 1; !host_star && (i <= 10); i++)
            {
                int j = ncelobjs - i;
                if (j < 0)
                {
                    continue;
                }
                if (cels[j]->type == star && !strcmp(cels[j]->name, hostname.c_str()))
                {
                    host_star = (Star*)cels[j];
                }
            }
        }
    }

    // If the star doesn't exist, instantiate it
    if (!host_star)
    {
        if (ncelobjs >= MAX_CELOBJS)
        {
            return nullptr;
        }
        bool star_exists = false;

        host_star = new Star();
        host_star->type = star;
        host_star->cenobj = host_star;
        snprintf(host_star->name, sizeof(host_star->name), "%s", hostname.c_str());
        host_star->namelen = strlen(host_star->name);

        // Fill in fields the search relies on.
        size_t name_len = strlen(host_star->name);
        if (name_len >= 3 && strncmp(host_star->name, "GJ ", 3) == 0)
        {
            strncpy(host_star->Gliese, host_star->name, sizeof(host_star->Gliese) - 1);
            host_star->Gliese[sizeof(host_star->Gliese) - 1] = '\0';
        }
        else if (name_len >= 3 && strncmp(host_star->name, "HD ", 3) == 0)
        {
            host_star->HD = atoi(&host_star->name[3]);
            if (host_star->HD <= MAX_HD)
            {
                if (hdcache && hdcache[host_star->HD])
                {
                    delete host_star;
                    host_star = hdcache[host_star->HD];
                    star_exists = true;
                }
            }
        }
        else if (name_len >= 4 && strncmp(host_star->name, "HIP ", 4) == 0)
        {
            host_star->HIP = atoi(&host_star->name[4]);
            if (host_star->HIP <= MAX_HIP)
            {
                if (hipcache && hipcache[host_star->HIP])
                {
                    delete host_star;
                    host_star = hipcache[host_star->HIP];
                    star_exists = true;
                }
            }
        }

        double sy_dist = isnan(row.sy_dist) ? 0 : row.sy_dist * parsec;
        double ra  = isnan(row.ra)  ? 0 : row.ra  * fiftyseventh;
        double dec = isnan(row.dec) ? 0 : row.dec * fiftyseventh;

        // Physical properties from row
        if (row.st_spectype.size())
        {
            strcpy(host_star->spectral_type, row.st_spectype.c_str());
            if (!host_star->BV_color)
            {
                host_star->BV_color = Star::interpolate_mseq_BV(Star::get_mseqidx_from_sptyp(host_star->spectral_type));
            }
        }

        if (!isnan(row.st_teff))
        {
            host_star->temperature = row.st_teff;
            host_star->estimate_BV(host_star->temperature);
        }

        if (!isnan(row.st_mass))
        {
            host_star->mass = row.st_mass * solar_mass;
        }

        if (!isnan(row.st_rad))
        {
            host_star->volumetric_mean_radius = row.st_rad * solar_radius;
        }

        if (!isnan(row.st_rotp))
        {
            host_star->sidereal_rotational_period = row.st_rotp * oneday;
        }

        if (host_star->is_white_dwarf() && (!(host_star->volumetric_mean_radius > 0) ||
            !std::isfinite(host_star->volumetric_mean_radius))) host_star->estimate_radius();

        // Position and distance
        if (exorow_has_position(row))
        {
            host_star->distance = sy_dist;
            host_star->right_ascension = ra;
            host_star->declination = dec;
            host_star->update_location(simnow);
            host_star->distance_known = true;
        }
        else if (ra && dec && !isnan(row.st_lum) && !isnan(row.sy_vmag) && row.sy_vmag < 1e203)
        {
            host_star->right_ascension = ra;
            host_star->declination = dec;
        }
        else
        {
            std::cerr << "WARNING: Missing ra/dec or distance for " << hostname << std::endl;
            delete host_star;
            return nullptr;
        }

        // Apparent magnitude from row if present
        bool has_row_vmag = (!isnan(row.sy_vmag) && row.sy_vmag > -30.0 && row.sy_vmag < 60.0);
        if (has_row_vmag)
        {
            host_star->apparent_magnitude = row.sy_vmag;
        }

        // Absolute magnitude
        if (!isnan(row.st_lum))
        {
            double lum = pow(10.0, row.st_lum);
            if (lum > 0)
            {
                double m_bol = 4.74 - log(lum) / log(magnbase);
                double teff = (host_star->temperature > 0) ? host_star->temperature : sun_temp;
                host_star->absolute_magnitude = m_bol - Star::bolometric_correction(teff);
            }
        }
        else if (has_row_vmag && host_star->distance > 0)
        {
            // Case B: st_lum missing, but observed apparent magnitude and distance are known (e.g. K2-106).
            // Compute true absolute magnitude from distance modulus.
            double intrinsic_brightness = pow(magnbase, -host_star->apparent_magnitude) * pow(fmax(AU, host_star->distance) / parsec / 10.0, 2);
            host_star->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
        }
        else if (host_star->volumetric_mean_radius > 0 && host_star->temperature > 0)
        {
            // Case C: Neither st_lum nor sy_vmag is known (e.g. K2-151 B), but radius and temperature are known.
            // Calculate luminosity via Stefan-Boltzmann: L = (R/R_sun)^2 * (T/T_sun)^4
            double r_sun = host_star->volumetric_mean_radius / solar_radius;
            double t_ratio = host_star->temperature / sun_temp;
            double lum = r_sun * r_sun * pow(t_ratio, 4.0);
            if (lum > 0)
            {
                double m_bol = 4.74 - log(lum) / log(magnbase);
                host_star->absolute_magnitude = m_bol - Star::bolometric_correction(host_star->temperature);
            }
        }
        else
        {
            // Case D: Fall back to expected main sequence magnitude based on spectral type / temp
            host_star->absolute_magnitude = host_star->expected_main_sequence_absmag();
            if (isinf(host_star->absolute_magnitude) || isnan(host_star->absolute_magnitude) || host_star->absolute_magnitude > 50.0)
            {
                host_star->absolute_magnitude = 10.0;
            }
        }

        // Complete apparent magnitude or distance if one was missing
        if (!has_row_vmag)
        {
            if (host_star->distance > 0 && !isinf(host_star->absolute_magnitude) && !isnan(host_star->absolute_magnitude))
            {
                host_star->apparent_magnitude = host_star->absolute_magnitude + 5.0 * (log10(fmax(AU, host_star->distance) / (parsec * 10.0)));
            }
            else
            {
                host_star->apparent_magnitude = 11.0;
            }
        }

        if (host_star->distance <= 0)
        {
            host_star->distance = host_star->distance_from_magnitudes(host_star->apparent_magnitude, host_star->absolute_magnitude);
            host_star->update_location(simnow);
        }

        if (!star_exists)
        {
            append_cel(host_star);
            if (lookup) lookup->add(host_star->seqno, *host_star);
            // Publish lookup pointers only after position validation succeeds.
            if (host_star->HD > 0 && host_star->HD <= MAX_HD && hdcache)
            {
                hdcache[host_star->HD] = host_star;
            }
            if (host_star->HIP > 0 && host_star->HIP <= MAX_HIP && hipcache)
            {
                hipcache[host_star->HIP] = host_star;
            }
        }
        *was_new = !star_exists;
    }

    if (!isnan(row.st_mass) && host_star->mass <= 0)
    {
        host_star->mass = row.st_mass * solar_mass;
    }
    if (!isnan(row.st_rad) && host_star->volumetric_mean_radius <= 0)
    {
        host_star->volumetric_mean_radius = row.st_rad * solar_radius;
    }
    if (row.st_spectype.size() && !host_star->spectral_type[0])
    {
        strcpy(host_star->spectral_type, row.st_spectype.c_str());
        if (!host_star->BV_color)
        {
            host_star->BV_color = Star::interpolate_mseq_BV(Star::get_mseqidx_from_sptyp(host_star->spectral_type));
        }
    }
    if (!isnan(row.st_rotp) && host_star->sidereal_rotational_period <= 0)
    {
        host_star->sidereal_rotational_period = row.st_rotp * oneday;
    }

    if (host_star->is_white_dwarf() && (!(host_star->volumetric_mean_radius > 0) ||
        !std::isfinite(host_star->volumetric_mean_radius))) host_star->estimate_radius();
    apply_stellar_abundances(host_star);
    return host_star;
}

bool CatalogReader::add_exoplanet_from_row(const ExoRow& row, Star* host_star, std::map<int, std::vector<int>>& planet_celids, unsigned int& result)
{
    std::lock_guard<std::recursive_mutex> catalog_lock(catalog_mutex);
    // 2. Instantiate and fill target Planet properties
    Planet* new_planet = new Planet();
    snprintf(new_planet->name, sizeof(new_planet->name), "%s", row.pl_name.c_str());
    new_planet->cenobj = (host_star && host_star->cenobj) ? host_star->cenobj : host_star;

    double inclination = accepted_exoplanet_inclination(row.pl_orbincl * fiftyseventh);
    if (row.pl_orbincl == 90.0 && !row.transiting) inclination = 0;
    new_planet->transiting = row.transiting;

    double pl_mass = 0, pl_msini = 0;
    bool pl_massknown = false, pl_msini_known = false, pl_radknown = false;
    if (!isnan(row.pl_msinij))
    {
        new_planet->mass = row.pl_msinij * jupiter_mass;
        pl_msini_known = true;
        pl_msini = new_planet->mass;
        new_planet->msini = pl_msini;
    }
    else if (!isnan(row.pl_msinie))
    {
        new_planet->mass = row.pl_msinie * earth_mass;
        pl_msini_known = true;
        pl_msini = new_planet->mass;
        new_planet->msini = pl_msini;
    }

    if (!isnan(row.pl_bmassj))
    {
        new_planet->mass = row.pl_bmassj * jupiter_mass;
        new_planet->mass_error_upper = row.pl_bmassjerr1 * jupiter_mass;
        new_planet->mass_error_lower = row.pl_bmassjerr2 * jupiter_mass;
        pl_massknown = true;
        pl_mass = new_planet->mass;
    }
    else if (!isnan(row.pl_bmasse))
    {
        new_planet->mass = row.pl_bmasse * earth_mass;
        new_planet->mass_error_upper = row.pl_bmasseerr1 * earth_mass;
        new_planet->mass_error_lower = row.pl_bmasseerr2 * earth_mass;
        pl_massknown = true;
        pl_mass = new_planet->mass;
    }

    if (!isnan(row.pl_radj))
    {
        new_planet->volumetric_mean_radius = row.pl_radj * jupiter_radius;
        new_planet->radius_error_upper = row.pl_radjerr1 * jupiter_radius;
        new_planet->radius_error_lower = row.pl_radjerr2 * jupiter_radius;
        pl_radknown = true;
    }
    else if (!isnan(row.pl_rade))
    {
        new_planet->volumetric_mean_radius = row.pl_rade * earth_radius;
        new_planet->radius_error_upper = row.pl_radeerr1 * earth_radius;
        new_planet->radius_error_lower = row.pl_radeerr2 * earth_radius;
        pl_radknown = true;
    }

    new_planet->inclination_error_upper = row.pl_orbinclerr1 * fiftyseventh;
    new_planet->inclination_error_lower = row.pl_orbinclerr2 * fiftyseventh;

    if (!isnan(row.pl_trueobliq))
        new_planet->obliquity = row.pl_trueobliq * fiftyseventh;

    // 3. Allocate and map the planetary Orbit data structure
    Orbit* orb = new Orbit();
    orb->center = host_star;
    orb->center_name = host_star->name;

    double pl_tranmid=0, pl_orbtper=0, pl_orblper=0;

    // Subtract light travel time to get true epochs.
    if (!isnan(row.pl_orbtper)) pl_orbtper = row.pl_orbtper - host_star->distance/light_year*oneyear/oneday;
    if (!isnan(row.pl_orblper)) pl_orblper = row.pl_orblper;
    if (!isnan(row.pl_tranmid)) pl_tranmid = row.pl_tranmid - host_star->distance/light_year*oneyear/oneday;

    if (!isnan(row.pl_orbper))
        orb->period = row.pl_orbper * oneday;
    if (!isnan(row.pl_orbsmax))
        orb->semimajor_axis = row.pl_orbsmax * AU;
    if (!isnan(row.pl_orbeccen))
        orb->eccentricity = row.pl_orbeccen;

    if (orb->period == 0 && orb->semimajor_axis > 0)
    {
        orb->compute_period(new_planet->mass);
    }
    else if (orb->semimajor_axis == 0 && orb->period > 0)
    {
        orb->compute_semimajor_axis(new_planet->mass);
    }

    orb->inclination = inclination;
    if (inclination)
    {
        host_star->planets_heliocen_inclination = inclination;
        if (host_star->disk_heliocen_node) host_star->planets_heliocen_node = host_star->disk_heliocen_node;
        else if (host_star->rot_heliocen_node) host_star->planets_heliocen_node = host_star->rot_heliocen_node;
    }

    if (pl_orblper)
    {
        orb->arg_periapsis = pl_orblper * fiftyseventh;
    }
    if (pl_orbtper)
    {
        orb->epoch = new_planet->epoch = pl_orbtper;
        orb->mean_anomaly = 0;
    }
    else if (pl_tranmid)
    {
        host_star->update_location(simnow);
        orb->epoch = new_planet->epoch = pl_tranmid;
        double pl_tranmid_timet = (pl_tranmid - J2000) * oneyear + J2000_TIME_T;
        orb->mean_anomaly = (360 - pl_orblper) * fiftyseventh + cels[0]->RA_as_radians(host_star->location, 0);
        for (int i=0; i<10; i++)
        {
            new_planet->update_location(pl_tranmid_timet);
            orb->mean_anomaly += cels[0]->RA_as_radians(host_star->location, 0) - new_planet->RA_as_radians(host_star->location, 0);
        }
    }

    for (const int h : host_star->pl_indices)
    {
        if (!cels[h] || !cels[h]->orbit) continue;
        if (fabs(cels[h]->orbit->period - orb->period) < 0.15 * fmin(cels[h]->orbit->period, orb->period)
            && fabs(cels[h]->orbit->semimajor_axis - orb->semimajor_axis) < 0.15 * fmin(cels[h]->orbit->semimajor_axis, orb->semimajor_axis)
            )
        {
            if (orb->inclination && !cels[h]->orbit->inclination) cels[h]->orbit->inclination = orb->inclination;
            if (orb->arg_periapsis && !cels[h]->orbit->arg_periapsis) cels[h]->orbit->arg_periapsis = orb->arg_periapsis;
            if ((orb->mean_anomaly && !cels[h]->orbit->mean_anomaly) || (orb->epoch != J2000 && cels[h]->orbit->epoch != J2000))
            {
                cels[h]->orbit->epoch = orb->epoch;
                cels[h]->orbit->mean_anomaly = orb->mean_anomaly;
            }
            delete orb;
            delete new_planet;
            return false;             // Skip adding a duplicate planet.
            // std::cerr << "WARNING: " << row.pl_name << " is too similar to " << cels[h]->name << "; possible duplicate." << std::endl;
            // break;
        }
    }

    new_planet->orbit = orb;
    new_planet->update_location(simnow);

    // 4. Run automated estimation/classification fallbacks provided in class declarations.
    // Can the reported "true" mass be trusted, or should it be recomputed from msini using the
    // star's own disk/rotation inclination? A pl_mass that just equals msini (or msini/sin i) is
    // the archive relabelling msini rather than an independent measurement -- recompute those.
    const double mass_consistency_tolerance = 0.10;
    bool mass_untrustworthy = false;
    if (pl_msini_known)
    {
        if (!pl_massknown)
        {
            mass_untrustworthy = true;
        }
        else
        {
            double expected_mass = inclination ? (pl_msini / sin(inclination)) : pl_msini;
            bool consistent = fabs(pl_mass - expected_mass) <= mass_consistency_tolerance * expected_mass;
            mass_untrustworthy = (expected_mass < (earth_mass*4200)) && (inclination ? !consistent : consistent);
        }
    }

    if (mass_untrustworthy)
    {
        double st_incl = inclination;
        if (!st_incl && host_star->disk_heliocen_inclination)
        {
            st_incl = host_star->disk_heliocen_inclination;           // e.g. Eps Eri, Tau Cet, 82 Eri
        }
        else if (!st_incl && host_star->rot_heliocen_incl)
        {
            st_incl = host_star->rot_heliocen_incl;                      // e.g. Alp Men
        }

        if (sin(st_incl) >= 0.1)
        {
            new_planet->mass = pl_msini / sin(st_incl);
            std::cout << "Mass of " << new_planet->name << " computed at " << (new_planet->mass / earth_mass)
                      << " m(Earth) based on system inclination " << (st_incl * fiftyseven) << std::endl;
            pl_massknown = true;
        }
    }

    new_planet->mass_evidence = pl_massknown ? PlanetMeasurement::observed
        : pl_msini_known ? PlanetMeasurement::minimum : PlanetMeasurement::unknown;
    if (row.pl_bmassprov == "M-R relationship" || (mass_untrustworthy && pl_massknown))
        new_planet->mass_evidence = PlanetMeasurement::estimated;
    else if (row.pl_bmassprov == "Msini") new_planet->mass_evidence = PlanetMeasurement::minimum;
    new_planet->radius_evidence = pl_radknown ? PlanetMeasurement::observed : PlanetMeasurement::unknown;
    if (std::isfinite(row.pl_bmasselim) && row.pl_bmasselim != 0)
        new_planet->mass_evidence = row.pl_bmasselim > 0 ? PlanetMeasurement::minimum : PlanetMeasurement::estimated;
    if (std::isfinite(row.pl_radelim) && row.pl_radelim != 0) new_planet->radius_evidence = PlanetMeasurement::estimated;
    new_planet->classify(new_planet->is_in_con_HZ(), (pl_massknown || pl_msini_known) && pl_radknown);

    new_planet->estimate_albedo_and_absmagn();
    new_planet->set_color_from_type(new_planet->is_in_con_HZ());
    new_planet->estimate_rotation();
    new_planet->setup_atm_ring_props();

    // Append planet object to global array
    append_cel(new_planet);
    result++;
    host_star->has_planets++;
    if (new_planet->is_in_con_HZ()) host_star->has_hz_planets++;
    host_star->pl_indices.push_back(new_planet->seqno);

    planet_celids[host_star->seqno].push_back(new_planet->seqno);
    return true;
}

void CatalogReader::dedup_planets(json& planets_array)
{
    if (!planets_array.is_array() || planets_array.empty())
    {
        return;
    }

    auto get_sma = [](const json& item) -> double
    {
        if (item.contains("pl_orbsmax") && item["pl_orbsmax"].is_number())
        {
            double v = item["pl_orbsmax"].get<double>();
            if (v > 0)
            {
                return v;
            }
        }
        if (item.contains("pl_orbper") && item["pl_orbper"].is_number())
        {
            double p = item["pl_orbper"].get<double>();
            if (p > 0)
            {
                double per_yr = p / 365.25;
                double st_m = (item.contains("st_mass") && item["st_mass"].is_number()) ? item["st_mass"].get<double>() : 1.0;
                if (st_m <= 0)
                {
                    st_m = 1.0;
                }
                return cbrt(st_m * per_yr * per_yr);
            }
        }
        return 0.0;
    };

    // Prune invalid or unbound items
    json pruned_array = json::array();
    for (auto& item : planets_array)
    {
        std::string pl_name = item.value("pl_name", "");
        std::string hostname = item.value("hostname", "");
        if (pl_name.empty() || hostname.empty())
        {
            continue;
        }
        if (pl_name.rfind("Exocomet", 0) == 0)
        {
            continue;
        }
        double per = (item.contains("pl_orbper") && item["pl_orbper"].is_number()) ? item["pl_orbper"].get<double>() : 0.0;
        double sma = get_sma(item);
        if (per <= 0 && sma > 100.0 && pl_name != "WD 0806-661 b")
        {
            continue;
        }
        pruned_array.push_back(std::move(item));
    }
    planets_array = std::move(pruned_array);

    struct PlanetCandidate
    {
        size_t orig_idx;
        double ra;
        double dec;
        bool has_coords;
        double per;
        double sma;
        std::string comp;
        std::string letter;
        std::string hostname;
        std::string pl_name;
        std::string norm_h;
    };

    auto extract_comp = [](const std::string& pl_name, const std::string& host) -> std::string
    {
        for (const std::string& s : {pl_name, host})
        {
            if (s.find("(AB)") != std::string::npos || s.find("(ab)") != std::string::npos ||
                s.find("(Ab)") != std::string::npos || s.find("(aB)") != std::string::npos)
            {
                return "AB";
            }
            size_t ab_pos = s.find("AB");
            while (ab_pos != std::string::npos)
            {
                bool prev_b = (ab_pos == 0 || s[ab_pos - 1] == ' ' || s[ab_pos - 1] == '(');
                bool next_b = (ab_pos + 2 == s.length() || s[ab_pos + 2] == ' ' || s[ab_pos + 2] == ')');
                if (prev_b && next_b)
                {
                    return "AB";
                }
                ab_pos = s.find("AB", ab_pos + 1);
            }
        }

        auto check_str = [](const std::string& s, bool is_pl) -> char
        {
            if (s.empty())
            {
                return 0;
            }
            for (char c : {'A', 'B', 'C', 'D'})
            {
                std::string spaced = std::string(" ") + c;
                size_t pos = s.find(spaced);
                while (pos != std::string::npos)
                {
                    size_t nxt = pos + spaced.length();
                    if (nxt == s.length() || s[nxt] == ' ')
                    {
                        return c;
                    }
                    if (is_pl)
                    {
                        if (s[nxt] >= 'b' && s[nxt] <= 'z')
                        {
                            if (nxt + 1 == s.length() || s[nxt + 1] == ' ')
                            {
                                return c;
                            }
                        }
                        else if (s[nxt] == ' ' && nxt + 1 < s.length() && s[nxt + 1] >= 'b' && s[nxt + 1] <= 'z')
                        {
                            if (nxt + 2 == s.length() || s[nxt + 2] == ' ')
                            {
                                return c;
                            }
                        }
                    }
                    pos = s.find(spaced, pos + 1);
                }
            }
            if (s.length() >= 2 && s[s.length() - 2] == ' ' && s.back() >= 'A' && s.back() <= 'D')
            {
                return s.back();
            }
            return 0;
        };

        char c = check_str(host, false);
        if (c)
        {
            return std::string(1, c);
        }
        c = check_str(pl_name, true);
        if (c)
        {
            return std::string(1, c);
        }
        return "";
    };

    auto extract_let = [](const std::string& name) -> std::string
    {
        for (char sep : {' ', '-'})
        {
            size_t pos = name.rfind(sep);
            if (pos != std::string::npos && pos + 1 < name.length())
            {
                std::string tail = name.substr(pos + 1);
                bool lower = true;
                for (char c : tail)
                {
                    if (!std::islower(c))
                    {
                        lower = false;
                        break;
                    }
                }
                if (lower && !tail.empty())
                {
                    return tail;
                }
            }
        }
        if (!name.empty() && std::islower(name.back()))
        {
            return std::string(1, name.back());
        }
        return "";
    };

    auto normalize_host = [](const std::string& host) -> std::string
    {
        std::string h = host;
        while (!h.empty() && (h.back() == ' ' || h.back() == '\t'))
        {
            h.pop_back();
        }
        if (h.length() >= 2 && h[h.length() - 2] == ' ' && h.back() >= 'A' && h.back() <= 'D')
        {
            h = h.substr(0, h.length() - 2);
            while (!h.empty() && (h.back() == ' ' || h.back() == '\t'))
            {
                h.pop_back();
            }
        }
        std::string res;
        for (char c : h)
        {
            if (c != ' ' && c != '-')
            {
                res += std::tolower(c);
            }
        }
        return res;
    };

    size_t n = planets_array.size();
    std::vector<PlanetCandidate> candidates(n);
    for (size_t i = 0; i < n; ++i)
    {
        const auto& item = planets_array[i];
        candidates[i].orig_idx = i;
        candidates[i].has_coords = false;
        candidates[i].ra = 0.0;
        candidates[i].dec = 0.0;
        if (item.contains("ra") && item["ra"].is_number() &&
            item.contains("dec") && item["dec"].is_number())
        {
            candidates[i].ra = item["ra"].get<double>();
            candidates[i].dec = item["dec"].get<double>();
            candidates[i].has_coords = true;
        }
        candidates[i].hostname = item.value("hostname", "");
        candidates[i].norm_h = normalize_host(candidates[i].hostname);
        candidates[i].pl_name = item.value("pl_name", "");
        candidates[i].per = (item.contains("pl_orbper") && item["pl_orbper"].is_number()) ? item["pl_orbper"].get<double>() : 0.0;
        candidates[i].sma = get_sma(item);
        candidates[i].comp = extract_comp(candidates[i].pl_name, candidates[i].hostname);
        candidates[i].letter = extract_let(candidates[i].pl_name);
    }

    auto is_single_comp = [](const std::string& c) -> bool
    {
        return c == "A" || c == "B" || c == "C" || c == "D";
    };

    std::vector<size_t> coord_indices;
    coord_indices.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        if (candidates[i].has_coords)
        {
            coord_indices.push_back(i);
        }
    }
    std::sort(coord_indices.begin(), coord_indices.end(), [&](size_t a, size_t b)
    {
        return candidates[a].dec < candidates[b].dec;
    });

    std::vector<bool> removed(n, false);

    // 1. Spatial matching among coordinate-bearing planets
    size_t n_coords = coord_indices.size();
    for (size_t i = 0; i < n_coords; ++i)
    {
        size_t idx1 = coord_indices[i];
        if (removed[idx1])
        {
            continue;
        }

        double dec1 = candidates[idx1].dec;
        double ra1 = candidates[idx1].ra;

        for (size_t j = i + 1; j < n_coords; ++j)
        {
            size_t idx2 = coord_indices[j];
            if (candidates[idx2].dec - dec1 > 0.04)
            {
                break;
            }
            if (removed[idx2])
            {
                continue;
            }

            double cosdec = cos((dec1 + candidates[idx2].dec) * 0.5 * fiftyseventh);
            double d_ra_deg = fabs(ra1 - candidates[idx2].ra);
            if (d_ra_deg > 180.0)
            {
                d_ra_deg = 360.0 - d_ra_deg;
            }
            double d_ra = d_ra_deg * cosdec;
            if (d_ra > 0.04)
            {
                continue;
            }

            double dist_arcsec = hypot(d_ra, candidates[idx2].dec - dec1) * 3600.0;
            if (dist_arcsec > 90.0)
            {
                continue;
            }

            // Stellar companion protection:
            // If binary member A vs member B, DO NOT merge!
            if (is_single_comp(candidates[idx1].comp) && is_single_comp(candidates[idx2].comp) &&
                candidates[idx1].comp != candidates[idx2].comp)
            {
                continue;
            }
            if ((candidates[idx1].comp == "AB" && candidates[idx2].comp == "B") ||
                (candidates[idx1].comp == "B" && candidates[idx2].comp == "AB"))
            {
                continue;
            }

            // Planet letter protection: 'b' vs 'c'
            if (!candidates[idx1].letter.empty() && !candidates[idx2].letter.empty() &&
                candidates[idx1].letter != candidates[idx2].letter)
            {
                continue;
            }

            // Orbit match check:
            bool orbit_match = false;
            double per1 = candidates[idx1].per, per2 = candidates[idx2].per;
            double sma1 = candidates[idx1].sma, sma2 = candidates[idx2].sma;
            if (per1 > 0 && per2 > 0)
            {
                double max_p = std::max(per1, per2);
                if (max_p > 0 && fabs(per1 - per2) / max_p <= 0.18)
                {
                    orbit_match = true;
                }
            }
            if (!orbit_match && sma1 > 0 && sma2 > 0)
            {
                double max_s = std::max(sma1, sma2);
                if (max_s > 0 && fabs(sma1 - sma2) / max_s <= 0.18)
                {
                    orbit_match = true;
                }
            }

            if (orbit_match)
            {
                auto& p1_json = planets_array[idx1];
                const auto& p2_json = planets_array[idx2];
                merge_exoplanet_metallicity(p1_json, p2_json);
                for (auto& [key, val] : p2_json.items())
                {
                    if (key.rfind("st_met", 0) == 0) continue;
                    if ((key == "pl_orbinclerr1" || key == "pl_orbinclerr2") &&
                        p1_json.contains("pl_orbincl") && !p1_json["pl_orbincl"].is_null() &&
                        p1_json["pl_orbincl"] != p2_json.value("pl_orbincl", json())) continue;
                    if (!val.is_null())
                    {
                        if (!p1_json.contains(key) || p1_json[key].is_null())
                        {
                            p1_json[key] = val;
                        }
                    }
                }
                if (candidates[idx1].per <= 0 && per2 > 0)
                {
                    candidates[idx1].per = per2;
                }
                if (candidates[idx1].sma <= 0 && sma2 > 0)
                {
                    candidates[idx1].sma = sma2;
                }
                removed[idx2] = true;
            }
        }
    }

    // 2. Same-host matching for planets where coordinates might be missing,
    // grouped by normalized hostname
    std::unordered_map<std::string, std::vector<size_t>> by_host;
    for (size_t i = 0; i < n; ++i)
    {
        if (removed[i])
        {
            continue;
        }
        if (!candidates[i].norm_h.empty())
        {
            by_host[candidates[i].norm_h].push_back(i);
        }
    }

    for (auto& [hname, indices] : by_host)
    {
        if (indices.size() < 2)
        {
            continue;
        }
        for (size_t i = 0; i < indices.size(); ++i)
        {
            size_t idx1 = indices[i];
            if (removed[idx1])
            {
                continue;
            }
            for (size_t j = i + 1; j < indices.size(); ++j)
            {
                size_t idx2 = indices[j];
                if (removed[idx2])
                {
                    continue;
                }

                if (is_single_comp(candidates[idx1].comp) && is_single_comp(candidates[idx2].comp) &&
                    candidates[idx1].comp != candidates[idx2].comp)
                {
                    continue;
                }
                if ((candidates[idx1].comp == "AB" && candidates[idx2].comp == "B") ||
                    (candidates[idx1].comp == "B" && candidates[idx2].comp == "AB"))
                {
                    continue;
                }
                if (!candidates[idx1].letter.empty() && !candidates[idx2].letter.empty() &&
                    candidates[idx1].letter != candidates[idx2].letter)
                {
                    continue;
                }

                bool orbit_match = false;
                double per1 = candidates[idx1].per, per2 = candidates[idx2].per;
                double sma1 = candidates[idx1].sma, sma2 = candidates[idx2].sma;
                if (per1 > 0 && per2 > 0)
                {
                    double max_p = std::max(per1, per2);
                    if (max_p > 0 && fabs(per1 - per2) / max_p <= 0.18)
                    {
                        orbit_match = true;
                    }
                }
                if (!orbit_match && sma1 > 0 && sma2 > 0)
                {
                    double max_s = std::max(sma1, sma2);
                    if (max_s > 0 && fabs(sma1 - sma2) / max_s <= 0.18)
                    {
                        orbit_match = true;
                    }
                }

                if (orbit_match)
                {
                    auto& p1_json = planets_array[idx1];
                    const auto& p2_json = planets_array[idx2];
                    merge_exoplanet_metallicity(p1_json, p2_json);
                    for (auto& [key, val] : p2_json.items())
                    {
                        if (key.rfind("st_met", 0) == 0) continue;
                        if ((key == "pl_orbinclerr1" || key == "pl_orbinclerr2") &&
                            p1_json.contains("pl_orbincl") && !p1_json["pl_orbincl"].is_null() &&
                            p1_json["pl_orbincl"] != p2_json.value("pl_orbincl", json())) continue;
                        if (!val.is_null())
                        {
                            if (!p1_json.contains(key) || p1_json[key].is_null())
                            {
                                p1_json[key] = val;
                            }
                        }
                    }
                    if (candidates[idx1].per <= 0 && per2 > 0)
                    {
                        candidates[idx1].per = per2;
                    }
                    if (candidates[idx1].sma <= 0 && sma2 > 0)
                    {
                        candidates[idx1].sma = sma2;
                    }
                    removed[idx2] = true;
                }
            }
        }
    }

    json new_array = json::array();
    for (size_t i = 0; i < n; ++i)
    {
        if (!removed[i])
        {
            new_array.push_back(std::move(planets_array[i]));
        }
    }
    planets_array = std::move(new_array);

    // Sort by host name and then by sma
    std::sort(planets_array.begin(), planets_array.end(), [get_sma](const json& a, const json& b)
    {
        if (a.contains("hostname") && b.contains("hostname")
            && a["hostname"].is_string() && b["hostname"].is_string()
            && a["hostname"] != b["hostname"]
            )
        {
            return a["hostname"] < b["hostname"];
        }

        double sma_a = get_sma(a);
        double sma_b = get_sma(b);
        if (sma_a != sma_b)
        {
            return sma_a < sma_b;
        }

        return false;
    });
}

unsigned int CatalogReader::load_exoplanets_from_tap(bool stars_only)
{
    unsigned int result = 0;
    bool do_download = true;
    std::map<int, std::vector<int>> planet_celids;
    static bool loaded_starsonly = false;
    ExactStarSearchIndex host_lookup = []()
    {
        std::lock_guard<std::recursive_mutex> catalog_lock(catalog_mutex);
        return ExactStarSearchIndex();
    }();

    const char* exocache = "catalogs/exoplanets.json";
    const char* derived_cache = stars_only ? "catalogs/exoplanets_stars.dat" : "catalogs/exoplanets_planets.dat";
    const char* position_catalog = "catalogs/exoplanet_host_positions.dat";
    const auto host_positions = read_exoplanet_host_positions(position_catalog);

    std::stringstream lmss;
    lmss << "Loading exoplanets from unified TAP sources...";
    mtx.lock();
    loading_msg = lmss.str();
    mtx.unlock();

    const int cache_max_age_seconds = 12 * 3600; // 12 hours
    if (file_exists(exocache) && file_age(exocache) < cache_max_age_seconds)
    {
        do_download = false;
    }
    if (radio_silence)
    {
        do_download = false;
    }

    // Derived caches also expire when the fixed host supplement changes.
    bool derived_cache_valid = file_exists(derived_cache) &&
                               file_exists(exocache) &&
                               (file_age(derived_cache) <= file_age(exocache) + 60) &&
                               (!file_exists(position_catalog) ||
                                fs::last_write_time(derived_cache) >= fs::last_write_time(position_catalog));

    if (derived_cache_valid)
    {
        std::ifstream cache_header(derived_cache);
        std::string version;
        std::getline(cache_header, version);
        derived_cache_valid = version == "# EXO_TRANSIT3";
        // Recover omitted mu Ara records once, without downloading again when the
        // host pass has already refreshed the unified catalog for the planet pass.
        if (version == "# EXO_INCLINATION_ERRORS1" && !radio_silence)
        {
            std::ifstream source(exocache);
            const std::string cached_json((std::istreambuf_iterator<char>(source)),
                std::istreambuf_iterator<char>());
            if (cached_json.find("\"mu Ara ") == std::string::npos
                && cached_json.find("\"HD 160691 ") == std::string::npos)
            {
                do_download = true;
            }
        }
    }

    if (exoplanet_tap_attempted || !exoplanet_tap_catalog.empty())
    {
        do_download = false;
    }

    if (!do_download && derived_cache_valid && exoplanet_tap_catalog.empty())
    {
        mtx.lock();
        loading_msg = std::string("Loading exoplanets from cache...");
        mtx.unlock();

        FILE* fp = fopen(derived_cache, "rb");
        if (fp)
        {
            fseek(fp, 0, SEEK_END);
            long total_bytes = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            ExoRow row;
            int addedexo = 0;
            Star* last_host_star = nullptr;
            std::string last_hostname;
            int line_count = 0;

            while (exorow_read_line(fp, row))
            {
                std::lock_guard<std::recursive_mutex> catalog_lock(catalog_mutex);
                if (abort_load)
                {
                    break;
                }
                if (ncelobjs >= MAX_CELOBJS)
                {
                    break;
                }

                if (!strcasecmp(row.pl_name.c_str(), "eps Ind Bb")) continue;

                line_count++;
                if (exoplanet_loading.load() && (line_count & 31) == 0 && total_bytes > 0)
                {
                    long pos = ftell(fp);
                    float frac = (float)pos / (float)total_bytes;
                    if (frac > 1.0f)
                    {
                        frac = 1.0f;
                    }
                    if (stars_only)
                    {
                        exoplanet_load_progress.store(0.05f + 0.38f * frac);
                    }
                    else
                    {
                        exoplanet_load_progress.store(0.50f + 0.48f * frac);
                        exoplanet_count_loaded.store(addedexo);
                    }
                }

                Star* host_star = nullptr;
                bool was_new = false;

                if (!row.hostname.empty() && row.hostname == last_hostname && last_host_star)
                {
                    host_star = last_host_star;
                }
                else
                {
                    host_star = resolve_or_create_exostar(row, loaded_starsonly, &was_new, &host_lookup);
                    last_host_star = host_star;
                    last_hostname = row.hostname;
                }

                if (!host_star)
                {
                    continue;
                }
                apply_exoplanet_metallicity(row, host_star);
                if (stars_only)
                {
                    continue;
                }
                if (add_exoplanet_from_row(row, host_star, planet_celids, result))
                {
                    addedexo++;
                }
                if (!(addedexo & 0x7f))
                {
                    loading_msg = std::string("Loaded ") + std::to_string(addedexo) + std::string(" exoplanets from cache...");
                }
            }
            fclose(fp);

            if (abort_load)
            {
                return result;
            }
            apply_exoplanet_names(planet_celids, &host_lookup);
            if (stars_only)
            {
                loaded_starsonly = true;
            }
            return result;
        }
    }

    std::string readBufferNASA;
    std::string readBufferEU;
    json planets_array = exoplanet_tap_catalog;

    if (radio_silence) do_download = false;

    if (do_download && !radio_silence)
    {
        exoplanet_tap_attempted = true;
        // Reusable lambda to abstract libcurl boilerplate
        auto fetch_tap = [&](const std::string& url, std::string& out_buffer) -> bool
        {
            CURL* curl = curl_easy_init();
            if (!curl) return false;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_buffer);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            CURLcode res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            if (res != CURLE_OK)
            {
                out_buffer.clear();
                return false;
            }
            return true;
        };

        // 1. Fetch NASA Data
        std::string nasa_url = "https://exoplanetarchive.ipac.caltech.edu/TAP/sync?maxrec=20000&query="
                               "select+pl_name,hostname,hd_name,hip_name,tran_flag,discoverymethod,pl_orbper,pl_orbsmax,pl_orbeccen,pl_orbincl,pl_orbinclerr1,pl_orbinclerr2,pl_orblper,pl_orbtper,pl_tranmid,"
                               "pl_bmasse,pl_bmasseerr1,pl_bmasseerr2,pl_bmasselim,pl_bmassj,pl_bmassjerr1,pl_bmassjerr2,pl_bmassprov,"
                               "pl_msinij,pl_msinie,pl_rade,pl_radeerr1,pl_radeerr2,pl_radelim,pl_radj,pl_radjerr1,pl_radjerr2,pl_trueobliq,"
                               "st_mass,st_rad,sy_dist,ra,dec,sy_vmag,st_spectype,st_teff,st_lum,st_rotp,st_met,st_meterr1,st_meterr2,st_metlim,st_metratio+"
                               "from+pscomppars+order+by+pl_name+asc&format=json";

        if (!fetch_tap(nasa_url, readBufferNASA))
        {
            std::cerr << "NASA TAP Query failed." << std::endl;
        }

        // 2. Fetch exoplanet.eu Data via PADC VESPA TAP
        // ADQL column aliasing maps EU's schema directly onto NASA's nomenclature.
        std::string eu_url = "http://voparis-tap-planeto.obspm.fr/tap/sync?"
                             "request=doQuery&lang=ADQL&format=json&maxrec=20000&query="
                             "select+target_name+as+pl_name,alt_target_name,star_name+as+hostname,detection_type,"
                             "period+as+pl_orbper,semi_major_axis+as+pl_orbsmax,eccentricity+as+pl_orbeccen,inclination+as+pl_orbincl,"
                             "periastron+as+pl_orblper,t_peri+as+pl_orbtper,"
                             "mass+as+pl_bmassj,mass_sin_i+as+pl_msinij,radius+as+pl_radj,"
                             "ra,dec,star_distance+as+sy_dist,mag_v+as+sy_vmag,"
                             "star_mass+as+st_mass,star_radius+as+st_rad,star_teff+as+st_teff,star_spec_type+as+st_spectype,"
                             "star_metallicity+as+st_met,'[Fe/H]'+as+st_metratio+"
                             "from+exoplanet.epn_core+where+publication_status='Published+in+a+refereed+paper'+order+by+target_name+asc";

        if (!fetch_tap(eu_url, readBufferEU))
        {
            std::cerr << "Exoplanet.eu TAP Query failed." << std::endl;
        }

        try
        {
            // Utility lambdas for string parsing
            auto normalize_name = [](std::string name) -> std::string
            {
                const char* cstr = name.c_str();
                const int namelen = name.size();
                std::string name1 = name;

                // Match if star is component A, e.g. ups And Ab matches ups And b, or ups And A b matches ups And b.
                if (namelen > 3)
                {
                    const char last1 = cstr[namelen - 1], last2 = cstr[namelen - 2], last3 = cstr[namelen - 3];
                    if (last1 >= 'b' && last1 <= 'z' && last2 == 'A' && last3 == ' ')
                    {
                        name1 = name.substr(0, namelen - 2) + std::string(1, last1);
                    }
                }
                if (namelen > 4)
                {
                    const char last1 = cstr[namelen - 1], last2 = cstr[namelen - 2], last3 = cstr[namelen - 3], last4 = cstr[namelen - 4];
                    if (last1 >= 'b' && last1 <= 'z' && last2 == ' ' && last3 == 'A' && last4 == ' ')
                    {
                        name1 = name.substr(0, namelen - 4) + " " + std::string(1, last1);
                    }
                }

                std::string res;
                for (char c : name1)
                {
                    if (c != ' ' && c != '-') res += std::tolower(c);
                }
                return res;
            };

            auto extract_letter = [](const std::string& name) -> std::string
            {
                size_t pos = name.find_last_of(" -");
                if (pos != std::string::npos && pos + 1 < name.length())
                {
                    std::string tail = name.substr(pos + 1);
                    bool lower = true;
                    for (char c : tail) if (!std::islower(c)) lower = false;
                    if (lower && !tail.empty()) return tail;
                }
                if (!name.empty() && std::islower(name.back())) return std::string(1, name.back());
                return "";
            };

            auto extract_cat_num = [](const std::string& str, const std::string& prefix) -> int
            {
                std::string upper = str;
                for (char& c : upper) c = std::toupper(c);
                size_t pos = upper.find(prefix);
                if (pos == std::string::npos) return -1;
                pos += prefix.length();
                while (pos < upper.length() && !std::isdigit(upper[pos])) pos++;
                if (pos == upper.length()) return -1;
                return std::stoi(upper.substr(pos));
            };

            std::unordered_map<std::string, std::string> primary_keys; // Maps aliases -> canonical target
            std::unordered_map<std::string, json> merged_planets;

            // Unified processing pipeline
            auto register_planet = [&](const json& item, bool from_nasa)
            {
                json litem = item;
                if (!accept_exoplanet_source(litem, from_nasa))
                {
                    return;
                }
                litem["st_met_source"] = from_nasa ? "NASA pscomppars" : "exoplanet.eu";
                std::string pl_name = litem.value("pl_name", "");
                if (pl_name.empty()) return;
                char cinit = pl_name.c_str()[0];
                std::string hostname = litem.value("hostname", "");
                if (cinit == 'L' && pl_name.substr(0, 14) == "Luyten's Star ")
                {
                    pl_name = std::string("GJ 273 ") + pl_name.substr(pl_name.size()-1, 1);
                    litem["pl_name"] = pl_name;
                }
                else if (cinit == 'T' && pl_name.substr(0, 12) == "Teegarden's ")
                {
                    pl_name = std::string("Teegarden's Star ") + pl_name.substr(pl_name.size()-1, 1);
                    litem["pl_name"] = pl_name;
                }
                else if (cinit == 'B' && (pl_name.substr(0, 15) == "Barnard's star " || pl_name.substr(0, 8) == "Barnard "))
                {
                    pl_name = std::string("Barnard's Star ") + pl_name.substr(pl_name.size()-1, 1);
                    litem["pl_name"] = pl_name;
                }
                else if (cinit == 'P' && pl_name.substr(0, 8) == "Proxima ")
                {
                    pl_name = std::string("Proxima ") + pl_name.substr(pl_name.size()-1, 1);
                    litem["pl_name"] = pl_name;
                }
                else if (cinit == 'H')
                {
                    if (pl_name.substr(0, 9) == "HD 20794 ")
                    {
                        std::string let = extract_letter(pl_name);
                        if (let == "d")
                        {
                            let = "c";
                        }
                        else if (let == "f")
                        {
                            let = "d";
                        }
                        pl_name = "82 Eri " + let;
                        litem["pl_name"] = pl_name;
                        litem["hostname"] = "82 Eri";
                        litem["hd_name"] = "HD 20794";
                    }
                    else if (pl_name.substr(0, 3) == "HD ")
                    {
                        int HD = std::max(extract_cat_num(hostname, "HD"), extract_cat_num(pl_name, "HD"));
                        if (HD < MAX_HD && hdcache && hdcache[HD] && hdcache[HD]->Gliese[0])
                        {
                            hostname = hdcache[HD]->Gliese;
                            pl_name = hostname + " " + std::string(" ") + pl_name.substr(pl_name.size()-1, 1);
                            litem["pl_name"] = pl_name;
                        }
                    }
                    else if (pl_name.substr(0, 4) == "HIP ")
                    {
                        int HIP = std::max(extract_cat_num(hostname, "HIP"), extract_cat_num(pl_name, "HIP"));
                        if (HIP < MAX_HIP && hipcache && hipcache[HIP] && hipcache[HIP]->Gliese[0])
                        {
                            hostname = hipcache[HIP]->Gliese;
                            pl_name = hostname + " " + std::string(" ") + pl_name.substr(pl_name.size()-1, 1);
                            litem["pl_name"] = pl_name;
                        }
                    }
                }

                if (cinit == '8' && pl_name.substr(0, 7) == "82 Eri ")
                {
                    litem["hd_name"] = "HD 20794";
                }
                if ((cinit == 'e' && pl_name.substr(0, 12) == "eps Eridani ") || hostname == "eps Eridani")
                {
                    std::string let = extract_letter(pl_name);
                    pl_name = "eps Eri " + let;
                    litem["pl_name"] = pl_name;
                    litem["hostname"] = "eps Eri";
                    litem["hd_name"] = "HD 22049";
                }

                std::string norm = normalize_name(pl_name);
                std::string letter = extract_letter(pl_name);
                std::string target_key = norm;

                // 1. Resolve Aliases (HD / HIP / Name)

                if (primary_keys.count(norm))
                {
                    target_key = primary_keys[norm];
                }
                else
                {
                    if (litem.contains("alt_target_name") && litem["alt_target_name"].is_string())
                    {
                        std::string alt_str = litem.value("alt_target_name", "");
                        std::stringstream ss(alt_str);
                        std::string item_token;
                        while (std::getline(ss, item_token, '#'))
                        {
                            std::stringstream ss2(item_token);
                            std::string sub_tok;
                            while (std::getline(ss2, sub_tok, ','))
                            {
                                std::string norm_alt = normalize_name(sub_tok);
                                if (!norm_alt.empty() && primary_keys.count(norm_alt))
                                {
                                    target_key = primary_keys[norm_alt];
                                    break;
                                }
                            }
                            if (target_key != norm)
                            {
                                break;
                            }
                        }
                    }

                    int hd = -1, hip = -1;

                    // NASA provides explicit fields; EU relies on hostname/pl_name
                    if (litem.contains("hd_name") && litem["hd_name"].is_string())
                    {
                        hd = extract_cat_num(litem.value("hd_name", ""), "HD");
                    }
                    if (litem.contains("hip_name") && litem["hip_name"].is_string())
                    {
                        hip = extract_cat_num(litem.value("hip_name", ""), "HIP");
                    }

                    std::string hostname = litem.value("hostname", "");
                    if (hd == -1)
                    {
                        hd = std::max(extract_cat_num(hostname, "HD"), extract_cat_num(pl_name, "HD"));
                    }
                    if (hip == -1)
                    {
                        hip = std::max(extract_cat_num(hostname, "HIP"), extract_cat_num(pl_name, "HIP"));
                    }

                    if (litem.contains("alt_target_name") && litem["alt_target_name"].is_string())
                    {
                        std::string alt_str = litem.value("alt_target_name", "");
                        if (!alt_str.empty())
                        {
                            if (hd == -1)
                            {
                                hd = extract_cat_num(alt_str, "HD");
                            }
                            if (hip == -1)
                            {
                                hip = extract_cat_num(alt_str, "HIP");
                            }
                        }
                    }

                    std::string hd_alias = hd != -1 ? "hd" + std::to_string(hd) + letter : "";
                    std::string hip_alias = hip != -1 ? "hip" + std::to_string(hip) + letter : "";

                    // Point to existing canonical record if an alias matches
                    if (!hd_alias.empty() && primary_keys.count(hd_alias))
                    {
                        target_key = primary_keys[hd_alias];
                    }
                    else if (!hip_alias.empty() && primary_keys.count(hip_alias))
                    {
                        target_key = primary_keys[hip_alias];
                    }

                    // Register this planet's identifiers to the canonical target
                    primary_keys[norm] = target_key;
                    if (!hd_alias.empty())
                    {
                        primary_keys[hd_alias] = target_key;
                    }
                    if (!hip_alias.empty())
                    {
                        primary_keys[hip_alias] = target_key;
                    }

                    if (litem.contains("alt_target_name") && litem["alt_target_name"].is_string())
                    {
                        std::string alt_str = litem.value("alt_target_name", "");
                        std::stringstream ss(alt_str);
                        std::string item_token;
                        while (std::getline(ss, item_token, '#'))
                        {
                            std::stringstream ss2(item_token);
                            std::string sub_tok;
                            while (std::getline(ss2, sub_tok, ','))
                            {
                                std::string norm_alt = normalize_name(sub_tok);
                                if (!norm_alt.empty())
                                {
                                    primary_keys[norm_alt] = target_key;
                                }
                            }
                        }
                    }
                }

                // 2. Field-level Merge
                if (merged_planets.find(target_key) != merged_planets.end())
                {
                    auto& existing = merged_planets[target_key];
                    merge_exoplanet_metallicity(existing, litem);
                    for (auto& [key, value] : litem.items())
                    {
                        if (key.rfind("st_met", 0) == 0) continue;
                        if ((key == "pl_orbinclerr1" || key == "pl_orbinclerr2") &&
                            existing.contains("pl_orbincl") && !existing["pl_orbincl"].is_null() &&
                            existing["pl_orbincl"] != litem.value("pl_orbincl", json())) continue;
                        if (!value.is_null())
                        {
                            if (!existing.contains(key) || existing[key].is_null())
                            {
                                existing[key] = value;
                            }
                        }
                    }
                }
                else
                {
                    merged_planets[target_key] = litem;
                }
            };

            // Read EU First (ensures EU's naming convention remains the canonical root)
            try
            {
                if (!readBufferEU.empty())
                {
                    json eu_json = json::parse(readBufferEU);

                    if (eu_json.contains("data") && eu_json.contains("columns"))
                    {
                        auto& columns = eu_json["columns"];
                        for (const auto& row_array : eu_json["data"])
                        {
                            json obj;
                            for (size_t i = 0; i < columns.size() && i < row_array.size(); ++i)
                            {
                                // Force the key to be a standard C++ string to prevent null-key crashes
                                std::string key = columns[i].value("name", "unknown_key");

                                // If the database returns null for a text field, replace it with an empty string.
                                // This protects register_planet() from std::string conversion crashes.
                                if (row_array[i].is_null() && columns[i].value("datatype", "") == "char")
                                {
                                    obj[key] = "";
                                }
                                else
                                {
                                    obj[key] = row_array[i];
                                }
                            }
                            register_planet(obj, false);
                        }
                    }
                }
            }
            catch (const json::exception&)
            {
                std::cerr << "Exoplanet.eu returned invalid catalog data; continuing with NASA." << std::endl;
            }

            // Read NASA Second (fills in the gaps)
            try
            {
                if (!readBufferNASA.empty())
                {
                    json nasa_json = json::parse(readBufferNASA);

                    // NASA uses a flat JSON array of objects
                    if (nasa_json.is_array())
                    {
                        for (const auto& item : nasa_json)
                        {
                            register_planet(item, true);
                        }
                    }
                }
            }
            catch (const json::exception&)
            {
                std::cerr << "NASA returned invalid catalog data; continuing with available data." << std::endl;
            }

            // Repackage to array
            for (auto& [key, val] : merged_planets)
            {
                planets_array.push_back(val);
            }

            // Deduplicate across catalogs with spatial and orbital matching
            dedup_planets(planets_array);

            std::fstream fs;
            if (!planets_array.empty())
            {
                fs.open(exocache, std::ios::out);
            }
            if (fs.is_open())
            {
                fs << planets_array.dump(4);
                fs.close();
            }
        }
        catch (const json::exception&)
        {
            std::cerr << "Invalid TAP catalog data; trying the saved catalog." << std::endl;
            planets_array = json::array();
        }
    }
    if (planets_array.empty())
    {
        std::fstream fs(exocache, std::ios::in);
        if (!fs)
        {
            return 0;
        }
        fs >> planets_array;
        fs.close();
        size_t prev_size = planets_array.size();
        planets_array.erase(std::remove_if(planets_array.begin(), planets_array.end(), [](json& row)
        {
            // Purge old EU entries from the persisted unified JSON as well.
            return !accept_exoplanet_source(row, row.value("source_catalog", "") == "NASA");
        }), planets_array.end());
        dedup_planets(planets_array);
        if (planets_array.size() < prev_size)
        {
            std::fstream out_fs(exocache, std::ios::out);
            if (out_fs)
            {
                out_fs << planets_array.dump(4);
                out_fs.close();
            }
        }
    }

    exoplanet_tap_catalog = planets_array;

    std::string tmp_cache = std::string(derived_cache) + ".tmp";
    FILE* cachefp = fopen(tmp_cache.c_str(), "wb");
    char cache_io_buf[65536];
    if (cachefp)
    {
        setvbuf(cachefp, cache_io_buf, _IOFBF, sizeof(cache_io_buf));
        fputs("# EXO_TRANSIT3\n", cachefp);
    }

    int rows_written = 0;
    Star* last_host_star = nullptr;
    std::string last_hostname;
    size_t total_p = planets_array.size();
    size_t cur_p = 0;
    std::vector<ExoRow> deferred_hosts;
    std::map<std::string, bool> catalog_hosts;

    for (const auto& jrow : planets_array)
    {
        std::lock_guard<std::recursive_mutex> catalog_lock(catalog_mutex);
        if (abort_load)
        {
            break;
        }
        cur_p++;
        if (exoplanet_loading.load() && (cur_p & 31) == 0 && total_p > 0)
        {
            float frac = (float)cur_p / (float)total_p;
            if (frac > 1.0f)
            {
                frac = 1.0f;
            }
            if (stars_only)
            {
                exoplanet_load_progress.store(0.05f + 0.38f * frac);
            }
            else
            {
                exoplanet_load_progress.store(0.50f + 0.48f * frac);
                exoplanet_count_loaded.store(result);
            }
        }

        if (ncelobjs >= MAX_CELOBJS)
        {
            std::cerr << "Warning: Maximum sequential object allocation threshold reached (" << MAX_CELOBJS << ")." << std::endl;
            break;
        }

        bool ok = false;
        ExoRow row = exorow_from_json(jrow, &ok);
        if (!ok)
        {
            continue;
        }

        if (!strcasecmp(row.pl_name.c_str(), "eps Ind Bb")) continue;

        catalog_hosts[row.hostname] = true;
        if (!exorow_has_position(row) && host_positions.count(row.hostname))
        {
            if (stars_only)
            {
                deferred_hosts.push_back(row);
                continue;
            }
            apply_exoplanet_host_position(row, host_positions);
        }

        Star* host_star = nullptr;
        bool was_new = false;

        // Check consecutive-row cache first (planets are grouped by hostname)
        if (!row.hostname.empty() && row.hostname == last_hostname && last_host_star)
        {
            host_star = last_host_star;
        }
        else
        {
            host_star = resolve_or_create_exostar(row, loaded_starsonly, &was_new, &host_lookup);
            last_host_star = host_star;
            last_hostname = row.hostname;
        }

        if (!host_star)
        {
            continue;
        }
        apply_exoplanet_metallicity(row, host_star);

        if (stars_only)
        {
            if (was_new && cachefp)
            {
                exorow_write_line(cachefp, row);
                rows_written++;
            }
            continue;
        }

        if (add_exoplanet_from_row(row, host_star, planet_celids, result))
        {
            if (cachefp)
            {
                exorow_write_line(cachefp, row);
                rows_written++;
            }
        }

        if (!(result & 0x7f))
        {
            lmss.str("");
            lmss << "Loaded " << result << " exoplanets from TAP catalogs...";
            mtx.lock();
            loading_msg = lmss.str();
            mtx.unlock();
        }
    }

    // A host can remain valid after its reported planet was rejected.
    if (stars_only)
    {
        for (const auto& entry : host_positions)
        {
            if (!catalog_hosts.count(entry.first))
            {
                deferred_hosts.push_back(entry.second);
            }
        }
    }

    // Recover identified hosts after the ordinary star pass, before committing its cache.
    for (ExoRow& row : deferred_hosts)
    {
        std::lock_guard<std::recursive_mutex> catalog_lock(catalog_mutex);
        if (abort_load || ncelobjs >= MAX_CELOBJS)
        {
            break;
        }
        apply_exoplanet_host_position(row, host_positions);
        bool was_new = false;
        Star* host_star = resolve_or_create_exostar(row, loaded_starsonly, &was_new, &host_lookup);
        apply_exoplanet_metallicity(row, host_star);
        if (host_star && was_new && cachefp)
        {
            exorow_write_line(cachefp, row);
            rows_written++;
        }
    }

    if (cachefp)
    {
        fclose(cachefp);
        if (!abort_load && rows_written > 0)
        {
            std::rename(tmp_cache.c_str(), derived_cache);
        }
        else
        {
            std::remove(tmp_cache.c_str());
        }
    }
    apply_exoplanet_names(planet_celids, &host_lookup);
    if (stars_only) loaded_starsonly = true;

    return result;
}

// ---- Galaxies --------------------------------------------------------------------------------
//
// UNGC (Karachentsev+ 2013, J/AJ/145/101) first: 869 Local Volume galaxies with MEASURED distances
// (TRGB, Cepheids), which inside ~20 Mpc is the only kind worth having -- peculiar motion swamps
// the Hubble flow there, and Andromeda's velocity is outright negative. RC3 (de Vaucouleurs+ 1991,
// VII/155) then supplies the other 23000 by velocity, plus the position angles the UNGC lacks.
// Deduplication is positional (within an arcminute), the two catalogs' spellings being hopeless.

// A galaxy's position goes into location.galactic_center, in MILLIONS OF LIGHT YEARS rather than
// the metres everything else uses (see CelestialLocation) -- a coarse unit at the top of the
// hierarchy is what stops a 3e24 m distance leaking down into every star's system_center.
static void place_galaxy(Galaxy *g, double ra, double decl, double distance_mpc)
{
    const double mly_per_mpc = 3.26156;                     // 1 parsec = 3.26156 light years
    g->right_ascension = ra;
    g->declination = decl;
    g->epoch = J2000;
    g->distance = distance_mpc * 1e6 * parsec;              // convert to meters
    g->distance_known = true;
    g->volumetric_mean_radius = g->distance * g->angular_diameter * 0.5;
    g->location.galactic_center = Point::from_ra_dec(ra, decl, distance_mpc * mly_per_mpc);
    g->location.system_center = Point(0, 0, 0);
    g->location.local_position = Point(0, 0, 0);
}

// Bring the UNGC's "NGC0224" into line with the RC3's "NGC 224" -- letters followed straight away
// by digits get a space, and the zero padding goes -- so one search finds Andromeda from either
// catalog. Names of any other shape ("HolmIX", "[KK2000] 57") are left exactly as they are.
static std::string normalize_galaxy_name(const std::string &raw)
{
    // The UNGC spells the Messier objects out in full and zero-padded ("MESSIER031"). Nobody
    // writes them that way or searches for them that way, so these become "M31".
    if (raw.size() > 7 && !raw.compare(0, 7, "MESSIER"))
    {
        size_t d = 7;
        while (d < raw.size() && raw[d] == '0') d++;
        if (d < raw.size()) return std::string("M") + raw.substr(d);
    }

    size_t letters = 0;
    while (letters < raw.size() && isalpha((unsigned char)raw[letters])) letters++;
    if (!letters || letters >= raw.size()) return raw;
    if (!isdigit((unsigned char)raw[letters])) return raw;

    size_t digits = letters;
    while (digits < raw.size() && raw[digits] == '0') digits++;         // drop the padding
    if (digits >= raw.size() || !isdigit((unsigned char)raw[digits])) digits = letters;

    return raw.substr(0, letters) + " " + raw.substr(digits);
}

// Deprojected inclination, by the Hubble relation as refined by Holmberg:
//     cos^2(i) = ((b/a)^2 - q0^2) / (1 - q0^2)
// q0 is a disc's thickness -- the axis ratio it still shows seen exactly edge-on. The values below
// are fitted against the UNGC's own inclination column, which keeps the RC3 galaxies on the same
// convention.
// For spheroids and ellipticals (T < 0 or dwarf morphology "Sph"), the flattening is
// intrinsic 3D shape rather than a tilted thin disc. They take 90 degrees (half_pi) so their
// polar symmetry axis lies in the plane of the sky as seen from Earth, reproducing the cataloged
// axis ratio and position angle when projected as a 3D spheroid.
static double galaxy_inclination(double axis_ratio, double T, bool T_known, const char* morph_type = nullptr)
{
    if ((T_known && T < 0) || (morph_type && (strstr(morph_type, "Sph") || strstr(morph_type, "sph"))))
    {
        return half_pi;
    }

    double q0 = 0.20;                                   // the classic value, for an unknown type
    if (T_known)
    {
        if (T <= 4.0)
        {
            q0 = 0.26;                        // early spirals, thicker discs
        }
        else if (T <= 7.0)
        {
            q0 = 0.16;                   // late spirals, the thinnest
        }
        else
        {
            q0 = 0.42;                                 // irregulars, genuinely puffy
        }
    }

    if (axis_ratio >= 1.0)
    {
        return 0;                    // round on the sky: face-on
    }
    if (axis_ratio <= q0)
    {
        return half_pi;               // at or past the edge-on limit
    }

    double cos2 = (axis_ratio*axis_ratio - q0*q0) / (1.0 - q0*q0);
    if (cos2 < 0)
    {
        cos2 = 0;
    }
    if (cos2 > 1)
    {
        cos2 = 1;
    }
    return acos(sqrt(cos2));
}

static double galaxy_apparent_to_absolute(double apparent, double distance_m)
{
    if (!(distance_m > 0)) return 0;
    double parsecs = distance_m / parsec;
    if (parsecs < 1) return 0;
    return apparent - 5.0 * (log10(parsecs) - 1.0);
}

int CatalogReader::read_UNGC_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs" _FILESLASH "UNGC" _FILESLASH "table1.dat";
    char buffer[1024], field[64];
    int num_read = 0, offset;

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);
    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    FILE* fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    // Keyed on the RAW catalog name, so table2 below -- which spells them the same way -- matches
    // straight off, before normalize_galaxy_name() rewrites them for the user's benefit.
    std::map<std::string, Galaxy*> by_raw_name;

    while (fgets(buffer, sizeof(buffer)-2, fp))
    {
        if (ncelobjs >= max-1) break;
        if (strlen(buffer) < 120) continue;

        //  115-119  F5.2  Mpc  Dist   Distance to galaxy
        read_field_onebased(buffer, 115, 119, field);
        double dist_mpc = atof(field);
        if (!(dist_mpc > 0)) continue;              // without a distance there is nowhere to put it

        Galaxy *g = new Galaxy();

        //   1- 18  A18  Name
        read_field_onebased(buffer, 1, 18, field);
        std::string raw_name = trim(field);
        snprintf(g->name, sizeof(g->name), "%s", normalize_galaxy_name(raw_name).c_str());
        if (!strlen(g->name))
        {
            delete g;
            continue;
        }

        //  20-29 RA (h m s), 31-39 Dec (sign d m s), both J2000
        read_field_onebased(buffer, 20, 21, field);
        double deg = atof(field) * 15;
        read_field_onebased(buffer, 23, 24, field);
        double mnt = atof(field) * 15;
        read_field_onebased(buffer, 26, 29, field);
        double sec = atof(field) * 15;
        g->right_ascension = (deg + mnt/60 + sec/3600) * fiftyseventh;

        read_field_onebased(buffer, 31, 31, field);
        int sgn = (field[0] == '-') ? -1 : 1;
        read_field_onebased(buffer, 32, 33, field);
        deg = atof(field);
        read_field_onebased(buffer, 35, 36, field);
        mnt = atof(field);
        read_field_onebased(buffer, 38, 39, field);
        sec = atof(field);
        g->declination = (deg + mnt/60 + sec/3600) * fiftyseventh * sgn;

        place_galaxy(g, g->right_ascension, g->declination, dist_mpc);

        //  41-46 a26 (arcmin), 48-51 b/a
        read_field_onebased(buffer, 41, 46, field);
        g->angular_diameter = atof(field) * fiftyseventh / 60.0;
        read_field_onebased(buffer, 48, 51, field);
        double ba = atof(field);
        if (ba > 0 && ba <= 1) g->axis_ratio = ba;

        //  66-70 Bmag
        read_field_onebased(buffer, 66, 70, field);
        double bt = atof(field);
        if (bt)
        {
            g->apparent_magnitude = bt;
            g->absolute_magnitude = galaxy_apparent_to_absolute(bt, g->distance);
        }

        //  99-100 T-type, 102-106 dwarf morphology
        read_field_onebased(buffer, 99, 100, field);
        if (strlen(trim(field).c_str()))
        {
            g->morphological_T = atof(field);
            g->T_known = true;
        }
        read_field_onebased(buffer, 102, 106, field);
        snprintf(g->morph_type, sizeof(g->morph_type), "%s", trim(field).c_str());

        // 110-113 heliocentric radial velocity, km/s
        read_field_onebased(buffer, 110, 113, field);
        g->radial_velocity = atof(field) * 1000.0;

        // Deprojected for now; table2 below replaces it wherever the catalog has its own.
        g->inclination = galaxy_inclination(g->axis_ratio, g->morphological_T, g->T_known, g->morph_type);

        // Our own galaxy is in the catalog like any other, but with no axis ratio and no position
        // angle -- neither is measurable from inside -- which would leave it with no system plane
        // at all, lying flat in the ICRF. It gets the constants worked out in misc.h instead.
        if (!strcmp(g->name, "Milky Way"))
        {
            g->inclination = milky_way_inclination;
            g->position_angle = milky_way_position_angle;
            g->position_angle_known = true;
            g->distance_known = true;
            g->volumetric_mean_radius = light_year * 100000;
            g->axis_ratio = 0.01;                       // ~100000 light years across, ~1000 thick

            // The catalog rounds our own distance to 0.01 Mpc, 4000 light years off. Seen from
            // inside, its ratio to the disc radius sets how far the bright half of the band
            // reaches, so that error moves the whole sky: use the measured value.
            place_galaxy(g, g->right_ascension, g->declination,
                         sun_to_galactic_center / (3.26156 * 1e+6));

            // a26 is blank too, and both renderers take the disc radius as
            // distance * angular_diameter / 2 -- so run that backwards from the radius we know.
            g->angular_diameter = 2.0 * milky_way_radius / sun_to_galactic_center;

            // draw_galaxy_band() (visuals.cpp) reads g->band's two boundary roads to draw the band
            // itself -- nothing populated it. GalaxyBand::load_dat_file() existed and was tested in
            // isolation, but was never wired to an actual file at load time, so the band was always
            // empty and the loop in draw_galaxy_band() drew zero points every time, regardless of
            // inside_galaxy_idx or position math being correct.
            g->band.load_dat_file("catalogs" _FILESLASH "Milky_Way.dat");
        }
        if (!strcmp(g->name, "LMC") || !strcmp(g->name, "Large Magellanic Cloud"))
        {
            g->position_angle = 115.0 * fiftyseventh;
            g->position_angle_known = true;
        }
        g->location.equatorial_plane = g->location.local_system_plane
                                       = system_plane_from_incl_and_node(g->inclination, g->position_angle, (Point)g->location);

        g->cenobj = g;
        g->distance_known = true;
        g->known_poles = g->position_angle_known;
        g->volumetric_mean_radius = g->distance * g->angular_diameter * 0.5;
        g->BV_color = -bv_correction;
        by_raw_name[raw_name] = g;
        append_cel(g);
        num_read++;
    }

    fclose(fp);

    // table2 carries the catalog's own inclination (bytes 47-48, degrees from face-on). Preferred
    // over the deprojection: where they disagree it is a kinematic inclination from the HI line
    // width, which owes nothing to the photometry.
    path = "catalogs" _FILESLASH "UNGC" _FILESLASH "table2.dat";
    fp = fopen(path.c_str(), "rb");
    if (fp)
    {
        while (fgets(buffer, sizeof(buffer)-2, fp))
        {
            if (strlen(buffer) < 48) continue;
            read_field_onebased(buffer, 1, 18, field);
            std::map<std::string, Galaxy*>::iterator it = by_raw_name.find(trim(field));
            if (it == by_raw_name.end()) continue;

            read_field_onebased(buffer, 47, 48, field);
            if (!strlen(trim(field).c_str()))
            {
                continue;
            }
            Galaxy *ig = it->second;
            ig->inclination = atof(field) * fiftyseventh;
            ig->location.equatorial_plane =
                ig->location.local_system_plane =
                    system_plane_from_incl_and_node(ig->inclination, ig->position_angle, (Point)ig->location);
        }
        fclose(fp);
    }

    return num_read;
}

int CatalogReader::read_RC3_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs" _FILESLASH "RC3" _FILESLASH "rc3";
    char buffer[1024], field[64];
    int num_read = 0, offset, i;

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);
    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    FILE* fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        std::string gzpath = path + ".gz";
        if (file_exists(gzpath.c_str()))
        {
            extract_archive(gzpath.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (!fp) return 0;

    // Galaxies already loaded (i.e. by the UNGC), for the duplicate test below.
    std::vector<Galaxy*> known;
    for (i=0; cels[i] && i<max; i++)
    {
        if (cels[i]->typeclass() != class_galaxy) continue;
        known.push_back((Galaxy*)cels[i]);
    }
    const double dup_limit = fiftyseventh / 60.0;           // one arcminute

    // The RC3 quotes velocity, not distance. Everything closer than this is the Local Volume,
    // where that conversion is meaningless -- and is exactly what the UNGC already covered.
    const double H0 = 70.0;                                 // km/s/Mpc
    const double min_velocity = 250.0;                      // km/s, about 3.5 Mpc

    while (fgets(buffer, sizeof(buffer)-2, fp))
    {
        if (abort_load) { fclose(fp); return num_read; }
        if (ncelobjs >= max-1) break;
        if (strlen(buffer) < 340) continue;

        //   1-  8 RA (h m s), 10-16 Dec (sign d m s), J2000
        read_field_onebased(buffer, 1, 2, field);
        double deg = atof(field) * 15;
        read_field_onebased(buffer, 3, 4, field);
        double mnt = atof(field) * 15;
        read_field_onebased(buffer, 5, 8, field);
        double sec = atof(field) * 15;
        double ra = (deg + mnt/60 + sec/3600) * fiftyseventh;

        read_field_onebased(buffer, 10, 10, field);
        int sgn = (field[0] == '-') ? -1 : 1;
        read_field_onebased(buffer, 11, 12, field);
        deg = atof(field);
        read_field_onebased(buffer, 13, 14, field);
        mnt = atof(field);
        read_field_onebased(buffer, 15, 16, field);
        sec = atof(field);
        double decl = (deg + mnt/60 + sec/3600) * fiftyseventh * sgn;

        Galaxy *dup = nullptr;
        for (size_t k = 0; k < known.size(); k++)
        {
            if (fabs(known[k]->declination - decl) > dup_limit) continue;    // cheap reject first
            if (fabs(known[k]->right_ascension - ra) * cos(decl) > dup_limit) continue;
            dup = known[k];
            break;
        }

        if (dup)
        {
            dup->inclination = dup->inclination
                               ? dup->inclination
                               : galaxy_inclination(dup->axis_ratio, dup->morphological_T, dup->T_known, dup->morph_type);

            // The UNGC entry stands, its measured distance being the better number -- but it has
            // no position angle and often no morphology, both of which the RC3 carries and an
            // oriented ellipse wants. Fill the gaps rather than discard the record.
            if (!dup->position_angle_known)
            {
                read_field_onebased(buffer, 186, 188, field);
                if (strlen(trim(field).c_str()))
                {
                    dup->position_angle = atof(field) * fiftyseventh;
                    dup->position_angle_known = true;
                }
            }
            if (!strcmp(dup->name, "LMC") || !strcmp(dup->name, "Large Magellanic Cloud"))
            {
                dup->position_angle = 115.0 * fiftyseventh;
                dup->position_angle_known = true;
            }
            dup->known_poles = dup->position_angle_known;
            dup->location.equatorial_plane = dup->location.local_system_plane =
                                                 system_plane_from_incl_and_node(dup->inclination, dup->position_angle, (Point)dup->location);
            if (!strlen(dup->morph_type))
            {
                read_field_onebased(buffer, 118, 124, field);
                snprintf(dup->morph_type, sizeof(dup->morph_type), "%s", trim(field).c_str());
            }
            if (!dup->T_known)
            {
                read_field_onebased(buffer, 132, 135, field);
                if (strlen(trim(field).c_str()))
                {
                    dup->morphological_T = atof(field);
                    dup->T_known = true;
                }
            }
            continue;
        }

        // Velocity is tested only here, after the duplicates above have handed over their position
        // angles -- the nearby galaxies the UNGC placed are slow, some outright approaching.
        // Preference order: 359-363 V3K (referred to the CMB, so our own motion is already out,
        // which is what the Hubble law wants, and much the best populated), then 343-347 cz
        // (optical heliocentric), then 334-338 V21 (HI).
        read_field_onebased(buffer, 359, 363, field);
        double v21 = atof(field);
        if (!v21)
        {
            read_field_onebased(buffer, 343, 347, field);
            v21 = atof(field);
        }
        if (!v21)
        {
            read_field_onebased(buffer, 334, 338, field);
            v21 = atof(field);
        }
        if (v21 < min_velocity) continue;

        Galaxy *g = new Galaxy();
        g->radial_velocity = v21 * 1000.0;
        place_galaxy(g, ra, decl, v21 / H0);

        //  63- 74 name, 75- 89 alternate name. Either may be blank; prefer the first.
        read_field_onebased(buffer, 63, 74, field);
        std::string nm = trim(field);
        if (!nm.size())
        {
            read_field_onebased(buffer, 75, 89, field);
            nm = trim(field);
        }
        if (!nm.size())
        {
            read_field_onebased(buffer, 106, 116, field);
            nm = trim(field);
        }
        if (!nm.size())
        {
            delete g;
            continue;
        }
        // The RC3 pads its catalog numbers out ("NGC   224"); squeeze the run of spaces so the
        // name reads the way anyone would write it.
        std::string squeezed;
        for (size_t k = 0; k < nm.size(); k++)
        {
            if (nm[k] == ' ' && squeezed.size() && squeezed[squeezed.size()-1] == ' ') continue;
            squeezed += nm[k];
        }
        snprintf(g->name, sizeof(g->name), "%s", squeezed.c_str());

        // 106-116 PGC number, printed as "PGC 2557"
        read_field_onebased(buffer, 106, 116, field);
        {
            const char *digits = field;
            while (*digits && (*digits < '0' || *digits > '9')) digits++;
            g->PGC = (uint32_t)atoi(digits);
        }

        // 118-124 type as printed, 132-135 Hubble stage T
        read_field_onebased(buffer, 118, 124, field);
        snprintf(g->morph_type, sizeof(g->morph_type), "%s", trim(field).c_str());
        read_field_onebased(buffer, 132, 135, field);
        if (strlen(trim(field).c_str()))
        {
            g->morphological_T = atof(field);
            g->T_known = true;
        }

        // 152-155 log D25 and 162-165 log R25, both logarithms: D25 in units of 0.1 arcmin (so
        // that the entries stay positive), R25 the major/minor ratio. axis_ratio is its reciprocal.
        read_field_onebased(buffer, 152, 155, field);
        if (strlen(trim(field).c_str()))
            g->angular_diameter = pow(10.0, atof(field)) * 0.1 * fiftyseventh / 60.0;
        read_field_onebased(buffer, 162, 165, field);
        if (strlen(trim(field).c_str()))
        {
            double r25 = pow(10.0, atof(field));
            if (r25 >= 1.0) g->axis_ratio = 1.0 / r25;
        }

        g->inclination = galaxy_inclination(g->axis_ratio, g->morphological_T, g->T_known, g->morph_type);

        // 186-188 position angle of the major axis, degrees
        read_field_onebased(buffer, 186, 188, field);
        if (strlen(trim(field).c_str()))
        {
            g->position_angle = atof(field) * fiftyseventh;
            g->position_angle_known = true;
        }
        if (!strcmp(g->name, "LMC") || !strcmp(g->name, "Large Magellanic Cloud"))
        {
            g->position_angle = 115.0 * fiftyseventh;
            g->position_angle_known = true;
        }
        g->known_poles = g->position_angle_known;
        g->location.equatorial_plane = g->location.local_system_plane
                                       = system_plane_from_incl_and_node(g->inclination, g->position_angle, (Point)g->location);

        // 190-194 total B magnitude
        read_field_onebased(buffer, 190, 194, field);
        double bt = atof(field);
        if (bt)
        {
            g->apparent_magnitude = bt;
            g->absolute_magnitude = galaxy_apparent_to_absolute(bt, g->distance);
        }

        g->cenobj = g;
        g->distance_known = true;       // treat it as known anyway, so that the distance appears in the N panel.
        g->volumetric_mean_radius = g->distance * g->angular_diameter * 0.5;
        g->BV_color = -bv_correction;
        append_cel(g);
        num_read++;
    }

    fclose(fp);
    return num_read;
}
