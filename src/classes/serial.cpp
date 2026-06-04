
#include <iostream>
#include "serial.h"

bool Serialization::save_string(FILE *of, std::string str)
{
    __uint8_t n = str.size();
    fwrite(&n, sizeof(__uint8_t), 1, of);
    fwrite(str.c_str(), sizeof(char), n, of);
    return true;
}

std::string Serialization::load_string(FILE *in)
{
    __uint8_t n;
    fread(&n, sizeof(__uint8_t), 1, in);
    auto buffer = std::make_unique<char[]>(n+1);
    fread(buffer.get(), sizeof(char), n, in);
    buffer[n] = 0;
    std::string ret(buffer.get());
    return ret;
}

int find_object(const char* search_term, bool os)
{
    int i, n;
    __uint32_t is_hd  = ((search_term[0]&0x5f) == 'H' && (search_term[1]&0x5f) == 'D') ? atoi(&search_term[2]) : 0,
        is_hip = ((search_term[0]&0x5f) == 'H' && (search_term[1]&0x5f) == 'I' && (search_term[2]&0x5f) == 'P') ? atoi(&search_term[3]) : 0;
    bool is_gliese = (((search_term[0]&0x5f) == 'G' && (search_term[1]&0x5f) == 'L')
        || ((search_term[0]&0x5f) == 'G' && (search_term[1]&0x5f) == 'J')
        || ((search_term[0]&0x5f) == 'W' && (search_term[1]&0x5f) == 'O')
        || ((search_term[0]&0x5f) == 'N' && (search_term[1]&0x5f) == 'N')
        ) && contains_digits_or_dots(search_term);
    int result = -1;
    const char *match_cons = nullptr;
    n = strlen(search_term);
    if (n>4 && search_term[n-4] == ' ' && search_term[n-3] >= 'A' && search_term[n-3] <= 'Z'
        && ((search_term[n-2] >= 'A' && search_term[n-2] <= 'Z') || (search_term[n-2] >= 'a' && search_term[n-2] <= 'z'))
        && ((search_term[n-1] >= 'A' && search_term[n-1] <= 'Z') || (search_term[n-1] >= 'a' && search_term[n-1] <= 'z'))
        )
        match_cons = &search_term[n-3];

    for (i=0; cels[i]; i++)
    {
        if (os && (cels[i]->typeclass() != class_star)) continue;
        if (match_cons && cels[i]->typeclass() == class_star && strcmp(((Star*)cels[i])->constellation, match_cons)) continue;
        if (!strcmp(cels[i]->name, search_term))
        {
            result = i;
            break;
        }
        if (cels[i]->typeclass() == class_star)
        {
            if ((is_hd && is_hd == ((Star*)cels[i])->HD)
                || (is_hip && is_hip == ((Star*)cels[i])->HIP))
            {
                result = i;
                break;
            }
            if (is_gliese && has_same_numbers(((Star*)cels[i])->Gliese, search_term))
            {
                result = i;
                break;
            }
        }
    }

    if (result < 0)
    {
        int best_Levenshtein = 1e6;
        std::string lookstr = search_term;
        for (i=0; cels[i]; i++)
        {
            if (os && (cels[i]->typeclass() != class_star)) continue;
            if (match_cons && cels[i]->typeclass() == class_star && strcmp(((Star*)cels[i])->constellation, match_cons)) continue;
            int lev = Damerau_Levenshtein(cels[i]->name, lookstr);
            if (!has_same_numbers(cels[i]->name, lookstr.c_str())) lev = 1e9;
            if (cels[i]->type == star)
            {
                int lev1 = Damerau_Levenshtein( ((Star*)cels[i])->Bayer, lookstr);
                if (!has_same_numbers(((Star*)cels[i])->Bayer, lookstr.c_str())) lev1 = 1e9;
                if (lev1 < lev) lev = lev1;
                lev1 = Damerau_Levenshtein( ((Star*)cels[i])->Flamsteed, lookstr);
                if (!has_same_numbers(((Star*)cels[i])->Flamsteed, lookstr.c_str())) lev1 = 1e9;
                if (lev1 < lev) lev = lev1;
            }
            if (lev < best_Levenshtein)
            {
                best_Levenshtein = lev;
                result = i;
                if (!lev) break;
            }
        }
    }

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
            std::string j = std::to_string(i);
            const char* l = j.c_str();
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
        for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);
        for (auto it = allobj.begin(); it != allobj.end(); ++it)
        {
            std::string key = it.key();
            i = atoi(key.c_str());
            if (i>ncelobjs) i = ncelobjs;
            json js = it.value();
            cel_obj_class c;
            js.at("typeclass").get_to(c);
            std::string name;
            js.at("!name").get_to(name);

            if (i<ncelobjs && strcmp(cels[i]->name, name.c_str()))
            {
                j = find_object(name.c_str(), c == class_star);
                if (j >= 0 && cels[j]->typeclass() == c) i = j;
            }

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
                break;

                default:
                std::cerr << "Attempted to load celestial object of blank or unknown type class." << std::endl;
                return false;
            }

            cels[i]->user_edited = true;

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
                if (!strcmp(cenname, cels[j]->name))
                {
                    cels[i]->orbit->center = cels[j];
                    break;
                }
            }
            if (!cels[i]->orbit->center) std::cout << "FAILED to place " << cels[i]->name << " in orbit around " << cenname << std::endl;

            if (cels[i]->typeclass() == class_planet || cels[i]->typeclass() == class_moon)
            {
                ((Star*)cels[i]->get_light_center())->has_planets++;
                if (((Planet*)cels[i])->is_in_con_HZ()) ((Star*)cels[i]->get_light_center())->has_hz_planets++;
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
        std::cerr << "FAILED to load universe: incorrectly formatted JSON." << std::endl;
        return false;
    }
}
