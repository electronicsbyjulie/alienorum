
#include "loaders.h"
#include "housekeeping.h"
#include "classes/cons.h"
#include <cstdlib>
#include <unordered_map>
#include <string_view>

using namespace alienorum;

int sats_added = 0, sat_errors = 0;
std::atomic<bool> batch_sats_running{false};

namespace
{
    struct TextureLoadTicket
    {
        ~TextureLoadTicket() { texture_loads_pending--; }
    };

    struct BatchSatTicket
    {
        ~BatchSatTicket() { batch_sats_running = false; }
    };
}

void spawn_texture_load(CelestialObject *cel)
{
    if (!cel || cel->looked_for_maps) return;
    cel->looked_for_maps = true;            // Prevent spawning infinite threads and crashing the system.
    texture_loads_pending++;
    std::thread ttex(load_textures, cel);
    ttex.detach();
}

void load_textures(CelestialObject* cel)
{
    TextureLoadTicket ticket;
    std::string filename;

    if (!cel->ignore_map_files)                 // For regenerating exoplanet textures.
    {
        std::string cloud_jpg = (std::string)"maps" + _FSSTR + (std::string)cel->name + "_clouds.jpg";
        std::string cloud_png = (std::string)"maps" + _FSSTR + (std::string)cel->name + "_clouds.png";
        bool prefer_png = false;

        cel_obj_class ccls = cel->typeclass();
        if (ccls == class_planet || ccls == class_moon)
        {
            Planet *p = (Planet*)cel;
            if (p->cloud_map_url.size())
            {
                prefer_png = (p->cloud_map_url.size() >= 4 && !strcasecmp(p->cloud_map_url.substr(p->cloud_map_url.size() - 4).c_str(), ".png"));
                check_and_download_clouds(p->cloud_map_url, prefer_png ? cloud_png : cloud_jpg);
            }
        }

        if (prefer_png)
        {
            if (file_exists(cloud_png.c_str()))
            {
                Map *map = new Map(cel);
                if (map->load_from_png(cloud_png) && map->is_complete())
                {
                    if (cel->cloud_map && cel->cloud_map != map)
                    {
                        delete cel->cloud_map;
                    }
                    cel->cloud_map = map;
                    cel->has_real_maps = true;
                }
                else
                {
                    delete map;
                    std::remove(cloud_png.c_str());
                    std::string ts = (std::string)"maps" + _FSSTR + (std::string)cel->name + "_clouds.timestamp";
                    std::remove(ts.c_str());
                }
            }
            else if (file_exists(cloud_jpg.c_str()))
            {
                Map *map = new Map(cel);
                if (map->load_from_jpeg(cloud_jpg) && map->is_complete())
                {
                    if (cel->cloud_map && cel->cloud_map != map)
                    {
                        delete cel->cloud_map;
                    }
                    cel->cloud_map = map;
                    cel->has_real_maps = true;
                }
                else
                {
                    delete map;
                    std::remove(cloud_jpg.c_str());
                    std::string ts = (std::string)"maps" + _FSSTR + (std::string)cel->name + "_clouds.timestamp";
                    std::remove(ts.c_str());
                }
            }
        }
        else
        {
            if (file_exists(cloud_jpg.c_str()))
            {
                Map *map = new Map(cel);
                if (map->load_from_jpeg(cloud_jpg) && map->is_complete())
                {
                    if (cel->cloud_map && cel->cloud_map != map)
                    {
                        delete cel->cloud_map;
                    }
                    cel->cloud_map = map;
                    cel->has_real_maps = true;
                }
                else
                {
                    delete map;
                    std::remove(cloud_jpg.c_str());
                    std::string ts = (std::string)"maps" + _FSSTR + (std::string)cel->name + "_clouds.timestamp";
                    std::remove(ts.c_str());
                }
            }
            else if (file_exists(cloud_png.c_str()))
            {
                Map *map = new Map(cel);
                if (map->load_from_png(cloud_png) && map->is_complete())
                {
                    if (cel->cloud_map && cel->cloud_map != map)
                    {
                        delete cel->cloud_map;
                    }
                    cel->cloud_map = map;
                    cel->has_real_maps = true;
                }
                else
                {
                    delete map;
                    std::remove(cloud_png.c_str());
                    std::string ts = (std::string)"maps" + _FSSTR + (std::string)cel->name + "_clouds.timestamp";
                    std::remove(ts.c_str());
                }
            }
        }

        filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_surf.jpg";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map(cel);
            if (map->load_from_jpeg(filename))
            {
                if (cel->surf_map && cel->surf_map != map)
                {
                    delete cel->surf_map;
                }
                cel->surf_map = map;
                cel->has_real_maps = true;
            }
            else
            {
                delete map;
            }
        }
        else
        {
            filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_surf.png";
            if (file_exists(filename.c_str()))
            {
                Map *map = new Map(cel);
                if (map->load_from_png(filename))
                {
                    if (cel->surf_map && cel->surf_map != map)
                    {
                        delete cel->surf_map;
                    }
                    cel->surf_map = map;
                    cel->has_real_maps = true;
                }
                else
                {
                    delete map;
                }
            }
        }

        if (cel->surf_map)
        {
            cel_obj_class cls = cel->typeclass();
            if (cls == class_planet || cls == class_moon)
            {
                Planet *p = (Planet*)cel;
                filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_bump.jpg";
                if (file_exists(filename.c_str()))
                {
                    cel->surf_map->load_from_jpeg(filename, true, p->estimate_bump_scale());
                    cel->has_real_maps = true;
                }
                else
                {
                    filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_bump.png";
                    if (file_exists(filename.c_str()))
                    {
                        cel->surf_map->load_from_png(filename, true, p->estimate_bump_scale());
                        cel->has_real_maps = true;
                    }
                }
            }
        }

        filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_night.jpg";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map();
            if (map->load_from_jpeg(filename))
            {
                if (cel->night_map && cel->night_map != map)
                {
                    delete cel->night_map;
                }
                cel->night_map = map;
                cel->has_real_maps = true;
            }
            else
            {
                delete map;
            }
        }
        else
        {
            filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_night.png";
            if (file_exists(filename.c_str()))
            {
                Map *map = new Map();
                if (map->load_from_png(filename))
                {
                    if (cel->night_map && cel->night_map != map)
                    {
                        delete cel->night_map;
                    }
                    cel->night_map = map;
                    cel->has_real_maps = true;
                }
                else
                {
                    delete map;
                }
            }
        }

        cel_obj_class cls = cel->typeclass();

        if (cls == class_planet)
        {
            Planet *p = (Planet*)cel;
            filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_ring.jpg";
            if (file_exists(filename.c_str()))
            {
                if (!p->ring_radius)
                {
                    p->generate_ring_parameters(true);
                }
                Map *map = new Map();
                if (map->load_from_jpeg(filename))
                {
                    if (cel->ring_map && cel->ring_map != map)
                    {
                        delete cel->ring_map;
                    }
                    cel->ring_map = map;
                    cel->has_real_maps = true;
                }
                else
                {
                    delete map;
                }
            }
            else
            {
                filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_ring.png";
                if (file_exists(filename.c_str()))
                {
                    if (!p->ring_radius)
                    {
                        p->generate_ring_parameters(true);
                    }
                    Map *map = new Map();
                    if (map->load_from_png(filename))
                    {
                        if (cel->ring_map && cel->ring_map != map)
                        {
                            delete cel->ring_map;
                        }
                        cel->ring_map = map;
                        cel->has_real_maps = true;
                    }
                    else
                    {
                        delete map;
                    }
                }
            }

            filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_ringx.jpg";
            if (file_exists(filename.c_str()))
            {
                if (!p->ring_radius)
                {
                    p->generate_ring_parameters(true);
                }
                Map *map = new Map();
                if (map->load_from_jpeg(filename))
                {
                    if (cel->ringx_map && cel->ringx_map != map)
                    {
                        delete cel->ringx_map;
                    }
                    cel->ringx_map = map;
                    cel->has_real_maps = true;
                }
                else
                {
                    delete map;
                }
            }
            else
            {
                filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_ringx.png";
                if (file_exists(filename.c_str()))
                {
                    if (!p->ring_radius)
                    {
                        p->generate_ring_parameters(true);
                    }
                    Map *map = new Map();
                    if (map->load_from_png(filename))
                    {
                        if (cel->ringx_map && cel->ringx_map != map)
                        {
                            delete cel->ringx_map;
                        }
                        cel->ringx_map = map;
                        cel->has_real_maps = true;
                    }
                    else
                    {
                        delete map;
                    }
                }
            }
        }
    }

    cel->looked_for_maps = true;
    cel->ignore_map_files = false;          // one-time use.

    if (uses_gaseous_map(cel->type) && !cel->cloud_map)
    {
        cel->cloud_map = new Map(cel);
        cel->cloud_map->generate_gas_giant_map(cel);
    }
    else if (uses_rocky_map(cel->type) && !cel->surf_map)
    {
        cel->surf_map = new Map(cel);
        cel->surf_map->generate_rocky_map(cel);
        if (cel->type == lavaworld && !cel->night_map)
        {
            cel->night_map = new Map(cel);
            cel->night_map->generate_lava_map(cel);
        }
    }
    else if (!cel->surf_map && !cel->cloud_map)
    {
        switch (stellar_regime(cel))
        {
            case regime_degenerate:
            case regime_stellar:
                cel->surf_map = new Map(cel);
                cel->surf_map->generate_stellar_map(cel);
                break;

            case regime_substellar:
                break;

            default:
                break;
        }
    }
}

