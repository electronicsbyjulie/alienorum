
#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION
#include "include/stb/stb_image.h"
#include "globals.h"
#include "loaders.h"
#include "housekeeping.h"
#include "inputs.h"
#include "dialogs.h"
#include "visuals.h"
// Learn more about ImGui here: https://github.com/ocornut/imgui/blob/master/docs/FAQ.md

using namespace alienorum;

// ImGui Example Code
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
        return false;
    fseek(f, 0, SEEK_SET);
    void* file_data = IM_ALLOC(file_size);
    fread(file_data, 1, file_size, f);
    fclose(f);
    bool ret = LoadTextureFromMemory(file_data, file_size, out_texture, out_width, out_height);
    IM_FREE(file_data);
    return ret;
}
// End ImGui Example Code

int main (int argc, char** argv)
{
    int i, j, l, n;
    cels = new CelestialObject*[MAX_CELOBJS];
    vmag_cache = new double[MAX_CELOBJS];
    bloomrad_cache = new double[MAX_CELOBJS];
    angular_radius = new double[MAX_CELOBJS];
    discinstead = new bool[MAX_CELOBJS];
    memset(cels, 0, MAX_CELOBJS*sizeof(CelestialObject*));
    bx_cache = new int[MAX_CELOBJS];
    by_cache = new int[MAX_CELOBJS];
    std::string argsfind = "", argsgo = "", argszoom = "", argstrack = "", argsmode = "", args1char = "";

    memset(lookfor, 0, 40);
    memset(looksat, 0, 40);

    for (l=1; l<argc; l++)
    {
        n = strlen(argv[l]);
        if (n == 1)
        {
            args1char += std::string(argv[l]);
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

        if (!strcmp(argv[l], "load"))
        {
            load_univ = argv[++l];
        }

        if (!strcmp(argv[l], "find"))
        {
            argsfind = argv[++l];
        }

        if (!strcmp(argv[l], "track"))
        {
            argstrack = argv[++l];
        }

        if (!strcmp(argv[l], "go"))
        {
            argsgo = argv[++l];
        }

        if (!strcmp(argv[l], "zoom"))
        {
            argszoom = argv[++l];
        }

        if (!strcmp(argv[l], "jd"))
        {
            setjd = argv[++l];
        }

        if (!strcmp(argv[l], "hz") || !strcmp(argv[l], "horizon"))
        {
            argsmode = "hz";
        }

        if (!strcmp(argv[l], "sun") || !strcmp(argv[l], "sunclock"))
        {
            argsmode = "sun";
        }

        if (!strcmp(argv[l], "theme"))
        {
            std::string theme = argv[++l];
            global_style.load(theme);
        }

        if (!strcmp(argv[l], "magtest")) magnitude_test = true;
        if (!strcmp(argv[l], "sizeof"))
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
    SDL_Surface* icon = IMG_Load("assets/icon48.png");
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

    // Load Fonts
    // - If fonts are not explicitly loaded, ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you have to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefaultVector();
    //io.Fonts->AddFontDefaultBitmap();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    // Our state
    ImVec4 background = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);
    set_gamma(global_gamma);

    std::thread t1(load_stuff);
    t1.detach();

    int splash_image_width = 0;
    int splash_image_height = 0;
    GLuint splash_image_texture = 0;
    bool ret = LoadTextureFromFile("assets/icon_full_seethru.png", &splash_image_texture, &splash_image_width, &splash_image_height);
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

    srand(std::time(nullptr));
    for (i=0; i<MAX_SPLASH_STARS; i++)
    {
        splash_star_positions[i] = ImVec2(frand(0, screen_x), frand(0, screen_y));
        splash_star_brghtness[i] = frand(0.1, 2.9) * pow(frand(0,1), 2);
    }

    global_style.load(viewer_theme);
    apply_default_style();

    ImU32 alien_color = global_style.ecliptic_color | IM_COL32(0,0,0,192);

    // Main loop
    bool done = false;
    viewchanged = true;
    ImVec2 PrevDispSize;
    while (!done)
    {
        auto frame_began = std::chrono::high_resolution_clock::now();
        if (hide_mouse && !is_mouse_over_window && !splash) SDL_ShowCursor(SDL_DISABLE);

        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to imgui, and hide them from your application based on those two flags.
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

            Color col(192, 225, 255);
            double jay;
            for (i=0; i<MAX_SPLASH_STARS; i++)
            {
                for (jay=splash_star_brghtness[i]; jay>=0; jay-=0.5)
                {
                    RGB rgb = Color::rgb_from_color(col, 1);
                    if (rgb.r >= 16 || rgb.b >= 16)
                        ImGui::GetBackgroundDrawList()->AddCircleFilled(splash_star_positions[i],
                            jay, Color::black_to_transparent(IM_COL32(rgb.r, rgb.g, rgb.b, 64)), 0);
                    if (rgb.r == 255 && rgb.b == 255) break;

                    col.red *= bloom_exponent; col.green *= bloom_exponent; col.blue *= bloom_exponent;
                }
            }

            mtx.lock();
            std::string wash_copilots_mouth_out_with_soap = loading_msg;
            mtx.unlock();
            const char* lloadmsg = wash_copilots_mouth_out_with_soap.c_str();

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

        //////////////////////////////////////////////////
        // End ImGui-specific setup code                //
        //////////////////////////////////////////////////
        }
        else
        {
            if (hide_mouse && !is_mouse_over_window && !splash) SDL_ShowCursor(SDL_DISABLE);
            dispcx = (int)io.DisplaySize.x/2;
            dispcy = (int)io.DisplaySize.y / 2;
            drawblxscalex = drawn_cache_split / io.DisplaySize.x;
            drawblxscaley = drawn_cache_split / io.DisplaySize.y;

            if (!cels[1]) ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2((int)io.DisplaySize.x, (int)io.DisplaySize.y), IM_COL32(78, 137, 225, 255));

            set_viewer_location_and_plane();

            compute_object_draw_coordinates();
            if (show_grid) draw_ra_dec_lines();
            if (show_consln) draw_cons_lines();
            if (view_mode == vm_horizon) draw_sky_gradient();
            else sky_mag_shift = 0;
            draw_objects();

            txtyscale = ImGui::GetTextLineHeightWithSpacing() * 1.116;
            txtycompact = ImGui::GetTextLineHeight();

            is_mouse_over_window = false;

            // Status window
            if (statuswnd) draw_status_window(io);

            // Object under cursor info
            if (objinfwnd) draw_objinf_window(io);

            viewchanged = searched || (trackidx>=0) || spin || velocity.magnitude() || (PrevDispSize.x != io.DisplaySize.x) || (PrevDispSize.y != io.DisplaySize.y);

            // Dialogs
            if (objedtwnd) draw_objedit_window(io);
            if (explorer) draw_system_explorer(io);
            if (addcelwnd) draw_addcel_window(io);
            if (astwnd) draw_ast_window(io);
            if (satwnd) draw_sat_window(io);

            is_click = io.MouseReleased[0];
            if (!is_mouse_over_window)
            {
                if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1) && !ImGui::IsMouseDown(2)) draw_mouse_cursor(io);
                identify_object_under_cursor(io);
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
            bool is_mouse_down = ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1) || ImGui::IsMouseDown(2);
            if (trackidx >= 0) dragging = false;
            if (is_mouse_down && !is_mouse_over_window && dragging) pan_with_crosshairs(io);
            if (is_mouse_down && (fabs(io.MousePos.x - lmx) >= 3 || fabs(io.MousePos.y - lmy) >= 3)) dragging = true;
            else if (!is_mouse_down || is_mouse_over_window) dragging = false;

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
            if (argsgo.size())
            {
                int goidx = find_object(argsgo.c_str());
                if (goidx >= 0) whereami = goidx;
                else std::cerr << "Not found " << argsgo << std::endl;
                argsgo = "";
                viewchanged = true;
            }
            else if (argsmode.size())
            {
                if (!strcmp(argsmode.c_str(), "hz")) view_mode = vm_horizon;
                else if (!strcmp(argsmode.c_str(), "sun")) view_mode = vm_sunclock;
                argsmode = "";
                viewchanged = true;
            }
            else if (argstrack.size())
            {
                int findidx = find_object(argstrack.c_str());
                if (findidx >= 0)
                {
                    trackidx = findidx;
                    center_selected();
                }
                else std::cerr << "Not found " << argstrack << std::endl;
                argstrack = "";
                viewchanged = true;
            }
            else if (argsfind.size())               // After go, wait to get new bearings then seek.
            {
                int findidx = find_object(argsfind.c_str());
                if (findidx >= 0)
                {
                    selected = findidx;
                    center_selected();
                }
                else std::cerr << "Not found " << argsfind << std::endl;
                argsfind = "";
                viewchanged = true;
            }
            else if (argszoom.size())               // Don't zoom until after go.
            {
                zoom = atof(argszoom.c_str());
                argszoom = "";
                viewchanged = true;
            }
            else while (args1char.size())
            {
                char c = args1char.c_str()[0];
                args1char = args1char.substr(1);
                process_key_cmd_char(c);
            }

            // Keyboard commands
            process_keyboard_commands(io);

            if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey"))
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
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F11))
        {
            SDL_SetWindowFullscreen(window, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            fullscreen = !fullscreen;
        }

        // More code copied from the ImGui example:
        // Rendering
        ImGui::Render();
        if (hide_mouse && !is_mouse_over_window && !splash) SDL_ShowCursor(SDL_DISABLE);
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(background.x * background.w, background.y * background.w, background.z * background.w, background.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

        if (!splash)
        {
            if (io.MousePos.x != lmx || io.MousePos.y != lmy) frames_without_mousemove = 0;
            else frames_without_mousemove++;

            if ((io.MousePos.x != lmx || io.MousePos.y != lmy || viewchanged || velocity.magnitude() || is_mouse_over_window)
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

            hide_mouse = !splash && !fdlg_shown && (frame_dur < 0.1 || abs(lmx - io.MousePos.x) <= 4 || abs(lmy - io.MousePos.y) <= 4) && cels[1];

            lmx = io.MousePos.x;
            lmy = io.MousePos.y;
            dragged = dragging;
            searched = false;
            PrevDispSize = io.DisplaySize;

            auto frame_finished = std::chrono::high_resolution_clock::now();
            auto frame_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frame_finished - frame_began);
            frame_dur = frame_elapsed.count() * 1e-6;
            if (frame_dur < best_frame_dur) best_frame_dur = frame_dur;

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

    save_universe();

    for (i=0; cels[i]; i++)
    {
        if (cels[i]->typeclass() == class_star && ((Star*)cels[i])->multisys)
        {
            ((Star*)cels[i])->multisys->unlink();
            delete ((Star*)cels[i])->multisys;
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
    delete[] bx_cache;
    delete[] by_cache;
    delete[] consaidx;
    delete[] consbidx;
    if (hdcache) delete[] hdcache;
    if (hipcache) delete[] hipcache;
    return 0;
}
