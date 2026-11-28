#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <gtest/gtest.h>
#include "imgui_internal.h"
#include "universe_fixture.h"
#include "../classes/satellite.h"
#include "../housekeeping.h"
#include "../inputs.h"

namespace
{
    namespace fs = std::filesystem;
    const fs::path source_root = fs::path(__FILE__).parent_path().parent_path();

    size_t literal_end(std::string_view code, size_t first)
    {
        for (size_t i = first + 1; i < code.size(); ++i)
        {
            if (code[i] == '\\') ++i;
            else if (code[i] == code[first]) return i + 1;
        }
        return code.size();
    }

    std::string read_code(const fs::path& path)
    {
        std::ifstream input(path);
        EXPECT_TRUE(input.is_open()) << path;
        const std::string code{std::istreambuf_iterator<char>(input), {}};
        std::string result;
        for (size_t i = 0; i < code.size();)
        {
            if (code.compare(i, 2, "//") == 0)
            {
                i = code.find('\n', i + 2);
            }
            else if (code.compare(i, 2, "/*") == 0)
            {
                const auto end = code.find("*/", i + 2);
                i = end == std::string::npos ? code.size() : end + 2;
            }
            else if (code[i] == '"' || code[i] == '\'')
            {
                const size_t end = literal_end(code, i);
                result.append(code, i, end - i);
                i = end;
            }
            else
            {
                if (!std::isspace(static_cast<unsigned char>(code[i]))) result += code[i];
                ++i;
            }
        }
        return result;
    }

    // Stop at the requested delimiter outside nested expressions and string literals.
    size_t delimiter(std::string_view code, size_t first, std::string_view stops)
    {
        int depth = 0;
        for (size_t i = first; i < code.size(); ++i)
        {
            const char c = code[i];
            if (c == '"' || c == '\'') i = literal_end(code, i) - 1;
            else if (depth == 0 && stops.find(c) != std::string_view::npos) return i;
            else if (c == '(' || c == '[' || c == '{') ++depth;
            else if (c == ')' || c == ']' || c == '}') --depth;
        }
        return std::string_view::npos;
    }

    std::string_view binding_block(std::string_view code, std::string_view begins, std::string_view ends)
    {
        const auto first = code.find(begins);
        EXPECT_NE(first, std::string_view::npos) << begins;
        if (first == std::string_view::npos) return {};
        auto content = first;
        if (ends == "}")
        {
            const auto open = delimiter(code, first, "{");
            EXPECT_NE(open, std::string_view::npos) << begins;
            if (open == std::string_view::npos) return {};
            content = open + 1;
        }
        const auto last = delimiter(code, content, ends);
        EXPECT_NE(last, std::string_view::npos) << begins;
        if (last == std::string_view::npos) return {};
        return code.substr(first, last - first + 1);
    }

    std::string menu_shortcuts(std::string_view menu)
    {
        std::vector<std::string_view> entries;
        constexpr std::string_view call = "ImGui::MenuItem(";
        for (auto start = menu.find(call); start != std::string_view::npos; start = menu.find(call, start + call.size()))
        {
            const auto label = start + call.size();
            const auto comma = delimiter(menu, label, ",");
            if (comma == std::string_view::npos) break;
            const auto end = delimiter(menu, comma + 1, ",)");
            if (end == std::string_view::npos) break;
            if (menu.substr(comma + 1, end - comma - 1) == "nullptr") continue;
            entries.push_back(menu.substr(label, end - label));
        }
        std::sort(entries.begin(), entries.end());
        std::string result;
        for (const auto entry : entries)
        {
            result.append(entry);
            result += '\n';
        }
        return result;
    }
}

