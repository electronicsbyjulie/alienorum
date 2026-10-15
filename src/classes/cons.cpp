#include <cmath>
#include <vector>
#include <unordered_set>
#include "cons.h"

int num_reg_cons = 0;
std::vector<Constellation> constellations;
Constellation *cons4lbl = nullptr;

// Helper to calculate true angular distance between two points on the celestial sphere
double get_angular_distance(const ConsBoundary& a, const ConsBoundary& b) 
{
    double d_ra = a.RA - b.RA;
    
    // Normalize the RA wrap-around seam so distances aren't exaggerated across 0 hours
    while (d_ra < -_pi) d_ra += 2.0 * _pi;
    while (d_ra > _pi) d_ra -= 2.0 * _pi;
    
    // Scale RA by the cosine of the average declination to get true physical distance
    double avg_dec = (a.decl + b.decl) / 2.0;
    d_ra *= cos(avg_dec);
    
    double d_dec = a.decl - b.decl;
    
    // Pythagorean distance using the scaled coordinates
    return sqrt(d_ra * d_ra + d_dec * d_dec);
}

// Run this ONCE per constellation after loading your catalog 
// to convert the raw data into a properly ordered line loop
void Constellation::build_constellation_perimeter()
{
    const std::vector<ConsBoundary>& raw_bounds = bounds;
    if (raw_bounds.size() < 3) return;

    std::vector<ConsBoundary> perimeter;
    std::vector<ConsBoundary> unvisited = raw_bounds;

    // Start anywhere (e.g., the first point in the list)
    perimeter.push_back(unvisited.front());
    unvisited.erase(unvisited.begin());

    RA_center = perimeter[0].RA;
    decl_center = perimeter[0].decl;
    int ra_dec_div = 2;
    double ra_dec_mul, ra_dec_mul_1;
    double new_ra, new_decl;

    // Walk from point to nearest point
    while (!unvisited.empty()) 
    {
        if (abort_load) return;
        const ConsBoundary& current = perimeter.back();

        auto nearest_it = unvisited.begin();
        double min_dist = get_angular_distance(current, *nearest_it);

        // Find the closest remaining vertex
        for (auto it = unvisited.begin() + 1; it != unvisited.end(); ++it) 
        {
            double dist = get_angular_distance(current, *it);
            if (dist < min_dist) 
            {
                min_dist = dist;
                nearest_it = it;
            }
        }

        // Add the nearest vertex to our perimeter and remove it from the unvisited list
        perimeter.push_back(*nearest_it);
        unvisited.erase(nearest_it);

        new_ra = perimeter.back().RA;
        new_decl = perimeter.back().decl;

        if (new_ra < RA_center - _pi) new_ra += _pi*2;
        else if (new_ra > RA_center + _pi) new_ra -= _pi*2;

        ra_dec_mul = 1.0/ra_dec_div;
        ra_dec_mul_1 = 1.0 - ra_dec_mul;
        RA_center = ra_dec_mul_1 * RA_center + ra_dec_mul * new_ra;
        decl_center = ra_dec_mul_1 * decl_center + ra_dec_mul * new_decl;
        ra_dec_div++;
    }

    bounds = perimeter;
}

// Helper algorithm to determine if a celestial point lies within a constellation polygon.
// Implemented using a robust spherical winding‑number technique.
// Robust spherical polygon inclusion test using angle summation (winding number).
// Returns true if the point (s_ra, s_decl) lies inside the polygon defined by bounds.
bool might_star_be_maybe_in_constellation(double s_ra, double s_decl, const std::vector<ConsBoundary>& bounds)
{
    // Convert the test point to a 3D unit vector.
    double cos_decl = cos(s_decl);
    double x0 = cos_decl * cos(s_ra);
    double y0 = cos_decl * sin(s_ra);
    double z0 = sin(s_decl);
    double sum_angle = 0.0;
    int n = bounds.size();
    if (n < 3) return false;

    for (int i = 0; i < n; ++i)
    {
        const ConsBoundary& a = bounds[i];
        const ConsBoundary& b = bounds[(i + 1) % n];
        // Convert vertices to unit vectors.
        double ca = cos(a.decl);
        double xb = ca * cos(a.RA);
        double yb = ca * sin(a.RA);
        double zb = sin(a.decl);

        double cb = cos(b.decl);
        double xc = cb * cos(b.RA);
        double yc = cb * sin(b.RA);
        double zc = sin(b.decl);

        // Vectors from point to the two vertices.
        double v1x = xb - x0;
        double v1y = yb - y0;
        double v1z = zb - z0;
        double v2x = xc - x0;
        double v2y = yc - y0;
        double v2z = zc - z0;

        // Normalize them.
        double len1 = sqrt(v1x * v1x + v1y * v1y + v1z * v1z);
        double len2 = sqrt(v2x * v2x + v2y * v2y + v2z * v2z);
        if (len1 == 0.0 || len2 == 0.0) continue;
        v1x /= len1; v1y /= len1; v1z /= len1;
        v2x /= len2; v2y /= len2; v2z /= len2;

        // Compute the angle between the two vectors.
        double dot = v1x * v2x + v1y * v2y + v1z * v2z;
        if (dot > 1.0) dot = 1.0; if (dot < -1.0) dot = -1.0;
        double angle = acos(dot);
        // Determine the sign of the angle using cross product direction.
        double cx = v1y * v2z - v1z * v2y;
        double cy = v1z * v2x - v1x * v2z;
        double cz = v1x * v2y - v1y * v2x;
        double sign = (cx * x0 + cy * y0 + cz * z0) >= 0.0 ? 1.0 : -1.0;
        sum_angle += sign * angle;
    }
    // Inside if absolute winding sum exceeds pi.
    return fabs(sum_angle) > M_PI;
}