void save_textures(CelestialObject* cel)
{
    std::string mapfname;
    if (cel->surf_map)
    {
        mapfname = std::string("maps") + _FSSTR + std::string(cel->name) + std::string("_surf.png");
        cel->surf_map->save_to_png(mapfname);
        if (cel->surf_map->has_bump_data())
        {
            mapfname = std::string("maps") + _FSSTR + std::string(cel->name) + std::string("_bump.png");
            cel->surf_map->save_to_png(mapfname, true);
        }
    }
    if (cel->cloud_map)
    {
        mapfname = std::string("maps") + _FSSTR + std::string(cel->name) + std::string("_clouds.png");
        cel->cloud_map->save_to_png(mapfname);
    }
    if (cel->night_map)
    {
        mapfname = std::string("maps") + _FSSTR + std::string(cel->name) + std::string("_night.png");
        cel->night_map->save_to_png(mapfname);
    }
    if (cel->ring_map)
    {
        mapfname = std::string("maps") + _FSSTR + std::string(cel->name) + std::string("_ring.png");
        cel->ring_map->save_to_png(mapfname);
    }
    if (cel->ringx_map)
    {
        mapfname = std::string("maps") + _FSSTR + std::string(cel->name) + std::string("_ringx.png");
        cel->ringx_map->save_to_png(mapfname);
    }
}

bool establish_project_root()
{
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;

    if (const char *home = std::getenv("ALIENORUM_HOME")) candidates.push_back(home);
    if (char *base = SDL_GetBasePath())
    {
        candidates.push_back(base);
        SDL_free(base);
    }
    candidates.push_back(fs::current_path());

    for (const fs::path &start : candidates)
    {
        std::error_code ec;
        fs::path dir = fs::weakly_canonical(start, ec);
        if (ec) continue;

        while (true)
        {
            if (fs::exists(dir / p, ec))
            {
                fs::current_path(dir, ec);
                return !ec;
            }
            if (dir == dir.parent_path()) break;
            dir = dir.parent_path();
        }
    }

    return false;
}

