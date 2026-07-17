
#include "globals.h"
#include "visuals.h"
#include "loaders.h"

using namespace alienorum;

double jay, appmag, bloomrad, flare, theta, lmasslim, hz_y;
ImVec2 xycoord;
ImFont *global_font = nullptr, *Greek_font = nullptr;
const char *Greek_symbol_mapping = "abgdezhuiklmnqoprstyfxjv";

void draw_ra_dec_lines()
{
    if (!cels[1]) return;
    int i, j;
    Cartesian2D prev, zdes;
    ImU32 gc = rgba_apply_redlight(global_style.grid_color);
    ImU32 gcb = rgba_apply_redlight(global_style.grid_color_brighter);
    ImU32 ec = rgba_apply_redlight(global_style.ecliptic_color);
    double node = (whereami >= 0) ? cels[whereami]->equinox_eff : 0;
    double myeq = (whereami >= 0) ? cels[whereami]->equinox_eff : 0;
    npaz = (view_mode == vm_horizon) ? fmod(npdummy.RA_as_radians(here, myeq), _pi*2) : 0;
    bool prev_valid = false;
    int jstart = -80; // (view_mode == vm_horizon) ? 0 : -80;
    bool is_sat = (whereami>0) && (cels[whereami]->typeclass() == class_satellite);
    Rotation ra_dec_plane = (whereami>0) ? (is_sat
            ? cels[whereami]->location.orbital_plane
            : cels[whereami]->location.equatorial_plane)
        : here.equatorial_plane;

    // RA and Dec lines.
    for (i=0; i<24; i++)
    {
        prev_valid = false;
        for (j=jstart; j<=80; j+=10)
        {
            Point jadolzhnaperejexatdoma = Point::from_ra_dec(fiftyseventh * i * 15, fiftyseventh * j, 5, node);
            if (view_mode == vm_horizon || is_sat)
            {
                jadolzhnaperejexatdoma = rotate3D(jadolzhnaperejexatdoma, center, ra_dec_plane.v, -ra_dec_plane.a);
                jadolzhnaperejexatdoma = to_viewer_plane(jadolzhnaperejexatdoma, 1);
                jadolzhnaperejexatdoma = rotate3D(jadolzhnaperejexatdoma, center, yaxis, -azimuth_correction);
            }
            zdes = Cartesian2D(jadolzhnaperejexatdoma, azimuth, altitude, zoom);
            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                prev_valid = false;
                continue;
            }

            if (j > jstart)
            {
                int dx1 = dispcx + zdes.x * dispcx,
                    dy1 = dispcy + zdes.y * dispcx,
                    dx2 = dispcx + prev.x * dispcx,
                    dy2 = dispcy + prev.y * dispcx;

                if (dx1 < -33554432 || dy1 < -33554432 || dx2 < -33554432 || dy2 < -33554432) continue;

                ImVec2 destart(dx1, dy1), deend(dx2, dy2);

                // if (distance(destart, deend) > 500) std::cout << "Dec gridline from " << dx1 << "," << dy1 << " to " << dx2 << "," << dy2 << std::endl;

                if (prev_valid)
                {
                    ImGui::GetBackgroundDrawList()->AddLine(destart, deend, gc, 1.1);
                }
            }

            prev = zdes;
            prev_valid = true;
        }
    }

    for (j=jstart; j <= 80; j+=10)
    {
        prev_valid = false;
        for (i=0; i<=360; i++)
        {
            Point umenjanetdeneg = Point::from_ra_dec(fiftyseventh * i, fiftyseventh * j, 5, node);
            if (view_mode == vm_horizon || is_sat)
            {
                umenjanetdeneg = rotate3D(umenjanetdeneg, center, ra_dec_plane.v, -ra_dec_plane.a);
                umenjanetdeneg = to_viewer_plane(umenjanetdeneg, 1);
                umenjanetdeneg = rotate3D(umenjanetdeneg, center, yaxis, -azimuth_correction);
            }
            zdes = Cartesian2D(umenjanetdeneg, azimuth, altitude, zoom);
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

                if (dx1 < -33554432 || dy1 < -33554432 || dx2 < -33554432 || dy2 < -33554432) continue;

                ImVec2 rastart(dx1, dy1), raend(dx2, dy2);

                // if (distance(rastart, raend) > 500) std::cout << "RA gridline from " << dx1 << "," << dy1 << " to " << dx2 << "," << dy2 << std::endl;

                if (prev_valid)
                {
                    ImGui::GetBackgroundDrawList()->AddLine(rastart, raend, j ? gc : gcb, 1.1);
                }
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
            pt = to_viewer_plane(pt);

            zdes = Cartesian2D(pt, azimuth+azimuth_correction, altitude, zoom);

            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                prev_valid = false;
                continue;
            }

            if (view_mode == vm_horizon && pt.y<0)
            {
                prev = zdes;
                prev_valid = true;
                continue;
            }

            if (i & 1)
            {
                int dx1 = dispcx + zdes.x * dispcx,
                    dy1 = dispcy + zdes.y * dispcx,
                    dx2 = dispcx + prev.x * dispcx,
                    dy2 = dispcy + prev.y * dispcx;

                if (view_mode == vm_skymap)
                {
                    if (dx1 > dx2 + 1.9 * dispcx) dx2 += dispcx*2;
                    if (dx2 > dx1 + 1.9 * dispcx) dx1 += dispcx*2;
                    if (dy1 > dy2 + 1.9 * dispcy) dy2 += dispcy*2;
                    if (dy2 > dy1 + 1.9 * dispcy) dy1 += dispcy*2;
                }

                if (prev_valid)
                ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx1, dy1), ImVec2(dx2, dy2), ec, 1.1);
            }

            prev = zdes;
            prev_valid = true;
        }
    }
}

