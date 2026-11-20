#include "../sphere_impostor.h"
#include "../horizon_ground_gpu.h"
#include "../classes/color.h"
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <map>
#include <stdexcept>
#include <iostream>

std::map<int, alienorum::RGB3> sky_grad;

static void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

int main()
{
    constexpr int width = 256, height = 128;
    check(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL initializes");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_Window* window = SDL_CreateWindow("Ring terrain regression", 0, 0, width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    check(window != nullptr, "Window opens");
    SDL_GLContext context = SDL_GL_CreateContext(window);
    check(context != nullptr, "OpenGL context opens");
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = ImVec2(width, height);
    check(ImGui_ImplOpenGL3_Init("#version 130"), "Renderer initializes");
    GLuint textures[2];
    glGenTextures(2, textures);
    const unsigned char pattern[16] = {255, 0, 0, 0, 0, 255, 0, 255, 0, 0, 255, 0, 255, 255, 0, 255};
    for (auto texture : textures)
    {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pattern);
    }
    bool textured = false, large = false;
    const ImU32 ground = IM_COL32(100, 120, 140, 255);
    auto capture = [&](bool ring, bool gpu)
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        alienorum::impostor_begin_frame();
        auto* draw = ImGui::GetBackgroundDrawList();
        draw->AddRectFilled({0, 0}, {width, height}, IM_COL32(15, 30, 45, 255));
        if (large)
        {
            for (int index = 0; index < 20000; ++index)
                draw->AddRectFilled({-20, -20}, {-10, -10}, IM_COL32(1, 2, 3, 255));
        }
        if (ring)
        {
            alienorum::RingImpostorInput input{};
            input.cz = 3;
            input.inner_r = .5;
            input.outer_r = 2;
            input.normal[1] = .6;
            input.normal[2] = .8;
            if (textured)
            {
                input.ring_map_texture = textures[0];
                input.ringx_map_texture = textures[1];
            }
            input.fallback_color = IM_COL32(180, 150, 120, 255);
            input.light_dir[2] = 1;
            input.amt_lit = 1;
            input.self_luminous = true;
            check(alienorum::queue_ring_impostor(input, 1, width / 2, height / 2, width / 2), "Ring queues");
        }
        if (gpu)
        {
            std::vector<double> horizon(width, height / 2);
            std::vector<int> top(width, height);
            check(alienorum::prepare_horizon_ground_gpu(width, height, {}, horizon, top, false), "Ground prepares");
            alienorum::queue_horizon_ground_gpu({{0, 0, width, height, ground}});
        }
        else draw->AddRectFilled({0, height / 2}, {width, height}, ground);
        ImGui::Render();
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        std::vector<unsigned char> pixels(width * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        check(glGetError() == GL_NO_ERROR, "No OpenGL error");
        return pixels;
    };
    for (int variant = 0; variant < 4; ++variant)
    {
        textured = variant & 1;
        large = variant & 2;
        for (bool gpu : {false, true})
        {
            const auto plain = capture(false, gpu);
            const auto rings = capture(true, gpu);
            int changed = 0;
            for (int y = 1; y < height / 2 - 1; ++y)
            {
                for (int x = 1; x < width - 1; ++x)
                {
                    for (int channel = 0; channel < 3; ++channel)
                        changed += plain[(y * width + x) * 4 + channel] != rings[(y * width + x) * 4 + channel];
                }
            }
            std::cout << (gpu ? "GPU" : "CPU") << " ground affected channels: " << changed << std::endl;
            check(changed == 0, "Opaque terrain hides rings behind it");
        }
    }
    glDeleteTextures(2, textures);
    alienorum::clear_horizon_ground_gpu();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
