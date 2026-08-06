
#ifndef _Star
#define _Star

#include <cstdint>
#include <math.h>
#include "celestial.h"

namespace alienorum
{
    class StarMulti;

    class Star : public CelestialObject
    {
        public:
        double proper_motion_RA = 0;                // radians / second
        double proper_motion_decl = 0;              // radians / second
        double radial_velocity = 0;                 // meters / second
        double apparent_magnitude;                  // visual/550nm
        double parallax = 0;                        // radians

        char spectral_type[32];
        char Bayer[32];
        char Flamsteed[32];
        char Gliese[16];
        int BayerGrkno = -1;
        int FlamsteedNo = -1;
        int GouldNo = -1;
        std::string alienorumid = "";
        char constellation[4] = {0,0,0,0};
        std::string CCDM, WD;
        char ccdm_compseq = 0;
        StarMulti* multisys = nullptr;
        char get_component();
        void set_component(char comp, Star* compA);

        uint32_t HR = 0;                            // Harvard Revised catalog number
        uint32_t HD = 0;                            // Henry Draper catalog number
        uint32_t HIP = 0;                           // Hipparcos catalog number
        uint32_t SAO = 0;                           // USNO/SAO catalog number
        uint32_t SB9 = 0;                           // 9th Catalogue of Spectroscopic Binary Orbits designation
        char Bonn_survey[3] = {0,0,0};              // BD = Bonn, CD = Cordoba, CP = Cape Town
        char Bonn_survey_sign = '+';
        int Bonn_survey_declination = 0;            // Declination category
        uint32_t Bonn_survey_sequential = 0;        // Serial number by right ascension.

        bool is_orbit_multiple = false;
        bool has_custom_name = false;
        int has_planets = 0;
        int has_hz_planets = 0;
        bool tmp_vis_flag;                          // Used only for rendering.
        bool has_disk = false;                      // E.g. dust, debris, cometary, asteroid belt, etc.
        bool rot_axis_known = false;
        bool has_hot_jupiter = false;
        double disk_heliocen_inclination = 0, disk_heliocen_node = 0;
        double disk_inner_edge_sma = 0;             // meters; e.g. a debris/Kuiper-analog belt's inner edge, when known
        double planets_heliocen_inclination = 0, planets_heliocen_node = 0;
        double rot_heliocen_incl = 0, rot_heliocen_node = 0;
        double m_bol = 0;

        Star();
        ~Star();

        void update_location(double tmnow);         // Apply proper motion and re-derive 3D coordinates from the result.
        void rename_from_Bayer_Flamsteed();
        bool matches_constellation(const char* search_cons);
        bool is_sunlike();
        bool is_in_visible_box(Point seen_from);
        bool is_really_truly_in_visible_box(Point seen_from);
        void make_universally_visible();
        inline bool is_universally_visible() { return _is_always_visible; }

        double estimate_temperature();              // Based on MK spectral type code
        double estimate_luminosity(double tempK);   // Based on radius and supplied temperature. Returns output scaled to absolute magnitude zero.
        double estimate_mass();
        void estimate_BV();                         // Blackbody value from estimated temperature from MK spectral type
        void estimate_UB();                         // "
        void estimate_BV(double tempK);             // Blackbody value from known temperature
        void estimate_UB(double tempK);             // Blackbody value from known temperature

        static void load_main_seq_dat();

        static double get_mseqidx_from_sptyp(const char* sptyp);
        static double get_mseqidx_from_mass(double mass);
        static double get_mseqidx_from_rad(double rad);
        static double get_mseqidx_from_lum(double lum);
        static double get_mseqidx_from_temp(double tempK);
        static double get_mseqidx_from_BV(double BV);

        static double interpolate_mseq_mass(double mseqidx);
        static double interpolate_mseq_rad(double mseqidx);
        static double interpolate_mseq_lum(double mseqidx);
        static double interpolate_mseq_temp(double mseqidx);
        static double interpolate_mseq_BV(double mseqidx);

        double estimate_radius();

        // Inverse corps noir de estimate_BV(double T), et relation masse-rayon degeneree. Toutes
        // deux deliberement independantes de la table de sequence principale, qui attribuerait a
        // une naine blanche de B-V voisin de 0 le rayon d'une A0 V -- deux ordres de grandeur de
        // trop. Voir STELLAR_TEXTURE_PLAN.md §5.
        static double temperature_from_BV(double BV);
        static double degenerate_radius(double mass_kg);            // metres

        void gotta_be_named_something();
        json to_json();
        bool from_json(json j);
        void make_companion_of(Star* primary, char comp = 'B');

    protected:
        Box visible_area;
        bool visible_area_set = false;
        bool _is_in_visible_range = true;
        bool _is_always_visible = false;            // for Sun and constellation line termini
    };

    // Regime physique d'un corps auto-lumineux, tel qu'employe pour aiguiller la generation de
    // texture (STELLAR_TEXTURE_PLAN.md §6.3). Le type declare de l'objet ne suffit pas : une naine
    // brune venue d'un catalogue est chargee comme une Star, la meme ecrite a la main dans un
    // systeme fictif serait un gas_giant. Un aiguillage sur cel->type se tromperait donc une fois
    // sur deux ; celui-ci se fait sur la photometrie.
    enum stellar_regime_t
    {
        regime_none = 0,            // rien a engendrer par cette voie
        regime_degenerate,          // naine blanche
        regime_substellar,          // naine brune
        regime_stellar              // sequence principale, sous-geantes, geantes, supergeantes
    };

    stellar_regime_t stellar_regime(CelestialObject *cel);

    class StarMulti
    {
        public:
        ~StarMulti();
        void add_member(Star* s, char comp);
        Star* get_member(char comp);
        char is_member(Star* s);
        int num_members();
        char next_available();
        void unlink();                              // Call this before deleting object and before deleting any stars.
        char get_allocated();
        void merge(StarMulti *other);

        protected:
        Star** members = nullptr;
        char allocated = 0;
    };
}

void rename_all_from_Bayer_Flamsteed();
void Gliese_doubles_fix();

#define mseqmin  3
#define mseqmax 70
extern double msq_mass[mseqmax], msq_rad[mseqmax], msq_lum[mseqmax], msq_temp[mseqmax], msq_BV[mseqmax];
extern alienorum::Star **hdcache, **hipcache;

#endif
