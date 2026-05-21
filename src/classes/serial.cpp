
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

bool Serialization::save_all(std::fstream& fs, CelestialObject **cels)
{
    try
    {
        int i;
        json allobjs;
        for (i=0; cels[i]; i++)
        {
            switch (cels[i]->typeclass())
            {
                case class_galaxy:
                allobjs[i] = ((Galaxy*)cels[i])->to_json();
                break;

                case class_star:
                ((Star*)cels[i])->gotta_be_named_something();                            // I am sick of these massive-flaring stars with no massive-flaring names!
                allobjs[i] = ((Star*)cels[i])->to_json();
                break;

                case class_planet:
                allobjs[i] = ((Planet*)cels[i])->to_json();
                break;

                case class_moon:
                allobjs[i] = ((Moon*)cels[i])->to_json();
                break;

                default:
                std::cerr << "Attempted to save CelestialObject of blank or unknown type class." << std::endl;
            }
        }

        fs << allobjs;
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
    return false;
}
