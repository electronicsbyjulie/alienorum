#include "fictionalstars_ui.h"
#include "classes/fictionalstars.h"
#include "globals.h"
#include "housekeeping.h"
#include "loaders.h"
#include "gputex.h"
#include <algorithm>
#include <chrono>
#include <future>
#include <random>

using namespace alienorum;

namespace
{
    struct RegionDialog
    {
        bool open = false;
        int galaxy_index = -1;
        FictionalGalaxy galaxy;
        FictionalRegion region;
        FictionalBatch batch;
        std::future<FictionalBatch> worker;
        std::atomic<bool> cancel{false};
        std::atomic<int> progress{0};
        bool running = false, preview_valid = false;
        double radial = 0, azimuth_degrees = 0, height = 0;
        double longitude = 0, latitude = 0, distance = 20000;
        float image_zoom = 1;
        char filter[100] = {};
        std::string message;
        int last_center = -1;
        std::string last_center_id;
        GLuint image_texture = 0;

        void clear_preview()
        {
            preview_valid = false;
            batch = FictionalBatch();
            message.clear();
        }
        void update_center()
        {
            const double angle = azimuth_degrees*fiftyseventh;
            region.center_ly = Point(radial*cos(angle), height, radial*sin(angle));
            coordinate_fields();
            clear_preview();
        }
        void coordinate_fields()
        {
            radial = hypot(region.center_ly.x, region.center_ly.z);
            azimuth_degrees = atan2(region.center_ly.z, region.center_ly.x)*fiftyseven;
            height = region.center_ly.y;
            if (galaxy.milky_way)
            {
                Point ray = galaxy.world_offset(region.center_ly) + galaxy.origin_mly*(1e6*light_year);
                distance = ray.magnitude()/light_year;
                const double along = ray.x*galaxy.east.x + ray.y*galaxy.east.y + ray.z*galaxy.east.z;
                const double across = ray.x*galaxy.south.x + ray.y*galaxy.south.y + ray.z*galaxy.south.z;
                const double up = ray.x*galaxy.height_axis.x + ray.y*galaxy.height_axis.y + ray.z*galaxy.height_axis.z;
                longitude = atan2(across, along)*fiftyseven;
                latitude = atan2(up, hypot(along, across))*fiftyseven;
            }
        }
        void suggest()
        {
            std::mt19937_64 random(region.seed);
            const double unit = (random() >> 11)*0x1.0p-53;
            const bool historical = galaxy.milky_way && region.historical_annulus;
            const double inner = historical ? 7000*parsec/light_year : galaxy.advisory_inner();
            const double outer = historical ? 9000*parsec/light_year : galaxy.advisory_outer();
            const double r = inner + (outer-inner)*unit;
            const double angle = (random() >> 11)*0x1.0p-53 * 2*_pi;
            region.center_ly = Point(r*cos(angle), 0, r*sin(angle));
            coordinate_fields();
            clear_preview();
        }
        void choose(int index)
        {
            galaxy_index = index;
            galaxy = FictionalGalaxy::from_galaxy(*static_cast<Galaxy*>(cels[index]));
            image_zoom = 1;
            if (image_texture) glDeleteTextures(1, &image_texture);
            image_texture = 0;
            if (galaxy.image.available())
            {
                // Preview the same CPU field used by generation, with an ordinary display encoding.
                std::vector<unsigned char> pixels(galaxy.image.rgb.size());
                for (size_t i = 0; i < pixels.size(); ++i)
                {
                    const double c = galaxy.image.rgb[i];
                    const double srgb = c <= 0.0031308 ? c*12.92 : 1.055*pow(c, 1/2.4)-0.055;
                    pixels[i] = std::clamp(int(srgb*255), 0, 255);
                }
                glGenTextures(1, &image_texture);
                glBindTexture(GL_TEXTURE_2D, image_texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, galaxy.image.width, galaxy.image.height,
                    0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            }
            suggest();
        }
    } dialog;

    void draw_region_footer()
    {
        if (ImGui::Button("Visit Center"))
        {
            if (dialog.galaxy_index < 0 || !fictional_visit_center(dialog.galaxy, dialog.region))
                dialog.message = "Choose a location inside a galaxy first.";
        }
        if (ImGui::Button("Close")) dialog.open = false;
    }

