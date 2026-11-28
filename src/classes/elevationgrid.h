#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <png.h>

namespace alienorum
{
    // Native-resolution numeric heights, independent of the globe's color texture.
    class ElevationGrid
    {
        std::vector<std::uint16_t> values;
        unsigned int width = 0, height = 0;
        double span = 0, offset = 0;
        bool reversed = false;

    public:
        bool empty() const { return values.empty(); }
        void clear() { values.clear(); width = height = 0; }
        void calibrate(double height_span, double zero_offset)
        {
            span = height_span;
            offset = zero_offset;
        }

        bool load(const std::string& filename, double height_span, double zero_offset, bool inverted = false)
        {
            if (!std::isfinite(height_span) || height_span < 0 || !std::isfinite(zero_offset)) return false;
            FILE* file = fopen(filename.c_str(), "rb");
            if (!file) return false;
            png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
            png_infop info = png ? png_create_info_struct(png) : nullptr;
            if (!info)
            {
                if (png) png_destroy_read_struct(&png, nullptr, nullptr);
                fclose(file);
                return false;
            }
            // Storage exists before setjmp so a decode error leaves normal C++ cleanup intact.
            std::vector<std::uint16_t> decoded;
            std::vector<unsigned char> row;
            if (setjmp(png_jmpbuf(png)))
            {
                png_destroy_read_struct(&png, &info, nullptr);
                fclose(file);
                return false;
            }
            png_init_io(png, file);
            png_read_info(png, info);
            const unsigned int w = png_get_image_width(png, info), h = png_get_image_height(png, info);
            if (!w || !h || w > 32768 || h > 16384
                || png_get_color_type(png, info) != PNG_COLOR_TYPE_GRAY
                || png_get_bit_depth(png, info) != 16
                || png_get_interlace_type(png, info) != PNG_INTERLACE_NONE)
            {
                png_destroy_read_struct(&png, &info, nullptr);
                fclose(file);
                return false;
            }
            decoded.resize(static_cast<size_t>(w) * h);
            row.resize(static_cast<size_t>(w) * 2);
            for (unsigned int y = 0; y < h; ++y)
            {
                png_read_row(png, row.data(), nullptr);
                for (unsigned int x = 0; x < w; ++x)
                {
                    const size_t index = static_cast<size_t>(y) * w + x;
                    decoded[inverted ? decoded.size() - 1 - index : index]
                        = (static_cast<unsigned int>(row[x * 2]) << 8) | row[x * 2 + 1];
                }
            }
            png_read_end(png, info);
            png_destroy_read_struct(&png, &info, nullptr);
            fclose(file);
            values = std::move(decoded);
            width = w;
            height = h;
            span = height_span;
            offset = zero_offset;
            reversed = inverted;
            return true;
        }

        bool save(const std::string& filename) const
        {
            if (empty()) return false;
            FILE* file = fopen(filename.c_str(), "wb");
            if (!file) return false;
            png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
            png_infop info = png ? png_create_info_struct(png) : nullptr;
            if (!info)
            {
                if (png) png_destroy_write_struct(&png, nullptr);
                fclose(file);
                return false;
            }
            std::vector<unsigned char> row(static_cast<size_t>(width) * 2);
            if (setjmp(png_jmpbuf(png)))
            {
                png_destroy_write_struct(&png, &info);
                fclose(file);
                return false;
            }
            png_init_io(png, file);
            png_set_IHDR(png, info, width, height, 16, PNG_COLOR_TYPE_GRAY,
                PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
            png_write_info(png, info);
            for (unsigned int y = 0; y < height; ++y)
            {
                for (unsigned int x = 0; x < width; ++x)
                {
                    const size_t index = static_cast<size_t>(y) * width + x;
                    const auto value = values[reversed ? values.size() - 1 - index : index];
                    row[x * 2] = value >> 8;
                    row[x * 2 + 1] = value & 255;
                }
                png_write_row(png, row.data());
            }
            png_write_end(png, info);
            png_destroy_write_struct(&png, &info);
            return fclose(file) == 0;
        }

        unsigned int get_width() const { return width; }
        unsigned int get_height() const { return height; }

        double elevation_at_index(size_t index) const
        {
            return (values[index] / 65535.0 - .5) * span + offset;
        }

        bool on_one_side_of_sea_level(double latitude, double longitude, double angle, bool above) const
        {
            constexpr double pi = 3.14159265358979323846;
            if (empty() || !std::isfinite(latitude) || !std::isfinite(longitude)
                || !std::isfinite(angle) || angle < 0 || fabs(latitude) + angle >= pi / 2) return false;
            const double longitude_radius = angle / cos(fabs(latitude) + angle);
            double wrapped = fmod(longitude + pi, 2 * pi);
            if (wrapped < 0) wrapped += 2 * pi;
            if (wrapped <= longitude_radius || wrapped + longitude_radius >= 2 * pi) return false;
            const long left = static_cast<long>(floor((wrapped - longitude_radius) * width / (2 * pi) - .5));
            const long right = static_cast<long>(floor((wrapped + longitude_radius) * width / (2 * pi) - .5)) + 1;
            const auto row = [&](double lat)
            {
                return static_cast<unsigned int>(std::clamp((pi / 2 - lat) * height / pi - .5, 0.0, double(height - 1)));
            };
            const auto top = row(latitude + angle);
            const auto bottom = std::min(row(latitude - angle) + 1, height - 1);
            if (right - left > 16 || bottom - top > 16) return false;
            // Bilinear interpolation stays within the extrema of the contributing cells.
            for (unsigned int y = top; y <= bottom; ++y)
            {
                for (long x = left; x <= right; ++x)
                {
                    const auto column = (x + width) % width;
                    const double elevation = elevation_at_index(static_cast<size_t>(y) * width + column);
                    if ((above ? elevation : -elevation) <= 1e-8) return false;
                }
            }
            return true;
        }

        double sample(double latitude, double longitude) const
        {
            if (empty() || !std::isfinite(latitude) || !std::isfinite(longitude)) return 0;
            constexpr double pi = 3.14159265358979323846;
            double wrapped = fmod(longitude + pi, 2 * pi);
            if (wrapped < 0) wrapped += 2 * pi;
            // Numeric maps describe pixel-centred cells, north first and -180 eastward.
            const double x = wrapped * width / (2 * pi) - .5;
            const double y = std::clamp((pi / 2 - latitude) * height / pi - .5, 0.0, double(height - 1));
            const long floor_x = static_cast<long>(floor(x));
            const unsigned int x0 = (floor_x + width) % width, x1 = (x0 + 1) % width;
            const unsigned int y0 = static_cast<unsigned int>(y), y1 = std::min(y0 + 1, height - 1);
            const double tx = x - floor_x, ty = y - y0;
            const double top = values[static_cast<size_t>(y0) * width + x0] * (1 - tx)
                + values[static_cast<size_t>(y0) * width + x1] * tx;
            const double bottom = values[static_cast<size_t>(y1) * width + x0] * (1 - tx)
                + values[static_cast<size_t>(y1) * width + x1] * tx;
            return ((top * (1 - ty) + bottom * ty) / 65535.0 - .5) * span + offset;
        }
    };
}
