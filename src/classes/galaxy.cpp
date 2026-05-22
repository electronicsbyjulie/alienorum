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
    // Nothing to load... yet. See celestial.cpp for template blanks.
    return true;
}
