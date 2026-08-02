#include <iostream>
#include <string.h>
#include <algorithm>
#include <math.h>
#include "cons.h"

using namespace alienorum;

double msq_mass[70], msq_rad[70], msq_lum[70], msq_temp[70], msq_BV[70];
Star **hdcache = nullptr, **hipcache = nullptr;

char Star::get_component()
{
    if (!multisys) return 0;
    return multisys->is_member(this);
}

void Star::set_component(char comp, Star* compA)
{
    if (!compA) compA = this;

    // assert(!multisys || (compA->multisys == multisys));
    if (multisys && compA->multisys && compA->multisys != multisys)
    {
        compA->multisys->merge(multisys);
    }

    if (!compA->multisys) compA->multisys = new StarMulti();
    multisys = compA->multisys;
    multisys->add_member(this, comp);

    if (comp > 'A')
    {
        if (compA->orbit && compA->orbit->center == this)
        {
            if (orbit) delete orbit;
            orbit = compA->orbit;
            compA->orbit = nullptr;
        }
        if (!orbit) orbit = new Orbit();
        orbit->center = compA;
    }
    else
    {
        if (orbit) delete orbit;
        orbit = nullptr;
    }

    if (!user_edited || user_added) origname = name;
    if (orbit && orbit->center) origcenname = orbit->center->name;
}

Star::Star()
{
    _class = class_star;
    memset(spectral_type, 0, 32*sizeof(char));
    memset(Bayer, 0, 32*sizeof(char));
    memset(Flamsteed, 0, 32*sizeof(char));
    memset(constellation, 0, 4*sizeof(char));
    memset(Gliese, 0, 16*sizeof(char));
    // sidereal_rotational_period = 25 * oneday;            // Sun
}

Star::~Star()
{
    if (orbit) delete orbit;
}

StarMulti::~StarMulti()
{
    if (members) delete[] members;
}

void Star::update_location(double tmnow)
{
    if (orbit && orbit->period)
    {
        update_orbit_location(tmnow);
        return;
    }

    // How many seconds since star's epoch
    double elapsed = tmnow - J2000_TIME_T + oneday * (J2000 - epoch);

    // Estimate RA and Decl using proper motion
    double l_RA = right_ascension + proper_motion_RA * elapsed;
    double l_Decl = declination + proper_motion_decl * elapsed;

    // Estimate distance using radial velocity
    double l_dist = distance + radial_velocity * elapsed;

    // Compute new location
    Point newloc = Point::from_ra_dec(l_RA, l_Decl, l_dist);

    // Set system location
    location.system_center = newloc;

    if (!estimated_poles)
    {
        if (!lock_system_plane) location.local_system_plane = system_plane_from_incl_and_node(known_poles ? obliquity : (_pi/2), equinox,
            Point::from_ra_dec(right_ascension, declination, distance));
        if ((mycenobj == this) && !lock_equatorial_plane)
        {
            location.orbital_plane = location.local_system_plane;

            Point eqaxis(sin(equinox), 0, cos(equinox));
            Point my_eq_pole = rotate3D(yaxis, center, eqaxis, obliquity);
            my_eq_pole = rotate3D(my_eq_pole, center, location.local_system_plane.v, -location.local_system_plane.a);
            location.equatorial_plane = align_points_3d(my_eq_pole, yaxis, center);
        }
    }
}

