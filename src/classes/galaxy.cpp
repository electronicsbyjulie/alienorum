#include "galaxy.h"

Galaxy::Galaxy()
{
    _class = class_galaxy;
}

json Galaxy::to_json()
{
    json towrite = CelestialObject::to_json();
    // TODO: Galaxy-specific properties will go here.
    return towrite;
}

bool Galaxy::from_json(json j)
{
    CelestialObject::from_json(j);
    // Nothing to load... yet. Here's a blank template for future implementation:
    // try { j.at("").get_to(); } catch (...) { ; }
    return true;
}
