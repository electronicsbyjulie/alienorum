#include <cmath>
#include "comet.h"

using namespace alienorum;

std::vector<CometRow> comets;

alienorum::Comet::Comet()
{
    _class = class_comet; type = icy_tailed;
}

void alienorum::Comet::update_location(double tmnow)
{
    if (orbit) update_orbit_location(tmnow);
}

// The comet's total magnitude -- nucleus and coma together, which is what an eye or a wide-field
// photograph sees and what the catalog's H1/R1/D1 describe. Falls back to the nucleus parameters
// for a comet the catalog has no total light curve for, and to a middling made-up comet for one
// it has neither set for, of which there are a few hundred in the file.
double alienorum::Comet::viewer_comet_magnitude(CelestialLocation seen_from)
{
    CelestialObject *light_center = get_light_center();
    if (!light_center) return 99;

    double r = location.distance_to(light_center->location) * invAU;
    double d = seen_from.distance_to(location) * invAU;

    // Right on top of either one the logarithms run away; nothing sensible is being asked at that
    // point anyway, so clamp at a hundredth of an AU, comfortably inside any nucleus's coma.
    if (r < 0.01) r = 0.01;
    if (d < 0.01) d = 0.01;

    double h = H1, sr = R1, sd = D1;
    if (!h && !sr) { h = H2; sr = R2; sd = D2; }
    if (!h && !sr) { h = 8.0; sr = 10.0; sd = 5.0; }
    if (!sd) sd = 5.0;                                  // Inverse square in the viewer's distance, the one part of this that is pure geometry.

    return h + sr * std::log10(r) + sd * std::log10(d);
}

json alienorum::Comet::to_json()
{
    json towrite = CelestialObject::to_json();

    if (designation.size()) towrite["designation"] = designation;
    towrite["H1"] = H1;
    towrite["R1"] = R1;
    towrite["D1"] = D1;
    towrite["H2"] = H2;
    towrite["R2"] = R2;
    towrite["D2"] = D2;

    return towrite;
}

bool alienorum::Comet::from_json(json j)
{
    CelestialObject::from_json(j);

    try { j.at("designation").get_to(designation); } catch (...) { ; }
    try { j.at("H1").get_to(H1); } catch (...) { ; }
    try { j.at("R1").get_to(R1); } catch (...) { ; }
    try { j.at("D1").get_to(D1); } catch (...) { ; }
    try { j.at("H2").get_to(H2); } catch (...) { ; }
    try { j.at("R2").get_to(R2); } catch (...) { ; }
    try { j.at("D2").get_to(D2); } catch (...) { ; }

    return true;
}
