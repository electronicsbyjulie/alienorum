#define GL_GLEXT_PROTOTYPES
#include "../src/globals.h"
#include "../src/dialogs.h"
#include "../src/classes/cat.h"
#include "../src/classes/fictionalstars.h"
#include "../src/classes/serial.h"
#include <fstream>
#include <map>
#include <numeric>

using namespace alienorum;
extern int neighbly, lneighbly;
extern bool onlysun, onlywd, onlyplt, onlyhz, onlyknp;

namespace
{
    std::string kind(const Star& star)
    {
        if (star.is_white_dwarf()) return "WD";
        const std::string spectral = star.spectral_type;
        if (star.volumetric_mean_radius > 3*solar_radius && star.absolute_magnitude < 3.5
            && !star.is_main_sequence() && (spectral.find("III") != std::string::npos ||
            (spectral.find("II") != std::string::npos && spectral.find("VII") == std::string::npos) ||
            spectral.find("Ia") != std::string::npos || spectral.find("Ib") != std::string::npos)) return "giant";
        if (spectral.find("IV") != std::string::npos) return "subgiant";
        for (char c : spectral) if (std::string("OBAFGKM").find(c) != std::string::npos) return std::string(1, c);
        return "other";
    }

    json measure(Star* center_star)
    {
        json result = {{"counts", json::object()}, {"magnitude_bins", json::object()}, {"visible_color_bins", json::object()}};
        const double radii[] = {5, 10, 25, 50, 100, 250, 500, 1000};
        const double magnitude_edges[] = {-100, -1, 0, 1, 2, 3, 4, 5, 6, 6.5, 8, 10, 15, 100};
        const double color_edges[] = {-10, 0, 0.3, 0.6, 1, 1.5, 10};
        std::map<std::string, std::vector<Star*>> nearby_types;
        std::vector<std::pair<double, Star*>> giants;
        for (int i = 0; cels[i]; ++i)
        {
            if (cels[i] == center_star || cels[i]->deleted || cels[i]->typeclass() != class_star) continue;
            Star* star = static_cast<Star*>(cels[i]);
            const double distance = star->location.distance_to(center_star->location)/light_year;
            if (!(distance > 0) || distance > 1000) continue;
            const std::string type = kind(*star);
            const double magnitude = star->viewer_magnitude(center_star->location);
            for (double radius : radii)
                if (distance <= radius)
                {
                    const std::string key = std::to_string(int(radius));
                    if (!result["counts"].contains(key)) result["counts"][key] = json::object();
                    result["counts"][key]["all"] = result["counts"][key].value("all", 0)+1;
                    result["counts"][key][type] = result["counts"][key].value(type, 0)+1;
                }
            for (int bin = 0; bin < 13; ++bin)
                if (magnitude >= magnitude_edges[bin] && magnitude < magnitude_edges[bin+1])
                {
                    const std::string key = std::to_string(bin);
                    result["magnitude_bins"][key] = result["magnitude_bins"].value(key, 0)+1;
                }
            if (magnitude <= 6.5)
            {
                result["naked_eye"] = result.value("naked_eye", 0)+1;
                if (!result.contains("visible_types")) result["visible_types"] = json::object();
                result["visible_types"][type] = result["visible_types"].value(type, 0)+1;
                for (int bin = 0; bin < 6; ++bin)
                    if (star->BV_color >= color_edges[bin] && star->BV_color < color_edges[bin+1])
                    {
                        const std::string key = std::to_string(bin);
                        result["visible_color_bins"][key] = result["visible_color_bins"].value(key, 0)+1;
                        if (!result.contains("visible_type_colors")) result["visible_type_colors"] = json::object();
                        if (!result["visible_type_colors"].contains(type)) result["visible_type_colors"][type] = json::object();
                        result["visible_type_colors"][type][key] = result["visible_type_colors"][type].value(key, 0)+1;
                    }
            }
            if (type == "giant") giants.push_back({distance, star});
            if (distance <= 100 && !star->orbit) nearby_types[type].push_back(star);
        }
        std::sort(giants.begin(), giants.end());
        result["nearest_giants"] = json::array();
        for (size_t i = 0; i < std::min(size_t(12), giants.size()); ++i)
            result["nearest_giants"].push_back({{"name", giants[i].second->name},
                {"ly", giants[i].first}, {"spectral", giants[i].second->spectral_type},
                {"absolute_magnitude", giants[i].second->absolute_magnitude}});
        for (const auto& entry : nearby_types)
        {
            double sum = 0;
            for (Star* star : entry.second)
            {
                double nearest = 1e300;
                for (Star* other : entry.second)
                    if (other != star) nearest = std::min(nearest, star->location.distance_to(other->location)/light_year);
                if (entry.second.size() > 1) sum += nearest;
            }
            result["same_type_mean_nearest_100ly"][entry.first] = entry.second.size() > 1 ? json(sum/entry.second.size()) : json(nullptr);
        }
        return result;
    }

