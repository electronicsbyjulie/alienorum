#ifndef ALIENORUM_GALAXY_GEOMETRY_H
#define ALIENORUM_GALAXY_GEOMETRY_H
#include "galaxy.h"
#include <utility>

namespace alienorum
{
    // Inertial image axes: an observer changes the projection, never the galaxy's disk plane.
    inline std::pair<Point, Point> galaxy_disk_axes(const Galaxy& galaxy)
    {
        const auto unit = [](Point point)
        {
            const double length = point.magnitude();
            return length > 1e-15 ? point*(1/length) : Point(1, 0, 0);
        };
        if (galaxy.reference_galaxy)
        {
            const Point pole = unit(Point::from_ra_dec(galactic_north_RA_J2000, galactic_north_Decl_J2000, 1));
            Point radial = unit(Point::from_ra_dec(galactic_center_RA_J2000, galactic_center_Decl_J2000, 1));
            const double along_pole = radial.x*pole.x+radial.y*pole.y+radial.z*pole.z;
            radial = unit(radial-pole*along_pole);
            return {radial, unit(compute_normal(center, radial, pole))};
        }
        const Point line = unit(Point::from_ra_dec(galaxy.right_ascension, galaxy.declination, 1));
        const Point east = unit(compute_normal(center, line, yaxis));
        const Point north = unit(compute_normal(center, east, line));
        const Point major = north*cos(galaxy.position_angle)+east*sin(galaxy.position_angle);
        const Point perpendicular = unit(compute_normal(center, line, major));
        return {unit(major*-1), unit((perpendicular*cos(galaxy.inclination+_pi)
            +line*sin(galaxy.inclination+_pi))*-1)};
    }
}
#endif
