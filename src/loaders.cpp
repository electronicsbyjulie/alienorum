
#include "loaders.h"
#include "housekeeping.h"
#include "classes/cons.h"
#include <cstdlib>              // std::getenv, pour ALIENORUM_HOME dans establish_project_root()

using namespace alienorum;

int sats_added = 0, sat_errors = 0;

void load_textures(CelestialObject* cel)
{
    std::string filename;

    if (!cel->ignore_map_files)                 // For regenerating exoplanet textures.
    {
        filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_clouds.jpg";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map(cel);
            if (map->load_from_jpeg(filename)) cel->cloud_map = map;
        }
        else
        {
            filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_clouds.png";
            if (file_exists(filename.c_str()))
            {
                Map *map = new Map(cel);
                if (map->load_from_png(filename)) cel->cloud_map = map;
            }
        }

        filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_surf.jpg";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map(cel);
            if (map->load_from_jpeg(filename)) cel->surf_map = map;
        }
        else
        {
            filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_surf.png";
            if (file_exists(filename.c_str()))
            {
                Map *map = new Map(cel);
                if (map->load_from_png(filename)) cel->surf_map = map;
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
                }
                else
                {
                    filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_bump.png";
                    if (file_exists(filename.c_str()))
                    {
                        cel->surf_map->load_from_png(filename, true, p->estimate_bump_scale());
                    }
                }
            }
        }

        filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_night.jpg";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map();
            if (map->load_from_jpeg(filename)) cel->night_map = map;
        }
        else
        {
            filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_night.png";
            if (file_exists(filename.c_str()))
            {
                Map *map = new Map();
                if (map->load_from_png(filename)) cel->night_map = map;
            }
        }

        filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_ring.jpg";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map();
            if (map->load_from_jpeg(filename)) cel->ring_map = map;
        }
        else
        {
            filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_ring.png";
            if (file_exists(filename.c_str()))
            {
                Map *map = new Map();
                if (map->load_from_png(filename)) cel->ring_map = map;
            }
        }

        filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_ringx.jpg";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map();
            if (map->load_from_jpeg(filename)) cel->ringx_map = map;
        }
        else
        {
            filename = (std::string)"maps" + _FSSTR + (std::string)cel->name + (std::string)"_ringx.png";
            if (file_exists(filename.c_str()))
            {
                Map *map = new Map();
                if (map->load_from_png(filename)) cel->ringx_map = map;
            }
        }
    }

    cel->looked_for_maps = true;
    cel->ignore_map_files = false;          // one-time use.

    if ((cel->type == gas_giant || cel->type == ice_giant || cel->type == hot_jupiter) && !cel->cloud_map)
    {
        cel->cloud_map = new Map(cel);
        cel->cloud_map->generate_gas_giant_map(cel);
    }
    else if ((cel->type == rocky || cel->type == icy || cel->type == waterworld || cel->type == lavaworld) && !cel->surf_map)
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
    }
    if (cel->cloud_map)
    {
        mapfname = std::string("maps") + _FSSTR + std::string(cel->name) + std::string("_clouds.png");
        cel->cloud_map->save_to_png(mapfname);
    }
    if (cel->night_map)
    {
        mapfname = std::string("maps") + _FSSTR + std::string(cel->name) + std::string("_night.png");
        cel->cloud_map->save_to_png(mapfname);
    }
}

static bool establish_project_root()
{
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;

    if (const char *home = std::getenv("ALIENORUM_HOME")) candidates.push_back(home);
    if (char *base = SDL_GetBasePath())
    {
        candidates.push_back(base);
        SDL_free(base);                                 // le tampon appartient a SDL
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
            if (dir == dir.parent_path()) break;         // racine reelle, pas une longueur de chaine
            dir = dir.parent_path();
        }
    }

    return false;
}

