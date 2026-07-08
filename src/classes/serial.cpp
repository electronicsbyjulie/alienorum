
#include <iostream>
#include "serial.h"

using namespace alienorum;

int find_object(const char* search_term, bool os, double ml, int levreq)
{
    // TODO:
    // 1.) Completely rewrite this fuction to be more efficient and make fewer mistakes;
    // 2.) Print out the current version of this function pre-rewrite and BURN IT IN A FIRE;
    // 3.) Permanently delete every digital copy of the legacy version.

    int i, m, n, termi = atoi(search_term);
    Star *s;

    uint32_t is_hd  = ((search_term[0]&0x5f) == 'H' && (search_term[1]&0x5f) == 'D') ? atoi(&search_term[2]) : 0,
        is_hip = ((search_term[0]&0x5f) == 'H' && (search_term[1]&0x5f) == 'I' && (search_term[2]&0x5f) == 'P') ? atoi(&search_term[3]) : 0;
    int result = -1;
    const char *match_cons = nullptr;
    char match_comp = 0;
    n = strlen(search_term);
    if (n > 2 && search_term[n-2] == ' '
        && ((search_term[n-1] >= 'A' && search_term[n-1] <= 'Z') || (search_term[n-1] >= 'a' && search_term[n-1] <= 'z')))
        match_comp = search_term[n-1];

    if (is_hd && hdcache && hdcache[is_hd] && (os || (match_comp < 'a')))   // if comp is lower case and a star is not required, we might be looking for a planet.
    {
        s = hdcache[is_hd];
        if (match_comp && s->multisys)
        {
            Star *b = s->multisys->get_member(match_comp);
            if (b) s = b;
        }
        return s->seqno;
    }
    if (is_hip && hipcache && hipcache[is_hip] && (os || (match_comp < 'a')))
    {
        s = hipcache[is_hip];
        if (match_comp && s->multisys)
        {
            Star *b = s->multisys->get_member(match_comp);
            if (b) s = b;
        }
        return s->seqno;
    }
    if ((is_hd || is_hip) && os) return -1;

    bool is_gliese = (((search_term[0]&0x5f) == 'G' && (search_term[1]&0x5f) == 'L' && (search_term[2] <= '9'))
        || ((search_term[0]&0x5f) == 'G' && (search_term[1]&0x5f) == 'L' && (search_term[2]&0x5f) == 'I' && (search_term[3]&0x5f) == 'E' && (search_term[4]&0x5f) == 'S' && (search_term[5]&0x5f) == 'E')
        || ((search_term[0]&0x5f) == 'G' && (search_term[1]&0x5f) == 'J' && (search_term[2] <= '9'))
        || ((search_term[0]&0x5f) == 'W' && (search_term[1]&0x5f) == 'O' && (search_term[2] <= '9'))
        || ((search_term[0]&0x5f) == 'W' && (search_term[1]&0x5f) == 'O' && (search_term[2]&0x5f) == 'O' && (search_term[3]&0x5f) == 'L' && (search_term[4]&0x5f) == 'L' && (search_term[5]&0x5f) == 'E')
        || ((search_term[0]&0x5f) == 'N' && (search_term[1]&0x5f) == 'N' && (search_term[2] <= '9') && (search_term[3] <= '9'))
        ) && contains_digits_or_dots(search_term);

    if ((search_term[0]&0x5f) == 'W' && (search_term[1]&0x5f) == 'O' && (search_term[2]&0x5f) == 'L' && (search_term[3]&0x5f) == 'F')
        is_gliese = false;                      // AND IT COULDN'T BE MORE FALSE. WOLF != WOLLEY, GOT IT DISMAL COMPUTER?

    int co = n-3;
    if (match_comp) co -= 2;
    if (co>1 && search_term[co-1] <= '9' && ((search_term[co] >= 'A' && search_term[co] <= 'Z') || (search_term[co] >= 'a' && search_term[co] <= 'z'))
        && ((search_term[co+1] >= 'A' && search_term[co+1] <= 'Z') || (search_term[co+1] >= 'a' && search_term[co+1] <= 'z'))
        && ((search_term[co+2] >= 'A' && search_term[co+2] <= 'Z') || (search_term[co+2] >= 'a' && search_term[co+2] <= 'z'))
        )
        match_cons = &search_term[co];

    for (i=0; cels[i]; i++)
    {
        if (os && (cels[i]->typeclass() != class_star)) continue;
        if (!strcmp(cels[i]->name, search_term))
        {
            result = i;
            break;
        }
        if (match_cons && cels[i]->typeclass() == class_star
            && (    (((Star*)cels[i])->constellation[0] & 0x5f) != (match_cons[0] & 0x5f)
                ||  (((Star*)cels[i])->constellation[1] & 0x5f) != (match_cons[1] & 0x5f)
                ||  (((Star*)cels[i])->constellation[2] & 0x5f) != (match_cons[2] & 0x5f)
                )) continue;
        if (match_comp)
        {
            m = strlen(cels[i]->name);
            if (cels[i]->name[m-2] == ' ' && cels[i]->name[m-1] != match_comp) continue;
            if (match_comp != 'A' && cels[i]->name[m-2] != ' ') continue;
        }
        if (cels[i]->typeclass() == class_star)
        {
            if (((Star*)cels[i])->apparent_magnitude > ml) continue;
            if (is_gliese && has_same_numbers(((Star*)cels[i])->Gliese, search_term))
            {
                result = i;
                break;
            }
        }
    }
    if (is_gliese)
    {
        return result;
    }

    if (match_cons && termi)
    {
        // TODO: put Bayer search in here too.
        if (result < 0) for (i=0; cels[i]; i++)
        {
            s = (cels[i]->typeclass() == class_star) ? ((Star*)cels[i]) : nullptr;
            if (!s) continue;
            if (!s->matches_constellation(match_cons)) continue;
            if (termi == s->FlamsteedNo)
            {
                if (match_comp)
                {
                    m = strlen(cels[i]->name);
                    if (cels[i]->name[m-2] == ' ' && cels[i]->name[m-1] != match_comp) continue;
                    if (match_comp != 'A' && cels[i]->name[m-2] != ' ') continue;
                }
                result = i;
            }
        }
        if (result < 0) for (i=0; cels[i]; i++)
        {
            s = (cels[i]->typeclass() == class_star) ? ((Star*)cels[i]) : nullptr;
            if (!s) continue;
            if (!s->matches_constellation(match_cons)) continue;
            if (termi == s->GouldNo)
            {
                if (match_comp)
                {
                    m = strlen(cels[i]->name);
                    if (cels[i]->name[m-2] == ' ' && cels[i]->name[m-1] != match_comp) continue;
                    if (match_comp != 'A' && cels[i]->name[m-2] != ' ') continue;
                }
                result = i;
            }
        }

        if (os) return result;
    }

    char buffer[256];
    if (result < 0)
    {
        std::string lookstr = search_term;
        int looklen = strlen(search_term);
        for (i=0; cels[i]; i++)
        {
            s = (cels[i]->typeclass() == class_star) ? ((Star*)cels[i]) : nullptr;
            if (os && !s) continue;
            if (match_comp)
            {
                m = strlen(cels[i]->name);
                if (cels[i]->name[m-2] == ' ' && cels[i]->name[m-1] != match_comp) continue;
                if (match_comp != 'A' && cels[i]->name[m-2] != ' ') continue;
            }

            if (s && strlen(s->Gliese)                    // HOW MANY TIMES DO I HAVE TO BEAT THIS INTO YOU, COMPUTER.
                && (lookstr[0]&0x5f) == 'W' && (lookstr[1]&0x5f) == 'O' && (lookstr[2]&0x5f) == 'L' && (lookstr[3]&0x5f) == 'F'
                && has_same_numbers(s->Gliese, lookstr.c_str()))
                continue;

            strcpy(buffer, cels[i]->name);
            if (looklen > 0.666 * strlen(buffer)) buffer[looklen] = 0;
            int lev = Damerau_Levenshtein(buffer, lookstr);
            if (cels[i]->type == star)
            {
                if (!has_same_numbers(cels[i]->name, lookstr.c_str())) lev = 1e9;
                if (s)
                {
                    int lev1 = Damerau_Levenshtein( s->Bayer, lookstr);
                    if (!has_same_numbers(s->Bayer, lookstr.c_str())) lev1 = 1e9;
                    if (lev1 < lev) lev = lev1;
                    lev1 = Damerau_Levenshtein( s->Flamsteed, lookstr);
                    if (!has_same_numbers(s->Flamsteed, lookstr.c_str())) lev1 = 1e9;
                    if (lev1 < lev) lev = lev1;
                }
            }
            if (lev < levreq)
            {
                levreq = lev;
                result = i;
                if (!lev) break;
            }
        }
    }

    #if 0
    if (result >= 0)
        std::cout << search_term << " = " << cels[result]->name << " with lev " << levreq << std::endl << std::flush;
    else
        std::cout << "This steaming pile of a search function has dismally failed yet again to identify " <<
            search_term << " with a Damerau-Levenshtein requirement of " << levreq << "." << std::endl << std::flush;
    #endif

    return result;
}