void Star::rename_from_Bayer_Flamsteed()
{
    if (!strlen(constellation)) return;
    if (has_custom_name) return;
    if (orbit && orbit->center && orbit->center->typeclass() == class_star
        && !BayerGrkno && !FlamsteedNo && GouldNo < 1
        )
    {
        std::string buildname = lop_component(orbit->center->name);
        buildname += std::string(" B");
        strcpy(name, buildname.c_str());
        return;
    }
    if (BayerGrkno < 0 && !FlamsteedNo && (GouldNo < 1)) return;

    if (!constellations.size())
    {
        std::cerr << "Must read constellation definitions before setting Bayer-Flamsteed names." << std::endl;
        throw 0xbadc0de;
    }

    // Find gentive of constellation.
    int i, j=-1, n = constellations.size();
    for (i=0; i<n; i++)
    {
        if (!strcmp(constellations[i].abbrev.c_str(), constellation))
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
        if (number)
        {
            if (!strcmp(constellations[j].abbrev.c_str(), "Ori") && BayerGrkno == 7)
                strcpy(name, (std::string("HD" + std::to_string(HD)).c_str()));
            if (!strcmp(constellations[j].abbrev.c_str(), "UMa") && BayerGrkno == 13)
                strcpy(name, Gliese);
            else strcpy(name, (Greek_letter[BayerGrkno] + std::string(" ") + std::to_string(number) + std::string(" ") + constellations[j].genitive).c_str());
        }
        else strcpy(name, (Greek_letter[BayerGrkno] + std::string(" ") + constellations[j].genitive).c_str());

        if (BayerGrkno == 5 && !strcmp(constellations[j].abbrev.c_str(), "Ret")) has_custom_name = true;
    }
    else if (FlamsteedNo > 0)
    {
        if (!strcmp(constellations[j].abbrev.c_str(), "UMa") && FlamsteedNo == 53)
            strcpy(name, Gliese);
        else strcpy(name, (std::to_string(FlamsteedNo) + std::string(" ") + constellations[j].genitive).c_str());
    }
    else if (GouldNo > 0)
    {
        if (GouldNo == 82 && !strcmp(constellations[j].abbrev.c_str(), "Eri"))
            strcpy(name, (std::to_string(GouldNo) + std::string(" ") + constellations[j].genitive).c_str());
        else strcpy(name, (std::to_string(GouldNo) + std::string(" G. ") + constellations[j].genitive).c_str());
    }

    if (multisys && multisys->get_member('A') == this)
    {
        Star* companion;
        for (char c = 'B'; (companion = multisys->get_member(c)); c++)
        {
            if (!companion->has_custom_name)
                strcpy(companion->name, (lop_component(name) + std::string(" ") + std::string(1, c)).c_str() );
        }
    }

    if (!user_edited || user_added) origname = name;
    if (orbit && orbit->center) origcenname = orbit->center->name;
}

bool alienorum::Star::matches_constellation(const char *match_cons)
{
    return (constellation[0] & 0x5f) == (match_cons[0] & 0x5f)
        && (constellation[1] & 0x5f) == (match_cons[1] & 0x5f)
        && (constellation[2] & 0x5f) == (match_cons[2] & 0x5f);
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
    if (!sptyp[i])
    {
        // For stars that don't have a Roman numeral,
        // but are within the temperature and luminosity limits,
        // let's go ahead and consider them sunlike.
        if (absolute_magnitude >= 4.12 && absolute_magnitude <= 5.93) return true;
        return false;
    }

    // There might be a space between.
    while (sptyp[i] == ' ') i++;
    if (!sptyp[i]) return false;

    // If the next letter is V, and it's not followed by I, then we're in the main sequence.
    bool mainseq = (sptyp[i] == 'V' && sptyp[i+1] != 'I');

    return mainseq && ((mklett == 'F' && mklettsub >= 8) || (mklett == 'G') || (mklett == 'K' && mklettsub <= 2));
}

bool Star::is_in_visible_box(Point seen_from)
{
    if (visible_area_set && frand(0,1) > 0.03) return _is_in_visible_range;
    return is_really_truly_in_visible_box(seen_from);
}

bool Star::is_really_truly_in_visible_box(Point seen_from)
{
    if (_is_always_visible) return true;
    double cutoff_dist = (pow(100.0, 0.2*(normal_best_mag_limit-apparent_magnitude)) * distance) * global_brightness;
    visible_area.corner1 = Point(-cutoff_dist, -cutoff_dist, -cutoff_dist) + location.system_center;
    visible_area.corner2 = Point( cutoff_dist,  cutoff_dist,  cutoff_dist) + location.system_center;
    visible_area_set = true;

    _is_in_visible_range = visible_area.point_in_box(seen_from);
    return _is_in_visible_range;
}

void Star::make_universally_visible()
{
    visible_area.corner1 = Point(-1.37e+9*light_year, -1.37e+9*light_year, -1.37e+9*light_year);
    visible_area.corner2 = Point( 1.37e+9*light_year,  1.37e+9*light_year,  1.37e+9*light_year);
    visible_area_set = true;
    _is_always_visible = true;
}

double Star::estimate_temperature()
{
    if (temperature) return temperature;
    if (!strlen(spectral_type)) return 5800;
    // double msqi = get_mseqidx_from_sptyp(spectral_type);
    double msqi = get_mseqidx_from_BV(BV_color);
    if (msqi >= mseqmin && msqi <= mseqmax) return interpolate_mseq_temp(msqi);

    return 2000;
}

