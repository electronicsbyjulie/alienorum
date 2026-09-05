
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <ctime>
#include <map>
#include <string.h>
#include "cat.h"
#include "serial.h"
#include "cons.h"
#include "shore.h"

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
            if (strstr(url, "astorb"))
            {
                std::thread tast(download_file, std::string(url), destfname);
                tast.detach();
            }
            else download_file(url, destfname);
        }

        // Any .gz files in the destination folder, unzip them.
        for (const auto& entry : fs::directory_iterator(destdir))
        {
            std::string entry_name = entry.path().filename().string();
            // std::cout << entry_name << std::endl;

            i = entry_name.size();
            j = i - 3;
            if (!strcmp(".tar.gz", &entry_name.c_str()[j]))
            {
                std::string entry_path = destdir + _FSSTR + entry_name;
                extract_archive(entry_path.c_str());
            }
            else if (!strcmp(".gz", &entry_name.c_str()[j]))
            {
                std::string decompressed_name = entry_name.substr(0, entry_name.size()-3);
                std::string entry_path = destdir + _FSSTR + decompressed_name;
                if (!fs::exists(entry_path.c_str()))
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
            s->namelen = 0;
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
        s->namelen = 0;

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
        if (field[3] < 'A') s->BayerGrkno = grkno_from_abbrev(field);
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
            s->namelen = 0;
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
            s->apparent_magnitude = f;
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
        //   1-  6  I6    ---      HIP      Identifier (HIP)                         (D01)
        read_field_onebased(buffer, 1, 6, field);
        HIP = atoi(field);

        Star* A = hipcache[HIP];
        if (!A) continue;

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

        A->location.local_system_plane = system_plane_from_incl_and_node(inclination, node, A->location.system_center);
        A->lock_system_plane = true;

        // A->update_location(J2000_TIME_T);
        s->location = A->location;
        s->distance_known = true;
        A->known_poles = true;
        s->known_poles = true;

        s->apparent_magnitude = 11;         // placeholder
        if (s->absolute_magnitude > 1e28) s->absolute_magnitude = A->absolute_magnitude + 1;      // garbage number

        num_read++;
        if (num_read >= max-4) return num_read;
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
        s->namelen = 0;
    }

    fclose(fp);
    return num_read;
}

// Collapses runs of whitespace, so gcvs_cat's padded "T     And" matches crossid's "T And".
static std::string squeeze_spaces(const char *s)
{
    std::string out;
    bool gap = false;
    for (; *s; s++)
    {
        if (isspace((unsigned char)*s))
        {
            gap = !out.empty();
            continue;
        }
        if (gap) out += ' ';
        gap = false;
        out += *s;
    }
    return out;
}

// GCVS 5.1, for the variables Hipparcos never fitted. It carries no HIP or HD column of its own,
// so stars are matched through the cross-identifications in crossid.dat.
int alienorum::CatalogReader::read_GCVS_catalog(CelestialObject **cels)
{
    if (!hipcache || !hdcache) return 0;

    char buffer[1024], field[256];
    std::string path = "catalogs" _FILESLASH "GCVS" _FILESLASH "crossid.dat";
    FILE *fp = fopen(path.c_str(), "rb");

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
        std::string gzpath = path + ".gz";
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
        if (!strlen(s->constellation) && strlen(trim(field).c_str())) strcpy(s->constellation, field);
    }

    return num_read;
}

