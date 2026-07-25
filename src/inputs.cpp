
#include "globals.h"
#include "misc.h"
#include "loaders.h"
#include "housekeeping.h"
#include "inputs.h"

using namespace alienorum;

void center_selected()
{
    if (selected >= 0)
    {
        azimuth = cels[selected]->RA_as_radians(here,
            (whereami >= 0 && view_mode == vm_sunclock) ? cels[whereami]->timeofday() : 0)
            * ((view_mode == vm_sunclock) ? 1 : -1);
        altitude = cels[selected]->Decl_as_radians(here);
    }
    enforce_y_pan_limit();
    viewchanged = true;
}

void center_tracked()
{
    if (trackidx >= 0)
    {
        azimuth = cels[trackidx]->RA_as_radians(here,
            (whereami >= 0 && view_mode == vm_sunclock) ? cels[whereami]->timeofday() : 0)
            * ((view_mode == vm_sunclock) ? 1 : -1);
        altitude = cels[trackidx]->Decl_as_radians(here);
    }
    enforce_y_pan_limit();
    viewchanged = true;
}

void identify_object_under_cursor(ImGuiIO& io)
{
    int i;

    is_an_obj_under_cursor = -1;
    is_a_locale_under_cursor = nullptr;
    obj_magn_under_cursor = 1e9;
    int threshold = circle_size*1.3;
    bool selected_this_turn = false;

    if (trackidx >= 0)
    {
        is_an_obj_under_cursor = trackidx;
    }
    else for (i=0; cels[i] && i<MAX_CELOBJS; i++)
    {
        if (i == whereami) continue;

        if ((abs(io.MousePos.x - cels[i]->drawnx) < threshold
            && abs(io.MousePos.y - cels[i]->drawny) < threshold)
            ||
            (   cels[i]->drawnxmin < cels[i]->drawnxmax
                && io.MousePos.x > cels[i]->drawnxmin && io.MousePos.x < cels[i]->drawnxmax
                && io.MousePos.y > cels[i]->drawnymin && io.MousePos.y < cels[i]->drawnymax)
            )
        {
            // Prioritize by brightness.
            double lmag = vmag_cache[i];
            if (lmag < obj_magn_under_cursor)
            {
                is_an_obj_under_cursor = i;
                obj_magn_under_cursor = lmag;

                if (i == selected) break;
                if (is_click && !dragged)
                {
                    selected = i;
                    selected_this_turn = true;
                    selected_locale = nullptr;
                }
            }
        }
    }

    if (view_mode == vm_sunclock && is_an_obj_under_cursor < 0)
    {
        double mlat = lat_from_y(io.MousePos.y - dispcy) * fiftyseven, mlon = lon_from_x(io.MousePos.x - dispcx) * fiftyseven, dlat, dlon, r, br = 1e29;

        if (mlon >  180) mlon -= 360;
        if (mlon < -180) mlon += 360;

        CelestialObject *cel = cels[whereami];
        if (cel->nlocales) for (i=0; i<cel->nlocales; i++)
        {
            dlat = fabs(cel->locales[i].lat - mlat);
            dlon = fabs(cel->locales[i].lon - mlon);
            if (dlon < 3 && dlat < 3)
            {
                r = sqrt(dlat*dlat + dlon*dlon);
                if (r < br)
                {
                    is_a_locale_under_cursor = &cel->locales[i];
                    br = r;
                }
            }
        }

        if (is_click && !dragged)
        {
            selected_locale = is_a_locale_under_cursor;
            if (!selected_this_turn) selected = -1;
        }
    }
}

