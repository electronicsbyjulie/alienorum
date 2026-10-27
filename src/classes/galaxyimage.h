#ifndef ALIENORUM_GALAXY_IMAGE_H
#define ALIENORUM_GALAXY_IMAGE_H
#include <SDL.h>
#include <algorithm>
#include <vector>
#include <array>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <map>

namespace alienorum
{
    inline std::array<double, 4> galaxy_asset_frame(const std::string& path)
    {
        static std::map<std::string, std::array<double, 4>> frames;
        static std::string loaded_folder;
        const std::filesystem::path file(path);
        const std::string folder = file.parent_path().string();
        if (loaded_folder != folder)
        {
            frames.clear();
            std::ifstream source(file.parent_path()/"framing.dat");
            std::string row;
            while (std::getline(source, row))
            {
                if (row.empty() || row[0] == '#') continue;
                std::istringstream values(row);
                std::string name;
                double original_w, original_h, left, top, width, height;
                if (values >> std::quoted(name) >> original_w >> original_h >> left >> top >> width >> height
                    && original_w > 0 && original_h > 0 && width > 0 && height > 0)
                    frames[name] = {left/original_w, top/original_h, width/original_w, height/original_h};
            }
            loaded_folder = folder;
        }
        const auto found = frames.find(file.filename().string());
        return found == frames.end() ? std::array<double, 4>{0, 0, 1, 1} : found->second;
    }

    // Ignore dark padding and isolated bright pixels; retain a two-pixel interpolation margin.
    inline SDL_Rect galaxy_image_bounds(const SDL_Surface& image)
    {
        std::vector<int> rows(image.h), columns(image.w);
        for (int y = 0; y < image.h; ++y)
            for (int x = 0; x < image.w; ++x)
            {
                const auto* pixel = static_cast<const unsigned char*>(image.pixels)+y*image.pitch+x*4;
                if (pixel[3] && std::max({pixel[0], pixel[1], pixel[2]}) >= 8)
                {
                    ++rows[y];
                    ++columns[x];
                }
            }
        int left = image.w, right = -1, top = image.h, bottom = -1;
        for (int x = 0; x < image.w; ++x)
            if (columns[x] >= std::max(3, image.h/200))
            {
                left = std::min(left, x);
                right = x;
            }
        for (int y = 0; y < image.h; ++y)
            if (rows[y] >= std::max(3, image.w/200))
            {
                top = std::min(top, y);
                bottom = y;
            }
        if (right < left || bottom < top) return {0, 0, image.w, image.h};
        left = std::max(0, left-2);
        top = std::max(0, top-2);
        right = std::min(image.w-1, right+2);
        bottom = std::min(image.h-1, bottom+2);
        return {left, top, right-left+1, bottom-top+1};
    }
}
#endif