double sphresolution = 0.1;
bool bugged = false;
int draw_sphere(CelestialObject* cel, double arad)
{
    if (cel->seqno == whereami) return 0;
    double d = cel->tmprel.magnitude(), horizon_angle, elevation = 0;
    cel_obj_class cls = cel->typeclass();

    if (d > light_year*zoom) return 0;

    double bs = 0;
    if (cls == class_planet || cls == class_moon)
    {
        bs = (((Planet*)cel)->surf_map && ((Planet*)cel)->surf_map->has_bump_data())
            ? ((Planet*)cel)->estimate_bump_scale() : 0;
    }

    if ((d < cel->volumetric_mean_radius || (cls == class_satellite && d < 100)))
    {
        if (velocity.magnitude() && took_off_from != cel->seqno)
        {
            double d1 = (cel->tmprel + velocity).magnitude();
            if (d1 > d)
            {
                if (cls == class_star)
                {
                    whereami = selected = trackidx = -1;
                    here = cels[0]->location;
                    here.local_position.y -= AU;
                    velocity = Point(0,0,0);
                    memset( &cels[1], 0, MAX_CELOBJS-2);
                    return 0;
                }
                else if (cls == class_satellite)
                {
                    whereami = cel->seqno;
                    velocity = Point(0,0,0);
                    return 0;
                }
                else
                {
                    here.system_center = cel->location.system_center;
                    here.equatorial_plane = cel->location.equatorial_plane;
                    viewer_lon = cel->RA_as_radians(here, /*cel->equinox +*/ cel->timeofday()) - _pi;
                    viewer_lat = -cel->Decl_as_radians(here);
                    // save_viewer_latlon = false;
                    whereami = cel->seqno;
                    velocity = Point(0,0,0);
                    view_mode = vm_horizon;
                    altitude = 0;
                    trackidx = -1;
                }
            }
        }
    }
    else if (tookoff_countdown)
    {
        tookoff_countdown--;
        if (!tookoff_countdown) took_off_from = -1;
    }

    cel->drawnxmin = cel->drawnxmax = cel->drawnx;
    cel->drawnymin = cel->drawnymax = cel->drawny;
    if (sphresolution < 0.001/sphere_quality) sphresolution = 0.001/sphere_quality;
    bool wireframe = dragging || !cel->onscreen || d < cel->volumetric_mean_radius;
    if (whereami<0 || cels[whereami]->type != artificial) cel->onscreen = false;
    int i, j, l, m, lastm, n, result=0;
    Cartesian2D prev, zdes;
    std::vector<ImVec2> todraw;
    std::vector<Point> tdland;
    std::vector<double> tdlat, tdlon;
    std::vector<bool> tdvalid;
    ImU32 gc = rgba_apply_redlight(IM_COL32(176, 170, 164, 255));
    ImU32 gm = rgba_apply_redlight(IM_COL32(  0, 255,   0, 255));
    Color daylight = Color::color_from_magnitude_indices(0, cel->get_light_center()->BV_color);
    double f = fmax(fmax(daylight.red, daylight.green), daylight.blue);
    daylight.red /= f;
    daylight.green /= f;
    daylight.blue /= f;

    // Compensate for the eye's white balance adjustment
    daylight.red = pow(daylight.red, 0.333);
    daylight.green = pow(daylight.green, 0.333);
    daylight.blue = pow(daylight.blue, 0.333);

    if (wireframe)
    {
        Color wcol = Color::color_from_magnitude_indices(0, cel->BV_color);
        RGB3Byte wrgb = Color::rgb_from_color(wcol, -1);
        gc = rgba_apply_redlight(IM_COL32(wrgb.r, wrgb.g, wrgb.b, 255));
    }

    bool prev_valid = false;
    bool dwh = false;

    if (cls == class_moon)
        dwh = (((Moon*)cel)->depth > zero_isnt_really_zero
            && ((Moon*)cel)->width > zero_isnt_really_zero
            && ((Moon*)cel)->height > zero_isnt_really_zero);

    double equatorial_radius, theta, vtheta, cos_theta, cos_vtheta, is_day, is_night;
    if (dwh)
        equatorial_radius = pow(((Moon*)cel)->depth * ((Moon*)cel)->width, 0.5) * .5;
    else
        equatorial_radius = cel->get_equatorial_radius();

    double lat, lon, z_cutoff = d + equatorial_radius * 0.2, obl = 1.0 - cel->oblateness;

    if (!wireframe && !cel->looked_for_maps)
    {
        cel->looked_for_maps = true;                // Prevent spawning infinite threads and crashing the system.
        std::thread ttex(load_textures, cel);
        ttex.detach();
    }

    horizon_angle = cel->get_horizon_angle();
    bool worth_using_map = (bloomrad_cache[cel->seqno] > 5);                // Only if the disc will be big enouh to see any details.

    int i360, latmin = 1e9, latmax = -1e9, lonmin = 1e9, lonmax = -1e9, nstep = wireframe ? 10 : 5;
    for (i=0; i<=360; i+=nstep)
    {
        i360 = (i>=180 && lonmin<=0 && lonmax<180) ? (i - 360) : i;           // Catch if visible longitudes wrap around zero.
        prev_valid = false;
        for (j=-90; j<=90; j+=nstep)
        {
            Point cursor = Point::from_ra_dec(fiftyseventh * i, fiftyseventh * j, dwh ? 1 : equatorial_radius, 0);

            if (dwh)
            {
                cursor.x *= ((Moon*)cel)->width * .5;
                cursor.y *= ((Moon*)cel)->height * .5;
                cursor.z *= ((Moon*)cel)->depth * .5;
            }
            else cursor.y *= obl;
            cursor = rotate3D(cursor, center, yaxis, -cel->timeofday());

            cursor = rotate3D(cursor, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);
            cursor += cel->tmprel;
            vtheta = fabs(fmod(find_3D_angle(cursor, center, cel->tmprel), _pi*2));
            cursor = to_viewer_plane(cursor);
            if (cursor.magnitude() > z_cutoff)
            {
                prev_valid = false;
                continue;
            }
            zdes = Cartesian2D(cursor, azimuth+azimuth_correction, altitude, zoom);
            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                prev_valid = false;
                continue;
            }

            if (vtheta < horizon_angle)
            {
                if (i >= 180) i360 = i;

                if (i360 < lonmin) lonmin = i360;
                if (i360 > lonmax) lonmax = i360;

                if (j < latmin) latmin = j;
                if (j > latmax) latmax = j;
            }

            if (wireframe && (j > -80))
            {
                int dx1 = dispcx + zdes.x * dispcx,
                    dy1 = dispcy + zdes.y * dispcx,
                    dx2 = dispcx + prev.x * dispcx,
                    dy2 = dispcy + prev.y * dispcx;

                if (view_mode == vm_skymap)
                {
                    if (dx1 > dx2 + 1.9 * dispcx) dx2 += dispcx*2;
                    if (dx2 > dx1 + 1.9 * dispcx) dx1 += dispcx*2;
                    if (dy1 > dy2 + 1.9 * dispcy) dy2 += dispcy*2;
                    if (dy2 > dy1 + 1.9 * dispcy) dy1 += dispcy*2;
                }

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
    RGB3Byte rgb = Color::rgb_from_color(Color::color_from_magnitude_indices(4.2, cel->BV_color), -1), nrgb = {0,0,0};
    Point cursor, land;
    CelestialObject *lightcen = cel->get_light_center();
    bool self_luminous = (lightcen == cel);
    ImU32 imcol;

    auto sphere_began = std::chrono::high_resolution_clock::now();
    double step = wireframe
            ? (fiftyseventh*15)
            : ( (worth_using_map && (cel->surf_map || cel->cloud_map))
                ? fmax(fmin(_pi*sphresolution/arad*fiftyseventh, fiftyseventh*15), fiftyseventh*0.2)
                : fiftyseventh * 3
              ),
        stepcoslat, invlaststepcoslat = 1.0 / step;
    int perline=0, dx1, dy1, dx2, dy2;
    double polyr, polyg, polyb, lum, lum1;
    l = 0;

    bool lonmin_crosses_zero = (lonmin <= 0 && lonmax < 180), filter_longitudes = ((lonmax - lonmin) <= 180);
    double latmin_rad = fiftyseventh * (latmin-5) - step, latmax_rad = fiftyseventh * (latmax+5) + step,
        lonmin_rad = fiftyseventh * lonmin, lonmax_rad = fiftyseventh * lonmax;
    double lon360;
    for (lat=-half_pi; lat <= half_pi; lat+=step)
    {
        if (lat < latmin_rad || lat > latmax_rad) continue;
        prev_valid = false;
        n = 0;
        stepcoslat = step / (cos(lat) + 0.1);
        for (lon=0; lon<=_pi*2; lon+=stepcoslat)
        {
            lon360 = lonmin_crosses_zero ? (lonmin_rad - _pi*2) : lonmin_rad;
            if (filter_longitudes && (lon360 < (lonmin_rad - stepcoslat) || lon360 > (lonmax_rad + stepcoslat))) continue;
            n++;
            elevation = (map && bs) ? (map->elevation_at(lat, lon)) : 0;
            land = Point::from_ra_dec(lon+_pi, lat, dwh ? 1 : (equatorial_radius + elevation), 0);

            if (dwh)
            {
                land.x *= ((Moon*)cel)->width  * .5;
                land.y *= ((Moon*)cel)->height * .5;
                land.z *= ((Moon*)cel)->depth  * .5;
                if (elevation) land.scale(land.magnitude()+elevation);          // TODO: This is a costly calculation - possible to streamline it?
            }
            else land.y *= obl;
            land = rotate3D(land, center, yaxis, -cel->timeofday());

            land = rotate3D(land, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);
            cursor = land + cel->tmprel;
            cursor = to_viewer_plane(cursor);
            if (cursor.magnitude() > z_cutoff)
            {
                todraw.push_back(ImVec2(0,0));
                tdvalid.push_back(false);
                tdland.push_back(center);
                tdlat.push_back(lat);
                tdlon.push_back(lon);
                l++;
                prev_valid = false;
                continue;
            }

            zdes = Cartesian2D(cursor, azimuth+azimuth_correction, altitude, zoom);
            if (zdes.x < -1e4 || zdes.y < -1e4 || prev.x < -1e4 || prev.y < -1e4)
            {
                todraw.push_back(ImVec2(0,0));
                tdvalid.push_back(false);
                tdland.push_back(center);
                tdlat.push_back(lat);
                tdlon.push_back(lon);
                l++;
                prev_valid = false;
                continue;
            }

            if (lon)
            {
                land += cel->location.local_position;
                vtheta = fabs(fmod(find_3D_angle(land, here.local_position, cel->location.local_position), _pi*2));
                if (!wireframe && vtheta > horizon_angle)
                {
                    todraw.push_back(ImVec2(-1e13, -2e13));
                    tdvalid.push_back(false);
                    tdland.push_back(center);
                    tdlat.push_back(lat);
                    tdlon.push_back(lon);
                }
                else
                {
                    dx1 = dispcx + zdes.x * dispcx;
                    dy1 = dispcy + zdes.y * dispcx;
                    dx2 = dispcx + prev.x * dispcx;
                    dy2 = dispcy + prev.y * dispcx;

                    if (view_mode == vm_skymap)
                    {
                        if (dx1 > dx2 + 1.9 * dispcx) dx2 += dispcx*2;
                        if (dx2 > dx1 + 1.9 * dispcx) dx1 += dispcx*2;
                        if (dy1 > dy2 + 1.9 * dispcy) dy2 += dispcy*2;
                        if (dy2 > dy1 + 1.9 * dispcy) dy1 += dispcy*2;
                    }

                    double yd = (dy1 - cel->drawny);
                    if (yd > result) result = yd;

                    ImVec2 v = ImVec2(dx1, dy1);
                    if (prev_valid)
                    {
                        if (wireframe) ImGui::GetBackgroundDrawList()->AddLine(v, ImVec2(dx2, dy2), gc, 1);
                        if (zdes.x > -1 && zdes.x < 1 && zdes.y > -1 && zdes.y < 1)
                        {
                            cel->onscreen = true;
                            if (dx1 < cel->drawnxmin) cel->drawnxmin = dx1;
                            if (dx1 > cel->drawnxmax) cel->drawnxmax = dx1;
                            if (dy1 < cel->drawnymin) cel->drawnymin = dy1;
                            if (dy1 > cel->drawnymax) cel->drawnymax = dy1;
                        }
                    }

                    todraw.push_back(v);
                    tdvalid.push_back(true);
                    // Also store 3D coordinates of each vertex.
                    tdland.push_back(land);
                    tdlat.push_back(lat);
                    tdlon.push_back(lon);

                    if (!wireframe && (lat>-half_pi) && !dragging && perline)
                    {
                        m = l - n - perline + round(lon*invlaststepcoslat) + 2;
                        if (m > 1 && tdvalid[l-1] && m < l && tdvalid[m] && tdvalid[m-1])
                        {
                            if (self_luminous)
                            {
                                cos_vtheta = cos(vtheta);
                                is_day = fmin(1, pow(cos_vtheta, 0.333));
                            }
                            else
                            {
                                // theta = fmod(find_3D_angle(land, lightcen->location.local_position, cel->location.local_position), _pi);

                                // Shade based on the normal of the 3D coordinates of the polygon vertices instead of angle to sun and cel center.
                                Point normal = compute_normal(land, tdland[l-1], tdland[m]) + compute_normal(tdland[l-1], tdland[m-1], tdland[m]);
                                theta = fmod(find_3D_angle(cel->location.local_position - normal, lightcen->location.local_position, cel->location.local_position), _pi);
                                if (fabs(theta) < half_pi)
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
                            double maplat = 0.25 * (lat + tdlat[l-1] + tdlat[m-1] + tdlat[m]);
                            double maplon = interpolate_angles(
                                interpolate_angles(lon, tdlon[l-1]),
                                interpolate_angles(tdlon[m-1], tdlon[m]));
                            if (map && is_day && worth_using_map) rgb = map->color_at(maplat, maplon-_pi);

                            if (view_mode == vm_skymap)
                            {
                                if (points[1].x > points[0].x + 1.9 * dispcx) points[0].x += dispcx*2;
                                if (points[2].x > points[0].x + 1.9 * dispcx) points[0].x += dispcx*2;
                                if (points[3].x > points[0].x + 1.9 * dispcx) points[0].x += dispcx*2;
                                if (points[0].x > points[1].x + 1.9 * dispcx) points[1].x += dispcx*2;
                                if (points[0].x > points[2].x + 1.9 * dispcx) points[2].x += dispcx*2;
                                if (points[0].x > points[3].x + 1.9 * dispcx) points[3].x += dispcx*2;

                                if (dy1 > dy2 + 1.9 * dispcy) dy2 += dispcy*2;
                                if (dy2 > dy1 + 1.9 * dispcy) dy1 += dispcy*2;
                            }

                            RGB3Byte rgblit = rgb;
                            rgblit.r *= daylight.red;
                            rgblit.g *= daylight.green;
                            rgblit.b *= daylight.blue;

                            if (nmap && worth_using_map)
                            {
                                is_night = 1.0 - is_day;
                                if (is_night)
                                {
                                    nrgb = nmap->color_at(maplat, maplon-_pi);

                                    polyr = is_day*rgblit.r + is_night*nrgb.r;
                                    polyg = is_day*rgblit.g + is_night*nrgb.g;
                                    polyb = is_day*rgblit.b + is_night*nrgb.b;
                                }
                            }
                            else
                            {
                                polyr = is_day*rgblit.r;
                                polyg = is_day*rgblit.g;
                                polyb = is_day*rgblit.b;
                            }

                            if (view_mode == vm_horizon)
                            {
                                if (sky_grad.find(dy1) != sky_grad.end())
                                {
                                    lum = sky_grad[dy1].luminance() * 0.003921569;
                                    lum1 = 1.0 - lum;
                                    polyr = fmin(255, lum1 * polyr + sky_grad[dy1].r);
                                    polyg = fmin(255, lum1 * polyg + sky_grad[dy1].g);
                                    polyb = fmin(255, lum1 * polyb + sky_grad[dy1].b);
                                }
                            }

                            imcol = rgba_apply_redlight(IM_COL32(polyr, polyg, polyb, 255));

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

        perline = max(0, n);
        invlaststepcoslat = 1.0/stepcoslat;
    } // for lat

    if (!wireframe && !dragging && (l > perline*2))
    {
        auto points = std::make_unique<ImVec2[]>(perline);
        n = 0;
        for (i=0; i<perline; i++)
        {
            j = l-perline-i-1;
            if (!tdvalid[j]) continue;
            points[n++] = todraw[j];
        }

        // Certain vars are left over from the last iteration; assume values are still good.
        ImU32 imcol = rgba_apply_redlight(IM_COL32(is_day*rgb.r, is_day*rgb.g, is_day*rgb.b, 255));
        ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(points.get(), n, imcol);
    }

    // Rings
    if (cls == class_planet && ((Planet*)cel)->ring_radius)
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

        n = round(_pi*2/step) * 13;
        m = fmax(4, fmin(result, round(_pi*2/step)/2));
        double step1 = (double)ringsize / m, step2 = _pi*2/n;

        Map *rmap = cel->ring_map, *rxmap = cel->ringx_map;
        rgb = {225, 208, 192};
        for (ringd = equatorial_radius; ringd <= pl->ring_radius; ringd += step1)
        {
            double xmapd = (double)(ringd - equatorial_radius) * _pi*2 / ringsize;
            double ring_opacity = rxmap ? (255.0 * (1.0-pow((double)rxmap->color_at(0, xmapd).g/255, gossamer_rings))) : 0.5;
            if (rmap) rgb = rmap->color_at(0, xmapd);
            double lonlim = _pi*2+0.5*step2;

            for (lon=0; lon<lonlim; lon+=step2)
            {
                dust = Point::from_ra_dec(lon+_pi, 0, ringd, 0);

                dust = rotate3D(dust, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);
                dust += cel->tmprel;
                Point yardstick = center - dust;
                yardstick.scale(equatorial_radius*2);
                yardstick += dust;
                cursor = to_viewer_plane(dust);

                if (cursor.magnitude() > z_cutoff && cel->tmprel.get_distance_to_line(dust, yardstick) < equatorial_radius )
                {
                    todrawr.push_back(ImVec2(-1e29,-1e53));
                    tdvalidr.push_back(false);
                    l++;
                    prev_valid = false;
                    prev = zdes;
                    continue;
                }

                zdes = Cartesian2D(cursor, azimuth+azimuth_correction, altitude, zoom);
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

                if (view_mode == vm_skymap)
                {
                    if (dx1 > dx2 + 1.9 * dispcx) dx2 += dispcx*2;
                    if (dx2 > dx1 + 1.9 * dispcx) dx1 += dispcx*2;
                    if (dy1 > dy2 + 1.9 * dispcy) dy2 += dispcy*2;
                    if (dy2 > dy1 + 1.9 * dispcy) dy1 += dispcy*2;
                }

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
            if (sphresolution < 0.2/sphere_quality)
            {
                sphresolution *= 1.3;
                if (sphere_elapsed.count() >= (3e5*sphere_quality)) sphresolution *= 2;
            }
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

void draw_flare(double flare, Color col)
{
    double divisor = 255.0 / fmax(fmax(col.blue, col.red), col.green);
    RGB3Byte rgb;
    rgb.r = (int)(col.red * divisor);
    rgb.g = (int)(col.green* divisor);
    rgb.b = (int)(col.blue * divisor);

    #define jmax 3
    for (int j=jmax; j>0; j--)
    {
        jay = 0.25 + 0.25 * j * flare;
        double jay15 = jay+max_bloomrad;
        ImVec2 radii(jay15, jay15*0.333);
        ImU32 fcol = rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, (jmax+1-j)*2));
        double thoff = _pi*0.1*j;
        for (theta=0; theta<_pi*2; theta += _pi*0.2)
            ImGui::GetBackgroundDrawList()->AddEllipseFilled(xycoord, radii, fcol, theta+thoff);
    }
}

int draw_satellite_icon(ImVec2 xycoord, ImU32 satcol)
{
    // Satellite icons.
    ImVec2 antenna_top              = ImVec2(xycoord.x,                                             xycoord.y - antenna_height  );
    ImVec2 panel_left_stem          = ImVec2(xycoord.x - antenna_height,                            xycoord.y                   );
    ImVec2 panel_right_stem         = ImVec2(xycoord.x + antenna_height,                            xycoord.y                   );
    ImVec2 panel_left_topprox       = ImVec2(xycoord.x - antenna_height + panel_tilt,               xycoord.y - antenna_height  );
    ImVec2 panel_left_topdist       = ImVec2(xycoord.x - antenna_height + panel_tilt - panel_width, xycoord.y - antenna_height  );
    ImVec2 panel_left_botprox       = ImVec2(xycoord.x - antenna_height - panel_tilt,               xycoord.y + antenna_height  );
    ImVec2 panel_left_botdist       = ImVec2(xycoord.x - antenna_height - panel_tilt - panel_width, xycoord.y + antenna_height  );
    ImVec2 panel_right_topprox      = ImVec2(xycoord.x + antenna_height + panel_tilt,               xycoord.y - antenna_height  );
    ImVec2 panel_right_topdist      = ImVec2(xycoord.x + antenna_height + panel_tilt + panel_width, xycoord.y - antenna_height  );
    ImVec2 panel_right_botprox      = ImVec2(xycoord.x + antenna_height - panel_tilt,               xycoord.y + antenna_height  );
    ImVec2 panel_right_botdist      = ImVec2(xycoord.x + antenna_height - panel_tilt + panel_width, xycoord.y + antenna_height  );

    ImGui::GetBackgroundDrawList()->AddLine(xycoord, antenna_top, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_left_stem, panel_right_stem, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_left_topprox, panel_left_topdist, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_left_botdist, panel_left_topdist, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_left_botdist, panel_left_botprox, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_left_topprox, panel_left_botprox, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_right_topprox, panel_right_topdist, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_right_botdist, panel_right_topdist, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_right_botdist, panel_right_botprox, satcol, 1);
    ImGui::GetBackgroundDrawList()->AddLine(panel_right_topprox, panel_right_botprox, satcol, 1);

    return antenna_height + panel_tilt + panel_width;
}

double global_magshift;
bool draw_one_object(int i)
{
    int j;
    cel_obj_class cls = cels[i]->typeclass();
    xycoord = ImVec2(cels[i]->drawnx, cels[i]->drawny);
    appmag = vmag_cache[i] - sky_mag_shift;
    double brght = pow(magnbase, -appmag);
    bloomrad = fabs(pow(brght, 0.5)*global_brightness);
    flare = (bloomrad>max_bloomrad) ? fmin(max_flare, fmax(0, 1.0+sqrt(bloomrad-1.5*max_bloomrad)*8)) : 0;
    bloomrad = fmin(max_bloomrad, bloomrad*10);
    if (cls == class_satellite)
    {
        if (!show_sats) return false;
        if (cels[i]->orbit && (cels[i]->tmprel.magnitude() > cels[i]->orbit->semimajor_axis*zoom*6))
        {
            cels[i]->drawnx = cels[i]->drawny = -1e9;
            return false;
        }

        double line_of_sight = cels[i]->orbit->center->location.local_position.get_distance_to_line(
            cels[i]->location.local_position, cels[i]->get_light_center()->location.local_position);

        ImU32 satcol = (i == selected)
            ? rgba_apply_redlight(global_style.selected_color)
            : ((line_of_sight < cels[i]->volumetric_mean_radius)
                ? rgba_apply_redlight(IM_COL32(128,  96,  64, 255))
                : rgba_apply_redlight(IM_COL32(255, 255, 255, 255)));

        if (show_labels || lbl_localsys || show_consln || show_grid)
        {
            bloomrad_cache[i] = bloomrad = draw_satellite_icon(xycoord, satcol);
        }
        else
        {
            ImGui::GetBackgroundDrawList()->AddCircleFilled(xycoord, 1, satcol);
            bloomrad_cache[i] = bloomrad = 1;
        }
    }
    else if (angular_radius[i]*zoom > sphere_rad_threshold)
    {
        if (flare)
        {
            Color col = Color::color_from_magnitude_indices(appmag, cels[i]->BV_color);
            draw_flare(flare, col);
        }

        CelestialObject *cel = cels[i];
        bloomrad_cache[i] = bloomrad = draw_sphere(cel, angular_radius[i]*zoom);
        discinstead[i] = false;
        if (!cels[1]) return false;

        if (selected == i)
        {
            ImGui::GetBackgroundDrawList()->AddCircle(xycoord, bloomrad+2, rgba_apply_redlight(global_style.selected_color), 0, 2);
        }
    }
    else
    {
        discinstead[i] = false;

        Color col = Color::color_from_magnitude_indices(appmag, cels[i]->BV_color);

        // Adjust for mesopic and scotopic color perception, e.g. dim red stars tend to look grayish.
        float effmag = vmag_cache[i] + global_magshift;
        if (effmag > 2)
        {
            double effect = fmin(1, (effmag - 2) / 8), effect1 = 1.0 - effect;
            col.red = effect*0.5*col.green + effect1*col.red;
            col.blue = effect*col.green + effect1*col.blue;
        }

        if (flare) draw_flare(flare, col);

        brght = pow(magnbase, -appmag) * global_brightness * 50;
        double circ, lbrght, lpxval, tosub, softmod = 1.0 - bloom_softness;
        // if (i == 1075) std::cout << i << ":" << cels[i]->name << ": " << brght << std::endl;
        bool first = true;
        std::vector<double> circradii, circpixvals;
        for (bloomrad = 0.5; brght > 0; bloomrad += 0.5)
        {
            if (first)
            {
                double area = _pi * bloomrad * bloomrad;
                lbrght = brght*softmod;
                lpxval = lbrght/area;
                tosub = fmin(area, lbrght);
                circradii.push_back(bloomrad);
                circpixvals.push_back(fmin(1, lpxval));
                // if (i == 1075) std::cout << " area " << area << " so " << brght << " - " << tosub << " = ";
                brght -= tosub;
                // if (i == 1075) std::cout << brght << std::endl;
                first = false;
            }
            else
            {
                circ = 2.0 * _pi * bloomrad;
                lbrght = brght*softmod;
                lpxval = lbrght/circ;
                tosub = fmin(circ, lbrght);
                circradii.push_back(bloomrad);
                circpixvals.push_back(fmin(1, lpxval));
                // if (i == 1075) std::cout << " circ " << circ << " so " << brght << " - " << tosub << " = ";
                brght -= tosub;
                // if (i == 1075) std::cout << brght << std::endl;
            }
            if (bloomrad >= max_bloomrad) break;
            if (lpxval < 0.03) break;
        }
        bloomrad_cache[i] = fmin(1.414, bloomrad);

        double divisor = 1.0 / fmin(col.red, col.blue);
        col.red *= divisor; col.green *= divisor; col.blue *= divisor;
        int n = circradii.size();
        // if (i == 1075) std::cout << n << " radii:" << std::endl;
        for (j=n-1; j>=0; j--)
        {
            jay = circradii[j];
            RGB3Byte rgb = Color::rgb_from_color(col, circpixvals[j]);
            // if (i == 1075) std::cout << " draw radius " << jay << " pixel value * " << circpixvals[j] << std::endl;
            if (rgb.r >= 8 || rgb.b >= 8)
            {
                ImGui::GetBackgroundDrawList()->AddCircleFilled(xycoord, fmax(0.9, jay),
                    Color::black_to_transparent(IM_COL32(rgb.r, rgb.g, rgb.b, 255)), 0);
                cels[i]->onscreen = true;
            }
            if (rgb.r == 255 && rgb.b == 255) break;
        }
    }
    if (selected == i && cels[1])
    {
        ImGui::GetBackgroundDrawList()->AddCircle(xycoord, bloomrad+2, rgba_apply_redlight(global_style.selected_color), 0, 2);
    }

    if ( (show_labels && cels[i]->type == star && !cels[i]->orbit &&
            ((!cbolbls_selected_idx && appmag <= appmagn_lblcut)
            || (cbolbls_selected_idx == lbltype_intrinsic && cels[i]->absolute_magnitude <= absmagn_lblcut)
            || (cbolbls_selected_idx == lbltype_nearby && here.distance_to(cels[i]->location) <= distance_lblcut)
            || (cbolbls_selected_idx == lbltype_Bayer && strlen(((Star*)cels[i])->Bayer))
            || (cbolbls_selected_idx == lbltype_Flamsteed && strlen(((Star*)cels[i])->Flamsteed))
            || (cbolbls_selected_idx == lbltype_Gould && (((Star*)cels[i])->GouldNo > 0))
            || (cbolbls_selected_idx == lbltype_sunlike && ((Star*)cels[i])->is_sunlike())
            || (cbolbls_selected_idx == lbltype_planets && (((Star*)cels[i])->has_planets >= planets_lblcut) )
            || (cbolbls_selected_idx == lbltype_planethz && (((Star*)cels[i])->has_hz_planets) )
            || (cbolbls_selected_idx == lbltype_binary && (((Star*)cels[i])->multisys))
            || (cbolbls_selected_idx == lbltype_knpole && cels[i]->known_poles)
            ))
        || ((cels[i]->cenobj == mycenobj) && lbl_localsys
            && ((cels[i]->mass >= lmasslim)
                || (vmag_cache[i] < (mag_limit_adjusted-4))
                || (cels[i]->tmprel.magnitude() < AU)
                )
            )
        || i == selected)
    {
        const char *dispname = cels[i]->name;
        ImFont *font = global_font;
        std::string str;
        cel_obj_class cls = cels[i]->typeclass();
        double lfontsz = global_font_size;
        if (cbolbls_selected_idx == lbltype_Bayer && cls == class_star)
        {
            // str = trim(std::string(((Star*)cels[i])->Bayer).substr(0, strlen(((Star*)cels[i])->Bayer)-3));
            // dispname = str.c_str();
            char c = Greek_symbol_mapping[((Star*)cels[i])->BayerGrkno];
            str = std::string(1, c);
            if (((Star*)cels[i])->Bayer[3] >= '1') str += std::string(1, ((Star*)cels[i])->Bayer[3]);
            dispname = str.c_str();
            font = Greek_font;
            lfontsz *= 1.312;           // for better visibility
        }
        else if (cbolbls_selected_idx == lbltype_Flamsteed && cls == class_star)
        {
            str = trim(std::string(((Star*)cels[i])->Flamsteed).substr(0, strlen(((Star*)cels[i])->Flamsteed)-3));
            dispname = str.c_str();
        }
        else if (cbolbls_selected_idx == lbltype_Gould && cls == class_star)
        {
            str = std::to_string(((Star*)cels[i])->GouldNo);
            dispname = str.c_str();
        }
        ImVec2 sz = ImGui::CalcTextSize(dispname);
        ImGui::GetBackgroundDrawList()->AddText(font, lfontsz, ImVec2(cels[i]->drawnx - sz.x/2, cels[i]->drawny+bloomrad+1),
            rgba_apply_redlight((i == selected) ? global_style.selected_color : global_style.objlbl_color),
            dispname);
    }
    return true;
}

void draw_objects()
{
    if (!ncelobjs) return;
    int i, j, n, pass;
    double step, dispw = dispcx*2, disph = dispcy*2;
    double orbseg = 81;
    lmasslim = lbllsys_mass_lim*1000;
    std::vector<CelestialObject*> to_draw_layered;
    global_magshift = -log(global_brightness) * invlogmagnbase;

    double mycensq = mycenobj->tmprel.squared_magnitude();
    double layer_cutoff = mycensq * 1.1 * zoom * zoom;
    mag_limit_adjusted = log(pow(magnbase, normal_best_mag_limit)*zoom) * invlogmagnbase;

    Point viewer_pole = to_viewer_plane(yaxis);
    Rotation viewer_plane = align_points_3d(viewer_pole, yaxis, center);

    // Orbits
    if (show_orbits) for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        if (!cels[i]->orbit) continue;
        if (cels[i]->cenobj != mycenobj && (whereami<0 || cels[i]->orbit->center != cels[whereami])) continue;
        if (cels[i]->orbit->center == mycenobj && cels[i]->mass < lmasslim) continue;

        Color col = Color::color_from_magnitude_indices(vmag_cache[i] + 5, cels[i]->BV_color);
        RGB3Byte rgb = Color::rgb_from_color(col, 1);
        ImU32 imcol = (i==selected) ? rgba_apply_redlight(global_style.selected_orbit_color) : rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, 64));
        step = cels[i]->orbit->period / orbseg;
        CelestialLocation was = cels[i]->location;
        bool is_star = (cels[i]->typeclass() == class_star),
            is_moon = (cels[i]->typeclass() == class_moon),
            is_sat = (cels[i]->typeclass() == class_satellite);

        double viewer_distance = cels[i]->tmprel.magnitude();
        double light_travel_time = viewer_distance / speed_of_light;

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
            if (is_star)
                ((Star*)cels[i])->update_location(simnow + step*j - light_travel_time);
            else if (is_moon)
                ((Moon*)cels[i])->update_location(simnow + step*j - light_travel_time);
            else if (is_sat)
                ((Satellite*)cels[i])->update_location(simnow + step*j - light_travel_time);
            else
                ((Planet*)cels[i])->update_location(simnow + step*j - light_travel_time);

            CelestialLocation orbrel = cels[i]->location - here;

            Point rel = rotate3D(Point(orbrel), center, viewer_plane.v, -viewer_plane.a);

            Cartesian2D cart;
            try
            {
                cart = Cartesian2D(rel, azimuth+azimuth_correction, altitude, zoom);
                cart.x = dispcx + cart.x * dispcx; cart.y = dispcy + cart.y * dispcx;

                double dx1 = cart.x, dy1 = cart.y, dx2 = lastcart.x, dy2 = lastcart.y;

                if (view_mode == vm_skymap)
                {
                    if (dx1 > dx2 + 1.9 * dispcx) dx2 += dispcx*2;
                    if (dx2 > dx1 + 1.9 * dispcx) dx1 += dispcx*2;
                    if (dy1 > dy2 + 1.9 * dispcy) dy2 += dispcy*2;
                    if (dy2 > dy1 + 1.9 * dispcy) dy1 += dispcy*2;
                }

                if (lastcart.x >= -200 && lastcart.y >= -200 && cart.x >= -200 && cart.y >= -200)
                    ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx1, dy1), ImVec2(dx2, dy2), imcol);
            }
            catch (...)
            {
                cart.x = cart.y = -1e9;
            }

            lastcart = cart;
        }
        cels[i]->location = was;
    }

    // Faraway objects
    for (pass=0; pass<=1; pass++) for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        cels[i]->drawnxmin = cels[i]->drawnxmax = cels[i]->drawnymin = cels[i]->drawnymax = -1e9;
        if (i == whereami) continue;

        if (!pass && fabs(bloomrad_cache[i]) > 3) continue;
        else if (pass && fabs(bloomrad_cache[i]) <= 3) continue;

        if (angular_radius[i]*zoom < sphere_rad_threshold)
        {
            if (cels[i]->drawnx < 0 || cels[i]->drawnx >= dispw) continue;
            if (cels[i]->drawny < 0 || cels[i]->drawny >= disph) continue;
        }

        // Counterintuitive that we would process *more* objects during dragging and not *less*,
        // but since discs become transparent wireframes during drag, it only makes sense that the
        // ground should become transparent as well.
        if (view_mode == vm_horizon && !dragging && cels[i]->viewrel.y < 0 && angular_radius[i] < sphere_rad_threshold)
        {
            continue;
        }

        xycoord = ImVec2(cels[i]->drawnx, cels[i]->drawny);
        appmag = vmag_cache[i] - sky_mag_shift;
        if (appmag > mag_limit_adjusted && i
            && (cbolbls_selected_idx != 6 || (((Star*)cels[i])->has_planets < planets_lblcut) )
            && (cbolbls_selected_idx != 7 || !(((Star*)cels[i])->has_hz_planets) )) continue;

        bloomrad = fabs(bloomrad_cache[i]);
        bloomrad = fmin(max_bloomrad, bloomrad);

        // if (cls != class_satellite && angular_radius[i]*zoom > sphere_rad_threshold)
        if (mycensq < light_year_sq
            && cels[i]->tmprel.squared_magnitude() < layer_cutoff)
        {
            n = to_draw_layered.size();
            if (!n)
            {
                to_draw_layered.push_back(cels[i]);
                discinstead[i] = true;
            }
            else
            {
                discinstead[i] = false;
                double trm = cels[i]->get_horizon_distance();
                for (j=0; j<n; j++)
                {
                    if (to_draw_layered[j]->get_horizon_distance() < trm)
                    {
                        to_draw_layered.insert(to_draw_layered.begin()+j, cels[i]);
                        discinstead[i] = true;
                        break;
                    }
                }
                if (!discinstead[i])
                {
                    to_draw_layered.push_back(cels[i]);
                }
                discinstead[i] = true;
            }
        }
        else draw_one_object(i);
        if (!cels[1]) return;
    }

    if (!cels[1]) return;

    // Near objects
    n = to_draw_layered.size();
    for (j=0; j<n; j++)
    {
        draw_one_object(to_draw_layered[j]->seqno);
        if (!cels[1]) return;
    }

}

ViewMode last_vmode = vm_skyatlas;
void draw_sunclock()
{
    if (whereami < 0) return;

    int i;
    if (last_vmode != view_mode) for (i=0; cels[i]; i++)
    {
        cels[i]->drawnx = cels[i]->drawny = -1e9;
    }

    CelestialObject *cel = cels[whereami];
    CelestialObject *lightcen = cel->get_light_center();
    bool self_luminous = (lightcen == cel);
    cel_obj_class cls = cel->typeclass();

    if (!cel->nlocales) cel->read_locales("locales.json");

    if (!cel->looked_for_maps)
    {
        cel->looked_for_maps = true;                // Prevent spawning infinite threads and crashing the system.
        std::thread ttex(load_textures, cel);
        ttex.detach();
    }

    Color c = Color::color_from_magnitude_indices(0, cel->BV_color);
    Color daylight = Color::color_from_magnitude_indices(0, cel->get_light_center()->BV_color);
    RGB3Byte prgb = Color::rgb_from_color(c, -1), rgb = prgb, nrgb(0,0,0);

    int x, y, dx, dy, step=3, size = dispcx/2, halfwid = size*2, wid = halfwid*2;
    sclk_scale = half_pi / size / zoom;
    double lat, lon, obl = 1.0 - cel->oblateness, elevation, line_of_sight;
    Map *map = cel->surf_map ? cel->surf_map : (cel->cloud_map ? cel->cloud_map : nullptr);
    Map *nmap = cel->night_map ? cel->night_map : nullptr;
    Point land;
    bool dwh = false;

    if (cls == class_moon)
        dwh = (((Moon*)cel)->depth > zero_isnt_really_zero
            && ((Moon*)cel)->width > zero_isnt_really_zero
            && ((Moon*)cel)->height > zero_isnt_really_zero);

    double equatorial_radius, theta, vtheta, cos_theta, cos_vtheta, is_day, is_night;
    if (dwh)
        equatorial_radius = pow(((Moon*)cel)->depth * ((Moon*)cel)->width, 0.5) * .5;
    else
        equatorial_radius = cel->get_equatorial_radius();


    for (y=dispcy; y>=-dispcy; y-=step)
    {
        dy = dispcy + y;
        lat = lat_from_y(y);
        if (fabs(lat) > half_pi) continue;

        for (x=-halfwid; x<halfwid; x+=step)
        {
            dx = dispcx + x;
            lon = lon_from_x(x);
            elevation = (map) ? (map->elevation_at(lat, lon)) : 0;
            land = Point::from_ra_dec(lon, lat, dwh ? 1 : (equatorial_radius + elevation), 0);

            if (dwh)
            {
                land.x *= ((Moon*)cel)->width  * .5;
                land.y *= ((Moon*)cel)->height * .5;
                land.z *= ((Moon*)cel)->depth  * .5;
                if (elevation) land.scale(land.magnitude()+elevation);          // TODO: This is a costly calculation - possible to streamline it?
            }
            else land.y *= obl;
            land = rotate3D(land, center, yaxis, -cel->timeofday());
            land = rotate3D(land, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);

            land += cel->location.local_position;
            if (self_luminous) is_day = 1;
            else
            {
                theta = fmod(find_3D_angle(land, lightcen->location.local_position, cel->location.local_position), _pi);
                if (fabs(theta) < half_pi)
                {
                    cos_theta = cos(theta);
                    is_day = fmin(1, pow(cos_theta, 0.333));
                    is_night = 0;
                }
                // TODO: Twilight
                else
                {
                    is_day = 0;
                    is_night = 1;
                }
            }
            // is_night = 1.0 - is_day;

            if (map) rgb = map->color_at(lat, lon);
            else rgb = prgb;

            if (nmap) nrgb = nmap->color_at(lat, lon);

            rgb.r *= is_day;
            rgb.g *= is_day;
            rgb.b *= is_day;

            if (is_night)
            {
                rgb.r += nrgb.r * is_night;
                rgb.g += nrgb.g * is_night;
                rgb.b += nrgb.b * is_night;
            }

            ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(dx, dy), ImVec2(dx+step, dy+step),
                rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, 255)));
        }
    }

    if (selected_locale)
    {
        dx = dispcx + fmod(selected_locale->lon * fiftyseventh - azimuth , _pi*2) / sclk_scale;
        dy = dispcy - fmod(selected_locale->lat * fiftyseventh - altitude, _pi*2) / sclk_scale;
        while (dx < 0) dx += dispcx*2;
        while (dx >= dispcx*2) dx -= dispcx*2;
        while (dy < 0) dy += dispcy*2;
        while (dy >= dispcy*2) dy -= dispcy*2;

        float circ_sz = 7, ln_spc = 10;

        ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx,0), ImVec2(dx,dy-ln_spc), rgba_apply_redlight(global_style.selected_color));
        ImGui::GetBackgroundDrawList()->AddLine(ImVec2(0,dy), ImVec2(dx-ln_spc,dy), rgba_apply_redlight(global_style.selected_color));
        ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx,dy+ln_spc), ImVec2(dx,dispcy*2), rgba_apply_redlight(global_style.selected_color));
        ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx+ln_spc,dy), ImVec2(dispcx*2,dy), rgba_apply_redlight(global_style.selected_color));
        ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(dx,dy), circ_sz, rgba_apply_redlight(global_style.selected_color), 0, 2);
    }

    Point satat;
    ImU32 satcol;
    if (show_sats && first_sat >= 0) for (i=first_sat; i<ncelobjs; i++)
    {
        if (cels[i] && cels[i]->typeclass() == class_satellite && cels[i]->orbit && cels[i]->orbit->center == cel)
        {
            satat = cels[i]->location.local_position - cel->location.local_position;
            satat = rotate3D(satat, center, cel->location.equatorial_plane.v, cel->location.equatorial_plane.a);
            satat = rotate3D(satat, center, yaxis, cel->timeofday());

            lon = fmod(find_angle(satat.z, -satat.x) - azimuth, _pi*2);
            if (lon >  _pi) lon -= _pi*2;
            if (lon < -_pi) lon += _pi*2;
            lat = fmod(find_angle(sqrt(satat.x*satat.x+satat.z*satat.z), satat.y) - altitude, _pi*2);
            if (lat < -half_pi) lat += _pi*2;
            if (lat >  half_pi) lat -= _pi*2;

            dx = dispcx + lon/sclk_scale;
            dy = dispcy - lat/sclk_scale;

            line_of_sight = cel->location.local_position.get_distance_to_line(
                cels[i]->location.local_position, cels[i]->get_light_center()->location.local_position);

            satcol = (i == selected)
                ? rgba_apply_redlight(global_style.selected_color)
                : ((line_of_sight < cel->volumetric_mean_radius)
                    ? rgba_apply_redlight(IM_COL32(128,  96,  64, 255))
                    : rgba_apply_redlight(IM_COL32(255, 255, 255, 255)));

            bloomrad = draw_satellite_icon(ImVec2(dx, dy), satcol);
            cels[i]->drawnx = dx;
            cels[i]->drawny = dy;

            if (show_labels)
            {
                const char *dispname = cels[i]->name;
                ImFont *font = global_font;
                std::string str;
                cel_obj_class cls = cels[i]->typeclass();
                double lfontsz = global_font_size;
                ImVec2 sz = ImGui::CalcTextSize(dispname);
                ImGui::GetBackgroundDrawList()->AddText(font, lfontsz, ImVec2(cels[i]->drawnx - sz.x/2, cels[i]->drawny+bloomrad+1),
                    rgba_apply_redlight((i == selected) ? global_style.selected_color : global_style.objlbl_color),
                    dispname);
            }

            if (show_orbits && cels[i]->orbit)
            {
                int dx1 = -1e9, dy1 = -1e9, dx2, dx2a, dy2;
                double sincewhen, hasta_la_pasta = simnow + 0.5*cels[i]->orbit->period, stepf = cels[i]->orbit->period / 29;
                bool satsunlit;

                for (sincewhen = simnow - 0.5*cels[i]->orbit->period; sincewhen <= hasta_la_pasta; sincewhen += stepf)
                {
                    ((Satellite*)cels[i])->update_location(sincewhen);

                    satat = cels[i]->location.local_position - cel->location.local_position;
                    satat = rotate3D(satat, center, cel->location.equatorial_plane.v, cel->location.equatorial_plane.a);
                    satat = rotate3D(satat, center, yaxis, cel->timeofday());

                    lon = fmod(find_angle(satat.z, -satat.x) - azimuth, _pi*2);
                    if (lon >  _pi) lon -= _pi*2;
                    if (lon < -_pi) lon += _pi*2;
                    lat = fmod(find_angle(sqrt(satat.x*satat.x+satat.z*satat.z), satat.y) - altitude, _pi*2);
                    if (lat < -half_pi) lat += _pi*2;
                    if (lat >  half_pi) lat -= _pi*2;

                    dx2 = dispcx + lon/sclk_scale;
                    dy2 = dispcy - lat/sclk_scale;

                    line_of_sight = cel->location.local_position.get_distance_to_line(
                        cels[i]->location.local_position, cels[i]->get_light_center()->location.local_position);

                    satsunlit = (line_of_sight < cel->volumetric_mean_radius);

                    if (dx1 >= -1000 && dy1 >= 0)
                    {
                        satcol = (i == selected)
                            ? rgba_apply_redlight(global_style.selected_color)
                            : (satsunlit
                                ? rgba_apply_redlight(IM_COL32(128,  96,  64, 128))
                                : rgba_apply_redlight(IM_COL32(255, 255, 255, 128)));

                        dx2a = dx2;
                        if (dx2a < dx1) dx2a += dispcx*2;

                        ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx1,dy1), ImVec2(dx2a,dy2), satcol);
                    }

                    dx1 = dx2;
                    dy1 = dy2;
                }
                ((Satellite*)cels[i])->update_location(simnow);
            }
        }
    }
}