    void draw_location_preview()
    {
        const auto& galaxy = dialog.galaxy;
        const bool panorama = galaxy.milky_way;
        const float available = ImGui::GetContentRegionAvail().x;
        const float width = std::max(220.0f, available - 16);
        const float height = panorama ? 180 : 260;
        ImGui::BeginChild("Region image", ImVec2(0, height + 24), true,
            ImGuiWindowFlags_HorizontalScrollbar);
        const float inner_width = std::max(100.0f, ImGui::GetContentRegionAvail().x);
        const float aspect = galaxy.image.available() ? float(galaxy.image.width)/galaxy.image.height : 1;
        const float base_width = panorama ? inner_width : std::min(inner_width, height*aspect);
        const ImVec2 size(base_width*dialog.image_zoom, base_width/aspect*dialog.image_zoom);
        if (size.x < inner_width) ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(inner_width-size.x)/2);
        const ImVec2 top = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("Galaxy position", size);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(top.x+size.x, top.y+size.y);
        draw->AddRectFilled(top, end, IM_COL32(8, 12, 20, 255));
        if (dialog.image_texture) draw->AddImage((ImTextureID)(intptr_t)dialog.image_texture, top, end);
        else
        {
            for (int band = 12; band > 0; --band)
            {
                const float r = band/12.0f;
                draw->AddEllipseFilled(ImVec2(top.x+size.x/2, top.y+size.y/2),
                    ImVec2(size.x*0.48f*r, size.y*0.48f*r), IM_COL32(35+8*(12-band), 38+7*(12-band), 50+5*(12-band), 255));
            }
        }
        if (ImGui::IsItemClicked())
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const double u = std::clamp(double((mouse.x-top.x)/size.x), 0.0, 1.0);
            const double v = std::clamp(double((mouse.y-top.y)/size.y), 0.0, 1.0);
            if (panorama)
            {
                Point ray = galaxy.image_ray(u, v);
                dialog.region.center_ly = galaxy.disk_offset(ray*(dialog.distance*light_year)
                    - galaxy.origin_mly*(1e6*light_year));
                dialog.longitude = atan2(ray.x*galaxy.south.x + ray.y*galaxy.south.y + ray.z*galaxy.south.z,
                    ray.x*galaxy.east.x + ray.y*galaxy.east.y + ray.z*galaxy.east.z)*fiftyseven;
                dialog.latitude = asin(std::clamp(ray.x*galaxy.height_axis.x + ray.y*galaxy.height_axis.y
                    + ray.z*galaxy.height_axis.z, -1.0, 1.0))*fiftyseven;
            }
            else dialog.region.center_ly = Point((u-0.5)*2*galaxy.radius_ly, dialog.height,
                (v-0.5)*2*galaxy.radius_ly);
            dialog.coordinate_fields();
            dialog.clear_preview();
        }
        if (!panorama)
        {
            const double inner = galaxy.advisory_inner()/galaxy.radius_ly/2;
            const double outer = galaxy.advisory_outer()/galaxy.radius_ly/2;
            for (double radial : {inner, outer})
                draw->AddEllipse(ImVec2(top.x+size.x/2, top.y+size.y/2),
                    ImVec2(size.x*radial, size.y*radial), IM_COL32(100, 220, 150, 140), 0, 80, 2);
            const ImVec2 position(top.x+size.x*(0.5 + dialog.region.center_ly.x/(2*galaxy.radius_ly)),
                top.y+size.y*(0.5 + dialog.region.center_ly.z/(2*galaxy.radius_ly)));
            draw->AddEllipse(position, ImVec2(size.x*dialog.region.radius_ly/(2*galaxy.radius_ly),
                size.y*dialog.region.radius_ly/(2*galaxy.radius_ly)), IM_COL32(255, 230, 110, 240), 0, 32, 2);
            draw->AddLine(ImVec2(position.x-8, position.y), ImVec2(position.x+8, position.y), IM_COL32_WHITE);
            draw->AddLine(ImVec2(position.x, position.y-8), ImVec2(position.x, position.y+8), IM_COL32_WHITE);
        }
        ImGui::EndChild();
        if (ImGui::SliderFloat("Image zoom", &dialog.image_zoom, 1, 5, "%.1fx")) viewchanged = true;

