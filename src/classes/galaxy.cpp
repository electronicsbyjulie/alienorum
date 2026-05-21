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
