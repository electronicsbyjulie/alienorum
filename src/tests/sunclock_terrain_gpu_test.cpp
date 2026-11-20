#include "../sunclock_gpu.h"
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

static void check(bool result, const char* message)
{
    if (!result) throw std::runtime_error(message);
}

int main()
{
    constexpr int width = 256, height = 128;
    constexpr double pi = 3.14159265358979323846;
    check(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL video initializes");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_Window* window = SDL_CreateWindow("Terrain shader regression", 0, 0, width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    check(window != nullptr, "OpenGL window opens");
    SDL_GLContext context = SDL_GL_CreateContext(window);
    check(context != nullptr, "OpenGL context opens");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(width, height);
    check(ImGui_ImplOpenGL3_Init("#version 130"), "OpenGL backend initializes");
    GLuint bump = 0;
    glGenTextures(1, &bump);
    glBindTexture(GL_TEXTURE_2D, bump);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::vector<float> heights(width * height, 0);
    auto upload = [&]()
    {
        glBindTexture(GL_TEXTURE_2D, bump);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, heights.data());
    };
    upload();
    alienorum::SunClockGpuInput input{};
    input.sclk_scale = pi / width;
    input.zoom = 1;
    input.bump_map_texture = bump;
    input.light_dir[2] = 1;
    for (int axis = 0; axis < 3; ++axis)
    {
        input.body_axes[axis] = 1000000;
        input.daylight_tint[axis] = 1;
        input.fallback_color[axis] = .65;
    }
    for (int axis = 0; axis < 4; ++axis) input.rot_matrix[axis * 5] = 1;
    const auto capture = [&](bool lighting)
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        input.terrain_lighting = lighting;
        check(alienorum::queue_sunclock_gpu(input, width / 2, height / 2), "Sun clock GPU queues");
        ImGui::Render();
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        std::vector<unsigned char> pixels(width * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        check(glGetError() == GL_NO_ERROR, "Terrain shader runs without OpenGL errors");
        return pixels;
    };
    const auto difference = [](const auto& a, const auto& b)
    {
        double total = 0;
        for (size_t pixel = 0; pixel < a.size(); pixel += 4)
        {
            for (int channel = 0; channel < 3; ++channel)
                total += std::abs(int(a[pixel + channel]) - int(b[pixel + channel]));
        }
        return total / (a.size() / 4 * 3);
    };
    const double flat_difference = difference(capture(false), capture(true));
    check(flat_difference < .3, "Flat terrain keeps smooth-sphere lighting");
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
            heights[y * width + x] = 100000 * sin(8 * pi * (x + .5) / width) * sin(pi * (y + .5) / height);
    }
    upload();
    const double slope_difference = difference(capture(false), capture(true));
    check(slope_difference > 1, "Physical slopes change sun clock lighting");
    input.self_luminous = true;
    check(difference(capture(false), capture(true)) == 0, "Self-luminous maps keep their brightness");
    glDeleteTextures(1, &bump);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "Sun clock GPU terrain checks passed; slope image difference " << slope_difference << "\n";
}
