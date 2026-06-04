
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <filesystem>
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include <algorithm> 
#include <thread>
#include <chrono>
#include <stdio.h>
#include <chrono>
#include <format>
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_image.h>
#ifdef _WIN32
#include <windows.h>        // SetProcessDPIAware()
#endif
#include "classes/misc.h"
#include "classes/color.h"
#include "classes/serial.h"
#include "classes/cat.h"
#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION
#include "include/stb/stb_image.h"

// Learn more about ImGui here: https://github.com/ocornut/imgui/blob/master/docs/FAQ.md

using namespace std;

char lookfor[256], edit_name[256];
bool edtname_dirty=false;
std::vector<int> drawnblocks[drawn_cache_split][drawn_cache_split];
std::filesystem::path p = "catalogs";
std::string load_univ = "", setjd = "";
bool catalogs_found = false;
int num_galaxies=0, num_stars=0, num_planets=0, num_moons=0, num_asteroids=0, num_comets=0, num_sat=0;
float dispcx, dispcy;
int frames_without_mousemove = 0, num_stars_in_box, editidx=-1, addcenidx=-1;
double txtyscale, txtycompact, edit_sma, edit_incl, edit_eccn, edit_argperi, edit_epoch,
    edit_node, edit_manom, edit_period, edit_eqincl, edit_equinox, edit_precnode, edit_procargperi;
bool is_click;
double frame_dur = 0, best_frame_dur = 1e9, scrollhold = 0;
bool splash = true, magnitude_test = false, redo_proper_motions = true;

// ImGui Example Code


// Simple helper function to load an image into a OpenGL texture with common settings
bool LoadTextureFromMemory(const void* data, size_t data_size, GLuint* out_texture, int* out_width, int* out_height)
{
    // Load from file
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load_from_memory((const unsigned char*)data, (int)data_size, &image_width, &image_height, NULL, 4);
    if (image_data == NULL)
        return false;

    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload pixels into texture
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);

    *out_texture = image_texture;
    *out_width = image_width;
    *out_height = image_height;

    return true;
}

// Open and read a file, then forward to LoadTextureFromMemory()
bool LoadTextureFromFile(const char* file_name, GLuint* out_texture, int* out_width, int* out_height)
{
    FILE* f = fopen(file_name, "rb");
    if (f == NULL)
        return false;
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size == -1L)
        return false;
    fseek(f, 0, SEEK_SET);
    void* file_data = IM_ALLOC(file_size);
    fread(file_data, 1, file_size, f);
    fclose(f);
    bool ret = LoadTextureFromMemory(file_data, file_size, out_texture, out_width, out_height);
    IM_FREE(file_data);
    return ret;
}

// End ImGui Example Code


void refresh_star_visibilities()
{
    int i;
    for (i=0; cels[i]; i++) if (cels[i]->typeclass() == class_star) ((Star*)cels[i])->is_really_truly_in_visible_box(here);
}

void draw_ra_dec_lines()
{
    int i, j;
    Cartesian2D prev, zdes;
    ImU32 gc = rgba_apply_redlight(grid_color);
    ImU32 gcb = rgba_apply_redlight(grid_color_brighter);
    ImU32 ec = rgba_apply_redlight(ecliptic_color);
    double node = (whereami >= 0) ? cels[whereami]->equinox_eff : 0;
    bool prev_valid = false;
    // RA and Dec lines.
    for (i=0; i<24; i++)
    {
        prev_valid = false;
        for (j=-80; j<=80; j+=10)
        {
            Point jadolzhnaperejexatdoma = Point::from_ra_dec(fiftyseventh * i * 15, fiftyseventh * j, 5, node);
            zdes = Cartesian2D(jadolzhnaperejexatdoma, azimuth, altitude, zoom);
            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                prev_valid = false;
                continue;
            }

            if (j > -80)
            {
                int dx1 = dispcx + zdes.x * dispcx,
                    dy1 = dispcy + zdes.y * dispcx,
                    dx2 = dispcx + prev.x * dispcx,
                    dy2 = dispcy + prev.y * dispcx;

                if (prev_valid)
                {
                    ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx1, dy1), ImVec2(dx2, dy2), gc, 1);
                }
            }

            prev = zdes;
            prev_valid = true;
        }
    }

    for (j=-80; j <= 80; j+=10)
    {
        prev_valid = false;
        for (i=0; i<=24; i++)
        {
            Point jadolzhnaperejexatdoma = Point::from_ra_dec(fiftyseventh * i * 15, fiftyseventh * j, 5, node);
            zdes = Cartesian2D(jadolzhnaperejexatdoma, azimuth, altitude, zoom);
            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                prev_valid = false;
                continue;
            }

            if (i)
            {
                int dx1 = dispcx + zdes.x * dispcx,
                    dy1 = dispcy + zdes.y * dispcx,
                    dx2 = dispcx + prev.x * dispcx,
                    dy2 = dispcy + prev.y * dispcx;

                    if (prev_valid)
                    ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx1, dy1), ImVec2(dx2, dy2), j?gc:gcb, 1);
            }

            prev = zdes;
            prev_valid = true;
        }
    }

    // Ecliptic
    if (whereami >= 0 && (cels[whereami]->typeclass() == class_planet))
    {
        prev_valid = false;
        for (i=0; i<=360; i++)
        {
            Point pt = Point::from_ra_dec(fiftyseventh * i, 0, AU);
            pt = rotate3D(pt, center, here.orbital_plane.v, -here.orbital_plane.a);
            pt = rotate3D(pt, center, here.equatorial_plane.v, here.equatorial_plane.a);
            zdes = Cartesian2D(pt, azimuth, altitude, zoom);
            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                prev_valid = false;
                continue;
            }

            if (i & 1)
            {
                int dx1 = dispcx + zdes.x * dispcx,
                    dy1 = dispcy + zdes.y * dispcx,
                    dx2 = dispcx + prev.x * dispcx,
                    dy2 = dispcy + prev.y * dispcx;

                if (prev_valid)
                ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx1, dy1), ImVec2(dx2, dy2), ec, 1);
            }

            prev = zdes;
            prev_valid = true;
        }
    }
}

void load_textures(CelestialObject* cel)
{
    std::string filename;

    filename = (std::string)"maps/" + (std::string)cel->name + (std::string)"_clouds.jpg";
    if (file_exists(filename.c_str()))
    {
        Map *map = new Map();
        if (map->load_from_jpeg(filename)) cel->cloud_map = map;
    }
    else
    {
        filename = (std::string)"maps/" + (std::string)cel->name + (std::string)"_clouds.png";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map();
            if (map->load_from_png(filename)) cel->cloud_map = map;
        }
    }

    filename = (std::string)"maps/" + (std::string)cel->name + (std::string)"_surf.jpg";
    if (file_exists(filename.c_str()))
    {
        Map *map = new Map();
        if (map->load_from_jpeg(filename)) cel->surf_map = map;
    }
    else
    {
        filename = (std::string)"maps/" + (std::string)cel->name + (std::string)"_surf.png";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map();
            if (map->load_from_png(filename)) cel->surf_map = map;
        }
    }

    filename = (std::string)"maps/" + (std::string)cel->name + (std::string)"_bump.jpg";
    if (file_exists(filename.c_str()))
    {
        Map *map = new Map();
        if (map->load_from_jpeg(filename)) cel->bump_map = map;
    }
    else
    {
        filename = (std::string)"maps/" + (std::string)cel->name + (std::string)"_bump.png";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map();
            if (map->load_from_png(filename)) cel->bump_map = map;
        }
    }

    filename = (std::string)"maps/" + (std::string)cel->name + (std::string)"_night.jpg";
    if (file_exists(filename.c_str()))
    {
        Map *map = new Map();
        if (map->load_from_jpeg(filename)) cel->night_map = map;
    }
    else
    {
        filename = (std::string)"maps/" + (std::string)cel->name + (std::string)"_night.png";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map();
            if (map->load_from_png(filename)) cel->night_map = map;
        }
    }

    filename = (std::string)"maps/" + (std::string)cel->name + (std::string)"_ring.jpg";
    if (file_exists(filename.c_str()))
    {
        Map *map = new Map();
        if (map->load_from_jpeg(filename)) cel->ring_map = map;
    }
    else
    {
        filename = (std::string)"maps/" + (std::string)cel->name + (std::string)"_ring.png";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map();
            if (map->load_from_png(filename)) cel->ring_map = map;
        }
    }

    filename = (std::string)"maps/" + (std::string)cel->name + (std::string)"_ringx.jpg";
    if (file_exists(filename.c_str()))
    {
        Map *map = new Map();
        if (map->load_from_jpeg(filename)) cel->ringx_map = map;
    }
    else
    {
        filename = (std::string)"maps/" + (std::string)cel->name + (std::string)"_ringx.png";
        if (file_exists(filename.c_str()))
        {
            Map *map = new Map();
            if (map->load_from_png(filename)) cel->ringx_map = map;
        }
    }

    cel->looked_for_maps = true;

    if ((cel->type == gas_giant || cel->type == ice_giant) && !cel->cloud_map)
    {
        cel->cloud_map = new Map();
        cel->cloud_map->generate_gas_giant_map(503, cel->BV_color);
    }
    else if (cel->type == rocky && !cel->surf_map)
    {
        cel->surf_map = new Map();
        double vmag = cel->cenobj->viewer_magnitude(cel->location);
        cel->surf_map->generate_rocky_map(503, cel->BV_color, ((Planet*)cel)->is_in_con_HZ());
    }
}

