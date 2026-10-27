#include "fictionalstars.h"
#include "galaxyimage.h"
#include "galaxygeometry.h"
#include "exocons.h"
#include "../globals.h"
#include <SDL_image.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <unordered_map>
#include <array>

using namespace alienorum;

namespace
{
    double dot(Point a, Point b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
    Point unit(Point a)
    {
        const double length = a.magnitude();
        return length > 1e-20 ? a * (1 / length) : Point(0, 1, 0);
    }
    double uniform(std::mt19937_64& random)
    {
        return (random() >> 11) * 0x1.0p-53;
    }
    Point direction(std::mt19937_64& random)
    {
        const double y = 2 * uniform(random) - 1, angle = 2 * _pi * uniform(random);
        const double radial = sqrt(std::max(0.0, 1 - y*y));
        return Point(radial*cos(angle), y, radial*sin(angle));
    }
    double normal(std::mt19937_64& random)
    {
        return sqrt(-2 * log(std::max(1e-15, uniform(random)))) * cos(2 * _pi * uniform(random));
    }
    Rotation isotropic(std::mt19937_64& random)
    {
        return align_points_3d(direction(random), yaxis, center);
    }
    Rotation tilted(Rotation plane, double spread, std::mt19937_64& random)
    {
        Point pole = rotate3D(yaxis, center, plane.v, -plane.a);
        pole = unit(pole + direction(random) * (spread * uniform(random)));
        return align_points_3d(pole, yaxis, center);
    }
    double luminance(Point color) { return 0.2126*color.x + 0.7152*color.y + 0.0722*color.z; }
    float linear_channel(unsigned char value)
    {
        const double channel = value / 255.0;
        return channel <= 0.04045 ? channel / 12.92 : pow((channel + 0.055) / 1.055, 2.4);
    }
    std::string image_path(const Galaxy& galaxy)
    {
        if (galaxy.reference_galaxy) return galaxy.internal_texture;
        std::vector<std::string> names{galaxy.name};
        std::string underscored = galaxy.name, compact;
        for (char& c : underscored) if (c == ' ') c = '_';
        for (char c : std::string(galaxy.name)) if (c != ' ') compact += c;
        names.push_back(underscored);
        names.push_back(compact);
        if (compact.size() > 1 && compact[0] == 'M' && isdigit(static_cast<unsigned char>(compact[1])))
        {
            names.push_back("M " + compact.substr(1));
            names.push_back("M_" + compact.substr(1));
        }
        for (const auto& name : names)
            for (const auto& extension : {".jpg", ".png"})
            {
                const std::string path = "galaxies/faceon/" + name + extension;
                if (std::filesystem::is_regular_file(path)) return path;
            }
        return {};
    }
    double analytic_density(const FictionalGalaxy& galaxy, Point p)
    {
        const double radius = sqrt(p.x*p.x + p.z*p.z) / galaxy.radius_ly;
        if (galaxy.spheroid)
        {
            const double r = sqrt(p.x*p.x + p.z*p.z + p.y*p.y) / galaxy.radius_ly;
            return exp(-5 * r);
        }
        const double thin = std::max(80.0, galaxy.radius_ly * (galaxy.morphology >= 9 ? 0.10 : 0.006));
        const double vertical = 0.85*exp(-fabs(p.y)/thin) + 0.15*exp(-fabs(p.y)/(thin*3));
        return exp(-3*radius) * vertical;
    }
    void main_sequence(Star& star, double index)
    {
        const char* letters = "OBAFGKM";
        index = std::clamp(index, 3.0, 68.0);
        snprintf(star.spectral_type, sizeof(star.spectral_type), "%c%dV", letters[int(index)/10], int(index)%10);
        star.mass = Star::interpolate_mseq_mass(index) * solar_mass;
        star.volumetric_mean_radius = Star::interpolate_mseq_rad(index) * solar_radius;
        star.temperature = Star::interpolate_mseq_temp(index);
        star.BV_color = Star::interpolate_mseq_BV(index);
        const double luminosity = Star::interpolate_mseq_lum(index);
        star.absolute_magnitude = 4.74 - 2.5*log10(std::max(1e-9, luminosity))
            - Star::bolometric_correction(star.temperature);
    }
    void stellar_properties(Star& star, int phase, double age, std::mt19937_64& random)
    {
        if (phase == 7 || phase == 8)
        {
            const bool cool_evolved = phase == 8;
            star.temperature = cool_evolved ? 2800+uniform(random)*700
                : std::clamp(4850+350*normal(random), 3800.0, 5300.0);
            star.mass = std::clamp(pow(10/std::max(0.03, age), 0.4)*(0.95+0.1*uniform(random)), 0.85, 15.0)*solar_mass;
            star.volumetric_mean_radius = (cool_evolved ? 80+pow(uniform(random), 2)*200
                : std::clamp(9.5*pow(4800/star.temperature, 6)*exp(0.3*normal(random)), 3.0, 100.0))*solar_radius;
            star.BV_color = Star::interpolate_mseq_BV(Star::get_mseqidx_from_temp(star.temperature));
            const double luminosity = pow(star.volumetric_mean_radius / solar_radius, 2)
                * pow(star.temperature / sun_temp, 4);
            star.absolute_magnitude = 4.74 - 2.5*log10(luminosity) - Star::bolometric_correction(star.temperature);
            const double index = Star::get_mseqidx_from_temp(star.temperature);
            const int spectral = std::clamp(int(index), 3, 68);
            snprintf(star.spectral_type, sizeof(star.spectral_type), "%c%d%s", "OBAFGKM"[spectral/10],
                spectral%10, star.mass/solar_mass > 5 ? "II" : "III");
        }
        else if (phase == 9)
        {
            const double mass = std::clamp(pow(10/std::max(0.03, age), 0.4), 0.85, 15.0);
            const double index = Star::get_mseqidx_from_mass(mass);
            main_sequence(star, index);
            star.temperature *= 0.8+0.15*uniform(random);
            star.volumetric_mean_radius *= 1.5+uniform(random)*1.5;
            star.BV_color = Star::interpolate_mseq_BV(Star::get_mseqidx_from_temp(star.temperature));
            const double luminosity = pow(star.volumetric_mean_radius/solar_radius, 2)*pow(star.temperature/sun_temp, 4);
            star.absolute_magnitude = 4.74-2.5*log10(luminosity)-Star::bolometric_correction(star.temperature);
            const int spectral = std::clamp(int(Star::get_mseqidx_from_temp(star.temperature)), 3, 68);
            snprintf(star.spectral_type, sizeof(star.spectral_type), "%c%dIV", "OBAFGKM"[spectral/10], spectral%10);
        }
        else if (phase == 10)
        {
            star.mass = (0.52 + uniform(random)*0.22)*solar_mass;
            star.volumetric_mean_radius = Star::degenerate_radius(star.mass);
            star.temperature = 6000 + uniform(random)*18000;
            star.estimate_BV(star.temperature);
            const double luminosity = pow(star.volumetric_mean_radius/solar_radius, 2)*pow(star.temperature/sun_temp, 4);
            star.absolute_magnitude = 4.74 - 2.5*log10(luminosity) - Star::bolometric_correction(star.temperature);
            strcpy(star.spectral_type, "DA");
        }
        else
        {
            const double draw = uniform(random);
            const double index = phase == 0 ? 60+8*sqrt(draw) : phase == 4 ? 20+9*sqrt(draw)
                : phase == 5 ? 10+9*pow(draw, 0.12) : phase == 6 ? 3+6*pow(draw, 0.1)
                : 60-10*phase+9*draw;
            main_sequence(star, index);
        }
        star.estimate_UB(star.temperature);
        star.sidereal_rotational_period = std::clamp(25.0 * sqrt(std::max(0.02, age)/4.6)
            * pow(sun_temp/star.temperature, 2), 0.25, 150.0) * oneday;
    }
}

bool FictionalImage::load(const std::string& filename, bool panorama)
{
    *this = FictionalImage();
    if (filename.empty()) return false;
    SDL_Surface* source = IMG_Load(filename.c_str());
    if (!source) return false;
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(source);
    if (!rgba) return false;
    path = filename;
    panoramic = panorama;
    // A bounded CPU field, independent of GPU texture limits. Each cell averages original pixels.
    const bool calibrated = !panorama && galaxy_measured_frame(filename).has_value();
    const SDL_Rect bounds = panorama || calibrated ? SDL_Rect{0, 0, rgba->w, rgba->h} : galaxy_image_bounds(*rgba);
    const double scale = std::min({1.0, 1024.0/bounds.w, 512.0/bounds.h});
    width = std::max(1, int(bounds.w*scale));
    height = std::max(1, int(bounds.h*scale));
    rgb.assign(size_t(width)*height*3, 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            Point sum;
            int count = 0;
            const int x0 = bounds.x+x*bounds.w/width, x1 = bounds.x+(x+1)*bounds.w/width;
            const int y0 = bounds.y+y*bounds.h/height, y1 = bounds.y+(y+1)*bounds.h/height;
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx)
                {
                    const auto* pixel = static_cast<unsigned char*>(rgba->pixels) + sy*rgba->pitch + sx*4;
                    const double alpha = pixel[3]/255.0;
                    sum += Point(linear_channel(pixel[0]), linear_channel(pixel[1]), linear_channel(pixel[2])) * alpha;
                    ++count;
                }
            const size_t offset = (size_t(y)*width + x)*3;
            rgb[offset] = sum.x/count;
            rgb[offset+1] = sum.y/count;
            rgb[offset+2] = sum.z/count;
        }
    SDL_FreeSurface(rgba);
    uint64_t value = 14695981039346656037ULL;
    std::ifstream input(filename, std::ios::binary);
    char byte;
    while (input.get(byte)) value = (value ^ static_cast<unsigned char>(byte))*1099511628211ULL;
    std::ostringstream out;
    out << std::hex << value;
    hash = out.str();
    return true;
}