bool Serialization::save_all(std::fstream& fs, CelestialObject **cels, bool oe)
{
    try
    {
        int i;
        json allobjs;
        for (i=0; cels[i]; i++)
        {
            std::string key = "";
            key = std::string(cels[i]->name);
            CelestialObject *cursor = cels[i];
            while (cursor->orbit && cursor->orbit->center)
            {
                cursor = cursor->orbit->center;
                key = std::string(cursor->name) + std::string(".") + key;
            }

            const char* l = key.c_str();
            if (oe && !cels[i]->user_edited) continue;
            switch (cels[i]->typeclass())
            {
                case class_galaxy:
                allobjs[l] = ((Galaxy*)cels[i])->to_json();
                break;

                case class_star:
                ((Star*)cels[i])->gotta_be_named_something();                            // I am sick of these massive-flaring stars with no massive-flaring names!
                allobjs[l] = ((Star*)cels[i])->to_json();
                break;

                case class_planet:
                allobjs[l] = ((Planet*)cels[i])->to_json();
                break;

                case class_moon:
                allobjs[l] = ((Moon*)cels[i])->to_json();
                break;

                case class_satellite:
                allobjs[l] = ((Satellite*)cels[i])->to_json();
                break;

                default:
                std::cerr << "Attempted to save CelestialObject " << cels[i]->name << " of blank or unknown type class." << std::endl;
            }
        }

        fs << allobjs.dump(4);
        return true;
    }
    catch (...)
    {
        std::cerr << "FAILED to save universe file." << std::endl;
        return false;
    }
}

