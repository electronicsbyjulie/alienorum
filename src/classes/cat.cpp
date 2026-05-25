
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <ctime>
#include <map>
#include <string.h>
#include "cat.h"

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
    "USNO", "SAO",
    "BSC", "BrightStarCatalog", "BrightStarCatalogue",
    "CCDM",
    "SB9",
    "2MASS",
    "REGALADE",
    "GALEX",
    "astorb",
    "comets"
    // TODO: Add hundreds more...
};

bool have_Gliese = false, have_BSC = false, have_HIP = false, have_CCDM = false, have_SB9 = false,
    have_astorb = false;

std::vector<std::string> consline_a, consline_b;
std::vector<int> considx, lnpercons;
std::vector<Cartesian2D> conscen;
int nconsln = 0;
int *consaidx, *consbidx;
Star **hdcache, **hipcache;
std::map<int,std::map<char,Star* > > hipcomps;

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
            std::string entry_name = entry.path().filename();
            if (fs::is_directory(entry.path())
                &&
                std::find(known_catalog_names.begin(), known_catalog_names.end(), entry_name) != known_catalog_names.end()
                )
            {
                results.push_back(path + "/" + entry_name);
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
    std::string path = "catalogs/urls.dat";
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp)
    {
        std::cerr << "File not found " << path << std::endl;
        throw 0xbadf12e;
    }

    char buffer[1024], *catname = nullptr, *url = nullptr;
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
        for (j++; buffer[j] && buffer[j] <= ' '; j++);
        url = &buffer[j];
        if (!*url) continue;
        for (l=j; buffer[l] && buffer[l] > ' '; l++);
        buffer[l] = 0;

        // If the destination folder exists, assume we already have the catalog.
        std::string destdir = (std::string)"catalogs/" + (std::string)catname;
        fs::path p = destdir.c_str();
        if (!fs::exists(p))
        {
            // Create the dest folder.
            fs::create_directories(destdir);

            // Download the gzipped tarball.
            std::string destfname = destdir + "/download.tar.gz";
            p = destfname.c_str();
            if (!fs::exists(p))
            {
                // TODO: Add compatibility for Windows and Mac.
                if (frist)
                {
                    loading_msg = "Downloading catalogs...";
                    std::cout << loading_msg << std::endl;
                }
                std::string cmd = (std::string)"wget -O " + destfname + (std::string)" " + (std::string)url;
                std::cout << cmd << std::endl;
                std::system(cmd.c_str());
                frist = false;
            }

            // Extract the tarball.
            std::string cmd = (std::string)"tar -xvzf " + destfname + (std::string)" -C " + destdir;
            std::cout << cmd << std::endl;
            std::system(cmd.c_str());

            // Delete the tarball.
            fs::remove(destfname);

            // Any .gz files in the destination folder, unzip them.
            for (const auto& entry : fs::directory_iterator(destdir))
            {
                std::string entry_name = entry.path().filename();
                i = entry_name.size();
                j = i - 3;
                if (!strcmp(".gz", &entry_name.c_str()[j]))
                {
                    cmd = (std::string)"gunzip " + destdir + (std::string)"/" + entry_name;
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
    std::string path = "catalogs/Gliese/catalog.dat";
    char buffer[300];
    char field[32];
    int num_read = 0;
    int offset, j;
    double deg, mnt, sec, pm, pmtheta, absmagn;
    std::string build_name;
    Star *s, *A = nullptr;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

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
            build_name = "Gliese ";
        else if (field[0] == 'G' && field[1] == 'J')
            build_name = "GJ ";
        else if (field[0] == 'W' && field[1] == 'o')
            build_name = "Woolley ";
        else if (field[0] == 'N' && field[1] == 'N')
            build_name = "NN ";
        else build_name = trim(field);

        j = atoi(&field[2]);
        if (j)
        {
            build_name += std::to_string(j);
            if (field[6] == '.')
                build_name += std::string(&field[6]);
        }

        //   9- 10  A2     ---     Comp     Components (A,B,C,... )
        read_field_onebased(buffer, 9, 10, field);
        std::string comp = trim(field);
        if (comp.size())
        {
            build_name += (std::string)" " + comp;
            s->component = field[0];

            if (A && s->component > 'A' && !s->orbit)
            {
                s->orbit = new Orbit();
                s->orbit->center = A;
            }
        }
        else A = nullptr;

        strcpy(s->name, trim(build_name.c_str()).c_str());
        strcpy(s->Gliese, trim(build_name.c_str()).c_str());

        //  13- 14  I2     h       RAh      ? Right Ascension B1950 (hours)
        read_field_onebased(buffer, 13, 14, field);
        deg = atof(field) * 15;

        //  16- 17  I2     min     RAm      ? Right Ascension B1950 (minutes)
        read_field_onebased(buffer, 16, 17, field);
        mnt = atof(field) * 15;

        //  19- 20  I2     s       RAs      ? Right Ascension B1950 (seconds)
        read_field_onebased(buffer, 19, 20, field);
        sec = atof(field) * 15;

        s->right_ascension = (deg + mnt/60 + sec/3600) * fiftyseventh;

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

        s->declination = (deg + mnt/60 + sec/3600) * fiftyseventh * sgndecl;
        s->epoch = 2433282.42345905;

        // TODO: Keep the epoch but translate the coordinates to the J2000 system.
        // Have to apply precession of the equinoxes; this is the major component.
        // Also take into account nutation, at least the two largest terms.
        // See: https://en.wikipedia.org/wiki/Nutation#Earth

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
        absmagn = atof(field);
        s->absolute_magnitude = absmagn;
        s->distance = CelestialObject::distance_from_magnitudes(s->apparent_magnitude, absmagn);
        if (absmagn && s->apparent_magnitude)
        {
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
            }
            else
            {
                delete s;
                continue;
            }
        }

        // 147-152  I6     ---     HD       [15/352860]? designation
        read_field_onebased(buffer, 147, 152, field);
        s->HD = atoi(field);

        s->update_location(J2000_TIME_T);

        if (!num_read)
        {
            Rotation rot = align_points_3d(solar_north, ecliptic_north, center);
            s->inclination = rot.a;
            s->equinox = find_angle_along_vector(rot.v, zaxis, center, yaxis);
            if (s->equinox < 0) s->equinox += (M_PI*2);

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

        cels[offset+num_read] = s;
        if (s->component == 'A') A = s;

        num_read++;
        if ((offset+num_read) >= (max-1)) break;

        if (!(num_read % 123)) loading_msg = std::string("Loaded ") + std::to_string(num_read) + std::string(" objects from Gliese's Third Catalogue of Nearby Stars...");
    }

    fclose(fp);
    return num_read;
}

int CatalogReader::read_BrightStars_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs/BSC/catalog";
    char buffer[65536];
    char field[32];
    int num_read = 0;
    int offset, HD, j;
    double deg, mnt, sec;
    bool HDfound;
    double f;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    hdcache = new Star*[MAX_HD+1];
    memset(hdcache, 0, sizeof(Star*)*(MAX_HD+1));
    FILE* fp = fopen(path.c_str(), "rb");

    while (fgets(buffer, 65520, fp))
    {
        Star* s;

        read_field_onebased(buffer, 26, 31, field);
        HD = atoi(field);

        HDfound = false;
        if (HD)
        {
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
        }

        if (HD) hdcache[HD] = s;

        //    Bytes Format  Units   Label    Explanations
        // --------------------------------------------------------------------------------
        //    1-  4  I4     ---     HR       [1/9110]+ Harvard Revised Number
        read_field_onebased(buffer, 1, 4, field);
        s->HR = atoi(field);

        //    5- 14  A10    ---     Name     Name, generally Bayer and/or Flamsteed name
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
        int BayerXtraNum = atoi(&field[3]);
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

        if (!s->component && buffer[49] > ' ' && strcmp(s->name, "41The1Ori")) s->component = buffer[49];

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
        if (!s->right_ascension && !s->declination) continue;
        s->epoch = J2000;

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
        // Assumed 90 degree inclination for all extrasolar systems unles sinclination known.
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

        if (!HDfound)
        {
            cels[offset+num_read] = s;
            num_read++;
            if ((offset+num_read) >= (max-1)) break;
        }

        if (!(num_read % 123)) loading_msg = std::string("Loaded ") + std::to_string(num_read) + std::string(" objects from Bright Star Catalogue...");
    }
    fclose(fp);
    return num_read;
}

