#ifndef _PERLIN
#define _PERLIN

#include <iostream>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <random>
#include <vector>
#include <numeric>

namespace alienorum
{
    // Simple Perlin Noise implementation for self-contained compilation
    class PerlinNoise
    {
        protected:
        std::vector<int> p;

        public:
        PerlinNoise();
        PerlinNoise(uint32_t seed);
        void reseed(uint32_t seed);
        double fade(double t);
        double lerp(double t, double a, double b);
        double grad(int hash, double x, double y, double z);
        double noise(double x, double y, double z);
    };
}

#endif