bool Serialization::load_all(std::fstream& fs, CelestialObject **cels, unsigned int max)
{
    try
    {
        json allobj;
        fs >> allobj;
        int i, j, n = allobj.size();
        for (i=0; cels[i]; i++);
        ncelobjs = i;
        for (auto it = allobj.begin(); it != allobj.end(); ++it)
        {
            i = ncelobjs;
            json js = it.value();
            cel_obj_class c;
            js.at("typeclass").get_to(c);
            std::string name, origname, origcenname;
            js.at("!name").get_to(name);
            try { js.at("!origname").get_to(origname); } catch (...) { origname = name; }
            try { js.at("!origcenname").get_to(origcenname); } catch (...) { origcenname = std::to_string(ncelobjs+1000); }     // at least don't match anything
            const char *origname_c = origname.c_str(), *origcenname_c = origcenname.c_str();

            for (j=0; cels[j]; j++)
            {
                if (cels[j]->typeclass() != c) continue;
                if (strcmp(cels[j]->origname.c_str(), origname_c)) continue;
                if (strcmp(cels[j]->origcenname.c_str(), origcenname_c)) continue;
                i = j;
                break;
            }

            #ifdef DEBUG
            if (i != ncelobjs)
                std::cout << "Clobbering " << i << " " << cels[i]->name << " with " << name << ", originally " << origname << std::endl << std::flush;
            else std::cout << "Creating new " << i << " " << name << ", originally " << origname << std::endl << std::flush;
            #endif

            switch (c)
            {
                case class_galaxy:
                if (!cels[i]) cels[i] = new Galaxy();
                ((Galaxy*)cels[i])->from_json(js);
                break;

                case class_star:
                if (!cels[i]) cels[i] = new Star();
                ((Star*)cels[i])->from_json(js);
                ((Star*)cels[i])->update_location(J2000_TIME_T);
                break;

                case class_planet:
                if (!cels[i]) cels[i] = new Planet();
                ((Planet*)cels[i])->from_json(js);
                break;

                case class_moon:
                if (!cels[i]) cels[i] = new Moon();
                ((Moon*)cels[i])->from_json(js);
                ((Moon*)cels[i])->orbit_type = ot_equatorial;
                break;

                case class_satellite:
                if (!cels[i]) cels[i] = new Satellite();
                ((Satellite*)cels[i])->from_json(js);
                break;

                default:
                std::cerr << "Attempted to load celestial object of blank or unknown type class." << std::endl;
                return false;
            }

            cels[i]->user_edited = true;
            cels[i]->estimated_poles = true;
            cels[i]->seqno = i;

            mtx.lock();
            loading_msg = std::string("Loaded ") + std::to_string(i+1) + std::string(" of ") + std::to_string(n) + std::string(" objects...");
            mtx.unlock();

            if (!cels[i]->orbit) continue;
            const char* cenname = cels[i]->orbit->center_name.c_str();
            cels[i]->orbit->center = nullptr;
            for (j=0; cels[j]; j++)
            {
                if (j==i) continue;
                if (cels[i]->type < cels[j]->type) continue;                    // There are two objects named Atlas, and one of them has a companion.
                if (!strcmp(cenname, cels[j]->name) || !strcmp(cenname, cels[j]->origname.c_str()))
                {
                    cels[i]->orbit->center = cels[j];
                    break;
                }
            }
            if (!cels[i]->orbit->center)
                std::cout << "FAILED to place " << cels[i]->name << " in orbit around " << cenname << std::endl;

            if (cels[i]->typeclass() == class_planet || cels[i]->typeclass() == class_moon)
            {
                Star* s = (Star*) cels[i]->get_light_center();
                if (!s) std::cerr << "JSON data integrity error! " << cels[i]->name << " has no illumination star." << std::endl << std::flush;
                else
                {
                    s->has_planets++;
                    if (((Planet*)cels[i])->is_in_con_HZ()) s->has_hz_planets++;
                }
            }

            if (i==ncelobjs) ncelobjs++;
            if ((unsigned int)ncelobjs >= max-1) return false;                                // Avoid overflowing the array.
        }

        for (i=0; cels[i]; i++)
        {
            cel_obj_class c = cels[i]->typeclass();
            if (c == class_moon) ((Moon*)cels[i])->update_location(J2000_TIME_T);
            else if (c == class_planet) ((Planet*)cels[i])->update_location(J2000_TIME_T);
            else if (c == class_star) ((Star*)cels[i])->update_location(J2000_TIME_T);
        }

        return true;
    }
    catch (...)
    {
        #ifdef DEBUG
        assert(false);
        #else
        std::cerr << "FAILED to load universe: incorrectly formatted JSON." << std::endl;
        #endif
        return false;
    }
}
