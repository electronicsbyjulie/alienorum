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

        std::unordered_map<std::string, int> vantage_counts;
        std::unordered_map<std::string, Point> vantage_locations;
        int own_count = 0;

        for (const auto& c : constellations)
        {
            std::string vname = c.vantage_name;
            Point vpt = c.vantage;
            if (vname.empty() && std::isnan(vpt.x))
            {
                vname = "Sun";
                vpt = Point(0, 0, 0);
            }
            else if (vname.empty())
            {
                if (vpt.distance_to(Point(0, 0, 0)) < light_year * 0.1)
                {
                    vname = "Sun";
                }
            }
            else
            {
                int sidx = find_object(vname.c_str(), true);
                if (sidx >= 0 && cels[sidx])
                {
                    vpt = cels[sidx]->location;
                }
            }

            bool is_same_vantage = (!c.vantage_name.empty() && c.vantage_name == vantage_name_out);
            if (c.vantage_name.empty() && (vantage_name_out == "Sun" || vantage_name_out == "Sol"))
            {
                is_same_vantage = true;
            }
            if ((!std::isnan(vpt.x) && vpt.distance_to(sys_loc) < light_year * 0.1) || is_same_vantage)
            {
                own_count++;
            }

            if (!vname.empty())
            {
                vantage_counts[vname]++;
                vantage_locations[vname] = vpt;
            }
        }

        if (own_count >= 30)
        {
            return false;
        }

        for (const auto& pair : vantage_counts)
        {
            if (pair.second >= 30)
            {
                Point vloc = vantage_locations[pair.first];
                double dist = vloc.distance_to(sys_loc);
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
            if (included_set.count(c.s) || existing_lined_stars.count(c.s) || c.mag > 6.5)
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
                    double score = d_deg + 0.5 * (u1.mag + u2.mag);
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
                    double score = d_deg + 0.8 * (u1.mag + u2.mag);
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

        // 5. Construct final Constellation objects grouped by IAU constellations
        std::unordered_map<std::string, std::vector<std::pair<Star*, Star*>>> lines_by_cons;
        for (const auto& l : all_lines)
        {
            std::string c_abbrev = l.first.orig_cons;
            if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
            {
                c_abbrev = l.second.orig_cons;
            }
            if (c_abbrev.empty() || existing_cons_abbrevs.count(c_abbrev))
            {
                for (int i = 0; i < 88; i++)
                {
                    std::string cand_abbr = iau_constellations[i].abbrev;
                    if (!existing_cons_abbrevs.count(cand_abbr))
                    {
                        c_abbrev = cand_abbr;
                        break;
                    }
                }
            }
            lines_by_cons[c_abbrev].push_back({l.first.s, l.second.s});
        }

        std::unordered_set<std::string> used_abbrevs = existing_cons_abbrevs;
        for (const auto& pair : lines_by_cons)
        {
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

        // Check if current system needs generation
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