TEST(KeyboardShortcutCompatibility, PreservesPublishedMenuLabelsAndKeys)
{
    const std::string inputs = read_code(source_root / "inputs.cpp");
    const auto standard_menu = menu_shortcuts(binding_block(inputs, "voidshow_menu(", "}"));
    constexpr std::string_view expected_menu = R"keys("Accelerate Backward","Home"
"Accelerate Forward","End"
"Add Asteroid...","."
"Add Comet...",";"
"Add Object...","Shift+A"
"Add Satellite...","^"
"Add to Favorite Stars","Ctrl+D"
"Advance One Century","Z"
"Advance One Day","D"
"Advance One Decade","Ctrl+Y"
"Advance One Hour","H"
"Advance One Millennium","Ctrl+Z"
"Advance One Minute","I"
"Advance One Month","M"
"Advance One Year","Y"
"Alert on Rise","["
"Alert on Set","]"
"Azimuth 180deg","8"
"Azimuth 270deg","4"
"Azimuth 90deg","6"
"Azimuth zero","5"
"Bayer Designations","Shift+F"
"Binary Systems","2"
"Brightest Stars","A"
"Clear Selection","Shift+S"
"Clear Tracking","Shift+T"
"Clouds","Ctrl+C"
"Constellations","C"
"Coordinate Axes","|"
"Decrease Brightness","Shift+B"
"Decrease Gamma","~"
"Default Annotations","1"
"Delete Object","Del"
"Dynamically Update Surface Ice","Ctrl+Shift+I"
"Earth-Up (for satellites)","Shift+J"
"Ecliptic","Ctrl+E"
"Edit Object...","Shift+E"
"Favorite Star Labels","Ctrl+Shift+D"
"Favorite Stars","F7"
"Find Shore","Ctrl+F"
"Flamsteed Numbers","F"
"Full Stop","X"
"Fullscreen","F11"
"Galactic","Ctrl+G"
"Galaxy Band","Shift+K"
"Galaxy Labels","K"
"Go and Use Local Timesteps","Ctrl+O"
"Go to Object","O"
"Gould Uranometria Numbers","Shift+G"
"Hide Mouse Cursor",","
"ICRF","Ctrl+I"
"Import SSC Add-On...","F6"
"Increase Brightness","B"
"Increase Gamma","`"
"Info Panel","N"
"Intrinsically Brightest","V"
"Load Universe...","F4"
"Load universe.json","F2"
"Local System Labels","P"
"Local System Objects","Shift+V"
"Local Timestep","F10"
"Local","Ctrl+L"
"Loosen Threshold","<"
"Nearest Stars","Shift+N"
"Next Theme","3"
"Orbits","Shift+O"
"Populate Region...","Ctrl+P"
"Pretty Star Colors","Ctrl+J"
"Previous Theme","#"
"RA/Dec Grid","G"
"Realism Mode (no annotations)","!"
"Red Light Mode","Shift+R"
"Regenerate Clouds","Ctrl+Shift+C"
"Reload Constellations","F5"
"Reset Brightness and Zoom","%"
"Return Home","R"
"Return to Present","@"
"Rewind One Century","Shift+Z"
"Rewind One Day","Shift+D"
"Rewind One Decade","Ctrl+Shift+Y"
"Rewind One Hour","Shift+H"
"Rewind One Millennium","Ctrl+Shift+Z"
"Rewind One Minute","Shift+I"
"Rewind One Month","Shift+M"
"Rewind One Year","Shift+Y"
"Satellites","J"
"Save Snapshot","F12"
"Save Vantage Constellations","Shift+F5"
"Save universe files","U"
"Search...","F3"
"Short Labels","Ctrl+S"
"Slow Down","-"
"Spaceflight/Speed Up","+"
"Star Labels","L"
"Stars with Known Poles","Shift+X"
"Stars with Planets in HZ","Shift+L"
"Stars with Planets","Shift+P"
"Status Panel","S"
"Steer Down","Down Arrow"
"Steer Left","Left Arrow"
"Steer Right","Right Arrow"
"Steer Up","Up Arrow"
"Stellar Neighborhood","0"
"Sunlike Stars","Shift+C"
"System Explorer","E"
"Terrain","Ctrl+T"
"Tighten Threshold",">"
"Track Selected","T"
"Unconfirmed and Controversial Planets","F8"
"Warp Speed","W"
"White Background Mode","Shift+W"
"Write User Settings","Shift+U"
"Zoom In","*"
"Zoom Out","/"
vmtext[0],"&"
vmtext[1],"_"
vmtext[3],"\\"
vmtext[4],"Ctrl+V"
vmtext[mode],next_surface_map_view(view_mode)==mode?"$":nullptr
vmtext[vm_model],":"
)keys";
    EXPECT_EQ(std::string_view(standard_menu), expected_menu) << "F1 menu labels and keys";
}