bool look_for_catalogs()
{
    catalogs_found = establish_project_root();

    radio_silence = radio_silence || !catalogs_found || std::filesystem::exists("nonet");

    if (!catalogs_found)
        std::cerr << "No star catalogs found. Ensure the catalogs folder exists, contains data, and that the files are readable." << endl;

    return catalogs_found;
}

bool save_universe()
{
    fstream fs;
    fs.open("universe.json", std::ios::out);
    if (fs)
    {
        if (!Serialization::save_all(fs, cels, true)) std::cerr << "FAILED to save universe file." << std::endl;
        fs.close();
        return true;
    }
    else std::cerr << "FAILED to write universe file." << std::endl;
    return false;
}

bool load_universe(std::string universe_fname)          // default is on the declaration, in loaders.h
{
    int i;
    fstream fs;
    fs.open(universe_fname.c_str(), std::ios::in);
    if (fs)
    {
        mtx.lock();
        loading_msg = "Loading Universe file...";
        mtx.unlock();
        if (Serialization::load_all(fs, cels, MAX_CELOBJS))
        {
            fs.close();
            for (i=0; cels[i]; i++) if (!strcmp(cels[i]->name, "Earth"))
            {
                whereami = iamhome = i;
                mycenobj = cels[i]->cenobj;
            }
            ncelobjs = i;
            refresh_star_visibilities();

            std::filesystem::file_time_type ftime_json = std::filesystem::last_write_time("universe.json");
            std::filesystem::file_time_type ftime_cat = std::filesystem::last_write_time("catalogs" _FILESLASH "star_orbits.dat");
            bool resave_json = false;
            if (ftime_cat > ftime_json)
            {
                CatalogReader cr;
                cr.read_star_orbits_dat(cels);
                resave_json = true;
            }
            if (resave_json) save_universe();           // We deliberately write back to universe.json, not to the loaded file. This is by design.
            set_center_objects();
            refresh_star_visibilities();
            link_astorb_with_cels();

            return true;
        }
        else
        {
            fs.close();
            return false;
        }
    }
    else return false;
}

// Shutdown checkpoint for the loading thread. The catalog readers below each run for anywhere
// from milliseconds to several seconds, so a quit request is honoured between them rather than
// part-way through one: abort latency is one catalog read, which is bounded and leaves `cels` in
// a consistent state for main() to tear down. This replaces the previous arrangement, in which
// the Escape handler nulled `cels` out from under this thread on purpose so that it would fault.
static bool load_aborted()
{
    if (!abort_load) return false;
    mtx.lock();
    loading_msg = "Stopping...";
    mtx.unlock();
    return true;
}