        // Fixed galaxy coordinates: top-down and side views make the chosen depth explicit.
        const ImVec2 top2 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("Location diagram", ImVec2(width, 92));
        draw = ImGui::GetWindowDrawList();
        const float half = width/2;
        const ImVec2 center_top(top2.x+half/2, top2.y+46), center_side(top2.x+half*1.5f, top2.y+46);
        const float rx = half*0.45f, ry = 38;
        draw->AddEllipse(center_top, ImVec2(rx, ry), IM_COL32(100, 160, 200, 180));
        draw->AddEllipse(center_side, ImVec2(rx, galaxy.spheroid ? ry : 7), IM_COL32(100, 160, 200, 180));
        const auto point_top = [&](Point p)
        {
            return ImVec2(center_top.x + rx*p.x/galaxy.radius_ly, center_top.y + ry*p.z/galaxy.radius_ly);
        };
        const ImVec2 chosen = point_top(dialog.region.center_ly);
        draw->AddCircleFilled(chosen, 3, IM_COL32(255, 230, 100, 255));
        draw->AddCircleFilled(ImVec2(center_side.x+rx*dialog.region.center_ly.x/galaxy.radius_ly,
            center_side.y-ry*dialog.region.center_ly.y/(galaxy.radius_ly*0.2)), 3, IM_COL32(255, 230, 100, 255));
        if (panorama)
        {
            const Point sun = galaxy.disk_offset(galaxy.origin_mly*(-1e6*light_year));
            const ImVec2 observer = point_top(sun);
            draw->AddCircleFilled(observer, 3, IM_COL32_WHITE);
            draw->AddLine(observer, chosen, IM_COL32(230, 200, 120, 220));
        }
        const bool historical = galaxy.milky_way && dialog.region.historical_annulus;
        const double advisory_inner = historical ? 7000*parsec/light_year : galaxy.advisory_inner();
        const double advisory_outer = historical ? 9000*parsec/light_year : galaxy.advisory_outer();
        for (double radius : {advisory_inner, advisory_outer})
            draw->AddEllipse(center_top, ImVec2(rx*radius/galaxy.radius_ly, ry*radius/galaxy.radius_ly),
                IM_COL32(90, 210, 140, 140), 0, 48);
        draw->AddText(ImVec2(top2.x+4, top2.y+2), IM_COL32_WHITE, "Top view");
        draw->AddText(ImVec2(top2.x+half+4, top2.y+2), IM_COL32_WHITE, "Side view");
    }
}

bool fictional_visit_center(const FictionalGalaxy& galaxy, const FictionalRegion& region)
{
    if (!galaxy.inside(region.center_ly) || !std::isfinite(region.center_ly.squared_magnitude())) return false;
    CelestialLocation destination;
    destination.galactic_center = galaxy.origin_mly;
    destination.system_center = galaxy.world_offset(region.center_ly);
    whereami = -1;
    here = destination;
    for (int i = 0; cels && cels[i]; ++i)
    {
        if (cels[i]->deleted || cels[i]->typeclass() != class_star) continue;
        Star* star = static_cast<Star*>(cels[i]);
        if (star->fictional_origin.is_object() && star->fictional_origin.value("anchor", false)
            && star->location.distance_to(destination) < light_year*1e-6)
        {
            whereami = i;
            mycenobj = cels[i];
            break;
        }
    }
    velocity = center;
    spin = 0;
    viewer_locale.clear();
    view_mode = vm_spaceship;
    set_viewer_location_and_plane();
    selected = trackidx = -1;
    zoom = 1;
    global_brightness = default_brightness;
    viewchanged = true;
    refresh_star_visibilities();
    return true;
}

int fictional_find_galaxy(const std::string& query)
{
    const std::string key = stellar_alias_key(query);
    if (key.empty()) return -1;
    int partial = -1;
    for (int i = 0; cels && cels[i]; ++i)
    {
        if (cels[i]->deleted || cels[i]->typeclass() != class_galaxy) continue;
        const Galaxy* galaxy = static_cast<Galaxy*>(cels[i]);
        const std::string name = stellar_alias_key(galaxy->name);
        const std::string original = stellar_alias_key(galaxy->origname);
        const std::string pgc = galaxy->PGC ? "PGC"+std::to_string(galaxy->PGC) : "";
        if (key == name || key == original || key == pgc) return i;
        if (partial < 0 && (name.find(key) != std::string::npos || original.find(key) != std::string::npos)) partial = i;
    }
    return partial;
}

