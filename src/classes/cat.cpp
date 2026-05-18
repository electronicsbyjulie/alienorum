
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "cat.h"

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

bool have_Gliese = false, have_BSC = false, have_HIP = false, have_CCDM = false;

std::vector<std::string> consline_a, consline_b;
std::vector<int> considx, lnpercons;
std::vector<Cartesian2D> conscen;
int nconsln = 0;
int *consaidx, *consbidx;

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
                if (frist) std::cout << "Downloading catalogs..." << std::endl;
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

// Assumes no other catalogs have been loaded before Gliese,
// since Gliese contains the Sun.
int CatalogReader::read_Gliese_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs/Gliese/catalog.dat";
    char buffer[65536];
    char field[32];
    int num_read = 0;
    int offset, j;
    double deg, mnt, sec, pm, pmtheta, absmagn;
    std::string build_name;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= max) return 0;

    FILE* fp = fopen(path.c_str(), "rb");

    while (fgets(buffer, 65520, fp))
    {
        Star* s = new Star();
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
        if (comp.size()) build_name += (std::string)" " + comp;

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
        pm = atof(field) / 3600 * fiftyseventh / year;

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
        s->RI_magnitude = atof(field);

        // 109-114  F6.1   mas     plx      ? Resulting parallax
        read_field_onebased(buffer, 109, 114, field);
        s->parallax = atof(field) / 1000 / 3600 * fiftyseventh;

        // 122-126  F5.2   mag     Mv       Absolute visual magnitude
        read_field_onebased(buffer, 122, 126, field);
        absmagn = atof(field);
        s->absolute_magnitude = absmagn;
        s->distance = CelestialObject::distance_from_magnitudes(s->apparent_magnitude, absmagn);
        if (absmagn && s->apparent_magnitude) s->distance_known = true;

        // Sun is distance zero.
        if (!s->right_ascension && !s->declination) s->distance = 0;

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

            s->location.local_system_plane = ICRF_to_ecliptic;
            s->location.equatorial_plane = rot;
        }
        else
        {
            s->update_location(J2000_TIME_T);
            // Assumed 90 degree inclination for all extrasolar systems unles sinclination known.
            s->location.local_system_plane = align_points_3d(cels[0]->location.system_center, Point(0,0,light_year*1e9), s->location.system_center);
        }

        cels[offset+num_read] = s;
        num_read++;
        if ((offset+num_read) >= (max-1)) break;
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
    int offset, HDno, j;
    double deg, mnt, sec;
    bool HDfound;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= max) return 0;

    FILE* fp = fopen(path.c_str(), "rb");

    while (fgets(buffer, 65520, fp))
    {
        Star* s;

        read_field_onebased(buffer, 26, 31, field);
        HDno = atoi(field);

        HDfound = false;
        if (HDno)
        {
            for (j=0; j<offset; j++)
            {
                if (cels[j]->type == star && ((Star*)cels[j])->HD == HDno)
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

        //    Bytes Format  Units   Label    Explanations
        // --------------------------------------------------------------------------------
        //    1-  4  I4     ---     HR       [1/9110]+ Harvard Revised Number
        read_field_onebased(buffer, 1, 4, field);
        s->HR = atoi(field);

        //    5- 14  A10    ---     Name     Name, generally Bayer and/or Flamsteed name
        read_field_onebased(buffer, 5, 14, field);
        if (strlen(trim(field).c_str())) strcpy(s->name, trim(field).c_str());

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
        s->RI_magnitude = atof(field);

        //  128-147  A20    ---     SpType   Spectral type
        read_field_onebased(buffer, 128, 147, field);
        strcpy(s->spectral_type, trim(field).c_str());

        //  149-154  F6.3 arcsec/yr pmRA    *?Annual proper motion in RA J2000, FK5 system
        read_field_onebased(buffer, 149, 154, field);
        s->proper_motion_RA = atof(field) * fiftyseventh / 3600 / year;

        //  155-160  F6.3 arcsec/yr pmDE     ?Annual proper motion in Dec J2000, FK5 system
        read_field_onebased(buffer, 155, 160, field);
        s->proper_motion_decl = atof(field) * fiftyseventh / 3600 / year;

        //  162-166  F5.3   arcsec  Parallax ? Trigonometric parallax (unless n_Parallax)
        read_field_onebased(buffer, 162, 166, field);
        s->parallax = atof(field);
        if (!strcmp(s->name, "Sun"))
            s->distance = 0;
        else
            s->distance = (s->parallax > 0) ? (parsec / s->parallax) : light_year*1e4;
        if (s->parallax > 0) s->distance_known = true;
        s->parallax /= fiftyseven * 3600;

        //  167-170  I4     km/s    RadVel   ? Heliocentric Radial Velocity
        read_field_onebased(buffer, 167, 170, field);
        s->radial_velocity = atof(field) * 1000;

        // Estimate some more parameters based on the data.
        if (!strlen(s->name))
        {
            if (s->HD) strcpy(s->name, ((std::string)"HD" + std::to_string(s->HD)).c_str());
        }

        s->VR_magnitude = (s->RI_magnitude + s->BV_color*2) / 3;      // VERY rough estimate
        double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
        s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;

        #if 0
        // stellar radius = sqrt( lum(sun) * 10^(0.4 * mag(sun)-mag) / 4 pi sigma T^4 )
        // better also apply https://en.wikipedia.org/wiki/Bolometric_correction
        s->volumetric_mean_radius = solar_radius * 

        s->mass = ?????
        #endif

        s->update_location(J2000_TIME_T);
        // Assumed 90 degree inclination for all extrasolar systems unles sinclination known.
        s->location.local_system_plane = align_points_3d(cels[0]->location.system_center, Point(0,0,light_year*1e9), s->location.system_center);

        if (!HDfound)
        {
            cels[offset+num_read] = s;
            num_read++;
            if ((offset+num_read) >= (max-1)) break;
        }
    }
    fclose(fp);
    return num_read;
}

int CatalogReader::read_Hipparcos_catalog(CelestialObject **cels, int max)
{
    std::string path = "catalogs/Hipparcos/hip_main.dat";
    char buffer[1024];
    char field[32];
    int num_read = 0;
    int offset, HD, HIP, j;
    double RA, Decl, f;
    Star* s;

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= max) return 0;

    FILE* fp = fopen(path.c_str(), "rb");
    while (fgets(buffer, 1020, fp))
    {
        //   9- 14  I6    ---     HIP       Identifier (HIP number)
        read_field_onebased(buffer, 9, 14, field);
        HIP = atoi(field);

        // 391-396  I6    ---     HD        [1/359083]? HD number <III/135>
        read_field_onebased(buffer, 391, 396, field);
        HD = atoi(field);

        // For now, do not load Hipparcos stars.
        // Only use this catalog to refine positional data of already loaded stars.
        // Later, we can expand program functionality to filter by absolute magnitude
        // and by distance to the user's current POV.
        // Note Hipparcos RA/Decl coordinates are in J1991.25 epoch.
        // Even if the star already has coordinates in J2000,
        // use the Hipparcos coordinates because they are high accuracy.
        s = nullptr;
        bool is_new = false;
        for (j=0; j<offset; j++)
        {
            if (cels[j]->type != star) continue;
            if ((HD && ((Star*)cels[j])->HD == HD)
                ||
                (HIP && ((Star*)cels[j])->HIP == HIP)
                )
            {
                s = (Star*)cels[j];
                break;
            }
        }
        if (!s)
        {
            // There are only a handful with no V magnitude; omit them.
            read_field_onebased(buffer, 42, 46, field);
            if (!trim(field).size()) continue;
            double appmag = atof(field);

            #if _filter_Hipparcos_stars_appmag
            f = atof(field);
            // From spectral type, determine if star is at least giant.
            read_field_onebased(buffer, 436, 447, field);
            if (!strchr(field, 'I') || strchr(field, 'V')) continue;
            if (f > 6.5) continue;
            #endif
            #if _filter_Hipparcos_stars_absmag
            read_field_onebased(buffer, 80, 86, field);
            double parallax = atof(field);
            double distance = (parallax > 0) ? (parsec / parallax * 1000) : light_year*1e4;
            double intrinsic_brightness = pow(magnbase, -appmag) * pow(fmax(AU, distance) / parsec / 10, 2);
            double absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;
            if (absolute_magnitude > 7) continue;
            #endif
            s = new Star();
            is_new = true;
        }

        s->HD = HD;
        s->HIP = HIP;

        //  52- 63  F12.8 deg     RAdeg    *? alpha, degrees (ICRS, Epoch=J1991.25)
        read_field_onebased(buffer, 52, 63, field);
        RA = atof(field) * fiftyseventh;

        //  65- 76  F12.8 deg     DEdeg    *? delta, degrees (ICRS, Epoch=J1991.25)
        read_field_onebased(buffer, 65, 76, field);
        Decl = atof(field) * fiftyseventh;

        if (RA && Decl)
        {
            s->right_ascension = RA;
            s->declination = Decl;
            s->epoch = 2448349.0625;
        }

        //  80- 86  F7.2  mas     Plx       ? Trigonometric parallax
        read_field_onebased(buffer, 80, 86, field);
        f = atof(field) / 1000 / 3600 * fiftyseventh;
        if (f)
        {
            s->parallax = f;
            s->distance = (s->parallax > 0) ? (parsec / atof(field) * 1000) : light_year*1e4;
            s->distance_known = true;
        }

        //  88- 95  F8.2 mas/yr   pmRA     *? Proper motion mu_alpha.cos(delta), ICRS
        read_field_onebased(buffer, 88, 95, field);
        f = atof(field) / 1000 / 3600 / year * fiftyseventh;
        if (f) s->proper_motion_RA = f;

        //  97-104  F8.2 mas/yr   pmDE     *? Proper motion mu_delta, ICRS 
        read_field_onebased(buffer, 97, 104, field);
        f = atof(field) / 1000 / 3600 / year * fiftyseventh;
        if (f) s->proper_motion_decl = f;

        //  42- 46  F5.2  mag     Vmag      ? Magnitude in Johnson V                  (H5)
        read_field_onebased(buffer, 42, 46, field);
        f = atof(field);
        if (trim(field).size()) s->apparent_magnitude = f;
        double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
        s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;

        // 246-251  F6.3  mag     B-V       ? Johnson B-V colour
        read_field_onebased(buffer, 246, 251, field);
        f = atof(field);
        if (f || trim(field).size()) s->BV_color = f;

        // 436-447  A12   ---     SpType    Spectral type
        read_field_onebased(buffer, 436, 447, field);
        if (trim(field).size()) strcpy(s->spectral_type, trim(field).c_str());

        s->update_location(J2000_TIME_T);
        if (is_new)
        {
            cels[offset++] = s;
            // std::cout << "Added HIP" << s->HIP << std::endl << std::flush;
        }
        else
        {
            if (frand(0,1) < 0.03 && s->name[0]) std::cout << "Updated " << s->name << std::endl << std::flush;
        }

        num_read++;
        if (num_read >= max-4) break;
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

    for (offset=0; offset<max && cels[offset]; offset++);
    if (offset >= max) return 0;

    bool already[max];
    memset(already, 0, max*sizeof(bool));

    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;

    while (fgets(buffer, 1020, fp))
    {
        //  99-104  A6     ---     HD       HD identifier
        read_field_onebased(buffer, 99, 104, field);
        HD = atoi(field);

        // 127-132  I6     ---     HIC      ? Hipparcos Input Catalogue (Turon et al., Cat. <I/196>) identifier (also HIP <I/239>)
        read_field_onebased(buffer, 127, 132, field);
        HIP = atoi(field);

        bool found = false;
        for (i=0; i<offset; i++)
        {
            if (already[i]) continue;
            if (cels[i]->type != star) continue;
            s = (Star*)cels[i];

            if ((HD && s->HD == HD) || (HIP && s->HIP == HIP))
            {
                found = true;
                break;
            }
        }

        if (already[i]) continue;

        // TODO: For systems where both members are not already loaded,
        // can load additional members.
        if (!found) continue;
        already[i] = true;

        //      13  A1     ---     Comp     [A-Z?] Concerned component (6)
        char component = buffer[12];

        if (component == '?') continue;
        if (component <= 'A')
        {
            A = s;
            continue;
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

        // Copy parameters from member A
        s->distance = A->distance;
        s->proper_motion_RA = A->proper_motion_RA;
        s->proper_motion_decl = A->proper_motion_decl;
        s->radial_velocity = A->radial_velocity;
        s->orbit = new Orbit();
        s->orbit->center = A;

        // The inclination is unknown, but let's assume zero degrees
        s->inclination = A->inclination = 0;
        A->location.local_system_plane = align_points_3d(cels[0]->location.system_center, Point(0,light_year*1e9,0), A->location.system_center);
        s->location = A->location;                      // Copies local system reference frame
        s->epoch = J2000;
        s->update_location(J2000_TIME_T);

        // Estimate the semimajor axis
        double sma = sin(rho) * A->distance;
        s->orbit->semimajor_axis = sma;

        // Figure the absolute magnitude
        double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
        s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;

        if (A->name[0] && s->name[0]) std::cout << "Updated " << A->name << ": " << s->name << std::endl << std::flush;

        // TODO: For systems where both members are not already loaded,
        // can load additional members.

        //  57- 58  I2     ---     Obs      ? for component A: number of components; other component: number of measurements
        //  60- 63  F4.1   mag     Vmag     ? magnitude
        //  65- 66  A2     ---     Sp       Spectral type
        //  68- 72  I5    mas/yr   pmRA     ? annual proper motion in 0"001
        //  73- 77  I5    mas/yr   pmDE     ? annual proper motion in 0"001
        num_read++;
    }
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
                std::cout << "Named " << s->name << std::endl << std::flush;
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

    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;

    Star *A, *s;

    while (fgets(buffer, 1020, fp))
    {
        if (*buffer == '#') continue;
        if (!trim(buffer).size()) continue;

        read_field_onebased(buffer, 1, 23, field);
        std::string cenname = trim(field);
        A = nullptr;
        for (i=0; cels[i]; i++) if (!strcmp(cels[i]->name, cenname.c_str()))
        {
            A = (Star*)cels[i];
            break;
        }

        if (!A) continue;

        read_field_onebased(buffer, 25, 47, field);
        std::string bdyname = trim(field);
        s = nullptr;
        for (i=0; cels[i]; i++) if (!strcmp(cels[i]->name, bdyname.c_str()))
        {
            s = (Star*)cels[i];
            break;
        }

        if (!s) continue;

        if (!s->orbit) s->orbit = new Orbit();
        s->orbit->center = A;

        read_field_onebased(buffer, 49, 63, field);
        s->orbit->period = atof(field);

        read_field_onebased(buffer, 65, 75, field);
        s->orbit->ascending_node = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 77, 87, field);
        s->orbit->inclination = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 89, 99, field);
        s->orbit->arg_periapsis = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 101, 111, field);
        s->orbit->semimajor_axis = atof(field);

        read_field_onebased(buffer, 113, 123, field);
        s->orbit->eccentricity = atof(field);

        read_field_onebased(buffer, 125, 143, field);
        s->orbit->mean_anomaly = atof(field) * fiftyseventh;

        read_field_onebased(buffer, 145, 155, field);
        s->orbit->mean_anomaly = atof(field) * fiftyseventh;

        // First, solve for inclination
        Rotation inclined = align_points_3d(cels[0]->location.system_center,
            Point( 0, cos(s->orbit->inclination) * light_year*1e9, sin(s->orbit->inclination) * light_year*1e9 ),
            A->location.system_center);

        // Then incline the stars' pole
        Point pole = rotate3D(yaxis, center, inclined.v, -inclined.a);

        // Then rotate along the Sun-star axis
        Point axis = A->location.system_center - cels[0]->location.system_center;
        pole = rotate3D(pole, center, axis, -(s->orbit->ascending_node - M_PI/2));

        // Then realign the points for the new pole
        A->location.local_system_plane = align_points_3d(pole, Point(0,light_year*1e9,0), center);
        A->location.orbital_plane.a = 0;
        A->location.equatorial_plane.a = 0;
        s->location = A->location;
        s->orbit->ascending_node = s->orbit->inclination = 0;           // Clear these because we transfered them to the system plane.

        if (A->HD && s->HD)
            std::cout << "Updated " << (strlen(A->name) ? A->name : (std::string("HD")+std::to_string(A->HD)))
                << ": " << (strlen(s->name) ? s->name : (std::string("HD")+std::to_string(s->HD))) << std::endl << std::flush;

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
    if (offset >= max) return 0;

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
        Planet* p = new Planet();
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

        read_field_onebased(buffer, 193, 210, field);
        p->sidereal_rotational_period = atof(field);

        read_field_onebased(buffer, 212, 223, field);
        p->mass = atof(field);
        if (p->mass < 4e+28) p->type = rocky;
        else if (p->mass >= 2.5e+29) p->type = gas_giant;
        else p->type = ice_giant;

        read_field_onebased(buffer, 225, 231, field);
        p->surface_pressure = atof(field);

        read_field_onebased(buffer, 233, 241, field);
        p->epoch = J2000 + (atof(field) - 2000)*(year/86400);

        p->color = Color::color_from_magnitude_indices(p->absolute_magnitude, p->BV_color);
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
