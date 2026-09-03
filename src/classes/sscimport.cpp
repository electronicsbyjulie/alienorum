
// Reads an .ssc add-on file -- the native scene format of another astronomy package -- and turns
// as much of it as this program has room for into our own objects: names, sizes, orbits,
// rotations, atmospheres, rings, and texture maps.
//
// Unit conventions on the .ssc side, which are not uniform and are the main thing this file is
// careful about:
//   Radius, ring radii, SemiMajorAxis around a planet   kilometres
//   SemiMajorAxis around a star                         AU
//   Period around a star                                Julian years
//   Period around anything else                         days
//   RotationPeriod, and a rotation model's Period       hours
//   PrecessionRate                                      radians per day
//   PrecessionPeriod                                    Julian years
//   BumpHeight                                          kilometres, black to white
//   a Location's LongLat                                degrees, degrees, kilometres
//   angles                                              degrees
//   Epoch                                               JD (default 2451545.0, i.e. J2000 noon;
//                                                       ours is midnight, so they are half a day
//                                                       apart and the value is passed through)

#include "sscimport.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

using namespace alienorum;

bool ssc_report_shown = false;
SSCImport last_ssc_import;


namespace
{

// ---------------------------------------------------------------- image reading and writing

struct SSCImage
{
    unsigned width = 0, height = 0;
    std::vector<unsigned char> rgba;                // width*height*4

    bool empty() const { return !width || !height || rgba.empty(); }
    const unsigned char* at(unsigned x, unsigned y) const { return &rgba[((size_t)y*width + x) * 4]; }
    unsigned char* at(unsigned x, unsigned y) { return &rgba[((size_t)y*width + x) * 4]; }
};

std::string lowercased(const std::string &s)
{
    std::string r = s;
    for (char &c : r) c = (char)tolower((unsigned char)c);
    return r;
}

std::string extension_of(const std::string &path)
{
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos) return "";
    if (slash != std::string::npos && dot < slash) return "";
    return lowercased(path.substr(dot));
}

bool read_png(const std::string &fname, SSCImage &img)
{
    FILE *fp = fopen(fname.c_str(), "rb");
    if (!fp) return false;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png)
    {
        fclose(fp);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info)
    {
        png_destroy_read_struct(&png, nullptr, nullptr);
        fclose(fp);
        return false;
    }
    if (setjmp(png_jmpbuf(png)))
    {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    // EXPAND brings palettes, sub-8-bit greys and tRNS chunks up to plain 8-bit channels, so the
    // loop below only ever has to deal with 8-bit RGB or RGBA.
    png_read_png(png, info,
        PNG_TRANSFORM_STRIP_16 | PNG_TRANSFORM_EXPAND | PNG_TRANSFORM_GRAY_TO_RGB | PNG_TRANSFORM_PACKING,
        nullptr);

    unsigned w = png_get_image_width(png, info);
    unsigned h = png_get_image_height(png, info);
    int channels = png_get_channels(png, info);
    png_bytepp rows = png_get_rows(png, info);

    img.width = w;
    img.height = h;
    img.rgba.assign((size_t)w * h * 4, 255);
    for (unsigned y=0; y<h; y++)
    {
        for (unsigned x=0; x<w; x++)
        {
            const png_byte *px = &rows[y][(size_t)x * channels];
            unsigned char *o = img.at(x, y);
            o[0] = px[0];
            o[1] = (channels >= 3) ? px[1] : px[0];
            o[2] = (channels >= 3) ? px[2] : px[0];
            o[3] = (channels == 4 || channels == 2) ? px[channels-1] : 255;
        }
    }

    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);
    return true;
}