class KeyboardShortcutEvents : public UniverseFixture
{
protected:
    void SetUp() override
    {
        UniverseFixture::SetUp();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800, 600);
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }

    void TearDown() override
    {
        selected = trackidx = -1;
        delete_the_universe();
        ImGui::DestroyContext();
        UniverseFixture::TearDown();
    }

    static void frame(bool dispatch)
    {
        ImGui::NewFrame();
        if (dispatch) process_keyboard_commands(ImGui::GetIO());
        ImGui::EndFrame();
    }

    static void type(char key)
    {
        ImGui::GetIO().AddInputCharacter(key);
        frame(true);
    }

    static void press(ImGuiKey key, bool control = false, bool shift = false)
    {
        auto& io = ImGui::GetIO();
        if (control) io.AddKeyEvent(ImGuiMod_Ctrl, true);
        if (shift) io.AddKeyEvent(ImGuiMod_Shift, true);
        io.AddKeyEvent(key, true);
        frame(true);
        io.AddKeyEvent(key, false);
        if (shift) io.AddKeyEvent(ImGuiMod_Shift, false);
        if (control) io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame(false);
    }
};

TEST_F(KeyboardShortcutEvents, ImperialUnitsShortcutRequiresAllThreeModifiers)
{
    const bool saved = use_imperial_units;
    use_imperial_units = false;
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiMod_Shift, true);
    press(ImGuiKey_M);
    EXPECT_FALSE(use_imperial_units);

    io.AddKeyEvent(ImGuiMod_Alt, true);
    io.AddKeyEvent(ImGuiKey_M, true);
    frame(true);
    EXPECT_TRUE(use_imperial_units);
    for (int i = 0; i < 30; ++i) frame(true);
    EXPECT_TRUE(use_imperial_units);
    io.AddKeyEvent(ImGuiKey_M, false);
    frame(true);
    press(ImGuiKey_M);
    EXPECT_FALSE(use_imperial_units);
    use_imperial_units = saved;
}

TEST_F(KeyboardShortcutEvents, SnapshotShortcutsRequireTheirExactModifiers)
{
    const auto saved = std::make_tuple(take_snapshot, snapshot_without_interface);
    take_snapshot = false;
    snapshot_without_interface = false;

    press(ImGuiKey_F12, true);
    EXPECT_FALSE(take_snapshot);
    press(ImGuiKey_F12, true, true);
    EXPECT_FALSE(take_snapshot);

    press(ImGuiKey_F12, false, true);
    EXPECT_TRUE(take_snapshot);
    EXPECT_TRUE(snapshot_without_interface);

    take_snapshot = false;
    press(ImGuiKey_F12);
    EXPECT_TRUE(take_snapshot);
    EXPECT_FALSE(snapshot_without_interface);

    std::tie(take_snapshot, snapshot_without_interface) = saved;
}

TEST_F(KeyboardShortcutEvents, ModifiedFunctionKeysLeaveUnmodifiedCommandsIdle)
{
    const auto saved = std::make_tuple(show_tentative_exoplanets, local_tmstep);
    press(ImGuiKey_F8, true);
    press(ImGuiKey_F8, false, true);
    press(ImGuiKey_F10, true);
    press(ImGuiKey_F10, false, true);
    EXPECT_EQ(std::tie(show_tentative_exoplanets, local_tmstep), saved);

    press(ImGuiKey_F10);
    EXPECT_NE(local_tmstep, std::get<1>(saved));
    std::tie(show_tentative_exoplanets, local_tmstep) = saved;
}