Point FictionalImage::sample(double u, double v) const
{
    if (!available() || !std::isfinite(u) || !std::isfinite(v)) return Point(0.5, 0.5, 0.5);
    if (panoramic) u -= floor(u);
    else if (u < 0 || u > 1 || v < 0 || v > 1) return center;
    const double x = std::clamp(u*width - 0.5, panoramic ? -0.5 : 0.0, double(width-1));
    const double y = std::clamp(v*height - 0.5, 0.0, double(height-1));
    const int ix = floor(x), iy = floor(y);
    const auto pixel = [&](int px, int py)
    {
        px = panoramic ? (px % width + width) % width : std::clamp(px, 0, width-1);
        py = std::clamp(py, 0, height-1);
        const size_t offset = (size_t(py)*width + px)*3;
        return Point(rgb[offset], rgb[offset+1], rgb[offset+2]);
    };
    return pixel(ix, iy)*((1-(x-ix))*(1-(y-iy))) + pixel(ix+1, iy)*((x-ix)*(1-(y-iy)))
        + pixel(ix, iy+1)*((1-(x-ix))*(y-iy)) + pixel(ix+1, iy+1)*((x-ix)*(y-iy));
}

FictionalGalaxy FictionalGalaxy::from_galaxy(const Galaxy& galaxy)
{
    FictionalGalaxy result;
    result.name = galaxy.name;
    result.milky_way = galaxy.reference_galaxy;
    result.key = result.milky_way ? "MW" : galaxy.PGC ? "PGC" + std::to_string(galaxy.PGC)
        : "GAL:" + (galaxy.origname.empty() ? result.name : galaxy.origname);
    for (char c : result.milky_way ? std::string("MW") : result.name)
        if (isalnum(static_cast<unsigned char>(c))) result.code += c;
    if (result.code.empty()) result.code = result.key;
    result.code = result.code.substr(0, 10);
    result.origin_mly = galaxy.location.galactic_center +
        (galaxy.location.system_center + galaxy.location.local_position)*(1/(1e6*light_year));
    if (!result.origin_mly.squared_magnitude() && galaxy.distance > 0)
        result.origin_mly = Point::from_ra_dec(galaxy.right_ascension, galaxy.declination,
            galaxy.distance/(1e6*light_year));
    result.radius_ly = galaxy.volumetric_mean_radius / light_year;
    if (!(result.radius_ly > 10)) result.radius_ly = galaxy.angular_diameter > 0 && galaxy.distance > 0
        ? galaxy.angular_diameter*galaxy.distance*0.5/light_year : 50000;
    result.morphology = galaxy.T_known ? galaxy.morphological_T : 3;
    result.spheroid = galaxy.is_spheroidal_or_elliptical();
    if (result.milky_way)
    {
        result.radius_ly = 15000*parsec/light_year;
        result.east = unit(Point::from_ra_dec(galactic_center_RA_J2000, galactic_center_Decl_J2000, 1));
        result.height_axis = unit(Point::from_ra_dec(galactic_north_RA_J2000, galactic_north_Decl_J2000, 1));
        result.east = unit(result.east - result.height_axis*dot(result.east, result.height_axis));
        result.south = unit(compute_normal(center, result.east, result.height_axis));
        result.origin_mly = result.east * (8200*parsec/light_year/1e6);
    }
    else
    {
        const auto axes = galaxy_disk_axes(galaxy);
        result.east = axes.first;
        result.south = axes.second;
        result.height_axis = unit(compute_normal(center, result.east, result.south));
    }
    result.band_plane = galaxy.location.local_system_plane;
    Point original_ray = result.origin_mly;
    original_ray = rotate3D(original_ray, center, result.band_plane.v, result.band_plane.a);
    result.band_yaw = find_angle_along_vector(zaxis, original_ray, center, yaxis);
    result.image.load(image_path(galaxy), result.milky_way);
    if (result.milky_way && result.image.available())
    {
        result.ray_columns.resize(64*32);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 64; ++x)
            {
                Point ray = result.image_ray((x+0.5)/64, (y+0.5)/32);
                double column = 0;
                for (int step = 0; step < 96; ++step)
                {
                    Point p = result.disk_offset(ray*((step+0.5)*result.radius_ly*2*light_year/96)
                        - result.origin_mly*(1e6*light_year));
                    if (result.inside(p)) column += analytic_density(result, p)*2/96;
                }
                result.ray_columns[y*64+x] = std::max(1e-5, column);
            }
    }
    return result;
}