double sphresolution = 0.1;
bool bugged = false;
int draw_sphere(CelestialObject* cel, double arad)
{
    if (sphresolution < 0.001/sphere_quality) sphresolution = 0.001/sphere_quality;
    bool wireframe = dragging || !cel->onscreen || cel->tmprel.magnitude() < cel->volumetric_mean_radius;
    cel->onscreen = false;
    int i, j, l, m, lastm, n, result=0;
    Cartesian2D prev, zdes;
    std::vector<ImVec2> todraw;
    std::vector<bool> tdvalid;
    ImU32 gc = rgba_apply_redlight(IM_COL32(176, 170, 164, 255));
    ImU32 gm = rgba_apply_redlight(IM_COL32(  0, 255,   0, 255));

    if (wireframe)
    {
        Color wcol = Color::color_from_magnitude_indices(0, cel->BV_color);
        RGB wrgb = Color::rgb_from_color(wcol, -1);
        gc = rgba_apply_redlight(IM_COL32(wrgb.r, wrgb.g, wrgb.b, 255));
    }

    bool prev_valid = false;
    bool dwh = false;
    if (cel->typeclass() == class_moon) dwh = (((Moon*)cel)->depth && ((Moon*)cel)->width && ((Moon*)cel)->height);
    double equatorial_radius, theta, vtheta, cos_theta, cos_vtheta, is_day, is_night;
    if (dwh)
        equatorial_radius = pow(((Moon*)cel)->depth * ((Moon*)cel)->width, 0.5) * 500;
    else
        equatorial_radius = cel->volumetric_mean_radius * pow(1.0 - cel->oblateness, 0.333);

    double lat, lon, z_cutoff = cel->tmprel.magnitude() + equatorial_radius * 0.2, obl = 1.0 - cel->oblateness;

    double rads_sec = cel->sidereal_rotational_period ? ((M_PI * 2) / cel->sidereal_rotational_period) : 0;
    double seconds_since_epoch = (simnow - J2000_TIME_T) + ((J2000 - cel->epoch)*oneday);
    double timeofday = rads_sec * seconds_since_epoch + M_PI_2;
    if (cel->orbit && fabs(cel->orbit->period - cel->sidereal_rotational_period) < 0.01 * cel->orbit->period)
    {
        timeofday += cel->orbit->ascending_node;
        timeofday += cel->orbit->arg_periapsis;
        timeofday += cel->orbit->mean_anomaly;
        timeofday += M_PI_2;
    }

    if (!wireframe && !cel->looked_for_maps)
    {
        cel->looked_for_maps = true;
        std::thread ttex(load_textures, cel);
        ttex.detach();
    }

    if (wireframe) for (i=0; i<24; i++)
    {
        prev_valid = false;
        for (j=-80; j<=90; j+=10)
        {
            Point cursor = Point::from_ra_dec(fiftyseventh * i * 15, fiftyseventh * j, dwh ? 1 : equatorial_radius, 0);

            if (dwh)
            {
                cursor.x *= ((Moon*)cel)->width * 500;
                cursor.y *= ((Moon*)cel)->height * 500;
                cursor.z *= ((Moon*)cel)->depth * 500;
            }
            else cursor.y *= obl;
            cursor = rotate3D(cursor, center, yaxis, -timeofday);

            cursor = rotate3D(cursor, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);
            cursor += cel->tmprel;
            cursor = rotate3D(cursor, center, here.equatorial_plane.v, here.equatorial_plane.a);
            if (cursor.magnitude() > z_cutoff)
            {
                prev_valid = false;
                continue;
            }
            zdes = Cartesian2D(cursor, azimuth, altitude, zoom);
            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                prev_valid = false;
                continue;
            }

            if (j > -80)
            {
                int dx1 = dispcx + zdes.x * dispcx,
                    dy1 = dispcy + zdes.y * dispcx,
                    dx2 = dispcx + prev.x * dispcx,
                    dy2 = dispcy + prev.y * dispcx;

                if (prev_valid)
                {
                    ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx1, dy1), ImVec2(dx2, dy2), i?gc:gm, 1);
                    if (zdes.x > -1 && zdes.x < 1 && zdes.y > -1 && zdes.y < 1) cel->onscreen = true;
                }
            }

            prev = zdes;
            prev_valid = true;
        }
    }

    Map *map = nullptr, *nmap = nullptr;
    if (cel->cloud_map) map = cel->cloud_map;
    else if (cel->surf_map) map = cel->surf_map;
    if (cel->night_map) nmap = cel->night_map;
    double night_illum = nmap ? 0 : starlight;
    RGB rgb = Color::rgb_from_color(Color::color_from_magnitude_indices(4.2, cel->BV_color), -1), nrgb = {0,0,0};
    Point cursor, land;
    CelestialObject *lightcen = cel->get_light_center();
    bool self_luminous = (lightcen == cel);
    ImU32 imcol;

    auto sphere_began = std::chrono::high_resolution_clock::now();
    double step = wireframe ? (fiftyseventh*15) : fmax(fmin(M_PI*sphresolution/arad*fiftyseventh, fiftyseventh*2), fiftyseventh*0.2),
        stepcoslat, invlaststepcoslat = 1.0 / step;
    int perline, dx1, dy1, dx2, dy2;
    l = 0;
    double d = cel->tmprel.magnitude(), horizon_angle;
    horizon_angle = acos(equatorial_radius / fmax(d, 1e-29));

    for (lat=-M_PI_2; lat <= M_PI_2; lat+=step)
    {
        prev_valid = false;
        n = 0;
        stepcoslat = step / (cos(lat) + 0.1);
        for (lon=0; lon<=M_PI*2; lon+=stepcoslat)
        {
            n++;
            land = Point::from_ra_dec(lon+M_PI, lat, dwh ? 1 : equatorial_radius, 0);

            if (dwh)
            {
                land.x *= ((Moon*)cel)->width * 500;
                land.y *= ((Moon*)cel)->height * 500;
                land.z *= ((Moon*)cel)->depth * 500;
            }
            else land.y *= obl;
            land = rotate3D(land, center, yaxis, -timeofday);

            land = rotate3D(land, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);
            cursor = land + cel->tmprel;
            cursor = rotate3D(cursor, center, here.equatorial_plane.v, here.equatorial_plane.a);
            if (cursor.magnitude() > z_cutoff)
            {
                todraw.push_back(ImVec2(0,0));
                tdvalid.push_back(false);
                l++;
                prev_valid = false;
                continue;
            }

            zdes = Cartesian2D(cursor, azimuth, altitude, zoom);
            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                todraw.push_back(ImVec2(0,0));
                tdvalid.push_back(false);
                l++;
                prev_valid = false;
                continue;
            }

            if (lon)
            {
                land += cel->location.local_position;
                vtheta = fabs(fmod(find_3D_angle(land, here.local_position, cel->location.local_position), M_PI*2));
                if (!wireframe && vtheta > horizon_angle)
                {
                    todraw.push_back(ImVec2(-1e13, -2e13));
                    tdvalid.push_back(false);
                }
                else
                {
                    dx1 = dispcx + zdes.x * dispcx;
                    dy1 = dispcy + zdes.y * dispcx;
                    dx2 = dispcx + prev.x * dispcx;
                    dy2 = dispcy + prev.y * dispcx;

                    double yd = (dy1 - cel->drawny);
                    if (yd > result) result = yd;

                    ImVec2 v = ImVec2(dx1, dy1);
                    if (prev_valid && wireframe)
                    {
                        ImGui::GetBackgroundDrawList()->AddLine(v, ImVec2(dx2, dy2), gc, 1);
                        if (zdes.x > -1 && zdes.x < 1 && zdes.y > -1 && zdes.y < 1) cel->onscreen = true;
                    }
                    todraw.push_back(v);
                    tdvalid.push_back(true);

                    if (!wireframe && (lat>-M_PI_2) && !dragging)
                    {
                        m = l - n - perline + round(lon*invlaststepcoslat) + 2;
                        if (tdvalid[l-1] && tdvalid[m] && tdvalid[m-1])
                        {
                            if (self_luminous)
                            {
                                cos_vtheta = cos(vtheta);
                                is_day = fmin(1, pow(cos_vtheta, 0.333));
                            }
                            else
                            {
                                theta = fmod(find_3D_angle(land, lightcen->location.local_position, cel->location.local_position), M_PI);
                                if (fabs(theta) < M_PI_2)
                                {
                                    cos_theta = cos(theta);
                                    is_day = fmin(1, pow(cos_theta, 0.333) + night_illum);
                                }
                                else is_day = night_illum;
                            }

                            ImVec2 points[4];
                            points[0] = v;
                            points[1] = todraw[l-1];
                            points[2] = todraw[m-1];
                            points[3] = todraw[m];
                            if (map && is_day) rgb = map->color_at(lat, lon);
                            if (nmap)
                            {
                                is_night = 1.0 - is_day;
                                if (is_night) nrgb = nmap->color_at(lat, lon);
                                imcol = rgba_apply_redlight(IM_COL32(
                                    is_day*rgb.r + is_night*nrgb.r,
                                    is_day*rgb.g + is_night*nrgb.g,
                                    is_day*rgb.b + is_night*nrgb.b,
                                    255));
                            }
                            else imcol = rgba_apply_redlight(IM_COL32(is_day*rgb.r, is_day*rgb.g, is_day*rgb.b, 255));
                            ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(points, 4, imcol);
                            if (m > lastm+1 && tdvalid[m-2])
                            {
                                points[2] = todraw[m-2];
                                points[3] = todraw[m-1];
                                ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(points, 4, imcol);
                            }
                            cel->onscreen = true;
                        }

                        lastm = m;
                    } // if not wireframe
                } // if within horizon angle
                l++;
            } // if lon

            prev = zdes;
            prev_valid = true;
        } // for lon

        perline = n;
        invlaststepcoslat = 1.0/stepcoslat;
    } // for lat

    if (!wireframe && !dragging)
    {
        ImVec2 points[perline];
        n = 0;
        for (i=0; i<perline; i++)
        {
            j = l-perline-i-1;
            if (!tdvalid[j]) continue;
            points[n++] = todraw[j];
        }

        // Certain vars are left over from the last iteration; assume values are still good.
        ImU32 imcol = rgba_apply_redlight(IM_COL32(is_day*rgb.r, is_day*rgb.g, is_day*rgb.b, 255));
        ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(points, n, imcol);
    }

    if (cel->typeclass() == class_planet && ((Planet*)cel)->ring_radius)
    {
        std::vector<ImVec2> todrawr;
        std::vector<bool> tdvalidr;
        l = 0;
        Point dust;
        Planet *pl = (Planet*)cel;
        double ringsize = pl->ring_radius - equatorial_radius, ringd;
        if (ringsize <= 0)
        {
            std::cerr << "ERROR: Ring size less than equatorial radius for " << cel->name << std::endl << std::flush;
            throw 0xbadda7a;
        }

        n = round(M_PI*2/step) * 13;
        m = fmax(4, fmin(result, round(M_PI*2/step)/2));
        double step1 = (double)ringsize / m, step2 = M_PI*2/n;

        Map *rmap = cel->ring_map, *rxmap = cel->ringx_map;
        rgb = {225, 208, 192};
        for (ringd = equatorial_radius; ringd <= pl->ring_radius; ringd += step1)
        {
            double xmapd = (double)(ringd - equatorial_radius) * M_PI*2 / ringsize;
            double ring_opacity = rxmap ? (255.0 * (1.0-pow((double)rxmap->color_at(0, xmapd).g/255, gossamer_rings))) : 0.5;
            if (rmap) rgb = rmap->color_at(0, xmapd);
            double lonlim = M_PI*2+0.5*step2;

            for (lon=0; lon<lonlim; lon+=step2)
            {
                dust = Point::from_ra_dec(lon+M_PI, 0, ringd, 0);

                dust = rotate3D(dust, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);
                dust += cel->tmprel;
                Point yardstick = center - dust;
                yardstick.scale(equatorial_radius*2);
                yardstick += dust;
                cursor = rotate3D(dust, center, here.equatorial_plane.v, here.equatorial_plane.a);

                if (cursor.magnitude() > z_cutoff && cel->tmprel.get_distance_to_line(dust, yardstick) < equatorial_radius )
                {
                    todrawr.push_back(ImVec2(-1e29,-1e53));
                    tdvalidr.push_back(false);
                    l++;
                    prev_valid = false;
                    prev = zdes;
                    continue;
                }

                zdes = Cartesian2D(cursor, azimuth, altitude, zoom);
                if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
                {
                    todrawr.push_back(ImVec2(-1e29,-1e9));
                    tdvalidr.push_back(false);
                    l++;
                    prev_valid = false;
                    prev = zdes;
                    continue;
                }

                dx1 = dispcx + zdes.x * dispcx;
                dy1 = dispcy + zdes.y * dispcx;
                dx2 = dispcx + prev.x * dispcx;
                dy2 = dispcy + prev.y * dispcx;

                ImVec2 v = ImVec2(dx1, dy1);
                if (lon)
                {
                    if (prev_valid && wireframe)
                    {
                        ImGui::GetBackgroundDrawList()->AddLine(v, ImVec2(dx2, dy2), gc, 1);
                        if (zdes.x > -1 && zdes.x < 1 && zdes.y > -1 && zdes.y < 1) cel->onscreen = true;
                    }

                    if (prev_valid && !wireframe && !dragging)
                    {
                        m = l - n - 1;
                        if (m>=1 && tdvalidr[l-1] && tdvalidr[m] && tdvalidr[m-1])
                        {
                            is_day = (cel->tmprel.get_distance_to_line(dust, lightcen->tmprel) < equatorial_radius)
                                ? 0 : (0.15 + 0.44 * pl->amt_lit);

                            ImVec2 points[4];
                            points[0] = v;
                            points[1] = todrawr[l-1];
                            points[2] = todrawr[m-1];
                            points[3] = todrawr[m];
                            double polycx = 0.25 * (points[0].x + points[1].x + points[2].x + points[3].x),
                                   polycy = 0.25 * (points[0].y + points[1].y + points[2].y + points[3].y);
                            for (i=0; i<4; i++)
                            {
                                points[i].x += sgn(points[i].x-polycx);
                                points[i].y += sgn(points[i].y-polycy);
                            }

                            ImU32 imcol = rgba_apply_redlight(IM_COL32(rgb.r*is_day, rgb.g*is_day, rgb.b*is_day, ring_opacity));
                            ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(points, 4, imcol);

                            cel->onscreen = true;
                        } // if all vertices valid
                    } // if ready draw filled poly
                } // if lon

                todrawr.push_back(v);
                tdvalidr.push_back(true);
                prev_valid = true;
                prev = zdes;
                l++;
            }
        }
    }

    auto sphere_finished = std::chrono::high_resolution_clock::now();
    auto sphere_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(sphere_finished - sphere_began);

    if (!wireframe && cel->onscreen)
    {
        if (sphere_elapsed.count() >= (1.3e5*sphere_quality))
        {
            if (sphresolution < 0.2/sphere_quality) sphresolution *= 1.3;
            else if (!bugged)
            {
                std::cout << "System too slow! Texture rendering may be terrible." << std::endl;
                bugged = true;
            }
        }
        else if (sphere_elapsed.count() < (8e4*sphere_quality) && cel->type != star) sphresolution *= 0.9;
    }

    return result;
}

bool look_for_catalogs()
{
    try
    {
        while (!std::filesystem::exists(p))
        {
            std::filesystem::path up = "..";
            std::filesystem::current_path(up);
            if (strlen(std::filesystem::current_path().c_str()) < 5) break;
        }
        if (std::filesystem::exists(p)) catalogs_found = true;
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::cerr << "Error: " << e.what() << endl;
        catalogs_found = false;
        return catalogs_found;
    }
    if (!catalogs_found)
    {
        std::cerr << "No star catalogs found. Ensure the catalogs folder exists, contains data, and that the files are readable." << endl;
        return catalogs_found;
    }

    return catalogs_found;
}

bool save_universe()
{
    mtx.lock();
    loading_msg = std::string("Writing Universe file...");
    mtx.unlock();

    fstream fs;
    fs.open("universe.json", std::ios::out);
    if (fs)
    {
        if (!Serialization::save_all(fs, cels, true)) std::cerr << "FAILED to save universe file." << std::endl;
        fs.close();
        return true;
    }
    else std::cerr << "FAILED to write universe file." << std::endl;
    return false;
}

bool load_universe(std::string universe_fname = "universe.json")
{
    int i;
    fstream fs;
    fs.open(universe_fname.c_str(), std::ios::in);
    if (fs)
    {
        mtx.lock();
        loading_msg = "Loading Universe file...";
        mtx.unlock();
        if (Serialization::load_all(fs, cels))
        {
            fs.close();
            for (i=0; cels[i]; i++) if (!strcmp(cels[i]->name, "Earth"))
            {
                whereami = iamhome = i;
                mycenobj = cels[i]->cenobj;
            }
            ncelobjs = i;
            refresh_star_visibilities();

            std::filesystem::file_time_type ftime_json = std::filesystem::last_write_time("universe.json");
            std::filesystem::file_time_type ftime_cat = std::filesystem::last_write_time("catalogs/star_orbits.dat");
            bool resave_json = false;
            if (ftime_cat > ftime_json)
            {
                CatalogReader cr;
                cr.read_star_orbits_dat(cels);
                resave_json = true;
            }
            if (resave_json) save_universe();

            return true;
        }
        else
        {
            fs.close();
            return false;
        }
    }
    else return false;
}

void load_catalogs()
{
    int i, n;

    // if (load_universe("universe.json")) return;

    // TODO: Read data from more star catalogs.
    CatalogReader cr;
    cr.download_catalogs();
    std::vector<std::string> cats = cr.find_catalogs("catalogs");

    cels[0] = nullptr;

    n = cats.size();
    for (i=0; i<n; i++)
    {
        cout << "Found " << cats[i] << endl;
        if (!strcmp(cats[i].c_str(), "catalogs/Gliese")) have_Gliese = true;
        if (!strcmp(cats[i].c_str(), "catalogs/BSC")) have_BSC = true;
        if (!strcmp(cats[i].c_str(), "catalogs/Hipparcos")) have_HIP = true;
        if (!strcmp(cats[i].c_str(), "catalogs/CCDM")) have_CCDM = true;
        if (!strcmp(cats[i].c_str(), "catalogs/SB9")) have_SB9 = true;
        if (!strcmp(cats[i].c_str(), "catalogs/astorb")) have_astorb = true;
    }

    if (have_Gliese)
    {
        mtx.lock();
        loading_msg = std::string("Loading Gliese catalog...");
        mtx.unlock();
        cout << "Reading Gliese catalog..." << endl << flush;
        int nGliese = cr.read_Gliese_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nGliese << " objects." << endl << flush;
        ncelobjs += nGliese;
    }
    if (have_astorb)
    {
        mtx.lock();
        loading_msg = std::string("Loading asteroid catalog...");
        mtx.unlock();
        cout << "Reading astorb catalog..." << endl << flush;
        int nastorb = cr.read_astorb_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nastorb << " objects." << endl << flush;
    }

    mtx.lock();
    loading_msg = std::string("Loading solar system...");
    mtx.unlock();
    cout << "Reading local planets..." << endl << flush;
    int npl = cr.read_local_planets(cels, MAX_CELOBJS);                   // Read solar system planets now, before painting the sky with stars
    num_planets += npl;
    for (i=0; cels[i]; i++) if (!strcmp(cels[i]->name, "Earth")) whereami = iamhome = i;
    cout << "Read " << npl << " objects." << endl << flush;

    if (have_BSC)
    {
        mtx.lock();
        loading_msg = std::string("Loading Bright Star Catalog...");
        mtx.unlock();
        cout << "Reading Bright Star Catalog..." << endl << flush;
        int nBSC = cr.read_BrightStars_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nBSC << " objects." << endl << flush;
        ncelobjs += nBSC;
        Gliese_doubles_fix();
    }
    if (have_HIP)
    {
        mtx.lock();
        loading_msg = std::string("Loading Hipparcos Catalog...");
        mtx.unlock();
        cout << "Reading Hipparcos catalog..." << endl << flush;
        int nHIP = cr.read_Hipparcos_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nHIP << " objects." << endl << flush;
        Gliese_doubles_fix();
    }

    cout << "Reading exoplanets..." << endl << flush;
    int nexo = cr.read_exoplanets_catalog(cels, MAX_CELOBJS);
    num_planets += nexo;
    cout << "Read " << nexo << " objects." << endl << flush;

    #if _USE_CCDM
    if (have_CCDM)
    {
        mtx.lock();
        loading_msg = std::string("Loading Catalogue of the Components of Double and Multiple Stars...");
        mtx.unlock();
        cout << "Reading CCDM catalog..." << endl << flush;
        int nCCDM = cr.read_CCDM_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nCCDM << " objects." << endl << flush;
    }
    #endif

    if (have_SB9)
    {
        mtx.lock();
        loading_msg = std::string("Loading Stellar Binaries Catalog...");
        mtx.unlock();
        cout << "Reading SB9 catalog..." << endl << flush;
        int nSB9 = cr.read_SB9_catalog(cels, MAX_CELOBJS);
        cout << "Read " << nSB9 << " objects." << endl << flush;
    }

    for (i=0; cels[i]; i++)
    {
        if (cels[i]->type == star) num_stars++;
        if (!cels[i]->cenobj) cels[i]->cenobj = cels[i];
    }

    mtx.lock();
    loading_msg = std::string("Orbiting stars...");
    mtx.unlock();
    cr.read_star_orbits_dat(cels);

    mtx.lock();
    loading_msg = std::string("Naming stars...");
    mtx.unlock();
    rename_all_from_Bayer_Flamsteed();
    cr.read_starname_dat(cels);

    if (magnitude_test)
    {
        for (i=0; i<290; i++)
        {
            double magnitude = -1.0 + 0.1 * i;
            Star* s = new Star();
            strcpy(s->name, ((std::string)"Test "+std::to_string(magnitude)).c_str());
            s->right_ascension = fiftyseventh * i;
            s->declination = -2.59 * fiftyseventh;
            s->apparent_magnitude = s->absolute_magnitude = magnitude;
            s->distance = parsec*10;
            s->proper_motion_decl = s->proper_motion_RA = s->radial_velocity = 0;
            s->BV_color = 0.5;
            s->epoch = J2000;
            s->update_location(simnow);
            cels[ncelobjs++] = s;
        }
    }

    if (load_univ.size()) load_universe(load_univ);

    for (i=0; cels[i]; i++)
    {
        // Center of each star system
        if (!cels[i]->cenobj) cels[i]->cenobj = cels[i];

        // Orbit integrity check
        if (cels[i]->orbit && cels[i]->orbit->center == cels[i])
        {
            delete cels[i]->orbit;
            cels[i]->orbit = nullptr;
        }

        // System center integrity
        while (cels[i]->cenobj->orbit && cels[i]->cenobj->orbit->center && cels[i]->cenobj->orbit->center->typeclass() != class_galaxy)
            cels[i]->cenobj = cels[i]->cenobj->orbit->center;
        if (cels[i]->type == star)
        {
            if (cels[i]->orbit && cels[i]->absolute_magnitude < cels[i]->cenobj->absolute_magnitude)
                cels[i]->absolute_magnitude = cels[i]->cenobj->absolute_magnitude + 1;
            ((Star*)cels[i])->update_location(simnow);
        }
        else if (cels[i]->orbit)
        {
            if (cels[i]->typeclass() == class_planet) ((Planet*)cels[i])->update_location(simnow);
            else if (cels[i]->typeclass() == class_moon) ((Moon*)cels[i])->update_location(simnow);
        }
    }

    refresh_star_visibilities();
}

