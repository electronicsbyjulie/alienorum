#ifndef _ExoCons_H
#define _ExoCons_H

#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include "star.h"
#include "cons.h"

// =====================================================================
// ExoCons Generator Constants
// =====================================================================

// Catalog and Array Dimensions
#define EXOCONS_NUM_IAU_CONSTELLATIONS            88
#define EXOCONS_DEFAULT_MAG                       99.0
#define EXOCONS_DEFAULT_MIN_END_DIST_DEG          0.8
#define EXOCONS_DEFAULT_SKY_SAMPLES               1000
#define EXOCONS_SKY_COVERAGE_SAMPLES              1000
#define EXOCONS_MIN_SEPARATION_EPSILON            1e-9
#define EXOCONS_INF_SCORE                         1e9
#define EXOCONS_DOT_PARALLEL_MAX                  0.99999

// Proximity and Vantage Thresholds
#define EXOCONS_SAME_VANTAGE_DIST_LY              0.1
#define EXOCONS_NEARBY_SUPPRESS_DIST_LY           10.0

// Star Inclusion & Selection Criteria
#define EXOCONS_CANDIDATE_MAX_MAG                 8.0
#define EXOCONS_PASS1_MAX_MAG                     4.0
#define EXOCONS_PASS2_MAX_MAG                     5.0
#define EXOCONS_PASS2_NEARBY_DEG                  4.0
#define EXOCONS_PASS3_MIN_DIST_DEG                7.0
#define EXOCONS_PASS3_ISOLATION_DEG               2.0
#define EXOCONS_PASS3_MAX_MAG                     6.0
#define EXOCONS_PASS3_MAG_MARGIN                  0.01
#define EXOCONS_MANDATORY_JOIN_MAG                3.0
#define EXOCONS_GAP_FILL_MAX_MAG                  6.5
#define EXOCONS_EXPANSION_MAX_MAG                 6.0

// Line Scoring & Weight Factors
#define EXOCONS_MAX_LINE_LENGTH_DEG               15.0
#define EXOCONS_SCORE_MAG_WEIGHT_INTRA            0.5
#define EXOCONS_SCORE_MAG_WEIGHT_INTER            0.8
#define EXOCONS_SCORE_DM_WEIGHT                   1.2
#define EXOCONS_SCORE_CAND_MAG_WEIGHT             0.5
#define EXOCONS_MAX_STAR_DEGREE                   3

// Sky Coverage
#define EXOCONS_SKY_COVERAGE_THRESHOLD            0.80
#define EXOCONS_SKY_COVERAGE_TARGET_DIST_DEG      5.0
#define EXOCONS_SKY_GAP_MAX_ITERATIONS            200

// Constellation Geometry & Line Counts
#define EXOCONS_MIN_LINES_PER_CONS                3
#define EXOCONS_MIN_ACTIVE_CONS_BEFORE_MERGE      55
#define EXOCONS_MAX_EXPANSE_DEG                   35.0
#define EXOCONS_MAX_EXPANSION_ATTEMPTS            15

// Passerby & Impingement Detection
#define EXOCONS_MAX_IMPINGE_DIST_DEG              2.0
#define EXOCONS_IMPINGE_NEARBY_DEG                17.0
#define EXOCONS_IMPINGE_INTERSECT_CHECK_DEG       30.5
#define EXOCONS_IMPINGING_POOL_BRIGHT_MAG         4.0

namespace alienorum
{
    struct ExoConsStarInfo
    {
        Star* s = nullptr;
        double mag = EXOCONS_DEFAULT_MAG;
        Point u;
        std::string orig_cons;
    };

    struct IAUConstellationDef
    {
        const char* abbrev;
        const char* name;
        const char* genitive;
    };

    extern const IAUConstellationDef iau_constellations[EXOCONS_NUM_IAU_CONSTELLATIONS];

    class ExoConsGenerator
    {
        public:
        static bool to_be_generated(Star* sys_star, std::string& vantage_name_out);
        static void generate_constellations(Star* sys_star, std::vector<Constellation>& out_conss);
        static void start_generation_for(Star* sys_star);
        static void update_frame();
        static void generate_all_synchronous(Star* sys_star);
        static void save_to_exocons_file(const std::string& vantage_name, const std::vector<Constellation>& conss);
        static double calculate_sky_coverage(const std::vector<Point>& lined_star_dirs, int num_samples = EXOCONS_DEFAULT_SKY_SAMPLES);
        static bool arcs_intersect(const Point& a, const Point& b, const Point& c, const Point& d);
        static bool point_near_arc(const Point& a, const Point& b, const Point& p, double max_dist_deg, double* dist_out = nullptr, double min_end_dist_deg = EXOCONS_DEFAULT_MIN_END_DIST_DEG);
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