void load_catalogs()
{
    int i, j, m, n;
    time_t began = time(NULL);
    bool have_astjson = false;

    cels[0] = nullptr;

    CatalogReader cr;
    if (load_aborted()) return;
    std::string ihcfn = cr.get_condensed_starcat_name();
    bool ihsc = false;
    if (!file_exists(ihcfn.c_str()))
    {
        std::string ihscgz = ihcfn + std::string(".gz");

        if (file_exists(ihscgz.c_str()))
        {
            extract_archive(ihscgz.c_str());
        }
    }
    if (file_exists(ihcfn.c_str()))
    {
        mtx.lock();
        loading_msg = std::string("Loading star catalog...");
        mtx.unlock();
        ihsc = cr.read_condensed_star_cat();
    }

    // TODO: Read data from more star catalogs.
    if (load_aborted()) return;
    cr.download_catalogs(ihsc);
    std::vector<std::string> cats = cr.find_catalogs("catalogs");

    n = cats.size();
    for (i=0; i<n; i++)
    {
        cout << "Found " << cats[i] << endl;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "Gliese")) have_Gliese = true;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "BSC")) have_BSC = true;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "Hipparcos")) have_HIP = true;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "Uranometria")) have_Uranio = true;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "WD")) have_WD = true;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "CCDM")) have_CCDM = true;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "SB9")) have_SB9 = true;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "astorb")) have_astorb = true;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "comets")) have_comets = true;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "RC3")) have_RC3 = true;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "UNGC")) have_UNGC = true;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "GCVS")) have_GCVS = true;
    }

    if (load_aborted()) return;
    if (have_Gliese && !ihsc)
    {
        mtx.lock();
        loading_msg = std::string("Loading Gliese catalog...");
        mtx.unlock();
        cout << "Reading Gliese catalog..." << endl << flush;
        int nGliese = cr.read_Gliese_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nGliese << " objects." << endl << flush;
    }

    if (load_aborted()) return;
    mtx.lock();
    loading_msg = std::string("Loading solar system...");
    mtx.unlock();

    int npl = 0;
    cout << "Reading local planets..." << endl << flush;
    npl += cr.read_local_planets(cels, MAX_CELOBJS, cels[0]);
    num_planets += npl;
    for (i=0; cels[i]; i++) if (!strcmp(cels[i]->name, "Earth")) whereami = iamhome = i;
    cout << "Read " << npl << " objects." << endl << flush;

    std::string astjson = std::string("catalogs") + _FILESLASH + std::string("asteroids.json");
    have_astjson = file_exists(astjson.c_str());

    if (have_astjson)
    {
        fstream fs(astjson.c_str(), std::ios::in);
        Serialization::load_all(fs, cels, MAX_CELOBJS, false);
        fs.close();
    }
    else
    {
        if (load_aborted()) return;
        int nastorb = 0;
        if (have_astorb)
        {
            cout << "Reading astorb catalog..." << endl << flush;
            nastorb = cr.read_astorb_catalog(cels, MAX_CELOBJS);
            cout << "Read " << nastorb << " objects." << endl << flush;
            astorb_loaded.store(true);
            astorb_rows_loaded.store(astorb.size());
            astorb_load_progress.store(1.0f);
        }

        json asts;
        for (i=0; cels[i]; i++)
        {
            cel_obj_class cls = cels[i]->typeclass();
            if (cls != class_planet && cls != class_moon) continue;

            std::string key = std::string(cels[i]->name);
            const char* l = key.c_str();

            if (cls == class_planet) asts[l] = ((Planet*)cels[i])->to_json();
            if (cls == class_moon  ) asts[l] = ((Moon*  )cels[i])->to_json();
        }

        fstream fs(astjson.c_str(), std::ios::out);
        fs << asts.dump(4);
    }

    if (load_aborted()) return;
    if (have_comets)
    {
        cout << "Reading comet catalog..." << endl << flush;
        int ncomets = cr.read_comets_catalog(cels, MAX_CELOBJS);
        num_comets += ncomets;
        cout << "Read " << ncomets << " objects, " << comets.size() << " catalogued." << endl << flush;
    }

    if (load_aborted()) return;
    cout << "Reading local moons..." << endl << flush;
    npl = cr.read_local_planets(cels, MAX_CELOBJS, nullptr, cels[0]);
    num_planets += npl;
    for (i=0; cels[i]; i++) if (!strcmp(cels[i]->name, "Earth")) whereami = iamhome = i;
    cout << "Read " << npl << " objects." << endl << flush;

    if (load_aborted()) return;
    if (have_BSC && !ihsc)
    {
        mtx.lock();
        loading_msg = std::string("Loading Bright Star Catalog...");
        mtx.unlock();
        cout << "Reading Bright Star Catalog..." << endl << flush;
        int nBSC = cr.read_BrightStars_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nBSC << " objects." << endl << flush;
    }
    Gliese_doubles_fix();
    if (load_aborted()) return;
    if (have_HIP && !ihsc && !magnitude_test)
    {
        mtx.lock();
        loading_msg = std::string("Loading Hipparcos Catalog...");
        mtx.unlock();
        cout << "Reading Hipparcos catalog..." << endl << flush;
        int nHIP = cr.read_Hipparcos_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nHIP << " objects." << endl << flush;
        Gliese_doubles_fix();
    }
    if (load_aborted()) return;
    if (have_GCVS)
    {
        mtx.lock();
        loading_msg = std::string("Loading GCVS Catalog...");
        mtx.unlock();
        cout << "Reading GCVS catalog..." << endl << flush;
        int nGCVS = cr.read_GCVS_catalog(cels);
        cout << "Read " << nGCVS << " objects." << endl << flush;
        Gliese_doubles_fix();
    }
    if (load_aborted()) return;
    if (have_Uranio && !ihsc)
    {
        mtx.lock();
        loading_msg = std::string("Loading Uranometria Catalog...");
        mtx.unlock();
        cout << "Reading Uranometria catalog..." << endl << flush;
        int nUra = cr.read_Uranometria_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nUra << " objects." << endl << flush;
    }
    if (0) // have_WD && !ihsc)
    {
        mtx.lock();
        loading_msg = std::string("Loading White Dwarfs Catalog...");
        mtx.unlock();
        cout << "Reading White Dwarfs catalog..." << endl << flush;
        int nWD = cr.read_WD_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nWD << " objects." << endl << flush;
        Gliese_doubles_fix();
    }

    if (load_aborted()) return;
    mtx.lock();
    loading_msg = std::string("Naming stars...");
    mtx.unlock();
    if (!ihsc)
    {
        rename_all_from_Bayer_Flamsteed();
        cr.read_starname_dat(cels);
    }

    if (load_aborted()) return;
    if (!magnitude_test)                    // If magnitude test, cut out all the slow loading stuff and streamline.
    {
        #if _USE_CCDM
        if (have_CCDM)
        {
            mtx.lock();
            loading_msg = std::string("Loading Catalogue of the Components of Double and Multiple Stars...");
            mtx.unlock();
            cout << "Reading CCDM catalog..." << endl << flush;
            int nCCDM = cr.read_CCDM_catalog(cels, MAX_CELOBJS);
            cout << "Read " << nCCDM << " objects." << endl << flush;
        }
        #endif

        if (have_SB9 && !ihsc)
        {
            mtx.lock();
            loading_msg = std::string("Loading Stellar Binaries Catalog...");
            mtx.unlock();
            cout << "Reading SB9 catalog..." << endl << flush;
            int nSB9 = cr.read_SB9_catalog(cels, MAX_CELOBJS);
            cout << "Read " << nSB9 << " objects." << endl << flush;
        }

        for (i=0; cels[i]; i++)
        {
            if (cels[i]->deleted) continue;
            if (cels[i]->type == star) num_stars++;
            if (!cels[i]->cenobj) cels[i]->cenobj = cels[i];
        }
    }

    if (!ihsc)
    {
        mtx.lock();
        loading_msg = std::string("Naming stars...");
        mtx.unlock();
        // rename_all_from_Bayer_Flamsteed();
        cr.read_starname_dat(cels);
    }

    // Galaxies. The UNGC goes first: its distances are measured rather than inferred from
    // velocity, and read_RC3_catalog() skips whatever it has already placed.
    if (load_aborted()) return;
    if (have_UNGC && !magnitude_test)
    {
        mtx.lock();
        loading_msg = std::string("Loading nearby galaxies...");
        mtx.unlock();
        cout << "Reading UNGC catalog..." << endl << flush;
        int nUNGC = cr.read_UNGC_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nUNGC << " objects." << endl << flush;
    }
    if (load_aborted()) return;
    if (have_RC3 && !magnitude_test)
    {
        mtx.lock();
        loading_msg = std::string("Loading bright galaxies...");
        mtx.unlock();
        cout << "Reading RC3 catalog..." << endl << flush;
        int nRC3 = cr.read_RC3_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nRC3 << " objects." << endl << flush;
    }

    // Because of system inclinations, we will die unless we read star orbits before reading exoplanets.
    // At the same time, there are stars in the star_orbits file that we don't have until we load exoplanets!
    // What to do, oh what to do...
    if (load_aborted()) return;
    if (!noexo) cr.load_exoplanets_from_tap(true);              // How about first we load exostars then fill them in with star orbits?

    mtx.lock();
    loading_msg = std::string("Orbiting stars...");
    mtx.unlock();
    if (!magnitude_test) cr.read_star_orbits_dat(cels);
    else splash = false;

    if (load_aborted()) return;
    if (!noexo)
    {
        cout << "Reading exoplanets..." << endl << flush;
        int nexo = cr.load_exoplanets_from_tap();
        if (!nexo) nexo = cr.read_exoplanets_catalog(cels, MAX_CELOBJS);
        if (nexo) have_exo = true;
        num_planets += nexo;
        cout << "Read " << nexo << " objects." << endl << flush;
    }

    if (magnitude_test)
    {
        for (i=0; i<290; i++)
        {
            double magnitude = -1.0 + 0.1 * i;
            Star* s = new Star();
            strcpy(s->name, ((std::string)"Test "+std::to_string(magnitude)).c_str());
            s->namelen = strlen(s->name);
            s->right_ascension = fiftyseventh * i;
            s->declination = -2.59 * fiftyseventh;
            s->apparent_magnitude = s->absolute_magnitude = magnitude;
            s->distance = parsec*10;
            s->proper_motion_decl = s->proper_motion_RA = s->radial_velocity = 0;
            s->BV_color = 0.5;
            s->epoch = J2000;
            s->update_location(simnow);
            append_cel(s);
        }
    }
    else if (!nosats)
    {
        mtx.lock();
        loading_msg = std::string("Loading satellite data...");
        mtx.unlock();
        cout << loading_msg << endl << flush;
        SatSource::read_sources_json();
        n = sat_sources.size();

        std::vector<int> sources_sorted;
        for (i=0; i<n; i++)
        {
            m = sources_sorted.size();
            if (!m) sources_sorted.push_back(i);
            else
            {
                bool inserted = false;
                for (j=0; j<m; j++)
                {
                    if (!sat_sources[i].is_supplemental
                        ||  (sat_sources[sources_sorted[j]].is_supplemental
                            && sat_sources[sources_sorted[j]].data_age_hours() < sat_sources[i].data_age_hours()))
                    {
                        sources_sorted.insert(sources_sorted.begin()+j, i);
                        inserted = true;
                        break;
                    }
                }
                if (!inserted) sources_sorted.push_back(i);
            }
        }

        for (i=0; i<n; i++)
        {
            if (load_aborted()) return;
            // std::cout << "Reading " << sat_sources[sources_sorted[i]].csv_fname() << " age " << sat_sources[sources_sorted[i]].data_age_hours() << std::endl;
            // if (!file_exists(sat_sources[sources_sorted[i]].csv_fname().c_str())) sat_sources[sources_sorted[i]].download_data();
            mtx.lock();
            loading_msg = std::string("Loading ") + sat_sources[sources_sorted[i]].local_name + std::string(" satellite data...");
            mtx.unlock();
            sat_sources[sources_sorted[i]].read_csv_data();
        }
    }

    if (load_univ.size())
    {
        if (load_universe(load_univ)) SDL_SetWindowTitle(window, (load_univ + std::string(" - Alienorum")).c_str());
    }

    set_center_objects();
    refresh_star_visibilities();

    time_t finished = time(NULL);
    std::string elapsed = elapsed_time(began, finished);
    std::cout << "Loaded data in " << elapsed << std::endl;
}