int CatalogReader::read_Hipparcos_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs/Hipparcos/hip_main.dat";
    char buffer[1024];
    char field[32];
    char Bonn[32], Cordoba[32], Cape[32];
    int num_read = 0;
    int offset, HD, HIP, j, cursor;
    double deg, mnt, sec, RA, Decl, f, f1;
    Star* s;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    hipcache = new Star*[MAX_HIP+1];
    memset(hipcache, 0, sizeof(Star*)*(MAX_HIP+1));

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

        if (HD && hdcache[HD]) s = (Star*)hdcache[HD];
        else if (HIP && hipcache[HIP]) s = (Star*)hipcache[HIP];
        if (!s)
        {
            // There are only a handful with no V magnitude; omit them.
            read_field_onebased(buffer, 42, 46, field);
            if (!trim(field).size()) continue;
            double appmag = atof(field);

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
            s->right_ascension = RA;
            s->declination = Decl;
            s->epoch = J2000 + (1991.25 - 2000);
        }
        else
        {
            std::cout << "ERROR: HIP" << HIP << " has no RA/Decl" << std::endl;
            throw 0xbadc0de;
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
            cursor = offset;
            cels[offset++] = s;
            loading_msg = std::string("Loading Hipparcos Catalog... Added HIP") + std::to_string(s->HIP);
        }
        else
        {
            loading_msg = std::string("Loading Hipparcos Catalog... Updated HIP") + std::to_string(s->HIP);
        }

        num_read++;
        if (num_read >= max-4) return num_read;

        if (!(num_read % 123)) loading_msg = std::string("Loaded ") + std::to_string(num_read) + std::string(" objects from The Hipparcos Catalogue...");
    }

    loading_msg = "Building Hipparcos-CCDM Cross Reference...";
    path = "catalogs/Hipparcos/h_dm_com.dat";
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
            Star* A = hipcache[HIP];
            s = new Star();
            s->make_companion_of(A, buffer[40]);
            s->epoch = J2000 + (1991.25 - 2000);
        }

        //  38- 39  I2     ---     seq      Sequential component number             (DCM6)
        read_field_onebased(buffer, 38, 39, field);
        s->ccdm_compseq = atoi(field);

        //      41  A1     ---     comp_id  Component identifier                     (DC7)
        read_field_onebased(buffer, 41, 41, field);
        s->component = field[0];

        hipcomps[HIP][s->component] = s;

        if (s != hipcache[HIP])
        {
            //  50- 55  F6.3   mag     Hp       Magnitude of component                   (DC9)
            read_field_onebased(buffer, 50, 55, field);
            s->apparent_magnitude = atof(field);
            double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
            s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;

            cels[offset++] = s;
            num_read++;
            if (num_read >= max-4) return num_read;
        }
    }

    loading_msg = "Loading Hipparcos Binary Star Orbits...";
    path = "catalogs/Hipparcos/hip_dm_o.dat";
    fp = fopen(path.c_str(), "rb");
    while (fgets(buffer, 1020, fp))
    {
        //   1-  6  I6    ---      HIP      Identifier (HIP)                         (D01)
        read_field_onebased(buffer, 1, 6, field);
        HIP = atoi(field);

        Star* A = hipcache[HIP];
        if (!A) continue;
        if (A->ccdm_compseq)
        {
            // If this error never shows up, delete this if block.
            std::cerr << "HIP" << HIP
                << " is present in both CCDM and hip_dm_o;"
                << " adjust code accordingly."
                << std::endl;
            throw 0xbadc0de;
        }

        A->component = 'A';
        hipcomps[HIP]['A'] = A;

        s = nullptr;
        for (j=0; j<offset; j++)
        {
            if (cels[j]->typeclass() != class_star) continue;
            if (!cels[j]->orbit) continue;
            if (cels[j]->orbit->center == A)
            {
                s = (Star*)cels[j];
                break;
            }
        }

        if (!s) s = new Star();
        s->make_companion_of(A, 'B');
        s->epoch = J2000 + (1991.25 - 2000);

        for (s->component = 'B'; s->component < 'Z'; s->component++)
        {
            auto it = hipcomps[HIP].find(s->component);
            if (it == hipcomps[HIP].end()) break;
        }

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
        A->inclination = atof(field) * fiftyseventh;
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
        s->absolute_magnitude = A->absolute_magnitude + 1;      // garbage number

        cels[offset++] = s;
        num_read++;
        if (num_read >= max-4) return num_read;
    }

    fclose(fp);
    return num_read;
}

