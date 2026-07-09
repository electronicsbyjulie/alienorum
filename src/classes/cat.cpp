
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <cstdlib>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <ctime>
#include <map>
#include <string.h>
#include "cat.h"
#include "serial.h"

// Zeta 1,2 Reticuli make for a good case study in the program correctly identifying binary systems,
// since they have a decent separation both in actual physical distance and in angular position from the solar system.
// They are close enough to Earth that differences in parallax might not be within margins of error, allowing a
// decent estimation of the parallax tolerances for stars in the same system, and the system's inclination is likely to be near edge-on.
// They also are not identified in the catalogs as stars A and B of a binary, which also makes them a great test
// since we can't just lazily require all binary systems to be marked this way.
#define _debug_sbinaries_zetret 0

namespace fs = std::filesystem;

std::vector<std::string> known_catalog_names =
{
    "Gliese", "GJ", "Gliese-Jahreiss",
    "HD", "HenryDraper",
    "Hipparcos",
    "Uranometria",
    "USNO", "SAO",
    "BSC", "BrightStarCatalog", "BrightStarCatalogue",
    "WD",
    "CCDM",
    "SB9",
    "2MASS",
    "REGALADE",
    "GALEX",
    "astorb",
    "comets"
    // TODO: Add hundreds more...
};

std::vector<std::string> consline_a, consline_b;
std::vector<int> considx, lnpercons;
std::vector<Point> consdir;
int nconsln = 0;
int *consaidx, *consbidx;

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