bool draw_marker[16];
double hz_dx[16], hz_dy[16];
void find_horizon()
{
    hz_y = dispcy*29;
    if (view_mode == vm_horizon)
    {
        int j;
        CelestialObject *cel = cels[whereami];
        if (!cel->looked_for_maps)
        {
            cel->looked_for_maps = true;                // Prevent spawning infinite threads and crashing the system.
            std::thread ttex(load_textures, cel);
            ttex.detach();
        }

        double theta = 0, step = _pi/8;
        for (j = 0; j < 16; j++)
        {
            draw_marker[j] = false;
            Point pt = rotate3D(zaxis, center, yaxis, theta);
            Cartesian2D horizon = Cartesian2D(pt, azimuth, altitude, zoom);
            hz_dx[j] = horizon.x * dispcx + dispcx;
            hz_dy[j] = horizon.y * dispcx + dispcy;
            if (hz_dy[j] < 0) hz_dy[j] = 0;
            else draw_marker[j] = (hz_dx[j] >= 0 && hz_dx[j] < dispcx*2);
            if (draw_marker[j] && hz_y > dispcy*2) hz_y = hz_dy[j];
            theta += step;
        }
    }
}

void draw_horizon()
{
    // Horizon
    // TODO: Render according to bump map and generate a fictitious skyline.
    if (view_mode == vm_horizon)
    {
        int j;
        CelestialObject *cel = cels[whereami];

        if (!cel->looked_for_maps)
        {
            cel->looked_for_maps = true;                // Prevent spawning infinite threads and crashing the system.
            std::thread ttex(load_textures, cel);
            ttex.detach();
        }

        double is_day = fmin(1, luminous_flux*2.5e-11 + starlight);

        Map *map = cel->surf_map;
        RGB3Byte rgb = map ? map->color_at(viewer_lat, viewer_lon) : RGB3Byte(0, 8, 24);
        rgb.r *= is_day;
        rgb.g *= is_day;
        rgb.b *= is_day;

        double hz_draw_y = (hz_y > dispcy*28 && altitude < 0) ? 0 : hz_y;
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, hz_draw_y), ImVec2(dispcx*2, dispcy*2),
            rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, dragging ? (192-128*is_day) : 255)));

        if (hz_y < dispcy*2)
        {
            double hzbrt = _lum_r_comp*rgb.r + _lum_g_comp*rgb.g * _lum_b_comp*rgb.b;
            ImU32 mkrcol = rgba_apply_redlight((hzbrt >= 176) ? IM_COL32(0,0,0,255) : global_style.conslbl_color);
            if (show_grid) for (j = 0; j < 16; j++) if (draw_marker[j])
            {
                ImGui::GetBackgroundDrawList()->AddText(ImVec2(hz_dx[j], hz_dy[j]), mkrcol, compass[j]);
                if (hzbrt >= 176) ImGui::GetBackgroundDrawList()->AddText(ImVec2(hz_dx[j]-1, hz_dy[j]), mkrcol, compass[j]);
            }
        }
    }
}

