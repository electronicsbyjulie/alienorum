#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include "exocons.h"
#include "celestial.h"
#include "serial.h"
#include "misc.h"

namespace alienorum
{
    const IAUConstellationDef iau_constellations[88] =
    {
        {"And", "Andromeda", "Andromedae"},
        {"Ant", "Antlia", "Antliae"},
        {"Aps", "Apus", "Apodis"},
        {"Aqr", "Aquarius", "Aquarii"},
        {"Aql", "Aquila", "Aquilae"},
        {"Ara", "Ara", "Arae"},
        {"Ari", "Aries", "Arietis"},
        {"Aur", "Auriga", "Aurigae"},
        {"Boo", "Bootes", "Bootis"},
        {"Cae", "Caelum", "Caeli"},
        {"Cam", "Camelopardalis", "Camelopardalis"},
        {"Cnc", "Cancer", "Cancri"},
        {"CVn", "Canes Venatici", "Canum Venaticorum"},
        {"CMa", "Canis Major", "Canis Majoris"},
        {"CMi", "Canis Minor", "Canis Minoris"},
        {"Cap", "Capricornus", "Capricorni"},
        {"Car", "Carina", "Carinae"},
        {"Cas", "Cassiopeia", "Cassiopeiae"},
        {"Cen", "Centaurus", "Centauri"},
        {"Cep", "Cepheus", "Cephei"},
        {"Cet", "Cetus", "Ceti"},
        {"Cha", "Chamaeleon", "Chamaeleontis"},
        {"Cir", "Circinus", "Circini"},
        {"Col", "Columba", "Columbae"},
        {"Com", "Coma Berenices", "Comae Berenices"},
        {"CrA", "Corona Australis", "Coronae Australis"},
        {"CrB", "Corona Borealis", "Coronae Borealis"},
        {"Crv", "Corvus", "Corvi"},
        {"Crt", "Crater", "Crateris"},
        {"Cru", "Crux", "Crucis"},
        {"Cyg", "Cygnus", "Cygni"},
        {"Del", "Delphinus", "Delphini"},
        {"Dor", "Dorado", "Doradus"},
        {"Dra", "Draco", "Draconis"},
        {"Equ", "Equuleus", "Equulei"},
        {"Eri", "Eridanus", "Eridani"},
        {"For", "Fornax", "Fornacis"},
        {"Gem", "Gemini", "Geminorum"},
        {"Gru", "Grus", "Gruis"},
        {"Her", "Hercules", "Herculis"},
        {"Hor", "Horologium", "Horologii"},
        {"Hya", "Hydra", "Hydrae"},
        {"Hyi", "Hydrus", "Hydri"},
        {"Ind", "Indus", "Indi"},
        {"Lac", "Lacerta", "Lacertae"},
        {"Leo", "Leo", "Leonis"},
        {"LMi", "Leo Minor", "Leonis Minoris"},
        {"Lep", "Lepus", "Leporis"},
        {"Lib", "Libra", "Librae"},
        {"Lup", "Lupus", "Lupi"},
        {"Lyn", "Lynx", "Lyncis"},
        {"Lyr", "Lyra", "Lyrae"},
        {"Men", "Mensa", "Mensae"},
        {"Mic", "Microscopium", "Microscopii"},
        {"Mon", "Monoceros", "Monocerotis"},
        {"Mus", "Musca", "Muscae"},
        {"Nor", "Norma", "Normae"},
        {"Oct", "Octans", "Octantis"},
        {"Oph", "Ophiuchus", "Ophiuchi"},
        {"Ori", "Orion", "Orionis"},
        {"Pav", "Pavo", "Pavonis"},
        {"Peg", "Pegasus", "Pegasi"},
        {"Per", "Perseus", "Persei"},
        {"Phe", "Phoenix", "Phoenicis"},
        {"Pic", "Pictor", "Pictoris"},
        {"Psc", "Pisces", "Piscium"},
        {"PsA", "Piscis Austrinus", "Piscis Austrini"},
        {"Pup", "Puppis", "Puppis"},
        {"Pyx", "Pyxis", "Pyxidis"},
        {"Ret", "Reticulum", "Reticuli"},
        {"Sge", "Sagitta", "Sagittae"},
        {"Sgr", "Sagittarius", "Sagittarii"},
        {"Sco", "Scorpius", "Scorpii"},
        {"Scl", "Sculptor", "Sculptoris"},
        {"Sct", "Scutum", "Scuti"},
        {"Ser", "Serpens", "Serpentis"},
        {"Sex", "Sextans", "Sextantis"},
        {"Tau", "Taurus", "Tauri"},
        {"Tel", "Telescopium", "Telescopii"},
        {"Tri", "Triangulum", "Trianguli"},
        {"TrA", "Triangulum Australe", "Trianguli Australis"},
        {"Tuc", "Tucana", "Tucanae"},
        {"UMa", "Ursa Major", "Ursae Majoris"},
        {"UMi", "Ursa Minor", "Ursae Minoris"},
        {"Vel", "Vela", "Velorum"},
        {"Vir", "Virgo", "Virginis"},
        {"Vol", "Volans", "Volantis"},
        {"Vul", "Vulpecula", "Vulpeculae"}
    };

    std::vector<Constellation> ExoConsGenerator::pending_conss;
    std::vector<Constellation> ExoConsGenerator::generated_conss;
    Star* ExoConsGenerator::current_sys_star = nullptr;
    std::string ExoConsGenerator::current_vantage_name = "";
    bool ExoConsGenerator::is_generating = false;
    std::unordered_set<std::string> ExoConsGenerator::completed_vantages;

