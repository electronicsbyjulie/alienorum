#include <cmath>
#include <vector>
#include "cons.h"

std::vector<Constellation> constellations;

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

    // Walk from point to nearest point
    while (!unvisited.empty()) 
    {
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
    }

    bounds = perimeter;
}

// Helper algorithm to handle Point-In-Polygon on the celestial sphere
bool is_star_in_constellation(double s_ra, double s_decl, const std::vector<ConsBoundary>& bounds) 
{
    bool inside = false;
    int n = bounds.size();

    // Loop through each edge of the polygon
    for (int i = 0, j = n - 1; i < n; j = i++) 
    {
        double decl_i = bounds[i].decl;
        double decl_j = bounds[j].decl;

        // 1. Check if the star's declination falls within the vertical range of this edge
        if ((decl_i > s_decl) != (decl_j > s_decl)) 
        {
            // 2. Shift the RA of the boundary points so the star is at relative RA = 0
            double ra_i = bounds[i].RA - s_ra;
            double ra_j = bounds[j].RA - s_ra;

            if (ra_i > _pi && ra_j > _pi) continue;

            // 3. Normalize the relative RA values to range [-pi, pi]
            while (ra_i <= -_pi) ra_i += 2.0 * _pi;
            while (ra_i >   _pi) ra_i -= 2.0 * _pi;
            while (ra_j <= -_pi) ra_j += 2.0 * _pi;
            while (ra_j >   _pi) ra_j -= 2.0 * _pi;

            // 4. Handle edges that cross the anti-meridian in our local coordinate system
            if (ra_j - ra_i > _pi) ra_j -= 2.0 * _pi;
            else if (ra_i - ra_j > _pi) ra_i -= 2.0 * _pi;

            // 5. Interpolate to find the exact RA intersection point on the edge
            double intersect_ra = ra_i + (s_decl - decl_i) / (decl_j - decl_i) * (ra_j - ra_i);

            // 6. Ray-cast check: If the intersection is in the positive RA direction, count it
            if (intersect_ra > 0.0 && intersect_ra < 2.0) 
            {
                inside = !inside;
            }
        }
    }

    return inside;
}

Constellation* identify_cons_of_star(Star* s) 
{
    if (s->declination >= _pi) s->declination -= _pi*2;
    for (auto& cons : constellations) 
    {
        if (cons.bounds.empty()) continue;              // Safety check

        // Filter by constellation distance.
        // Calculate the RA distance between the star and the constellation center
        double d_ra = fabs(s->right_ascension - cons.RA_center);
        if (d_ra > _pi) d_ra = 2.0 * _pi - d_ra; 

        // If the center is more than ~114 degrees (2.0 radians) away,
        // it's impossible for the star to be inside it. Skip it.
        if (d_ra > 2.0) continue;

        if (is_star_in_constellation(s->right_ascension, s->declination, cons.bounds)) 
        {
            return &cons;
        }
    }

    // The Polar Fallback: If the star didn't fit into any closed polygon,
    // assume it must be in the polar caps where the 2D projection breaks down.

    // Positive declination = Northern Hemisphere -> Ursa Minor
    if (s->declination > 0) 
    {
        for (auto& cons : constellations) {
            if (cons.name == "Ursa Minor" || cons.abbrev == "UMi") return &cons;
        }
    }
    // Negative declination = Southern Hemisphere -> Octans
    else 
    {
        for (auto& cons : constellations) {
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

void fill_alienorum_ids()
{
    std::map<Constellation*, std::map<int, std::map<char, std::vector<Star*>>>> bins;

    int i, j, n;
    for (i=0; cels[i]; i++)
    {
        if (cels[i]->typeclass() != class_star) continue;
        Star* s = (Star*)cels[i];

        if (s->multisys)
        {
            Star* A = s->multisys->get_member('A');
            if (A && s != A)
            {
                s->alienorumid = lop_component(A->alienorumid.c_str()) + std::string(" ") + std::string(1, s->get_component());
                continue;
            }
        }

        Constellation *cs = identify_cons_of_star(s);
        if (!cs) continue;
        int m = floor(fabs(s->apparent_magnitude)) * sgn(s->apparent_magnitude);
        char cc = s->spectral_type[0];
        if (cc < 'A' || cc > 'Z') cc = s->spectral_type[1];
        if (cc == 'O' || cc == 'B') cc = 'b';
        else if (cc == 'A') cc = 'c';
        else if (cc == 'F') cc = 'w';
        else if (cc == 'G') cc = 'y';
        else if (cc == 'K') cc = 'o';
        else if (cc == 'M' || cc == 'C' || cc == 'T') cc = 'r';
        else cc = get_color_code_from_temp(s->estimate_temperature());

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
        for (const auto& [m, val2] : val1)
        {
            for (const auto& [cc, val3] : val2)
            {
                n = val3.size();
                for (i=0; i<n; i++)
                {
                    val3[i]->alienorumid = cs->abbrev + std::to_string(m) + std::string(1, cc) + std::to_string(i+1);
                }
            }
        }
    }
}