void draw_sky_gradient()
{
    sky_grad.clear();
    if (!dragging && (cels[whereami]->typeclass() == class_planet || cels[whereami]->typeclass() == class_moon))
    {
        Planet *p = (Planet*)cels[whereami];
        if (p->surface_pressure)
        {
            double Rayleigh = 1.0 - p->atmospheric_particulates;
            Color pcol = Color::color_from_magnitude_indices(0, p->BV_color);
            pcol.normalize(1);

            float city_lights = 0;
            if (cels[whereami]->night_map)
            {
                RGB3Byte rgb = cels[whereami]->night_map->color_at(viewer_lat, viewer_lon);
                if (rgb.r > 0.7*rgb.b) city_lights = rgb.r;
            }

            int x_extent = dispcx*2-1;
            double skylight = fmin(1, pow(luminous_flux*2.5e-11, 1.0/5.5) + starlight + 0.001 * city_lights);
            sky_mag_shift = skylight * -10;
            double  r = fmin(1, (Rayleigh * 0.37 + p->atmospheric_particulates * pcol.red  ) * skylight),
                    g = fmin(1, (Rayleigh * 0.58 + p->atmospheric_particulates * pcol.green) * skylight),
                    b = fmin(1, (Rayleigh * 0.81 + p->atmospheric_particulates * pcol.blue ) * skylight),
                    a = fmin(1, pow(p->surface_pressure, 0.1) * skylight);
            unsigned char r255, g255, b255;
            for (int y = fmin(hz_y, dispcy*2-1); y>=0; y--)
            {
                r255 = r*255; g255 = g*255; b255 = b*255;
                ImGui::GetBackgroundDrawList()->AddLine(ImVec2(0, y), ImVec2(x_extent, y),
                    rgba_apply_redlight(IM_COL32( (int)(r255), (int)(g255), (int)(b255), (int)(a*255) ) ));

                r *= 0.999;
                g *= 0.9995;
                b *= 0.9999;
                // a *= 0.99999;

                sky_grad[y] = RGB3Byte(r255, g255, b255);
            }
        }
    }
}

