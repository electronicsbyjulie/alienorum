#ifndef ALIENORUM_HORIZON_TERRAIN_H
#define ALIENORUM_HORIZON_TERRAIN_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <array>

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

    struct HorizonTerrainSector
    {
        std::array<double, 5> maximum;
        double scale = 1;
        bool uncertain = false;
        HorizonTerrainSector() { maximum.fill(-std::numeric_limits<double>::infinity()); }
        void include(double x, double y, double z, double horizontal, double vertical)
        {
            const double planes[] = {z - .1, x + z * horizontal, z * horizontal - x,
                y + z * vertical, z * vertical - y};
            // Include the rasterizer's barycentric tolerance and arithmetic roundoff.
            const double magnitude = 1 + fabs(x) + fabs(y) + fabs(z)
                * (1 + fabs(horizontal) + fabs(vertical));
            scale = std::max(scale, magnitude);
            for (int plane = 0; plane < 5; ++plane)
            {
                if (!std::isfinite(planes[plane]) || !std::isfinite(magnitude)) uncertain = true;
                else maximum[plane] = std::max(maximum[plane], planes[plane]);
            }
        }
        bool outside(const HorizonTerrainSector& other) const
        {
            if (uncertain || other.uncertain) return false;
            const double margin = 1e-8 * std::max(scale, other.scale);
            for (int plane = 0; plane < 5; ++plane)
                if (maximum[plane] < -margin && other.maximum[plane] < -margin) return true;
            return false;
        }
    };

    // A depth buffer for the terrain mesh; biome colors are sampled at pixel centers.
    class HorizonTerrainRaster
    {
        int width = 0, height = 0;
        bool tile_metadata = true;
        std::vector<float> inverse_depth;
        std::vector<float> surface_distance;
        std::vector<int> column_top;
        static constexpr int depth_tile_size = 16;
        int depth_columns = 0;
        std::vector<unsigned short> filled, updates;
        std::vector<float> lower_depth;

        int tile_area(int tx, int ty) const
        {
            return std::min(depth_tile_size, width - tx * depth_tile_size)
                * std::min(depth_tile_size, height - ty * depth_tile_size);
        }
        void update_depth_tile(int x, int y, float before, float after)
        {
            const int tx = x / depth_tile_size, ty = y / depth_tile_size;
            const size_t tile = ty * depth_columns + tx;
            const int count = tile_area(tx, ty);
            if (!(before > 0) && after > 0)
            {
                if (++filled[tile] == count) updates[tile] = count;
            }
            else if (after > before && filled[tile] == count && updates[tile] < count) ++updates[tile];
        }
        double minimum_depth(int left, int top, int right, int bottom)
        {
            double result = std::numeric_limits<double>::infinity();
            for (int ty = top / depth_tile_size; ty <= (bottom - 1) / depth_tile_size; ++ty)
            {
                for (int tx = left / depth_tile_size; tx <= (right - 1) / depth_tile_size; ++tx)
                {
                    const size_t tile = ty * depth_columns + tx;
                    const int count = tile_area(tx, ty);
                    if (filled[tile] != count) return 0;
                    if (updates[tile] >= count)
                    {
                        float closest = std::numeric_limits<float>::infinity();
                        for (int y = ty * depth_tile_size; y < std::min(height, (ty + 1) * depth_tile_size); ++y)
                            for (int x = tx * depth_tile_size; x < std::min(width, (tx + 1) * depth_tile_size); ++x)
                                closest = std::min(closest, inverse_depth[static_cast<size_t>(y) * width + x]);
                        lower_depth[tile] = closest;
                        updates[tile] = 0;
                    }
                    result = std::min(result, static_cast<double>(lower_depth[tile]));
                }
            }
            return result;
        }

        public:
        size_t rejected_tiles = 0, covered_tiles = 0, occluded_tiles = 0, occluded_triangles = 0;
        void deactivate() { width = height = 0; }
        void reset(int w, int h, bool tiled = true)
        {
            tile_metadata = tiled;
            width = std::max(0, w);
            height = std::max(0, h);
            inverse_depth.assign(static_cast<size_t>(width) * height, 0);
            surface_distance.assign(inverse_depth.size(), 0);
            column_top.assign(width, height);
            rejected_tiles = covered_tiles = occluded_tiles = occluded_triangles = 0;
            depth_columns = (width + depth_tile_size - 1) / depth_tile_size;
            const size_t tiles = tile_metadata ? static_cast<size_t>(depth_columns) * ((height + depth_tile_size - 1) / depth_tile_size) : 0;
            filled.assign(tiles, 0);
            updates.assign(tiles, 0);
            lower_depth.assign(tiles, 0);
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

        const std::vector<float> &distances() const
        {
            static const std::vector<float> empty;
            return width && height ? surface_distance : empty;
        }
        const std::vector<float>& inverse_depths() const
        {
            static const std::vector<float> empty;
            return width && height ? inverse_depth : empty;
        }

        void store(int x, int y, double depth, double distance)
        {
            if (x < 0 || y < 0 || x >= width || y >= height) return;
            const size_t index = static_cast<size_t>(y) * width + x;
            if (depth > inverse_depth[index] && std::isfinite(distance) && distance > 0)
            {
                if (tile_metadata) update_depth_tile(x, y, inverse_depth[index], static_cast<float>(depth));
                inverse_depth[index] = static_cast<float>(depth);
                surface_distance[index] = static_cast<float>(distance);
                column_top[x] = std::min(column_top[x], y);
            }
        }

        void triangle(const ProjectedTerrainVertex &a, const ProjectedTerrainVertex &b,
            const ProjectedTerrainVertex &c, bool tiled = true)
        {
            tiled = tiled && tile_metadata;
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
            const double maximum_depth = std::max(a.inverse_depth, std::max(b.inverse_depth, c.inverse_depth))
                + 1e-8 * (fabs(a.inverse_depth) + fabs(b.inverse_depth) + fabs(c.inverse_depth));
            if (tiled && (right - left) * (bottom - top) >= 256 && std::isfinite(maximum_depth)
                && maximum_depth <= minimum_depth(static_cast<int>(left), static_cast<int>(top),
                    static_cast<int>(ceil(right)), static_cast<int>(ceil(bottom))))
            {
                ++occluded_triangles;
                return;
            }
            if (tiled && (right - left) * (bottom - top) >= 256 && right - left >= 4 && bottom - top >= 4)
            {
                constexpr int tile_size = 8;
                const int first_x = static_cast<int>(left), last_x = static_cast<int>(ceil(right));
                const int last_y = static_cast<int>(ceil(bottom));
                for (int top_y = static_cast<int>(top); top_y < last_y; top_y += tile_size)
                {
                    const int rows = std::min(tile_size, last_y - top_y);
                    double row_a[tile_size], row_b[tile_size];
                    for (int row = 0; row < rows; ++row)
                    {
                        const double py = top_y + row + .5;
                        row_a[row] = ((b.y - c.y) * (first_x + .5 - c.x) + (c.x - b.x) * (py - c.y)) / determinant;
                        row_b[row] = ((c.y - a.y) * (first_x + .5 - c.x) + (a.x - c.x) * (py - c.y)) / determinant;
                    }
                    for (int left_x = first_x; left_x < last_x; left_x += tile_size)
                    {
                        const int columns = std::min(tile_size, last_x - left_x);
                        double minimum[3] = {INFINITY, INFINITY, INFINITY};
                        double maximum[3] = {-INFINITY, -INFINITY, -INFINITY};
                        for (int row = 0; row < rows; ++row)
                        {
                            for (int column : {0, columns - 1})
                            {
                                const double wa = row_a[row] + column * da_dx;
                                const double wb = row_b[row] + column * db_dx;
                                const double weights[] = {wa, wb, 1.0 - wa - wb};
                                for (int edge = 0; edge < 3; ++edge)
                                {
                                    minimum[edge] = std::min(minimum[edge], weights[edge]);
                                    maximum[edge] = std::max(maximum[edge], weights[edge]);
                                }
                            }
                        }
                        double scale = 1 + columns * (fabs(da_dx) + fabs(db_dx));
                        for (int edge = 0; edge < 3; ++edge)
                            scale += fabs(minimum[edge]) + fabs(maximum[edge]);
                        const double margin = scale * 1e-12;
                        bool outside = false, covered = true;
                        for (int edge = 0; edge < 3; ++edge)
                        {
                            outside |= maximum[edge] < -1e-9 - margin;
                            covered &= minimum[edge] >= -1e-9 + margin;
                        }
                        if (outside) ++rejected_tiles;
                        else if (covered) ++covered_tiles;
                        const bool occluded = !outside && std::isfinite(maximum_depth)
                            && maximum_depth <= minimum_depth(left_x, top_y, left_x + columns, top_y + rows);
                        if (occluded) ++occluded_tiles;
                        for (int row = 0; row < rows; ++row)
                        {
                            double wa = row_a[row], wb = row_b[row];
                            for (int column = 0; column < columns; ++column, wa += da_dx, wb += db_dx)
                            {
                                // Advance even rejected spans identically to the reference recurrence.
                                if (outside || occluded) continue;
                                const double wc = 1.0 - wa - wb;
                                if (!covered && (wa < -1e-9 || wb < -1e-9 || wc < -1e-9)) continue;
                                const double depth = wa * a.inverse_depth + wb * b.inverse_depth + wc * c.inverse_depth;
                                const int x = left_x + column, y = top_y + row;
                                const size_t index = static_cast<size_t>(y) * width + x;
                                if (depth > inverse_depth[index])
                                {
                                    const double distance = (wa * a.distance_over_depth + wb * b.distance_over_depth
                                        + wc * c.distance_over_depth) / depth;
                                    if (std::isfinite(distance) && distance > 0)
                                    {
                                        update_depth_tile(x, y, inverse_depth[index], static_cast<float>(depth));
                                        inverse_depth[index] = static_cast<float>(depth);
                                        surface_distance[index] = static_cast<float>(distance);
                                        column_top[x] = std::min(column_top[x], y);
                                    }
                                }
                            }
                            row_a[row] = wa;
                            row_b[row] = wb;
                        }
                    }
                }
                return;
            }
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
                            if (tiled) update_depth_tile(x, y, inverse_depth[index], static_cast<float>(depth));
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