Point FictionalGalaxy::world_offset(Point p) const
{
    return (east*p.x + height_axis*p.y + south*p.z)*light_year;
}

Point FictionalGalaxy::disk_offset(Point p) const
{
    return Point(dot(p, east), dot(p, height_axis), dot(p, south))*(1/light_year);
}

Point FictionalGalaxy::from_milky_ray(double longitude, double latitude, double distance_ly) const
{
    Point ray = east*(cos(longitude)*cos(latitude)) + south*(sin(longitude)*cos(latitude))
        + height_axis*sin(latitude);
    return disk_offset(ray*(distance_ly*light_year) - origin_mly*(1e6*light_year));
}

Point FictionalGalaxy::image_ray(double u, double v) const
{
    Point ray = Point::from_ra_dec((u-0.5)*2*_pi, (0.5-v)*(_pi/3), 1);
    ray = rotate3D(ray, center, yaxis, band_yaw);
    return rotate3D(ray, center, band_plane.v, -band_plane.a);
}

Point FictionalGalaxy::population_color(Point p) const
{
    if (!image.available())
    {
        const double blue = spheroid ? 0.25 : morphology >= 8 ? 0.8 : 0.5;
        return Point(0.6, 0.5, blue);
    }
    if (!milky_way) return image.sample(0.5 + p.x/(2*radius_ly), 0.5 + p.z/(2*radius_ly));
    Point ray = world_offset(p) + origin_mly*(1e6*light_year);
    ray = rotate3D(ray, center, band_plane.v, band_plane.a);
    ray = rotate3D(ray, center, yaxis, -band_yaw);
    const double longitude = atan2(-ray.x, ray.z), latitude = atan2(ray.y, hypot(ray.x, ray.z));
    if (fabs(latitude) > _pi/6) return Point(0.015, 0.015, 0.015);
    return image.sample(0.5 + longitude/(2*_pi), 0.5 - latitude/(_pi/3));
}

bool FictionalGalaxy::inside(Point p) const
{
    if (spheroid) return p.squared_magnitude() < radius_ly*radius_ly;
    return hypot(p.x, p.z) < radius_ly && fabs(p.y) < radius_ly*(morphology >= 8 ? 0.45 : 0.2);
}