void draw_cons_lines()
{
    if (!cels[1]) return;
    int i, l, n;
    double dispw = dispcx*2, disph = dispcy*2;
    bool initcons = false;

    // Hide lines if more than 10 l.y. from Sun.
    draw_actual_conslines = here.distance_to(cels[0]->location) < light_year*10;

    n = consname.size();
    if (!consdir.size())
    {
        initcons = true;
        for (l=0; l<n; l++)
        {
            consdir.push_back(Point(0,0,0));
            lnpercons[l] = 0;
        }
    }
    n = show_xonsm ? (nconsln+11) : nconsln;
    for (i=0; i<n; i++)
    {
        if (consaidx[i] < 0 || consbidx[i] < 0) continue;
        if (cels[consaidx[i]] == mycenobj) continue;
        if (cels[consbidx[i]] == mycenobj) continue;

        int dx1, dx2, dy1, dy2;
        if (i >= nconsln) considx[i] = consname.size()-1;
        l = considx[i];

        assert (l < (int)consdir.size());
        if (initcons) consdir[l] += Point(cels[consaidx[i]]->location) + Point(cels[consbidx[i]]->location);
        lnpercons[l]++;

        dx1 = cels[consaidx[i]]->drawnx;
        dy1 = cels[consaidx[i]]->drawny;
        if (dx1 < -1e3) continue;
        if (dy1 < -1e3) continue;

        dx2 = cels[consbidx[i]]->drawnx;
        dy2 = cels[consbidx[i]]->drawny;
        if (dx2 < -1e3) continue;
        if (dy2 < -1e3) continue;

        if (view_mode == vm_skymap)
        {
            if (dx1 > dx2 + 1.9 * dispcx) dx2 += dispcx*2;
            if (dx2 > dx1 + 1.9 * dispcx) dx1 += dispcx*2;
            if (dy1 > dy2 + 1.9 * dispcy) dy2 += dispcy*2;
            if (dy2 > dy1 + 1.9 * dispcy) dy1 += dispcy*2;
        }

        if (draw_actual_conslines || i >= nconsln)
            ImGui::GetBackgroundDrawList()->AddLine(
                ImVec2(dx1, dy1), ImVec2(dx2, dy2),
                rgba_apply_redlight((i<nconsln) ? global_style.consline_color : IM_COL32(255, 64, 0, 128)), 1);
    }

    // Constellation labels
    n=l;
    if (show_labels || (show_consln && !draw_actual_conslines)) for (l=0; l<=n; l++)
    {
        if (!lnpercons[l]) continue;
        // if (initcons) consdir[l].scale(1e303);
        Point lconsdir = to_viewer_plane(consdir[l]);
        Cartesian2D cart(lconsdir, azimuth+azimuth_correction, altitude, zoom);
        float dx = (int)(dispcx + cart.x * dispcx), dy = (int)(dispcy + cart.y * dispcx);

        if (dx < 0 || dy < 0) continue;
        ImVec2 sz = ImGui::CalcTextSize(consname[l].c_str());
        dx -= sz.x/2;
        dy -= sz.y/2;
        if (dx >= 0 && dx < dispw && dy >= 0 && dy < disph)
        {
            ImGui::GetBackgroundDrawList()->AddText(ImVec2(dx, dy),
                rgba_apply_redlight((l<nconsln) ? global_style.conslbl_color : IM_COL32(255, 64, 0, 128)),
                consname[l].c_str());
        }
    }

    if (show_axes)
    {
        Point axisdir[6] = {xaxis, yaxis, zaxis, center-xaxis, center-yaxis, center-zaxis};
        for (i=0; i<6; i++)
        {
            Point laxdir = to_viewer_plane(axisdir[i]);
            Cartesian2D cart(laxdir, azimuth+azimuth_correction, altitude, zoom);
            float dx = (int)(dispcx + cart.x * dispcx), dy = (int)(dispcy + cart.y * dispcx);

            if (dx < 0 || dy < 0) continue;
            ImVec2 sz(64,64);
            dx -= sz.x/2;
            dy -= sz.y/2;
            if (dx >= 0 && dx < dispw && dy >= 0 && dy < disph)
            {
                std::string axname = (i<3) ? "+" : "-";
                ImU32 axcolor;

                switch (i % 3)
                {
                    case 0: axname += std::string("X"); axcolor = IM_COL32(255, 0, 0, 255); break;
                    case 1: axname += std::string("Y"); axcolor = IM_COL32(0, 255, 0, 255); break;
                    case 2: axname += std::string("Z"); axcolor = IM_COL32(0, 0, 255, 255); break;
                }

                ImGui::GetBackgroundDrawList()->AddText(global_font, 64,
                    ImVec2(dx, dy),
                    rgba_apply_redlight(axcolor),
                    axname.c_str());
            }
        }
    }
}

