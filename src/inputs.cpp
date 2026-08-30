
#include "globals.h"
#include "misc.h"
#include "loaders.h"
#include "housekeeping.h"
#include "inputs.h"
#include "classes/sscimport.h"

using namespace alienorum;

void center_selected()
{
    if (selected >= 0)
    {
        azimuth = cels[selected]->RA_as_radians(here,
            (whereami >= 0 && view_mode == vm_sunclock) ? cels[whereami]->timeofday() : 0)
            * ((view_mode == vm_sunclock) ? 1 : -1);
        altitude = cels[selected]->Decl_as_radians_refracted(here);
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
        altitude = cels[trackidx]->Decl_as_radians_refracted(here);
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
        if ((i == whereami) && (view_mode != vm_system)) continue;
        if (cels[i]->deleted) continue;

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
    if (trackidx >= 0) return;
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
    else
    {
        dragging = false;
        return;
    }

    if (io.MouseDelta.x < 0 && io.MousePos.x < -mouse_drag_threshold*2*io.MouseDelta.x)
    {
        io.MousePos = ImVec2(dispcx*2, io.MousePos.y);
        io.WantSetMousePos = true;
    }
    else if (io.MouseDelta.x > 0 && io.MousePos.x > dispcx*2 - mouse_drag_threshold*2*io.MouseDelta.x)
    {
        io.MousePos = ImVec2(0, io.MousePos.y);
        io.WantSetMousePos = true;
    }

    if (io.MouseDelta.y < 0 && io.MousePos.y < -mouse_drag_threshold*2*io.MouseDelta.y)
    {
        io.MousePos = ImVec2(io.MousePos.x, dispcy*2);
        io.WantSetMousePos = true;
    }
    else if (io.MouseDelta.y > 0 && io.MousePos.y > dispcy*2 - mouse_drag_threshold*2*io.MouseDelta.y)
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

void show_menu()
{
    menu_clicked = false;
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            mouse_over_menu = true;
            if (ImGui::MenuItem("Save Snapshot", "F12")) { process_key_F12(); menu_clicked = true; }
            if (ImGui::MenuItem("Write universe.json", "U")) { process_key_cmd_char('u'); menu_clicked = true; }
            if (ImGui::MenuItem("Load Universe...", "F4")) { process_key_F4(); menu_clicked = true; }
            if (ImGui::MenuItem("Import SSC Add-On...", "F6")) { process_key_F6(); menu_clicked = true; }
            if (ImGui::MenuItem("Overwrite Map Files On Import", nullptr, &last_ssc_import.overwrite_maps)) menu_clicked = true;
            if (ImGui::MenuItem("Write User Settings", "Shift+U")) { process_key_cmd_char('U'); menu_clicked = true; }
            if (ImGui::MenuItem("Reload Constellations", "F5")) { process_key_F5(); menu_clicked = true; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Object"))
        {
            mouse_over_menu = true;
            if (ImGui::MenuItem("Search...", "F3")) { process_key_F3(); menu_clicked = true; }
            if (ImGui::MenuItem("Track Selected", "T")) { process_key_cmd_char('t'); menu_clicked = true; }
            if (ImGui::MenuItem("Clear Selection", "Shift+S")) { process_key_cmd_char('S'); menu_clicked = true; }
            if (ImGui::MenuItem("Clear Tracking", "Shift+T")) { process_key_cmd_char('T'); menu_clicked = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Add Object...", "Shift+A")) { process_key_cmd_char('A'); menu_clicked = true; }
            if (ImGui::MenuItem("Add Satellite...", "^")) { process_key_cmd_char('^'); menu_clicked = true; }
            if (ImGui::MenuItem("Add Asteroid...", ".")) { process_key_cmd_char('.'); menu_clicked = true; }
            if (ImGui::MenuItem("Add Comet...", ";")) { process_key_cmd_char(';'); menu_clicked = true; }
            if (ImGui::MenuItem("Edit Object...", "Shift+E")) { process_key_cmd_char('E'); menu_clicked = true; }
            if (ImGui::MenuItem("Delete Object", "Del")) { process_key_delete(); menu_clicked = true; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Spacetime"))
        {
            mouse_over_menu = true;
            if (ImGui::MenuItem("Go to Object", "O")) { process_key_cmd_char('o'); menu_clicked = true; }
            if (ImGui::MenuItem("Return Home", "R")) { process_key_cmd_char('r'); menu_clicked = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Spaceflight/Speed Up", "+")) { process_key_cmd_char('+'); menu_clicked = true; }
            if (ImGui::MenuItem("Slow Down", "-")) { process_key_cmd_char('-'); menu_clicked = true; }
            if (ImGui::MenuItem("Steer Left", "Left Arrow")) { process_key_arrowleft(); menu_clicked = true; }
            if (ImGui::MenuItem("Steer Right", "Right Arrow")) { process_key_arrowright(); menu_clicked = true; }
            if (ImGui::MenuItem("Steer Up", "Up Arrow")) { process_key_arrowup(); menu_clicked = true; }
            if (ImGui::MenuItem("Steer Down", "Down Arrow")) { process_key_arrowdn(); menu_clicked = true; }
            if (ImGui::MenuItem("Accelerate Forward", "End")) { process_key_end(); menu_clicked = true; }
            if (ImGui::MenuItem("Accelerate Backward", "Home")) { process_key_home(); menu_clicked = true; }
            if (ImGui::MenuItem("Warp Speed", "W")) { process_key_cmd_char('w'); menu_clicked = true; }
            if (ImGui::MenuItem("Full Stop", "X")) { process_key_cmd_char('x'); menu_clicked = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Advance One Minute", "I")) { process_key_cmd_char('i'); menu_clicked = true; }
            if (ImGui::MenuItem("Rewind One Minute", "Shift+I")) { process_key_cmd_char('I'); menu_clicked = true; }
            if (ImGui::MenuItem("Advance One Hour", "H")) { process_key_cmd_char('h'); menu_clicked = true; }
            if (ImGui::MenuItem("Rewind One Hour", "Shift+H")) { process_key_cmd_char('H'); menu_clicked = true; }
            if (ImGui::MenuItem("Advance One Day", "D")) { process_key_cmd_char('d'); menu_clicked = true; }
            if (ImGui::MenuItem("Rewind One Day", "Shift+D")) { process_key_cmd_char('D'); menu_clicked = true; }
            if (ImGui::MenuItem("Advance One Month", "M")) { process_key_cmd_char('m'); menu_clicked = true; }
            if (ImGui::MenuItem("Rewind One Month", "Shift+M")) { process_key_cmd_char('M'); menu_clicked = true; }
            if (ImGui::MenuItem("Advance One Year", "Y")) { process_key_cmd_char('y'); menu_clicked = true; }
            if (ImGui::MenuItem("Rewind One Year", "Shift+Y")) { process_key_cmd_char('Y'); menu_clicked = true; }
            if (ImGui::MenuItem("Advance One Century", "Z")) { process_key_cmd_char('z'); menu_clicked = true; }
            if (ImGui::MenuItem("Rewind One Century", "Shift+Z")) { process_key_cmd_char('Z'); menu_clicked = true; }
            if (ImGui::MenuItem("Return to Present", "@")) { process_key_cmd_char('@'); menu_clicked = true; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            mouse_over_menu = true;
            if (ImGui::MenuItem("Increase Brightness", "B")) { process_key_cmd_char('b'); menu_clicked = true; }
            if (ImGui::MenuItem("Decrease Brightness", "Shift+B")) { process_key_cmd_char('B'); menu_clicked = true; }
            if (ImGui::MenuItem("Increase Gamma", "`")) { process_key_cmd_char('`'); menu_clicked = true; }
            if (ImGui::MenuItem("Decrease Gamma", "~")) { process_key_cmd_char('~'); menu_clicked = true; }
            if (ImGui::MenuItem("Zoom In", "*")) { process_key_cmd_char('*'); menu_clicked = true; }
            if (ImGui::MenuItem("Zoom Out", "/")) { process_key_cmd_char('/'); menu_clicked = true; }
            if (ImGui::MenuItem("Reset Brightness and Zoom", "%")) { process_key_cmd_char('%'); menu_clicked = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Azimuth zero", "5", nullptr, (trackidx < 0))) { process_key_cmd_char('5'); menu_clicked = true; }
            if (ImGui::MenuItem("Azimuth 90deg", "6", nullptr, (trackidx < 0))) { process_key_cmd_char('6'); menu_clicked = true; }
            if (ImGui::MenuItem("Azimuth 180deg", "8", nullptr, (trackidx < 0))) { process_key_cmd_char('8'); menu_clicked = true; }
            if (ImGui::MenuItem("Azimuth 270deg", "4", nullptr, (trackidx < 0))) { process_key_cmd_char('4'); menu_clicked = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Red Light Mode", "Shift+R", redlight_mode)) { process_key_cmd_char('R'); menu_clicked = true; }
            if (ImGui::MenuItem("White Background Mode", "Shift+W", whtbkgd)) { process_key_cmd_char('W'); menu_clicked = true; }
            if (ImGui::MenuItem("Fullscreen", "F11", fullscreen)) { process_key_F11(); menu_clicked = true; }
            ImGui::Separator();
            if (ImGui::MenuItem(vmtext[0], "&", view_mode == vm_spaceship)) { process_key_cmd_char('&'); menu_clicked = true; }
            if (ImGui::MenuItem(vmtext[1], "_", view_mode == vm_horizon)) { process_key_cmd_char('_'); menu_clicked = true; }
            if (ImGui::MenuItem(vmtext[2], "$", view_mode == vm_sunclock)) { process_key_cmd_char('$'); menu_clicked = true; }
            if (ImGui::MenuItem(vmtext[3], "\\", view_mode == vm_skymap)) { process_key_cmd_char('\\'); menu_clicked = true; }
            if (ImGui::BeginMenu("Viewer Plane"))
            {
                mouse_over_menu = true;
                if (ImGui::MenuItem("Local", "Ctrl+L", vplane_mode == vplane_local)) { process_key_cmd_ctrl_char('L'); menu_clicked = true; }
                if (ImGui::MenuItem("ICRF", "Ctrl+I", vplane_mode == vplane_ICRF)) { process_key_cmd_ctrl_char('I'); menu_clicked = true; }
                if (ImGui::MenuItem("Ecliptic", "Ctrl+E", vplane_mode == vplane_ecliptic)) { process_key_cmd_ctrl_char('E'); menu_clicked = true; }
                if (ImGui::MenuItem("Galactic", "Ctrl+G", vplane_mode == vplane_galactic)) { process_key_cmd_ctrl_char('G'); menu_clicked = true; }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Earth-Up (for satellites)", "Shift+J", satview_upsidedown)) { process_key_cmd_char('J'); menu_clicked = true; }
            if (ImGui::MenuItem("Previous Theme", "#")) { process_key_cmd_char('#'); menu_clicked = true; }
            if (ImGui::MenuItem("Next Theme", "3")) { process_key_cmd_char('3'); menu_clicked = true; }           
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Show"))
        {
            mouse_over_menu = true;
            if (ImGui::MenuItem("Info Panel", "N", objinfwnd)) { process_key_cmd_char('n'); menu_clicked = true; }
            if (ImGui::MenuItem("Status Panel", "S", statuswnd)) { process_key_cmd_char('s'); menu_clicked = true; }
            if (ImGui::MenuItem("System Explorer", "E", explorer)) { process_key_cmd_char('e'); menu_clicked = true; }
            if (ImGui::MenuItem("Stellar Neighborhood", "0", neighborhood)) { process_key_cmd_char('0'); menu_clicked = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Constellations", "C", show_consln)) { process_key_cmd_char('c'); menu_clicked = true; }
            if (ImGui::MenuItem("RA/Dec Grid", "G", show_grid)) { process_key_cmd_char('g'); menu_clicked = true; }
            if (ImGui::MenuItem("Star Labels", "L", show_labels)) { process_key_cmd_char('l'); menu_clicked = true; }
            if (ImGui::BeginMenu("Label Stars By"))
            {
                mouse_over_menu = true;
                if (ImGui::MenuItem("Brightest Stars", "A", cbolbls_selected_idx == lbltype_brightest)) { process_key_cmd_char('a'); menu_clicked = true; }
                if (ImGui::MenuItem("Intrinsically Brightest", "V", cbolbls_selected_idx == lbltype_intrinsic)) { process_key_cmd_char('v'); menu_clicked = true; }
                if (ImGui::MenuItem("Nearest Stars", "Shift+N", cbolbls_selected_idx == lbltype_nearby)) { process_key_cmd_char('N'); menu_clicked = true; }
                if (ImGui::MenuItem("Bayer Designations", "Shift+F", cbolbls_selected_idx == lbltype_Bayer)) { process_key_cmd_char('F'); menu_clicked = true; }
                if (ImGui::MenuItem("Flamsteed Numbers", "F", cbolbls_selected_idx == lbltype_Flamsteed)) { process_key_cmd_char('f'); menu_clicked = true; }
                if (ImGui::MenuItem("Gould Uranometria Numbers", "Shift+G", cbolbls_selected_idx == lbltype_Gould)) { process_key_cmd_char('G'); menu_clicked = true; }
                if (ImGui::MenuItem("Sunlike Stars", "Shift+C", cbolbls_selected_idx == lbltype_sunlike)) { process_key_cmd_char('C'); menu_clicked = true; }
                if (ImGui::MenuItem("Stars with Planets", "Shift+P", cbolbls_selected_idx == lbltype_planets)) { process_key_cmd_char('P'); menu_clicked = true; }
                if (ImGui::MenuItem("Stars with Planets in HZ", "Shift+L", cbolbls_selected_idx == lbltype_planethz)) { process_key_cmd_char('L'); menu_clicked = true; }
                if (ImGui::MenuItem("Stars with Known Poles", "Shift+X", cbolbls_selected_idx == lbltype_knpole)) { process_key_cmd_char('X'); menu_clicked = true; }
                if (ImGui::MenuItem("Binary Systems", "2", cbolbls_selected_idx == lbltype_binary)) { process_key_cmd_char('2'); menu_clicked = true; }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Galaxy Labels", "K", label_galaxies)) { process_key_cmd_char('k'); menu_clicked = true; }
            if (ImGui::MenuItem("Galaxy Band", "Shift+K", show_galaxy_band)) { process_key_cmd_char('K'); menu_clicked = true; }
            if (ImGui::MenuItem("Satellites", "J", show_sats)) { process_key_cmd_char('j'); menu_clicked = true; }
            if (ImGui::MenuItem("Local System Objects", "Shift+V", show_localsys)) { process_key_cmd_char('V'); menu_clicked = true; }
            if (ImGui::MenuItem("Local System Labels", "P", lbl_localsys)) { process_key_cmd_char('p'); menu_clicked = true; }
            if (ImGui::MenuItem("Orbits", "Shift+O", show_orbits)) { process_key_cmd_char('O'); menu_clicked = true; }
            if (ImGui::MenuItem("Coordinate Axes", "|", show_axes)) { process_key_cmd_char('|'); menu_clicked = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Realism Mode (no annotations)", "!")) { process_key_cmd_char('!'); menu_clicked = true; }
            if (ImGui::MenuItem("Default Annotations", "1")) { process_key_cmd_char('1'); menu_clicked = true; }
            if (ImGui::MenuItem("Terrain", "Ctrl+T", show_terrain)) { process_key_cmd_ctrl_char('T'); menu_clicked = true; }
            if (ImGui::MenuItem("Hide Mouse Cursor", ",")) { process_key_cmd_char(','); menu_clicked = true; }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
        if (ImGui::IsItemHovered()) mouse_over_menu = true;                 // Yet another ImgUI feature that doesn't work.
    }
}

void process_key_cmd_char(char c)
{
    cel_obj_class cls;
    if (!cels[1]) return;

    // Keep this line to uncomment when testing which keystrokes ImGui recognizes.
    // std::cout << c << std::endl;

    // IMPORTANT: Any keyboard shortcuts added here should also be added to show_menu().
    //
    // Three are deliberately absent from the menu and should stay that way: 'q' and 'Q' drive the
    // dev dial, which is a development aid the end user has no business finding, and ':' is a
    // placeholder for the unimplemented vm_model view mode.

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
        case 'k': label_galaxies = !label_galaxies; break;
        case 'K': show_galaxy_band = !show_galaxy_band; break;
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
            viewer_tz  = selected_locale->tz;
            viewer_dst = selected_locale->dst_rule;
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
        case 'q': dev_dial *= (1.0 + dev_dial_step); viewchanged=true; break;
        case 'Q': dev_dial *= (1.0 - dev_dial_step); viewchanged=true; break;

        case 'r':
        velocity = center;
        whtbkgd = false;
        zoom = 1;
        spin = 0;
        whereami = iamhome;
        trackidx = -1;
        view_mode = vm_spaceship;
        vplane_mode = vplane_local;
        viewer_lat = viewer_home_lat;
        viewer_lon = viewer_home_lon;
        viewer_tz  = viewer_home_tz;
        viewer_dst = viewer_home_dst;
        viewer_locale = "";
        save_viewer_latlon = true;
        set_viewer_location_and_plane();
        global_brightness = default_brightness;
        global_gamma = viewer_gamma;
        neighb_rthresh = 25 * light_year;
        show_consln = show_grid = show_labels = lbl_localsys = show_localsys = show_sats = statuswnd = objinfwnd = label_galaxies = show_galaxy_band = true;
        show_orbits = false;
        cbolbls_selected_idx = lbltype_brightest;
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

        case 'U':
        save_viewer_latlon = true;
        save_user_json();
        break;

        case 'v': cbolbls_selected_idx = lbltype_intrinsic; show_labels = true; break;
        case 'V': show_localsys = !show_localsys; viewchanged = true; break;

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
        case '1': show_consln = show_grid = show_labels = lbl_localsys = statuswnd = objinfwnd = show_localsys = label_galaxies = true; break;
        case '2': cbolbls_selected_idx = lbltype_binary; show_labels = true; break;

        case '3':
        themes_selected_idx++;
        if ((unsigned)themes_selected_idx >= themes.size()) themes_selected_idx = 0;
        global_style.load(themes[themes_selected_idx]);
        apply_default_style();
        break;

        case '#':
        themes_selected_idx--;
        if (themes_selected_idx < 0) themes_selected_idx = themes.size()-1;
        global_style.load(themes[themes_selected_idx]);
        apply_default_style();
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
                if (view_mode == vm_spaceship || view_mode == vm_skymap)
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
        case ';': cometwnd = !cometwnd; break;
        case ',': frames_without_mousemove = 1000; break;
        case '|': show_axes = !show_axes; break;
        case '!': show_consln = show_grid = show_labels = lbl_localsys = show_orbits = label_galaxies = false; break;
        case '%':
        zoom = 1;
        global_brightness = 1;
        viewchanged = true;
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

        case '_':
        if (whereami >= 0)
        {
            cel_obj_class cls = cels[whereami]->typeclass();
            // TODO: The below if block allows standing on the surfaces of planets, moons, asteroids, and KBOs.
            // It is a goal to allow standing on a comet, but this will involve significant changes to the code
            // that currently relies on cels[whereami] to be an instance of Planet or a derived class. Attempting
            // with the code as-is causes a segfault. Further, a comet's coma is a greenish haze, and we are going
            // to want that haze to be displayed either as an atmosphere in draw_sky_gradient() or as a GPU effect.
            // So comet surface standing is turned off for the time being.
            if (cls == class_planet || cls == class_moon)
            {
                view_mode = vm_horizon;
                viewchanged = true;
                altitude = 0;
            }
        }
        break;

        case '$':
        if (whereami >= 0)
        {
            cel_obj_class cls = cels[whereami]->typeclass();
            // Galaxies, comets, and satellites do not currently have texture map behavior defined so there's nothing
            // to show in a sun clock, and trying to show a galactic sun clock results in a crash.
            if (cls == class_star || cls == class_planet || cls == class_moon)
            {
                view_mode = vm_sunclock;
                zoom=1;
                altitude=0;
                azimuth=0;
                viewchanged = true;
            }
        }
        break;
    
        case '&': view_mode = vm_spaceship; viewer_lat = viewer_home_lat; viewer_lon = viewer_home_lon; viewer_tz = viewer_home_tz; save_viewer_latlon = viewchanged = true; break;
        case '\\': view_mode = vm_skymap; zoom=1; altitude=0; azimuth=0; break;
        case ':': /* view_mode = vm_model; */ break;                 // not yet implemented but want to keep the placeholder

        default:
        ;
    }
}

void process_key_cmd_ctrl_char(char c)
{
    // Ctrl+letter combinations to avoid, since they are claimed by the terminal, the OS,
    // or common tools the app may be run alongside:
    //   Ctrl+C - SIGINT (terminal interrupt)
    //   Ctrl+D - EOF / terminal exit
    //   Ctrl+Z - SIGTSTP (terminal suspend)
    //   Ctrl+\ - SIGQUIT (terminal quit)
    //   Ctrl+Q / Ctrl+S - terminal XON/XOFF flow control
    //   Ctrl+A / Ctrl+B - tmux/screen prefix keys
    //   Ctrl+V - system paste
    //   Ctrl+W - window close (most OSes)
    switch (c)
    {
        case 'E': vplane_mode = vplane_ecliptic; break;
        case 'G': vplane_mode = vplane_galactic; break;
        case 'I': vplane_mode = vplane_ICRF; break;
        case 'L': vplane_mode = vplane_local; break;
        case 'T': show_terrain = !show_terrain; viewchanged = true; break;
        case 'W': done = true; break;

        default:
        ;
    }
}

double steering_rate, walk_speed;
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

    steering_rate = _pi/16/zoom;
    walk_speed = 4 * frame_dur;                              // a fast run
    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow) && !is_mouse_over_window) process_key_arrowleft();
    if (ImGui::IsKeyDown(ImGuiKey_RightArrow) && !is_mouse_over_window) process_key_arrowright();
    if (ImGui::IsKeyDown(ImGuiKey_UpArrow) && !is_mouse_over_window) process_key_arrowup();
    if (ImGui::IsKeyDown(ImGuiKey_DownArrow) && !is_mouse_over_window) process_key_arrowdn();
    if (ImGui::IsKeyDown(ImGuiKey_Delete) && !is_mouse_over_window) process_key_delete();
    if (ImGui::IsKeyDown(ImGuiKey_End) && !is_mouse_over_window) process_key_end();
    if (ImGui::IsKeyDown(ImGuiKey_Home) && !is_mouse_over_window) process_key_home();
    if (ImGui::IsKeyPressed(ImGuiKey_F2)) process_key_F2();
    if (ImGui::IsKeyPressed(ImGuiKey_F3)) process_key_F3();
    if (ImGui::IsKeyPressed(ImGuiKey_F4)) process_key_F4();
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) process_key_F5();
    if (ImGui::IsKeyPressed(ImGuiKey_F6)) process_key_F6();
    if (ImGui::IsKeyPressed(ImGuiKey_F12)) process_key_F12();

    if (io.KeyCtrl)
    {
        for (i = 0; i < 26; i++)
        {
            if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_A + i))) process_key_cmd_ctrl_char('A' + i);
        }
    }
}

void steer(Point axis, double sr)
{
    #if 1
    velocity = rotate3D(velocity, center, axis, -sr);
    #else
    double vel = velocity.magnitude();
    Point facing = Point::from_ra_dec(azimuth, altitude, vel, myeq);
    facing = to_viewer_plane(facing, 1);
    velocity += rotate3D(facing, center, axis, -sr);
    velocity.scale(vel);
    #endif
}

void process_key_arrowup()
{
    if (!ImGui::IsKeyDown(ImGuiKey_End) && !ImGui::IsKeyDown(ImGuiKey_Home))
    {
        if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) steering_rate *= 0.1;
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) steering_rate *= 0.01;
    }
    Point pitch = to_viewer_plane(xaxis, -1);
    steer(pitch, -steering_rate);
    if (trackidx<0) altitude += steering_rate;
    if (altitude > half_pi) altitude = half_pi;
    enforce_y_pan_limit();
}

void process_key_arrowdn()
{
    if (!ImGui::IsKeyDown(ImGuiKey_End) && !ImGui::IsKeyDown(ImGuiKey_Home))
    {
        if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) steering_rate *= 0.1;
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) steering_rate *= 0.01;
    }
    Point pitch = to_viewer_plane(xaxis, -1);
    steer(pitch, steering_rate);
    if (trackidx<0) altitude -= steering_rate;
    if (altitude < -half_pi) altitude = -half_pi;
    enforce_y_pan_limit();
}

void process_key_arrowleft()
{
    if (!ImGui::IsKeyDown(ImGuiKey_End) && !ImGui::IsKeyDown(ImGuiKey_Home))
    {
        if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) steering_rate *= 0.1;
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) steering_rate *= 0.01;
    }
    Point yaw = to_viewer_plane(yaxis, -1);
    steer(yaw, steering_rate);
    if (trackidx<0) azimuth -= steering_rate;
}

void process_key_arrowright()
{
    if (!ImGui::IsKeyDown(ImGuiKey_End) && !ImGui::IsKeyDown(ImGuiKey_Home))
    {
        if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) steering_rate *= 0.1;
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) steering_rate *= 0.01;
    }
    Point yaw = to_viewer_plane(yaxis, -1);
    steer(yaw, -steering_rate);
    if (trackidx<0) azimuth += steering_rate;
}