double Star::estimate_luminosity(double tempK)
{
    // double msqi = get_mseqidx_from_sptyp(spectral_type);
    double msqi = get_mseqidx_from_BV(BV_color);
    if (msqi >= mseqmin && msqi <= mseqmax) return interpolate_mseq_lum(msqi);

    return std::pow(volumetric_mean_radius/solar_radius, 2) * std::pow(tempK/sun_temp, 4) * pow(magnbase, -4.85);
}

void Star::estimate_BV()
{
    // double msqi = get_mseqidx_from_sptyp(spectral_type);
    double msqi = get_mseqidx_from_BV(BV_color);
    if (msqi >= mseqmin && msqi <= mseqmax) BV_color = interpolate_mseq_BV(msqi);
    else
    {
        double T = estimate_temperature();
        estimate_BV(T);
    }
}

void Star::estimate_UB()
{
    double T = estimate_temperature();
    estimate_UB(T);
}

void Star::estimate_BV(double T)
{
    BV_color = log(blackbody_flux(T, V_band) / blackbody_flux(T, B_band)) * invlogmagnbase - bv_correction;
}

void Star::estimate_UB(double T)
{
    UB_color = log(blackbody_flux(T, B_band) / blackbody_flux(T, U_band)) * invlogmagnbase;
}

void alienorum::Star::load_main_seq_dat()
{
    FILE *fp = fopen("catalogs/mainseq.dat", "rb");
    if (fp)
    {
        char buffer[1024];
        int i = mseqmin;
        while (fgets(buffer, 1022, fp))
        {
            if (strlen(buffer) < 41) continue;
            if (buffer[0] == '#') continue;
            //           1111111111222222222233333333334444444444
            // 01234567890123456789012345678901234567890123456789
            // O3V     59      13.43   660693  45900   −0.330
            buffer[15] = buffer[23] = buffer[31] = buffer[39] = 0;
            msq_mass[i] = atof(&buffer[8]);
            msq_rad[i] = atof(&buffer[16]);
            msq_lum[i] = atof(&buffer[24]);
            msq_temp[i] = atof(&buffer[32]);
            msq_BV[i] = atof(&buffer[40]);
            i++;
            if (i>=mseqmax) break;
        }
        fclose(fp);
    }
}

double alienorum::Star::get_mseqidx_from_sptyp(const char *sptyp)
{
    int offset = 0;
    if (sptyp[offset] >= 'a' && sptyp[offset] <= 'z') offset++;
    double result = -1;
    if (sptyp[offset] == 'M') result = 60;
    if (sptyp[offset] == 'K') result = 50;
    if (sptyp[offset] == 'G') result = 40;
    if (sptyp[offset] == 'F') result = 30;
    if (sptyp[offset] == 'A') result = 20;
    if (sptyp[offset] == 'B') result = 10;
    if (sptyp[offset] == 'O') result =  0;
    if (result < 0) return result;
    result += atof(&sptyp[offset+1]);
    return result;
}

double alienorum::Star::get_mseqidx_from_mass(double m)
{
    int i;
    double delta, d, coeff;
    for (i=mseqmin; i<mseqmax; i++)
    {
        if (msq_mass[i] <= m)
        {
            if (i == mseqmin) return i;
            delta = msq_mass[i-1] - msq_mass[i];
            d = m - msq_mass[i];
            coeff = d/delta;
            return (double)i - coeff;
        }
    }
    return mseqmax-1;
}

double alienorum::Star::get_mseqidx_from_rad(double rad)
{
    int i;
    double delta, d, coeff;
    for (i=mseqmin; i<mseqmax; i++)
    {
        if (msq_rad[i] <= rad)
        {
            if (i == mseqmin) return i;
            delta = msq_rad[i-1] - msq_rad[i];
            d = rad - msq_rad[i];
            coeff = d/delta;
            return (double)i - coeff;
        }
    }
    return mseqmax-1;
}

double alienorum::Star::get_mseqidx_from_lum(double lum)
{
    int i;
    double delta, d, coeff;
    for (i=mseqmin; i<mseqmax; i++)
    {
        if (msq_lum[i] <= lum)
        {
            if (i == mseqmin) return i;
            delta = msq_lum[i-1] - msq_lum[i];
            d = lum - msq_lum[i];
            coeff = d/delta;
            return (double)i - coeff;
        }
    }
    return mseqmax-1;
}