static void parse_cons_lines_file(const char* filename, int& l, std::string& vantage_name)
{
    FILE* fp = fopen(filename, "rb");
    if (fp)
    {
        char buffer[65536];
        while (fgets(buffer, 65532, fp))
        {
            char* newline = strchr(buffer, '\n');
            if (newline)
            {
                *newline = 0;
            }
            newline = strchr(buffer, '\r');
            if (newline)
            {
                *newline = 0;
            }
            if (*buffer == ':')
            {
                vantage_name = trim(&buffer[1]);
            }
            if (*buffer == '~')
            {
                char* name2 = strchr(buffer, ',');
                if (!name2)
                {
                    continue;
                }
                *name2 = 0;
                name2++;
                while (*name2 == ' ')
                {
                    *name2 = 0;
                    name2++;
                }
                char* name3 = strchr(name2, ',');
                if (name3)
                {
                    *name3 = 0;
                    name3++;
                    while (*name3 == ' ')
                    {
                        *name3 = 0;
                        name3++;
                    }
                }
                if (strlen(name2))
                {
                    Constellation c;
                    c.name = name2;
                    c.abbrev = &buffer[1];
                    if (name3 && strlen(name3))
                    {
                        c.genitive = name3;
                    }
                    c.vantage_name = vantage_name;
                    c.vantage_resolved = false;
                    constellations.push_back(c);
                    num_reg_cons++;
                    l++;
                }
            }
            else if (l >= 0)
            {
                char *name1 = buffer, *name2, *name3;
                name2 = strchr(name1, ',');
                if (!name2)
                {
                    goto _no_more_names;
                }
                *name2 = 0;
                name2++;
                while (*name2 == ' ')
                {
                    *name2 = 0;
                    name2++;
                }

                do
                {
                    name3 = nullptr;
                    if (strlen(name2))
                    {
                        name3 = strchr(name2, ',');
                        if (name3)
                        {
                            *name3 = 0;
                            name3++;
                            while (*name3 == ' ')
                            {
                                *name3 = 0;
                                name3++;
                            }
                        }
                        ConsLine cl;
                        cl.starnamea = name1;
                        cl.starnameb = trim(name2);
                        constellations[l].lines.push_back(cl);
                    }

                    name1 = name2;
                    name2 = name3;
                }
                while (name3);
            }

            _no_more_names:
            ;
        }
        fclose(fp);
    }
}

