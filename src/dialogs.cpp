
// shlwapi.h must be included before any project header pulls in `using namespace std`
// (see globals.h) -- shlwapi.h's own transitive COM headers declare an unqualified
// `byte` typedef that becomes ambiguous against std::byte once that using-directive
// is active.
#ifdef __MINGW32__
#include <shlwapi.h>
#define strcasestr StrStrI
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>
#endif

#include "dialogs.h"
#include "inputs.h"
#include "loaders.h"
#include "housekeeping.h"
#include <cstdint>
#include <algorithm>

using namespace alienorum;

// US DST rule: 02:00 local standard time on the second Sunday in March through
// 02:00 local standard time on the first Sunday in November. Uses Sakamoto's
// algorithm for day-of-week instead of timegm()/mktime(), since the latter are
// not portably available (MinGW lacks timegm) and behave inconsistently across
// platforms for pre-epoch or far-future years.
static bool in_us_dst_window(const struct tm &std_local_time)
{
    int year = std_local_time.tm_year + 1900;
    int mon = std_local_time.tm_mon + 1;
    int mday = std_local_time.tm_mday;

    auto day_of_week = [](int y, int m, int d) -> int
    {
        static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        if (m < 3) y -= 1;
        return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
    };

    int mar_first_wday = day_of_week(year, 3, 1);
    int mar_second_sunday = 1 + (7 - mar_first_wday) % 7 + 7;

    int nov_first_wday = day_of_week(year, 11, 1);
    int nov_first_sunday = 1 + (7 - nov_first_wday) % 7;

    auto before = [](int m1, int d1, int h1, int mi1, int s1, int m2, int d2, int h2, int mi2, int s2) -> bool
    {
        if (m1 != m2) return m1 < m2;
        if (d1 != d2) return d1 < d2;
        if (h1 != h2) return h1 < h2;
        if (mi1 != mi2) return mi1 < mi2;
        return s1 < s2;
    };

    bool at_or_after_start = !before(mon, mday, std_local_time.tm_hour, std_local_time.tm_min, std_local_time.tm_sec,
                                      3, mar_second_sunday, 2, 0, 0);
    bool before_end = before(mon, mday, std_local_time.tm_hour, std_local_time.tm_min, std_local_time.tm_sec,
                              11, nov_first_sunday, 2, 0, 0);

    return at_or_after_start && before_end;
}