bool look_for_catalogs()
{
    catalogs_found = establish_project_root();

    // radio_silence se decide depuis la racine *resolue*, d'ou sa place apres la recherche et non
    // avant : le fichier appartient au projet, pas au repertoire d'ou l'on a lance la commande.
    // Si la racine n'a pas pu etre etablie, on ignore si `nonet` s'y trouve -- le drapeau echoue
    // donc du cote ferme plutot que d'accorder la permission par defaut. std::filesystem::exists()
    // plutot que file_exists() : ce dernier ouvre le fichier, donc un `nonet` present mais illisible
    // ne comptait pas.
    // On accumule au lieu d'affecter : le drapeau s'enclenche et ne se relache plus. Il existe un
    // second controle du meme fichier dans main() (alienorum.cpp, juste apres l'appel a cette
    // fonction) ; une affectation seche ici l'ecraserait si l'ordre des deux venait a s'inverser.
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

bool load_universe(std::string universe_fname = "universe.json")
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
            if (resave_json) save_universe();
            set_center_objects();
            refresh_star_visibilities();

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

void load_catalogs()
{
    int i, j, m, n;
    time_t began = time(NULL);

    cels[0] = nullptr;

    CatalogReader cr;
    std::string ihcfn = cr.get_condensed_starcat_name();
    bool ihsc = false;
    if (!file_exists(ihcfn.c_str()))
    {
        std::string ihscgz = ihcfn + std::string(".gz");

        if (file_exists(ihscgz.c_str()))
        {
            #ifdef _WIN32
            std::string cmd = (std::string)"7z e -y " + ihscgz + std::string(" -so > ") + ihcfn;
            #else
            std::string cmd = (std::string)"gunzip -k " + ihscgz;
            #endif
            std::cout << cmd << std::endl;
            std::system(cmd.c_str());
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
    cr.download_catalogs();
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
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "RC3")) have_RC3 = true;
        if (!strcmp(cats[i].c_str(), "catalogs" _FILESLASH "UNGC")) have_UNGC = true;
    }

    if (have_Gliese && !ihsc)
    {
        mtx.lock();
        loading_msg = std::string("Loading Gliese catalog...");
        mtx.unlock();
        cout << "Reading Gliese catalog..." << endl << flush;
        int nGliese = cr.read_Gliese_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nGliese << " objects." << endl << flush;
    }

    mtx.lock();
    loading_msg = std::string("Loading solar system...");
    mtx.unlock();

    cout << "Reading local planets..." << endl << flush;
    int npl = cr.read_local_planets(cels, MAX_CELOBJS, cels[0]);
    num_planets += npl;
    for (i=0; cels[i]; i++) if (!strcmp(cels[i]->name, "Earth")) whereami = iamhome = i;
    cout << "Read " << npl << " objects." << endl << flush;

    int nastorb = 0;
    if (have_astorb)
    {
        cout << "Reading astorb catalog..." << endl << flush;
        nastorb = cr.read_astorb_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nastorb << " objects." << endl << flush;
    }

    cout << "Reading local moons..." << endl << flush;
    npl = cr.read_local_planets(cels, MAX_CELOBJS, nullptr, cels[0]);
    num_planets += npl;
    for (i=0; cels[i]; i++) if (!strcmp(cels[i]->name, "Earth")) whereami = iamhome = i;
    cout << "Read " << npl << " objects." << endl << flush;

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

    mtx.lock();
    loading_msg = std::string("Naming stars...");
    mtx.unlock();
    if (!ihsc)
    {
        rename_all_from_Bayer_Flamsteed();
        cr.read_starname_dat(cels);
    }

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
    if (have_UNGC && !magnitude_test)
    {
        mtx.lock();
        loading_msg = std::string("Loading nearby galaxies...");
        mtx.unlock();
        cout << "Reading UNGC catalog..." << endl << flush;
        int nUNGC = cr.read_UNGC_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nUNGC << " objects." << endl << flush;
    }
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
    if (!noexo) cr.load_exoplanets_from_tap(true);              // How about first we load exostars then fill them in with star orbits?

    mtx.lock();
    loading_msg = std::string("Orbiting stars...");
    mtx.unlock();
    if (!magnitude_test) cr.read_star_orbits_dat(cels);
    else splash = false;

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
            // std::cout << "Reading " << sat_sources[sources_sorted[i]].csv_fname() << " age " << sat_sources[sources_sorted[i]].data_age_hours() << std::endl;
            if (!file_exists(sat_sources[sources_sorted[i]].csv_fname().c_str())) sat_sources[sources_sorted[i]].download_data();
            mtx.lock();
            loading_msg = std::string("Loading ") + sat_sources[sources_sorted[i]].local_name + std::string(" satellite data...");
            mtx.unlock();
            sat_sources[i].read_csv_data();
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

void read_cons_lines()
{
    int l;
    FILE* fp = fopen("consline.dat", "rb");
    if (fp)
    {
        char buffer[65536];
        l = -1;
        while (fgets(buffer, 65532, fp))
        {
            char* newline = strchr(buffer, '\n');
            if (newline) *newline = 0;
            newline = strchr(buffer, '\r');
            if (newline) *newline = 0;
            if (*buffer == '~')
            {
                char* name2 = strchr(buffer, ',');
                if (!name2) continue;
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
                    if (name3 && strlen(name3)) c.genitive = name3;
                    constellations.push_back(c);
                    l++;
                }
            }
            else if (l>=0)
            {
                char *name1=buffer, *name2, *name3;
                name2 = strchr(name1, ',');
                if (!name2) goto _no_more_names;
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
                } while (name3);
            }

            _no_more_names:
            ;
        }
        fclose(fp);
    }
}