int alienorum::CatalogReader::read_WD_catalog(CelestialObject **cels, int max)
{
    std::string namespath = "catalogs" _FILESLASH "WD" _FILESLASH "names.dat";
    std::string catpath = "catalogs" _FILESLASH "WD" _FILESLASH "catalog.dat";
    char buffer[1024];
    char field[32];
    int i, num_read = 0;
    double deg, mnt, sec, pm, pmtheta, vmag, absmag;
    std::map<std::string, Star*> exist_stars, comp_of_exist;
    std::map<std::string, char> comp;
    Star *s, *A;

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
        //   1- 14  A14    ---     Name     Common name of the object
        read_field_onebased(buffer, 1, 14, field);
        std::string star_name = trim(field);

        if (star_name.c_str()[1] >= 'A' && star_name.c_str()[1] <= 'Z')
        {
            if (strcmp(star_name.substr(0,2).c_str(), "HD")
                    && strcmp(star_name.substr(0,2).c_str(), "BD")
                    && strcmp(star_name.substr(0,2).c_str(), "CD")
                    && strcmp(star_name.substr(0,2).c_str(), "CP")
               ) continue;
        }

        //  17- 26  A10    ---     WD       White Dwarf (WD) number
        read_field_onebased(buffer, 15, 26, field);
        std::string WD = trim(field);

        if (exist_stars.find(WD) != exist_stars.end()) continue;
        if (comp_of_exist.find(WD) != comp_of_exist.end()) continue;

        char lcomp = 0;
        int l = star_name.size();
        const char* cstr = star_name.c_str();
        char c = cstr[l-1], penult = cstr[l-2], ante = cstr[l-3];
        std::string nameA = "", name0 = "";

        if (c >= 'B' && c <= 'Z' && penult <= '9')
        {
            lcomp = c;
            name0 = nameA = star_name.substr(0, l-2);
        }
        else if (c >= 'a' && c <= 'z' && penult >= 'A' && penult <= 'Z' && ante <= '9')
        {
            lcomp = c & 0x5f;
            lcomp = c;
            nameA = star_name.substr(0, l-1);
            name0 = star_name.substr(0, l-3);
        }

        comp[WD] = lcomp;

        i = find_object(star_name.c_str(), true, 9e29);
        if (i < 0)
        {
            if (lcomp)
            {
                i = find_object(nameA.c_str(), true, 9e29);
                if (i<0) i = find_object(name0.c_str(), true, 9e29);
                if (i<0)
                {
                    // Example: WD2124+191/BD+18 4794B: host star absent, ignore
                    comp_of_exist[WD] = nullptr;
                }
                else
                {
                    // Example: WD1253+261/HD112313 B: create new companion
                    comp_of_exist[WD] = (Star*)cels[i];
                }
            }
            else
            {
                // Example: WD0347+171/BD+16 0516: not already present; add new
                continue;
            }
        }
        else
        {
            //  73- 78  F6.3   mag     Vmag    [6.4/24.3]? V or other magnitude (see n_Vmag)
            read_field_onebased(buffer, 73, 78, field);
            vmag = atof(field);

            // 146-150  F5.2   mag     AbsMag  [-0.2/18.1]? Absolute visual (or B) magnitude (3)
            read_field_onebased(buffer, 146, 150, field);
            absmag = atof(field);

            if (fabs(vmag - ((Star*)cels[i])->apparent_magnitude) > 1.5
                    && fabs(absmag - ((Star*)cels[i])->absolute_magnitude) > 1.5
               )
            {
                // Example: WD0114-027/HD7672 B: names entry is incorrect, verify vmag/absmag
                // TODO: Find out if this happens just a few times and hard code (eww!) a workaround,
                // or if it happens often and requires more code thingie.
                std::cout << "ERROR: Magnitudes of " << cels[i]->name << " and " << star_name << "/" << WD << " do not match." << std::endl << std::flush;
                comp_of_exist[WD] = nullptr;
            }
            else
            {
                // Example: WD0642-166/Sirius B: update existing star, including WD #
                // Example: WD1121+216/GJ 427: update existing star, including WD #
                exist_stars[WD] = (Star*)cels[i];
                std::cout << "Found " << (Star*)cels[i]->name << std::endl << std::flush;
            }
        }
    }
    fclose(fp);

    fp = fopen(catpath.c_str(), "rb");

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

    std::string prev_name = "kwyjibo";
    while (fgets(buffer, 1020, fp))
    {
        //   2- 11  A10    ---     WD      White Dwarf (WD) number (1)
        read_field_onebased(buffer, 2, 11, field);

        std::string star_name = std::string("WD") + trim(field);
        if (star_name == prev_name) continue;
        if (exist_stars.find(star_name) != exist_stars.end())
        {
            s = exist_stars[star_name];
            A = nullptr;
        }
        else if (comp_of_exist.find(star_name) != comp_of_exist.end())
        {
            A = comp_of_exist[star_name];
            if (!A) continue;
            s = new Star();
            strcpy(s->name, field);
            s->namelen = 0;
            s->make_companion_of(A, comp[star_name]);
            append_cel(s);
        }
        else
        {
            s = new Star();
            strcpy(s->name, field);
            s->namelen = 0;
            append_cel(s);
            A = nullptr;
        }
        s->WD = field;

        double ra, dec, ra2000, dec2000;

        //  13- 14  I2     h       RAh     ?Hours RA, Equinox=B1950, Epoch=1950.0 (2)
        read_field_onebased(buffer, 13, 14, field);
        deg = atof(field) * 15;

        //  16- 17  I2     min     RAm     ?Minutes RA, Equinox=B1950, Epoch=1950.0 (2)
        read_field_onebased(buffer, 16, 17, field);
        mnt = atof(field) * 15;

        //  19- 20  I2     s       RAs     [0/60]? Seconds RA (2)
        read_field_onebased(buffer, 19, 20, field);
        sec = atof(field) * 15;

        ra = (deg + mnt/60 + sec/3600) * fiftyseventh;

        //      22  A1     ---     DE-     ?Declination sign (2)
        read_field_onebased(buffer, 22, 22, field);
        int sgndecl = (field[0] == '-') ? -1 : 1;

        //  23- 24  I2     deg     DEd     ?Degrees Dec, Equinox=B1950, Epoch=1950.0 (2)
        read_field_onebased(buffer, 23, 24, field);
        deg = atof(field);

        //  26- 29  F4.1   arcmin  DEm     ?Minutes Dec, Equinox=B1950, Epoch=1950.0 (2)
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

        //  31- 40  A10    ---     SpT     Spectral type (definitions in the paper, or in file "preface.tex").
        read_field_onebased(buffer, 31, 40, field);
        strcpy(s->spectral_type, trim(field).c_str());

        //      41  A1     ---     bNote   [*b?] 'b' if white dwarf is member of binary, '*' indicates a note in file "notes.dat"
        // TODO:

        //  73- 78  F6.3   mag     Vmag    [6.4/24.3]? V or other magnitude (see n_Vmag)
        read_field_onebased(buffer, 73, 78, field);
        double f = atof(field);
        if (f) s->apparent_magnitude = f;

        //  82- 87  F6.3   mag     B-V     [-0.7/2]? B-V color index in the UBV system
        read_field_onebased(buffer, 82, 87, field);
        f = atof(field);
        if (f) s->BV_color = f;

        //  92- 97  F6.3   mag     U-B     [-9.9/1.4]? U-B color index in the UBV system
        read_field_onebased(buffer, 92, 97, field);
        f = atof(field);
        if (f) s->UB_color = f;

        // 146-150  F5.2   mag     AbsMag  [-0.2/18.1]? Absolute visual (or B) magnitude (3)
        read_field_onebased(buffer, 146, 150, field);
        f = atof(field);
        if (f) s->absolute_magnitude = f;

        // 153-155  I3     kK      Teff    [10/170]? Effective temperature
        read_field_onebased(buffer, 153, 155, field);
        double T = atof(field);
        if (T && !s->BV_color) s->estimate_BV(T);
        if (T && !s->UB_color) s->estimate_UB(T);

        // 162-167  F6.4 arcsec/yr pm      [0/7]? Total proper motion
        read_field_onebased(buffer, 162, 167, field);
        pm = atof(field) / 3600 * fiftyseventh / oneyear;

        // 170-174  F5.1   deg     pmPA    ? Position angle of proper motion vector
        read_field_onebased(buffer, 170, 174, field);
        pmtheta = atof(field) * fiftyseventh;

        if (pm)
        {
            s->proper_motion_RA = pm * sin(pmtheta);
            s->proper_motion_decl = pm * cos(pmtheta);
        }

        // 180-186  F7.2   km/s    RadVel  [-262/422]? Radial velocity
        read_field_onebased(buffer, 180, 186, field);
        f = atof(field);
        if (f) s->radial_velocity = f*0.001;

        // 200-206  F7.4   arcsec  Plx     [-0.003/0.6]? Trigonometric parallax
        f = atof(field);
        if (f)
        {
            s->distance = parsec / f;
            s->distance_known = true;
        }
        else if (A)
        {
            s->distance = A->distance;
            s->distance_known = true;
        }

        if (!s->absolute_magnitude && s->distance_known)
        {
            double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
            s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
        }

        prev_name = star_name;
    }


    return num_read;
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

    for (i=0; i<n; i++)
    {
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
                A->namelen = 0;
            }
            if (!s->has_custom_name)
            {
                s->assign_identifier_name();
                if (!trim(s->name).size() || !strcmp(trim(s->name).c_str(), A->name))
                    strcpy(s->name, (std::string(A->name) + std::string(" B")).c_str() );
                s->namelen = 0;
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
    p->namelen = 0;
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

    // 487-521: the two light curves, total and nucleus.
    read_field_onebased(buffer, 487, 491, field);
    c->H1 = atof(field);
    read_field_onebased(buffer, 493, 497, field);
    c->R1 = atof(field);
    read_field_onebased(buffer, 499, 503, field);
    c->D1 = atof(field);
    read_field_onebased(buffer, 505, 509, field);
    c->H2 = atof(field);
    read_field_onebased(buffer, 511, 515, field);
    c->R2 = atof(field);
    read_field_onebased(buffer, 517, 521, field);
    c->D2 = atof(field);

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

#define _debug_exoplanet_inclinations 0
void CatalogReader::apply_exoplanet_names(std::map<int, std::vector<int>> planet_celids)
{
    std::map<std::string, std::string> planet_names;
    std::map<std::string, std::string> planet_types;
    std::map<std::string, double> planet_temps;
    std::map<std::string, double> planet_incls;
    std::map<std::string, double> planet_nodes;
    std::map<std::string, double> planet_istars;
    std::map<std::string, double> planet_bvcols;
    std::map<std::string, double> planet_albedines;

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
                // A bare "HD nnnnnn" designation names the star itself; anything trailing the
                // number ("HD nnnnnn b") is one of its planets, and belongs to the pass below.
                read_field_onebased(buffer, 1, 39, field);
                std::string desig = trim(field);
                int HD = 0;
                if (desig.find_first_not_of("0123456789 ", 2) == std::string::npos) HD = atoi(&desig[2]);

                if (HD > 0 && HD <= MAX_HD && hdcache[HD])
                {
                    read_field_onebased(buffer, 41, 63, field);
                    hdcache[HD]->local_name = trim(field);
                    continue;
                }
            }

            read_field_onebased(buffer, 1, 39, field);
            std::string designation = trim(field);

            read_field_onebased(buffer, 41, 63, field);
            std::string friendly = trim(field);

            planet_names[designation] = friendly;

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
        for (i=0; i<n; i++)
        {
            Planet *p = (Planet*)cels[row[i]];
            std::string designation = p->name;
            p->origname = designation;

            if (planet_incls.find(designation) != planet_incls.end())
                pincls.push_back(planet_incls[designation] * fiftyseventh);
            else if (p->orbit) pincls.push_back(p->orbit->inclination);

            double p_node = 0;
            if (planet_nodes.find(designation) != planet_nodes.end())
                p_node = planet_nodes[designation] * fiftyseventh;
            pnodes.push_back(p_node);

            if (planet_names.find(designation) != planet_names.end()) strcpy(p->name, planet_names[designation].c_str());
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
            }
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
        if (pmeanincl && !sysincl) sysincl = pmeanincl;
        if (!sysincl && stincl) sysincl = stincl;
        if (cmeanincl && !sysincl) sysincl = cmeanincl;
        if (!sysincl && s->orbit && s->orbit->heliocentric_inclination) sysincl = s->orbit->heliocentric_inclination;
        if (sysincl)
        {
            if (!stincl) stincl = sysincl;
            n = pincls.size();
            for (i=0; i<n; i++) if (!pincls[i] || fabs(pincls[i] - half_pi) < 0.0000001) pincls[i] = sysincl;
            n = cincls.size();
            for (i=0; i<n; i++) if (!cincls[i]) cincls[i] = sysincl;
        }

        if (pmeannode && !sysnode) sysnode = pmeannode;
        if (!sysnode && stnode) sysnode = stnode;
        if (cmeannode && !sysnode) sysnode = cmeannode;
        if (!sysnode && s->orbit && s->orbit->heliocentric_node) sysnode = s->orbit->heliocentric_node;
        if (sysnode)
        {
            if (!stnode) stnode = sysnode;
            n = pnodes.size();
            for (i=0; i<n; i++)
            {
                if (!pnodes[i]) pnodes[i] = sysnode;
            }
            n = cnodes.size();
            for (i=0; i<n; i++) if (!cnodes[i]) cnodes[i] = sysnode;
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
                        s->namelen = 0;
                        s->cenobj = s;
                        s_is_new = true;
                    }

                    if (!p)
                    {
                        p = new Planet();
                        if (planet_name.size()) strcpy(p->name, planet_name.c_str());
                        p->namelen = 0;
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
                s->namelen = 0;
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
                p->classify(HZ);

                // Estimate radius if unknown.
                if (!p->volumetric_mean_radius) p->estimate_radius();

                // Estimate albedo and absolute magnitude.
                p->estimate_albedo_and_absmagn();

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
                s->namelen = 0;
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
                        companion->namelen = 0;
                    }
                }

                break;
            }
        }
    }

    return num_read;
}