TEST_F(KeyboardShortcutEvents, ViewMenuShowsImperialUnitsBelowTheThemeControls)
{
    const auto prior_flags = std::make_tuple(use_imperial_units, mouse_over_menu, menu_clicked);
    use_imperial_units = false;
    ImGui::NewFrame();
    if (ImGui::BeginMainMenuBar())
    {
        ImGui::OpenPopup("View");
        ImGui::EndMainMenuBar();
    }
    show_menu();
    ImGui::EndFrame();

    ImGui::NewFrame();
    ImGui::LogToBuffer();
    show_menu();
    const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
    ImGui::LogFinish();
    const auto theme = text.find("Next Theme"), units = text.find("Imperial Units");
    EXPECT_NE(theme, std::string::npos) << text;
    EXPECT_NE(units, std::string::npos) << text;
    EXPECT_LT(theme, units);
    for (const char* part : {"Ctrl", "Alt", "Shift", "+M"})
        EXPECT_NE(text.find(part, units), std::string::npos) << text;
    const auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
    const ImGuiWindow* popup = popups.empty() ? nullptr : popups.back().Window;
    EXPECT_NE(popup, nullptr);
    ImGui::EndFrame();
    if (popup)
    {
        auto& io = ImGui::GetIO();
        const float item_y = popup->Pos.y + popup->Size.y - ImGui::GetStyle().WindowPadding.y - ImGui::GetFontSize() / 2;
        io.AddMousePosEvent(popup->Pos.x + 25, item_y);
        io.AddMouseButtonEvent(0, true);
        ImGui::NewFrame();
        show_menu();
        ImGui::EndFrame();
        io.AddMouseButtonEvent(0, false);
        ImGui::NewFrame();
        show_menu();
        EXPECT_TRUE(menu_clicked);
        EXPECT_TRUE(use_imperial_units);
        ImGui::EndFrame();
    }
    std::tie(use_imperial_units, mouse_over_menu, menu_clicked) = prior_flags;
}

TEST_F(KeyboardShortcutEvents, WalkingFollowsChangesInGradeWithGravityDefiningUp)
{
    class RampMap : public Map
    {
        public:
        static unsigned long columns() { return 3600; }

        RampMap()
        {
            image_width = columns();
            image_height = 2;
            allocated = image_width * image_height;
            lon_scale = image_width / (2 * _pi);
            lat_scale = image_height / _pi;
            red_data = new unsigned char[allocated]{};
            green_data = new unsigned char[allocated]{};
            blue_data = new unsigned char[allocated]{};
            bump_data = new double[allocated]{};
            for (unsigned long row = 0; row < image_height; ++row)
            {
                const double heights[]{0, 5, 10, 15, 15, 15, 10, 5, 0};
                std::copy(std::begin(heights), std::end(heights),
                    bump_data + row * image_width + image_width / 2);
            }
            touch_gen();
        }
    };

    auto state = std::tie(view_mode, whereami, trackidx, viewer_lat, viewer_lon, frame_dur, azimuth,
        altitude, here, azimuth_correction, npaz, save_viewer_latlon, viewchanged, editing);
    const auto saved = std::apply([](auto&... values) { return std::make_tuple(values...); }, state);
    auto* world = make_planet(make_star());
    const double cell = 2 * _pi / RampMap::columns();
    world->volumetric_mean_radius = 10 / cell;
    world->sidereal_rotational_period = 86400;
    auto* ramp = new RampMap();
    view_mode = vm_horizon;
    whereami = world->seqno;
    trackidx = -1;
    viewer_lat = 0;
    viewer_lon = -cell;
    frame_dur = .25;
    azimuth = half_pi;
    constexpr double initial_altitude = .1;
    altitude = initial_altitude;

    // An ordinary frame covers one metre, even with no surface map.
    press(ImGuiKey_End);
    EXPECT_NEAR(viewer_lon, -.9 * cell, 1e-12);
    EXPECT_DOUBLE_EQ(altitude, initial_altitude);
    viewer_lon = -cell;
    world->surf_map = ramp;

    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_LeftShift, true);
    io.AddKeyEvent(ImGuiKey_End, true);
    frame(true);
    EXPECT_NEAR(viewer_lon, 0, 1e-12);
    EXPECT_GT(altitude, initial_altitude);
    frame(true);
    EXPECT_NEAR(viewer_lon, cell, 1e-12);
    const double uphill_altitude = altitude;
    EXPECT_NEAR(uphill_altitude, initial_altitude + atan(.5), .002);
    set_viewer_location_and_plane();
    const Point radial = here.local_position - world->location.local_position;
    const Point up = to_viewer_plane(radial * (1 / radial.magnitude()));
    EXPECT_NEAR(up.x, 0, 1e-10);
    EXPECT_NEAR(up.y, 1, 1e-10);
    EXPECT_NEAR(up.z, 0, 1e-10);
    frame(true);
    EXPECT_NEAR(altitude, uphill_altitude, .002);
    frame(true);
    EXPECT_LT(altitude, uphill_altitude);
    io.AddKeyEvent(ImGuiKey_End, false);
    frame(true);
    const double crest_altitude = altitude;
    frame(true);
    EXPECT_DOUBLE_EQ(altitude, crest_altitude);

    press(ImGuiKey_Home);
    EXPECT_NEAR(viewer_lon, 2 * cell, 1e-12);
    EXPECT_GT(altitude, crest_altitude);
    io.AddKeyEvent(ImGuiKey_LeftShift, false);
    frame(true);

    azimuth = 0;
    altitude = initial_altitude;
    press(ImGuiKey_End);
    EXPECT_NEAR(viewer_lat, .1 * cell, 1e-12);
    EXPECT_NEAR(altitude, initial_altitude, 1e-10);

    viewer_lat = 0;
    viewer_lon = 4 * cell;
    azimuth = half_pi;
    io.AddKeyEvent(ImGuiKey_LeftShift, true);
    io.AddKeyEvent(ImGuiKey_End, true);
    for (int i = 0; i < 3; ++i) frame(true);
    EXPECT_NEAR(viewer_lon, 7 * cell, 1e-12);
    EXPECT_NEAR(altitude, initial_altitude - atan(.5), .002);
    for (int i = 0; i < 2; ++i) frame(true);
    EXPECT_NEAR(viewer_lon, 9 * cell, 1e-12);
    EXPECT_NEAR(altitude, initial_altitude, 1e-10);
    state = saved;
}