double alienorum::Star::get_mseqidx_from_temp(double T)
{
    int i;
    double delta, d, coeff;
    for (i=mseqmin; i<mseqmax; i++)
    {
        if (msq_temp[i] <= T)
        {
            if (i == mseqmin) return i;
            delta = msq_temp[i-1] - msq_temp[i];
            d = T - msq_temp[i];
            coeff = d/delta;
            return (double)i - coeff;
        }
    }
    return mseqmax-1;
}

double alienorum::Star::get_mseqidx_from_BV(double BV)
{
    // Unlike msq_mass/msq_rad/msq_lum/msq_temp, msq_BV increases with i (hot to cool),
    // so this scans for the first entry >= BV rather than <= BV.
    int i;
    double delta, d, coeff;
    for (i=mseqmin; i<mseqmax; i++)
    {
        if (msq_BV[i] >= BV)
        {
            if (i == mseqmin) return i;
            delta = msq_BV[i] - msq_BV[i-1];
            d = BV - msq_BV[i-1];
            coeff = d/delta;
            return (double)(i-1) + coeff;
        }
    }
    return mseqmax-1;
}

double alienorum::Star::interpolate_mseq_mass(double mseqidx)
{
    if (mseqidx < mseqmin) mseqidx = mseqmin;
    if (mseqidx > mseqmax) mseqidx = mseqmax;
    int i = floor(mseqidx);
    if (i == mseqmax-1) return msq_mass[i];
    double coeff1 = mseqidx - i, coeff0 = 1.0 - coeff1;
    return coeff0 * msq_mass[i] + coeff1 * msq_mass[i+1];
}

double alienorum::Star::interpolate_mseq_rad(double mseqidx)
{
    if (mseqidx < mseqmin) mseqidx = mseqmin;
    if (mseqidx > mseqmax) mseqidx = mseqmax;
    int i = floor(mseqidx);
    if (i == mseqmax-1) return msq_rad[i];
    double coeff1 = mseqidx - i, coeff0 = 1.0 - coeff1;
    return coeff0 * msq_rad[i] + coeff1 * msq_rad[i+1];
}

double alienorum::Star::interpolate_mseq_lum(double mseqidx)
{
    if (mseqidx < mseqmin) mseqidx = mseqmin;
    if (mseqidx > mseqmax) mseqidx = mseqmax;
    int i = floor(mseqidx);
    if (i == mseqmax-1) return msq_lum[i];
    double coeff1 = mseqidx - i, coeff0 = 1.0 - coeff1;
    return coeff0 * msq_lum[i] + coeff1 * msq_lum[i+1];
}

double alienorum::Star::interpolate_mseq_temp(double mseqidx)
{
    if (mseqidx < mseqmin) mseqidx = mseqmin;
    if (mseqidx > mseqmax) mseqidx = mseqmax;
    int i = floor(mseqidx);
    if (i == mseqmax-1) return msq_temp[i];
    double coeff1 = mseqidx - i, coeff0 = 1.0 - coeff1;
    return coeff0 * msq_temp[i] + coeff1 * msq_temp[i+1];
}

double alienorum::Star::interpolate_mseq_BV(double mseqidx)
{
    if (mseqidx < mseqmin) mseqidx = mseqmin;
    if (mseqidx > mseqmax) mseqidx = mseqmax;
    int i = floor(mseqidx);
    if (i == mseqmax-1) return msq_BV[i];
    double coeff1 = mseqidx - i, coeff0 = 1.0 - coeff1;
    return coeff0 * msq_BV[i] + coeff1 * msq_BV[i+1];
}

double Star::estimate_radius()
{
    // double msqi = get_mseqidx_from_sptyp(spectral_type);
    double msqi = get_mseqidx_from_BV(BV_color);
    if (msqi >= mseqmin && msqi <= mseqmax) return volumetric_mean_radius = interpolate_mseq_rad(msqi) * solar_radius;

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
    double solar_radii = std::sqrt(luminosity) * std::pow(sun_temp / T, 2.0);
    return volumetric_mean_radius = solar_radii * solar_radius;
}