double FictionalGalaxy::density(Point p) const
{
    if (!inside(p)) return 0;
    const double analytic = analytic_density(*this, p);
    if (!image.available()) return analytic;
    const double light = luminance(population_color(p));
    // Processed imagery is a relative prior with a small floor for obscured populations.
    double column = 1;
    if (milky_way && !ray_columns.empty())
    {
        Point ray = world_offset(p) + origin_mly*(1e6*light_year);
        ray = rotate3D(ray, center, band_plane.v, band_plane.a);
        ray = rotate3D(ray, center, yaxis, -band_yaw);
        const double u = 0.5 + atan2(-ray.x, ray.z)/(2*_pi);
        const double v = 0.5 - atan2(ray.y, hypot(ray.x, ray.z))/(_pi/3);
        const int ix = (int(floor(u*64)) % 64 + 64) % 64;
        const int iy = std::clamp(int(v*32), 0, 31);
        column = ray_columns[iy*64+ix];
    }
    return analytic * (0.015 + light) / column;
}

double FictionalGalaxy::advisory_inner() const { return radius_ly * (spheroid ? 0.25 : 0.35); }
double FictionalGalaxy::advisory_outer() const { return radius_ly * (spheroid ? 0.75 : 0.70); }

std::string alienorum::fictional_population_id()
{
    static std::atomic<uint64_t> counter{0};
    std::random_device device;
    const uint64_t value = (uint64_t(device()) << 32) ^ device()
        ^ std::chrono::high_resolution_clock::now().time_since_epoch().count() ^ ++counter;
    std::ostringstream out;
    out << 'B' << std::hex << value;
    return out.str();
}

int alienorum::fictional_next_number()
{
    int highest = 0;
    for (int i = 0; cels && cels[i]; ++i)
    {
        int number = 0;
        if (sscanf(cels[i]->name, "F-%d", &number) == 1) highest = std::max(highest, number);
        if (sscanf(cels[i]->origname.c_str(), "F-%d", &number) == 1) highest = std::max(highest, number);
        if (cels[i]->typeclass() == class_star)
        {
            const auto& origin = static_cast<Star*>(cels[i])->fictional_origin;
            if (origin.is_object() && sscanf(origin.value("designation", "").c_str(), "F-%d", &number) == 1)
                highest = std::max(highest, number);
        }
    }
    return highest+1;
}

std::string alienorum::fictional_region_id()
{
    // Three restricted digits and one consonant avoid words and letter/digit imitations.
    const char* digits = "2357";
    const char* letters = "BCDJLMNPQRVW";
    constexpr int capacity = 4*4*4*12;
    std::set<std::string> used;
    if (cels)
        for (int i = 0; cels[i]; ++i)
        {
            if (cels[i]->typeclass() != class_star) continue;
            const auto& origin = static_cast<Star*>(cels[i])->fictional_origin;
            if (origin.is_object() && origin.contains("batch") && origin.at("batch").is_object())
                used.insert(origin.at("batch").value("region", ""));
        }
    static std::atomic<unsigned int> sequence{0};
    const unsigned int first = sequence.fetch_add(1);
    for (int attempt = 0; attempt < capacity; ++attempt)
    {
        unsigned int value = (first + attempt) % capacity;
        std::string code(4, ' ');
        code[3] = letters[value % 12];
        value /= 12;
        for (int i = 2; i >= 0; --i)
        {
            code[i] = digits[value % 4];
            value /= 4;
        }
        if (!used.count(code)) return code;
    }
    return {};
}

const char* alienorum::fictional_center_type(int index)
{
    static const char* types[] = {"F8V", "F9V", "G0V", "G1V", "G2V", "G3V", "G4V", "G5V",
        "G6V", "G7V", "G8V", "G9V", "K0V", "K1V", "K2V"};
    return types[std::clamp(index, 0, 14)];
}

