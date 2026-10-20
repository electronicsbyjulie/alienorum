#ifndef ALIENORUM_HORIZON_TERRAIN_H
#define ALIENORUM_HORIZON_TERRAIN_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace alienorum
{
    struct HorizonTerrainVertex
    {
        double distance = 0;
        double bearing = 0;
        double elevation = 0;
    };

    struct ProjectedTerrainVertex
    {
        double x = 0;
        double y = 0;
        double inverse_depth = 0;
        double distance_over_depth = 0;
    };

    // A depth buffer for the terrain mesh; biome colors are sampled at pixel centers.
    class HorizonTerrainRaster
    {
        int width = 0, height = 0;
        std::vector<float> inverse_depth;
        std::vector<float> surface_distance;
        std::vector<int> column_top;

        public:
        void reset(int w, int h)
        {
            width = std::max(0, w);
            height = std::max(0, h);
            inverse_depth.assign(static_cast<size_t>(width) * height, 0);
            surface_distance.assign(inverse_depth.size(), 0);
            column_top.assign(width, height);
        }

        double distance_at(double x, double y) const
        {
            if (x < 0 || y < 0 || x >= width || y >= height)
            {
                return std::numeric_limits<double>::infinity();
            }
            const size_t index = static_cast<size_t>(static_cast<int>(y)) * width + static_cast<int>(x);
            return inverse_depth[index] > 0 ? surface_distance[index] : std::numeric_limits<double>::infinity();
        }

        int top_at(int x) const
        {
            return x >= 0 && x < width ? column_top[x] : height;
        }

        const std::vector<float> &distances() const { return surface_distance; }

        void store(int x, int y, double depth, double distance)
        {
            const size_t index = static_cast<size_t>(y) * width + x;
            if (depth > inverse_depth[index] && std::isfinite(distance) && distance > 0)
            {
                inverse_depth[index] = static_cast<float>(depth);
                surface_distance[index] = static_cast<float>(distance);
                column_top[x] = std::min(column_top[x], y);
            }
        }

        void triangle(const ProjectedTerrainVertex &a, const ProjectedTerrainVertex &b,
            const ProjectedTerrainVertex &c)
        {
            if (a.inverse_depth <= 0 || b.inverse_depth <= 0 || c.inverse_depth <= 0)
            {
                return;
            }
            const double determinant = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
            if (!std::isfinite(determinant) || fabs(determinant) < 1e-10)
            {
                return;
            }
            const double left = std::max(0.0, std::min(a.x, std::min(b.x, c.x)));
            const double right = std::min(static_cast<double>(width), std::max(a.x, std::max(b.x, c.x)));
            const double top = std::max(0.0, std::min(a.y, std::min(b.y, c.y)));
            const double bottom = std::min(static_cast<double>(height), std::max(a.y, std::max(b.y, c.y)));
            if (left >= right || top >= bottom)
            {
                return;
            }
            const double da_dx = (b.y - c.y) / determinant;
            const double db_dx = (c.y - a.y) / determinant;
            for (int y = static_cast<int>(top); y < static_cast<int>(ceil(bottom)); ++y)
            {
                const double py = y + 0.5;
                const int first_x = static_cast<int>(left);
                double wa = ((b.y - c.y) * (first_x + 0.5 - c.x) + (c.x - b.x) * (py - c.y)) / determinant;
                double wb = ((c.y - a.y) * (first_x + 0.5 - c.x) + (a.x - c.x) * (py - c.y)) / determinant;
                for (int x = first_x; x < static_cast<int>(ceil(right)); ++x, wa += da_dx, wb += db_dx)
                {
                    const double wc = 1.0 - wa - wb;
                    if (wa < -1e-9 || wb < -1e-9 || wc < -1e-9)
                    {
                        continue;
                    }
                    const double depth = wa * a.inverse_depth + wb * b.inverse_depth + wc * c.inverse_depth;
                    const size_t index = static_cast<size_t>(y) * width + x;
                    if (depth > inverse_depth[index])
                    {
                        const double distance = (wa * a.distance_over_depth + wb * b.distance_over_depth
                            + wc * c.distance_over_depth) / depth;
                        if (std::isfinite(distance) && distance > 0)
                        {
                            inverse_depth[index] = static_cast<float>(depth);
                            surface_distance[index] = static_cast<float>(distance);
                            column_top[x] = std::min(column_top[x], y);
                        }
                    }
                }
            }
        }
    };
}

#endif