int CatalogReader::read_CCDM_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs/CCDM/ccdm.dat";
    char buffer[1024];
    char field[32];
    int num_read = 0;
    int offset, HD, HIP, i;
    Star *s, *A = nullptr;
    std::string CCDM;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    bool already[max];
    memset(already, 0, max*sizeof(bool));

    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;

    while (fgets(buffer, 1020, fp))
    {
        //   2- 11  A10    ---     CCDM     (Catalogue of the Components of the Double and Multiple stars) identifier (1)
        read_field_onebased(buffer, 2, 11, field);
        CCDM = trim(field);
        const char* cCCDM = CCDM.c_str();

        //  99-104  A6     ---     HD       HD identifier
        read_field_onebased(buffer, 99, 104, field);
        HD = atoi(field);

        // 127-132  I6     ---     HIC      ? Hipparcos Input Catalogue (Turon et al., Cat. <I/196>) identifier (also HIP <I/239>)
        read_field_onebased(buffer, 127, 132, field);
        HIP = atoi(field);
        if (!HIP) continue;             // TODO: Revisit this.

        char refcomp = buffer[11], conccomp = buffer[12];
        if (refcomp == ' ') refcomp = 'A';

        bool found = false;
        auto it = hipcomps.find(HIP);
        if (it != hipcomps.end())
        {
            auto it1 = hipcomps[HIP].find(refcomp);
            if (it1 != hipcomps[HIP].end())
            {
                auto it2 = hipcomps[HIP].find(conccomp);
                if (it2 != hipcomps[HIP].end())
                {
                    A = hipcomps[HIP][refcomp];
                    s = hipcomps[HIP][conccomp];
                    found = true;
                }
                else
                {
                    s = new Star();
                    s->make_companion_of(A, conccomp);
                    s->epoch = J2000 + (1991.25 - 2000);
                }
            }
            else
            {
                continue;
            }
        }
        else continue;

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

            strcpy(A->name, lop_component(A->name).c_str());
            strcpy(s->name, (std::string(A->name) + std::string(" B")).c_str() );
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

        if (!A->known_poles)
        {
            // The inclination is unknown, but let's assume zero degrees
            s->inclination = A->inclination = 0;
            A->location.equatorial_plane = A->location.local_system_plane =
                align_points_3d(cels[0]->location.system_center, Point(0,light_year*1e9,0), A->location.system_center);
        }
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

        // TODO: For systems where both members are not already loaded,
        // can load additional members.

        //  57- 58  I2     ---     Obs      ? for component A: number of components; other component: number of measurements
        //  60- 63  F4.1   mag     Vmag     ? magnitude
        //  65- 66  A2     ---     Sp       Spectral type
        //  68- 72  I5    mas/yr   pmRA     ? annual proper motion in 0"001
        //  73- 77  I5    mas/yr   pmDE     ? annual proper motion in 0"001

        s->gotta_be_named_something();
        num_read++;

        loading_msg = std::string("Loaded ") + std::to_string(num_read) + std::string(" objects from Catalogue of the Components of Double and Multiple Stars...");
    }
    return num_read;
}