int CatalogReader::read_star_orbits_dat(CelestialObject **cels)
{
    std::string path = "catalogs" _FILESLASH "star_orbits.dat";
    char buffer[1024];
    char field[32];
    int num_read = 0;
    double f;

    // Fix for Mirfak seen from Hamal
    if (hdcache[12929])
    {
        hdcache[12929]->obliquity = half_pi;
        hdcache[12929]->equinox = _pi;
    }

    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;

    Star *A, *s;
    std::string str;
    int l;
    char last = 0, nxtlast = 0;
    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);

    while (fgets(buffer, 1020, fp))
    {
        if (*buffer == '#') continue;
        if (!trim(buffer).size()) continue;

        read_field_onebased(buffer, 1, 23, field);
        std::string cenname = trim(field);
        const char* censtr = cenname.c_str();
        A = nullptr;
        int sioxt = find_object(censtr, true);
        if (sioxt >= 0)
        {
            A = (Star*)cels[sioxt];
        }

        if (!A)
        {
            std::cerr << "Warning: " << censtr << " not found in loaded data." << std::endl;
            continue;
        }

        read_field_onebased(buffer, 65, 75, field);
        double ascending_node = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 77, 87, field);
        double inclination = atof(field) * fiftyseventh;

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
        if (A->has_disk)
        {
            A->disk_heliocen_inclination = inclination;
            A->disk_heliocen_node = ascending_node;

            read_field_onebased(buffer, 101, 111, field);
            str = trim(field);
            last = '\0';
            l = str.size();
            if (l) last = str.c_str()[l-1];
            if (l>1) nxtlast = str.c_str()[l-2];
            double disk_sma;
            if (nxtlast == 'A' && last == 'U') disk_sma = atof(field) * AU;
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
            std::cerr << "FAILED to orbit " << bdyname << " around " << cenname << ": center not found." << std::endl;
            continue;
        }

        if (!s && A->multisys)
        {
            char comp = bdystr[strlen(bdystr)-1];
            if (comp > 'A' && comp <= 'Z') s = A->multisys->get_member(comp);
        }
        if (!s)
        {
            sioxt = find_object(bdystr, true);
            if (sioxt >= 0)
            {
                s = (Star*)cels[sioxt];
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

            strcpy(s->name, bdyname.c_str());
            s->namelen = 0;
            s->has_custom_name = true;
            s->distance_known = true;
            read_field_onebased(buffer, 161, 175, field);
            strcpy(s->spectral_type, trim(field).c_str());
            read_field_onebased(buffer, 177, 191, field);
            f = atof(field);
            if (f || (field[0] == '0')) s->absolute_magnitude = f;
            double msun=0, rsun=0, lum=0, tempK=0;
            if (bs > 193)
            {
                read_field_onebased(buffer, 193, 203, field);
                msun = atof(field);
                s->mass = msun * solar_mass;
            }
            if (bs > 205)
            {
                read_field_onebased(buffer, 205, 215, field);
                rsun = atof(field);
                s->volumetric_mean_radius = rsun * solar_radius;
                assert(!isinf(s->volumetric_mean_radius));
            }
            if (bs > 217)
            {
                read_field_onebased(buffer, 217, 227, field);
                lum = atof(field);
                if (!s->absolute_magnitude)
                {
                    // The unconditional "= 11" used to run whether or not lum was actually given,
                    // discarding the just-computed magnitude for every star that did carry one
                    // (57 lines in this catalog, including Proxima Centauri: 4.5 magnitudes too
                    // bright at a flat 11 instead of its real ~15.6). 11 is meant only as the
                    // fallback guess for a star with no luminosity column at all.
                    if (lum)
                    {
                        double magshift = log(lum)/log(magnbase);
                        s->absolute_magnitude = 4.85 - magshift;
                    }
                    else s->absolute_magnitude = 11;
                }
            }
            if (bs > 229)
            {
                read_field_onebased(buffer, 229, 238, field);
                tempK = atof(field);
                // if (A->HD == 47152) std::cout << s->name << " tempK=" << tempK << std::endl;
                if (tempK)
                {
                    // BV_color has to be set from the real temperature before estimate_luminosity()
                    // reads it: that call looks up the main-sequence entry via
                    // get_mseqidx_from_BV(BV_color), and a newly-made star's BV_color is still its
                    // construction default of 0 -- the B-V of an A0V star -- until estimate_BV()
                    // runs. Called in the old order, a cool star (e.g. an M dwarf at ~3500K) got
                    // priced as an A0V (38 Lsun instead of ~0.03), several magnitudes too bright.
                    s->estimate_BV(tempK);
                    s->estimate_UB(tempK);
                    if (s->volumetric_mean_radius && !lum) s->absolute_magnitude = -log(s->estimate_luminosity(tempK))/log(magnbase);
                }
            }

            if (s_is_new)
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
                    mseqi += (mseqit = Star::get_mseqidx_from_lum(tempK));
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
                        if (!lum  )
                        {
                            double llum = Star::interpolate_mseq_lum(mseqi);
                            double magshift = log(llum)/log(magnbase);
                            s->absolute_magnitude = 4.85 - magshift;
                        }
                        if (!tempK)
                        {
                            double ltempK = Star::interpolate_mseq_temp(mseqi);
                            s->estimate_BV(ltempK);
                            s->estimate_UB(ltempK);
                        }
                    }
                }

#if 0
                if (A->HD == 205877) std::cout << s->name << ": " << msun << " " << rsun << " " << lum << " " << tempK
                                                   << " | " << mseqi
                                                   << " | " << Star::interpolate_mseq_temp(mseqi)
                                                   << std::endl;
