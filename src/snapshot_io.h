#ifndef ALIENORUM_SNAPSHOT_IO_H
#define ALIENORUM_SNAPSHOT_IO_H
#include "background_file.h"
#include <png.h>
#include <vector>
#include <limits>

#ifndef PERF_P095
#define PERF_P095 1
#endif

namespace alienorum
{
    inline BackgroundFile& snapshot_writer()
    {
        static BackgroundFile writer;
        return writer;
    }
    inline BackgroundFile& layout_writer()
    {
        static BackgroundFile writer;
        return writer;
    }
    inline bool png_pixels(std::ostream* stream, FILE* file, int width, int height,
        const std::vector<unsigned char>& pixels)
    {
        if (width <= 0 || height <= 0 || size_t(width) > std::numeric_limits<size_t>::max() / 3 / size_t(height)
            || pixels.size() != size_t(width) * height * 3) return false;
        std::vector<png_bytep> rows(height);
        for (int y = 0; y < height; ++y)
            rows[y] = const_cast<png_bytep>(pixels.data() + size_t(height - 1 - y) * width * 3);
        png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        if (!png) return false;
        png_infop info = png_create_info_struct(png);
        if (!info) { png_destroy_write_struct(&png, nullptr); return false; }
        if (setjmp(png_jmpbuf(png)))
        {
            png_destroy_write_struct(&png, &info);
            return false;
        }
        if (file) png_init_io(png, file);
        else png_set_write_fn(png, stream, [](png_structp handle, png_bytep bytes, png_size_t length)
        {
            auto& output = *static_cast<std::ostream*>(png_get_io_ptr(handle));
            output.write(reinterpret_cast<char*>(bytes), length);
            if (!output) png_error(handle, "PNG output failed.");
        }, [](png_structp) {});
        png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
            PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_write_info(png, info);
        png_write_image(png, rows.data());
        png_write_end(png, nullptr);
        png_destroy_write_struct(&png, &info);
        return true;
    }
    inline bool save_snapshot_pixels(const std::string& filename, int width, int height,
        std::vector<unsigned char> pixels, bool background = PERF_P095)
    {
        if (width <= 0 || height <= 0 || size_t(width) > std::numeric_limits<size_t>::max() / 3 / size_t(height)
            || pixels.size() != size_t(width) * height * 3) return false;
        if (!background)
        {
            snapshot_writer().finish();
            FILE* file = fopen(filename.c_str(), "wb");
            if (!file) return false;
            const bool ok = png_pixels(nullptr, file, width, height, pixels);
            return fclose(file) == 0 && ok;
        }
        return snapshot_writer().submit([&]()
        {
            auto destination = std::filesystem::absolute(std::filesystem::u8path(filename));
            return [destination, width, height, pixels = std::move(pixels)]()
            {
                const bool ok = publish_file(destination, [&](std::ostream& stream)
                {
                    return png_pixels(&stream, nullptr, width, height, pixels);
                });
                if (ok) std::cout << "Saved " << destination.u8string() << " (" << width << 'x' << height << ')' << std::endl;
                else std::cerr << "Failed to save " << destination.u8string() << std::endl;
                return ok;
            };
        });
    }
    inline bool save_layout_text(const std::filesystem::path& path, std::string text)
    {
        return layout_writer().submit([&]()
        {
            auto destination = std::filesystem::absolute(path);
            return [destination, text = std::move(text)]()
            {
                return publish_file(destination, [&](std::ostream& stream)
                {
                    stream.write(text.data(), text.size());
                    return stream.good();
                });
            };
        });
    }
}
#endif