int CatalogReader::read_SB9_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs/SB9/main.dat";
    char buffer[1024];
    char field[32], Bonn, Bonn_sign, cen[5], comp[5];
    int num_read = 0;
    int HD, HIP, SB9, Bonn_decl, Bonn_seq, i, j, l, n, found, offset;
    Star *s, *A, *B;
    double f;

    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;

    for (offset=0; cels[offset]; offset++);
    if (offset >= (max-1)) return num_read;

    while (fgets(buffer, 1020, fp))
    {
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
        if (HIP && hipcache[HIP])
        {
            A = hipcache[HIP];
        }
        else if (HD && hdcache[HD])
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

        //   1-  4  I4    ---     Seq     System Number (SB8 number when Seq<=1469)
        read_field_onebased(buffer, 1, 4, field);
        SB9 = atoi(field);

        found = -1;
        B = nullptr;
        auto it_hip = hipcomps.find(HIP);
        if (it_hip != hipcomps.end())
        {
            auto it_b = hipcomps[HIP].find(comp[0]);
            if (it_b != hipcomps[HIP].end())
            {
                B = hipcomps[HIP][comp[0]];
            }
        }
        else
        {
            if ((A->BayerGrkno == 7 && !strcmp(A->constellation, "Ori")))
            {
                found = -2;
            }
            if ((A->BayerGrkno != 7 || strcmp(A->constellation, "Ori"))
                && (strlen(comp) == 1 && comp[0] > 'A' && comp[0] <= 'K')
                )
            {
                double foundmag[10] = {99,99,99,99,99,99,99,99,99,99};
                int foundidx[10] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
                for (i=0; cels[i]; i++)
                {
                    if (!cels[i]->orbit) continue;
                    if (cels[i]->type != star) continue;
                    s = (Star*)cels[i];

                    if (s->orbit->center == A)
                    {
                        for (j=0; j<10; j++) if (s->apparent_magnitude < foundmag[j])
                        {
                            for (l=9; l>j; l--)
                            {
                                foundmag[l] = foundmag[l-1];
                                foundidx[l] = foundidx[l-1];
                            }
                            foundmag[j] = s->apparent_magnitude;
                            foundidx[j] = i;
                            break;      // j
                        }
                    }
                }

                found = foundidx[comp[0]-'B'];
            }

            if (found < 0)
            {
                B = new Star();
                B->type = star;
                if (A->HD == 20766)
                {
                    std::cerr << "BAD! 1061" << std::endl;
                    throw 0xbadc0de;
                }
                B->make_companion_of(A, comp[0]);
                B->epoch = A->epoch;
                cels[offset++] = B;
                cels[offset] = 0;
            }
            else
            {
                B = (Star*)cels[found];
            }
        }
        if (!B)
        {
            B = new Star();
            B->make_companion_of(A, comp[0]);
        }

        B->SB9 = SB9;

        //  52- 57  F6.3  mag     mag2    ? Magnitude of component 2
        read_field_onebased(buffer, 52, 57, field);
        if (!trim(field).size()) B->apparent_magnitude = A->apparent_magnitude + 1;             // a complete guess!!!
        else B->apparent_magnitude = atof(field);
        double intrinsic_brightness = pow(magnbase, -B->apparent_magnitude) * pow(fmax(AU, B->distance) / parsec / 10, 2);
        B->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;

        //  93-102  A10   ---     Sp2     MK Spectral type component 2
        read_field_onebased(buffer, 93, 102, field);
        strcpy(B->spectral_type, trim(field).c_str());
        if (found < 0) B->estimate_BV();

        B->gotta_be_named_something();
        B->estimate_radius();
        B->estimate_mass();
    }
    fclose(fp);

    path = "catalogs/SB9/orbits.dat";
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

        if (!(num_read % 123)) loading_msg = std::string("Loaded ") + std::to_string(num_read) + std::string(" objects from Stellar Binaries Catalogue...");
    }
    fclose(fp);

    return num_read;
}