void read_cons_lines()
{
    int l;
    FILE* fp = fopen("consline.dat", "rb");
    if (fp)
    {
        char buffer[256];
        l = -1;
        while (fgets(buffer, 253, fp))
        {
            char* newline = strchr(buffer, '\n');
            if (newline) *newline = 0;
            newline = strchr(buffer, '\r');
            if (newline) *newline = 0;
            if (*buffer == '~')
            {
                char* name2 = strchr(buffer, ',');
                if (!name2) continue;
                *name2 = 0;
                name2++;
                while (*name2 == ' ')
                {
                    *name2 = 0;
                    name2++;
                }
                char* name3 = strchr(name2, ',');
                if (name3)
                {
                    *name3 = 0;
                    name3++;
                    while (*name3 == ' ')
                    {
                        *name3 = 0;
                        name3++;
                    }
                }
                if (strlen(name2))
                {
                    consname.push_back(name2);
                    consabbrev.push_back(&buffer[1]);
                    lnpercons.push_back(0);
                    l++;
                }
                if (name3 && strlen(name3)) consgen.push_back(name3);
                else consgen.push_back("");
            }
            else if (l>=0)
            {
                char* name2 = strchr(buffer, ',');
                if (!name2) continue;
                *name2 = 0;
                name2++;
                while (*name2 == ' ')
                {
                    *name2 = 0;
                    name2++;
                }
                if (strlen(name2))
                {
                    consline_a.push_back(buffer);
                    consline_b.push_back(trim(name2));
                    considx.push_back(l);
                    lnpercons[l]++;
                    nconsln++;
                }
            }
        }
        fclose(fp);
    }
}

void cache_cons_lines()
{
    int i, j;

    // Cache star indices of consline termini
    consaidx = new int[nconsln+16];
    consbidx = new int[nconsln+16];
    for (i=0; i<nconsln; i++)
    {
        int founda = -1, foundb = -1;
        float foundamag = 1e9, foundbmag = 1e9;
        for (j=0; cels[j]; j++)
        {
            if (cels[j]->type != star) continue;
            Star* s = (Star*)cels[j];
            // Equuleus has some stars with apparent magnitudes dimmer than 6.5 that form part of the shape of the constellation.
            // If we remove this line, the constellation lines won't all work.
            if (!strcmp(s->constellation, "Equ") && s->apparent_magnitude > 7.5) continue;
            else if (s->apparent_magnitude > 6.5) continue;
            if (founda < 0
                && 
                (
                    !strcmp(s->Bayer, consline_a[i].c_str())
                    ||
                    !strcmp(s->Flamsteed, consline_a[i].c_str())
                    ||
                    (
                        consline_a[i].c_str()[0] == 'H' && consline_a[i].c_str()[1] == 'D'
                        && s->HD && (unsigned)atoi(&consline_a[i].c_str()[2]) == s->HD
                    )
                ))
            {
                if (s->apparent_magnitude > foundamag) continue;
                founda = j;
                foundamag = s->apparent_magnitude;
            }
            else if (foundb < 0
                &&
                (
                    !strcmp(s->Bayer, consline_b[i].c_str())
                    ||
                    !strcmp(s->Flamsteed, consline_b[i].c_str())
                    ||
                    (
                        consline_b[i].c_str()[0] == 'H' && consline_b[i].c_str()[1] == 'D'
                        && s->HD && (unsigned)atoi(&consline_b[i].c_str()[2]) == s->HD
                    )
                ))
            {
                if (s->apparent_magnitude > foundbmag) continue;
                foundb = j;
                foundbmag = s->apparent_magnitude;
            }
        }

        if (founda < 0) std::cerr << "Warning: Failed to identify " << consline_a[i] << std::endl;
        if (foundb < 0) std::cerr << "Warning: Failed to identify " << consline_b[i] << std::endl;

        consaidx[i] = founda;
        consbidx[i] = foundb;
    }

    if (show_xonsm)
    {
        for (i=0; i<11; i++)
        {
            int founda = -1, foundb = -1;
            __uint32_t ztym = xonsm[i] & 65535, srap = xonsm[i] / 65536;
            for (j=0; cels[j]; j++)
            {
                if (cels[j]->type != star) continue;
                Star* s = (Star*)cels[j];
                if (founda < 0 && ((!j && !ztym) || s->HD == ztym)) founda = j;
                else if (foundb < 0 && ((!j && !srap) || s->HD == srap)) foundb = j;
            }

            consname.push_back("");
            if (founda >= 0 && foundb >= 0)
            {
                consaidx[i+nconsln] = founda;
                consbidx[i+nconsln] = foundb;
                ((Star*)cels[founda])->make_universally_visible();
                ((Star*)cels[foundb])->make_universally_visible();
            }
        }
    }
}

void compute_object_draw_coordinates()
{
    if (!ncelobjs) return;
    int i, j, bx, by;
    double dispw = dispcx*2, disph = dispcy*2;
    if (whereami >= 0) mycenobj = cels[whereami]->cenobj;
    double mycenobj_dist = mycenobj->location.distance_to(here), lmasslim = lbllsys_mass_lim * 1000;
    if (viewchanged || redo_proper_motions)
    {
        num_stars_in_box = 0;
        bool star_in_box;
        for (i=0; i<drawn_cache_split; i++) for (j=0; j<drawn_cache_split; j++) drawnblocks[i][j].clear();
        for (i=0; cels[i] && i<MAX_CELOBJS; i++)
        {
            CelestialLocation tmp = cels[i]->location - here;
            cels[i]->tmprel = Point(tmp);
            switch (cels[i]->typeclass())
            {
                case class_star:
                if ((star_in_box = (i ? ((Star*)cels[i])->is_in_visible_box(Point(here)) : true))) num_stars_in_box++;              // ANC
                ((Star*)cels[i])->tmp_vis_flag = star_in_box;
                if (i!=selected && i!=trackidx && i!=editidx && i!=whereami && cels[i]->cenobj!=mycenobj)
                {
                    if (!star_in_box)
                    {
                        cels[i]->drawnx = cels[i]->drawny = -1e9;
                        continue;
                    }
                    if (!redo_proper_motions && !cels[i]->orbit) continue;
                    if (cels[i]->orbit && cels[i]->orbit->center && cels[i]->orbit->center != cels[whereami]
                        && (cels[i]->orbit->center->drawnx < 0 || cels[i]->orbit->center->drawny < 0
                            || cels[i]->orbit->center->drawnx > dispw || cels[i]->orbit->center->drawny > disph
                            || cels[i]->orbit->semimajor_axis < cels[i]->location.distance_to(here)*1e-4*zoom
                            )
                        )
                    {
                        cels[i]->drawnx = cels[i]->drawny = -1e9;
                        continue;
                    }
                }

                ((Star*)cels[i])->update_location(simnow);
                tmp = cels[i]->location - here;
                cels[i]->tmprel = Point(tmp);
                if (whereami >= 0 && cels[i]->tmprel.magnitude() < cels[whereami]->volumetric_mean_radius)
                {
                    cels[i]->drawnx = cels[i]->drawny = -1e9;
                    continue;
                }

                // If entering a new star system, change allegiance to new center object.
                if (whereami < 0
                    // .magnitude() is more expensive than simple xyz comparisons, and the distance sphere will always fit in the dimension cube.
                    && cels[i]->tmprel.x < mycenobj_dist && cels[i]->tmprel.y < mycenobj_dist && cels[i]->tmprel.z < mycenobj_dist
                    && cels[i]->tmprel.magnitude() < mycenobj_dist)
                {
                    mycenobj = cels[i]->cenobj;
                }
                break;

                case class_planet:
                if (i!=selected && i!=trackidx && i!=editidx && i!=whereami)
                {
                    if (cels[i]->cenobj!=mycenobj)
                    {
                        cels[i]->drawnx = cels[i]->drawny = -1e9;
                        continue;
                    }
                    else if (cels[i]->orbit &&
                        (
                              ((cels[i]->mass >= lmasslim)
                            && (cels[i]->tmprel.magnitude() > AU)
                        ))
                        && (((Planet*)cels[i])->viewer_reflectance_magnitude(here, 1, mycenobj->absolute_magnitude, cels[i]->orbit->semimajor_axis) > 6.5))
                    {
                        cels[i]->drawnx = cels[i]->drawny = -1e9;
                        continue;
                    }
                }
                ((Planet*)cels[i])->update_location(simnow);
                break;

                case class_moon:
                if (i!=selected && i!=trackidx && i!=editidx && i!=whereami)
                {
                    if (cels[i]->cenobj!=mycenobj)
                    {
                        cels[i]->drawnx = cels[i]->drawny = -1e9;
                        continue;
                    }
                    else if (cels[i]->orbit &&
                        (
                              ((cels[i]->mass >= lmasslim)
                            && (cels[i]->tmprel.magnitude() > AU)
                        ))
                        && (((Planet*)cels[i])->viewer_reflectance_magnitude(here, 1, mycenobj->absolute_magnitude, cels[i]->orbit->semimajor_axis) > 6.5))
                    {
                        cels[i]->drawnx = cels[i]->drawny = -1e9;
                        continue;
                    }
                }
                ((Moon*)cels[i])->update_location(simnow);
                break;

                default:
                ;
            }
        }

        if (whereami >= 0) here = cels[whereami]->location;

        Point viewer_pole = rotate3D(yaxis, center, here.equatorial_plane.v, here.equatorial_plane.a);
        Rotation viewer_plane = align_points_3d(viewer_pole, yaxis, center);

        for (i=0; cels[i] && i<MAX_CELOBJS; i++)
        {
            if (isnan(cels[i]->tmprel.x)) continue;

            if (cels[i]->typeclass() == class_star
                && i!=selected && i!=trackidx && i!=whereami && cels[i]->cenobj!=mycenobj
                && !((Star*)cels[i])->tmp_vis_flag)
                continue;

            Point rel = cels[i]->tmprel;

            rel = rotate3D(rel, center, viewer_plane.v, -viewer_plane.a);

            vmag_cache[i] = (cels[i]->typeclass() == class_planet || cels[i]->typeclass() == class_moon)
                ? ((Planet*)cels[i])->viewer_reflectance_magnitude(here)
                : cels[i]->viewer_magnitude(here);

            double brght = global_brightness * pow(magnbase, -vmag_cache[i]);
            bloomrad_cache[i] = fmax(1.414, sqrt(brght)*global_brightness);

            Cartesian2D cart(rel, azimuth, altitude, zoom);
            float dx = (int)(dispcx + cart.x * dispcx), dy = (int)(dispcy + cart.y * dispcx);
            cels[i]->drawnx = dx;
            cels[i]->drawny = dy;

            if (dx < 0 || dx >= dispw) continue;
            if (dy < 0 || dy >= disph) continue;

            bx = dx*drawblxscalex;
            by = dy*drawblxscaley;
            if (bx<0 || bx>=drawn_cache_split || by<0 || by>=drawn_cache_split) continue;
            drawnblocks[bx][by].push_back(i);
            bx_cache[i] = bx;
            by_cache[i] = by;

            angular_radius[i] = fabs(std::atan2(cels[i]->volumetric_mean_radius, rel.magnitude()));
        }
    }

    redo_proper_motions = false;
}