void CatalogReader::download_catalogs()
{
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
        if (buffer[0] == '#') continue;

        for (i=0; buffer[i] && buffer[i] <= ' '; i++);
        catname = &buffer[i];
        if (!*catname) continue;
        if (catname[0] == '#') continue;

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
            if (!strcmp(".tar.gz", &entry_name.c_str()[j]))
            {
                cmd = (std::string)"tar -xvzf " + destdir + _FSSTR + entry_name;
                std::cout << cmd << std::endl;
                std::system(cmd.c_str());
            }
            else if (!strcmp(".gz", &entry_name.c_str()[j]))
            {
                std::string decompressed_name = entry_name.substr(0, entry_name.size()-3);
                if (!fs::exists((destdir + _FSSTR + decompressed_name).c_str()))
                {
                    #ifdef _WIN32
                    cmd = (std::string)"7z e -y " + destdir + _FSSTR + entry_name
                        + std::string(" -so > ") + destdir + _FSSTR + decompressed_name;
                    #else
                    cmd = (std::string)"gunzip " + destdir + _FSSTR + entry_name;
                    #endif
                    std::cout << cmd << std::endl;
                    std::system(cmd.c_str());
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

        if (j == 3785)                  // Alcor.
        {
            int i;
            for (i = ncelobjs-1; atoi(&((Star*)cels[i])->Gliese[2]) != 3784; i--);
            StarMulti *sm = ((Star*)cels[i])->multisys;
            A = sm->get_member('A');
            if (A) std::cout << "Mizar A = " << A->name << std::endl;
            else std::cout << "Mizar A is null." << std::endl;
            Star *B = sm->get_member('B');
            if (B) std::cout << "Mizar B = " << B->name << std::endl;
            else std::cout << "Mizar B is null." << std::endl;

            sm->add_member(s, 'E');
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
            s->absolute_magnitude = absmagn;
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
                s->mass = Msun;
                s->volumetric_mean_radius = Rsun;
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

        // CNS5 could have been a wonderful update to the earlier CNS3 with thousands of
        // new stars, but the team had to go and ruin it by removing data that were useful
        // and critically important like spectral type and B-V color. They replaced the color
        // indices with some bizarre format that would require complex temperature estimation
        // to convert, and while it may be possible to estimate spectral types from temperature
        // and absolute magnitude, there would undoubtedly be errors in the estimates.
        // So unfortunately we cannot load any new stars from CNS5 not already in CNS3,
        // and the new updated catalog is relegated to being mostly a cross reference with
        // a few data like radial velocity that can be updated.
        // What a sad waste of all the work they put into it to have so many of its rows end
        // up completely unusable.
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
    char comp = 0;
    // StarMulti *current_multi = nullptr;
    double current_multi_ra, current_multi_decl;
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

    Star *s, *A = nullptr;
    while (fgets(buffer, 65520, fp))
    {
        bool is_new = false;

        //   50- 51  A2     ---     ADScomp  ADS number components
        comp = buffer[49];          // Correct for one-based field description.
        if (comp < 'A') comp = 0;

        //   1-  4  I4     ---     HR       [1/9110]+ Harvard Revised Number = Bright Star Number
        read_field_onebased(buffer, 1, 4, field);
        HR = atoi(field);

        read_field_onebased(buffer, 26, 31, field);
        HD = atoi(field);

        HDfound = false;
        if (HD)
        {
            if (comp <= 'A' && hdcache[HD])
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

        //  15- 25  A11    ---     DM       Durchmusterung Identification (zone in bytes 17-19)
        read_field_onebased(buffer, 15, 16, field);
        s->Bonn_survey[0] = field[0];
        s->Bonn_survey[1] = field[1];
        read_field_onebased(buffer, 17, 19, field);
        s->Bonn_survey_sign = field[0];
        s->Bonn_survey_declination = atoi(field);
        read_field_onebased(buffer, 20, 25, field);
        s->Bonn_survey_sequential = atoi(field);

        read_field_onebased(buffer, 5, 7, field);
        s->FlamsteedNo = atoi(field);
        read_field_onebased(buffer, 8, 11, field);
        std::string bayer = trim(field);
        if (field[3] < 'A') s->BayerGrkno = Grkno_from_abbrev(field);
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

        if (HD)
        {
            if (comp <= 'A') hdcache[HD] = s;
        }

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
    Star *s, *A;

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
    }

    FILE* fp = fopen(path.c_str(), "rb");
    while (fgets(buffer, 1020, fp))
    {
        //   9- 14  I6    ---     HIP       Identifier (HIP number)
        read_field_onebased(buffer, 9, 14, field);
        HIP = atoi(field);

        // 391-396  I6    ---     HD        [1/359083]? HD number <III/135>
        read_field_onebased(buffer, 391, 396, field);
        HD = atoi(field);

        s = nullptr;
        bool is_new = false;

        if (HD && hdcache && hdcache[HD]) s = (Star*)hdcache[HD];
        else if (HIP && hipcache && hipcache[HIP]) s = (Star*)hipcache[HIP];
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

        // 398-407  A10   ---     BD        Bonner DM <I/119>, <I/122>               (H72)
        read_field_onebased(buffer, 398, 407, Bonn);

        // 409-418  A10   ---     CoD       Cordoba Durchmusterung (DM) <I/114>      (H73)
        read_field_onebased(buffer, 409, 418, Cordoba);

        // 420-429  A10   ---     CPD       Cape Photographic DM <I/108>             (H74)
        read_field_onebased(buffer, 420, 429, Cape);

        if (Cape[0] != ' ')
        {
            s->Bonn_survey[0] = 'C';
            s->Bonn_survey[1] = 'P';
            read_field_onebased(buffer, 421, 423, field);
            s->Bonn_survey_sign = field[0];
            s->Bonn_survey_declination = atoi(field);
            read_field_onebased(buffer, 424, 429, field);
            s->Bonn_survey_sequential = atoi(field);
        }
        else if (Cordoba[0] != ' ')
        {
            s->Bonn_survey[0] = 'C';
            s->Bonn_survey[1] = 'D';
            read_field_onebased(buffer, 410, 412, field);
            s->Bonn_survey_sign = field[0];
            s->Bonn_survey_declination = atoi(field);
            read_field_onebased(buffer, 413, 418, field);
            s->Bonn_survey_sequential = atoi(field);
        }
        else if (Bonn[0] != ' ')
        {
            s->Bonn_survey[0] = 'B';
            s->Bonn_survey[1] = 'D';
            read_field_onebased(buffer, 399, 401, field);
            s->Bonn_survey_sign = field[0];
            s->Bonn_survey_declination = atoi(field);
            read_field_onebased(buffer, 402, 407, field);
            s->Bonn_survey_sequential = atoi(field);
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

    mtx.lock();
    loading_msg = "Building Hipparcos-CCDM Cross Reference...";
    mtx.unlock();
    path = "catalogs" _FILESLASH "Hipparcos" _FILESLASH "h_dm_com.dat";
    fp = fopen(path.c_str(), "rb");
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

    mtx.lock();
    loading_msg = "Loading Hipparcos Binary Star Orbits...";
    mtx.unlock();
    path = "catalogs" _FILESLASH "Hipparcos" _FILESLASH "hip_dm_o.dat";
    fp = fopen(path.c_str(), "rb");
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
        s->orbit->semimajor_axis = (atof(field)/3600000) * fiftyseventh * s->distance;

        //  40- 45  F6.4  ---      ecc      [0,1] Eccentricity                       (DO5)
        read_field_onebased(buffer, 40, 45, field);
        s->orbit->eccentricity = atof(field);

        //  47- 52  F6.2  deg      w       *[0,360] Argument of periastron           (DO6)
        read_field_onebased(buffer, 47, 52, field);
        s->orbit->arg_periapsis = atof(field) * fiftyseventh;

        //  54- 59  F6.2  deg      i       *[0,180] Inclination                      (DO7)
        read_field_onebased(buffer, 54, 59, field);
        A->obliquity = atof(field) * fiftyseventh;
        s->orbit->inclination = 0;

        //  61- 66  F6.2  deg      Omega   *[0,360] Position angle of the node       (DO8)
        read_field_onebased(buffer, 61, 66, field);
        A->equinox = atof(field) * fiftyseventh;
        s->orbit->ascending_node = 0;

        A->update_location(J2000_TIME_T);
        s->location = A->location;
        A->known_poles = true;
        s->known_poles = true;

        s->apparent_magnitude = 11;         // placeholder
        if (s->absolute_magnitude > 1e28) s->absolute_magnitude = A->absolute_magnitude + 1;      // garbage number

        num_read++;
        if (num_read >= max-4) return num_read;
    }

    fclose(fp);
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
            s->make_companion_of(A, comp[star_name]);
            append_cel(s);
        }
        else
        {
            s = new Star();
            strcpy(s->name, field);
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

            if (!A->has_custom_name) strcpy(A->name, lop_component(A->name).c_str());
            if (!s->has_custom_name) strcpy(s->name, (std::string(A->name) + std::string(" B")).c_str() );
        }

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
        double rho = atof(field);
        if (!rho) continue;
        rho /= (3600 * fiftyseven);

        // Fill in positional parameters based on angular separation
        s->right_ascension = A->right_ascension - rho * sin(theta) / cos(A->declination);
        s->declination = A->declination + rho * cos(theta);

        s->location = A->location;                      // Copies local system reference frame
        s->epoch = J2000;
        s->update_location(J2000_TIME_T);

        // Estimate the semimajor axis
        double sma = sin(rho) * A->distance;
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

        // Since we are keeping star A stationary and orbiting star B around it
        // (an imperfect simulation) we must sum the two semimajor axes in order to
        // get the distance between stars.
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
    p->absolute_magnitude = absmagn;

    //  55- 58  F4.2  mag     B-V       ? Color index (see E.F.Tedesco, pp.1090-1138)
    read_field_onebased(buffer, 55, 58, field);
    if (trim(field).size())
        p->BV_color = atof(field);
    else p->BV_color = 0.71;                            // typical value for asteroids

    p->volumetric_mean_radius = r->diam * 500;
    if (!p->volumetric_mean_radius && !strcmp(r->name.c_str(), "Pluto")) p->volumetric_mean_radius = 1188300;
    assert(!isinf(p->volumetric_mean_radius));
    p->mass = p->volumetric_mean_radius * p->volumetric_mean_radius * p->volumetric_mean_radius * 4.0/3 * _pi * 1853;  // Pluto density.

    // 107-114  A8 "YYYYMMDD" Epoch     Epoch of osculation, yyyymmdd (TDT) (2)
    read_field_onebased(buffer, 107, 110, field);
    _year = atoi(field);
    read_field_onebased(buffer, 111, 112, field);
    _month = atoi(field);
    read_field_onebased(buffer, 113, 114, field);
    _day = atoi(field);

    tm epoch;
    epoch.tm_year = _year-1900;
    epoch.tm_mon = _month-1;
    epoch.tm_mday = _day;
    time_t t = mktime(&epoch);
    p->epoch = (t/oneday) + 2440587.5;

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
            && asno != 7000 && asno != 50000 && asno != 90377 && asno != 90482 && asno != 134340 && asno != 136108 && asno != 136199
            && asno != 136472 && asno != 163693 && asno != 541132
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
                else if (!strcmp(ihavetomove, "lavaworld")) p->type = lavaworld;
                else if (!strcmp(ihavetomove, "ice_giant")) p->type = ice_giant;
                else if (!strcmp(ihavetomove, "icy")) p->type = icy;
                else if (!strcmp(ihavetomove, "waterworld")) p->type = waterworld;
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

        s->location.local_system_plane = system_plane_from_incl_and_node(sysincl, sysnode, s->location.system_center);
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

            elements_in_new_reference_plane(system_plane_from_incl_and_node(pincls[i], pnodes[i], s->location.system_center),
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

        elements_in_new_reference_plane(system_plane_from_incl_and_node(stincl, stnode, s->location.system_center),
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
                    elements_in_new_reference_plane(system_plane_from_incl_and_node(cincls[i], cnodes[i], s->location.system_center),
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
                        s->cenobj = s;
                        s_is_new = true;
                    }

                    if (!p)
                    {
                        p = new Planet();
                        if (planet_name.size()) strcpy(p->name, planet_name.c_str());
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
                ((Star*)s)->has_planets++;
                if (HZ) ((Star*)s)->has_hz_planets++;

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
                strcpy(s->name, trim(field).c_str());
                s->has_custom_name = true;
                num_read++;

                if (s->multisys && s->multisys->get_member('A') == s)
                {
                    Star* companion;
                    for (char c = 'B'; (companion = s->multisys->get_member(c)); c++)
                    {
                        if (!companion->has_custom_name)
                            strcpy(companion->name, (lop_component(s->name) + std::string(" ") + std::string(1, c)).c_str() );
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

        if (!strcmp(bdystr, "(system inclination)")
            || strstr(bdystr, " disk") || strstr(bdystr, " disc")
            || strstr(bdystr, " belt"))
            A->has_disk = A->known_poles;
        if (A->has_disk)
        {
            A->disk_heliocen_inclination = inclination;
            A->disk_heliocen_node = ascending_node;
        }

        if (!strcmp(bdystr, "(stellar rotation)"))
        {
            A->rot_axis_known = A->known_poles;
            read_field_onebased(buffer, 49, 63, field);
            A->sidereal_rotational_period = atof(field);
        }

        if (bdystr[0] == '(') continue;

        if (!A)
        {
            std::cerr << "FAILED to orbit " << bdyname << " around " << cenname << ": center not found." << std::endl;
            continue;
        }

        s = nullptr;
        if (A->multisys)
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

        if (!s || s == A)
        {
            int bs = trim(buffer).size();
            if (bs >= 180)
            {
                s = new Star();
                s->distance_known = true;
                append_cel(s);
                strcpy(s->name, bdyname.c_str());
                read_field_onebased(buffer, 161, 175, field);
                strcpy(s->spectral_type, trim(field).c_str());
                read_field_onebased(buffer, 177, 191, field);
                s->absolute_magnitude = atof(field);
                double lum, tempK;
                if (bs > 193)
                {
                    read_field_onebased(buffer, 193, 203, field);
                    s->mass = atof(field) * solar_mass;
                }
                if (bs > 205)
                {
                    read_field_onebased(buffer, 205, 215, field);
                    s->volumetric_mean_radius = atof(field) * solar_radius;
                    assert(!isinf(s->volumetric_mean_radius));
                }
                if (bs > 217)
                {
                    read_field_onebased(buffer, 217, 227, field);
                    lum = atof(field) * solar_radius;
                    if (!s->absolute_magnitude)
                    {
                        double magshift = log(lum)/log(magnbase);
                        s->absolute_magnitude = 4.85 - magshift;
                    }
                }
                if (bs > 229)
                {
                    read_field_onebased(buffer, 229, 238, field);
                    tempK = atof(field);
                    if (!lum && s->volumetric_mean_radius && tempK)
                    {
                        s->absolute_magnitude = -log(s->estimate_luminosity(tempK))/log(magnbase);
                        s->estimate_BV(tempK);
                        s->estimate_UB(tempK);
                    }
                }
            }
            else
            {
                std::cerr << "FAILED to orbit " << bdyname << " around " << cenname << ": member not found and insufficient data to construct new." << std::endl;
                continue;
            }
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
        f = atof(field);
        if (f) s->orbit->period = f;

        read_field_onebased(buffer, 89, 99, field);
        f = atof(field) * fiftyseventh;
        if (f) s->orbit->arg_periapsis = f;

        read_field_onebased(buffer, 101, 111, field);
        f = atof(field);
        if (f) s->orbit->semimajor_axis = f;

        read_field_onebased(buffer, 113, 123, field);
        f = atof(field);
        if (f) s->orbit->eccentricity = f;

        read_field_onebased(buffer, 125, 143, field);
        f = atof(field) * fiftyseventh;
        if (f) s->orbit->mean_anomaly = f;

        read_field_onebased(buffer, 145, 155, field);
        f = atof(field) * fiftyseventh;
        if (f) s->orbit->mean_anomaly = f;

        if (inclination || ascending_node)
        {
            s->location.equatorial_plane = s->location.orbital_plane = s->location.local_system_plane = new_orbital_plane;
            s->lock_system_plane = true;
            s->obliquity = 0;
            s->equinox = 0;
            A->known_poles = s->known_poles = true;
        }

        num_read++;
    }

    return num_read;
}

int CatalogReader::read_local_planets(CelestialObject **cels, int max)
{
    std::fstream fs(std::string("catalogs" _FILESLASH "planets.json"), std::ios::in);
    if (!fs) throw 0xbadf12e;
    int result = 0, offset;
    json planets;
    fs >> planets;
    int i, j, k, n = planets.size();
    bool createnew;
    Planet *p;
    Moon *m;

    for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);
    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    for (i=0; i<n; i++)
    {
        json pl = planets[i];
        std::string bodyname, cenname, mapurl;
        try
        {
            pl.at("BODYNAME").get_to(bodyname);
            j = find_object(bodyname.c_str(), false, 9e+29, 0);
            cenname = "";
            try { pl.at("CENTER_OF_ORBIT").get_to(cenname); } catch (...) { ; }
            k = -1;
            if (cenname.size()) k = find_object(cenname.c_str(), false, 9e+29, 0);

            if (j < 0 || k >= 0)                // Name not taken or center of orbit,
            {                                   // create new.
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
            {                                   // update existing.
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
            } catch (...) { ; }
            try { pl.at("ABSMG").get_to(p->absolute_magnitude); } catch (...) { ; }
            try { pl.at("ArgPeri").get_to(p->orbit->arg_periapsis); p->orbit->arg_periapsis *= fiftyseventh; } catch (...) { ; }
            try { pl.at("AscNode").get_to(p->orbit->ascending_node); p->orbit->ascending_node *= fiftyseventh; } catch (...) { ; }
            try { pl.at("AtmosphericTau").get_to(p->atmospheric_tau); } catch (...) { ; }
            try { pl.at("BVmag").get_to(p->BV_color); } catch (...) { if (createnew) p->BV_color = p->orbit->center->BV_color; }
            try { pl.at("UBmag").get_to(p->UB_color); } catch (...) { if (createnew) p->UB_color = p->orbit->center->UB_color; }
            try { pl.at("Eccentricity").get_to(p->orbit->eccentricity); } catch (...) { ; }
            try { pl.at("Epoch").get_to(p->epoch); p->epoch = J2000 + (p->epoch - 2000)*(oneyear/oneday); p->orbit->epoch = p->epoch; } catch (...) { ; }
            try { double pre; pl.at("EqPrecession").get_to(pre); p->precession = pre ? (_pi * 2 / pre / oneyear) : 0; } catch (...) { ; }
            try { double pre; pl.at("NodePrecession").get_to(pre); p->orbit->prec_node = pre ? (_pi * 2 / pre / oneyear) : 0; } catch (...) { ; }
            try { double pro; pl.at("ArgPeriProcession").get_to(pro); p->orbit->proc_argperi = pro ? (_pi * 2 / pro / oneyear) : 0; } catch (...) { ; }
            try { pl.at("Equinox").get_to(p->equinox); p->equinox *= fiftyseventh; } catch (...) { ; }
            try { pl.at("Incl").get_to(p->orbit->inclination); p->orbit->inclination *= fiftyseventh; } catch (...) { ; }
            try { pl.at("J2").get_to(p->J2); } catch (...) { ; }
            try { pl.at("Lon_J2000_offset").get_to(p->lon_J2000_offset); p->lon_J2000_offset *= fiftyseventh; } catch (...) { ; }
            try
            {
                pl.at("Mass").get_to(p->mass);
                p->mass *= 1000;
                if (p->orbit->semimajor_axis >= 2e+12) p->type = ice_giant;
                else if (p->mass >= rocky_mass_cutoff) p->type = gas_giant;
            } catch (...) { ; }
            try { pl.at("MeanAnom").get_to(p->orbit->mean_anomaly); p->orbit->mean_anomaly *= fiftyseventh; } catch (...) { ; }
            try { pl.at("Oblateness").get_to(p->oblateness); } catch (...) { ; }
            try { pl.at("Obliquity").get_to(p->obliquity); p->obliquity *= fiftyseventh; } catch (...) { ; }
            try { pl.at("OrbitPeriod").get_to(p->orbit->period); } catch (...) { ; }
            try { pl.at("Particulates").get_to(p->atmospheric_particulates); } catch (...) { ; }
            try { pl.at("RotationPeriod").get_to(p->sidereal_rotational_period); } catch (...) { ; }
            try { pl.at("SEMIMAJOR_AXIS").get_to(p->orbit->semimajor_axis); } catch (...) { ; }
            try { pl.at("SurfacePressure").get_to(p->surface_pressure); } catch (...) { ; }
            try { pl.at("VolMeanRad").get_to(p->volumetric_mean_radius); } catch (...) { ; }
            try { pl.at("RingRadius").get_to(p->ring_radius); p->ring_radius *= 1000; } catch (...) { ; }
            // try { pl.at("").get_to(p->); } catch (...) { ; }

            if (m)
            {
                try { pl.at("Depth" ).get_to(m->depth ); m->depth  *= 1000; } catch (...) { ; }
                try { pl.at("Width" ).get_to(m->width ); m->width  *= 1000; } catch (...) { ; }
                try { pl.at("Height").get_to(m->height); m->height *= 1000; } catch (...) { ; }
                if (!m->sidereal_rotational_period) m->sidereal_rotational_period = m->orbit->period;
                if (m->depth > zero_isnt_really_zero && m->width > zero_isnt_really_zero && m->height > zero_isnt_really_zero)
                    m->volumetric_mean_radius = pow(m->depth * m->width * m->height, 0.333333333) * 0.5;
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
                } catch (...) { ; }
            }

            if (p->orbit && p->orbit->center && createnew)
            {
                p->known_poles = p->obliquity && p->equinox;
                p->location = p->orbit->center->location;          // Copy the system center and local plane. The local position will auto-fill later.
                p->location.equatorial_plane.a = p->obliquity;
                p->location.equatorial_plane.v = Point(std::sin(p->equinox), 0, -std::cos(p->equinox));

                Star* s = (Star*)p->get_light_center();
                if (p->orbit->center == s)
                {
                    s->has_planets++;
                    if (p->is_in_con_HZ()) s->has_hz_planets++;
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

unsigned int CatalogReader::load_exoplanets_from_tap()
{
    unsigned int result = 0;
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        std::cerr << "Failed to initialize libcurl." << std::endl;
        return false;
    }

    std::string readBuffer;
    json planets_array;
    bool do_download = true;
    std::map<int, std::vector<int>> planet_celids;

    const char* exocache = "catalogs/exoplanets.json";
    std::stringstream lmss;
    lmss << "Loading exoplanets from NASA via TAP...";
    mtx.lock();
    loading_msg = lmss.str();
    mtx.unlock();

    if (file_exists(exocache)) // && file_age(exocache) < 7*86400)
    {
        do_download = false;
        std::cout << "File age: " << file_age(exocache) << " seconds." << std::endl;
        std::fstream fs(exocache, std::ios::in);
        if (!fs) return 0;
        fs >> planets_array;
        fs.close();
    }

    if (do_download)
    {
        // Constructing the synchronous TAP ADQL query targeting the pscomppars table
        // Selects core planetary and fallback/stellar fields
        std::string url = "https://exoplanetarchive.ipac.caltech.edu/TAP/sync?query="
                        "select+pl_name,hostname,hd_name,hip_name,pl_orbper,pl_orbsmax,pl_orbeccen,pl_orbincl,pl_orblper,pl_orbtper,"
                        "pl_bmasse,pl_massj,pl_msinij,pl_msinie,pl_rade,pl_radj,pl_trueobliq,"
                        "st_mass,st_rad,sy_dist,ra,dec,sy_vmag,st_spectype,st_teff,st_lum,st_rotp+"
                        "from+pscomppars+order+by+pl_name+asc"
                        "&format=json";

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L); // Generous timeout for large dataset
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            std::cerr << "TAP Query failed via curl: " << curl_easy_strerror(res) << std::endl;
            return 0;
        }
        planets_array = json::parse(readBuffer);
    }

    try
    {
        if (!planets_array.is_array())
        {
            std::cerr << "Unexpected JSON structural response format." << std::endl << std::endl << readBuffer << std::endl;
            return 0;
        }

        if (do_download)
        {
            std::fstream fs(exocache, std::ios::out);
            if (fs)
            {
                fs << planets_array.dump(4);
                fs.close();
            }
        }

        for (const auto& row : planets_array)
        {
            // Guard against overflowing the global registry tracking array
            if (ncelobjs >= MAX_CELOBJS)
            {
                std::cerr << "Warning: Maximum sequential object allocation threshold reached (" << MAX_CELOBJS << ")." << std::endl;
                break;
            }

            // Ensure baseline primary keys exist
            if (!row.contains("pl_name") || row["pl_name"].is_null() ||
                !row.contains("hostname") || row["hostname"].is_null())
            {
                continue;
            }

            std::string pl_name = row["pl_name"].get<std::string>();
            std::string hostname = row["hostname"].get<std::string>();

            std::string hd_name = "", hip_name = "";

            try { hd_name  = row.contains("hd_name" ) ? row["hd_name" ].get<std::string>() : ""; } catch (...) { ; }
            try { hip_name = row.contains("hip_name") ? row["hip_name"].get<std::string>() : ""; } catch (...) { ; }

            // 1. Resolve host star context: check if it already exists in global array
            Star* host_star = nullptr;
            if (!host_star && hd_name.size() > 2)
            {
                int HD = atoi(&(hd_name.c_str()[2]));
                if (hdcache[HD]) host_star = hdcache[HD];
            }
            if (!host_star && hip_name.size() > 3)
            {
                int HIP = atoi(&(hip_name.c_str()[3]));
                if (hipcache[HIP]) host_star = hipcache[HIP];
            }
            if (!host_star)
            {
                if (worth_searching(hostname))
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
                if (ncelobjs >= MAX_CELOBJS) continue;

                host_star = new Star();
                host_star->type = star;
                snprintf(host_star->name, sizeof(host_star->name), "%s", hostname.c_str());

                double sy_dist = 0, ra = 0, dec = 0;
                if (row.contains("sy_dist") && !row["sy_dist"].is_null()) sy_dist = row["sy_dist"].get<double>() * parsec;
                if (row.contains("ra" ) && !row["ra" ].is_null()) ra  = row["ra" ].get<double>() * fiftyseventh;
                if (row.contains("dec") && !row["dec"].is_null()) dec = row["dec"].get<double>() * fiftyseventh;

                if (sy_dist && (ra || dec))
                {
                    host_star->distance = sy_dist;
                    host_star->right_ascension = ra;
                    host_star->declination = dec;
                    host_star->update_location(simnow);
                    host_star->distance_known = true;
                }
                else
                {
                    std::cerr << "WARNING: Missing ra/dec or distance for " << hostname << std::endl;
                    delete host_star;
                    continue;
                }

                if (row.contains("st_lum") && !row["st_lum"].is_null())
                {
                    double lum = pow(10, row["st_lum"].get<double>());
                    if (lum) host_star->absolute_magnitude = 4.85 - log(lum) / log(magnbase);
                }
                else host_star->absolute_magnitude = 10;

                if (row.contains("st_teff") && !row["st_teff"].is_null())
                {
                    host_star->estimate_BV(row["st_teff"].get<double>());
                }
                if (row.contains("sy_vmag") && !row["sy_vmag"].is_null())
                {
                    host_star->apparent_magnitude = row["sy_vmag"].get<double>();
                }
                else host_star->apparent_magnitude = 10;

                if (isinf(host_star->absolute_magnitude))
                {
                    double intrinsic_brightness = pow(magnbase, -host_star->apparent_magnitude) * pow(fmax(AU, host_star->distance) / parsec / 10, 2);
                    host_star->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
                }

                append_cel(host_star);
            }

            if (row.contains("st_mass") && !row["st_mass"].is_null())
            {
                host_star->mass = row["st_mass"].get<double>() * solar_mass;
            }
            if (row.contains("st_rad") && !row["st_rad"].is_null())
            {
                host_star->volumetric_mean_radius = row["st_rad"].get<double>() * solar_radius;
            }
            if (row.contains("st_spectype") && !row["st_spectype"].is_null())
            {
                strcpy(host_star->spectral_type, row["st_spectype"].get<std::string>().c_str());
            }
            if (row.contains("st_rotp") && !row["st_rotp"].is_null())
            {
                host_star->sidereal_rotational_period = row["st_rotp"].get<double>() * oneday;
            }

            // 2. Instantiate and fill target Planet properties
            Planet* new_planet = new Planet();
            snprintf(new_planet->name, sizeof(new_planet->name), "%s", pl_name.c_str());
            new_planet->cenobj = host_star;

            double inclination = 0;
            if (row.contains("pl_orbincl") && !row["pl_orbincl"].is_null())
            {
                inclination = row["pl_orbincl"].get<double>() * fiftyseventh;
            }

            if (row.contains("pl_massj") && !row["pl_massj"].is_null())
            {
                new_planet->mass = row["pl_massj"].get<double>() * jupiter_mass;
            }
            else if (row.contains("pl_bmasse") && !row["pl_bmasse"].is_null())
            {
                new_planet->mass = row["pl_bmasse"].get<double>() * earth_mass;
            }
            else if (row.contains("pl_msinij") && !row["pl_msinij"].is_null())
            {
                new_planet->mass = row["pl_msinij"].get<double>() * jupiter_mass / (inclination ? sin(inclination) : 1);
            }
            else if (row.contains("pl_msinie") && !row["pl_msinie"].is_null())
            {
                new_planet->mass = row["pl_msinie"].get<double>() * earth_mass / (inclination ? sin(inclination) : 1);
            }

            if (row.contains("pl_radj") && !row["pl_radj"].is_null())
            {
                new_planet->volumetric_mean_radius = row["pl_radj"].get<double>() * jupiter_radius;
            }
            else if (row.contains("pl_rade") && !row["pl_rade"].is_null())
            {
                new_planet->volumetric_mean_radius = row["pl_rade"].get<double>() * earth_radius;
            }

            if (row.contains("pl_trueobliq") && !row["pl_trueobliq"].is_null())
            {
                new_planet->obliquity = row["pl_trueobliq"].get<double>() * fiftyseventh;
            }

            // 3. Allocate and map the planetary Orbit data structure
            Orbit* orb = new Orbit();
            orb->center = host_star;
            orb->center_name = host_star->name;

            if (row.contains("pl_orbtper") && !row["pl_orbtper"].is_null())
            {
                double ep = row["pl_orbtper"].get<double>();
                orb->epoch = ep;
                orb->mean_anomaly = 0;
            }
            if (row.contains("pl_orbper") && !row["pl_orbper"].is_null())
            {
                orb->period = row["pl_orbper"].get<double>() * oneday;
            }
            if (row.contains("pl_orbsmax") && !row["pl_orbsmax"].is_null())
            {
                orb->semimajor_axis = row["pl_orbsmax"].get<double>() * AU;
            }
            if (row.contains("pl_orbeccen") && !row["pl_orbeccen"].is_null())
            {
                orb->eccentricity = row["pl_orbeccen"].get<double>();
            }
            if (row.contains("pl_orblper") && !row["pl_orblper"].is_null())
            {
                orb->arg_periapsis = row["pl_orblper"].get<double>() * fiftyseventh;
            }
            orb->inclination = inclination;

            new_planet->orbit = orb;
            new_planet->update_location(simnow);

            // 4. Run automated estimation/classification fallbacks provided in class declarations
            new_planet->classify();
            if (new_planet->mass > 0 && new_planet->volumetric_mean_radius == 0)
            {
                new_planet->estimate_radius();
            }
            new_planet->estimate_albedo_and_absmagn();
            new_planet->estimate_rotation();

            // Append planet object to global array
            append_cel(new_planet);
            result++;
            host_star->has_planets++;
            if (new_planet->is_in_con_HZ()) host_star->has_hz_planets++;

            if (planet_celids.find(host_star->seqno) == planet_celids.end())
                planet_celids[host_star->seqno] = std::vector<int>();
            planet_celids[host_star->seqno].push_back(new_planet->seqno);
        }

        apply_exoplanet_names(planet_celids);
    }
    catch (const json::parse_error& e)
    {
        std::cerr << "JSON Parsing Exception encountered: " << e.what() << std::endl;
        return 0;
    }

    return result;
}