void cache_cons_lines()
{
    int i, j, l, n, ncons, nln;

    ncons = constellations.size();
    for (i=0; i<ncons; i++)
    {
        double mag_limit = (i == 34) ? 7.5 : 6.5;

        mtx.lock();
        loading_msg = std::string("Assigning ") + constellations[i].name + std::string("...");
        mtx.unlock();

        nln = constellations[i].lines.size();
        for (l=0; l<nln; l++)
        {
            int founda = -1, foundb = -1, rechercher;
            if (constellation_index.count(constellations[i].abbrev))
            {
                n = constellation_index[constellations[i].abbrev].size();
                for (j=0; j<n; j++)
                {
                    Star* s = (Star*) constellation_index[constellations[i].abbrev][j];
                    if (s->apparent_magnitude > mag_limit) continue;
                    if ((founda<0) && !strcmp(s->Bayer, constellations[i].lines[l].starnamea.c_str()))
                        founda = s->seqno;
                    if ((founda<0) && !strcmp(s->Flamsteed, constellations[i].lines[l].starnamea.c_str()))
                        founda = s->seqno;
                    if ((foundb<0) && !strcmp(s->Bayer, constellations[i].lines[l].starnameb.c_str()))
                        foundb = s->seqno;
                    if ((foundb<0) && !strcmp(s->Flamsteed, constellations[i].lines[l].starnameb.c_str()))
                        foundb = s->seqno;
                }
            }

            if (founda<0 || foundb<0)
            {
                rechercher = find_object(constellations[i].lines[l].starnamea.c_str(), true, mag_limit);
                if (rechercher >= 0) founda = rechercher;
                rechercher = find_object(constellations[i].lines[l].starnameb.c_str(), true, mag_limit);
                if (rechercher >= 0) foundb = rechercher;
            }

            if (founda < 0) std::cerr << "Warning: Failed to identify " << constellations[i].lines[l].starnamea << " for constellation lines." << std::endl;
            if (foundb < 0) std::cerr << "Warning: Failed to identify " << constellations[i].lines[l].starnameb << " for constellation lines." << std::endl;

            constellations[i].lines[l].a = (Star*)cels[founda];
            constellations[i].lines[l].b = (Star*)cels[foundb];
            if (founda >= 0) ((Star*)cels[founda])->make_universally_visible();
            if (foundb >= 0) ((Star*)cels[foundb])->make_universally_visible();
        }
    }
}

void add_batch_satellites(std::vector<std::string> listlines)
{
    int i;
    for (i=0; cels[i]; i++);               // get count
    ncelobjs = i;
    char buffer[256];

    int m = listlines.size();
    sats_added = sat_errors = 0;
    for (int n=0; n<m; n++)
    {
        mtx.lock();
        Satellite *sat = new Satellite();
        append_cel(sat);

        strcpy(buffer, listlines[n].c_str());
        char *hashmarks = strstr(buffer, "##");
        i = atoi(&hashmarks[2]);

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

    fstream fs("user.json", std::ios::in);
    if (fs)
    {
        viewer_locale = "";
        json j;
        fs >> j;
        double dbl;
        try { j.at("Latitude").get_to(dbl); viewer_lat = viewer_home_lat = dbl * fiftyseventh; } catch(...) { ; }
        try { j.at("Longitude").get_to(dbl); viewer_lon = viewer_home_lon = dbl * fiftyseventh; } catch(...) { ; }
        try { j.at("Theme").get_to(viewer_theme); } catch(...) { ; }
        try { j.at("Gamma").get_to(viewer_gamma); global_gamma = viewer_gamma; } catch(...) { ; }
        fs.close();
    }
    else
    {
        viewer_lat = 32.5425   * fiftyseventh;              // Babylon, site of some of the earliest attested astronomical knowledge.
        viewer_lon = 44.421111 * fiftyseventh;
    }

    mtx.lock();
    loading_msg = "Reading constellations...";
    mtx.unlock();
    read_cons_lines();

    mtx.lock();
    loading_msg = "Reading constellation boundaries...";
    mtx.unlock();
    CatalogReader cr;
    cr.read_cons_boundaries();

    mtx.lock();
    loading_msg = "Loading star data...";
    mtx.unlock();
    load_catalogs();

    mtx.lock();
    loading_msg = "Assigning constellations...";
    mtx.unlock();
    cache_cons_lines();
    std::string ihcfn = cr.get_condensed_starcat_name();
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
    mtx.unlock();
}

void reload_stuff()
{
    mtx.lock();
    loading_msg = "Refreshing spectral types...";
    mtx.unlock();
    Star::load_main_seq_dat();

    CatalogReader cr;
    constellations.clear();

    mtx.lock();
    loading_msg = "Refreshing constellations...";
    mtx.unlock();
    read_cons_lines();
    cr.read_cons_boundaries();
    mtx.lock();
    loading_msg = "Assigning stars to constellations...";
    mtx.unlock();
    cache_cons_lines();
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
    splash = false;
    mtx.unlock();
}

bool save_user_json()
{
    try
    {
        json j;

        j["Latitude"] = viewer_home_lat * fiftyseven;
        j["Longitude"] = viewer_home_lon * fiftyseven;
        j["Theme"] = viewer_theme;
        j["Gamma"] = global_gamma;

        std::fstream fs("user.json", std::ios::out);
        fs << j.dump(4);
        fs.close();

        return true;
    }
    catch (...)
    {
        return false;
    }
}