struct SSCJpegErrorMgr
{
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

void ssc_jpeg_error_exit(j_common_ptr cinfo)
{
    SSCJpegErrorMgr *err = (SSCJpegErrorMgr*)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(err->setjmp_buffer, 1);
}

bool read_jpeg(const std::string &fname, SSCImage &img)
{
    FILE *fp = fopen(fname.c_str(), "rb");
    if (!fp) return false;

    struct jpeg_decompress_struct cinfo;
    SSCJpegErrorMgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = ssc_jpeg_error_exit;
    if (setjmp(jerr.setjmp_buffer))
    {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    img.width = cinfo.output_width;
    img.height = cinfo.output_height;
    img.rgba.assign((size_t)img.width * img.height * 4, 255);

    std::vector<unsigned char> row((size_t)cinfo.output_width * cinfo.output_components);
    unsigned char *rowp = row.data();
    while (cinfo.output_scanline < cinfo.output_height)
    {
        unsigned y = cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, &rowp, 1);
        for (unsigned x=0; x<img.width; x++)
        {
            unsigned char *o = img.at(x, y);
            o[0] = row[x*3 + 0];
            o[1] = row[x*3 + 1];
            o[2] = row[x*3 + 2];
            o[3] = 255;
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    return true;
}

uint32_t read_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Expands one BC1 colour block (the 8 bytes shared by DXT1/3/5) into 16 RGB texels. Alpha is left
// to the caller: DXT1 encodes it in the colour block's ordering, DXT3 and DXT5 carry their own.
void decode_bc1_colors(const unsigned char *b, unsigned char out[16][4], bool dxt1_alpha)
{
    uint16_t c0 = (uint16_t)(b[0] | (b[1] << 8));
    uint16_t c1 = (uint16_t)(b[2] | (b[3] << 8));

    int r[4], g[4], bl[4], a[4];
    auto unpack = [](uint16_t c, int &rr, int &gg, int &bb)
    {
        rr = ((c >> 11) & 0x1f) * 255 / 31;
        gg = ((c >>  5) & 0x3f) * 255 / 63;
        bb = ( c        & 0x1f) * 255 / 31;
    };
    unpack(c0, r[0], g[0], bl[0]);
    unpack(c1, r[1], g[1], bl[1]);
    a[0] = a[1] = a[2] = a[3] = 255;

    if (c0 > c1 || !dxt1_alpha)
    {
        r[2]  = (2*r[0]  + r[1] ) / 3;   g[2] = (2*g[0] + g[1]) / 3;   bl[2] = (2*bl[0] + bl[1]) / 3;
        r[3]  = (r[0]  + 2*r[1] ) / 3;   g[3] = (g[0] + 2*g[1]) / 3;   bl[3] = (bl[0] + 2*bl[1]) / 3;
    }
    else
    {
        r[2]  = (r[0]  + r[1] ) / 2;     g[2] = (g[0] + g[1]) / 2;     bl[2] = (bl[0] + bl[1]) / 2;
        r[3]  = g[3] = bl[3] = 0;
        a[3]  = 0;
    }

    uint32_t bits = read_le32(&b[4]);
    for (int i=0; i<16; i++)
    {
        int sel = (bits >> (i*2)) & 3;
        out[i][0] = (unsigned char)r[sel];
        out[i][1] = (unsigned char)g[sel];
        out[i][2] = (unsigned char)bl[sel];
        out[i][3] = (unsigned char)a[sel];
    }
}

// DDS is the one texture format .ssc add-ons use that neither libpng nor libjpeg will touch,
// and both compressed files in the Vulcan add-on are in it. Handles the three block formats that
// account for very nearly every add-on texture ever shipped -- DXT1, DXT3, DXT5 -- plus plain
// uncompressed 24- and 32-bit surfaces. A DX10-header file naming a format outside that set is
// reported as unsupported rather than decoded into garbage.
bool read_dds(const std::string &fname, SSCImage &img, std::string &why_not)
{
    std::ifstream fs(fname, std::ios::binary);
    if (!fs) return false;

    std::vector<unsigned char> data((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
    if (data.size() < 128 || memcmp(data.data(), "DDS ", 4))
    {
        why_not = "not a DDS file";
        return false;
    }

    unsigned h = read_le32(&data[12]);
    unsigned w = read_le32(&data[16]);
    uint32_t pf_flags = read_le32(&data[80]);
    uint32_t four_cc  = read_le32(&data[84]);
    uint32_t bit_count = read_le32(&data[88]);
    uint32_t rmask = read_le32(&data[92]), gmask = read_le32(&data[96]),
             bmask = read_le32(&data[100]), amask = read_le32(&data[104]);

    const uint32_t fcc_dxt1 = 0x31545844, fcc_dxt2 = 0x32545844, fcc_dxt3 = 0x33545844,
                   fcc_dxt4 = 0x34545844, fcc_dxt5 = 0x35545844, fcc_dx10 = 0x30315844;

    size_t offset = 128;
    if (four_cc == fcc_dx10)
    {
        if (data.size() < 148)
        {
            why_not = "truncated DX10 header";
            return false;
        }
        uint32_t dxgi = read_le32(&data[128]);
        offset = 148;
        // The BC1/BC2/BC3 DXGI codes, in both their UNORM and SRGB spellings.
        if      (dxgi == 71 || dxgi == 72) four_cc = fcc_dxt1;
        else if (dxgi == 74 || dxgi == 75) four_cc = fcc_dxt3;
        else if (dxgi == 77 || dxgi == 78) four_cc = fcc_dxt5;
        else
        {
            why_not = "DX10 format " + std::to_string(dxgi) + " is not one we decode";
            return false;
        }
    }

    if (!w || !h)
    {
        why_not = "zero-sized surface";
        return false;
    }

    img.width = w;
    img.height = h;
    img.rgba.assign((size_t)w * h * 4, 255);

    if (four_cc == fcc_dxt1 || four_cc == fcc_dxt2 || four_cc == fcc_dxt3
        || four_cc == fcc_dxt4 || four_cc == fcc_dxt5)
    {
        bool is_dxt1 = (four_cc == fcc_dxt1);
        size_t block_bytes = is_dxt1 ? 8 : 16;
        unsigned bw = (w + 3) / 4, bh = (h + 3) / 4;
        if (data.size() < offset + (size_t)bw * bh * block_bytes)
        {
            why_not = "truncated pixel data";
            return false;
        }

        for (unsigned by=0; by<bh; by++)
        {
            for (unsigned bx=0; bx<bw; bx++)
            {
                const unsigned char *blk = &data[offset + ((size_t)by * bw + bx) * block_bytes];
                unsigned char texels[16][4];
                unsigned char alpha[16];
                for (int i=0; i<16; i++) alpha[i] = 255;

                if (four_cc == fcc_dxt2 || four_cc == fcc_dxt3)
                {
                    // Explicit 4-bit alpha, two texels per byte.
                    for (int i=0; i<16; i++)
                    {
                        unsigned char nib = (i & 1) ? (blk[i/2] >> 4) : (blk[i/2] & 0x0f);
                        alpha[i] = (unsigned char)(nib * 17);
                    }
                }
                else if (four_cc == fcc_dxt4 || four_cc == fcc_dxt5)
                {
                    int a0 = blk[0], a1 = blk[1];
                    int lut[8];
                    lut[0] = a0;
                    lut[1] = a1;
                    if (a0 > a1) for (int i=1; i<=5; i++) lut[i+1] = ((6-i)*a0 + i*a1) / 7;
                    else
                    {
                        for (int i=1; i<=3; i++) lut[i+1] = ((4-i)*a0 + i*a1) / 5;
                        lut[6] = 0;
                        lut[7] = 255;
                    }
                    uint64_t abits = 0;
                    for (int i=0; i<6; i++) abits |= (uint64_t)blk[2+i] << (i*8);
                    for (int i=0; i<16; i++) alpha[i] = (unsigned char)lut[(abits >> (i*3)) & 7];
                }

                decode_bc1_colors(is_dxt1 ? blk : &blk[8], texels, is_dxt1);

                for (int i=0; i<16; i++)
                {
                    unsigned px = bx*4 + (i & 3), py = by*4 + (i >> 2);
                    if (px >= w || py >= h) continue;
                    unsigned char *o = img.at(px, py);
                    o[0] = texels[i][0];
                    o[1] = texels[i][1];
                    o[2] = texels[i][2];
                    o[3] = is_dxt1 ? texels[i][3] : alpha[i];
                }
            }
        }
        return true;
    }

    if ((pf_flags & 0x40) && (bit_count == 24 || bit_count == 32))       // DDPF_RGB
    {
        size_t stride = (size_t)w * (bit_count / 8);
        if (data.size() < offset + stride * h)
        {
            why_not = "truncated pixel data";
            return false;
        }
        auto shift_of = [](uint32_t mask)
        {
            int s = 0;
            if (!mask) return -1;
            while (!(mask & 1)) { mask >>= 1; s++; }
            return s;
        };
        int rs = shift_of(rmask), gs = shift_of(gmask), bs = shift_of(bmask), as = shift_of(amask);
        for (unsigned y=0; y<h; y++)
        {
            for (unsigned x=0; x<w; x++)
            {
                const unsigned char *px = &data[offset + y*stride + (size_t)x * (bit_count/8)];
                uint32_t v = (bit_count == 32) ? read_le32(px)
                                               : ((uint32_t)px[0] | ((uint32_t)px[1] << 8) | ((uint32_t)px[2] << 16));
                unsigned char *o = img.at(x, y);
                o[0] = (rs >= 0) ? (unsigned char)((v & rmask) >> rs) : 0;
                o[1] = (gs >= 0) ? (unsigned char)((v & gmask) >> gs) : 0;
                o[2] = (bs >= 0) ? (unsigned char)((v & bmask) >> bs) : 0;
                o[3] = (as >= 0) ? (unsigned char)((v & amask) >> as) : 255;
            }
        }
        return true;
    }

    why_not = "unsupported pixel format";
    return false;
}

bool read_image(const std::string &path, SSCImage &img, std::string &why_not)
{
    std::string ext = extension_of(path);
    if (ext == ".png") { if (read_png(path, img)) return true; why_not = "PNG could not be read"; return false; }
    if (ext == ".jpg" || ext == ".jpeg")
    {
        if (read_jpeg(path, img)) return true;
        why_not = "JPEG could not be read";
        return false;
    }
    if (ext == ".dds") return read_dds(path, img, why_not);
    why_not = "no reader for " + (ext.size() ? ext : std::string("that file type"));
    return false;
}

bool write_png(const std::string &fname, const SSCImage &img)
{
    if (img.empty()) return false;

    FILE *fp = fopen(fname.c_str(), "wb");
    if (!fp) return false;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png)
    {
        fclose(fp);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info)
    {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        return false;
    }
    if (setjmp(png_jmpbuf(png)))
    {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, img.width, img.height, 8, PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<unsigned char> row((size_t)img.width * 3);
    for (unsigned y=0; y<img.height; y++)
    {
        for (unsigned x=0; x<img.width; x++)
        {
            const unsigned char *px = img.at(x, y);
            row[x*3 + 0] = px[0];
            row[x*3 + 1] = px[1];
            row[x*3 + 2] = px[2];
        }
        png_write_row(png, row.data());
    }

    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return true;
}

// ---------------------------------------------------------------- normal map to height map

bool is_power_of_two(unsigned n) { return n && !(n & (n-1)); }

void fft_1d(std::complex<float> *a, size_t n, bool inverse)
{
    for (size_t i=1, j=0; i<n; i++)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    for (size_t len=2; len<=n; len <<= 1)
    {
        double ang = 2 * _pi / (double)len * (inverse ? 1 : -1);
        std::complex<float> wlen((float)cos(ang), (float)sin(ang));
        for (size_t i=0; i<n; i+=len)
        {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k=0; k<len/2; k++)
            {
                std::complex<float> u = a[i+k], v = a[i+k+len/2] * w;
                a[i+k] = u + v;
                a[i+k+len/2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) for (size_t i=0; i<n; i++) a[i] /= (float)n;
}

void fft_2d(std::vector<std::complex<float>> &a, unsigned w, unsigned h, bool inverse)
{
    for (unsigned y=0; y<h; y++) fft_1d(&a[(size_t)y * w], w, inverse);

    std::vector<std::complex<float>> col(h);
    for (unsigned x=0; x<w; x++)
    {
        for (unsigned y=0; y<h; y++) col[y] = a[(size_t)y * w + x];
        fft_1d(col.data(), h, inverse);
        for (unsigned y=0; y<h; y++) a[(size_t)y * w + x] = col[y];
    }
}

void halve(SSCImage &img)
{
    SSCImage out;
    out.width = img.width / 2;
    out.height = img.height / 2;
    out.rgba.assign((size_t)out.width * out.height * 4, 255);
    for (unsigned y=0; y<out.height; y++)
    {
        for (unsigned x=0; x<out.width; x++)
        {
            int acc[4] = {0,0,0,0};
            for (int dy=0; dy<2; dy++) for (int dx=0; dx<2; dx++)
            {
                const unsigned char *p = img.at(x*2+dx, y*2+dy);
                for (int c=0; c<4; c++) acc[c] += p[c];
            }
            unsigned char *o = out.at(x, y);
            for (int c=0; c<4; c++) o[c] = (unsigned char)(acc[c] / 4);
        }
    }
    img = out;
}

// An .ssc file states relief as a tangent-space normal map; we state it as an elevation, so the
// gradient field has to be integrated back into the surface it came from. That is a Poisson
// problem, solved here the Frankot-Chellappa way: take the FFT of the two gradient components,
// divide by the frequency, transform back. It is the least-squares best surface whose gradient
// matches, which is the right answer for a field that -- being a painted normal map rather than a
// measured one -- is not exactly integrable in the first place.
//
// Two things make the sphere different from the flat plate the method assumes. Longitude really
// does wrap, so the horizontal periodicity the FFT imposes is free; latitude does not, so the
// field is mirrored top-to-bottom into a doubled image (negating the vertical gradient in the
// reflected half, which is what makes the seam continuous) and only the top half is kept. And an
// equirectangular grid's columns crowd together towards the poles as cos(latitude), so the
// horizontal gradient is weighted by that before integrating -- without it, polar detail is
// stretched into streaks.
//
// The result is relative: elevations come out in arbitrary units and are normalized into the
// 0-255 the bump loader reads, where 128 is the datum and the full range spans
// Planet::estimate_bump_scale() metres. The true vertical scale is not in a normal map to recover.
bool normal_map_to_height(SSCImage src, SSCImage &height_out, std::string &why_not)
{
    while (src.width > 2048 && !(src.width & 1) && !(src.height & 1)) halve(src);

    unsigned w = src.width, h = src.height;
    if (!is_power_of_two(w) || !is_power_of_two(h))
    {
        why_not = "resolution " + std::to_string(w) + "x" + std::to_string(h) + " is not a power of two";
        return false;
    }

    unsigned h2 = h * 2;                                    // mirrored, so the field is periodic vertically
    std::vector<std::complex<float>> P((size_t)w * h2), Q((size_t)w * h2);

    for (unsigned y=0; y<h2; y++)
    {
        bool mirrored = (y >= h);
        unsigned sy = mirrored ? (h2 - 1 - y) : y;
        double lat = (0.5 - ((double)sy + 0.5) / h) * _pi;
        double coslat = cos(lat);

        for (unsigned x=0; x<w; x++)
        {
            const unsigned char *px = src.at(x, sy);
            double nx = px[0] / 127.5 - 1.0;
            double ny = px[1] / 127.5 - 1.0;
            double nz = px[2] / 127.5 - 1.0;
            if (nz < 0.01) nz = 0.01;                       // a normal lying flat says nothing about height

            // Green up is the OpenGL convention these normal maps follow, so +G points north
            // and rows run the other way.
            double p = -nx / nz * coslat;
            double q =  ny / nz;
            if (mirrored) q = -q;

            P[(size_t)y * w + x] = std::complex<float>((float)p, 0.0f);
            Q[(size_t)y * w + x] = std::complex<float>((float)q, 0.0f);
        }
    }

    fft_2d(P, w, h2, false);
    fft_2d(Q, w, h2, false);

    for (unsigned y=0; y<h2; y++)
    {
        double wy = 2 * _pi * ((y < h2/2) ? (double)y : (double)y - (double)h2) / (double)h2;
        for (unsigned x=0; x<w; x++)
        {
            double wx = 2 * _pi * ((x < w/2) ? (double)x : (double)x - (double)w) / (double)w;
            double denom = wx*wx + wy*wy;
            size_t i = (size_t)y * w + x;
            if (denom < 1e-12)
            {
                P[i] = std::complex<float>(0.0f, 0.0f);     // the mean elevation, which is our datum
                continue;
            }
            std::complex<float> minus_i(0.0f, -1.0f);
            P[i] = (minus_i * (float)wx * P[i] + minus_i * (float)wy * Q[i]) / (float)denom;
        }
    }

    fft_2d(P, w, h2, true);

    double sum = 0, sumsq = 0;
    size_t n = (size_t)w * h;
    for (unsigned y=0; y<h; y++) for (unsigned x=0; x<w; x++)
    {
        double v = P[(size_t)y * w + x].real();
        sum += v;
        sumsq += v*v;
    }
    double mean = sum / n;
    double sigma = sqrt(fmax(0.0, sumsq/n - mean*mean));

    double peak = 0;
    for (unsigned y=0; y<h; y++) for (unsigned x=0; x<w; x++)
        peak = fmax(peak, fabs(P[(size_t)y * w + x].real() - mean));
    // A handful of outliers -- one bad texel in a painted normal map is enough -- would otherwise
    // crush every real feature into a couple of grey levels, so the range is capped at four sigma
    // and anything beyond it clips.
    double scale = (sigma > 0) ? fmin(peak, 4.0 * sigma) : peak;
    if (scale <= 0)
    {
        why_not = "the normal map is flat";
        return false;
    }

    height_out.width = w;
    height_out.height = h;
    height_out.rgba.assign((size_t)w * h * 4, 255);
    for (unsigned y=0; y<h; y++)
    {
        for (unsigned x=0; x<w; x++)
        {
            double v = 128.0 + 127.0 * (P[(size_t)y * w + x].real() - mean) / scale;
            unsigned char g = (unsigned char)fmax(0.0, fmin(255.0, v));
            unsigned char *o = height_out.at(x, y);
            o[0] = o[1] = o[2] = g;
        }
    }
    return true;
}

// ---------------------------------------------------------------- .ssc parsing

void skip_space(const std::string &s, size_t &i)
{
    while (i < s.size())
    {
        if (s[i] == '#')
        {
            while (i < s.size() && s[i] != '\n') i++;
        }
        else if (isspace((unsigned char)s[i])) i++;
        else break;
    }
}

bool parse_quoted(const std::string &s, size_t &i, std::string &out)
{
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    out.clear();
    while (i < s.size() && s[i] != '"')
    {
        if (s[i] == '\\' && i+1 < s.size()) i++;
        out += s[i++];
    }
    if (i >= s.size()) return false;
    i++;
    return true;
}

bool parse_value(const std::string &s, size_t &i, json &out);

bool parse_braced(const std::string &s, size_t &i, json &out)
{
    i++;                                                // past '{'
    out = json::object();
    for (;;)
    {
        skip_space(s, i);
        if (i >= s.size()) return false;
        if (s[i] == '}') { i++; return true; }

        std::string key;
        if (s[i] == '"')
        {
            if (!parse_quoted(s, i, key)) return false;
        }
        else
        {
            size_t start = i;
            while (i < s.size() && (isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
            if (i == start) return false;
            key = s.substr(start, i - start);
        }

        skip_space(s, i);
        json val;
        if (!parse_value(s, i, val)) return false;
        out[key] = val;
    }
}

bool parse_value(const std::string &s, size_t &i, json &out)
{
    skip_space(s, i);
    if (i >= s.size()) return false;

    if (s[i] == '"')
    {
        std::string str;
        if (!parse_quoted(s, i, str)) return false;
        out = str;
        return true;
    }
    if (s[i] == '{') return parse_braced(s, i, out);
    if (s[i] == '[')
    {
        i++;
        out = json::array();
        for (;;)
        {
            skip_space(s, i);
            if (i >= s.size()) return false;
            if (s[i] == ']') { i++; return true; }
            if (s[i] == ',') { i++; continue; }
            json el;
            if (!parse_value(s, i, el)) return false;
            out.push_back(el);
        }
    }
    if (isalpha((unsigned char)s[i]))
    {
        size_t start = i;
        while (i < s.size() && isalnum((unsigned char)s[i])) i++;
        std::string word = s.substr(start, i - start);
        std::string lw = lowercased(word);
        if (lw == "true")  { out = true;  return true; }
        if (lw == "false") { out = false; return true; }
        out = word;                                     // an unquoted token; kept as a string
        return true;
    }

    const char *begin = s.c_str() + i;
    char *end = nullptr;
    double v = strtod(begin, &end);
    if (end == begin) return false;
    i += (size_t)(end - begin);
    out = v;
    return true;
}

// Older .ssc files have no Mass field at all, and habitually write the
// figure into the comment beside Radius instead ("# Mass=9.113 Earths (density = 5.517 gm/cm^3").
// It is the only statement of mass those files contain, and mass decides what kind of world
// classify() thinks this is, so it is worth reading.
void scan_mass_hint(const std::string &text, SSCBlock &blk)
{
    size_t at = 0;
    while ((at = text.find("Mass", at)) != std::string::npos)
    {
        size_t j = at + 4;
        while (j < text.size() && (isspace((unsigned char)text[j]) || text[j] == '=')) j++;
        char *end = nullptr;
        double v = strtod(text.c_str() + j, &end);
        if (end && end != text.c_str() + j && v > 0)
        {
            size_t k = (size_t)(end - text.c_str());
            while (k < text.size() && isspace((unsigned char)text[k])) k++;
            if (!text.compare(k, 5, "Earth")) blk.mass_hint_earths = v;
        }
        at += 4;
    }

    at = 0;
    while ((at = text.find("density", at)) != std::string::npos)
    {
        size_t j = at + 7;
        while (j < text.size() && (isspace((unsigned char)text[j]) || text[j] == '=')) j++;
        char *end = nullptr;
        double v = strtod(text.c_str() + j, &end);
        if (end && end != text.c_str() + j && v > 0 && v < 30) blk.density_hint = v;
        at += 7;
    }
}

bool parse_ssc(const std::string &text, std::vector<SSCBlock> &blocks, std::string &error)
{
    size_t i = 0;
    for (;;)
    {
        skip_space(text, i);
        if (i >= text.size()) return true;

        SSCBlock blk;
        size_t block_start = i;

        // Up to two bare words stand in front of the name: what to do with the definition
        // (Add, Modify, Replace) and what kind of thing is being defined (a body unless it
        // says AltSurface, Location, ReferencePoint or SurfaceObject). Either may be left out,
        // so each word is placed by what it says rather than by where it sits.
        while (i < text.size() && isalpha((unsigned char)text[i]))
        {
            size_t start = i;
            while (i < text.size() && isalpha((unsigned char)text[i])) i++;
            std::string word = text.substr(start, i - start);
            std::string lw = lowercased(word);
            if (lw == "add" || lw == "modify" || lw == "replace") blk.disposition = word;
            else blk.item_type = word;
            skip_space(text, i);
        }

        if (!parse_quoted(text, i, blk.name))
        {
            error = "expected a quoted object name at byte " + std::to_string(i);
            return false;
        }
        skip_space(text, i);
        if (!parse_quoted(text, i, blk.parent))
        {
            error = "expected a quoted parent name for \"" + blk.name + "\"";
            return false;
        }
        skip_space(text, i);
        if (i >= text.size() || text[i] != '{')
        {
            error = "expected '{' after \"" + blk.name + "\"";
            return false;
        }
        if (!parse_braced(text, i, blk.fields))
        {
            error = "malformed definition for \"" + blk.name + "\"";
            return false;
        }

        scan_mass_hint(text.substr(block_start, i - block_start), blk);
        blocks.push_back(blk);
    }
}

// A sibling star-catalog file: same format family as the .ssc itself (a leading catalog number,
// a quoted name that may carry ':'-separated aliases the way a body's name does, then a braced
// field list), but always flat -- one star per entry, nothing nested under it -- since a star
// catalog has no orbits of its own to place things in. Reuses the .ssc block reader's own
// primitives (skip_space/parse_quoted/parse_braced) rather than a second parser.
bool parse_stc(const std::string &text, std::vector<std::pair<std::string, json>> &out)
{
    size_t i = 0;
    for (;;)
    {
        skip_space(text, i);
        if (i >= text.size()) return true;

        if (isdigit((unsigned char)text[i]) || text[i] == '-')
        {
            while (i < text.size() && (isdigit((unsigned char)text[i]) || text[i] == '-')) i++;
            skip_space(text, i);
        }

        std::string name;
        if (!parse_quoted(text, i, name)) return false;
        skip_space(text, i);
        if (i >= text.size() || text[i] != '{') return false;

        json fields;
        if (!parse_braced(text, i, fields)) return false;
        out.push_back({name, fields});
    }
}

// ---------------------------------------------------------------- field access

bool get_num(const json &j, const char *key, double &out)
{
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return false;
    out = it->get<double>();
    return true;
}

bool get_str(const json &j, const char *key, std::string &out)
{
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}

bool get_bool(const json &j, const char *key, bool &out)
{
    auto it = j.find(key);
    if (it == j.end() || !it->is_boolean()) return false;
    out = it->get<bool>();
    return true;
}

const json* get_obj(const json &j, const char *key)
{
    auto it = j.find(key);
    if (it == j.end() || !it->is_object()) return nullptr;
    return &(*it);
}

// [ x y z ], which is how the format states every vector it has: a colour, a set of axes, a
// place on a surface.
bool get_vec3(const json &j, const char *key, double out[3])
{
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 3) return false;
    for (int i=0; i<3; i++)
    {
        if (!(*it)[i].is_number()) return false;
        out[i] = (*it)[i].get<double>();
    }
    return true;
}

// The file paints a body it has no texture for in a flat colour, and paints the dot the body
// shrinks to at a distance in that same colour. We say the second of those in one number
// instead -- B-V, which color_from_magnitude_indices() turns back into exactly that dot. Its
// two-index form builds blue as magnbase^-(B-V) and red as magnbase^(B-V) against a fixed
// green, so the whole of B-V sits in the red-to-blue ratio and recovering it is one division.
// The stated triple is display-side, i.e. gamma-encoded, and undoing that leaves the gamma
// behind as a plain factor on the logarithm. Green carries nothing a one-number colour can
// hold, and is dropped.
double bv_from_ssc_color(const double rgb[3])
{
    // A channel at zero would send the logarithm to infinity, and a colour written as zero
    // still lands on the darkest step the screen has rather than on no light at all.
    double red = fmax(rgb[0], 1.0/255), blue = fmax(rgb[2], 1.0/255);
    double bv = 0.5 * get_gamma() * log(red / blue) * invlogmagnbase - bv_correction;
    return fmax(-0.5, fmin(2.5, bv));
}

// ---------------------------------------------------------------- the import itself

std::string parent_directory(const std::string &path)
{
    size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
}

// An .ssc file names a texture by bare filename, found under textures/<resolution>/, preferring the
// highest resolution present. Same order here, plus the add-on root, because some add-ons drop
// their maps straight beside the .ssc.
std::string find_texture(const std::string &base_dir, const std::string &fname)
{
    if (fname.empty()) return "";
    static const char *dirs[] = { "/textures/hires/", "/textures/medres/", "/textures/lores/",
                                  "/textures/", "/" };
    for (const char *d : dirs)
    {
        std::string candidate = base_dir + d + fname;
        if (file_exists(candidate.c_str())) return candidate;
    }
    return "";
}

std::string map_path(const CelestialObject *cel, const char *suffix, const char *ext)
{
    return std::string("maps") + _FSSTR + std::string(cel->name) + std::string(suffix) + std::string(ext);
}

}   // anonymous namespace

// Nothing here ever overwrites a map that is already on disk unless the user has asked for it:
// the maps folder holds hand-made and hard-won textures, and an import that silently replaced one
// would be unrecoverable. Returns false, with a note, when it declines.
bool SSCImport::may_write_map(const std::string &dest)
{
    if (!file_exists(dest.c_str())) return true;
    if (overwrite_maps) return true;
    report.note(dest + " already exists and was left alone (File > Overwrite Map Files On Import to replace).");
    return false;
}

bool SSCImport::copy_file(const std::string &from, const std::string &to)
{
    std::ifstream in(from, std::ios::binary);
    if (!in) return false;
    std::ofstream out(to, std::ios::binary);
    if (!out) return false;
    out << in.rdbuf();
    return out.good();
}

// A texture that we could hand to the loader unchanged is copied rather than re-encoded -- a
// 2048x1024 JPEG is a few hundred kilobytes and the PNG it would become is several megabytes, and
// the re-encode would cost a generation of quality for nothing.
bool SSCImport::install_plain_texture(const std::string &src, CelestialObject *cel, const char *suffix)
{
    std::string ext = extension_of(src);
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png")
    {
        std::string dest = map_path(cel, suffix, (ext == ".png") ? ".png" : ".jpg");
        if (!may_write_map(dest)) return false;
        if (!copy_file(src, dest))
        {
            report.note(std::string("Could not write ") + dest + ".");
            return false;
        }
        report.textures_written++;
        return true;
    }

    SSCImage img;
    std::string why;
    if (!read_image(src, img, why))
    {
        report.note(src + ": " + why + "; skipped.");
        return false;
    }
    std::string dest = map_path(cel, suffix, ".png");
    if (!may_write_map(dest)) return false;
    if (!write_png(dest, img))
    {
        report.note(std::string("Could not write ") + dest + ".");
        return false;
    }
    report.textures_written++;
    return true;
}

// An .ssc file paints its clouds as a separate translucent shell over the surface; we have one day-lit
// map per body, and cloud_map is what a world made entirely of weather uses instead of a surface,
// not a layer above one. So for a body with ground underneath, the cloud layer is composited into
// the surface map here -- which is what it looks like from orbit anyway -- rather than imported as
// a cloud map that would hide the ground completely.
//
// TODO: this is a stop-gap, and a regression against what the renderer is meant to grow into.
// Overlaying clouds on the surface from space, and drawing them as a generated cloudy sky from the
// ground, is on the list. When that lands, drop the compositing here and import the cloud map as
// its own layer -- the file has already told us which texture is which.
bool SSCImport::install_composited_surface(const std::string &surf_src, const std::string &cloud_src,
    CelestialObject *cel)
{
    SSCImage surf, cloud;
    std::string why;
    if (!read_image(surf_src, surf, why))
    {
        report.note(surf_src + ": " + why + "; skipped.");
        return false;
    }
    if (!read_image(cloud_src, cloud, why))
    {
        report.note(cloud_src + ": " + why + "; surface imported without its clouds.");
        return install_plain_texture(surf_src, cel, "_surf");
    }

    std::string dest = map_path(cel, "_surf", ".png");
    if (!may_write_map(dest)) return false;

    for (unsigned y=0; y<surf.height; y++)
    {
        unsigned cy = (unsigned)((uint64_t)y * cloud.height / surf.height);
        for (unsigned x=0; x<surf.width; x++)
        {
            unsigned cx = (unsigned)((uint64_t)x * cloud.width / surf.width);
            const unsigned char *c = cloud.at(cx, cy);
            unsigned char *o = surf.at(x, y);
            double a = c[3] / 255.0;
            for (int k=0; k<3; k++) o[k] = (unsigned char)(o[k] * (1.0 - a) + c[k] * a);
        }
    }

    if (!write_png(dest, surf))
    {
        report.note(std::string("Could not write ") + dest + ".");
        return false;
    }
    report.textures_written++;
    return true;
}

// Our ring strip runs from the planet's own equatorial radius out to the outer edge, with the gap
// between surface and inner edge carried by the transparency map; an .ssc file's runs from inner edge
// to outer edge and carries transparency in its alpha channel. So the strip is rebuilt rather than
// copied, and split into the colour map and the transparency map the renderer reads separately.
bool SSCImport::install_ring_textures(const std::string &src, Planet *pl, double inner_m, double outer_m)
{
    SSCImage ring;
    std::string why;
    if (!read_image(src, ring, why))
    {
        report.note(src + ": " + why + "; a ring pattern was generated instead.");
        return false;
    }

    // Our ring transparency map is exactly that -- a per-radius transparency channel -- so the
    // conversion below only means something if the source actually carries one. A texture with
    // no alpha channel at all decodes here as uniformly opaque (see read_png/read_jpeg/read_dds
    // above, which default every pixel's alpha to 255 when the format has none), and copying that
    // in would draw a ring with no falloff at either edge: the same solid-disc result a missing
    // ring texture gives. Caught here by the same test either failure produces -- no real
    // variation in the decoded alpha -- rather than by asking each decoder what it saw.
    unsigned char amin = 255, amax = 0;
    for (size_t i = 3; i < ring.rgba.size(); i += 4)
    {
        if (ring.rgba[i] < amin) amin = ring.rgba[i];
        if (ring.rgba[i] > amax) amax = ring.rgba[i];
    }
    if ((int)amax - (int)amin < 8)
    {
        report.note(src + " carries no real transparency data (its opacity is uniform); "
            "a ring pattern was generated instead.");
        return false;
    }

    std::string dest = map_path(pl, "_ring", ".png"), destx = map_path(pl, "_ringx", ".png");
    // Evaluated separately (not short-circuited), so a run where only one of the two already
    // exists still gets its own "already exists" note for that specific file -- but the decision
    // to write is taken as a pair below: color and transparency are sampled together at render
    // time, so replacing only the half that happens to be missing would pair fresh content
    // against an existing file it was never generated alongside, from a possibly unrelated
    // source. An existing file on either side means the whole pair is left alone.
    bool dest_writable = may_write_map(dest), destx_writable = may_write_map(destx);
    bool may_write_pair = dest_writable && destx_writable;

    // These ring textures are strips: one axis is radius, the other is a couple of texels of
    // padding. Whichever axis is longer is the radial one.
    bool radial_is_x = (ring.width >= ring.height);
    unsigned radial_n = radial_is_x ? ring.width : ring.height;

    double eqr = pl->get_equatorial_radius();
    if (outer_m <= eqr) return false;

    const unsigned out_w = 1024, out_h = 29;
    SSCImage colors, alphas;
    colors.width = alphas.width = out_w;
    colors.height = alphas.height = out_h;
    colors.rgba.assign((size_t)out_w * out_h * 4, 255);
    alphas.rgba.assign((size_t)out_w * out_h * 4, 255);

    for (unsigned x=0; x<out_w; x++)
    {
        double r = eqr + (outer_m - eqr) * ((double)x + 0.5) / out_w;
        unsigned char rgb[3] = {225, 208, 192};
        double a = 0;

        if (r >= inner_m)
        {
            double f = (r - inner_m) / (outer_m - inner_m);
            unsigned s = (unsigned)fmin((double)radial_n - 1, f * radial_n);
            // Average the strip's short axis, so a texture with any real content across it is not
            // decided by whichever row we happened to land on.
            double acc[4] = {0,0,0,0};
            unsigned across = radial_is_x ? ring.height : ring.width;
            for (unsigned t=0; t<across; t++)
            {
                const unsigned char *px = radial_is_x ? ring.at(s, t) : ring.at(t, s);
                for (int k=0; k<4; k++) acc[k] += px[k];
            }
            for (int k=0; k<3; k++) rgb[k] = (unsigned char)(acc[k] / across);
            a = acc[3] / across / 255.0;
        }

        for (unsigned y=0; y<out_h; y++)
        {
            unsigned char *c = colors.at(x, y);
            c[0] = rgb[0];
            c[1] = rgb[1];
            c[2] = rgb[2];
            // The renderer reads opacity as 1 - (green/255)^gossamer, so white is clear and black
            // is solid -- the opposite way round from the alpha channel it came out of.
            unsigned char t = (unsigned char)fmax(0.0, fmin(255.0, 255.0 * (1.0 - a)));
            unsigned char *xp = alphas.at(x, y);
            xp[0] = xp[1] = xp[2] = t;
        }
    }

    // !may_write_pair means an existing file was deliberately left alone (may_write_map() already
    // noted which one) -- not a failure. The ring still ends up with a real texture, whether it
    // is the pair already on disk or the one just decoded from the add-on, so the caller should
    // not treat this as a reason to fall back to a generated ring.
    if (!may_write_pair) return true;

    if (!write_png(dest, colors))
    {
        report.note(std::string("Could not write ") + dest + ".");
        return false;
    }
    if (!write_png(destx, alphas))
    {
        report.note(std::string("Could not write ") + destx + ".");
        return false;
    }
    report.textures_written += 2;
    return true;
}

// Builds a ring pattern with the app's own procedural generator and saves it alongside the other
// imported maps, wherever may_write_map() allows -- used whenever the add-on's own ring texture
// is missing, undecodable, or (per install_ring_textures() above) carries no real transparency
// data to convert. Mirrors exactly how celestial.cpp builds a ring for a body it is texturing
// itself: replace ring_map/ringx_map, call Map::generate_ring_map() with the geometry already
// settled on this body, then save what came out.
void SSCImport::install_procedural_ring(Planet *pl)
{
    if (pl->ring_map) delete pl->ring_map;
    if (pl->ringx_map) delete pl->ringx_map;

    pl->ring_map = new Map(pl);
    pl->ringx_map = new Map(pl);
    int res = (int)(pl->ring_radius / pl->volumetric_mean_radius * 1024);
    pl->ring_map->generate_ring_map(pl, res, pl->ring_inner_radius / pl->ring_radius,
        pl->ring_mean_opacity, pl->ringx_map);

    std::string dest = map_path(pl, "_ring", ".png"), destx = map_path(pl, "_ringx", ".png");
    // Same pairing rule as install_ring_textures(): color and transparency here come out of one
    // generate_ring_map() call together, so an existing file on either side means the disk copy
    // of the pair is left as it is rather than mixed with a freshly generated other half.
    bool dest_writable = may_write_map(dest), destx_writable = may_write_map(destx);
    if (!(dest_writable && destx_writable)) return;

    if (pl->ring_map->save_to_png(dest) && pl->ringx_map->save_to_png(destx)) report.textures_written += 2;
}

bool SSCImport::install_bump_from_normal_map(const std::string &src, CelestialObject *cel)
{
    SSCImage normals;
    std::string why;
    if (!read_image(src, normals, why))
    {
        report.note(src + ": " + why + "; no relief imported.");
        return false;
    }

    std::string dest = map_path(cel, "_bump", ".png");
    if (!may_write_map(dest)) return false;

    SSCImage heights;
    if (!normal_map_to_height(normals, heights, why))
    {
        report.note(std::string(cel->name) + ": normal map not integrated (" + why + ").");
        return false;
    }
    if (!write_png(dest, heights))
    {
        report.note(std::string("Could not write ") + dest + ".");
        return false;
    }
    report.bumps_built++;
    return true;
}

// A relief map states its heights outright, which is the same thing our own bump data holds, so
// unlike a normal map it arrives ready to use and all it wants is a vertical scale. The file's
// BumpHeight is that scale in kilometres from black to white; ours is fixed per body by
// estimate_bump_scale(), which reads a radius and an air pressure rather than the picture. So the
// grey levels are stretched about their midpoint to make the two agree, and the stated relief
// survives instead of being quietly replaced by ours.
bool SSCImport::install_bump_from_height_map(const std::string &src, CelestialObject *cel,
    double bump_height_km)
{
    SSCImage heights;
    std::string why;
    if (!read_image(src, heights, why))
    {
        report.note(src + ": " + why + "; no relief imported.");
        return false;
    }

    std::string dest = map_path(cel, "_bump", ".png");
    if (!may_write_map(dest)) return false;

    double ours = ((Planet*)cel)->estimate_bump_scale();
    double stretch = (bump_height_km > 0 && ours > 0) ? (bump_height_km * 1000.0 / ours) : 1.0;

    for (unsigned y=0; y<heights.height; y++)
    {
        for (unsigned x=0; x<heights.width; x++)
        {
            unsigned char *px = heights.at(x, y);
            double grey = (_lum_r_comp*px[0] + _lum_g_comp*px[1] + _lum_b_comp*px[2]) / 255.0;
            grey = 0.5 + (grey - 0.5) * stretch;
            unsigned char lvl = (unsigned char)(255.0 * fmax(0.0, fmin(1.0, grey)) + 0.5);
            px[0] = px[1] = px[2] = lvl;
            px[3] = 255;
        }
    }

    if (!write_png(dest, heights))
    {
        report.note(std::string("Could not write ") + dest + ".");
        return false;
    }
    report.bumps_built++;
    return true;
}

// The pictures. The surface map goes to _surf for a world with ground and to _clouds for one that
// is nothing but weather, which is where our renderer looks for each; the relief comes from a
// height map if the file has one and from an integrated normal map if it has only that.
void SSCImport::install_body_textures(const json &fields, const json *atmos, CelestialObject *cel,
    bool all_weather)
{
    std::string name = cel->name;
    std::string texname, cloudname, nightname, normalname, bumpname;
    get_str(fields, "Texture", texname);
    get_str(fields, "NightTexture", nightname);
    get_str(fields, "NormalMap", normalname);
    get_str(fields, "BumpMap", bumpname);
    if (atmos) get_str(*atmos, "CloudMap", cloudname);

    std::string tex_path = find_texture(base_dir, texname);
    std::string cloud_path = find_texture(base_dir, cloudname);

    if (texname.size() && tex_path.empty())
        report.note("Texture " + texname + " for \"" + name + "\" is not in the add-on; skipped.");
    if (cloudname.size() && cloud_path.empty())
        report.note("Cloud map " + cloudname + " for \"" + name + "\" is not in the add-on; skipped.");

    if (tex_path.size())
    {
        if (all_weather) install_plain_texture(tex_path, cel, "_clouds");
        else if (cloud_path.size())
        {
            if (install_composited_surface(tex_path, cloud_path, cel))
                report.note(name + ": its cloud layer was composited into the "
                    "surface map, which is the only place we can put it.");
        }
        else install_plain_texture(tex_path, cel, "_surf");
    }
    else if (cloud_path.size()) install_plain_texture(cloud_path, cel, "_clouds");

    if (nightname.size())
    {
        std::string np = find_texture(base_dir, nightname);
        if (np.size()) install_plain_texture(np, cel, "_night");
        else report.note("Night texture " + nightname + " for \"" + name + "\" is not in the add-on; skipped.");
    }

    // Relief belongs to a body with a surface to carve; a comet is a coma with a speck in it, and
    // nothing downstream would ever read the map.
    cel_obj_class cls = cel->typeclass();
    if (cls != class_planet && cls != class_moon) return;

    double bump_height = 2.0;                           // the format's own default
    get_num(fields, "BumpHeight", bump_height);

    if (bumpname.size())
    {
        std::string bp = find_texture(base_dir, bumpname);
        if (bp.size()) install_bump_from_height_map(bp, cel, bump_height);
        else report.note("Relief map " + bumpname + " for \"" + name + "\" is not in the add-on; skipped.");
        if (normalname.size())
            report.note(name + " states both a relief map and a normal map; the relief map was used.");
    }
    else if (normalname.size())
    {
        std::string np = find_texture(base_dir, normalname);
        if (np.size()) install_bump_from_normal_map(np, cel);
        else report.note("Normal map " + normalname + " for \"" + name + "\" is not in the add-on; skipped.");
    }
}

// Everything the format can state that has nothing on our side to correspond to. Named once per
// body rather than left to vanish: an add-on that says a thing and is never told it was dropped
// looks, from the outside, exactly like one that was read in full.
void SSCImport::note_dropped_fields(const json &fields, const json *atmos, const json *rings,
    const std::string &name, const char *skip)
{
    // Meshes and everything that places or scales one; the four maps and three constants of a
    // lighting model we do not run; a light curve stated as a scattering law rather than as a
    // slope; the bond albedo, which we work out from the geometry instead; the flags and dates
    // and web pages an object can carry.
    static const char *body_fields[] =
    {
        "Mesh", "MeshCenter", "MeshScale", "NormalizeMesh", "Orientation",
        "SpecularTexture", "SpecularColor", "SpecularPower", "OverlayTexture", "BlendTexture",
        "LunarLambert", "BondAlbedo", "Visible", "InfoURL", "OrbitColor", "HazeColor",
        "Beginning", "Ending", "Timeline",
        "CustomOrbit", "SpiceOrbit", "ScriptedOrbit", "FixedPosition",
        "SampledOrientation", "ScriptedRotation",
    };
    // A sky we build out of a pressure, a composition and a haze fraction, rather than out of
    // stated colours at stated heights.
    static const char *atmos_fields[] =
    {
        "Height", "CloudHeight", "CloudSpeed", "CloudNormalMap", "Absorption",
        "MieScaleHeight", "MieAsymmetry", "Lower", "Upper", "Sky", "Sunset",
    };

    std::string dropped;
    auto add = [&dropped](const std::string &what)
    {
        if (dropped.size()) dropped += ", ";
        dropped += what;
    };

    for (const char *f : body_fields)
    {
        if (skip && !strcmp(skip, f)) continue;
        if (fields.contains(f)) add(f);
    }
    if (atmos) for (const char *f : atmos_fields) if (atmos->contains(f)) add(std::string("Atmosphere ") + f);
    if (rings && rings->contains("Color")) add("Rings Color");

    if (dropped.size())
        report.note("\"" + name + "\": the file also specifies " + dropped
            + ", which we have no equivalent for; ignored.");
}

// A Location is a named place on a body's surface, and we have those: they are what the sun clock
// labels, and what the viewer can stand on. The block gives a longitude, a latitude and an
// altitude; we keep the first two.
void SSCImport::attach_locations(std::map<std::string, std::vector<Locale>> &pending,
    std::map<std::string, CelestialObject*> &imported)
{
    for (auto &entry : pending)
    {
        const std::string &path = entry.first;
        CelestialObject *cel = nullptr;

        auto here = imported.find(path);
        if (here != imported.end()) cel = here->second;
        else
        {
            // A place may sit on a body this file never defines -- an add-on that does nothing
            // but name craters on our own Moon is a perfectly ordinary one -- so the last name
            // in the path is looked for among the objects we already have.
            size_t slash = path.find_last_of('/');
            std::string leaf = first_alias((slash == std::string::npos) ? path : path.substr(slash+1));
            int idx = find_object(leaf.c_str(), false, 9e29, 0);
            if (idx >= 0) cel = cels[idx];
        }

        if (!cel)
        {
            report.note("The places named on \"" + path + "\" have nowhere to go: no such body; skipped.");
            continue;
        }

        // read_locales() fills a body's places the first time anyone looks at it, and only a body
        // that has none -- so a body given one place here would otherwise never be given the
        // hundreds it already has a right to. Its own list is fetched first, and these join it.
        if (!cel->nlocales) cel->read_locales("locales.json");
        int have = cel->nlocales;
        Locale *all = new Locale[have + entry.second.size()];
        for (int i=0; i<have; i++) all[i] = cel->locales[i];
        for (size_t i=0; i<entry.second.size(); i++) all[have+i] = entry.second[i];
        delete[] cel->locales;
        cel->locales = all;
        cel->nlocales = have + (int)entry.second.size();

        report.note(std::to_string(entry.second.size()) + " place(s) on \"" + std::string(cel->name)
            + "\" were read. Their time zones are local mean solar time, the file stating none, and "
            "places are not written to universe.json -- they last as long as this session.");
    }
}

std::string SSCImport::first_alias(const std::string &ssc_name)
{
    size_t colon = ssc_name.find(':');
    return (colon == std::string::npos) ? ssc_name : ssc_name.substr(0, colon);
}

// update_location() is not virtual, and the three classes that have one mean genuinely different
// things by it -- a moon's is the only one that supplies the Laplace plane its orbit is measured
// against, and calling the Planet one on a moon throws. So the class decides which is called.
void SSCImport::update_body_location(CelestialObject *cel)
{
    switch (cel->typeclass())
    {
        case class_planet:    ((Planet*)cel)->update_location(J2000_TIME_T);    break;
        case class_moon:      ((Moon*)cel)->update_location(J2000_TIME_T);      break;
        case class_comet:     ((Comet*)cel)->update_location(J2000_TIME_T);     break;
        case class_satellite: ((Satellite*)cel)->update_location(J2000_TIME_T); break;
        default: break;
    }
}

// A body's parent is always found, one way or another: a real catalog star first, then a star
// this same add-on defines in a sibling star-catalog file, and only failing both of those a
// plausible invented one -- never a hard failure that drops everything orbiting it.
CelestialObject* SSCImport::resolve_star(const std::string &ssc_name)
{
    auto found = star_cache.find(ssc_name);
    if (found != star_cache.end()) return found->second;

    std::string want = first_alias(ssc_name);
    if (want == "Sol") want = "Sun";                    // the one name the two programs disagree on

    int idx = find_object(want.c_str(), true, 9e29, 2);
    CelestialObject *star = (idx >= 0) ? cels[idx] : nullptr;

    if (star)
        report.note("Star \"" + ssc_name + "\" resolved to " + std::string(star->name) + ".");
    else
    {
        star = find_star_in_stc_files(ssc_name);
        if (!star) star = create_fictitious_star(ssc_name);
    }

    star_cache[ssc_name] = star;
    return star;
}

CelestialObject* SSCImport::find_star_in_stc_files(const std::string &ssc_name)
{
    std::error_code ec;
    std::filesystem::directory_iterator it(base_dir, ec), end;
    if (ec) return nullptr;

    std::string want = first_alias(ssc_name);
    for (; it != end && !ec; it.increment(ec))
    {
        if (!it->is_regular_file()) continue;
        if (lowercased(it->path().extension().string()) != ".stc") continue;

        std::ifstream fs(it->path(), std::ios::binary);
        if (!fs) continue;
        std::string text((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());

        std::vector<std::pair<std::string, json>> records;
        if (!parse_stc(text, records)) continue;

        for (auto &rec : records)
        {
            // The record's own name may itself carry ':'-separated aliases (the star-catalog
            // format's "Bajor:B'hava'el" is exactly the ':'-alias convention a body's name uses),
            // so every alias is a candidate match, not just the one the file leads with.
            size_t from = 0;
            while (from <= rec.first.size())
            {
                size_t colon = rec.first.find(':', from);
                std::string one = rec.first.substr(from, (colon == std::string::npos) ? std::string::npos : colon - from);
                if (one == want || one == ssc_name)
                {
                    CelestialObject *star = create_star_from_stc_record(rec.second, rec.first);
                    if (star)
                        report.note("Star \"" + ssc_name + "\" has no catalog entry, but "
                            + it->path().filename().string() + " in the add-on defines it; that definition was used.");
                    return star;
                }
                if (colon == std::string::npos) break;
                from = colon + 1;
            }
        }
    }
    return nullptr;
}

CelestialObject* SSCImport::create_star_from_stc_record(const json &fields, const std::string &raw_name)
{
    std::string name = first_alias(raw_name);
    if (name.size() >= name_max_len) name = name.substr(0, name_max_len-1);

    Star *s = new Star();
    strcpy(s->name, name.c_str());
    s->namelen = 0;
    s->origname = name;

    double ra_deg = 0, dec_deg = 0, distance_ly = 0;
    get_num(fields, "RA", ra_deg);
    get_num(fields, "Dec", dec_deg);
    get_num(fields, "Distance", distance_ly);
    get_num(fields, "AppMag", s->apparent_magnitude);
    s->right_ascension = ra_deg * fiftyseventh;
    s->declination = dec_deg * fiftyseventh;
    s->distance = distance_ly * light_year;
    s->distance_known = true;

    std::string sptyp;
    if (get_str(fields, "SpectralType", sptyp))
    {
        if (sptyp.size() >= sizeof(s->spectral_type)) sptyp = sptyp.substr(0, sizeof(s->spectral_type)-1);
        strcpy(s->spectral_type, sptyp.c_str());
        double msqi = Star::get_mseqidx_from_sptyp(s->spectral_type);
        if (msqi >= mseqmin && msqi <= mseqmax) s->BV_color = Star::interpolate_mseq_BV(msqi);
    }

    // Same distance-modulus algebra CelestialObject::to_json()'s callers use elsewhere in this
    // codebase, just run on real apparent-magnitude-and-distance data instead of a catalog's own
    // parallax.
    double intrinsic_brightness = pow(magnbase, -s->apparent_magnitude) * pow(fmax(AU, s->distance) / parsec / 10, 2);
    s->absolute_magnitude = -log(intrinsic_brightness) * invlogmagnbase;

    if (!cels[0])
    {
        // estimate_mass()/estimate_radius() fall back to a comparison against the Sun when the
        // main-sequence color lookup itself fails, and throw rather than dereference a null Sun
        // if that ever happens with no catalogs loaded. It should not happen for an ordinary
        // dwarf spectral type, but nothing here guarantees the add-on gave us one.
        delete s;
        return nullptr;
    }
    s->mass = s->estimate_mass();
    s->volumetric_mean_radius = s->estimate_radius();
    s->temperature = s->estimate_temperature();
    s->estimate_UB();
    s->user_added = true;
    s->user_edited = true;

    if (!append_cel(s))
    {
        delete s;
        return nullptr;
    }
    s->update_location(J2000_TIME_T);
    return s;
}

// The add-on names a star neither we nor it can supply a definition for. Refusing to import
// anything under it would throw away everything the file DOES tell us -- the planets' names,
// sizes, atmospheres, texture maps -- over one missing line, so a Sun-like G2 V star is invented
// in its place instead. G2 V specifically because these add-ons are near-universally designed
// with real-world distances in mind: a "habitable" world here is placed at roughly 1 AU because
// that is the habitable zone of a Sun-like star, not because the file says so anywhere -- Risa
// orbits its own primary at exactly 1 AU, Bajor III at 1.6. A Sun-like default makes that design
// intent come out right without this importer having to go looking at texture names to guess at
// habitability itself.
CelestialObject* SSCImport::create_fictitious_star(const std::string &raw_name)
{
    if (!cels[0]) return nullptr;                       // see create_star_from_stc_record()

    std::string name = first_alias(raw_name);
    if (name.size() >= name_max_len) name = name.substr(0, name_max_len-1);

    // Deterministic placement from the name (not random), so re-importing the same file lands
    // this star in the same empty patch of sky rather than drifting, and two different invented
    // stars don't stack exactly on top of each other.
    uint32_t h = 2166136261u;
    for (unsigned char c : name) h = (h ^ c) * 16777619u;

    Star *s = new Star();
    strcpy(s->name, name.c_str());
    s->namelen = 0;
    s->origname = name;
    s->right_ascension = (double)(h % 360000u) / 1000.0 * fiftyseventh;
    s->declination = ((double)((h / 360000u) % 180000u) / 1000.0 - 90.0) * fiftyseventh;
    s->distance = 200.0 * light_year;                    // arbitrary, just out of the way
    s->distance_known = true;
    strcpy(s->spectral_type, "G2 V");

    double msqi = Star::get_mseqidx_from_sptyp(s->spectral_type);
    if (msqi >= mseqmin && msqi <= mseqmax) s->BV_color = Star::interpolate_mseq_BV(msqi);
    s->mass = s->estimate_mass();
    s->volumetric_mean_radius = s->estimate_radius();
    s->temperature = s->estimate_temperature();
    s->estimate_UB();

    // The Sun's own true absolute V magnitude, so "Sun-like" is not just its spectral class but
    // its actual brightness too; apparent magnitude then follows from that and the distance just
    // chosen above, rather than the other way around, since neither one is an observation.
    s->absolute_magnitude = 4.83;
    double dist_pc = fmax(s->distance / parsec, 1.0);
    s->apparent_magnitude = s->absolute_magnitude + 5.0 * log10(dist_pc / 10.0);

    s->user_added = true;
    s->user_edited = true;

    if (!append_cel(s))
    {
        delete s;
        return nullptr;
    }
    s->update_location(J2000_TIME_T);

    report.note("Star \"" + raw_name + "\" is not in our catalogs, and the add-on carries no "
        "definition for it either, so a Sun-like G2 V star was invented in its place.");
    return s;
}

cel_obj_class SSCImport::class_from_ssc(const json &fields, const CelestialObject *parent)
{
    std::string cls;
    bool emissive = false;
    get_bool(fields, "Emissive", emissive);
    std::cout << "Emissive: " << (emissive ? "Y" : "N") << std::endl;
    if (get_str(fields, "Class", cls))
    {
        cls = lowercased(cls);
        if (cls == "spacecraft") return class_satellite;
        if (cls == "comet") return class_comet;
        if (cls == "moon" || cls == "minormoon") return class_moon;
        if (cls == "planet" || cls == "dwarfplanet" || cls == "asteroid" || cls == "minorbody")
            return class_planet;
        if (emissive) return class_star;
        return class_unknown;                           // surface, invisible, component, ...
    }

    if (fields.contains("Mesh")) return class_satellite;
    if (parent && parent->typeclass() == class_satellite) return class_satellite;
    if (emissive) return class_star;
    return (parent && parent->typeclass() == class_star) ? class_planet : class_moon;
}

void SSCImport::apply_orbit(const json &orb, CelestialObject *cel, CelestialObject *parent)
{
    bool around_star = (parent->typeclass() == class_star);
    double v;

    cel->orbit = new Orbit();
    cel->orbit->center = parent;
    cel->orbit->center_name = parent->name;

    if (get_num(orb, "SemiMajorAxis", v)) cel->orbit->semimajor_axis = v * (around_star ? AU : 1000.0);
    if (get_num(orb, "PericenterDistance", v)) cel->orbit->periapsis_distance = v * (around_star ? AU : 1000.0);
    if (get_num(orb, "Eccentricity", v)) cel->orbit->eccentricity = v;
    if (get_num(orb, "Inclination", v)) cel->orbit->inclination = v * fiftyseventh;
    if (get_num(orb, "AscendingNode", v)) cel->orbit->ascending_node = v * fiftyseventh;
    if (get_num(orb, "MeanAnomaly", v)) cel->orbit->mean_anomaly = v * fiftyseventh;
    if (get_num(orb, "Epoch", v)) cel->orbit->epoch = v;
    else cel->orbit->epoch = 2451545.0;                 // the format's default, half a day off ours

    if (get_num(orb, "Period", v))
        cel->orbit->period = v * (around_star ? (365.25 * oneday) : oneday);

    // The format lets an orbit be stated with longitudes measured from the reference direction
    // instead of from the node. Both reduce to what we store.
    double node_deg = cel->orbit->ascending_node * fiftyseven;
    if (get_num(orb, "ArgOfPericenter", v)) cel->orbit->arg_periapsis = v * fiftyseventh;
    else if (get_num(orb, "LongOfPericenter", v)) cel->orbit->arg_periapsis = (v - node_deg) * fiftyseventh;

    double peri_deg = cel->orbit->arg_periapsis * fiftyseven + node_deg;
    if (get_num(orb, "MeanLongitude", v)) cel->orbit->mean_anomaly = (v - peri_deg) * fiftyseventh;

    if (!cel->orbit->semimajor_axis && cel->orbit->periapsis_distance && cel->orbit->eccentricity < 1)
        cel->orbit->semimajor_axis = cel->orbit->periapsis_distance / (1.0 - cel->orbit->eccentricity);

    if (!cel->orbit->period && !cel->orbit->semimajor_axis)
        report.note(std::string(cel->name) + " has an orbit with neither a period nor a size; one will be invented.");
    
    cel->cenobj = cel->orbit->center;
    int prevent_infloop_circref = 0;
    while (cel->cenobj->orbit && cel->cenobj->orbit->center)
    {
        cel->cenobj = cel->cenobj->orbit->center;
        prevent_infloop_circref++;
        if (prevent_infloop_circref > 1000)
        {
            report.note(std::string(cel->name) + " has a circular reference in its orbit hierarchy.");
        }
    }
}

void SSCImport::apply_rotation(const json &fields, CelestialObject *cel)
{
    double v;
    double meridian_epoch = J2000;                      // JD the prime meridian below is quoted at

    if (get_num(fields, "RotationPeriod", v)) cel->sidereal_rotational_period = v * 3600.0;
    if (get_num(fields, "Obliquity", v)) cel->obliquity = v * fiftyseventh;
    if (get_num(fields, "EquatorAscendingNode", v)) cel->equinox = v * fiftyseventh;
    // What the same angle was called before EquatorAscendingNode replaced it, and what the
    // older add-ons in circulation still write. It sits where the newer name sits, next to
    // Obliquity, and means the same thing.
    else if (get_num(fields, "LongOfRotationAxis", v)) cel->equinox = v * fiftyseventh;
    if (get_num(fields, "RotationOffset", v)) cel->lon_J2000_offset = v * fiftyseventh;
    if (get_num(fields, "RotationEpoch", v)) meridian_epoch = v;

    // The axis's own drift. Theirs is how fast the equatorial node moves, in radians a day;
    // ours is how fast that node moves *backwards*, per second -- update_orbit_location() reads
    // equinox_eff = equinox - precession * seconds -- so the sign turns over on the way in.
    if (get_num(fields, "PrecessionRate", v)) cel->precession = -v / oneday;

    // Newer .ssc files state all of the above as a rotation model instead: one block, of one of
    // three kinds. Its Inclination and AscendingNode place the pole exactly as Obliquity and
    // EquatorAscendingNode do, and MeridianAngle is where the prime meridian sits at the block's
    // own Epoch, which is our lon_J2000_offset once carried back to J2000.
    bool precessing = false, fixed = false;
    const json *rot = get_obj(fields, "UniformRotation");
    if (!rot && (rot = get_obj(fields, "PrecessingRotation"))) precessing = true;
    if (!rot && (rot = get_obj(fields, "FixedRotation"))) fixed = true;

    if (rot)
    {
        if (get_num(*rot, "Period", v)) cel->sidereal_rotational_period = v * 3600.0;
        else if (!fixed && cel->orbit && cel->orbit->period)
        {
            // A rotation model that states no period at all is how the format says "tidally
            // locked", and that is as much a statement about the spin as a number would be.
            cel->sidereal_rotational_period = cel->orbit->period;
        }
        if (get_num(*rot, "Inclination", v)) cel->obliquity = v * fiftyseventh;
        if (get_num(*rot, "AscendingNode", v)) cel->equinox = v * fiftyseventh;
        if (get_num(*rot, "MeridianAngle", v)) cel->lon_J2000_offset = v * fiftyseventh;
        if (get_num(*rot, "Epoch", v)) meridian_epoch = v;

        // How long the axis takes to go round once, in years -- which is the very form we hold
        // the same quantity in on the way to and from universe.json.
        if (precessing && get_num(*rot, "PrecessionPeriod", v) && v)
            cel->precession = (_pi * 2) / (v * oneyear);

        if (fixed)
            report.note(std::string(cel->name) + " is stated as not rotating at all, which we have "
                "no way of saying; it turns like any other body here.");
    }

    // Both ways of stating the meridian quote it at an epoch of their own choosing, ours is
    // quoted at J2000, and between the two lies some whole number of turns and a fraction. Only
    // the fraction moves the meridian, and only if we know how long a turn takes.
    if (meridian_epoch != J2000 && cel->sidereal_rotational_period)
    {
        double turns = (J2000 - meridian_epoch) * oneday / cel->sidereal_rotational_period;
        cel->lon_J2000_offset += (turns - floor(turns)) * (_pi * 2);
        cel->lon_J2000_offset = fmod(cel->lon_J2000_offset, _pi * 2);
        if (cel->lon_J2000_offset < 0) cel->lon_J2000_offset += _pi * 2;
    }

    cel->known_poles = true;
}

// Which plane a moon's orbital elements are measured in. Theirs is a named reference frame,
// ours is one of three planes, and they agree on the two that come up: elements given in the
// ecliptic frame are elements in the parent's own orbital plane, and elements given in a body's
// equatorial frame -- what the format falls back on for anything orbiting a planet, and what an
// Earth satellite's frame amounts to as well -- are elements in the parent's equatorial plane.
void SSCImport::apply_orbit_frame(const json &fields, CelestialObject *cel)
{
    if (cel->typeclass() != class_moon) return;
    const json *frame = get_obj(fields, "OrbitFrame");
    if (!frame) return;

    if (frame->contains("EclipticJ2000")) ((Moon*)cel)->orbit_type = ot_ecliptic;
    else if (frame->contains("EquatorJ2000") || frame->contains("MeanEquator")
        || frame->contains("BodyFixed")) ((Moon*)cel)->orbit_type = ot_equatorial;
}

// Applied after classify(), which sets a colour of its own from the body type: a colour the
// add-on went to the trouble of stating outranks one we inferred from a mass and a radius.
void SSCImport::apply_color(const json &fields, CelestialObject *cel)
{
    double rgb[3];
    if (get_vec3(fields, "Color", rgb)) cel->BV_color = bv_from_ssc_color(rgb);
}

// An .ssc file states a radius and, occasionally, a mass or a density. We have to end up with a mass
// either way, because it is what classify() reads to decide what kind of world this is. Preference
// order: a stated mass, a stated density against the volume, the "Mass=... Earths" comment older
// add-ons carry, and failing all of those our own mass-radius relation run backwards.
void SSCImport::establish_mass(Planet *pl, const json &fields, const SSCBlock &blk)
{
    double v;

    if (get_num(fields, "Mass", v) && v > 0)
    {
        pl->mass = v * earth_mass;
        return;
    }
    if (get_num(fields, "Density", v) && v > 0)
    {
        pl->mass = sphere_volume(pl->volumetric_mean_radius) * v * 1e-3;    // kg/m^3 -> g/cm^3
        return;
    }
    if (blk.mass_hint_earths > 0)
    {
        pl->mass = blk.mass_hint_earths * earth_mass;
        report.note(std::string(pl->name) + ": mass taken from the file's own comment ("
            + std::to_string(blk.mass_hint_earths) + " Earths).");
        return;
    }
    if (blk.density_hint > 0)
    {
        pl->mass = sphere_volume(pl->volumetric_mean_radius) * blk.density_hint;
        return;
    }

    // estimate_radius() run backwards, using the same two branches and the same exponents, so an
    // imported world sits where our own mass-radius relation would put it.
    double r = pl->volumetric_mean_radius;
    double rocky_ceiling = 1.02 * earth_radius * pow(rocky_mass_cutoff / earth_mass, 0.27);
    if (r <= rocky_ceiling) pl->mass = earth_mass * pow(r / (1.02 * earth_radius), 1.0/0.27);
    else pl->mass = earth_mass * pow(r / (0.56 * earth_radius), 1.0/0.67);
    report.note(std::string(pl->name) + ": the file states no mass, so one was derived from its radius.");
}

bool SSCImport::read(const std::string &ssc_path)
{
    report = SSCImportReport();
    report.source = ssc_path;

    std::ifstream fs(ssc_path, std::ios::binary);
    if (!fs)
    {
        report.note("Could not open " + ssc_path + ".");
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
    fs.close();

    std::vector<SSCBlock> blocks;
    std::string error;
    if (!parse_ssc(text, blocks, error))
    {
        report.note("Could not read the file: " + error + ".");
        return false;
    }
    if (blocks.empty())
    {
        report.note("The file defines no objects.");
        return false;
    }

    base_dir = parent_directory(ssc_path);
    std::map<std::string, CelestialObject*> imported;    // keyed by full Celestia path
    std::map<std::string, std::vector<Locale>> places;   // likewise, and applied once the bodies exist
    bool place_detail_dropped = false;
    std::vector<CelestialObject*> fresh;

    // A moon may be defined before the planet it belongs to, so the list is walked repeatedly
    // until a pass places nothing new. Whatever is left after that has a parent the file never
    // defines and our catalogs do not have.
    std::vector<bool> done(blocks.size(), false);
    bool progress = true;
    while (progress)
    {
        progress = false;
        for (size_t bi=0; bi<blocks.size(); bi++)
        {
            if (done[bi]) continue;
            SSCBlock &blk = blocks[bi];

            // Not every block defines a body. A place on a surface is one of ours and is set
            // aside until the body it stands on has been made; the other kinds -- an extra skin
            // for a body, a thing standing on one -- are nothing we can hold.
            std::string kind = lowercased(blk.item_type);
            if (kind == "location")
            {
                done[bi] = true;
                progress = true;

                // Two spellings of the same three numbers: the older LongLat, and the fixed
                // position in body coordinates that replaced it.
                double longlat[3];
                const json *fixed_at = get_obj(blk.fields, "FixedPosition");
                if (!get_vec3(blk.fields, "LongLat", longlat)
                    && !(fixed_at && get_vec3(*fixed_at, "Planetographic", longlat)))
                {
                    report.note("\"" + first_alias(blk.name) + "\" is a place whose position is "
                        "stated in a way we cannot read; skipped.");
                    continue;
                }

                // A place here is a name and a point, with no extent and no kind of thing it is.
                if (blk.fields.contains("Size") || blk.fields.contains("Importance")
                    || blk.fields.contains("Type")) place_detail_dropped = true;

                Locale place;
                place.name = first_alias(blk.name);
                place.lon = longlat[0];                 // east longitude, degrees, as ours are
                place.lat = longlat[1];
                // The format has no time zones. Local mean solar time is the one answer that does
                // not pick a civil convention out of the air for a world that may not have one.
                place.tz = longlat[0] / 15.0 * 3600.0;
                place.user_added = true;
                places[blk.parent].push_back(place);
                continue;
            }
            if (kind == "altsurface" || kind == "surfaceobject")
            {
                done[bi] = true;
                progress = true;
                report.note("\"" + first_alias(blk.name) + "\" is " + blk.item_type
                    + ", which has no equivalent here; skipped.");
                report.bodies_skipped++;
                continue;
            }

            CelestialObject *parent = nullptr;
            std::string parent_path = blk.parent;
            auto already = imported.find(parent_path);
            if (already != imported.end()) parent = already->second;
            else if (parent_path.find('/') == std::string::npos)
            {
                parent = resolve_star(parent_path);
                if (!parent)
                {
                    done[bi] = true;
                    report.bodies_skipped++;
                    progress = true;
                    continue;
                }
            }
            else continue;                              // its parent has not been created yet

            done[bi] = true;
            progress = true;

            std::string name = first_alias(blk.name);
            std::string full_path = parent_path + "/" + blk.name;

            cel_obj_class cls = class_from_ssc(blk.fields, parent);
            if (cls == class_unknown)
            {
                std::string what;
                get_str(blk.fields, "Class", what);
                report.note("\"" + name + "\" is a foreign " + (what.size() ? what : "object")
                    + ", which has no equivalent here; skipped.");
                report.bodies_skipped++;
                continue;
            }

            if (!lowercased(blk.disposition).compare(0, 6, "modify"))
                report.note("\"" + name + "\" is a Modify directive; it was imported as a new object instead.");

            CelestialObject *cel = nullptr;
            switch (cls)
            {
                case class_star:      cel = new Star();      cel->type = star;       break;
                case class_planet:    cel = new Planet();    cel->type = rocky;      break;
                case class_moon:      cel = new Moon();      cel->type = rocky;      break;
                case class_comet:     cel = new Comet();     cel->type = icy_tailed; break;
                case class_satellite: cel = new Satellite(); cel->type = artificial; break;
                default: break;
            }
            if (!cel) continue;

            if (name.size() >= name_max_len)
            {
                report.note("\"" + name + "\" is longer than we can store; shortened to "
                    + name.substr(0, name_max_len-1) + ".");
                name = name.substr(0, name_max_len-1);
            }
            strcpy(cel->name, name.c_str());
            cel->namelen = 0;

            if (!append_cel(cel))
            {
                delete cel;
                report.note("Ran out of room for objects; the rest of the file was not imported.");
                report.ok = true;
                return true;
            }

            cel->user_added = true;
            // save_all() writes only the objects marked as edited, so without this an imported
            // universe could not be written back out to a .json file at all -- which is the whole
            // point of importing one.
            cel->user_edited = true;
            cel->distance_known = true;
            cel->cenobj = parent->cenobj ? parent->cenobj : parent;
            if (cls == class_moon) ((Moon*)cel)->orbit_type = ot_equatorial;

            double v, semi[3];
            bool have_semi = get_vec3(blk.fields, "SemiAxes", semi);
            bool have_radius = get_num(blk.fields, "Radius", v);

            if (have_semi)
            {
                // Three axes on their own are kilometres. Three axes beside a Radius are a shape
                // and nothing more, and that Radius is the scale they are drawn at.
                if (have_radius) for (int k=0; k<3; k++) semi[k] *= v;
                cel->volumetric_mean_radius = 1000.0 * pow(fabs(semi[0]*semi[1]*semi[2]), 1.0/3.0);
            }
            else if (have_radius) cel->volumetric_mean_radius = v * 1000.0;
            else
            {
                // A block with no size at all -- a bare point put there for other things to orbit
                // is the usual reason -- would otherwise reach classify() with a zero volume and a
                // density of nothing over nothing.
                cel->volumetric_mean_radius = 1000.0;
                report.note("\"" + name + "\" is stated with no size at all; it was given a "
                    "one-kilometre radius so that it could be placed.");
            }

            if (get_num(blk.fields, "Oblateness", v)) cel->oblateness = v;

            // Radius in an .ssc file is the equatorial radius; ours is the radius of the sphere of
            // equal volume, which for a flattened body is the smaller of the two. Three axes give
            // that sphere directly, and are left alone.
            if (!have_semi && cel->oblateness > 0 && cel->oblateness < 1)
                cel->volumetric_mean_radius *= pow(1.0 - cel->oblateness, 1.0/3.0);

            // A small moon is drawn as the ellipsoid it is rather than as a sphere, and these
            // three diameters are where the renderer looks for its shape. Which axis is which is
            // not something the format says, so they are taken in the order every measured moon
            // is quoted in: longest toward the planet it is locked to, shortest through the poles.
            if (cls == class_moon && have_semi)
            {
                std::sort(semi, semi + 3, std::greater<double>());
                Moon *mn = (Moon*)cel;
                mn->depth  = semi[0] * 2000.0;
                mn->width  = semi[1] * 2000.0;
                mn->height = semi[2] * 2000.0;
            }

            const json *orb = get_obj(blk.fields, "EllipticalOrbit");
            if (orb) apply_orbit(*orb, cel, parent);
            else
            {
                report.note("\"" + name + "\" has no elliptical orbit we can read"
                    + (blk.fields.contains("SampledOrbit") || blk.fields.contains("SampledTrajectory")
                        ? " (its trajectory is a sampled file)" : "") + "; it was placed on a guessed one.");
                cel->orbit = new Orbit();
                cel->orbit->center = parent;
                cel->orbit->center_name = parent->name;
                cel->orbit->semimajor_axis = parent->volumetric_mean_radius * 4;
                cel->orbit->epoch = J2000;
            }

            apply_orbit_frame(blk.fields, cel);
            apply_rotation(blk.fields, cel);

            imported[full_path] = cel;
            // A name in these files may list aliases after a colon ("Vulcan:40 Eri A I"), and a child
            // is free to name its parent by any of them, so each is registered as a path of its
            // own rather than only the spelling the parent's own line happened to lead with.
            {
                size_t from = 0;
                std::string names = blk.name;
                while (from <= names.size())
                {
                    size_t colon = names.find(':', from);
                    std::string one = names.substr(from, (colon == std::string::npos) ? std::string::npos : colon - from);
                    if (one.size()) imported[parent_path + "/" + one] = cel;
                    if (colon == std::string::npos) break;
                    from = colon + 1;
                }
            }
            fresh.push_back(cel);
            report.bodies_added++;

            if (cls == class_star)
            {
                apply_color(blk.fields, cel);
                Star *s = (Star*)cel;
                s->temperature = s->estimate_temperature();
                double lum = s->estimate_luminosity(s->temperature);
                std::cout << "Fucking retard " << lum << std::endl;
                s->absolute_magnitude = 4.85 - log(lum) * invlogmagnbase;
                s->mass = s->estimate_mass();
                s->make_universally_visible();
                note_dropped_fields(blk.fields, nullptr, nullptr, name, "Atmosphere");
                continue;
            }

            if (cls == class_satellite)
            {
                std::string mesh;
                if (get_str(blk.fields, "Mesh", mesh))
                    report.note("\"" + name + "\": its mesh " + mesh + " was not imported; only the orbit was.");
                if (!cel->sidereal_rotational_period) cel->sidereal_rotational_period = oneday;
                apply_color(blk.fields, cel);
                note_dropped_fields(blk.fields, nullptr, nullptr, name, "Mesh");
                continue;
            }

            // A comet is not a Planet here -- its brightness follows how hard the Sun is boiling
            // it rather than how big its disc is -- so none of the mass, air and ring work below
            // applies to one. What the file states about it is its orbit, its nucleus and its
            // colour, and all three are already in hand.
            if (cls == class_comet)
            {
                if (!cel->sidereal_rotational_period) cel->sidereal_rotational_period = oneday * 0.5;
                apply_color(blk.fields, cel);
                install_body_textures(blk.fields, nullptr, cel, false);
                note_dropped_fields(blk.fields, nullptr, nullptr, name, nullptr);
                report.note("\"" + name + "\" is a comet; the file states no light curve, so the "
                    "brightness of a middling one was assumed for it.");
                continue;
            }

            Planet *pl = (Planet*)cel;

            establish_mass(pl, blk.fields, blk);
            if (get_num(blk.fields, "J2", v)) pl->J2 = v;

            update_body_location(pl);
            // Classified with the density, always. classify()'s other branch splits rocky worlds
            // from ice giants on mass alone, and puts anything past 4.37 Earths in the ice giant
            // bin -- which makes an ice giant of every large terrestrial an add-on describes.
            // Vulcan, 1.58 Earth radii of solid ground, landed there. We reach this line with a
            // radius and a mass in hand whichever route establish_mass() took, so the density is
            // available and it is the thing that actually tells the two apart.
            pl->classify(pl->is_in_con_HZ(), true);
            pl->estimate_albedo_and_absmagn();

            double stated_albedo = 0;
            if (get_num(blk.fields, "Albedo", stated_albedo) || get_num(blk.fields, "GeomAlbedo", stated_albedo))
            {
                pl->albedo = stated_albedo;
                double rearths = fmax(0.01, pl->volumetric_mean_radius / earth_radius);
                pl->absolute_magnitude = fmax(-10.0, earth_absmag
                    - log(rearths * rearths * stated_albedo / earth_albedo) / log(magnbase));
            }

            apply_color(blk.fields, cel);

            if (!cel->sidereal_rotational_period) pl->estimate_rotation();

            const json *atmos = get_obj(blk.fields, "Atmosphere");
            bool all_weather = (pl->type == gas_giant || pl->type == ice_giant || pl->type == hot_jupiter);
            if (atmos)
            {
                // The cosmic shoreline is a model of what a terrestrial world can hold on to, and
                // asking it about a gas giant gets an answer in the thousands of atmospheres --
                // true of the deep interior, useless as the reference level for a body whose
                // visible surface is its cloud tops. Giants get the conventional 1 bar those cloud
                // tops are defined at instead, which is the level the rest of the app reasons about
                // them at as well.
                if (all_weather) pl->ensure_atmosphere()->surface_pressure = oneatm;
                else pl->apply_cosmic_shoreline();
                if (pl->get_surface_pressure() <= 0) pl->ensure_atmosphere()->surface_pressure = oneatm;
                if (pl->estimate_habitability())
                    pl->ensure_atmosphere()->ensure_composition()->generate_fictitious_habitable();
                else
                    pl->ensure_atmosphere()->ensure_composition()->generate_fictitious_for_planet(pl->type);
                report.note(std::string(pl->name) + ": the file states no air pressure, so "
                    + std::to_string((int)(pl->get_surface_pressure() / oneatm * 1000) / 1000.0)
                    + " atm was estimated for it.");

                // How much of the sky is dust and how much is air. The file states the two
                // scattering coefficients in the same units, so the share between them is exactly
                // what particulates means here: 0 is a sky of pure Rayleigh blue, 1 a sky that
                // repeats the colour of the ground below it. Older add-ons say the same thing in
                // one number, and it is already the fraction we want.
                double mie = 0, haze = 0, rayleigh[3];
                bool have_rayleigh = get_vec3(*atmos, "Rayleigh", rayleigh);
                if (get_num(*atmos, "Mie", mie) && have_rayleigh)
                {
                    double air = (rayleigh[0] + rayleigh[1] + rayleigh[2]) / 3.0;
                    if (mie + air > 0) pl->ensure_atmosphere()->particulates = mie / (mie + air);
                }
                else if (get_num(blk.fields, "HazeDensity", haze))
                    pl->ensure_atmosphere()->particulates = fmax(0.0, fmin(1.0, haze));
            }

            install_body_textures(blk.fields, atmos, cel, all_weather);

            const json *rings = get_obj(blk.fields, "Rings");
            if (rings)
            {
                double inner = 0, outer = 0;
                get_num(*rings, "Inner", inner);
                get_num(*rings, "Outer", outer);
                inner *= 1000.0;
                outer *= 1000.0;
                if (outer > pl->get_equatorial_radius())
                {
                    pl->ring_inner_radius = inner;
                    pl->ring_radius = outer;
                    pl->ring_mean_opacity = 0.75;

                    std::string ringtex;
                    std::string ring_path;
                    if (get_str(*rings, "Texture", ringtex)) ring_path = find_texture(base_dir, ringtex);

                    bool have_ring_texture = ring_path.size() && install_ring_textures(ring_path, pl, inner, outer);
                    if (!have_ring_texture)
                    {
                        if (ringtex.size() && ring_path.empty())
                            report.note("Ring texture " + ringtex + " for \"" + name
                                + "\" is not in the add-on (the source package ships it); a ring pattern was generated instead.");
                        install_procedural_ring(pl);
                    }
                }
                else report.note("\"" + name + "\": its rings sit inside the planet and were dropped.");
            }

            note_dropped_fields(blk.fields, atmos, rings, name, nullptr);
        }
    }

    attach_locations(places, imported);
    if (place_detail_dropped)
        report.note("The places in this file state sizes or topographical types as well; a place "
            "here is a name and a point, so those were dropped.");

    for (size_t bi=0; bi<blocks.size(); bi++)
    {
        if (done[bi]) continue;
        report.note("\"" + first_alias(blocks[bi].name) + "\" orbits \"" + blocks[bi].parent
            + "\", which the file never defines; skipped.");
        report.bodies_skipped++;
    }

    for (CelestialObject *cel : fresh) update_body_location(cel);

    report.ok = true;
    return true;
}

// ---------------------------------------------------------------- report window

void draw_ssc_import_window(ImGuiIO &io)
{
    if (!ssc_report_shown) return;

    ImGui::Begin("SSC Import", &ssc_report_shown, 0);

    const SSCImportReport &rpt = last_ssc_import.report;

    ImGui::TextUnformatted(rpt.source.c_str());
    ImGui::Separator();
    ImGui::Text("%d added, %d skipped, %d texture files written, %d relief maps built.",
        rpt.bodies_added, rpt.bodies_skipped, rpt.textures_written, rpt.bumps_built);

    if (rpt.notes.size())
    {
        ImGui::Separator();
        ImGui::BeginChild("ssc_notes", ImVec2(720, 320), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (const std::string &n : rpt.notes)
        {
            ImGui::PushTextWrapPos(700);
            ImGui::TextUnformatted(n.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Press U to write what was imported into universe.json.");

    ImGui::SetWindowSize(ImVec2(0,0));
    ImVec2 pos = ImGui::GetWindowPos(), siz = ImGui::GetWindowSize();
    ImGui::End();

    if (io.MousePos.x >= pos.x && io.MousePos.y >= pos.y
        && io.MousePos.x < pos.x+siz.x && io.MousePos.y < pos.y+siz.y)
        is_mouse_over_window = true;
}