int CatalogReader::read_astorb_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs/astorb/astorb.dat";
    char buffer[1024];
    char field[32];
    int asno, num_read = 0, offset, _year, _month, _day;
    std::string name;
    double absmagn, sma;

    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    while (fgets(buffer, 1020, fp))
    {
        //   8- 25  A18   ---     Name      Name or preliminary designation.
        read_field_onebased(buffer, 8, 25, field);
        name = trim(field);

        //  43- 47  F5.2  mag     H         Absolute magnitude H parameter (1)
        read_field_onebased(buffer, 43, 47, field);
        absmagn = atof(field);

        //   1-  6  I6    ---     Planet    [1,]?+ Asteroid number (blank if unnumbered)
        read_field_onebased(buffer, 1, 6, field);

        if (!(asno = atoi(field))) continue;
        if ((asno > 4 || absmagn >= 8)
            && strcmp(name.c_str(), "Pluto")
            && strcmp(name.c_str(), "Enya")
            )
            continue;

        Planet *p = new Planet();
        p->type = rocky;
        p->asteroid_no = asno;
        p->location = cels[0]->location;
        p->cenobj = cels[0];
        p->orbit = new Orbit();
        p->orbit->center = cels[0];
        strcpy(p->name, name.c_str());
        p->absolute_magnitude = absmagn;

        //  55- 58  F4.2  mag     B-V       ? Color index (see E.F.Tedesco, pp.1090-1138)
        read_field_onebased(buffer, 55, 58, field);
        p->BV_color = atof(field);

        //  60- 64  F5.1  km      Diam      ? IRAS diameter (see E.F.Tedesco, pp.1151-1161; catalog <II/190>)
        read_field_onebased(buffer, 60, 64, field);
        p->volumetric_mean_radius = atof(field) * 500;
        if (!p->volumetric_mean_radius && !strcmp(name.c_str(), "Pluto")) p->volumetric_mean_radius = 1188300;
        p->mass = p->volumetric_mean_radius * p->volumetric_mean_radius * p->volumetric_mean_radius * 4.0/3 * M_PI * 1853;  // Pluto density.

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

        // 148-157  F10.6 deg     i         Inclination (3)
        read_field_onebased(buffer, 148, 157, field);
        p->orbit->inclination = atof(field) * fiftyseventh;

        // 159-168  F10.8 ---     e         Eccentricity (3)
        read_field_onebased(buffer, 159, 168, field);
        p->orbit->eccentricity = atof(field);

        // 169-181  F13.8 AU      a         ? Semimajor axis (3)
        read_field_onebased(buffer, 169, 181, field);
        sma = atof(field);
        p->orbit->semimajor_axis = sma * AU;
        p->orbit->period = sqrt(sma*sma*sma) * oneyear;

        if (!strcmp(name.c_str(), "Pluto"))
        {
            _day = 0;
        }

        num_read++;
        cels[offset++] = p;
        if (offset >= (max-1))
        {
            fclose(fp);
            return num_read;
        }
    }

    fclose(fp);
    return num_read;
}