FictionalBatch alienorum::generate_fictional_stars(const FictionalGalaxy& galaxy, const FictionalRegion& region,
    const std::atomic<bool>* cancel, std::atomic<int>* progress)
{
    FictionalBatch result;
    const auto start = std::chrono::steady_clock::now();
    if (region.star_count < 1 || region.star_count >= MAX_CELOBJS || !(region.radius_ly > 0)
        || !std::isfinite(region.minimum_spacing_ly) || region.minimum_spacing_ly < 1 || region.minimum_spacing_ly > 5
        || !std::isfinite(region.radius_ly) || region.radius_ly > 1000 || !galaxy.inside(region.center_ly)
        || 4 + galaxy.code.size() + 1 + region.region_id.size() + 1 + 6 + 2 >= name_max_len
        || region.region_id.empty() || region.region_id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
    {
        result.error = "Choose a center inside the galaxy, a radius up to 1,000 light years, an available star count, and a compact region designation.";
        return result;
    }
    if (!(Star::interpolate_mseq_mass(40) > 0) || !(Star::interpolate_mseq_temp(40) > 0)
        || !(Star::interpolate_mseq_rad(40) > 0) || !(Star::interpolate_mseq_lum(40) > 0))
    {
        result.error = "The stellar model is unavailable. Restore catalogs/mainseq.dat before generating stars.";
        return result;
    }
    std::mt19937_64 positions(region.seed), properties(region.seed ^ 0x853c49e6748fea9bULL),
        planes(region.seed ^ 0xda3e39cb94b95bdbULL), multiples(region.seed ^ 0x9e3779b97f4a7c15ULL);
    Rotation band_plane = galaxy.band_plane;
    const std::string region_identity = region.identity.empty()
        ? galaxy.key + ":" + region.region_id + ":" + std::to_string(region.seed) : region.identity;
    result.provenance = {{"version", 3}, {"id", region_identity}, {"minimum_spacing_ly", region.minimum_spacing_ly}, {"galaxy", galaxy.key}, {"region", region.region_id},
        {"seed", region.seed}, {"center_ly", region.center_ly.to_json()}, {"radius_ly", region.radius_ly},
        {"requested_stars", region.star_count}, {"spacing_model", "independent_1_5ly_pair_max"},
        {"population_reference", "CNS5_Golovin_2023_Mamajek_local_density_Solar_catalog"}, {"source_image", galaxy.image.path}, {"source_hash", galaxy.image.hash},
        {"image_encoding", "display_sRGB_approximation"}, {"model", "image_prior_disk_spheroid_v3"},
        {"selection", "stratified_solar_cns5_v3"}, {"clusters", region.clusters},
        {"center_type", fictional_center_type(region.center_subtype)}, {"galaxy_radius_ly", galaxy.radius_ly},
        {"morphology", galaxy.morphology}, {"spheroid", galaxy.spheroid},
        {"origin_mly", galaxy.origin_mly.to_json()}, {"east", galaxy.east.to_json()},
        {"height_axis", galaxy.height_axis.to_json()}, {"south", galaxy.south.to_json()},
        {"band_plane", band_plane.to_json()}, {"band_yaw_rad", galaxy.band_yaw},
        {"habitability_advice", galaxy.milky_way && region.historical_annulus ? "historical_7_9_kpc" : "broad_morphology_heuristic"}};
    // Condition a physical luminosity function on represented volume, not equal counts per type.
    struct PopulationSample
    {
        double mass, radius, temperature, magnitude, bv, age, cumulative;
        std::string spectral;
    };
    std::vector<PopulationSample> population;
    const auto append_sample = [&](std::vector<PopulationSample>& samples, int phase, double blue, double age, double cluster_radius)
    {
        Star prototype;
        stellar_properties(prototype, phase, age, properties);
        const double lifetime = 10*pow(prototype.mass/solar_mass, -2.5);
        if (phase < 7 && lifetime < age)
        {
            if (cluster_radius >= 0 || galaxy.spheroid) return;
            age = std::max(0.000001, 0.95*lifetime*uniform(properties));
        }
        if (phase == 10 && age < 1) return;
        const double dwarf_densities[] = {0.0676, 0.0105, 0.0033, 0.0024, 0.00049, 0.000032, 0.000000044};
        const double main_fraction = 4946.0/5230-0.0068;
        const double prior = phase < 7 ? main_fraction*dwarf_densities[phase]/0.084322044
            : phase == 7 ? 20.0/5230*0.965 : phase == 8 ? 20.0/5230*0.035
            : phase == 9 ? 0.0068 : 264.0/5230;
        const double phase_weight = phase == 9 ? 0.55*pow(prototype.mass/solar_mass, -2.35)
            : phase == 7 || phase == 8 ? pow(prototype.mass/solar_mass, -1.35) : 1.0;
        const double color_weight = phase >= 5 && phase <= 6 ? 0.85*(0.25+1.5*blue) : phase == 4 ? 0.7+0.6*blue : 1.0;
        const double visibility = pow(10.0, (6.5-prototype.absolute_magnitude+5)/5)*parsec/light_year;
        const double horizon = std::min(region.radius_ly, std::max(25.0, visibility));
        // Stratified classes retain their census probabilities without rare-phase sampling noise.
        const double calibration = phase == 0 ? 0.9 : phase == 2 ? 1.1 : phase == 3 ? 0.85 : phase == 10 ? 1.8 : 1.0;
        const double weight = prior*phase_weight*color_weight*calibration*(cluster_radius < 0 ? horizon*horizon*horizon
            : cluster_radius <= 25 || visibility >= cluster_radius ? 1.0 : 0.0);
        if (!(weight > 0)) return;
        const double cumulative = (samples.empty() ? 0 : samples.back().cumulative)+weight;
        samples.push_back({prototype.mass, prototype.volumetric_mean_radius, prototype.temperature,
            prototype.absolute_magnitude, prototype.BV_color, age, cumulative, prototype.spectral_type});
    };
    const Point center_color = galaxy.population_color(region.center_ly);
    const double center_blue = std::clamp((center_color.z-center_color.x)/(center_color.x+center_color.z+1e-6)*0.5+0.5, 0.0, 1.0);
    for (int sample = 0; sample < 11264; ++sample)
    {
        if (sample % 256 == 0 && cancel && cancel->load())
        {
            result.error = "Preview cancelled.";
            return result;
        }
        const double age = galaxy.spheroid ? 5+6*uniform(properties)
            : uniform(properties) < center_blue*0.1 ? 0.03+uniform(properties)*0.3 : 0.5+9*uniform(properties);
        append_sample(population, sample/1024, center_blue, age, -1);
    }
    struct Cluster
    {
        Point center;
        double width, age;
        std::vector<PopulationSample> population;
    };
    std::vector<Cluster> clusters;
    if (region.clusters && region.star_count >= 100)
    {
        for (int i = 0; i < std::min(4, 1 + region.star_count/4000); ++i)
        {
            const double radius = std::min(region.radius_ly*0.65, i == 0 ? 300.0 : 650.0);
            Point offset = direction(positions)*(radius*(0.4 + 0.6*uniform(positions)));
            if (galaxy.inside(region.center_ly + offset))
            {
                Cluster cluster{offset, 3+7*uniform(positions), i % 2 ? 0.08 : 0.6, {}};
                for (int sample = 0; sample < 5632; ++sample)
                {
                    if (sample % 256 == 0 && cancel && cancel->load())
                    {
                        result.error = "Preview cancelled.";
                        return result;
                    }
                    append_sample(cluster.population, sample/512, 0.9, cluster.age, offset.magnitude());
                }
                if (!cluster.population.empty()) clusters.push_back(std::move(cluster));
            }
        }
    }
    result.clusters = clusters.size();
    result.stars.reserve(region.star_count);
    struct OccupiedSystem { Point position; double spacing; };
    std::unordered_map<int64_t, std::vector<OccupiedSystem>> occupied;
    const auto cell_key = [](int x, int y, int z)
    {
        return ((int64_t(x)+2048)*4096 + y+2048)*4096 + z+2048;
    };
    const auto cell = [&](Point p)
    {
        return std::array<int, 3>{int(floor(p.x/5)),
            int(floor(p.y/5)), int(floor(p.z/5))};
    };
    const auto separated = [&](Point p, double spacing)
    {
        const auto index = cell(p);
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z)
                {
                    const auto found = occupied.find(cell_key(index[0]+x, index[1]+y, index[2]+z));
                    if (found == occupied.end()) continue;
                    for (const auto& other : found->second)
                    {
                        const double exclusion = std::max(spacing, other.spacing);
                        if ((p-other.position).squared_magnitude() < exclusion*exclusion) return false;
                    }
                }
        return true;
    };
    int rejected_systems = 0;
    while (int(result.stars.size()) < region.star_count)
    {
        if (cancel && cancel->load())
        {
            result.stars.clear();
            result.error = "Preview cancelled.";
            return result;
        }
        const bool anchor = result.stars.empty();
        const double spacing = anchor ? region.minimum_spacing_ly
            : region.minimum_spacing_ly+(5-region.minimum_spacing_ly)*uniform(positions);
        const int system_number = result.systems + 1;
        const int cluster_index = !anchor && !clusters.empty() && uniform(positions) < 0.01
            ? int(uniform(positions)*clusters.size()) : -1;
        Point color = galaxy.population_color(region.center_ly);
        const double blue = std::clamp((color.z - color.x)/(color.x + color.z + 1e-6)*0.5 + 0.5, 0.0, 1.0);
        double age = cluster_index >= 0 ? clusters[cluster_index].age
            : galaxy.spheroid ? 5 + 6*uniform(properties)
            : uniform(properties) < blue*0.65 ? 0.03 + uniform(properties)*0.3
            : 0.5 + pow(uniform(properties), 0.5)*9;
        auto primary = std::make_unique<Star>();
        if (anchor)
        {
            main_sequence(*primary, Star::get_mseqidx_from_sptyp(fictional_center_type(region.center_subtype)));
            age = std::min(2 + 3*uniform(properties), 0.8*10*pow(primary->mass/solar_mass, -2.5));
        }
        else
        {
            const auto& candidates = cluster_index < 0 ? population : clusters[cluster_index].population;
            const double draw = uniform(properties)*candidates.back().cumulative;
            const auto& sample = *std::lower_bound(candidates.begin(), candidates.end(), draw,
                [](const PopulationSample& entry, double value) { return entry.cumulative < value; });
            primary->mass = sample.mass;
            primary->volumetric_mean_radius = sample.radius;
            primary->temperature = sample.temperature;
            primary->absolute_magnitude = sample.magnitude;
            primary->BV_color = sample.bv;
            snprintf(primary->spectral_type, sizeof(primary->spectral_type), "%s", sample.spectral.c_str());
            age = sample.age;
        }
        Point offset;
        bool placed = anchor;
        const double reference_density = std::max(1e-12, galaxy.density(region.center_ly));
        for (int attempt = 0; !placed && attempt < 512; ++attempt)
        {
            if (cancel && cancel->load()) break;
            const double limit = std::min(region.radius_ly, std::max(1.0,
                pow(10.0, (6.5 - primary->absolute_magnitude + 5)/5)*parsec/light_year));
            const double max_radius = std::min(region.radius_ly, std::max(25.0, limit));
            if (cluster_index >= 0)
            {
                const auto& cluster = clusters[cluster_index];
                offset = cluster.center + Point(normal(positions), normal(positions), normal(positions))*cluster.width;
                // Cluster luminosities share an age and are conditioned on their actual distance.
            }
            else
            {
                double radial = max_radius*cbrt(uniform(positions));
                offset = direction(positions)*radial;
            }
            const double radius = offset.magnitude();
            const double apparent = primary->absolute_magnitude + 5*log10(std::max(1e-6, radius)*light_year/(10*parsec));
            if (radius > region.radius_ly || (radius > 25 && apparent > 6.5) || !separated(offset, spacing)) continue;
            const Point local_color = galaxy.population_color(region.center_ly + offset);
            const double local_blue = std::clamp((local_color.z-local_color.x)/(local_color.x+local_color.z+1e-6)*0.5+0.5, 0.0, 1.0);
            const double population_weight = cluster_index >= 0 ? 1 : age < 0.5 ? 0.05+0.95*local_blue : 1-0.6*local_blue;
            const double density = galaxy.density(region.center_ly + offset);
            placed = density > 0 && uniform(positions) < population_weight*std::min(1.0, density/(reference_density*1.5));
        }
        if (!placed)
        {
            if (cancel && cancel->load())
            {
                result.error = "Preview cancelled.";
                result.stars.clear();
                return result;
            }
            // Resample the system rather than violating spacing in a crowded local volume.
            if (++rejected_systems > 256)
            {
                result.error = "The requested population cannot fit with this spacing and radius. Reduce Star count or minimum spacing, or increase the radius.";
                result.stars.clear();
                return result;
            }
            continue;
        }
        ++result.systems;
        rejected_systems = 0;
        const auto index = cell(offset);
        occupied[cell_key(index[0], index[1], index[2])].push_back({offset, spacing});
        const double metallicity = std::clamp(-0.05 - 0.5*hypot(region.center_ly.x, region.center_ly.z)/galaxy.radius_ly
            + 0.15*normal(properties) + (galaxy.spheroid ? -0.2 : 0.15), -1.5, 0.5);
        const int remaining = region.star_count - result.stars.size();
        const double multiple_draw = uniform(multiples);
        const double primary_mass = primary->mass/solar_mass;
        const double multiple_fraction = primary->is_white_dwarf() ? 0.30 : primary_mass < 0.5 ? 0.26
            : primary_mass < 1.5 ? 0.40 : primary_mass < 5 ? 0.60 : 0.75;
        const double triple_fraction = primary_mass < 0.5 ? 0.03 : primary_mass < 1.5 ? 0.08 : 0.18;
        const int member_count = anchor ? 1 : std::min(remaining,
            multiple_draw < triple_fraction ? 3 : multiple_draw < multiple_fraction ? 2 : 1);
        const std::string system_id = region_identity + ":" + std::to_string(system_number);
        Star* root = primary.get();
        Rotation system_plane = isotropic(planes);
        double inner_sma = std::max((0.2 + pow(uniform(multiples), 2)*60)*AU, root->volumetric_mean_radius*20);
        for (int member = 0; member < member_count; ++member)
        {
            std::unique_ptr<Star> star = member == 0 ? std::move(primary) : std::make_unique<Star>();
            if (member)
            {
                const double desired_mass = std::max(0.09, root->mass/solar_mass*(0.15 + uniform(properties)*0.8));
                main_sequence(*star, Star::get_mseqidx_from_mass(desired_mass));

            }
            std::ostringstream short_name;
            short_name << "F-" << std::setw(6) << std::setfill('0') << region.first_number+system_number-1;
            if (member) short_name << ' ' << char('A'+member);
            const std::string name = short_name.str();
            snprintf(star->name, sizeof(star->name), "%s", name.c_str());
            star->namelen = name.size();
            star->origname = name;
            star->has_custom_name = true;
            star->user_added = star->user_edited = true;
            star->distance_known = true;
            star->cenobj = root;
            star->stellar_system = {{"system", system_id}, {"slot", int('A'+member)}, {"label", name}};
            star->fictional_origin = {{"id", system_id + ":" + char('A'+member)}, {"batch", result.provenance},
                {"system_index", system_number}, {"component", int('A'+member)}, {"designation", name}, {"minimum_spacing_ly", spacing}, {"age_gyr", age},
                {"cluster", cluster_index}, {"anchor", anchor}, {"disk_position_ly", (region.center_ly + offset).to_json()}};
            star->metallicity = metallicity;
            star->metallicity_ratio = "[Fe/H]";
            star->metallicity_error_upper = star->metallicity_error_lower = 0.15;
            star->metallicity_source = "fictional population model v1";
            star->estimate_UB(star->temperature);
            star->sidereal_rotational_period = std::clamp(25*sqrt(age/4.6)*pow(sun_temp/star->temperature, 2), 0.25, 150.0)*oneday;
            star->stellar_rotation = {{"estimated", true}, {"source", "fictional population model v1"},
                {"inclination_supplied", false}, {"node_supplied", false}};
            star->location.galactic_center = galaxy.origin_mly;
            star->location.system_center = galaxy.world_offset(region.center_ly + offset);
            Point position = star->location.system_center;
            star->distance = std::max(1.0, position.magnitude());
            star->right_ascension = atan2(-position.x, position.z);
            if (star->right_ascension < 0) star->right_ascension += 2*_pi;
            star->declination = atan2(position.y, hypot(position.x, position.z));
            star->location.local_system_plane = system_plane;
            star->location.orbital_plane = system_plane;
            star->location.equatorial_plane = uniform(planes) < 0.80 ? tilted(system_plane, 0.15, planes) : isotropic(planes);
            star->known_poles = star->rot_axis_known = false;
            star->lock_system_plane = star->lock_equatorial_plane = star->estimated_poles = true;
            if (!member && member_count > 1)
            {
                star->set_component('A', root);
                result.staged_systems.emplace_back(root->multisys);
            }
            if (member)
            {
                star->set_component('A'+member, root);
                if (!star->orbit) star->orbit = new Orbit();
                star->orbit->center = root;
                star->orbit->center_name = root->name;
                star->origcenname = root->origname;
                star->orbit->semimajor_axis = member == 1 ? inner_sma : inner_sma*(30 + uniform(multiples)*70);
                star->orbit->eccentricity = uniform(multiples)*0.5;
                star->orbit->period = oneyear*sqrt(pow(star->orbit->semimajor_axis/AU, 3)/((root->mass+star->mass)/solar_mass));
                star->orbit->epoch = J2000;
                star->orbit->mean_anomaly = uniform(multiples)*2*_pi;
                star->orbit->arg_periapsis = uniform(multiples)*2*_pi;
                star->orbit->inclination = uniform(planes) < 0.7 ? uniform(planes)*0.15 : acos(2*uniform(planes)-1);
                star->orbit->ascending_node = uniform(planes)*2*_pi;
            }
            star->apparent_magnitude = star->absolute_magnitude + 5*log10(std::max(1.0, star->location.distance_to(CelestialLocation()))/(10*parsec));
            star->update_location(J2000_TIME_T);
            const Point spin_pole = rotate3D(yaxis, center, star->location.equatorial_plane.v,
                -star->location.equatorial_plane.a);
            const Point orbit_pole = rotate3D(yaxis, center, star->location.orbital_plane.v,
                -star->location.orbital_plane.a);
            star->obliquity = find_3D_angle(spin_pole, orbit_pole, center);
            const double radial = offset.magnitude();
            if (radial <= 100) ++result.nearby;
            if (!anchor && star->absolute_magnitude + 5*log10(std::max(1e-6, radial)*light_year/(10*parsec)) <= 6.5) ++result.naked_eye;
            result.stars.push_back(std::move(star));
        }
        if (progress) progress->store(result.stars.size());
    }
    std::ifstream name_source("object_name_pool.dat");
    std::vector<std::string> name_pool;
    std::set<std::string> pool_entries;
    std::string candidate;
    while (std::getline(name_source, candidate))
    {
        candidate = trim(candidate);
        if (!candidate.empty() && candidate[0] != '#' && candidate.size() < name_max_len-4
            && pool_entries.insert(candidate).second) name_pool.push_back(candidate);
    }
    if (!name_pool.empty())
    {
        std::mt19937_64 names(region.seed ^ 0x58c83a917be1f273ULL);
        for (size_t i = name_pool.size(); i > 1; --i) std::swap(name_pool[i-1], name_pool[names()%i]);
        std::vector<Star*> ranked;
        for (size_t i = 1; i < result.stars.size(); ++i) ranked.push_back(result.stars[i].get());
        const CelestialLocation anchor = result.stars.front()->location;
        const auto magnitude = [&](Star* star)
        {
            const double radius = star->location.distance_to(anchor);
            return star->absolute_magnitude+5*log10(std::max(1.0, radius)/(10*parsec));
        };
        std::stable_sort(ranked.begin(), ranked.end(), [&](Star* a, Star* b) { return magnitude(a) < magnitude(b); });
        ranked.resize(std::min(size_t(200), ranked.size()));
        ranked.insert(ranked.begin(), result.stars.front().get());
        std::set<std::string> used(region.reserved_names.begin(), region.reserved_names.end());
        size_t cursor = 0;
        for (Star* star : ranked)
        {
            do
            {
                candidate = name_pool[cursor%name_pool.size()];
                if (cursor >= name_pool.size()) candidate += " "+std::to_string(1+cursor/name_pool.size());
                ++cursor;
            } while (!used.insert(candidate).second);
            snprintf(star->name, sizeof(star->name), "%s", candidate.c_str());
            star->namelen = candidate.size();
            star->fictional_origin["proper_name"] = candidate;
        }
        for (auto& star : result.stars)
        {
            Star* root = static_cast<Star*>(star->cenobj);
            if (star->orbit && !star->fictional_origin.contains("proper_name")
                && root->fictional_origin.contains("proper_name"))
            {
                candidate = std::string(root->name)+" "+char(star->fictional_origin.at("component").get<int>());
                if (used.insert(candidate).second)
                {
                    snprintf(star->name, sizeof(star->name), "%s", candidate.c_str());
                    star->namelen = strlen(star->name);
                }
            }
            star->stellar_system["label"] = star->name;
        }
    }
    result.elapsed_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    if (!result.stars.empty()) result.estimated_save_bytes = result.stars.front()->to_json().dump(4).size()*result.stars.size();
    return result;
}