#endif
            }
        }
        else if (!s || s == A)
        {
            std::cerr << "FAILED to orbit " << bdyname << " around " << cenname << ": member not found and insufficient data to construct new." << std::endl;
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

        char comp = 'B';
        if (!A->multisys) A->set_component('A', A);
        if (A->multisys) while (A->multisys->get_member(comp)) comp++;
        s->make_companion_of(A, comp);

        read_field_onebased(buffer, 49, 63, field);
        str = trim(field);
        last = '\0';
        l = str.size();
        if (l) last = str.c_str()[l-1];
        if (last == 'y') f = atof(field) * oneyear;
        else if (last == 'd') f = atof(field) * oneday;
        else f = atof(field);
        if (f) s->orbit->period = f;

        read_field_onebased(buffer, 89, 99, field);
        f = atof(field) * fiftyseventh;
        if (f) s->orbit->arg_periapsis = f;

        read_field_onebased(buffer, 101, 111, field);
        str = trim(field);
        last = '\0';
        l = str.size();
        if (l) last = str.c_str()[l-1];
        if (l>1) nxtlast = str.c_str()[l-2];
        if (nxtlast == 'A' && last == 'U') f = atof(field) * AU;
        else if (last == 's') f = atof(field) * A->distance / light_year * 0.29278287 * AU;
        else f = atof(field);
        if (f) s->orbit->semimajor_axis = f;

        read_field_onebased(buffer, 113, 123, field);
        f = atof(field);
        if (f) s->orbit->eccentricity = f;

        read_field_onebased(buffer, 125, 143, field);
        f = atof(field) * fiftyseventh;
        if (f) s->orbit->mean_anomaly = f;

        read_field_onebased(buffer, 145, 155, field);
        str = trim(field);
        l = str.size();
        if (l)
        {
            if (str.c_str()[0] == 'J' && str.c_str()[1] == 'D') f = atof(&field[2]);
            else f = (atof(field)-2000) * oneyear + J2000;
            if (f) s->orbit->epoch = f;
        }

        if (inclination || ascending_node)
        {
            s->location.equatorial_plane = s->location.orbital_plane = s->location.local_system_plane = new_orbital_plane;
            if (!A->lock_equatorial_plane && !A->lock_system_plane)
                A->location.equatorial_plane = A->location.orbital_plane = A->location.local_system_plane = new_orbital_plane;
            s->lock_system_plane = true;
            s->lock_equatorial_plane = true;
            s->obliquity = 0;
            s->equinox = 0;
            A->known_poles = s->known_poles = true;
        }

        num_read++;
    }

    return num_read;
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

    for (i=0; i<n; i++)
    {
        json pl = planets[i];
        std::string bodyname, cenname, mapurl;
        m = nullptr;                            // MERCURY ISN'T A FUCKING MOON, IMBECILE.
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
            if (cenname.size()) k = find_object(cenname.c_str(), false, 9e+29, 0);
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
                p->namelen = 0;
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
            try
            {
                pl.at("ABSMG").get_to(p->absolute_magnitude);
            }
            catch (...) { ; }
            try
            {
                pl.at("ArgPeri").get_to(p->orbit->arg_periapsis);
                p->orbit->arg_periapsis *= fiftyseventh;
            }
            catch (...) { ; }
            try
            {
                pl.at("AscNode").get_to(p->orbit->ascending_node);
                p->orbit->ascending_node *= fiftyseventh;
            }
            catch (...) { ; }

            try { /* TODO: pl.at("BondAlbedo").get_to(p->BV_color); */ }
            catch (...) { ; }
            try
            {
                pl.at("BVmag").get_to(p->BV_color);
            }
            catch (...)
            {
                if (createnew) p->BV_color = p->orbit->center->BV_color;
            }
            try
            {
                pl.at("UBmag").get_to(p->UB_color);
            }
            catch (...)
            {
                if (createnew) p->UB_color = p->orbit->center->UB_color;
            }
            try
            {
                pl.at("Eccentricity").get_to(p->orbit->eccentricity);
            }
            catch (...) { ; }
            try
            {
                pl.at("Epoch").get_to(p->epoch);
                p->epoch = J2000 + (p->epoch - 2000)*(oneyear/oneday);
                p->orbit->epoch = p->epoch;
            }
            catch (...) { ; }
            try
            {
                double pre;
                pl.at("EqPrecession").get_to(pre);
                p->precession = pre ? (_pi * 2 / pre / oneyear) : 0;
            }
            catch (...) { ; }
            try
            {
                double pre;
                pl.at("NodePrecession").get_to(pre);
                p->orbit->prec_node = pre ? (_pi * 2 / pre / oneyear) : 0;
            }
            catch (...) { ; }
            try
            {
                double pro;
                pl.at("ArgPeriProcession").get_to(pro);
                p->orbit->proc_argperi = pro ? (_pi * 2 / pro / oneyear) : 0;
            }
            catch (...) { ; }
            try
            {
                pl.at("Equinox").get_to(p->equinox);
                p->equinox *= fiftyseventh;
            }
            catch (...) { ; }
            try
            {
                pl.at("Incl").get_to(p->orbit->inclination);
                p->orbit->inclination *= fiftyseventh;
            }
            catch (...) { ; }
            try
            {
                pl.at("J2").get_to(p->J2);
            }
            catch (...) { ; }
            try
            {
                pl.at("Lon_J2000_offset").get_to(p->lon_J2000_offset);
                p->lon_J2000_offset *= fiftyseventh;
            }
            catch (...) { ; }
            try
            {
                pl.at("Mass").get_to(p->mass);
                p->mass *= 1000;
            }
            catch (...) { ; }
            try
            {
                pl.at("MeanAnom").get_to(p->orbit->mean_anomaly);
                p->orbit->mean_anomaly *= fiftyseventh;
            }
            catch (...) { ; }
            try
            {
                pl.at("Oblateness").get_to(p->oblateness);
            }
            catch (...) { ; }
            try
            {
                pl.at("Obliquity").get_to(p->obliquity);
                p->obliquity *= fiftyseventh;
            }
            catch (...) { ; }
            try
            {
                pl.at("OrbitPeriod").get_to(p->orbit->period);
            }
            catch (...) { ; }
            try
            {
                pl.at("RotationPeriod").get_to(p->sidereal_rotational_period);
            }
            catch (...) { ; }
            try
            {
                pl.at("SEMIMAJOR_AXIS").get_to(p->orbit->semimajor_axis);
            }
            catch (...) { ; }
            try
            {
                pl.at("SurfaceTemperature").get_to(p->temperature);
            }
            catch (...) { ; }
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
            try
            {
                pl.at("VolMeanRad").get_to(p->volumetric_mean_radius);
            }
            catch (...) { ; }
            try
            {
                pl.at("RingRadius").get_to(p->ring_radius);
                p->ring_radius *= 1000;
            }
            catch (...) { ; }
            // try { pl.at("").get_to(p->); } catch (...) { ; }

            if (m)
            {
                try
                {
                    pl.at("Depth" ).get_to(m->depth );
                    m->depth  *= 1000;
                }
                catch (...) { ; }
                try
                {
                    pl.at("Width" ).get_to(m->width );
                    m->width  *= 1000;
                }
                catch (...) { ; }
                try
                {
                    pl.at("Height").get_to(m->height);
                    m->height *= 1000;
                }
                catch (...) { ; }
                try
                {
                    pl.at("Major" ).get_to(m->major_moon);
                }
                catch (...) { ; }
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
                        if (!file_exists(destfname.c_str()))
                            download_file(mapurl, destfname);
                    }
                }
                catch (...) { ; }
            }

            if (p->orbit && p->orbit->center && createnew)
            {
                p->known_poles = p->obliquity && p->equinox;
                p->location = p->orbit->center->location;           // Copy the system center and local plane. The local position will auto-fill later.
                p->location.equatorial_plane.a = p->obliquity;
                p->location.equatorial_plane.v = Point(std::sin(p->equinox), 0, -std::cos(p->equinox));
                // classify() used to also assign a cosmic-shoreline atmosphere here as a side
                // effect, which was never verified safe for every Solar System object -- it no
                // longer touches atmosphere at all, so real bodies here stay exactly as
                // catalogs/planets.json specified them (airless, if it said nothing).
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

void alienorum::CatalogReader::write_condensed_star_cat_line(FILE *fp, Star *s)
{
    std::stringstream line;
    line << s->alienorumid << std::flush;
    int l = 15;
    line << std::string(l - line.str().size(), ' ');

    line << std::string(s->name) << std::flush;
    l += 40;
    line << std::string(l - line.str().size(), ' ');

    line << s->RA_as_hms(0) << std::flush;
    l += 11;
    line << std::string(l - line.str().size(), ' ');

    line << s->Decl_as_degms() << std::flush;
    l += 10;
    line << std::string(l - line.str().size(), ' ');

    line << ((s->apparent_magnitude >= 0) ? "+" : "-");
    if (fabs(s->apparent_magnitude) < 10) line << "0";
    if (s->apparent_magnitude < 99) line << std::fixed << std::setprecision(2) << fabs(s->apparent_magnitude) << std::flush;
    l += 7;
    line << std::string(l - line.str().size(), ' ');

    line << s->spectral_type << std::flush;
    l += 16;
    line << std::string(l - line.str().size(), ' ');

    if (!s->BV_color) s->estimate_BV(s->estimate_temperature());
    line << ((s->BV_color >= 0) ? "+" : "-");
    if (fabs(s->BV_color) < 10) line << "0";
    line << std::fixed << std::setprecision(2) << fabs(s->BV_color) << std::flush;
    l += 7;
    line << std::string(l - line.str().size(), ' ');

    if (!s->UB_color) s->estimate_BV(s->estimate_temperature());
    line << ((s->UB_color >= 0) ? "+" : "-");
    if (fabs(s->UB_color) < 10) line << "0";
    line << std::fixed << std::setprecision(2) << fabs(s->UB_color) << std::flush;
    l += 7;
    line << std::string(l - line.str().size(), ' ');

    if (s->HD) line << s->HD << std::flush;
    l += 7;
    line << std::string(l - line.str().size(), ' ');

    if (s->HIP) line << s->HIP << std::flush;
    l += 7;
    line << std::string(l - line.str().size(), ' ');

    if (s->HR) line << s->HR << std::flush;
    l += 7;
    line << std::string(l - line.str().size(), ' ');

    if (s->SB9) line << s->SB9 << std::flush;
    l += 7;
    line << std::string(l - line.str().size(), ' ');

    if (s->FlamsteedNo > 0) line << s->FlamsteedNo << std::flush;
    l += 4;
    line << std::string(l - line.str().size(), ' ');

    line << s->Bayer << std::flush;
    l += 8;
    line << std::string(l - line.str().size(), ' ');

    line << s->Gliese << std::flush;
    l += 16;
    line << std::string(l - line.str().size(), ' ');

    if (s->GouldNo > 0) line << s->GouldNo << std::flush;
    l += 4;
    line << std::string(l - line.str().size(), ' ');

    if (s->proper_motion_RA >= 0) line << " ";
    line << std::scientific << std::setprecision(4) << s->proper_motion_RA << std::flush;
    l += 12;
    line << std::string(l - line.str().size(), ' ');

    if (s->proper_motion_decl >= 0) line << " ";
    line << std::scientific << std::setprecision(4) << s->proper_motion_decl << std::flush;
    l += 12;
    line << std::string(l - line.str().size(), ' ');

    if (s->radial_velocity >= 0) line << " ";
    line << std::scientific << std::setprecision(4) << s->radial_velocity << std::flush;
    l += 12;
    line << std::string(l - line.str().size(), ' ');

    line << std::scientific << std::setprecision(4) << s->parallax << std::flush;
    l += 12;
    line << std::string(l - line.str().size(), ' ') << std::flush;

    line << std::scientific << std::setprecision(4) << (s->distance / light_year) << std::flush;
    l += 12;
    line << std::string(l - line.str().size(), ' ');

    if (!s->absolute_magnitude)
    {
        double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
        s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
    }
    line << ((s->absolute_magnitude >= 0) ? "+" : "-");
    if (fabs(s->absolute_magnitude) < 10) line << "0";
    line << std::fixed << std::setprecision(2) << fabs(s->absolute_magnitude) << std::flush;
    l += 7;
    line << std::string(l - line.str().size(), ' ');

    if (s->mass)
    {
        double M = s->mass / solar_mass;
        line << std::scientific << std::setprecision(4) << M << std::flush;
    }
    l += 12;
    line << std::string(l - line.str().size(), ' ');

    if (s->volumetric_mean_radius)
    {
        double R = s->volumetric_mean_radius / solar_radius;
        line << std::scientific << std::setprecision(4) << R << std::flush;
    }
    l += 12;
    line << std::string(l - line.str().size(), ' ');

    if (s->temperature)
    {
        int T = s->temperature;
        if (T < 100000) line << " ";
        if (T <  10000) line << " ";
        if (T <   1000) line << " ";
        if (T <    100) line << " ";
        if (T <     10) line << " ";
        line << std::fixed << T << std::flush;
    }
    l += 7;
    line << std::string(l - line.str().size(), ' ');

    if (s->sidereal_rotational_period)
    {
        double P = s->sidereal_rotational_period / oneday;
        if (P < 100000) line << " ";
        if (P <  10000) line << " ";
        if (P <   1000) line << " ";
        if (P <    100) line << " ";
        if (P <     10) line << " ";
        line << std::fixed << std::setprecision(3) << P << std::flush;
    }
    l += 11;
    line << std::string(l - line.str().size(), ' ');

    if (s->orbit && s->orbit->center) line << ((Star*)s->orbit->center)->alienorumid;
    l += 15;
    line << std::string(l - line.str().size(), ' ');

    if (s->orbit && s->orbit->period) line << std::scientific << std::setprecision(5) << (s->orbit->period / oneday);
    l += 13;
    line << std::string(l - line.str().size(), ' ');

    if (s->orbit && s->orbit->semimajor_axis) line << std::scientific << std::setprecision(5) << (s->orbit->semimajor_axis / AU);
    l += 13;
    line << std::string(l - line.str().size(), ' ');

    if (s->orbit && s->orbit->eccentricity) line << std::fixed << std::setprecision(5) << (s->orbit->eccentricity);
    l += 13;
    line << std::string(l - line.str().size(), ' ');

    if (s->orbit && s->orbit->arg_periapsis)
    {
        double omega = s->orbit->arg_periapsis * fiftyseven;
        if (omega < 0) omega += 360;
        if (omega < 100) line << " ";
        if (omega <  10) line << " ";
        line << std::fixed << std::setprecision(4) << omega << std::flush;
    }
    l += 9;
    line << std::string(l - line.str().size(), ' ');

    if (s->orbit && s->orbit->mean_anomaly)
    {
        double omega = s->orbit->mean_anomaly * fiftyseven;
        if (omega < 0) omega += 360;
        if (omega < 100) line << " ";
        if (omega <  10) line << " ";
        line << std::fixed << std::setprecision(4) << omega << std::flush;
    }
    l += 9;
    line << std::string(l - line.str().size(), ' ');

    if (s->orbit && s->orbit->epoch && (s->orbit->mean_anomaly || (fabs(s->orbit->epoch - J2000) > 0.001)))
        line << std::scientific << std::setprecision(8) << s->orbit->epoch;
    l += 15;
    line << std::string(l - line.str().size(), ' ');

    if (s->orbit && s->orbit->heliocentric_inclination)
    {
        double omega = s->orbit->heliocentric_inclination * fiftyseven;
        if (omega < 0) omega += 360;
        if (omega < 100) line << " ";
        if (omega <  10) line << " ";
        line << std::fixed << std::setprecision(4) << omega << std::flush;
    }
    l += 9;
    line << std::string(l - line.str().size(), ' ');

    if (s->orbit && s->orbit->heliocentric_node)
    {
        double omega = s->orbit->heliocentric_node * fiftyseven;
        if (omega < 0) omega += 360;
        if (omega < 100) line << " ";
        if (omega <  10) line << " ";
        line << std::fixed << std::setprecision(4) << omega << std::flush;
    }
    l += 9;
    line << std::string(l - line.str().size(), ' ');

    // Variability
    // TODO:
    if (s->variability_period) line << std::scientific << (s->variability_period / oneday);
    l += 13;
    line << std::string(l - line.str().size(), ' ');

    if (s->minmag)
    {
        line << ((s->minmag >= 0) ? "+" : "-");
        if (fabs(s->minmag) < 10) line << "0";
        line << std::fixed << std::setprecision(2) << fabs(s->minmag) << std::flush;
    }
    l += 7;
    line << std::string(l - line.str().size(), ' ');

    if (s->maxmag)
    {
        line << ((s->maxmag >= 0) ? "+" : "-");
        if (fabs(s->maxmag) < 10) line << "0";
        line << std::fixed << std::setprecision(2) << fabs(s->maxmag) << std::flush;
    }
    l += 7;
    line << std::string(l - line.str().size(), ' ');

    if (s->variability_period && s->epoch_max_brightness)
        line << std::fixed << std::setprecision(4) << s->epoch_max_brightness;
    l += 18;
    line << std::string(l - line.str().size(), ' ');

    if (s->variability_period && s->is_eclipsing_binary) line << "E";
    l += 2;
    line << std::string(l - line.str().size(), ' ');

    // Durchmusterung (BD/CD/CP) designation, kept so re-derived caches and later-loaded catalogs
    // (SB9, CCDM) can still cross-reference stars that carry no HD/HIP -- see bonn_survey_key().
    if (s->Bonn_survey[0]) line << s->Bonn_survey[0] << s->Bonn_survey[1];
    l += 3;
    line << std::string(l - line.str().size(), ' ');

    if (s->Bonn_survey[0])
    {
        line << ((s->Bonn_survey_declination >= 0) ? "+" : "-");
        if (abs(s->Bonn_survey_declination) < 10) line << "0";
        line << abs(s->Bonn_survey_declination) << std::flush;
    }
    l += 5;
    line << std::string(l - line.str().size(), ' ');

    if (s->Bonn_survey[0]) line << s->Bonn_survey_sequential;
    l += 7;
    line << std::string(l - line.str().size(), ' ');

    // has_custom_name: not derivable from anything else in this format, and unlike every other
    // field here it's read back into a struct member that starts false, so simply never touching
    // it doesn't leave it correct by omission -- every reload from this cache silently forgot
    // which stars had a settled name (a proper name from starname.dat, a hardcoded exception like
    // 55 Cnc B) versus one still open to Bayer/Flamsteed re-derivation. Concretely: any named
    // companion star that read_star_orbits_dat() also processes (make_companion_of() gates on this
    // exact flag) got silently renamed back to its Bayer designation on the *second* load from a
    // cache that had it right the first time -- e.g. Alpha Centauri B, "Toliman" -> "Alpha 2
    // Centauri", only after closing and reopening once the correct name was already baked in.
    if (s->has_custom_name) line << "Y";
    l += 2;
    line << std::string(l - line.str().size(), ' ');

    // std::cout << line << std::endl;
    line << std::string("\n");
    fputs(line.str().c_str(), fp);
}

int alienorum::CatalogReader::write_condensed_star_cat(ConsBins cb)
{
    int i, n, written = 0;
    char c;
    std::string path = get_condensed_starcat_name();
    FILE *fp = fopen(path.c_str(), "w");
    if (fp)
    {
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
                    }
                }
            }
        }

        fclose(fp);
    }

    return written;
}

