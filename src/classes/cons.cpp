#include <array>
#include <cmath>
#include <set>
#include <vector>
#include <sstream>
#include <fstream>
#include "alienorumid.h"
#include "cons.h"
#include "serial.h"
#include "browncatalog.h"

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
// Implemented using a robust spherical winding-number technique.
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

    const auto direction_to = [&](const ConsBoundary& boundary) -> std::array<double, 3>
    {
        const double cos_boundary_decl = cos(boundary.decl);
        const double x = cos_boundary_decl * cos(boundary.RA) - x0;
        const double y = cos_boundary_decl * sin(boundary.RA) - y0;
        const double z = sin(boundary.decl) - z0;
        const double length = sqrt(x * x + y * y + z * z);
        if (length == 0.0) return {};
        return {x / length, y / length, z / length};
    };

    // Adjacent edges share a vertex, so reuse its normalized direction.
    const auto first = direction_to(bounds.front());
    auto previous = first;
    for (int i = 1; i <= n; ++i)
    {
        const auto [v1x, v1y, v1z] = previous;
        previous = i < n ? direction_to(bounds[i]) : first;
        const auto [v2x, v2y, v2z] = previous;
        if ((v1x == 0.0 && v1y == 0.0 && v1z == 0.0) ||
            (v2x == 0.0 && v2y == 0.0 && v2z == 0.0)) continue;

        // Compute the angle between the two vectors.
        double dot = v1x * v2x + v1y * v2y + v1z * v2z;
        if (dot > 1.0) dot = 1.0;
        if (dot < -1.0) dot = -1.0;
        double angle = acos(dot);
        // Determine the sign of the angle using cross product direction.
        double cx = v1y * v2z - v1z * v2y;
        double cy = v1z * v2x - v1x * v2z;
        double cz = v1x * v2y - v1y * v2x;
        double sign = (cx * x0 + cy * y0 + cz * z0) >= 0.0 ? 1.0 : -1.0;
        sum_angle += sign * angle;
    }
    // Inside if absolute winding sum exceeds pi.
    return fabs(sum_angle) > _pi;
}

Constellation *identify_cons_from_coords(double RA, double Decl)
{
    if (!std::isfinite(RA) || !std::isfinite(Decl) || std::abs(Decl) > half_pi) return nullptr;
    struct BoundaryStrip
    {
        double first_hour, last_hour, minimum_declination;
        std::string abbreviation;
    };
    static const auto strips = []
    {
        std::vector<BoundaryStrip> result;
        std::ifstream input("catalogs/constellation_lookup.dat");
        for (std::string line; std::getline(input, line);)
        {
            BoundaryStrip row;
            std::istringstream fields(line);
            if (fields >> row.first_hour >> row.last_hour >> row.minimum_declination >> row.abbreviation)
                result.push_back(std::move(row));
        }
        return result;
    }();
    if (!strips.empty())
    {
        constexpr double b1875 = (2433282.42345905 - 75 * 365.242198781 - 2451545.0) / 365.25;
        double zeta, z, theta;
        get_Earth_precession_angles(0, b1875, zeta, z, theta);
        const Point point = apply_precession_rotation(Point::from_ra_dec(RA, Decl, 1), zeta, z, theta);
        const double hours = std::fmod(std::atan2(-point.x, point.z) * 12 / _pi + 24, 24);
        const double degrees = std::atan2(point.y, std::hypot(point.x, point.z)) * fiftyseven;
        for (const auto& row : strips)
        {
            if (hours < row.first_hour || hours >= row.last_hour || degrees < row.minimum_declination) continue;
            for (auto& constellation : constellations)
                if ((constellation.vantage_name == "Sun" || is_reference_vantage(constellation.vantage_name))
                    && !strcasecmp(constellation.abbrev.c_str(), row.abbreviation.c_str())) return &constellation;
            break;
        }
    }
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

    Constellation* polar = nullptr;
    double extreme = 0;
    for (auto& candidate : constellations)
    {
        double min_ra = 2 * _pi, max_ra = 0;
        for (const auto& boundary : candidate.bounds)
        {
            min_ra = std::min(min_ra, boundary.RA);
            max_ra = std::max(max_ra, boundary.RA);
        }
        if (max_ra - min_ra < _pi) continue;
        for (const auto& boundary : candidate.bounds)
        {
            const double score = boundary.decl * (Decl >= 0 ? 1 : -1);
            if (score > extreme)
            {
                polar = &candidate;
                extreme = score;
            }
        }
    }
    if (polar) return polar;

    return nullptr;
}

