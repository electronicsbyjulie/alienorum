
#include "globals.h"
#include "loaders.h"
#include "housekeeping.h"
#include "inputs.h"

using namespace alienorum;

void center_selected()
{
    if (selected >= 0)
    {
        azimuth = -cels[selected]->RA_as_radians(here, 0);
        altitude = cels[selected]->Decl_as_radians(here);
    }
    viewchanged = true;
}

void center_tracked()
{
    if (trackidx >= 0)
    {
        azimuth = -cels[trackidx]->RA_as_radians(here, 0);
        altitude = cels[trackidx]->Decl_as_radians(here);
    }
    viewchanged = true;
}

void identify_object_under_cursor(ImGuiIO& io)
{
    int i;
    double myeq = (whereami >= 0) ? cels[whereami]->equinox_eff : 0;

    is_an_obj_under_cursor = -1;
    obj_magn_under_cursor = 1e9;
    int threshold = circle_size*1.3;
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
                if (is_click && !dragged) selected = i;
            }
        }
    }

    if (is_an_obj_under_cursor >= 0)
    {
        i = is_an_obj_under_cursor;
        double lmag = vmag_cache[i];
        bool am_satellite = (whereami>0) && (cels[whereami]->type == artificial);
        bool sat_low_orbit = am_satellite && (cels[i]->tmprel.magnitude() < cels[i]->volumetric_mean_radius*2);

        std::stringstream oss;

        // TODO: Refactor this as multi-line ImGui::Text() and move it to the show_objinfo_window ftn
        // in dialogs.cpp make the objinfo window look more like the status window. That way it can also
        // turn the "habitable zone" text green (don't forget the redlight mode correction).
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

        if (view_mode == vm_skyatlas)
        {
            objinfo += (std::string)"RA:       " + cels[i]->RA_as_hms(here, myeq) + (std::string)"\n"
                    + (std::string)"Decl:     " + cels[i]->Decl_as_degms(here) + (std::string)"\n";
        }
        else
        {
            npaz = fmod(npdummy.RA_as_radians(here, 0), _pi*2);
            double objaz = fmod(npaz - cels[i]->RA_as_radians(here, 0), _pi*2);
            if (objaz < 0) objaz += _pi*2;
            objinfo += (std::string)"Altitude: " + std::to_string(cels[i]->Decl_as_radians(here)*fiftyseven) + (std::string)"\n"
                    + (std::string)"Azimuth:  " 
                    + std::to_string(objaz*fiftyseven)
                    + (std::string)"\n";
        }
        if (!sat_low_orbit)
            oss << "Mag:      " << std::setprecision(2) << lmag << std::endl;

        objinfo += oss.str();
        oss.str("");
        oss.clear();

        if (cels[i]->type == star)
        {
            Star* s = (Star*)cels[i];
            if (s->distance_known)
            {
                oss << "Dist:     " << cels[i]->scaled_distance(here, sat_low_orbit) << std::endl;
                oss << "AbsMag:   " << std::setprecision(2) << s->absolute_magnitude << "\n";
            }
            objinfo += (std::string)"SpTyp:    " + s->spectral_type + (std::string)"\n";
        }
        else if (cels[i]->type == galaxy)
        {
            // TODO:
        }
        else if (cels[i]->type == artificial)
        {
            oss << "Dist:     " << cels[i]->scaled_distance(here) << std::endl;
        }
        else
        {
            oss << "Dist:     " << cels[i]->scaled_distance(here, sat_low_orbit) << std::endl;
            if (!sat_low_orbit)
                oss << "Lit %:    " << std::setprecision(1) << ((int)(((Planet*)cels[i])->amt_lit*100)) << std::endl;
            if (((Planet*)cels[i])->is_in_con_HZ()) oss << "          Habitable Zone" << std::endl;
        }

        cel_obj_class cls = cels[i]->typeclass();
        if (cels[i]->mass)
        {
            if (cls == class_star) 
                ; // oss << "Mass:  " << std::setprecision(2) << (cels[i]->mass / Msun) << " M(sun)\n" << std::endl;       // TODO: Fix Star::estimate_mass()
            else if (cls == class_planet || cls == class_moon)
            {
                oss << "Mass:     " << std::setprecision(2) << (cels[i]->mass / cels[iamhome]->mass) << " M(earth)" << std::endl;
                oss << "Mass:     " << std::scientific << std::setprecision(2) << (cels[i]->mass / 1000) << " kg" << std::endl;
            }
        }
        if (cels[i]->volumetric_mean_radius)
        {
            if (cls == class_star)
                ; // oss << "Radius: " << std::setprecision(2) << (cels[i]->volumetric_mean_radius / Rsun) << " R(sun)" << std::endl;       // TODO: Fix Star::estimate_radius()
            else if (cls == class_planet || cls == class_moon)
            {
                oss << "Radius:   " << std::setprecision(2) << (cels[i]->volumetric_mean_radius / cels[iamhome]->volumetric_mean_radius)
                    << " R(earth)" << std::endl;
                oss << "Radius:   " << std::scientific << std::setprecision(2) << (cels[i]->volumetric_mean_radius / 1000) << " km" << std::endl;
            }
        }

        #ifdef DEBUG
        oss << "index:    " << is_an_obj_under_cursor << std::endl;
        #endif

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
        if (altitude >  _pi/2) altitude =  _pi/2;
        if (altitude < -_pi/2) altitude = -_pi/2;
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
        if (altitude >  _pi/2) altitude =  _pi/2;
        if (altitude < -_pi/2) altitude = -_pi/2;
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
        if (altitude >  _pi/2) altitude =  _pi/2;
        if (altitude < -_pi/2) altitude = -_pi/2;
        spin = 0;
        viewchanged = true;

        ImVec2 topcen(dispcx, 0), botcen(dispcx, (int)io.DisplaySize.y-1),
            leftcen(0, dispcy), rightcen((int)io.DisplaySize.x-1, dispcy);
        ImGui::GetBackgroundDrawList()->AddLine(topcen, botcen, rgba_apply_redlight(IM_COL32(255, 96, 0, 96)), 1);
        ImGui::GetBackgroundDrawList()->AddLine(leftcen, rightcen, rgba_apply_redlight(IM_COL32(255, 96, 0, 96)), 1);
    }
}

