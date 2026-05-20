
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

bool Serialization::save_object(FILE *of, CelestialObject *cel)
{
    cel_obj_class _class = cel->typeclass();
    fwrite(&_class, sizeof(cel_obj_class), 1, of);
    switch (cel->typeclass())
    {
        case class_galaxy:
        fwrite(cel, sizeof(Galaxy), 1, of);
        break;

        case class_star:
        ((Star*)cel)->gotta_be_named_something();                                   // I am sick of these massive-flaring stars with no massive-flaring names!
        fwrite(cel, sizeof(Star), 1, of);
        break;

        case class_planet:
        fwrite(cel, sizeof(Planet), 1, of);
        break;

        case class_moon:
        fwrite(cel, sizeof(Moon), 1, of);
        break;

        default:
        std::cerr << "Attempted to write CelestialObject of unknown class." << std::endl;
        throw 0xbadc0de;
    }
    if (cel->orbit)
    {
        std::string cenname = cel->orbit->center->name;
        save_string(of, cenname);
        fwrite(cel->orbit, sizeof(Orbit), 1, of);
    }
    return true;
}

bool Serialization::save_all(FILE *of, CelestialObject **cels)
{
    __uint32_t ver = _serial_version, n;
    fwrite(&ver, sizeof(__uint32_t), 1, of);
    int i, pass;

    for (n=0; cels[n]; n++);
    fwrite(&n, sizeof(__uint32_t), 1, of);

    for (pass=0; pass<2; pass++) for (i=0; i<n; i++)
    {
        if (!pass && cels[i]->orbit) continue;
        if (pass && !cels[i]->orbit) continue;
        if (!save_object(of, cels[i])) return false;
    }
    return true;
}

CelestialObject* Serialization::load_object(FILE *in, CelestialObject **cfocl)
{
    cel_obj_class typeclass;
    fread(&typeclass, sizeof(cel_obj_class), 1, in);
    CelestialObject *cel = nullptr;

    std::string cenname;
    Galaxy *g;
    Star *s;
    Planet *p;
    switch (typeclass)
    {
        case class_galaxy:
        g = new Galaxy();
        cel = g;
        fread(g, sizeof(Galaxy), 1, in);
        // std::cout << cel->name << " is a galaxy." << std::endl;
        break;

        case class_star:
        s = new Star();
        cel = s;
        fread(s, sizeof(Star), 1, in);
        s->gotta_be_named_something();
        // std::cout << cel->name << " is a star." << std::endl;
        break;

        case class_planet:
        p = new Planet();
        cel = p;
        fread(p, sizeof(Planet), 1, in);
        // std::cout << cel->name << " is a planet." << std::endl;
        break;

        case class_moon:
        p = new Moon();
        cel = p;
        fread(p, sizeof(Moon), 1, in);
        // std::cout << cel->name << " is a moon." << std::endl;
        break;

        default:
        std::cerr << "Attempted to read CelestialObject of unknown class." << std::endl;
        throw 0xbadc0de;
    }

    // Pointer loaded from file is bad, just check that it's nonzero.
    if (cel->orbit)
    {
        // Ignore loaded pointer and make a new one.
        cel->orbit = new Orbit();
        cenname = load_string(in);
        fread(cel->orbit, sizeof(Orbit), 1, in);
        cel->orbit->center = nullptr;
        int i;
        for (i=0; cfocl[i]; i++)
        {
            if (!strcmp(cenname.c_str(), cfocl[i]->name))
            {
                cel->orbit->center = cfocl[i];
                break;
            }
        }
        if (!cel->orbit->center)
        {
            std::cerr << "FAILED to place " << cel->name << " in orbit around " << cenname << std::endl;
            delete cel->orbit;
            delete cel;
            return nullptr;
        }
    }

    return cel;
}

bool Serialization::load_all(FILE *fp, CelestialObject **cels, int max)
{
    __uint32_t ver, n;
    fread(&ver, sizeof(__uint32_t), 1, fp);
    if (ver != _serial_version)
    {
        std::cerr << "Cannot restore state: file version mismatch." << std::endl;
        return false;
    }
    int i=0;
    fread(&n, sizeof(__uint32_t), 1, fp);
    if (n >= max)
    {
        std::cerr << "Cannot restore state: too many objects." << std::endl;
        throw 0xbadda7a;
    }
    while (i<n && !feof(fp))
    {
        CelestialObject* cel = load_object(fp, cels);
        if (feof(fp)) break;
        if (!(cels[i] = cel)) return false;
        i++;
    }
    cels[i] = nullptr;
    return true;
}
