#include <iostream>
#include <string.h>
#include <algorithm>
#include <math.h>
#include "star.h"

Star::Star()
{
    _class = class_star;
}

void Star::update_location(double tmnow)
{
    if (orbit && orbit->period)
    {
        update_orbit_location(tmnow);
        return;
    }

    // How many seconds since star's epoch
    double elapsed = tmnow - J2000_TIME_T + 86400 * (J2000 - epoch);

    // Estimate RA and Decl using proper motion
    double l_RA = right_ascension + proper_motion_RA * elapsed;
    double l_Decl = declination + proper_motion_decl * elapsed;

    // Estimate distance using radial velocity
    double l_dist = distance + radial_velocity * elapsed;

    // Compute new location
    Point newloc = Point::from_ra_dec(l_RA, l_Decl, l_dist);

    // Set system location
    location.system_center = newloc;
}

void Star::rename_from_Bayer_Flamsteed()
{
    if (!strlen(constellation)) return;
    if (BayerGrkno < 0 && !FlamsteedNo) return;

    if (!consabbrev.size() || !consgen.size())
    {
        std::cerr << "Must read constellation definitions before setting Bayer-Flamsteed names." << std::endl;
        throw 0xbadc0de;
    }

    // Find gentive of constellation.
    int i, j=-1, n = consabbrev.size();
    for (i=0; i<n; i++)
    {
        if (!strcmp(consabbrev[i].c_str(), constellation))
        {
            j = i;
            break;
        }
    }

    if (j<0)
    {
        // Not a valid constellation.
        constellation[0] = 0;
        return;
    }

    if (BayerGrkno >= 0)
    {
        int number = atoi(std::string(Bayer).substr(3, 1).c_str());
        if (number) strcpy(name, (Greek_letter[BayerGrkno] + std::string(" ") + std::to_string(number) + std::string(" ") + consgen[j]).c_str());
        else strcpy(name, (Greek_letter[BayerGrkno] + std::string(" ") + consgen[j]).c_str());
    }
    else if (FlamsteedNo)
    {
        strcpy(name, (std::to_string(FlamsteedNo) + std::string(" ") + consgen[j]).c_str());
    }
}

bool Star::is_sunlike()
{
    const char* sptyp = spectral_type;
    int i;

    // Must contain any of the letters OBAFGKM
    for (i=0; sptyp[i] && (sptyp[i] < 'A' || sptyp[i] > 'Z'); i++);
    if (!sptyp[i]) return false;
    char mklett = sptyp[i];

    // Followed by a number
    i++;
    if (sptyp[i] < '0' || sptyp[i] > '9') return false;
    float mklettsub = atof(&sptyp[i]);

    // Number might contain a decimal point and more digits.
    i++;
    while (sptyp[i] && (sptyp[i] == '.' || (sptyp[i] >= '0' && sptyp[i] <= '9'))) i++;
    if (!sptyp[i]) return false;

    // There might be a space between.
    while (sptyp[i] == ' ') i++;
    if (!sptyp[i]) return false;

    // If the next letter is V, and it's not followed by I, then we're in the main sequence.
    bool mainseq = (sptyp[i] == 'V' && sptyp[i+1] != 'I');

    return mainseq && ((mklett == 'F' && mklettsub >= 8) || (mklett == 'G') || (mklett == 'K' && mklettsub <= 2));
}

bool Star::is_in_visible_box(Point seen_from)
{
    if (!visible_area_set)
    {
        double intrinsic = pow(magnbase, -absolute_magnitude);
        double ratio = intrinsic / intrinsic_cutoff;
        double cutoff_dist = sqrt(ratio) * parsec * 10;
        visible_area.corner1 = Point(-cutoff_dist, -cutoff_dist, -cutoff_dist) + location.system_center;
        visible_area.corner2 = Point( cutoff_dist,  cutoff_dist,  cutoff_dist) + location.system_center;
        visible_area_set = true;
    }

    return visible_area.point_in_box(seen_from);
}

