#ifndef ALIENORUM_HORIZON_BACKGROUND_H
#define ALIENORUM_HORIZON_BACKGROUND_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace alienorum
{
    // Return the first row below which the unchanged ground painter is fully opaque.
    inline float opaque_background_bottom(const double* xs, const double* ys, int nodes,
        int width, int height, double radius, double eye, double lift, double altitude, double focal)
    {
        if (nodes < 2 || width < 1 || height < 1 || !(radius > 0) || !(eye > 0)
            || !(focal > 0) || !std::isfinite(focal) || !std::isfinite(radius)
            || !std::isfinite(eye) || !std::isfinite(lift) || !std::isfinite(altitude)) return height;
        const double cosine = cos(altitude), sine = sin(altitude), cy = height * .5, cx = width * .5;
        if (cosine < 0 || cosine - cy / focal * sine <= 0 || cosine + cy / focal * sine <= 0) return height;
        const double range = sqrt(2 * radius * eye), curvature = .5 / radius - lift / range;
        if (!std::isfinite(range) || !std::isfinite(curvature)) return height;
        const auto covered = [&](double sx, double sy)
        {
            const double x = (sx - cx) / focal, y = (cy - sy) / focal;
            const double north = cosine - y * sine, up = y * cosine + sine;
            const double slope = -up / fmax(1e-12, hypot(x, north));
            const double discriminant = slope * slope - 4 * curvature * eye;
            if (!(discriminant >= 0)) return false;
            const double denominator = slope + sqrt(discriminant);
            const double distance = denominator > 0 ? 2 * eye / denominator : std::numeric_limits<double>::infinity();
            return std::isfinite(distance) && distance <= range;
        };
        // At fixed y, the slope is monotone in |x|. Either center or edges
        // maximize distance, including negative curvature from atmospheric lift.
        const auto covered_row = [&](double y)
        {
            return covered(.5, y) && covered(cx, y) && covered(width - .5, y);
        };
        if (!covered_row(height - .5)) return height;
        const bool all_ground = covered(0, 0) && covered(width, 0)
            && covered(0, height) && covered(width, height);
        if (all_ground && covered_row(.5)) return std::min(height, 4);
        int first_row = 0, last_row = height;
        while (first_row < last_row)
        {
            const int middle = (first_row + last_row) / 2;
            if (covered_row(middle + .5)) last_row = middle;
            else first_row = middle + 1;
        }
        std::vector<double> columns(width, all_ground ? 0 : 1e9);
        for (int j = 0; j <= nodes; ++j)
        {
            const int a = (j + nodes - 1) % nodes, b = j % nodes;
            double left = xs[a], right = xs[b], top = ys[a], bottom = ys[b];
            if (!(left > -1e5 && right > -1e5 && top > -1e4 && bottom > -1e4)
                || !std::isfinite(left) || !std::isfinite(right) || !std::isfinite(top) || !std::isfinite(bottom)
                || fabs(left - right) >= focal || left > 1e8 || right > 1e8) continue;
            if (left > right)
            {
                std::swap(left, right);
                std::swap(top, bottom);
            }
            const int first = std::max(0, static_cast<int>(floor(left)));
            const int last = std::min(width - 1, static_cast<int>(ceil(right)));
            const double span = right - left;
            for (int x = first; x <= last; ++x)
            {
                const double fraction = std::clamp(span > .001 ? (x - left) / span : 0.0, 0.0, 1.0);
                columns[x] = std::min(columns[x], top + (bottom - top) * fraction);
            }
        }
        double limit = first_row;
        for (double column : columns)
        {
            if (!(column < height)) return height;
            limit = std::max(limit, column);
        }
        // Interior coverage only: retain a four-pixel edge/filter/roundoff margin.
        return static_cast<float>(std::min(double(height), std::max(0.0, ceil(limit) + 4)));
    }
}
#endif