void draw_objects()
{
    if (!ncelobjs) return;
    int i, j, n, pass;
    double jay, step, dispw = dispcx*2, disph = dispcy*2;
    ImVec2 xycoord;
    double appmag, bloomrad, flare, theta;
    double orbseg = 81;
    double lmasslim = lbllsys_mass_lim*1000;
    std::vector<CelestialObject*> to_draw_sphere;
    std::vector<int> to_draw_idx;

    Point viewer_pole = rotate3D(yaxis, center, here.equatorial_plane.v, here.equatorial_plane.a);
    Rotation viewer_plane = align_points_3d(viewer_pole, yaxis, center);

    // Orbits
    if (show_orbits) for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        if (!cels[i]->orbit) continue;
        if (cels[i]->cenobj != mycenobj && (whereami<0 || cels[i]->orbit->center != cels[whereami])) continue;
        if (cels[i]->orbit->center == mycenobj && cels[i]->mass < lmasslim) continue;

        Color col = Color::color_from_magnitude_indices(5, cels[i]->BV_color);
        RGB rgb = Color::rgb_from_color(col, 1);
        ImU32 imcol = (i==selected) ? rgba_apply_redlight(selected_orbit_color) : rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, 64));
        step = cels[i]->orbit->period / orbseg;
        CelestialLocation was = cels[i]->location;
        bool is_moon = (cels[i]->typeclass() == class_moon);

        Cartesian2D lastcart;
        try
        {
            lastcart = Cartesian2D(cels[i]->drawnx, cels[i]->drawny);
        }
        catch(...)
        {
            lastcart.x = lastcart.y = -1e9;
        }
        for (j=-4; j<=orbseg; j++)
        {
            if (is_moon)
                ((Moon*)cels[i])->update_location(simnow + step*j);
            else
                ((Planet*)cels[i])->update_location(simnow + step*j);

            CelestialLocation orbrel = cels[i]->location - here;

            Point rel = rotate3D(Point(orbrel), center, viewer_plane.v, -viewer_plane.a);

            Cartesian2D cart;
            try
            {
                cart = Cartesian2D(rel, azimuth, altitude, zoom);
                cart.x = dispcx + cart.x * dispcx; cart.y = dispcy + cart.y * dispcx;
                if (lastcart.x >= -200 && lastcart.y >= -200 && cart.x >= -200 && cart.y >= -200)
                    ImGui::GetBackgroundDrawList()->AddLine(ImVec2(lastcart.x, lastcart.y), ImVec2(cart.x, cart.y), imcol);
            }
            catch (...)
            {
                cart.x = cart.y = -1e9;
            }

            lastcart = cart;
        }
        cels[i]->location = was;
    }

    // Dits and doscs
    for (pass=0; pass<=1; pass++) for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        if (i == whereami) continue;

        if (!pass && fabs(bloomrad_cache[i]) > 3) continue;
        else if (pass && fabs(bloomrad_cache[i]) <= 3) continue;

        if (angular_radius[i]*zoom < fiftyseventh)
        {
            if (cels[i]->drawnx < 0 || cels[i]->drawnx >= dispw) continue;
            if (cels[i]->drawny < 0 || cels[i]->drawny >= disph) continue;
        }

        if (cels[i]->typeclass() == class_star
            && i!=selected && i!=trackidx && i!=whereami && cels[i]->cenobj!=mycenobj
            && !((Star*)cels[i])->tmp_vis_flag)
            continue;

        xycoord = ImVec2(cels[i]->drawnx, cels[i]->drawny);
        appmag = vmag_cache[i];
        if (appmag > 6.5) continue;

        #define max_bloomrad 10

        bloomrad = fabs(bloomrad_cache[i]);
        flare = (bloomrad>max_bloomrad) ? fmin(225, fmax(0, 1.0+sqrt(bloomrad-0.5*max_bloomrad)*8)) : 0;
        bloomrad = fmin(max_bloomrad, bloomrad);

        #define bloom_exponent 2.5

        if (angular_radius[i]*zoom > fiftyseventh)
        {
            n = to_draw_sphere.size();
            if (!n)
            {
                to_draw_sphere.push_back(cels[i]);
                to_draw_idx.push_back(i);
                discinstead[i] = true;
            }
            else
            {
                discinstead[i] = false;
                double trm = cels[i]->tmprel.magnitude();
                for (j=0; j<n; j++)
                {
                    if (to_draw_sphere[j]->tmprel.magnitude() < trm)
                    {
                        to_draw_sphere.insert(to_draw_sphere.begin()+j, cels[i]);
                        to_draw_idx.insert(to_draw_idx.begin()+j, i);
                        discinstead[i] = true;
                        break;
                    }
                }
                if (!discinstead[i])
                {
                    to_draw_sphere.push_back(cels[i]);
                    to_draw_idx.push_back(i);
                }
                discinstead[i] = true;
            }
        }
        else
        {
            discinstead[i] = false;
            Color col = Color::color_from_magnitude_indices(appmag, cels[i]->BV_color);
            if (flare)
            {
                double divisor = 255.0 / fmax(fmax(col.blue, col.red), col.green);
                RGB rgb;
                rgb.r = (int)(col.red * divisor);
                rgb.g = (int)(col.green* divisor);
                rgb.b = (int)(col.blue * divisor);

                double flare2 = flare*0.666;
                #define jmax 3
                for (j=jmax; j>0; j--)
                {
                    jay = 0.25 + 0.25 * j * flare;
                    double jay15 = jay+max_bloomrad;
                    ImVec2 radii(jay15, jay15*0.333);
                    ImU32 fcol = rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, (jmax+1-j)*2));
                    double thoff = M_PI*0.1*j;
                    for (theta=0; theta<M_PI*2; theta += M_PI*0.2)
                        ImGui::GetBackgroundDrawList()->AddEllipseFilled(xycoord, radii, fcol, theta+thoff);
                }
            }

            double mgrc = bloomrad_cache[i];
            double divisor = (1.0 / (pow(bloom_exponent, mgrc-1)));

            if (mgrc >= 2)
            {
                mgrc = 2.0 * sqrt(mgrc/2);
                divisor = (2.9 / fmax(col.red, col.blue));
            }

            col.red *= divisor; col.green *= divisor; col.blue *= divisor;
            for (jay=bloomrad; jay>=0; jay-=0.7)
            {
                RGB rgb = Color::rgb_from_color(col, 1);
                if (rgb.r >= 16 || rgb.b >= 16)
                {
                    ImGui::GetBackgroundDrawList()->AddCircleFilled(xycoord, jay, Color::black_to_transparent(IM_COL32(rgb.r, rgb.g, rgb.b, 255)), 0);
                    cels[i]->onscreen = true;
                }
                if (rgb.r == 255 && rgb.b == 255) break;

                col.red *= bloom_exponent; col.green *= bloom_exponent; col.blue *= bloom_exponent;
            }
        }
        if (selected == i)
        {
            ImGui::GetBackgroundDrawList()->AddCircle(xycoord, bloomrad+2, rgba_apply_redlight(selected_color), 0, 2);
        }
    }

    // Labels and selection
    if (show_labels || lbl_localsys) for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        if (cels[i]->typeclass() == class_star
            && i!=selected && i!=trackidx && i!=whereami && cels[i]->cenobj!=mycenobj
            && !((Star*)cels[i])->tmp_vis_flag)
            continue;

        if (i == whereami) continue;
        if (discinstead[i]) continue;
        // if (cels[i]->type == star && i!=selected && i!=trackidx && !((Star*)cels[i])->is_in_visible_box(here.system_center)) continue;
        // if (cels[i]->orbit) std::cout << cels[i]->name << " " << cels[i]->location.distance_to(here) << " " << cels[i]->orbit->semimajor_axis << std::endl;
        if (cels[i]->orbit && cels[i]->location.distance_to(here) > 1e3*cels[i]->orbit->semimajor_axis) continue;
        xycoord = ImVec2(cels[i]->drawnx, cels[i]->drawny);
        appmag = vmag_cache[i];
        if (angular_radius[i]*zoom > fiftyseventh)
            bloomrad = bloomrad_cache[i];
        else bloomrad = fmin(max_bloomrad, bloomrad_cache[i]);
        if ( (show_labels && cels[i]->type == star && !cels[i]->orbit &&
               ((!cbolbls_selected_idx && appmag <= appmagn_lblcut)
                || (cbolbls_selected_idx == 1 && cels[i]->absolute_magnitude <= absmagn_lblcut)
                || (cbolbls_selected_idx == 2 && here.distance_to(cels[i]->location) <= distance_lblcut)
                || (cbolbls_selected_idx == 3 && ((Star*)cels[i])->is_sunlike())
                || (cbolbls_selected_idx == 4 && (((Star*)cels[i])->has_planets >= planets_lblcut) )
                || (cbolbls_selected_idx == 5 && (((Star*)cels[i])->has_hz_planets) )
                || (cbolbls_selected_idx == 6 && (cels[i]->orbit || ((Star*)cels[i])->is_orbit_multiple))
                || (cbolbls_selected_idx == 7 && cels[i]->known_poles)
             ))
            || (cels[i]->orbit && (cels[i]->cenobj == mycenobj) && lbl_localsys
                && ((cels[i]->mass >= lmasslim)
                 || (vmag_cache[i] < 2.5)
                 || (cels[i]->tmprel.magnitude() < AU)
                   )
               )
            || i == selected)
        {
            ImVec2 sz = ImGui::CalcTextSize(cels[i]->name);
            ImGui::GetBackgroundDrawList()->AddText(ImVec2(cels[i]->drawnx - sz.x/2, cels[i]->drawny+bloomrad+1),
                rgba_apply_redlight(objlbl_color),
                cels[i]->name);
        }
    }

    n = to_draw_sphere.size();
    for (j=0; j<n; j++)
    {
        i = to_draw_idx[j];
        CelestialObject *cel = to_draw_sphere[j];
        bloomrad_cache[i] = bloomrad = draw_sphere(cel, angular_radius[i]*zoom);
        discinstead[i] = false;

        xycoord = ImVec2(cel->drawnx, cel->drawny);
        if (selected == i)
        {
            ImGui::GetBackgroundDrawList()->AddCircle(xycoord, bloomrad+2, rgba_apply_redlight(selected_color), 0, 2);
        }

        if ( (show_labels && cels[i]->type == star && !cels[i]->orbit &&
               ((!cbolbls_selected_idx && appmag <= appmagn_lblcut)
                || (cbolbls_selected_idx == 1 && cels[i]->absolute_magnitude <= absmagn_lblcut)
                || (cbolbls_selected_idx == 2 && here.distance_to(cels[i]->location) <= distance_lblcut)
                || (cbolbls_selected_idx == 3 && ((Star*)cels[i])->is_sunlike())
                || (cbolbls_selected_idx == 4 && (((Star*)cels[i])->has_planets >= planets_lblcut) )
                || (cbolbls_selected_idx == 5 && (((Star*)cels[i])->has_hz_planets) )
                || (cbolbls_selected_idx == 6 && (cels[i]->orbit || ((Star*)cels[i])->is_orbit_multiple))
                || (cbolbls_selected_idx == 7 && cels[i]->known_poles)
             ))
            || (cels[i]->orbit && (cels[i]->cenobj == mycenobj) && lbl_localsys
                && ((cels[i]->mass >= lmasslim)
                 || (vmag_cache[i] < 2.5)
                 || (cels[i]->tmprel.magnitude() < AU)
                   )
               )
            || i == selected)
        {
            ImVec2 sz = ImGui::CalcTextSize(cel->name);
            ImGui::GetBackgroundDrawList()->AddText(ImVec2(cel->drawnx - sz.x/2, cel->drawny+bloomrad+1),
                rgba_apply_redlight(objlbl_color),
                cel->name);
        }
    }
}

void draw_cons_lines()
{
    int i, l, n;
    double dispw = dispcx*2, disph = dispcy*2;

    // Hide lines if more than 10 l.y. from Sun.
    draw_actual_conslines = here.distance_to(cels[0]->location) < light_year*10;

    conscen.clear();
    n = consname.size();
    for (l=0; l<n; l++)
    {
        conscen.push_back(Cartesian2D(0,0));
        lnpercons[l] = 0;
    }
    n = show_xonsm ? (nconsln+11) : nconsln;
    for (i=0; i<n; i++)
    {
        if (consaidx[i] < 0 || consbidx[i] < 0) continue;

        int dx1, dx2, dy1, dy2;
        if (i >= nconsln) considx[i] = consname.size()-1;
        l = considx[i];

        dx1 = cels[consaidx[i]]->drawnx;
        dy1 = cels[consaidx[i]]->drawny;
        if (dx1 < -1e3) continue;
        if (dy1 < -1e3) continue;

        dx2 = cels[consbidx[i]]->drawnx;
        dy2 = cels[consbidx[i]]->drawny;
        if (dx2 < -1e3) continue;
        if (dy2 < -1e3) continue;

        if (draw_actual_conslines)
            ImGui::GetBackgroundDrawList()->AddLine(
                ImVec2(dx1, dy1), ImVec2(dx2, dy2),
                rgba_apply_redlight((i<nconsln) ? consline_color : IM_COL32(255, 64, 0, 128)), 1);

        assert (l < (int)conscen.size());
        conscen[l] += Cartesian2D((dx1+dx2)/2, (dy1+dy2)/2);
        lnpercons[l]++;
    }

    // Constellation labels
    n=l;
    if (show_labels) for (l=0; l<n; l++)
    {
        if (!lnpercons[l]) continue;
        conscen[l] /= lnpercons[l];
        if (conscen[l].x < 0 || conscen[l].y < 0) continue;
        int dx = conscen[l].x, dy = conscen[l].y;
        ImVec2 sz = ImGui::CalcTextSize(consname[l].c_str());
        dx -= sz.x/2;
        dy -= sz.y/2;
        if (dx >= 0 && dx < dispw && dy >= 0 && dy < disph)
        {
            ImGui::GetBackgroundDrawList()->AddText(ImVec2(dx, dy),
                rgba_apply_redlight((l<nconsln) ? conslbl_color : IM_COL32(255, 64, 0, 128)),
                consname[l].c_str());
        }
    }
}

