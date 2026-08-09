#include "galaxy.h"
#include <string.h>

using namespace alienorum;

Galaxy::Galaxy()
{
    _class = class_galaxy;
    type = galaxy;
    memset(morph_type, 0, sizeof(morph_type));
}

json Galaxy::to_json()
{
    json towrite = CelestialObject::to_json();

    if (T_known) towrite["morphological_T"] = morphological_T;
    if (angular_diameter) towrite["angular_diameter"] = angular_diameter;
    if (axis_ratio != 1) towrite["axis_ratio"] = axis_ratio;
    if (position_angle_known) towrite["position_angle"] = position_angle;
    if (inclination) towrite["inclination"] = inclination;
    if (apparent_magnitude) towrite["apparent_magnitude"] = apparent_magnitude;
    if (radial_velocity) towrite["radial_velocity"] = radial_velocity;
    if (PGC) towrite["PGC"] = PGC;
    if (strlen(morph_type)) towrite["morph_type"] = morph_type;

    return towrite;
}

bool Galaxy::from_json(json j)
{
    CelestialObject::from_json(j);

    try { j.at("morphological_T").get_to(morphological_T); T_known = true; } catch (...) { ; }
    try { j.at("angular_diameter").get_to(angular_diameter); } catch (...) { ; }
    try { j.at("axis_ratio").get_to(axis_ratio); } catch (...) { ; }
    try { j.at("position_angle").get_to(position_angle); position_angle_known = true; } catch (...) { ; }
    try { j.at("inclination").get_to(inclination); } catch (...) { ; }
    try { j.at("apparent_magnitude").get_to(apparent_magnitude); } catch (...) { ; }
    try { j.at("radial_velocity").get_to(radial_velocity); } catch (...) { ; }
    try { j.at("PGC").get_to(PGC); } catch (...) { ; }
    try
    {
        std::string s;
        j.at("morph_type").get_to(s);
        snprintf(morph_type, sizeof(morph_type), "%s", s.c_str());
    } catch (...) { ; }

    return true;
}

int alienorum::GalaxyBand::load_dat_file(std::string fname)
{
    FILE *fp = fopen(fname.c_str(), "r");
    if (!fp) return 0;

    int num_read = 0;
    char buffer[256], northsouth = 0, *comma = nullptr;
    while (fgets(buffer, 255, fp))
    {
        if (buffer[0] == '#') continue;
        else if (buffer[0] == 'N' || buffer[0] == 'S') northsouth = buffer[0];
        else if (comma = strchr(buffer, ','))   // conditioned on assignment
        {
            if (northsouth == 'N')
            {
                road1_gra.push_back(atof(buffer));
                road1_gdecl.push_back(atof(&comma[1]));
                num_read++;
            }
            else if (northsouth == 'S')
            {
                road2_gra.push_back(atof(buffer));
                road2_gdecl.push_back(atof(&comma[1]));
                num_read++;
            }
        }
    }

    return num_read;
}
