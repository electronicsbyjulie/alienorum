
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
    char buffer[1+n];
    fread(buffer, sizeof(char), n, in);
    buffer[n] = 0;
    return std::string(buffer);
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

bool Serialization::load_all(std::fstream& fs, CelestialObject **cels, int max)
{
    try
    {
        json allobj;
        allobj << fs;
        int i, j, n = allobj.size();
        for (auto it = allobj.begin(); it != allobj.end(); ++it)
        {
            std::string key = it.key();
            i = atoi(key.c_str());
            json js = it.value();
            cel_obj_class c;
            js.at("typeclass").get_to(c);

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