    inline double dot_product(const Point& a, const Point& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    inline Point cross_product(const Point& a, const Point& b)
    {
        return Point(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        );
    }

    inline Point normalize_point(const Point& p)
    {
        double m = p.magnitude();
        if (m > 1e-12)
        {
            return Point(p.x / m, p.y / m, p.z / m);
        }
        return p;
    }

    inline double ang_dist_rad(const Point& a, const Point& b)
    {
        double c = dot_product(a, b);
        if (c > 1.0)
        {
            c = 1.0;
        }
        if (c < -1.0)
        {
            c = -1.0;
        }
        return acos(c);
    }

    inline double ang_dist_deg(const Point& a, const Point& b)
    {
        return ang_dist_rad(a, b) * (180.0 / _pi);
    }

    const IAUConstellationDef* ExoConsGenerator::find_iau_def(const std::string& abbrev)
    {
        for (int i = 0; i < 88; i++)
        {
            if (abbrev == iau_constellations[i].abbrev)
            {
                return &iau_constellations[i];
            }
        }
        return nullptr;
    }

    bool ExoConsGenerator::arcs_intersect(const Point& a, const Point& b, const Point& c, const Point& d)
    {
        Point n1 = cross_product(a, b);
        Point n2 = cross_product(c, d);
        Point l = cross_product(n1, n2);
        double mag_l = l.magnitude();
        if (mag_l < 1e-12)
        {
            return false;
        }
        Point p = normalize_point(l);
        Point neg_p = Point(-p.x, -p.y, -p.z);

        auto check_cand = [&](const Point& cand) -> bool
        {
            Point c1 = cross_product(a, cand);
            Point c2 = cross_product(cand, b);
            if (dot_product(c1, n1) > 1e-8 && dot_product(c2, n1) > 1e-8)
            {
                Point c3 = cross_product(c, cand);
                Point c4 = cross_product(cand, d);
                if (dot_product(c3, n2) > 1e-8 && dot_product(c4, n2) > 1e-8)
                {
                    return true;
                }
            }
            return false;
        };

        if (check_cand(p))
        {
            return true;
        }
        if (check_cand(neg_p))
        {
            return true;
        }
        return false;
    }

    bool ExoConsGenerator::point_near_arc(const Point& a, const Point& b, const Point& p, double max_dist_deg, double* dist_out, double min_end_dist_deg)
    {
        Point ua = normalize_point(a);
        Point ub = normalize_point(b);
        Point up = normalize_point(p);

        Point n = cross_product(ua, ub);
        double n_mag = n.magnitude();
        if (n_mag < 1e-12)
        {
            return false;
        }

        double dAB = dot_product(ua, ub);
        double dPA = dot_product(up, ua);
        double dPB = dot_product(up, ub);

        // Check if projection of p lies strictly between a and b along the arc
        if ((dPB - dAB * dPA <= 0.0) || (dPA - dAB * dPB <= 0.0))
        {
            return false;
        }

        double cross_dot = dot_product(up, n);
        double sin_dist = std::abs(cross_dot) / n_mag;
        double dist_rad = asin(std::min(1.0, sin_dist));
        double dist_deg = dist_rad * 180.0 / _pi;

        if (dist_deg > max_dist_deg)
        {
            return false;
        }

        Point n_unit = n * (1.0 / n_mag);
        Point p_proj = up - n_unit * dot_product(up, n_unit);
        double proj_mag = p_proj.magnitude();
        if (proj_mag < 1e-12)
        {
            return false;
        }
        p_proj = p_proj * (1.0 / proj_mag);

        double dot_proj_a = dot_product(p_proj, ua);
        double dot_proj_b = dot_product(p_proj, ub);
        double x_deg = acos(std::max(-1.0, std::min(1.0, dot_proj_a))) * 180.0 / _pi;
        double y_deg = acos(std::max(-1.0, std::min(1.0, dot_proj_b))) * 180.0 / _pi;

        if (x_deg < min_end_dist_deg || y_deg < min_end_dist_deg)
        {
            return false;
        }

        if (dist_out)
        {
            *dist_out = dist_deg;
        }

        return true;
    }

    std::string ExoConsGenerator::get_consline_star_name(Star* s)
    {
        if (!s)
        {
            return "";
        }
        if (s->Bayer[0])
        {
            return trim(s->Bayer);
        }
        if (s->Flamsteed[0])
        {
            return trim(s->Flamsteed);
        }
        if (s->name[0])
        {
            return trim(s->name);
        }
        if (s->HD)
        {
            return std::string("HD") + std::to_string(s->HD);
        }
        if (s->HIP)
        {
            return std::string("HIP") + std::to_string(s->HIP);
        }
        if (s->HR)
        {
            return std::string("HR") + std::to_string(s->HR);
        }
        if (s->Gliese[0])
        {
            return trim(s->Gliese);
        }
        if (s->alienorumid.size())
        {
            return trim(s->alienorumid);
        }
        return "";
    }

    static std::string extract_star_cons_abbrev(Star* s)
    {
        if (s->constellation[0])
        {
            std::string c = s->constellation;
            if (ExoConsGenerator::find_iau_def(c))
            {
                return c;
            }
        }
        if (s->Gouldcons[0])
        {
            std::string c = s->Gouldcons;
            if (ExoConsGenerator::find_iau_def(c))
            {
                return c;
            }
        }
        if (s->alienorumid.size())
        {
            std::string c = cons_from_alienorumid(s->alienorumid);
            if (ExoConsGenerator::find_iau_def(c))
            {
                return c;
            }
        }
        if (s->Bayer[0])
        {
            std::string b = s->Bayer;
            std::stringstream ss(b);
            std::string token;
            while (ss >> token)
            {
                if (token.size() == 3 && ExoConsGenerator::find_iau_def(token))
                {
                    return token;
                }
            }
        }
        if (s->Flamsteed[0])
        {
            std::string f = s->Flamsteed;
            std::stringstream ss(f);
            std::string token;
            while (ss >> token)
            {
                if (token.size() == 3 && ExoConsGenerator::find_iau_def(token))
                {
                    return token;
                }
            }
        }
        Constellation* ic = identify_cons_from_coords(s->right_ascension, s->declination);
        if (ic && ic->abbrev.size() && ExoConsGenerator::find_iau_def(ic->abbrev))
        {
            return ic->abbrev;
        }
        return "UMa";
    }

    double ExoConsGenerator::calculate_sky_coverage(const std::vector<Point>& lined_star_dirs, int num_samples)
    {
        if (lined_star_dirs.empty() || num_samples <= 0)
        {
            return 0.0;
        }

        const double cos5deg = cos(5.0 * _pi / 180.0);
        const double phi = _pi * (sqrt(5.0) - 1.0);
        int covered = 0;

        for (int i = 0; i < num_samples; i++)
        {
            double y = 1.0 - ((double)i / (double)(num_samples - 1)) * 2.0;
            double radius = sqrt(std::max(0.0, 1.0 - y * y));
            double theta = phi * (double)i;
            double x = cos(theta) * radius;
            double z = sin(theta) * radius;
            Point p(x, y, z);

            for (const auto& u : lined_star_dirs)
            {
                if (dot_product(p, u) >= cos5deg)
                {
                    covered++;
                    break;
                }
            }
        }

        return (double)covered / (double)num_samples;
    }

    bool ExoConsGenerator::to_be_generated(Star* sys_star, std::string& vantage_name_out)
    {
        if (!sys_star)
        {
            return false;
        }
        if (sys_star->cenobj && sys_star->cenobj->typeclass() == class_star)
        {
            sys_star = (Star*)sys_star->cenobj;
        }

        vantage_name_out = sys_star->name;
        if (vantage_name_out.empty())
        {
            vantage_name_out = get_consline_star_name(sys_star);
        }

        if (completed_vantages.count(vantage_name_out))
        {
            return false;
        }

        Point sys_loc = sys_star->location;

        struct VantageGroup
        {
            Point loc;
            std::string name;
            int count = 0;
        };
        std::vector<VantageGroup> groups;
        int own_count = 0;

        std::unordered_map<std::string, Point> resolved_vantages;
        resolved_vantages["Sun"] = (cels && cels[0]) ? cels[0]->location : Point(0, 0, 0);
        resolved_vantages["Sol"] = (cels && cels[0]) ? cels[0]->location : Point(0, 0, 0);

        for (auto& c : constellations)
        {
            std::string vname = c.vantage_name;
            if (!c.vantage_resolved)
            {
                if (vname.empty() || vname == "Sun" || vname == "Sol")
                {
                    c.vantage = (cels && cels[0]) ? cels[0]->location : Point(0, 0, 0);
                    c.vantage_resolved = true;
                }
                else
                {
                    auto it = resolved_vantages.find(vname);
                    if (it != resolved_vantages.end())
                    {
                        c.vantage = it->second;
                        c.vantage_resolved = true;
                    }
                    else
                    {
                        int sidx = find_object(vname.c_str(), true);
                        if (sidx >= 0 && cels && cels[sidx])
                        {
                            c.vantage = cels[sidx]->location;
                        }
                        else
                        {
                            c.vantage.x = c.vantage.y = c.vantage.z = nanf("vantage");
                        }
                        resolved_vantages[vname] = c.vantage;
                        c.vantage_resolved = true;
                    }
                }
            }

            Point vpt = c.vantage;
            if (std::isnan(vpt.x))
            {
                continue;
            }

            if (vname.empty() && vpt.distance_to(Point(0, 0, 0)) < light_year * 0.1)
            {
                vname = "Sun";
            }

            bool is_same_vantage = (!c.vantage_name.empty() && c.vantage_name == vantage_name_out);
            if (c.vantage_name.empty() && (vantage_name_out == "Sun" || vantage_name_out == "Sol"))
            {
                is_same_vantage = true;
            }
            if (vpt.distance_to(sys_loc) < light_year * 0.1 || is_same_vantage)
            {
                own_count++;
            }

            bool matched_group = false;
            for (auto& g : groups)
            {
                if ((!vname.empty() && !g.name.empty() && g.name == vname) || g.loc.distance_to(vpt) < light_year * 0.1)
                {
                    g.count++;
                    if (g.name.empty() && !vname.empty())
                    {
                        g.name = vname;
                    }
                    matched_group = true;
                    break;
                }
            }
            if (!matched_group)
            {
                groups.push_back({vpt, vname, 1});
            }
        }

        if (own_count >= 30)
        {
            return false;
        }

        for (const auto& g : groups)
        {
            if (g.count >= 30)
            {
                double dist = g.loc.distance_to(sys_loc);
                if (dist < light_year * 10.0)
                {
                    return false;
                }
            }
        }

        return true;
    }

    void ExoConsGenerator::generate_constellations(Star* sys_star, std::vector<Constellation>& out_conss)
    {
        if (!show_consln) return;           // Only generate if showing cons lines, that way the file doesn't grow huge if you star hop in realism mode.

        out_conss.clear();
        if (!sys_star)
        {
            return;
        }
        if (sys_star->cenobj && sys_star->cenobj->typeclass() == class_star)
        {
            sys_star = (Star*)sys_star->cenobj;
        }

        std::string sys_name = sys_star->name;
        if (sys_name.empty())
        {
            sys_name = get_consline_star_name(sys_star);
        }

        CelestialLocation vantage_loc = sys_star->location;
        Point vantage_pt = sys_star->location;
        CelestialObject* local_cenobj = sys_star->cenobj ? sys_star->cenobj : sys_star;

        // 1. Identify existing lined stars and lines for this vantage
        std::unordered_set<Star*> existing_lined_stars;
        std::vector<std::pair<Point, Point>> existing_lines;
        std::unordered_set<std::string> existing_cons_abbrevs;

        for (const auto& c : constellations)
        {
            bool is_match = (c.vantage.distance_to(vantage_pt) < light_year * 0.1)
                || (!c.vantage_name.empty() && c.vantage_name == sys_name);
            if (is_match && c.lines.size() > 0)
            {
                existing_cons_abbrevs.insert(c.abbrev);
                for (const auto& cl : c.lines)
                {
                    if (cl.a && cl.b)
                    {
                        existing_lined_stars.insert(cl.a);
                        existing_lined_stars.insert(cl.b);

                        Point rel_a = (Point)cl.a->location - vantage_pt;
                        Point rel_b = (Point)cl.b->location - vantage_pt;
                        existing_lines.push_back(std::make_pair(normalize_point(rel_a), normalize_point(rel_b)));
                    }
                }
            }
        }

        // 2. Collect candidate stars outside the local system
        std::vector<ExoConsStarInfo> candidates;
        candidates.reserve(8192);
        std::unordered_map<Star*, ExoConsStarInfo> star_info_map;

        for (int i = 0; cels[i]; i++)
        {
            if (cels[i]->deleted || cels[i]->typeclass() != class_star)
            {
                continue;
            }
            Star* s = (Star*)cels[i];
            if (s == sys_star || s == local_cenobj || s->cenobj == local_cenobj || s->cenobj == sys_star)
            {
                continue;
            }

            // Exclude secondary companion stars if primary star is present and brighter
            if (s->cenobj && s->cenobj != s && s->cenobj->typeclass() == class_star)
            {
                Star* primary = (Star*)s->cenobj;
                if (primary->viewer_magnitude(vantage_loc) <= s->viewer_magnitude(vantage_loc))
                {
                    continue;
                }
            }

            double mag = s->viewer_magnitude(vantage_loc);
            if (std::isnan(mag) || std::isinf(mag) || mag > 8.0)
            {
                continue;
            }

            Point rel = (Point)s->location - vantage_pt;
            double dist = rel.magnitude();
            if (dist < 1e-9)
            {
                continue;
            }

            ExoConsStarInfo info;
            info.s = s;
            info.mag = mag;
            info.u = normalize_point(rel);
            info.orig_cons = extract_star_cons_abbrev(s);
            candidates.push_back(info);
            star_info_map[s] = info;
        }

        // 3. Inclusion criteria:
        std::vector<ExoConsStarInfo> included;
        std::unordered_set<Star*> included_set;

        // Pass 1: Mag < 4.0
        for (const auto& c : candidates)
        {
            if (c.mag < 4.0 && !existing_lined_stars.count(c.s))
            {
                included.push_back(c);
                included_set.insert(c.s);
            }
        }

        // Pass 2: Mag < 5.0 and close (<= 4.0 deg) to an already included star
        const double cos4deg = cos(4.0 * _pi / 180.0);
        for (int iter = 0; iter < 2; iter++)
        {
            for (const auto& c : candidates)
            {
                if (c.mag < 5.0 && !included_set.count(c.s) && !existing_lined_stars.count(c.s))
                {
                    for (const auto& inc : included)
                    {
                        if (dot_product(c.u, inc.u) >= cos4deg)
                        {
                            included.push_back(c);
                            included_set.insert(c.s);
                            break;
                        }
                    }
                }
            }
        }

        // Pass 3: More than 7 degrees from a lined star and the brightest within a 2 degree radius
        const double cos7deg = cos(7.0 * _pi / 180.0);
        const double cos2deg = cos(2.0 * _pi / 180.0);

        for (const auto& c : candidates)
        {
            if (included_set.count(c.s) || existing_lined_stars.count(c.s) || c.mag > 6.0)
            {
                continue;
            }

            bool within_7deg = false;
            for (const auto& inc : included)
            {
                if (dot_product(c.u, inc.u) > cos7deg)
                {
                    within_7deg = true;
                    break;
                }
            }
            if (!within_7deg)
            {
                for (Star* es : existing_lined_stars)
                {
                    Point eu = normalize_point((Point)es->location - vantage_pt);
                    if (dot_product(c.u, eu) > cos7deg)
                    {
                        within_7deg = true;
                        break;
                    }
                }
            }

            if (!within_7deg)
            {
                bool is_brightest = true;
                for (const auto& other : candidates)
                {
                    if (other.s != c.s && dot_product(c.u, other.u) >= cos2deg)
                    {
                        if (other.mag < c.mag - 0.01)
                        {
                            is_brightest = false;
                            break;
                        }
                    }
                }
                if (is_brightest)
                {
                    included.push_back(c);
                    included_set.insert(c.s);
                }
            }
        }

        // 4. Line formation
        const double cos15deg = cos(15.0 * _pi / 180.0);
        std::vector<std::pair<ExoConsStarInfo, ExoConsStarInfo>> all_lines;
        std::unordered_map<Star*, int> degrees;

        std::unordered_map<std::string, std::vector<ExoConsStarInfo>> by_cons;
        for (const auto& c : included)
        {
            by_cons[c.orig_cons].push_back(c);
        }

        for (auto& pair : by_cons)
        {
            std::sort(pair.second.begin(), pair.second.end(), [](const ExoConsStarInfo& a, const ExoConsStarInfo& b)
            {
                return a.mag < b.mag;
            });
        }

        // Connect intra-constellation lines first
        for (const auto& pair : by_cons)
        {
            if (existing_cons_abbrevs.count(pair.first))
            {
                continue;
            }
            const auto& c_stars = pair.second;
            int n_stars = (int)c_stars.size();
            if (n_stars < 2)
            {
                continue;
            }

            for (int i = 0; i < n_stars; i++)
            {
                const auto& u1 = c_stars[i];
                if (degrees[u1.s] >= 3)
                {
                    continue;
                }

                int best_match_idx = -1;
                double best_score = 1e9;

                for (int j = 0; j < n_stars; j++)
                {
                    if (i == j)
                    {
                        continue;
                    }
                    const auto& u2 = c_stars[j];
                    if (degrees[u2.s] >= 3)
                    {
                        continue;
                    }

                    bool already_connected = false;
                    for (const auto& l : all_lines)
                    {
                        if ((l.first.s == u1.s && l.second.s == u2.s) || (l.first.s == u2.s && l.second.s == u1.s))
                        {
                            already_connected = true;
                            break;
                        }
                    }
                    if (already_connected)
                    {
                        continue;
                    }

                    if (dot_product(u1.u, u2.u) < cos15deg)
                    {
                        continue;
                    }

                    double d_deg = ang_dist_deg(u1.u, u2.u);
                    double dm = std::abs(u1.mag - u2.mag);
                    double score = d_deg + 0.5 * (u1.mag + u2.mag) + 1.2 * dm;
                    if (score < best_score)
                    {
                        bool crosses = false;
                        for (const auto& el : existing_lines)
                        {
                            if (arcs_intersect(u1.u, u2.u, el.first, el.second))
                            {
                                crosses = true;
                                break;
                            }
                        }
                        if (!crosses)
                        {
                            for (const auto& al : all_lines)
                            {
                                if (al.first.s == u1.s || al.first.s == u2.s || al.second.s == u1.s || al.second.s == u2.s)
                                {
                                    continue;
                                }
                                if (arcs_intersect(u1.u, u2.u, al.first.u, al.second.u))
                                {
                                    crosses = true;
                                    break;
                                }
                            }
                        }

                        if (!crosses)
                        {
                            best_score = score;
                            best_match_idx = j;
                        }
                    }
                }

                if (best_match_idx >= 0)
                {
                    all_lines.push_back({u1, c_stars[best_match_idx]});
                    degrees[u1.s]++;
                    degrees[c_stars[best_match_idx].s]++;
                }
            }
        }

        auto check_line_valid = [&](const Point& u1, const Point& u2, Star* s1, Star* s2) -> bool
        {
            if (s1 == s2 || dot_product(u1, u2) < cos15deg || dot_product(u1, u2) > 0.99999)
            {
                return false;
            }
            for (const auto& al : all_lines)
            {
                if ((al.first.s == s1 && al.second.s == s2) || (al.first.s == s2 && al.second.s == s1))
                {
                    return false;
                }
            }
            for (const auto& el : existing_lines)
            {
                if (arcs_intersect(u1, u2, el.first, el.second))
                {
                    return false;
                }
            }
            for (const auto& al : all_lines)
            {
                if (al.first.s == s1 || al.first.s == s2 || al.second.s == s1 || al.second.s == s2)
                {
                    continue;
                }
                if (arcs_intersect(u1, u2, al.first.u, al.second.u))
                {
                    return false;
                }
            }
            return true;
        };

        auto try_connect_star = [&](const ExoConsStarInfo& u1, int max_degree) -> bool
        {
            int best_cand_idx = -1;
            bool from_included = true;
            double best_score = 1e9;

            for (size_t k = 0; k < included.size(); k++)
            {
                const auto& u2 = included[k];
                if (u2.s == u1.s || degrees[u2.s] >= max_degree) continue;
                if (!check_line_valid(u1.u, u2.u, u1.s, u2.s)) continue;

                double d_deg = ang_dist_deg(u1.u, u2.u);
                double dm = std::abs(u1.mag - u2.mag);
                double score = d_deg + 0.5 * (u1.mag + u2.mag) + 1.2 * dm;
                if (score < best_score)
                {
                    best_score = score;
                    best_cand_idx = (int)k;
                    from_included = true;
                }
            }

            if (best_cand_idx < 0)
            {
                for (size_t k = 0; k < candidates.size(); k++)
                {
                    const auto& u2 = candidates[k];
                    if (u2.s == u1.s || degrees[u2.s] >= max_degree || u2.mag > 6.0) continue;
                    if (!check_line_valid(u1.u, u2.u, u1.s, u2.s)) continue;

                    double d_deg = ang_dist_deg(u1.u, u2.u);
                    double dm = std::abs(u1.mag - u2.mag);
                    double score = d_deg + 0.5 * (u1.mag + u2.mag) + 1.2 * dm;
                    if (score < best_score)
                    {
                        best_score = score;
                        best_cand_idx = (int)k;
                        from_included = false;
                    }
                }
            }

            if (best_cand_idx >= 0)
            {
                ExoConsStarInfo neighbor = from_included ? included[best_cand_idx] : candidates[best_cand_idx];
                all_lines.push_back({u1, neighbor});
                degrees[u1.s]++;
                degrees[neighbor.s]++;
                if (!included_set.count(neighbor.s))
                {
                    included.push_back(neighbor);
                    included_set.insert(neighbor.s);
                }
                return true;
            }
            return false;
        };

        // Guarantee that no star brighter than magnitude 3 gets ignored for line joining
        std::vector<ExoConsStarInfo> bright_stars;
        for (const auto& c : candidates)
        {
            if (c.mag < 3.0 && !existing_lined_stars.count(c.s))
            {
                bright_stars.push_back(c);
            }
        }
        std::sort(bright_stars.begin(), bright_stars.end(), [](const ExoConsStarInfo& a, const ExoConsStarInfo& b)
        {
            return a.mag < b.mag;
        });

        for (const auto& bs : bright_stars)
        {
            if (degrees[bs.s] == 0)
            {
                if (!try_connect_star(bs, 3))
                {
                    if (!try_connect_star(bs, 4))
                    {
                        try_connect_star(bs, 5);
                    }
                }
            }
            if (degrees[bs.s] == 1)
            {
                try_connect_star(bs, 3);
            }
        }

        // Inter-constellation connections to ensure at least 50 constellations
        for (const auto& pair : by_cons)
        {
            if (existing_cons_abbrevs.count(pair.first))
            {
                continue;
            }
            bool has_line = false;
            for (const auto& l : all_lines)
            {
                if (l.first.orig_cons == pair.first || l.second.orig_cons == pair.first)
                {
                    has_line = true;
                    break;
                }
            }

            if (!has_line && pair.second.size() > 0)
            {
                const auto& u1 = pair.second[0];
                if (degrees[u1.s] >= 3)
                {
                    continue;
                }

                int best_match_idx = -1;
                double best_score = 1e9;

                for (size_t k = 0; k < included.size(); k++)
                {
                    const auto& u2 = included[k];
                    if (u2.s == u1.s || degrees[u2.s] >= 3)
                    {
                        continue;
                    }
                    if (dot_product(u1.u, u2.u) < cos15deg)
                    {
                        continue;
                    }

                    double d_deg = ang_dist_deg(u1.u, u2.u);
                    double dm = std::abs(u1.mag - u2.mag);
                    double score = d_deg + 0.8 * (u1.mag + u2.mag) + 1.2 * dm;
                    if (score < best_score)
                    {
                        bool crosses = false;
                        for (const auto& el : existing_lines)
                        {
                            if (arcs_intersect(u1.u, u2.u, el.first, el.second))
                            {
                                crosses = true;
                                break;
                            }
                        }
                        if (!crosses)
                        {
                            for (const auto& al : all_lines)
                            {
                                if (al.first.s == u1.s || al.first.s == u2.s || al.second.s == u1.s || al.second.s == u2.s)
                                {
                                    continue;
                                }
                                if (arcs_intersect(u1.u, u2.u, al.first.u, al.second.u))
                                {
                                    crosses = true;
                                    break;
                                }
                            }
                        }

                        if (!crosses)
                        {
                            best_score = score;
                            best_match_idx = (int)k;
                        }
                    }
                }

                if (best_match_idx >= 0)
                {
                    all_lines.push_back({u1, included[best_match_idx]});
                    degrees[u1.s]++;
                    degrees[included[best_match_idx].s]++;
                }
            }
        }

        // Sky coverage enforcement (>= 80% within 5 degrees)
        std::vector<Point> lined_dirs;
        auto refresh_lined_dirs = [&]()
        {
            lined_dirs.clear();
            std::unordered_set<Star*> ls;
            for (Star* es : existing_lined_stars)
            {
                ls.insert(es);
                Point eu = normalize_point((Point)es->location - vantage_pt);
                lined_dirs.push_back(eu);
            }
            for (const auto& l : all_lines)
            {
                if (!ls.count(l.first.s))
                {
                    ls.insert(l.first.s);
                    lined_dirs.push_back(l.first.u);
                }
                if (!ls.count(l.second.s))
                {
                    ls.insert(l.second.s);
                    lined_dirs.push_back(l.second.u);
                }
            }
        };

        refresh_lined_dirs();
        double current_cov = calculate_sky_coverage(lined_dirs, 1000);

        const double phi = _pi * (sqrt(5.0) - 1.0);
        int max_gap_iterations = 200;

        while (current_cov < 0.80 && max_gap_iterations-- > 0)
        {
            Point worst_p;
            double worst_dist = -1.0;

            for (int i = 0; i < 1000; i++)
            {
                double y = 1.0 - ((double)i / 999.0) * 2.0;
                double radius = sqrt(std::max(0.0, 1.0 - y * y));
                double theta = phi * (double)i;
                Point p(cos(theta) * radius, y, sin(theta) * radius);

                double min_d = 1e9;
                for (const auto& u : lined_dirs)
                {
                    double d = ang_dist_deg(p, u);
                    if (d < min_d)
                    {
                        min_d = d;
                    }
                }
                if (min_d > worst_dist)
                {
                    worst_dist = min_d;
                    worst_p = p;
                }
            }

            if (worst_dist <= 5.0)
            {
                break;
            }

            int best_cand_idx = -1;
            double best_cand_dist = 1e9;
            for (size_t i = 0; i < candidates.size(); i++)
            {
                if (degrees[candidates[i].s] >= 3)
                {
                    continue;
                }
                double d = ang_dist_deg(worst_p, candidates[i].u);
                if (d < best_cand_dist && candidates[i].mag < 6.5)
                {
                    best_cand_dist = d;
                    best_cand_idx = (int)i;
                }
            }

            if (best_cand_idx < 0)
            {
                break;
            }

            const auto& cand_star = candidates[best_cand_idx];
            int best_neighbor_idx = -1;
            double best_n_dist = 1e9;

            for (size_t j = 0; j < candidates.size(); j++)
            {
                if ((int)j == best_cand_idx || degrees[candidates[j].s] >= 3)
                {
                    continue;
                }
                if (dot_product(cand_star.u, candidates[j].u) < cos15deg)
                {
                    continue;
                }
                double d = ang_dist_deg(cand_star.u, candidates[j].u);
                if (d < best_n_dist)
                {
                    bool crosses = false;
                    for (const auto& el : existing_lines)
                    {
                        if (arcs_intersect(cand_star.u, candidates[j].u, el.first, el.second))
                        {
                            crosses = true;
                            break;
                        }
                    }
                    if (!crosses)
                    {
                        for (const auto& al : all_lines)
                        {
                            if (al.first.s == cand_star.s || al.first.s == candidates[j].s || al.second.s == cand_star.s || al.second.s == candidates[j].s)
                            {
                                continue;
                            }
                            if (arcs_intersect(cand_star.u, candidates[j].u, al.first.u, al.second.u))
                            {
                                crosses = true;
                                break;
                            }
                        }
                    }

                    if (!crosses)
                    {
                        best_n_dist = d;
                        best_neighbor_idx = (int)j;
                    }
                }
            }

            if (best_neighbor_idx >= 0)
            {
                all_lines.push_back({cand_star, candidates[best_neighbor_idx]});
                degrees[cand_star.s]++;
                degrees[candidates[best_neighbor_idx].s]++;
                refresh_lined_dirs();
                current_cov = calculate_sky_coverage(lined_dirs, 1000);
            }
            else
            {
                break;
            }
        }

        // Assign constellation abbreviations to current lines
        std::vector<std::string> line_assigned_cons(all_lines.size());
        for (size_t i = 0; i < all_lines.size(); i++)
        {
            std::string c_abbrev = all_lines[i].first.orig_cons;
            if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
            {
                c_abbrev = all_lines[i].second.orig_cons;
            }
            if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
            {
                for (int k = 0; k < 88; k++)
                {
                    std::string cand_abbr = iau_constellations[k].abbrev;
                    if (!existing_cons_abbrevs.count(cand_abbr))
                    {
                        c_abbrev = cand_abbr;
                        break;
                    }
                }
            }
            line_assigned_cons[i] = c_abbrev;
        }

        std::unordered_map<std::string, std::vector<size_t>> cons_to_lines;
        for (size_t i = 0; i < all_lines.size(); i++)
        {
            cons_to_lines[line_assigned_cons[i]].push_back(i);
        }

        // Phase 1: Connect little clusters (< 3 lines) to nearby clusters and merge them
        std::vector<std::string> cons_list;
        for (const auto& pair : cons_to_lines)
        {
            cons_list.push_back(pair.first);
        }

        for (size_t i = 0; i < cons_list.size(); i++)
        {
            std::string c_a = cons_list[i];
            if (cons_to_lines[c_a].empty() || cons_to_lines[c_a].size() >= 3)
            {
                continue;
            }

            int active_count = 0;
            for (const auto& pair : cons_to_lines)
            {
                if (!pair.second.empty()) active_count++;
            }
            if (active_count <= 55)
            {
                break;
            }

            int best_match_b = -1;
            Star* best_sa = nullptr;
            Star* best_sb = nullptr;
            double best_dist = 1e9;

            for (size_t j = 0; j < cons_list.size(); j++)
            {
                if (i == j) continue;
                std::string c_b = cons_list[j];
                if (cons_to_lines[c_b].empty() || cons_to_lines[c_b].size() >= 3) continue;

                for (size_t lidx_a : cons_to_lines[c_a])
                {
                    Star* sa_cands[2] = {all_lines[lidx_a].first.s, all_lines[lidx_a].second.s};
                    for (Star* sa : sa_cands)
                    {
                        if (degrees[sa] >= 3) continue;
                        auto it_a = star_info_map.find(sa);
                        if (it_a == star_info_map.end()) continue;
                        const auto& info_a = it_a->second;

                        for (size_t lidx_b : cons_to_lines[c_b])
                        {
                            Star* sb_cands[2] = {all_lines[lidx_b].first.s, all_lines[lidx_b].second.s};
                            for (Star* sb : sb_cands)
                            {
                                if (degrees[sb] >= 3) continue;
                                auto it_b = star_info_map.find(sb);
                                if (it_b == star_info_map.end()) continue;
                                const auto& info_b = it_b->second;

                                if (check_line_valid(info_a.u, info_b.u, sa, sb))
                                {
                                    double d_deg = ang_dist_deg(info_a.u, info_b.u);
                                    double dm = std::abs(info_a.mag - info_b.mag);
                                    double score = d_deg + 1.2 * dm;
                                    if (score < best_dist)
                                    {
                                        best_dist = score;
                                        best_match_b = (int)j;
                                        best_sa = sa;
                                        best_sb = sb;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (best_match_b >= 0)
            {
                std::string c_b = cons_list[best_match_b];
                const auto& info_a = star_info_map[best_sa];
                const auto& info_b = star_info_map[best_sb];
                all_lines.push_back({info_a, info_b});
                line_assigned_cons.push_back(c_a);
                degrees[best_sa]++;
                degrees[best_sb]++;
                cons_to_lines[c_a].push_back(all_lines.size() - 1);

                for (size_t lidx : cons_to_lines[c_b])
                {
                    line_assigned_cons[lidx] = c_a;
                    cons_to_lines[c_a].push_back(lidx);
                }
                cons_to_lines[c_b].clear();
            }
        }

        // Phase 2: Expand constellations that still have < 3 lines to form normal sized constellations
        for (const auto& c_name : cons_list)
        {
            if (cons_to_lines[c_name].empty()) continue;

            int attempts = 0;
            while (cons_to_lines[c_name].size() < 3 && attempts++ < 15)
            {
                std::unordered_set<Star*> c_stars_set;
                for (size_t lidx : cons_to_lines[c_name])
                {
                    c_stars_set.insert(all_lines[lidx].first.s);
                    c_stars_set.insert(all_lines[lidx].second.s);
                }

                bool added = false;
                // First try connecting two stars in the same constellation if not already connected
                for (Star* s1 : c_stars_set)
                {
                    if (degrees[s1] >= 3) continue;
                    auto it1 = star_info_map.find(s1);
                    if (it1 == star_info_map.end()) continue;

                    for (Star* s2 : c_stars_set)
                    {
                        if (s1 == s2 || degrees[s2] >= 3) continue;
                        auto it2 = star_info_map.find(s2);
                        if (it2 == star_info_map.end()) continue;

                        if (check_line_valid(it1->second.u, it2->second.u, s1, s2))
                        {
                            all_lines.push_back({it1->second, it2->second});
                            line_assigned_cons.push_back(c_name);
                            degrees[s1]++;
                            degrees[s2]++;
                            cons_to_lines[c_name].push_back(all_lines.size() - 1);
                            added = true;
                            break;
                        }
                    }
                    if (added) break;
                }

                // If no internal connection, connect to a candidate star
                if (!added)
                {
                    for (Star* cs : c_stars_set)
                    {
                        if (degrees[cs] >= 3) continue;
                        auto it_cs = star_info_map.find(cs);
                        if (it_cs == star_info_map.end()) continue;
                        const auto& cs_info = it_cs->second;

                        int best_cand = -1;
                        double best_score = 1e9;
                        for (size_t k = 0; k < candidates.size(); k++)
                        {
                            const auto& cand = candidates[k];
                            if (cand.s == cs || c_stars_set.count(cand.s) || degrees[cand.s] >= 3 || cand.mag > 6.0) continue;
                            if (!check_line_valid(cs_info.u, cand.u, cs, cand.s)) continue;

                            // Check constellation expanse constraint (keep constellation span within empirical bounds <= 35.0 deg)
                            bool exceeds_expanse = false;
                            for (Star* exist_s : c_stars_set)
                            {
                                auto it_ex = star_info_map.find(exist_s);
                                if (it_ex != star_info_map.end())
                                {
                                    if (ang_dist_deg(it_ex->second.u, cand.u) > 35.0)
                                    {
                                        exceeds_expanse = true;
                                        break;
                                    }
                                }
                            }
                            if (exceeds_expanse) continue;

                            double d_deg = ang_dist_deg(cs_info.u, cand.u);
                            double dm = std::abs(cs_info.mag - cand.mag);
                            double score = d_deg + 0.5 * cand.mag + 1.2 * dm;
                            if (score < best_score)
                            {
                                best_score = score;
                                best_cand = (int)k;
                            }
                        }

                        if (best_cand >= 0)
                        {
                            const auto& cand_info = candidates[best_cand];
                            all_lines.push_back({cs_info, cand_info});
                            line_assigned_cons.push_back(c_name);
                            degrees[cs]++;
                            degrees[cand_info.s]++;
                            cons_to_lines[c_name].push_back(all_lines.size() - 1);
                            if (!included_set.count(cand_info.s))
                            {
                                included.push_back(cand_info);
                                included_set.insert(cand_info.s);
                            }
                            added = true;
                            break;
                        }
                    }
                }

                if (!added) break;
            }
        }

        // Phase 3: Final check to guarantee that no star brighter than magnitude 3 was omitted
        for (const auto& bs : bright_stars)
        {
            if (degrees[bs.s] == 0)
            {
                if (!try_connect_star(bs, 3))
                {
                    if (!try_connect_star(bs, 4))
                    {
                        try_connect_star(bs, 5);
                    }
                }
            }
        }

        // Phase 4: Passerby line rerouting through impinging stars
        // If a line passes a degree or two from a bright star or a star that has lines,
        // have the passerby line connect to the impinging star.
        for (Star* es : existing_lined_stars)
        {
            if (star_info_map.find(es) == star_info_map.end())
            {
                ExoConsStarInfo einfo;
                einfo.s = es;
                Point rel = (Point)es->location - vantage_pt;
                einfo.u = normalize_point(rel);
                einfo.mag = es->viewer_magnitude(vantage_loc);
                einfo.orig_cons = extract_star_cons_abbrev(es);
                candidates.push_back(einfo);
                star_info_map[es] = einfo;
            }
        }

        while (line_assigned_cons.size() < all_lines.size())
        {
            size_t idx = line_assigned_cons.size();
            std::string c_abbr = all_lines[idx].first.orig_cons;
            if (c_abbr.empty() || existing_cons_abbrevs.count(c_abbr))
            {
                c_abbr = all_lines[idx].second.orig_cons;
            }
            line_assigned_cons.push_back(c_abbr);
        }

        const double max_impinge_dist_deg = 2.0;
        const double cos17deg = cos(17.0 * _pi / 180.0);
        const double cos30_5deg = cos(30.5 * _pi / 180.0);

        auto is_segment_valid = [&](Star* s1, Star* s2, const Point& u1, const Point& u2, size_t ignore_line_idx) -> bool
        {
            if (s1 == s2) return false;
            double dp = dot_product(u1, u2);
            if (dp < cos15deg || dp > 0.99999) return false;

            for (const auto& el : existing_lines)
            {
                if (dot_product(u1, el.first) < cos30_5deg) continue;
                if (arcs_intersect(u1, u2, el.first, el.second))
                {
                    return false;
                }
            }
            for (size_t k = 0; k < all_lines.size(); k++)
            {
                if (k == ignore_line_idx) continue;
                const auto& al = all_lines[k];
                if (al.first.s == s1 || al.first.s == s2 || al.second.s == s1 || al.second.s == s2)
                {
                    continue;
                }
                if (dot_product(u1, al.first.u) < cos30_5deg) continue;
                if (arcs_intersect(u1, u2, al.first.u, al.second.u))
                {
                    return false;
                }
            }
            return true;
        };

        struct ImpingingCand
        {
            int cand_idx;
            double dist_deg;
        };

        int max_reroute_passes = 5;
        for (int pass = 0; pass < max_reroute_passes; pass++)
        {
            std::vector<ExoConsStarInfo> impinging_pool;
            for (const auto& cand : candidates)
            {
                if (degrees[cand.s] > 0 || existing_lined_stars.count(cand.s) || cand.mag < 4.0)
                {
                    impinging_pool.push_back(cand);
                }
            }

            bool any_rerouted = false;
            size_t num_lines_to_check = all_lines.size();

            for (size_t i = 0; i < num_lines_to_check; i++)
            {
                Star* sa = all_lines[i].first.s;
                Star* sb = all_lines[i].second.s;
                const Point& ua = all_lines[i].first.u;
                const Point& ub = all_lines[i].second.u;

                std::vector<ImpingingCand> impinging;

                for (size_t c_idx = 0; c_idx < impinging_pool.size(); c_idx++)
                {
                    const auto& cand = impinging_pool[c_idx];
                    Star* sp = cand.s;
                    if (sp == sa || sp == sb) continue;

                    if (dot_product(ua, cand.u) < cos17deg) continue;

                    double dist_deg = 0.0;
                    if (point_near_arc(ua, ub, cand.u, max_impinge_dist_deg, &dist_deg, 0.8))
                    {
                        impinging.push_back({(int)c_idx, dist_deg});
                    }
                }

                if (impinging.empty()) continue;

                std::sort(impinging.begin(), impinging.end(), [](const ImpingingCand& x, const ImpingingCand& y) {
                    return x.dist_deg < y.dist_deg;
                });

                for (const auto& imp : impinging)
                {
                    const auto& cand_p = impinging_pool[imp.cand_idx];
                    Star* sp = cand_p.s;
                    const Point& up = cand_p.u;

                    bool has_ap = false;
                    bool has_pb = false;
                    for (size_t k = 0; k < all_lines.size(); k++)
                    {
                        if ((all_lines[k].first.s == sa && all_lines[k].second.s == sp) ||
                            (all_lines[k].first.s == sp && all_lines[k].second.s == sa))
                        {
                            has_ap = true;
                        }
                        if ((all_lines[k].first.s == sp && all_lines[k].second.s == sb) ||
                            (all_lines[k].first.s == sb && all_lines[k].second.s == sp))
                        {
                            has_pb = true;
                        }
                    }

                    bool ap_valid = has_ap || is_segment_valid(sa, sp, ua, up, i);
                    bool pb_valid = has_pb || is_segment_valid(sp, sb, up, ub, i);

                    if (ap_valid && pb_valid)
                    {
                        std::string c_abbrev = (i < line_assigned_cons.size()) ? line_assigned_cons[i] : all_lines[i].first.orig_cons;
                        if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
                        {
                            c_abbrev = all_lines[i].second.orig_cons;
                        }

                        ExoConsStarInfo info_a = all_lines[i].first;
                        ExoConsStarInfo info_b = all_lines[i].second;

                        if (!has_ap && !has_pb)
                        {
                            all_lines[i] = {info_a, cand_p};
                            line_assigned_cons[i] = c_abbrev;
                            all_lines.push_back({cand_p, info_b});
                            line_assigned_cons.push_back(c_abbrev);
                            degrees[sp] += 2;
                            if (!included_set.count(sp))
                            {
                                included.push_back(cand_p);
                                included_set.insert(sp);
                            }
                            any_rerouted = true;
                            break;
                        }
                        else if (!has_ap && has_pb)
                        {
                            all_lines[i] = {info_a, cand_p};
                            line_assigned_cons[i] = c_abbrev;
                            degrees[sb]--;
                            degrees[sp]++;
                            if (!included_set.count(sp))
                            {
                                included.push_back(cand_p);
                                included_set.insert(sp);
                            }
                            any_rerouted = true;
                            break;
                        }
                        else if (has_ap && !has_pb)
                        {
                            all_lines[i] = {cand_p, info_b};
                            line_assigned_cons[i] = c_abbrev;
                            degrees[sa]--;
                            degrees[sp]++;
                            if (!included_set.count(sp))
                            {
                                included.push_back(cand_p);
                                included_set.insert(sp);
                            }
                            any_rerouted = true;
                            break;
                        }
                        else if (has_ap && has_pb)
                        {
                            all_lines.erase(all_lines.begin() + i);
                            line_assigned_cons.erase(line_assigned_cons.begin() + i);
                            degrees[sa]--;
                            degrees[sb]--;
                            any_rerouted = true;
                            i--;
                            num_lines_to_check--;

                            int c_count = 0;
                            for (const auto& ca : line_assigned_cons)
                            {
                                if (ca == c_abbrev) c_count++;
                            }
                            if (c_count < 3)
                            {
                                Star* star_nodes[3] = {sa, sb, sp};
                                for (Star* sn : star_nodes)
                                {
                                    if (degrees[sn] >= 3) continue;
                                    auto it_sn = star_info_map.find(sn);
                                    if (it_sn == star_info_map.end()) continue;

                                    int best_k = -1;
                                    double best_sc = 1e9;
                                    for (size_t k = 0; k < candidates.size(); k++)
                                    {
                                        const auto& cand = candidates[k];
                                        if (cand.s == sn || degrees[cand.s] >= 3 || cand.mag > 6.0) continue;
                                        if (!is_segment_valid(sn, cand.s, it_sn->second.u, cand.u, all_lines.size())) continue;

                                        double d_deg = ang_dist_deg(it_sn->second.u, cand.u);
                                        double dm = std::abs(it_sn->second.mag - cand.mag);
                                        double sc = d_deg + 0.5 * cand.mag + 1.2 * dm;
                                        if (sc < best_sc)
                                        {
                                            best_sc = sc;
                                            best_k = (int)k;
                                        }
                                    }
                                    if (best_k >= 0)
                                    {
                                        const auto& cand_info = candidates[best_k];
                                        all_lines.push_back({it_sn->second, cand_info});
                                        line_assigned_cons.push_back(c_abbrev);
                                        degrees[sn]++;
                                        degrees[cand_info.s]++;
                                        if (!included_set.count(cand_info.s))
                                        {
                                            included.push_back(cand_info);
                                            included_set.insert(cand_info.s);
                                        }
                                        break;
                                    }
                                }
                            }
                            break;
                        }
                    }
                    else
                    {
                        // Direct rerouting through sp crosses another line.
                        // Attempt to eliminate this passerby line (sa, sb) that passes near sp.
                        std::string c_abbrev = (i < line_assigned_cons.size()) ? line_assigned_cons[i] : all_lines[i].first.orig_cons;
                        if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
                        {
                            c_abbrev = all_lines[i].second.orig_cons;
                        }

                        bool can_remove = false;
                        if (degrees[sa] >= 2 && degrees[sb] >= 2)
                        {
                            can_remove = true;
                        }
                        else
                        {
                            // One endpoint has degree 1, try to find an alternative connection for it
                            Star* deg1_star = (degrees[sa] == 1) ? sa : sb;
                            auto it_ds = star_info_map.find(deg1_star);
                            if (it_ds != star_info_map.end())
                            {
                                int best_k = -1;
                                double best_sc = 1e9;
                                for (size_t k = 0; k < candidates.size(); k++)
                                {
                                    const auto& cand = candidates[k];
                                    if (cand.s == deg1_star || cand.s == sa || cand.s == sb || degrees[cand.s] >= 3 || cand.mag > 6.0) continue;
                                    if (!is_segment_valid(deg1_star, cand.s, it_ds->second.u, cand.u, i)) continue;

                                    double d_deg = ang_dist_deg(it_ds->second.u, cand.u);
                                    double dm = std::abs(it_ds->second.mag - cand.mag);
                                    double sc = d_deg + 0.5 * cand.mag + 1.2 * dm;
                                    if (sc < best_sc)
                                    {
                                        best_sc = sc;
                                        best_k = (int)k;
                                    }
                                }
                                if (best_k >= 0)
                                {
                                    const auto& cand_info = candidates[best_k];
                                    all_lines.push_back({it_ds->second, cand_info});
                                    line_assigned_cons.push_back(c_abbrev);
                                    degrees[deg1_star]++;
                                    degrees[cand_info.s]++;
                                    if (!included_set.count(cand_info.s))
                                    {
                                        included.push_back(cand_info);
                                        included_set.insert(cand_info.s);
                                    }
                                    can_remove = true;
                                }
                            }
                        }

                        if (can_remove)
                        {
                            all_lines.erase(all_lines.begin() + i);
                            line_assigned_cons.erase(line_assigned_cons.begin() + i);
                            degrees[sa]--;
                            degrees[sb]--;
                            any_rerouted = true;
                            i--;
                            num_lines_to_check--;

                            int c_count = 0;
                            for (const auto& ca : line_assigned_cons)
                            {
                                if (ca == c_abbrev) c_count++;
                            }
                            if (c_count < 3)
                            {
                                Star* star_nodes[2] = {sa, sb};
                                for (Star* sn : star_nodes)
                                {
                                    if (degrees[sn] >= 3) continue;
                                    auto it_sn = star_info_map.find(sn);
                                    if (it_sn == star_info_map.end()) continue;

                                    int best_k = -1;
                                    double best_sc = 1e9;
                                    for (size_t k = 0; k < candidates.size(); k++)
                                    {
                                        const auto& cand = candidates[k];
                                        if (cand.s == sn || degrees[cand.s] >= 3 || cand.mag > 6.0) continue;
                                        if (!is_segment_valid(sn, cand.s, it_sn->second.u, cand.u, all_lines.size())) continue;

                                        double d_deg = ang_dist_deg(it_sn->second.u, cand.u);
                                        double dm = std::abs(it_sn->second.mag - cand.mag);
                                        double sc = d_deg + 0.5 * cand.mag + 1.2 * dm;
                                        if (sc < best_sc)
                                        {
                                            best_sc = sc;
                                            best_k = (int)k;
                                        }
                                    }
                                    if (best_k >= 0)
                                    {
                                        const auto& cand_info = candidates[best_k];
                                        all_lines.push_back({it_sn->second, cand_info});
                                        line_assigned_cons.push_back(c_abbrev);
                                        degrees[sn]++;
                                        degrees[cand_info.s]++;
                                        if (!included_set.count(cand_info.s))
                                        {
                                            included.push_back(cand_info);
                                            included_set.insert(cand_info.s);
                                        }
                                        break;
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
            }

            if (!any_rerouted) break;
        }


        // 5. Construct final Constellation objects grouped by IAU constellations
        std::unordered_map<std::string, std::vector<std::pair<Star*, Star*>>> lines_by_cons;
        for (size_t i = 0; i < all_lines.size(); i++)
        {
            std::string c_abbrev = (i < line_assigned_cons.size()) ? line_assigned_cons[i] : all_lines[i].first.orig_cons;
            if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
            {
                c_abbrev = all_lines[i].second.orig_cons;
            }
            if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
            {
                for (int k = 0; k < 88; k++)
                {
                    std::string cand_abbr = iau_constellations[k].abbrev;
                    if (!existing_cons_abbrevs.count(cand_abbr))
                    {
                        c_abbrev = cand_abbr;
                        break;
                    }
                }
            }
            lines_by_cons[c_abbrev].push_back({all_lines[i].first.s, all_lines[i].second.s});
        }

        std::unordered_set<std::string> used_abbrevs = existing_cons_abbrevs;
        for (const auto& pair : lines_by_cons)
        {
            if (pair.second.empty())
            {
                continue;
            }
            std::string actual_abbrev = pair.first;
            if (used_abbrevs.count(actual_abbrev))
            {
                for (int i = 0; i < 88; i++)
                {
                    std::string cand_abbr = iau_constellations[i].abbrev;
                    if (!used_abbrevs.count(cand_abbr))
                    {
                        actual_abbrev = cand_abbr;
                        break;
                    }
                }
            }
            used_abbrevs.insert(actual_abbrev);

            const IAUConstellationDef* def = find_iau_def(actual_abbrev);
            if (!def)
            {
                def = &iau_constellations[0];
            }

            Constellation c;
            c.abbrev = def->abbrev;
            c.name = def->name;
            c.genitive = def->genitive;
            c.vantage_name = sys_name;
            c.vantage = vantage_pt;
            c.vantage_resolved = true;

            for (const auto& line_pair : pair.second)
            {
                ConsLine cl;
                cl.a = line_pair.first;
                cl.b = line_pair.second;
                cl.starnamea = get_consline_star_name(line_pair.first);
                cl.starnameb = get_consline_star_name(line_pair.second);
                c.lines.push_back(cl);
            }

            out_conss.push_back(c);
        }
    }

    void ExoConsGenerator::save_to_exocons_file(const std::string& vantage_name, const std::vector<Constellation>& conss)
    {
        std::vector<std::string> existing_lines;
        std::ifstream infile("exocons.dat");
        if (infile.is_open())
        {
            std::string line;
            bool skipping = false;
            while (std::getline(infile, line))
            {
                if (!line.empty() && line[0] == ':')
                {
                    std::string v = trim(line.substr(1));
                    if (v == vantage_name)
                    {
                        skipping = true;
                    }
                    else
                    {
                        skipping = false;
                    }
                }
                if (!skipping)
                {
                    existing_lines.push_back(line);
                }
            }
            infile.close();
        }

        std::ofstream outfile("exocons.dat");
        if (!outfile.is_open())
        {
            return;
        }

        for (const auto& l : existing_lines)
        {
            outfile << l << "\n";
        }

        outfile << ":" << vantage_name << "\n";
        for (const auto& c : conss)
        {
            outfile << "~" << c.abbrev << ", " << c.name << ", " << c.genitive << "\n";
            for (const auto& cl : c.lines)
            {
                outfile << cl.starnamea << ", " << cl.starnameb << "\n";
            }
        }
        outfile.close();
    }

    void ExoConsGenerator::generate_all_synchronous(Star* sys_star)
    {
        std::string vname;
        if (!to_be_generated(sys_star, vname))
        {
            return;
        }

        std::vector<Constellation> generated;
        generate_constellations(sys_star, generated);

        for (auto& c : generated)
        {
            c.vantage = sys_star->location;
            c.vantage_name = vname;
            c.vantage_resolved = true;
            for (auto& cl : c.lines)
            {
                if (cl.a)
                {
                    cl.a->make_universally_visible();
                }
                if (cl.b)
                {
                    cl.b->make_universally_visible();
                }
            }
            constellations.push_back(c);
        }

        save_to_exocons_file(vname, generated);
        completed_vantages.insert(vname);
    }

    void ExoConsGenerator::start_generation_for(Star* sys_star)
    {
        std::string vname;
        if (!to_be_generated(sys_star, vname))
        {
            return;
        }

        current_sys_star = sys_star;
        current_vantage_name = vname;
        pending_conss.clear();
        generate_constellations(sys_star, pending_conss);
        generated_conss = pending_conss;
        is_generating = true;
    }

    void ExoConsGenerator::update_frame()
    {
        if (is_generating)
        {
            int batch_size = 3;
            while (batch_size-- > 0 && !pending_conss.empty())
            {
                Constellation c = pending_conss.back();
                pending_conss.pop_back();

                if (current_sys_star)
                {
                    c.vantage = current_sys_star->location;
                }
                c.vantage_name = current_vantage_name;
                c.vantage_resolved = true;
                for (auto& cl : c.lines)
                {
                    if (cl.a)
                    {
                        cl.a->make_universally_visible();
                    }
                    if (cl.b)
                    {
                        cl.b->make_universally_visible();
                    }
                }
                constellations.push_back(c);
            }

            if (pending_conss.empty())
            {
                is_generating = false;
                save_to_exocons_file(current_vantage_name, generated_conss);
                completed_vantages.insert(current_vantage_name);
                generated_conss.clear();
            }
            return;
        }

        // Check if current system is due for generation
        Star* sys_star = (Star*)(mycenobj ? mycenobj : (whereami >= 0 ? cels[whereami] : nullptr));
        if (sys_star && sys_star->cenobj && sys_star->cenobj->typeclass() == class_star)
        {
            sys_star = (Star*)sys_star->cenobj;
        }

        std::string vname;
        if (sys_star && to_be_generated(sys_star, vname))
        {
            start_generation_for(sys_star);
        }
    }

    void ExoConsGenerator::reset()
    {
        pending_conss.clear();
        generated_conss.clear();
        current_sys_star = nullptr;
        current_vantage_name = "";
        is_generating = false;
        completed_vantages.clear();
    }
}