void read_cons_lines()
{
    int l = (int)constellations.size() - 1;
    std::string vantage_name;
    parse_cons_lines_file("consline.dat", l, vantage_name);
    parse_cons_lines_file("exocons.dat", l, vantage_name);
}

void cache_cons_lines()
{
    int ncons = constellations.size();
    std::unordered_map<std::string, int> resolved_cache;
    resolved_cache.reserve(1024);

    for (int i = 0; i < ncons; i++)
    {
        if (!constellations[i].vantage_resolved)
        {
            if (constellations[i].vantage_name.empty() || constellations[i].vantage_name == "Sun" || constellations[i].vantage_name == "Sol")
            {
                constellations[i].vantage = (cels && cels[0]) ? cels[0]->location : Point(0, 0, 0);
                constellations[i].vantage_resolved = true;
            }
            else
            {
                int sidx = find_object(constellations[i].vantage_name.c_str(), true);
                if (sidx >= 0 && cels && cels[sidx])
                {
                    constellations[i].vantage = cels[sidx]->location;
                    constellations[i].vantage_resolved = true;
                }
            }
        }

        double mag_limit = (i == 34) ? 7.5 : 6.5;

        mtx.lock();
        loading_msg = std::string("Assigning ") + constellations[i].name + std::string("...");
        mtx.unlock();

        std::unordered_map<std::string_view, int> cons_stars;
        if (constellation_index.count(constellations[i].abbrev))
        {
            const auto& cstars = constellation_index[constellations[i].abbrev];
            cons_stars.reserve(cstars.size() * 2);
            for (CelestialObject* co : cstars)
            {
                Star* s = (Star*)co;
                if (s->apparent_magnitude > mag_limit)
                {
                    continue;
                }
                if (s->Bayer[0])
                {
                    cons_stars.emplace(s->Bayer, s->seqno);
                }
                if (s->Flamsteed[0])
                {
                    cons_stars.emplace(s->Flamsteed, s->seqno);
                }
                if (s->name[0])
                {
                    cons_stars.emplace(s->name, s->seqno);
                }
            }
        }

        auto resolve_endpoint = [&](const std::string& starname) -> int
        {
            auto it_cached = resolved_cache.find(starname);
            if (it_cached != resolved_cache.end())
            {
                return it_cached->second;
            }

            auto it_local = cons_stars.find(starname);
            if (it_local != cons_stars.end())
            {
                resolved_cache.emplace(starname, it_local->second);
                return it_local->second;
            }

            int found = find_object(starname.c_str(), true, mag_limit);
            resolved_cache.emplace(starname, found);
            return found;
        };

        int nln = constellations[i].lines.size();
        for (int l = 0; l < nln; l++)
        {
            int founda = resolve_endpoint(constellations[i].lines[l].starnamea);
            int foundb = resolve_endpoint(constellations[i].lines[l].starnameb);

            if (founda < 0)
            {
                std::cerr << "Warning: Failed to identify " << constellations[i].lines[l].starnamea << " for constellation lines." << std::endl;
            }
            if (foundb < 0)
            {
                std::cerr << "Warning: Failed to identify " << constellations[i].lines[l].starnameb << " for constellation lines." << std::endl;
            }

            if (founda >= 0)
            {
                constellations[i].lines[l].a = (Star*)cels[founda];
                ((Star*)cels[founda])->make_universally_visible();
            }
            if (foundb >= 0)
            {
                constellations[i].lines[l].b = (Star*)cels[foundb];
                ((Star*)cels[foundb])->make_universally_visible();
            }
        }
    }
}

void add_batch_satellites(std::vector<std::string> listlines)
{
    BatchSatTicket ticket;
    int i;
    for (i=0; cels[i]; i++);               // get count
    ncelobjs = i;
    char buffer[1024];

    int m = listlines.size();
    sats_added = sat_errors = 0;
    for (int n=0; n<m; n++)
    {
        mtx.lock();
        Satellite *sat = new Satellite();
        if (!append_cel(sat))
        {
            // Array full. Nothing later in the list will fit either, so stop rather than
            // allocating and discarding one object per remaining row.
            delete sat;
            sat_errors++;
            mtx.unlock();
            break;
        }

        if (n < listlines.size()) strcpy(buffer, listlines[n].c_str());
        char *hashmarks = strstr(buffer, "##");
        i = hashmarks ? atoi(&hashmarks[2]) : 0;

        if (SatSource::populate(sat, i, 24))
        {
            sats_added++;
        }
        else
        {
            ncelobjs--;
            cels[ncelobjs] = 0;
            sat_errors++;
        }
        mtx.unlock();
    }
}