int CatalogReader::read_starname_dat(CelestialObject **cels)
{
    std::string path = "starname.dat";
    char buffer[1024];
    char field[32];
    int num_read = 0;
    int HD, HIP, i;
    std::string Gliese;

    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;

    while (fgets(buffer, 1020, fp))
    {
        read_field_onebased(buffer, 26, 32, field);
        HD = atoi(field);

        read_field_onebased(buffer, 34, 39, field);
        HIP = atoi(field);

        read_field_onebased(buffer, 41, 48, field);
        i = atoi(field);
        Gliese = trim(field);
        if (i >= 9000) Gliese = std::string("Woolley ") + Gliese;
        else if (i >= 3000) Gliese = std::string("NN ") + Gliese;
        else if (i >= 1000) Gliese = std::string("GJ ") + Gliese;
        else Gliese = std::string("Gliese ") + Gliese;

        read_field_onebased(buffer, 1, 25, field);

        for (i=0; cels[i]; i++)
        {
            if (cels[i]->type != star) continue;
            Star* s = (Star*)cels[i];
            if ((HD && s->HD == HD) || (HIP && s->HIP == HIP) || (Gliese.size() && !strcmp(s->Gliese, Gliese.c_str())))
            {
                strcpy(s->name, trim(field).c_str());
                num_read++;
                break;
            }
        }
    }

    return num_read;
}