bool FictionalBatch::install(std::string& message)
{
    if (!error.empty() || stars.empty())
    {
        message = error.empty() ? "Generate a preview first." : error;
        return false;
    }
    if (!cels || stars.size() >= size_t(MAX_CELOBJS - ncelobjs))
    {
        message = "The population exceeds available object capacity. Reduce Star count and preview again.";
        return false;
    }
    ExoConsGenerator::reset();
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    std::set<std::string> names, identities;
    for (int i = 0; cels[i]; ++i)
    {
        names.insert(cels[i]->name);
        if (cels[i]->typeclass() == class_star)
        {
            Star* star = static_cast<Star*>(cels[i]);
            if (star->fictional_origin.is_object()) identities.insert(star->fictional_origin.value("id", ""));
        }
    }
    std::set<const CelestialObject*> members;
    for (const auto& star : stars) members.insert(star.get());
    for (const auto& star : stars)
    {
        const auto positive = [](double value) { return std::isfinite(value) && value > 0; };
        if (!star || !positive(star->mass) || !positive(star->temperature) || !positive(star->volumetric_mean_radius)
            || !positive(star->sidereal_rotational_period) || !std::isfinite(star->absolute_magnitude)
            || !std::isfinite(star->location.system_center.squared_magnitude())
            || !std::isfinite(star->location.galactic_center.squared_magnitude())
            || !star->fictional_origin.is_object() || !star->fictional_origin.contains("id")
            || !star->fictional_origin.at("id").is_string()
            || (star->orbit && (!members.count(star->orbit->center) || !positive(star->orbit->semimajor_axis)
                || !positive(star->orbit->period) || !std::isfinite(star->orbit->eccentricity)
                || star->orbit->eccentricity < 0 || star->orbit->eccentricity >= 1)))
        {
            message = "A generated star has invalid properties. Generate a fresh preview.";
            return false;
        }
    }
    for (const auto& star : stars)
        if (!names.insert(star->name).second || !identities.insert(star->fictional_origin.at("id")).second)
        {
            message = "This region identity or designation already exists. Start a new region preview.";
            return false;
        }
    if (stars.size() >= size_t(MAX_CELOBJS - ncelobjs))
    {
        message = "Object capacity changed. Reduce Star count and preview again.";
        return false;
    }
    for (auto& star : stars)
    {
        const std::string designation = star->origname;
        Star* object = star.release();
        append_cel(object);
        object->origname = designation;
    }
    stars.clear();
    for (auto& system : staged_systems) system.release();
    staged_systems.clear();
    lsyscache.clear();
    viewchanged = true;
    message = "Fictional stars added. Save the universe to preserve this region.";
    return true;
}