void draw_mouse_cursor(ImGuiIO& io)
{
    if ((frames_without_mousemove > 203) || !cels[1]) return;

    cursor_size = (int)io.DisplaySize.x/99;
    circle_size = cursor_size / 2.5;

    ImU32 cc[3];
    cc[0] = rgba_apply_redlight(global_style.cursor_color1);
    cc[1] = rgba_apply_redlight(global_style.cursor_color2);
    cc[2] = rgba_apply_redlight(global_style.cursor_color3);

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
}

std::vector<Cloud> skyclouds;
void draw_cloudy_sky()
{
    if (view_mode != vm_horizon) return;

    unsigned int seed = 65536 * (viewer_lat + _pi);
    srand(seed);
    seed = (rand() % 65536) + (65536 * fabs(viewer_lon));

    CelestialObject *cel = cels[whereami];
    if (!cel->cloud_map) return;

    RGB3Byte rgb = cel->cloud_map->color_at(viewer_lat, viewer_lon);
    double cloudiness = sqrt(fmin(1,rgb.luminance()/192));
    double is_day = fmin(1, luminous_flux*2.5e-11 + starlight);

    rgb.r *= is_day;
    rgb.g *= is_day;
    rgb.b *= is_day;

    ImU32 imc = IM_COL32(rgb.r, rgb.g, rgb.b, (dragging ? 128 : 255)*cloudiness);
    if (hz_y > 0 && (hz_y < dispcy*28 || altitude > 1)) ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(dispcx*2, hz_y), imc);

    #if 0
    if (!skyclouds.size())
    {
        // TODO:

        Cloud c;
        c.color = rgb;
        c.core_dist = cel->volumetric_mean_radius + 1500;
        c.height = 200;
        c.width = 500;
        c.latitude = viewer_lat;
        c.longitude = viewer_lon;

        skyclouds.push_back(c);
    }

    int i, n = skyclouds.size();
    for (i=0; i<n; i++) skyclouds[i].draw(cel->volumetric_mean_radius);
    #endif
}