void process_key_cmd_char(char c)
{
    cel_obj_class cls;
    if (!cels[1]) return;

    // Keep this line to uncomment when testing which keystrokes ImGui recognizes.
    // std::cout << c << std::endl;

    switch (c)
    {
        case 'A':
        if (whereami < 0) return;
        if (explorer && celidx_sel_in_sysxplor >= 0) addcenidx = celidx_sel_in_sysxplor;
        else if (selected >= 0) addcenidx = selected;
        else if (trackidx >= 0) addcenidx = trackidx;
        else if (whereami >= 0) addcenidx = whereami;
        cboceltyp_selected_idx = 2;
        cls = cels[addcenidx]->typeclass();
        if (cls == class_planet) cboceltyp_selected_idx = 3;
        else if (cls == class_moon) cboceltyp_selected_idx = 4;
        addcelwnd = true;
        break;

        case 'b': global_brightness *= 1.1; viewchanged = true; break;
        case 'B': global_brightness *= 0.9; viewchanged = true; break;
        case 'c': show_consln = !show_consln; break;
        case 'd': JDnow += 1; viewchanged = true; compute_object_draw_coordinates(); break;
        case 'D': JDnow -= 1; viewchanged = true; compute_object_draw_coordinates(); break;

        case 'e': explorer = !explorer; break;

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
        if (selected < 0 && trackidx >= 0) selected = trackidx;
        if (selected >= 0)
        {
            whereami = selected;
            set_viewer_location_and_plane();
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
        view_mode = vm_skyatlas;
        viewer_lat = viewer_home_lat;
        viewer_lon = viewer_home_lon;
        save_viewer_latlon = true;
        set_viewer_location_and_plane();
        global_brightness = default_brightness;
        show_consln = show_grid = show_labels = true;
        show_orbits = false;
        cbolbls_selected_idx = 0;
        appmagn_lblcut = 2.5;
        absmagn_lblcut = -3.5;
        distance_lblcut = 25*light_year;
        planets_lblcut = 1;
        [[fallthrough]];
        case '@':
        viewchanged = true;
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
            velocity.x =  sin(azimuth+azimuth_correction) * cos(altitude) * 1000;
            velocity.z =  cos(azimuth+azimuth_correction) * cos(altitude) * 1000;
            velocity.y =  sin(altitude) * 1000;
            velocity = to_viewer_plane(velocity, -1);
            if (whereami >= 0) took_off_from = whereami;
            whereami = -1;
        }
        viewchanged = true;
        break;

        case '.': astwnd = !astwnd; break;
        case ',': frames_without_mousemove = 1000; break;
        case '!': show_consln = show_grid = show_labels = lbl_localsys = show_orbits = false; break;
        case '%': zoom = 1; global_brightness = 1; viewchanged = true; break;
        case '*': zoom *= 1.1; global_brightness *= 1.05; viewchanged = true; scrollhold = 1; break;
        case '/': zoom *= 0.9; if (zoom < 1) zoom = 1; else global_brightness *= 0.95; viewchanged = true; scrollhold = 1; break;
        case '^': satwnd = true; break;

        case '-':
        vm = velocity.magnitude();
        velocity.scale(vm * 0.666);
        viewchanged = true;
        break;

        case '`': global_gamma += 0.2; set_gamma(global_gamma); break;
        case '~': global_gamma -= 0.2; set_gamma(global_gamma); break;

        case '&': view_mode = vm_skyatlas; viewer_lat = viewer_home_lat; viewer_home_lon = viewer_home_lon; save_viewer_latlon = viewchanged = true; break;
        case '_': view_mode = vm_horizon; viewchanged = true; break;
        case '$': /* view_mode = vm_sunclock; */ break;                 // not yet implemented but want to keep the placeholder

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
    if (ImGui::IsKeyPressed(ImGuiKey_F4))
    {
        IGFD::FileDialogConfig config;
        config.path = ".";
        ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".json", config);
        fdlg_shown = true;
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
