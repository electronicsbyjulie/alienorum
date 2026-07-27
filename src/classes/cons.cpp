#include <cmath>
#include <vector>
#include "cons.h"

std::vector<Constellation> constellations;

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
            if (intersect_ra > 0.0) 
            {
                inside = !inside;
            }
        }
    }

    return inside;
}

Constellation* identify_cons_of_star(Star* s) 
{
    for (auto& cons : constellations) 
    {
        if (cons.bounds.empty()) continue; // Safety check

        if (is_star_in_constellation(s->right_ascension, s->declination, cons.bounds)) 
        {
            return &cons;
        }
    }

    // If we reach this point, something is wrong.
    assert(false);

    return nullptr;
}