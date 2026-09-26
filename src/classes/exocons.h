#ifndef _ExoCons_H
#define _ExoCons_H

#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include "star.h"
#include "cons.h"

namespace alienorum
{
    struct ExoConsStarInfo
    {
        Star* s = nullptr;
        double mag = 99.0;
        Point u;
        std::string orig_cons;
    };

    struct IAUConstellationDef
    {
        const char* abbrev;
        const char* name;
        const char* genitive;
    };

    extern const IAUConstellationDef iau_constellations[88];

    class ExoConsGenerator
    {
        public:
        static bool to_be_generated(Star* sys_star, std::string& vantage_name_out);
        static void generate_constellations(Star* sys_star, std::vector<Constellation>& out_conss);
        static void start_generation_for(Star* sys_star);
        static void update_frame();
        static void generate_all_synchronous(Star* sys_star);
        static void save_to_exocons_file(const std::string& vantage_name, const std::vector<Constellation>& conss);
        static double calculate_sky_coverage(const std::vector<Point>& lined_star_dirs, int num_samples = 1000);
        static bool arcs_intersect(const Point& a, const Point& b, const Point& c, const Point& d);
        static bool point_near_arc(const Point& a, const Point& b, const Point& p, double max_dist_deg, double* dist_out = nullptr, double min_end_dist_deg = 0.8);
        static std::string get_consline_star_name(Star* s);
        static const IAUConstellationDef* find_iau_def(const std::string& abbrev);
        static void reset();
        static bool get_is_generating() { return is_generating; }

        private:
        static std::vector<Constellation> pending_conss;
        static std::vector<Constellation> generated_conss;
        static Star* current_sys_star;
        static std::string current_vantage_name;
        static bool is_generating;
        static std::unordered_set<std::string> completed_vantages;
    };
}

#endif
