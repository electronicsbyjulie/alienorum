#ifndef ALIENORUM_GALAXY_IMAGE_H
#define ALIENORUM_GALAXY_IMAGE_H
#include <SDL.h>
#include <algorithm>
#include <vector>

namespace alienorum
{
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