Constellation* identify_designated_cons_of_star(const Star* star)
{
    if (!star) return nullptr;
    const auto match = [](const std::string& abbreviation) -> Constellation*
    {
        if (abbreviation.empty()) return nullptr;
        for (auto& cons : constellations)
        {
            if (is_reference_vantage(cons.vantage_name) &&
                !strcasecmp(abbreviation.c_str(), cons.abbrev.c_str())) return &cons;
        }
        return nullptr;
    };
    const auto designation = [&](const std::string& name) -> Constellation*
    {
        std::istringstream words(name);
        std::string word;
        words >> word;
        if (word.size() > 3)
        {
            if (Constellation* cons = match(word.substr(word.size() - 3))) return cons;
        }
        // The remaining words contain the constellation and any component suffix.
        while (words >> word)
        {
            if (Constellation* cons = match(word)) return cons;
        }
        return nullptr;
    };

    if (Constellation* cons = designation(star->Flamsteed)) return cons;
    if (star->FlamsteedNo > 0)
    {
        if (Constellation* cons = match(star->constellation)) return cons;
    }
    if (Constellation* cons = designation(star->Bayer)) return cons;
    if (star->BayerGrkno >= 0)
    {
        if (Constellation* cons = match(star->constellation)) return cons;
    }
    if (Constellation* cons = designation(star->alienorumid)) return cons;
    return match(star->Gouldcons);
}