TEST_F(KeyboardShortcutEvents, BracketsExclusivelyToggleRiseAndSetAlerts)
{
    auto* tracked = make_star("Tracked object");
    auto* target = make_star("Selected object");
    selected = target->seqno;
    trackidx = tracked->seqno;
    target->has_prev_horizon_alt = true;
    type('[');
    EXPECT_TRUE(target->alert_rise);
    EXPECT_FALSE(target->alert_set);
    EXPECT_FALSE(target->has_prev_horizon_alt);
    target->has_prev_horizon_alt = true;
    type(']');
    EXPECT_TRUE(target->alert_rise);
    EXPECT_TRUE(target->alert_set);
    EXPECT_FALSE(target->has_prev_horizon_alt);
    EXPECT_FALSE(tracked->alert_rise);
    EXPECT_FALSE(tracked->alert_set);

    const bool show_tentative = show_tentative_exoplanets;
    press(ImGuiKey_F8);
    EXPECT_NE(show_tentative_exoplanets, show_tentative);
    show_tentative_exoplanets = show_tentative;
    EXPECT_TRUE(target->alert_rise);
    EXPECT_TRUE(target->alert_set);
    press(ImGuiKey_F9);
    EXPECT_TRUE(target->alert_rise);
    EXPECT_TRUE(target->alert_set);
    press(ImGuiKey_F11, false, true);
    EXPECT_EQ(show_tentative_exoplanets, show_tentative);
    EXPECT_TRUE(target->alert_rise);
    EXPECT_TRUE(target->alert_set);
    press(ImGuiKey_R, true);
    EXPECT_TRUE(target->alert_rise);
    EXPECT_TRUE(target->alert_set);
    press(ImGuiKey_R, true, true);
    EXPECT_TRUE(target->alert_rise);
    EXPECT_TRUE(target->alert_set);

    type('[');
    EXPECT_FALSE(target->alert_rise);
    EXPECT_TRUE(target->alert_set);
    type(']');
    EXPECT_FALSE(target->alert_set);
    selected = -1;
    type('[');
    type(']');
    EXPECT_TRUE(tracked->alert_rise);
    EXPECT_TRUE(tracked->alert_set);
    EXPECT_FALSE(target->alert_rise);
    EXPECT_FALSE(target->alert_set);
}