void pan_with_crosshairs(ImGuiIO& io)
{
    double amount = 1;
    if (view_mode == vm_skymap) amount = 3;
    else if (view_mode == vm_sunclock) amount = 5;

    if (ImGui::IsMouseDown(2))
    {
        azimuth -= 0.01 * amount * fiftyseventh * io.MouseDelta.x / zoom;
        altitude += 0.01 * amount * fiftyseventh * io.MouseDelta.y / zoom;
        enforce_y_pan_limit();
        spin = 0;
        viewchanged = true;

        ImVec2 topcen(dispcx, 0), botcen(dispcx, (int)io.DisplaySize.y-1),
            leftcen(0, dispcy), rightcen((int)io.DisplaySize.x-1, dispcy);
        ImGui::GetBackgroundDrawList()->AddLine(topcen, botcen, rgba_apply_redlight(IM_COL32(0, 0, 255, 128)), 1);
        ImGui::GetBackgroundDrawList()->AddLine(leftcen, rightcen, rgba_apply_redlight(IM_COL32(0, 0, 255, 128)), 1);
    }
    else if (ImGui::IsMouseDown(1))
    {
        azimuth -= 0.03 * amount * fiftyseventh * io.MouseDelta.x / zoom;
        altitude += 0.03 * amount * fiftyseventh * io.MouseDelta.y / zoom;
        enforce_y_pan_limit();
        spin = 0;
        viewchanged = true;

        ImVec2 topcen(dispcx, 0), botcen(dispcx, (int)io.DisplaySize.y-1),
            leftcen(0, dispcy), rightcen((int)io.DisplaySize.x-1, dispcy);
        ImGui::GetBackgroundDrawList()->AddLine(topcen, botcen, rgba_apply_redlight(IM_COL32(0, 255, 0, 64)), 1);
        ImGui::GetBackgroundDrawList()->AddLine(leftcen, rightcen, rgba_apply_redlight(IM_COL32(0, 255, 0, 64)), 1);
    }
    else if (ImGui::IsMouseDown(0))
    {
        azimuth -= 0.1 * amount * fiftyseventh * io.MouseDelta.x / zoom;
        altitude += 0.1 * amount * fiftyseventh * io.MouseDelta.y / zoom;
        enforce_y_pan_limit();
        spin = 0;
        viewchanged = true;

        ImVec2 topcen(dispcx, 0), botcen(dispcx, (int)io.DisplaySize.y-1),
            leftcen(0, dispcy), rightcen((int)io.DisplaySize.x-1, dispcy);
        ImGui::GetBackgroundDrawList()->AddLine(topcen, botcen, rgba_apply_redlight(IM_COL32(255, 96, 0, 96)), 1);
        ImGui::GetBackgroundDrawList()->AddLine(leftcen, rightcen, rgba_apply_redlight(IM_COL32(255, 96, 0, 96)), 1);
    }

    if (io.MouseDelta.x < 0 && io.MousePos.x < -3*io.MouseDelta.x)
    {
        io.MousePos = ImVec2(dispcx*2, io.MousePos.y);
        io.WantSetMousePos = true;
    }
    else if (io.MouseDelta.x > 0 && io.MousePos.x > dispcx*2 - 3*io.MouseDelta.x)
    {
        io.MousePos = ImVec2(0, io.MousePos.y);
        io.WantSetMousePos = true;
    }

    if (io.MouseDelta.y < 0 && io.MousePos.y < -3*io.MouseDelta.y)
    {
        io.MousePos = ImVec2(io.MousePos.x, dispcy*2);
        io.WantSetMousePos = true;
    }
    else if (io.MouseDelta.y > 0 && io.MousePos.y > dispcy*2 - 3*io.MouseDelta.y)
    {
        io.MousePos = ImVec2(io.MousePos.x, 0);
        io.WantSetMousePos = true;
    }
}

void thread_check_sats()
{
    std::thread tsat(SatSource::check_satcat_and_latest);
    tsat.detach();
}