void draw_status_window(ImGuiIO& io)            // the S panel
{
    if (!cels[1]) return;
    int stattop = menu ? menu_ht : 0, statleft = 0;
    ImGui::Begin("Status", &statuswnd, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    /////////////////////////////////////////////////////

    int styles_to_pop = 0;
    if (lookfor_notfound)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
        styles_to_pop++;
    }
    if (focus_findbox)
    {
        ImGui::SetKeyboardFocusHere();
        focus_findbox = false;
    }
    if (ImGui::InputText("##find", lookfor, name_max_len, ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_EnterReturnsTrue, lookfor_cb)) do_find();
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        const char* clip = ImGui::GetClipboardText();
        if (clip)
        {
            size_t i = 0;
            // Names never span lines, so stop at the first newline rather than pasting garbage.
            while (i < name_max_len - 1 && clip[i] && clip[i] != '\n' && clip[i] != '\r')
            {
                lookfor[i] = clip[i];
                i++;
            }
            lookfor[i] = 0;
            lookfor_notfound = false;
            focus_findbox = true;
        }
    }
    if (styles_to_pop) ImGui::PopStyleColor(styles_to_pop);
    ImGui::SameLine();
    if (ImGui::Button("Find")) do_find();

    std::string flagstr;

    flagstr = (std::string)"Zoom (*/): " + std::to_string(zoom);
    ImGui::Text("%s", flagstr.c_str());

    flagstr = (std::string)"Brghtns (B): " + std::to_string(global_brightness);
    ImGui::Text("%s", flagstr.c_str());

    flagstr = (std::string)"Gamma (`): " + std::to_string(get_gamma());
    ImGui::Text("%s", flagstr.c_str());

    ImGui::Separator();

    flagstr = (std::string)"Redlgt (Sh+R): "
        + std::string(redlight_mode ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());

    flagstr = (std::string)"Obj info (N): "
        + std::string(objinfwnd ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());

    flagstr = (std::string)"Status (S): "
        + std::string(statuswnd ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());

    flagstr = (std::string)"RA/Decl (G): "
        + std::string(show_grid ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());

    flagstr = (std::string)"Cons ln (C): "
        + std::string(show_consln ? (draw_actual_conslines ? "ON" : "(hidden)") : "OFF");
    ImGui::Text("%s", flagstr.c_str());

    flagstr = (std::string)"Orbits (Sh+O): "
        + std::string(show_orbits ? (show_localsys ? "ON" : "(hidden)") : "OFF");
    ImGui::Text("%s", flagstr.c_str());

    if (nsatobjs)
    {
        flagstr = (std::string)"Satellites (J): "
            + std::string(show_sats ? "ON" : "OFF");
        ImGui::Text("%s", flagstr.c_str());
    }

    ImGui::Separator();

    flagstr = (std::string)"Labels (L): "
        + std::string(show_labels ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());

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

    flagstr = (std::string)"Galaxy labels (K): "
        + std::string(label_galaxies ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());

    flagstr = (std::string)"Galaxy band (Sh+K): "
        + std::string(show_galaxy_band ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());

    if (cbolbls_selected_idx == lbltype_brightest)
    {
        ImGui::Text("%s", "Mag limit:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(67);
        ImGui::InputDouble("##appmaglim", &appmagn_lblcut, 0, 0, "%.2f");
    }
    else if (cbolbls_selected_idx == lbltype_intrinsic)
    {
        ImGui::Text("%s", "Mag limit:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(67);
        ImGui::InputDouble("##absmaglim", &absmagn_lblcut, 0, 0, "%.2f");
    }
    else if (cbolbls_selected_idx == lbltype_nearby)
    {
        // Held in metres, shown in light years, and still Claude is not PTSD friendly.
        double dist_ly = distance_lblcut / light_year;
        ImGui::Text("%s", "Dist. l.y.:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(67);
        if (ImGui::InputDouble("##distlim", &dist_ly, 0, 0, "%.2f"))
            distance_lblcut = dist_ly * light_year;
    }
    else if (cbolbls_selected_idx == lbltype_planets)
    {
        ImGui::Text("%s", "# Planets:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(67);
        ImGui::InputInt("##npltlim", &planets_lblcut, 1, 0);
        if (planets_lblcut < 1) planets_lblcut = 1;
    }

    flagstr = (std::string)"Lbl planets (P): "
        + std::string(lbl_localsys ? "ON" : "OFF");
    ImGui::Text("%s", flagstr.c_str());

    if (lbl_localsys)
    {
        ImGui::Text("Mass limit:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(81);
        ImGui::InputDouble("##lbllsysmasslim", &lbllsys_mass_lim, 0, 0, "%.2e");
        ImGui::SameLine();
        ImGui::Text("kg");
    }

    ImGui::Separator();

    std::string vfstr;
    if (whereami >= 0)
    {
        if (viewer_locale.size()) vfstr = std::string("View from ") + viewer_locale;
        else vfstr = std::string("View from ") + cels[whereami]->name;
    }
    else vfstr = std::string("View from space");
    ImGui::Text("%s", vfstr.c_str());

    if (whereami > 0 && cels[whereami]->type >= gas_giant && cels[whereami]->type < artificial
        && ((Planet*)cels[whereami])->is_in_con_HZ())
    {
        ImVec4 hzcolor = redlight_mode ? ImVec4(1, 0, 0, 1) : ImVec4(0, 1, 0, 1);
        ImGui::TextColored(hzcolor, "In Habitable Zone");
    }

    ImGui::Text("Mag limit: %.1f", ((view_mode == vm_horizon) ? normal_best_mag_limit : mag_limit_adjusted) + sky_mag_shift);

    ImGui::Separator();

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

    int dstadd = 0;
    if (viewer_dst)
    {
        time_t std_local = simnow + (time_t)viewer_tz;
        struct tm std_local_tm = *std::gmtime(&std_local);
        if (in_us_dst_window(std_local_tm)) dstadd = 3600;
    }
    double eff_tz = viewer_tz + dstadd;
    time_t tmpnow = simnow + eff_tz;

    if (fabs(eff_tz) > 0.5 || viewer_dst)
    {
        struct tm *local_time = std::gmtime(&tmpnow);

        int mon = local_time->tm_mon + 1, mday = local_time->tm_mday;
        std::string datedisp = std::to_string(local_time->tm_year + 1900)
            + std::string("-") + std::string((mon<10)?"0":"") + std::to_string(mon)
            + std::string("-") + std::string((mday<10)?"0":"") + std::to_string(mday);

        int hr = local_time->tm_hour, mn = local_time->tm_min, sec = local_time->tm_sec;

        int tzsgn = (eff_tz < 0) ? -1 : 1;
        int tzhr = floor(fabs(eff_tz) / 3600);
        int tzmin = floor(fmod(fabs(eff_tz), 3600) / 60);

        std::string timedisp = std::string((hr<10)?"0":"") + std::to_string(hr)
            + std::string(":") + std::string((mn<10)?"0":"") + std::to_string(mn)
            + std::string(":") + std::string((sec<10)?"0":"") + std::to_string(sec)
            + std::string(" ")
            + ((tzsgn < 0) ? std::string("-") : std::string("+"))
            + ((tzhr < 10) ? std::string("0") : std::string(""))
            + std::to_string(tzhr)
            + std::string(":")
            + ((tzmin < 10) ? std::string("0") : std::string(""))
            + std::to_string(tzmin);
        ImGui::Text("%s %s", datedisp.c_str(), timedisp.c_str());
    }
    else
    {
        struct tm *utc_time = std::gmtime(&tmpnow);

        int mon = utc_time->tm_mon + 1, mday = utc_time->tm_mday;
        std::string datedisp = std::to_string(utc_time->tm_year + 1900)
            + std::string("-") + std::string((mon<10)?"0":"") + std::to_string(mon)
            + std::string("-") + std::string((mday<10)?"0":"") + std::to_string(mday);

        int hr = utc_time->tm_hour, mn = utc_time->tm_min, sec = utc_time->tm_sec;
        std::string timedisp = std::string((hr<10)?"0":"") + std::to_string(hr)
            + std::string(":") + std::string((mn<10)?"0":"") + std::to_string(mn)
            + std::string(":") + std::string((sec<10)?"0":"") + std::to_string(sec)
            + std::string(" UTC");
        ImGui::Text("%s %s", datedisp.c_str(), timedisp.c_str());
    }

    ImGui::Separator();

    std::string numobjs;
    if (num_stars)
    {
        numobjs = std::to_string(num_stars) + " stars";
        ImGui::Text("%s", numobjs.c_str());
    }
    if (num_stars_in_box>1)             // There will always be at least one.
    {
        numobjs = std::to_string(num_stars_in_box) + " stars in range";
        ImGui::Text("%s", numobjs.c_str());
    }
    /* if (num_planets)
    {
        numobjs = std::to_string(num_planets) + " planets";
        ImGui::Text("%s", numobjs.c_str());
    } */

    std::string JDdisp = std::string("JD") + std::to_string(JDnow);
    ImGui::Text("%s", JDdisp.c_str());

    float frame_rate = 1.0 / frame_dur;
    ImGui::Text("%.1f frames/s", frame_rate);

    if (show_dev_dial)
    {
        ImGui::Text("Dev Dial: %.5f", dev_dial);
    }

    ImGui::Separator();

    if (whereami >= 0 && cels[whereami]->typeclass() != class_satellite)
    {
        ImGuiComboFlags cbovm_flags = 0;
        const char* combo_vm_value = vmtext[view_mode];
        ImGui::Text("%s", "View Mode:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(123);
        if (ImGui::BeginCombo("##cbovm", combo_vm_value, cbovm_flags))
        {
            for (int n = 0; n < NUM_VIEWMODES; n++)
            {
                const bool is_selected = (n == view_mode);
                if (ImGui::Selectable(vmtext[n], is_selected))
                {
                    view_mode = (ViewMode)n;
                    set_viewer_location_and_plane();
                    viewchanged = true;
                }

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (n == view_mode)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGuiComboFlags cbovp_flags = 0;
        const char* combo_vp_value = vptext[vplane_mode];
        ImGui::Text("%s", "View Plane:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(123);
        if (ImGui::BeginCombo("##cbovp", combo_vp_value, cbovp_flags))
        {
            for (int n = 0; n < NUM_VPLANES; n++)
            {
                const bool is_selected = (n == vplane_mode);
                if (ImGui::Selectable(vptext[n], is_selected))
                {
                    vplane_mode = (ViewerPlaneMode)n;
                    set_viewer_location_and_plane();
                    viewchanged = true;
                }

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (n == vplane_mode)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (view_mode == vm_horizon)
        {
            stringstream sssg;
            double f = cels[whereami]->estimate_surface_gravity();
            sssg << "Est. gravity: " << (f >= 0.01 ? std::fixed : std::scientific) << std::setprecision(3) << f << " g";
            ImGui::Text("%s", sssg.str().c_str());
            sssg.str("");
            sssg << "Est. temp.:   " << std::fixed << std::setprecision(1) << ((Planet*)cels[whereami])->estimate_surface_temperature() << " K";
            ImGui::Text("%s", sssg.str().c_str());
            sssg.str("");
            double bar = ((Planet*)cels[whereami])->get_surface_pressure() / 1e5;
            sssg << "Pressure:     " << std::fixed << std::setprecision(bar<1 ? 5 : 2) << bar << " bar";
            ImGui::Text("%s", sssg.str().c_str());

            CelestialObject *lcen = cels[whereami]->get_light_center();
            double lightalt = lcen->Decl_as_radians(here)*fiftyseven;
            // std::cout << "Sun at altitude " << (lightalt) << std::endl;
            if (lightalt > 0) ImGui::Text("%s", "Daytime");
            else if (whereami == iamhome && lightalt > -6) ImGui::Text("%s", "Civil Twilight");
            else if (whereami == iamhome && lightalt > -12) ImGui::Text("%s", "Nautical Twilight");
            else if (whereami == iamhome && lightalt > -18) ImGui::Text("%s", "Astronomical Twilight");
            else ImGui::Text("%s", "Nighttime");

            if (1) // save_viewer_latlon)
            {
                double vlat_edit = viewer_lat * fiftyseven;
                ImGui::Text("%s", "Lat:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(123);
                if (ImGui::InputDouble("##vlat", &vlat_edit, 0.1, 1, "%.3f"))
                {
                    viewer_home_lat = viewer_lat = vlat_edit * fiftyseventh;
                    set_viewer_location_and_plane();
                    viewchanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("...##locwnd"))
                {
                    locwnd = true;
                }

                double vlon_edit = viewer_lon * fiftyseven;
                ImGui::Text("%s", "Lon:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(123);
                if (ImGui::InputDouble("##vlon", &vlon_edit, 0.1, 1, "%.3f"))
                {
                    viewer_home_lon = viewer_lon = vlon_edit * fiftyseventh;
                    set_viewer_location_and_plane();
                    viewchanged = true;
                }
            }
            else
            {
                ImGui::Text("%s %s%.6f", "Lat: ", (viewer_lat >= 0) ? "+" : "", viewer_lat*fiftyseven );
                ImGui::Text("%s %s%.6f", "Lon:", (viewer_lon >= 0) ? "+" : "", viewer_lon*fiftyseven );
            }
        }
    }

    int th = themes.size();
    if (themes_selected_idx < 0)
    {
        for (int n = 0; n < th; n++)
        {
            if (!strcmp(themes[n].c_str(), viewer_theme.c_str()))
            {
                themes_selected_idx = n;
                break;
            }
        }
    }
    if (themes_selected_idx < 0)
    {
        for (int n = 0; n < th; n++)
        {
            if (!strcmp(themes[n].c_str(), default_theme.c_str()))
            {
                themes_selected_idx = n;
                break;
            }
        }
    }

    ImGuiComboFlags cbothemes_flags = 0;
    const char* combo_theme_value = themes[themes_selected_idx].c_str();
    ImGui::Text("%s", "Theme:");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##cbothemes", combo_theme_value, cbothemes_flags))
    {
        for (int n = 0; n < th; n++)
        {
            const bool is_selected = (n == themes_selected_idx);
            if (ImGui::Selectable(themes[n].c_str(), is_selected))
            {
                themes_selected_idx = n;
                viewer_theme = themes[n];
                global_style.load(themes[n]);
                apply_default_style();
                save_user_json();
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (n == themes_selected_idx)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    /////////////////////////////////////////////////////

    ImGui::SetWindowPos(ImVec2(statleft, stattop));
    ImGui::SetWindowSize(ImVec2(0, 0));
    ImVec2 siz = ImGui::GetWindowSize();
    ImGui::End();

    if (io.MousePos.x >= statleft && io.MousePos.y >= stattop
        && io.MousePos.x < statleft+siz.x && io.MousePos.y < stattop+siz.y)
        is_mouse_over_window = true;
}

void draw_objinf_window(ImGuiIO& io)                // the N panel
{
    if (!cels[1]) return;
    ImGui::Begin("Object", &objinfwnd, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    if (trackidx >= 0)
    {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "TRACKING");
    }

    if (is_an_obj_under_cursor >= 0)
    {
        int i = is_an_obj_under_cursor;
        double lmag = vmag_cache[i];
        bool am_satellite = (whereami>0) && (cels[whereami]->type == artificial);
        bool sat_low_orbit = am_satellite && (cels[i]->tmprel.magnitude() < cels[i]->volumetric_mean_radius*2);

        std::stringstream oss;

        objname = cels[i]->name;
        ImGui::Text("%s", objname.c_str());
        ImGui::Separator();

        if (cels[i]->type == star)
        {
            Star* s = (Star*)cels[i];
            if (s->alienorumid.size()) ImGui::Text("%s", s->alienorumid.c_str());
            if (strlen(s->Bayer) && strlen(s->Flamsteed))
            {
                int Fl = atoi(s->Flamsteed);
                ImGui::Text("%s", (std::to_string(Fl) + (std::string)s->Bayer).c_str());
            }
            else if (strlen(s->Flamsteed)) ImGui::Text("%s", s->Flamsteed);
            else if (strlen(s->Bayer)) ImGui::Text("%s", s->Bayer);
            if (s->GouldNo > 0) ImGui::Text("%s", (std::to_string(s->GouldNo) + std::string(" G. ") + std::string(s->constellation)).c_str());

            if (strlen(s->Gliese)) ImGui::Text("%s", s->Gliese);
            if (s->HD) ImGui::Text("%s", ((std::string)"HD" + std::to_string(s->HD)).c_str());
            if (s->HR) ImGui::Text("%s", ((std::string)"HR" + std::to_string(s->HR)).c_str());
            if (s->HIP) ImGui::Text("%s", ((std::string)"HIP" + std::to_string(s->HIP)).c_str());
            if (s->WD.size()) ImGui::Text("%s", s->WD.c_str());
            if (s->Bonn_survey[0])
            {
                char BD[3] = {s->Bonn_survey[0],s->Bonn_survey[1],0};
                std::string bdstr = std::string(BD) + (s->Bonn_survey_declination > 0 ? std::string(1, s->Bonn_survey_sign) : std::string(""))
                    + std::to_string(s->Bonn_survey_declination) + std::string(" ") + std::to_string(s->Bonn_survey_sequential);
                ImGui::Text("%s", bdstr.c_str());
            }
            ImGui::Separator();
        }

        myeq = (whereami >= 0) ? cels[whereami]->equinox_RA : 0;
        if (view_mode == vm_spaceship || view_mode == vm_skymap)
        {
            ImGui::Text("RA:       %s", cels[i]->RA_as_hms(here, myeq).c_str());
            ImGui::Text("Decl:     %s", cels[i]->Decl_as_degms(here).c_str());
        }
        else if (view_mode == vm_sunclock)
        {
            double lat = cels[i]->Decl_as_radians(here), lon = cels[i]->RA_as_radians(here, cels[whereami]->timeofday());
            if (lon > _pi) lon -= _pi*2;
            ImGui::Text("Lat:      %s", std::to_string(lat * fiftyseven).c_str());
            ImGui::Text("Lon:      %s", std::to_string(lon * fiftyseven).c_str());
        }
        else
        {
            npaz = fmod(npdummy.RA_as_radians(here, 0), _pi*2);
            double objaz = fmod(npaz - cels[i]->RA_as_radians(here, 0), _pi*2);
            if (objaz < 0) objaz += _pi*2;
            double shown_alt = cels[i]->Decl_as_radians_refracted(here);
            if (view_mode == vm_horizon)
            {
                shown_alt = cels[i]->Decl_as_radians(here);
            }
            ImGui::Text("Altitude: %s", std::to_string(shown_alt*fiftyseven).c_str());
            ImGui::Text("Azimuth:  %s", std::to_string(objaz*fiftyseven).c_str());
        }
        if (!sat_low_orbit && cels[i]->typeclass() != class_satellite)
        {
            oss << "Mag:      " << std::setprecision(4) << lmag;
            ImGui::Text("%s", oss.str().c_str());
            oss.str("");
            oss.clear();
        }

        ImGui::Separator();
        if (cels[i]->type == star)
        {
            Star* s = (Star*)cels[i];
            if (s->distance_known || s->cenobj == mycenobj)
            {
                oss << "Dist:     " << cels[i]->scaled_distance(here, sat_low_orbit);
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
                oss << "AbsMag:   " << std::setprecision(4) << s->absolute_magnitude;
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
            }
            ImGui::Text("SpTyp:    %s", s->spectral_type);
        }
        else if (cels[i]->type == galaxy)
        {
            // TODO:
        }
        else if (cels[i]->type == artificial)
        {
            if (view_mode == vm_sunclock && whereami >= 0)
                oss << "Alt:      "
                    << std::fixed << std::setprecision(3)
                    << ((cels[i]->location.distance_to(here) - cels[whereami]->volumetric_mean_radius) / 1000)  // TODO: Compensate for oblateness.
                    << " km";
            else oss << "Dist:     " << cels[i]->scaled_distance(here);
            ImGui::Text("%s", oss.str().c_str());
            oss.str("");
            oss.clear();
        }
        else if (cels[i]->typeclass() == class_comet)
        {
            Comet *cm = (Comet*)cels[i];
            oss << "Dist:     " << cels[i]->scaled_distance(here);
            ImGui::Text("%s", oss.str().c_str());
            oss.str("");
            oss.clear();
            if (cm->orbit)
            {
                double q = cm->orbit->periapsis_distance;
                if (!q) q = cm->orbit->semimajor_axis * fabs(1.0 - cm->orbit->eccentricity);
                oss << "Perih.:   " << std::setprecision(4) << (q / AU) << " AU";
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
                oss << "Ecc.:     " << std::setprecision(6) << cm->orbit->eccentricity;
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
                if (cm->orbit->period)
                {
                    oss << "Period:   " << std::setprecision(2) << (cm->orbit->period / oneyear) << " yr";
                    ImGui::Text("%s", oss.str().c_str());
                    oss.str("");
                    oss.clear();
                }
                else ImGui::Text("          Unbound; will not return");
            }
        }
        else
        {
            oss << "Dist:     " << cels[i]->scaled_distance(here, sat_low_orbit);
            ImGui::Text("%s", oss.str().c_str());
            oss.str("");
            oss.clear();
            if (!sat_low_orbit)
            {
                oss << "Lit %:    " << std::setprecision(1) << ((int)(((Planet*)cels[i])->amt_lit*100));
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
            }
            if (((Planet*)cels[i])->is_in_con_HZ())
            {
                ImVec4 hzcolor = redlight_mode ? ImVec4(1, 0, 0, 1) : ImVec4(0, 1, 0, 1);
                ImGui::TextColored(hzcolor, "          Habitable Zone");
            }
        }

        cel_obj_class cls = cels[i]->typeclass();
        if (cels[i]->mass)
        {
            if (cls == class_star)
                ; // oss << "Mass:  " << std::setprecision(2) << (cels[i]->mass / solar_mass) << " M(sun)";       // TODO: Fix Star::estimate_mass()
            else if (cls == class_planet || cls == class_moon)
            {
                oss << "Mass:     " << std::setprecision(2) << (cels[i]->mass / cels[iamhome]->mass) << " M(earth)";
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
                oss << "          " << std::scientific << std::setprecision(2) << (cels[i]->mass / 1000) << " kg";
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
            }
        }
        if (cels[i]->volumetric_mean_radius)
        {
            if (cls == class_star)
                ; // oss << "Radius: " << std::setprecision(2) << (cels[i]->volumetric_mean_radius / solar_radius) << " R(sun)";       // TODO: Fix Star::estimate_radius()
            else if (cls == class_planet || cls == class_moon)
            {
                oss << "Radius:   " << std::setprecision(2) << (cels[i]->volumetric_mean_radius / cels[iamhome]->volumetric_mean_radius)
                    << " R(earth)";
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
                oss << "          " << std::scientific << std::setprecision(2) << (cels[i]->volumetric_mean_radius / 1000) << " km";
                ImGui::Text("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
            }
        }

        #ifdef DEBUG
        ImGui::Text("index:    %d", is_an_obj_under_cursor);
        #endif
    }
    else if (is_a_locale_under_cursor)
    {
        objname = is_a_locale_under_cursor->name;
        ImGui::Text("%s", objname.c_str());
        ImGui::Text("Lat.: %s%.3f", (is_a_locale_under_cursor->lat < 0 ? "" : "+"), is_a_locale_under_cursor->lat);
        ImGui::Text("Lon.: %s%.3f", (is_a_locale_under_cursor->lon < 0 ? "" : "+"), is_a_locale_under_cursor->lon);
    }
    else
    {
        objname = "Press N to toggle\nthis window.";
        ImGui::Text("%s", objname.c_str());
        if (is_click && !dragged) selected = -1;
    }

    ImGui::SetWindowSize(ImVec2(0, 0));
    ImVec2 siz = ImGui::GetWindowSize();
    int objinfwidth = siz.x, objinfheight = siz.y, objinftop = menu ? menu_ht : 0, objinfleft = (int)io.DisplaySize.x - objinfwidth;
    ImGui::SetWindowPos(ImVec2(objinfleft, objinftop));
    ImGui::End();

    if (io.MousePos.x >= objinfleft && io.MousePos.y >= objinftop
        && io.MousePos.x < objinfleft+objinfwidth && io.MousePos.y < objinftop+objinfheight)
        is_mouse_over_window = true;
}

void draw_addcel_window(ImGuiIO& io)
{
    if (!cels[1]) return;
    ImGui::Begin("Add Object", &addcelwnd);

    ImGui::Text("New object orbiting %s", cels[addcenidx]->name);

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

    if (cboceltyp_selected_idx == 4)
        ImGui::Text("Note: Use this to add custom or fictitious objects.\nTo add real satellites (such as ISS, GPS, Iridium,\nHubble, etc.) use the ^ command instead.");

    if (ImGui::Button("Go"))
    {
        int i;
        for (i=0; cels[i]; i++);
        ncelobjs = i;
        if (ncelobjs < (MAX_CELOBJS-1))
        {
            cel_obj_class cls = cels[addcenidx]->typeclass();
            bool is_cen_planet = (cls == class_planet || cls == class_moon);

            if (cboceltyp_selected_idx == 2 && is_cen_planet) cboceltyp_selected_idx = 3;
            else if (cboceltyp_selected_idx == 3 && !is_cen_planet) cboceltyp_selected_idx = 2;

            CelestialObject *cel = nullptr;
            switch (cboceltyp_selected_idx)
            {
                case 0: cel = new Galaxy();     cel->type = galaxy;        break;
                case 1: cel = new Star();       cel->type = star;          break;
                case 2: cel = new Planet();     cel->type = rocky;         break;
                case 3:
                    cel = new Moon();
                    cel->type = rocky;
                    ((Moon*)cel)->orbit_type = ot_equatorial;
                    break;
                case 4: cel = new Satellite();  cel->type = artificial;    break;

                default:
                std::cerr << "Unimplemented object type" << std::endl;
                break;
            }

            if (cel && !append_cel(cel))
            {
                delete cel;
                cel = nullptr;
            }

            if (cel)
            {
                strcpy(cel->name, "new");
                cel->user_added = true;
                cels[addcenidx]->distance_known = true;
                cel->distance_known = true;
                if (cel->type >= cels[addcenidx]->type)
                {
                    cel->orbit = new Orbit();
                    cel->orbit->center = cels[addcenidx];
                    cel->orbit->semimajor_axis = 1e8;
                    cel->orbit->period = oneday*7;
                    cel->orbit->epoch = JDnow;
                    cel->cenobj = cels[addcenidx]->cenobj;
                }
                if (cel->typeclass() == class_planet || cel->typeclass() == class_moon)
                {
                    Planet* pl = (Planet*)cel;
                    pl->mass = (cel->typeclass() == class_moon) ? lunar_mass : earth_mass;
                    pl->classify(pl->is_in_con_HZ(), true);
                    pl->estimate_radius();
                    pl->estimate_albedo_and_absmagn();
                    pl->estimate_rotation();
                    pl->apply_cosmic_shoreline();
                }
                editidx = ncelobjs-1;
                objedtwnd = true;
                addcelwnd = false;
            }
        }
    }

    ImGui::SetWindowSize(ImVec2(0,0));
    ImVec2 pos = ImGui::GetWindowPos(), siz = ImGui::GetWindowSize();
    ImGui::End();

    if (io.MousePos.x >= pos.x && io.MousePos.y >= pos.y
        && io.MousePos.x < pos.x+siz.x && io.MousePos.y < pos.y+siz.y)
        is_mouse_over_window = true;
}

double rp, ra;
int last_edit_idx = -1;
static int cbo_edt_units = 0;
#define cbo_edt_num_units 4
const char* mass_units[cbo_edt_num_units] = {"kg", "Sun", "Jup.", "Earth"};
const double mass_units_conv[cbo_edt_num_units] = {1000, solar_mass, jupiter_mass, earth_mass};
const char* size_units[cbo_edt_num_units] = {"km", "Sun", "Jup.", "Earth"};
const double size_units_conv[cbo_edt_num_units] = {1000, solar_radius, jupiter_radius, earth_radius};
void draw_objedit_window(ImGuiIO& io)
{
    if (!cels[1]) return;
    if (editidx < 0)
    {
        objedtwnd = false;
        return;
    }

    CelestialObject *cel = cels[editidx];
    Orbit *orb = cel->orbit;

    ImGui::Begin("Edit Object", &objedtwnd, 0);

    double col1 = 123, col2 = 403, col3 = 517, txtwid = 167;
    cel_obj_class tc = cel->typeclass();

    strcpy(edit_name, cel->name);
    ImGui::Text("%s", "Name");
    ImGui::SameLine(col1);
    ImGui::SetNextItemWidth(txtwid*2);
    if (ImGui::InputText("##edtname", edit_name, name_max_len, 0))
    {
        strcpy(cels[editidx]->name, edit_name);
        cel->user_edited = true;
    }
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
    ImGui::SameLine();
    if (ImGui::Button("Track"))
    {
        trackidx = editidx;
        viewchanged = true;
    }

    ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
    if (ImGui::BeginTabBar("##edittabs", tab_bar_flags))
    {
        if (tc != class_satellite && ImGui::BeginTabItem("Bulk"))
        {
            double edit_mass = cel->mass / mass_units_conv[cbo_edt_units];
            ImGui::Text("%s", "Mass");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtmass", &edit_mass, 0, 0, cbo_edt_units ? "%.9f" : "%.9e"))
            {
                cel->mass = edit_mass * mass_units_conv[cbo_edt_units];
                if (cel->typeclass() == class_planet
                    || cel->typeclass() == class_moon               // See Kepler-1625b.
                    ) ((Planet*)cel)->classify(((Planet*)cel)->is_in_con_HZ(), true);
                cel->user_edited = true;
                viewchanged = true;
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(txtwid/2.5);
            if (ImGui::BeginCombo("##cboedtunitsm", mass_units[cbo_edt_units], 0))
            {
                for (int n = 0; n < cbo_edt_num_units; n++)
                {
                    const bool is_selected = (cbo_edt_units == n);
                    if (ImGui::Selectable(mass_units[n], is_selected))
                    {
                        cbo_edt_units = n;
                    }

                    // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine(col2);
            double edit_radius = cel->volumetric_mean_radius / size_units_conv[cbo_edt_units];
            ImGui::Text("%s", "Radius");
            ImGui::SameLine(col3);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtvmrad", &edit_radius, 0, 0, "%.6f"))
            {
                cel->volumetric_mean_radius = edit_radius * size_units_conv[cbo_edt_units];
                assert(!isinf(cel->volumetric_mean_radius));
                if (cel->typeclass() == class_planet
                    || cel->typeclass() == class_moon               // See Kepler-1625b.
                    ) ((Planet*)cel)->classify(((Planet*)cel)->is_in_con_HZ(), true);
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon)
                {
                    Moon *m = (Moon*)cel;
                    m->depth = m->width = m->height = 0;
                    m->update_location(simnow);
                }
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(txtwid/2.5);
            if (ImGui::BeginCombo("##cboedtunitsr", size_units[cbo_edt_units], 0))
            {
                for (int n = 0; n < cbo_edt_num_units; n++)
                {
                    const bool is_selected = (cbo_edt_units == n);
                    if (ImGui::Selectable(size_units[n], is_selected))
                    {
                        cbo_edt_units = n;
                    }

                    // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            stringstream massss;
            massss << "Density " << std::setprecision(3) << cel->density() << " g/cm^3";
            std::string dens = massss.str();
            ImGui::Text("%s", dens.c_str());
            ImGui::SameLine(col2);
            stringstream sssurfgrav;
            sssurfgrav << "Surface gravity " << std::setprecision(3)
                << cel->estimate_surface_gravity()
                << " g";
            ImGui::Text("%s", sssurfgrav.str().c_str());

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
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }
            if (tc == class_planet || tc == class_moon)
            {
                ImGui::SameLine(col2);
                Planet* p = (Planet*)cel;
                p->estimate_albedo();
                ImGui::Text("%s", "Albedo");
                double edit_albedo = p->albedo;
                ImGui::SameLine(col3);
                ImGui::SetNextItemWidth(txtwid);
                if (ImGui::InputDouble("##edtalbdo", &edit_albedo, 0, 0, "%.6f"))
                {
                    p->albedo = edit_albedo;

                    double disc_area = pow(p->volumetric_mean_radius / earth_radius, 2);
                    double absmag = earth_absmag - log(disc_area * p->albedo / earth_albedo) / log(magnbase);
                    if (!isnan(absmag)) p->absolute_magnitude = absmag;
                    if (cel->typeclass() == class_planet || cel->typeclass() == class_moon) ((Planet*)cel)->estimate_surface_temperature();

                    cel->user_edited = true;
                    viewchanged = true;
                    if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                    else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                    else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
                }
            }

            double edit_bvcol = cel->BV_color;
            ImGui::Text("%s", "B-V color");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtbv", &edit_bvcol, 0, 0, "%.3f"))
            {
                cel->BV_color = edit_bvcol;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }

            ImGui::SameLine(col2);
            double edit_ubcol = cel->UB_color;
            ImGui::Text("%s", "U-B color");
            ImGui::SameLine(col3);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtub", &edit_ubcol, 0, 0, "%.3f"))
            {
                cel->UB_color = edit_ubcol;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }

            edit_eqincl = cel->obliquity * fiftyseven;
            if (tc == class_star && !cel->orbit)
                ImGui::Text("%s", "Inclination");
            else ImGui::Text("%s", "Obliquity");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edteqinc", &edit_eqincl, 0, 0, "%.3f"))
            {
                cel->obliquity = edit_eqincl * fiftyseventh;
                cel->lock_equatorial_plane = false;
                cel->known_poles = true;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            if (ImGui::Button("rnd##obliquity"))
            {
                cel->obliquity = frand(0, frand(0, frand(0, _pi)));
                cel->user_edited = true;
            }
            ImGui::SameLine(col2);
            edit_equinox = cel->equinox * fiftyseven;
            if (tc == class_star && !cel->orbit)
                ImGui::Text("%s", "Asc. Node");
            else ImGui::Text("%s", "Equinox");
            ImGui::SameLine(col3);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edteqnox", &edit_equinox, 0, 0, "%.3f"))
            {
                cel->equinox = edit_equinox * fiftyseventh;
                cel->lock_equatorial_plane = false;
                cel->known_poles = true;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            if (ImGui::Button("rnd##equinox"))
            {
                cel->equinox = frand(0, _pi*2);
                cel->user_edited = true;
            }

            double edit_prcseq = cel->precession ? (_pi * 2 / cel->precession / oneyear) : 0;
            ImGui::Text("%s", "Precession");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtprcs", &edit_prcseq, 0, 0, "%.3f"))
            {
                cel->precession = edit_prcseq ? (_pi * 2 / (edit_prcseq * oneyear)) : 0;
                cel->lock_equatorial_plane = false;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            ImGui::Text("%s", "y");

            double edit_oblt = cel->oblateness;
            ImGui::Text("%s", "Oblateness");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtoblt", &edit_oblt, 0, 0, "%.5e"))
            {
                cel->oblateness = edit_oblt;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }
            if (cel->typeclass() == class_moon)
            {
                Moon *m = (Moon*)cel;
                double edt_dep = m->depth * 1e-3, edt_wid = m->width * 1e-3, edt_hei = m->height * 1e-3;
                ImGui::SameLine();
                ImGui::Text("%s", ", OR:");

                ImGui::SameLine(col2);
                ImGui::Text("%s", "D/W/H, km");
                ImGui::SameLine(col3);
                ImGui::SetNextItemWidth(txtwid/3);
                if (ImGui::InputDouble("##edtdep", &edt_dep, 0, 0, "%.4f"))
                {
                    m->depth = edt_dep * 1e3;
                    cel->user_edited = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(txtwid/3);
                if (ImGui::InputDouble("##edtwid", &edt_wid, 0, 0, "%.4f"))
                {
                    m->width = edt_wid * 1e3;
                    cel->user_edited = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(txtwid/3);
                if (ImGui::InputDouble("##edthei", &edt_hei, 0, 0, "%.4f"))
                {
                    m->height = edt_hei * 1e3;
                    cel->user_edited = true;
                }
            }

            double edit_rot = cel->sidereal_rotational_period / oneday;
            ImGui::Text("%s", "Rotation, d");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtrot", &edit_rot, 0, 0, "%.6f"))
            {
                cel->sidereal_rotational_period = edit_rot * oneday;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }
            if (cel->typeclass() == class_planet || cel->typeclass() == class_moon)
            {
                ImGui::SameLine();
                if (ImGui::Button("estimate##edit_rotation"))
                {
                    ((Planet*)cel)->estimate_rotation();
                }
            }
            ImGui::SameLine(col2);
            double edit_lonoff = cel->lon_J2000_offset * fiftyseven;
            ImGui::Text("%s", "Lon. Offset");
            ImGui::SameLine(col3);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtlonoff", &edit_lonoff, 0, 0, "%.3f"))
            {
                cel->lon_J2000_offset = edit_lonoff * fiftyseventh;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }

            ImGui::EndTabItem();
        }
        /* if (ImGui::BeginTabItem("Template"))
        {
            ImGui::EndTabItem();
        } */
        if ((tc == class_star) && ImGui::BeginTabItem("Starstuff"))
        {
            Star* s = (Star*)cels[editidx];

            // snprintf, not strcpy: RA_as_hms() and Decl_as_degms() build their output with
            // std::to_string on values this dialog lets the user set directly, so neither has a
            // length these fixed buffers can rely on -- a declination of a few thousand degrees
            // is enough to run past 20 characters. Sized from the buffer itself throughout, so
            // the widget below and the copy above cannot disagree about it.
            char edit_ra[32];
            snprintf(edit_ra, sizeof(edit_ra), "%s", s->RA_as_hms(0).c_str());
            ImGui::Text("%s", "R.Ascension");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputText("##edtra", edit_ra, sizeof(edit_ra)))
            {
                s->RA_from_hms(edit_ra);
                s->update_location(simnow);
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
            }
            ImGui::SameLine(col2);
            char edit_decl[32];
            snprintf(edit_decl, sizeof(edit_decl), "%s", s->Decl_as_degms().c_str());
            ImGui::Text("%s", "Declination");
            ImGui::SameLine(col3);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputText("##edtdecl", edit_decl, sizeof(edit_decl)))
            {
                s->Decl_from_degms(edit_decl);
                s->update_location(simnow);
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
            }

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

            ImGui::EndTabItem();
        }
        if (orb && ImGui::BeginTabItem("Orbit"))
        {
            std::string orbcen = "Center of Orbit: ";
            orbcen += std::string(cel->orbit->center->name);
            ImGui::Text("%s", orbcen.c_str());
            if (orb->center && orb->center->type == star)
            {
                ImGui::SameLine(col2);
                stringstream oss;
                double star_appmag = orb->center->viewer_magnitude(cel->location);
                oss << "Star apparent mag. " << (star_appmag > 0 ? "+" : "") << std::setprecision(5) << star_appmag;
                ImGui::Text("%s", oss.str().c_str());
            }

            if (cel->typeclass() == class_moon)
            {
                Moon *m = (Moon*)cel;
                ImGui::SameLine(col2);
                ImGui::Text("%s", "Plane");
                ImGui::SameLine(col3);
                ImGui::SetNextItemWidth(txtwid);
                std::vector<const char*> orb_planes;
                std::vector<OrbitType> orb_types;
                orb_planes.push_back("Ecliptic");
                orb_types.push_back(ot_ecliptic);
                orb_planes.push_back("Equatorial");
                orb_types.push_back(ot_equatorial);
                if (((Planet*)orb->center)->J2)
                {
                    orb_planes.push_back("Laplace");
                    orb_types.push_back(ot_Laplace);
                }

                long unsigned int n;
                const char* preview_orb_plane = "";
                for (n = 0; n < orb_planes.size(); n++)
                {
                    if (orb_types[n] == m->orbit_type) preview_orb_plane = orb_planes[n];
                }

                if (ImGui::BeginCombo("##cboorbplane", preview_orb_plane, 0))
                {
                    for (n = 0; n < orb_planes.size(); n++)
                    {
                        const bool is_selected = (orb_types[n] == m->orbit_type);
                        if (ImGui::Selectable(orb_planes[n], is_selected))
                        {
                            m->orbit_type = orb_types[n];
                            m->user_edited = true;
                            viewchanged = true;
                        }

                        // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                        if (is_selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            double cen_radius = cel->orbit->center->get_equatorial_radius();
            double sma_limit = cen_radius + cel->get_equatorial_radius();
            if (orb->semimajor_axis < sma_limit)
            {
                orb->semimajor_axis = sma_limit;
                orb->compute_period(cel->mass);
            }
            edit_sma = orb->semimajor_axis / AU;
            ImGui::Text("%s", "Semimaj.Axis");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtsma", &edit_sma, 0, 0, "%.9f"))
            {
                orb->semimajor_axis = fmax(edit_sma * AU, sma_limit);
                cel->temperature = 0;
                if (cel->user_added) orb->compute_period(cel->mass);
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_planet
                    || cel->typeclass() == class_moon               // See Kepler-1625b.
                    ) ((Planet*)cel)->classify(((Planet*)cel)->is_in_con_HZ(), true);
                rp = (orb->semimajor_axis * (1.0 - orb->eccentricity) - cen_radius) * 1e-3;
                ra = (orb->semimajor_axis * (1.0 + orb->eccentricity) - cen_radius) * 1e-3;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            ImGui::Text("%s", "AU");
            edit_period = cel->orbit->period / oneday;
            if (cel->orbit->center->typeclass() == class_star)
            {
                ImGui::SameLine();
                if (ImGui::Button("HZ"))
                {
                    // TODO: The following is a very very poor approximation.
                    double s_eff_lum = pow(magnbase, cels[0]->absolute_magnitude-cel->orbit->center->absolute_magnitude);
                    double sma_au = cel->orbit->semimajor_axis / AU;
                    double instellation = s_eff_lum / (sma_au*sma_au);
                    orb->semimajor_axis *= sqrt(instellation);
                    orb->compute_period(cel->mass);
                    cel->temperature = 0;
                    if (cel->typeclass() == class_planet
                        || cel->typeclass() == class_moon               // See Kepler-1625b.
                        ) ((Planet*)cel)->classify(((Planet*)cel)->is_in_con_HZ(), true);
                }
            }
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
                    if (orb->semimajor_axis < sma_limit)
                    {
                        orb->semimajor_axis = sma_limit;
                        orb->compute_period(cel->mass);
                    }
                    cel->temperature = 0;
                    if (cel->typeclass() == class_planet
                        || cel->typeclass() == class_moon               // See Kepler-1625b.
                        ) ((Planet*)cel)->classify(((Planet*)cel)->is_in_con_HZ(), true);
                    rp = (orb->semimajor_axis * (1.0 - orb->eccentricity) - cen_radius) * 1e-3;
                    ra = (orb->semimajor_axis * (1.0 + orb->eccentricity) - cen_radius) * 1e-3;
                    cel->user_edited = true;
                    viewchanged = true;
                    if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                    else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                    else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                    else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
                }
            }
            ImGui::SameLine();
            ImGui::Text("%s", "days");
            if (cel->typeclass() != class_satellite)
            {
                ImGui::SameLine();
                if (ImGui::Button("tidal##edit_period"))
                {
                    cel->sidereal_rotational_period = cel->orbit->period;
                }
            }

            if (((!rp && !ra) || (ra >= rp)) && orb->eccentricity >= 0 && orb->eccentricity < 1)
            {
                if (tc == class_satellite && orb && orb->center)
                {
                    rp = (orb->semimajor_axis * (1.0 - orb->eccentricity) - cen_radius) * 1e-3;
                    ra = (orb->semimajor_axis * (1.0 + orb->eccentricity) - cen_radius) * 1e-3;
                }
                else
                {
                    rp = orb->semimajor_axis * (1.0 - orb->eccentricity);
                    ra = orb->semimajor_axis * (1.0 + orb->eccentricity);
                }
            }

            if (tc == class_satellite && orb && orb->center)
            {
                ImGui::Text("%s", "Semimaj.Axis");
                ImGui::SameLine(col1);
                ImGui::Text("%.3f %s radii", orb->semimajor_axis/cen_radius, orb->center->name);
                ImGui::SameLine(col2);
                ImGui::Text("%s", "Period");
                ImGui::SameLine(col3);
                ImGui::Text("%.6f minutes", orb->period/60);

                Satellite* sat = ((Satellite*)cel);
                ImGui::Text("%s", "Periapsis, km");
                ImGui::SameLine(col1);
                ImGui::SetNextItemWidth(txtwid);
                if (ImGui::InputDouble("##edtperi", &rp, 0, 0, "%.9f") && rp<=ra)
                {
                    if (rp < 0) rp = 0;
                    if (ra < 0) ra = 0;
                    double rp1 = rp * 1e3+cen_radius, ra1 = ra * 1e3+cen_radius;
                    double a = (rp1+ra1);
                    edit_eccn = (ra1-rp1)/a;
                    a *= 0.5;
                    orb->semimajor_axis = a;
                    orb->eccentricity = edit_eccn;
                    orb->compute_period(cel->mass);
                    edit_period = orb->period / oneday;
                    cel->user_edited = true;
                    viewchanged = true;
                    sat->update_location(simnow);
                }
                ImGui::SameLine(col2);
                ImGui::Text("%s", "Apoapsis, km");
                ImGui::SameLine(col3);
                ImGui::SetNextItemWidth(txtwid);
                if (ImGui::InputDouble("##edtapo", &ra, 0, 0, "%.9f") && rp<=ra)
                {
                    if (rp < 0) rp = 0;
                    if (ra < 0) ra = 0;
                    double rp1 = rp * 1e3+cen_radius, ra1 = ra * 1e3+cen_radius;
                    double a = (rp1+ra1);
                    edit_eccn = (ra1-rp1)/a;
                    a *= 0.5;
                    orb->semimajor_axis = a;
                    orb->eccentricity = edit_eccn;
                    orb->compute_period(cel->mass);
                    edit_period = orb->period / oneday;
                    cel->user_edited = true;
                    viewchanged = true;
                    sat->update_location(simnow);
                }
                edit_node = cel->orbit->ascending_node * fiftyseven;
            }
            else
            {
                ImGui::Text("%s", "Periapsis");
                ImGui::SameLine(col1);
                ImGui::SetNextItemWidth(txtwid);
                ImGui::Text("%.5f AU", rp / AU);
                ImGui::SameLine(col2);
                ImGui::Text("%s", "Apoapsis");
                ImGui::SameLine(col3);
                ImGui::SetNextItemWidth(txtwid);
                ImGui::Text("%.5f AU", ra / AU);
            }

            if (cel->orbit && cel->orbit->center)
            {
                double Roche = cel->orbit->center->Roche_limit(cel);
                if (rp < Roche) ImGui::TextColored(ImVec4(1,0,0,1), "DANGER: OBJECT IS INSIDE ROCHE LIMIT.");
            }

            edit_incl = cel->orbit->inclination * fiftyseven;
            ImGui::Text("%s", "Inclination");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtincl", &edit_incl, 0, 0, "%.9f"))
            {
                orb->inclination = edit_incl * fiftyseventh;
                cel->lock_equatorial_plane = false;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            if (ImGui::Button("rnd##orbincl"))
            {
                orb->inclination = pow(frand(0, half_pi), 3);
                if (frand(0,1) < 0.03) orb->inclination = _pi - orb->inclination;
                cel->user_edited = true;
            }
            ImGui::SameLine(col2);
            edit_node = cel->orbit->ascending_node * fiftyseven;
            cel->lock_equatorial_plane = false;
            ImGui::Text("%s", "Asc. Node");
            ImGui::SameLine(col3);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtnode", &edit_node, 0, 0, "%.9f"))
            {
                cels[editidx]->orbit->ascending_node = edit_node * fiftyseventh;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            if (ImGui::Button("rnd##node&argperi&manom"))
            {
                cel->orbit->ascending_node = frand(0, _pi*2);
                cel->orbit->arg_periapsis = frand(0, _pi*2);
                cel->orbit->mean_anomaly = frand(0, _pi*2);
                cel->user_edited = true;
            }

            edit_eccn = cel->orbit->eccentricity;
            ImGui::Text("%s", "Eccentricity");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtecc", &edit_eccn, 0, 0, "%.9f"))
            {
                cel->orbit->eccentricity = edit_eccn;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            if (ImGui::Button("rnd##ecce"))
            {
                cel->orbit->eccentricity = pow(frand(0, 0.999), 10);
                cel->user_edited = true;
            }
            ImGui::SameLine(col2);
            edit_argperi = cel->orbit->arg_periapsis * fiftyseven;
            ImGui::Text("%s", "Arg.Periapsis");
            ImGui::SameLine(col3);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtargperi", &edit_argperi, 0, 0, "%.9f"))
            {
                cel->orbit->arg_periapsis = edit_argperi * fiftyseventh;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }

            edit_epoch = cel->orbit->epoch;
            ImGui::Text("%s", "Epoch, JD");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtepoch", &edit_epoch, 0, 0, "%.9f"))
            {
                cel->orbit->epoch = edit_epoch;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            if (ImGui::Button("J2000"))
            {
                cel->orbit->epoch = J2000;
            }
            ImGui::SameLine(col2);
            edit_manom = cel->orbit->mean_anomaly * fiftyseven;
            ImGui::Text("%s", "Mean Anomaly");
            ImGui::SameLine(col3);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtmanom", &edit_manom, 0, 0, "%.9f"))
            {
                cel->orbit->mean_anomaly = edit_manom * fiftyseventh;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }

            edit_precnode = cel->orbit->prec_node ? (_pi * 2 / cel->orbit->prec_node / oneday) : 0;
            ImGui::Text("%s", "Prec. Node");
            ImGui::SameLine(col1);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtprcnd", &edit_precnode, 0, 0, "%.9f"))
            {
                cels[editidx]->orbit->prec_node = edit_precnode ? (_pi * 2 / (edit_precnode * oneday)) : 0;
                cel->lock_equatorial_plane = false;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            ImGui::Text("%s", "d.");
            ImGui::SameLine(col2);
            edit_procargperi = cel->orbit->proc_argperi ? (_pi * 2 / cel->orbit->proc_argperi / oneday) : 0;
            ImGui::Text("%s", "ProcArgPeri");
            ImGui::SameLine(col3);
            ImGui::SetNextItemWidth(txtwid);
            if (ImGui::InputDouble("##edtprcap", &edit_procargperi, 0, 0, "%.9f"))
            {
                cels[editidx]->orbit->proc_argperi = edit_procargperi ? (_pi * 2 / (edit_procargperi * oneday)) : 0;
                cel->user_edited = true;
                viewchanged = true;
                if (cel->typeclass() == class_star) ((Star*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_planet) ((Planet*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_moon) ((Moon*)cel)->update_location(simnow);
                else if (cel->typeclass() == class_satellite) ((Satellite*)cel)->update_location(simnow);
            }
            ImGui::SameLine();
            ImGui::Text("%s", "d.");

            ImGui::EndTabItem();
        }
        if ((tc == class_star || tc == class_planet || tc == class_moon) && ImGui::BeginTabItem("Maps"))
        {
            if ((tc == class_planet || tc == class_moon))
            {
                Planet* p = (Planet*)cel;

                double edit_surf_presh = p->get_surface_pressure() / oneatm;
                if (isinf(edit_surf_presh)) edit_surf_presh = 0;
                ImGui::Text("%s", "Pressure, atm");
                ImGui::SameLine(col1);
                ImGui::SetNextItemWidth(txtwid);
                bool update_taucalc = false;
                if (ImGui::InputDouble("##edtpresh", &edit_surf_presh, 0, 0, "%.3f"))
                {
                    p->ensure_atmosphere()->surface_pressure = edit_surf_presh * oneatm;
                    p->temperature = 0;
                    update_taucalc = true;
                    cel->user_edited = true;
                    if (cel->typeclass() == class_planet
                        || cel->typeclass() == class_moon               // See Kepler-1625b.
                        ) ((Planet*)cel)->classify(((Planet*)cel)->is_in_con_HZ(), true);
                }
                ImGui::SameLine(col2);
                ImGui::Text("%s", "Total tau");
                ImGui::SameLine(col3);
                double edit_atm_tau = p->get_atmospheric_tau();
                ImGui::SetNextItemWidth(txtwid);
                if (ImGui::InputDouble("##edttau", &edit_atm_tau, 0, 0, "%.5f"))
                {
                    p->ensure_atmosphere()->tau = edit_atm_tau;
                    p->temperature = 0;
                    cel->user_edited = true;
                    if (cel->typeclass() == class_planet
                        || cel->typeclass() == class_moon               // See Kepler-1625b.
                        ) ((Planet*)cel)->classify(((Planet*)cel)->is_in_con_HZ(), true);
                }
                ImGui::SameLine();
                if (ImGui::Button("...##tau_calculator"))
                {
                    show_taucalc = !show_taucalc;
                }

                double edit_particulates = p->get_particulates();
                ImGui::Text("%s", "Particulates");
                ImGui::SameLine(col1);
                ImGui::SetNextItemWidth(txtwid);
                if (ImGui::InputDouble("##edtpartic", &edit_particulates, 0, 0, "%.6f"))
                {
                    p->ensure_atmosphere()->particulates = edit_particulates;
                    viewchanged = true;
                    cel->user_edited = true;
                }
                ImGui::SameLine();
                ImGui::Text("%s", "How much the sky color resembles the ground color.");

                if (show_taucalc)
                {
                    double col15 = col2 - 0.8*txtwid;

                    // Normalize pressure relative to Earth's sea-level pressure (1 atm)
                    constexpr double P_EARTH = 101325.0;
                    double normalized_pressure = p->get_surface_pressure() / P_EARTH;

                    ImGui::Text("%s", "Use this tool to calculate total tau from atmospheric pressure and composition.");
                    ImGui::Text("%s", "We ignore gases like nitrogen, oxygen, argon that do not contribute to the greenhouse effect.");
                    // Backed by the planet's own AtmosphereComposition rather than by statics. Statics
                    // survived a change of selection, so opening this panel on a second planet showed
                    // the first one's gases and silently recomputed its tau from them.
                    //
                    // The composition is only created once the user actually edits a field: merely
                    // looking at a body must not give it an atmosphere. Until then the fields show
                    // starting suggestions, which is what the old initializers were for.
                    AtmosphereComposition *comp = (p->atm && p->atm->comp) ? p->atm->comp : nullptr;
                    double co2_percent  = comp ? comp->CO2_portion  * 100 : (p->is_in_con_HZ() ? 0.04 : 90),
                        ch4_percent  = comp ? comp->CH4_portion  * 100 : (p->is_in_con_HZ() ? 0.0002 : 0),
                        h2o_percent  = comp ? comp->H2O_portion  * 100 : (p->is_in_con_HZ() ? 1 : 0),
                        n2o_percent  = comp ? comp->N2O_portion  * 100 : 0,
                        o3_percent   = comp ? comp->O3_portion   * 100 : 0,
                        so2_percent  = comp ? comp->SO2_portion  * 100 : 0,
                        h2s_percent  = comp ? comp->H2S_portion  * 100 : 0,
                        co_percent   = comp ? comp->CO_portion   * 100 : 0,
                        hcn_percent  = comp ? comp->HCN_portion  * 100 : 0,
                        h2_percent   = comp ? comp->H2_portion   * 100 : 0,
                        nh3_percent  = comp ? comp->NH3_portion  * 100 : 0,
                        c2h6_percent = comp ? comp->C2H6_portion * 100 : 0;

                    ImGui::Text("%s", "Carbon dioxide %");
                    ImGui::SameLine(col15);
                    ImGui::SetNextItemWidth(txtwid);
                    if (ImGui::InputDouble("##edtc02", &co2_percent, 0, 0, "%.6f"))
                    {
                        p->ensure_atmosphere()->ensure_composition()->CO2_portion = co2_percent * 0.01;
                        update_taucalc = true;
                        cel->user_edited = true;
                    }
                    if (p->type != gas_giant && p->type != ice_giant)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Randomize  ");
                        ImGui::SameLine();
                        ImGui::Checkbox("##randomize_txgen", &randomize_txgen);
                    }
                    else randomize_txgen = true;

                    ImGui::Text("%s", "Methane %");
                    ImGui::SameLine(col15);
                    ImGui::SetNextItemWidth(txtwid);
                    if (ImGui::InputDouble("##edtch4", &ch4_percent, 0, 0, "%.6f"))
                    {
                        p->ensure_atmosphere()->ensure_composition()->CH4_portion = ch4_percent * 0.01;
                        update_taucalc = true;
                        cel->user_edited = true;
                    }
                    if (has_water < 0) has_water = 0;
                    if (has_water > 1) has_water = 1;
                    if (p->type == rocky && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Water up to:    ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        ImGui::InputFloat("##edth2olvl", &has_water);
                    }

                    ImGui::Text("%s", "Water vapor %");
                    ImGui::SameLine(col15);
                    ImGui::SetNextItemWidth(txtwid);
                    if (ImGui::InputDouble("##edth2o", &h2o_percent, 0, 0, "%.6f"))
                    {
                        p->ensure_atmosphere()->ensure_composition()->H2O_portion = h2o_percent * 0.01;
                        update_taucalc = true;
                        cel->user_edited = true;
                    }
                    if (p->type == rocky && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Coldest vegetation:");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        ImGui::InputFloat("##edtvegcold", &veg_min_temp);
                        ImGui::SameLine();
                        ImGui::Text("%s", "degK");
                    }

                    ImGui::Text("%s", "Nitrous oxide %");
                    ImGui::SameLine(col15);
                    ImGui::SetNextItemWidth(txtwid);
                    if (ImGui::InputDouble("##edtn2o", &n2o_percent, 0, 0, "%.6f"))
                    {
                        p->ensure_atmosphere()->ensure_composition()->N2O_portion = n2o_percent * 0.01;
                        update_taucalc = true;
                        cel->user_edited = true;
                    }
                    if (p->type == rocky && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Hottest vegetation:");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        ImGui::InputFloat("##edtveghot", &veg_max_temp);
                        ImGui::SameLine();
                        ImGui::Text("%s", "degK");
                    }

                    ImGui::Text("%s", "Ozone %");
                    ImGui::SameLine(col15);
                    ImGui::SetNextItemWidth(txtwid);
                    if (ImGui::InputDouble("##edto3", &o3_percent, 0, 0, "%.6f"))
                    {
                        p->ensure_atmosphere()->ensure_composition()->O3_portion = o3_percent * 0.01;
                        update_taucalc = true;
                        cel->user_edited = true;
                    }
                    if (vegetation_r < 0) vegetation_r = 0;
                    if (vegetation_r > 255) vegetation_r = 255;
                    if (p->type == rocky && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Vegetation R:   ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        ImGui::InputInt("##edtvegr", &vegetation_r);
                    }

                    ImGui::Text("%s", "Sulfur dioxide %");
                    ImGui::SameLine(col15);
                    ImGui::SetNextItemWidth(txtwid);
                    if (ImGui::InputDouble("##edtso2", &so2_percent, 0, 0, "%.6f"))
                    {
                        p->ensure_atmosphere()->ensure_composition()->SO2_portion = so2_percent * 0.01;
                        update_taucalc = true;
                        cel->user_edited = true;
                    }
                    if (vegetation_g < 0) vegetation_g = 0;
                    if (vegetation_g > 255) vegetation_g = 255;
                    if (p->type == rocky && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Vegetation G:   ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        ImGui::InputInt("##edtvegg", &vegetation_g);
                    }

                    ImGui::Text("%s", "Hydrogen sulfide %");
                    ImGui::SameLine(col15);
                    ImGui::SetNextItemWidth(txtwid);
                    if (ImGui::InputDouble("##edth2s", &h2s_percent, 0, 0, "%.6f"))
                    {
                        p->ensure_atmosphere()->ensure_composition()->H2S_portion = h2s_percent * 0.01;
                        update_taucalc = true;
                        cel->user_edited = true;
                    }
                    if (vegetation_b < 0) vegetation_b = 0;
                    if (vegetation_b > 255) vegetation_b = 255;
                    if (p->type == rocky && !randomize_txgen)
                    {
                        ImGui::SameLine();
                        ImGui::Text("%s", "Vegetation B:   ");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(txtwid*.6);
                        ImGui::InputInt("##edtvegb", &vegetation_b);
                    }

                    ImGui::Text("%s", "Carbon monoxide %");
                    ImGui::SameLine(col15);
                    ImGui::SetNextItemWidth(txtwid);
                    if (ImGui::InputDouble("##edtco", &co_percent, 0, 0, "%.6f"))
                    {
                        p->ensure_atmosphere()->ensure_composition()->CO_portion = co_percent * 0.01;
                        update_taucalc = true;
                        cel->user_edited = true;
                    }
                    ImGui::Text("%s", "Hydrogen cyanide %");
                    ImGui::SameLine(col15);
                    ImGui::SetNextItemWidth(txtwid);
                    if (ImGui::InputDouble("##edthcn", &hcn_percent, 0, 0, "%.6f"))
                    {
                        p->ensure_atmosphere()->ensure_composition()->HCN_portion = hcn_percent * 0.01;
                        update_taucalc = true;
                        cel->user_edited = true;
                    }
                    ImGui::Text("%s", "Hydrogen %");
                    ImGui::SameLine(col15);
                    ImGui::SetNextItemWidth(txtwid);
                    if (ImGui::InputDouble("##edth2", &h2_percent, 0, 0, "%.6f"))
                    {
                        p->ensure_atmosphere()->ensure_composition()->H2_portion = h2_percent * 0.01;
                        update_taucalc = true;
                        cel->user_edited = true;
                    }
                    ImGui::Text("%s", "Ammonia %");
                    ImGui::SameLine(col15);
                    ImGui::SetNextItemWidth(txtwid);
                    if (ImGui::InputDouble("##edtnh3", &nh3_percent, 0, 0, "%.6f"))
                    {
                        p->ensure_atmosphere()->ensure_composition()->NH3_portion = nh3_percent * 0.01;
                        update_taucalc = true;
                        cel->user_edited = true;
                    }
                    ImGui::Text("%s", "Ethane %");
                    ImGui::SameLine(col15);
                    ImGui::SetNextItemWidth(txtwid);
                    if (ImGui::InputDouble("##edtc2h6", &c2h6_percent, 0, 0, "%.6f"))
                    {
                        p->ensure_atmosphere()->ensure_composition()->C2H6_portion = c2h6_percent * 0.01;
                        update_taucalc = true;
                        cel->user_edited = true;
                    }

                    if (update_taucalc)
                    {
                        p->temperature = 0;
                        AtmosphereComposition *tc = p->ensure_atmosphere()->ensure_composition();
                        p->atm->tau = edit_atm_tau = atmospheric_tau(normalized_pressure,
                            tc->CO2_portion, tc->CH4_portion, tc->H2O_portion, tc->N2O_portion,
                            tc->O3_portion,  tc->SO2_portion, tc->H2S_portion, tc->CO_portion,
                            tc->HCN_portion, tc->H2_portion,  tc->NH3_portion, tc->C2H6_portion);
                    }
                    if (ImGui::Button("Clear##atmosph_comp"))
                    {
                        co2_percent=ch4_percent=h2o_percent=n2o_percent=o3_percent=so2_percent=h2s_percent=co_percent=hcn_percent=h2_percent=nh3_percent=c2h6_percent=0;
                        if (p->atm && p->atm->comp) *(p->atm->comp) = AtmosphereComposition(cel);
                        update_taucalc = true;
                        cel->user_edited = true;
                    }
                }

                ImGui::Text("Surface temperature: %fK", p->estimate_surface_temperature());
            }

            ImGui::Text("%s", "Texture");
            ImGui::SameLine();
            if (!generating_fic_texture && ImGui::Button("Save"))
            {
                std::thread save_tex(save_textures, cel);
                save_tex.detach();
            }
            ImGui::SameLine();
            if (!generating_fic_texture && cel->has_real_maps && ImGui::Button("Reload"))
            {
                if (cel->surf_map)
                {
                    delete cel->surf_map;
                    cel->surf_map = nullptr;
                }
                if (cel->cloud_map)
                {
                    delete cel->cloud_map;
                    cel->cloud_map = nullptr;
                }
                if (cel->night_map)
                {
                    delete cel->night_map;
                    cel->night_map = nullptr;
                }
                cel->ignore_map_files = false;
                cel->looked_for_maps = false;
            }
            ImGui::SameLine();
            if (!generating_fic_texture && ImGui::Button("Update"))
            {
                cel->looked_for_maps = false;
                cel->ignore_map_files = true;
                if (cel->surf_map)
                {
                    cel->surf_map->mark_for_map_regen(cel);
                }
                if (cel->cloud_map)
                {
                    cel->cloud_map->mark_for_map_regen(cel);
                }
                if (cel->night_map)
                {
                    cel->night_map->mark_for_map_regen(cel);
                }
            }
            ImGui::SameLine();
            if (!generating_fic_texture && ImGui::Button("Regenerate"))
            {
                cel->rnd_seed = rand();
                if (randomize_txgen) vegetation_r = vegetation_g = vegetation_b = 0;     // force regenerate
                cel->looked_for_maps = false;
                cel->ignore_map_files = true;
                if (cel->surf_map)
                {
                    cel->surf_map->mark_for_map_regen(cel, true);
                }
                if (cel->cloud_map)
                {
                    cel->cloud_map->mark_for_map_regen(cel, true);
                }
                if (cel->night_map)
                {
                    cel->night_map->mark_for_map_regen(cel, true);
                }
            }
            ImGui::SameLine();
            ImGui::Text("Map Height");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(txtwid);
            int edt_map_ht = (int)std::min<unsigned int>(cel->fictitious_map_height, 8388608);        // limit imposed by long index for height*width.
            ImGui::InputInt("px##edit_map_ht", &edt_map_ht, 0, 0);
            cel->fictitious_map_height = (unsigned int)std::max(50, edt_map_ht);

            spawn_texture_load(cel);

            Map *celmaps[3];
            celmaps[0] = cel->surf_map;
            celmaps[1] = cel->cloud_map;
            celmaps[2] = cel->night_map;

            const char *maptabs[3] = { "Surface", "Clouds", "Night" };

            if (ImGui::BeginTabBar("##editmaps", tab_bar_flags))
            {
                int i;
                for (i=0; i<3; i++)
                {
                    Map *map = celmaps[i];
                    int x, y;
                    double xrad, yrad;
                    RGB3Byte rgb;
                    ImU32 imu;

                    if (map)
                    {
                        if (map->has_bump_data() && !map->has_rgb_data())
                            ImGui::Text("Please wait, this may take a few minutes...");
                        else if (ImGui::BeginTabItem(maptabs[i]))
                        {
                            ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();      // ImDrawList API uses screen coordinates.
                            // canvas_p0.x += col2;
                            ImVec2 canvas_sz;
                            canvas_sz.x = 360.0f;
                            canvas_sz.y = 180.0f;
                            ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

                            // Draw border and background color
                            ImDrawList* draw_list = ImGui::GetWindowDrawList();
                            draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(0, 0, 0, 255));
                            draw_list->AddRect(canvas_p0, canvas_p1, rgba_apply_redlight(IM_COL32(0, 16, 128, 255)));

                            for (y=0; y<180; y++)
                            {
                                yrad = (90-y) * fiftyseventh;
                                for (x=0; x<360; x++)
                                {
                                    xrad = x * fiftyseventh;

                                    rgb = map->color_at(yrad, xrad-_pi);
                                    imu = rgba_apply_redlight(IM_COL32(rgb.r, rgb.g, rgb.b, 255));
                                    draw_list->AddRectFilled(ImVec2(canvas_p0.x+x, canvas_p0.y+y), ImVec2(canvas_p0.x+x+1, canvas_p0.y+y+1), imu);
                                }
                            }

                            // Hold my place.
                            ImGui::InvisibleButton("canvas", canvas_sz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                            ImGui::EndTabItem();
                        }
                    }
                }
                ImGui::EndTabBar();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::SetWindowSize(ImVec2(0, 0));
    ImVec2 pos = ImGui::GetWindowPos(), siz = ImGui::GetWindowSize();
    ImGui::End();

    last_edit_idx = editidx;

    if (io.MousePos.x >= pos.x && io.MousePos.y >= pos.y
        && io.MousePos.x < pos.x+siz.x && io.MousePos.y < pos.y+siz.y)
        is_mouse_over_window = true;
}

void draw_system_explorer(ImGuiIO& io)
{
    if (!cels[1]) return;
    ImGui::Begin("System Explorer", &explorer, 0);

    if (last_xplored_cen != mycenobj) xplorfor[0] = 0;
    last_xplored_cen = mycenobj;

    ImGui::Text("%s", "Search:");
    ImGui::SameLine();
    ImGui::InputText("##xplorsearch", xplorfor, name_max_len, 0);

    static bool list_planets = true, list_moons = true, list_asteroids = false, list_comets = false, list_kbos = true, list_sats = false;
    ImGui::Text("%s", "Include:");
    ImGui::SameLine();
    ImGui::Checkbox("Planets##xplorr", &list_planets);
    ImGui::SameLine();
    ImGui::Checkbox("Moons##xplorr", &list_moons);
    ImGui::SameLine();
    ImGui::Checkbox("Asteroids##xplorr", &list_asteroids);
    ImGui::SameLine();
    ImGui::Checkbox("KBOs##xplorr", &list_kbos);
    ImGui::SameLine();
    ImGui::Checkbox("Comets##xplorr", &list_comets);
    ImGui::SameLine();
    ImGui::Checkbox("Sats##xplorr", &list_sats);

    int i, j, l, xplorlen = strlen(xplorfor);
    std::vector<int> list_item_celids;
    static int item_selected_idx = 0;
    int item_highlighted_idx = -1;
    ImGui::Text("%s", " Name                                Orbits                            Period, d       Mass, e          HZ?");
    if (ImGui::BeginListBox("##syslist", ImVec2(916, 16 * ImGui::GetTextLineHeightWithSpacing())))
    {
        j = 0;
        for (i=0; cels[i]; i++)
        {
            if (cels[i]->deleted) continue;
            if (cels[i]->cenobj != mycenobj) continue;
            bool is_selected = (item_selected_idx == j);

            cel_obj_class cls = cels[i]->typeclass();
            if (cls == class_satellite && !list_sats) continue;
            if (cls == class_moon && !list_moons) continue;
            if (cls == class_planet)
            {
                if (cels[i]->mass > 0.05 * earth_mass)
                {
                    if (!list_planets) continue;
                }
                else
                {
                    if (cels[i]->orbit)
                    {
                        if (cels[i]->orbit->period < (60000.0*oneday))
                        {
                            if (!list_asteroids) continue;
                        }
                        else
                        {
                            if (!list_kbos) continue;
                        }
                    }
                }
            }
            if (cls == class_comet && !list_comets) continue;

            std::string line = std::string(cels[i]->name).substr(0, 20);
            l = 36 - line.size();
            if (l > 0) line += std::string(l, ' ');

            if (cels[i]->orbit && cels[i]->orbit->center)
                line += std::string(cels[i]->orbit->center->name).substr(0, 18);
            else line += std::string("-");

            if (xplorlen && !strcasestr(line.c_str(), xplorfor)) continue;
            list_item_celids.push_back(i);

            l = 70 - line.size();
            if (l > 0) line += std::string(l, ' ');

            if (cels[i]->orbit && cels[i]->orbit->period)
            {
                stringstream pss;
                pss << setprecision(7) << (cels[i]->orbit->period/oneday);
                line += pss.str();
            }
            else line += std::string("-");

            l = 86 - line.size();
            if (l > 0) line += std::string(l, ' ');

            if (cels[i]->mass)
            {
                double f = cels[i]->mass / earth_mass;
                stringstream mss;
                mss << (f >= 0.1 ? std::fixed : std::scientific) << setprecision(3) << f;
                line += mss.str();
            }
            else line += std::string("?");

            l = 105 - line.size();
            if (l > 0) line += std::string(l, ' ');

            if (cels[i]->orbit && cels[i]->orbit->period)
            {
                cel_obj_class cls = cels[i]->typeclass();
                if ((cls == class_planet || cls == class_moon) && ((Planet*)cels[i])->is_in_con_HZ())
                    line += "Y";
                else line += std::string("");
            }
            else line += std::string("");

            ImGuiSelectableFlags flags = (item_highlighted_idx == j) ? ImGuiSelectableFlags_Highlight : 0;
            if (ImGui::Selectable(line.c_str(), is_selected, flags))
                item_selected_idx = j;

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();

            j++;
        }
        ImGui::EndListBox();
    }

    if ((int64_t)item_selected_idx < (int64_t)list_item_celids.size())
        celidx_sel_in_sysxplor = list_item_celids[item_selected_idx];
    else celidx_sel_in_sysxplor = -1;
    if (ImGui::Button("Select##explored"))
    {
        selected = celidx_sel_in_sysxplor;
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Find##explored"))
    {
        selected = celidx_sel_in_sysxplor;
        center_selected();
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Track##explored"))
    {
        trackidx = celidx_sel_in_sysxplor;
        center_tracked();
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Go##explored"))
    {
        if (celidx_sel_in_sysxplor >= 0)
        {
            whereami = celidx_sel_in_sysxplor;
            set_viewer_location_and_plane();
            selected = trackidx = -1;
            global_brightness = default_brightness;
            zoom = 1;
            viewchanged = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit##explored"))
    {
        if (celidx_sel_in_sysxplor >= 0)
        {
            editidx = celidx_sel_in_sysxplor;
            objedtwnd = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add New...##explored"))
    {
        selected = celidx_sel_in_sysxplor;
        process_key_cmd_char('A');
    }
    if (celidx_sel_in_sysxplor > 0)
    {
        CelestialObject *cel = cels[celidx_sel_in_sysxplor];
        if (cel->typeclass() == class_planet)
        {
            ImGui::SameLine();
            if (ImGui::Button("Gen. Fic. Moons##explored"))
            {
                cel->rnd_seed = 0;          // force rerandomization.
                vegetation_r = vegetation_g = vegetation_b = 0;
                double A = sqrt(cel->orbit->period / oneday) / 4;
                double B = sqrt(cel->mass / earth_mass);
                double C = sqrt(A*B);
                double Hill_over_Roche = cel->Hill_sphere_radius() / cel->Roche_limit();
                double D = C * Hill_over_Roche / 500;
                /*int n = round(frand(0.1, 1) * C);*/

                double P = 0;
                for (i=0; i<20; i++)
                {
                    Moon *m = new Moon();
                    std::string mname = std::string(cel->name) + std::string(" ") + Roman(i+1);
                    strcpy(m->name, mname.c_str());
                    m->user_added = true;
                    m->user_edited = true;
                    m->mass = pow(frand(0, 1), 4) * cel->mass / 4000;
                    m->volumetric_mean_radius = pow(m->mass / earth_mass, 1.0/3.0) * frand(0.9, 1.5) * earth_radius;
                    m->albedo = frand(0.1, 0.6);
                    m->BV_color = frand(0.8, 1.8);
                    m->cenobj = cel->cenobj;
                    m->epoch = J2000;
                    m->equinox = frand(0, _pi*2);
                    m->obliquity = pow(frand(0, 1), 10) * half_pi;
                    m->major_moon = true;
                    m->orbit = new Orbit();
                    m->orbit->center = cel;
                    m->orbit->arg_periapsis = frand(0, _pi*2);
                    m->orbit->ascending_node = frand(0, _pi*2);
                    m->orbit->eccentricity = pow(frand(0, 0.1), 10);
                    m->orbit->epoch = J2000;
                    m->orbit->inclination = pow(frand(0, 1), 4) * 0.2 * half_pi; if (frand(0,1) < 0.03) m->orbit->inclination = _pi - m->orbit->inclination;
                    m->orbit->mean_anomaly = frand(0, _pi*2);
                    if (!P)
                    {
                        double A = cel->Roche_limit(m) * (1.1 + pow(frand(0,1), 4) * 20);
                        m->orbit->semimajor_axis = A;
                        m->orbit->compute_period(m->mass);
                    }
                    else
                    {
                        m->orbit->period = frand(1.8, 2.2) * P;
                        m->orbit->compute_semimajor_axis(m->mass);
                    }

                    if (frand(0,1) > D)
                    {
                        P = m->orbit->period;
                        delete m->orbit;
                        m->orbit = nullptr;
                        delete m;
                        continue;
                    }

                    if (m->orbit->semimajor_axis > ((Planet*)cel)->Hill_sphere_radius())
                    {
                        delete m->orbit;
                        m->orbit = nullptr;
                        delete m;
                        break;
                    }
                    m->estimate_rotation();
                    if (!m->sidereal_rotational_period) m->sidereal_rotational_period = oneday;
                    m->classify();
                    double disc_area = pow(m->volumetric_mean_radius / earth_radius, 2);
                    m->absolute_magnitude = earth_absmag - log(disc_area * m->albedo / earth_albedo) / log(magnbase);

                    if (!append_cel(m))
                    {
                        delete m;               // ~Moon takes the orbit with it
                        break;
                    }
                    P = m->orbit->period;
                }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Satellite...##explored"))
    {
        process_key_cmd_char('^');
    }

    ImGui::SetWindowSize(ImVec2(0, 0));                         // Auto size to fit contents.
    ImVec2 pos = ImGui::GetWindowPos(), siz = ImGui::GetWindowSize();
    ImGui::End();

    // Code to ensure mouse interacts with window and not viewport.
    if (io.MousePos.x >= pos.x && io.MousePos.y >= pos.y && io.MousePos.x < (pos.x+siz.x) && io.MousePos.y < (pos.y+siz.y))
        is_mouse_over_window = true;
}

bool onlysun = false, onlyplt = false;
std::vector<int> neighb_celids;
std::vector<double> neighb_celr;
void draw_stellar_neighborhood(ImGuiIO &io)
{
    if (!cels[1]) return;
    ImGui::Begin("Stellar Neighborhood", &neighborhood, 0);

    ImGui::Checkbox("Only Sunlike##", &onlysun);
    ImGui::SameLine();
    ImGui::Checkbox("Must Have Planets##", &onlyplt);

    int i, j, l, n;
    double r, m;
    static int item_selected_idx = 0;
    int item_highlighted_idx = -1;
    ImGui::Text("%s", " Name                                Mag.        Sp. Type        Planets     Distance");
    if (ImGui::BeginListBox("##neighblist", ImVec2(768, 16 * ImGui::GetTextLineHeightWithSpacing())))
    {
        j = 0;
        if (last_neighb_cen != mycenobj)
        {
            neighb_celids.clear();
            neighb_celr.clear();
            for (i=0; cels[i]; i++)
            {
                if (cels[i]->deleted) continue;
                cel_obj_class cls = cels[i]->typeclass();
                if (cls != class_star) continue;
                r = cels[i]->tmprel.magnitude();
                if (r > neighb_rthresh) continue;

                l = neighb_celids.size();
                if (!l)
                {
                    neighb_celids.push_back(i);
                    neighb_celr.push_back(r);
                }
                else
                {
                    bool inserted = false;
                    for (j=0; !inserted && j<l; j++)
                    {
                        if (neighb_celr[j] > r)
                        {
                            neighb_celids.insert(neighb_celids.begin()+j, i);
                            neighb_celr.insert(neighb_celr.begin()+j, r);
                            inserted = true;
                        }
                    }
                    if (!inserted)
                    {
                        neighb_celids.push_back(i);
                        neighb_celr.push_back(r);
                    }
                }
            }
        }

        n = neighb_celids.size();
        for (j=0; j<n; j++)
        {
            i = neighb_celids[j];
            Star *s = (Star*)cels[i];
            if (onlysun && !s->is_sunlike()) continue;
            if (onlyplt && !s->has_planets) continue;

            stringstream line;
            line << cels[i]->name;
            l = line.str().size();
            if (l < 36) line << std::string(36-l, ' ');

            r = cels[i]->tmprel.magnitude();
            if (r)
            {
                m = cels[i]->viewer_magnitude(here);
                line << setprecision(4) << ((m>=0) ? "+" : "") << m;
            }
            else line << "-";
            l = line.str().size();
            if (l < 48) line << std::string(48-l, ' ');

            line << s->spectral_type;
            l = line.str().size();
            if (l < 64) line << std::string(64-l, ' ');

            if (s->has_planets) line << s->has_planets;
            l = line.str().size();
            if (l < 76) line << std::string(76-l, ' ');

            if (r < 0.1*AU) line << setprecision(3) << (r / 1000) << " km";
            else if (r < 0.1 * light_year) line << setprecision(3) << (r / AU) << " A.U.";
            else line << setprecision(3) << (r / light_year) << " l.y.";

            bool is_selected = (item_selected_idx == j);
            ImGuiSelectableFlags flags = (item_highlighted_idx == j) ? ImGuiSelectableFlags_Highlight : 0;
            if (ImGui::Selectable(line.str().c_str(), is_selected, flags))
                item_selected_idx = j;

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }

    l = neighb_celids.size();
    if (l < 100) neighb_rthresh *= 1.1;
    else if (l > 200 && (neighb_rthresh > 25 * light_year)) neighb_rthresh *= 0.9;
    else last_neighb_cen = mycenobj;

    if (item_selected_idx >= neighb_celids.size()) item_selected_idx = 0;
    if (ImGui::Button("Select##neighbors"))
    {
        selected = neighb_celids[item_selected_idx];
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Find##neighbors"))
    {
        selected = neighb_celids[item_selected_idx];
        center_selected();
        viewchanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Go##neighbors"))
    {
        if (neighb_celids[item_selected_idx] >= 0)
        {
            whereami = neighb_celids[item_selected_idx];
            set_viewer_location_and_plane();
            selected = trackidx = -1;
            global_brightness = default_brightness;
            zoom = 1;
            viewchanged = true;
        }
    }

    ImGui::SetWindowSize(ImVec2(0, 0));                         // Auto size to fit contents.
    ImVec2 pos = ImGui::GetWindowPos(), siz = ImGui::GetWindowSize();
    ImGui::End();

    // Code to ensure mouse interacts with window and not viewport.
    if (io.MousePos.x >= pos.x && io.MousePos.y >= pos.y && io.MousePos.x < (pos.x+siz.x) && io.MousePos.y < (pos.y+siz.y))
        is_mouse_over_window = true;
}

void draw_loc_window(ImGuiIO & io)
{
    if (whereami < 0) return;
    if (view_mode != vm_horizon) return;

    CelestialObject *cel = cels[whereami];

    if (!cel->nlocales) cel->read_locales("locales.json");

    int i, j, l, looklen = strlen(lookloc);
    unsigned int n=0;
    static unsigned int item_selected_idx = 0;
    int item_highlighted_idx = -1;
    double sellat = viewer_lat, sellon = viewer_lon, seltz=0;
    std::string selloc = viewer_locale;
    ImGui::Begin("Locales", &locwnd, 0);

    ImGui::Text("%s", "Search:");
    ImGui::SameLine();
    ImGui::InputText("##lookloc", lookloc, name_max_len, 0);

    ImGui::Text(" Locale                                  Lat.    Lon.");
    if (ImGui::BeginListBox("##loclist", ImVec2(503, 11 * ImGui::GetTextLineHeightWithSpacing())))
    {
        j=0;

        n = cel->nlocales;
        std::string name;
        double lat, lon;
        for (i=0; (unsigned)i<n; i++)
        {
            name = cel->locales[i].name;
            if (!name.size()) continue;
            if (looklen && !strcasestr(name.c_str(), lookloc)) continue;

            lat = cel->locales[i].lat;
            lon = cel->locales[i].lon;
            stringstream line;

            line << name;

            l = line.str().size();
            if (l < 40) line << std::string(40-l, ' ');

            line << setprecision(5) << lat;

            l = line.str().size();
            if (l < 48) line << std::string(48-l, ' ');

            line << setprecision(6) << lon;

            bool is_selected = (item_selected_idx == (unsigned)j);

            ImGuiSelectableFlags flags = ((int64_t)item_highlighted_idx == (int64_t)j) ? ImGuiSelectableFlags_Highlight : 0;
            if (ImGui::Selectable(line.str().c_str(), is_selected, flags))
            {
                item_selected_idx = j;
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
            {
                ImGui::SetItemDefaultFocus();

                sellat = lat * fiftyseventh;
                sellon = lon * fiftyseventh;
                seltz = cel->locales[i].tz;
                selloc = name;
            }

            j++;
        }

        ImGui::EndListBox();
    }

    if (ImGui::Button("Go"))
    {
        viewer_lat = sellat;
        viewer_lon = sellon;
        viewer_tz = seltz;
        viewer_locale = selloc;
    }

    ImGui::SetWindowSize(ImVec2(0, 0));                         // Auto size to fit contents.
    ImVec2 pos = ImGui::GetWindowPos(), siz = ImGui::GetWindowSize();
    ImGui::End();

    // Code to ensure mouse interacts with window and not viewport.
    if (io.MousePos.x >= pos.x && io.MousePos.y >= pos.y && io.MousePos.x < (pos.x+siz.x) && io.MousePos.y < (pos.y+siz.y))
        is_mouse_over_window = true;
}

void draw_ast_window(ImGuiIO & io)
{
    if (!cels[1]) return;
    static std::vector<std::string> astlistlines;
    ImGui::Begin("Add Asteroids", &astwnd, 0);

    static std::string mesg = "";
    ImGui::Text("%s", "Search:");
    ImGui::SameLine();
    if (ImGui::InputText("##lookast", lookast, name_max_len, 0)) astlistlines.clear();

    int i, l, looklen = strlen(lookast);
    unsigned int n=0, nasts = astorb.size();
    static unsigned int item_selected_idx = 0;
    int item_highlighted_idx = -1;
    ImGui::Text(" Number Name               S.M.A.          Diam.         Incl.");
    if (ImGui::BeginListBox("##astlist", ImVec2(623, 13 * ImGui::GetTextLineHeightWithSpacing())))
    {
        i=0;
        if (astlistlines.size())
        {
            nasts = astlistlines.size();
            for (n=0; n<nasts; n++)
            {
                bool is_selected = (item_selected_idx == n);

                ImGuiSelectableFlags flags = ((int64_t)item_highlighted_idx == (int64_t)n) ? ImGuiSelectableFlags_Highlight : 0;
                if (ImGui::Selectable(astlistlines[n].c_str(), is_selected, flags))
                    item_selected_idx = n;

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        else for (n=0; n<nasts; n++)
        {
            stringstream line;
            if (astorb[n].number) line << astorb[n].number;

            l = 7 - line.str().size();
            if (l > 0) line << std::string(l, ' ');

            line << astorb[n].name;

            l = 26 - line.str().size();
            if (l > 0) line << std::string(l, ' ');

            if (looklen && !strcasestr(line.str().c_str(), lookast)) continue;

            line << astorb[n].sma;

            l = 42 - line.str().size();
            if (l > 0) line << std::string(l, ' ');

            if (astorb[n].diam) line << astorb[n].diam;
            else line << "?";

            l = 56 - line.str().size();
            if (l > 0) line << std::string(l, ' ');

            line << astorb[n].incl;
            line << " ##" << n;
            astlistlines.push_back(line.str());

            bool is_selected = ((int64_t)item_selected_idx == (int64_t)i);

            ImGuiSelectableFlags flags = (item_highlighted_idx == i) ? ImGuiSelectableFlags_Highlight : 0;
            if (ImGui::Selectable(line.str().c_str(), is_selected, flags))
                item_selected_idx = i;

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();

            i++;
            if (i >= 25) break;
        }

        ImGui::EndListBox();
    }

    static ImVec4 msg_color = ImVec4(255, 255, 255, 255);

    if (item_selected_idx < astlistlines.size())
    {
        n = item_selected_idx;
        const char *hashmarks = strstr(astlistlines[n].c_str(), "##");
        i = atoi(&hashmarks[2]);
    }
    else
    {
        item_selected_idx = 0;
        i = -1;
    }

    if (i < (int)astorb.size())
    {
        if (i>=0 && astorb[i].cel)
        {
            if (ImGui::Button("Find##asteroid"))
            {
                selected = astorb[i].cel->seqno;
                center_selected();
            }
            ImGui::SameLine();
            if (ImGui::Button("Track##asteroid"))
            {
                trackidx = astorb[i].cel->seqno;
                center_tracked();
            }
            ImGui::SameLine();
            if (ImGui::Button("Go##asteroid"))
            {
                whereami = astorb[i].cel->seqno;
                viewchanged = true;
            }
        }
        else if (ImGui::Button("Add Selected##asteroid"))
        {
            mesg = "";
            for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);               // get count

            if (!CatalogReader::load_asteroid(&astorb[i]) || !astorb[i].cel)
            {
                mesg = "ERROR - Asteroid failed to load.";
                msg_color = ImVec4(255, 0, 0, 255);
            }
            else
            {
                selected = astorb[i].cel->seqno;
                compute_object_location(astorb[i].cel);
                compute_object_draw_coordinates();
                center_selected();
                viewchanged = true;
                astwnd = false;
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextColored(redlight_mode ? ImVec4(255, 24, 0, 255) : msg_color, "%s", mesg.c_str());

    ImGui::SetWindowSize(ImVec2(0, 0));                         // Auto size to fit contents.
    ImVec2 pos = ImGui::GetWindowPos(), siz = ImGui::GetWindowSize();
    ImGui::End();

    // Code to ensure mouse interacts with window and not viewport.
    if (io.MousePos.x >= pos.x && io.MousePos.y >= pos.y && io.MousePos.x < (pos.x+siz.x) && io.MousePos.y < (pos.y+siz.y))
        is_mouse_over_window = true;
}

void draw_sat_window(ImGuiIO& io)
{
    if (!cels[1]) return;
    ImGui::Begin("Add Satellite", &satwnd, 0);

    static std::string mesg = "";
    ImGui::Text("%s", "Search:");
    ImGui::SameLine();
    ImGui::InputText("##looksat", looksat, name_max_len, 0);

    int nsats = sat_data.size(), i, l, looklen = strlen(looksat);
    int n=0;
    static int item_selected_idx = 0;
    int item_highlighted_idx = -1;
    std::vector<std::string> listlines;
    ImGuiSelectableFlags flags = 0;
    if (ImGui::BeginListBox("##satlist", ImVec2(821, 11 * ImGui::GetTextLineHeightWithSpacing())))
    {
        i=0;
        if (updating_sats)
        {
            ImGui::Selectable("Please wait...", false, flags);
        }
        else for (n=0; n<nsats; n++)
        {
            if (!sat_data[n].catalog.size()) continue;
            std::string line = std::string(sat_data[n].OBJECT_NAME);
            l = line.size();

            l = 40 - l;
            if (l > 0) line += std::string(l, ' ');
            line += sat_data[n].OBJECT_ID;
            l = 52 - line.size();
            if (l > 0) line += std::string(l, ' ');
            line += sat_data[n].catalog;
            line += std::string(" ##") + std::to_string(n);

            if (looklen && !strcasestr(line.c_str(), looksat)) continue;
            listlines.push_back(line);

            bool is_selected = (item_selected_idx == i);

            flags = (item_highlighted_idx == i) ? ImGuiSelectableFlags_Highlight : 0;
            if (ImGui::Selectable(line.c_str(), is_selected, flags))
                item_selected_idx = i;

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();

            i++;
        }
        ImGui::EndListBox();
    }

    static ImVec4 msg_color = ImVec4(255, 255, 255, 255);

    if (!updating_sats)
    {
        if (ImGui::Button("Add Selected##satellite"))
        {
            mesg = "";
            for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);               // get count
            Satellite *sat = new Satellite();
            if (!append_cel(sat))
            {
                delete sat;
                mesg = "ERROR - Object limit reached.";
                msg_color = ImVec4(255, 0, 0, 255);
            }
            else
            {
                n = item_selected_idx;

                char buffer[1024];
                if (n < listlines.size()) strcpy(buffer, listlines[n].c_str());
                char *hashmarks = strstr(buffer, "##");
                i = hashmarks ? atoi(&hashmarks[2]) : 0;

                if (SatSource::populate(sat, i))
                {
                    selected = ncelobjs-1;
                    compute_object_location(sat);
                    compute_object_draw_coordinates();
                    center_selected();
                    viewchanged = true;
                    satwnd = false;
                }
                else
                {
                    ncelobjs--;
                    cels[ncelobjs] = 0;
                    mesg = "ERROR - Satellite failed to load.";
                    msg_color = ImVec4(255, 0, 0, 255);
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Add and Leave Open##satellite"))
        {
            mesg = "";
            for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);               // get count
            Satellite *sat = new Satellite();
            // See the Add Selected button above -- same check, same reason.
            if (!append_cel(sat))
            {
                delete sat;
                mesg = "ERROR - Object limit reached.";
                msg_color = ImVec4(255, 0, 0, 255);
            }
            else
            {
                n = item_selected_idx;

                char buffer[1024];
                if (n < listlines.size()) strcpy(buffer, listlines[n].c_str());
                char *hashmarks = strstr(buffer, "##");
                i = hashmarks ? atoi(&hashmarks[2]) : 0;

                if (SatSource::populate(sat, i))
                {
                    selected = ncelobjs-1;
                    compute_object_location(sat);
                    compute_object_draw_coordinates();
                    center_selected();
                    viewchanged = true;
                }
                else
                {
                    ncelobjs--;
                    cels[ncelobjs] = 0;
                    mesg = "ERROR - Satellite failed to load.";
                    msg_color = ImVec4(255, 0, 0, 255);
                }
            }
        }

        static bool awaiting_batch = false;
        ImGui::SameLine();
        if (ImGui::Button("Add All Shown##satellites") && !batch_sats_running)
        {
            mesg = "Adding satellites...";
            msg_color = ImVec4(255, 224, 0, 255);
            batch_sats_running = true;
            awaiting_batch = true;

            std::thread tsat(add_batch_satellites, listlines);
            tsat.detach();
        }

        if (awaiting_batch && !batch_sats_running)
        {
            awaiting_batch = false;
            if (sats_added)
            {
                if (sat_errors)
                {
                    mesg = std::string("Added ") + std::to_string(sats_added)
                        + std::string("; ") + std::to_string(sat_errors) + std::string(" failed.");
                    msg_color = ImVec4(255, 224, 0, 255);
                }
                else
                {
                    mesg = std::string("Added ") + std::to_string(sats_added) + std::string(" satellites.");
                    msg_color = ImVec4(0, 255, 0, 255);
                }
            }
            else
            {
                if (sat_errors)
                {
                    mesg = std::string("ERROR");
                    msg_color = ImVec4(255, 0, 0, 255);
                }
                else
                {
                    mesg = "";
                }
            }
        }

        ImGui::SameLine();
        ImGui::TextColored(redlight_mode ? ImVec4(255, 24, 0, 255) : msg_color, "%s", mesg.c_str());
    }

    ImGui::SetWindowSize(ImVec2(0, 0));
    ImVec2 pos = ImGui::GetWindowPos(), siz = ImGui::GetWindowSize();
    ImGui::End();

    if (io.MousePos.x >= pos.x && io.MousePos.y >= pos.y && io.MousePos.x < (pos.x+siz.x) && io.MousePos.y < (pos.y+siz.y))
        is_mouse_over_window = true;
}

#if 0
// Use this template to add new windows to the application.

// Replace wndbool with a new boolean you create in misc.h and misc.cpp. It will control whether the window is displayed.

void draw_app_window_template(ImGuiIO& io)
{
    if (!cels[1]) return;
    ImGui::Begin("Window Name", &wnd, 0);                       // Replace wnd with a new dedicated bool.

    ImGui::Text("%s", "Text Field");                            // Example text label.
    ImGui::SameLine();
    ImGui::InputText("##textdata", text_data, 256, 0);          // Replace the ## string with a unique id and text_data with a char array.

    // Add the main body of the window here.

    ImGui::SetWindowSize(ImVec2(0, 0));                         // Auto size to fit contents.
    ImVec2 pos = ImGui::GetWindowPos(), siz = ImGui::GetWindowSize();
    ImGui::End();

    // Code to ensure mouse interacts with window and not viewport.
    if (io.MousePos.x >= pos.x && io.MousePos.y >= pos.y && io.MousePos.x < (pos.x+siz.x) && io.MousePos.y < (pos.y+siz.y))
        is_mouse_over_window = true;
}
#endif

void draw_comet_window(ImGuiIO & io)
{
    if (!cels[1]) return;
    static std::vector<std::string> cometlistlines;
    ImGui::Begin("Add Comets", &cometwnd, 0);

    static std::string mesg = "";
    ImGui::Text("%s", "Search:");
    ImGui::SameLine();
    if (ImGui::InputText("##lookcomet", lookcomet, name_max_len, 0)) cometlistlines.clear();

    int i, l, looklen = strlen(lookcomet);
    unsigned int n=0, ncomet = comets.size();
    static unsigned int item_selected_idx = 0;
    int item_highlighted_idx = -1;
    ImGui::Text(" Name                             Perihelion    Ecc.          Incl.");
    if (ImGui::BeginListBox("##cometlist", ImVec2(623, 13 * ImGui::GetTextLineHeightWithSpacing())))
    {
        i=0;
        if (cometlistlines.size())
        {
            ncomet = cometlistlines.size();
            for (n=0; n<ncomet; n++)
            {
                bool is_selected = (item_selected_idx == n);

                ImGuiSelectableFlags flags = ((int64_t)item_highlighted_idx == (int64_t)n) ? ImGuiSelectableFlags_Highlight : 0;
                if (ImGui::Selectable(cometlistlines[n].c_str(), is_selected, flags))
                    item_selected_idx = n;

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        else for (n=0; n<ncomet; n++)
        {
            stringstream line;
            line << comets[n].name;

            l = 34 - line.str().size();
            if (l > 0) line << std::string(l, ' ');

            if (looklen && !strcasestr(line.str().c_str(), lookcomet)) continue;

            line << comets[n].q;

            l = 48 - line.str().size();
            if (l > 0) line << std::string(l, ' ');

            line << comets[n].e;

            l = 62 - line.str().size();
            if (l > 0) line << std::string(l, ' ');

            line << comets[n].incl;
            line << " ##" << n;
            cometlistlines.push_back(line.str());

            bool is_selected = ((int64_t)item_selected_idx == (int64_t)i);

            ImGuiSelectableFlags flags = (item_highlighted_idx == i) ? ImGuiSelectableFlags_Highlight : 0;
            if (ImGui::Selectable(line.str().c_str(), is_selected, flags))
                item_selected_idx = i;

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();

            i++;
            if (i >= 25) break;
        }

        ImGui::EndListBox();
    }

    static ImVec4 msg_color = ImVec4(255, 255, 255, 255);

    if (item_selected_idx < cometlistlines.size())
    {
        n = item_selected_idx;
        const char *hashmarks = strstr(cometlistlines[n].c_str(), "##");
        i = atoi(&hashmarks[2]);
    }
    else
    {
        item_selected_idx = 0;
        i = -1;
    }

    if (i >= 0 && i < (int)comets.size())
    {
        if (comets[i].cel)
        {
            if (ImGui::Button("Find##comet"))
            {
                selected = comets[i].cel->seqno;
                center_selected();
            }
            ImGui::SameLine();
            if (ImGui::Button("Track##comet"))
            {
                trackidx = comets[i].cel->seqno;
                center_tracked();
            }
            ImGui::SameLine();
            if (ImGui::Button("Go##comet"))
            {
                whereami = comets[i].cel->seqno;
                viewchanged = true;
            }
        }
        else if (ImGui::Button("Add Selected##comet"))
        {
            mesg = "";
            for (ncelobjs=0; cels[ncelobjs]; ncelobjs++);               // get count

            if (!CatalogReader::load_comet(&comets[i]))
            {
                mesg = "ERROR - Comet failed to load.";
                msg_color = ImVec4(255, 0, 0, 255);
            }
            else
            {
                selected = ncelobjs-1;
                num_comets++;
                compute_object_location(comets[i].cel);
                compute_object_draw_coordinates();
                center_selected();
                viewchanged = true;
                cometwnd = false;
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextColored(redlight_mode ? ImVec4(255, 24, 0, 255) : msg_color, "%s", mesg.c_str());

    ImGui::SetWindowSize(ImVec2(0, 0));                         // Auto size to fit contents.
    ImVec2 cpos = ImGui::GetWindowPos(), csiz = ImGui::GetWindowSize();
    ImGui::End();

    // Code to ensure mouse interacts with window and not viewport.
    if (io.MousePos.x >= cpos.x && io.MousePos.y >= cpos.y && io.MousePos.x < (cpos.x+csiz.x) && io.MousePos.y < (cpos.y+csiz.y))
        is_mouse_over_window = true;
}