int CatalogReader::read_star_orbits_dat(CelestialObject **cels)
{
    std::string path = "catalogs/star_orbits.dat";
    char buffer[1024];
    char field[32];
    int i, j, offset, num_read = 0;
    double f;

    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;

    Star *A, *s;

    while (fgets(buffer, 1020, fp))
    {
        if (*buffer == '#') continue;
        if (!trim(buffer).size()) continue;

        read_field_onebased(buffer, 1, 23, field);
        std::string cenname = trim(field);
        const char* censtr = cenname.c_str();
        A = nullptr;
        for (i=0; cels[i]; i++) if (!strcmp(cels[i]->name, censtr)
            || (cels[i]->typeclass() == class_star && censtr[0] == 'H' && censtr[1] == 'D' && atoi(&censtr[2]) == ((Star*)cels[i])->HD)
            )
        {
            A = (Star*)cels[i];
            break;
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

        if (inclination || ascending_node)
        {
            A->location.local_system_plane = system_plane_from_incl_and_node(inclination, ascending_node,
                A->location.system_center - cels[0]->location.system_center);
            A->location.orbital_plane = A->location.equatorial_plane = A->location.local_system_plane;
            A->inclination = inclination;
            A->equinox = ascending_node;
            A->known_poles = true;
        }

        read_field_onebased(buffer, 25, 47, field);
        std::string bdyname = trim(field);
        const char* bdystr = bdyname.c_str();

        if (bdystr[0] == '(') continue;

        if (!A)
        {
            std::cerr << "FAILED to orbit " << bdyname << " around " << cenname << std::endl;
            continue;
        }

        s = nullptr;
        for (i=0; cels[i]; i++) if (!strcmp(cels[i]->name, bdystr)
            || (cels[i]->typeclass() == class_star && bdystr[0] == 'H' && bdystr[1] == 'D' && atoi(&bdystr[2]) == ((Star*)cels[i])->HD)
            )
        {
            s = (Star*)cels[i];
            break;
        }

        if (!s)
        {
            std::cerr << "FAILED to orbit " << bdyname << " around " << cenname << std::endl;
            continue;
        }

        if (A->HD == 20766)
        {
            std::cerr << "BAD! 1061" << std::endl;
            throw 0xbadc0de;
        }
        if (!s->orbit) s->orbit = new Orbit();
        s->orbit->center = A;

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
            s->location = A->location;
            s->inclination = inclination;
            s->equinox = ascending_node;
            A->known_poles = s->known_poles = true;
        }

        num_read++;
    }

    return num_read;
}

