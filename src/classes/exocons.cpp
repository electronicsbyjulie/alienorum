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

    static bool is_named_cons_member(Star* s)
    {
        if (!s)
        {
            return false;
        }
        return (s->Bayer[0] != 0 || s->Flamsteed[0] != 0);
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
        if (sys_star && sys_star->cenobj && sys_star->cenobj->typeclass() == class_star)
        {
            sys_star = (Star*)sys_star->cenobj;
        }
        if (!sys_star)
        {
            return false;
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

    struct CandStar
    {
        Star* s = nullptr;
        double mag = 99.0;
        Point u;
        int id = 0;
    };

    struct CandSpatialGrid
    {
        static const int GRID_RES = 10;
        std::vector<int> head;
        std::vector<int> next;
        const std::vector<CandStar>& stars;

        CandSpatialGrid(const std::vector<CandStar>& cands)
            : head(GRID_RES * GRID_RES * GRID_RES, -1),
              next(cands.size(), -1),
              stars(cands)
        {
            for (size_t i = 0; i < cands.size(); i++)
            {
                int cell = get_cell(cands[i].u);
                if (cell >= 0 && cell < (int)head.size())
                {
                    next[i] = head[cell];
                    head[cell] = (int)i;
                }
            }
        }

        static int cell_coord(double v)
        {
            int c = (int)((v + 1.0) * 0.5 * (double)GRID_RES);
            if (c < 0)
            {
                return 0;
            }
            if (c >= GRID_RES)
            {
                return GRID_RES - 1;
            }
            return c;
        }

        static int get_cell(const Point& p)
        {
            int cx = cell_coord(p.x);
            int cy = cell_coord(p.y);
            int cz = cell_coord(p.z);
            return cx + GRID_RES * (cy + GRID_RES * cz);
        }

        template<typename Func>
        void for_each_near(const Point& pt, double radius_deg, Func&& func) const
        {
            double rad = radius_deg * (_pi / 180.0);
            double chord = 2.0 * sin(0.5 * rad) * 1.05;
            int min_x = cell_coord(pt.x - chord);
            int max_x = cell_coord(pt.x + chord);
            int min_y = cell_coord(pt.y - chord);
            int max_y = cell_coord(pt.y + chord);
            int min_z = cell_coord(pt.z - chord);
            int max_z = cell_coord(pt.z + chord);

            for (int cz = min_z; cz <= max_z; cz++)
            {
                for (int cy = min_y; cy <= max_y; cy++)
                {
                    for (int cx = min_x; cx <= max_x; cx++)
                    {
                        int cell = cx + GRID_RES * (cy + GRID_RES * cz);
                        int idx = head[cell];
                        while (idx >= 0)
                        {
                            func(idx);
                            idx = next[idx];
                        }
                    }
                }
            }
        }
    };

    void ExoConsGenerator::generate_constellations(Star* sys_star, std::vector<Constellation>& out_conss)
    {
        out_conss.clear();
        if (sys_star && sys_star->cenobj && sys_star->cenobj->typeclass() == class_star)
        {
            sys_star = (Star*)sys_star->cenobj;
        }
        if (!sys_star)
        {
            return;
        }

        std::string sys_name = sys_star->name;
        if (sys_name.empty())
        {
            sys_name = get_consline_star_name(sys_star);
        }

        CelestialLocation vantage_loc = sys_star->location;
        Point vantage_pt = sys_star->location;
        CelestialObject* local_cenobj = sys_star->cenobj ? sys_star->cenobj : sys_star;

        // 1. Existing constellations from consline.dat for this vantage (e.g. Orion/Taurus at Alpha Mensae)
        std::unordered_set<Star*> existing_lined_stars;
        std::unordered_set<std::string> existing_cons_abbrevs;

        struct LineRecord
        {
            Star* a = nullptr;
            Star* b = nullptr;
            Point ua;
            Point ub;
            double len_deg = 0.0;
            double cos_half_len = 0.0;
            int cons_idx = -1;
        };

        struct PlacedStar
        {
            Star* s = nullptr;
            Point u;
            int cons_idx = -1;
        };

        std::vector<LineRecord> all_lines;
        std::vector<PlacedStar> placed_stars;
        std::vector<std::unordered_set<Star*>> cons_stars;

        for (const auto& c : constellations)
        {
            bool is_match = (c.vantage.distance_to(vantage_pt) < light_year * EXOCONS_SAME_VANTAGE_DIST_LY)
                || (!c.vantage_name.empty() && c.vantage_name == sys_name);
            if (is_match && c.lines.size() > 0)
            {
                existing_cons_abbrevs.insert(c.abbrev);
                std::unordered_set<Star*> c_stars;
                int c_idx = (int)cons_stars.size();
                for (const auto& cl : c.lines)
                {
                    if (cl.a && cl.b)
                    {
                        existing_lined_stars.insert(cl.a);
                        existing_lined_stars.insert(cl.b);
                        c_stars.insert(cl.a);
                        c_stars.insert(cl.b);

                        Point rel_a = (Point)cl.a->location - vantage_pt;
                        Point rel_b = (Point)cl.b->location - vantage_pt;
                        Point ua = normalize_point(rel_a);
                        Point ub = normalize_point(rel_b);
                        double len = ang_dist_deg(ua, ub);
                        double cos_half = cos(0.5 * len * (_pi / 180.0));
                        all_lines.push_back({cl.a, cl.b, ua, ub, len, cos_half, c_idx});
                        placed_stars.push_back({cl.a, ua, c_idx});
                        placed_stars.push_back({cl.b, ub, c_idx});
                    }
                }
                cons_stars.push_back(c_stars);
            }
        }
        const int existing_cons_count = (int)cons_stars.size();

        // 2. Collect candidate stars outside local system
        std::vector<CandStar> candidates;
        candidates.reserve(8192);
        std::unordered_map<Star*, Point> star_u_map;
        std::unordered_map<Star*, double> star_mag_map;

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
            if (existing_lined_stars.count(s))
            {
                continue;
            }

            if (s->cenobj && s->cenobj != s && s->cenobj->typeclass() == class_star)
            {
                Star* primary = (Star*)s->cenobj;
                if (primary->viewer_magnitude(vantage_loc) <= s->viewer_magnitude(vantage_loc))
                {
                    continue;
                }
            }

            double mag = s->viewer_magnitude(vantage_loc);
            if (std::isnan(mag) || std::isinf(mag) || mag > 6.2)
            {
                continue;
            }

            Point rel = (Point)s->location - vantage_pt;
            if (rel.magnitude() < 1e-9)
            {
                continue;
            }

            Point u = normalize_point(rel);
            CandStar cand;
            cand.s = s;
            cand.mag = mag;
            cand.u = u;
            cand.id = (int)candidates.size();
            candidates.push_back(cand);
            star_u_map[s] = u;
            star_mag_map[s] = mag;
        }

        std::sort(candidates.begin(), candidates.end(), [](const CandStar& a, const CandStar& b)
        {
            return a.mag < b.mag;
        });
        for (size_t i = 0; i < candidates.size(); i++)
        {
            candidates[i].id = (int)i;
        }

        CandSpatialGrid cand_grid(candidates);

        // 3. Cluster stars by spatial proximity and brightness similarity
        std::vector<bool> assigned(candidates.size(), false);

        struct SkyCluster
        {
            std::vector<int> star_indices;
            Point center;
            double mean_mag = 0.0;
        };

        std::vector<SkyCluster> clusters;

        auto grow_cluster = [&](int seed_idx, int max_size)
        {
            SkyCluster cl;
            cl.star_indices.push_back(seed_idx);
            assigned[seed_idx] = true;
            cl.center = candidates[seed_idx].u;
            cl.mean_mag = candidates[seed_idx].mag;

            std::vector<int> cluster_candidates;
            cand_grid.for_each_near(cl.center, 25.0, [&](int i)
            {
                cluster_candidates.push_back(i);
            });

            while ((int)cl.star_indices.size() < max_size)
            {
                int best_cand = -1;
                double best_cost = 1e9;

                for (int i : cluster_candidates)
                {
                    if (assigned[i])
                    {
                        continue;
                    }
                    const auto& cand = candidates[i];

                    // Skip dimmer stars in initial cluster growth to allow constellations
                    // to reach across wider areas of prominent stars
                    if (cand.mag > 5.2 && cl.mean_mag < 4.5)
                    {
                        continue;
                    }
                    if (cand.mag > 5.7)
                    {
                        continue;
                    }

                    double dist_center = ang_dist_deg(cand.u, cl.center);
                    if (dist_center > 24.0)
                    {
                        continue;
                    }

                    double min_nbr_dist = 1e9;
                    double nbr_mag = cl.mean_mag;
                    for (int member_idx : cl.star_indices)
                    {
                        double d = ang_dist_deg(cand.u, candidates[member_idx].u);
                        if (d < min_nbr_dist)
                        {
                            min_nbr_dist = d;
                            nbr_mag = candidates[member_idx].mag;
                        }
                    }

                    if (min_nbr_dist > 14.5)
                    {
                        continue;
                    }

                    double mag_diff_nbr = std::abs(cand.mag - nbr_mag);
                    double mag_diff_mean = std::abs(cand.mag - cl.mean_mag);
                    // Clusters must consist of similarly bright stars
                    double max_delta = (cand.mag <= 3.5 || cl.mean_mag <= 3.5) ? 2.8 : 1.8;
                    if (mag_diff_mean > max_delta || mag_diff_nbr > max_delta)
                    {
                        continue;
                    }

                    // Spatial proximity is the most important metric, penalized by magnitude difference
                    double cost = min_nbr_dist + 2.5 * mag_diff_nbr + 1.0 * mag_diff_mean;
                    if (cost < best_cost)
                    {
                        best_cost = cost;
                        best_cand = (int)i;
                    }
                }

                if (best_cand >= 0 && best_cost <= 22.0)
                {
                    cl.star_indices.push_back(best_cand);
                    assigned[best_cand] = true;

                    Point sum_u(0, 0, 0);
                    double sum_mag = 0.0;
                    for (int idx : cl.star_indices)
                    {
                        sum_u = sum_u + candidates[idx].u;
                        sum_mag += candidates[idx].mag;
                    }
                    cl.center = normalize_point(sum_u);
                    cl.mean_mag = sum_mag / (double)cl.star_indices.size();
                }
                else
                {
                    break;
                }
            }

            clusters.push_back(cl);
        };

        // Phase A: Seed clusters from bright stars (mag < 3.2)
        for (size_t i = 0; i < candidates.size(); i++)
        {
            if (candidates[i].mag >= 3.2)
            {
                break;
            }
            if (!assigned[i])
            {
                grow_cluster((int)i, 13);
            }
        }

        // Phase B: Seed clusters from medium-bright stars (mag < 4.8), keeping cluster centers spaced
        for (size_t i = 0; i < candidates.size(); i++)
        {
            if (candidates[i].mag >= 4.8)
            {
                break;
            }
            if (assigned[i])
            {
                continue;
            }

            bool too_close = false;
            for (const auto& cl : clusters)
            {
                if (ang_dist_deg(candidates[i].u, cl.center) < 18.0)
                {
                    too_close = true;
                    break;
                }
            }
            if (!too_close)
            {
                grow_cluster((int)i, 13);
            }
        }

        // Phase C: Fill sky coverage using Fibonacci sphere points
        const int num_fib_samples = 150;
        const double phi = _pi * (sqrt(5.0) - 1.0);
        for (int i = 0; i < num_fib_samples; i++)
        {
            double y = 1.0 - ((double)i / (double)(num_fib_samples - 1)) * 2.0;
            double radius = sqrt(std::max(0.0, 1.0 - y * y));
            double theta = phi * (double)i;
            double x = cos(theta) * radius;
            double z = sin(theta) * radius;
            Point sample_pt(x, y, z);

            bool region_covered = false;
            for (const auto& cl : clusters)
            {
                if (ang_dist_deg(sample_pt, cl.center) < 18.0)
                {
                    region_covered = true;
                    break;
                }
            }

            if (!region_covered)
            {
                int best_seed = -1;
                double best_mag = 1e9;
                cand_grid.for_each_near(sample_pt, 14.0, [&](int ci)
                {
                    if (assigned[ci] || candidates[ci].mag > 5.5)
                    {
                        return;
                    }
                    if (ang_dist_deg(sample_pt, candidates[ci].u) < 12.0)
                    {
                        if (candidates[ci].mag < best_mag)
                        {
                            best_mag = candidates[ci].mag;
                            best_seed = ci;
                        }
                    }
                });

                if (best_seed >= 0)
                {
                    grow_cluster(best_seed, 11);
                }
            }
        }

        // Guarantee all stars with mag < 3.0 belong to a cluster
        for (size_t i = 0; i < candidates.size(); i++)
        {
            if (candidates[i].mag >= 3.0)
            {
                break;
            }
            if (!assigned[i])
            {
                int best_cl_idx = -1;
                double min_dist = 1e9;
                for (size_t ci = 0; ci < clusters.size(); ci++)
                {
                    double d = ang_dist_deg(candidates[i].u, clusters[ci].center);
                    if (d < min_dist && std::abs(candidates[i].mag - clusters[ci].mean_mag) <= 1.8)
                    {
                        min_dist = d;
                        best_cl_idx = (int)ci;
                    }
                }
                if (best_cl_idx >= 0 && min_dist <= 20.0)
                {
                    clusters[best_cl_idx].star_indices.push_back((int)i);
                    assigned[i] = true;
                }
                else
                {
                    grow_cluster((int)i, 8);
                }
            }
        }

        // 4. Line separation, impinging near-miss, and validity rules
        const std::unordered_set<Star*>* active_cluster_stars = nullptr;
        auto is_candidate_line_valid = [&](
            Star* sa, Point ua,
            Star* sb, Point ub,
            double len_deg, int target_cons_idx) -> bool
        {
            if (len_deg > 15.0 || len_deg < 0.1)
            {
                return false;
            }

            double half_len = 0.5 * len_deg;
            double cos_half = cos(half_len * (_pi / 180.0));

            // Condition 1: line (sa, sb) must not connect any star that is less than half its length
            // from a connected star of a different constellation.
            for (const auto& ps : placed_stars)
            {
                if (ps.cons_idx == target_cons_idx || ps.s == sa || ps.s == sb)
                {
                    continue;
                }
                if (dot_product(ua, ps.u) > cos_half || dot_product(ub, ps.u) > cos_half)
                {
                    return false;
                }
            }

            // Condition 1b: During initial cluster line generation (Phase 5), a line must not
            // have an endpoint closer than half its length to any unconnected bright star (mag < 3.0)
            // outside this cluster. Such a line would engulf the bright star in its exclusion zone
            // and permanently prevent it from joining any constellation.
            if (active_cluster_stars)
            {
                for (const auto& bc : candidates)
                {
                    if (bc.mag >= 3.0)
                    {
                        break;
                    }
                    if (bc.s == sa || bc.s == sb)
                    {
                        continue;
                    }
                    if (active_cluster_stars->count(bc.s))
                    {
                        continue;
                    }
                    if (dot_product(ua, bc.u) > cos_half || dot_product(ub, bc.u) > cos_half)
                    {
                        return false;
                    }
                }
            }

            // Condition 2: connecting sa or sb must not violate the half-length distance of any existing line in another constellation
            bool sa_new = (target_cons_idx >= (int)cons_stars.size() || !cons_stars[target_cons_idx].count(sa));
            bool sb_new = (target_cons_idx >= (int)cons_stars.size() || !cons_stars[target_cons_idx].count(sb));

            for (const auto& el : all_lines)
            {
                if (el.cons_idx == target_cons_idx)
                {
                    continue;
                }
                if (sa_new)
                {
                    if (dot_product(el.ua, ua) > el.cos_half_len || dot_product(el.ub, ua) > el.cos_half_len)
                    {
                        return false;
                    }
                }
                if (sb_new)
                {
                    if (dot_product(el.ua, ub) > el.cos_half_len || dot_product(el.ub, ub) > el.cos_half_len)
                    {
                        return false;
                    }
                }
            }

            // Condition 3: arcs must not intersect any existing line
            for (const auto& el : all_lines)
            {
                if (arcs_intersect(ua, ub, el.ua, el.ub))
                {
                    return false;
                }
            }

            // Condition 4: Near miss check against all candidate stars
            Point mid_u = normalize_point(ua + ub);
            bool near_miss = false;
            cand_grid.for_each_near(mid_u, 0.5 * len_deg + 2.5, [&](int cand_idx)
            {
                if (near_miss)
                {
                    return;
                }
                const auto& cand = candidates[cand_idx];
                if (cand.s == sa || cand.s == sb)
                {
                    return;
                }
                double d_out = 0.0;
                if (point_near_arc(ua, ub, cand.u, 1.5, &d_out, 0.8))
                {
                    near_miss = true;
                }
            });
            if (near_miss)
            {
                return false;
            }

            return true;
        };

        // 5. Line generation per cluster
        size_t name_cursor = 0;
        auto allocate_cons_name = [&](std::string& out_abbrev, std::string& out_name, std::string& out_genitive)
        {
            while (name_cursor < EXOCONS_NUM_IAU_CONSTELLATIONS)
            {
                std::string cand_abbr = iau_constellations[name_cursor].abbrev;
                if (!existing_cons_abbrevs.count(cand_abbr))
                {
                    out_abbrev = iau_constellations[name_cursor].abbrev;
                    out_name = iau_constellations[name_cursor].name;
                    out_genitive = iau_constellations[name_cursor].genitive;
                    existing_cons_abbrevs.insert(cand_abbr);
                    name_cursor++;
                    return;
                }
                name_cursor++;
            }
            out_abbrev = "Exo" + std::to_string(out_conss.size() + 1);
            out_name = out_abbrev;
            out_genitive = out_abbrev;
        };

        for (size_t cl_i = 0; cl_i < clusters.size(); cl_i++)
        {
            const auto& cluster = clusters[cl_i];
            const auto& star_indices = cluster.star_indices;
            int n_stars = (int)star_indices.size();
            if (n_stars < 2)
            {
                continue;
            }

            std::unordered_set<Star*> curr_cluster_set;
            for (int idx : star_indices)
            {
                curr_cluster_set.insert(candidates[idx].s);
            }
            active_cluster_stars = &curr_cluster_set;

            struct CandEdge
            {
                int u_idx = 0;
                int v_idx = 0;
                double cost = 0.0;
                double len_deg = 0.0;
            };

            std::vector<CandEdge> valid_edges;
            int target_c_idx = (int)cons_stars.size();

            for (int i = 0; i < n_stars; i++)
            {
                for (int j = i + 1; j < n_stars; j++)
                {
                    const auto& ca = candidates[star_indices[i]];
                    const auto& cb = candidates[star_indices[j]];

                    double len = ang_dist_deg(ca.u, cb.u);
                    if (len > 15.0 || len < 0.1)
                    {
                        continue;
                    }

                    if (!is_candidate_line_valid(ca.s, ca.u, cb.s, cb.u, len, target_c_idx))
                    {
                        continue;
                    }

                    double mag_diff = std::abs(ca.mag - cb.mag);
                    double cost = len * (1.0 + 0.4 * mag_diff) * (1.0 + 0.1 * std::min(ca.mag, cb.mag));
                    valid_edges.push_back({i, j, cost, len});
                }
            }

            if (valid_edges.empty())
            {
                continue;
            }

            std::sort(valid_edges.begin(), valid_edges.end(), [](const CandEdge& a, const CandEdge& b)
            {
                return a.cost < b.cost;
            });

            // Minimum Spanning Tree / Forest using Disjoint Set Union
            std::vector<int> parent(n_stars);
            for (int i = 0; i < n_stars; i++)
            {
                parent[i] = i;
            }
            std::function<int(int)> find_parent = [&](int x) -> int
            {
                if (parent[x] == x)
                {
                    return x;
                }
                return parent[x] = find_parent(parent[x]);
            };

            std::vector<int> degrees(n_stars, 0);
            std::vector<CandEdge> accepted_edges;

            for (const auto& edge : valid_edges)
            {
                if (degrees[edge.u_idx] >= 3 || degrees[edge.v_idx] >= 3)
                {
                    continue;
                }
                int root_u = find_parent(edge.u_idx);
                int root_v = find_parent(edge.v_idx);
                if (root_u == root_v)
                {
                    continue;
                }

                // Check intersection with edges in this constellation
                const auto& ca = candidates[star_indices[edge.u_idx]];
                const auto& cb = candidates[star_indices[edge.v_idx]];
                bool crosses = false;
                for (const auto& acc : accepted_edges)
                {
                    const auto& ua = candidates[star_indices[acc.u_idx]];
                    const auto& ub = candidates[star_indices[acc.v_idx]];
                    if (arcs_intersect(ca.u, cb.u, ua.u, ub.u))
                    {
                        crosses = true;
                        break;
                    }
                }
                if (crosses)
                {
                    continue;
                }

                parent[root_u] = root_v;
                degrees[edge.u_idx]++;
                degrees[edge.v_idx]++;
                accepted_edges.push_back(edge);
            }

            // Add cycle / chord edges to close open ends into loops (triangles, quadrilaterals, polygons)
            if (accepted_edges.size() >= 3)
            {
                std::vector<CandEdge> chord_candidates;
                for (const auto& edge : valid_edges)
                {
                    bool already = false;
                    for (const auto& acc : accepted_edges)
                    {
                        if ((acc.u_idx == edge.u_idx && acc.v_idx == edge.v_idx) ||
                            (acc.u_idx == edge.v_idx && acc.v_idx == edge.u_idx))
                        {
                            already = true;
                            break;
                        }
                    }
                    if (!already)
                    {
                        chord_candidates.push_back(edge);
                    }
                }

                std::sort(chord_candidates.begin(), chord_candidates.end(), [&](const CandEdge& a, const CandEdge& b)
                {
                    int deg_score_a = (degrees[a.u_idx] == 1 ? 3 : (degrees[a.u_idx] == 2 ? 1 : 0)) +
                                      (degrees[a.v_idx] == 1 ? 3 : (degrees[a.v_idx] == 2 ? 1 : 0));
                    int deg_score_b = (degrees[b.u_idx] == 1 ? 3 : (degrees[b.u_idx] == 2 ? 1 : 0)) +
                                      (degrees[b.v_idx] == 1 ? 3 : (degrees[b.v_idx] == 2 ? 1 : 0));
                    if (deg_score_a != deg_score_b)
                    {
                        return deg_score_a > deg_score_b;
                    }
                    return a.cost < b.cost;
                });

                int chords_added = 0;
                int max_chords = std::min(3, std::max(1, (int)accepted_edges.size() / 3));

                for (const auto& edge : chord_candidates)
                {
                    if (chords_added >= max_chords)
                    {
                        break;
                    }
                    if (degrees[edge.u_idx] >= 3 || degrees[edge.v_idx] >= 3)
                    {
                        continue;
                    }

                    const auto& ca = candidates[star_indices[edge.u_idx]];
                    const auto& cb = candidates[star_indices[edge.v_idx]];
                    bool crosses = false;
                    for (const auto& acc : accepted_edges)
                    {
                        const auto& ua = candidates[star_indices[acc.u_idx]];
                        const auto& ub = candidates[star_indices[acc.v_idx]];
                        if (arcs_intersect(ca.u, cb.u, ua.u, ub.u))
                        {
                            crosses = true;
                            break;
                        }
                    }
                    if (crosses)
                    {
                        continue;
                    }

                    degrees[edge.u_idx]++;
                    degrees[edge.v_idx]++;
                    accepted_edges.push_back(edge);
                    chords_added++;
                }
            }

            if (accepted_edges.empty())
            {
                continue;
            }

            std::unordered_map<int, std::vector<int>> adj;
            for (const auto& acc : accepted_edges)
            {
                adj[acc.u_idx].push_back(acc.v_idx);
                adj[acc.v_idx].push_back(acc.u_idx);
            }

            std::unordered_set<int> visited;
            int best_comp_size = 0;
            std::unordered_set<int> best_comp_stars;

            for (const auto& pair : adj)
            {
                if (visited.count(pair.first))
                {
                    continue;
                }
                std::unordered_set<int> comp;
                std::vector<int> q;
                q.push_back(pair.first);
                visited.insert(pair.first);
                comp.insert(pair.first);

                while (!q.empty())
                {
                    int curr = q.back();
                    q.pop_back();
                    for (int nbr : adj[curr])
                    {
                        if (!visited.count(nbr))
                        {
                            visited.insert(nbr);
                            comp.insert(nbr);
                            q.push_back(nbr);
                        }
                    }
                }

                double comp_score = (double)comp.size();
                for (int star_idx : comp)
                {
                    if (candidates[star_indices[star_idx]].mag < 3.0)
                    {
                        comp_score += 100.0;
                    }
                }

                if (comp_score > best_comp_size)
                {
                    best_comp_size = (int)comp_score;
                    best_comp_stars = comp;
                }
            }

            std::vector<CandEdge> final_edges;
            for (const auto& acc : accepted_edges)
            {
                if (best_comp_stars.count(acc.u_idx) && best_comp_stars.count(acc.v_idx))
                {
                    final_edges.push_back(acc);
                }
            }

            if (final_edges.size() < 3)
            {
                continue;
            }

            // Select next available constellation name
            std::string c_abbrev;
            std::string c_name;
            std::string c_genitive;
            allocate_cons_name(c_abbrev, c_name, c_genitive);

            Constellation cons;
            cons.abbrev = c_abbrev;
            cons.name = c_name;
            cons.genitive = c_genitive;
            cons.vantage = vantage_pt;
            cons.vantage_name = sys_name;
            cons.vantage_resolved = true;

            std::unordered_set<Star*> new_c_stars;
            int new_c_idx = (int)cons_stars.size();

            for (const auto& edge : final_edges)
            {
                Star* sa = candidates[star_indices[edge.u_idx]].s;
                Star* sb = candidates[star_indices[edge.v_idx]].s;
                Point ua = candidates[star_indices[edge.u_idx]].u;
                Point ub = candidates[star_indices[edge.v_idx]].u;

                ConsLine cl;
                cl.a = sa;
                cl.b = sb;
                cl.starnamea = get_consline_star_name(sa);
                cl.starnameb = get_consline_star_name(sb);
                cons.lines.push_back(cl);

                new_c_stars.insert(sa);
                new_c_stars.insert(sb);
                double cos_half = cos(0.5 * edge.len_deg * (_pi / 180.0));
                all_lines.push_back({sa, sb, ua, ub, edge.len_deg, cos_half, new_c_idx});
                placed_stars.push_back({sa, ua, new_c_idx});
                placed_stars.push_back({sb, ub, new_c_idx});
            }

            cons_stars.push_back(new_c_stars);
            out_conss.push_back(cons);
        }

        active_cluster_stars = nullptr;

        // 6. Guarantee all stars with mag < 3.0 are connected
        std::unordered_set<Star*> all_connected_stars;
        for (const auto& c_set : cons_stars)
        {
            for (Star* s : c_set)
            {
                all_connected_stars.insert(s);
            }
        }

        for (size_t ci = 0; ci < candidates.size(); ci++)
        {
            if (candidates[ci].mag >= 3.0)
            {
                break;
            }
            Star* s_bright = candidates[ci].s;
            if (all_connected_stars.count(s_bright))
            {
                continue;
            }

            Point u_bright = candidates[ci].u;
            bool connected = false;

            // Step A: Attempt to attach to an existing constellation (direct or via bridging star)
            Star* best_existing_target = nullptr;
            Point best_existing_target_u;
            Star* best_mid_target = nullptr;
            Point best_mid_target_u;
            int best_existing_c_idx = -1;
            int best_existing_out_idx = -1;
            double best_existing_dist = 1e9;

            for (size_t out_i = 0; out_i < out_conss.size(); out_i++)
            {
                int c_idx = existing_cons_count + (int)out_i;
                std::unordered_map<Star*, int> deg_map;
                for (const auto& cl : out_conss[out_i].lines)
                {
                    deg_map[cl.a]++;
                    deg_map[cl.b]++;
                }

                for (const auto& pair : deg_map)
                {
                    if (pair.second >= 3)
                    {
                        continue;
                    }
                    Star* target = pair.first;
                    auto it_u = star_u_map.find(target);
                    if (it_u == star_u_map.end())
                    {
                        continue;
                    }
                    double len = ang_dist_deg(u_bright, it_u->second);
                    if (len <= 14.0 && len < best_existing_dist)
                    {
                        if (is_candidate_line_valid(s_bright, u_bright, target, it_u->second, len, c_idx))
                        {
                            best_existing_dist = len;
                            best_existing_target = target;
                            best_existing_target_u = it_u->second;
                            best_existing_c_idx = c_idx;
                            best_existing_out_idx = (int)out_i;
                            best_mid_target = nullptr;
                        }
                        else
                        {
                            // Check if an intermediate unconnected star bridges s_bright and target
                            cand_grid.for_each_near(u_bright, len + 1.5, [&](int mi)
                            {
                                if (best_mid_target)
                                {
                                    return;
                                }
                                Star* sm = candidates[mi].s;
                                if (sm == s_bright || sm == target || all_connected_stars.count(sm))
                                {
                                    return;
                                }
                                Point um = candidates[mi].u;
                                double len_a = ang_dist_deg(u_bright, um);
                                double len_b = ang_dist_deg(um, it_u->second);
                                if (len_a > 15.0 || len_b > 15.0)
                                {
                                    return;
                                }
                                if (len_a + len_b > len + 1.5)
                                {
                                    return;
                                }
                                if (is_candidate_line_valid(s_bright, u_bright, sm, um, len_a, c_idx) &&
                                    is_candidate_line_valid(sm, um, target, it_u->second, len_b, c_idx))
                                {
                                    best_existing_dist = len;
                                    best_existing_target = target;
                                    best_existing_target_u = it_u->second;
                                    best_existing_c_idx = c_idx;
                                    best_existing_out_idx = (int)out_i;
                                    best_mid_target = sm;
                                    best_mid_target_u = um;
                                }
                            });
                        }
                    }
                }
            }

            if (best_existing_target && best_existing_out_idx >= 0 && best_existing_out_idx < (int)out_conss.size())
            {
                if (best_mid_target)
                {
                    ConsLine cl1;
                    cl1.a = s_bright;
                    cl1.b = best_mid_target;
                    cl1.starnamea = get_consline_star_name(s_bright);
                    cl1.starnameb = get_consline_star_name(best_mid_target);
                    out_conss[best_existing_out_idx].lines.push_back(cl1);

                    double la = ang_dist_deg(u_bright, best_mid_target_u);
                    double cos_half_a = cos(0.5 * la * (_pi / 180.0));
                    all_lines.push_back({s_bright, best_mid_target, u_bright, best_mid_target_u, la, cos_half_a, best_existing_c_idx});
                    placed_stars.push_back({s_bright, u_bright, best_existing_c_idx});
                    placed_stars.push_back({best_mid_target, best_mid_target_u, best_existing_c_idx});

                    ConsLine cl2;
                    cl2.a = best_mid_target;
                    cl2.b = best_existing_target;
                    cl2.starnamea = get_consline_star_name(best_mid_target);
                    cl2.starnameb = get_consline_star_name(best_existing_target);
                    out_conss[best_existing_out_idx].lines.push_back(cl2);

                    double lb = ang_dist_deg(best_mid_target_u, best_existing_target_u);
                    double cos_half_b = cos(0.5 * lb * (_pi / 180.0));
                    all_lines.push_back({best_mid_target, best_existing_target, best_mid_target_u, best_existing_target_u, lb, cos_half_b, best_existing_c_idx});

                    cons_stars[best_existing_c_idx].insert(s_bright);
                    cons_stars[best_existing_c_idx].insert(best_mid_target);
                    all_connected_stars.insert(s_bright);
                    all_connected_stars.insert(best_mid_target);
                    connected = true;
                }
                else
                {
                    ConsLine cl;
                    cl.a = s_bright;
                    cl.b = best_existing_target;
                    cl.starnamea = get_consline_star_name(s_bright);
                    cl.starnameb = get_consline_star_name(best_existing_target);
                    out_conss[best_existing_out_idx].lines.push_back(cl);

                    double cos_half = cos(0.5 * best_existing_dist * (_pi / 180.0));
                    all_lines.push_back({s_bright, best_existing_target, u_bright, best_existing_target_u, best_existing_dist, cos_half, best_existing_c_idx});
                    placed_stars.push_back({s_bright, u_bright, best_existing_c_idx});
                    cons_stars[best_existing_c_idx].insert(s_bright);
                    all_connected_stars.insert(s_bright);
                    connected = true;
                }
            }

            if (!connected)
            {
                // Step B: Form a dedicated constellation around s_bright using unconnected stars
                std::vector<int> b_stars;
                b_stars.push_back((int)ci);

                cand_grid.for_each_near(u_bright, 15.0, [&](int other_i)
                {
                    if (other_i == (int)ci || all_connected_stars.count(candidates[other_i].s) || candidates[other_i].mag > 6.0)
                    {
                        return;
                    }
                    double d = ang_dist_deg(u_bright, candidates[other_i].u);
                    if (d <= 15.0 && d >= 1.0)
                    {
                        b_stars.push_back(other_i);
                    }
                });

                if (b_stars.size() >= 4)
                {
                    int bn = (int)b_stars.size();
                    int target_c_idx = (int)cons_stars.size();

                    struct BEdge
                    {
                        int u = 0;
                        int v = 0;
                        double cost = 0.0;
                        double len = 0.0;
                    };
                    std::vector<BEdge> b_edges;

                    for (int ba = 0; ba < bn; ba++)
                    {
                        for (int bb = ba + 1; bb < bn; bb++)
                        {
                            Star* sa = candidates[b_stars[ba]].s;
                            Star* sb = candidates[b_stars[bb]].s;
                            Point ua = candidates[b_stars[ba]].u;
                            Point ub = candidates[b_stars[bb]].u;
                            double len = ang_dist_deg(ua, ub);

                            if (is_candidate_line_valid(sa, ua, sb, ub, len, target_c_idx))
                            {
                                double cost = len * (1.0 + 0.4 * std::abs(candidates[b_stars[ba]].mag - candidates[b_stars[bb]].mag));
                                b_edges.push_back({ba, bb, cost, len});
                            }
                        }
                    }

                    if (!b_edges.empty())
                    {
                        std::sort(b_edges.begin(), b_edges.end(), [](const BEdge& a, const BEdge& b)
                        {
                            return a.cost < b.cost;
                        });

                        std::vector<int> b_parent(bn);
                        for (int bi = 0; bi < bn; bi++)
                        {
                            b_parent[bi] = bi;
                        }
                        std::function<int(int)> find_bp = [&](int x) -> int
                        {
                            if (b_parent[x] == x)
                            {
                                return x;
                            }
                            return b_parent[x] = find_bp(b_parent[x]);
                        };

                        std::vector<BEdge> b_accepted;
                        std::vector<int> b_deg(bn, 0);

                        for (const auto& be : b_edges)
                        {
                            if (b_deg[be.u] >= 3 || b_deg[be.v] >= 3)
                            {
                                continue;
                            }
                            int ru = find_bp(be.u);
                            int rv = find_bp(be.v);
                            if (ru == rv)
                            {
                                continue;
                            }

                            bool cross = false;
                            for (const auto& acc : b_accepted)
                            {
                                if (arcs_intersect(candidates[b_stars[be.u]].u, candidates[b_stars[be.v]].u,
                                                   candidates[b_stars[acc.u]].u, candidates[b_stars[acc.v]].u))
                                {
                                    cross = true;
                                    break;
                                }
                            }
                            if (cross)
                            {
                                continue;
                            }

                            b_parent[ru] = rv;
                            b_deg[be.u]++;
                            b_deg[be.v]++;
                            b_accepted.push_back(be);
                        }

                        if (b_accepted.size() >= 3)
                        {
                            std::vector<BEdge> b_chord_cands;
                            for (const auto& be : b_edges)
                            {
                                bool already = false;
                                for (const auto& acc : b_accepted)
                                {
                                    if ((acc.u == be.u && acc.v == be.v) ||
                                        (acc.u == be.v && acc.v == be.u))
                                    {
                                        already = true;
                                        break;
                                    }
                                }
                                if (!already)
                                {
                                    b_chord_cands.push_back(be);
                                }
                            }

                            int b_chords = 0;
                            for (const auto& be : b_chord_cands)
                            {
                                if (b_chords >= 2 || b_deg[be.u] >= 3 || b_deg[be.v] >= 3)
                                {
                                    break;
                                }
                                bool cross = false;
                                for (const auto& acc : b_accepted)
                                {
                                    if (arcs_intersect(candidates[b_stars[be.u]].u, candidates[b_stars[be.v]].u,
                                                       candidates[b_stars[acc.u]].u, candidates[b_stars[acc.v]].u))
                                    {
                                        cross = true;
                                        break;
                                    }
                                }
                                if (cross)
                                {
                                    continue;
                                }
                                b_deg[be.u]++;
                                b_deg[be.v]++;
                                b_accepted.push_back(be);
                                b_chords++;
                            }
                        }

                        std::unordered_map<int, std::vector<int>> b_adj;
                        for (const auto& acc : b_accepted)
                        {
                            b_adj[acc.u].push_back(acc.v);
                            b_adj[acc.v].push_back(acc.u);
                        }

                        std::unordered_set<int> b_comp;
                        std::vector<int> q;
                        q.push_back(0);
                        b_comp.insert(0);

                        while (!q.empty())
                        {
                            int curr = q.back();
                            q.pop_back();
                            for (int nbr : b_adj[curr])
                            {
                                if (!b_comp.count(nbr))
                                {
                                    b_comp.insert(nbr);
                                    q.push_back(nbr);
                                }
                            }
                        }

                        std::vector<ConsLine> b_lines;
                        std::vector<LineRecord> b_line_records;
                        std::unordered_set<Star*> b_c_stars;

                        for (const auto& be : b_accepted)
                        {
                            if (b_comp.count(be.u) && b_comp.count(be.v))
                            {
                                Star* sa = candidates[b_stars[be.u]].s;
                                Star* sb = candidates[b_stars[be.v]].s;
                                Point ua = candidates[b_stars[be.u]].u;
                                Point ub = candidates[b_stars[be.v]].u;

                                ConsLine cl;
                                cl.a = sa;
                                cl.b = sb;
                                cl.starnamea = get_consline_star_name(sa);
                                cl.starnameb = get_consline_star_name(sb);
                                double cos_half = cos(0.5 * be.len * (_pi / 180.0));
                                b_lines.push_back(cl);
                                b_line_records.push_back({sa, sb, ua, ub, be.len, cos_half, target_c_idx});
                                b_c_stars.insert(sa);
                                b_c_stars.insert(sb);
                            }
                        }

                        if (b_lines.size() >= 3)
                        {
                            std::string c_abbrev;
                            std::string c_name;
                            std::string c_genitive;
                            allocate_cons_name(c_abbrev, c_name, c_genitive);

                            Constellation cons;
                            cons.abbrev = c_abbrev;
                            cons.name = c_name;
                            cons.genitive = c_genitive;
                            cons.vantage = vantage_pt;
                            cons.vantage_name = sys_name;
                            cons.vantage_resolved = true;
                            cons.lines = b_lines;

                            for (const auto& bl : b_line_records)
                            {
                                all_lines.push_back(bl);
                                placed_stars.push_back({bl.a, bl.ua, target_c_idx});
                                placed_stars.push_back({bl.b, bl.ub, target_c_idx});
                            }
                            for (Star* s : b_c_stars)
                            {
                                all_connected_stars.insert(s);
                            }

                            cons_stars.push_back(b_c_stars);
                            out_conss.push_back(cons);
                            connected = true;
                        }
                    }
                }

                if (!connected)
                {
                    std::vector<int> near_indices;
                    cand_grid.for_each_near(u_bright, 15.0, [&](int other_i)
                    {
                        if (other_i == (int)ci || all_connected_stars.count(candidates[other_i].s))
                        {
                            return;
                        }
                        double d = ang_dist_deg(u_bright, candidates[other_i].u);
                        if (d <= 15.0)
                        {
                            near_indices.push_back(other_i);
                        }
                    });
                    std::sort(near_indices.begin(), near_indices.end(), [&](int a, int b)
                    {
                        return ang_dist_deg(u_bright, candidates[a].u) < ang_dist_deg(u_bright, candidates[b].u);
                    });

                    std::vector<Star*> dedicated_stars;
                    int target_c_idx = (int)cons_stars.size();

                    for (int near_idx : near_indices)
                    {
                        Star* st = candidates[near_idx].s;
                        Point ut = candidates[near_idx].u;
                        double len = ang_dist_deg(u_bright, ut);
                        if (is_candidate_line_valid(s_bright, u_bright, st, ut, len, target_c_idx))
                        {
                            dedicated_stars.push_back(st);
                            if (dedicated_stars.size() == 3)
                            {
                                break;
                            }
                        }
                    }

                    if (dedicated_stars.size() >= 3)
                    {
                        std::string c_abbrev;
                        std::string c_name;
                        std::string c_genitive;
                        allocate_cons_name(c_abbrev, c_name, c_genitive);

                        Constellation cons;
                        cons.abbrev = c_abbrev;
                        cons.name = c_name;
                        cons.genitive = c_genitive;
                        cons.vantage = vantage_pt;
                        cons.vantage_name = sys_name;
                        cons.vantage_resolved = true;

                        std::unordered_set<Star*> new_c_stars;
                        new_c_stars.insert(s_bright);
                        placed_stars.push_back({s_bright, u_bright, target_c_idx});

                        for (Star* st : dedicated_stars)
                        {
                            ConsLine cl;
                            cl.a = s_bright;
                            cl.b = st;
                            cl.starnamea = get_consline_star_name(s_bright);
                            cl.starnameb = get_consline_star_name(st);
                            cons.lines.push_back(cl);

                            Point ut = star_u_map[st];
                            double len = ang_dist_deg(u_bright, ut);
                            double cos_half = cos(0.5 * len * (_pi / 180.0));
                            all_lines.push_back({s_bright, st, u_bright, ut, len, cos_half, target_c_idx});
                            placed_stars.push_back({st, ut, target_c_idx});
                            new_c_stars.insert(st);
                            all_connected_stars.insert(st);
                        }

                        cons_stars.push_back(new_c_stars);
                        all_connected_stars.insert(s_bright);
                        out_conss.push_back(cons);
                        connected = true;
                    }
                }
            }
        }

        // 6.5. Scavenge unjoined stars with mag <= 4.0 to join the nearest constellation
        for (size_t ci = 0; ci < candidates.size(); ci++)
        {
            if (candidates[ci].mag > 4.0)
            {
                break;
            }
            Star* s_cand = candidates[ci].s;
            if (all_connected_stars.count(s_cand))
            {
                continue;
            }

            Point u_cand = candidates[ci].u;

            // Find closest candidate star in placed_stars to connect directly
            Star* best_target = nullptr;
            Point best_target_u;
            int best_c_idx = -1;
            double best_dist = 1e9;

            for (const auto& ps : placed_stars)
            {
                if (ps.cons_idx < existing_cons_count)
                {
                    continue;
                }
                int out_i = ps.cons_idx - existing_cons_count;
                if (out_i < 0 || out_i >= (int)out_conss.size())
                {
                    continue;
                }
                double d = ang_dist_deg(u_cand, ps.u);
                if (d <= 15.0 && d < best_dist)
                {
                    int deg = 0;
                    for (const auto& cl : out_conss[out_i].lines)
                    {
                        if (cl.a == ps.s || cl.b == ps.s)
                        {
                            deg++;
                        }
                    }
                    if (deg < 3 && is_candidate_line_valid(s_cand, u_cand, ps.s, ps.u, d, ps.cons_idx))
                    {
                        best_dist = d;
                        best_target = ps.s;
                        best_target_u = ps.u;
                        best_c_idx = ps.cons_idx;
                    }
                }
            }

            if (best_target && best_c_idx >= existing_cons_count)
            {
                int out_i = best_c_idx - existing_cons_count;
                if (out_i >= 0 && out_i < (int)out_conss.size())
                {
                    ConsLine cl;
                    cl.a = s_cand;
                    cl.b = best_target;
                    cl.starnamea = get_consline_star_name(s_cand);
                    cl.starnameb = get_consline_star_name(best_target);
                    out_conss[out_i].lines.push_back(cl);

                    double cos_half = cos(0.5 * best_dist * (_pi / 180.0));
                    all_lines.push_back({s_cand, best_target, u_cand, best_target_u, best_dist, cos_half, best_c_idx});
                    placed_stars.push_back({s_cand, u_cand, best_c_idx});
                    cons_stars[best_c_idx].insert(s_cand);
                    all_connected_stars.insert(s_cand);

                    // Optionally close a loop if another node in this constellation is within 14.0 deg
                    for (const auto& ps : placed_stars)
                    {
                        if (ps.cons_idx != best_c_idx || ps.s == s_cand || ps.s == best_target)
                        {
                            continue;
                        }
                        double d2 = ang_dist_deg(u_cand, ps.u);
                        if (d2 <= 14.0)
                        {
                            int deg2 = 0;
                            for (const auto& cl : out_conss[out_i].lines)
                            {
                                if (cl.a == ps.s || cl.b == ps.s)
                                {
                                    deg2++;
                                }
                            }
                            if (deg2 < 3 && is_candidate_line_valid(s_cand, u_cand, ps.s, ps.u, d2, best_c_idx))
                            {
                                ConsLine cl_loop;
                                cl_loop.a = s_cand;
                                cl_loop.b = ps.s;
                                cl_loop.starnamea = get_consline_star_name(s_cand);
                                cl_loop.starnameb = get_consline_star_name(ps.s);
                                out_conss[out_i].lines.push_back(cl_loop);

                                double cos_half2 = cos(0.5 * d2 * (_pi / 180.0));
                                all_lines.push_back({s_cand, ps.s, u_cand, ps.u, d2, cos_half2, best_c_idx});
                                break;
                            }
                        }
                    }
                }
            }
        }
        // 7. Guarantee at least 50 constellations and >= 80% sky coverage
        std::vector<Point> lined_dirs;
        for (const auto& l : all_lines)
        {
            lined_dirs.push_back(l.ua);
            lined_dirs.push_back(l.ub);
        }

        double coverage = calculate_sky_coverage(lined_dirs, 1000);

        const int fill_samples = 400;
        for (int i = 0; i < fill_samples && (coverage < 0.80 || out_conss.size() < 50); i++)
        {
            double y = 1.0 - ((double)i / (double)(fill_samples - 1)) * 2.0;
            double radius = sqrt(std::max(0.0, 1.0 - y * y));
            double theta = phi * (double)i;
            double x = cos(theta) * radius;
            double z = sin(theta) * radius;
            Point sample_pt(x, y, z);

            bool near_lined = false;
            for (const auto& u : lined_dirs)
            {
                if (ang_dist_deg(sample_pt, u) < 6.0)
                {
                    near_lined = true;
                    break;
                }
            }
            if (near_lined && coverage >= 0.80 && out_conss.size() >= 50)
            {
                continue;
            }

            std::vector<int> gap_stars;
            cand_grid.for_each_near(sample_pt, 14.0, [&](int ci)
            {
                if (all_connected_stars.count(candidates[ci].s) || candidates[ci].mag > 5.8)
                {
                    return;
                }
                if (ang_dist_deg(sample_pt, candidates[ci].u) < 12.0)
                {
                    gap_stars.push_back(ci);
                }
            });

            if (gap_stars.size() >= 4)
            {
                int target_c_idx = (int)cons_stars.size();
                int gn = (int)gap_stars.size();

                struct GEdge
                {
                    int u = 0;
                    int v = 0;
                    double cost = 0.0;
                    double len = 0.0;
                };
                std::vector<GEdge> g_edges;

                for (int ga = 0; ga < gn; ga++)
                {
                    for (int gb = ga + 1; gb < gn; gb++)
                    {
                        Star* sa = candidates[gap_stars[ga]].s;
                        Star* sb = candidates[gap_stars[gb]].s;
                        Point ua = candidates[gap_stars[ga]].u;
                        Point ub = candidates[gap_stars[gb]].u;
                        double len = ang_dist_deg(ua, ub);

                        if (is_candidate_line_valid(sa, ua, sb, ub, len, target_c_idx))
                        {
                            double cost = len * (1.0 + 0.4 * std::abs(candidates[gap_stars[ga]].mag - candidates[gap_stars[gb]].mag));
                            g_edges.push_back({ga, gb, cost, len});
                        }
                    }
                }

                if (g_edges.empty())
                {
                    continue;
                }

                std::sort(g_edges.begin(), g_edges.end(), [](const GEdge& a, const GEdge& b)
                {
                    return a.cost < b.cost;
                });

                std::vector<int> g_parent(gn);
                for (int gi = 0; gi < gn; gi++)
                {
                    g_parent[gi] = gi;
                }
                std::function<int(int)> find_gp = [&](int x) -> int
                {
                    if (g_parent[x] == x)
                    {
                        return x;
                    }
                    return g_parent[x] = find_gp(g_parent[x]);
                };

                std::vector<GEdge> g_accepted;
                std::vector<int> g_deg(gn, 0);

                for (const auto& ge : g_edges)
                {
                    if (g_deg[ge.u] >= 3 || g_deg[ge.v] >= 3)
                    {
                        continue;
                    }
                    int ru = find_gp(ge.u);
                    int rv = find_gp(ge.v);
                    if (ru == rv)
                    {
                        continue;
                    }
                    // Arcs intersect check
                    bool cross = false;
                    for (const auto& acc : g_accepted)
                    {
                        if (arcs_intersect(candidates[gap_stars[ge.u]].u, candidates[gap_stars[ge.v]].u,
                                           candidates[gap_stars[acc.u]].u, candidates[gap_stars[acc.v]].u))
                        {
                            cross = true;
                            break;
                        }
                    }
                    if (cross)
                    {
                        continue;
                    }

                    g_parent[ru] = rv;
                    g_deg[ge.u]++;
                    g_deg[ge.v]++;
                    g_accepted.push_back(ge);
                }

                // Add chord edges to gap constellation to close loops
                if (g_accepted.size() >= 3)
                {
                    std::vector<GEdge> gap_chord_cands;
                    for (const auto& ge : g_edges)
                    {
                        bool already = false;
                        for (const auto& acc : g_accepted)
                        {
                            if ((acc.u == ge.u && acc.v == ge.v) ||
                                (acc.u == ge.v && acc.v == ge.u))
                            {
                                already = true;
                                break;
                            }
                        }
                        if (!already)
                        {
                            gap_chord_cands.push_back(ge);
                        }
                    }

                    std::sort(gap_chord_cands.begin(), gap_chord_cands.end(), [&](const GEdge& a, const GEdge& b)
                    {
                        int score_a = (g_deg[a.u] == 1 ? 3 : (g_deg[a.u] == 2 ? 1 : 0)) +
                                      (g_deg[a.v] == 1 ? 3 : (g_deg[a.v] == 2 ? 1 : 0));
                        int score_b = (g_deg[b.u] == 1 ? 3 : (g_deg[b.u] == 2 ? 1 : 0)) +
                                      (g_deg[b.v] == 1 ? 3 : (g_deg[b.v] == 2 ? 1 : 0));
                        if (score_a != score_b)
                        {
                            return score_a > score_b;
                        }
                        return a.cost < b.cost;
                    });

                    int gap_chords = 0;
                    for (const auto& ge : gap_chord_cands)
                    {
                        if (gap_chords >= 2)
                        {
                            break;
                        }
                        if (g_deg[ge.u] >= 3 || g_deg[ge.v] >= 3)
                        {
                            continue;
                        }
                        bool crosses = false;
                        for (const auto& acc : g_accepted)
                        {
                            if (arcs_intersect(candidates[gap_stars[ge.u]].u, candidates[gap_stars[ge.v]].u,
                                               candidates[gap_stars[acc.u]].u, candidates[gap_stars[acc.v]].u))
                            {
                                crosses = true;
                                break;
                            }
                        }
                        if (crosses)
                        {
                            continue;
                        }

                        g_deg[ge.u]++;
                        g_deg[ge.v]++;
                        g_accepted.push_back(ge);
                        gap_chords++;
                    }
                }

                // Find largest component
                std::unordered_map<int, std::vector<int>> g_adj;
                for (const auto& acc : g_accepted)
                {
                    g_adj[acc.u].push_back(acc.v);
                    g_adj[acc.v].push_back(acc.u);
                }
                std::unordered_set<int> g_visited;
                int best_comp_sz = 0;
                std::unordered_set<int> best_comp_nodes;

                for (const auto& pair : g_adj)
                {
                    if (g_visited.count(pair.first))
                    {
                        continue;
                    }
                    std::unordered_set<int> comp;
                    std::vector<int> q;
                    q.push_back(pair.first);
                    g_visited.insert(pair.first);
                    comp.insert(pair.first);
                    while (!q.empty())
                    {
                        int curr = q.back();
                        q.pop_back();
                        for (int nbr : g_adj[curr])
                        {
                            if (!g_visited.count(nbr))
                            {
                                g_visited.insert(nbr);
                                comp.insert(nbr);
                                q.push_back(nbr);
                            }
                        }
                    }
                    if ((int)comp.size() > best_comp_sz)
                    {
                        best_comp_sz = (int)comp.size();
                        best_comp_nodes = comp;
                    }
                }

                std::vector<ConsLine> gap_lines;
                std::vector<LineRecord> gap_line_records;
                std::unordered_set<Star*> gap_c_stars;

                for (const auto& ge : g_accepted)
                {
                    if (best_comp_nodes.count(ge.u) && best_comp_nodes.count(ge.v))
                    {
                        Star* sa = candidates[gap_stars[ge.u]].s;
                        Star* sb = candidates[gap_stars[ge.v]].s;
                        Point ua = candidates[gap_stars[ge.u]].u;
                        Point ub = candidates[gap_stars[ge.v]].u;

                        ConsLine cl;
                        cl.a = sa;
                        cl.b = sb;
                        cl.starnamea = get_consline_star_name(sa);
                        cl.starnameb = get_consline_star_name(sb);
                        double cos_half = cos(0.5 * ge.len * (_pi / 180.0));
                        gap_lines.push_back(cl);
                        gap_line_records.push_back({sa, sb, ua, ub, ge.len, cos_half, target_c_idx});
                        gap_c_stars.insert(sa);
                        gap_c_stars.insert(sb);
                    }
                }

                if (gap_lines.size() >= 3)
                {
                    std::string c_abbrev;
                    std::string c_name;
                    std::string c_genitive;
                    allocate_cons_name(c_abbrev, c_name, c_genitive);

                    Constellation cons;
                    cons.abbrev = c_abbrev;
                    cons.name = c_name;
                    cons.genitive = c_genitive;
                    cons.vantage = vantage_pt;
                    cons.vantage_name = sys_name;
                    cons.vantage_resolved = true;
                    cons.lines = gap_lines;

                    for (const auto& gl : gap_line_records)
                    {
                        all_lines.push_back(gl);
                        placed_stars.push_back({gl.a, gl.ua, target_c_idx});
                        placed_stars.push_back({gl.b, gl.ub, target_c_idx});
                        lined_dirs.push_back(gl.ua);
                        lined_dirs.push_back(gl.ub);
                    }
                    for (Star* s : gap_c_stars)
                    {
                        all_connected_stars.insert(s);
                    }

                    cons_stars.push_back(gap_c_stars);
                    out_conss.push_back(cons);
                    coverage = calculate_sky_coverage(lined_dirs, 1000);
                }
            }
        }

        // 8. Cast net wider to aim for constellations about 15-20 degrees across
        auto get_cons_span = [&](const Constellation& c) -> double
        {
            double max_span = 0.0;
            std::vector<Point> u_stars;
            for (const auto& cl : c.lines)
            {
                if (cl.a && cl.b)
                {
                    auto it_a = star_u_map.find(cl.a);
                    auto it_b = star_u_map.find(cl.b);
                    if (it_a != star_u_map.end() && it_b != star_u_map.end())
                    {
                        u_stars.push_back(it_a->second);
                        u_stars.push_back(it_b->second);
                    }
                }
            }
            for (size_t a = 0; a < u_stars.size(); a++)
            {
                for (size_t b = a + 1; b < u_stars.size(); b++)
                {
                    double d = ang_dist_deg(u_stars[a], u_stars[b]);
                    if (d > max_span)
                    {
                        max_span = d;
                    }
                }
            }
            return max_span;
        };

        for (size_t out_i = 0; out_i < out_conss.size(); out_i++)
        {
            int c_idx = existing_cons_count + (int)out_i;
            int max_steps = 10;
            while (max_steps-- > 0)
            {
                double cur_span = get_cons_span(out_conss[out_i]);
                if (cur_span >= 15.0)
                {
                    break;
                }

                std::unordered_map<Star*, int> deg_map;
                for (const auto& cl : out_conss[out_i].lines)
                {
                    deg_map[cl.a]++;
                    deg_map[cl.b]++;
                }

                Star* best_cand = nullptr;
                Star* best_attach = nullptr;
                Point best_cand_u;
                Point best_attach_u;
                double best_new_span = cur_span;
                double best_len = 0.0;

                for (const auto& pair : deg_map)
                {
                    if (pair.second >= 3)
                    {
                        continue;
                    }
                    Star* sa = pair.first;
                    Point ua = star_u_map[sa];

                    cand_grid.for_each_near(ua, 15.0, [&](int ci)
                    {
                        Star* sb = candidates[ci].s;
                        if (all_connected_stars.count(sb) || candidates[ci].mag > 5.8)
                        {
                            return;
                        }
                        Point ub = candidates[ci].u;
                        double len = ang_dist_deg(ua, ub);
                        if (len > 15.0 || len < 1.0)
                        {
                            return;
                        }

                        double test_span = cur_span;
                        for (const auto& other_pair : deg_map)
                        {
                            test_span = std::max(test_span, ang_dist_deg(ub, star_u_map[other_pair.first]));
                        }

                        if (test_span > best_new_span && is_candidate_line_valid(sa, ua, sb, ub, len, c_idx))
                        {
                            best_new_span = test_span;
                            best_cand = sb;
                            best_attach = sa;
                            best_cand_u = ub;
                            best_attach_u = ua;
                            best_len = len;
                        }
                    });
                }

                if (best_cand && best_attach)
                {
                    ConsLine cl;
                    cl.a = best_attach;
                    cl.b = best_cand;
                    cl.starnamea = get_consline_star_name(best_attach);
                    cl.starnameb = get_consline_star_name(best_cand);
                    out_conss[out_i].lines.push_back(cl);

                    double cos_half = cos(0.5 * best_len * (_pi / 180.0));
                    all_lines.push_back({best_attach, best_cand, best_attach_u, best_cand_u, best_len, cos_half, c_idx});
                    placed_stars.push_back({best_cand, best_cand_u, c_idx});
                    cons_stars[c_idx].insert(best_cand);
                    all_connected_stars.insert(best_cand);
                    lined_dirs.push_back(best_cand_u);

                    deg_map[best_attach]++;
                    deg_map[best_cand]++;

                    // Attempt chord loop closure to another node in this constellation if within 14 deg
                    for (const auto& pair : deg_map)
                    {
                        Star* sc = pair.first;
                        if (sc == best_attach || sc == best_cand || pair.second >= 3)
                        {
                            continue;
                        }
                        Point uc = star_u_map[sc];
                        double d2 = ang_dist_deg(best_cand_u, uc);
                        if (d2 >= 1.0 && d2 <= 14.0 && is_candidate_line_valid(best_cand, best_cand_u, sc, uc, d2, c_idx))
                        {
                            ConsLine cl_loop;
                            cl_loop.a = best_cand;
                            cl_loop.b = sc;
                            cl_loop.starnamea = get_consline_star_name(best_cand);
                            cl_loop.starnameb = get_consline_star_name(sc);
                            out_conss[out_i].lines.push_back(cl_loop);

                            double cos_half2 = cos(0.5 * d2 * (_pi / 180.0));
                            all_lines.push_back({best_cand, sc, best_cand_u, uc, d2, cos_half2, c_idx});
                            break;
                        }
                    }
                }
                else
                {
                    break;
                }
            }
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

            int batch_size = (!::window) ? (int)pending_conss.size() : 3;
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
                completed_vantages.insert(current_vantage_name);
                generated_conss.clear();
            }
            return;
        }

        // Check if current system is due for generation
        CelestialObject* cur_obj = mycenobj ? mycenobj : (whereami >= 0 && cels ? cels[whereami] : nullptr);
        Star* sys_star = nullptr;
        if (cur_obj)
        {
            if (cur_obj->cenobj && cur_obj->cenobj->typeclass() == class_star)
            {
                sys_star = (Star*)cur_obj->cenobj;
            }
            else if (cur_obj->typeclass() == class_star)
            {
                sys_star = (Star*)cur_obj;
            }
        }

        std::string vname;
        if (sys_star && check_should_generate_for(sys_star, vname))
        {
            start_generation_for(sys_star);
        }
    }

    bool ExoConsGenerator::save_current_vantage()
    {
        CelestialObject* cur_obj = mycenobj ? mycenobj : (whereami >= 0 && cels ? cels[whereami] : nullptr);
        Star* sys_star = nullptr;
        if (cur_obj)
        {
            if (cur_obj->cenobj && cur_obj->cenobj->typeclass() == class_star)
            {
                sys_star = (Star*)cur_obj->cenobj;
            }
            else if (cur_obj->typeclass() == class_star)
            {
                sys_star = (Star*)cur_obj;
            }
        }

        if (!sys_star)
        {
            return false;
        }

        std::string vname = sys_star->name;
        if (vname.empty())
        {
            vname = get_consline_star_name(sys_star);
        }
        if (vname.empty())
        {
            return false;
        }

        std::vector<Constellation> conss_to_save;
        Point sys_loc = sys_star->location;
        for (const auto& c : constellations)
        {
            if (c.vantage_name == vname || (c.vantage_resolved && c.vantage.distance_to(sys_loc) < light_year * EXOCONS_SAME_VANTAGE_DIST_LY))
            {
                conss_to_save.push_back(c);
            }
        }

        if (conss_to_save.empty())
        {
            generate_all_synchronous(sys_star);
            for (const auto& c : constellations)
            {
                if (c.vantage_name == vname || (c.vantage_resolved && c.vantage.distance_to(sys_loc) < light_year * EXOCONS_SAME_VANTAGE_DIST_LY))
                {
                    conss_to_save.push_back(c);
                }
            }
        }

        if (conss_to_save.empty())
        {
            return false;
        }

        save_to_exocons_file(vname, conss_to_save);
        return true;
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
