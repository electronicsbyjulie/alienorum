
#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION
#include <queue>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "png.h"
#include "include/stb/stb_image.h"
#include "globals.h"
#include "loaders.h"
#include "housekeeping.h"
#include "inputs.h"
#include "dialogs.h"
#include "visuals.h"
#include "sphere_impostor.h"
#include "classes/sscimport.h"
// Learn more about ImGui here: https://github.com/ocornut/imgui/blob/master/docs/FAQ.md

using namespace alienorum;

// Replayable command-line tokens, in one ordered queue drained a token per frame by the main
// loop. Replay order must match command-line order, so that e.g. an F12 snapshot sees the state
// of everything typed before it.
struct CliCmd
{
    enum Kind { k_go, k_mode, k_track, k_find, k_zoom, k_fkey, k_char, k_import } kind;
    std::string s;
    int fkey = 0;
    char c = 0;
};

// Simple helper function to load an image into a OpenGL texture with common settings
bool LoadTextureFromMemory(const void* data, size_t data_size, GLuint* out_texture, int* out_width, int* out_height)
{
    // Load from file
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load_from_memory((const unsigned char*)data, (int)data_size, &image_width, &image_height, NULL, 4);
    if (image_data == NULL)
        return false;

    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload pixels into texture
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);

    *out_texture = image_texture;
    *out_width = image_width;
    *out_height = image_height;

    return true;
}

// Open and read a file, then forward to LoadTextureFromMemory()
bool LoadTextureFromFile(const char* file_name, GLuint* out_texture, int* out_width, int* out_height)
{
    FILE* f = fopen(file_name, "rb");
    if (f == NULL)
        return false;
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size == -1L)
    {
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);
    void* file_data = IM_ALLOC(file_size);
    int foad = fread(file_data, 1, file_size, f);
    fclose(f);
    bool ret = LoadTextureFromMemory(file_data, file_size, out_texture, out_width, out_height);
    IM_FREE(file_data);
    return ret;
}
// End ImGui Example Code