Constellation *identify_cons_from_coords(double RA, double Decl)
{
    Constellation* result = nullptr;
    double best = 1e29;

    for (auto& cons : constellations) 
    {
        if (cons.bounds.empty()) continue;              // Safety check

        // Filter by constellation distance.
        // Calculate the RA distance between the star and the constellation center
        double d_ra = fabs(RA - cons.RA_center);
        if (d_ra > _pi) d_ra = 2.0 * _pi - d_ra; 

        // If the center is more than 90 degrees away,
        // it's impossible for the star to be inside it. Skip it.
        if (d_ra > half_pi) continue;

        if (might_star_be_maybe_in_constellation(RA, Decl, cons.bounds)) 
        {
            double d_decl = fabs(Decl - cons.decl_center);
            double r = sqrt(d_ra*d_ra+d_decl*d_decl);

            if (r < best)
            {
                result = &cons;
                best = r;
            }
        }
    }

    if (result) return result;

    // The Polar Fallback: If the star didn't fit into any closed polygon,
    // assume it must be in the polar caps where the 2D projection breaks down.

    // Positive declination = Northern Hemisphere -> Ursa Minor
    if (Decl > 0) 
    {
        for (auto& cons : constellations)
        {
            if (cons.name == "Ursa Minor" || cons.abbrev == "UMi") return &cons;
        }
    }
    // Negative declination = Southern Hemisphere -> Octans
    else 
    {
        for (auto& cons : constellations)
        {
            if (cons.name == "Octans" || cons.abbrev == "Oct") return &cons;
        }
    }
}

Constellation *identify_cons_of_star(Star *s)
{
    Constellation* result = nullptr;
    double best = 1e29;

    for (auto& cons : constellations) 
    {
        if (cons.bounds.empty()) continue;              // Safety check

        if (s->constellation && !strcasecmp(s->constellation, cons.abbrev.c_str())) return &cons;
        if (s->Gouldcons && !strcasecmp(s->Gouldcons, cons.abbrev.c_str())) return &cons;

        // Filter by constellation distance.
        // Calculate the RA distance between the star and the constellation center
        double d_ra = fabs(s->right_ascension - cons.RA_center);
        if (d_ra > _pi) d_ra = 2.0 * _pi - d_ra; 

        // If the center is more than 90 degrees away,
        // it's impossible for the star to be inside it. Skip it.
        if (d_ra > half_pi) continue;

        if (might_star_be_maybe_in_constellation(s->right_ascension, s->declination, cons.bounds)) 
        {
            double d_decl = fabs(s->declination - cons.decl_center);
            double r = sqrt(d_ra*d_ra+d_decl*d_decl);

            if (r < best)
            {
                result = &cons;
                best = r;
            }
        }
    }

    if (result) return result;

    // The Polar Fallback: If the star didn't fit into any closed polygon,
    // assume it must be in the polar caps where the 2D projection breaks down.

    // Positive declination = Northern Hemisphere -> Ursa Minor
    if (s->declination > 0) 
    {
        for (auto& cons : constellations)
        {
            if (cons.name == "Ursa Minor" || cons.abbrev == "UMi") return &cons;
        }
    }
    // Negative declination = Southern Hemisphere -> Octans
    else 
    {
        for (auto& cons : constellations)
        {
            if (cons.name == "Octans" || cons.abbrev == "Oct") return &cons;
        }
    }

    // If we reach this point, something is wrong.
    // assert(false);

    // For now...
    std::cerr << "ERROR: " << s->name << " decl=" << (s->declination*fiftyseven) << " failed to identify a constellation." << std::endl;

    return nullptr;
}

char get_color_code_from_temp(double tempK)
{
    if (tempK > 10000) return 'b';
    if (tempK >  7300) return 'c';
    if (tempK >  6000) return 'w';
    if (tempK >  5300) return 'y';
    if (tempK >  3900) return 'o';
    return 'r';
}