int CatalogReader::read_local_planets(CelestialObject **cels, int max)
{
    std::string path = "catalogs/planets.dat";
    char buffer[1024];
    char field[32];
    int i, j, offset, num_read = 0;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;

    while (fgets(buffer, 1020, fp))
    {
        if (*buffer == '#') continue;
        if (!trim(buffer).size()) continue;

        j = -1;
        read_field_onebased(buffer, 1, 25, field);
        std::string cenname = trim(field);
        for (i=0; i<offset; i++)
        {
            if (!strcmp(cels[i]->name, cenname.c_str()))
            {
                j = i;
                break;
            }
        }

        if (j < 0)
        {
            read_field_onebased(buffer, 26, 42, field);
            std::cerr << "Warning: center of orbit unknown for " << field << std::endl;
            continue;
        }

        Orbit* o = new Orbit();
        o->center = cels[j];
        Planet* p;

        if (o->center->orbit && o->center->orbit->center)
        {
            p = (Planet*)new Moon();
        }
        else
        {
            p = new Planet();
        }
        p->orbit = o;
        read_field_onebased(buffer, 26, 42, field);
        strcpy(p->name, trim(field).c_str());

        read_field_onebased(buffer, 44, 58, field);
        o->semimajor_axis = atof(field);
        if (!o->semimajor_axis)
        {
            delete p;
            delete o;
            continue;
        }

        read_field_onebased(buffer, 60, 64, field);
        p->BV_color = atof(field);

        read_field_onebased(buffer, 66, 70, field);
        p->UB_color = atof(field);

        read_field_onebased(buffer, 72, 77, field);
        o->inclination = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 81, 87, field);
        o->ascending_node = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 91, 98, field);
        o->arg_periapsis = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 101, 110, field);
        o->mean_anomaly = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 112, 117, field);
        p->absolute_magnitude = atof(field);

        read_field_onebased(buffer, 119, 128, field);
        p->volumetric_mean_radius = atof(field);

        read_field_onebased(buffer, 132, 141, field);
        p->oblateness = atof(field);

        read_field_onebased(buffer, 143, 154, field);
        o->eccentricity = atof(field);

        read_field_onebased(buffer, 156, 173, field);
        o->period = atof(field);

        read_field_onebased(buffer, 175, 181, field);
        p->inclination = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 183, 191, field);
        p->equinox = atof(field) * fiftyseventh;
        p->known_poles = p->inclination && p->equinox;

        read_field_onebased(buffer, 193, 210, field);
        p->sidereal_rotational_period = atof(field);

        read_field_onebased(buffer, 212, 223, field);
        p->mass = atof(field);
        if (p->mass < 1.6 * earth_mass) p->type = rocky;        // https://doi.org/10.1051/0004-6361/202348690
        else if (p->mass < 2.5e+29) p->type = ice_giant;
        else p->type = gas_giant;

        read_field_onebased(buffer, 225, 231, field);
        p->surface_pressure = atof(field);

        read_field_onebased(buffer, 233, 243, field);
        p->epoch = J2000 + (atof(field) - 2000)*(oneyear/oneday);

        read_field_onebased(buffer, 245, 259, field);
        float f = atof(field);
        p->precession = f ? (M_PI * 2 / f) : 0;

        read_field_onebased(buffer, 261, 287, field);           // TODO: Laplace planes
        p->J2 = atof(field);

        read_field_onebased(buffer, 289, 303, field);
        f = atof(field);
        o->prec_node = f ? (M_PI * 2 / f) : 0;

        read_field_onebased(buffer, 305, 316, field);
        f = atof(field);
        o->proc_argperi = f ? (M_PI * 2 / f) : 0;
        p->distance_known = true;

        p->location = o->center->location;          // Copy the system center and local plane. The local position will auto-fill later.
        p->location.equatorial_plane.a = p->inclination;
        p->location.equatorial_plane.v = Point(std::sin(p->equinox), 0, -std::cos(p->equinox));

        cels[offset++] = p;
        num_read++;
    }

    return num_read;
}

void CatalogReader::read_field_onebased(char *buffer, int start, int end, char *out)
{
    start--;
    int len = end - start;
    int i;
    for (i=0; i<len; i++) if (!(out[i] = buffer[i+start])) break;
    out[i] = 0;
}