void load_stuff()
{
    mtx.lock();
    loading_msg = "Reading spectral types...";
    mtx.unlock();
    Star::load_main_seq_dat();

    std::vector<std::string> lfaves;
    fstream fs("user.json", std::ios::in);
    if (fs)
    {
        viewer_locale = "";
        json j;
        fs >> j;
        double dbl;
        try { j.at("Latitude").get_to(dbl); viewer_lat = viewer_home_lat = dbl * fiftyseventh; } catch(...) { ; }
        try { j.at("Longitude").get_to(dbl); viewer_lon = viewer_home_lon = dbl * fiftyseventh; } catch(...) { ; }
        try { j.at("Timezone").get_to(dbl); viewer_tz = viewer_home_tz = dbl * 60; } catch(...) { ; }
        try { j.at("Theme").get_to(viewer_theme); } catch(...) { ; }
        try { j.at( (std::string("Theme") + std::to_string(wkday)).c_str() ).get_to(viewer_theme); } catch(...) { ; }
        try { j.at("StarPoint").get_to(npointedstar); } catch(...) { ; }
        try { j.at("Gamma").get_to(viewer_gamma); global_gamma = viewer_gamma; } catch(...) { ; }

        try
        {
            j.at("FaveStars").get_to(lfaves);
        }
        catch (...)
        {
            ;
        }

        try
        {
            j.at("PlayRiseSetSounds").get_to(play_rise_set_sound);
        }
        catch (...)
        {
            ;
        }

        try
        {
            j.at("RiseSound").get_to(rise_sound_path);
        }
        catch (...)
        {
            try
            {
                j.at("RiseSoundPath").get_to(rise_sound_path);
            }
            catch (...)
            {
                ;
            }
        }

        try
        {
            j.at("SetSound").get_to(set_sound_path);
        }
        catch (...)
        {
            try
            {
                j.at("SetSoundPath").get_to(set_sound_path);
            }
            catch (...)
            {
                ;
            }
        }

        fs.close();
    }
    else
    {
        viewer_lat = 32.5425   * fiftyseventh;              // Babylon, site of some of the earliest attested astronomical knowledge.
        viewer_lon = 44.421111 * fiftyseventh;
    }

    if (load_aborted()) return;
    mtx.lock();
    loading_msg = "Reading constellations...";
    mtx.unlock();
    read_cons_lines();

    if (load_aborted()) return;
    mtx.lock();
    loading_msg = "Reading constellation boundaries...";
    mtx.unlock();
    CatalogReader cr;
    cr.read_cons_boundaries();

    if (load_aborted()) return;
    mtx.lock();
    loading_msg = "Loading star data...";
    mtx.unlock();
    load_catalogs();

    // load_catalogs() returns early on abort, so cels[0] may not exist -- everything below here
    // assumes the Sun is loaded (see the bv_correction line, which reads cels[0] directly).
    if (load_aborted() || !cels[0])
    {
        return;
    }

    for (const std::string& favename : lfaves)
    {
        int i = find_object(favename.c_str(), true);
        if (i >= 0 && cels[i]->typeclass() == class_star)
        {
            favestars.push_back((Star*)cels[i]);
            ((Star*)cels[i])->is_faved = true;
        }
    }

    mtx.lock();
    loading_msg = "Auditing main sequence stars...";
    mtx.unlock();
    Star::audit_and_correct_main_sequence_stars(cels);

    std::string ihcfn = cr.get_condensed_starcat_name();
    if (!file_exists(ihcfn.c_str()))
    {
        mtx.lock();
        loading_msg = "Applying Gaia astrometry & distances...";
        mtx.unlock();
        cr.apply_gaia_astrometry(cels);
    }

    mtx.lock();
    loading_msg = "Assigning constellations...";
    mtx.unlock();
    cache_cons_lines();
    if (!file_exists(ihcfn.c_str()))
    {
        ConsBins cb = fill_alienorum_ids();
        cr.write_condensed_star_cat(cb);
    }

    bv_correction = log(blackbody_flux(sun_temp, V_band) / blackbody_flux(sun_temp, B_band)) * invlogmagnbase - cels[0]->BV_color;
    std::cout << "B-V correction: " << bv_correction << std::endl;

    // Galaxies were given BV_color = -bv_correction while it was being read in load_catalogs(),
    // above, before this correction was known -- which left them stamped with 0 instead of the
    // value that actually cancels it out. Fix them up now that bv_correction is final, or they
    // render with whatever tint bv_correction happens to be rather than the intended neutral
    // white/gray.
    for (int i=0; cels[i]; i++)
        if (cels[i]->type == galaxy) cels[i]->BV_color = -bv_correction;

    mtx.lock();
    loading_msg = "Done!";
    splash = false;
    load_completed = true;
    mtx.unlock();
}

void reload_stuff()
{
    struct ReloadGuard
    {
        ~ReloadGuard()
        {
            mtx.lock();
            splash = false;
            mtx.unlock();
            is_reloading = false;
        }
    } guard;

    try
    {
        if (abort_load)
        {
            return;
        }
        mtx.lock();
        loading_msg = "Refreshing spectral types...";
        mtx.unlock();
        Star::load_main_seq_dat();

        cons4lbl = nullptr;
        is_a_locale_under_cursor = nullptr;
        selected_locale = nullptr;

        CatalogReader cr;
        constellations.clear();
        num_reg_cons = 0;

        if (abort_load)
        {
            return;
        }
        mtx.lock();
        loading_msg = "Refreshing constellations...";
        mtx.unlock();
        read_cons_lines();
        cr.read_cons_boundaries();

        if (abort_load)
        {
            return;
        }
        mtx.lock();
        loading_msg = "Assigning stars to constellations...";
        mtx.unlock();
        cache_cons_lines();

        if (abort_load)
        {
            return;
        }
        mtx.lock();
        loading_msg = "Refreshing star orbits...";
        mtx.unlock();
        cr.read_star_orbits_dat(cels);

        int i;
        for (i=0; cels[i]; i++)
        {
            delete[] cels[i]->locales;
            cels[i]->locales = nullptr;
            cels[i]->nlocales = 0;
        }

        mtx.lock();
        loading_msg = "Done!";
        mtx.unlock();
        link_astorb_with_cels();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in reload_stuff: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown exception in reload_stuff." << std::endl;
    }
}