void draw_mouse_cursor(ImGuiIO& io)
{
    if (frames_without_mousemove > 203) return;

    cursor_size = (int)io.DisplaySize.x/99;
    circle_size = cursor_size / 2.5;

    ImU32 cc[3];
    cc[0] = rgba_apply_redlight(cursor_color1);
    cc[1] = rgba_apply_redlight(cursor_color2);
    cc[2] = rgba_apply_redlight(cursor_color3);

    int i;

    for (i=0; i<3; i++)
    {
        // top
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x - circle_size, io.MousePos.y - circle_size*2 + (i-1)*_cursor_fade),
            ImVec2(io.MousePos.x, io.MousePos.y - cursor_size - circle_size + (i-1)*_cursor_fade),
            cc[i], _cursor_fade+1);
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x + circle_size, io.MousePos.y - circle_size*2 + (i-1)*_cursor_fade),
            ImVec2(io.MousePos.x, io.MousePos.y - cursor_size - circle_size + (i-1)*_cursor_fade),
            cc[i], _cursor_fade+1);

        // left
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x - circle_size*2 + (i-1)*_cursor_fade, io.MousePos.y - circle_size),
            ImVec2(io.MousePos.x - cursor_size - circle_size + (i-1)*_cursor_fade, io.MousePos.y),
            cc[i], _cursor_fade+1);
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x - circle_size*2 + (i-1)*_cursor_fade, io.MousePos.y + circle_size),
            ImVec2(io.MousePos.x - cursor_size - circle_size + (i-1)*_cursor_fade, io.MousePos.y),
            cc[i], _cursor_fade+1);

        // bottom
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x - circle_size, io.MousePos.y + circle_size*2 - (i-1)*_cursor_fade),
            ImVec2(io.MousePos.x, io.MousePos.y + cursor_size + circle_size - (i-1)*_cursor_fade),
            cc[i], _cursor_fade+1);
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x + circle_size, io.MousePos.y + circle_size*2 - (i-1)*_cursor_fade),
            ImVec2(io.MousePos.x, io.MousePos.y + cursor_size + circle_size - (i-1)*_cursor_fade),
            cc[i], _cursor_fade+1);

        // right
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x + circle_size*2 - (i-1)*_cursor_fade, io.MousePos.y + circle_size),
            ImVec2(io.MousePos.x + cursor_size + circle_size - (i-1)*_cursor_fade, io.MousePos.y),
            cc[i], _cursor_fade+1);
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(io.MousePos.x + circle_size*2 - (i-1)*_cursor_fade, io.MousePos.y - circle_size),
            ImVec2(io.MousePos.x + cursor_size + circle_size - (i-1)*_cursor_fade, io.MousePos.y),
            cc[i], _cursor_fade+1);
    }

    /*ImU32 c = rgba_apply_redlight(cursor_color);
    ImGui::GetBackgroundDrawList()->AddLine(
        ImVec2(io.MousePos.x, io.MousePos.y - cursor_size),
        ImVec2(io.MousePos.x, io.MousePos.y - circle_size - 1),
        c, 1);
    ImGui::GetBackgroundDrawList()->AddLine(
        ImVec2(io.MousePos.x, io.MousePos.y + cursor_size + 1),
        ImVec2(io.MousePos.x, io.MousePos.y + circle_size + 2),
        c, 1);
    ImGui::GetBackgroundDrawList()->AddLine(
        ImVec2(io.MousePos.x - cursor_size, io.MousePos.y),
        ImVec2(io.MousePos.x - circle_size - 1, io.MousePos.y),
        c, 1);
    ImGui::GetBackgroundDrawList()->AddLine(
        ImVec2(io.MousePos.x + cursor_size + 1, io.MousePos.y),
        ImVec2(io.MousePos.x + circle_size + 2, io.MousePos.y),
        c, 1);
    ImGui::GetBackgroundDrawList()->AddCircle(
        ImVec2(io.MousePos.x, io.MousePos.y),
        circle_size, c, 8, 1);*/
}

void identify_object_under_cursor(ImGuiIO& io)
{
    int i;
    double myeq = (whereami >= 0) ? cels[whereami]->equinox_eff : 0;

    is_an_obj_under_cursor = -1;
    obj_magn_under_cursor = 1e9;
    if (trackidx >= 0)
    {
        is_an_obj_under_cursor = trackidx;
        azimuth = -cels[trackidx]->RA_as_radians(here, 0);
        altitude = cels[trackidx]->Decl_as_radians(here);
    }
    else for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        if (abs(cels[i]->drawnx - io.MousePos.x) < circle_size
            &&
            abs(cels[i]->drawny - io.MousePos.y) < circle_size
            )
        {
            // Prioritize by brightness.
            double lmag = vmag_cache[i];
            if (lmag < obj_magn_under_cursor)
            {
                is_an_obj_under_cursor = i;
                obj_magn_under_cursor = lmag;

                if (i == selected) break;
                if (is_click && !dragged) selected = i;
            }
        }
    }

    if (is_an_obj_under_cursor >= 0)
    {
        i = is_an_obj_under_cursor;
        double lmag = vmag_cache[i];

        std::stringstream oss;

        objname = cels[i]->name;
        objinfo = "";
        if (cels[i]->type == star)
        {
            Star* s = (Star*)cels[i];
            if (strlen(s->Bayer) && strlen(s->Flamsteed))
            {
                int Fl = atoi(s->Flamsteed);
                objinfo += std::to_string(Fl) + (std::string)s->Bayer + (std::string)"\n";
            }
            else if (strlen(s->Flamsteed)) objinfo += (std::string)s->Flamsteed + (std::string)"\n";
            else if (strlen(s->Bayer)) objinfo += (std::string)s->Bayer + (std::string)"\n";

            if (strlen(s->Gliese)) objinfo += (std::string)s->Gliese + (std::string)"\n";
            if (s->HD) objinfo += (std::string)"HD" + std::to_string(s->HD) + (std::string)"\n";
            if (s->HR) objinfo += (std::string)"HR" + std::to_string(s->HR) + (std::string)"\n";
            if (s->HIP) objinfo += (std::string)"HIP" + std::to_string(s->HIP) + (std::string)"\n";
            if (s->Bonn_survey[0])
            {
                char BD[3] = {s->Bonn_survey[0],s->Bonn_survey[1],0};
                objinfo += std::string(BD) + (s->Bonn_survey_declination > 0 ? std::string(1, s->Bonn_survey_sign) : std::string(""))
                    + std::to_string(s->Bonn_survey_declination) + std::string(" ") + std::to_string(s->Bonn_survey_sequential) + std::string("\n");
            }
        }

        objinfo += (std::string)"RA:      " + cels[i]->RA_as_hms(here, myeq) + (std::string)"\n"
                + (std::string)"Decl:    " + cels[i]->Decl_as_degms(here) + (std::string)"\n";
        oss << "Mag:     " << std::setprecision(2) << lmag << std::endl;
        objinfo += oss.str();
        oss.str("");
        oss.clear();

        if (cels[i]->type == star)
        {
            Star* s = (Star*)cels[i];
            if (s->distance_known)
            {
                oss << "Dist:    " << cels[i]->scaled_distance(here) << std::endl;
                oss << "AbsMag:  " << std::setprecision(2) << s->absolute_magnitude << "\n";
            }
            objinfo += (std::string)"SpTyp:   " + s->spectral_type + (std::string)"\n";
        }
        else if (cels[i]->type == galaxy)
        {
            // TODO:
        }
        else
        {
            oss << "Dist:    " << cels[i]->scaled_distance(here) << std::endl;
            oss << "Lit %:   " << std::setprecision(1) << ((int)(((Planet*)cels[i])->amt_lit*100)) << std::endl;
            if (((Planet*)cels[i])->is_in_con_HZ())
                oss << "*****    In conserv. HZ" << std::endl;
        }

        if (cels[i]->mass)
        {
            if (cels[i]->type == star) 
                ; // oss << "Mass:  " << std::setprecision(2) << (cels[i]->mass / Msun) << " M(sun)\n" << std::endl;       // TODO: Fix Star::estimate_mass()
            else if (cels[i]->type == rocky || cels[i]->type == gas_giant || cels[i]->type == ice_giant)
            {
                oss << "Mass:    " << std::setprecision(2) << (cels[i]->mass / cels[iamhome]->mass) << " M(earth)" << std::endl;
                oss << "Mass:    " << std::scientific << std::setprecision(2) << (cels[i]->mass / 1000) << " kg" << std::endl;
            }
        }
        if (cels[i]->volumetric_mean_radius)
        {
            if (cels[i]->type == star)
                ; // oss << "Radius: " << std::setprecision(2) << (cels[i]->volumetric_mean_radius / Rsun) << " R(sun)" << std::endl;       // TODO: Fix Star::estimate_radius()
            else if (cels[i]->type == rocky || cels[i]->type == gas_giant || cels[i]->type == ice_giant)
            {
                oss << "Radius:  " << std::setprecision(2) << (cels[i]->volumetric_mean_radius / cels[iamhome]->volumetric_mean_radius)
                    << " R(earth)" << std::endl;
                oss << "Radius:  " << std::scientific << std::setprecision(2) << (cels[i]->volumetric_mean_radius / 1000) << " km" << std::endl;
            }
        }
        objinfo += oss.str();
        oss.clear();
    }
    else
    {
        objname = "Press N to toggle\nthis window.";
        objinfo = "\n\n";
        if (is_click && !dragged) selected = -1;
    }
}

void pan_with_crosshairs(ImGuiIO& io)
{
    if (ImGui::IsMouseDown(2))
    {
        azimuth -= 0.01 * fiftyseventh * io.MouseDelta.x / zoom;
        altitude += 0.01 * fiftyseventh * io.MouseDelta.y / zoom;
        if (altitude >  M_PI/2) altitude =  M_PI/2;
        if (altitude < -M_PI/2) altitude = -M_PI/2;
        spin = 0;
        viewchanged = true;

        ImVec2 topcen(dispcx, 0), botcen(dispcx, (int)io.DisplaySize.y-1),
            leftcen(0, dispcy), rightcen((int)io.DisplaySize.x-1, dispcy);
        ImGui::GetBackgroundDrawList()->AddLine(topcen, botcen, rgba_apply_redlight(IM_COL32(0, 0, 255, 128)), 1);
        ImGui::GetBackgroundDrawList()->AddLine(leftcen, rightcen, rgba_apply_redlight(IM_COL32(0, 0, 255, 128)), 1);
    }
    else if (ImGui::IsMouseDown(1))
    {
        azimuth -= 0.03 * fiftyseventh * io.MouseDelta.x / zoom;
        altitude += 0.03 * fiftyseventh * io.MouseDelta.y / zoom;
        if (altitude >  M_PI/2) altitude =  M_PI/2;
        if (altitude < -M_PI/2) altitude = -M_PI/2;
        spin = 0;
        viewchanged = true;

        ImVec2 topcen(dispcx, 0), botcen(dispcx, (int)io.DisplaySize.y-1),
            leftcen(0, dispcy), rightcen((int)io.DisplaySize.x-1, dispcy);
        ImGui::GetBackgroundDrawList()->AddLine(topcen, botcen, rgba_apply_redlight(IM_COL32(0, 255, 0, 64)), 1);
        ImGui::GetBackgroundDrawList()->AddLine(leftcen, rightcen, rgba_apply_redlight(IM_COL32(0, 255, 0, 64)), 1);
    }
    else if (ImGui::IsMouseDown(0))
    {
        azimuth -= 0.1 * fiftyseventh * io.MouseDelta.x / zoom;
        altitude += 0.1 * fiftyseventh * io.MouseDelta.y / zoom;
        if (altitude >  M_PI/2) altitude =  M_PI/2;
        if (altitude < -M_PI/2) altitude = -M_PI/2;
        spin = 0;
        viewchanged = true;

        ImVec2 topcen(dispcx, 0), botcen(dispcx, (int)io.DisplaySize.y-1),
            leftcen(0, dispcy), rightcen((int)io.DisplaySize.x-1, dispcy);
        ImGui::GetBackgroundDrawList()->AddLine(topcen, botcen, rgba_apply_redlight(IM_COL32(255, 96, 0, 96)), 1);
        ImGui::GetBackgroundDrawList()->AddLine(leftcen, rightcen, rgba_apply_redlight(IM_COL32(255, 96, 0, 96)), 1);
    }
}

void center_selected()
{
    if (selected >= 0)
    {
        azimuth = -cels[selected]->RA_as_radians(here, 0);
        altitude = cels[selected]->Decl_as_radians(here);
    }
}

void process_key_cmd_char(char c)
{
    switch (c)
    {
        case 'A':
        if (selected >= 0) addcenidx = selected;
        else if (trackidx >= 0) addcenidx = trackidx;
        else if (whereami >= 0) addcenidx = whereami;
        addcelwnd = true;
        break;

        case 'b': global_brightness *= 1.1; viewchanged = true; break;
        case 'B': global_brightness *= 0.9; viewchanged = true; break;
        case 'c': show_consln = !show_consln; break;
        case 'd': JDnow += 1; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'D': JDnow -= 1; viewchanged = true; compute_object_draw_coordinates(); break;

        case 'e':
        // TODO: Solar system explorer, or local system if outside solar system.
        break;

        case 'E':
        if (selected >= 0) editidx = selected;
        else if (trackidx >= 0) editidx = trackidx;
        else if (whereami >= 0) editidx = whereami;
        objedtwnd = (editidx >= 0);
        break;

        case 'g': show_grid = !show_grid; break;
        case 'h': JDnow += 1.0/24; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'H': JDnow -= 1.0/24; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'i': JDnow += 1.0/1440; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'I': JDnow -= 1.0/1440; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'l': show_labels = !show_labels; break;
        case 'm': JDnow += 30; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'M': JDnow -= 30; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'n': objinfwnd = !objinfwnd; break;

        case 'o':
        if (selected >= 0)
        {
            here = cels[selected]->location;
            whereami = selected;
            selected = trackidx = -1;
            global_brightness = default_brightness;
            zoom = 1;
        }
        velocity = center;
        viewchanged = true;
        refresh_star_visibilities();
        break;

        case 'O': show_orbits = !show_orbits; break;
        case 'p': lbl_localsys = !lbl_localsys; break;
        case 'q': sphere_quality *= 1.3; viewchanged=true; break;
        case 'Q': sphere_quality *= 0.7; viewchanged=true; break;

        case 'r':
        velocity = center;
        zoom = 1;
        spin = 0;
        whereami = iamhome;
        trackidx = -1;
        here = cels[whereami]->location;
        global_brightness = default_brightness;
        // Fall through to same functionality
        case '@':
        viewchanged = true;
        simnow = std::time(nullptr);
        JDnow = ((double)simnow - J2000_TIME_T)/oneday + J2000;
        refresh_star_visibilities();
        compute_object_draw_coordinates();
        break;

        case 'R': redlight_mode = !redlight_mode; break;
        case 's': statuswnd = !statuswnd; break;
        case 'S': selected = -1; break;

        case 't':
        center_selected();
        trackidx = selected;
        selected = -1;
        viewchanged = true;
        break;

        case 'T': trackidx = -1; break;
        case 'u': save_universe(); break;

        case 'w':
        if (velocity.magnitude())
        {
            velocity.scale(speed_of_light * 1.00001 / target_frame_rate);
        }
        else
        {
            velocity.x =  sin(azimuth) * cos(altitude) * speed_of_light * 1.00001 / target_frame_rate;
            velocity.z =  cos(azimuth) * cos(altitude) * speed_of_light * 1.00001 / target_frame_rate;
            velocity.y =  sin(altitude) * speed_of_light * 1.00001 / target_frame_rate;
            velocity = rotate3D(velocity, center, here.equatorial_plane.v, -here.equatorial_plane.a);
        }
        spin = 0;
        viewchanged = true;
        whereami = -1;
        break;

        case 'x':
        velocity = center;
        viewchanged = true;
        break;

        case 'y': JDnow += (oneyear/oneday); redo_proper_motions = viewchanged = true; compute_object_draw_coordinates(); break;
        case 'Y': JDnow -= (oneyear/oneday); redo_proper_motions = viewchanged = true; compute_object_draw_coordinates(); break;
        case 'z': JDnow += (oneyear/864); redo_proper_motions = viewchanged = true; compute_object_draw_coordinates(); break;
        case 'Z': JDnow -= (oneyear/864); redo_proper_motions = viewchanged = true; compute_object_draw_coordinates(); break;

        case '+':
        vm = velocity.magnitude();
        vmfr = vm * target_frame_rate;
        if (vmfr > 1e100)
        {
            // In Trek, the fastest possible warp is warp 10. But that doesn't make for impressive spaceflights.
            // How about we set the limit to warp 1 googol?
            // Besides, go much faster than that and the app crashes.
            velocity.scale(speed_of_light * 1e100 / target_frame_rate);
        }
        else if (vm)
        {
            if (vmfr < 0.1 * speed_of_light) velocity.scale(vm + 0.5 * vm * compute_time_dilation(vmfr));
            else if (vmfr < speed_of_light) velocity.scale(vm + 0.5 * (speed_of_light - vmfr) / target_frame_rate * compute_time_dilation(vmfr));
            else velocity.scale(vm * 1.5);
        }
        else
        {
            velocity.x =  sin(azimuth) * cos(altitude) * 1000;
            velocity.z =  cos(azimuth) * cos(altitude) * 1000;
            velocity.y =  sin(altitude) * 1000;
            velocity = rotate3D(velocity, center, here.equatorial_plane.v, -here.equatorial_plane.a);
            whereami = -1;
        }
        viewchanged = true;
        break;

        case '!': show_consln = show_grid = show_labels = lbl_localsys = show_orbits = false; break;
        case '%': zoom = 1; global_brightness = 1; viewchanged = true; break;
        case '*': zoom *= 1.1; global_brightness *= 1.05; viewchanged = true; scrollhold = 1; break;
        case '/': zoom *= 0.9; if (zoom < 1) zoom = 1; else global_brightness *= 0.95; viewchanged = true; scrollhold = 1; break;

        case '-':
        vm = velocity.magnitude();
        velocity.scale(vm * 0.666);
        viewchanged = true;
        break;

        case '`': global_gamma += 0.2; set_gamma(global_gamma); break;
        case '~': global_gamma -= 0.2; set_gamma(global_gamma); break;

        default:
        ;
    }
}

