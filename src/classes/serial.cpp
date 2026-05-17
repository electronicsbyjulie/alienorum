
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
    fwrite(&cel->type, sizeof(cel_obj_type), 1, of);
    switch (cel->type)
    {
        case galaxy:
        fwrite(cel, sizeof(Galaxy), 1, of);
        break;

        case star:
        fwrite(cel, sizeof(Star), 1, of);
        break;

        case rocky: case ice_giant: case gas_giant:
        fwrite(cel, sizeof(Planet), 1, of);
        break;

        default:
        fwrite(cel, sizeof(CelestialObject), 1, of);
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
    __uint32_t ver = _serial_version;
    fwrite(&ver, sizeof(__uint32_t), 1, of);
    int i, pass;
    for (pass=0; pass<2; pass++) for (i=0; cels[i]; i++)
    {
        if (!pass && cels[i]->orbit) continue;
        if (pass && !cels[i]->orbit) continue;
        if (!save_object(of, cels[i])) return false;
    }
    return true;
}

CelestialObject* Serialization::load_object(FILE *in, CelestialObject **cfocl)
{
    cel_obj_type type;
    fread(&type, sizeof(cel_obj_type), 1, in);
    CelestialObject *cel = nullptr;

    std::string cenname;
    Galaxy *g;
    Star *s;
    Planet *p;
    switch (type)
    {
        case galaxy:
        g = new Galaxy();
        cel = g;
        fread(g, sizeof(Galaxy), 1, in);
        break;

        case star:
        s = new Star();
        cel = s;
        fread(s, sizeof(Star), 1, in);
        break;

        case rocky: case gas_giant: case ice_giant:
        p = new Planet();
        cel = p;
        fread(p, sizeof(Planet), 1, in);
        break;

        default:
        cel = new CelestialObject();
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
    __uint32_t ver;
    fread(&ver, sizeof(__uint32_t), 1, fp);
    if (ver > _serial_version)
    {
        std::cerr << "Cannot restore state: file version too new." << std::endl;
        return false;
    }
    int i=0;
    while (!feof(fp))
    {
        CelestialObject* cel = load_object(fp, cels);
        if (feof(fp)) break;
        if (!(cels[i] = cel)) return false;
        i++;
        if (i >= max)
        {
            std::cerr << "Cannot restore state: file too big." << std::endl;
            throw 0xbadda7a;
        }
    }
    cels[i] = nullptr;
    return true;
}
