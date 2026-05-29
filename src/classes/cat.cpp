
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
                    mtx.lock();
                    loading_msg = "Downloading catalogs...";
                    mtx.unlock();
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
    StarMulti *current_multi = nullptr;
    float current_multi_gjno = 0;

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
            if (fabs(current_multi_gjno-f) >= 0.05)
            {
                current_multi = nullptr;
                A = nullptr;
            }

            build_name += (std::string)" " + comp;
            s->multisys = current_multi;
            if (field[0] == '-') field[0] = 'D';            // GJ 1255 fix
            if (field[0] == 'A') A = s;
            s->set_component(field[0], A);
            current_multi = s->multisys;
            current_multi_gjno = f;

            // Special case for Proxima since Gliese et al couldn't be bothered to group it with Alp Cen AB.
            if ((fabs(f-559) < 0.05) && A && s->get_component() > 'A')
            {
                Star *C = (Star*) cels[find_object("GJ 551", true)];
                A->multisys->add_member(C, 'C');
            }

            // Special case for Zeta Reticuli
            if ((fabs(f-138) < 0.05) && A && s->get_component() > 'A')
            {
                Star *B = (Star*) cels[find_object("GJ 136", true)];
                A->multisys->add_member(B, 'B');
            }
        }

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

        num_read++;
        if ((offset+num_read) >= (max-1)) break;

        mtx.lock();
        if (!(num_read % 123)) loading_msg = std::string("Loaded ") + std::to_string(num_read) + std::string(" objects from Gliese's Third Catalogue of Nearby Stars...");
        mtx.unlock();
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
    int offset, HD, HR, j;
    double deg, mnt, sec;
    bool HDfound;
    double f;
    StarMulti *current_multi = nullptr;
    int current_multi_hrno = 0;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    hdcache = new Star*[MAX_HD+1];
    memset(hdcache, 0, sizeof(Star*)*(MAX_HD+1));
    FILE* fp = fopen(path.c_str(), "rb");

    Star *s, *A = nullptr;
    while (fgets(buffer, 65520, fp))
    {
        //   1-  4  I4     ---     HR       [1/9110]+ Harvard Revised Number = Bright Star Number
        read_field_onebased(buffer, 1, 4, field);
        HR = atoi(field);

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

        read_field_onebased(buffer, 1, 4, field);
        s->HR = HR;

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

        if (!s->get_component() && buffer[49] > ' ' && strcmp(s->name, "41The1Ori"))
        {
            if (HR != current_multi_hrno)
            {
                current_multi = nullptr;
                A = nullptr;
            }
            s->multisys = current_multi;
            s->set_component(buffer[49], A);
            current_multi = s->multisys;
            if (buffer[49] == 'A') A = s;
        }

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

        mtx.lock();
        if (!(num_read % 123)) loading_msg = std::string("Loaded ") + std::to_string(num_read) + std::string(" objects from Bright Star Catalogue...");
        mtx.unlock();
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
    Star *s, *A;

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
            A = hipcache[HIP];
            if (!A->multisys) A->set_component('A', A);
            s = A->multisys->get_member(buffer[40]);
            if (!s)
            {
                s = new Star();
                cels[offset++] = s;
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

        A->set_component('A', A);

        s = A->multisys->get_member('B');

        if (!s)
        {
            s = new Star();
            cels[offset++] = s;
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
        if (s->absolute_magnitude > 1e28) s->absolute_magnitude = A->absolute_magnitude + 1;      // garbage number

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
    std::string CCDM, CCDM_A;

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
            if (HIP && hipcache[HIP]) A = hipcache[HIP];
            else if (HD && hdcache[HD]) A = hdcache[HD];
        }
        else
        {
            if (HIP && hipcache[HIP]) s = hipcache[HIP];
            else if (HD && hdcache[HD]) s = hdcache[HD];
        }

        if (!A) continue;
        if (!A->multisys) A->set_component('A', A);
        A = A->multisys->get_member(refcomp);
        if (!A) continue;
        s = A->multisys->get_member(conccomp);

        if (!s)
        {
            s = new Star();
            s->epoch = J2000 + (1991.25 - 2000);

            cels[offset++] = s;
            cels[offset] = nullptr;
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
            cels[offset++] = B;
            cels[offset] = 0;
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

        mtx.lock();
        if (!(num_read % 123)) loading_msg = std::string("Loaded ") + std::to_string(num_read)
            + std::string(" objects from Stellar Binaries Catalogue...");
        mtx.unlock();
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
        /*    && asno != 55
            && asno != 89
            && asno != 105
            && asno != 116
            && asno != 490
            && asno != 742
            && asno != 896
            && asno != 1001
            && asno != 1006
            && asno != 1134
            && asno != 1143
            && asno != 1221
            && asno != 1388
            && asno != 1404
            && asno != 1421
            && asno != 1566
            && asno != 1604
            && asno != 1691
            && asno != 1693
            && asno != 1709
            && asno != 1741
            && asno != 1776
            && asno != 1789
            && asno != 1790
            && asno != 1791
            && asno != 1814
            && asno != 1815
            && asno != 1823
            && asno != 1862
            && asno != 1964
            && asno != 1991
            && asno != 2000
            && asno != 2001
            && asno != 2002
            && asno != 2060
            && asno != 2062
            && asno != 2069
            && asno != 2101
            && asno != 2161
            && asno != 2244
            && asno != 2247
            && asno != 2309
            && asno != 2322
            && asno != 2362
            && asno != 2476
            && asno != 2675
            && asno != 2688
            && asno != 2709
            && asno != 2769
            && asno != 2801
            && asno != 2807
            && asno != 2810
            && asno != 2830
            && asno != 2937
            && asno != 2985
            && asno != 2999
            && asno != 3130
            && asno != 3142
            && asno != 3153
            && asno != 3163
            && asno != 3313
            && asno != 3350
            && asno != 3351
            && asno != 3352
            && asno != 3353
            && asno != 3354
            && asno != 3355
            && asno != 3356
            && asno != 3366
            && asno != 3412
            && asno != 3524
            && asno != 3534
            && asno != 3600
            && asno != 3768
            && asno != 3838
            && asno != 3895
            && asno != 3905
            && asno != 3948
            && asno != 4062
            && asno != 4147
            && asno != 4169
            && asno != 4179
            && asno != 4180
            && asno != 4221
            && asno != 4330
            && asno != 4337
            && asno != 4444
            && asno != 4457
            && asno != 4500
            && asno != 4513
            && asno != 4628
            && asno != 4659
            && asno != 4716
            && asno != 4804
            && asno != 4987
            && asno != 5000
            && asno != 5020
            && asno != 5370
            && asno != 5471
            && asno != 5535
            && asno != 5668
            && asno != 5747
            && asno != 5773
            && asno != 5790
            && asno != 5803
            && asno != 5811
            && asno != 6006
            && asno != 6032
            && asno != 6123
            && asno != 6143
            && asno != 6186*/
            && asno != 6433
            /*&& asno != 6469
            && asno != 6470
            && asno != 6471
            && asno != 6486
            && asno != 6493
            && asno != 6701
            && asno != 6714
            && asno != 6826
            && asno != 6875
            && asno != 6914
            && asno != 6999
            && asno != 7000*/
            && asno != 50000
            && asno != 90377
            && asno != 90482
            && asno != 134340
            && asno != 136108
            && asno != 136199
            && asno != 136472
            && asno != 163693
            && asno != 541132
            )
            continue;

        if (asno == 5747) name = "Williamina";              // She invented the OBAFGKM system and you chauvanists can't honor her namesake in astorb????

        Planet *p = new Planet();
        p->type = rocky;
        p->asteroid_no = asno;
        p->location = cels[0]->location;
        p->cenobj = cels[0];
        p->orbit = new Orbit();
        p->orbit->center = cels[0];
        strcpy(p->name, (std::to_string(asno) + std::string(" ") + name).c_str());
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

int CatalogReader::read_exoplanets_catalog(CelestialObject **cels, int max)
{
    int offset, num_added = 0;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= (max-1)) return 0;

    std::string path = "catalogs/";
    std::string startswith = "PSCompPars_";
    std::string candidate = "";
    std::vector<std::string> results;
    try
    {
        for (const auto& entry : fs::directory_iterator(path))
        {
            std::string entry_name = entry.path().filename();
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
    FILE *fp = fopen(path.c_str(), "r");
    char buffer[2048], wasfirst = 0;
    int i=0;
    int col_plnm=-1, col_stnm=-1, col_hd=-1, col_orbper=-1, col_sma=-1, col_rade=-1, col_radj=-1,
        col_mass_e=-1, col_mass_j=-1, col_eccn=-1, col_incl=-1, col_periepo=-1, col_argperi=-1,
        col_oblt=-1, col_sptp=-1, col_srad=-1, col_smass=-1,
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
            Planet *p = nullptr;
            char *comma, *field = buffer;
            std::string planet_name = "", star_name = "", spectral_type = "";
            double p_incl, star_radius=0, star_mass=0, star_ra=0, star_decl=0, star_dist=0, star_vmag = 1e29;
            for (i=0; strlen(field); i++)
            {
                comma = strchr(field, ',');
                if (comma) *comma = 0;
                if (i == col_plnm)
                {
                    planet_name = field;
                    if (p) strcpy(p->name, planet_name.c_str());
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
                    else if (HIP && hipcache[HIP]) s = hipcache[HIP];
                    else if (!HD && !HIP)
                    {
                        // In case of multi-planet system, scan the last several objects for an EXACT name match.
                        for (j=0; j<10; j++)
                        {
                            CelestialObject *cel = cels[offset-2-j];
                            if (cel->typeclass() != class_star) continue;
                            if (!strcmp(cel->name, star_name.c_str()))
                            {
                                s = (Star*)cel;
                                break;
                            }
                        }

                        bool do_search = false;      // Full search is expensive. Only search if good chance of finding it (Gliese, named stars).

                        if (!strcmp(star_name.substr(0, 3).c_str(), "GJ ")) do_search = true;
                        else if (star_name.c_str()[0] >= 'A' && star_name.c_str()[0] <= 'Z'
                                && star_name.c_str()[1] >= 'a' && star_name.c_str()[1] <= 'z'
                                && star_name.c_str()[2] >= 'a' && star_name.c_str()[2] <= 'z'
                                )
                            do_search = true;
                        if (!strcmp(star_name.substr(0, 6).c_str(), "Kepler")) do_search = false;
                        if (!strcmp(star_name.substr(0, 5).c_str(), "CoRoT")) do_search = false;
                        if (!strcmp(star_name.substr(0, 5).c_str(), "Qatar")) do_search = false;
                        if (!strcmp(star_name.substr(0, 4).c_str(), "Gaia")) do_search = false;

                        if (!s && do_search)
                        {
                            j = find_object(star_name.c_str(), true);
                            if (j < 0)
                            {
                                std::cout << "Warning: failed to identify star " << star_name << std::endl;
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
                        strcpy(s->name, star_name.c_str());
                        s_is_new = true;
                    }

                    if (!p) p = new Planet();
                    if (planet_name.size()) strcpy(p->name, planet_name.c_str());
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
                        else if (i == col_incl) p_incl = atof(field) * fiftyseventh;
                        else if (i == col_periepo)
                        {
                            p->orbit->epoch = atof(field);
                            p->orbit->mean_anomaly = 0;
                        }
                        else if (i == col_argperi) p->orbit->arg_periapsis = atof(field) * fiftyseventh;
                        else if (i == col_oblt) p->inclination = atof(field) * fiftyseventh;
                        else if (i == col_sptp) spectral_type = field;
                        else if (i == col_srad) star_radius = atof(field) * solar_radius;
                        else if (i == col_smass) star_mass = atof(field) * solar_mass;
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
                if (!star_dist || (star_vmag > 1e28)) continue;
                if (s && (fabs(s->right_ascension - star_ra) > fiftyseventh)
                        || fabs(s->declination - star_decl) > fiftyseventh
                        || fabs(s->apparent_magnitude - star_vmag) > 0.5
                    )
                {
                    s = new Star();
                    s->type = star;
                    strcpy(s->name, star_name.c_str());
                    p->orbit->center = p->cenobj = s;
                    s_is_new = true;
                }

                s->right_ascension = star_ra;
                s->declination = star_decl;
                s->distance = star_dist;
                s->distance_known = true;
                s->mass = star_mass;
                s->volumetric_mean_radius = star_radius;
                s->apparent_magnitude = star_vmag;
                double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
                s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
                strcpy(s->spectral_type, spectral_type.c_str());

                if (!s->mass) s->estimate_mass();
                if (!s->volumetric_mean_radius) s->estimate_radius();

                s->is_really_truly_in_visible_box(cels[0]->location);

                if (s_is_new)
                {
                    cels[offset] = s;
                    offset++;
                    cels[offset] = nullptr;
                    if (offset >= max-1)
                    {
                        fclose(fp);
                        return num_added;
                    }
                }

                s->inclination = p_incl;
            }

            if (s && p && p->orbit->period)
            {
                ((Star*)s)->has_planets = true;
                if (p->mass < 1.6 * earth_mass) p->type = rocky;        // https://doi.org/10.1051/0004-6361/202348690
                else if (p->mass < 2.5e+29) p->type = ice_giant;
                else p->type = gas_giant;
                if (!p->volumetric_mean_radius) p->estimate_radius();
                double p_rad_e = fmax(0.01, p->volumetric_mean_radius / earth_radius);
                p->absolute_magnitude = fmax(-10, earth_absmag - log(p_rad_e*p_rad_e) / log(magnbase));
                if (!p->orbit->semimajor_axis) p->orbit->compute_semimajor_axis(p->mass);
                cels[offset] = p;
                offset++;
                cels[offset] = nullptr;
                num_added++;
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
        }

        wasfirst = buffer[0];
    }

    fclose(fp);
    return num_added;
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
                num_read++;

                if (s->multisys && s->multisys->get_member('A') == s)
                {
                    char c;
                    Star* companion;
                    for (c = 'B'; companion = s->multisys->get_member(c); c++)
                    {
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
        if (cels[j]->typeclass() == class_star) ((Star*)cels[j])->has_planets = true;
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