Constellation *identify_cons_of_star(Star *s)
{
    Constellation* result = nullptr;
    double best = 1e29;

    for (auto& cons : constellations) 
    {
        if (cons.bounds.empty()) continue;              // Safety check

        if (s->constellation[0] && !strcasecmp(s->constellation, cons.abbrev.c_str())) return &cons;
        if (s->Gouldcons[0] && !strcasecmp(s->Gouldcons, cons.abbrev.c_str())) return &cons;

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

    Constellation* polar = nullptr;
    double extreme = 0;
    for (auto& candidate : constellations)
    {
        double min_ra = 2 * _pi, max_ra = 0;
        for (const auto& boundary : candidate.bounds)
        {
            min_ra = std::min(min_ra, boundary.RA);
            max_ra = std::max(max_ra, boundary.RA);
        }
        if (max_ra - min_ra < _pi) continue;
        for (const auto& boundary : candidate.bounds)
        {
            const double score = boundary.decl * (s->declination >= 0 ? 1 : -1);
            if (score > extreme)
            {
                polar = &candidate;
                extreme = score;
            }
        }
    }
    if (polar) return polar;

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

static Constellation* alienorum_id_constellation(Star* star)
{
    auto* cons = identify_cons_of_star(star);
    if (!cons) return nullptr;
    // Serpens Caput and Serpens Cauda share one sequence.
    std::string base = cons->abbrev;
    while (!base.empty() && std::isdigit(static_cast<unsigned char>(base.back()))) base.pop_back();
    if (base != cons->abbrev)
    {
        for (auto& candidate : constellations) if (candidate.abbrev == base) return &candidate;
    }
    return cons;
}

void alienorum::assign_purple_catalog_ids()
{
    if (constellations.empty()) return;
    std::map<std::pair<int, std::string>, std::vector<Star*>> bins;
    for (int i = 1; cels[i]; ++i)
    {
        if (cels[i]->typeclass() != class_star) continue;
        auto* star = static_cast<Star*>(cels[i]);
        if (!star->alienorumid.empty() || !purple_catalog_primary(*star)) continue;
        const auto* cons = alienorum_id_constellation(star);
        if (!cons) continue;
        bins[{static_cast<int>(star->apparent_magnitude), cons->abbrev}].push_back(star);
    }
    for (auto& [bin, stars] : bins)
    {
        const auto [magnitude, constellation] = bin;
        std::sort(stars.begin(), stars.end(), [](const Star* a, const Star* b)
        {
            if (a->apparent_magnitude != b->apparent_magnitude) return a->apparent_magnitude < b->apparent_magnitude;
            if (a->right_ascension != b->right_ascension) return a->right_ascension < b->right_ascension;
            return std::strcmp(a->name, b->name) < 0;
        });
        for (size_t i = 0; i < stars.size(); ++i)
        {
            auto* star = stars[i];
            star->alienorumid = std::to_string(magnitude) + "p " + constellation + " " + std::to_string(i + 1);
            if (!star->multisys) continue;
            for (char component = 'B'; component <= 'Z'; ++component)
            {
                auto* member = star->multisys->get_member(component);
                if (member && member->alienorumid.empty()) member->alienorumid = star->alienorumid + " " + component;
            }
        }
    }
}

static void assign_catalog_component_ids(Star& primary, bool include_blank)
{
    if (!primary.multisys || primary.get_component() != 'A') return;
    for (char slot = 'B'; slot <= 'Z'; ++slot)
    {
        auto* member = primary.multisys->get_member(slot);
        if (!member || member->deleted || member->multisys != primary.multisys ||
            (!include_blank && member->alienorumid.empty())) continue;
        member->alienorumid = primary.alienorumid.empty() ? "" : primary.alienorumid + " " + slot;
    }
}

static void number_catalog_id_bin(const std::string& prefix, const std::vector<Star*>& stars, bool assign_companions)
{
    for (size_t i = 0; i < stars.size(); ++i)
    {
        auto* star = stars[i];
        star->alienorumid = prefix;
        if (!prefix.empty() && (prefix.front() == 'V' || stars.size() > 1))
            star->alienorumid += " " + std::to_string(i + 1);
        assign_catalog_component_ids(*star, assign_companions);
    }
}

static bool catalog_ra_order(const Star* a, const Star* b)
{
    if (a->right_ascension != b->right_ascension) return a->right_ascension < b->right_ascension;
    return strcmp(a->name, b->name) < 0;
}

Star* catalog_id_primary(Star& star)
{
    if (star.get_component() > 'A')
    {
        Star* primary = star.multisys->get_member('A');
        if (primary && !primary->deleted) return primary;
    }
    return &star;
}

static bool catalog_id_variable(const Star& star)
{
    return star.variability_period && !star.is_eclipsing_binary &&
        fabs(star.maxmag - star.minmag) > .5;
}

static void sort_catalog_id_bin(std::vector<Star*>& stars, bool variable)
{
    std::sort(stars.begin(), stars.end(), [variable](const Star* a, const Star* b)
    {
        if (!variable && a->apparent_magnitude != b->apparent_magnitude)
            return a->apparent_magnitude < b->apparent_magnitude;
        return catalog_ra_order(a, b);
    });
}

void repair_missing_catalog_ids()
{
    std::map<std::string, std::vector<Star*>> bins;
    std::set<std::string> changed;
    for (int i = 1; cels[i]; ++i)
    {
        if (cels[i]->deleted || cels[i]->typeclass() != class_star) continue;
        auto* star = static_cast<Star*>(cels[i]);
        if (catalog_id_primary(*star) != star) continue;
        const auto& id = star->alienorumid;
        if (valid_alienorum_id(id) && !(id.back() >= 'B' && id.back() <= 'Z' && id[id.size() - 2] == ' '))
        {
            bins[id.substr(0, id.find(' ', id.find(' ') + 1))].push_back(star);
            continue;
        }
        // A suffix without a live primary is a stale component designation.
        star->alienorumid.clear();
        const double magnitude = star->apparent_magnitude;
        if (!std::isfinite(magnitude) || magnitude <= -31 || magnitude >= alienorum_magnitude_limit) continue;
        auto* cons = alienorum_id_constellation(star);
        if (!cons) continue;
        const std::string prefix = (catalog_id_variable(*star) ? "V" : std::to_string(static_cast<int>(magnitude)))
            + std::string(1, get_color_code_from_temp(star->estimate_temperature())) + " " + cons->abbrev;
        bins[prefix].push_back(star);
        changed.insert(prefix);
    }
    for (const auto& prefix : changed)
    {
        auto& stars = bins.at(prefix);
        sort_catalog_id_bin(stars, prefix.front() == 'V');
        number_catalog_id_bin(prefix, stars, true);
    }
    for (int i = 1; cels[i]; ++i)
    {
        if (!cels[i]->deleted && cels[i]->typeclass() == class_star)
            assign_catalog_component_ids(*static_cast<Star*>(cels[i]), true);
    }
}

void repair_legacy_alienorum_id_sequences()
{
    if (cels[0] && cels[0]->typeclass() == class_star)
    {
        auto* first = static_cast<Star*>(cels[0]);
        if (is_catalog_sun(*first)) first->alienorumid = "-26";
    }
    std::map<std::string, std::vector<Star*>> bins;
    for (int i = 1; cels[i]; ++i)
    {
        if (cels[i]->deleted || cels[i]->typeclass() != class_star) continue;
        auto* star = static_cast<Star*>(cels[i]);
        const std::string& id = star->alienorumid;
        if (id.empty() || purple_catalog_id(id) || star->get_component() > 'A') continue;
        if (id.back() >= 'B' && id.back() <= 'Z' && id[id.size() - 2] == ' ') continue;
        const auto prefix_end = id.find(' ', id.find(' ') + 1);
        bins[id.substr(0, prefix_end)].push_back(star);
    }
    for (auto& [prefix, stars] : bins)
    {
        const bool variable = prefix.front() == 'V';
        const auto old_rank = [&](const Star* star)
        {
            return star->alienorumid.size() > prefix.size()
                ? std::strtoul(star->alienorumid.c_str() + prefix.size() + 1, nullptr, 10) : 0ul;
        };
        std::sort(stars.begin(), stars.end(), [&](const Star* a, const Star* b)
        {
            if (!variable && old_rank(a) != old_rank(b)) return old_rank(a) < old_rank(b);
            return catalog_ra_order(a, b);
        });
        number_catalog_id_bin(prefix, stars, false);
    }
}

ConsBins fill_alienorum_ids()
{
    ConsBins bins;
    finalize_brown_catalog();
    constexpr int variable_bin = -31;
    for (int i = 0; cels[i]; ++i)
    {
        if (cels[i]->deleted || cels[i]->typeclass() != class_star) continue;
        auto* star = static_cast<Star*>(cels[i]);
        if (star->declination >= _pi) star->declination -= _pi * 2;
        if (catalog_id_primary(*star) != star) continue;
        if (star->seqno == 0)
        {
            if (is_catalog_sun(*star)) star->alienorumid = "-26";
            continue;
        }
        if (purple_catalog_id(star->alienorumid)) continue;

        auto* cons = alienorum_id_constellation(star);
        if (!cons) continue;
        const double magnitude = star->apparent_magnitude;
        int bin = std::isfinite(magnitude) && magnitude > -31 && magnitude < 51
            ? static_cast<int>(magnitude) : 20;
        if (bin < alienorum_magnitude_limit && catalog_id_variable(*star)) bin = variable_bin;
        bins[cons][bin][get_color_code_from_temp(star->estimate_temperature())].push_back(star);
    }

    for (auto& [cons, magnitudes] : bins)
    {
        for (auto& [magnitude, colors] : magnitudes)
        {
            const bool variable = magnitude == variable_bin;
            for (auto& [color, stars] : colors)
            {
                // Catalog coverage ends before magnitude 10.
                if (magnitude >= alienorum_magnitude_limit)
                {
                    for (auto* star : stars)
                    {
                        star->alienorumid.clear();
                        if (!star->multisys) continue;
                        for (char slot = 'B'; slot <= 'Z'; ++slot)
                        {
                            auto* companion = star->multisys->get_member(slot);
                            if (companion) companion->alienorumid.clear();
                        }
                    }
                    continue;
                }
                sort_catalog_id_bin(stars, variable);
                const std::string prefix = (variable ? "V" : std::to_string(magnitude))
                    + std::string(1, color) + " " + cons->abbrev;
                number_catalog_id_bin(prefix, stars, true);
            }
        }
    }
    return bins;
}
bool is_reference_vantage(const std::string& name)
{
    if (name.empty()) return true;
    if (!cels || !cels[0]) return false;
    if (name == cels[0]->name) return true;
    if (cels[0]->typeclass() != class_star) return false;
    const auto& aliases = static_cast<Star*>(cels[0])->catalog_aliases;
    return std::find(aliases.begin(), aliases.end(), name) != aliases.end();
}