void process_key_cmd_char(char c)
{
    cel_obj_class cls;
    if (!cels[1]) return;

    // Keep this line to uncomment when testing which keystrokes ImGui recognizes.
    // std::cout << c << std::endl;

    switch (c)
    {
        case 'a': cbolbls_selected_idx = lbltype_brightest; show_labels = true; break;

        case 'A':
        if (explorer && celidx_sel_in_sysxplor >= 0) addcenidx = celidx_sel_in_sysxplor;
        else if (selected >= 0) addcenidx = selected;
        else if (trackidx >= 0) addcenidx = trackidx;
        else if (whereami >= 0) addcenidx = whereami;
        if (addcenidx < 0) return;
        cboceltyp_selected_idx = 2;
        cls = cels[addcenidx]->typeclass();
        if (cls == class_planet) cboceltyp_selected_idx = 3;
        else if (cls == class_moon) cboceltyp_selected_idx = 4;
        addcelwnd = true;
        break;

        case 'b': global_brightness *= 1.1; viewchanged = true; break;
        case 'B': global_brightness *= 0.9; viewchanged = true; break;
        case 'c': show_consln = !show_consln; break;
        case 'C': cbolbls_selected_idx = lbltype_sunlike; show_labels = true; break;
        case 'd': JDnow += 1; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'D': JDnow -= 1; viewchanged = true; compute_object_draw_coordinates(); break;

        case 'e': explorer = !explorer; break;

        case 'E':
        if (selected >= 0) editidx = selected;
        else if (trackidx >= 0) editidx = trackidx;
        else if (whereami >= 0) editidx = whereami;
        objedtwnd = (editidx >= 0);
        break;
        case 'f': cbolbls_selected_idx = lbltype_Flamsteed; show_labels = true; break;
        case 'F': cbolbls_selected_idx = lbltype_Bayer; show_labels = true; break;

        case 'g': show_grid = !show_grid; break;
        case 'G': cbolbls_selected_idx = lbltype_Gould; show_labels = true; break;
        case 'h': JDnow += 1.0/24; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'H': JDnow -= 1.0/24; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'i': JDnow += 1.0/1440; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'I': JDnow -= 1.0/1440; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'j': show_sats = !show_sats; break;
        case 'J': satview_upsidedown = !satview_upsidedown; break;
        case 'l': show_labels = !show_labels; break;
        case 'L': cbolbls_selected_idx = lbltype_planethz; show_labels = true; break;
        case 'm': JDnow += 30; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'M': JDnow -= 30; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'n': objinfwnd = !objinfwnd; break;
        case 'N': cbolbls_selected_idx = lbltype_nearby; show_labels = true; break;

        case 'o':
        if (selected < 0 && trackidx >= 0) selected = trackidx;
        if (selected >= 0)
        {
            whereami = selected;
            if (whereami >= 0 && cels[whereami] == cels[whereami]->cenobj) cbolbls_selected_idx = lbltype_brightest;
            set_viewer_location_and_plane();
            selected = trackidx = -1;
            global_brightness = default_brightness;
            zoom = 1;
        }
        else if (selected_locale)
        {
            viewer_lat = selected_locale->lat * fiftyseventh;
            viewer_lon = selected_locale->lon * fiftyseventh;
            viewer_locale = selected_locale->name;
            view_mode = vm_horizon;
        }
        if (view_mode == vm_skymap || view_mode == vm_sunclock) altitude = 0;
        velocity = center;
        viewchanged = true;
        refresh_star_visibilities();
        break;

        case 'O': show_orbits = !show_orbits; break;
        case 'p': lbl_localsys = !lbl_localsys; break;
        case 'P': cbolbls_selected_idx = lbltype_planets; show_labels = true; break;
        case 'q': sphere_quality *= 1.3; viewchanged=true; break;
        case 'Q': sphere_quality *= 0.7; viewchanged=true; break;

        case 'r':
        velocity = center;
        whtbkgd = false;
        zoom = 1;
        spin = 0;
        whereami = iamhome;
        trackidx = -1;
        view_mode = vm_skyatlas;
        viewer_lat = viewer_home_lat;
        viewer_lon = viewer_home_lon;
        viewer_locale = "";
        save_viewer_latlon = true;
        set_viewer_location_and_plane();
        global_brightness = default_brightness;
        global_gamma = viewer_gamma;
        neighb_rthresh = 25 * light_year;
        show_consln = show_grid = show_labels = lbl_localsys = show_localsys = show_sats = statuswnd = objinfwnd = true;
        show_orbits = false;
        cbolbls_selected_idx = lbltype_brightest;
        appmagn_lblcut = 2.5;
        absmagn_lblcut = -3.5;
        distance_lblcut = 25*light_year;
        planets_lblcut = 1;
        [[fallthrough]];
        case '@':
        viewchanged = true;
        sphere_quality = 1;
        simnow = std::time(nullptr);
        JDnow = ((double)simnow - J2000_TIME_T)/oneday + J2000;
        refresh_star_visibilities();
        compute_object_draw_coordinates();
        break;

        case 'R': redlight_mode = !redlight_mode; apply_default_style(); break;
        case 's': statuswnd = !statuswnd; break;
        case 'S': selected = -1; break;

        case 't':
        if (trackidx >= 0)
        {
            selected = trackidx;
            trackidx = -1;
            center_selected();
        }
        else
        {
            center_selected();
            trackidx = selected;
            selected = -1;
        }
        viewchanged = true;
        break;

        case 'T': trackidx = -1; break;
        case 'u': save_universe(); break;

        case 'U':
        save_viewer_latlon = true;
        save_user_json();
        break;

        case 'v': cbolbls_selected_idx = lbltype_intrinsic; show_labels = true; break;
        case 'V': show_localsys = !show_localsys; break;

        case 'w':
        if (velocity.magnitude())
        {
            velocity.scale(speed_of_light * 1.00001 / target_frame_rate);
        }
        else
        {
            velocity.x =  sin(azimuth+azimuth_correction) * cos(altitude) * speed_of_light * 1.00001 / target_frame_rate;
            velocity.z =  cos(azimuth+azimuth_correction) * cos(altitude) * speed_of_light * 1.00001 / target_frame_rate;
            velocity.y =  sin(altitude) * speed_of_light * 1.00001 / target_frame_rate;
            velocity = to_viewer_plane(velocity, -1);
        }
        spin = 0;
        viewchanged = true;
        took_off_from = whereami;
        tookoff_countdown = 5;
        whereami = -1;
        break;

        case 'W': whtbkgd = !whtbkgd; break;

        case 'x':
        velocity = center;
        viewchanged = true;
        break;
        case 'X': cbolbls_selected_idx = lbltype_knpole; show_labels = true; break;

        case 'y': JDnow += (oneyear/oneday); redo_proper_motions = viewchanged = true; compute_object_draw_coordinates(); break;
        case 'Y': JDnow -= (oneyear/oneday); redo_proper_motions = viewchanged = true; compute_object_draw_coordinates(); break;
        case 'z': JDnow += (oneyear/864); redo_proper_motions = viewchanged = true; compute_object_draw_coordinates(); break;
        case 'Z': JDnow -= (oneyear/864); redo_proper_motions = viewchanged = true; compute_object_draw_coordinates(); break;

        case '0': neighborhood = !neighborhood; break;
        case '1': show_consln = show_grid = show_labels = lbl_localsys = statuswnd = objinfwnd = show_localsys = true; break;
        case '2': cbolbls_selected_idx = lbltype_binary; show_labels = true; break;

        case '3':
        themes_selected_idx++;
        if ((unsigned)themes_selected_idx >= themes.size()) themes_selected_idx = 0;
        global_style.load(themes[themes_selected_idx]);
        break;

        case '#':
        themes_selected_idx--;
        if (themes_selected_idx < 0) themes_selected_idx = themes.size()-1;
        global_style.load(themes[themes_selected_idx]);
        break;

        case '4': azimuth = -half_pi; viewchanged = true; break;            // look west
        case '6': azimuth =  half_pi; viewchanged = true; break;            // look east
        case '5': azimuth = 0; viewchanged = true; break;                   // look north, or toward equinox
        case '8': azimuth = _pi; viewchanged = true; break;                // look south, or away from equinox

        case '+':
        vm = velocity.magnitude();
        vmfr = vm * target_frame_rate;
        if (vmfr > 1e100)
        {
            // In Trek, the fastest possible warp is warp 10.
            // But that doesn't make for impressive spaceflights.
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
            velocity.x =  sin(azimuth+azimuth_correction) * cos(altitude) * 1000;
            velocity.z =  cos(azimuth+azimuth_correction) * cos(altitude) * 1000;
            velocity.y =  sin(altitude) * 1000;
            velocity = to_viewer_plane(velocity, -1);
            if (whereami >= 0)
            {
                if (view_mode == vm_skyatlas || view_mode == vm_skymap)
                {
                    Point fromsurf = velocity;
                    fromsurf.scale(velocity.magnitude() + cels[whereami]->get_equatorial_radius());
                    here.local_position += fromsurf;
                }
                took_off_from = whereami;
                tookoff_countdown = 5;
            }
            whereami = -1;
        }
        viewchanged = true;
        break;

        case '.': astwnd = !astwnd; break;
        case ',': frames_without_mousemove = 1000; break;
        case '|': show_axes = !show_axes; break;
        case '!': show_consln = show_grid = show_labels = lbl_localsys = show_orbits = false; break;
        case '%':
        zoom = 1;
        global_brightness = 1;
        viewchanged = true;
        sphere_quality = 1;
        if (view_mode == vm_skymap || view_mode == vm_sunclock) altitude = 0;
        break;
        case '*': zoom *= 1.1; global_brightness *= 1.05; viewchanged = true; scrollhold = 1; break;
        case '/': zoom *= 0.9; if (zoom < 1) zoom = 1; else global_brightness *= 0.95; viewchanged = true; scrollhold = 1; break;
        case '^':
        satwnd = true;
        thread_check_sats();
        break;

        case '-':
        vm = velocity.magnitude();
        velocity.scale(vm * 0.666);
        viewchanged = true;
        break;

        case '`': global_gamma += 0.2; set_gamma(global_gamma); break;
        case '~': global_gamma -= 0.2; set_gamma(global_gamma); break;

        case '&': view_mode = vm_skyatlas; viewer_lat = viewer_home_lat; viewer_home_lon = viewer_home_lon; save_viewer_latlon = viewchanged = true; break;
        case '_': view_mode = vm_horizon; viewchanged = true; altitude = 0; break;
        case '$': view_mode = vm_sunclock; zoom=1; altitude=0; azimuth=0; viewchanged = true; break;
        case '\\': view_mode = vm_skymap; altitude = 0; break;
        case ';': /* view_mode = vm_model; */ break;                 // not yet implemented but want to keep the placeholder

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
        if (keyprobe) std::cout << "Key press: " << c << std::endl;         // Output the ASCII value.
        process_key_cmd_char(c);
    }

    double steering_rate = _pi/16/zoom;
    double walk_speed = 4 * frame_dur;                              // a fast run
    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow) && !is_mouse_over_window)
    {
        if (!ImGui::IsKeyDown(ImGuiKey_End) && !ImGui::IsKeyDown(ImGuiKey_Home))
        {
            if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) steering_rate *= 0.1;
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) steering_rate *= 0.01;
        }
        Point yaw = to_viewer_plane(yaxis, -1);
        velocity = rotate3D(velocity, center, yaw, -steering_rate);
        if (trackidx<0) azimuth -= steering_rate;
    }
    if (ImGui::IsKeyDown(ImGuiKey_RightArrow) && !is_mouse_over_window)
    {
        if (!ImGui::IsKeyDown(ImGuiKey_End) && !ImGui::IsKeyDown(ImGuiKey_Home))
        {
            if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) steering_rate *= 0.1;
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) steering_rate *= 0.01;
        }
        Point yaw = to_viewer_plane(yaxis, -1);
        velocity = rotate3D(velocity, center, yaw,  steering_rate);
        if (trackidx<0) azimuth += steering_rate;
    }
    if (ImGui::IsKeyDown(ImGuiKey_UpArrow) && !is_mouse_over_window)
    {
        if (!ImGui::IsKeyDown(ImGuiKey_End) && !ImGui::IsKeyDown(ImGuiKey_Home))
        {
            if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) steering_rate *= 0.1;
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) steering_rate *= 0.01;
        }
        Point pitch = to_viewer_plane(xaxis, -1);
        velocity = rotate3D(velocity, center, pitch, -steering_rate);
        if (trackidx<0) altitude += steering_rate;
        if (altitude > half_pi) altitude = half_pi;
        enforce_y_pan_limit();
    }
    if (ImGui::IsKeyDown(ImGuiKey_DownArrow) && !is_mouse_over_window)
    {
        if (!ImGui::IsKeyDown(ImGuiKey_End) && !ImGui::IsKeyDown(ImGuiKey_Home))
        {
            if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) steering_rate *= 0.1;
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) steering_rate *= 0.01;
        }
        Point pitch = to_viewer_plane(xaxis, -1);
        velocity = rotate3D(velocity, center, pitch,  steering_rate);
        if (trackidx<0) altitude -= steering_rate;
        if (altitude < -half_pi) altitude = -half_pi;
        enforce_y_pan_limit();
    }
    if (ImGui::IsKeyDown(ImGuiKey_End) && !is_mouse_over_window)
    {
        double vmag;
        if (view_mode == vm_horizon)
        {
            if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) walk_speed *= 10;
            if (ImGui::IsKeyDown(ImGuiKey_RightShift)) walk_speed *= 10;
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) walk_speed *= 100;
            if (ImGui::IsKeyDown(ImGuiKey_RightCtrl)) walk_speed *= 100;
            double inv_circ = 1.0 / (_pi * 2 * cels[whereami]->volumetric_mean_radius);
            double coslat = cos(viewer_lat);
            viewer_lat += walk_speed * inv_circ * cos(azimuth);
            if (coslat) viewer_lon += walk_speed * inv_circ * sin(azimuth) / coslat;
            save_viewer_latlon = false;
        }
        else if ((vmag = velocity.magnitude()))                 // assignment not comparison
        {
            double acceleration = vmag * 0.1;
            Point forward;
            forward.x =  sin(azimuth+azimuth_correction) * cos(altitude) * acceleration;
            forward.z =  cos(azimuth+azimuth_correction) * cos(altitude) * acceleration;
            forward.y =  sin(altitude) * acceleration;
            forward = to_viewer_plane(forward, -1);
            velocity += forward;
        }
    }
    if (ImGui::IsKeyDown(ImGuiKey_Home) && !is_mouse_over_window)
    {
        double vmag;
        if (view_mode == vm_horizon)
        {
            if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) walk_speed *= 10;
            if (ImGui::IsKeyDown(ImGuiKey_RightShift)) walk_speed *= 10;
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) walk_speed *= 100;
            if (ImGui::IsKeyDown(ImGuiKey_RightCtrl)) walk_speed *= 100;
            double inv_circ = 1.0 / (_pi * 2 * cels[whereami]->volumetric_mean_radius);
            double coslat = cos(viewer_lat);
            viewer_lat -= walk_speed * inv_circ * cos(azimuth);
            if (coslat) viewer_lon -= walk_speed * inv_circ * sin(azimuth) / coslat;
            save_viewer_latlon = false;
        }
        else if ((vmag = velocity.magnitude()))                 // assignment not comparison
        {
            double acceleration = velocity.magnitude() * 0.1;
            Point forward;
            forward.x =  sin(azimuth+azimuth_correction) * cos(altitude) * acceleration;
            forward.z =  cos(azimuth+azimuth_correction) * cos(altitude) * acceleration;
            forward.y =  sin(altitude) * acceleration;
            forward = to_viewer_plane(forward, -1);
            velocity -= forward;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F3))
    {
        focus_findbox = true;
        statuswnd = true;
        lookfor[0] = 0;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F4))
    {
        IGFD::FileDialogConfig config;
        config.path = ".";
        ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".json", config);
        fdlg_shown = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F5))
    {
        splash = true;
        std::thread t1(reload_stuff);
        t1.detach();
    }
}

void do_find()
{
    int i = find_object(lookfor, false, 9e+29, 6);
    if (i>=0)
    {
        if (view_mode == vm_sunclock) view_mode = vm_skyatlas;
        selected = i;
        trackidx = -1;
        center_selected();
        searched = true;
        lookfor_notfound = false;
    }
    else lookfor_notfound = true;
}

int lookfor_cb(ImGuiInputTextCallbackData* data)
{
    lookfor_notfound = false;
    if (data->EventChar == '\n') do_find();
    return 0;
}