std::string alienorum::CatalogReader::get_condensed_starcat_name()
{
    return "catalogs" _FILESLASH "soles_alienorum.dat";
}

int alienorum::CatalogReader::read_condensed_star_cat()
{
    int num_read = 0, i, j;
    char buffer[1024];
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

    while (fgets(buffer, 1022, fp))
    {
        Star *s = new Star();
        read_field_onebased(buffer, 1, 14, field);
        s->alienorumid = trim(field);

        read_field_onebased(buffer, 16, 54, field);

        str = trim(field);
        strcpy(s->name, str.c_str());
        s->namelen = 0;
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
        s->apparent_magnitude = atof(field);

        read_field_onebased(buffer, 84, 98, field);
        str = trim(field);
        strcpy(s->spectral_type, str.c_str());

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

        read_field_onebased(buffer, 128, 132, field);
        s->HR = atoi(field);

        read_field_onebased(buffer, 134, 140, field);
        s->HR = atoi(field);

        read_field_onebased(buffer, 142, 145, field);
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

        Constellation *mycons = nullptr;
        if (strlen(s->Bayer) || (s->FlamsteedNo > 0) || (s->GouldNo > 0))
        {
            strcpy(s->constellation, cons_from_alienorumid(s->alienorumid).c_str());
            j = constellations.size();
            for (i=0; i<j && !mycons; i++) if (!strcmp(constellations[i].name.c_str(), s->constellation)) mycons = &constellations[i];
        }

        if (s->FlamsteedNo > 0)
        {
            str = std::to_string(s->FlamsteedNo) + std::string(" ");
            if (s->FlamsteedNo < 10) str += std::string(" ");
            str += std::string(s->constellation);
            strcpy(s->Flamsteed, str.c_str());

            if (mycons) mycons->Flamsteed_stars[s->FlamsteedNo] = s;
        }

        if (mycons && s->BayerGrkno >= 0) mycons->Bayer_stars[s->BayerGrkno] = s;
        if (mycons && s->GouldNo > 0) mycons->Gould_stars[s->GouldNo] = s;

        read_field_onebased(buffer, 174, 184, field);
        s->proper_motion_RA = atof(field);

        read_field_onebased(buffer, 186, 196, field);
        s->proper_motion_decl = atof(field);

        read_field_onebased(buffer, 198, 208, field);
        s->radial_velocity = atof(field);

        read_field_onebased(buffer, 210, 219, field);
        s->parallax = atof(field);

        read_field_onebased(buffer, 222, 231, field);
        s->distance = atof(field) * light_year;
        s->update_location(simnow);
        if (s->parallax > 0) s->distance_known = true;

        read_field_onebased(buffer, 234, 239, field);
        s->absolute_magnitude = atof(field);

        read_field_onebased(buffer, 241, 250, field);
        s->mass = atof(field) * solar_mass;
        if (s->mass < jupiter_mass) s->mass = s->estimate_mass();

        read_field_onebased(buffer, 253, 262, field);
        s->volumetric_mean_radius = atof(field) * solar_radius;
        if (s->volumetric_mean_radius < 0.5 * jupiter_radius) s->volumetric_mean_radius = s->estimate_radius();

        read_field_onebased(buffer, 264, 270, field);
        s->temperature = atof(field);

        read_field_onebased(buffer, 272, 281, field);
        s->sidereal_rotational_period = atof(field) * oneday;
        if (!s->sidereal_rotational_period) s->sidereal_rotational_period = oneday*25;

        read_field_onebased(buffer, 283, 296, field);
        str = trim(field);
        if (str.size())
        {
            s->orbit = new Orbit();
            s->orbit->center_name = str;
        }

        read_field_onebased(buffer, 298, 308, field);
        if (s->orbit) s->orbit->period = atof(field) * oneday;

        read_field_onebased(buffer, 311, 322, field);
        if (s->orbit) s->orbit->semimajor_axis = atof(field) * AU;

        read_field_onebased(buffer, 324, 330, field);
        if (s->orbit) s->orbit->eccentricity = atof(field);

        read_field_onebased(buffer, 337, 344, field);
        if (s->orbit) s->orbit->arg_periapsis = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 346, 353, field);
        if (s->orbit) s->orbit->mean_anomaly = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 355, 368, field);
        if (s->orbit) s->orbit->epoch = atof(field);

        read_field_onebased(buffer, 370, 377, field);
        f = atof(field) * fiftyseventh;
        if (s->orbit) s->orbit->heliocentric_inclination = f;

        read_field_onebased(buffer, 379, 386, field);
        f = atof(field) * fiftyseventh;
        if (s->orbit) s->orbit->heliocentric_node = f;

        read_field_onebased(buffer, 388, 399, field);
        s->variability_period = atof(field) * oneday;

        read_field_onebased(buffer, 401, 406, field);
        s->minmag = atof(field);

        read_field_onebased(buffer, 408, 413, field);
        s->maxmag = atof(field);

        read_field_onebased(buffer, 415, 431, field);
        s->epoch_max_brightness = atof(field);

        read_field_onebased(buffer, 433, 433, field);
        if (field[0] == 'E') s->is_eclipsing_binary = true;

        // 435-448  Durchmusterung (BD/CD/CP), written by write_condensed_star_cat_line(). Older
        // caches predating this column simply read back empty here, same as any other blank field.
        read_field_onebased(buffer, 435, 436, field);
        str = trim(field);
        if (str.size() >= 2)
        {
            s->Bonn_survey[0] = str[0];
            s->Bonn_survey[1] = str[1];

            read_field_onebased(buffer, 438, 441, field);
            s->Bonn_survey_sign = field[0];
            s->Bonn_survey_declination = atoi(field);

            read_field_onebased(buffer, 443, 448, field);
            s->Bonn_survey_sequential = atoi(field);

            std::string dmkey = bonn_survey_key(s->Bonn_survey, s->Bonn_survey_declination, s->Bonn_survey_sequential);
            if (dmkey.size() && !dmcache.count(dmkey)) dmcache[dmkey] = s;
        }

        // 450  has_custom_name -- without this, every reload from this cache forgot which stars had
        // a settled name (starname.dat, or a hardcoded exception), leaving them open to being
        // silently renamed back to a Bayer/Flamsteed designation by whichever later pass runs
        // unconditionally (make_companion_of() via read_star_orbits_dat(), in particular).
        read_field_onebased(buffer, 450, 450, field);
        if (field[0] == 'Y') s->has_custom_name = true;

        append_cel(s);
        num_read++;
    }

    fclose(fp);


    ((Star*)cels[0])->distance_known = true;

    for (i=0; cels[i]; i++)
    {
        if (cels[i]->orbit && cels[i]->orbit->center_name.size())
        {
            for (j=0; cels[j]; j++)
            {
                if (j==i) continue;
                if (!strcmp(((Star*)cels[j])->alienorumid.c_str(), cels[i]->orbit->center_name.c_str()))
                {
                    Star *A = (Star*)cels[j];
                    cels[i]->orbit->center = A;
                    cels[i]->origcenname = A->name;

                    A->update_location(simnow);
                    if (cels[i]->orbit->heliocentric_inclination || cels[i]->orbit->heliocentric_node)
                    {
                        if (!A->lock_system_plane)
                        {
                            A->location.equatorial_plane = A->location.orbital_plane = A->location.local_system_plane
                                                           = system_plane_from_incl_and_node(cels[i]->orbit->heliocentric_inclination ?: half_pi,
                                                                   cels[i]->orbit->heliocentric_node, A->location.system_center);
                            // A->lock_system_plane = true;
                        }

                        cels[i]->known_poles = A->known_poles = true;
                    }
                    break;
                }
            }
        }
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
    if (!row.contains("pl_name") || row["pl_name"].is_null()
            || !row.contains("hostname") || row["hostname"].is_null()
            || !row.contains("pl_orbsmax") || row["pl_orbsmax"].is_null()
            || !row.contains("pl_orbper") || row["pl_orbper"].is_null()
       )
    {
        return r;
    }

    r.pl_name = row["pl_name"].get<std::string>();
    r.hostname = row["hostname"].get<std::string>();

    try
    {
        r.hd_name  = row.contains("hd_name" ) ? row["hd_name" ].get<std::string>() : "";
    }
    catch (...) { ; }
    try
    {
        r.hip_name = row.contains("hip_name") ? row["hip_name"].get<std::string>() : "";
    }
    catch (...) { ; }

    auto getd = [&row](const char* key) -> double
    {
        return (row.contains(key) && !row[key].is_null()) ? row[key].get<double>() : NAN;
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

    if (row.contains("st_spectype") && !row["st_spectype"].is_null())
    {
        r.st_spectype = row["st_spectype"].get<std::string>();
        size_t pos = r.st_spectype.find("&plusmn;");
        if (pos != std::string::npos) r.st_spectype.replace(pos, 8, "\xf1");
    }

    r.pl_orbincl = getd("pl_orbincl");
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

    fputc('\n', fp);
}

bool CatalogReader::exorow_read_line(FILE* fp, ExoRow& row)
{
    char buffer[2048];
    if (!fgets(buffer, sizeof(buffer), fp)) return false;

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

    return true;
}

Star* CatalogReader::resolve_or_create_exostar(const ExoRow& row, bool loaded_starsonly, bool* was_new)
{
    *was_new = false;
    std::string hostname = row.hostname;
    char cinit = hostname.c_str()[0];

    if (cinit == 'P' && hostname.substr(0, 8) == "Proxima ") hostname = "Proxima Cen";
    else if (cinit == 'T' && hostname.substr(0, 11) == "Teegarden's") hostname = "Teegarden's Star";


    // 1. Resolve host star context: check if it already exists in global array
    Star* host_star = nullptr;
    if (!host_star && hostname.substr(0, 6) == "82 Eri" && hdcache[20794]) host_star = hdcache[20794];
    if (!host_star && row.hd_name.size() > 2)
    {
        int HD = atoi(&(row.hd_name.c_str()[2]));
        if (hdcache[HD]) host_star = hdcache[HD];
    }
    if (!host_star && row.hip_name.size() > 3)
    {
        int HIP = atoi(&(row.hip_name.c_str()[3]));
        if (hipcache[HIP]) host_star = hipcache[HIP];
    }
    if (!host_star)
    {
        if (loaded_starsonly || worth_searching(hostname))
        {
            if (!strcmp(hostname.c_str(), "55 Cnc B")) hostname = "GJ 324 B";
            int i = find_object(hostname.c_str(), true);
            if (i>0) host_star = (Star*)cels[i];
        }
        else
        {
            for (int i=1; !host_star && (i<=10); i++)
            {
                int j = ncelobjs-i;
                if (j<0) continue;
                if (cels[j]->type == star && !strcmp(cels[j]->name, hostname.c_str()))
                    host_star = (Star*)cels[j];
            }
        }
    }

    // If the star doesn't exist, instantiate it
    if (!host_star)
    {
        if (loaded_starsonly) return nullptr;
        if (ncelobjs >= MAX_CELOBJS) return nullptr;
        bool star_exists = false;

        host_star = new Star();
        host_star->type = star;
        snprintf(host_star->name, sizeof(host_star->name), "%s", hostname.c_str());

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
                if (hdcache[host_star->HD])
                {
                    delete host_star;
                    host_star = hdcache[host_star->HD];
                    star_exists = true;
                }
                else hdcache[host_star->HD] = host_star;
            }
        }
        else if (name_len >= 4 && strncmp(host_star->name, "HIP ", 4) == 0)
        {
            host_star->HIP = atoi(&host_star->name[4]);
            if (host_star->HIP <= MAX_HIP)
            {
                if (hipcache[host_star->HIP])
                {
                    delete host_star;
                    host_star = hipcache[host_star->HIP];
                    star_exists = true;
                }
                else hipcache[host_star->HIP] = host_star;
            }
        }

        double sy_dist = isnan(row.sy_dist) ? 0 : row.sy_dist * parsec;
        double ra  = isnan(row.ra)  ? 0 : row.ra  * fiftyseventh;
        double dec = isnan(row.dec) ? 0 : row.dec * fiftyseventh;
        double st_lum = 0, sy_vmag = 1e290;

        // Temperature that st_lum depends on.
        if (!isnan(row.st_teff))
        {
            host_star->temperature = row.st_teff;
            host_star->estimate_BV(host_star->temperature);
        }

        if (!isnan(row.st_lum))
        {
            st_lum = row.st_lum;
            double lum = pow(10, st_lum);
            if (lum)
            {
                // st_lum is BOLOMETRIC (log10 L/Lsol), but absolute_magnitude is VISUAL
                // everywhere else in the program. Subtract the correction est_bolometric_flux()
                // will add back, or the bolometric correction gets counted twice (harmless for a
                // G star, a factor of 42 for an M dwarf).
                double m_bol = 4.74 - log(lum) / log(magnbase);
                host_star->absolute_magnitude = m_bol - Star::bolometric_correction(host_star->temperature);
            }
        }
        else host_star->absolute_magnitude = 10;

        if (!isnan(row.sy_vmag))
        {
            sy_vmag = row.sy_vmag;
            host_star->apparent_magnitude = sy_vmag;
        }

        if (sy_dist && (ra || dec))
        {
            host_star->distance = sy_dist;
            host_star->right_ascension = ra;
            host_star->declination = dec;
            host_star->update_location(simnow);
            host_star->distance_known = true;
        }
        else if (ra && dec && st_lum && sy_vmag < 1e203)
        {
            host_star->distance = host_star->distance_from_magnitudes(host_star->apparent_magnitude, host_star->absolute_magnitude);
            host_star->right_ascension = ra;
            host_star->declination = dec;
            host_star->update_location(simnow);
        }
        else
        {
            std::cerr << "WARNING: Missing ra/dec or distance for " << hostname << std::endl;
            delete host_star;
            return nullptr;
        }

        if (!isnan(row.st_teff))
        {
            host_star->estimate_BV(row.st_teff);
        }
        else host_star->apparent_magnitude = 11;

        if (isinf(host_star->absolute_magnitude))
        {
            double intrinsic_brightness = pow(magnbase, -host_star->apparent_magnitude) * pow(fmax(AU, host_star->distance) / parsec / 10, 2);
            host_star->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
        }

        if (!star_exists) append_cel(host_star);
        *was_new = !star_exists;
    }

    if (!isnan(row.st_mass))
        host_star->mass = row.st_mass * solar_mass;
    if (!isnan(row.st_rad))
        host_star->volumetric_mean_radius = row.st_rad * solar_radius;
    if (row.st_spectype.size())
    {
        strcpy(host_star->spectral_type, row.st_spectype.c_str());
        if (!host_star->BV_color) host_star->BV_color = Star::interpolate_mseq_BV(Star::get_mseqidx_from_sptyp(host_star->spectral_type));
    }
    if (!isnan(row.st_rotp))
        host_star->sidereal_rotational_period = row.st_rotp * oneday;

    return host_star;
}

