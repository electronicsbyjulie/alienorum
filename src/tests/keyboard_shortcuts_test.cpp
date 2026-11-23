#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <string>
#include <string_view>
#include <vector>
#include <gtest/gtest.h>
#include "universe_fixture.h"
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

    std::string read_code(const fs::path& path, bool keep_literals = true)
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
                if (keep_literals) result.append(code, i, end - i);
                i = end;
            }
            else
            {
                if (!keep_literals || !std::isspace(static_cast<unsigned char>(code[i]))) result += code[i];
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

    // FNV-1a over the binding source, with comments and formatting removed by read_code.
    uint64_t fingerprint(std::string_view code)
    {
        uint64_t hash = 14695981039346656037ull;
        for (unsigned char value : code) hash = (hash ^ value) * 1099511628211ull;
        return hash;
    }
}

TEST(KeyboardShortcutCompatibility, PreservesPublishedBindingsAndDispatch)
{
    const std::string inputs = read_code(source_root / "inputs.cpp");
    const std::string app = read_code(source_root / "alienorum.cpp");
    const std::string dialogs = read_code(source_root / "dialogs.cpp");
    const std::string population = read_code(source_root / "fictionalstars_ui.cpp");
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

    // These blocks include the inline actions for typed keys, the named-key routes and
    // their handlers, modifier rules, and the keys owned by the main loop and search fields.
    const struct
    {
        std::string_view code;
        std::string_view begins;
        std::string_view ends;
        uint64_t expected;
    } bindings[] =
    {
        {inputs, "voidprocess_key_cmd_char(", "}", 0xf614624ba5381b2full},
        {inputs, "voidprocess_key_cmd_ctrl_char(", "}", 0xfe5c65d59bedf0beull},
        {inputs, "boolkeyboard_edit_field_active(", "}", 0xfddd8c4cac7912f3ull},
        {inputs, "voidprocess_keyboard_commands(", "}", 0x0c56069e591a7379ull},
        {inputs, "ViewModenext_surface_map_view(", "}", 0x2d60300408fc617dull},
        {inputs, "voidprocess_key_arrowleft(", "}", 0x015c577e4c870064ull},
        {inputs, "voidprocess_key_arrowright(", "}", 0x2a7d5fdc0eb58b1cull},
        {inputs, "voidprocess_key_arrowup(", "}", 0xb40b98f03fa1ff39ull},
        {inputs, "voidprocess_key_arrowdn(", "}", 0xddb2ad618bf60b93ull},
        {inputs, "voidprocess_key_home(", "}", 0x0f9371de90d7e258ull},
        {inputs, "voidprocess_key_end(", "}", 0xc39ba3f4b7281f4eull},
        {inputs, "voidprocess_key_delete(", "}", 0x95c9238d585bb4a4ull},
        {inputs, "voidprocess_key_F1(", "}", 0xb6518818ebf4ac9aull},
        {inputs, "voidprocess_key_F2(", "}", 0x5db4581b7527ffa3ull},
        {inputs, "voidprocess_key_F3(", "}", 0x304fa45f52697833ull},
        {inputs, "voidprocess_key_F4(", "}", 0xf86c4852d0241c12ull},
        {inputs, "voidprocess_key_F5(", "}", 0xef2f33048232174aull},
        {inputs, "voidprocess_key_shift_F5(", "}", 0xc9659d4a1b78db2eull},
        {inputs, "voidprocess_key_F6(", "}", 0x7610bba4eb0b2529ull},
        {inputs, "voidprocess_key_F7(", "}", 0x0d33c1d0f6889c24ull},
        {inputs, "voidprocess_key_F8(", "}", 0x44a6df5351201637ull},
        {inputs, "voidprocess_key_F9(", "}", 0x5c5abf48f148a3d4ull},
        {inputs, "voidprocess_key_F10(", "}", 0xe3990df841dd5d1eull},
        {inputs, "voidprocess_key_F11(", "}", 0x4ce39e156b22005bull},
        {inputs, "voidprocess_key_F12(", "}", 0x62641ba69eb1cce5ull},
        {app, "if(loading_frame&&event.type==SDL_KEYDOWN", ";", 0x8556d7a88bd3faf6ull},
        {app, "if(ImGui::IsKeyPressed(ImGuiKey_Escape)", "}", 0xead7f7148ce02a4full},
        {app, "if(argsfs||(!io.KeyShift", "}", 0xa9a7201129d01488ull},
        {dialogs, "if(ImGui::InputText(\"##find\"", ";", 0x3fcb766cc1b60cc6ull},
        {population, "boolsearch=ImGui::InputText(\"##Find galaxy\"", ";", 0xee6e3166736d002bull}
    };
    for (const auto& entry : bindings)
    {
        SCOPED_TRACE(entry.begins);
        EXPECT_EQ(fingerprint(binding_block(entry.code, entry.begins, entry.ends)), entry.expected);
    }
}

TEST(KeyboardShortcutCompatibility, KeepsKeyboardInputAtKnownSites)
{
    const std::regex key_api(R"(\b(IsKeyPressed|IsKeyReleased|IsKeyDown|IsKeyChordPressed|Shortcut|SetNextItemShortcut|InputQueueCharacters|SDL_GetKeyboardState|SDL_KEYDOWN|SDL_KEYUP|ImGuiInputTextFlags_EnterReturnsTrue)\b)");
    const std::map<std::string, int> expected =
    {
        {"alienorum.cpp", 3},
        {"dialogs.cpp", 1},
        {"fictionalstars_ui.cpp", 1},
        {"inputs.cpp", 56},
        {"visuals.cpp", 2}
    };
    std::map<std::string, int> actual;
    for (auto it = fs::recursive_directory_iterator(source_root); it != fs::recursive_directory_iterator(); ++it)
    {
        const auto& path = it->path();
        if (it->is_directory())
        {
            if (path.filename() == "include" || path.filename() == "vendor" || path.filename() == "tests")
                it.disable_recursion_pending();
            continue;
        }
        if (path.extension() != ".cpp" && path.extension() != ".h") continue;
        const auto code = read_code(path, false);
        const auto count = std::distance(std::sregex_iterator(code.begin(), code.end(), key_api), std::sregex_iterator());
        if (count) actual.emplace(path.lexically_relative(source_root).generic_string(), count);
    }
    EXPECT_EQ(actual, expected);
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
