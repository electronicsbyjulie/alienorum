#ifndef ALIENORUM_GROUNDDETAIL_H
#define ALIENORUM_GROUNDDETAIL_H
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace alienorum
{
    // A small repeatable material atlas. Generated once, then sampled with mip filtering.
    inline std::vector<unsigned char> ground_detail_atlas(int size = 128)
    {
        const auto random = [](int x, int y)
        {
            uint32_t hash = uint32_t(x & 127)*1597334677u ^ uint32_t(y & 127)*3812015801u;
            hash ^= hash >> 16; hash *= 2246822519u; hash ^= hash >> 13;
            return double(hash & 65535)/65535;
        };
        std::vector<unsigned char> pixels(size*size*4);
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const double u = double(x)/size, v = double(y)/size;
                const double grain = random(x, y)-.5;
                const double cluster = .25*sin(6.28318530718*u*3+sin(6.28318530718*v*2))
                    +.2*cos(6.28318530718*v*5+cos(6.28318530718*u*4));
                const double ripples = sin(6.28318530718*(v*24+ .17*sin(u*6.28318530718*4)));
                const double cracks = pow(fabs(sin(6.28318530718*u*8)*sin(6.28318530718*v*8)), .2);
                const double tufts = sin(6.28318530718*(u*32+v*8)) * sin(6.28318530718*(v*24-u*8));
                const double channels[] = {.5+.55*grain+.25*cluster, .5+.16*ripples+.14*grain,
                    .5+.25*(cracks-.7)+.2*grain, .5+.25*tufts+.35*cluster+.2*grain};
                for (int channel = 0; channel < 4; ++channel)
                    pixels[(y*size+x)*4+channel] = std::clamp(std::lround(channels[channel]*255), 0L, 255L);
            }
        }
        return pixels;
    }

    inline std::array<float, 4> ground_detail_material(double pressure_atm, bool vegetation)
    {
        pressure_atm = std::max(0.0, pressure_atm);
        const double vacuum = 1/(1+pressure_atm/.0003);
        const double dust = (1-vacuum)/(1+pressure_atm/.1);
        return {float(vacuum), float(dust), float(std::max(0.0, 1-vacuum-dust)), vegetation ? 1.f : 0.f};
    }
}
#endif