void CatalogReader::add_exoplanet_from_row(const ExoRow& row, Star* host_star, std::map<int, std::vector<int>>& planet_celids, unsigned int& result)
{
    // 2. Instantiate and fill target Planet properties
    Planet* new_planet = new Planet();
    snprintf(new_planet->name, sizeof(new_planet->name), "%s", row.pl_name.c_str());
    new_planet->cenobj = host_star;

    double inclination = isnan(row.pl_orbincl) ? 0 : row.pl_orbincl * fiftyseventh;

    double pl_mass = 0, pl_msini = 0;
    bool pl_massknown = false, pl_msini_known = false, pl_radknown = false;
    if (!isnan(row.pl_msinij))
    {
        new_planet->mass = row.pl_msinij * jupiter_mass;
        pl_msini_known = true;
        pl_msini = new_planet->mass;
    }
    else if (!isnan(row.pl_msinie))
    {
        new_planet->mass = row.pl_msinie * earth_mass;
        pl_msini_known = true;
        pl_msini = new_planet->mass;
    }

    if (!isnan(row.pl_bmassj))
    {
        new_planet->mass = row.pl_bmassj * jupiter_mass;
        pl_massknown = true;
        pl_mass = new_planet->mass;
    }
    else if (!isnan(row.pl_bmasse))
    {
        new_planet->mass = row.pl_bmasse * earth_mass;
        pl_massknown = true;
        pl_mass = new_planet->mass;
    }

    if (!isnan(row.pl_radj))
    {
        new_planet->volumetric_mean_radius = row.pl_radj * jupiter_radius;
        pl_radknown = true;
    }
    else if (!isnan(row.pl_rade))
    {
        new_planet->volumetric_mean_radius = row.pl_rade * earth_radius;
        pl_radknown = true;
    }

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
            return;             // Skip adding a duplicate planet.
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
            mass_untrustworthy = inclination ? !consistent : consistent;
        }
    }

    if (mass_untrustworthy)
    {
        double st_incl = 0;
        if (host_star->disk_heliocen_inclination) st_incl = host_star->disk_heliocen_inclination;           // e.g. Eps Eri, Tau Cet, 82 Eri
        else if (host_star->rot_heliocen_incl) st_incl = host_star->rot_heliocen_incl;                      // e.g. Alp Men

        if (st_incl)
        {
            new_planet->mass = pl_msini / sin(st_incl);
            std::cout << "Mass of " << new_planet->name << " computed at " << (new_planet->mass / earth_mass)
                      << " m(Earth) based on system inclination " << (st_incl * fiftyseven) << std::endl;
            pl_massknown = true;
        }
    }
    new_planet->classify(new_planet->is_in_con_HZ(), pl_massknown && pl_radknown);

    if (new_planet->mass > 0 && new_planet->volumetric_mean_radius == 0)
    {
        new_planet->estimate_radius();
    }
    new_planet->estimate_albedo_and_absmagn();
    new_planet->estimate_rotation();
    new_planet->setup_atm_ring_props();

    // Append planet object to global array
    append_cel(new_planet);
    result++;
    host_star->has_planets++;
    if (new_planet->is_in_con_HZ()) host_star->has_hz_planets++;
    host_star->pl_indices.push_back(new_planet->seqno);

    if (planet_celids.find(host_star->seqno) == planet_celids.end())
        planet_celids[host_star->seqno] = std::vector<int>();
    planet_celids[host_star->seqno].push_back(new_planet->seqno);
}