TEST_F(KeyboardShortcutEvents, PressingDeleteWhileEditingTextPreservesTheSelectedPlanet)
{
    auto* planet = make_planet(make_star(), "Editable target");
    planet->user_added = true;
    selected = planet->seqno;
    const bool prior_editing = editing;
    char buffer[32] = "0123";
    auto& io = ImGui::GetIO();
    for (int i = 0; i < 4; ++i)
    {
        if (i == 2) io.AddKeyEvent(ImGuiKey_Delete, true);
        if (i == 3) io.AddKeyEvent(ImGuiKey_Delete, false);
        ImGui::NewFrame();
        ImGui::Begin("Editor");
        if (i == 0) ImGui::SetKeyboardFocusHere();
        ImGui::InputText("Name", buffer, sizeof(buffer));
        ImGui::End();
        if (i == 2) EXPECT_TRUE(keyboard_edit_field_active());
        process_keyboard_commands(io);
        ImGui::EndFrame();
    }
    EXPECT_FALSE(planet->deleted);
    EXPECT_EQ(selected, planet->seqno);
    editing = prior_editing;
}

TEST_F(KeyboardShortcutEvents, CloudAndConstellationBindingsRespectCaseAndModifiers)
{
    make_star();
    make_star("Second star");
    const bool prior_constellations = show_consln;
    const bool prior_clouds = show_clouds;
    const bool prior_changed = viewchanged;
    const bool prior_labels = show_labels;
    const auto prior_label_type = cbolbls_selected_idx;
    show_consln = show_clouds = show_labels = false;

    type('c');
    EXPECT_TRUE(show_consln);
    EXPECT_FALSE(show_clouds);
    EXPECT_FALSE(show_labels);

    press(ImGuiKey_C, true);
    EXPECT_TRUE(show_clouds);
    EXPECT_TRUE(show_consln);
    EXPECT_FALSE(show_labels);

    type('C');
    EXPECT_TRUE(show_labels);
    EXPECT_EQ(cbolbls_selected_idx, lbltype_sunlike);
    EXPECT_TRUE(show_consln);
    EXPECT_TRUE(show_clouds);

    show_consln = prior_constellations;
    show_clouds = prior_clouds;
    show_labels = prior_labels;
    cbolbls_selected_idx = prior_label_type;
    viewchanged = prior_changed;
}

TEST_F(KeyboardShortcutEvents, FunctionKeysOpenTheMenuAndToggleTheirOwnSettings)
{
    const bool prior_menu = menu;
    const bool prior_favorites = show_favestars;
    const bool prior_timestep = local_tmstep;
    menu = show_favestars = local_tmstep = false;

    press(ImGuiKey_F1);
    EXPECT_TRUE(menu);
    EXPECT_FALSE(show_favestars);
    EXPECT_FALSE(local_tmstep);

    press(ImGuiKey_F7);
    EXPECT_TRUE(menu);
    EXPECT_TRUE(show_favestars);
    EXPECT_FALSE(local_tmstep);

    press(ImGuiKey_F10);
    EXPECT_TRUE(menu);
    EXPECT_TRUE(show_favestars);
    EXPECT_TRUE(local_tmstep);

    press(ImGuiKey_F1);
    EXPECT_FALSE(menu);
    EXPECT_TRUE(show_favestars);
    EXPECT_TRUE(local_tmstep);

    menu = prior_menu;
    show_favestars = prior_favorites;
    local_tmstep = prior_timestep;
}

class KeyboardWarpEvents : public KeyboardShortcutEvents
{
protected:
    const ViewMode prior_mode = view_mode;
    const ViewerPlaneMode prior_plane = vplane_mode;
    const CelestialLocation prior_location = here;
    const Point prior_velocity = velocity;
    const int prior_observer = whereami;
    const int prior_departure = took_off_from;
    const int prior_countdown = tookoff_countdown;
    const double prior_azimuth = azimuth;
    const double prior_altitude = altitude;
    const double prior_azimuth_correction = azimuth_correction;
    const double prior_north = npaz;
    const double prior_spin = spin;
    const std::string prior_locale = viewer_locale;
    const bool prior_inversion = satview_upsidedown;
    const bool prior_changed = viewchanged;