void Star::gotta_be_named_something()
{
    if (!trim(name).size())
    {
        if (multisys && multisys->get_member('A') != this) return;              // update this name later, based on the main star's name
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
        else std::cerr << "Failed to name star @ RA: " << RA_as_hms(0) << " decl " << Decl_as_degms() << " magnitude " << apparent_magnitude
            << " distance " << (distance/light_year) << std::endl;
    }

    if (multisys)
    {
        Star* companion;
        for (char c = 'B'; (companion = multisys->get_member(c)); c++)
        {
            if(companion == nullptr)
                break;
            if (!companion->has_custom_name)
                strcpy(companion->name, (lop_component(name) + std::string(" ") + std::string(1, c)).c_str() );
        }
    }

    if (!user_edited || user_added) origname = name;
    if (orbit && orbit->center) origcenname = orbit->center->name;
}

json Star::to_json()
{
    json towrite = CelestialObject::to_json();

    towrite["proper_motion_RA"] = proper_motion_RA*fiftyseven*oneyear;
    towrite["proper_motion_decl"] = proper_motion_decl*fiftyseven*oneyear;
    towrite["radial_velocity"] = radial_velocity;
    towrite["apparent_magnitude"] = apparent_magnitude;
    towrite["parallax"] = parallax*fiftyseven*3600*1000;
    towrite["spectral_type"] = spectral_type;
    towrite["Bayer"] = Bayer;
    towrite["Flamsteed"] = Flamsteed;
    towrite["Gliese"] = Gliese;
    towrite["BayerGrkno"] = BayerGrkno;
    towrite["FlamsteedNo"] = FlamsteedNo;
    towrite["constellation"] = constellation;
    towrite["HR"] = HR;
    towrite["HD"] = HD;
    towrite["HIP"] = HIP;
    towrite["SAO"] = SAO;
    towrite["SB9"] = SB9;
    towrite["CCDM"] = CCDM;
    towrite["Bonn_survey"] = Bonn_survey;
    towrite["Bonn_survey_sign"] = std::string(1, Bonn_survey_sign);
    towrite["Bonn_survey_declination"] = Bonn_survey_declination;
    towrite["Bonn_survey_sequential"] = Bonn_survey_sequential;
    towrite["is_orbit_multiple"] = is_orbit_multiple;

    return towrite;
}

bool Star::from_json(json j)
{
    CelestialObject::from_json(j);
    try { j.at("proper_motion_RA").get_to(proper_motion_RA); proper_motion_RA *= fiftyseventh/oneyear; } catch (...) { ; }
    try { j.at("proper_motion_decl").get_to(proper_motion_decl); proper_motion_decl *= fiftyseventh/oneyear; } catch (...) { ; }
    try { j.at("radial_velocity").get_to(radial_velocity); } catch (...) { ; }
    try { j.at("apparent_magnitude").get_to(apparent_magnitude); } catch (...) { ; }
    try { j.at("parallax").get_to(parallax); parallax *= fiftyseventh / 3600000; } catch (...) { ; }
    try
    {
        std::string str;
        j.at("spectral_type").get_to(str);
        strcpy(spectral_type, str.c_str());
    } catch (...) { ; }
    try
    {
        std::string str;
        j.at("Bayer").get_to(str);
        strcpy(Bayer, str.c_str());
    } catch (...) { ; }
    try
    {
        std::string str;
        j.at("Flamsteed").get_to(str);
        strcpy(Flamsteed, str.c_str());
    } catch (...) { ; }
    try
    {
        std::string str;
        j.at("Gliese").get_to(str);
        strcpy(Gliese, str.c_str());
    } catch (...) { ; }
    try { j.at("HR").get_to(HR); } catch (...) { ; }
    try { j.at("HD").get_to(HD); } catch (...) { ; }
    try { j.at("HIP").get_to(HIP); } catch (...) { ; }
    try { j.at("SAO").get_to(SAO); } catch (...) { ; }
    try { j.at("SB9").get_to(SB9); } catch (...) { ; }
    try { j.at("CCDM").get_to(CCDM); } catch (...) { ; }
    try
    {
        std::string str;
        j.at("Bonn_survey").get_to(str);
        strcpy(Bonn_survey, str.c_str());
    } catch (...) { ; }
    try
    {
        std::string str;
        j.at("Bonn_survey_sign").get_to(str);
        Bonn_survey_sign = str.c_str()[0];
    } catch (...) { ; }
    try { j.at("Bonn_survey_declination").get_to(Bonn_survey_declination); } catch (...) { ; }
    try { j.at("Bonn_survey_sequential").get_to(Bonn_survey_sequential); } catch (...) { ; }
    try { j.at("is_orbit_multiple").get_to(is_orbit_multiple); } catch (...) { ; }
    return true;
}