unsigned int CatalogReader::load_exoplanets_from_tap(bool stars_only)
{
    unsigned int result = 0;
    bool do_download = true;
    std::map<int, std::vector<int>> planet_celids;
    static bool loaded_starsonly = false;

    const char* exocache = "catalogs/exoplanets.json";
    const char* derived_cache = stars_only ? "catalogs/exoplanets_stars.dat" : "catalogs/exoplanets_planets.dat";

    std::stringstream lmss;
    lmss << "Loading exoplanets from unified TAP sources...";
    mtx.lock();
    loading_msg = lmss.str();
    mtx.unlock();

    if (file_exists(exocache) && file_age(exocache) < 7*86400) do_download = false;

    if (!do_download && file_exists(derived_cache))
    {
        mtx.lock();
        loading_msg = std::string("Loading exoplanets from cache...");
        mtx.unlock();

        FILE* fp = fopen(derived_cache, "rb");
        if (fp)
        {
            ExoRow row;
            while (exorow_read_line(fp, row))
            {
                if (ncelobjs >= MAX_CELOBJS) break;
                bool was_new = false;
                Star* host_star = resolve_or_create_exostar(row, loaded_starsonly, &was_new);
                if (!host_star) continue;
                if (stars_only) continue;
                add_exoplanet_from_row(row, host_star, planet_celids, result);
            }
            fclose(fp);

            apply_exoplanet_names(planet_celids);
            if (stars_only) loaded_starsonly = true;
            return result;
        }
    }

    std::string readBufferNASA;
    std::string readBufferEU;
    json planets_array = json::array(); // Unified array

    if (radio_silence) do_download = false;

    if (do_download && !radio_silence)
    {
        // Reusable lambda to abstract libcurl boilerplate
        auto fetch_tap = [&](const std::string& url, std::string& out_buffer) -> bool
        {
            CURL* curl = curl_easy_init();
            if (!curl) return false;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_buffer);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            CURLcode res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            return res == CURLE_OK;
        };

        // 1. Fetch NASA Data
        std::string nasa_url = "https://exoplanetarchive.ipac.caltech.edu/TAP/sync?maxrec=20000&query="
                               "select+pl_name,hostname,hd_name,hip_name,pl_orbper,pl_orbsmax,pl_orbeccen,pl_orbincl,pl_orblper,pl_orbtper,pl_tranmid,"
                               "pl_bmasse,pl_bmassj,pl_msinij,pl_msinie,pl_rade,pl_radj,pl_trueobliq,"
                               "st_mass,st_rad,sy_dist,ra,dec,sy_vmag,st_spectype,st_teff,st_lum,st_rotp+"
                               "from+pscomppars+order+by+pl_name+asc&format=json";

        if (!fetch_tap(nasa_url, readBufferNASA))
        {
            std::cerr << "NASA TAP Query failed." << std::endl;
        }

        // 2. Fetch exoplanet.eu Data via PADC VESPA TAP
        // ADQL column aliasing maps EU's schema directly onto NASA's nomenclature.
        std::string eu_url = "http://voparis-tap-planeto.obspm.fr/tap/sync?"
                             "request=doQuery&lang=ADQL&format=json&maxrec=20000&query="
                             "select+target_name+as+pl_name,star_name+as+hostname,"
                             "period+as+pl_orbper,semi_major_axis+as+pl_orbsmax,eccentricity+as+pl_orbeccen,inclination+as+pl_orbincl,"
                             "periastron+as+pl_orblper,t_peri+as+pl_orbtper,"
                             "mass+as+pl_bmassj,mass_sin_i+as+pl_msinij,radius+as+pl_radj,"
                             "ra,dec,star_distance+as+sy_dist,mag_v+as+sy_vmag,"
                             "star_mass+as+st_mass,star_radius+as+st_rad,star_teff+as+st_teff,star_spec_type+as+st_spectype+"
                             "from+exoplanet.epn_core+order+by+target_name+asc";

        if (!fetch_tap(eu_url, readBufferEU))
        {
            std::cerr << "Exoplanet.eu TAP Query failed." << std::endl;
        }

        try
        {
            // Utility lambdas for string parsing
            auto normalize_name = [](std::string name) -> std::string
            {
                std::string res;
                for (char c : name)
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
            auto register_planet = [&](const json& item)
            {
                json litem = item;
                std::string pl_name = litem.value("pl_name", "");
                if (pl_name.empty()) return;
                char cinit = pl_name.c_str()[0];
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
                else if (cinit == 'H' && pl_name.substr(0, 9) == "HD 20794 ")
                {
                    pl_name = std::string("82 Eri ") + pl_name.substr(pl_name.size()-1, 1);
                    if (pl_name == "82 Eri d") pl_name = "82 Eri c";            // this will not affect exoplanet.eu's 82 Eri d because we're already filtering on the NASA nomenclature.
                    else if (pl_name == "82 Eri f") pl_name = "82 Eri d";
                    litem["pl_name"] = pl_name;
                }

                std::string hostname = litem.value("hostname", "");
                if (cinit == '8' && pl_name.substr(0, 7) == "82 Eri ")
                {
                    litem["hd_name"] = "HD 20794";
                }
                else if (cinit == 'H' && pl_name.substr(0, 3) == "HD ")
                {
                    int HD = std::max(extract_cat_num(hostname, "HD"), extract_cat_num(pl_name, "HD"));
                    if (HD < MAX_HD && hdcache[HD] && hdcache[HD]->Gliese[0])
                    {
                        hostname = hdcache[HD]->Gliese;
                        pl_name = hostname + " " + std::string(" ") + pl_name.substr(pl_name.size()-1, 1);
                        litem["pl_name"] = pl_name;
                    }
                }
                else if (cinit == 'H' && pl_name.substr(0, 4) == "HIP ")
                {
                    int HIP = std::max(extract_cat_num(hostname, "HIP"), extract_cat_num(pl_name, "HIP"));
                    if (HIP < MAX_HIP && hipcache[HIP] && hipcache[HIP]->Gliese[0])
                    {
                        hostname = hipcache[HIP]->Gliese;
                        pl_name = hostname + " " + std::string(" ") + pl_name.substr(pl_name.size()-1, 1);
                        litem["pl_name"] = pl_name;
                    }
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
                    int hd = -1, hip = -1;

                    // NASA provides explicit fields; EU relies on hostname/pl_name
                    if (litem.contains("hd_name") && litem["hd_name"].is_string()) hd = extract_cat_num(litem.value("hd_name", ""), "HD");
                    if (litem.contains("hip_name") && litem["hip_name"].is_string()) hip = extract_cat_num(litem.value("hip_name", ""), "HIP");

                    std::string hostname = litem.value("hostname", "");
                    if (hd == -1) hd = std::max(extract_cat_num(hostname, "HD"), extract_cat_num(pl_name, "HD"));
                    if (hip == -1) hip = std::max(extract_cat_num(hostname, "HIP"), extract_cat_num(pl_name, "HIP"));

                    std::string hd_alias = hd != -1 ? "hd" + std::to_string(hd) + letter : "";
                    std::string hip_alias = hip != -1 ? "hip" + std::to_string(hip) + letter : "";

                    // Point to existing canonical record if an alias matches
                    if (!hd_alias.empty() && primary_keys.count(hd_alias)) target_key = primary_keys[hd_alias];
                    else if (!hip_alias.empty() && primary_keys.count(hip_alias)) target_key = primary_keys[hip_alias];

                    // Register this planet's identifiers to the canonical target
                    primary_keys[norm] = target_key;
                    if (!hd_alias.empty()) primary_keys[hd_alias] = target_key;
                    if (!hip_alias.empty()) primary_keys[hip_alias] = target_key;
                }

                // 2. Field-level Merge
                if (merged_planets.find(target_key) != merged_planets.end())
                {
                    auto& existing = merged_planets[target_key];
                    for (auto& [key, value] : litem.items())
                    {
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
                        register_planet(obj);
                    }
                }
            }

            // Read NASA Second (fills in the gaps)
            if (!readBufferNASA.empty())
            {
                json nasa_json = json::parse(readBufferNASA);
                
                // NASA uses a flat JSON array of objects
                if (nasa_json.is_array())
                {
                    for (const auto& item : nasa_json)
                    {
                        register_planet(item);
                    }
                }
            }

            if (merged_planets.empty())
            {
                std::cerr << "Both TAP queries failed or returned empty data." << std::endl;
                return 0;
            }

            // TODO: dedup planets of the same star that have SMA or period within 15%.

            // Repackage to array
            for (auto& [key, val] : merged_planets) planets_array.push_back(val);

            std::fstream fs(exocache, std::ios::out);
            if (fs)
            {
                fs << planets_array.dump(4);
                fs.close();
            }
        }
        catch (const json::parse_error& e)
        {
            std::cerr << "JSON Parsing Exception encountered: " << e.what() << std::endl << std::endl
                << "NASA returned:" << std::endl << readBufferNASA << std::endl << std::endl
                << "EU returned:" << std::endl << readBufferEU << std::endl << std::endl
                ;
            return 0;
        }
    }
    else
    {
        std::fstream fs(exocache, std::ios::in);
        if (!fs) return 0;
        fs >> planets_array;
        fs.close();
    }

    FILE* cachefp = fopen(derived_cache, "wb");
    for (const auto& jrow : planets_array)
    {
        if (ncelobjs >= MAX_CELOBJS)
        {
            std::cerr << "Warning: Maximum sequential object allocation threshold reached (" << MAX_CELOBJS << ")." << std::endl;
            break;
        }

        bool ok = false;
        ExoRow row = exorow_from_json(jrow, &ok);
        if (!ok) continue;

        bool was_new = false;
        Star* host_star = resolve_or_create_exostar(row, loaded_starsonly, &was_new);
        if (!host_star) continue;

        if (stars_only)
        {
            if (was_new && cachefp) exorow_write_line(cachefp, row);
            continue;
        }

        add_exoplanet_from_row(row, host_star, planet_celids, result);
        if (cachefp) exorow_write_line(cachefp, row);

        if (frand(0,1) < 0.01)
        {
            lmss.str("");
            lmss << "Loaded " << result << " exoplanets from TAP catalogs...";
            mtx.lock();
            loading_msg = lmss.str();
            mtx.unlock();
        }
    }

    if (cachefp) fclose(cachefp);
    apply_exoplanet_names(planet_celids);
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
// convention. Ellipticals have nothing to deproject (apparent flattening is intrinsic shape, not
// orientation), so they take a flat 90 degrees.
static double galaxy_inclination(double axis_ratio, double T, bool T_known)
{
    if (T_known && T < 0) return half_pi;               // elliptical or S0: see above

    double q0 = 0.20;                                   // the classic value, for an unknown type
    if (T_known)
    {
        if (T <= 4.0) q0 = 0.26;                        // early spirals, thicker discs
        else if (T <= 7.0) q0 = 0.16;                   // late spirals, the thinnest
        else q0 = 0.42;                                 // irregulars, genuinely puffy
    }

    if (axis_ratio >= 1.0) return 0;                    // round on the sky: face-on
    if (axis_ratio <= q0) return half_pi;               // at or past the edge-on limit

    double cos2 = (axis_ratio*axis_ratio - q0*q0) / (1.0 - q0*q0);
    if (cos2 < 0) cos2 = 0;
    if (cos2 > 1) cos2 = 1;
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
        g->inclination = galaxy_inclination(g->axis_ratio, g->morphological_T, g->T_known);

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
        g->location.equatorial_plane = g->location.local_system_plane
                                       = system_plane_from_incl_and_node(g->inclination, g->position_angle, (Point)g->location);

        g->cenobj = g;
        g->distance_known = true;
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
            if (!strlen(trim(field).c_str())) continue;
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
                               : galaxy_inclination(dup->axis_ratio, dup->morphological_T, dup->T_known);

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

        g->inclination = galaxy_inclination(g->axis_ratio, g->morphological_T, g->T_known);

        // 186-188 position angle of the major axis, degrees
        read_field_onebased(buffer, 186, 188, field);
        if (strlen(trim(field).c_str()))
        {
            g->position_angle = atof(field) * fiftyseventh;
            g->position_angle_known = true;
        }
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
