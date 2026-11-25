#pragma once

#include "point.h"
#include "terrain_contours.h"
#include <queue>
#include <random>
#include <vector>

namespace alienorum::volcanism
{
    inline double dot(Point a, Point b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

    struct Frame
    {
        Point center, east, north;

        explicit Frame(Point p) : center(p)
        {
            const double ring = std::hypot(p.x, p.y);
            east = ring > 0 ? Point(-p.y / ring, p.x / ring, 0) : Point(0, 1, 0);
            north = Point(-p.z * east.y, p.z * east.x, ring);
        }

        Point offset(double angle, double arc) const
        {
            return center * std::cos(arc) + (east * std::cos(angle) + north * std::sin(angle)) * std::sin(arc);
        }
    };

    struct Vent
    {
        Frame frame;
        double radius, height, aspect, pit, twist, orientation, age, fallout, hot_weight;
        bool so2_frost = false, district_source = false;
    };

    inline std::vector<Vent> make_vents(uint32_t seed, double radius_m, double gravity,
        double activity, double pressure)
    {
        std::vector<Vent> vents;
        if (activity <= 0) return vents;
        std::mt19937 rng(seed ^ 0x76656e74U);
        std::uniform_real_distribution<double> uniform(0, 1);
        const auto draw = [&]() { return uniform(rng); };
        // An ensemble of large volcanic districts. Density, clustering, age and duty
        // cycle are scenario priors. A district can contain many unresolved vents.
        const int districts = int(std::clamp(4 * _pi * radius_m * radius_m * activity / 3.0e12, 2.0, 420.0));
        for (int i = 0; i < districts; ++i)
        {
            const double z = 2 * draw() - 1, lon = 2 * _pi * draw();
            const double ring = std::sqrt(1 - z*z);
            const Frame rift(Point(ring * std::cos(lon), ring * std::sin(lon), z));
            const double axis = 2 * _pi * draw();
            const int count = 1 + int(6 * draw());
            for (int j = 0; j < count; ++j)
            {
                const double along = j ? (draw() - .5) * std::min(.55, 1.6e6 / radius_m) : 0;
                const Point p = rift.offset(axis + .65 * (draw() - .5), along);
                // Many small vents, rare large shields. Lengths refer to a physical
                // edifice, bounded on small moons so a local tangent plane is valid.
                const double base = std::min(.095 * radius_m, 22000 * std::pow(15.0, draw()));
                const double slope = .006 + .032 * draw();
                const double height = std::min(base * slope, 2.0e8 / (3000 * gravity));
                const double aspect = .38 + .62 * draw(), pit = .10 + .35 * draw();
                const double twist = 2 * _pi * draw(), orientation = axis + 2 * (draw() - .5);
                const double age = draw();
                // Ballistic coarse ejecta from a gas-driven eruption in near vacuum.
                // v is a scenario draw (m/s). Thick air suppresses this transport mode.
                const double speed = 180 + 850 * draw();
                const double fallout = draw() < .35 && pressure < 100 && speed * speed < 2 * gravity * radius_m
                    ? std::min(.13, speed * speed / (gravity * radius_m)) : 0;
                const double hot_weight = draw() < .16 + .20 * activity ? base * base * aspect * pit * pit : 0;
                vents.push_back({Frame(p), base / radius_m, height, aspect, pit, twist, orientation,
                    age, fallout, hot_weight, false, j == 0});
            }
        }
        return vents;
    }

    inline bool retains_so2_frost(double temperature, double partial_pa)
    {
        if (!(partial_pa > 0 && temperature > 0 && temperature < 197.64)) return false;
        // Clausius-Clapeyron, anchored at the NIST SO2 triple point (Pa, K).
        const double saturation = 1670 * std::exp(4510 * (1 / 197.64 - 1 / temperature));
        return partial_pa >= saturation;
    }

    struct Profile
    {
        double r, angle, relief, floor;
    };

    inline Profile profile(const Vent& vent, Point p)
    {
        const double x = dot(p, vent.frame.east) / vent.radius;
        const double y = dot(p, vent.frame.north) / vent.radius;
        const double c = std::cos(vent.orientation), s = std::sin(vent.orientation);
        const double u = c*x + s*y, v = (c*y - s*x) / vent.aspect;
        const double angle = std::atan2(v, u);
        // A skewed collapse scar shares its planform with the dark pit floor.
        const double edge = 1 + .16 * std::sin(3*angle + vent.twist) + .09 * std::cos(5*angle - vent.twist);
        const double r = std::hypot(u, v) / edge;
        const double floor = 1 - TerrainContours::smooth(.70 * vent.pit, vent.pit, r);
        const double relief = vent.height * (1 - TerrainContours::smooth(0, 1, r)) * (1 - .93 * floor);
        return {r, angle, relief, floor};
    }

    // Visit only texels inside the spherical cap. The longitude interval wraps
    // once at the seam, and a row through either pole can span all longitudes.
    template<class Paint>
    void visit_cap(Point center, double arc, int width, int height, Paint paint)
    {
        const double lat = std::asin(std::clamp(center.z, -1.0, 1.0));
        const double lon = std::atan2(center.y, center.x);
        const double cos_limit = std::cos(arc), lat_scale = height / _pi, lon_scale = width / (2 * _pi);
        const int top = std::max(0, int(std::ceil((half_pi - lat - arc) * lat_scale)));
        const int bottom = std::min(height - 1, int(std::floor((half_pi - lat + arc) * lat_scale)));
        for (int y = top; y <= bottom; ++y)
        {
            const double theta = y / lat_scale, z = std::cos(theta), ring = std::sin(theta);
            const double divisor = ring * std::cos(lat);
            const double limit = divisor > 1e-14 ? (cos_limit - z * center.z) / divisor : -1;
            if (limit > 1) continue;
            const double span = std::acos(std::clamp(limit, -1.0, 1.0));
            const int first = int(std::ceil((lon - span) * lon_scale));
            const int last = std::min(first + width - 1, int(std::floor((lon + span) * lon_scale)));
            for (int col = first; col <= last; ++col)
            {
                const int x = (col + width) % width;
                const double phi = x / lon_scale;
                paint(size_t(y) * width + x, Point(ring * std::cos(phi), ring * std::sin(phi), z));
            }
        }
    }

    struct FloodCell
    {
        float elevation = 0, resistance = 1, cost = 0;
        float light = 0, red = 0, blue = 0, opacity = 0;
        int episode = -1;
    };

    // An old flood-basalt province comprises many flows from a district. Grow a
    // connected field over sampled relief. The work queue visits its easiest
    // paths first, charging for travel distance, uphill steps and rough ground.
    template<class Elevation, class Roughness>
    std::vector<FloodCell> flood_provinces(const std::vector<Vent>& history, int rows,
        double radius, double flux, Elevation elevation, Roughness roughness)
    {
        const int cols = 2 * rows;
        std::vector<FloodCell> grid(rows * cols);
        std::vector<double> ring(rows);
        const double dy = _pi * radius / rows;
        for (int y = 0; y < rows; ++y)
        {
            const double theta = (y + .5) * _pi / rows;
            ring[y] = std::sin(theta);
            for (int x = 0; x < cols; ++x)
            {
                const double lon = (x + .5) * _pi / rows;
                const Point p(ring[y] * std::cos(lon), ring[y] * std::sin(lon), std::cos(theta));
                FloodCell& cell = grid[y * cols + x];
                cell.elevation = elevation(p);
                const double rock = roughness(p);
                cell.resistance = std::exp(5.4 * (rock - .5));
            }
        }
        for (size_t id = 0; id < history.size(); ++id)
        {
            const auto& vent = history[id];
            if (!vent.district_source) continue;
            const Point start = vent.frame.offset(vent.orientation, .70 * vent.radius);
            const int y0 = std::clamp(int(std::acos(std::clamp(start.z, -1.0, 1.0)) * rows / _pi), 0, rows-1);
            const int x0 = int(std::floor(std::atan2(start.y,start.x) * rows / _pi)) + cols;
            const int source = y0 * cols + x0 % cols;
            // Mass balance. Q = F * catchment area, V = fraction * Q * time /
            // (density * specific enthalpy). A stack of flows covers V/thickness.
            const double years = 2800 * std::pow(30., vent.twist / (2 * _pi)) / (1 + flux / 20);
            const double thickness = 75 + 155 * vent.age;
            const double catchment = std::min(3e12, 2 * _pi * radius * radius);
            const double volume = .025 * flux * catchment * years * 31557600 / (3000 * 1.5e6);
            const double area = std::min(.90 * catchment, volume / thickness);
            double remaining = area;
            const double runout = std::min(.6 * radius, 3.5 * std::sqrt(area));
            using Entry = std::pair<float,int>;
            std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> front;
            grid[source].episode = int(id);
            grid[source].cost = 0;
            front.emplace(0, source);
            while (remaining > 0 && !front.empty())
            {
                const auto [cost, index] = front.top();
                front.pop();
                FloodCell& cell = grid[index];
                if (cost != cell.cost || cost > runout) continue;
                const int y = index / cols, x = index % cols;
                // Distal flows interleave with exposed older rock. The covered
                // fraction falls as the volume budget or travel head runs out.
                const double supply = std::min(remaining / area, 1 - cost / runout);
                const double margin = .45 + .40 * vent.age;
                const double opacity = (.85 - .20 * vent.age) * TerrainContours::smooth(0, margin, supply);
                const double rock = 39 + 84 * vent.age + 10 * std::log(cell.resistance);
                const double underlying = cell.opacity * (1 - opacity);
                const double total = underlying + opacity;
                if (total > 0)
                {
                    cell.light = (cell.light * underlying + rock * opacity) / total;
                    cell.red = (cell.red * underlying + (2 + 10 * vent.age * std::sin(vent.twist)) * opacity) / total;
                    cell.blue = (cell.blue * underlying - (4 + 9 * vent.age) * opacity) / total;
                    cell.opacity = total;
                }
                remaining -= dy * dy * ring[y];
                // Local permeability bends the preferred flow direction along a
                // rift. Uphill travel costs more even along its preferred axis.
                const double heading = vent.orientation + 1.05 * std::log(cell.resistance);
                const double east = std::cos(heading), north = std::sin(heading);
                for (int dr = -1; dr <= 1; ++dr)
                    for (int dc = -1; dc <= 1; ++dc)
                    {
                        if (!dr && !dc) continue;
                        int row = y + dr, col = x + dc;
                        if (row < 0 || row >= rows)
                        {
                            row = y;
                            col += rows;
                        }
                        const int next = row * cols + (cols + col) % cols;
                        FloodCell& target = grid[next];
                        if (target.elevation > grid[source].elevation + thickness) continue;
                        const double dx = dc * .5 * (ring[y] + ring[row]);
                        const double distance = std::hypot(dx, dr);
                        const double along = (east * dx - north * dr) / distance;
                        const double uphill = std::max(0.f, target.elevation - cell.elevation);
                        const double transverse = (north * dx + east * dr) / distance;
                        const double drag = .55 + 2.6 * transverse * transverse + 1.05 * std::max(0., -along);
                        const float score = cost + dy * distance * target.resistance * drag + 520 * uphill;
                        if (score > runout || (target.episode == int(id) && target.cost <= score)) continue;
                        target.episode = int(id);
                        target.cost = score;
                        front.emplace(score, next);
                    }
            }
        }
        return grid;
    }

    // Route successive lobes on the rendered relief. Magellan's Mylitta Fluctus
    // extends ~800 km. V/(width * thickness) gives a runout budget, while local
    // minima can arrest a branch earlier. Each step tests a fan at equal distance.
    template<class Elevation, class Paint>
    void route_flows(const Vent& vent, double radius_m, Elevation elevation, Paint paint)
    {
        const double base_m = vent.radius * radius_m;
        const int branches = 2 + int(4 * vent.age);
        for (int branch = 0; branch < branches; ++branch)
        {
            double heading = vent.orientation + branch * 2.4 + vent.twist;
            Point p = vent.frame.offset(heading, vent.radius * (.48 + .13 * std::sin(heading)));
            double h = elevation(p);
            const double width = base_m * (.15 + .44 * (.5 + .5 * std::sin(heading * 3)));
            const double thickness = 12 + 24 * vent.age;
            const double volume = base_m * base_m * vent.height * (.0009 + .0018 * vent.age);
            const double budget = std::min({1.0e6, .25 * radius_m, volume / (width * thickness)});
            const double step = std::max(width * .46, budget / 42) / radius_m;
            for (double distance = 0; distance + step * radius_m <= budget; distance += step * radius_m)
            {
                const Frame local(p);
                double best = h;
                Point next = p;
                for (int ray = -4; ray <= 4; ++ray)
                {
                    const double angle = heading + ray * .38;
                    const Point candidate = local.offset(angle, step);
                    const double value = elevation(candidate);
                    if (value >= best || elevation(local.offset(angle, .5 * step)) > h + thickness) continue;
                    best = value;
                    next = candidate;
                }
                if (best >= h) break;
                const double spread = width / radius_m * (.58 + .38 * distance / budget
                    + .15 * std::cos(branch + distance / width));
                paint(p, next, spread, h + thickness, branch);
                const Point tangent = next - p;
                const Frame at_next(next);
                heading = std::atan2(dot(tangent, at_next.north), dot(tangent, at_next.east));
                p = next;
                h = best;
            }
        }
    }
}