void process_key_delete()
{
    // No deleting catalog objects, only objects added by the user, whether fictional objects or satellites.
    // We use >0 rather than >=0 because you cannot delete the Sun; too many things depend on its presence.
    if (selected > 0) cels[selected]->deleted = (cels[selected]->user_added || cels[selected]->type == artificial);
    else if (trackidx > 0) cels[trackidx]->deleted = (cels[trackidx]->user_added || cels[trackidx]->type == artificial);
}

void process_key_home()
{
        double vmag;
        // whereami >= 0 as well as the view mode: walking is only meaningful with a world
        // underfoot, and 'w' (warp) sets whereami to -1 without changing view_mode, so horizon
        // mode alone does not guarantee one. See find_horizon() in visuals.cpp for the same test.
        if (view_mode == vm_horizon && whereami >= 0 && cels[whereami])
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

void process_key_end()
{
        double vmag;
        // See process_key_home(): same test, same reason.
        if (view_mode == vm_horizon && whereami >= 0 && cels[whereami])
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

void process_key_F1()
{
}

void process_key_F2()
{
    menu = !menu;
}

void process_key_F3()
{
        focus_findbox = true;
        statuswnd = true;
        lookfor[0] = 0;
}

void process_key_F4()
{
    IGFD::FileDialogConfig config;
    config.path = ".";
    ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".json", config);
    fdlg_shown = true;
}

void process_key_F5()
{
    splash = true;
    std::thread t1(reload_stuff);
    t1.detach();
}

void process_key_F6()
{
    IGFD::FileDialogConfig config;
    config.path = ".";
    ImGuiFileDialog::Instance()->OpenDialog("ImportSscDlgKey", "Import SSC Add-On", ".ssc", config);
    fdlg_shown = true;
}

void process_key_F7()
{
}

void process_key_F8()
{
}

void process_key_F9()
{
}

void process_key_F10()
{
}

void process_key_F11()
{
    SDL_SetWindowFullscreen(window, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    fullscreen = !fullscreen;
}

void process_key_F12()
{
    take_snapshot = true;
}

void do_find()
{
    int i = find_object(lookfor, false, 9e+29, 6);
    if (i>=0)
    {
        if (view_mode == vm_sunclock) view_mode = vm_spaceship;
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
