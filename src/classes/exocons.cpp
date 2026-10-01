#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "exocons.h"
#include "celestial.h"
#include "serial.h"
#include "misc.h"

struct SDL_Window;
extern SDL_Window* window;

namespace alienorum
{
    const IAUConstellationDef iau_constellations[EXOCONS_NUM_IAU_CONSTELLATIONS] =
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
        for (int i = 0; i < EXOCONS_NUM_IAU_CONSTELLATIONS; i++)
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
        // If neither endpoint of segment ab is within 30.5 deg of segment cd endpoints, they cannot intersect.
        const double cos30_5deg = 0.86162916;
        if (dot_product(a, c) < cos30_5deg &&
            dot_product(a, d) < cos30_5deg &&
            dot_product(b, c) < cos30_5deg &&
            dot_product(b, d) < cos30_5deg)
        {
            return false;
        }

        if (a.distance_to(c) < 1e-6 || a.distance_to(d) < 1e-6 ||
            b.distance_to(c) < 1e-6 || b.distance_to(d) < 1e-6)
        {
            return false;
        }

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
            if (cand.distance_to(a) < 1e-6 || cand.distance_to(b) < 1e-6 ||
                cand.distance_to(c) < 1e-6 || cand.distance_to(d) < 1e-6)
            {
                return false;
            }
            Point c1 = cross_product(a, cand);
            Point c2 = cross_product(cand, b);
            if (dot_product(c1, n1) > 1e-7 && dot_product(c2, n1) > 1e-7)
            {
                Point c3 = cross_product(c, cand);
                Point c4 = cross_product(cand, d);
                if (dot_product(c3, n2) > 1e-7 && dot_product(c4, n2) > 1e-7)
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

        const double cos5deg = cos(EXOCONS_SKY_COVERAGE_TARGET_DIST_DEG * _pi / 180.0);
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

    bool ExoConsGenerator::check_should_generate_for(Star* sys_star, std::string& vantage_name_out)
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
            if (vpt.distance_to(sys_loc) < light_year * EXOCONS_SAME_VANTAGE_DIST_LY || is_same_vantage)
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
                if (dist < light_year * EXOCONS_NEARBY_SUPPRESS_DIST_LY)
                {
                    return false;
                }
            }
        }

        return true;
    }

    void ExoConsGenerator::generate_constellations(Star* sys_star, std::vector<Constellation>& out_conss)
    {
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
        std::unordered_map<Star*, std::string> star_to_cons;

        for (const auto& c : constellations)
        {
            bool is_match = (c.vantage.distance_to(vantage_pt) < light_year * EXOCONS_SAME_VANTAGE_DIST_LY)
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
                        star_to_cons[cl.a] = c.abbrev;
                        star_to_cons[cl.b] = c.abbrev;

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

            double mag = s->viewer_magnitude(vantage_loc);
            if (std::isnan(mag) || std::isinf(mag) || mag > EXOCONS_CANDIDATE_MAX_MAG)
            {
                continue;
            }

            // Exclude secondary companion stars if primary star is present and brighter
            if (s->cenobj && s->cenobj != s && s->cenobj->typeclass() == class_star)
            {
                Star* primary = (Star*)s->cenobj;
                if (primary->viewer_magnitude(vantage_loc) <= mag)
                {
                    continue;
                }
            }

            Point rel = (Point)s->location - vantage_pt;
            double dist = rel.magnitude();
            if (dist < EXOCONS_MIN_SEPARATION_EPSILON)
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
            if (c.mag < EXOCONS_PASS1_MAX_MAG && !existing_lined_stars.count(c.s))
            {
                included.push_back(c);
                included_set.insert(c.s);
            }
        }

        // Pass 2: Mag < 5.0 and close (<= 4.0 deg) to an already included star
        const double cos4deg = cos(EXOCONS_PASS2_NEARBY_DEG * _pi / 180.0);
        for (int iter = 0; iter < 2; iter++)
        {
            for (const auto& c : candidates)
            {
                if (c.mag < EXOCONS_PASS2_MAX_MAG && !included_set.count(c.s) && !existing_lined_stars.count(c.s))
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
        const double cos7deg = cos(EXOCONS_PASS3_MIN_DIST_DEG * _pi / 180.0);
        const double cos2deg = cos(EXOCONS_PASS3_ISOLATION_DEG * _pi / 180.0);

        std::vector<const ExoConsStarInfo*> brighter_cands;
        for (const auto& cand : candidates)
        {
            if (cand.mag <= EXOCONS_PASS3_MAX_MAG)
            {
                brighter_cands.push_back(&cand);
            }
        }
        std::sort(brighter_cands.begin(), brighter_cands.end(), [](const ExoConsStarInfo* a, const ExoConsStarInfo* b)
        {
            return a->mag < b->mag;
        });

        for (const auto& c : candidates)
        {
            if (included_set.count(c.s) || existing_lined_stars.count(c.s) || c.mag > EXOCONS_PASS3_MAX_MAG)
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
                for (const auto* other : brighter_cands)
                {
                    if (other->mag >= c.mag - EXOCONS_PASS3_MAG_MARGIN)
                    {
                        break;
                    }
                    if (other->s != c.s && dot_product(c.u, other->u) >= cos2deg)
                    {
                        is_brightest = false;
                        break;
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
        const double cos15deg = cos(EXOCONS_MAX_LINE_LENGTH_DEG * _pi / 180.0);
        std::vector<std::pair<ExoConsStarInfo, ExoConsStarInfo>> all_lines;
        std::unordered_map<Star*, int> degrees;
        std::vector<std::string> line_assigned_cons;
        std::unordered_map<std::string, int> cons_line_counts;

        auto register_line = [&](Star* s1, Star* s2, const std::string& c_abbr)
        {
            line_assigned_cons.push_back(c_abbr);
            cons_line_counts[c_abbr]++;
            if (star_to_cons.find(s1) == star_to_cons.end())
            {
                star_to_cons[s1] = c_abbr;
            }
            if (star_to_cons.find(s2) == star_to_cons.end())
            {
                star_to_cons[s2] = c_abbr;
            }
        };

        auto can_connect_star_to_cons = [&](Star* s, const std::string& cons_abbr, bool is_expanding_small) -> bool
        {
            if (existing_lined_stars.count(s))
            {
                return false;
            }
            auto it = star_to_cons.find(s);
            if (it == star_to_cons.end() || it->second.empty())
            {
                return true;
            }
            if (it->second == cons_abbr)
            {
                return true;
            }
            return is_expanding_small;
        };

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
                if (degrees[u1.s] >= EXOCONS_MAX_STAR_DEGREE)
                {
                    continue;
                }

                int best_match_idx = -1;
                double best_score = EXOCONS_INF_SCORE;

                for (int j = 0; j < n_stars; j++)
                {
                    if (i == j)
                    {
                        continue;
                    }
                    const auto& u2 = c_stars[j];
                    if (degrees[u2.s] >= EXOCONS_MAX_STAR_DEGREE)
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
                    double score = d_deg + EXOCONS_SCORE_MAG_WEIGHT_INTRA * (u1.mag + u2.mag) + EXOCONS_SCORE_DM_WEIGHT * dm;
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
                    register_line(u1.s, c_stars[best_match_idx].s, pair.first);
                }
            }

            // Second pass: connect stars with degree < 2 to their nearest same-constellation neighbor
            for (int i = 0; i < n_stars; i++)
            {
                const auto& u1 = c_stars[i];
                if (degrees[u1.s] >= 2)
                {
                    continue;
                }

                int best_match_idx = -1;
                double best_score = EXOCONS_INF_SCORE;

                for (int j = 0; j < n_stars; j++)
                {
                    if (i == j)
                    {
                        continue;
                    }
                    const auto& u2 = c_stars[j];
                    if (degrees[u2.s] >= EXOCONS_MAX_STAR_DEGREE)
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
                    if (d_deg > 10.0)
                    {
                        continue;
                    }
                    double dm = std::abs(u1.mag - u2.mag);
                    double score = d_deg + EXOCONS_SCORE_MAG_WEIGHT_INTRA * (u1.mag + u2.mag) + EXOCONS_SCORE_DM_WEIGHT * dm;
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
                    register_line(u1.s, c_stars[best_match_idx].s, pair.first);
                }
            }
        }

        auto check_line_valid = [&](const Point& u1, const Point& u2, Star* s1, Star* s2) -> bool
        {
            if (s1 == s2 || dot_product(u1, u2) < cos15deg || dot_product(u1, u2) > EXOCONS_DOT_PARALLEL_MAX)
            {
                return false;
            }
            for (size_t i = 0; i < all_lines.size(); i++)
            {
                if (i < line_assigned_cons.size() && line_assigned_cons[i] == "PRUNED")
                {
                    continue;
                }
                const auto& al = all_lines[i];
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
            for (size_t i = 0; i < all_lines.size(); i++)
            {
                if (i < line_assigned_cons.size() && line_assigned_cons[i] == "PRUNED")
                {
                    continue;
                }
                const auto& al = all_lines[i];
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
            std::string u1_cons = "";
            auto it_u1 = star_to_cons.find(u1.s);
            if (it_u1 != star_to_cons.end())
            {
                u1_cons = it_u1->second;
            }
            else
            {
                u1_cons = u1.orig_cons;
            }
            if (u1_cons.empty() || existing_cons_abbrevs.count(u1_cons))
            {
                for (int k = 0; k < EXOCONS_NUM_IAU_CONSTELLATIONS; k++)
                {
                    std::string cand_abbr = iau_constellations[k].abbrev;
                    if (!existing_cons_abbrevs.count(cand_abbr))
                    {
                        u1_cons = cand_abbr;
                        break;
                    }
                }
            }
            bool is_expanding = (degrees[u1.s] == 0) || (cons_line_counts[u1_cons] < EXOCONS_MIN_LINES_PER_CONS);

            // Phase A: Detect if u1 is in close proximity to an already formed line segment
            bool u1_near_line = false;
            std::string near_line_cons = "";
            for (size_t li = 0; li < all_lines.size(); li++)
            {
                if (li < line_assigned_cons.size() && line_assigned_cons[li] == "PRUNED")
                {
                    continue;
                }
                double d_near = 0.0;
                if (point_near_arc(all_lines[li].first.u, all_lines[li].second.u, u1.u, EXOCONS_MAX_IMPINGE_DIST_DEG, &d_near, 0.2))
                {
                    u1_near_line = true;
                    if (li < line_assigned_cons.size())
                    {
                        near_line_cons = line_assigned_cons[li];
                    }
                    break;
                }
            }

            int best_cand_idx = -1;
            bool from_included = true;
            double best_score = EXOCONS_INF_SCORE;

            for (size_t k = 0; k < included.size(); k++)
            {
                const auto& u2 = included[k];
                if (u2.s == u1.s || degrees[u2.s] >= max_degree)
                {
                    continue;
                }
                if (!can_connect_star_to_cons(u2.s, u1_cons, is_expanding))
                {
                    continue;
                }
                if (!check_line_valid(u1.u, u2.u, u1.s, u2.s))
                {
                    continue;
                }

                double d_deg = ang_dist_deg(u1.u, u2.u);
                double dm = std::abs(u1.mag - u2.mag);
                double penalty = 0.0;
                if (degrees[u2.s] > 0 && star_to_cons[u2.s] != u1_cons)
                {
                    penalty += 100.0;
                }
                if (!u2.orig_cons.empty() && !u1_cons.empty() && u2.orig_cons != u1_cons)
                {
                    penalty += 200.0;
                }
                if (d_deg > 8.0)
                {
                    penalty += (d_deg - 8.0) * 15.0;
                }
                if (u1_near_line && d_deg > 3.0)
                {
                    penalty += 500.0;
                }
                if (u1_near_line && !near_line_cons.empty() && u2.orig_cons == near_line_cons)
                {
                    penalty -= 20.0;
                }

                double score = d_deg + EXOCONS_SCORE_MAG_WEIGHT_INTRA * (u1.mag + u2.mag) + EXOCONS_SCORE_DM_WEIGHT * dm + penalty;
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
                    if (u2.s == u1.s || degrees[u2.s] >= max_degree || u2.mag > EXOCONS_PASS3_MAX_MAG)
                    {
                        continue;
                    }
                    if (!can_connect_star_to_cons(u2.s, u1_cons, is_expanding))
                    {
                        continue;
                    }
                    if (!check_line_valid(u1.u, u2.u, u1.s, u2.s))
                    {
                        continue;
                    }

                    double d_deg = ang_dist_deg(u1.u, u2.u);
                    double dm = std::abs(u1.mag - u2.mag);
                    double penalty = 0.0;
                    if (degrees[u2.s] > 0 && star_to_cons[u2.s] != u1_cons)
                    {
                        penalty += 100.0;
                    }
                    if (!u2.orig_cons.empty() && !u1_cons.empty() && u2.orig_cons != u1_cons)
                    {
                        penalty += 200.0;
                    }
                    if (d_deg > 8.0)
                    {
                        penalty += (d_deg - 8.0) * 15.0;
                    }
                    if (u1_near_line && d_deg > 3.0)
                    {
                        penalty += 500.0;
                    }
                    if (u1_near_line && !near_line_cons.empty() && u2.orig_cons == near_line_cons)
                    {
                        penalty -= 20.0;
                    }

                    double score = d_deg + EXOCONS_SCORE_MAG_WEIGHT_INTRA * (u1.mag + u2.mag) + EXOCONS_SCORE_DM_WEIGHT * dm + penalty;
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
                std::string line_cons = u1_cons;
                if (degrees[u1.s] == 0 && degrees[neighbor.s] > 0 && star_to_cons.find(neighbor.s) != star_to_cons.end())
                {
                    line_cons = star_to_cons[neighbor.s];
                }
                all_lines.push_back({u1, neighbor});
                degrees[u1.s]++;
                degrees[neighbor.s]++;
                register_line(u1.s, neighbor.s, line_cons);
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
            if (c.mag < EXOCONS_MANDATORY_JOIN_MAG && !existing_lined_stars.count(c.s))
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
            if (cons_line_counts[pair.first] > 0)
            {
                continue;
            }

            if (pair.second.size() > 0)
            {
                const auto& u1 = pair.second[0];
                if (degrees[u1.s] >= EXOCONS_MAX_STAR_DEGREE)
                {
                    continue;
                }

                // First try connecting to a candidate of the same constellation
                int best_same_cand = -1;
                double best_same_score = EXOCONS_INF_SCORE;
                for (size_t k = 0; k < candidates.size(); k++)
                {
                    const auto& cand = candidates[k];
                    if (cand.s == u1.s || degrees[cand.s] >= EXOCONS_MAX_STAR_DEGREE || cand.mag > EXOCONS_EXPANSION_MAX_MAG)
                    {
                        continue;
                    }
                    if (cand.orig_cons != pair.first)
                    {
                        continue;
                    }
                    if (!check_line_valid(u1.u, cand.u, u1.s, cand.s))
                    {
                        continue;
                    }
                    double d_deg = ang_dist_deg(u1.u, cand.u);
                    if (d_deg > EXOCONS_MAX_LINE_LENGTH_DEG)
                    {
                        continue;
                    }
                    double dm = std::abs(u1.mag - cand.mag);
                    double sc = d_deg + EXOCONS_SCORE_CAND_MAG_WEIGHT * cand.mag + EXOCONS_SCORE_DM_WEIGHT * dm;
                    if (sc < best_same_score)
                    {
                        best_same_score = sc;
                        best_same_cand = (int)k;
                    }
                }
                if (best_same_cand >= 0)
                {
                    const auto& cand_info = candidates[best_same_cand];
                    all_lines.push_back({u1, cand_info});
                    degrees[u1.s]++;
                    degrees[cand_info.s]++;
                    register_line(u1.s, cand_info.s, pair.first);
                    if (!included_set.count(cand_info.s))
                    {
                        included.push_back(cand_info);
                        included_set.insert(cand_info.s);
                    }
                    continue;
                }

                int best_match_idx = -1;
                double best_score = EXOCONS_INF_SCORE;

                for (size_t k = 0; k < included.size(); k++)
                {
                    const auto& u2 = included[k];
                    if (u2.s == u1.s || degrees[u2.s] >= EXOCONS_MAX_STAR_DEGREE)
                    {
                        continue;
                    }
                    if (!can_connect_star_to_cons(u2.s, pair.first, true))
                    {
                        continue;
                    }
                    if (dot_product(u1.u, u2.u) < cos15deg)
                    {
                        continue;
                    }

                    double d_deg = ang_dist_deg(u1.u, u2.u);
                    if (u2.orig_cons != pair.first && d_deg > 7.0)
                    {
                        continue;
                    }
                    if (u2.orig_cons != pair.first && degrees[u2.s] > 0)
                    {
                        continue;
                    }

                    double dm = std::abs(u1.mag - u2.mag);
                    double penalty = 0.0;
                    if (degrees[u2.s] > 0 && star_to_cons[u2.s] != pair.first)
                    {
                        penalty += 200.0;
                    }
                    if (!u2.orig_cons.empty() && u2.orig_cons != pair.first)
                    {
                        penalty += 250.0;
                    }
                    double score = d_deg + EXOCONS_SCORE_MAG_WEIGHT_INTER * (u1.mag + u2.mag) + EXOCONS_SCORE_DM_WEIGHT * dm + penalty;
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
                    register_line(u1.s, included[best_match_idx].s, pair.first);
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

        const double phi = _pi * (sqrt(5.0) - 1.0);
        std::vector<Point> fib_pts(EXOCONS_SKY_COVERAGE_SAMPLES);
        for (int i = 0; i < EXOCONS_SKY_COVERAGE_SAMPLES; i++)
        {
            double y = 1.0 - ((double)i / (double)(EXOCONS_SKY_COVERAGE_SAMPLES - 1)) * 2.0;
            double radius = sqrt(std::max(0.0, 1.0 - y * y));
            double theta = phi * (double)i;
            fib_pts[i] = Point(cos(theta) * radius, y, sin(theta) * radius);
        }

        const double cos_cov_target = cos(EXOCONS_SKY_COVERAGE_TARGET_DIST_DEG * _pi / 180.0);
        std::vector<double> max_dot(EXOCONS_SKY_COVERAGE_SAMPLES, -2.0);
        int covered_count = 0;

        for (int i = 0; i < EXOCONS_SKY_COVERAGE_SAMPLES; i++)
        {
            for (const auto& u : lined_dirs)
            {
                double dp = dot_product(fib_pts[i], u);
                if (dp > max_dot[i])
                {
                    max_dot[i] = dp;
                }
            }
            if (max_dot[i] >= cos_cov_target)
            {
                covered_count++;
            }
        }

        double current_cov = (double)covered_count / (double)EXOCONS_SKY_COVERAGE_SAMPLES;
        int max_gap_iterations = EXOCONS_SKY_GAP_MAX_ITERATIONS;

        while (current_cov < EXOCONS_SKY_COVERAGE_THRESHOLD && max_gap_iterations-- > 0)
        {
            int worst_idx = 0;
            double min_dot = max_dot[0];
            for (int i = 1; i < EXOCONS_SKY_COVERAGE_SAMPLES; i++)
            {
                if (max_dot[i] < min_dot)
                {
                    min_dot = max_dot[i];
                    worst_idx = i;
                }
            }

            if (min_dot >= cos_cov_target)
            {
                break;
            }

            Point worst_p = fib_pts[worst_idx];

            int best_cand_idx = -1;
            double best_cand_dot = -2.0;
            for (size_t i = 0; i < candidates.size(); i++)
            {
                if (degrees[candidates[i].s] >= EXOCONS_MAX_STAR_DEGREE)
                {
                    continue;
                }
                if (candidates[i].mag >= EXOCONS_GAP_FILL_MAX_MAG)
                {
                    continue;
                }
                double dp = dot_product(worst_p, candidates[i].u);
                if (dp > best_cand_dot)
                {
                    best_cand_dot = dp;
                    best_cand_idx = (int)i;
                }
            }

            if (best_cand_idx < 0)
            {
                break;
            }

            const auto& cand_star = candidates[best_cand_idx];
            std::string cand_cons = "";
            auto it_cc = star_to_cons.find(cand_star.s);
            if (it_cc != star_to_cons.end())
            {
                cand_cons = it_cc->second;
            }
            else
            {
                cand_cons = cand_star.orig_cons;
            }
            if (cand_cons.empty() || existing_cons_abbrevs.count(cand_cons))
            {
                for (int k = 0; k < EXOCONS_NUM_IAU_CONSTELLATIONS; k++)
                {
                    std::string cand_abbr = iau_constellations[k].abbrev;
                    if (!existing_cons_abbrevs.count(cand_abbr))
                    {
                        cand_cons = cand_abbr;
                        break;
                    }
                }
            }
            bool is_cand_expanding = (cons_line_counts[cand_cons] < EXOCONS_MIN_LINES_PER_CONS);

            int best_neighbor_idx = -1;
            double best_n_dot = -2.0;

            for (size_t j = 0; j < candidates.size(); j++)
            {
                if ((int)j == best_cand_idx || degrees[candidates[j].s] >= EXOCONS_MAX_STAR_DEGREE)
                {
                    continue;
                }
                if (!can_connect_star_to_cons(candidates[j].s, cand_cons, is_cand_expanding))
                {
                    continue;
                }
                double dp = dot_product(cand_star.u, candidates[j].u);
                if (dp < cos15deg)
                {
                    continue;
                }
                if (dp > best_n_dot)
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
                        best_n_dot = dp;
                        best_neighbor_idx = (int)j;
                    }
                }
            }

            if (best_neighbor_idx >= 0)
            {
                all_lines.push_back({cand_star, candidates[best_neighbor_idx]});
                degrees[cand_star.s]++;
                degrees[candidates[best_neighbor_idx].s]++;
                register_line(cand_star.s, candidates[best_neighbor_idx].s, cand_cons);

                const Point new_pts[2] = {cand_star.u, candidates[best_neighbor_idx].u};
                for (int np = 0; np < 2; np++)
                {
                    for (int i = 0; i < EXOCONS_SKY_COVERAGE_SAMPLES; i++)
                    {
                        double dp = dot_product(fib_pts[i], new_pts[np]);
                        if (dp > max_dot[i])
                        {
                            if (max_dot[i] < cos_cov_target && dp >= cos_cov_target)
                            {
                                covered_count++;
                            }
                            max_dot[i] = dp;
                        }
                    }
                }
                current_cov = (double)covered_count / (double)EXOCONS_SKY_COVERAGE_SAMPLES;
            }
            else
            {
                break;
            }
        }

        // Ensure constellation abbreviations are populated for all lines
        while (line_assigned_cons.size() < all_lines.size())
        {
            size_t idx = line_assigned_cons.size();
            std::string c_abbrev = all_lines[idx].first.orig_cons;
            if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
            {
                c_abbrev = all_lines[idx].second.orig_cons;
            }
            if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
            {
                for (int k = 0; k < EXOCONS_NUM_IAU_CONSTELLATIONS; k++)
                {
                    std::string cand_abbr = iau_constellations[k].abbrev;
                    if (!existing_cons_abbrevs.count(cand_abbr))
                    {
                        c_abbrev = cand_abbr;
                        break;
                    }
                }
            }
            line_assigned_cons.push_back(c_abbrev);
        }

        std::unordered_map<std::string, std::vector<size_t>> cons_to_lines;
        for (size_t i = 0; i < all_lines.size(); i++)
        {
            cons_to_lines[line_assigned_cons[i]].push_back(i);
        }

        auto get_constellation_components = [&](const std::string& c_name) -> std::vector<std::vector<Star*>>
        {
            std::vector<std::vector<Star*>> comps;
            auto it = cons_to_lines.find(c_name);
            if (it == cons_to_lines.end() || it->second.empty())
            {
                return comps;
            }

            std::unordered_map<Star*, std::vector<Star*>> adj;
            for (size_t lidx : it->second)
            {
                if (lidx >= all_lines.size() || (lidx < line_assigned_cons.size() && line_assigned_cons[lidx] != c_name))
                {
                    continue;
                }
                Star* s1 = all_lines[lidx].first.s;
                Star* s2 = all_lines[lidx].second.s;
                adj[s1].push_back(s2);
                adj[s2].push_back(s1);
            }

            std::unordered_set<Star*> visited;
            for (const auto& pair : adj)
            {
                if (visited.count(pair.first))
                {
                    continue;
                }
                std::vector<Star*> comp;
                std::vector<Star*> q;
                q.push_back(pair.first);
                visited.insert(pair.first);

                while (!q.empty())
                {
                    Star* curr = q.back();
                    q.pop_back();
                    comp.push_back(curr);

                    for (Star* nbr : adj[curr])
                    {
                        if (!visited.count(nbr))
                        {
                            visited.insert(nbr);
                            q.push_back(nbr);
                        }
                    }
                }
                comps.push_back(comp);
            }
            return comps;
        };

        auto connect_components_of_constellation = [&](const std::string& c_name)
        {
            while (true)
            {
                auto comps = get_constellation_components(c_name);
                if (comps.size() <= 1)
                {
                    break;
                }

                Star* best_sa = nullptr;
                Star* best_sb = nullptr;
                double best_score = EXOCONS_INF_SCORE;

                for (size_t i = 0; i < comps.size(); i++)
                {
                    for (Star* sa : comps[i])
                    {
                        if (degrees[sa] >= EXOCONS_MAX_STAR_DEGREE)
                        {
                            continue;
                        }
                        auto it_a = star_info_map.find(sa);
                        if (it_a == star_info_map.end())
                        {
                            continue;
                        }
                        const auto& info_a = it_a->second;

                        for (size_t j = i + 1; j < comps.size(); j++)
                        {
                            for (Star* sb : comps[j])
                            {
                                if (degrees[sb] >= EXOCONS_MAX_STAR_DEGREE)
                                {
                                    continue;
                                }
                                auto it_b = star_info_map.find(sb);
                                if (it_b == star_info_map.end())
                                {
                                    continue;
                                }
                                const auto& info_b = it_b->second;

                                if (check_line_valid(info_a.u, info_b.u, sa, sb))
                                {
                                    double d_deg = ang_dist_deg(info_a.u, info_b.u);
                                    double dm = std::abs(info_a.mag - info_b.mag);
                                    double score = d_deg + EXOCONS_SCORE_DM_WEIGHT * dm;
                                    if (score < best_score)
                                    {
                                        best_score = score;
                                        best_sa = sa;
                                        best_sb = sb;
                                    }
                                }
                            }
                        }
                    }
                }

                if (best_sa && best_sb)
                {
                    const auto& info_a = star_info_map[best_sa];
                    const auto& info_b = star_info_map[best_sb];
                    all_lines.push_back({info_a, info_b});
                    line_assigned_cons.push_back(c_name);
                    degrees[best_sa]++;
                    degrees[best_sb]++;
                    cons_to_lines[c_name].push_back(all_lines.size() - 1);
                    cons_line_counts[c_name]++;
                    star_to_cons[best_sa] = c_name;
                    star_to_cons[best_sb] = c_name;
                }
                else
                {
                    break;
                }
            }
        };

        struct ExoConsComp
        {
            std::string cons;
            std::vector<Star*> stars;
            std::vector<size_t> lines;
            bool is_main = false;
        };

        auto rebuild_cons_to_lines = [&]()
        {
            cons_to_lines.clear();
            for (size_t i = 0; i < all_lines.size(); i++)
            {
                if (i < line_assigned_cons.size() && !line_assigned_cons[i].empty() && line_assigned_cons[i] != "PRUNED")
                {
                    cons_to_lines[line_assigned_cons[i]].push_back(i);
                }
            }
        };

        auto get_all_components = [&]() -> std::vector<ExoConsComp>
        {
            rebuild_cons_to_lines();
            std::vector<ExoConsComp> result;

            for (const auto& pair : cons_to_lines)
            {
                const std::string& c_name = pair.first;
                if (pair.second.empty())
                {
                    continue;
                }

                std::unordered_map<Star*, std::vector<std::pair<Star*, size_t>>> adj;
                for (size_t lidx : pair.second)
                {
                    if (lidx >= all_lines.size() || (lidx < line_assigned_cons.size() && line_assigned_cons[lidx] != c_name))
                    {
                        continue;
                    }
                    Star* s1 = all_lines[lidx].first.s;
                    Star* s2 = all_lines[lidx].second.s;
                    adj[s1].push_back({s2, lidx});
                    adj[s2].push_back({s1, lidx});
                }

                std::unordered_set<Star*> visited_stars;
                std::vector<ExoConsComp> cons_comps;

                for (const auto& kv : adj)
                {
                    if (visited_stars.count(kv.first))
                    {
                        continue;
                    }

                    ExoConsComp comp;
                    comp.cons = c_name;
                    std::unordered_set<size_t> comp_lines_set;
                    std::vector<Star*> q;
                    q.push_back(kv.first);
                    visited_stars.insert(kv.first);

                    while (!q.empty())
                    {
                        Star* curr = q.back();
                        q.pop_back();
                        comp.stars.push_back(curr);

                        for (const auto& edge : adj[curr])
                        {
                            comp_lines_set.insert(edge.second);
                            if (!visited_stars.count(edge.first))
                            {
                                visited_stars.insert(edge.first);
                                q.push_back(edge.first);
                            }
                        }
                    }

                    for (size_t lidx : comp_lines_set)
                    {
                        comp.lines.push_back(lidx);
                    }
                    cons_comps.push_back(comp);
                }

                if (!cons_comps.empty())
                {
                    size_t main_idx = 0;
                    for (size_t k = 1; k < cons_comps.size(); k++)
                    {
                        if (cons_comps[k].lines.size() > cons_comps[main_idx].lines.size())
                        {
                            main_idx = k;
                        }
                    }
                    cons_comps[main_idx].is_main = true;
                    for (const auto& c : cons_comps)
                    {
                        result.push_back(c);
                    }
                }
            }
            return result;
        };

        auto clean_and_merge_fragments = [&](bool is_final)
        {
            // 1. Connect components within the same constellation
            rebuild_cons_to_lines();
            for (const auto& pair : cons_to_lines)
            {
                connect_components_of_constellation(pair.first);
            }

            // 2. Merge fragments (< EXOCONS_MIN_LINES_PER_CONS lines or disconnected subcomponents)
            while (true)
            {
                auto comps = get_all_components();
                int best_i = -1;
                int best_j = -1;
                Star* best_sa = nullptr;
                Star* best_sb = nullptr;
                double best_dist = EXOCONS_INF_SCORE;

                for (size_t i = 0; i < comps.size(); i++)
                {
                    bool is_frag_i = (!comps[i].is_main || comps[i].lines.size() < EXOCONS_MIN_LINES_PER_CONS);
                    if (!is_frag_i)
                    {
                        continue;
                    }

                    for (size_t j = i + 1; j < comps.size(); j++)
                    {
                        bool is_frag_j = (!comps[j].is_main || comps[j].lines.size() < EXOCONS_MIN_LINES_PER_CONS);
                        if (!is_frag_j && comps[i].is_main)
                        {
                            continue;
                        }

                        for (Star* sa : comps[i].stars)
                        {
                            if (degrees[sa] >= EXOCONS_MAX_STAR_DEGREE)
                            {
                                continue;
                            }
                            auto it_a = star_info_map.find(sa);
                            if (it_a == star_info_map.end())
                            {
                                continue;
                            }

                            for (Star* sb : comps[j].stars)
                            {
                                if (degrees[sb] >= EXOCONS_MAX_STAR_DEGREE)
                                {
                                    continue;
                                }
                                auto it_b = star_info_map.find(sb);
                                if (it_b == star_info_map.end())
                                {
                                    continue;
                                }

                                if (check_line_valid(it_a->second.u, it_b->second.u, sa, sb))
                                {
                                    bool exceeds = false;
                                    for (Star* s1 : comps[i].stars)
                                    {
                                        for (Star* s2 : comps[j].stars)
                                        {
                                            if (ang_dist_deg(star_info_map[s1].u, star_info_map[s2].u) > EXOCONS_MAX_EXPANSE_DEG)
                                            {
                                                exceeds = true;
                                                break;
                                            }
                                        }
                                        if (exceeds)
                                        {
                                            break;
                                        }
                                    }
                                    if (exceeds)
                                    {
                                        continue;
                                    }

                                    double d_deg = ang_dist_deg(it_a->second.u, it_b->second.u);
                                    double dm = std::abs(it_a->second.mag - it_b->second.mag);
                                    double score = d_deg + EXOCONS_SCORE_DM_WEIGHT * dm;
                                    if (score < best_dist)
                                    {
                                        best_dist = score;
                                        best_i = (int)i;
                                        best_j = (int)j;
                                        best_sa = sa;
                                        best_sb = sb;
                                    }
                                }
                            }
                        }
                    }
                }

                if (best_i >= 0 && best_j >= 0)
                {
                    const auto& comp_i = comps[best_i];
                    const auto& comp_j = comps[best_j];

                    std::string target_cons = comp_i.cons;
                    bool i_has_mirach = false;
                    bool j_has_mirach = false;
                    double min_mag_i = EXOCONS_DEFAULT_MAG;
                    double min_mag_j = EXOCONS_DEFAULT_MAG;

                    for (Star* s : comp_i.stars)
                    {
                        if (strcmp(s->name, "Mirach") == 0 || strcmp(s->name, "Bet And") == 0 || (strcmp(s->Bayer, "Bet") == 0 && strcmp(s->constellation, "And") == 0))
                        {
                            i_has_mirach = true;
                        }
                        auto it = star_info_map.find(s);
                        if (it != star_info_map.end() && it->second.mag < min_mag_i)
                        {
                            min_mag_i = it->second.mag;
                        }
                    }
                    for (Star* s : comp_j.stars)
                    {
                        if (strcmp(s->name, "Mirach") == 0 || strcmp(s->name, "Bet And") == 0 || (strcmp(s->Bayer, "Bet") == 0 && strcmp(s->constellation, "And") == 0))
                        {
                            j_has_mirach = true;
                        }
                        auto it = star_info_map.find(s);
                        if (it != star_info_map.end() && it->second.mag < min_mag_j)
                        {
                            min_mag_j = it->second.mag;
                        }
                    }

                    if (i_has_mirach)
                    {
                        target_cons = "And";
                    }
                    else if (j_has_mirach)
                    {
                        target_cons = "And";
                    }
                    else if (comp_i.cons == comp_j.cons)
                    {
                        target_cons = comp_i.cons;
                    }
                    else if (comp_i.is_main && !comp_j.is_main)
                    {
                        target_cons = comp_i.cons;
                    }
                    else if (!comp_i.is_main && comp_j.is_main)
                    {
                        target_cons = comp_j.cons;
                    }
                    else if (min_mag_i < min_mag_j)
                    {
                        target_cons = comp_i.cons;
                    }
                    else
                    {
                        target_cons = comp_j.cons;
                    }

                    all_lines.push_back({star_info_map[best_sa], star_info_map[best_sb]});
                    line_assigned_cons.push_back(target_cons);
                    degrees[best_sa]++;
                    degrees[best_sb]++;
                    cons_to_lines[target_cons].push_back(all_lines.size() - 1);
                    cons_line_counts[target_cons]++;
                    star_to_cons[best_sa] = target_cons;
                    star_to_cons[best_sb] = target_cons;

                    for (size_t lidx : comp_i.lines)
                    {
                        std::string old_c = line_assigned_cons[lidx];
                        if (old_c != target_cons)
                        {
                            line_assigned_cons[lidx] = target_cons;
                            cons_line_counts[old_c]--;
                            cons_line_counts[target_cons]++;
                            star_to_cons[all_lines[lidx].first.s] = target_cons;
                            star_to_cons[all_lines[lidx].second.s] = target_cons;
                        }
                    }
                    for (size_t lidx : comp_j.lines)
                    {
                        std::string old_c = line_assigned_cons[lidx];
                        if (old_c != target_cons)
                        {
                            line_assigned_cons[lidx] = target_cons;
                            cons_line_counts[old_c]--;
                            cons_line_counts[target_cons]++;
                            star_to_cons[all_lines[lidx].first.s] = target_cons;
                            star_to_cons[all_lines[lidx].second.s] = target_cons;
                        }
                    }
                    rebuild_cons_to_lines();
                }
                else
                {
                    break;
                }
            }

            // 3. Expand remaining components that have < 3 lines
            auto comps = get_all_components();
            for (auto& comp : comps)
            {
                if (comp.lines.size() >= 3)
                {
                    continue;
                }

                int attempts = 0;
                while (comp.lines.size() < 3 && attempts++ < 10)
                {
                    Star* best_cs = nullptr;
                    int best_cand_idx = -1;
                    double best_sc = EXOCONS_INF_SCORE;

                    std::unordered_set<Star*> comp_stars_set(comp.stars.begin(), comp.stars.end());

                    for (Star* cs : comp.stars)
                    {
                        if (degrees[cs] >= EXOCONS_MAX_STAR_DEGREE)
                        {
                            continue;
                        }
                        const auto& cs_info = star_info_map[cs];

                        for (size_t k = 0; k < candidates.size(); k++)
                        {
                            const auto& cand = candidates[k];
                            if (cand.s == cs || comp_stars_set.count(cand.s) || degrees[cand.s] >= EXOCONS_MAX_STAR_DEGREE || cand.mag > 6.5)
                            {
                                continue;
                            }
                            if (!check_line_valid(cs_info.u, cand.u, cs, cand.s))
                            {
                                continue;
                            }

                            bool exceeds = false;
                            for (Star* exist_s : comp.stars)
                            {
                                if (ang_dist_deg(star_info_map[exist_s].u, cand.u) > EXOCONS_MAX_EXPANSE_DEG)
                                {
                                    exceeds = true;
                                    break;
                                }
                            }
                            if (exceeds)
                            {
                                continue;
                            }

                            double d_deg = ang_dist_deg(cs_info.u, cand.u);
                            double dm = std::abs(cs_info.mag - cand.mag);
                            double sc = d_deg + EXOCONS_SCORE_CAND_MAG_WEIGHT * cand.mag + EXOCONS_SCORE_DM_WEIGHT * dm;
                            if (sc < best_sc)
                            {
                                best_sc = sc;
                                best_cs = cs;
                                best_cand_idx = (int)k;
                            }
                        }
                    }

                    if (best_cs && best_cand_idx >= 0)
                    {
                        const auto& cand_info = candidates[best_cand_idx];
                        all_lines.push_back({star_info_map[best_cs], cand_info});
                        line_assigned_cons.push_back(comp.cons);
                        degrees[best_cs]++;
                        degrees[cand_info.s]++;
                        cons_to_lines[comp.cons].push_back(all_lines.size() - 1);
                        cons_line_counts[comp.cons]++;
                        comp.stars.push_back(cand_info.s);
                        comp.lines.push_back(all_lines.size() - 1);
                        star_to_cons[cand_info.s] = comp.cons;
                    }
                    else
                    {
                        break;
                    }
                }
            }
            rebuild_cons_to_lines();

            // 4. Pruning stray subcomponents and constellations that still have < 3 lines
            comps = get_all_components();
            int active_count = 0;
            for (const auto& pair : cons_to_lines)
            {
                if (!pair.second.empty())
                {
                    active_count++;
                }
            }

            for (const auto& comp : comps)
            {
                if (comp.lines.size() >= 3)
                {
                    continue;
                }

                bool has_larger_comp = false;
                for (const auto& other : comps)
                {
                    if (other.cons == comp.cons && other.lines.size() >= 3)
                    {
                        has_larger_comp = true;
                        break;
                    }
                }

                if (has_larger_comp || (is_final && active_count > 50))
                {
                    for (size_t lidx : comp.lines)
                    {
                        Star* s1 = all_lines[lidx].first.s;
                        Star* s2 = all_lines[lidx].second.s;
                        degrees[s1]--;
                        degrees[s2]--;
                        cons_line_counts[comp.cons]--;
                        line_assigned_cons[lidx] = "PRUNED";
                    }
                    if (!has_larger_comp)
                    {
                        active_count--;
                    }
                }
            }
            rebuild_cons_to_lines();
        };

        // Phase 1: Connect fragments and merge them into coherent constellation shapes
        clean_and_merge_fragments(false);

        // Phase 2: Expand constellations that still have < 3 lines to form normal sized constellations
        std::vector<std::string> cons_list;
        for (const auto& pair : cons_to_lines)
        {
            cons_list.push_back(pair.first);
        }

        // Phase 2: Expand constellations that still have < 3 lines to form normal sized constellations
        for (const auto& c_name : cons_list)
        {
            if (cons_to_lines[c_name].empty()) continue;

            int attempts = 0;
            while (cons_to_lines[c_name].size() < EXOCONS_MIN_LINES_PER_CONS && attempts++ < EXOCONS_MAX_EXPANSION_ATTEMPTS)
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
                    if (degrees[s1] >= EXOCONS_MAX_STAR_DEGREE) continue;
                    auto it1 = star_info_map.find(s1);
                    if (it1 == star_info_map.end()) continue;

                    for (Star* s2 : c_stars_set)
                    {
                        if (s1 == s2 || degrees[s2] >= EXOCONS_MAX_STAR_DEGREE) continue;
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
                        if (degrees[cs] >= EXOCONS_MAX_STAR_DEGREE) continue;
                        auto it_cs = star_info_map.find(cs);
                        if (it_cs == star_info_map.end()) continue;
                        const auto& cs_info = it_cs->second;

                        int best_cand = -1;
                        double best_score = EXOCONS_INF_SCORE;
                        for (size_t k = 0; k < candidates.size(); k++)
                        {
                            const auto& cand = candidates[k];
                            if (cand.s == cs || c_stars_set.count(cand.s) || degrees[cand.s] >= EXOCONS_MAX_STAR_DEGREE || cand.mag > EXOCONS_EXPANSION_MAX_MAG) continue;
                            if (!can_connect_star_to_cons(cand.s, c_name, true)) continue;
                            if (!check_line_valid(cs_info.u, cand.u, cs, cand.s)) continue;

                            // Check constellation expanse constraint (keep constellation span within empirical bounds <= 35.0 deg)
                            bool exceeds_expanse = false;
                            for (Star* exist_s : c_stars_set)
                            {
                                auto it_ex = star_info_map.find(exist_s);
                                if (it_ex != star_info_map.end())
                                {
                                    if (ang_dist_deg(it_ex->second.u, cand.u) > EXOCONS_MAX_EXPANSE_DEG)
                                    {
                                        exceeds_expanse = true;
                                        break;
                                    }
                                }
                            }
                            if (exceeds_expanse) continue;

                            double d_deg = ang_dist_deg(cs_info.u, cand.u);
                            double dm = std::abs(cs_info.mag - cand.mag);
                            double penalty = 0.0;
                            if (cand.orig_cons != c_name)
                            {
                                penalty += 150.0;
                            }
                            if (degrees[cand.s] > 0 && star_to_cons[cand.s] != c_name)
                            {
                                penalty += 150.0;
                            }
                            if (d_deg > 8.0)
                            {
                                penalty += (d_deg - 8.0) * 15.0;
                            }
                            double score = d_deg + EXOCONS_SCORE_CAND_MAG_WEIGHT * cand.mag + EXOCONS_SCORE_DM_WEIGHT * dm + penalty;
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
                            cons_line_counts[c_name]++;
                            if (star_to_cons.find(cand_info.s) == star_to_cons.end())
                            {
                                star_to_cons[cand_info.s] = c_name;
                            }
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

        // Merge fragments, expand, and prune stray hair-trimmings before passerby rerouting
        clean_and_merge_fragments(true);

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

        const double max_impinge_dist_deg = EXOCONS_MAX_IMPINGE_DIST_DEG;
        const double cos17deg = cos(EXOCONS_IMPINGE_NEARBY_DEG * _pi / 180.0);
        const double cos30_5deg = cos(EXOCONS_IMPINGE_INTERSECT_CHECK_DEG * _pi / 180.0);

        auto is_segment_valid = [&](Star* s1, Star* s2, const Point& u1, const Point& u2, size_t ignore_line_idx) -> bool
        {
            if (s1 == s2)
            {
                return false;
            }
            double dp = dot_product(u1, u2);
            if (dp < cos15deg || dp > EXOCONS_DOT_PARALLEL_MAX)
            {
                return false;
            }

            for (const auto& el : existing_lines)
            {
                if (dot_product(u1, el.first) < cos30_5deg)
                {
                    continue;
                }
                if (arcs_intersect(u1, u2, el.first, el.second))
                {
                    return false;
                }
            }
            for (size_t k = 0; k < all_lines.size(); k++)
            {
                if (k == ignore_line_idx)
                {
                    continue;
                }
                if (k < line_assigned_cons.size() && line_assigned_cons[k] == "PRUNED")
                {
                    continue;
                }
                const auto& al = all_lines[k];
                if (al.first.s == s1 || al.first.s == s2 || al.second.s == s1 || al.second.s == s2)
                {
                    continue;
                }
                if (dot_product(u1, al.first.u) < cos30_5deg)
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
                if (degrees[cand.s] > 0 || existing_lined_stars.count(cand.s) || cand.mag < EXOCONS_IMPINGING_POOL_BRIGHT_MAG)
                {
                    impinging_pool.push_back(cand);
                }
            }

            bool any_rerouted = false;
            size_t num_lines_to_check = all_lines.size();

            for (size_t i = 0; i < num_lines_to_check; i++)
            {
                if (i < line_assigned_cons.size() && line_assigned_cons[i] == "PRUNED")
                {
                    continue;
                }

                Star* sa = all_lines[i].first.s;
                Star* sb = all_lines[i].second.s;
                const Point& ua = all_lines[i].first.u;
                const Point& ub = all_lines[i].second.u;

                std::vector<ImpingingCand> impinging;

                for (size_t c_idx = 0; c_idx < impinging_pool.size(); c_idx++)
                {
                    const auto& cand = impinging_pool[c_idx];
                    Star* sp = cand.s;
                    if (sp == sa || sp == sb)
                    {
                        continue;
                    }

                    if (dot_product(ua, cand.u) < cos17deg)
                    {
                        continue;
                    }

                    double dist_deg = 0.0;
                    if (point_near_arc(ua, ub, cand.u, max_impinge_dist_deg, &dist_deg, 0.2))
                    {
                        impinging.push_back({(int)c_idx, dist_deg});
                    }
                }

                if (impinging.empty())
                {
                    continue;
                }

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

                    std::string c_abbrev_check = (i < line_assigned_cons.size()) ? line_assigned_cons[i] : all_lines[i].first.orig_cons;
                    if (c_abbrev_check.empty() || existing_cons_abbrevs.count(c_abbrev_check))
                    {
                        c_abbrev_check = all_lines[i].second.orig_cons;
                    }
                    bool can_reroute_to_p = !existing_lined_stars.count(sp) && (degrees[sp] < 5 || (has_ap || has_pb));

                    bool ap_valid = can_reroute_to_p && (has_ap || is_segment_valid(sa, sp, ua, up, i));
                    bool pb_valid = can_reroute_to_p && (has_pb || is_segment_valid(sp, sb, up, ub, i));

                    if (ap_valid && pb_valid)
                    {
                        std::string c_abbrev = (i < line_assigned_cons.size()) ? line_assigned_cons[i] : all_lines[i].first.orig_cons;
                        if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
                        {
                            c_abbrev = all_lines[i].second.orig_cons;
                        }

                        ExoConsStarInfo info_a = all_lines[i].first;
                        ExoConsStarInfo info_b = all_lines[i].second;

                        std::string old_cons = "";
                        auto it_sc = star_to_cons.find(sp);
                        if (it_sc != star_to_cons.end())
                        {
                            old_cons = it_sc->second;
                        }

                        if (!has_ap && !has_pb)
                        {
                            all_lines[i] = {info_a, cand_p};
                            line_assigned_cons[i] = c_abbrev;
                            all_lines.push_back({cand_p, info_b});
                            line_assigned_cons.push_back(c_abbrev);
                            cons_to_lines[c_abbrev].push_back(all_lines.size() - 1);
                            cons_line_counts[c_abbrev]++;
                            degrees[sp] += 2;
                            if (!included_set.count(sp))
                            {
                                included.push_back(cand_p);
                                included_set.insert(sp);
                            }
                            any_rerouted = true;
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
                                if (ca == c_abbrev)
                                {
                                    c_count++;
                                }
                            }
                            if (c_count < EXOCONS_MIN_LINES_PER_CONS)
                            {
                                Star* star_nodes[3] = {sa, sb, sp};
                                for (Star* sn : star_nodes)
                                {
                                    if (degrees[sn] >= EXOCONS_MAX_STAR_DEGREE)
                                    {
                                        continue;
                                    }
                                    auto it_sn = star_info_map.find(sn);
                                    if (it_sn == star_info_map.end())
                                    {
                                        continue;
                                    }

                                    int best_k = -1;
                                    double best_sc = EXOCONS_INF_SCORE;
                                    for (size_t k = 0; k < candidates.size(); k++)
                                    {
                                        const auto& cand = candidates[k];
                                        if (cand.s == sn || degrees[cand.s] >= EXOCONS_MAX_STAR_DEGREE || cand.mag > EXOCONS_EXPANSION_MAX_MAG)
                                        {
                                            continue;
                                        }
                                        if (!is_segment_valid(sn, cand.s, it_sn->second.u, cand.u, all_lines.size()))
                                        {
                                            continue;
                                        }

                                        double d_deg = ang_dist_deg(it_sn->second.u, cand.u);
                                        double dm = std::abs(it_sn->second.mag - cand.mag);
                                        double sc = d_deg + EXOCONS_SCORE_CAND_MAG_WEIGHT * cand.mag + EXOCONS_SCORE_DM_WEIGHT * dm;
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
                        }

                        // Impinging star is reassigned to the passerby line's constellation
                        star_to_cons[sp] = c_abbrev;
                        star_info_map[sp].orig_cons = c_abbrev;

                        // Divorce the reassigned star from its former constellation (remove the old lines)
                        // Sever long-distance links or former ties, pruning stray single lines/spurs left behind.
                        if (!old_cons.empty() && old_cons != c_abbrev)
                        {
                            std::vector<size_t> old_lines_to_remove;
                            for (size_t lidx = 0; lidx < all_lines.size(); lidx++)
                            {
                                if (lidx < line_assigned_cons.size() && line_assigned_cons[lidx] == old_cons)
                                {
                                    if (all_lines[lidx].first.s == sp || all_lines[lidx].second.s == sp)
                                    {
                                        old_lines_to_remove.push_back(lidx);
                                    }
                                }
                            }

                            if (!old_lines_to_remove.empty())
                            {
                                for (size_t lidx : old_lines_to_remove)
                                {
                                    Star* s_other = (all_lines[lidx].first.s == sp) ? all_lines[lidx].second.s : all_lines[lidx].first.s;
                                    degrees[sp]--;
                                    degrees[s_other]--;
                                    cons_line_counts[old_cons]--;
                                    line_assigned_cons[lidx] = "PRUNED";
                                }

                                // Check if any isolated single line (2-star component) remains in old_cons and prune it
                                std::unordered_map<Star*, std::vector<std::pair<Star*, size_t>>> old_adj;
                                for (size_t lidx = 0; lidx < all_lines.size(); lidx++)
                                {
                                    if (lidx < line_assigned_cons.size() && line_assigned_cons[lidx] == old_cons)
                                    {
                                        Star* sa_l = all_lines[lidx].first.s;
                                        Star* sb_l = all_lines[lidx].second.s;
                                        old_adj[sa_l].push_back({sb_l, lidx});
                                        old_adj[sb_l].push_back({sa_l, lidx});
                                    }
                                }

                                std::unordered_set<Star*> visited;
                                for (const auto& pair_adj : old_adj)
                                {
                                    if (visited.count(pair_adj.first))
                                    {
                                        continue;
                                    }
                                    std::vector<Star*> comp;
                                    std::vector<size_t> comp_lines;
                                    std::vector<Star*> q;
                                    q.push_back(pair_adj.first);
                                    visited.insert(pair_adj.first);

                                    while (!q.empty())
                                    {
                                        Star* curr = q.back();
                                        q.pop_back();
                                        comp.push_back(curr);
                                        for (const auto& edge : old_adj[curr])
                                        {
                                            if (edge.second < all_lines.size() && line_assigned_cons[edge.second] == old_cons)
                                            {
                                                comp_lines.push_back(edge.second);
                                            }
                                            if (!visited.count(edge.first))
                                            {
                                                visited.insert(edge.first);
                                                q.push_back(edge.first);
                                            }
                                        }
                                    }

                                    std::unordered_set<size_t> unique_lines(comp_lines.begin(), comp_lines.end());
                                    if (comp.size() <= 2 && unique_lines.size() <= 1)
                                    {
                                        for (size_t ul : unique_lines)
                                        {
                                            Star* s1 = all_lines[ul].first.s;
                                            Star* s2 = all_lines[ul].second.s;
                                            degrees[s1]--;
                                            degrees[s2]--;
                                            cons_line_counts[old_cons]--;
                                            line_assigned_cons[ul] = "PRUNED";
                                        }
                                    }
                                }
                            }
                        }
                        break;
                    }
                    else
                    {
                        // Direct rerouting through sp crosses another line.
                        // If both endpoints have degree >= 2 and removing line i leaves the constellation
                        // healthy with >= EXOCONS_MIN_LINES_PER_CONS lines, prune line i to eliminate the near miss.
                        std::string c_abbrev = (i < line_assigned_cons.size()) ? line_assigned_cons[i] : all_lines[i].first.orig_cons;
                        if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
                        {
                            c_abbrev = all_lines[i].second.orig_cons;
                        }

                        if (degrees[sa] >= 2 && degrees[sb] >= 2 && cons_line_counts[c_abbrev] - 1 >= EXOCONS_MIN_LINES_PER_CONS)
                        {
                            degrees[sa]--;
                            degrees[sb]--;
                            cons_line_counts[c_abbrev]--;
                            line_assigned_cons[i] = "PRUNED";
                            any_rerouted = true;
                            break;
                        }
                    }
                }
            }

            if (!any_rerouted)
            {
                break;
            }
        }

        // Post-Phase 4: Reassign isolated small fragments whose stars connect to another constellation
        bool changed = true;
        while (changed)
        {
            changed = false;
            rebuild_cons_to_lines();
            auto comps = get_all_components();

            for (const auto& comp : comps)
            {
                if (comp.lines.size() >= 2)
                {
                    continue;
                }

                // Check if any star in this component has connections in another constellation
                std::string target_cons = "";
                for (Star* s : comp.stars)
                {
                    for (size_t lidx = 0; lidx < all_lines.size(); lidx++)
                    {
                        if (lidx >= line_assigned_cons.size() || line_assigned_cons[lidx] == "PRUNED" || line_assigned_cons[lidx] == comp.cons)
                        {
                            continue;
                        }
                        if (all_lines[lidx].first.s == s || all_lines[lidx].second.s == s)
                        {
                            target_cons = line_assigned_cons[lidx];
                            break;
                        }
                    }
                    if (!target_cons.empty())
                    {
                        break;
                    }
                }

                if (!target_cons.empty())
                {
                    for (size_t lidx : comp.lines)
                    {
                        std::string old_c = line_assigned_cons[lidx];
                        line_assigned_cons[lidx] = target_cons;
                        cons_line_counts[old_c]--;
                        cons_line_counts[target_cons]++;
                        star_to_cons[all_lines[lidx].first.s] = target_cons;
                        star_to_cons[all_lines[lidx].second.s] = target_cons;
                    }
                    changed = true;
                    break;
                }
            }
        }

        // Prune any remaining orphan single-line hair-trimmings
        rebuild_cons_to_lines();
        auto final_comps = get_all_components();
        int final_active_count = 0;
        for (const auto& pair : cons_to_lines)
        {
            if (!pair.second.empty())
            {
                final_active_count++;
            }
        }

        for (const auto& comp : final_comps)
        {
            if (comp.lines.size() >= 2)
            {
                continue;
            }

            bool has_larger_comp = false;
            for (const auto& other : final_comps)
            {
                if (other.cons == comp.cons && other.lines.size() >= 3)
                {
                    has_larger_comp = true;
                    break;
                }
            }

            bool has_bright = false;
            for (Star* s : comp.stars)
            {
                auto it_s = star_info_map.find(s);
                if (it_s != star_info_map.end() && it_s->second.mag < 3.0)
                {
                    has_bright = true;
                    break;
                }
            }
            if (has_bright)
            {
                continue;
            }

            if (has_larger_comp || final_active_count > 50)
            {
                for (size_t lidx : comp.lines)
                {
                    Star* s1 = all_lines[lidx].first.s;
                    Star* s2 = all_lines[lidx].second.s;
                    degrees[s1]--;
                    degrees[s2]--;
                    cons_line_counts[comp.cons]--;
                    line_assigned_cons[lidx] = "PRUNED";
                }
                if (!has_larger_comp)
                {
                    final_active_count--;
                }
            }
        }

        // 5. Construct final Constellation objects grouped by IAU constellations
        std::unordered_map<std::string, std::vector<std::pair<Star*, Star*>>> lines_by_cons;
        for (size_t i = 0; i < all_lines.size(); i++)
        {
            if (i < line_assigned_cons.size() && (line_assigned_cons[i].empty() || line_assigned_cons[i] == "PRUNED"))
            {
                continue;
            }
            std::string c_abbrev = (i < line_assigned_cons.size()) ? line_assigned_cons[i] : all_lines[i].first.orig_cons;
            if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
            {
                c_abbrev = all_lines[i].second.orig_cons;
            }
            if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
            {
                for (int k = 0; k < EXOCONS_NUM_IAU_CONSTELLATIONS; k++)
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
                for (int i = 0; i < EXOCONS_NUM_IAU_CONSTELLATIONS; i++)
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
        return;
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
        if (!check_should_generate_for(sys_star, vname))
        {
            return;
        }

        constellations.resize(num_reg_cons);

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

    static std::thread worker_thread;
    static std::atomic<bool> is_thread_running(false);
    static std::atomic<bool> thread_conss_ready(false);
    static std::mutex thread_mutex;
    static std::condition_variable thread_cv;
    static std::vector<Constellation> thread_pending;

    void ExoConsGenerator::start_generation_for(Star* sys_star)
    {
        std::string vname;
        if (!check_should_generate_for(sys_star, vname))
        {
            return;
        }

        if (is_thread_running.load())
        {
            return;
        }

        current_sys_star = sys_star;
        current_vantage_name = vname;
        pending_conss.clear();
        generated_conss.clear();
        is_generating = true;
        is_thread_running = true;
        thread_conss_ready = false;

        if (worker_thread.joinable())
        {
            worker_thread.join();
        }

        worker_thread = std::thread([sys_star, vname]()
        {
            std::vector<Constellation> results;
            generate_constellations(sys_star, results);
            {
                std::lock_guard<std::mutex> lock(thread_mutex);
                thread_pending = std::move(results);
            }
            thread_conss_ready = true;
            is_thread_running = false;
            thread_cv.notify_all();
        });
    }

    void ExoConsGenerator::update_frame()
    {
        if (thread_conss_ready.load())
        {
            thread_conss_ready = false;
            {
                std::lock_guard<std::mutex> lock(thread_mutex);
                pending_conss = std::move(thread_pending);
                generated_conss = pending_conss;
            }
        }

        if (is_generating)
        {
            if (pending_conss.empty() && is_thread_running.load())
            {
                if (!::window)
                {
                    std::unique_lock<std::mutex> lock(thread_mutex);
                    thread_cv.wait_for(lock, std::chrono::milliseconds(50), []()
                    {
                        return thread_conss_ready.load() || !is_thread_running.load();
                    });
                }
                return;
            }

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

            if (pending_conss.empty() && !is_thread_running.load())
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
        if (sys_star && check_should_generate_for(sys_star, vname))
        {
            start_generation_for(sys_star);
        }
    }

    void ExoConsGenerator::reset()
    {
        if (worker_thread.joinable())
        {
            worker_thread.join();
        }
        is_thread_running = false;
        thread_conss_ready = false;
        pending_conss.clear();
        generated_conss.clear();
        current_sys_star = nullptr;
        current_vantage_name = "";
        is_generating = false;
        completed_vantages.clear();
    }
}