void Star::make_universally_visible()
{
    visible_area.corner1 = Point(-1.37e+9*light_year, -1.37e+9*light_year, -1.37e+9*light_year);
    visible_area.corner2 = Point( 1.37e+9*light_year,  1.37e+9*light_year,  1.37e+9*light_year);
    visible_area_set = true;
}

double Star::estimate_temperature()
{
    if (!strlen(spectral_type)) return 5800;
    double subtype = atof(&spectral_type[1]) / 10;

    // https://en.wikipedia.org/wiki/Stellar_classification#Harvard_spectral_classification
    // https://en.wikipedia.org/wiki/O-type_star
    #define O_hitemp 52000.0
    #define O_lowtemp 33000.0
    #define B_lowtemp 10000.0
    #define A_lowtemp  7300.0
    #define F_lowtemp  6000.0
    #define G_lowtemp  5300.0
    #define K_lowtemp  3900.0
    #define M_lowtemp  2300.0

    if (spectral_type[0] == 'O') return O_lowtemp + (1.0-subtype) * (O_hitemp -O_lowtemp);
    if (spectral_type[0] == 'B') return B_lowtemp + (1.0-subtype) * (O_lowtemp-B_lowtemp);
    if (spectral_type[0] == 'A') return A_lowtemp + (1.0-subtype) * (B_lowtemp-A_lowtemp);
    if (spectral_type[0] == 'F') return F_lowtemp + (1.0-subtype) * (A_lowtemp-F_lowtemp);
    if (spectral_type[0] == 'G') return G_lowtemp + (1.0-subtype) * (F_lowtemp-G_lowtemp);
    if (spectral_type[0] == 'K') return K_lowtemp + (1.0-subtype) * (G_lowtemp-K_lowtemp);
    if (spectral_type[0] == 'M') return M_lowtemp + (1.0-subtype) * (K_lowtemp-M_lowtemp);
    return 2000;
}

double Star::estimate_BV()
{
    double T = estimate_temperature();
    return log(blackbody_flux(T, V_band) / blackbody_flux(T, B_band)) * invlogmagnbase - bv_correction;
}

double Star::estimate_UB()
{
    double T = estimate_temperature();
    return log(blackbody_flux(T, B_band) / blackbody_flux(T, U_band)) * invlogmagnbase;
}

double Star::estimate_radius()
{
    if (!cels[0])
    {
        std::cerr << "Called Star::estimate_radius() before loading Sun." << std::endl;
        throw 0xbadc0de;
    }
    double T = estimate_temperature();
    // 1. Calculate Luminosity relative to the Sun (L/L_sun)
    double logL = (cels[0]->absolute_magnitude - absolute_magnitude);
    double luminosity = std::pow(magnbase, logL);

    // 2. Calculate radius relative to the Sun (R/R_sun) using Stefan-Boltzmann then scale to meters
    return volumetric_mean_radius = std::sqrt(luminosity) * std::pow(sun_temp / T, 2.0) * Rsun;
}

void Star::gotta_be_named_something()
{
    if (trim(name).size()) return;           // already am
    else if (orbit && orbit->center && strlen(orbit->center->name))
    {
        int n = strlen(orbit->center->name);
        if (orbit->center->name[n-1] >= 'A' && orbit->center->name[n-2] == ' ')
        {
            strcpy(name, orbit->center->name);
            name[n]++;
        }
        else
        {
            strcpy(name, ( std::string(orbit->center->name) + std::string(" B") ).c_str());
        }
    }
    else if (BayerGrkno && strlen(constellation)) rename_from_Bayer_Flamsteed();
    else if (FlamsteedNo && strlen(constellation)) rename_from_Bayer_Flamsteed();
    else if (HD) strcpy(name, (std::string("HD")+std::to_string(HD)).c_str() );
    else if (HIP) strcpy(name, (std::string("HIP")+std::to_string(HIP)).c_str() );
    else if (SAO) strcpy(name, (std::string("SAO")+std::to_string(SAO)).c_str() );
    else if (Bonn_survey_sequential)
    {
        name[0] = Bonn_survey[0];
        name[1] = Bonn_survey[1];
        name[2] = Bonn_survey_sign;
        strcpy(&name[3], (std::to_string(abs(Bonn_survey_declination)) + std::string(" ")
            + std::to_string(Bonn_survey_sequential) ).c_str() );
    }
    else if (SB9) strcpy(name, (std::string("SB9-")+std::to_string(SB9)).c_str() );
    else std::cerr << "Failed to name star @ RA: " << RA_as_hms() << " decl " << Decl_as_degms() << " magnitude " << apparent_magnitude
        << " distance " << (distance/light_year) << std::endl;
}