    void SetUp() override
    {
        KeyboardShortcutEvents::SetUp();
        vplane_mode = vplane_local;
        azimuth = altitude = azimuth_correction = 0;
        velocity = Point();
    }

    void TearDown() override
    {
        view_mode = prior_mode;
        vplane_mode = prior_plane;
        here = prior_location;
        velocity = prior_velocity;
        whereami = prior_observer;
        took_off_from = prior_departure;
        tookoff_countdown = prior_countdown;
        azimuth = prior_azimuth;
        altitude = prior_altitude;
        azimuth_correction = prior_azimuth_correction;
        npaz = prior_north;
        spin = prior_spin;
        viewer_locale = prior_locale;
        satview_upsidedown = prior_inversion;
        viewchanged = prior_changed;
        KeyboardShortcutEvents::TearDown();
    }
};

TEST_F(KeyboardWarpEvents, WarpEntersSpaceshipAndDetachesFromTheCurrentObject)
{
    auto* world = make_planet(make_star());
    for (const auto mode : {vm_spaceship, vm_horizon, vm_sunclock, vm_skymap,
                            vm_system, vm_bumpmap, vm_thermal, vm_model})
    {
        SCOPED_TRACE(mode);
        view_mode = mode;
        whereami = world->seqno;
        velocity = Point(0, 0, 10);

        type('w');

        EXPECT_EQ(view_mode, vm_spaceship);
        EXPECT_EQ(whereami, -1);
        EXPECT_EQ(took_off_from, world->seqno);
        EXPECT_GT(velocity.magnitude() * target_frame_rate, speed_of_light);
        EXPECT_DOUBLE_EQ(velocity.z, velocity.magnitude());
    }
}

TEST_F(KeyboardWarpEvents, WarpFromModelStartsAtTheFocus)
{
    make_star();
    auto* focus = make_star("Model focus");
    focus->location.system_center = Point(1000, 2000, 3000);
    focus->location.local_position = Point(100, 200, 300);
    whereami = focus->seqno;
    view_mode = vm_model;

    type('w');

    EXPECT_EQ(view_mode, vm_spaceship);
    EXPECT_EQ(whereami, -1);
    EXPECT_EQ(took_off_from, focus->seqno);
    EXPECT_DOUBLE_EQ((here.system_center - focus->location.system_center).magnitude(), 0);
    EXPECT_DOUBLE_EQ((here.local_position - focus->location.local_position).magnitude(), 0);
    EXPECT_GT(velocity.magnitude() * target_frame_rate, speed_of_light);
}

TEST_F(KeyboardWarpEvents, WarpFromSatelliteModelUsesItsFlightPositionAndPlane)
{
    auto* planet = make_planet(make_star());
    auto* satellite = new Satellite();
    ASSERT_TRUE(append_cel(satellite));
    Orbit orbit;
    orbit.center = planet;
    orbit.period = 7200;
    satellite->orbit = &orbit;
    const Point outward(3e6, 4e6, 5e6);
    satellite->location.local_position = planet->location.local_position + outward;
    whereami = satellite->seqno;
    view_mode = vm_model;
    satview_upsidedown = false;

    type('w');
    satellite->orbit = nullptr;

    EXPECT_EQ(view_mode, vm_spaceship);
    EXPECT_EQ(whereami, -1);
    EXPECT_EQ(took_off_from, satellite->seqno);
    EXPECT_DOUBLE_EQ((here.local_position - satellite->location.local_position).magnitude(), 0);
    const Point up = to_viewer_plane(outward * (1.0 / outward.magnitude()));
    EXPECT_NEAR(up.x, 0, 1e-9);
    EXPECT_NEAR(up.y, 1, 1e-9);
    EXPECT_NEAR(up.z, 0, 1e-9);
}