bool save_user_json()
{
    try
    {
        json j;

        std::fstream fsi("user.json", std::ios::in);
        fsi >> j;
        fsi.close();

        j["Latitude"] = viewer_lat * fiftyseven;
        j["Longitude"] = viewer_lon * fiftyseven;
        j["Timezone"] = (int)(viewer_home_tz / 60);
        j["Theme"] = themes[themes_selected_idx];
        j["Gamma"] = global_gamma;

        std::vector<std::string> favenames;
        for (const Star *s : favestars) favenames.push_back(s->name);
        j["FaveStars"] = favenames;

        j["PlayRiseSetSounds"] = play_rise_set_sound;
        if (!rise_sound_path.empty())
        {
            j["RiseSound"] = rise_sound_path;
        }
        if (!set_sound_path.empty())
        {
            j["SetSound"] = set_sound_path;
        }

        std::fstream fso("user.json", std::ios::out);
        fso << j.dump(4);
        fso.close();

        return true;
    }
    catch (...)
    {
        return false;
    }
}

namespace
{
    void load_astorb_worker(std::unordered_map<int, Planet*> by_number, std::unordered_map<std::string, Planet*> by_name)
    {
        std::string path = "catalogs" _FILESLASH "astorb" _FILESLASH "astorb.dat";
        FILE* fp = fopen(path.c_str(), "rb");

        if (!fp)
        {
            std::string gzpath = path + ".gz";
            if (file_exists(gzpath.c_str()))
            {
                extract_archive(gzpath.c_str());
                fp = fopen(path.c_str(), "rb");
            }
        }

        if (!fp)
        {
            astorb_load_failed.store(true);
            astorb_loading.store(false);
            return;
        }

        fseek(fp, 0, SEEK_END);
        long long total_bytes = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (total_bytes <= 0)
        {
            total_bytes = 1;
        }

        std::vector<AstorbRow> temp_astorb;
        temp_astorb.reserve(1100000);

        char buffer[1024];
        char field[32];
        size_t count = 0;

        while (fgets(buffer, sizeof(buffer), fp))
        {
            if (astorb_cancel.load())
            {
                fclose(fp);
                astorb_loading.store(false);
                return;
            }

            AstorbRow row;
            row.cel = nullptr;

            //   8- 25  A18   ---     Name      Name or preliminary designation.
            CatalogReader::read_field_onebased(buffer, 8, 25, field);
            row.name = trim(field);

            //   1-  6  I6    ---     Planet    [1,]?+ Asteroid number (blank if unnumbered)
            CatalogReader::read_field_onebased(buffer, 1, 6, field);
            row.number = atoi(field);
            if (row.number == 5747)
            {
                row.name = "Williamina";
            }

            //  60- 64  F5.1  km      Diam      ? IRAS diameter (see E.F.Tedesco, pp.1151-1161; catalog <II/190>)
            CatalogReader::read_field_onebased(buffer, 60, 64, field);
            row.diam = atof(field);

            // 148-157  F10.6 deg     i         Inclination (3)
            CatalogReader::read_field_onebased(buffer, 148, 157, field);
            row.incl = atof(field);

            // 169-181  F13.8 AU      a         ? Semimajor axis (3)
            CatalogReader::read_field_onebased(buffer, 169, 181, field);
            row.sma = atof(field);

            if (row.number > 0)
            {
                auto it = by_number.find(row.number);
                if (it != by_number.end())
                {
                    row.cel = it->second;
                }
            }
            if (!row.cel && !row.name.empty())
            {
                auto it = by_name.find(row.name);
                if (it != by_name.end())
                {
                    row.cel = it->second;
                }
            }

            temp_astorb.push_back(std::move(row));
            count++;

            if ((count & 8191) == 0)
            {
                astorb_rows_loaded.store(count);
                long long pos = ftell(fp);
                float prog = (float)pos / (float)total_bytes;
                if (prog > 1.0f)
                {
                    prog = 1.0f;
                }
                astorb_load_progress.store(prog);
            }
        }

        fclose(fp);

        if (astorb_cancel.load())
        {
            astorb_loading.store(false);
            return;
        }

        astorb_rows_loaded.store(temp_astorb.size());
        astorb_load_progress.store(1.0f);
        astorb = std::move(temp_astorb);
        astorb_loaded.store(true);
        astorb_loading.store(false);
    }
}

void start_astorb_background_load()
{
    if (astorb_loaded.load() || astorb_loading.load())
    {
        return;
    }

    if (astorb_thread.joinable())
    {
        astorb_thread.join();
    }

    astorb_cancel.store(false);
    astorb_load_failed.store(false);
    astorb_load_progress.store(0.0f);
    astorb_rows_loaded.store(0);
    astorb_loading.store(true);

    std::unordered_map<int, Planet*> by_number;
    std::unordered_map<std::string, Planet*> by_name;
    for (int i = 0; cels[i]; ++i)
    {
        if (cels[i]->typeclass() == class_planet)
        {
            Planet* p = static_cast<Planet*>(cels[i]);
            if (p->asteroid_no > 0)
            {
                by_number[p->asteroid_no] = p;
            }
            if (p->name[0] != '\0')
            {
                by_name[p->name] = p;
            }
        }
    }

    astorb_thread = std::thread(load_astorb_worker, std::move(by_number), std::move(by_name));
}

void link_astorb_with_cels()
{
    if (!astorb_loaded.load() || astorb.empty())
    {
        return;
    }

    std::unordered_map<int, Planet*> by_number;
    std::unordered_map<std::string, Planet*> by_name;
    for (int i = 0; cels[i]; ++i)
    {
        if (cels[i]->typeclass() == class_planet)
        {
            Planet* p = static_cast<Planet*>(cels[i]);
            if (p->asteroid_no > 0)
            {
                by_number[p->asteroid_no] = p;
            }
            if (p->name[0] != '\0')
            {
                by_name[p->name] = p;
            }
        }
    }

    for (AstorbRow& row : astorb)
    {
        row.cel = nullptr;
        if (row.number > 0)
        {
            auto it = by_number.find(row.number);
            if (it != by_number.end())
            {
                row.cel = it->second;
                continue;
            }
        }
        if (!row.name.empty())
        {
            auto it = by_name.find(row.name);
            if (it != by_name.end())
            {
                row.cel = it->second;
            }
        }
    }
}