int fictional_default_galaxy()
{
    const auto valid = [](int index)
    {
        return cels && index >= 0 && index < ncelobjs && cels[index] && !cels[index]->deleted
            && cels[index]->typeclass() == class_galaxy;
    };
    if (valid(whereami)) return whereami;
    CelestialLocation vantage = whereami >= 0 && whereami < ncelobjs && cels[whereami]
        ? cels[whereami]->location : here;
    int nearest = -1, reference = -1;
    double nearest_distance = 1e300;
    for (int i = 0; cels && cels[i]; ++i)
    {
        if (!valid(i)) continue;
        Galaxy* galaxy = static_cast<Galaxy*>(cels[i]);
        if (galaxy->reference_galaxy) reference = i;
        if (!galaxy->reference_galaxy && !galaxy->distance_known &&
            !galaxy->location.galactic_center.squared_magnitude() && !galaxy->location.system_center.squared_magnitude()) continue;
        const double radius = galaxy->volumetric_mean_radius > 0 ? galaxy->volumetric_mean_radius : 50000*light_year;
        const double distance = vantage.distance_to(galaxy->location);
        if (distance <= radius && distance < nearest_distance)
        {
            nearest = i;
            nearest_distance = distance;
        }
    }
    if (nearest >= 0) return nearest;
    if (valid(inside_galaxy_idx)) return inside_galaxy_idx;
    if (valid(selected)) return selected;
    return reference;
}

void open_fictional_region()
{
    if (dialog.running)
    {
        dialog.open = true;
        return;
    }
    dialog.open = true;
    dialog.region.region_id = fictional_region_id();
    dialog.region.identity = fictional_population_id();
    std::random_device random;
    dialog.region.seed = (uint64_t(random()) << 32) ^ random();
    dialog.region.center_subtype = random()%15;
    const int target = fictional_default_galaxy();
    if (target >= 0) dialog.choose(target);
}

void close_fictional_region()
{
    dialog.open = false;
    dialog.cancel.store(true);
    if (dialog.worker.valid()) dialog.worker.wait();
    dialog.batch = FictionalBatch();
    dialog.running = false;
    dialog.preview_valid = false;
    dialog.galaxy_index = -1;
    dialog.last_center = -1;
    if (dialog.image_texture) glDeleteTextures(1, &dialog.image_texture);
    dialog.image_texture = 0;
}

