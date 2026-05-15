#include <iostream>
#include <string.h>
#include <algorithm>
#include "star.h"

void Star::update_location(double tmnow)
{
    // How many seconds since star's epoch
    double elapsed = tmnow - J2000_TIME_T + 86400 * (J2000 - epoch);

    // Estimate RA and Decl using proper motion
    double l_RA = right_ascension + proper_motion_RA * elapsed;
    double l_Decl = declination + proper_motion_decl * elapsed;

    // Estimate distance using radial velocity
    double l_dist = distance + radial_velocity * elapsed;

    // Compute new location
    Point newloc = Point::from_ra_dec(l_RA, l_Decl, l_dist);

    // Set to galactic coordinates
    newloc = rotate3D(newloc, center, ICRF_to_galactic.v, ICRF_to_galactic.a);

    // Set system location
    location.system_center = newloc;
}

void Star::rename_from_Bayer_Flamsteed()
{
    if (!constellation.size()) return;
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
        if (!strcmp(consabbrev[i].c_str(), constellation.c_str()))
        {
            j = i;
            break;
        }
    }

    if (j<0)
    {
        // Not a valid constellation.
        constellation = "";
        return;
    }

    if (BayerGrkno >= 0)
    {
        int number = atoi(Bayer.substr(3, 1).c_str());
        if (number) name = Greek_letter[BayerGrkno] + std::string(" ") + std::to_string(number) + std::string(" ") + consgen[j];
        else name = Greek_letter[BayerGrkno] + std::string(" ") + consgen[j];
    }
    else if (FlamsteedNo)
    {
        name = std::to_string(FlamsteedNo) + std::string(" ") + consgen[j];
    }
}

bool Star::is_sunlike()
{
    const char* sptyp = spectral_type.c_str();
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
            if (!s1->Gliese.size()) continue;

            strcpy(name1, s1->Gliese.c_str());
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
                m = s2->Gliese.size();
                if (!m) continue;
                if (m < n) continue;
                if (s2->Gliese.c_str()[n] != ' ') continue;

                strcpy(name2, s2->Gliese.c_str());
                name2[n] = 0;
                if (!strcmp(name1, name2))
                {
                    s2->name = s1->name + std::string(" ") + &name2[n+1];
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