ConsBins fill_alienorum_ids()
{
    ConsBins bins;
    std::unordered_set<Star*> included;
    auto is_variable = [](const Star *star)
    {
        return star->variability_period && !star->is_eclipsing_binary
            && fabs(star->maxmag - star->minmag) > 0.5;
    };

    // Serpens Caput/Cauda are two disjoint boundary regions (SER1/SER2) but one constellation:
    // unify them under plain "Ser" so alienorumids count through Serpens as a single sequence.
    Constellation *ser = nullptr;
    for (auto& c : constellations)
    {
        if (c.abbrev == "Ser")
        {
            ser = &c;
            break;
        }
    }

    int i, j, n;
    for (i=0; cels[i]; i++)
    {
        if (cels[i]->deleted || cels[i]->typeclass() != class_star) continue;
        Star* s = (Star*)cels[i];
        if (!included.insert(s).second)
        {
            continue;
        }
        if (s->declination >= _pi) s->declination -= _pi*2;

        if (s->multisys)
        {
            Star* A = s->multisys->get_member('A');
            if (A && s != A) continue;                      // Leave components out of the serial numbering.
        }

        if (s->seqno == 0)
        {
            // The Sun sits at RA/Dec zero, which resolves to a real constellation (Pisces,
            // currently) rather than nowhere -- but the Sun isn't actually "in" that
            // constellation, it just has no fixed sky position of its own. Binning it there
            // used to get it written into the condensed star catalog a second time, once here
            // and once via the explicit cels[0] write in write_condensed_star_cat().
            int m = floor(fabs(s->apparent_magnitude)) * sgn(s->apparent_magnitude);
            s->alienorumid = std::to_string(m);          // The only -26 mag star from Earth, so this alone is unique.
            continue;
        }

        Constellation *cs = identify_cons_of_star(s);
        if (!cs) continue;

        if (ser && (cs->abbrev == "Ser1" || cs->abbrev == "Ser2")) cs = ser;

        const double magnitude = s->apparent_magnitude;
        int m = std::isfinite(magnitude) && magnitude > -31 && magnitude < 51
            ? static_cast<int>(magnitude) : 20;
        char cc = get_color_code_from_temp(s->estimate_temperature());

        if (bins.find(cs) != bins.end()
         && bins[cs].find(m) != bins[cs].end()
         && bins[cs][m].find(cc) != bins[cs][m].end()
         && bins[cs][m][cc].size())
        {
            bool added = false;
            n = bins[cs][m][cc].size();
            for (j=0; j<n; j++)
            {
                if (s->apparent_magnitude < bins[cs][m][cc][j]->apparent_magnitude - 0.00001
                    || ( fabs(s->apparent_magnitude - bins[cs][m][cc][j]->apparent_magnitude) < 0.00001
                        && s->right_ascension < bins[cs][m][cc][j]->right_ascension)
                    )
                {
                    bins[cs][m][cc].insert(bins[cs][m][cc].begin()+j, s);
                    added = true;
                    break;
                }
            }

            if (!added) bins[cs][m][cc].push_back(s);
        }
        else
        {
            bins[cs][m][cc].push_back(s);
        }
    }

    for (const auto& [cs, val1] : bins)
    {
        // Count V IDs across every magnitude bin while retaining ordinary numbering.
        std::map<char, int> variable_counts, variable_sequences;
        for (const auto& [m, val2] : val1)
        {
            for (const auto& [cc, stars] : val2)
            {
                for (Star *star : stars)
                {
                    if (is_variable(star))
                    {
                        variable_counts[cc]++;
                    }
                }
            }
        }
        for (const auto& [m, val2] : val1)
        {
            for (const auto& [cc, val3] : val2)
            {
                n = val3.size();
                for (i=0; i<n; i++)
                {
                    Star *s = val3[i];
                    const bool variable = is_variable(s);
                    const int sequence = variable ? ++variable_sequences[cc] : i + 1;
                    const int count = variable ? variable_counts[cc] : n;
                    s->alienorumid =
                        (variable ? std::string("V") : std::to_string(m))
                        + std::string(1, cc)
                        + std::string(" ")
                        + cs->abbrev
                        + std::string(" ")
                        + (count > 1 ? std::to_string(sequence) : std::string(""))
                        ;

                    if (s->multisys)
                    {
                        char comp;
                        for (comp = 'B'; comp <= 'Z'; comp++)
                        {
                            Star *B = s->multisys->get_member(comp);
                            if (B)
                            {
                                B->alienorumid = lop_component(s->alienorumid.c_str()) + std::string(" ") + std::string(1, B->get_component());
                            }
                        }
                    }
                }
            }
        }
    }

    return bins;
}
