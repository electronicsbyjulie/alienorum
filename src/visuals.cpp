
#include "globals.h"
#include "visuals.h"
#include "loaders.h"

using namespace alienorum;

double jay, appmag, bloomrad, flare, theta, lmasslim;
ImVec2 xycoord;

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

                if (prev_valid)
                {
                    ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx1, dy1), ImVec2(dx2, dy2), gc, 1);
                }
            }

            prev = zdes;
            prev_valid = true;
        }
    }

    for (j=jstart; j <= 80; j+=10)
    {
        prev_valid = false;
        for (i=0; i<=24; i++)
        {
            Point umenjanetdeneg = Point::from_ra_dec(fiftyseventh * i * 15, fiftyseventh * j, 5, node);
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

                if (prev_valid)
                ImGui::GetBackgroundDrawList()->AddLine(ImVec2(dx1, dy1), ImVec2(dx2, dy2), ec, 1);
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
    double d = cel->tmprel.magnitude(), horizon_angle, elevation = 0;
    cel_obj_class cls = cel->typeclass();

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
                    double rads_sec = cel->sidereal_rotational_period ? ((_pi * 2) / cel->sidereal_rotational_period) : 0;
                    double seconds_since_epoch = (simnow - J2000_TIME_T) + ((J2000 - cel->epoch)*oneday);
                    double timeofday = fmod(rads_sec * seconds_since_epoch - cel->lon_J2000_offset, _pi*2);
                    here.system_center = cel->location.system_center;
                    here.equatorial_plane = cel->location.equatorial_plane;
                    viewer_lon = cel->RA_as_radians(here, /*cel->equinox +*/ timeofday) - _pi;
                    viewer_lat = -cel->Decl_as_radians(here);
                    save_viewer_latlon = false;
                    whereami = cel->seqno;
                    velocity = Point(0,0,0);
                    view_mode = vm_horizon;
                    altitude = 0;
                    trackidx = -1;
                }
            }
        }
    }
    else took_off_from = -1;

    cel->drawnxmin = cel->drawnxmax = cel->drawnx;
    cel->drawnymin = cel->drawnymax = cel->drawny;
    if (sphresolution < 0.001/sphere_quality) sphresolution = 0.001/sphere_quality;
    bool wireframe = dragging || !cel->onscreen || d < cel->volumetric_mean_radius;
    if (whereami<0 || cels[whereami]->type != artificial) cel->onscreen = false;
    int i, j, l, m, lastm, n, result=0;
    Cartesian2D prev, zdes;
    std::vector<ImVec2> todraw;
    std::vector<bool> tdvalid;
    ImU32 gc = rgba_apply_redlight(IM_COL32(176, 170, 164, 255));
    ImU32 gm = rgba_apply_redlight(IM_COL32(  0, 255,   0, 255));
    Color daylight = Color::color_from_magnitude_indices(0, cel->get_light_center()->BV_color);
    double f = fmax(fmax(daylight.red, daylight.green), daylight.blue);
    daylight.red /= f;
    daylight.green /= f;
    daylight.blue /= f;

    if (wireframe)
    {
        Color wcol = Color::color_from_magnitude_indices(0, cel->BV_color);
        RGB wrgb = Color::rgb_from_color(wcol, -1);
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
        equatorial_radius = pow(((Moon*)cel)->depth * ((Moon*)cel)->width, 0.5) * 500;
    else
        equatorial_radius = cel->volumetric_mean_radius * pow(1.0 - cel->oblateness, 0.333);

    double lat, lon, z_cutoff = d + equatorial_radius * 0.2, obl = 1.0 - cel->oblateness;

    double rads_sec = cel->sidereal_rotational_period ? ((_pi * 2) / cel->sidereal_rotational_period) : 0;
    double seconds_since_epoch = (simnow - J2000_TIME_T) + ((J2000 - cel->epoch)*oneday);
    double timeofday = fmod(rads_sec * seconds_since_epoch - cel->lon_J2000_offset, _pi*2);
    if (cel->orbit && fabs(cel->orbit->period - cel->sidereal_rotational_period) < 0.01 * cel->orbit->period)
    {
        timeofday += cel->orbit->ascending_node;
        timeofday += cel->orbit->arg_periapsis;
        timeofday += cel->orbit->mean_anomaly;
        timeofday += half_pi;
    }

    if (!wireframe && !cel->looked_for_maps)
    {
        cel->looked_for_maps = true;                // Prevent spawning infinite threads and crashing the system.
        std::thread ttex(load_textures, cel);
        ttex.detach();
    }

    horizon_angle = acos(equatorial_radius / fmax(d, 1e-29));

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
                cursor.x *= ((Moon*)cel)->width * 500;
                cursor.y *= ((Moon*)cel)->height * 500;
                cursor.z *= ((Moon*)cel)->depth * 500;
            }
            else cursor.y *= obl;
            cursor = rotate3D(cursor, center, yaxis, -timeofday);

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
    double step = wireframe ? (fiftyseventh*15) : fmax(fmin(_pi*sphresolution/arad*fiftyseventh, fiftyseventh*2), fiftyseventh*0.2),
        stepcoslat, invlaststepcoslat = 1.0 / step;
    int perline=0, dx1, dy1, dx2, dy2;
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
            elevation = map ? map->elevation_at(lat, lon) : 0;
            land = Point::from_ra_dec(lon+_pi, lat, dwh ? 1 : (equatorial_radius + elevation), 0);

            if (dwh)
            {
                land.x *= ((Moon*)cel)->width * 500;
                land.y *= ((Moon*)cel)->height * 500;
                land.z *= ((Moon*)cel)->depth * 500;
                if (elevation) land.scale(land.magnitude()+elevation);          // TODO: This is a costly calculation - possible to streamline it?
            }
            else land.y *= obl;
            land = rotate3D(land, center, yaxis, -timeofday);

            land = rotate3D(land, center, cel->location.equatorial_plane.v, -cel->location.equatorial_plane.a);
            cursor = land + cel->tmprel;
            cursor = to_viewer_plane(cursor);
            if (cursor.magnitude() > z_cutoff)
            {
                todraw.push_back(ImVec2(0,0));
                tdvalid.push_back(false);
                l++;
                prev_valid = false;
                continue;
            }

            zdes = Cartesian2D(cursor, azimuth+azimuth_correction, altitude, zoom);
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
                vtheta = fabs(fmod(find_3D_angle(land, here.local_position, cel->location.local_position), _pi*2));
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

                    // TODO: Also store 3D coordinates of each vertex.
                    todraw.push_back(v);
                    tdvalid.push_back(true);

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
                                // TODO: Shade based on the normal of the 3D coordinates of the polygon vertices instead of angle to sun and cel center.
                                theta = fmod(find_3D_angle(land, lightcen->location.local_position, cel->location.local_position), _pi);
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
                            if (map && is_day) rgb = map->color_at(lat, lon-_pi);

                            rgb.r *= daylight.red;
                            rgb.g *= daylight.green;
                            rgb.b *= daylight.blue;

                            if (nmap)
                            {
                                is_night = 1.0 - is_day;
                                if (is_night) nrgb = nmap->color_at(lat, lon-_pi);
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

bool draw_one_object(int i)
{
    int j;
    cel_obj_class cls = cels[i]->typeclass();
    xycoord = ImVec2(cels[i]->drawnx, cels[i]->drawny);
    bloomrad = fabs(bloomrad_cache[i]);
    flare = (bloomrad>max_bloomrad) ? fmin(225, fmax(0, 1.0+sqrt(bloomrad-0.5*max_bloomrad)*8)) : 0;
    bloomrad = fmin(max_bloomrad, bloomrad);
    if (cls == class_satellite)
    {
        if (cels[i]->orbit && (cels[i]->tmprel.magnitude() > cels[i]->orbit->semimajor_axis*zoom*6))
        {
            cels[i]->drawnx = cels[i]->drawny = -1e9;
            return false;
        }

        double line_of_sight = cels[i]->orbit->center->location.local_position.get_distance_to_line(
            cels[i]->location.local_position, cels[i]->get_light_center()->location.local_position);
        ImU32 satcol = (line_of_sight < cels[i]->orbit->center->volumetric_mean_radius)
            ? rgba_apply_redlight(IM_COL32(128,  96,  64, 255))
            : rgba_apply_redlight(IM_COL32(255, 255, 255, 255));
        if (show_labels || lbl_localsys || show_consln || show_grid)
        {
            // Satellite icons.
            ImVec2 antenna_top              = ImVec2(xycoord.x,                                             xycoord.y - antenna_height  );
            ImVec2 panel_left_stem          = ImVec2(xycoord.x - antenna_height,                            xycoord.y                   );
            ImVec2 panel_right_stem         = ImVec2(xycoord.x + antenna_height,                            xycoord.y                    );
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

            bloomrad_cache[i] = bloomrad = antenna_height + panel_tilt + panel_width;
        }
        else
        {
            ImGui::GetBackgroundDrawList()->AddCircleFilled(xycoord, 1, satcol);
            bloomrad_cache[i] = bloomrad = 1;
        }
    }
    else if (angular_radius[i]*zoom > fiftyseventh)
    {
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
        if (flare)
        {
            double divisor = 255.0 / fmax(fmax(col.blue, col.red), col.green);
            RGB rgb;
            rgb.r = (int)(col.red * divisor);
            rgb.g = (int)(col.green* divisor);
            rgb.b = (int)(col.blue * divisor);

            #define jmax 3
            for (j=jmax; j>0; j--)
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
    if (selected == i && cels[1])
    {
        ImGui::GetBackgroundDrawList()->AddCircle(xycoord, bloomrad+2, rgba_apply_redlight(global_style.selected_color), 0, 2);
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
        || ((cels[i]->cenobj == mycenobj) && lbl_localsys
            && ((cels[i]->mass >= lmasslim)
                || (vmag_cache[i] < 2.5)
                || (cels[i]->tmprel.magnitude() < AU)
                )
            )
        || i == selected)
    {
        ImVec2 sz = ImGui::CalcTextSize(cels[i]->name);
        ImGui::GetBackgroundDrawList()->AddText(ImVec2(cels[i]->drawnx - sz.x/2, cels[i]->drawny+bloomrad+1),
            rgba_apply_redlight(global_style.objlbl_color),
            cels[i]->name);
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

    double mycensq = mycenobj->tmprel.squared_magnitude();
    double layer_cutoff = mycensq * 1.1 * zoom * zoom;

    Point viewer_pole = to_viewer_plane(yaxis);
    Rotation viewer_plane = align_points_3d(viewer_pole, yaxis, center);

    // Orbits
    if (show_orbits) for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        if (!cels[i]->orbit) continue;
        if (cels[i]->cenobj != mycenobj && (whereami<0 || cels[i]->orbit->center != cels[whereami])) continue;
        if (cels[i]->orbit->center == mycenobj && cels[i]->mass < lmasslim) continue;

        Color col = Color::color_from_magnitude_indices(5, cels[i]->BV_color);
        RGB rgb = Color::rgb_from_color(col, 1);
        ImU32 imcol = (i==selected) ? rgba_apply_redlight(global_style.selected_orbit_color) : rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, 64));
        step = cels[i]->orbit->period / orbseg;
        CelestialLocation was = cels[i]->location;
        bool is_moon = (cels[i]->typeclass() == class_moon), is_sat = (cels[i]->typeclass() == class_satellite);

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
            else if (is_sat)
                ((Satellite*)cels[i])->update_location(simnow + step*j);
            else
                ((Planet*)cels[i])->update_location(simnow + step*j);

            CelestialLocation orbrel = cels[i]->location - here;

            Point rel = rotate3D(Point(orbrel), center, viewer_plane.v, -viewer_plane.a);

            Cartesian2D cart;
            try
            {
                cart = Cartesian2D(rel, azimuth+azimuth_correction, altitude, zoom);
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

    // Faraway objects
    for (pass=0; pass<=1; pass++) for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        cels[i]->drawnxmin = cels[i]->drawnxmax = cels[i]->drawnymin = cels[i]->drawnymax = -1e9;
        if (i == whereami) continue;

        if (!pass && fabs(bloomrad_cache[i]) > 3) continue;
        else if (pass && fabs(bloomrad_cache[i]) <= 3) continue;

        if (angular_radius[i]*zoom < fiftyseventh)
        {
            if (cels[i]->drawnx < 0 || cels[i]->drawnx >= dispw) continue;
            if (cels[i]->drawny < 0 || cels[i]->drawny >= disph) continue;
        }

        // Counterintuitive that we would process *more* objects during dragging and not *less*,
        // but since discs become transparent wireframes during drag, it only makes sense that the
        // ground should become transparent as well.
        if (view_mode == vm_horizon && !dragging && cels[i]->viewrel.y < 0 && angular_radius[i] < fiftyseventh)
        {
            continue;
        }

        cel_obj_class cls = cels[i]->typeclass();

        if (cls == class_star
            && i!=selected && i!=trackidx && i!=whereami && cels[i]->cenobj!=mycenobj
            && !((Star*)cels[i])->tmp_vis_flag)
            continue;

        xycoord = ImVec2(cels[i]->drawnx, cels[i]->drawny);
        appmag = vmag_cache[i] - sky_mag_shift;
        if (appmag > 6.5) continue;

        bloomrad = fabs(bloomrad_cache[i]);
        flare = (bloomrad>max_bloomrad) ? fmin(225, fmax(0, 1.0+sqrt(bloomrad-0.5*max_bloomrad)*8)) : 0;
        bloomrad = fmin(max_bloomrad, bloomrad);

        // if (cls != class_satellite && angular_radius[i]*zoom > fiftyseventh)
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
                double trm = cels[i]->tmprel.magnitude();
                for (j=0; j<n; j++)
                {
                    if (to_draw_layered[j]->tmprel.magnitude() < trm)
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

    // Labels
    if (!cels[1]) return;
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
            || ((cels[i]->cenobj == mycenobj) && lbl_localsys
                && ((cels[i]->mass >= lmasslim)
                 || (vmag_cache[i] < 2.5)
                 || (cels[i]->tmprel.magnitude() < AU)
                   )
               )
            || i == selected)
        {
            ImVec2 sz = ImGui::CalcTextSize(cels[i]->name);
            ImGui::GetBackgroundDrawList()->AddText(ImVec2(cels[i]->drawnx - sz.x/2, cels[i]->drawny+bloomrad+1),
                rgba_apply_redlight(global_style.objlbl_color),
                cels[i]->name);
        }
    }

    // Near objects
    n = to_draw_layered.size();
    for (j=0; j<n; j++)
    {
        draw_one_object(to_draw_layered[j]->seqno);
        if (!cels[1]) return;
    }

    // Horizon
    // TODO: Render according to bump map and generate a fictitious skyline.
    if (view_mode == vm_horizon)
    {
        CelestialObject *cel = cels[whereami];
        if (!cel->looked_for_maps)
        {
            cel->looked_for_maps = true;                // Prevent spawning infinite threads and crashing the system.
            std::thread ttex(load_textures, cel);
            ttex.detach();
        }

        double theta = 0, step = _pi/8, dx[16], dy[16], dy1 = dispcy*29;
        bool draw_marker[16];
        for (j = 0; j < 16; j++)
        {
            draw_marker[j] = false;
            Point pt = rotate3D(zaxis, center, yaxis, theta);
            Cartesian2D horizon = Cartesian2D(pt, azimuth, altitude, zoom);
            dx[j] = horizon.x * dispcx + dispcx;
            dy[j] = horizon.y * dispcx + dispcy;
            if (dy[j] < 0) dy[j] = 0;
            else draw_marker[j] = (dx[j] >= 0 && dx[j] < dispcx*2);
            if (draw_marker[j] && dy1 > dispcy*2) dy1 = dy[j];
            theta += step;
        }

        double is_day = fmin(1, luminous_flux/4e+10 + starlight);
        if (dy1 < dispcy*2)
        {
            Map *map = cel->surf_map;
            RGB rgb = map ? map->color_at(viewer_lat, viewer_lon) : RGB(0, 8, 24);
            rgb.r *= is_day;
            rgb.g *= is_day;
            rgb.b *= is_day;
            ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, dy1), ImVec2(dispcx*2, dispcy*2),
                rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, dragging ? (192-128*is_day) : 255)));

            double hzbrt = 0.29*rgb.r + 0.56*rgb.g * 0.15*rgb.b;
            ImU32 mkrcol = rgba_apply_redlight((hzbrt >= 176) ? IM_COL32(0,0,0,255) : global_style.conslbl_color);
            for (j = 0; j < 16; j++) if (draw_marker[j])
            {
                ImGui::GetBackgroundDrawList()->AddText(ImVec2(dx[j], dy[j]), mkrcol, compass[j]);
                if (hzbrt >= 176) ImGui::GetBackgroundDrawList()->AddText(ImVec2(dx[j]-1, dy[j]), mkrcol, compass[j]);
            }
        }
    }
}

