
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
    "2MASS",
    "REGALADE",
    "GALEX",
    "astorb",
    "comets"
    // TODO: Add hundreds more...
};

bool have_Gliese = false, have_BSC = false, have_HIP = false;

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

        s->name = trim(build_name.c_str());
        s->Gliese = trim(build_name.c_str());

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
        s->spectral_type = trim(field);

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
            s->inclination = find_3D_angle(solar_north, galactic_north, center);
            s->equinox = galcen_longitude;
            std::cout << s->name << " inclination: " << (s->inclination * fiftyseven) << std::endl;
            std::cout << s->name << " equinox: " << (s->equinox * fiftyseven) << std::endl;
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
        if (strlen(trim(field).c_str())) s->name = trim(field);

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
                s->Bayer = bayer 
                + std::string(bayer.size() < 3 ? " " : "")
                + std::string(bayer.size() < 4 ? " " : "")
                + cons;

                s->constellation = cons;
            }

            if (s->FlamsteedNo)
            {
                s->Flamsteed = std::to_string(s->FlamsteedNo)
                + std::string((s->FlamsteedNo < 10) ? " " : "")
                + std::string((s->FlamsteedNo < 100) ? " " : "")
                + std::string((s->FlamsteedNo < 1000) ? " " : "")
                + cons;

                s->constellation = cons;
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
        s->spectral_type = trim(field);

        //  149-154  F6.3 arcsec/yr pmRA    *?Annual proper motion in RA J2000, FK5 system
        read_field_onebased(buffer, 149, 154, field);
        s->proper_motion_RA = atof(field) * fiftyseventh / 3600 / year;

        //  155-160  F6.3 arcsec/yr pmDE     ?Annual proper motion in Dec J2000, FK5 system
        read_field_onebased(buffer, 155, 160, field);
        s->proper_motion_decl = atof(field) * fiftyseventh / 3600 / year;

        //  162-166  F5.3   arcsec  Parallax ? Trigonometric parallax (unless n_Parallax)
        read_field_onebased(buffer, 162, 166, field);
        s->parallax = atof(field);
        if (!strcmp(s->name.c_str(), "Sun"))
            s->distance = 0;
        else
            s->distance = (s->parallax > 0) ? (parsec / s->parallax) : light_year*1e4;
        if (s->parallax > 0) s->distance_known = true;
        s->parallax /= fiftyseven * 3600;

        //  167-170  I4     km/s    RadVel   ? Heliocentric Radial Velocity
        read_field_onebased(buffer, 167, 170, field);
        s->radial_velocity = atof(field) * 1000;

        // Estimate some more parameters based on the data.
        if (!s->name.size())
        {
            if (s->HD) s->name = (std::string)"HD" + std::to_string(s->HD);
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
        if (!s) continue;

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

        // 231-236  F6.3  mag     VTmag     ? Mean VT magnitude
        read_field_onebased(buffer, 231, 236, field);
        f = atof(field);
        if (f || trim(field).size()) s->apparent_magnitude = f;
        double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
        s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;

        // 246-251  F6.3  mag     B-V       ? Johnson B-V colour
        read_field_onebased(buffer, 246, 251, field);
        f = atof(field);
        if (f || trim(field).size()) s->BV_color = f;

        // 436-447  A12   ---     SpType    Spectral type
        read_field_onebased(buffer, 436, 447, field);
        if (trim(field).size()) s->spectral_type = field;

        s->update_location(J2000_TIME_T);

        num_read++;
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
            if ((HD && s->HD == HD) || (HIP && s->HIP == HIP) || (Gliese.size() && s->Gliese == Gliese))
            {
                s->name = trim(field);
                num_read++;
                break;
            }
        }
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

        j = -1;
        read_field_onebased(buffer, 1, 25, field);
        std::string cenname = trim(field);
        for (i=0; i<offset; i++)
        {
            if (!strcmp(cels[i]->name.c_str(), cenname.c_str()))
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
        p->name = trim(field);
        if (!strcmp(p->name.c_str(), "Earth")) whereami = iamhome = offset;

        read_field_onebased(buffer, 44, 58, field);
        o->semimajor_axis = atof(field);

        read_field_onebased(buffer, 60, 64, field);
        p->BV_color = atof(field);

        read_field_onebased(buffer, 66, 70, field);
        p->UB_color = atof(field);

        read_field_onebased(buffer, 72, 77, field);
        o->eccentricity = atof(field);

        read_field_onebased(buffer, 79, 85, field);
        o->ascending_node = atof(field);

        read_field_onebased(buffer, 87, 94, field);
        o->arg_periapsis = atof(field);

        read_field_onebased(buffer, 96, 105, field);
        o->mean_anomaly = atof(field);

        read_field_onebased(buffer, 107, 112, field);
        p->absolute_magnitude = atof(field);

        read_field_onebased(buffer, 114, 123, field);
        p->volumetric_mean_radius = atof(field);

        read_field_onebased(buffer, 125, 134, field);
        p->oblateness = atof(field);

        read_field_onebased(buffer, 136, 147, field);
        o->eccentricity = atof(field);

        read_field_onebased(buffer, 149, 166, field);
        o->orbit_period = atof(field);

        read_field_onebased(buffer, 168, 174, field);
        p->inclination = atof(field);

        read_field_onebased(buffer, 176, 184, field);
        p->equinox = atof(field);

        read_field_onebased(buffer, 186, 203, field);
        p->sidereal_rotational_period = atof(field);

        read_field_onebased(buffer, 205, 216, field);
        p->mass = atof(field);
        if (p->mass < 4e+28) p->type = rocky;
        else if (p->mass >= 2.5e+29) p->type = gas_giant;
        else p->type = ice_giant;

        read_field_onebased(buffer, 218, 223, field);
        p->surface_pressure = atof(field);

        p->epoch = J2000;
        p->color = Color::color_from_magnitude_indices(p->absolute_magnitude, p->BV_color);

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
    for (i=0; i<len; i++) out[i] = buffer[i+start];
    out[i] = 0;
}
