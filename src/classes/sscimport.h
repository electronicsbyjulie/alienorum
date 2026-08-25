#ifndef _SSCImport
#define _SSCImport

#include "serial.h"

namespace alienorum
{
    // Importing another package's add-on is a lossy business -- the two programs describe a world
    // in different terms, and an .ssc file states things we have no room for (mesh files, haze
    // colors) while leaving out things this program cannot do without (mass, surface pressure). So
    // the importer keeps a running commentary of every judgement call it made, and the caller shows
    // it: nothing is quietly dropped or quietly invented.
    struct SSCImportReport
    {
        std::string source;
        int bodies_added = 0;
        int bodies_modified = 0;
        int bodies_skipped = 0;
        int textures_written = 0;
        int bumps_built = 0;
        std::vector<std::string> notes;
        bool ok = false;

        void note(const std::string &s) { notes.push_back(s); }
    };

    // One object definition as it stood in the file: the disposition word, the object's name (which
    // may carry ':'-separated aliases), the '/'-separated path of its parent, and everything inside
    // its braces as a json tree.
    struct SSCBlock
    {
        std::string disposition = "Add";
        std::string name;
        std::string parent;
        json fields = json::object();
        double mass_hint_earths = 0;                // from a "# Mass=... Earths" comment
        double density_hint = 0;                    // g/cm^3, from the same comment
    };

    // Reads one .ssc add-on file -- the native scene format of another astronomy package -- and
    // adds what it describes to cels[]. Textures named by the file are copied (or decoded, or
    // composited) into maps/ under the names load_textures() looks for.
    //
    // The object survives its read(), so the caller can show the report afterwards and so that
    // overwrite_maps stays set between imports.
    class SSCImport
    {
        public:
        SSCImportReport report;

        // False by default, and deliberately so: the maps folder holds hand-made textures that an
        // import must never replace behind the user's back. With this off, a map file that already
        // exists is left alone and said so in the report.
        bool overwrite_maps = false;

        // Returns false only when the file could not be read or parsed at all; a file that yielded
        // some bodies and some complaints returns true with the complaints in the report.
        //
        // Rebuilding the indices afterwards is the caller's job -- set_center_objects() and
        // refresh_star_visibilities() both live a layer above this one -- and it has to be done
        // before the new objects are drawn.
        bool read(const std::string &ssc_path);

        private:
        std::map<std::string, CelestialObject*> star_cache;

        static bool copy_file(const std::string &from, const std::string &to);
        static std::string first_alias(const std::string &ssc_name);
        static void update_body_location(CelestialObject *cel);
        static cel_obj_class class_from_ssc(const json &fields, const CelestialObject *parent);
        static void apply_rotation(const json &fields, CelestialObject *cel);

        bool may_write_map(const std::string &dest);
        bool install_plain_texture(const std::string &src, CelestialObject *cel, const char *suffix);
        bool install_composited_surface(const std::string &surf_src, const std::string &cloud_src,
            CelestialObject *cel);
        bool install_ring_textures(const std::string &src, Planet *pl, double inner_m, double outer_m);
        bool install_bump_from_normal_map(const std::string &src, CelestialObject *cel);
        CelestialObject* resolve_star(const std::string &ssc_name);
        void apply_orbit(const json &orb, CelestialObject *cel, CelestialObject *parent);
        void establish_mass(Planet *pl, const json &fields, const SSCBlock &blk);
    };
}

// The last import this session, and whether its report window is up. Both live outside the class
// because they are the user interface's business rather than the importer's.
extern alienorum::SSCImport last_ssc_import;
extern bool ssc_report_shown;

void draw_ssc_import_window(ImGuiIO &io);       // A no-op while ssc_report_shown is false.

#endif