void draw_sky_gradient()
{
    if (!dragging && (cels[whereami]->typeclass() == class_planet || cels[whereami]->typeclass() == class_moon))
    {
        Planet *p = (Planet*)cels[whereami];
        if (p->surface_pressure)
        {
            int x_extent = dispcx*2-1;
            double skylight = fmin(1, pow(luminous_flux/4e+10, 1.0/5.5) + starlight);
            sky_mag_shift = skylight * -10;
            double r = fmin(1, 0.37 * skylight),
                    g = fmin(1, 0.58 * skylight),
                    b = fmin(1, 0.81 * skylight),
                    a = fmin(1, pow(p->surface_pressure, 0.1) * skylight);
            for (int y=dispcy*2-1; y>=0; y--)
            {
                ImGui::GetBackgroundDrawList()->AddLine(ImVec2(0, y), ImVec2(x_extent, y),
                    rgba_apply_redlight(IM_COL32( (int)(r*255), (int)(g*255), (int)(b*255), (int)(a*255) ) ));

                r *= 0.998;
                g *= 0.9992;
                b *= 0.9998;
                a *= 0.99999;
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

        if (draw_actual_conslines)
            ImGui::GetBackgroundDrawList()->AddLine(
                ImVec2(dx1, dy1), ImVec2(dx2, dy2),
                rgba_apply_redlight((i<nconsln) ? global_style.consline_color : IM_COL32(255, 64, 0, 128)), 1);
    }

    // Constellation labels
    n=l;
    if (show_labels) for (l=0; l<n; l++)
    {
        if (!lnpercons[l]) continue;
        if (initcons) consdir[l].scale(1e303);

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