void draw_fictional_region()
{
    if (dialog.running && dialog.worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            dialog.batch = dialog.worker.get();
            dialog.preview_valid = dialog.batch.error.empty() && !dialog.batch.stars.empty();
            dialog.message = dialog.batch.error;
        }
        catch (const std::exception& error) { dialog.message = error.what(); }
        dialog.running = false;
    }
    if (!dialog.open)
    {
        if (dialog.running) dialog.cancel.store(true);
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(0, std::min(900.0f, display.y-40)), ImGuiCond_Always);
    ImGui::Begin("Populate Region", &dialog.open);
    editing = true;
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) is_mouse_over_window = true;
    const float input_width = ImGui::CalcTextSize("000000000000000000000000").x;
    const float natural_width = std::max(input_width+ImGui::CalcTextSize("Star count (including companions)").x,
        ImGui::CalcTextSize("Generate Preview  Cancel Preview  Add Stars  Visit Center  Close").x)
        + 10*ImGui::GetStyle().FramePadding.x + ImGui::GetStyle().ScrollbarSize;
    ImGui::BeginChild("Region controls", ImVec2(std::min(natural_width, display.x-60), -2*ImGui::GetFrameHeightWithSpacing()), false);
    ImGui::PushItemWidth(input_width);
    ImGui::BeginDisabled(dialog.running);
    bool search = ImGui::InputText("Find galaxy", dialog.filter, sizeof(dialog.filter), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    search |= ImGui::Button("Search");
    if (search)
    {
        const int match = fictional_find_galaxy(dialog.filter);
        if (match >= 0) dialog.choose(match);
        else dialog.message = "No matching galaxy found.";
    }
    if (ImGui::BeginCombo("Galaxy", dialog.galaxy.name.empty() ? "Choose a galaxy" : dialog.galaxy.name.c_str()))
    {
        const std::string filter = stellar_alias_key(dialog.filter);
        std::vector<int> matches;
        for (int i = 0; cels[i]; ++i)
            if (!cels[i]->deleted && cels[i]->typeclass() == class_galaxy
                && (filter.empty() || stellar_alias_key(cels[i]->name).find(filter) != std::string::npos)) matches.push_back(i);
        ImGuiListClipper clipper;
        clipper.Begin(matches.size());
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                if (ImGui::Selectable(cels[matches[row]]->name, dialog.galaxy_index == matches[row])) dialog.choose(matches[row]);
        ImGui::EndCombo();
    }
    if (dialog.galaxy_index < 0)
    {
        ImGui::TextWrapped("Choose a galaxy to place a new fictional stellar neighborhood.");
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::EndChild();
        draw_region_footer();
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s", dialog.galaxy.image.available() ? (dialog.galaxy.milky_way
        ? "Click the Milky Way band to choose a direction, then adjust distance. Top and side views show the location."
        : "Click the image to choose a location. Green boundaries suggest a habitable region.")
        : "No image is available for this galaxy. Using a fictional model based on galaxy shape.");
    draw_location_preview();
    if (dialog.galaxy.milky_way)
    {
        bool changed = ImGui::InputDouble("Galactic longitude (deg)", &dialog.longitude, 1, 10, "%.2f");
        changed |= ImGui::InputDouble("Galactic latitude (deg)", &dialog.latitude, 1, 10, "%.2f");
        changed |= ImGui::InputDouble("Distance from Sun (ly)", &dialog.distance, 100, 1000, "%.1f");
        if (changed)
        {
            dialog.latitude = std::clamp(dialog.latitude, -90.0, 90.0);
            dialog.distance = std::max(1.0, dialog.distance);
            dialog.region.center_ly = dialog.galaxy.from_milky_ray(dialog.longitude*fiftyseventh,
                dialog.latitude*fiftyseventh, dialog.distance);
            dialog.coordinate_fields();
            dialog.clear_preview();
        }
        if (ImGui::Checkbox("Historical 7-9 kpc annulus", &dialog.region.historical_annulus)) dialog.clear_preview();
    }
    bool changed = ImGui::InputDouble("Galactocentric radius (ly)", &dialog.radial, 100, 1000, "%.1f");
    changed |= ImGui::InputDouble("Galactocentric azimuth (deg)", &dialog.azimuth_degrees, 1, 10, "%.2f");
    changed |= ImGui::InputDouble("Height above galaxy plane (ly)", &dialog.height, 10, 100, "%.1f");
    if (changed) dialog.update_center();
    ImGui::TextWrapped("Habitable region: an advisory model, not a measured boundary. You may choose any location inside the galaxy.");
    if (ImGui::Button("Suggest Location"))
    {
        ++dialog.region.seed;
        dialog.suggest();
    }
    changed = ImGui::InputDouble("Region radius (ly)", &dialog.region.radius_ly, 10, 100, "%.0f");
    changed |= ImGui::InputInt("Star count (including companions)", &dialog.region.star_count, 100, 1000);
    ImGui::TextUnformatted("Minimum spacing varies between systems: 1-5 light years.");
    const int available = std::max(0, MAX_CELOBJS-ncelobjs-1);
    dialog.region.star_count = std::clamp(dialog.region.star_count, 1, std::max(1, available));
    dialog.region.radius_ly = std::clamp(dialog.region.radius_ly, 1.0, 1000.0);
    changed |= ImGui::SliderInt("Center star", &dialog.region.center_subtype, 0, 14,
        fictional_center_type(dialog.region.center_subtype));
    changed |= ImGui::Checkbox("Include fictional open clusters", &dialog.region.clusters);
    changed |= ImGui::InputScalar("Random seed", ImGuiDataType_U64, &dialog.region.seed);
    if (changed) dialog.clear_preview();
    ImGui::Text("Region: %s  |  Available object slots: %d", dialog.region.region_id.c_str(), available);
    ImGui::Text("Star storage estimate: %.1f MiB plus metadata and indices", sizeof(Star)*dialog.region.star_count/(1024.0*1024));
    const bool inside = dialog.galaxy.inside(dialog.region.center_ly);
    if (!inside) ImGui::TextWrapped("The center is outside the modeled galaxy. Move the crosshair or choose Suggest Location.");
    if (dialog.region.region_id.empty()) ImGui::TextUnformatted("No unused sector codes remain in this universe.");
    ImGui::BeginDisabled(!inside || available < 1 || dialog.region.region_id.empty());
    if (ImGui::Button("Generate Preview"))
    {
        dialog.clear_preview();
        dialog.cancel.store(false);
        dialog.progress.store(0);
        const FictionalGalaxy galaxy = dialog.galaxy;
        dialog.region.first_number = fictional_next_number();
        const FictionalRegion region = dialog.region;
        dialog.worker = std::async(std::launch::async, [galaxy, region]()
        {
            return generate_fictional_stars(galaxy, region, &dialog.cancel, &dialog.progress);
        });
        dialog.running = true;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (dialog.running)
    {
        ImGui::ProgressBar(float(dialog.progress.load())/dialog.region.star_count);
        if (ImGui::Button("Cancel Preview")) dialog.cancel.store(true);
    }
    if (dialog.preview_valid)
    {
        ImGui::Text("Preview: %zu stars, %d systems, %d clusters; %.2f seconds", dialog.batch.stars.size(),
            dialog.batch.systems, dialog.batch.clusters, dialog.batch.elapsed_seconds);
        ImGui::TextWrapped("Within 100 ly: %d  |  Magnitude 6.5 or brighter from center: %d", dialog.batch.nearby, dialog.batch.naked_eye);
        ImGui::Text("Estimated JSON size: %.1f MiB", dialog.batch.estimated_save_bytes/(1024.0*1024));
        const ImVec2 preview_top = ImGui::GetCursorScreenPos();
        const float preview_width = std::min(320.0f, ImGui::GetContentRegionAvail().x);
        ImGui::InvisibleButton("Population preview", ImVec2(preview_width, 180));
        ImDrawList* preview_draw = ImGui::GetWindowDrawList();
        preview_draw->AddRectFilled(preview_top, ImVec2(preview_top.x+preview_width, preview_top.y+180),
            IM_COL32(5, 8, 16, 255));
        const Point center_offset = dialog.galaxy.world_offset(dialog.region.center_ly);
        for (size_t i = 0; i < dialog.batch.stars.size(); i += std::max(size_t(1), dialog.batch.stars.size()/2500))
        {
            const Star& star = *dialog.batch.stars[i];
            const Point offset = dialog.galaxy.disk_offset(star.location.system_center-center_offset);
            const ImVec2 point(preview_top.x+preview_width*(0.5+offset.x/(2*dialog.region.radius_ly)),
                preview_top.y+180*(0.5+offset.z/(2*dialog.region.radius_ly)));
            const ImU32 color = star.temperature > 7500 ? IM_COL32(160, 190, 255, 190)
                : star.temperature < 4500 ? IM_COL32(255, 175, 120, 190) : IM_COL32(255, 245, 215, 190);
            preview_draw->AddCircleFilled(point, 1.1f, color, 5);
        }
        preview_draw->AddText(ImVec2(preview_top.x+6, preview_top.y+6), IM_COL32_WHITE, "Population preview (top view)");
        if (ImGui::Button("Add Stars"))
        {
            const int center_index = ncelobjs;
            if (dialog.batch.install(dialog.message))
            {
                dialog.last_center = center_index;
                Star* anchor = static_cast<Star*>(cels[center_index]);
                dialog.last_center_id = anchor->fictional_origin.at("id");
                anchor->is_faved = true;
                favestars.push_back(anchor);
                set_center_objects();
                refresh_star_visibilities();
                save_user_json();
                dialog.preview_valid = false;
                dialog.region.region_id = fictional_region_id();
                dialog.region.identity = fictional_population_id();
            }
        }
    }
    if (!dialog.message.empty()) ImGui::TextWrapped("%s", dialog.message.c_str());
    ImGui::PopItemWidth();
    ImGui::EndChild();
    draw_region_footer();
    ImGui::End();
}