// Writes the just-rendered back buffer out as a PNG (same libpng plumbing as Map::save_to_png,
// but sourced from glReadPixels). GL's row 0 is the bottom of the image, so rows go out in
// reverse to come out right-side up.
bool save_snapshot_png(const std::string& filename, int width, int height)
{
    if (width <= 0 || height <= 0) return false;

    std::vector<unsigned char> pixels(width * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    FILE *fp = fopen(filename.c_str(), "wb");
    if (!fp)
    {
        std::cerr << "Failed to open " << filename << " for writing." << std::endl;
        return false;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        fclose(fp);
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return false;
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(
        png_ptr,
        info_ptr,
        width,
        height,
        8,                          // 8 bits per channel
        PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

    std::vector<png_bytep> row_pointers(height);
    for (int y = 0; y < height; y++)
        row_pointers[y] = &pixels[(size_t)(height - 1 - y) * width * 3];

    png_write_info(png_ptr, info_ptr);
    png_write_image(png_ptr, row_pointers.data());
    png_write_end(png_ptr, NULL);

    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return true;
}

int main (int argc, char** argv)
{
    int i, j, l, n;
    cels = new CelestialObject*[MAX_CELOBJS];
    vmag_cache = new double[MAX_CELOBJS];
    bloomrad_cache = new double[MAX_CELOBJS];
    angular_radius = new double[MAX_CELOBJS];
    discinstead = new bool[MAX_CELOBJS];
    memset(cels, 0, MAX_CELOBJS*sizeof(CelestialObject*));

    std::vector<CliCmd> cli_cmds;
    size_t cli_cmd_pos = 0;
    auto push_str = [&](CliCmd::Kind k, const std::string& s) { CliCmd cmd; cmd.kind = k; cmd.s = s; cli_cmds.push_back(cmd); };
    auto push_fkey = [&](int n) { CliCmd cmd; cmd.kind = CliCmd::k_fkey; cmd.fkey = n; cli_cmds.push_back(cmd); };
    auto push_char = [&](char c) { CliCmd cmd; cmd.kind = CliCmd::k_char; cmd.c = c; cli_cmds.push_back(cmd); };

    auto next_arg = [&](const char* opt) -> const char*
    {
        if (l+1 >= argc)
        {
            std::cerr << "Option '" << opt << "' takes a value, and none was given." << std::endl;
            return nullptr;
        }
        return argv[++l];
    };

    bool argsfs = false;

    memset(lookfor, 0, name_max_len);
    memset(edit_name, 0, name_max_len);
    memset(looksat, 0, name_max_len);
    memset(lookast, 0, name_max_len);
    memset(lookcomet, 0, name_max_len);
    memset(lookloc, 0, name_max_len);

    for (l=1; l<argc; l++)
    {
        n = strlen(argv[l]);
        if (n == 1)
        {
            push_char(argv[l][0]);
            continue;
        }

        if ((unsigned int)n == ((xonsm[4] & 017) ^ 015))
        {
            const char* ucpdhahzs = "\x2b\x85\xe9\x80\x57\xe4\x70\x00";
            i = 0;
            for (j=0; ucpdhahzs[j]; j++)
                if ((unsigned int)argv[l][j] == ((ucpdhahzs[j] ^ xonsm[j]) & 0377)) i++;

            if (i==n)
            {
                show_xonsm = true;
                xaorngsim = l;
            }
        }
        else if (!strcmp(argv[l], "load"))
        {
            if (const char* a = next_arg("load")) load_univ = a;
        }
        else if (!strcmp(argv[l], "find"))
        {
            if (const char* a = next_arg("find")) push_str(CliCmd::k_find, a);
        }
        else if (!strcmp(argv[l], "import"))
        {
            if (const char* a = next_arg("import")) push_str(CliCmd::k_import, a);
        }
        else if (!strcmp(argv[l], "track"))
        {
            if (const char* a = next_arg("track")) push_str(CliCmd::k_track, a);
        }
        else if (!strcmp(argv[l], "go"))
        {
            if (const char* a = next_arg("go")) push_str(CliCmd::k_go, a);
        }
        else if (!strcmp(argv[l], "zoom"))
        {
            if (const char* a = next_arg("zoom")) push_str(CliCmd::k_zoom, a);
        }
        else if (!strcmp(argv[l], "fs") || !strcmp(argv[l], "fullscreen"))
        {
            argsfs = true;
        }
        else if (!strcmp(argv[l], "jd") || !strcmp(argv[l], "JD"))
        {
            if (const char* a = next_arg("jd")) setjd = a;
        }
        else if (argv[l][0] == 'J' && argv[l][1] == 'D' && argv[l][2] >= '0' && argv[l][2] <= '9')
        {
            setjd = &argv[l][2];
        }
        else if (!strcmp(argv[l], "hz") || !strcmp(argv[l], "horizon"))
        {
            push_str(CliCmd::k_mode, "hz");
        }
        else if (!strcmp(argv[l], "sun") || !strcmp(argv[l], "sunclock"))
        {
            push_str(CliCmd::k_mode, "sun");
        }
        else if (!strcmp(argv[l], "lblmag"))
        {
            if (const char* a = next_arg("lblmag")) appmagn_lblcut = atof(a);
        }
        else if (!strcmp(argv[l], "theme"))
        {
            if (const char* a = next_arg("theme")) global_style.load(std::string(a));
        }
        else if (!strcmp(argv[l], "noexo"))
        {
            noexo = true;
        }
        else if (!strcmp(argv[l], "nosats") || !strcmp(argv[l], "nosat"))
        {
            nosats = true;
        }
        else if (!strcmp(argv[l], "F1")) push_fkey(1);
        else if (!strcmp(argv[l], "F2")) push_fkey(2);
        else if (!strcmp(argv[l], "F3")) push_fkey(3);
        else if (!strcmp(argv[l], "F4")) push_fkey(4);
        else if (!strcmp(argv[l], "F5")) push_fkey(5);
        else if (!strcmp(argv[l], "F6")) push_fkey(6);
        else if (!strcmp(argv[l], "F7")) push_fkey(7);
        else if (!strcmp(argv[l], "F8")) push_fkey(8);
        else if (!strcmp(argv[l], "F9")) push_fkey(9);
        else if (!strcmp(argv[l], "F10")) push_fkey(10);
        else if (!strcmp(argv[l], "F11")) push_fkey(11);
        else if (!strcmp(argv[l], "F12")) push_fkey(12);
        else if (!strcmp(argv[l], "keyprobe"))
        {
            keyprobe = true;
        }
        else if (!strcmp(argv[l], "magtest")) magnitude_test = true;
        else if (!strcmp(argv[l], "sizeof"))
        {
            std::cout << "Size of CelestialObject: " << sizeof(CelestialObject) << std::endl;
            std::cout << "Size of Galaxy: " << sizeof(Galaxy) << std::endl;
            std::cout << "Size of Star: " << sizeof(Star) << std::endl;
            std::cout << "Size of Planet: " << sizeof(Planet) << std::endl;
            std::cout << "Size of Moon: " << sizeof(Moon) << std::endl;
            std::cout << "Size of Orbit: " << sizeof(Orbit) << std::endl;
            std::cout << "Size of Point: " << sizeof(Point) << std::endl;
            std::cout << "Size of CelestialLocation: " << sizeof(CelestialLocation) << std::endl;
        }
    }

    //////////////////////////////////////////////////
    // Begin ImGui-specific setup code              //
    // This section is subject to the same license  //
    // as the contents of the imgui folder.         //
    //////////////////////////////////////////////////

    // Setup SDL
#ifdef _WIN32
    ::SetProcessDPIAware();
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return 1;
    }

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
    const char* glsl_version = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    double main_scale = 1; // ImGui_ImplSDL2_GetContentScaleForDisplay(0);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    window = SDL_CreateWindow("Alienorum", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return 1;
    }

    if (!look_for_catalogs()) return -1;
    if (file_exists("nonet")) radio_silence = true;
    SDL_Surface* icon = IMG_Load("assets" _FILESLASH "icon48.png");
    if (icon)
    {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr)
    {
        printf("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup ImGui style
    ImGui::StyleColorsDark();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts. AddFont* returns nullptr on failure; see imgui's docs/FONTS.md.
    global_font = io.Fonts->AddFontFromFileTTF("assets" _FILESLASH "LiberationMono" _FILESLASH "LiberationMono-Regular.ttf");
    Greek_font = io.Fonts->AddFontFromFileTTF("assets" _FILESLASH "LiberationSerif" _FILESLASH "LiberationSerif-GreekSymbol.ttf");                    // really craving a gyro right now

    IM_ASSERT(global_font != nullptr);
    IM_ASSERT(Greek_font != nullptr);
    ImGui::GetStyle().FontSizeBase = global_font_size;

    // Our state
    ImVec4 background = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);
    set_gamma(global_gamma);

    std::thread t1(load_stuff);

    int splash_image_width = 0;
    int splash_image_height = 0;
    GLuint splash_image_texture = 0;
    bool ret = LoadTextureFromFile("assets" _FILESLASH "icon_full_seethru.png", &splash_image_texture, &splash_image_width, &splash_image_height);
    if(!ret)
    {
        printf("Failed to load icon texture\n");
    }

    ImVec2 splash_star_positions[MAX_SPLASH_STARS];
    double splash_star_brghtness[MAX_SPLASH_STARS];
    int screen_x = 1920, screen_y = 1080;               // The most common values.

    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0)
    {
        screen_x = dm.w;
        screen_y = dm.h;
    }

    // std::srand(std::time(nullptr));
    for (i=0; i<MAX_SPLASH_STARS; i++)
    {
        splash_star_positions[i] = ImVec2(frand(0, screen_x), frand(0, screen_y));
        splash_star_brghtness[i] = frand(0.1, 2.9) * pow(frand(0,1), 2);
    }

    global_style.load(viewer_theme);
    apply_default_style();

    ImU32 alien_color = global_style.ecliptic_color | IM_COL32(0,0,0,192);
    #define aliend 0.0003
    #define gravity 0.00000001
    #define repulsion 0.0000000003
    float alienb = 0.003921569 * ((alien_color & 0xff0000) >> 16), alieng = 0.003921569 * ((alien_color & 0xff00) >> 8),
        alienr = 0.003921569 * ((alien_color & 0xff)),
        aliendr = frand(-aliend, aliend), aliendg = frand(-aliend, aliend), aliendb = frand(-aliend, aliend);

    std::time_t now = std::time(nullptr);
    struct tm *loc_time = std::localtime(&now);
    bool nlo = (loc_time->tm_mon == 3 && loc_time->tm_mday == 1);
    ImVec2 ovni(2061, 123), nlorad(54, 29);
    double dxovni = -1.3, dyovni = -0.0029;
    std::deque<ImVec2> trail;

    SDL_Surface* surface = IMG_Load("assets/blank.png");
    SDL_Cursor* empty_cursor = nullptr;
    if (surface)
    {
        // 0, 0 is the top-left of the cursor (the click point)
        empty_cursor = SDL_CreateColorCursor(surface, 0, 0);
        SDL_FreeSurface(surface);
    }
    SDL_Cursor* default_cursor = SDL_GetDefaultCursor();

    // Main loop
    viewchanged = true;
    ImVec2 PrevDispSize;
    view_mode = vm_spaceship;
    bool is_mouse_down = false, was_mouse_down = false;
    double last_click = 0;
    ImVec2 last_click_pos;
    while (!done)
    {
        auto frame_began = std::chrono::high_resolution_clock::now();
        if (hide_mouse && (!is_mouse_over_window || dragging) && !splash)
        {
            // SDL_ShowCursor(SDL_DISABLE);
            if (empty_cursor) SDL_SetCursor(empty_cursor);
        }
        else SDL_SetCursor(default_cursor);

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // Start the ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        impostor_begin_frame();

        //////////////////////////////////////////////////
        // End ImGui-specific setup code                //
        //////////////////////////////////////////////////

        if (splash)
        {
            double splash_width = io.DisplaySize.x - 5, splash_height = io.DisplaySize.y/1.61803398875;
            double splash_top = io.DisplaySize.y/2 - splash_height/2;
            double aspect_width = splash_height * splash_image_width / splash_image_height;
            double left = fmax(0, (splash_width - aspect_width) / 2);

            // Sky gradient.
            int y;
            double r = 0.0003, g = 0.002, b = 0.02;
            for (y=0; y<io.DisplaySize.y; y++)
            {
                ImGui::GetBackgroundDrawList()->AddLine(ImVec2(0, y), ImVec2(io.DisplaySize.x, y),
                    IM_COL32( (int)(fmin(.44,r)*255), (int)(fmin(.53,g)*255), (int)(fmin(.81,b)*255), 255 ) );

                r *= 1.0067;
                g *= 1.0053;
                b *= 1.0037;
            }

            double jay;
            for (i=0; i<MAX_SPLASH_STARS; i++)
            {
                Color col(192, 225, 255);
                for (jay=splash_star_brghtness[i]; jay>=0; jay-=0.5)
                {
                    RGB3Byte rgb = Color::rgb_from_color(col, 1);
                    if (rgb.r >= 16 || rgb.b >= 16)
                    {
                        ImGui::GetBackgroundDrawList()->AddCircleFilled(splash_star_positions[i],
                            jay, Color::black_to_transparent(IM_COL32(rgb.r, rgb.g, rgb.b, 64)), 0);
                    }
                    if (rgb.r == 255 && rgb.b == 255) break;

                    col.red *= bloom_exponent; col.green *= bloom_exponent; col.blue *= bloom_exponent;
                }
            }

            mtx.lock();
            std::string wash_copilots_mouth_out_with_soap = loading_msg;
            mtx.unlock();
            const char* lloadmsg = wash_copilots_mouth_out_with_soap.c_str();

            aliendr += gravity; aliendr += repulsion * sgn(alienr - alieng) + repulsion * sgn(alienr - alienb);
            aliendg += gravity; aliendg += repulsion * sgn(alieng - alienr) + repulsion * sgn(alieng - alienb);
            aliendb += gravity; aliendb += repulsion * sgn(alienb - alienr) + repulsion * sgn(alienb - alieng);

            alienr += aliendr; if (alienr < 0) aliendr = fabs(aliendr); else if (alienr > 1) aliendr = -fabs(aliendr);
            alieng += aliendg; if (alieng < 0) aliendg = fabs(aliendg); else if (alieng > 1) aliendg = -fabs(aliendg);
            alienb += aliendb; if (alienb < 0) aliendb = fabs(aliendb); else if (alienb > 1) aliendb = -fabs(aliendb);

            alien_color = IM_COL32(255*fmax(0, fmin(1, alienr)), 255*fmax(0, fmin(1, alieng)), 255*fmax(0, fmin(1, alienb)), 255);

            if (!lloadmsg || !strlen(lloadmsg) || *lloadmsg < ' ' || *lloadmsg > 'Z') lloadmsg = "Loading...";

            if (ImGui::Begin("Loading...", &splash, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings))
            {
                ImGui::SetWindowPos(ImVec2(left,splash_top));
                ImGui::SetWindowSize(ImVec2(aspect_width+16, splash_height+35));
                ImGui::Text("%s", lloadmsg);
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                ImVec2 canvas_p0 = ImGui::GetCursorScreenPos(), canvas_p1(canvas_p0.x+aspect_width, canvas_p0.y+splash_height);
                draw_list->AddRectFilled(canvas_p0, canvas_p1, alien_color);
                ImGui::Image((ImTextureID)(intptr_t)splash_image_texture, ImVec2(aspect_width, splash_height));
            }
            else std::cout << "ImGui::Begin() failed." << std::endl;
            ImGui::End();

            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                abort_load = true;
                done = true;
            }
        }
        else
        {
            mouse_over_menu = menu && (io.MousePos.y < menu_ht);
            if (menu) show_menu();
            if (menu_clicked || splash) goto _render;                       // Prevent click-selecting object behind menu and prevent crash if user reloads constellations.

            dispcx = (int)io.DisplaySize.x / 2;
            dispcy = (int)io.DisplaySize.y / 2;

            if (!cels[1]) ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2((int)io.DisplaySize.x, (int)io.DisplaySize.y), IM_COL32(78, 137, 225, 255));

            if (whtbkgd) ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0,0), ImVec2(dispcx*2,dispcy*2), rgba_apply_redlight(IM_COL32(255,255,255,255)));
            if (view_mode == vm_skymap && whereami >= 0)
            {
                std::string toplbl = std::string("Sky map for ") + std::string(cels[whereami]->name);
                ImGui::GetBackgroundDrawList()->AddText(ImVec2(5,5),
                        rgba_apply_redlight(whtbkgd
                            ? IM_COL32(0,0,0,255)
                            : IM_COL32(255,255,255,255)),
                    toplbl.c_str());
            }

            set_viewer_location_and_plane();
            compute_object_draw_coordinates();
            if (view_mode == vm_horizon)
            {
                find_horizon();
                draw_sky_gradient();
            }
            else
            {
                sky_grad.clear();
                sky_mag_shift = 0;
            }

            if (view_mode == vm_sunclock) draw_sunclock();
            else
            {
                is_a_locale_under_cursor = nullptr;
                selected_locale = nullptr;
                draw_galaxy_band();                 // behind everything: it is the backdrop
                if (show_grid) draw_ra_dec_lines();
                if (show_consln) draw_cons_lines();
                draw_objects();
                draw_cloudy_sky();
                draw_horizon();
            }

            txtyscale = ImGui::GetTextLineHeightWithSpacing() * 1.116;
            txtycompact = ImGui::GetTextLineHeight();

            // Because the windows are dynamically sized, we can never know if the mouse is over a window in the *current* frame until
            // after the windows are drawn, which is too late for the info that populates the N panel. So we depend on the *previous*
            // frame's answer to "is the mouse over a window" and so far this has been good enough.
            is_mouse_over_window = false;

            // A native file dialog is its own top-level window, not one of ours, so nothing
            // below sets this from its bounds the way it does for our own ImGui windows. Left
            // unset, sky-panning's own dragging (pan_with_crosshairs()) would arm underneath it
            // -- and that function wraps the cursor back onto the screen near either edge, which
            // fights the dialog's own edge-drag resize. Set unconditionally, before the dragging
            // gate further down reads it this same frame, for as long as the dialog is open.
            if (fdlg_shown) is_mouse_over_window = true;

            // Status window
            if (statuswnd) draw_status_window(io);

            // Object under cursor info
            if (objinfwnd) draw_objinf_window(io);

            viewchanged = searched || (trackidx>=0) || spin || velocity.magnitude() || (PrevDispSize.x != io.DisplaySize.x) || (PrevDispSize.y != io.DisplaySize.y);

            // Dialogs
            editing = false;
            if (objedtwnd) draw_objedit_window(io);
            if (explorer) draw_system_explorer(io);
            if (addcelwnd) draw_addcel_window(io);
            if (astwnd) draw_ast_window(io);
            if (cometwnd) draw_comet_window(io);
            if (satwnd) draw_sat_window(io);
            if (neighborhood) draw_stellar_neighborhood(io);
            if (locwnd) draw_loc_window(io);
            draw_ssc_import_window(io);

            if (!is_mouse_over_window && !dragging)
            {
                is_click = io.MouseReleased[0];
                // if (is_click) std::cout << "CLICK! Last click = " << (simnow - last_click) << " seconds ago." << std::endl;
                if (is_click && (simnow - last_click) < (frame_dur*2 + 0.2) && distance(last_click_pos, io.MousePos) < 3) is_dbl_click = true;
                if (is_click)
                {
                    last_click = simnow;
                    last_click_pos = io.MousePos;
                }
                if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1) && !ImGui::IsMouseDown(2)) draw_mouse_cursor(io);
                identify_object_under_cursor(io);
            }

            if (is_dbl_click && (view_mode == vm_sunclock))
            {
                viewer_lat = (double)(dispcy - io.MousePos.y) * sclk_scale + altitude;
                viewer_lon = (double)(io.MousePos.x - dispcx) * sclk_scale + azimuth;
                viewer_tz  = 0;
                viewer_dst = dst_none;
                view_mode = vm_horizon;
            }

            // Positioning updates
            vm = velocity.magnitude();
            vmfr = vm * target_frame_rate;
            Point vdil = velocity;
            if (vmfr < speed_of_light) vdil.scale(vdil.magnitude() / compute_time_dilation(vmfr));
            here.local_position += vdil;
            azimuth += spin;

            // Slow down to avoid zipping past tracked object
            if (trackidx >= 0)
            {
                double r = here.distance_to(cels[trackidx]->location);
                if (vm > 0.03*r) velocity.scale(0.9*vm);
            }

            // Clicking and dragging
            is_mouse_down = ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1) || ImGui::IsMouseDown(2);
            if (trackidx >= 0) dragging = false;
            else if (is_mouse_down && !is_mouse_over_window && !was_mouse_down) draggable = true;
            else if (!is_mouse_down) dragging = draggable = false;

            if (draggable && (fabs(io.MousePos.x - lmx) >= mouse_drag_threshold || fabs(io.MousePos.y - lmy) >= mouse_drag_threshold)) dragging = true;
            if (!draggable) dragging = false;
            if (dragging) pan_with_crosshairs(io);

            if (!dragging && scrollhold)
            {
                scrollhold -= frame_dur;
                if (scrollhold <= 0) scrollhold = 0;
                else dragging = true;
            }

            // Scroll wheel to zoom
            if (!is_mouse_over_window)
            {
                if (io.MouseWheel > 0)
                {
                    zoom *= 1.1;
                    global_brightness *= 1.07;
                    dragging = true;
                    scrollhold = 1;
                    viewchanged = true;
                }
                else if (io.MouseWheel < 0 && zoom > 1)
                {
                    zoom = fmax(1, zoom * 0.9);
                    global_brightness *= 0.93;
                    dragging = true;
                    scrollhold = 1;
                    viewchanged = true;
                }
            }

            // Command line args
            if (setjd.size())
            {
                JDnow = atof(setjd.c_str());
                setjd = "";
            }
            if (cli_cmd_pos < cli_cmds.size())
            {
                CliCmd &cmd = cli_cmds[cli_cmd_pos++];
                switch (cmd.kind)
                {
                    case CliCmd::k_go:
                    {
                        int goidx = find_object(cmd.s.c_str());
                        if (goidx >= 0) whereami = goidx;
                        else std::cerr << "Not found " << cmd.s << std::endl;
                        viewchanged = true;
                        break;
                    }
                    case CliCmd::k_mode:
                        if (cmd.s == "hz") view_mode = vm_horizon;
                        else if (cmd.s == "sun") view_mode = vm_sunclock;
                        viewchanged = true;
                        break;
                    case CliCmd::k_track:
                    {
                        int findidx = find_object(cmd.s.c_str());
                        if (findidx >= 0)
                        {
                            trackidx = findidx;
                            center_selected();
                        }
                        else std::cerr << "Not found " << cmd.s << std::endl;
                        viewchanged = true;
                        break;
                    }
                    case CliCmd::k_find:
                    {
                        int findidx = find_object(cmd.s.c_str());
                        if (findidx >= 0)
                        {
                            selected = findidx;
                            center_selected();
                        }
                        else std::cerr << "Not found " << cmd.s << std::endl;
                        viewchanged = true;
                        break;
                    }
                    case CliCmd::k_import:
                        // Queued like every other command-line token rather than run before the
                        // window opens, because the importer looks its parent stars up in cels[]
                        // and that is not filled until the catalogs have finished loading.
                        last_ssc_import.read(cmd.s);
                        set_center_objects();
                        refresh_star_visibilities();
                        for (const std::string &note : last_ssc_import.report.notes)
                            std::cout << note << std::endl;
                        std::cout << last_ssc_import.report.bodies_added << " objects imported from "
                            << cmd.s << ", " << last_ssc_import.report.bodies_skipped << " skipped." << std::endl;
                        // No report window on this path. The same commentary has just gone to the
                        // console, where a scripted run can read it, and a window sitting over the
                        // view is exactly what a scripted run does not want.
                        viewchanged = true;
                        break;
                    case CliCmd::k_zoom:
                        zoom = atof(cmd.s.c_str());
                        viewchanged = true;
                        break;
                    case CliCmd::k_fkey:
                        switch (cmd.fkey)
                        {
                            case 1: process_key_F1(); break;
                            case 2: process_key_F2(); break;
                            case 3: process_key_F3(); break;
                            case 4: process_key_F4(); break;
                            case 5: process_key_F5(); break;
                            case 6: process_key_F6(); break;
                            case 7: process_key_F7(); break;
                            case 8: process_key_F8(); break;
                            case 9: process_key_F9(); break;
                            case 10: process_key_F10(); break;
                            case 11: process_key_F11(); break;
                            case 12: process_key_F12(); break;
                        }
                        break;
                    case CliCmd::k_char:
                        process_key_cmd_char(cmd.c);
                        break;
                }
            }

            // Keyboard commands
            if (ImGui::IsAnyItemActive()) editing = true;
            if (!editing) process_keyboard_commands(io);

            // A minimum size big enough to be usable on open, so resizing it -- awkward, since
            // the sky view's own edge-drag panning has to be held off the whole time one of these
            // is up (see the fdlg_shown check above) -- is rarely something the user needs to do.
            if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey", ImGuiWindowFlags_NoCollapse, ImVec2(720, 480)))
            {
                if (ImGuiFileDialog::Instance()->IsOk())
                {
                    // action if OK
                    std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                    std::string filePath = ImGuiFileDialog::Instance()->GetCurrentPath();
                    // action

                    if (load_universe(filePathName))
                    {
                        const char* slash = strrchr(filePathName.c_str(), '/');
                        if (!slash) slash = strrchr(filePathName.c_str(), '\\');
                        if (slash) load_univ = &slash[1];
                        else load_univ = filePathName;
                        SDL_SetWindowTitle(window, (load_univ + std::string(" - Alienorum")).c_str());
                    }
                }

                // close
                ImGuiFileDialog::Instance()->Close();
                fdlg_shown = false;
            }

            if (ImGuiFileDialog::Instance()->Display("ImportSscDlgKey", ImGuiWindowFlags_NoCollapse, ImVec2(720, 480)))
            {
                if (ImGuiFileDialog::Instance()->IsOk())
                {
                    std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                    last_ssc_import.read(filePathName);
                    set_center_objects();
                    refresh_star_visibilities();
                    ssc_report_shown = true;
                }

                ImGuiFileDialog::Instance()->Close();
                fdlg_shown = false;
            }

            was_mouse_down = is_mouse_down;
        }
        if (argsfs || ImGui::IsKeyPressed(ImGuiKey_F11))
        {
            process_key_F11();
            argsfs = false;
        }

        if (nlo)
        {
            n = trail.size();
            if (n > 54) trail.pop_front();
            for (i=0; i<n; i++)
            {
                if (trail[i].x && trail[i].y)
                    ImGui::GetBackgroundDrawList()->AddEllipseFilled(trail[i], nlorad, IM_COL32(0, 255, 0, i*0.2+1), 85);
            }

            ImGui::GetBackgroundDrawList()->AddEllipseFilled(ovni, nlorad, IM_COL32(0, 255, 0, 255), 85);
            ImVec2 vec2 = ovni;
            vec2.x += frand (-5, 5);
            vec2.y += frand (-5, 5);
            trail.push_back(vec2);
            ovni.x += dxovni;
            ovni.y += dyovni;

            dxovni += frand (-0.01, 0.01);
            dyovni += frand (-0.01, 0.01);

            if (ovni.x < -200) nlo = false;
        }

        // std::cout << ImGui::GetBackgroundDrawList()->_VtxCurrentIdx << " _VtxCurrentIdx." << std::endl << std::flush;

        // More code copied from the ImGui example:
        // Rendering
        _render:
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(background.x * background.w, background.y * background.w, background.z * background.w, background.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (take_snapshot)
        {
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);

            const char* snapdir = "snapshots";
            std::filesystem::path p = snapdir;
            if (!std::filesystem::exists(p))
            {
                // Create the dest folder.
                std::filesystem::create_directories(snapdir);
            }

            // Format the time into a stringstream
            std::stringstream shnapsot_fname;
            shnapsot_fname << snapdir << _FILESLASH << "snapshot." 
                << std::put_time(std::localtime(&time_t_now), "%Y%m%d.%H%M%S") 
                << ".png";

            if (save_snapshot_png(shnapsot_fname.str(), (int)io.DisplaySize.x, (int)io.DisplaySize.y))
                std::cout << "Saved " << shnapsot_fname.str() << " (" << (int)io.DisplaySize.x << "x" << (int)io.DisplaySize.y << ")" << std::endl;
            else
                std::cerr << "Failed to save " << shnapsot_fname.str() << " to disk." << std::endl;
            take_snapshot = false;
        }
        SDL_GL_SwapWindow(window);
        reap_released_objects();

        if (!splash)
        {
            if (io.MousePos.x != lmx || io.MousePos.y != lmy) frames_without_mousemove = 0;
            else frames_without_mousemove++;

            if ((io.MousePos.x != lmx || io.MousePos.y != lmy || viewchanged || velocity.magnitude() || is_mouse_over_window || nlo)
                && frame_dur > (1.0/target_frame_rate))
            {
                timeout_ms *= 0.333;
                if (timeout_ms < 5) timeout_ms = 5;
            }
            else
            {
                timeout_ms *= 1.5;
                if (timeout_ms > 67) timeout_ms = 67;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));

            hide_mouse = !splash && !fdlg_shown
                && !mouse_over_menu
                && (frame_dur < 0.2 || /* yes this is an or */                       // Bring back the standard mouse pointer if the frame rate is low enough
                    abs(lmx - io.MousePos.x) <= 4 || abs(lmy - io.MousePos.y) <= 4)  // so the user doesn't get impatient waiting for the annoying red blob to follow.
                && cels[1];

            lmx = io.MousePos.x;
            lmy = io.MousePos.y;
            dragged = dragging;
            searched = false;
            PrevDispSize = io.DisplaySize;

            auto frame_finished = std::chrono::high_resolution_clock::now();
            auto frame_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frame_finished - frame_began);
            frame_dur = frame_elapsed.count() * 1e-6;
            std::this_thread::sleep_for(std::chrono::duration<double, std::micro>(66666 - frame_dur * 1e6));
            if (frame_dur < best_frame_dur) best_frame_dur = frame_dur;
            io.DeltaTime = frame_dur;
            is_click = is_dbl_click = false;

            vmfr = velocity.magnitude() * target_frame_rate;
            if (vmfr >= speed_of_light)
            {
                // No time dilation in warp mode because faster than light is impossible so warp mode is
                // some kind of hand wavy physics that bypass relativity.
                JDnow += frame_dur/oneday;
            }
            else
            {
                JDnow += frame_dur/oneday / compute_time_dilation(vmfr);
            }

            simnow = (double)(JDnow - J2000)*oneday + J2000_TIME_T;
        }
    }

    // Stop the loader and wait for it before touching anything it writes to. This has to come
    // ahead of save_universe() as well as ahead of the deletes: the loader appends to `cels` as
    // it goes, and Serialization::save_all() walks that same array.
    abort_load = true;
    if (t1.joinable()) t1.join();

    if (cels[1]) save_universe();

    for (i=0; cels[i]; i++)
    {
        if (cels[i]->typeclass() == class_star && ((Star*)cels[i])->multisys)
        {
            StarMulti *sm = ((Star*)cels[i])->multisys;
            sm->unlink();
            delete sm;
        }
    }

    for (i=0; cels[i]; i++)
    {
        delete cels[i];
        cels[i] = nullptr;
    }

    delete[] cels;
    delete[] vmag_cache;
    delete[] bloomrad_cache;
    delete[] discinstead;
    if (hdcache) delete[] hdcache;
    if (hipcache) delete[] hipcache;
    return 0;
}