void process_keyboard_commands(ImGuiIO& io)
{
    int i;
    for (i = 0; i < io.InputQueueCharacters.Size; i++)
    {
        timeout_ms = 5;
        ImWchar c = io.InputQueueCharacters[i];
        process_key_cmd_char(c);
    }
}

void lookfor_cb()
{
    int i = find_object(lookfor);
    if (i>=0)
    {
        selected = i;
        trackidx = -1;
        center_selected();
        searched = true;
    }
}

void draw_status_window(ImGuiIO& io)
{
    // TODO: If redlight_mode, set all window and text colors accordingly.
    int stattop = 0, statleft = 0, statwidth = 254, statheight = txtyscale*2.3;
    ImGui::Begin("Status", &statuswnd, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    /////////////////////////////////////////////////////

    if (ImGui::InputText("##find", lookfor, 255, ImGuiInputTextFlags_EnterReturnsTrue)) lookfor_cb();
    ImGui::SameLine();
    if (ImGui::Button("Find")) lookfor_cb();
    statheight += txtyscale;

    std::string flagstr;

    flagstr = (std::string)"Zoom (*/): " + std::to_string(zoom);
    ImGui::Text("%s", flagstr.c_str());
    statheight += txtyscale;

    flagstr = (std::string)"Brghtns (B): " + std::to_string(global_brightness);
    ImGui::Text("%s", flagstr.c_str());
    statheight += txtyscale;

    flagstr = (std::string)"Gamma (`): " + std::to_string(get_gamma());
    ImGui::Text("%s", flagstr.c_str());
    statheight += txtyscale;

    flagstr = (std::string)"RA/Decl (G): "
        + std::string(show_grid ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());
    statheight += txtyscale;

    flagstr = (std::string)"Cons ln (C): "
        + std::string(show_consln ? (draw_actual_conslines ? "ON" : "(hidden)") : "OFF");
    ImGui::Text("%s", flagstr.c_str());
    statheight += txtyscale;

    flagstr = (std::string)"Labels (L): "
        + std::string(show_labels ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());
    statheight += txtyscale;

    flagstr = (std::string)"Lbl planets (P): "
        + std::string(lbl_localsys ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());
    statheight += txtyscale;

    if (lbl_localsys)
    {
        ImGui::Text("Mass limit:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(81);
        ImGui::InputDouble("##lbllsysmasslim", &lbllsys_mass_lim, 0, 0, "%.2e");
        ImGui::SameLine();
        ImGui::Text("kg");
    }

    flagstr = (std::string)"Orbits (Sh+O): "
        + std::string(show_orbits ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());
    statheight += txtyscale;

    // Pass in the preview value visible before opening the combo (it could technically be different contents or not pulled from items[])
    ImGuiComboFlags cbolbls_flags = 0;
    const char* combo_preview_value = lbltypes[cbolbls_selected_idx];
    ImGui::Text("%s", "Labels:");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##cbolabels", combo_preview_value, cbolbls_flags))
    {
        for (int n = 0; n < nlbltyp; n++)
        {
            const bool is_selected = (cbolbls_selected_idx == n);
            if (ImGui::Selectable(lbltypes[n], is_selected))
            {
                cbolbls_selected_idx = n;
                show_labels = true;
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    statheight += txtyscale;

    if (cbolbls_selected_idx == 0)
    {
        snprintf(lblcut0, sizeof(lblcut0), "%.2f", appmagn_lblcut);
        ImGui::Text("%s", "Mag limit:");
        ImGui::SameLine();
        ImGui::InputText("##appmaglim", lblcut0, 255);
        statheight += txtyscale*1.3;
        appmagn_lblcut = atof(lblcut0);
    }
    else if (cbolbls_selected_idx == 1)
    {
        snprintf(lblcut1, sizeof(lblcut1), "%.2f", absmagn_lblcut);
        ImGui::Text("%s", "Mag limit:");
        ImGui::SameLine();
        ImGui::InputText("##absmaglim", lblcut1, 255);
        statheight += txtyscale;
        absmagn_lblcut = atof(lblcut1);
    }
    else if (cbolbls_selected_idx == 2)
    {
        snprintf(lblcut2, sizeof(lblcut2), "%.2f", distance_lblcut/light_year);
        ImGui::Text("%s", "Dist. l.y.:");
        ImGui::SameLine();
        ImGui::InputText("##distlim", lblcut2, 255);
        statheight += txtyscale;
        distance_lblcut = atof(lblcut2)*light_year;
    }
    else if (cbolbls_selected_idx == 4)
    {
        ImGui::Text("%s", "# Planets:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(81);
        ImGui::InputInt("##npltlim", &planets_lblcut, 1, 0);
        statheight += txtyscale;
        if (planets_lblcut < 1) planets_lblcut = 1;
    }

    flagstr = (std::string)"Redlgt (Sh+R): "
        + std::string(redlight_mode ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());
    statheight += txtyscale;

    flagstr = (std::string)"Obj info (N): "
        + std::string(objinfwnd ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());
    statheight += txtyscale;

    flagstr = (std::string)"Status (S): "
        + std::string(statuswnd ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());
    statheight += txtyscale;

    ImGui::Text("%s", "-----");
    statheight += txtyscale;

    std::string vfstr;
    if (whereami >= 0)
    {
        vfstr = std::string("View from ") + cels[whereami]->name;
        if (((Planet*)cels[whereami])->is_in_con_HZ())
            vfstr += "\nIn conserv. HZ";
    }
    else vfstr = std::string("View from space");
    ImGui::Text("%s", vfstr.c_str());
    statheight += txtyscale;

    double vm = velocity.magnitude() * target_frame_rate;
    if (isnan(vm)) vm = 0;
    velocmag = vm;
    std::string velocstr;
    if (velocmag < 0.01 * speed_of_light)
    {
        stringstream oss;
        oss << std::scientific << std::setprecision(4) << (velocmag / 1000 * 3600);
        velocstr = std::string("Velocity: ") + oss.str() + std::string(" km/h");
        oss.str("");
    }
    else if (velocmag < speed_of_light)
    {
        std::ostringstream oss;
        oss << std::setprecision(13) << (velocmag / speed_of_light);
        velocstr = std::string("Velocity: ") + oss.str() + std::string(" c");
        oss.str("");
    }
    else
    {
        std::ostringstream oss;
        oss << std::scientific << std::setprecision(2) << (velocmag / speed_of_light);
        velocstr = std::string("Velocity: Warp ") + oss.str();
        oss.str("");
    }
    ImGui::Text("%s", velocstr.c_str());
    statheight += txtyscale;

    std::string numobjs;
    if (num_stars)
    {
        numobjs = std::to_string(num_stars) + " stars";
        ImGui::Text("%s", numobjs.c_str());
        statheight += txtyscale;
    }
    if (num_stars_in_box>1)             // There will always be at least one.
    {
        numobjs = std::to_string(num_stars_in_box) + " stars in range";
        ImGui::Text("%s", numobjs.c_str());
        statheight += txtyscale;
    }
    /* if (num_planets)
    {
        numobjs = std::to_string(num_planets) + " planets";
        ImGui::Text("%s", numobjs.c_str());
        statheight += txtyscale;
    } */

    time_t tmpnow = simnow;
    struct tm *utc_time = std::gmtime(&tmpnow);
    int mon = utc_time->tm_mon + 1, mday = utc_time->tm_mday;
    std::string datedisp = std::to_string(utc_time->tm_year + 1900)
        + std::string("-") + std::string((mon<10)?"0":"") + std::to_string(mon)
        + std::string("-") + std::string((mday<10)?"0":"") + std::to_string(mday);
    ImGui::Text("%s", datedisp.c_str());
    statheight += txtyscale;

    int hr = utc_time->tm_hour, mn = utc_time->tm_min, sec = utc_time->tm_sec;
    std::string timedisp = std::string((hr<10)?"0":"") + std::to_string(hr)
        + std::string(":") + std::string((mn<10)?"0":"") + std::to_string(mn)
        + std::string(":") + std::string((sec<10)?"0":"") + std::to_string(sec)
        + std::string(" UTC");
    ImGui::Text("%s", timedisp.c_str());
    statheight += txtyscale;

    std::string JDdisp = std::string("JD") + std::to_string(JDnow);
    ImGui::Text("%s", JDdisp.c_str());
    statheight += txtyscale;

    std::string frame_rate = std::to_string(1.0 / frame_dur) + std::string(" frames/s");
    ImGui::Text("%s", frame_rate.c_str());
    statheight += txtyscale;

    /////////////////////////////////////////////////////

    ImGui::SetWindowPos(ImVec2(statleft, stattop));
    ImGui::SetWindowSize(ImVec2(statwidth, statheight));
    ImGui::End();

    if (io.MousePos.x >= statleft && io.MousePos.y >= stattop
        && io.MousePos.x < statleft+statwidth && io.MousePos.y < stattop+statheight)
        is_mouse_over_window = true;
}

void draw_objinf_window(ImGuiIO& io)
{
    // TODO: If redlight_mode, set all window and text colors accordingly.
    ImGui::Begin("Object", &objinfwnd, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
    int objinfwidth = 254, objinfheight = txtyscale*2, objinftop = 0, objinfleft = (int)io.DisplaySize.x - objinfwidth;

    if (trackidx >= 0)
    {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "TRACKING");
        objinfheight += txtyscale;
    }

    ImGui::Text("%s", objname.c_str());
    objinfheight += txtyscale;

    int txtlines = std::count(objinfo.begin(), objinfo.end(), '\n');
    ImGui::Text("%s", objinfo.c_str());
    objinfheight += txtlines*txtycompact;

    ImGui::SetWindowPos(ImVec2(objinfleft, objinftop));
    ImGui::SetWindowSize(ImVec2(objinfwidth, objinfheight));
    ImGui::End();

    if (io.MousePos.x >= objinfleft && io.MousePos.y >= objinftop
        && io.MousePos.x < objinfleft+objinfwidth && io.MousePos.y < objinftop+objinfheight)
        is_mouse_over_window = true;
}

void draw_addcel_window(ImGuiIO& io)
{
    ImGui::Begin("Add Object", &addcelwnd);
    ImGui::SetWindowSize(ImVec2(225, 123));

    ImGui::Text("%s", "Type");
    ImGui::SameLine();
    ImGuiComboFlags cboceltyp_flags = 0;
    const char* combo_preview_value = celtypes[cboceltyp_selected_idx];
    if (ImGui::BeginCombo("##cboceltyp", combo_preview_value, cboceltyp_flags))
    {
        for (int n = 0; n < nceltyp; n++)
        {
            const bool is_selected = (cboceltyp_selected_idx == n);
            if (ImGui::Selectable(celtypes[n], is_selected))
            {
                cboceltyp_selected_idx = n;
                show_labels = true;
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Go"))
    {
        for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);
        if (ncelobjs < (MAX_CELOBJS-1))
        {
            bool is_cen_planet = (cels[addcenidx]->type == rocky || cels[addcenidx]->type == ice_giant || cels[addcenidx]->type == gas_giant);

            if (cboceltyp_selected_idx == 2 && is_cen_planet) cboceltyp_selected_idx = 3;
            else if (cboceltyp_selected_idx == 3 && !is_cen_planet) cboceltyp_selected_idx = 2;

            switch (cboceltyp_selected_idx)
            {
                case 0: cels[ncelobjs] = new Galaxy(); cels[ncelobjs]->type = galaxy; break;
                case 1: cels[ncelobjs] = new Star(); cels[ncelobjs]->type = star; break;
                case 2: cels[ncelobjs] = new Planet(); cels[ncelobjs]->type = rocky; break;
                case 3: cels[ncelobjs] = new Moon(); cels[ncelobjs]->type = rocky; break;

                default:
                std::cerr << "Unimplemented object type" << std::endl;
                break;
            }

            if (cels[ncelobjs])
            {
                strcpy(cels[ncelobjs]->name, "new");
                cels[ncelobjs]->user_added = true;
                cels[addcenidx]->distance_known = true;
                cels[ncelobjs]->distance_known = true;
                if (cels[ncelobjs]->type >= cels[addcenidx]->type)
                {
                    cels[ncelobjs]->orbit = new Orbit();
                    cels[ncelobjs]->orbit->center = cels[addcenidx];
                    cels[ncelobjs]->orbit->semimajor_axis = 1e6;
                    cels[ncelobjs]->orbit->period = oneday;
                    cels[ncelobjs]->orbit->epoch = JDnow;
                    cels[ncelobjs]->cenobj = cels[addcenidx]->cenobj;
                }
                editidx = ncelobjs;
                objedtwnd = true;
                addcelwnd = false;
                ncelobjs++;
            }
        }
    }

    ImVec2 pos = ImGui::GetWindowPos(), siz = ImGui::GetWindowSize();
    ImGui::End();

    if (io.MousePos.x >= pos.x && io.MousePos.y >= pos.y
        && io.MousePos.x < pos.x+siz.x && io.MousePos.y < pos.y+siz.y)
        is_mouse_over_window = true;
}

void draw_objedit_window(ImGuiIO& io)
{
    if (editidx < 0)
    {
        objedtwnd = false;
        return;
    }

    CelestialObject *cel = cels[editidx];
    Orbit *orb = cel->orbit;

    // TODO: If redlight_mode, set all window and text colors accordingly.
    ImGui::Begin("Edit Object", &objedtwnd, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    int objedtwidth = 768, objedtheight = 81;

    double col1 = 123, col2 = 359, col3 = 503, txtwid = 167;
    cel_obj_class tc = cels[editidx]->typeclass();

    strcpy(edit_name, cel->name);
    ImGui::Text("%s", "Name");
    ImGui::SameLine(col1);
    ImGui::SetNextItemWidth(txtwid*2);
    if (ImGui::InputText("##edtname", edit_name, 255, 0)) cel->user_edited = true;
    ImGui::SameLine(col3);
    if (ImGui::Button("Select"))
    {
        selected = editidx;
    }
    ImGui::SameLine();
    if (ImGui::Button("Focus"))
    {
        selected = editidx;
        searched = true;
        viewchanged = true;
        center_selected();
    }
    objedtheight += txtyscale;

    if (tc == class_star)
    {
        Star* s = (Star*)cels[editidx];

        char edit_ra[20];
        strcpy(edit_ra, s->RA_as_hms(0).c_str());
        ImGui::Text("%s", "R.Ascension");
        ImGui::SameLine(col1);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputText("##edtra", edit_ra, 20))
        {
            s->RA_from_hms(edit_ra);
            s->update_location(simnow);
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
        }
        ImGui::SameLine(col2);
        char edit_decl[20];
        strcpy(edit_decl, s->Decl_as_degms().c_str());
        ImGui::Text("%s", "Declination");
        ImGui::SameLine(col3);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputText("##edtdecl", edit_decl, 20))
        {
            s->Decl_from_degms(edit_decl);
            s->update_location(simnow);
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
        }
        objedtheight += txtyscale;

        double edit_dist = s->distance / light_year;
        ImGui::Text("%s", "Distance");
        ImGui::SameLine(col1);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtdist", &edit_dist, 0, 0, "%.3f"))
        {
            s->distance = edit_dist * light_year;
            s->update_location(simnow);
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
        }
        ImGui::SameLine();
        ImGui::Text("%s", "l.y.");
        ImGui::SameLine(col2);
        double edit_rv = s->radial_velocity;
        ImGui::Text("%s", "Rad. Vel.");
        ImGui::SameLine(col3);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtrv", &edit_rv, 0, 0, "%.9f"))
        {
            s->radial_velocity = edit_rv;
            s->update_location(simnow);
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
        }
        ImGui::SameLine();
        ImGui::Text("%s", "m/s");
        objedtheight += txtyscale;
    }

    edit_eqincl = cel->obliquity * fiftyseven;
    ImGui::Text("%s", "Obliquity");
    ImGui::SameLine(col1);
    ImGui::SetNextItemWidth(txtwid);
    if (ImGui::InputDouble("##edteqinc", &edit_eqincl, 0, 0, "%.9f"))
    {
        cels[editidx]->obliquity = edit_eqincl * fiftyseventh;
        cels[editidx]->known_poles = true;
        cel->user_edited = true;
        viewchanged = true;
        if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
    }
    ImGui::SameLine(col2);
    edit_equinox = cel->equinox * fiftyseven;
    ImGui::Text("%s", "Equinox");
    ImGui::SameLine(col3);
    ImGui::SetNextItemWidth(txtwid);
    if (ImGui::InputDouble("##edteqnox", &edit_equinox, 0, 0, "%.9f"))
    {
        cels[editidx]->equinox = edit_equinox * fiftyseventh;
        cels[editidx]->known_poles = true;
        cel->user_edited = true;
        viewchanged = true;
        if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
    }
    objedtheight += txtyscale;

    double edit_mass = cel->mass / 1000;
    ImGui::Text("%s", "Mass, kg");
    ImGui::SameLine(col1);
    ImGui::SetNextItemWidth(txtwid);
    if (ImGui::InputDouble("##edtmass", &edit_mass, 0, 0, "%.9e"))
    {
        cel->mass = edit_mass * 1000;
        cel->user_edited = true;
        viewchanged = true;
    }
    ImGui::SameLine(col2);
    double edit_radius = cel->volumetric_mean_radius / 1000;
    ImGui::Text("%s", "Radius, km");
    ImGui::SameLine(col3);
    ImGui::SetNextItemWidth(txtwid);
    if (ImGui::InputDouble("##edtvmrad", &edit_radius, 0, 0, "%.6f"))
    {
        cel->volumetric_mean_radius = edit_radius * 1000;
        cel->user_edited = true;
        viewchanged = true;
        if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
    }
    objedtheight += txtyscale;

    stringstream massss;
    massss << "Density " << std::setprecision(3) << (cel->mass / sphere_volume(cel->volumetric_mean_radius) * 1e-3) << " g/cm^3";
    std::string dens = massss.str();
    ImGui::Text("%s", dens.c_str());
    objedtheight += txtyscale;

    double edit_oblt = cel->oblateness;
    ImGui::Text("%s", "Oblateness");
    ImGui::SameLine(col1);
    ImGui::SetNextItemWidth(txtwid);
    if (ImGui::InputDouble("##edtoblt", &edit_oblt, 0, 0, "%.9e"))
    {
        cel->oblateness = edit_oblt;
        cel->user_edited = true;
        viewchanged = true;
        if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
    }
    ImGui::SameLine(col2);
    double edit_rot = cel->sidereal_rotational_period / oneday;
    ImGui::Text("%s", "Rotation, d");
    ImGui::SameLine(col3);
    ImGui::SetNextItemWidth(txtwid);
    if (ImGui::InputDouble("##edtrot", &edit_rot, 0, 0, "%.6f"))
    {
        cel->sidereal_rotational_period = edit_rot * oneday;
        cel->user_edited = true;
        viewchanged = true;
        if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
    }
    objedtheight += txtyscale;

    double edit_absmag = cel->absolute_magnitude;
    ImGui::Text("%s", "Abs. Magn.");
    ImGui::SameLine(col1);
    ImGui::SetNextItemWidth(txtwid);
    if (ImGui::InputDouble("##edtabsmag", &edit_absmag, 0, 0, "%.3f"))
    {
        cel->absolute_magnitude = edit_absmag;
        cel->user_edited = true;
        viewchanged = true;
        if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
    }
    if (cel->typeclass() == class_planet || cel->typeclass() == class_moon)
    {
        Planet* p = (Planet*)cel;
        p->estimate_albedo();
        ImGui::SameLine(col2);
        ImGui::Text("%s", "Albedo");
        double edit_albedo = p->albedo;
        ImGui::SameLine(col3);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtalbdo", &edit_albedo, 0, 0, "%.3f"))
        {
            p->albedo = edit_albedo;

            double disc_area = pow(p->volumetric_mean_radius / earth_radius, 2);
            double absmag = earth_absmag - log(disc_area * p->albedo / earth_albedo) / log(magnbase);
            if (!isnan(absmag)) p->absolute_magnitude = absmag;

            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
        }
    }
    objedtheight += txtyscale;

    double edit_prcseq = cel->precession / oneyear;
    ImGui::Text("%s", "Precession");
    ImGui::SameLine(col1);
    ImGui::SetNextItemWidth(txtwid);
    if (ImGui::InputDouble("##edtprcs", &edit_prcseq, 0, 0, "%.9e"))
    {
        cel->precession = edit_prcseq * oneyear;
        cel->user_edited = true;
        viewchanged = true;
        if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
    }
    ImGui::SameLine(col2);
    double edit_bvcol = cel->BV_color;
    ImGui::Text("%s", "B-V color");
    ImGui::SameLine(col3);
    ImGui::SetNextItemWidth(txtwid);
    if (ImGui::InputDouble("##edtbv", &edit_bvcol, 0, 0, "%.6f"))
    {
        cel->BV_color = edit_bvcol;
        cel->user_edited = true;
        viewchanged = true;
        if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
        else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
    }
    objedtheight += txtyscale;

    if (orb)
    {
        ImGui::Separator();
        objedtheight += txtyscale;

        std::string orbcen = "Center of Orbit: ";
        orbcen += std::string(cel->orbit->center->name);
        ImGui::Text("%s", orbcen.c_str());
        if (orb->center && orb->center->type == star)
        {
            ImGui::SameLine(col2);
            stringstream oss;
            double star_appmag = orb->center->viewer_magnitude(cel->location);
            oss << "Star apparent mag. " << (star_appmag > 0 ? "+" : "") << std::setprecision(2) << star_appmag;
            ImGui::Text("%s", oss.str().c_str());
        }
        objedtheight += txtyscale;

        edit_sma = cel->orbit->semimajor_axis / AU;
        ImGui::Text("%s", "Semimaj.Axis");
        ImGui::SameLine(col1);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtsma", &edit_sma, 0, 0, "%.9f"))
        {
            orb->semimajor_axis = edit_sma * AU;
            if (cel->user_added) orb->compute_period(cel->mass);
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
        }
        ImGui::SameLine();
        ImGui::Text("%s", "AU");
        edit_period = cel->orbit->period / oneday;
        ImGui::SameLine(col2);
        ImGui::Text("%s", "Period");
        ImGui::SameLine(col3);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtper", &edit_period, 0, 0, "%.9f"))
        {
            if (edit_period)
            {
                cels[editidx]->orbit->period = edit_period * oneday;
                if (cel->user_added) orb->compute_semimajor_axis(cel->mass);
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
            }
        }
        ImGui::SameLine();
        ImGui::Text("%s", "days");
        objedtheight += txtyscale;

        edit_incl = cel->orbit->inclination * fiftyseven;
        ImGui::Text("%s", "Inclination");
        ImGui::SameLine(col1);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtincl", &edit_incl, 0, 0, "%.9f"))
        {
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
        }
        ImGui::SameLine(col2);
        edit_node = cel->orbit->ascending_node * fiftyseven;
        ImGui::Text("%s", "Asc. Node");
        ImGui::SameLine(col3);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtnode", &edit_node, 0, 0, "%.9f"))
        {
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
        }
        objedtheight += txtyscale;

        edit_eccn = cel->orbit->eccentricity;
        ImGui::Text("%s", "Eccentricity");
        ImGui::SameLine(col1);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtecc", &edit_eccn, 0, 0, "%.9f"))
        {
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
        }
        ImGui::SameLine(col2);
        edit_argperi = cel->orbit->arg_periapsis * fiftyseven;
        ImGui::Text("%s", "Arg.Periapsis");
        ImGui::SameLine(col3);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtargperi", &edit_argperi, 0, 0, "%.9f"))
        {
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
        }
        objedtheight += txtyscale;

        edit_epoch = cel->orbit->epoch;
        ImGui::Text("%s", "Epoch, JD");
        ImGui::SameLine(col1);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtepoch", &edit_epoch, 0, 0, "%.9f"))
        {
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
        }
        ImGui::SameLine(col2);
        edit_manom = cel->orbit->mean_anomaly * fiftyseven;
        ImGui::Text("%s", "Mean Anomaly");
        ImGui::SameLine(col3);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtmanom", &edit_manom, 0, 0, "%.9f"))
        {
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
        }
        objedtheight += txtyscale;

        edit_precnode = cel->orbit->prec_node ? (M_PI * 2 / cel->orbit->prec_node / oneday) : 0;
        ImGui::Text("%s", "Prec. Node");
        ImGui::SameLine(col1);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtprcnd", &edit_precnode, 0, 0, "%.9f"))
        {
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
        }
        ImGui::SameLine(col2);
        edit_procargperi = cel->orbit->proc_argperi ? (M_PI * 2 / cel->orbit->proc_argperi / oneday) : 0;
        ImGui::Text("%s", "ProcArgPeri");
        ImGui::SameLine(col3);
        ImGui::SetNextItemWidth(txtwid);
        if (ImGui::InputDouble("##edtprcap", &edit_procargperi, 0, 0, "%.9f"))
        {
            cel->user_edited = true;
            viewchanged = true;
            if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
            else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
        }
        objedtheight += txtyscale;
    }

    int objedttop = io.DisplaySize.y - objedtheight, objedtleft = io.DisplaySize.x - objedtwidth;
    ImGui::SetWindowSize(ImVec2(objedtwidth, objedtheight));
    ImGui::SetWindowPos(ImVec2(objedtleft, objedttop));
    ImGui::End();

    if (io.MousePos.x >= objedtleft && io.MousePos.y >= objedttop)
        is_mouse_over_window = true;
}

void load_stuff()
{
    mtx.lock();
    loading_msg = "Reading constellations...";
    mtx.unlock();
    read_cons_lines();
    mtx.lock();
    loading_msg = "Loading star data...";
    mtx.unlock();
    load_catalogs();
    mtx.lock();
    loading_msg = "Assigning constellations...";
    mtx.unlock();
    cache_cons_lines();
    bv_correction = log(blackbody_flux(sun_temp, V_band) / blackbody_flux(sun_temp, B_band)) * invlogmagnbase - cels[0]->BV_color;
    std::cout << "B-V correction: " << bv_correction << std::endl;

    mtx.lock();
    loading_msg = "Done!";
    splash = false;
    mtx.unlock();
}

int main (int argc, char** argv)
{
    int i, j, l, n;
    cels = new CelestialObject*[MAX_CELOBJS];
    vmag_cache = new double[MAX_CELOBJS];
    bloomrad_cache = new double[MAX_CELOBJS];
    angular_radius = new double[MAX_CELOBJS];
    discinstead = new bool[MAX_CELOBJS];
    memset(cels, 0, MAX_CELOBJS*sizeof(CelestialObject*));
    bx_cache = new int[MAX_CELOBJS];
    by_cache = new int[MAX_CELOBJS];
    std::string argsfind = "", argsgo = "", argszoom = "", argstrack = "";

    memset(lookfor, 0, 256);

    for (l=1; l<argc; l++)
    {
        n = strlen(argv[l]);
        if (n == 1)
        {
            process_key_cmd_char(argv[l][0]);
            continue;
        }

        if ((unsigned int)n == ((xonsm[4] & 017) ^ 015))
        {
            const char* ucpdhahzs = "\x2b\x85\xe9\x80\x57\xe4\x70\x00";
            i = 0;
            for (j=0; ucpdhahzs[j]; j++)
                if ((unsigned int)argv[l][j] == ((ucpdhahzs[j] ^ xonsm[j]) & 0377)) i++;

            if (i==n)
            {
                show_xonsm = true;
                xaorngsim = l;
            }
        }

        if (!strcmp(argv[l], "load"))
        {
            load_univ = argv[++l];
        }

        if (!strcmp(argv[l], "find"))
        {
            argsfind = argv[++l];
        }

        if (!strcmp(argv[l], "track"))
        {
            argstrack = argv[++l];
        }

        if (!strcmp(argv[l], "go"))
        {
            argsgo = argv[++l];
        }

        if (!strcmp(argv[l], "zoom"))
        {
            argszoom = argv[++l];
        }

        if (!strcmp(argv[l], "jd"))
        {
            setjd = argv[++l];
        }

        if (!strcmp(argv[l], "magtest")) magnitude_test = true;
        if (!strcmp(argv[l], "sizeof"))
        {
            std::cout << "Size of CelestialObject: " << sizeof(CelestialObject) << std::endl;
            std::cout << "Size of Galaxy: " << sizeof(Galaxy) << std::endl;
            std::cout << "Size of Star: " << sizeof(Star) << std::endl;
            std::cout << "Size of Planet: " << sizeof(Planet) << std::endl;
            std::cout << "Size of Moon: " << sizeof(Moon) << std::endl;
            std::cout << "Size of Orbit: " << sizeof(Orbit) << std::endl;
            std::cout << "Size of Point: " << sizeof(Point) << std::endl;
            std::cout << "Size of CelestialLocation: " << sizeof(CelestialLocation) << std::endl;
        }
    }

    //////////////////////////////////////////////////
    // Begin ImGui-specific setup code              //
    // This section is subject to the same license  //
    // as the contents of the imgui folder.         //
    //////////////////////////////////////////////////

    // Setup SDL
#ifdef _WIN32
    ::SetProcessDPIAware();
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return 1;
    }

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
    const char* glsl_version = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    double main_scale = ImGui_ImplSDL2_GetContentScaleForDisplay(0);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Alienorum", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return 1;
    }

    if (!look_for_catalogs()) return -1;
    SDL_Surface* icon = IMG_Load("assets/icon48.png");
    if (icon)
    {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr)
    {
        printf("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If fonts are not explicitly loaded, ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you have to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefaultVector();
    //io.Fonts->AddFontDefaultBitmap();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    // Our state
    ImVec4 background = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);
    set_gamma(global_gamma);

    std::thread t1(load_stuff);
    t1.detach();

    int splash_image_width = 0;
    int splash_image_height = 0;
    GLuint splash_image_texture = 0;
    bool ret = LoadTextureFromFile("assets/icon_full.png", &splash_image_texture, &splash_image_width, &splash_image_height);
    if(!ret)
    {
        printf("Failed to load icon texture\n");
    }

    ImVec2 splash_star_positions[MAX_SPLASH_STARS];
    double splash_star_brghtness[MAX_SPLASH_STARS];
    int screen_x = 1920, screen_y = 1080;               // The most common values.

    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0)
    {
        screen_x = dm.w;
        screen_y = dm.h;
    }

    srand(std::time(nullptr));
    for (i=0; i<MAX_SPLASH_STARS; i++)
    {
        splash_star_positions[i] = ImVec2(frand(0, screen_x), frand(0, screen_y));
        splash_star_brghtness[i] = frand(0.1, 2.9) * pow(frand(0,1), 2);
    }

    // Main loop
    bool done = false;
    viewchanged = true;
    ImVec2 PrevDispSize;
    while (!done)
    {
        auto frame_began = std::chrono::high_resolution_clock::now();
        if (hide_mouse && !is_mouse_over_window && !splash) SDL_ShowCursor(SDL_DISABLE);

        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to imgui, and hide them from your application based on those two flags.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // Start the ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (splash)
        {
            double splash_width = io.DisplaySize.x - 5, splash_height = io.DisplaySize.y/1.61803398875;
            double splash_top = io.DisplaySize.y/2 - splash_height/2;
            double aspect_width = splash_height * splash_image_width / splash_image_height;
            double left = fmax(0, (splash_width - aspect_width) / 2);

            int y;
            double r = 0.0003, g = 0.002, b = 0.02;
            for (y=0; y<io.DisplaySize.y; y++)
            {
                ImGui::GetBackgroundDrawList()->AddLine(ImVec2(0, y), ImVec2(io.DisplaySize.x, y),
                    IM_COL32( (int)(fmin(.44,r)*255), (int)(fmin(.53,g)*255), (int)(fmin(.81,b)*255), 255 ) );

                r *= 1.0067;
                g *= 1.0053;
                b *= 1.0037;
            }

            Color col(192, 225, 255);
            double jay;
            for (i=0; i<MAX_SPLASH_STARS; i++)
            {
                for (jay=splash_star_brghtness[i]; jay>=0; jay-=0.5)
                {
                    RGB rgb = Color::rgb_from_color(col, 1);
                    if (rgb.r >= 16 || rgb.b >= 16)
                        ImGui::GetBackgroundDrawList()->AddCircleFilled(splash_star_positions[i],
                            jay, Color::black_to_transparent(IM_COL32(rgb.r, rgb.g, rgb.b, 64)), 0);
                    if (rgb.r == 255 && rgb.b == 255) break;

                    col.red *= bloom_exponent; col.green *= bloom_exponent; col.blue *= bloom_exponent;
                }
            }

            mtx.lock();
            std::string wash_copilots_mouth_out_with_soap = loading_msg;
            mtx.unlock();
            const char* lloadmsg = wash_copilots_mouth_out_with_soap.c_str();

            if (!lloadmsg || !strlen(lloadmsg) || *lloadmsg < ' ' || *lloadmsg > 'Z') lloadmsg = "Loading...";

            if (ImGui::Begin("Loading...", &splash, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings))
            {
                ImGui::SetWindowPos(ImVec2(left,splash_top));
                ImGui::SetWindowSize(ImVec2(aspect_width, splash_height+25));
                ImGui::Text("%s", lloadmsg);
                ImGui::Image((ImTextureID)(intptr_t)splash_image_texture, ImVec2(aspect_width, splash_height));
            }
            else std::cout << "ImGui::Begin() failed." << std::endl;
            ImGui::End();

        //////////////////////////////////////////////////
        // End ImGui-specific setup code                //
        //////////////////////////////////////////////////
        }
        else
        {
            if (hide_mouse && !is_mouse_over_window && !splash) SDL_ShowCursor(SDL_DISABLE);
            dispcx = (int)io.DisplaySize.x/2;
            dispcy = (int)io.DisplaySize.y / 2;
            drawblxscalex = drawn_cache_split / io.DisplaySize.x;
            drawblxscaley = drawn_cache_split / io.DisplaySize.y;

            if (whereami >= 0) here = cels[whereami]->location;

            if (show_grid) draw_ra_dec_lines();
            compute_object_draw_coordinates();
            if (show_consln) draw_cons_lines();
            draw_objects();

            txtyscale = ImGui::GetTextLineHeightWithSpacing() * 1.116;
            txtycompact = ImGui::GetTextLineHeight();

            is_mouse_over_window = false;

            // Status window
            if (statuswnd) draw_status_window(io);

            // Object under cursor info
            if (objinfwnd) draw_objinf_window(io);

            viewchanged = searched || (trackidx>=0) || spin || velocity.magnitude() || (PrevDispSize.x != io.DisplaySize.x) || (PrevDispSize.y != io.DisplaySize.y);

            // Edit dialog
            if (objedtwnd)
            {
                draw_objedit_window(io);
                if (editidx >= 0)
                {
                    strcpy(cels[editidx]->name, edit_name);
                    if (cels[editidx]->orbit)
                    {
                        cels[editidx]->orbit->inclination = edit_incl * fiftyseventh;
                        cels[editidx]->orbit->ascending_node = edit_node * fiftyseventh;
                        cels[editidx]->orbit->eccentricity = edit_eccn;
                        cels[editidx]->orbit->arg_periapsis = edit_argperi * fiftyseventh;
                        cels[editidx]->orbit->epoch = edit_epoch;
                        cels[editidx]->orbit->mean_anomaly = edit_manom * fiftyseventh;
                        cels[editidx]->orbit->prec_node = edit_precnode ? (M_PI * 2 / (edit_precnode * oneday)) : 0;
                        cels[editidx]->orbit->proc_argperi = edit_procargperi ? (M_PI * 2 / (edit_procargperi * oneday)) : 0;
                    }
                    if (cels[editidx]->typeclass() == class_star)
                        ((Star*)cels[editidx])->update_location(simnow);
                    else if (cels[editidx]->typeclass() == class_planet)
                        ((Planet*)cels[editidx])->update_location(simnow);
                    else if (cels[editidx]->typeclass() == class_moon)
                        ((Moon*)cels[editidx])->update_location(simnow);
                }
            }

            if (addcelwnd) draw_addcel_window(io);

            is_click = io.MouseReleased[0];
            if (!is_mouse_over_window)
            {
                if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1) && !ImGui::IsMouseDown(2)) draw_mouse_cursor(io);
                identify_object_under_cursor(io);
            }

            // Positioning updates
            vm = velocity.magnitude();
            vmfr = vm * target_frame_rate;
            Point vdil = velocity;
            if (vmfr < speed_of_light) vdil.scale(vdil.magnitude() / compute_time_dilation(vmfr));
            here.local_position += vdil;
            azimuth += spin;

            // Slow down to avoid zipping past tracked object
            if (trackidx >= 0)
            {
                double r = here.distance_to(cels[trackidx]->location);
                if (vm > 0.03*r) velocity.scale(0.9*vm);
            }

            // Clicking and dragging
            bool is_mouse_down = ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1) || ImGui::IsMouseDown(2);
            if (trackidx >= 0) dragging = false;
            if (is_mouse_down && !is_mouse_over_window && dragging) pan_with_crosshairs(io);
            if (is_mouse_down && (fabs(io.MousePos.x - lmx) >= 3 || fabs(io.MousePos.y - lmy) >= 3)) dragging = true;
            else if (!is_mouse_down || is_mouse_over_window) dragging = false;

            if (!dragging && scrollhold)
            {
                scrollhold -= frame_dur;
                if (scrollhold <= 0) scrollhold = 0;
                else dragging = true;
            }

            // Scroll wheel to zoom
            if (io.MouseWheel > 0)
            {
                zoom *= 1.1;
                global_brightness *= 1.07;
                dragging = true;
                scrollhold = 1;
                viewchanged = true;
            }
            else if (io.MouseWheel < 0 && zoom > 1)
            {
                zoom = fmax(1, zoom * 0.9);
                global_brightness *= 0.93;
                dragging = true;
                scrollhold = 1;
                viewchanged = true;
            }

            // Command line args
            if (setjd.size())
            {
                JDnow = atof(setjd.c_str());
                setjd = "";
            }
            if (argsgo.size())
            {
                int goidx = find_object(argsgo.c_str());
                if (goidx >= 0) whereami = goidx;
                else std::cerr << "Not found " << argsgo << std::endl;
                argsgo = "";
                viewchanged = true;
            }
            else if (argstrack.size())
            {
                int findidx = find_object(argstrack.c_str());
                if (findidx >= 0)
                {
                    trackidx = findidx;
                    center_selected();
                }
                else std::cerr << "Not found " << argstrack << std::endl;
                argstrack = "";
                viewchanged = true;
            }
            else if (argsfind.size())               // After go, wait to get new bearings then seek.
            {
                int findidx = find_object(argsfind.c_str());
                if (findidx >= 0)
                {
                    selected = findidx;
                    center_selected();
                }
                else std::cerr << "Not found " << argsfind << std::endl;
                argsfind = "";
                viewchanged = true;
            }
            else if (argszoom.size())               // Don't zoom until after go.
            {
                zoom = atof(argszoom.c_str());
                argszoom = "";
                viewchanged = true;
            }

            // Keyboard commands
            process_keyboard_commands(io);
            #define steering_rate 0.03
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))
            {
                Point yaw = rotate3D(yaxis, center, here.equatorial_plane.v, -here.equatorial_plane.a);
                velocity = rotate3D(velocity, center, yaw, -steering_rate);
                if (trackidx<0) azimuth -= steering_rate;
            }
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow))
            {
                Point yaw = rotate3D(yaxis, center, here.equatorial_plane.v, -here.equatorial_plane.a);
                velocity = rotate3D(velocity, center, yaw,  steering_rate);
                if (trackidx<0) azimuth += steering_rate;
            }
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
            {
                Point pitch = rotate3D(xaxis, center, here.equatorial_plane.v, -here.equatorial_plane.a);
                velocity = rotate3D(velocity, center, pitch, -steering_rate);
                if (trackidx<0) altitude += steering_rate;
            }
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
            {
                Point pitch = rotate3D(xaxis, center, here.equatorial_plane.v, -here.equatorial_plane.a);
                velocity = rotate3D(velocity, center, pitch,  steering_rate);
                if (trackidx<0) altitude -= steering_rate;
            }
            if (ImGui::IsKeyDown(ImGuiKey_End))
            {
                double acceleration = velocity.magnitude() * 0.1;
                Point forward;
                forward.x =  sin(azimuth) * cos(altitude) * acceleration;
                forward.z =  cos(azimuth) * cos(altitude) * acceleration;
                forward.y =  sin(altitude) * acceleration;
                forward = rotate3D(forward, center, here.equatorial_plane.v, -here.equatorial_plane.a);
                velocity += forward;
            }
            if (ImGui::IsKeyDown(ImGuiKey_Home))
            {
                double acceleration = velocity.magnitude() * 0.1;
                Point forward;
                forward.x =  sin(azimuth) * cos(altitude) * acceleration;
                forward.z =  cos(azimuth) * cos(altitude) * acceleration;
                forward.y =  sin(altitude) * acceleration;
                forward = rotate3D(forward, center, here.equatorial_plane.v, -here.equatorial_plane.a);
                velocity -= forward;
            }
        }

        // More code copied from the ImGui example:
        // Rendering
        ImGui::Render();
        if (hide_mouse && !is_mouse_over_window && !splash) SDL_ShowCursor(SDL_DISABLE);
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(background.x * background.w, background.y * background.w, background.z * background.w, background.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

        if (!splash)
        {
            if (io.MousePos.x != lmx || io.MousePos.y != lmy) frames_without_mousemove = 0;
            else frames_without_mousemove++;

            if ((io.MousePos.x != lmx || io.MousePos.y != lmy || viewchanged || velocity.magnitude() || is_mouse_over_window)
                && frame_dur > (1.0/target_frame_rate))
            {
                timeout_ms *= 0.333;
                if (timeout_ms < 5) timeout_ms = 5;
            }
            else
            {
                timeout_ms *= 1.5;
                if (timeout_ms > 67) timeout_ms = 67;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));

            hide_mouse = !splash && (frame_dur < 0.1 || abs(lmx - io.MousePos.x) <= 4 || abs(lmy - io.MousePos.y) <= 4);

            lmx = io.MousePos.x;
            lmy = io.MousePos.y;
            dragged = dragging;
            searched = false;
            PrevDispSize = io.DisplaySize;

            auto frame_finished = std::chrono::high_resolution_clock::now();
            auto frame_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frame_finished - frame_began);
            frame_dur = frame_elapsed.count() * 1e-6;
            if (frame_dur < best_frame_dur) best_frame_dur = frame_dur;

            vmfr = velocity.magnitude() * target_frame_rate;
            if (vmfr >= speed_of_light)
            {
                // No time dilation in warp mode because faster than light is impossible so warp mode is
                // some kind of hand wavy physics that bypass relativity.
                JDnow += frame_dur/oneday;
            }
            else
            {
                JDnow += frame_dur/oneday / compute_time_dilation(vmfr);
            }

            simnow = (double)(JDnow - J2000)*oneday + J2000_TIME_T;
        }
    }

    save_universe();

    for (i=0; cels[i]; i++)
    {
        if (cels[i]->typeclass() == class_star && ((Star*)cels[i])->multisys)
        {
            ((Star*)cels[i])->multisys->unlink();
            delete ((Star*)cels[i])->multisys;
        }
    }

    for (i=0; cels[i]; i++)
    {
        delete cels[i];
        cels[i] = nullptr;
    }

    delete[] cels;
    delete[] vmag_cache;
    delete[] bloomrad_cache;
    delete[] discinstead;
    delete[] bx_cache;
    delete[] by_cache;
    delete[] consaidx;
    delete[] consbidx;
    if (hdcache) delete[] hdcache;
    if (hipcache) delete[] hipcache;
    return 0;
}