double Star::estimate_mass()
{
    if (!cels[0])
    {
        std::cerr << "Called Star::estimate_mass() before loading Sun." << std::endl;
        throw 0xbadc0de;
    }
    double T = estimate_temperature();
    double logL = (cels[0]->absolute_magnitude - absolute_magnitude);
    double luminosity = std::pow(magnbase, logL);
    double radius = estimate_radius() / Rsun;

    // Approximate Surface Gravity (log g) based on empirical stellar trends
    // Hotter and more luminous stars have different surface profiles.
    // This polynomial roughly mimics evolutionary paths on the HR diagram.
    double logT = std::log10(T);
    double est_log_G;

    if (luminosity > 10000.0)
    {
        // Supergiants / Bright Giants (Class I & II)
        est_log_G = 5.0 - 1.1 * (logT - 3.5);
    }
    else if (luminosity > 50.0)
    {
        // Regular Giants (Class III)
        est_log_G = 3.2 - 1.5 * (logT - 3.6);
    }
    else
    {
        // Main Sequence / Subgiants (Class V & IV)
        est_log_G = 4.4 - 0.6 * (logT - 3.7);
    }

    // Clamp log g to physically realistic boundaries for safety
    if (est_log_G < 0.5) est_log_G = 0.5;
    if (est_log_G > 5.0) est_log_G = 5.0;

    // Convert log10(g) back to raw gravity value in cgs (cm/s^2)
    double gravity = std::pow(10.0, est_log_G);

    // Calculate Mass via g = GM / R^2 
    // Expressed cleanly using solar constants: 
    // (g / g_sun) * (R / R_sun)^2 = M / M_sun
    double solargravity = 27400.0; // Sun's surface gravity is ~27,400 cm/s^2
    mass = (gravity / solargravity) * std::pow(radius, 2.0) * Msun;

    return mass;
}


void rename_all_from_Bayer_Flamsteed()
{
    int i;
    for (i=0; cels[i]; i++)
    {
        if (cels[i]->type == star)
        {
            Star* s = (Star*)cels[i];
            s->rename_from_Bayer_Flamsteed();           // has no effect if not a Bayer-Flamsteed star.
        }
    }
}

void Gliese_doubles_fix()
{
    int i, j, m, n;
    char name1[29], name2[29];

    // Fix for members B of multiple systems getting left behind when stellar positions are updated from B1950 to later epochs.
    for (i=0; cels[i]; i++)
    {
        if (cels[i]->type == star)
        {
            if (cels[i]->type != star) continue;
            Star* s1 = (Star*)cels[i];
            if (!strlen(s1->Gliese)) continue;

            strcpy(name1, s1->Gliese);
            n = strlen(name1);
            if (name1[n-1] == 'A' && name1[n-2] == ' ')
                name1[n-2] = 0;
            else continue;

            n = strlen(name1);

            for (j=std::max(0, i-100); cels[j] && j<i+100; j++)
            {
                if (j==i) continue;
                if (cels[j]->type != star) continue;
                Star* s2 = (Star*)cels[j];
                m = strlen(s2->Gliese);
                if (!m) continue;
                if (m < n) continue;
                if (s2->Gliese[n] != ' ') continue;

                strcpy(name2, s2->Gliese);
                name2[n] = 0;
                if (!strcmp(name1, name2))
                {
                    s2->right_ascension = s1->right_ascension;
                    s2->declination = s1->declination;
                    s2->distance = s1->distance;
                    s2->proper_motion_RA = s1->proper_motion_RA;
                    s2->proper_motion_decl = s1->proper_motion_decl;
                    s2->radial_velocity = s1->radial_velocity;

                    s2->update_location(J2000_TIME_T);
                }
            }
        }
    }
}
