#pragma once

#include "../classes/craterprofile.h"
#include "../classes/celestial.h"

class CraterLightingMap : public alienorum::Map
{
    public:
    using alienorum::Map::stamp_craters;
    CraterLightingMap(unsigned int height, bool polar = false, bool flat = false)
    {
        using namespace alienorum;
        image_height = height;
        image_width = 2 * height;
        allocated = image_width * image_height;
        lat_scale = image_height / _pi;
        lon_scale = image_width / (2 * _pi);
        inv_lat_scale = 1 / lat_scale;
        inv_lon_scale = 1 / lon_scale;
        red_data = new unsigned char[allocated];
        green_data = new unsigned char[allocated];
        blue_data = new unsigned char[allocated];
        bump_data = new double[allocated];
        surface_materials.assign(allocated, SurfaceMaterial::ground);
        for (unsigned long row = 0; row < image_height; ++row)
        {
            const double latitude = half_pi - row * _pi / image_height;
            for (unsigned long column = 0; column < image_width; ++column)
            {
                const double longitude = 2 * _pi * column / image_width - _pi;
                const double y = sin(latitude), z = cos(longitude) * cos(latitude);
                const double distance = acos(std::clamp(polar ? y : -z, -1.0, 1.0));
                const auto index = row * image_width + column;
                red_data[index] = green_data[index] = blue_data[index] = 160;
                bump_data[index] = flat ? 0 : CraterProfile::elevation(distance / .25, .035, .005, .2, 0);
            }
        }
        touch_gen();
    }
};
