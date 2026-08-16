
#ifndef _Comet
#define _Comet

#include "celestial.h"

namespace alienorum
{
    // A comet: a dirty snowball on an orbit that is usually far too eccentric to close, and which
    // is seen by the coma it grows rather than by the few kilometers of ice at the middle of it.
    // That is why it does not share Planet's photometry. A planet's brightness follows its disc,
    // so it fades with the square of its distance from the Sun and no faster; a comet's follows
    // how hard the Sun is boiling it, which runs far steeper -- typically as the fourth power of
    // the distance and sometimes as the sixth -- and is why a comet can be invisible at Jupiter's
    // distance and outshine Venus a few months later.
    class Comet : public CelestialObject
    {
        public:
        std::string designation;                        // IAU code: "1P", "C/1995 O1".

        // m = H1 + R1 log10(r) + D1 log10(D), r being the distance to the Sun and D the distance
        // to the viewer, both in AU: the standard cometary light curve, and the form the IMCCE
        // catalog states its parameters in. R1 is the steep one -- 10 means brightness going as
        // r^-4 -- and holds the whole difference between a comet and a rock. The second set
        // describes the bare nucleus, for a comet too far out to have grown a coma at all.
        double H1 = 0, R1 = 0, D1 = 0;
        double H2 = 0, R2 = 0, D2 = 0;

        // The three constants actually in play once the fallbacks above have been applied. Kept
        // apart from the magnitude itself because the drawing code sizes the coma and the tail
        // from the same numbers the photometry runs on, and the two must not drift apart: a comet
        // that the light curve says is bright and the picture says is a bare dot is worse than
        // either mistake on its own.
        void light_curve_parameters(double &h, double &slope_r, double &slope_d) const;

        double viewer_comet_magnitude(CelestialLocation seen_from);
        void update_location(double tmnow);

        Comet();
        ~Comet() { if (orbit) delete orbit; }

        json to_json();
        bool from_json(json j);
    };

    // One line of the comet catalog, held for every comet in it so the picker can list them all
    // without any of them costing a cels[] slot until asked for. Mirrors AstorbRow.
    struct CometRow
    {
        std::string code, name;
        float q = 0, e = 0, incl = 0;
        double T_peri = 0;                              // JD
        Comet *cel = nullptr;                           // Null until loaded.
    };
}

extern std::vector<alienorum::CometRow> comets;

#endif
