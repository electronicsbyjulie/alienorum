#ifndef ALIENORUM_TEXTURE_UPLOAD_FIXTURE_H
#define ALIENORUM_TEXTURE_UPLOAD_FIXTURE_H

#include "../classes/celestial.h"
#include <stdexcept>

class TextureUploadTestMap : public alienorum::Map
{
    public:
    static constexpr unsigned long capacity = 1024 * 512;
    TextureUploadTestMap()
    {
        red_data = new unsigned char[capacity];
        green_data = new unsigned char[capacity];
        blue_data = new unsigned char[capacity];
        bump_data = new double[capacity];
        for (unsigned long i = 0; i < capacity; ++i)
        {
            red_data[i] = (i * 17) & 255;
            green_data[i] = (i * 9) & 255;
            blue_data[i] = (i * 5) & 255;
            bump_data[i] = (i % 257) / 256.0;
        }
        reshape(512, 256, 0);
    }
    void update(int phase)
    {
        const unsigned int value = static_cast<unsigned int>(phase + 2) & 255;
        red_data[0] = value;
        green_data[0] = (value * 7) & 255;
        blue_data[0] = (value * 13) & 255;
        bump_data[0] = value / 256.0;
        touch_gen();
    }
    void reshape(unsigned long width, unsigned long height, int phase)
    {
        if (!width || !height || width > capacity / height) throw std::length_error("Upload fixture dimensions exceed capacity.");
        image_width = width;
        image_height = height;
        allocated = width * height;
        lat_scale = image_height / _pi;
        lon_scale = image_width / (2 * _pi);
        update(phase);
    }
};
#endif