    void snapshot(Star* observer, const json& report, const std::string& file, const std::string& caption)
    {
        here = observer->location;
        mycenobj = observer;
        whereami = observer->seqno;
        neighborhood = true;
        onlysun = onlywd = onlyplt = onlyhz = onlyknp = false;
        neighbly = 100;
        lneighbly = -1;
        last_neighb_cen = nullptr;
        for (int i = 0; cels[i]; ++i)
        {
            CelestialLocation relative = cels[i]->location-here;
            cels[i]->tmprel = Point(relative);
        }
        for (int frame = 0; frame < 2; ++frame)
        {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(12, 48));
            draw_stellar_neighborhood(ImGui::GetIO());
            ImGui::GetBackgroundDrawList()->AddText(ImVec2(16, 15), IM_COL32_WHITE, caption.c_str());
            ImGui::SetNextWindowPos(ImVec2(12, 485));
            ImGui::SetNextWindowSize(ImVec2(980, 390));
            ImGui::Begin("Population audit", nullptr, ImGuiWindowFlags_NoSavedSettings);
            const std::string counts = report.at("counts").dump();
            ImGui::TextWrapped("Counts by radius (ly): %s", counts.c_str());
            ImGui::Text("Stars with apparent magnitude <= 6.5: %d", report.value("naked_eye", 0));
            ImGui::TextUnformatted("Nearest giants:");
            for (const auto& giant : report.at("nearest_giants"))
                ImGui::Text("%-37s %8.2f ly  %-12s Mv=%6.2f", giant.at("name").get<std::string>().c_str(),
                    giant.at("ly").get<double>(), giant.at("spectral").get<std::string>().c_str(),
                    giant.at("absolute_magnitude").get<double>());
            ImGui::End();
            ImGui::Render();
            glViewport(0, 0, 1024, 900);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0.015, 0.02, 0.035, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        std::vector<unsigned char> pixels(1024*900*4), flipped(pixels.size());
        glReadPixels(0, 0, 1024, 900, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (int y = 0; y < 900; ++y)
            std::copy_n(pixels.data()+(899-y)*1024*4, 1024*4, flipped.data()+y*1024*4);
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(flipped.data(), 1024, 900, 32, 4096, SDL_PIXELFORMAT_RGBA32);
        if (IMG_SavePNG(surface, file.c_str()) != 0) throw std::runtime_error(IMG_GetError());
        SDL_FreeSurface(surface);
    }
}

int main(int argc, char** argv)
{
    const std::string prefix = argc > 1 ? argv[1] : "snapshots/population_refinement/00_baseline";
    std::filesystem::create_directories(std::filesystem::path(prefix).parent_path());
    cels = new CelestialObject*[MAX_CELOBJS]();
    Star::load_main_seq_dat();
    Star* sun = new Star();
    strcpy(sun->name, "Sun");
    strcpy(sun->spectral_type, "G2V");
    sun->mass = solar_mass;
    sun->temperature = sun_temp;
    sun->volumetric_mean_radius = solar_radius;
    sun->absolute_magnitude = 4.83;
    sun->BV_color = Star::interpolate_mseq_BV(42);
    bv_correction = log(blackbody_flux(sun_temp, V_band)/blackbody_flux(sun_temp, B_band))*invlogmagnbase-sun->BV_color;
    sun->distance = 0;
    sun->cenobj = sun;
    append_cel(sun);
    radio_silence = true;
    CatalogReader reader;
    reader.read_condensed_star_cat();
    for (int i = 0; cels[i]; ++i)
        if (cels[i] != sun && !strcmp(cels[i]->name, "Sun")) cels[i]->deleted = true;
    const auto solar = measure(sun);
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_Window* window = SDL_CreateWindow("Population audit", 0, 0, 1024, 900, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) throw std::runtime_error(SDL_GetError());
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = ImVec2(1024, 900);
    ImGui::GetIO().DeltaTime = 1.0f/60;
    ImGui_ImplOpenGL3_Init("#version 130");
    snapshot(sun, solar, prefix+"_solar.png", "Actual Solar catalog - Stellar Neighborhood and population statistics");
    FictionalGalaxy galaxy;
    galaxy.key = galaxy.code = "MW";
    galaxy.east = Point(1,0,0);
    galaxy.height_axis = Point(0,1,0);
    galaxy.south = Point(0,0,1);
    galaxy.origin_mly = Point(0,0,20);
    FictionalRegion region;
    region.center_ly = Point(25000,0,0);
    region.region_id = "237R";
    region.seed = argc > 2 ? std::stoull(argv[2]) : 7519;
    region.center_subtype = 4;
    const int center_index = ncelobjs;
    auto batch = generate_fictional_stars(galaxy, region);
    std::string message;
    if (!batch.install(message)) throw std::runtime_error(message);
    Star* center_star = static_cast<Star*>(cels[center_index]);
    const auto generated = measure(center_star);
    snapshot(center_star, generated, prefix+"_generated.png", prefix+" - Generated Stellar Neighborhood and population statistics");
    json census = {{"source", "catalogs/CNS5/cns5.dat"}, {"objects_by_radius_ly", json::object()}};
    std::ifstream census_file("catalogs/CNS5/cns5.dat");
    std::string row;
    int rows = 0;
    while (std::getline(census_file, row))
    {
        if (row.size() < 148) continue;
        const double parallax_mas = atof(row.substr(129, 19).c_str());
        if (!(parallax_mas > 0)) continue;
        ++rows;
        const double distance_ly = 1000/parallax_mas*parsec/light_year;
        for (int radius : {5, 10, 25, 50, 82})
        {
            const std::string key = std::to_string(radius);
            if (distance_ly <= radius)
                census["objects_by_radius_ly"][key] = census["objects_by_radius_ly"].value(key, 0)+1;
        }
    }
    census["rows_with_parallax"] = rows;
    census["scope"] = "All catalog objects, including brown dwarfs; Gaia G is not Johnson V.";
    const json report = {{"configuration", batch.provenance}, {"systems", batch.systems},
        {"cns5", census}, {"solar", solar}, {"generated", generated}, {"generation_seconds", batch.elapsed_seconds}};
    std::ofstream(prefix+".json") << report.dump(2);
    std::cout << report.dump(2) << std::endl;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