void Star::make_companion_of(Star *A, char comp)
{
    right_ascension = A->right_ascension;
    declination = A->declination;
    parallax = A->parallax;
    distance = A->distance;
    location = A->location;
    if (!has_custom_name) strcpy(name, (std::string(lop_component(A->name)) + std::string(" ") + std::string(1, comp)).c_str());
    CCDM = A->CCDM;
    proper_motion_RA = A->proper_motion_RA;
    proper_motion_decl = A->proper_motion_decl;
    visible_area = A->visible_area;
    type = star;

    // A->set_component('A', A);
    set_component(comp, A);
}

double Star::estimate_mass()
{
    // double msqi = get_mseqidx_from_sptyp(spectral_type);
    double msqi = get_mseqidx_from_BV(BV_color);
    if (msqi >= mseqmin && msqi <= mseqmax) return mass = interpolate_mseq_mass(msqi) * solar_mass;

    if (!cels[0])
    {
        std::cerr << "Called Star::estimate_mass() before loading Sun." << std::endl;
        throw 0xbadc0de;
    }
    double T = estimate_temperature();
    double logL = (cels[0]->absolute_magnitude - absolute_magnitude);
    double luminosity = std::pow(magnbase, logL);
    double radius = (volumetric_mean_radius ? volumetric_mean_radius : estimate_radius()) / solar_radius;

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
    mass = (gravity / solargravity) * std::pow(radius, 2.0) * solar_mass;

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
                if (!strcmp(name1, name2) || lop_component(s1->Gliese) == lop_component(s2->Gliese))
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

void StarMulti::add_member(Star *s, char comp)
{
    comp &= 0x5f;
    if (comp < 'A' || comp > 'Z') throw 0xbadc0de;

    int cidx = comp - 'A';
    if (!members || cidx >= allocated)
    {
        char toalloc = std::max(2, cidx+1);
        Star** new_members = new Star*[toalloc];
        memset(new_members, 0, sizeof(Star*)*toalloc);
        if (members)
        {
            int i;
            for (i=0; i<allocated; i++) new_members[i] = members[i];
            delete[] members;
        }
        members = new_members;
        allocated = toalloc;
    }
    members[cidx] = s;
    s->multisys = this;
}

Star *StarMulti::get_member(char comp)
{
    comp &= 0x5f;
    if (comp < 'A' || comp > 'Z') throw 0xbadc0de;
    int i = comp - 'A';
    if (i >= allocated) return nullptr;
    return members[i];
}

char StarMulti::is_member(Star *s)
{
    if (!allocated) return 0;
    int i;
    for (i=0; i<allocated; i++)
        if (members[i] == s) return 'A' + i;
    return 0;
}

int alienorum::StarMulti::num_members()
{
    int i, result = 0;
    for (i=0; i<allocated; i++) if (members[i]) result++;
    return result;
}

char alienorum::StarMulti::next_available()
{
    int i;
    if (!allocated || !members) return 0;
    for (i=0; i<allocated; i++) if (!members[i]) break;
    return 'A' + i;
}

void StarMulti::unlink()
{
    if (!allocated) return;
    int i;
    for (i=0; i<allocated; i++)
    {
        if (members[i])
        {
            members[i]->multisys = nullptr;
            members[i] = nullptr;
        }
    }
}

#define _debug_merge 0
void alienorum::StarMulti::merge(StarMulti *other)
{
    int toalloc = std::max(2, allocated + other->allocated);
    Star** new_members = new Star*[toalloc];
    memset(new_members, 0, sizeof(Star*)*toalloc);

    #if _debug_merge
    std::cout << "Merge StarMulti objects: " << std::endl;
    #endif
    int i, j=0;
    if (members)
    {
        for (i=0; i<allocated; i++) if (members[i])
        {
            #if _debug_merge
            std::cout << "(1) -> " << members[i]->name << std::endl;
            #endif
            new_members[j++] = members[i];
        }
        delete[] members;
    }
    if (other->members)
    {
        for (i=0; i<other->allocated; i++) if (other->members[i])
        {
            #if _debug_merge
            std::cout << "(2) -> " << other->members[i]->name << std::endl;
            #endif
            new_members[j++] = other->members[i];
        }
        delete[] other->members;
        other->members = nullptr;
    }
    members = new_members;
    allocated = toalloc;
    for (i=0; i<allocated; i++)
        if (members[i])
            members[i]->multisys = this;

    delete other;
    #if _debug_merge
    std::cout << std::endl << std::flush;
    #endif
}
